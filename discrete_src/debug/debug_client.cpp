/*******************************************************************************
Copyright (c) 2026 Curt Hartung -- curt.hartung@gmail.com

MIT Licence

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*******************************************************************************/

#include "wrench.h"

#ifdef WRENCH_INCLUDE_DEBUG_CLIENT

#include "debug/debug_client.h"

#include <algorithm>
#include <chrono>
#include <thread>

int wr_operandSizeForOpcode( const uint8_t* opPtr, const uint8_t* end, const uint8_t op );

static uint32_t wr_rd32( const uint8_t* p )
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t wr_rd16( const uint8_t* p )
{
	return (uint16_t)(p[0] | (p[1] << 8));
}

static void wr_put16( std::vector<uint8_t>& out, uint16_t v )
{
	out.push_back( (uint8_t)v );
	out.push_back( (uint8_t)(v >> 8) );
}

static void wr_put32( std::vector<uint8_t>& out, uint32_t v )
{
	out.push_back( (uint8_t)v );
	out.push_back( (uint8_t)(v >> 8) );
	out.push_back( (uint8_t)(v >> 16) );
	out.push_back( (uint8_t)(v >> 24) );
}

void WrenchDebugProgram::clear()
{
	m_bytecode.clear();
	m_hash = 0;
	m_flags = 0;
	m_sourceHash = 0;
	m_hasSource = false;
	m_source.clear();
	m_codeStart = 0;
	m_codeEnd = 0;
	m_functions.clear();
	m_lineTable.clear();
	m_lines.clear();
	m_members.clear();
}

std::string WrenchDebugProgram::displayName( const std::string& label )
{
	if ( label.size() > 2 && label[0] == ':' && label[1] == ':' )
	{
		return label.substr( 2 );
	}
	return label;
}

bool WrenchDebugProgram::parse( const uint8_t* b, uint32_t len, std::string* error )
{
	clear();

	if ( !b || !wr_isBytecodeValid(b, len) )
	{
		if ( error )
		{
			*error = "bytecode is damaged or was built for another wrench version";
		}
		return false;
	}

	uint32_t globals = b[0];
	uint32_t functionCount = b[1];
	m_flags = b[2];
	m_hash = wr_rd32( b + len - 4 );

	uint32_t pos = 3 + functionCount * WR_FUNCTION_CORE_SIZE;
	if ( pos + 4 > len )
	{
		if ( error )
		{
			*error = "truncated header";
		}
		return false;
	}

	std::vector<uint32_t> offsets;
	for ( uint32_t f = 0; f < functionCount; ++f )
	{
		offsets.push_back( wr_rd16(b + 3 + f * WR_FUNCTION_CORE_SIZE + 2) );
	}

	std::vector<uint32_t> globalHashes;
	if ( m_flags & WR_INCLUDE_GLOBALS )
	{
		for ( uint32_t g = 0; g < globals && pos + 4 <= len; ++g, pos += 4 )
		{
			globalHashes.push_back( wr_rd32(b + pos) );
		}
	}

	const uint8_t* symbols = 0;
	uint32_t symbolsLen = 0;
	if ( (m_flags & (WR_EMBED_DEBUG_CODE | WR_EMBED_SOURCE_CODE)) && pos + 6 <= len )
	{
		m_sourceHash = wr_rd32( b + pos );
		symbolsLen = wr_rd16( b + pos + 4 );
		if ( pos + 6 + symbolsLen <= len )
		{
			symbols = b + pos + 6;
		}
		pos += 6 + symbolsLen;
	}

	if ( (m_flags & WR_EMBED_SOURCE_CODE) && pos + 4 <= len )
	{
		uint32_t sourceLen = wr_rd32( b + pos );
		if ( sourceLen <= len - pos - 4 )
		{
			m_source.assign( (const char*)b + pos + 4, sourceLen );
			m_hasSource = true;
			pos += 4 + sourceLen;
		}
	}

	m_codeStart = pos;
	m_codeEnd = len - 4;
	if ( m_codeStart > m_codeEnd )
	{
		if ( error )
		{
			*error = "truncated header";
		}
		return false;
	}

	// function bodies must lie inside the code, the line table walks them
	for ( uint32_t f = 0; f < functionCount; ++f )
	{
		if ( offsets[f] < m_codeStart || offsets[f] > m_codeEnd )
		{
			if ( error )
			{
				*error = "function offset outside the code";
			}
			return false;
		}
	}

	m_bytecode.assign( b, b + len );

	// units: 0 is global code, n is function n - 1
	m_functions.resize( functionCount + 1 );
	m_functions[0].name = "::global";
	for ( uint32_t u = 1; u <= functionCount; ++u )
	{
		m_functions[u].name = "function" + std::to_string( u - 1 );
	}

	if ( symbols )
	{
		uint32_t p = 0;
		uint32_t units = (symbolsLen >= 2) ? wr_rd16( symbols ) : 0;
		p = 2;
		for ( uint32_t u = 0; u < units && p + 2 <= symbolsLen; ++u )
		{
			uint32_t count = symbols[p];
			uint32_t args = symbols[p + 1];
			p += 2;

			auto readLabel = [&]() -> std::string
			{
				std::string s;
				while ( p < symbolsLen && symbols[p] )
				{
					s += (char)symbols[p++];
				}
				++p;
				return s;
			};

			std::string name = readLabel();
			std::vector<std::string> locals;
			for ( uint32_t v = 0; v < count && p <= symbolsLen; ++v )
			{
				locals.push_back( readLabel() );
			}

			if ( u < m_functions.size() )
			{
				m_functions[u].name = name;
				m_functions[u].arguments = (int)args;
				m_functions[u].locals = locals;
			}

			// struct members are hashed by their bare name
			for ( size_t v = 0; v < locals.size(); ++v )
			{
				std::string bare = displayName( locals[v] );
				m_members[wr_hashStr(bare.c_str())] = bare;
			}
		}
	}
	else if ( !globalHashes.empty() )
	{
		for ( uint32_t g = 0; g < globals; ++g )
		{
			char text[24];
			snprintf( text, sizeof(text), "::g%u", g );
			m_functions[0].locals.push_back( text );
		}
	}

	// code ranges, global code first then each function up to the next one
	std::vector<uint32_t> starts = offsets;
	std::sort( starts.begin(), starts.end() );
	m_functions[0].codeStart = m_codeStart;
	m_functions[0].codeEnd = starts.empty() ? m_codeEnd : starts[0];
	for ( uint32_t u = 1; u <= functionCount; ++u )
	{
		uint32_t start = offsets[u - 1];
		uint32_t end = m_codeEnd;
		for ( size_t s = 0; s < starts.size(); ++s )
		{
			if ( starts[s] > start )
			{
				end = starts[s];
				break;
			}
		}
		m_functions[u].codeStart = start;
		m_functions[u].codeEnd = end;
	}

	buildLineTable();
	return true;
}

void WrenchDebugProgram::buildLineTable()
{
	const uint8_t* base = m_bytecode.data();
	for ( size_t u = 0; u < m_functions.size(); ++u )
	{
		const uint8_t* pc = base + m_functions[u].codeStart;
		const uint8_t* end = base + m_functions[u].codeEnd;
		int lineHigh = 0;
		while ( pc < end )
		{
			uint8_t op = *pc;
			if ( op >= O_LAST || op >= O_HASH_PLACEHOLDER )
			{
				break; // data (struct tables) follows the code
			}
			int operands = wr_operandSizeForOpcode( pc + 1, end, op );
			if ( operands < 0 || pc + 1 + operands > end )
			{
				break;
			}

			if ( op == O_DebugInfo && operands == 2 )
			{
				uint16_t codeword = wr_rd16( pc + 1 );
				if ( (codeword & WRD_TypeMask) == WRD_LineHigh )
				{
					lineHigh = (int)(codeword & WRD_PayloadMask) << 14;
				}
				else if ( (codeword & WRD_TypeMask) == WRD_LineNumber )
				{
					LineEntry e;
					e.pcOffset = (uint32_t)(pc + 1 - base);
					e.line = (codeword & WRD_PayloadMask) | lineHigh;
					lineHigh = 0;
					m_lineTable.push_back( e );
					m_lines.insert( e.line );
				}
			}

			if ( op == O_Return || op == O_ReturnZero || op == O_Stop || op == O_GlobalStop )
			{
				// anything after the last return of a function may be data
				if ( u != 0 && pc + 1 + operands >= end )
				{
					break;
				}
			}

			pc += 1 + operands;
		}
	}

	std::sort( m_lineTable.begin(), m_lineTable.end(),
						[]( const LineEntry& a, const LineEntry& b ) { return a.pcOffset < b.pcOffset; } );
}

int WrenchDebugProgram::functionForPc( uint32_t pcOffset ) const
{
	for ( size_t u = 1; u < m_functions.size(); ++u )
	{
		if ( pcOffset >= m_functions[u].codeStart && pcOffset < m_functions[u].codeEnd )
		{
			return (int)u - 1;
		}
	}
	return -1;
}

int WrenchDebugProgram::lineForPc( uint32_t pcOffset ) const
{
	int unit = functionForPc( pcOffset ) + 1;
	uint32_t start = m_functions.empty() ? 0 : m_functions[unit].codeStart;
	int line = 0;
	for ( size_t i = 0; i < m_lineTable.size(); ++i )
	{
		if ( m_lineTable[i].pcOffset > pcOffset )
		{
			break;
		}
		if ( m_lineTable[i].pcOffset >= start )
		{
			line = m_lineTable[i].line;
		}
	}
	return line;
}

int WrenchDebugProgram::breakableLine( int line ) const
{
	auto it = m_lines.lower_bound( line );
	return it == m_lines.end() ? -1 : *it;
}

std::string WrenchDebugProgram::functionName( int vmFunction ) const
{
	size_t unit = (size_t)(vmFunction + 1);
	if ( unit < m_functions.size() )
	{
		return unit == 0 ? std::string( "<global>" ) : displayName( m_functions[unit].name );
	}
	return "<unknown>";
}

std::string WrenchDebugProgram::globalName( int slot ) const
{
	if ( !m_functions.empty() && slot >= 0 && (size_t)slot < m_functions[0].locals.size() )
	{
		return displayName( m_functions[0].locals[slot] );
	}
	return "global" + std::to_string( slot );
}

std::string WrenchDebugProgram::localName( int vmFunction, int slot ) const
{
	size_t unit = (size_t)(vmFunction + 1);
	if ( unit < m_functions.size() && slot >= 0 && (size_t)slot < m_functions[unit].locals.size() )
	{
		return displayName( m_functions[unit].locals[slot] );
	}
	return "local" + std::to_string( slot );
}

int WrenchDebugProgram::globalSlot( const std::string& name ) const
{
	if ( m_functions.empty() )
	{
		return -1;
	}
	for ( size_t i = 0; i < m_functions[0].locals.size(); ++i )
	{
		if ( displayName(m_functions[0].locals[i]) == displayName(name) )
		{
			return (int)i;
		}
	}
	return -1;
}

int WrenchDebugProgram::localSlot( int vmFunction, const std::string& name ) const
{
	size_t unit = (size_t)(vmFunction + 1);
	if ( unit == 0 || unit >= m_functions.size() )
	{
		return -1;
	}
	for ( size_t i = 0; i < m_functions[unit].locals.size(); ++i )
	{
		if ( displayName(m_functions[unit].locals[i]) == name )
		{
			return (int)i;
		}
	}
	return -1;
}

std::string WrenchDebugProgram::memberName( uint32_t hash ) const
{
	auto it = m_members.find( hash );
	return it == m_members.end() ? std::string() : it->second;
}

bool WrenchDebugValue::expandable() const
{
	return (tag == WRD_ValArray || tag == WRD_ValHash || tag == WRD_ValStruct || tag == WRD_ValRaw)
				&& handle != 0 && count > 0;
}

static std::string wr_quote( const std::string& s )
{
	std::string out = "\"";
	for ( unsigned char c : s )
	{
		switch ( c )
		{
			case '"': out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\n': out += "\\n"; break;
			case '\r': out += "\\r"; break;
			case '\t': out += "\\t"; break;
			default:
			{
				if ( c < 0x20 || c >= 0x7F )
				{
					char hex[8];
					snprintf( hex, sizeof(hex), "\\x%02X", c );
					out += hex;
				}
				else
				{
					out += (char)c;
				}
			}
		}
	}
	out += "\"";
	return out;
}

std::string WrenchDebugValue::display( bool hex ) const
{
	char buf[64];
	switch ( tag )
	{
		case WRD_ValInt:
		{
			if ( hex )
			{
				snprintf( buf, sizeof(buf), "0x%08X", (unsigned int)i );
			}
			else
			{
				snprintf( buf, sizeof(buf), "%d", (int)i );
			}
			return buf;
		}
		case WRD_ValFloat:
		{
			snprintf( buf, sizeof(buf), "%g", (double)f );
			std::string s = buf;
			if ( s.find_first_of(".eEn") == std::string::npos )
			{
				s += ".0"; // keep floats recognisable
			}
			return s;
		}
		case WRD_ValString:
		{
			std::string s = wr_quote( text );
			if ( count > text.size() )
			{
				s += "...";
			}
			return s;
		}
		case WRD_ValArray: snprintf( buf, sizeof(buf), "array[%u]", (unsigned)count ); return buf;
		case WRD_ValHash: snprintf( buf, sizeof(buf), "hash{%u}", (unsigned)count ); return buf;
		case WRD_ValStruct: snprintf( buf, sizeof(buf), "struct{%u}", (unsigned)count ); return buf;
		case WRD_ValRaw: snprintf( buf, sizeof(buf), "raw[%u]", (unsigned)count ); return buf;
		default: snprintf( buf, sizeof(buf), "<type 0x%02X>", (unsigned)other ); return buf;
	}
}

std::string WrenchDebugValue::typeName() const
{
	switch ( tag )
	{
		case WRD_ValInt: return "int";
		case WRD_ValFloat: return "float";
		case WRD_ValString: return "string";
		case WRD_ValArray: return "array";
		case WRD_ValHash: return "hash";
		case WRD_ValStruct: return "struct";
		case WRD_ValRaw: return "raw";
		default: return "other";
	}
}

WrenchDebugValue WrenchDebugValue::makeInt( int32_t v )
{
	WrenchDebugValue r;
	r.tag = WRD_ValInt;
	r.i = v;
	return r;
}

WrenchDebugValue WrenchDebugValue::makeFloat( float v )
{
	WrenchDebugValue r;
	r.tag = WRD_ValFloat;
	r.f = v;
	return r;
}

WrenchDebugValue WrenchDebugValue::makeString( const std::string& v )
{
	WrenchDebugValue r;
	r.tag = WRD_ValString;
	r.text = v;
	r.count = (uint32_t)v.size();
	return r;
}

const char* WrenchDebugClient::errorName( uint8_t code )
{
	switch ( code )
	{
		case WRD_ErrNone: return "no error";
		case WRD_ErrUnknownRequest: return "target does not know this request";
		case WRD_ErrBadRequest: return "malformed request";
		case WRD_ErrNoProgram: return "no program loaded on the target";
		case WRD_ErrNotStopped: return "target is not stopped";
		case WRD_ErrBadFrame: return "no such stack frame";
		case WRD_ErrBadSlot: return "no such variable";
		case WRD_ErrBadHandle: return "value is no longer valid";
		case WRD_ErrNoMemory: return "target is out of memory";
		case WRD_ErrBadBytecode: return "target rejected the bytecode";
		case WRD_ErrTooManyBreakpoints: return "too many breakpoints for the target";
		case WRD_ErrBusy: return "target is busy executing";
		case WRD_ErrVersion: return "debug protocol version mismatch";
		case WRD_ErrUnsupported: return "not supported by the target";
		case WRD_ErrUploadDisabled: return "the target does not accept uploaded programs";
		default: return "unknown error";
	}
}

WrenchDebugClient::WrenchDebugClient( WrenchDebugTransport* transport )
	: m_transport( transport )
{
	// big enough for anything a target may send
	m_rx.resize( 65535 + WRD_FRAME_OVERHEAD );
	m_decoder.init( m_rx.data(), (uint32_t)m_rx.size() );
}

void WrenchDebugClient::handleFrame( const WrenchDebugFrame& frame )
{
	if ( frame.type == WRD_MsgReplyOk || frame.type == WRD_MsgReplyError )
	{
		if ( frame.seq == m_waitSeq && !m_haveReply )
		{
			m_haveReply = true;
			m_replyType = frame.type;
			m_reply.assign( frame.payload, frame.payload + frame.length );
		}
		return; // late reply to a retried request
	}

	WrenchDebugReader in( frame.payload, frame.length );
	WrenchDebugEvent e;
	switch ( frame.type )
	{
		case WRD_MsgEventStopped:
		{
			e.type = WrenchDebugEvent::Stopped;
			e.reason = in.u8();
			e.line = in.i32();
			e.error = in.u8();
			e.missingFunction = in.u32();
			break;
		}
		case WRD_MsgEventOutput:
		{
			e.type = WrenchDebugEvent::Output;
			e.text.assign( (const char*)frame.payload, frame.length );
			break;
		}
		case WRD_MsgEventEnded:
		{
			e.type = WrenchDebugEvent::Ended;
			e.error = in.u8();
			e.flags = in.u8();
			e.line = in.i32();
			e.missingFunction = in.u32();
			if ( in.remaining() )
			{
				readValue( in, e.value );
			}
			break;
		}
		case WRD_MsgEventLoaded:
		{
			e.type = WrenchDebugEvent::Loaded;
			break;
		}
		default:
		{
			return;
		}
	}
	m_events.push_back( e );
}

void WrenchDebugClient::poll( int timeoutMs, bool replyOnly )
{
	uint8_t chunk[4096];
	uint32_t start = m_transport->millis();
	for ( ;; )
	{
		int got = m_transport->read( chunk, sizeof(chunk) );
		if ( got < 0 )
		{
			if ( m_linkUp )
			{
				m_linkUp = false;
				WrenchDebugEvent e;
				e.type = WrenchDebugEvent::Disconnected;
				m_events.push_back( e );
			}
			return;
		}

		if ( got > 0 )
		{
			uint32_t now = m_transport->millis();
			for ( int i = 0; i < got; ++i )
			{
				if ( m_decoder.feed(chunk[i], now) )
				{
					handleFrame( m_decoder.frame() );
				}
			}
			// keep draining while data flows, but come back for the caller
			if ( m_haveReply || (!replyOnly && !m_events.empty()) )
			{
				return;
			}
			continue;
		}

		// waiting for a reply, queued events must not cut the wait short (the
		// caller would spin without ever idling)
		if ( m_haveReply || (!replyOnly && !m_events.empty()) || (int)(m_transport->millis() - start) >= timeoutMs )
		{
			return;
		}
		m_transport->idle();
	}
}

bool WrenchDebugClient::request( uint8_t type, const std::vector<uint8_t>& payload, std::vector<uint8_t>* reply )
{
	if ( !m_linkUp )
	{
		m_error = "link to the target is down";
		return false;
	}

	if ( payload.size() > 0xFFFF )
	{
		m_error = "request too large";
		return false;
	}

	if ( ++m_seq == 0 )
	{
		m_seq = 1;
	}

	std::vector<uint8_t> frame( payload.size() + WRD_FRAME_OVERHEAD );
	if ( !payload.empty() )
	{
		memcpy( frame.data() + WRD_FRAME_HEADER, payload.data(), payload.size() );
	}
	uint32_t total = wr_debugFinishFrame( frame.data(), type, m_seq, (uint16_t)payload.size() );

	m_waitSeq = m_seq;
	for ( int attempt = 0; attempt <= m_retries; ++attempt )
	{
		m_haveReply = false;
		if ( !m_transport->write(frame.data(), total) )
		{
			m_linkUp = false;
			m_error = "write to the target failed";
			return false;
		}

		uint32_t start = m_transport->millis();
		while ( !m_haveReply && m_linkUp && (int)(m_transport->millis() - start) < m_timeoutMs )
		{
			poll( 20, true );
		}

		if ( m_haveReply )
		{
			m_waitSeq = 0;
			if ( m_replyType == WRD_MsgReplyError )
			{
				uint8_t code = m_reply.empty() ? 0 : m_reply[0];
				m_error = errorName( code );
				return false;
			}
			if ( reply )
			{
				reply->swap( m_reply );
			}
			return true;
		}

		if ( !m_linkUp )
		{
			break;
		}
	}

	m_waitSeq = 0;
	m_error = m_linkUp ? "target did not answer" : "link to the target is down";
	return false;
}

bool WrenchDebugClient::control( uint8_t type )
{
	return request( type, std::vector<uint8_t>(), 0 );
}

bool WrenchDebugClient::connect( WrenchDebugTargetInfo& info )
{
	m_linkUp = true;
	std::vector<uint8_t> payload;
	wr_put16( payload, WRD_PROTOCOL_VERSION );
	std::vector<uint8_t> reply;

	// targets that just booted may still be printing, give them a moment
	int savedRetries = m_retries;
	m_retries = savedRetries < 3 ? 3 : savedRetries;
	bool ok = request( WRD_MsgHello, payload, &reply );
	m_retries = savedRetries;
	if ( !ok )
	{
		return false;
	}

	WrenchDebugReader in( reply.data(), (uint32_t)reply.size() );
	info.protocol = in.u16();
	uint8_t major = in.u8();
	uint8_t minor = in.u8();
	uint8_t build = in.u8();
	info.maxPayload = in.u16();
	info.caps = in.u8();
	info.maxBreakpoints = in.u16();
	if ( in.bad() )
	{
		m_error = "malformed hello reply";
		return false;
	}
	info.wrenchVersion = std::to_string( major ) + "." + std::to_string( minor ) + "." + std::to_string( build );
	m_maxPayload = info.maxPayload < 64 ? 64 : info.maxPayload;

	return refreshInfo( info );
}

bool WrenchDebugClient::refreshInfo( WrenchDebugTargetInfo& info )
{
	std::vector<uint8_t> reply;
	if ( !request(WRD_MsgGetInfo, std::vector<uint8_t>(), &reply) )
	{
		return false;
	}

	WrenchDebugReader in( reply.data(), (uint32_t)reply.size() );
	info.state = in.u8();
	info.programHash = in.u32();
	info.bytecodeLen = in.u32();
	info.compilerFlags = in.u8();
	info.symbolsLen = in.u32();
	info.sourceLen = in.u32();
	info.globals = in.u16();
	info.functions = in.u8();
	info.lastLine = in.i32();
	if ( in.bad() )
	{
		m_error = "malformed info reply";
		return false;
	}
	return true;
}

bool WrenchDebugClient::upload( const std::vector<uint8_t>& bytecode, std::function<void(uint32_t, uint32_t)> progress )
{
	std::vector<uint8_t> payload;
	wr_put32( payload, (uint32_t)bytecode.size() );
	if ( !request(WRD_MsgLoadBegin, payload, 0) )
	{
		return false;
	}

	uint32_t chunk = m_maxPayload - 4;
	for ( uint32_t offset = 0; offset < bytecode.size(); offset += chunk )
	{
		uint32_t n = (uint32_t)bytecode.size() - offset;
		if ( n > chunk )
		{
			n = chunk;
		}
		payload.clear();
		wr_put32( payload, offset );
		payload.insert( payload.end(), bytecode.begin() + offset, bytecode.begin() + offset + n );
		if ( !request(WRD_MsgLoadData, payload, 0) )
		{
			return false;
		}
		if ( progress )
		{
			progress( offset + n, (uint32_t)bytecode.size() );
		}
	}

	std::vector<uint8_t> reply;
	return request( WRD_MsgLoadEnd, std::vector<uint8_t>(), &reply );
}

bool WrenchDebugClient::readSection( uint8_t section, std::vector<uint8_t>& out )
{
	out.clear();
	uint32_t total = 0;
	do
	{
		std::vector<uint8_t> payload;
		payload.push_back( section );
		wr_put32( payload, (uint32_t)out.size() );
		wr_put16( payload, (uint16_t)(m_maxPayload - 6) );
		std::vector<uint8_t> reply;
		if ( !request(WRD_MsgReadProgram, payload, &reply) )
		{
			return false;
		}

		WrenchDebugReader in( reply.data(), (uint32_t)reply.size() );
		total = in.u32();
		uint16_t n = in.u16();
		const uint8_t* bytes = in.bytes( n );
		if ( in.bad() || (n == 0 && out.size() < total) )
		{
			m_error = "malformed program read reply";
			return false;
		}
		out.insert( out.end(), bytes, bytes + n );
	} while ( out.size() < total );

	return true;
}

bool WrenchDebugClient::launch( bool stopOnEntry )
{
	std::vector<uint8_t> payload;
	payload.push_back( stopOnEntry ? WRD_LaunchStopOnEntry : 0 );
	return request( WRD_MsgLaunch, payload, 0 );
}

bool WrenchDebugClient::resume( bool resumeStep )
{
	if ( !resumeStep )
	{
		return control( WRD_MsgContinue );
	}
	std::vector<uint8_t> payload;
	payload.push_back( WRD_ContinueResumeStep );
	return request( WRD_MsgContinue, payload, 0 );
}
bool WrenchDebugClient::pause() { return control( WRD_MsgPause ); }
bool WrenchDebugClient::stepIn() { return control( WRD_MsgStepIn ); }
bool WrenchDebugClient::stepOver() { return control( WRD_MsgStepOver ); }
bool WrenchDebugClient::stepOut() { return control( WRD_MsgStepOut ); }
bool WrenchDebugClient::terminate() { return control( WRD_MsgTerminate ); }
bool WrenchDebugClient::detach() { return control( WRD_MsgDetach ); }
bool WrenchDebugClient::ping() { return control( WRD_MsgPing ); }

bool WrenchDebugClient::setBreakpoints( const std::vector<int>& lines )
{
	std::vector<uint8_t> payload;
	wr_put16( payload, (uint16_t)lines.size() );
	for ( int line : lines )
	{
		wr_put32( payload, (uint32_t)line );
	}
	return request( WRD_MsgSetBreakpoints, payload, 0 );
}

bool WrenchDebugClient::getStack( std::vector<WrenchDebugStackFrame>& frames, const WrenchDebugProgram* program, int topLine )
{
	frames.clear();
	uint16_t first = 0;
	for ( ;; )
	{
		std::vector<uint8_t> payload;
		wr_put16( payload, first );
		wr_put16( payload, 64 );
		std::vector<uint8_t> reply;
		if ( !request(WRD_MsgGetStack, payload, &reply) )
		{
			return false;
		}

		WrenchDebugReader in( reply.data(), (uint32_t)reply.size() );
		uint16_t total = in.u16();
		uint16_t count = in.u16();
		for ( uint16_t i = 0; i < count; ++i )
		{
			WrenchDebugStackFrame f;
			f.kind = in.u8();
			uint8_t function = in.u8();
			f.pcOffset = in.u32();
			in.u16(); // frame base, informational
			f.slots = in.u16();
			f.function = (f.kind == WRD_FrameFunction) ? (int)function : -1;
			if ( program )
			{
				f.name = program->functionName( f.function );
				f.line = program->lineForPc( f.pcOffset );
			}
			else
			{
				f.name = f.function < 0 ? "<global>" : "function" + std::to_string( f.function );
			}
			frames.push_back( f );
		}
		if ( in.bad() )
		{
			m_error = "malformed stack reply";
			return false;
		}

		first = (uint16_t)(first + count);
		if ( count == 0 || first >= total )
		{
			break;
		}
	}

	// the target knows exactly which line it stopped on
	if ( !frames.empty() && topLine > 0 )
	{
		frames[0].line = topLine;
	}
	return true;
}

bool WrenchDebugClient::readValue( WrenchDebugReader& in, WrenchDebugValue& v )
{
	v = WrenchDebugValue();
	v.tag = in.u8();
	switch ( v.tag )
	{
		case WRD_ValInt: v.i = in.i32(); break;
		case WRD_ValFloat: v.f = in.f32(); break;
		case WRD_ValString:
		{
			v.handle = in.u32();
			v.count = in.u32();
			uint16_t n = in.u16();
			const uint8_t* bytes = in.bytes( n );
			if ( bytes )
			{
				v.text.assign( (const char*)bytes, n );
			}
			break;
		}
		case WRD_ValArray:
		case WRD_ValHash:
		case WRD_ValStruct:
		case WRD_ValRaw:
		{
			v.handle = in.u32();
			v.count = in.u32();
			break;
		}
		default:
		{
			v.other = in.u8();
			break;
		}
	}
	return !in.bad();
}

void WrenchDebugClient::writeValue( std::vector<uint8_t>& out, const WrenchDebugValue& v )
{
	if ( v.tag == WRD_ValFloat )
	{
		out.push_back( WRD_ValFloat );
		uint32_t bits;
		memcpy( &bits, &v.f, 4 );
		wr_put32( out, bits );
	}
	else if ( v.tag == WRD_ValString )
	{
		out.push_back( WRD_ValString );
		uint16_t n = (uint16_t)(std::min<size_t>)( v.text.size(), 0xFFFF );
		wr_put16( out, n );
		out.insert( out.end(), v.text.begin(), v.text.begin() + n );
	}
	else
	{
		out.push_back( WRD_ValInt );
		wr_put32( out, (uint32_t)v.i );
	}
}

bool WrenchDebugClient::getLocals( int frame, std::vector<WrenchDebugValue>& values )
{
	values.clear();
	uint16_t first = 0;
	for ( ;; )
	{
		std::vector<uint8_t> payload;
		wr_put16( payload, (uint16_t)frame );
		wr_put16( payload, first );
		wr_put16( payload, 0xFFFF );
		std::vector<uint8_t> reply;
		if ( !request(WRD_MsgGetLocals, payload, &reply) )
		{
			return false;
		}

		WrenchDebugReader in( reply.data(), (uint32_t)reply.size() );
		uint16_t total = in.u16();
		uint16_t count = in.u16();
		for ( uint16_t i = 0; i < count; ++i )
		{
			WrenchDebugValue v;
			readValue( in, v );
			values.push_back( v );
		}
		if ( in.bad() )
		{
			m_error = "malformed locals reply";
			return false;
		}
		first = (uint16_t)(first + count);
		if ( count == 0 || first >= total )
		{
			return true;
		}
	}
}

bool WrenchDebugClient::getGlobals( std::vector<WrenchDebugValue>& values )
{
	values.clear();
	uint16_t first = 0;
	for ( ;; )
	{
		std::vector<uint8_t> payload;
		wr_put16( payload, first );
		wr_put16( payload, 0xFFFF );
		std::vector<uint8_t> reply;
		if ( !request(WRD_MsgGetGlobals, payload, &reply) )
		{
			return false;
		}

		WrenchDebugReader in( reply.data(), (uint32_t)reply.size() );
		uint16_t total = in.u16();
		uint16_t count = in.u16();
		for ( uint16_t i = 0; i < count; ++i )
		{
			WrenchDebugValue v;
			readValue( in, v );
			values.push_back( v );
		}
		if ( in.bad() )
		{
			m_error = "malformed globals reply";
			return false;
		}
		first = (uint16_t)(first + count);
		if ( count == 0 || first >= total )
		{
			return true;
		}
	}
}

bool WrenchDebugClient::getChildren( const WrenchDebugValue& parent, uint32_t first, uint32_t count,
																		std::vector<WrenchDebugChild>& children, uint32_t& total,
																		const WrenchDebugProgram* program )
{
	children.clear();
	total = 0;
	if ( !parent.handle )
	{
		m_error = "value has no children";
		return false;
	}

	uint32_t position = first;
	uint32_t end = first + count;
	while ( position < end )
	{
		std::vector<uint8_t> payload;
		wr_put32( payload, parent.handle );
		wr_put32( payload, position );
		uint32_t want = end - position;
		wr_put16( payload, (uint16_t)(want > 0xFFFF ? 0xFFFF : want) );
		std::vector<uint8_t> reply;
		if ( !request(WRD_MsgGetChildren, payload, &reply) )
		{
			return false;
		}

		WrenchDebugReader in( reply.data(), (uint32_t)reply.size() );
		total = in.u32();
		uint16_t n = in.u16();

		if ( parent.tag == WRD_ValRaw || parent.tag == WRD_ValString )
		{
			const uint8_t* bytes = in.bytes( n );
			for ( uint16_t i = 0; bytes && i < n; ++i )
			{
				WrenchDebugChild c;
				c.index = position + i;
				c.name = "[" + std::to_string( c.index ) + "]";
				c.value = WrenchDebugValue::makeInt( bytes[i] );
				children.push_back( c );
			}
		}
		else
		{
			for ( uint16_t i = 0; i < n; ++i )
			{
				WrenchDebugChild c;
				c.index = position + i;
				if ( parent.tag == WRD_ValStruct )
				{
					uint32_t hash = in.u32();
					readValue( in, c.value );
					c.name = program ? program->memberName( hash ) : std::string();
					if ( c.name.empty() )
					{
						char text[24];
						snprintf( text, sizeof(text), "#%08X", (unsigned)hash );
						c.name = text;
					}
				}
				else if ( parent.tag == WRD_ValHash )
				{
					readValue( in, c.key );
					readValue( in, c.value );
					c.name = "[" + c.key.display() + "]";
				}
				else
				{
					readValue( in, c.value );
					c.name = "[" + std::to_string( c.index ) + "]";
				}
				children.push_back( c );
			}
		}

		if ( in.bad() )
		{
			m_error = "malformed children reply";
			return false;
		}

		position += n;
		if ( n == 0 || position >= total )
		{
			break;
		}
	}
	return true;
}

bool WrenchDebugClient::getString( const WrenchDebugValue& value, std::string& out, uint32_t maxLen )
{
	out = value.text;
	if ( value.tag != WRD_ValString || value.count <= value.text.size() )
	{
		return true;
	}

	while ( out.size() < value.count && out.size() < maxLen )
	{
		std::vector<uint8_t> payload;
		wr_put32( payload, value.handle );
		wr_put32( payload, (uint32_t)out.size() );
		wr_put16( payload, 0xFFFF );
		std::vector<uint8_t> reply;
		if ( !request(WRD_MsgGetChildren, payload, &reply) )
		{
			return false;
		}
		WrenchDebugReader in( reply.data(), (uint32_t)reply.size() );
		in.u32();
		uint16_t n = in.u16();
		const uint8_t* bytes = in.bytes( n );
		if ( in.bad() || n == 0 )
		{
			break;
		}
		out.append( (const char*)bytes, n );
	}
	return true;
}

bool WrenchDebugClient::setLocal( int frame, int slot, const WrenchDebugValue& value, WrenchDebugValue& result )
{
	std::vector<uint8_t> payload;
	wr_put16( payload, (uint16_t)frame );
	wr_put16( payload, (uint16_t)slot );
	writeValue( payload, value );
	std::vector<uint8_t> reply;
	if ( !request(WRD_MsgSetLocal, payload, &reply) )
	{
		return false;
	}
	WrenchDebugReader in( reply.data(), (uint32_t)reply.size() );
	return readValue( in, result );
}

bool WrenchDebugClient::setGlobal( int slot, const WrenchDebugValue& value, WrenchDebugValue& result )
{
	std::vector<uint8_t> payload;
	wr_put16( payload, (uint16_t)slot );
	writeValue( payload, value );
	std::vector<uint8_t> reply;
	if ( !request(WRD_MsgSetGlobal, payload, &reply) )
	{
		return false;
	}
	WrenchDebugReader in( reply.data(), (uint32_t)reply.size() );
	return readValue( in, result );
}

bool WrenchDebugClient::setChild( const WrenchDebugValue& parent, uint32_t index, const WrenchDebugValue& value, WrenchDebugValue& result )
{
	std::vector<uint8_t> payload;
	wr_put32( payload, parent.handle );
	wr_put32( payload, index );
	writeValue( payload, value );
	std::vector<uint8_t> reply;
	if ( !request(WRD_MsgSetChild, payload, &reply) )
	{
		return false;
	}
	WrenchDebugReader in( reply.data(), (uint32_t)reply.size() );
	return readValue( in, result );
}

bool WrenchDebugClient::waitEvent( WrenchDebugEvent& event, int timeoutMs )
{
	if ( m_events.empty() )
	{
		poll( timeoutMs );
	}
	if ( m_events.empty() )
	{
		return false;
	}
	event = m_events.front();
	m_events.pop_front();
	return true;
}

WrenchDebugPipe::WrenchDebugPipe()
{
	m_ends[0].pipe = this;
	m_ends[0].side = 0;
	m_ends[1].pipe = this;
	m_ends[1].side = 1;
}

void WrenchDebugPipe::close()
{
	std::lock_guard<std::mutex> guard( m_lock );
	m_closed = true;
	m_signal.notify_all();
}

bool WrenchDebugPipe::End::write( const uint8_t* data, uint32_t len )
{
	std::lock_guard<std::mutex> guard( pipe->m_lock );
	if ( pipe->m_closed )
	{
		return false;
	}
	// side 0 is the client, it writes toward the server queue
	std::deque<uint8_t>& q = pipe->m_queues[side == 0 ? 1 : 0];
	for ( uint32_t i = 0; i < len; ++i )
	{
		uint8_t b = data[i];
		if ( side == 1 && pipe->m_corruptEvery > 0 && (++pipe->m_byteCount % (uint32_t)pipe->m_corruptEvery) == 0 )
		{
			b ^= 0x5A;
		}
		q.push_back( b );
	}
	pipe->m_signal.notify_all();
	return true;
}

int WrenchDebugPipe::End::read( uint8_t* data, uint32_t maxLen )
{
	std::lock_guard<std::mutex> guard( pipe->m_lock );
	std::deque<uint8_t>& q = pipe->m_queues[side == 0 ? 0 : 1];
	if ( q.empty() )
	{
		return pipe->m_closed ? -1 : 0;
	}
	uint32_t n = 0;
	while ( n < maxLen && !q.empty() )
	{
		data[n++] = q.front();
		q.pop_front();
	}
	return (int)n;
}

uint32_t WrenchDebugPipe::End::millis()
{
	using namespace std::chrono;
	return (uint32_t)duration_cast<milliseconds>( steady_clock::now().time_since_epoch() ).count();
}

void WrenchDebugPipe::End::idle()
{
	std::unique_lock<std::mutex> lock( pipe->m_lock );
	std::deque<uint8_t>& q = pipe->m_queues[side == 0 ? 0 : 1];
	if ( q.empty() && !pipe->m_closed )
	{
		pipe->m_signal.wait_for( lock, std::chrono::milliseconds(10) );
	}
}

bool WrenchDebugPipe::End::connected()
{
	std::lock_guard<std::mutex> guard( pipe->m_lock );
	return !pipe->m_closed;
}

#endif
