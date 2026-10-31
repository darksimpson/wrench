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

#ifdef WRENCH_INCLUDE_DEBUG_CODE

// strings shown inline in variable lists are cut to this many bytes, the
// client fetches the rest through the string handle
#define WRD_STRING_PREVIEW 48

struct WrenchDebugFrameInfo
{
	uint8_t kind;
	uint8_t function;
	uint32_t pcOffset;
	WRValue* frameBase;
	WRContext* context; // whose code the frame runs
};

// walks the live VM frames of a stopped context, innermost first. A function
// of an imported module is a nested call: past its frame the walk goes on in
// the program that called it, as the import calls recorded it
class WrenchDebugFrameWalker
{
public:
	WrenchDebugFrameWalker( const WrenchDebugServerPrivate* I, WRContext* context, const unsigned char* pc, WRValue* frameBase )
		: m_I( I ), m_context( context ), m_pc( pc ), m_frameBase( frameBase ), m_done( context == 0 || pc == 0 ),
		m_call( (I && I->importCallDepth <= WRENCH_DEBUG_MAX_IMPORT_CALLS) ? I->importCallDepth : 0 ) {}

	bool next( WrenchDebugFrameInfo& info );

private:
	int functionForOffset( uint32_t offset ) const;
	bool onStack( const WRValue* v ) const;

	const WrenchDebugServerPrivate* m_I;
	WRContext* m_context;
	const unsigned char* m_pc;
	WRValue* m_frameBase;
	bool m_done;
	int m_call; // import calls not yet walked past: [0, m_call)
};

int WrenchDebugFrameWalker::functionForOffset( uint32_t offset ) const
{
	// global code always precedes the function bodies, so the owner of an
	// offset is the function with the highest start at or below it
	int best = -1;
	uint32_t bestOffset = 0;
	for ( int i = 0; i < m_context->numLocalFunctions; ++i )
	{
		uint32_t start = m_context->localFunctions[i].functionOffset;
		if ( start <= offset && (best < 0 || start >= bestOffset) )
		{
			best = i;
			bestOffset = start;
		}
	}
	return best;
}

bool WrenchDebugFrameWalker::onStack( const WRValue* v ) const
{
	return v >= m_context->stack && v < m_context->stack + m_context->w->stackSize;
}

bool WrenchDebugFrameWalker::next( WrenchDebugFrameInfo& info )
{
	if ( m_done )
	{
		return false;
	}

	if ( m_pc < m_context->bottom || m_pc >= m_context->bottom + m_context->bottomSize )
	{
		m_done = true;
		return false;
	}

	info.pcOffset = (uint32_t)(m_pc - m_context->bottom);
	info.context = m_context;
	int func = functionForOffset( info.pcOffset );

	if ( func < 0 || !m_frameBase )
	{
		info.kind = WRD_FrameGlobal;
		info.function = 0;
		info.frameBase = 0;
		m_done = true;
		return true;
	}

	info.kind = WRD_FrameFunction;
	info.function = (uint8_t)func;
	info.frameBase = m_frameBase;

	// the return vector sits right after the arguments and locals
	const WRFunction* f = m_context->localFunctions + func;
	WRValue* returnVector = m_frameBase + f->frameBaseAdjustment - 1;
	if ( !onStack(m_frameBase) || !onStack(returnVector) )
	{
		m_done = true;
		return true;
	}

	intptr_t offset = returnVector->returnOffset;
	if ( offset < 0 || offset >= m_context->bottomSize )
	{
		m_done = true;
		return true;
	}

	const unsigned char* callerPc = m_context->bottom + offset;
	if ( callerPc == m_context->stopLocation )
	{
		// called through wr_callFunction: by a debugged program, when this is
		// an import it called, else by native code and nothing is above us
		while ( m_call > 0 )
		{
			const WrenchDebugImportCall& call = m_I->importCalls[--m_call];
			if ( call.callee == m_context )
			{
				m_context = call.caller;
				m_pc = call.pc;
				m_frameBase = call.frameBase;
				return true;
			}
		}
		m_done = true;
		return true;
	}

	m_pc = callerPc;
	m_frameBase = returnVector->frame;
	if ( m_frameBase && !onStack(m_frameBase) )
	{
		m_done = true;
	}
	return true;
}

// one byte past the frame base is "deeper" than global code, which has no
// frame base, so the key orders frames by depth for stepping
static uint32_t wr_debugDepthKey( WRContext* context, const WRValue* frameBase )
{
	return frameBase ? (uint32_t)(frameBase - context->stack) + 1 : 0;
}

class WrenchDebugServerImpl
{
public:
	static void init( WrenchDebugServerPrivate* I );
	static void pump( WrenchDebugServerPrivate* I );
	static void process( WrenchDebugServerPrivate* I, const WrenchDebugFrame& frame );
	static bool stopLoop( WrenchDebugServerPrivate* I, uint8_t reason, uint8_t err );
	static void announceStop( WrenchDebugServerPrivate* I );
	static bool waitForClient( WrenchDebugServerPrivate* I, uint32_t waitMs );
	static void readProgramInfo( WrenchDebugProgramSlot& p );
	static void releaseProgram( WrenchDebugServerPrivate* I, int program );
	static int programOf( const WrenchDebugServerPrivate* I, const WRContext* context );
	static int addProgram( WrenchDebugServerPrivate* I, WRContext* context, uint8_t importer, const char* name );
	static void announceProgram( WrenchDebugServerPrivate* I, uint8_t type, int program );
	static void onDisconnect( WrenchDebugServerPrivate* I );

	static uint8_t* payload( WrenchDebugServerPrivate* I ) { return I->tx + WRD_FRAME_HEADER; }
	static uint32_t missingFunction( WrenchDebugServerPrivate* I, uint8_t err )
	{
		return (err == WR_ERR_function_not_found || err == WR_ERR_lib_function_not_found) ? I->w->missingFunction : 0;
	}
	static void send( WrenchDebugServerPrivate* I, uint8_t type, uint8_t seq, uint32_t len );
	static void replyOk( WrenchDebugServerPrivate* I, uint8_t seq ) { send( I, WRD_MsgReplyOk, seq, 0 ); }
	static void replyError( WrenchDebugServerPrivate* I, uint8_t seq, uint8_t code );

	static void resetHandles( WrenchDebugServerPrivate* I );
	static uint32_t addHandle( WrenchDebugServerPrivate* I, uint8_t kind, const void* p, uint32_t aux );
	static WrenchDebugHandle* findHandle( WrenchDebugServerPrivate* I, uint32_t handle );

	static const WRValue* resolve( const WRValue* v, WRValue& temp );
	static void writeValue( WrenchDebugServerPrivate* I, WrenchDebugWriter& out, const WRValue& value );
	static bool readValue( WrenchDebugServerPrivate* I, WrenchDebugReader& in, WRValue* target, WRContext* context );
	static WRValue* storageOf( WRValue* slot );

	static bool frameAt( WrenchDebugServerPrivate* I, uint16_t index, WrenchDebugFrameInfo& info );
	static void getChildren( WrenchDebugServerPrivate* I, uint8_t seq, WrenchDebugReader& in );
	static WRValue* childSlot( WrenchDebugServerPrivate* I, WrenchDebugHandle* h, uint32_t index );
	static uint32_t hashEntryCount( const WRGCObject* va );
	static int hashSlotForIndex( const WRGCObject* va, uint32_t index );
	static uint32_t structMemberHash( const WRGCObject* va, uint32_t index );

	static void addWatch( WrenchDebugServerPrivate* I, uint8_t seq, WrenchDebugReader& in );
	static const WRValue* followWatch( WrenchDebugServerPrivate* I, const WrenchDebugWatch& watch, WRValue& temp );
	static void snapshot( const WRValue* v, WrenchDebugWatchSnapshot& s );
	static void refreshWatches( WrenchDebugServerPrivate* I );
	static void removeWatch( WrenchDebugServerPrivate* I, int index, bool announce );
	static void clearWatches( WrenchDebugServerPrivate* I, bool localsOnly, bool announce );
	static uint8_t checkWatches( WrenchDebugServerPrivate* I, WRContext* context, const unsigned char* pc, WRValue* frameBase );
};

void WrenchDebugServerImpl::init( WrenchDebugServerPrivate* I )
{
	I->decoder.init( I->rx, sizeof(I->rx) );
	I->state = WRD_StateNoProgram;
	I->stepMode = WRD_StepNone;
	I->lastLine = -1;
	I->stopProgram = WRD_NoProgram;
	I->entryProgram = WRD_NoProgram;
	for ( int p = 0; p < WRENCH_DEBUG_MAX_PROGRAMS; ++p )
	{
		I->programs[p].importer = WRD_NoProgram;
	}
}

int WrenchDebugServerImpl::programOf( const WrenchDebugServerPrivate* I, const WRContext* context )
{
	for ( int p = 0; context && p < WRENCH_DEBUG_MAX_PROGRAMS; ++p )
	{
		if ( I->programs[p].context == context )
		{
			return p;
		}
	}
	return -1;
}

void WrenchDebugServerImpl::announceProgram( WrenchDebugServerPrivate* I, uint8_t type, int program )
{
	if ( I->helloReceived )
	{
		WrenchDebugWriter out( payload(I), WRENCH_DEBUG_MAX_PAYLOAD );
		out.u8( (uint8_t)program );
		send( I, type, 0, out.size() );
	}
}

// one more program, in the first free slot after program 0 (the first
// context attached, the one uploads replace); -1 when they are all taken
int WrenchDebugServerImpl::addProgram( WrenchDebugServerPrivate* I, WRContext* context, uint8_t importer, const char* name )
{
	int existing = programOf( I, context );
	if ( existing >= 0 )
	{
		return existing;
	}
	for ( int p = 1; p < WRENCH_DEBUG_MAX_PROGRAMS; ++p )
	{
		WrenchDebugProgramSlot& slot = I->programs[p];
		if ( slot.context )
		{
			continue;
		}
		memset( (char*)&slot, 0, sizeof(slot) );
		slot.context = context;
		slot.bytes = context->bottom;
		slot.bytesLen = (uint32_t)context->bottomSize;
		slot.importer = importer;
		for ( int c = 0; name && name[c] && c < WRENCH_DEBUG_PROGRAM_NAME - 1; ++c )
		{
			slot.name[c] = name[c];
		}
		readProgramInfo( slot );
		context->debugServer = I->parent;
		for ( uint16_t b = 0; b < I->breakpointCount; ++b )
		{
			WrenchDebugBreakpoint& bp = I->breakpoints[b];
			if ( bp.program == WRD_NoProgram && slot.programHash && bp.parkedHash == slot.programHash )
			{
				bp.program = (uint8_t)p;
			}
		}
		announceProgram( I, WRD_MsgEventProgramAdded, p );
		return p;
	}
	return -1;
}

void WrenchDebugServerImpl::send( WrenchDebugServerPrivate* I, uint8_t type, uint8_t seq, uint32_t len )
{
	if ( len > WRENCH_DEBUG_MAX_PAYLOAD )
	{
		len = 0;
		type = WRD_MsgReplyError;
	}
	if ( I->processingControl && (type == WRD_MsgReplyOk || type == WRD_MsgReplyError) )
	{
		// remembered so a retransmission gets the same answer
		I->lastControlReply = (type == WRD_MsgReplyOk) ? 0 : (len ? payload(I)[0] : WRD_ErrBadRequest);
		I->processingControl = false;
	}
	uint32_t total = wr_debugFinishFrame( I->tx, type, seq, (uint16_t)len );
	I->transport->write( I->tx, total );
}

void WrenchDebugServerImpl::replyError( WrenchDebugServerPrivate* I, uint8_t seq, uint8_t code )
{
	payload( I )[0] = code;
	send( I, WRD_MsgReplyError, seq, 1 );
}

void WrenchDebugServerImpl::resetHandles( WrenchDebugServerPrivate* I )
{
	I->handleCount = 0;
	++I->handleGeneration;
}

uint32_t WrenchDebugServerImpl::addHandle( WrenchDebugServerPrivate* I, uint8_t kind, const void* p, uint32_t aux )
{
	for ( uint16_t i = 0; i < I->handleCount; ++i )
	{
		if ( I->handles[i].p == p && I->handles[i].kind == kind )
		{
			return ((uint32_t)I->handleGeneration << 16) | (uint32_t)(i + 1);
		}
	}

	if ( I->handleCount >= WRENCH_DEBUG_MAX_HANDLES )
	{
		return 0;
	}

	WrenchDebugHandle* h = I->handles + I->handleCount++;
	h->p = p;
	h->aux = aux;
	h->kind = kind;
	return ((uint32_t)I->handleGeneration << 16) | (uint32_t)I->handleCount;
}

WrenchDebugHandle* WrenchDebugServerImpl::findHandle( WrenchDebugServerPrivate* I, uint32_t handle )
{
	uint32_t index = handle & 0xFFFF;
	if ( (handle >> 16) != I->handleGeneration || index == 0 || index > I->handleCount )
	{
		return 0;
	}
	return I->handles + (index - 1);
}

const WRValue* WrenchDebugServerImpl::resolve( const WRValue* v, WRValue& temp )
{
	for ( int guard = 0; guard < 16 && v; ++guard )
	{
		if ( v->type == WR_REF )
		{
			v = v->r;
		}
		else if ( v->type == WR_EX && IS_CONTAINER_MEMBER(v->xtype) )
		{
			// deref() hands back a shared temporary for some members, copy it
			temp = v->deref();
			return &temp;
		}
		else
		{
			return v;
		}
	}
	temp.init();
	return &temp;
}

WRValue* WrenchDebugServerImpl::storageOf( WRValue* slot )
{
	for ( int guard = 0; guard < 16 && slot; ++guard )
	{
		if ( slot->type != WR_REF )
		{
			return (slot->type == WR_EX && IS_CONTAINER_MEMBER(slot->xtype)) ? 0 : slot;
		}
		slot = slot->r;
	}
	return 0;
}

void WrenchDebugServerImpl::writeValue( WrenchDebugServerPrivate* I, WrenchDebugWriter& out, const WRValue& value )
{
	WRValue temp;
	const WRValue* v = resolve( &value, temp );

	if ( v->type == WR_INT )
	{
		out.u8( WRD_ValInt );
		out.i32( v->i );
		return;
	}

	if ( v->type == WR_FLOAT )
	{
		out.u8( WRD_ValFloat );
		out.f32( v->f );
		return;
	}

	if ( v->type == WR_EX )
	{
		if ( v->xtype == WR_EX_ARRAY && v->va )
		{
			if ( v->va->m_type == SV_CHAR )
			{
				uint32_t total = v->va->m_size;
				uint16_t n = (uint16_t)(total > WRD_STRING_PREVIEW ? WRD_STRING_PREVIEW : total);
				out.u8( WRD_ValString );
				out.u32( addHandle(I, WRD_ValString, v->va, 0) );
				out.u32( total );
				out.u16( n );
				out.bytes( v->va->m_Cdata, n );
				return;
			}

			if ( v->va->m_type == SV_VALUE )
			{
				out.u8( WRD_ValArray );
				out.u32( addHandle(I, WRD_ValArray, v->va, 0) );
				out.u32( v->va->m_size );
				return;
			}
		}
		else if ( v->xtype == WR_EX_HASH_TABLE && v->va )
		{
			out.u8( WRD_ValHash );
			out.u32( addHandle(I, WRD_ValHash, v->va, 0) );
			out.u32( hashEntryCount(v->va) );
			return;
		}
		else if ( v->xtype == WR_EX_STRUCT && v->va )
		{
			out.u8( WRD_ValStruct );
			out.u32( addHandle(I, WRD_ValStruct, v->va, 0) );
			out.u32( v->va->m_size );
			return;
		}
		else if ( IS_RAW_ARRAY(v->xtype) )
		{
			uint32_t size = (uint32_t)EX_RAW_ARRAY_SIZE_FROM_P2( v->p2 );
			out.u8( WRD_ValRaw );
			out.u32( addHandle(I, WRD_ValRaw, v->c, size) );
			out.u32( size );
			return;
		}
	}

	out.u8( WRD_ValOther );
	out.u8( v->type == WR_EX ? (uint8_t)v->xtype : (uint8_t)v->type );
}

bool WrenchDebugServerImpl::readValue( WrenchDebugServerPrivate* I, WrenchDebugReader& in, WRValue* target, WRContext* context )
{
	uint8_t tag = in.u8();
	if ( tag == WRD_ValInt )
	{
		int32_t i = in.i32();
		if ( in.bad() )
		{
			return false;
		}
		target->init( (int)i );
		return true;
	}

	if ( tag == WRD_ValFloat )
	{
		float f = in.f32();
		if ( in.bad() )
		{
			return false;
		}
		target->init( f );
		return true;
	}

	if ( tag == WRD_ValString )
	{
		uint16_t n = in.u16();
		const uint8_t* bytes = in.bytes( n );
		if ( in.bad() || !context )
		{
			return false;
		}

		WRValue made;
		made.init();
		wr_makeString( context, &made, n ? (const char*)bytes : "", n );
		if ( made.type != WR_EX )
		{
			return false;
		}
		*target = made;
		return true;
	}

	return false;
}

uint32_t WrenchDebugServerImpl::hashEntryCount( const WRGCObject* va )
{
	uint32_t count = 0;
	for ( uint32_t i = 0; i < va->hashSlots(); ++i )
	{
		if ( va->m_hashTable[i] != WRENCH_NULL_HASH )
		{
			++count;
		}
	}
	return count;
}

int WrenchDebugServerImpl::hashSlotForIndex( const WRGCObject* va, uint32_t index )
{
	uint32_t seen = 0;
	for ( uint32_t i = 0; i < va->hashSlots(); ++i )
	{
		if ( va->m_hashTable[i] != WRENCH_NULL_HASH )
		{
			if ( seen++ == index )
			{
				return (int)i;
			}
		}
	}
	return -1;
}

uint32_t WrenchDebugServerImpl::structMemberHash( const WRGCObject* va, uint32_t index )
{
	// struct members are laid out by index, the compiled-in table maps
	// member name hashes to that index (5 bytes per entry)
	const unsigned char* table = va->m_ROMHashTable;
	for ( uint32_t e = 0; e < va->m_mod; ++e, table += 5 )
	{
		uint32_t hash = (uint32_t)READ_32_FROM_PC( table );
		if ( hash != WRENCH_NULL_HASH && (uint32_t)READ_8_FROM_PC(table + 4) == index )
		{
			return hash;
		}
	}
	return 0;
}

void WrenchDebugServerImpl::addWatch( WrenchDebugServerPrivate* I, uint8_t seq, WrenchDebugReader& in )
{
	WrenchDebugWatch watch;
	memset( (char*)&watch, 0, sizeof(watch) );
	watch.root = in.u8();
	watch.program = in.u8();
	uint16_t frameIndex = in.u16();
	watch.slot = in.u16();
	watch.stepCount = in.u8();
	if ( watch.stepCount > WRENCH_DEBUG_MAX_WATCH_STEPS )
	{
		replyError( I, seq, WRD_ErrUnsupported );
		return;
	}
	for ( uint8_t s = 0; s < watch.stepCount; ++s )
	{
		watch.stepKind[s] = in.u8();
		watch.step[s] = in.u32();
	}
	if ( in.bad() || (watch.root != WRD_WatchGlobal && watch.root != WRD_WatchLocal) )
	{
		replyError( I, seq, WRD_ErrBadRequest );
		return;
	}
	if ( watch.root == WRD_WatchGlobal )
	{
		WRContext* context = (watch.program < WRENCH_DEBUG_MAX_PROGRAMS) ? I->programs[watch.program].context : 0;
		if ( !context )
		{
			replyError( I, seq, WRD_ErrNoProgram );
			return;
		}
		if ( watch.slot >= context->globals )
		{
			replyError( I, seq, WRD_ErrBadWatch );
			return;
		}
	}
	else
	{
		// a local belongs to one call: name it while that call is on the stack
		WrenchDebugFrameInfo info;
		if ( I->state != WRD_StateStopped )
		{
			replyError( I, seq, WRD_ErrNotStopped );
			return;
		}
		if ( !frameAt(I, frameIndex, info) || info.kind != WRD_FrameFunction
				|| watch.slot >= info.context->localFunctions[info.function].frameBaseAdjustment - 1 )
		{
			replyError( I, seq, WRD_ErrBadWatch );
			return;
		}
		watch.frameBase = info.frameBase;
		watch.stack = info.context->stack;
		watch.function = info.function;
		watch.functionContext = info.context;
	}

	int found = -1;
	for ( int i = 0; i < WRENCH_DEBUG_MAX_WATCHES; ++i )
	{
		if ( !I->watches[i].used )
		{
			found = i;
			break;
		}
	}
	if ( found < 0 )
	{
		replyError( I, seq, WRD_ErrTooManyWatches );
		return;
	}

	watch.used = 1;
	WRValue temp;
	snapshot( followWatch(I, watch, temp), watch.last );
	I->watches[found] = watch;
	++I->watchCount;

	WrenchDebugWriter out( payload(I), WRENCH_DEBUG_MAX_PAYLOAD );
	out.u8( (uint8_t)(found + 1) );
	send( I, WRD_MsgReplyOk, seq, out.size() );
}

// the value a watch names now, 0 when its path leads nowhere (an element
// past the end, a key not present, a step into something that is no
// container). Everything is looked up again from the root, so a container
// that grew, moved or was collected since is never touched
const WRValue* WrenchDebugServerImpl::followWatch( WrenchDebugServerPrivate* I, const WrenchDebugWatch& watch, WRValue& temp )
{
	const WRValue* v = 0;
	if ( watch.root == WRD_WatchGlobal )
	{
		WRContext* context = (watch.program < WRENCH_DEBUG_MAX_PROGRAMS) ? I->programs[watch.program].context : 0;
		if ( !context || watch.slot >= context->globals )
		{
			return 0;
		}
		v = ((const WRValue*)(context + 1)) + watch.slot;
	}
	else
	{
		v = watch.frameBase + watch.slot;
	}

	for ( uint8_t s = 0; s < watch.stepCount; ++s )
	{
		v = resolve( v, temp );
		uint32_t n = watch.step[s];
		bool last = (s + 1 == watch.stepCount);
		if ( v->type != WR_EX )
		{
			return 0;
		}

		if ( v->xtype == WR_EX_ARRAY && v->va )
		{
			if ( watch.stepKind[s] != WRD_WatchIndex || n >= v->va->m_size )
			{
				return 0;
			}
			if ( v->va->m_type == SV_VALUE )
			{
				v = v->va->m_Vdata + n;
				continue;
			}
			if ( v->va->m_type != SV_CHAR || !last )
			{
				return 0;
			}
			int c = (unsigned char)v->va->m_Cdata[n];
			temp.init( c );
			return &temp;
		}

		if ( IS_RAW_ARRAY(v->xtype) )
		{
			if ( watch.stepKind[s] != WRD_WatchIndex || !last || n >= (uint32_t)EX_RAW_ARRAY_SIZE_FROM_P2(v->p2) )
			{
				return 0;
			}
			int c = (unsigned char)v->c[n];
			temp.init( c );
			return &temp;
		}

		if ( v->xtype == WR_EX_HASH_TABLE && v->va )
		{
			// an integer key is its own hash, so both kinds of step work
			v = v->va->exists( n, false );
		}
		else if ( v->xtype == WR_EX_STRUCT && v->va && watch.stepKind[s] == WRD_WatchKey )
		{
			v = wr_valueFromConfirmedStruct( (WRValue*)v, n );
		}
		else
		{
			return 0;
		}

		if ( !v )
		{
			return 0;
		}
	}

	return resolve( v, temp );
}

// numbers by value; strings by identity, length and contents; other
// containers by identity and size (a changed element is caught by watching
// the element)
void WrenchDebugServerImpl::snapshot( const WRValue* v, WrenchDebugWatchSnapshot& s )
{
	memset( (char*)&s, 0, sizeof(s) );
	if ( !v )
	{
		return;
	}

	s.kind = (uint8_t)(v->type + 1);
	if ( v->type == WR_INT || v->type == WR_FLOAT )
	{
		s.bits = v->ui;
		return;
	}
	if ( v->type != WR_EX )
	{
		return;
	}

	s.xtype = (uint8_t)v->xtype;
	if ( IS_RAW_ARRAY(v->xtype) )
	{
		s.p = v->c;
		s.count = (uint32_t)EX_RAW_ARRAY_SIZE_FROM_P2( v->p2 );
	}
	else if ( (v->xtype == WR_EX_ARRAY || v->xtype == WR_EX_HASH_TABLE || v->xtype == WR_EX_STRUCT) && v->va )
	{
		s.p = v->va;
		s.count = v->va->m_size;
		if ( v->xtype == WR_EX_ARRAY && v->va->m_type == SV_CHAR )
		{
			s.bits = wr_hash( v->va->m_Cdata, (int)v->va->m_size );
		}
	}
}

// while stopped only the debugger changes values: take them all again
// before the program goes on, so its own writes do not count as changes
void WrenchDebugServerImpl::refreshWatches( WrenchDebugServerPrivate* I )
{
	for ( int i = 0; i < WRENCH_DEBUG_MAX_WATCHES; ++i )
	{
		if ( I->watches[i].used )
		{
			WRValue temp;
			snapshot( followWatch(I, I->watches[i], temp), I->watches[i].last );
		}
	}
}

void WrenchDebugServerImpl::removeWatch( WrenchDebugServerPrivate* I, int index, bool announce )
{
	if ( !I->watches[index].used )
	{
		return;
	}
	I->watches[index].used = 0;
	--I->watchCount;
	if ( announce && I->helloReceived )
	{
		WrenchDebugWriter out( payload(I), WRENCH_DEBUG_MAX_PAYLOAD );
		out.u8( (uint8_t)(index + 1) );
		send( I, WRD_MsgEventWatchRemoved, 0, out.size() );
	}
}

void WrenchDebugServerImpl::clearWatches( WrenchDebugServerPrivate* I, bool localsOnly, bool announce )
{
	for ( int i = 0; i < WRENCH_DEBUG_MAX_WATCHES; ++i )
	{
		if ( !localsOnly || I->watches[i].root == WRD_WatchLocal )
		{
			removeWatch( I, i, announce );
		}
	}
}

// the id of the first watch whose value changed since it was last taken,
// 0 for none. A local whose call has returned goes away (and the client
// hears about it): the slot belongs to someone else by now
uint8_t WrenchDebugServerImpl::checkWatches( WrenchDebugServerPrivate* I, WRContext* context, const unsigned char* pc, WRValue* frameBase )
{
	uint32_t depth = wr_debugDepthKey( context, frameBase );
	int function = -2;
	for ( int i = 0; i < WRENCH_DEBUG_MAX_WATCHES; ++i )
	{
		WrenchDebugWatch& watch = I->watches[i];
		if ( !watch.used )
		{
			continue;
		}

		// a local's call can only have returned when code runs on its stack
		// (another program may run on one of its own meanwhile)
		if ( watch.root == WRD_WatchLocal && watch.stack == context->stack )
		{
			uint32_t watchDepth = wr_debugDepthKey( context, watch.frameBase );
			if ( depth == watchDepth && function == -2 )
			{
				// same depth: still that call, or a later one in its place?
				WrenchDebugFrameWalker walker( I, context, pc, frameBase );
				WrenchDebugFrameInfo info;
				bool inFunction = walker.next( info ) && info.kind == WRD_FrameFunction && info.context == watch.functionContext;
				function = inFunction ? info.function : -1;
			}
			if ( depth < watchDepth || (depth == watchDepth && function != watch.function) )
			{
				removeWatch( I, i, true );
				continue;
			}
		}

		WRValue temp;
		WrenchDebugWatchSnapshot now;
		snapshot( followWatch(I, watch, temp), now );
		if ( now.kind != watch.last.kind || now.xtype != watch.last.xtype || now.bits != watch.last.bits
				|| now.p != watch.last.p || now.count != watch.last.count )
		{
			return (uint8_t)(i + 1);
		}
	}
	return 0;
}

bool WrenchDebugServerImpl::frameAt( WrenchDebugServerPrivate* I, uint16_t index, WrenchDebugFrameInfo& info )
{
	if ( I->state != WRD_StateStopped )
	{
		return false;
	}

	WrenchDebugFrameWalker walker( I, I->stopContext, I->stopPc, I->stopFrameBase );
	for ( uint16_t i = 0; walker.next(info); ++i )
	{
		if ( i == index )
		{
			return true;
		}
	}
	return false;
}

WRValue* WrenchDebugServerImpl::childSlot( WrenchDebugServerPrivate* I, WrenchDebugHandle* h, uint32_t index )
{
	WRGCObject* va = (WRGCObject*)h->p;
	if ( h->kind == WRD_ValArray || h->kind == WRD_ValStruct )
	{
		return index < va->m_size ? va->m_Vdata + index : 0;
	}

	if ( h->kind == WRD_ValHash )
	{
		int slot = hashSlotForIndex( va, index );
		return slot < 0 ? 0 : va->m_Vdata + (slot << 1);
	}

	return 0;
}

void WrenchDebugServerImpl::getChildren( WrenchDebugServerPrivate* I, uint8_t seq, WrenchDebugReader& in )
{
	uint32_t handle = in.u32();
	uint32_t first = in.u32();
	uint16_t count = in.u16();
	if ( in.bad() )
	{
		replyError( I, seq, WRD_ErrBadRequest );
		return;
	}

	WrenchDebugHandle* h = findHandle( I, handle );
	if ( !h )
	{
		replyError( I, seq, WRD_ErrBadHandle );
		return;
	}

	WrenchDebugWriter out( payload(I), WRENCH_DEBUG_MAX_PAYLOAD );
	const WRGCObject* va = (const WRGCObject*)h->p;

	if ( h->kind == WRD_ValString || h->kind == WRD_ValRaw )
	{
		const uint8_t* data = (h->kind == WRD_ValString) ? va->m_Cdata : (const uint8_t*)h->p;
		uint32_t total = (h->kind == WRD_ValString) ? va->m_size : h->aux;
		uint32_t n = 0;
		if ( first < total )
		{
			n = total - first;
			if ( n > count )
			{
				n = count;
			}
			if ( n > WRENCH_DEBUG_MAX_PAYLOAD - 6 )
			{
				n = WRENCH_DEBUG_MAX_PAYLOAD - 6;
			}
		}
		out.u32( total );
		out.u16( (uint16_t)n );
		out.bytes( data + first, n );
		send( I, WRD_MsgReplyOk, seq, out.size() );
		return;
	}

	uint32_t total = (h->kind == WRD_ValHash) ? hashEntryCount( va ) : va->m_size;
	out.u32( total );
	uint8_t* countAt = out.reserve( 2 );
	uint16_t written = 0;

	for ( uint32_t i = first; i < total && written < count; ++i )
	{
		uint32_t mark = out.size();

		if ( h->kind == WRD_ValArray )
		{
			writeValue( I, out, va->m_Vdata[i] );
		}
		else if ( h->kind == WRD_ValStruct )
		{
			out.u32( structMemberHash(va, i) );
			writeValue( I, out, va->m_Vdata[i] );
		}
		else
		{
			int slot = hashSlotForIndex( va, i );
			if ( slot < 0 )
			{
				break;
			}
			writeValue( I, out, va->m_Vdata[(slot << 1) + 1] ); // key
			writeValue( I, out, va->m_Vdata[slot << 1] ); // value
		}

		if ( out.overflow() )
		{
			out.truncate( mark );
			break;
		}
		++written;
	}

	countAt[0] = (uint8_t)written;
	countAt[1] = (uint8_t)(written >> 8);
	send( I, WRD_MsgReplyOk, seq, out.size() );
}

void WrenchDebugServerImpl::readProgramInfo( WrenchDebugProgramSlot& p )
{
	const uint8_t* b = p.bytes;
	p.symbols = 0;
	p.symbolsLen = 0;
	p.source = 0;
	p.sourceLen = 0;
	p.programHash = (uint32_t)READ_32_FROM_PC( b + p.bytesLen - 4 );
	p.compilerFlags = (uint8_t)READ_8_FROM_PC( b + 2 );

	uint32_t globals = (uint8_t)READ_8_FROM_PC( b );
	uint32_t functions = (uint8_t)READ_8_FROM_PC( b + 1 );
	uint32_t pos = 3 + functions * WR_FUNCTION_CORE_SIZE;

	if ( p.compilerFlags & WR_INCLUDE_GLOBALS )
	{
		pos += globals * 4;
	}

	if ( (p.compilerFlags & (WR_EMBED_DEBUG_CODE | WR_EMBED_SOURCE_CODE)) && pos + 6 <= p.bytesLen )
	{
		uint32_t len = (uint16_t)READ_16_FROM_PC( b + pos + 4 );
		if ( pos + 6 + len <= p.bytesLen )
		{
			p.symbols = b + pos + 6;
			p.symbolsLen = len;
		}
		pos += 6 + len;
	}

	if ( (p.compilerFlags & WR_EMBED_SOURCE_CODE) && pos + 4 <= p.bytesLen )
	{
		uint32_t len = (uint32_t)READ_32_FROM_PC( b + pos );
		if ( len <= p.bytesLen - pos - 4 )
		{
			p.source = b + pos + 4;
			p.sourceLen = len;
		}
	}
}

// stop debugging a program, and the modules it imported (the context is the
// firmware's, it may live on undebugged)
void WrenchDebugServerImpl::releaseProgram( WrenchDebugServerPrivate* I, int program )
{
	WrenchDebugProgramSlot& p = I->programs[program];

	// handles may point into the program's objects, which are about to go,
	// and its breakpoints and watches name its lines and variables
	resetHandles( I );
	for ( int i = 0; i < WRENCH_DEBUG_MAX_WATCHES; ++i )
	{
		const WrenchDebugWatch& watch = I->watches[i];
		bool its = (watch.root == WRD_WatchGlobal) ? watch.program == program : watch.functionContext == p.context;
		if ( watch.used && its )
		{
			removeWatch( I, i, true );
		}
	}
	// program 0's go, the client sets them with the next upload; a module's
	// are parked, the next run that imports it again has them from the start
	uint16_t kept = 0;
	for ( uint16_t b = 0; b < I->breakpointCount; ++b )
	{
		WrenchDebugBreakpoint bp = I->breakpoints[b];
		if ( bp.program == program )
		{
			if ( program == 0 || !p.programHash )
			{
				continue;
			}
			bp.program = WRD_NoProgram;
			bp.parkedHash = p.programHash;
			bool twin = false;
			for ( uint16_t k = 0; k < kept && !twin; ++k )
			{
				twin = I->breakpoints[k].program == WRD_NoProgram && I->breakpoints[k].parkedHash == bp.parkedHash && I->breakpoints[k].line == bp.line;
			}
			if ( twin )
			{
				continue;
			}
		}
		I->breakpoints[kept++] = bp;
	}
	I->breakpointCount = kept;

	WRContext* context = p.context;
	bool wasThere = context != 0;
	memset( (char*)&p, 0, sizeof(p) );
	p.importer = WRD_NoProgram;

	if ( context )
	{
		context->debugServer = 0;
	}
	if ( context && I->abortContext == context )
	{
		I->abortPending = false;
		I->abortContext = 0;
	}
	for ( int m = 0; wasThere && m < WRENCH_DEBUG_MAX_PROGRAMS; ++m )
	{
		if ( I->programs[m].context && I->programs[m].importer == program )
		{
			releaseProgram( I, m );
		}
	}

	if ( program == 0 )
	{
		I->contextRan = false;
		I->state = WRD_StateNoProgram;
	}
	else if ( wasThere )
	{
		announceProgram( I, WRD_MsgEventProgramRemoved, program );
	}
}

void WrenchDebugServerImpl::onDisconnect( WrenchDebugServerPrivate* I )
{
	// never leave the target halted or trapping for a client that is gone
	I->helloReceived = false;
	I->breakpointCount = 0;
	clearWatches( I, false, false );
	I->stepMode = WRD_StepNone;
	if ( I->state == WRD_StateStopped )
	{
		I->resume = true;
	}
	if ( I->abortOnDisconnect && (I->inHook || I->state == WRD_StateStopped) )
	{
		I->abortPending = true;
		I->abortContext = 0;
	}
	I->decoder.reset();
}

void WrenchDebugServerImpl::pump( WrenchDebugServerPrivate* I )
{
	uint8_t chunk[64];
	for ( int rounds = 0; rounds < 64; ++rounds )
	{
		int got = I->transport->read( chunk, sizeof(chunk) );
		if ( got < 0 )
		{
			if ( I->helloReceived )
			{
				onDisconnect( I );
			}
			return;
		}

		if ( got == 0 )
		{
			if ( I->helloReceived && !I->transport->connected() )
			{
				onDisconnect( I );
			}
			return;
		}

		uint32_t now = I->transport->millis();
		I->lastRxMs = now;
		for ( int i = 0; i < got; ++i )
		{
			if ( I->decoder.feed(chunk[i], now) )
			{
				process( I, I->decoder.frame() );
				if ( I->resume && I->state == WRD_StateStopped )
				{
					// a continue or step was accepted: requests read after it
					// (the client sends them as soon as it has the reply) are
					// answered as running, the stop loop ends when pump returns
					I->state = WRD_StateRunning;
				}
			}
		}
	}
}

void WrenchDebugServerImpl::announceStop( WrenchDebugServerPrivate* I )
{
	WrenchDebugWriter out( payload(I), WRENCH_DEBUG_MAX_PAYLOAD );
	out.u8( I->stopReason );
	out.i32( I->lastLine );
	out.u8( I->stopError );
	out.u32( missingFunction(I, I->stopError) );
	out.u8( I->stopReason == WRD_StopWatch ? I->watchHit : 0 );
	out.u8( I->stopProgram );
	send( I, WRD_MsgEventStopped, 0, out.size() );
}

bool WrenchDebugServerImpl::stopLoop( WrenchDebugServerPrivate* I, uint8_t reason, uint8_t err )
{
	I->state = WRD_StateStopped;
	I->stepMode = WRD_StepNone;
	I->resume = false;
	I->stopReason = reason;
	I->stopError = err;
	I->lastRxMs = I->transport->millis();
	resetHandles( I );

	announceStop( I );

	while ( !I->resume )
	{
		pump( I );
		if ( I->resume )
		{
			break;
		}
		if ( !I->helloReceived )
		{
			// client detached while we were stopped
			I->resume = true;
			break;
		}
		if ( I->linkTimeoutMs && (uint32_t)(I->transport->millis() - I->lastRxMs) >= I->linkTimeoutMs )
		{
			// nothing from the client for too long (a link that cannot
			// report loss, like a UART): behave as if it disconnected
			onDisconnect( I );
			I->resume = true;
			break;
		}
		I->transport->idle();
	}

	I->resume = false;
	I->state = WRD_StateRunning;
	I->lastPollMs = I->transport->millis();
	resetHandles( I );
	refreshWatches( I );

	// not one meant for a yielded call of another context
	if ( I->abortPending && (!I->abortContext || I->abortContext == I->stopContext) )
	{
		I->abortPending = false;
		I->abortContext = 0;
		return true;
	}
	return false;
}

// inside the VM: service the link until a client says hello. Requests that
// need an idle VM (upload, launch) are refused meanwhile. False when waitMs
// (0 waits forever) ran out first
bool WrenchDebugServerImpl::waitForClient( WrenchDebugServerPrivate* I, uint32_t waitMs )
{
	uint32_t start = I->transport->millis();
	bool wasInHook = I->inHook;
	I->inHook = true;
	while ( !I->helloReceived )
	{
		pump( I );
		if ( I->helloReceived )
		{
			break;
		}
		if ( waitMs && (uint32_t)(I->transport->millis() - start) >= waitMs )
		{
			break;
		}
		I->transport->idle();
	}
	I->inHook = wasInHook;
	return I->helloReceived;
}

void WrenchDebugServerImpl::process( WrenchDebugServerPrivate* I, const WrenchDebugFrame& frame )
{
	WrenchDebugReader in( frame.payload, frame.length );
	uint8_t seq = frame.seq;
	bool inVm = I->inHook || I->state == WRD_StateStopped;
	bool stopped = (I->state == WRD_StateStopped);

	// a retransmitted control request (its reply was lost) must not act
	// twice. A retransmission repeats the request right before it, same type
	// and sequence number; any other request in between makes it a new one
	// (the 8 bit sequence numbers wrap)
	bool control = frame.type == WRD_MsgLaunch || frame.type == WRD_MsgContinue || frame.type == WRD_MsgPause
		|| frame.type == WRD_MsgStepIn || frame.type == WRD_MsgStepOver || frame.type == WRD_MsgStepOut
		|| frame.type == WRD_MsgTerminate || frame.type == WRD_MsgLoadEnd || frame.type == WRD_MsgDetach;
	bool repeat = seq != 0 && seq == I->lastRequestSeq && frame.type == I->lastRequestType;
	I->lastRequestSeq = seq;
	I->lastRequestType = frame.type;
	I->processingControl = false;
	if ( control )
	{
		if ( repeat )
		{
			if ( I->lastControlReply )
			{
				replyError( I, seq, I->lastControlReply );
			}
			else
			{
				replyOk( I, seq );
			}
			return;
		}
		I->processingControl = true;
	}

	if ( !I->helloReceived && frame.type != WRD_MsgHello && frame.type != WRD_MsgPing )
	{
		replyError( I, seq, WRD_ErrBadRequest );
		return;
	}

	switch ( frame.type )
	{
		case WRD_MsgHello:
		{
			uint16_t version = in.u16();
			if ( version != WRD_PROTOCOL_VERSION )
			{
				replyError( I, seq, WRD_ErrVersion );
				return;
			}

			I->helloReceived = true;
			I->breakpointCount = 0;
			clearWatches( I, false, false );
			I->stepMode = WRD_StepNone;
			I->lastRequestSeq = 0;
			I->lastRequestType = 0;
			I->tickFlags |= WRD_TickClientAttached;

			uint8_t caps = WRD_CapSetValue | WRD_CapWatch;
			if ( I->launchable )
			{
				caps |= WRD_CapLaunch;
				if ( !I->uploadDisabled )
				{
					caps |= WRD_CapLoad;
				}
			}
			if ( I->hostDriven )
			{
				caps |= WRD_CapHostDriven;
			}

			WrenchDebugWriter out( payload(I), WRENCH_DEBUG_MAX_PAYLOAD );
			out.u16( WRD_PROTOCOL_VERSION );
			out.u8( WRENCH_VERSION_MAJOR );
			out.u8( WRENCH_VERSION_MINOR );
			out.u8( WRENCH_VERSION_BUILD );
			out.u16( WRENCH_DEBUG_MAX_PAYLOAD );
			out.u8( caps );
			out.u16( WRENCH_DEBUG_MAX_BREAKPOINTS );
			out.u8( WRENCH_DEBUG_MAX_WATCHES );
			out.u8( WRENCH_DEBUG_MAX_PROGRAMS );
			send( I, WRD_MsgReplyOk, seq, out.size() );

			if ( stopped )
			{
				// a client attaching to a halted target must learn about it
				announceStop( I );
			}
			return;
		}

		case WRD_MsgPing:
		{
			replyOk( I, seq );
			return;
		}

		case WRD_MsgGetInfo:
		{
			// the state, and program 0
			const WrenchDebugProgramSlot& p = I->programs[0];
			WrenchDebugWriter out( payload(I), WRENCH_DEBUG_MAX_PAYLOAD );
			out.u8( I->state );
			out.u32( p.programHash );
			out.u32( p.bytesLen );
			out.u8( p.compilerFlags );
			out.u32( p.symbolsLen );
			out.u32( p.sourceLen );
			out.u16( p.context ? p.context->globals : 0 );
			out.u8( p.context ? p.context->numLocalFunctions : 0 );
			out.i32( I->lastLine );
			send( I, WRD_MsgReplyOk, seq, out.size() );
			return;
		}

		case WRD_MsgGetPrograms:
		{
			// u8 n, then each: u8 id, u8 flags, u8 importer, u32 hash, u32
			// bytecode length, u8 compiler flags, u32 symbols length, u32 source
			// length, u16 globals, u8 functions, u8 name length, name
			WrenchDebugWriter out( payload(I), WRENCH_DEBUG_MAX_PAYLOAD );
			uint8_t* countAt = out.reserve( 1 );
			uint8_t count = 0;
			for ( int n = 0; n < WRENCH_DEBUG_MAX_PROGRAMS; ++n )
			{
				const WrenchDebugProgramSlot& p = I->programs[n];
				if ( !p.context )
				{
					continue;
				}
				uint8_t flags = 0;
				if ( p.importer != WRD_NoProgram )
				{
					flags |= WRD_ProgramImported;
				}
				if ( p.context->stopLocation || (n == 0 && I->contextRan) )
				{
					flags |= WRD_ProgramRan;
				}
				uint8_t nameLen = (uint8_t)strlen( p.name );
				out.u8( (uint8_t)n );
				out.u8( flags );
				out.u8( p.importer );
				out.u32( p.programHash );
				out.u32( p.bytesLen );
				out.u8( p.compilerFlags );
				out.u32( p.symbolsLen );
				out.u32( p.sourceLen );
				out.u16( p.context->globals );
				out.u8( p.context->numLocalFunctions );
				out.u8( nameLen );
				out.bytes( p.name, nameLen );
				++count;
			}
			if ( countAt )
			{
				countAt[0] = count;
			}
			send( I, WRD_MsgReplyOk, seq, out.size() );
			return;
		}

		case WRD_MsgLoadBegin:
		{
			uint32_t size = in.u32();
			if ( I->uploadDisabled || !I->launchable )
			{
				replyError( I, seq, WRD_ErrUploadDisabled );
				return;
			}
			if ( in.bad() || size < 8 )
			{
				replyError( I, seq, WRD_ErrBadRequest );
				return;
			}
			if ( inVm )
			{
				replyError( I, seq, WRD_ErrBusy );
				return;
			}

			// a new transfer replaces one the firmware has not taken yet
			g_free( I->upload );
			I->upload = (uint8_t*)g_malloc( size );
			I->uploadSize = I->upload ? size : 0;
			I->uploadReceived = 0;
			I->uploadReady = false;
			if ( !I->upload )
			{
				replyError( I, seq, WRD_ErrNoMemory );
				return;
			}
			replyOk( I, seq );
			return;
		}

		case WRD_MsgLoadData:
		{
			uint32_t offset = in.u32();
			uint32_t len = in.remaining();
			const uint8_t* data = in.bytes( len );
			if ( I->uploadDisabled || !I->launchable )
			{
				replyError( I, seq, WRD_ErrUploadDisabled );
				return;
			}
			if ( in.bad() || !I->upload || I->uploadReady || offset > I->uploadSize || len > I->uploadSize - offset )
			{
				replyError( I, seq, WRD_ErrBadRequest );
				return;
			}
			memcpy( I->upload + offset, data, len );
			if ( offset + len > I->uploadReceived )
			{
				I->uploadReceived = offset + len;
			}
			replyOk( I, seq );
			return;
		}

		case WRD_MsgLoadEnd:
		{
			if ( I->uploadDisabled || !I->launchable )
			{
				replyError( I, seq, WRD_ErrUploadDisabled );
				return;
			}
			if ( inVm )
			{
				replyError( I, seq, WRD_ErrBusy );
				return;
			}
			uint32_t hash = 0;
			if ( !I->upload || I->uploadReady || I->uploadReceived != I->uploadSize
					|| !wr_isBytecodeValid(I->upload, I->uploadSize, &hash) )
			{
				g_free( I->upload );
				I->upload = 0;
				I->uploadSize = 0;
				I->uploadReady = false;
				replyError( I, seq, WRD_ErrBadBytecode );
				return;
			}

			// the old program 0 is done with: the client sets the breakpoints of
			// the new one right after this reply, they wait for the context the
			// firmware makes of the upload (the next one attached is program 0)
			releaseProgram( I, 0 );
			I->uploadReady = true;
			I->tickFlags |= WRD_TickProgramUploaded;

			WrenchDebugWriter out( payload(I), WRENCH_DEBUG_MAX_PAYLOAD );
			out.u32( hash );
			send( I, WRD_MsgReplyOk, seq, out.size() );
			return;
		}

		case WRD_MsgReadProgram:
		{
			uint8_t program = in.u8();
			uint8_t section = in.u8();
			uint32_t offset = in.u32();
			uint16_t maxLen = in.u16();
			const uint8_t* base = 0;
			uint32_t len = 0;
			const WrenchDebugProgramSlot* p = (program < WRENCH_DEBUG_MAX_PROGRAMS) ? I->programs + program : 0;
			if ( !p || !p->context )
			{
				p = 0;
			}
			else if ( section == WRD_SectionBytecode )
			{
				base = p->bytes;
				len = p->bytesLen;
			}
			else if ( section == WRD_SectionSymbols )
			{
				base = p->symbols;
				len = p->symbolsLen;
			}
			else if ( section == WRD_SectionSource )
			{
				base = p->source;
				len = p->sourceLen;
			}

			if ( in.bad() || (!p && program != 0) )
			{
				replyError( I, seq, WRD_ErrBadRequest );
				return;
			}

			uint32_t n = 0;
			if ( base && offset < len )
			{
				n = len - offset;
				if ( n > maxLen )
				{
					n = maxLen;
				}
				if ( n > WRENCH_DEBUG_MAX_PAYLOAD - 6 )
				{
					n = WRENCH_DEBUG_MAX_PAYLOAD - 6;
				}
			}

			WrenchDebugWriter out( payload(I), WRENCH_DEBUG_MAX_PAYLOAD );
			out.u32( len );
			out.u16( (uint16_t)n );
			uint8_t* dest = out.reserve( n );
			for ( uint32_t i = 0; dest && i < n; ++i )
			{
				// the image may live in flash that needs special reads
				dest[i] = (uint8_t)READ_8_FROM_PC( base + offset + i );
			}
			send( I, WRD_MsgReplyOk, seq, out.size() );
			return;
		}

		case WRD_MsgSetBreakpoints:
		{
			// replaces one program's breakpoints, the others' stay (parked
			// ones make room when there is none)
			uint8_t program = in.u8();
			uint16_t count = in.u16();
			uint16_t others = 0;
			uint16_t parked = 0;
			for ( uint16_t b = 0; b < I->breakpointCount; ++b )
			{
				parked += (I->breakpoints[b].program == WRD_NoProgram) ? 1 : 0;
				others += (I->breakpoints[b].program != program) ? 1 : 0;
			}
			if ( count > WRENCH_DEBUG_MAX_BREAKPOINTS - (others - parked) )
			{
				replyError( I, seq, WRD_ErrTooManyBreakpoints );
				return;
			}

			int32_t lines[WRENCH_DEBUG_MAX_BREAKPOINTS];
			for ( uint16_t i = 0; i < count; ++i )
			{
				lines[i] = in.i32();
			}
			if ( in.bad() || program >= WRENCH_DEBUG_MAX_PROGRAMS )
			{
				replyError( I, seq, WRD_ErrBadRequest );
				return;
			}

			int drop = (int)count - (WRENCH_DEBUG_MAX_BREAKPOINTS - others);
			uint16_t kept = 0;
			for ( uint16_t b = 0; b < I->breakpointCount; ++b )
			{
				const WrenchDebugBreakpoint& bp = I->breakpoints[b];
				if ( bp.program == WRD_NoProgram && drop > 0 )
				{
					--drop;
				}
				else if ( bp.program != program )
				{
					I->breakpoints[kept++] = bp;
				}
			}
			for ( uint16_t i = 0; i < count; ++i )
			{
				I->breakpoints[kept].program = program;
				I->breakpoints[kept].parkedHash = 0;
				I->breakpoints[kept++].line = lines[i];
			}
			I->breakpointCount = kept;
			replyOk( I, seq );
			return;
		}

		case WRD_MsgAddWatch:
		{
			addWatch( I, seq, in );
			return;
		}

		case WRD_MsgRemoveWatch:
		{
			uint8_t id = in.u8();
			if ( in.bad() || id > WRENCH_DEBUG_MAX_WATCHES )
			{
				replyError( I, seq, WRD_ErrBadRequest );
				return;
			}
			if ( id == 0 )
			{
				clearWatches( I, false, false );
			}
			else
			{
				removeWatch( I, id - 1, false );
			}
			replyOk( I, seq );
			return;
		}

		case WRD_MsgLaunch:
		{
			uint8_t flags = in.u8();
			if ( !I->launchable )
			{
				replyError( I, seq, WRD_ErrUnsupported );
				return;
			}
			if ( inVm )
			{
				replyError( I, seq, WRD_ErrBusy );
				return;
			}
			if ( !I->programs[0].context && !I->uploadReady )
			{
				replyError( I, seq, WRD_ErrNoProgram );
				return;
			}

			// the firmware runs it when tick() returns; a stop on entry halts
			// its first line (not one of another program the firmware runs)
			I->entryBreak = (flags & WRD_LaunchStopOnEntry) != 0;
			I->entryProgram = 0;
			I->entryWaitMs = 0;
			I->stepMode = WRD_StepNone;
			I->abortPending = false;
			I->abortContext = 0;
			I->lastLine = -1;
			I->launched = true;
			I->tickFlags |= WRD_TickRunRequested;
			replyOk( I, seq );
			return;
		}

		case WRD_MsgContinue:
		{
			uint8_t flags = in.remaining() ? in.u8() : 0;
			if ( (flags & WRD_ContinueResumeStep) && I->interruptedStepMode != WRD_StepNone )
			{
				// the client looked at a breakpoint inside a step and let it
				// go: the step carries on
				I->stepMode = I->interruptedStepMode;
				I->stepKey = I->interruptedStepKey;
				I->stepStack = I->interruptedStepStack;
			}
			else
			{
				I->stepMode = WRD_StepNone;
			}
			I->interruptedStepMode = WRD_StepNone;
			I->resume = stopped;
			replyOk( I, seq );
			return;
		}

		case WRD_MsgPause:
		{
			replyOk( I, seq );
			if ( !stopped )
			{
				I->stepMode = WRD_StepPause;
			}
			else
			{
				// already halted: say so again, the first event may be lost
				announceStop( I );
			}
			return;
		}

		case WRD_MsgStepIn:
		case WRD_MsgStepOver:
		case WRD_MsgStepOut:
		{
			if ( !stopped )
			{
				replyError( I, seq, WRD_ErrNotStopped );
				return;
			}
			I->stepMode = (frame.type == WRD_MsgStepIn) ? WRD_StepIn
				: (frame.type == WRD_MsgStepOver) ? WRD_StepOver : WRD_StepOut;
			I->stepKey = wr_debugDepthKey( I->stopContext, I->stopFrameBase );
			I->stepStack = I->stopContext->stack;
			I->interruptedStepMode = WRD_StepNone;
			I->resume = true;
			replyOk( I, seq );
			return;
		}

		case WRD_MsgTerminate:
		{
			// a run not started yet is off; script code that runs (or is halted)
			// ends on its next line; a call of program 0 that yielded, its
			// global code or a function the firmware called, ends on its next
			// line once the firmware continues it (wr_continue() returns 0 with
			// WR_ERR_debugger_terminated)
			if ( I->tickFlags & WRD_TickRunRequested )
			{
				I->tickFlags &= ~WRD_TickRunRequested;
				I->launched = false;
			}
			WRContext* main = I->programs[0].context;
			if ( inVm )
			{
				I->abortPending = true;
				I->abortContext = 0;
				I->resume = stopped;
			}
			else if ( main && main->yield_pc )
			{
				I->abortPending = true;
				I->abortContext = main;
			}
			replyOk( I, seq );
			return;
		}

		case WRD_MsgDetach:
		{
			I->breakpointCount = 0;
			clearWatches( I, false, false );
			I->stepMode = WRD_StepNone;
			I->resume = stopped;
			replyOk( I, seq );
			I->helloReceived = false;
			return;
		}

		case WRD_MsgGetStack:
		{
			uint16_t first = in.u16();
			uint16_t maxFrames = in.u16();
			if ( !stopped )
			{
				replyError( I, seq, WRD_ErrNotStopped );
				return;
			}

			WrenchDebugWriter out( payload(I), WRENCH_DEBUG_MAX_PAYLOAD );
			uint8_t* totalAt = out.reserve( 2 );
			uint8_t* countAt = out.reserve( 2 );
			uint16_t total = 0;
			uint16_t written = 0;

			WrenchDebugFrameWalker walker( I, I->stopContext, I->stopPc, I->stopFrameBase );
			WrenchDebugFrameInfo info;
			while ( walker.next(info) && total < 0xFFFF )
			{
				if ( total >= first && written < maxFrames && out.remaining() >= 16 )
				{
					// a frame of an imported module names that program
					int program = programOf( I, info.context );
					out.u8( info.kind );
					out.u8( info.function );
					out.u32( info.pcOffset );
					out.u16( info.frameBase ? (uint16_t)(info.frameBase - info.context->stack) : 0xFFFF );
					out.u16( (info.kind == WRD_FrameFunction)
									? (uint16_t)(info.context->localFunctions[info.function].frameBaseAdjustment - 1) : 0 );
					out.u8( program < 0 ? WRD_NoProgram : (uint8_t)program );
					++written;
				}
				++total;
			}

			totalAt[0] = (uint8_t)total;
			totalAt[1] = (uint8_t)(total >> 8);
			countAt[0] = (uint8_t)written;
			countAt[1] = (uint8_t)(written >> 8);
			send( I, WRD_MsgReplyOk, seq, out.size() );
			return;
		}

		case WRD_MsgGetLocals:
		{
			uint16_t frameIndex = in.u16();
			uint16_t first = in.u16();
			uint16_t count = in.u16();
			WrenchDebugFrameInfo info;
			if ( !stopped )
			{
				replyError( I, seq, WRD_ErrNotStopped );
				return;
			}
			if ( in.bad() || !frameAt(I, frameIndex, info) )
			{
				replyError( I, seq, WRD_ErrBadFrame );
				return;
			}

			uint16_t slots = (info.kind == WRD_FrameFunction)
											? (uint16_t)(info.context->localFunctions[info.function].frameBaseAdjustment - 1) : 0;

			WrenchDebugWriter out( payload(I), WRENCH_DEBUG_MAX_PAYLOAD );
			out.u16( slots );
			uint8_t* countAt = out.reserve( 2 );
			uint16_t written = 0;
			for ( uint32_t s = first; s < slots && written < count; ++s )
			{
				uint32_t mark = out.size();
				writeValue( I, out, info.frameBase[s] );
				if ( out.overflow() )
				{
					out.truncate( mark );
					break;
				}
				++written;
			}
			countAt[0] = (uint8_t)written;
			countAt[1] = (uint8_t)(written >> 8);
			send( I, WRD_MsgReplyOk, seq, out.size() );
			return;
		}

		case WRD_MsgGetGlobals:
		{
			uint8_t program = in.u8();
			uint16_t first = in.u16();
			uint16_t count = in.u16();
			WRContext* context = (program < WRENCH_DEBUG_MAX_PROGRAMS) ? I->programs[program].context : 0;
			if ( in.bad() )
			{
				replyError( I, seq, WRD_ErrBadRequest );
				return;
			}
			if ( !context || (I->state == WRD_StateRunning) )
			{
				replyError( I, seq, context ? WRD_ErrNotStopped : WRD_ErrNoProgram );
				return;
			}

			const WRValue* globals = (const WRValue*)(context + 1);
			WrenchDebugWriter out( payload(I), WRENCH_DEBUG_MAX_PAYLOAD );
			out.u16( context->globals );
			uint8_t* countAt = out.reserve( 2 );
			uint16_t written = 0;
			for ( uint32_t s = first; s < context->globals && written < count; ++s )
			{
				uint32_t mark = out.size();
				writeValue( I, out, globals[s] );
				if ( out.overflow() )
				{
					out.truncate( mark );
					break;
				}
				++written;
			}
			countAt[0] = (uint8_t)written;
			countAt[1] = (uint8_t)(written >> 8);
			send( I, WRD_MsgReplyOk, seq, out.size() );
			return;
		}

		case WRD_MsgGetChildren:
		{
			if ( I->state == WRD_StateRunning )
			{
				replyError( I, seq, WRD_ErrNotStopped );
				return;
			}
			getChildren( I, seq, in );
			return;
		}

		case WRD_MsgSetLocal:
		case WRD_MsgSetGlobal:
		case WRD_MsgSetChild:
		{
			WRValue* slot = 0;
			// strings written are made in the context the value belongs to
			WRContext* context = stopped ? I->stopContext : I->programs[0].context;
			if ( I->state == WRD_StateRunning )
			{
				replyError( I, seq, WRD_ErrNotStopped );
				return;
			}

			if ( frame.type == WRD_MsgSetLocal )
			{
				uint16_t frameIndex = in.u16();
				uint16_t index = in.u16();
				WrenchDebugFrameInfo info;
				if ( !frameAt(I, frameIndex, info) || info.kind != WRD_FrameFunction )
				{
					replyError( I, seq, WRD_ErrBadFrame );
					return;
				}
				if ( index >= info.context->localFunctions[info.function].frameBaseAdjustment - 1 )
				{
					replyError( I, seq, WRD_ErrBadSlot );
					return;
				}
				slot = info.frameBase + index;
				context = info.context;
			}
			else if ( frame.type == WRD_MsgSetGlobal )
			{
				uint8_t program = in.u8();
				uint16_t index = in.u16();
				context = (program < WRENCH_DEBUG_MAX_PROGRAMS) ? I->programs[program].context : 0;
				if ( !context )
				{
					replyError( I, seq, WRD_ErrNoProgram );
					return;
				}
				if ( index >= context->globals )
				{
					replyError( I, seq, WRD_ErrBadSlot );
					return;
				}
				slot = ((WRValue*)(context + 1)) + index;
			}
			else
			{
				uint32_t handle = in.u32();
				uint32_t index = in.u32();
				WrenchDebugHandle* h = findHandle( I, handle );
				if ( !h )
				{
					replyError( I, seq, WRD_ErrBadHandle );
					return;
				}
				slot = childSlot( I, h, index );
				if ( !slot )
				{
					replyError( I, seq, WRD_ErrBadSlot );
					return;
				}
			}

			WRValue* target = storageOf( slot );
			if ( !target )
			{
				replyError( I, seq, WRD_ErrUnsupported );
				return;
			}

			WRValue value;
			if ( !readValue(I, in, &value, context) )
			{
				replyError( I, seq, WRD_ErrBadRequest );
				return;
			}
			*target = value;

			WrenchDebugWriter out( payload(I), WRENCH_DEBUG_MAX_PAYLOAD );
			writeValue( I, out, *target );
			send( I, WRD_MsgReplyOk, seq, out.size() );
			return;
		}

		default:
		{
			replyError( I, seq, WRD_ErrUnknownRequest );
			return;
		}
	}
}

WrenchDebugServer::WrenchDebugServer( WRState* w, WrenchDebugTransport* transport )
{
	I = (WrenchDebugServerPrivate*)g_malloc( sizeof(WrenchDebugServerPrivate) );
	if ( !I )
	{
		return;
	}
	memset( (char*)I, 0, sizeof(WrenchDebugServerPrivate) );
	I->parent = this;
	I->w = w;
	I->transport = transport;
	WrenchDebugServerImpl::init( I );
}

WrenchDebugServer::~WrenchDebugServer()
{
	if ( !I )
	{
		return;
	}
	detach();
	g_free( I->upload );
	g_free( I );
}

int WrenchDebugServer::tick()
{
	if ( !I )
	{
		return 0;
	}

	if ( I->state == WRD_StateRunning )
	{
		// script code left the VM since we last looked, anything handed out
		// before may have been changed or collected meanwhile
		I->state = WRD_StateLoaded;
		WrenchDebugServerImpl::resetHandles( I );
	}

	I->stopContext = 0;
	I->importCallDepth = 0; // no script runs, so no import call is under way
	WrenchDebugServerImpl::pump( I );

	int flags = I->tickFlags;
	I->tickFlags = 0;
	return flags;
}

void WrenchDebugServer::poll()
{
	// inside the VM, like the polling between script lines: requests that
	// need an idle VM (upload, launch) are answered busy
	if ( !I || I->inHook || I->state == WRD_StateStopped )
	{
		return;
	}
	I->lastPollMs = I->transport->millis();
	I->inHook = true;
	WrenchDebugServerImpl::pump( I );
	I->inHook = false;
}

int WrenchDebugServer::attach( WRContext* context, const char* name )
{
	if ( !I || !context )
	{
		return -1;
	}

	// the first context attached is program 0, and the first after an upload
	// released it (the one the firmware made of the upload)
	int program = WrenchDebugServerImpl::programOf( I, context );
	if ( program < 0 && !I->programs[0].context )
	{
		WrenchDebugProgramSlot& p = I->programs[0];
		memset( (char*)&p, 0, sizeof(p) );
		p.context = context;
		p.bytes = context->bottom;
		p.bytesLen = (uint32_t)context->bottomSize;
		p.importer = WRD_NoProgram;
		for ( int c = 0; name && name[c] && c < WRENCH_DEBUG_PROGRAM_NAME - 1; ++c )
		{
			p.name[c] = name[c];
		}
		context->debugServer = this;
		WrenchDebugServerImpl::readProgramInfo( p );
		I->state = WRD_StateLoaded;
		I->contextRan = context->stopLocation || context->yield_pc;
		I->lastLine = -1;

		if ( I->helloReceived )
		{
			WrenchDebugWriter out( WrenchDebugServerImpl::payload(I), WRENCH_DEBUG_MAX_PAYLOAD );
			out.u32( p.programHash );
			WrenchDebugServerImpl::send( I, WRD_MsgEventLoaded, 0, out.size() );
		}
		return 0;
	}
	if ( program < 0 )
	{
		program = WrenchDebugServerImpl::addProgram( I, context, WRD_NoProgram, name );
	}
	if ( program >= 0 && I->state == WRD_StateNoProgram )
	{
		I->state = WRD_StateLoaded;
	}
	return program;
}

void WrenchDebugServer::detach( WRContext* context )
{
	int program = I ? WrenchDebugServerImpl::programOf( I, context ) : -1;
	if ( program >= 0 )
	{
		WrenchDebugServerImpl::releaseProgram( I, program );
	}
}

void WrenchDebugServer::detach()
{
	if ( !I )
	{
		return;
	}
	for ( int p = WRENCH_DEBUG_MAX_PROGRAMS - 1; p >= 0; --p )
	{
		if ( I->programs[p].context || p == 0 )
		{
			WrenchDebugServerImpl::releaseProgram( I, p );
		}
	}
}

void WrenchDebugServer::onImport( WRContext* importer, WRContext* import )
{
	int from = I ? WrenchDebugServerImpl::programOf( I, importer ) : -1;
	if ( from >= 0 )
	{
		// without a free slot the module just runs undebugged
		WrenchDebugServerImpl::addProgram( I, import, (uint8_t)from, 0 );
	}
}

void WrenchDebugServer::onImportCall( WRContext* caller, const unsigned char* pc, WRValue* frameBase, WRContext* callee )
{
	if ( !I )
	{
		return;
	}
	if ( I->importCallDepth < WRENCH_DEBUG_MAX_IMPORT_CALLS )
	{
		WrenchDebugImportCall& call = I->importCalls[I->importCallDepth];
		call.caller = caller;
		call.pc = pc;
		call.frameBase = frameBase;
		call.callee = callee;
	}
	if ( I->importCallDepth < 0xFF )
	{
		++I->importCallDepth;
	}
}

void WrenchDebugServer::onImportReturn()
{
	if ( I && I->importCallDepth )
	{
		--I->importCallDepth;
	}
}

void WrenchDebugServer::onContextDestroyed( WRContext* context )
{
	int program = I ? WrenchDebugServerImpl::programOf( I, context ) : -1;
	if ( program >= 0 )
	{
		WrenchDebugServerImpl::releaseProgram( I, program );
	}
}

void WrenchDebugServer::onContextReset( WRContext* context )
{
	int program = I ? WrenchDebugServerImpl::programOf( I, context ) : -1;
	if ( program < 0 )
	{
		return;
	}
	// its globals start over (watches on them follow) and the values handed
	// out may be gone; a yielded call it had is gone, a terminate for it too
	if ( I->abortContext == context )
	{
		I->abortPending = false;
		I->abortContext = 0;
	}
	WrenchDebugServerImpl::resetHandles( I );
	WrenchDebugServerImpl::clearWatches( I, true, true );
	WrenchDebugServerImpl::refreshWatches( I );
	if ( program == 0 )
	{
		I->contextRan = false;
		I->state = WRD_StateLoaded;
	}
}

void WrenchDebugServer::onGlobalCodeEnd( WRContext* context, WRValue* ret )
{
	// the end of program 0's run, whoever started it
	if ( !I || WrenchDebugServerImpl::programOf(I, context) != 0 )
	{
		return;
	}

	I->state = WRD_StateEnded;
	I->stepMode = WRD_StepNone;
	I->entryBreak = false;
	I->abortPending = false;
	I->abortContext = 0;
	WrenchDebugServerImpl::resetHandles( I );
	bool launched = I->launched;
	I->launched = false;
	if ( !I->helloReceived && !launched )
	{
		return;
	}

	uint8_t err = I->w->err;
	WrenchDebugWriter out( WrenchDebugServerImpl::payload(I), WRENCH_DEBUG_MAX_PAYLOAD );
	out.u8( err );
	uint8_t flags = 0;
	if ( err == WR_ERR_debugger_terminated )
	{
		flags |= WRD_EndedAborted;
	}
	if ( I->hostDriven )
	{
		flags |= WRD_EndedHostDriven;
	}
	out.u8( flags );
	out.i32( I->lastLine );
	out.u32( WrenchDebugServerImpl::missingFunction(I, err) );
	if ( ret )
	{
		WrenchDebugServerImpl::writeValue( I, out, *ret );
	}
	else
	{
		WRValue zero;
		zero.init();
		WrenchDebugServerImpl::writeValue( I, out, zero );
	}
	WrenchDebugServerImpl::send( I, WRD_MsgEventEnded, 0, out.overflow() ? 10 : out.size() );
}

void WrenchDebugServer::setHostDriven( const bool hostDriven )
{
	if ( I )
	{
		I->hostDriven = hostDriven;
	}
}

void WrenchDebugServer::breakOnEntry( const uint32_t waitMs )
{
	if ( I )
	{
		I->entryBreak = true;
		I->entryProgram = WRD_NoProgram;
		I->entryWaitMs = waitMs;
	}
}

void WrenchDebugServer::setLaunchable( const bool launchable )
{
	if ( I )
	{
		I->launchable = launchable;
	}
}

bool WrenchDebugServer::takeUpload( uint8_t*& bytes, int& size )
{
	if ( !I || !I->uploadReady )
	{
		return false;
	}
	bytes = I->upload;
	size = (int)I->uploadSize;
	I->upload = 0;
	I->uploadSize = 0;
	I->uploadReceived = 0;
	I->uploadReady = false;
	return true;
}

void WrenchDebugServer::setLinkTimeout( const uint32_t ms )
{
	if ( I )
	{
		I->linkTimeoutMs = ms;
	}
}

void WrenchDebugServer::setAllowUpload( const bool allow )
{
	if ( !I )
	{
		return;
	}
	I->uploadDisabled = !allow;
	if ( !allow )
	{
		// drop a transfer in progress
		g_free( I->upload );
		I->upload = 0;
		I->uploadSize = 0;
		I->uploadReceived = 0;
		I->uploadReady = false;
	}
}

bool WrenchDebugServer::clientAttached() const
{
	return I && I->helloReceived;
}

void WrenchDebugServer::setAbortOnDisconnect( const bool abort )
{
	if ( I )
	{
		I->abortOnDisconnect = abort;
	}
}

void WrenchDebugServer::print( const char* text, const int len )
{
	if ( !I || !I->helloReceived || !text )
	{
		return;
	}

	uint32_t remaining = (len < 0) ? (uint32_t)strlen( text ) : (uint32_t)len;
	while ( remaining )
	{
		uint32_t n = remaining > WRENCH_DEBUG_MAX_PAYLOAD ? WRENCH_DEBUG_MAX_PAYLOAD : remaining;
		memcpy( WrenchDebugServerImpl::payload(I), text, n );
		WrenchDebugServerImpl::send( I, WRD_MsgEventOutput, 0, n );
		text += n;
		remaining -= n;
	}
}

bool WrenchDebugServer::onDebugInfo( WRContext* context, const unsigned char* pc, WRValue* frameBase, WRValue* stackTop )
{
	uint16_t codeword = (uint16_t)READ_16_FROM_PC( pc );
	if ( (codeword & WRD_TypeMask) == WRD_LineHigh )
	{
		I->lineHigh = (int32_t)(codeword & WRD_PayloadMask) << 14;
		return false;
	}
	if ( (codeword & WRD_TypeMask) != WRD_LineNumber )
	{
		return false;
	}

	int32_t line = (codeword & WRD_PayloadMask) | I->lineHigh;
	I->lineHigh = 0;
	int program = WrenchDebugServerImpl::programOf( I, context );
	if ( program < 0 )
	{
		return false; // not one of ours (any more)
	}
	I->lastLine = line;
	if ( I->state != WRD_StateStopped && I->state != WRD_StateRunning )
	{
		// script code runs again (firmware calling in): values handed out
		// while it was idle may be changed or collected from here on
		WrenchDebugServerImpl::resetHandles( I );
		I->state = WRD_StateRunning;
		I->contextRan = true;
	}

	uint8_t reason = WRD_StopStep;
	bool stop = false;

	// a watched value changed since the previous line marker: stop on the
	// line after the one that wrote it
	if ( I->watchCount && I->helloReceived )
	{
		uint8_t hit = WrenchDebugServerImpl::checkWatches( I, context, pc, frameBase );
		if ( hit )
		{
			stop = true;
			reason = WRD_StopWatch;
			I->watchHit = hit;
		}
	}

	if ( !stop && I->stepMode != WRD_StepNone )
	{
		// a step goes on in its own "thread": the stack it started on (an
		// imported module shares its importer's); other programs on stacks of
		// their own run past it
		bool sameStack = context->stack == I->stepStack;
		uint32_t key = wr_debugDepthKey( context, frameBase );
		switch ( I->stepMode )
		{
			case WRD_StepIn: stop = sameStack; break;
			case WRD_StepOver: stop = sameStack && key <= I->stepKey; break;
			case WRD_StepOut: stop = sameStack && key < I->stepKey; break;
			case WRD_StepPause: stop = true; reason = WRD_StopPause; break;
			case WRD_StepEntry: stop = true; reason = WRD_StopEntry; break;
			default: break;
		}
	}

	if ( !stop )
	{
		for ( uint16_t b = 0; b < I->breakpointCount; ++b )
		{
			if ( I->breakpoints[b].line == line && I->breakpoints[b].program == program )
			{
				stop = true;
				reason = WRD_StopBreakpoint;
				break;
			}
		}
	}

	if ( I->entryBreak && (I->entryProgram == WRD_NoProgram || I->entryProgram == program) )
	{
		// breakOnEntry() or a launch: halt here, first waiting for a debugger
		// if needed
		I->entryBreak = false;
		if ( I->helloReceived || WrenchDebugServerImpl::waitForClient(I, I->entryWaitMs) )
		{
			stop = true;
			reason = WRD_StopEntry;
		}
	}

	// the link, every WRENCH_DEBUG_POLL_MS however long lines take; the clock
	// is read only every few lines, which keeps short lines cheap
	if ( !stop && ++I->pollCounter >= WRENCH_DEBUG_POLL_LINES )
	{
		I->pollCounter = 0;
		uint32_t now = I->transport->millis();
		if ( (uint32_t)(now - I->lastPollMs) >= WRENCH_DEBUG_POLL_MS )
		{
			I->lastPollMs = now;
			I->inHook = true;
			WrenchDebugServerImpl::pump( I );
			I->inHook = false;
			if ( I->stepMode == WRD_StepPause )
			{
				stop = true;
				reason = WRD_StopPause;
			}
		}
	}

	// a terminate for a yielded call waits for that call to go on
	if ( I->abortPending && (!I->abortContext || I->abortContext == context) )
	{
		I->abortPending = false;
		I->abortContext = 0;
		return true;
	}

	if ( !stop || !I->helloReceived )
	{
		if ( stop && reason != WRD_StopBreakpoint )
		{
			I->stepMode = WRD_StepNone;
		}
		return false;
	}

	// a breakpoint or watch inside a step over/out: remember the step, the
	// client may decide (condition, hit count, logpoint) not to stop here
	// after all
	if ( (reason == WRD_StopBreakpoint || reason == WRD_StopWatch) && (I->stepMode == WRD_StepOver || I->stepMode == WRD_StepOut) )
	{
		I->interruptedStepMode = I->stepMode;
		I->interruptedStepKey = I->stepKey;
		I->interruptedStepStack = I->stepStack;
	}
	else
	{
		I->interruptedStepMode = WRD_StepNone;
	}

	I->stopContext = context;
	I->stopProgram = (uint8_t)program;
	I->stopPc = pc;
	I->stopFrameBase = frameBase;
	I->stopStackTop = stackTop;

	bool abort = WrenchDebugServerImpl::stopLoop( I, reason, 0 );

	I->stopPc = 0;
	return abort;
}

void WrenchDebugServer::onError( WRContext* context, const unsigned char* pc, WRValue* frameBase, WRValue* stackTop )
{
	if ( !I || !I->helloReceived || I->state == WRD_StateStopped )
	{
		return;
	}

	int program = WrenchDebugServerImpl::programOf( I, context );
	if ( program < 0 )
	{
		return;
	}
	uint8_t err = I->w->err;
	I->stopContext = context;
	I->stopProgram = (uint8_t)program;
	I->stopPc = pc;
	I->stopFrameBase = frameBase;
	I->stopStackTop = stackTop;

	WrenchDebugServerImpl::stopLoop( I, WRD_StopError, err );

	I->stopPc = 0;
	I->stepMode = WRD_StepNone;
	I->w->err = err; // the VM is failing either way
}

#endif
