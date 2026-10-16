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

// "wrench dap": Debug Adapter Protocol server on stdin/stdout for VS Code
// and any other DAP client.

#include "wrench.h"

#ifdef WRENCH_INCLUDE_DEBUG_CLIENT

#include "utils/json.h"
#include "utils/json_stream.h"
#include "utils/wrench_debug_host.h"
#include "utils/wrench_debug_eval.h"

#include <stdio.h>
#include <string>
#include <vector>
#include <map>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <memory>
#include <chrono>
#include <exception>
#include <filesystem>
#include <algorithm>
#include <algorithm>

#if defined(_WIN32)
#include <io.h>
#include <fcntl.h>
#else
#include <unistd.h>
#endif

namespace
{

struct Breakpoint
{
	int id = 0;
	int requestedLine = 0;
	int line = 0;
	bool verified = false;
	std::string condition;
	std::string hitCondition;
	std::string logMessage;
	int hits = 0;
};

// what a variablesReference points at
struct VarRef
{
	enum Kind
	{
		Locals,
		Globals,
		Children,
	};

	Kind kind = Locals;
	int frame = 0;
	WrenchDebugValue value;
};

// two spellings of one file: relative or absolute, "." and "..", slashes.
// Case only matters outside Windows
bool wr_samePath( const std::string& a, const std::string& b )
{
	std::string pa;
	std::string pb;
	try
	{
		std::error_code ec;
		pa = std::filesystem::absolute( std::filesystem::u8path(a), ec ).lexically_normal().u8string();
		pb = std::filesystem::absolute( std::filesystem::u8path(b), ec ).lexically_normal().u8string();
	}
	catch ( const std::exception& )
	{
		pa = a;
		pb = b;
	}
	std::replace( pa.begin(), pa.end(), '\\', '/' );
	std::replace( pb.begin(), pb.end(), '\\', '/' );
#if defined(_WIN32)
	std::transform( pa.begin(), pa.end(), pa.begin(), [](char c) { return (char)tolower((unsigned char)c); } );
	std::transform( pb.begin(), pb.end(), pb.begin(), [](char c) { return (char)tolower((unsigned char)c); } );
#endif
	return pa == pb;
}

std::string errorText( uint8_t code )
{
	return wr_debugErrorText( code );
}

class DapServer
{
public:
	int run();

private:
	void dispatch( const Json& request );
	void dispatchRequest( const Json& request, const std::string& command );
	void onLaunch( const Json& req ) { onLaunchOrAttach( req, false ); }
	void onAttach( const Json& req ) { onLaunchOrAttach( req, true ); }
	void onDisconnectRequest( const Json& req ) { onDisconnect( req, false ); }
	void onTerminateRequest( const Json& req ) { onDisconnect( req, true ); }
	void respond( const Json& request, bool success, const Json& body = Json::object(), const std::string& message = std::string() );
	void event( const char* name, const Json& body = Json::object() );
	void output( const std::string& text, const char* category = "console" );

	// requests
	void onInitialize( const Json& req );
	void onLaunchOrAttach( const Json& req, bool attach );
	void onSetBreakpoints( const Json& req );
	void onSetExceptionBreakpoints( const Json& req );
	void onConfigurationDone( const Json& req );
	void onThreads( const Json& req );
	void onStackTrace( const Json& req );
	void onScopes( const Json& req );
	void onVariables( const Json& req );
	void onSetVariable( const Json& req );
	void onEvaluate( const Json& req );
	void onCompletions( const Json& req );
	void onBreakpointLocations( const Json& req );
	void onExceptionInfo( const Json& req );
	void onSource( const Json& req );
	void onControl( const Json& req, const std::string& command );
	void onDisconnect( const Json& req, bool terminateRequest );

	// target side
	void pollTarget();
	void handleStopped( const WrenchDebugEvent& e );
	void handleEnded( const WrenchDebugEvent& e );
	bool sendBreakpoints();
	void resetStopState();
	bool ensureStack();
	bool hitConditionMet( const Breakpoint& bp );
	int newRef( const VarRef& ref );
	Json variableJson( const std::string& name, const WrenchDebugValue& v, const std::string& evaluateName, bool hex );
	Json sourceJson();
	void endSession( int exitCode );

	JsonStdioStream m_stream;
	int m_seq = 1;
	bool m_quit = false;

	// session
	bool m_attach = false;
	bool m_started = false;
	bool m_stopped = false;
	bool m_running = false;
	bool m_stopOnEntry = false;
	bool m_noDebug = false;
	bool m_breakOnErrors = true;
	bool m_linesStartAt1 = true;
	bool m_columnsStartAt1 = true;
	bool m_terminated = false;
	std::string m_programPath;
	bool m_sourceFromTarget = false; // source embedded in the bytecode, served by "source"
	std::string m_sourceName;
	WrenchTransportSpec m_spec;
	WrenchClientLink m_link;
	std::unique_ptr<WrenchDebugClient> m_client;
	WrenchDebugProgram m_program;
	std::string m_sourceText;
	WrenchProject m_project;
	WrenchDebugTargetInfo m_info;

	std::vector<Breakpoint> m_breakpoints;
	int m_nextBreakpointId = 1;

	// valid while stopped
	int m_stopLine = 0;
	uint8_t m_stopReason = 0;
	uint8_t m_stopError = 0;
	std::string m_stopDetail; // which function was missing
	bool m_haveStack = false;
	std::vector<WrenchDebugStackFrame> m_frames;
	std::map<int, VarRef> m_refs;
	int m_nextRef = 1;
};

void DapServer::respond( const Json& request, bool success, const Json& body, const std::string& message )
{
	Json r = Json::object();
	r.set( "seq", m_seq++ );
	r.set( "type", "response" );
	r.set( "request_seq", request["seq"].asInt() );
	r.set( "success", success );
	r.set( "command", request["command"].asString() );
	if ( !success )
	{
		r.set( "message", message.empty() ? std::string("failed") : message );
		Json err = Json::object();
		err.set( "id", 1 );
		err.set( "format", message );
		err.set( "showUser", false );
		Json b = Json::object();
		b.set( "error", err );
		r.set( "body", b );
	}
	else
	{
		r.set( "body", body );
	}
	m_stream.send( r );
}

void DapServer::event( const char* name, const Json& body )
{
	Json e = Json::object();
	e.set( "seq", m_seq++ );
	e.set( "type", "event" );
	e.set( "event", name );
	e.set( "body", body );
	m_stream.send( e );
}

void DapServer::output( const std::string& text, const char* category )
{
	Json b = Json::object();
	b.set( "category", category );
	b.set( "output", text );
	event( "output", b );
}

Json DapServer::sourceJson()
{
	Json s = Json::object();
	if ( m_sourceFromTarget )
	{
		s.set( "name", m_sourceName );
		s.set( "sourceReference", 1 );
	}
	else
	{
		std::string name = m_programPath;
		size_t slash = name.find_last_of( "/\\" );
		if ( slash != std::string::npos )
		{
			name = name.substr( slash + 1 );
		}
		s.set( "name", name );
		s.set( "path", m_programPath );
	}
	return s;
}

void DapServer::resetStopState()
{
	m_stopped = false;
	m_haveStack = false;
	m_frames.clear();
	m_refs.clear();
	m_nextRef = 1;
}

int DapServer::newRef( const VarRef& ref )
{
	int id = m_nextRef++;
	m_refs[id] = ref;
	return id;
}

bool DapServer::ensureStack()
{
	if ( m_haveStack )
	{
		return true;
	}
	if ( !m_client || !m_stopped )
	{
		return false;
	}
	if ( !m_client->getStack(m_frames, &m_program, m_stopLine) )
	{
		return false;
	}
	m_haveStack = true;
	return true;
}

void DapServer::onInitialize( const Json& req )
{
	const Json& args = req["arguments"];
	m_linesStartAt1 = args["linesStartAt1"].asBool( true );
	m_columnsStartAt1 = args["columnsStartAt1"].asBool( true );

	Json caps = Json::object();
	caps.set( "supportsConfigurationDoneRequest", true );
	caps.set( "supportsSetVariable", true );
	caps.set( "supportsConditionalBreakpoints", true );
	caps.set( "supportsHitConditionalBreakpoints", true );
	caps.set( "supportsLogPoints", true );
	caps.set( "supportsEvaluateForHovers", true );
	caps.set( "supportsTerminateRequest", true );
	caps.set( "supportsValueFormattingOptions", true );
	caps.set( "supportsExceptionInfoRequest", true );
	caps.set( "supportsBreakpointLocationsRequest", true );
	caps.set( "supportsCompletionsRequest", true );
	caps.set( "supportsDelayedStackTraceLoading", false );
	caps.set( "supportTerminateDebuggee", true );

	Json filters = Json::array();
	Json f = Json::object();
	f.set( "filter", "runtimeErrors" );
	f.set( "label", "Runtime errors" );
	f.set( "description", "Stop where a wrench runtime error happens, with the stack still intact" );
	f.set( "default", true );
	filters.push( f );
	caps.set( "exceptionBreakpointFilters", filters );

	respond( req, true, caps );
}

void DapServer::onLaunchOrAttach( const Json& req, bool attach )
{
	const Json& args = req["arguments"];
	m_attach = attach;
	m_stopOnEntry = args["stopOnEntry"].asBool( false );
	m_noDebug = args["noDebug"].asBool( false );
	m_programPath = args["program"].asString( "" );
	m_stream.setTrace( args["trace"].asBool(false) );

	// transport: either "target": "tcp:host:port" or the separate fields
	std::string target = args["target"].asString( "" );
	if ( target.empty() )
	{
		std::string transport = args["transport"].asString( "local" );
		if ( transport == "tcp" )
		{
			target = "tcp:" + args["host"].asString( "127.0.0.1" ) + ":" + std::to_string( args["port"].asInt(0) );
		}
		else if ( transport == "serial" )
		{
			target = "serial:" + args["serialPort"].asString( "" ) + ":" + std::to_string( args["baudRate"].asInt(115200) );
		}
		else
		{
			target = "local";
		}
	}

	std::string error;
	if ( !m_spec.parse(target, error) )
	{
		respond( req, false, Json::object(), error );
		return;
	}

	if ( attach && m_spec.kind == WrenchTransportSpec::Local )
	{
		respond( req, false, Json::object(), "attach needs a tcp or serial target" );
		return;
	}

	// local symbols, unless attaching without a program file. A compiled
	// program (.bin) is used as it is, its source comes from the bytecode
	std::vector<uint8_t> bytecode;
	bool programIsBytecode = false;
	if ( !m_programPath.empty() )
	{
		std::string source;
		if ( !wr_loadForDebug(m_programPath, bytecode, source, error, &programIsBytecode) )
		{
			respond( req, false, Json::object(), error );
			return;
		}
		if ( !m_program.parse(bytecode.data(), (uint32_t)bytecode.size(), &error) )
		{
			respond( req, false, Json::object(), error );
			return;
		}
		m_sourceText = source;
		if ( programIsBytecode )
		{
			std::string name = m_programPath;
			size_t slash = name.find_last_of( "/\\" );
			m_sourceName = (slash == std::string::npos ? name : name.substr(slash + 1)) + " (embedded source)";
			m_sourceFromTarget = true;
		}
	}
	else if ( !attach )
	{
		respond( req, false, Json::object(), "launch needs \"program\", a .w file or a .bin compiled with -debugsource" );
		return;
	}

	// the host application the script is written for: the local target
	// imitates it, error messages use its function names
	std::string projectPath = args["project"].asString( "" );
	if ( !m_programPath.empty() || !projectPath.empty() )
	{
		std::string projectError;
		if ( !wr_projectForScript(m_programPath, projectPath, m_project, projectError) )
		{
			output( "Ignoring the host description: " + projectError + "\n", "important" );
		}
		else if ( m_project.loaded() && m_spec.kind == WrenchTransportSpec::Local )
		{
			output( "Imitating the host application described by " + m_project.path() + "\n" );
		}
	}

	if ( !m_link.open(m_spec, error, args["connectTimeout"].asInt(5000), m_project.loaded() ? &m_project : nullptr) )
	{
		respond( req, false, Json::object(), error );
		return;
	}

	m_client.reset( new WrenchDebugClient(m_link.transport()) );
	int timeout = args["timeout"].asInt( m_spec.kind == WrenchTransportSpec::Serial ? 2000 : 3000 );
	m_client->setTimeout( timeout, m_spec.kind == WrenchTransportSpec::Serial ? 4 : 2 );

	if ( !m_client->connect(m_info) )
	{
		respond( req, false, Json::object(), "no answer from the wrench target on " + m_spec.describe() + ": " + m_client->lastError() );
		m_link.close();
		return;
	}

	output( "Connected to " + m_spec.describe() + " (wrench " + m_info.wrenchVersion + ")\n" );

	if ( attach )
	{
		if ( m_programPath.empty() )
		{
			// debug whatever the target runs, using its embedded symbols and source
			std::vector<uint8_t> remote;
			if ( !m_client->readSection(WRD_SectionBytecode, remote) || !m_program.parse(remote.data(), (uint32_t)remote.size(), &error) )
			{
				respond( req, false, Json::object(), "cannot read the program from the target: " + (error.empty() ? m_client->lastError() : error) );
				return;
			}
			if ( !m_program.hasSource() )
			{
				output( "The target program carries no source (compile with -debugsource), only stacks and variables are available.\n" );
			}
			m_sourceFromTarget = true;
			m_sourceName = "target source";
			m_sourceText = m_program.source();
		}
		else
		{
			// the firmware may have been built with other flags (-debug without
			// the source): compile the local file the same way so the code
			// offsets, and the build hash, match
			if ( !programIsBytecode && m_info.programHash != m_program.hash() && m_info.compilerFlags != m_program.compilerFlags()
					&& (m_info.compilerFlags & WR_EMBED_DEBUG_CODE) )
			{
				std::vector<uint8_t> matching;
				std::string source;
				std::string matchError;
				WrenchDebugProgram rebuilt;
				if ( wr_compileWithFlags(m_programPath, matching, source, matchError, m_info.compilerFlags)
						&& rebuilt.parse(matching.data(), (uint32_t)matching.size(), &matchError) )
				{
					m_program = rebuilt;
				}
			}
			if ( m_info.programHash != m_program.hash() )
			{
				output( "Warning: the target runs a different build than " + m_programPath + ", lines may not match.\n", "important" );
			}
		}
	}
	else if ( args["upload"].asBool(true) && !(m_info.caps & WRD_CapLoad) )
	{
		respond( req, false, Json::object(), "the target does not accept uploaded programs (its firmware called setAllowUpload(false)): "
				"use \"request\": \"attach\" to debug the program it runs, or \"upload\": false to launch that program again" );
		return;
	}
	else if ( args["upload"].asBool(true) )
	{
		int lastPercent = -1;
		bool slow = m_spec.kind == WrenchTransportSpec::Serial;
		if ( !m_client->upload(m_program.bytecode(), [&](uint32_t sent, uint32_t total)
				{
					int percent = (int)(100.0 * sent / (total ? total : 1));
					if ( slow && percent / 25 != lastPercent / 25 )
					{
						output( "Uploading " + std::to_string(percent) + "%\n" );
					}
					lastPercent = percent;
				}) )
		{
			respond( req, false, Json::object(), "upload failed: " + m_client->lastError() );
			return;
		}
	}
	else if ( m_info.programHash != m_program.hash() )
	{
		output( "Warning: upload is off and the target holds a different build.\n", "important" );
	}

	if ( programIsBytecode && !m_program.hasSource() )
	{
		output( m_programPath + " carries no source (compile it with -debugsource), only stacks and variables are available.\n", "important" );
	}
	if ( !m_program.hasDebugCode() )
	{
		output( "The program was compiled without debug information (-debug or -debugsource): it runs, but cannot stop at breakpoints or step.\n", "important" );
	}

	respond( req, true );
	event( "initialized" );
}

bool DapServer::sendBreakpoints()
{
	if ( !m_client )
	{
		return false;
	}

	std::vector<int> lines;
	if ( !m_noDebug )
	{
		for ( Breakpoint& bp : m_breakpoints )
		{
			if ( bp.verified && std::find(lines.begin(), lines.end(), bp.line) == lines.end() )
			{
				if ( (int)lines.size() >= m_info.maxBreakpoints )
				{
					bp.verified = false;
					continue;
				}
				lines.push_back( bp.line );
			}
		}
	}
	return m_client->setBreakpoints( lines );
}

void DapServer::onSetBreakpoints( const Json& req )
{
	const Json& args = req["arguments"];
	const Json& source = args["source"];
	const Json& requested = args["breakpoints"];

	bool ourFile = true;
	if ( !m_sourceFromTarget && !m_programPath.empty() )
	{
		ourFile = wr_samePath( source["path"].asString(""), m_programPath );
	}
	else if ( m_sourceFromTarget )
	{
		ourFile = source["sourceReference"].asInt( 0 ) == 1;
	}

	Json result = Json::array();
	if ( !ourFile )
	{
		for ( size_t i = 0; i < requested.size(); ++i )
		{
			Json b = Json::object();
			b.set( "verified", false );
			b.set( "line", requested[i]["line"].asInt() );
			b.set( "message", "only the program being debugged can hold breakpoints" );
			result.push( b );
		}
		Json body = Json::object();
		body.set( "breakpoints", result );
		respond( req, true, body );
		return;
	}

	// keep hit counts of breakpoints that survive the update
	std::vector<Breakpoint> old;
	old.swap( m_breakpoints );
	for ( size_t i = 0; i < requested.size(); ++i )
	{
		const Json& r = requested[i];
		Breakpoint bp;
		bp.requestedLine = r["line"].asInt() + (m_linesStartAt1 ? 0 : 1);
		bp.condition = r["condition"].asString( "" );
		bp.hitCondition = r["hitCondition"].asString( "" );
		bp.logMessage = r["logMessage"].asString( "" );
		bp.line = m_program.valid() ? m_program.breakableLine( bp.requestedLine ) : -1;
		bp.verified = bp.line > 0;
		bp.id = m_nextBreakpointId++;
		for ( const Breakpoint& o : old )
		{
			if ( o.line == bp.line && o.condition == bp.condition && o.hitCondition == bp.hitCondition )
			{
				bp.hits = o.hits;
			}
		}
		m_breakpoints.push_back( bp );
	}

	bool sent = sendBreakpoints();

	for ( const Breakpoint& bp : m_breakpoints )
	{
		Json b = Json::object();
		b.set( "id", bp.id );
		b.set( "verified", bp.verified && sent );
		b.set( "line", (bp.line > 0 ? bp.line : bp.requestedLine) - (m_linesStartAt1 ? 0 : 1) );
		b.set( "source", sourceJson() );
		if ( !bp.verified )
		{
			b.set( "message", bp.line > 0 ? "the target has no room for more breakpoints" : "no code at or after this line" );
		}
		else if ( !sent )
		{
			b.set( "message", m_client ? m_client->lastError() : std::string("not connected") );
		}
		result.push( b );
	}

	Json body = Json::object();
	body.set( "breakpoints", result );
	respond( req, true, body );
}

void DapServer::onSetExceptionBreakpoints( const Json& req )
{
	m_breakOnErrors = false;
	const Json& filters = req["arguments"]["filters"];
	for ( size_t i = 0; i < filters.size(); ++i )
	{
		if ( filters[i].asString() == "runtimeErrors" )
		{
			m_breakOnErrors = true;
		}
	}
	respond( req, true );
}

void DapServer::onConfigurationDone( const Json& req )
{
	respond( req, true );
	if ( !m_client || m_started )
	{
		return;
	}

	m_started = true;
	if ( m_attach )
	{
		// a target waiting on its first line (breakOnEntry) is already halted
		pollTarget();
		if ( m_stopped )
		{
			return;
		}
		m_running = true;
		if ( m_stopOnEntry )
		{
			m_client->pause();
		}
		return;
	}

	if ( !m_client->launch(m_stopOnEntry && !m_noDebug) )
	{
		output( "Launch failed: " + m_client->lastError() + "\n", "important" );
		endSession( 1 );
		return;
	}
	m_running = true;
}

void DapServer::onThreads( const Json& req )
{
	Json threads = Json::array();
	Json t = Json::object();
	t.set( "id", 1 );
	t.set( "name", "wrench" );
	threads.push( t );
	Json body = Json::object();
	body.set( "threads", threads );
	respond( req, true, body );
}

void DapServer::onStackTrace( const Json& req )
{
	if ( !ensureStack() )
	{
		respond( req, false, Json::object(), m_stopped ? m_client->lastError() : std::string("not stopped") );
		return;
	}

	int start = req["arguments"]["startFrame"].asInt( 0 );
	int levels = req["arguments"]["levels"].asInt( 0 );
	if ( start < 0 )
	{
		start = 0;
	}
	Json frames = Json::array();
	for ( int i = start; i < (int)m_frames.size() && (levels <= 0 || i < start + levels); ++i )
	{
		const WrenchDebugStackFrame& f = m_frames[i];
		Json j = Json::object();
		j.set( "id", i + 1 );
		j.set( "name", f.name );
		j.set( "line", f.line - (m_linesStartAt1 ? 0 : 1) );
		j.set( "column", m_linesStartAt1 ? 1 : 0 );
		if ( !m_programPath.empty() || m_program.hasSource() )
		{
			j.set( "source", sourceJson() );
		}
		frames.push( j );
	}

	Json body = Json::object();
	body.set( "stackFrames", frames );
	body.set( "totalFrames", (int)m_frames.size() );
	respond( req, true, body );
}

void DapServer::onScopes( const Json& req )
{
	int frame = req["arguments"]["frameId"].asInt( 1 ) - 1;
	Json scopes = Json::array();
	if ( ensureStack() && frame >= 0 && frame < (int)m_frames.size() )
	{
		if ( m_frames[frame].function >= 0 )
		{
			VarRef ref;
			ref.kind = VarRef::Locals;
			ref.frame = frame;
			Json s = Json::object();
			s.set( "name", "Locals" );
			s.set( "presentationHint", "locals" );
			s.set( "variablesReference", newRef(ref) );
			s.set( "namedVariables", m_frames[frame].slots );
			s.set( "expensive", false );
			scopes.push( s );
		}

		VarRef ref;
		ref.kind = VarRef::Globals;
		Json s = Json::object();
		s.set( "name", "Globals" );
		s.set( "variablesReference", newRef(ref) );
		s.set( "expensive", false );
		scopes.push( s );
	}
	Json body = Json::object();
	body.set( "scopes", scopes );
	respond( req, true, body );
}

Json DapServer::variableJson( const std::string& name, const WrenchDebugValue& v, const std::string& evaluateName, bool hex )
{
	Json j = Json::object();
	j.set( "name", name );
	j.set( "value", v.display(hex) );
	j.set( "type", v.typeName() );
	if ( !evaluateName.empty() )
	{
		j.set( "evaluateName", evaluateName );
	}

	int ref = 0;
	if ( v.expandable() )
	{
		VarRef r;
		r.kind = VarRef::Children;
		r.value = v;
		ref = newRef( r );
		if ( v.tag == WRD_ValArray || v.tag == WRD_ValRaw )
		{
			j.set( "indexedVariables", (int)v.count );
		}
		else
		{
			j.set( "namedVariables", (int)v.count );
		}
	}
	j.set( "variablesReference", ref );
	return j;
}

void DapServer::onVariables( const Json& req )
{
	const Json& args = req["arguments"];
	int id = args["variablesReference"].asInt();
	bool hex = args["format"]["hex"].asBool( false );
	auto it = m_refs.find( id );
	if ( it == m_refs.end() || !m_stopped )
	{
		respond( req, false, Json::object(), "variables are only available while stopped" );
		return;
	}

	const VarRef ref = it->second;
	Json vars = Json::array();

	if ( ref.kind == VarRef::Locals )
	{
		std::vector<WrenchDebugValue> values;
		if ( !m_client->getLocals(ref.frame, values) )
		{
			respond( req, false, Json::object(), m_client->lastError() );
			return;
		}
		int function = m_frames[ref.frame].function;
		for ( size_t i = 0; i < values.size(); ++i )
		{
			std::string name = m_program.localName( function, (int)i );
			vars.push( variableJson(name, values[i], name, hex) );
		}
	}
	else if ( ref.kind == VarRef::Globals )
	{
		std::vector<WrenchDebugValue> values;
		if ( !m_client->getGlobals(values) )
		{
			respond( req, false, Json::object(), m_client->lastError() );
			return;
		}
		for ( size_t i = 0; i < values.size(); ++i )
		{
			std::string name = m_program.globalName( (int)i );
			vars.push( variableJson(name, values[i], "::" + name, hex) );
		}
	}
	else
	{
		uint32_t start = (uint32_t)args["start"].asInt( 0 );
		uint32_t count = (uint32_t)args["count"].asInt( 0 );
		if ( count == 0 || start + count > ref.value.count )
		{
			count = ref.value.count > start ? ref.value.count - start : 0;
		}
		std::vector<WrenchDebugChild> kids;
		uint32_t total = 0;
		if ( !m_client->getChildren(ref.value, start, count, kids, total, &m_program) )
		{
			respond( req, false, Json::object(), m_client->lastError() );
			return;
		}
		for ( const WrenchDebugChild& c : kids )
		{
			vars.push( variableJson(c.name, c.value, std::string(), hex) );
		}
	}

	Json body = Json::object();
	body.set( "variables", vars );
	respond( req, true, body );
}

void DapServer::onSetVariable( const Json& req )
{
	const Json& args = req["arguments"];
	int id = args["variablesReference"].asInt();
	std::string name = args["name"].asString( "" );
	std::string valueText = args["value"].asString( "" );
	auto it = m_refs.find( id );
	if ( it == m_refs.end() || !m_stopped || !ensureStack() )
	{
		respond( req, false, Json::object(), "not stopped" );
		return;
	}

	const VarRef ref = it->second;
	WrenchDebugEvaluator ev( *m_client, m_program, m_frames, ref.kind == VarRef::Locals ? ref.frame : 0 );
	WrenchDebugLocation where;
	std::string error;

	if ( ref.kind == VarRef::Locals )
	{
		where.kind = WrenchDebugLocation::Local;
		where.frame = ref.frame;
		where.slot = m_program.localSlot( m_frames[ref.frame].function, name );
	}
	else if ( ref.kind == VarRef::Globals )
	{
		where.kind = WrenchDebugLocation::Global;
		where.slot = m_program.globalSlot( name );
	}
	else
	{
		std::vector<WrenchDebugChild> kids;
		uint32_t total = 0;
		if ( m_client->getChildren(ref.value, 0, ref.value.count, kids, total, &m_program) )
		{
			for ( const WrenchDebugChild& c : kids )
			{
				if ( c.name == name )
				{
					where.kind = WrenchDebugLocation::Child;
					where.parent = ref.value;
					where.index = c.index;
				}
			}
		}
	}

	if ( where.kind == WrenchDebugLocation::None || where.slot < 0 )
	{
		respond( req, false, Json::object(), "cannot find " + name );
		return;
	}

	WrenchDebugValue value;
	if ( !ev.evaluate(valueText, value, error) )
	{
		respond( req, false, Json::object(), error );
		return;
	}

	WrenchDebugValue result;
	if ( !ev.store(where, value, result, error) )
	{
		respond( req, false, Json::object(), error );
		return;
	}

	Json v = variableJson( name, result, std::string(), false );
	Json body = Json::object();
	body.set( "value", v["value"] );
	body.set( "type", v["type"] );
	body.set( "variablesReference", v["variablesReference"] );
	respond( req, true, body );
}

void DapServer::onEvaluate( const Json& req )
{
	const Json& args = req["arguments"];
	std::string expression = args["expression"].asString( "" );
	std::string context = args["context"].asString( "repl" );
	int frame = args["frameId"].asInt( 1 ) - 1;
	bool hex = args["format"]["hex"].asBool( false );

	if ( !m_stopped || !ensureStack() )
	{
		respond( req, false, Json::object(), m_running ? "the program is running, pause it first" : "no program" );
		return;
	}
	if ( frame < 0 || frame >= (int)m_frames.size() )
	{
		frame = 0;
	}

	WrenchDebugEvaluator ev( *m_client, m_program, m_frames, frame );
	std::string error;

	// "name = value" in the debug console assigns
	size_t eq = WrenchDebugEvaluator::findAssignment( expression );
	if ( context == "repl" && eq != std::string::npos )
	{
		WrenchDebugValue result;
		std::string path = expression.substr( 0, eq );
		if ( !ev.assign(path, expression.substr(eq + 1), result, error) )
		{
			respond( req, false, Json::object(), error );
			return;
		}
		Json body = Json::object();
		body.set( "result", result.display(hex) );
		body.set( "variablesReference", 0 );
		respond( req, true, body );
		return;
	}

	WrenchDebugValue v;
	if ( !ev.evaluate(expression, v, error) )
	{
		respond( req, false, Json::object(), error );
		return;
	}

	Json var = variableJson( expression, v, expression, hex );
	Json body = Json::object();
	std::string text = v.display( hex );
	if ( v.isString() && context != "hover" )
	{
		std::string full;
		m_client->getString( v, full );
		text = WrenchDebugValue::makeString( full ).display();
	}
	body.set( "result", text );
	body.set( "type", var["type"] );
	body.set( "variablesReference", var["variablesReference"] );
	if ( var.has("indexedVariables") )
	{
		body.set( "indexedVariables", var["indexedVariables"] );
	}
	if ( var.has("namedVariables") )
	{
		body.set( "namedVariables", var["namedVariables"] );
	}
	respond( req, true, body );
}

void DapServer::onCompletions( const Json& req )
{
	const Json& args = req["arguments"];
	std::string text = args["text"].asString( "" );
	const int columnBase = m_columnsStartAt1 ? 1 : 0;
	int column = args["column"].asInt( (int)text.size() + columnBase ) - columnBase;
	int frame = args["frameId"].asInt( 1 ) - 1;
	if ( column > (int)text.size() )
	{
		column = (int)text.size();
	}
	if ( column < 0 )
	{
		column = 0;
	}

	int start = column;
	while ( start > 0 && (isalnum((unsigned char)text[start - 1]) || text[start - 1] == '_' || text[start - 1] == ':') )
	{
		--start;
	}
	std::string prefix = text.substr( start, column - start );

	std::vector<std::string> names;
	// the target may run a program with more functions than the local one
	if ( ensureStack() && frame >= 0 && frame < (int)m_frames.size() && m_frames[frame].function >= 0
		&& m_frames[frame].function + 1 < (int)m_program.functions().size() )
	{
		for ( const std::string& l : m_program.functions()[m_frames[frame].function + 1].locals )
		{
			names.push_back( WrenchDebugProgram::displayName(l) );
		}
	}
	if ( !m_program.functions().empty() )
	{
		for ( const std::string& g : m_program.functions()[0].locals )
		{
			names.push_back( WrenchDebugProgram::displayName(g) );
		}
	}

	Json targets = Json::array();
	for ( const std::string& n : names )
	{
		if ( n.compare(0, prefix.size(), prefix) == 0 )
		{
			Json t = Json::object();
			t.set( "label", n );
			t.set( "type", "variable" );
			t.set( "start", start + columnBase );
			t.set( "length", (int)prefix.size() );
			targets.push( t );
		}
	}
	Json body = Json::object();
	body.set( "targets", targets );
	respond( req, true, body );
}

void DapServer::onBreakpointLocations( const Json& req )
{
	const Json& args = req["arguments"];
	int line = args["line"].asInt( 1 ) + (m_linesStartAt1 ? 0 : 1);
	int endLine = args["endLine"].isNumber() ? args["endLine"].asInt() + (m_linesStartAt1 ? 0 : 1) : line;
	Json locations = Json::array();
	for ( int l : m_program.lines() )
	{
		if ( l >= line && l <= endLine )
		{
			Json j = Json::object();
			j.set( "line", l - (m_linesStartAt1 ? 0 : 1) );
			locations.push( j );
		}
	}
	Json body = Json::object();
	body.set( "breakpoints", locations );
	respond( req, true, body );
}

void DapServer::onExceptionInfo( const Json& req )
{
	Json body = Json::object();
	body.set( "exceptionId", errorText(m_stopError) );
	std::string description = std::string( "wrench runtime error " ) + errorText( m_stopError ) + " at line " + std::to_string( m_stopLine );
	if ( !m_stopDetail.empty() )
	{
		description += ": " + m_stopDetail;
	}
	body.set( "description", description );
	body.set( "breakMode", "always" );
	respond( req, true, body );
}

void DapServer::onSource( const Json& req )
{
	if ( !m_program.hasSource() )
	{
		respond( req, false, Json::object(), "the program has no embedded source" );
		return;
	}
	Json body = Json::object();
	body.set( "content", m_program.source() );
	body.set( "mimeType", "text/x-wrench" );
	respond( req, true, body );
}

void DapServer::onControl( const Json& req, const std::string& command )
{
	if ( !m_client )
	{
		respond( req, false, Json::object(), "not connected" );
		return;
	}

	bool ok = false;
	if ( command == "pause" )
	{
		ok = m_client->pause();
		respond( req, ok, Json::object(), ok ? std::string() : m_client->lastError() );
		return;
	}

	if ( !m_stopped )
	{
		respond( req, false, Json::object(), "not stopped" );
		return;
	}

	if ( command == "continue" )
	{
		ok = m_client->resume();
	}
	else if ( command == "next" )
	{
		ok = m_client->stepOver();
	}
	else if ( command == "stepIn" )
	{
		ok = m_client->stepIn();
	}
	else if ( command == "stepOut" )
	{
		ok = m_client->stepOut();
	}

	if ( ok )
	{
		resetStopState();
		m_running = true;
		Json body = Json::object();
		if ( command == "continue" )
		{
			body.set( "allThreadsContinued", true );
		}
		respond( req, true, body );
	}
	else
	{
		respond( req, false, Json::object(), m_client->lastError() );
	}
}

void DapServer::endSession( int exitCode )
{
	if ( m_terminated )
	{
		return;
	}
	m_terminated = true;
	m_running = false;
	resetStopState();
	Json exited = Json::object();
	exited.set( "exitCode", exitCode );
	event( "exited", exited );
	event( "terminated" );
}

void DapServer::onDisconnect( const Json& req, bool terminateRequest )
{
	bool terminate = req["arguments"]["terminateDebuggee"].asBool( !m_attach );
	if ( m_client && m_client->linkUp() )
	{
		if ( terminateRequest || terminate || m_link.isLocal() )
		{
			m_client->terminate();
			WrenchDebugEvent e;
			for ( int i = 0; i < 40 && m_client->waitEvent(e, 50) && e.type != WrenchDebugEvent::Ended; ++i )
			{
			}
		}
		if ( !terminateRequest )
		{
			m_client->detach();
		}
	}

	if ( terminateRequest )
	{
		respond( req, true );
		endSession( 0 );
		return;
	}

	respond( req, true );
	m_link.close();
	m_client.reset();
	m_quit = true;
}

bool DapServer::hitConditionMet( const Breakpoint& bp )
{
	if ( bp.hitCondition.empty() )
	{
		return true;
	}
	std::string c = bp.hitCondition;
	c.erase( std::remove(c.begin(), c.end(), ' '), c.end() );
	int n = 0;
	if ( c.compare(0, 2, ">=") == 0 ) { n = atoi( c.c_str() + 2 ); return bp.hits >= n; }
	if ( c.compare(0, 2, "<=") == 0 ) { n = atoi( c.c_str() + 2 ); return bp.hits <= n; }
	if ( c.compare(0, 2, "==") == 0 ) { n = atoi( c.c_str() + 2 ); return bp.hits == n; }
	if ( c[0] == '>' ) { n = atoi( c.c_str() + 1 ); return bp.hits > n; }
	if ( c[0] == '<' ) { n = atoi( c.c_str() + 1 ); return bp.hits < n; }
	if ( c[0] == '%' ) { n = atoi( c.c_str() + 1 ); return n > 0 && bp.hits % n == 0; }
	n = atoi( c.c_str() );
	return bp.hits >= n;
}

void DapServer::handleStopped( const WrenchDebugEvent& e )
{
	resetStopState();
	m_stopped = true;
	m_running = false;
	m_stopLine = e.line;
	m_stopReason = e.reason;
	m_stopError = e.error;
	m_stopDetail = wr_describeMissingFunction( e.error, e.missingFunction, m_sourceText, m_project.loaded() ? &m_project : nullptr );

	if ( m_noDebug )
	{
		// "Run Without Debugging": never halt, the error is reported when the
		// program ends
		resetStopState();
		if ( m_client->resume() )
		{
			m_running = true;
		}
		return;
	}

	const char* reason = "step";
	std::string description;
	Json hitIds = Json::array();

	if ( e.reason == WRD_StopBreakpoint )
	{
		// evaluate conditions, hit counts and log points on the host
		bool stop = false;
		bool anyAtLine = false;
		for ( Breakpoint& bp : m_breakpoints )
		{
			if ( !bp.verified || bp.line != e.line )
			{
				continue;
			}
			anyAtLine = true;

			if ( !bp.condition.empty() )
			{
				if ( !ensureStack() )
				{
					stop = true;
					continue;
				}
				WrenchDebugEvaluator ev( *m_client, m_program, m_frames, 0 );
				WrenchDebugValue v;
				std::string error;
				if ( !ev.evaluate(bp.condition, v, error) )
				{
					output( "Breakpoint condition '" + bp.condition + "' failed: " + error + "\n", "important" );
					stop = true;
					hitIds.push( bp.id );
					continue;
				}
				if ( !WrenchDebugEvaluator::truthy(v) )
				{
					continue;
				}
			}

			++bp.hits;
			if ( !hitConditionMet(bp) )
			{
				continue;
			}

			if ( !bp.logMessage.empty() )
			{
				ensureStack();
				WrenchDebugEvaluator ev( *m_client, m_program, m_frames, 0 );
				output( ev.interpolate(bp.logMessage) + "\n", "console" );
				continue;
			}

			stop = true;
			hitIds.push( bp.id );
		}

		if ( anyAtLine && !stop )
		{
			// not stopping here after all: a step this breakpoint interrupted
			// (stepping over a call that hits it) must carry on
			resetStopState();
			if ( m_client->resume(true) )
			{
				m_running = true;
			}
			return;
		}
		reason = "breakpoint";
	}
	else if ( e.reason == WRD_StopStep )
	{
		// a step that ends on a breakpoint line still counts the hit and
		// prints a logpoint (the stop itself stays a step)
		for ( Breakpoint& bp : m_breakpoints )
		{
			if ( !bp.verified || bp.line != e.line )
			{
				continue;
			}
			if ( !bp.condition.empty() )
			{
				WrenchDebugValue v;
				std::string error;
				if ( !ensureStack() )
				{
					continue;
				}
				WrenchDebugEvaluator ev( *m_client, m_program, m_frames, 0 );
				if ( !ev.evaluate(bp.condition, v, error) || !WrenchDebugEvaluator::truthy(v) )
				{
					continue;
				}
			}
			++bp.hits;
			if ( !bp.logMessage.empty() && hitConditionMet(bp) )
			{
				ensureStack();
				WrenchDebugEvaluator ev( *m_client, m_program, m_frames, 0 );
				output( ev.interpolate(bp.logMessage) + "\n", "console" );
			}
		}
	}
	else if ( e.reason == WRD_StopError )
	{
		if ( !m_breakOnErrors )
		{
			resetStopState();
			m_client->resume();
			m_running = true;
			return;
		}
		reason = "exception";
		description = std::string( "Runtime error: " ) + errorText( e.error );
		if ( !m_stopDetail.empty() )
		{
			description += ", " + m_stopDetail;
		}
	}
	else if ( e.reason == WRD_StopPause )
	{
		reason = "pause";
	}
	else if ( e.reason == WRD_StopEntry )
	{
		reason = "entry";
	}

	Json body = Json::object();
	body.set( "reason", reason );
	body.set( "threadId", 1 );
	body.set( "allThreadsStopped", true );
	if ( !description.empty() )
	{
		body.set( "description", description );
		body.set( "text", errorText(e.error) );
	}
	if ( hitIds.size() )
	{
		body.set( "hitBreakpointIds", hitIds );
	}
	event( "stopped", body );
}

void DapServer::handleEnded( const WrenchDebugEvent& e )
{
	m_running = false;
	resetStopState();

	if ( e.error && !(e.flags & WRD_EndedAborted) )
	{
		std::string detail = wr_describeMissingFunction( e.error, e.missingFunction, m_sourceText, m_project.loaded() ? &m_project : nullptr );
		output( std::string("Program failed with ") + errorText(e.error) + " near line " + std::to_string(e.line)
				+ (detail.empty() ? std::string() : ": " + detail) + "\n", "stderr" );
	}

	if ( (e.flags & WRD_EndedHostDriven) && !m_link.isLocal() )
	{
		// the firmware keeps calling into the script, stay attached
		output( "Global code finished, the target keeps running its own calls.\n" );
		return;
	}

	endSession( e.error ? (int)e.error : 0 );
}

void DapServer::pollTarget()
{
	// events wait for configurationDone: a target halted by breakOnEntry()
	// reports its stop while we are still connecting
	if ( !m_client || m_terminated || !m_started )
	{
		return;
	}

	WrenchDebugEvent e;
	int budget = 64;
	while ( budget-- > 0 && m_client->waitEvent(e, 0) )
	{
		switch ( e.type )
		{
			case WrenchDebugEvent::Output: output( e.text, "stdout" ); break;
			case WrenchDebugEvent::Stopped: handleStopped( e ); break;
			case WrenchDebugEvent::Ended: handleEnded( e ); break;
			case WrenchDebugEvent::Loaded:
			{
				output( "The target loaded a new program.\n", "important" );
				break;
			}
			case WrenchDebugEvent::Disconnected:
			{
				output( "Lost the connection to the target.\n", "important" );
				endSession( 1 );
				return;
			}
		}
	}

	// notice a dead link even when nothing is expected
	if ( !m_client->linkUp() && !m_terminated )
	{
		output( "Lost the connection to the target.\n", "important" );
		endSession( 1 );
	}
}

void DapServer::dispatch( const Json& req )
{
	if ( req.has("wrenchParseError") )
	{
		// DAP has no way to answer a message it cannot read
		fprintf( stderr, "wrench dap: ignored a malformed message: %s\n", req["wrenchParseError"].asString().c_str() );
		return;
	}
	if ( req["type"].asString() != "request" )
	{
		return;
	}

	// a request with odd arguments must fail on its own, not end the session
	try
	{
		dispatchRequest( req, req["command"].asString() );
	}
	catch ( const std::exception& e )
	{
		respond( req, false, Json::object(), std::string("internal error: ") + e.what() );
	}
}

void DapServer::dispatchRequest( const Json& req, const std::string& command )
{
	typedef void (DapServer::*Handler)( const Json& );
	struct Route
	{
		const char* command;
		Handler handler;
	};
	static const Route routes[] =
	{
		{ "initialize", &DapServer::onInitialize },
		{ "launch", &DapServer::onLaunch },
		{ "attach", &DapServer::onAttach },
		{ "setBreakpoints", &DapServer::onSetBreakpoints },
		{ "setExceptionBreakpoints", &DapServer::onSetExceptionBreakpoints },
		{ "configurationDone", &DapServer::onConfigurationDone },
		{ "threads", &DapServer::onThreads },
		{ "stackTrace", &DapServer::onStackTrace },
		{ "scopes", &DapServer::onScopes },
		{ "variables", &DapServer::onVariables },
		{ "setVariable", &DapServer::onSetVariable },
		{ "evaluate", &DapServer::onEvaluate },
		{ "completions", &DapServer::onCompletions },
		{ "breakpointLocations", &DapServer::onBreakpointLocations },
		{ "exceptionInfo", &DapServer::onExceptionInfo },
		{ "source", &DapServer::onSource },
		{ "disconnect", &DapServer::onDisconnectRequest },
		{ "terminate", &DapServer::onTerminateRequest },
	};

	for ( const Route& r : routes )
	{
		if ( command == r.command )
		{
			(this->*r.handler)( req );
			return;
		}
	}

	if ( command == "continue" || command == "next" || command == "stepIn" || command == "stepOut" || command == "pause" )
	{
		onControl( req, command );
		return;
	}

	respond( req, false, Json::object(), "unsupported request " + command );
}

int DapServer::run()
{
	m_stream.startReader();

	std::chrono::steady_clock::time_point lastPing = std::chrono::steady_clock::now();
	while ( !m_quit )
	{
		Json message;
		bool haveMessage = m_stream.next( message, (m_running || m_stopped) ? 10 : 50 );
		if ( haveMessage )
		{
			dispatch( message );
		}
		else if ( m_stream.closed() )
		{
			break;
		}
		pollTarget();

		// while halted the target may only hear from us when the user acts:
		// keep the link alive for targets with a link timeout (UART)
		std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
		if ( m_stopped && m_client && m_client->linkUp() && !m_link.isLocal() && now - lastPing > std::chrono::seconds(1) )
		{
			m_client->ping();
			lastPing = now;
		}
	}

	if ( m_client && m_client->linkUp() )
	{
		if ( m_link.isLocal() )
		{
			m_client->terminate();
		}
		else
		{
			m_client->detach();
		}
	}
	m_link.close();
	return 0;
}

}

int wr_dapMain( int argn, char* argv[] )
{
	DapServer server;
	return server.run();
}

#endif
