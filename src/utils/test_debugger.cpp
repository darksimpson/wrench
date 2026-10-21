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

// end to end tests of the debugger: a real WrenchDebugServer on its own
// thread, a WrenchDebugClient talking to it over a WrenchDebugPipe

#include "wrench.h"

#ifdef WRENCH_INCLUDE_DEBUG_CLIENT

#include "debug/debug_client.h"
#include "utils/wrench_debug_eval.h"
#include "utils/wrench_debug_host.h"
#include "utils/wrench_project.h"

#include <filesystem>

#include <thread>
#include <atomic>
#include <chrono>

namespace
{

int g_checks = 0;
int g_failed = 0;

#define DCHECK(cond) do { ++g_checks; if ( !(cond) ) { ++g_failed; printf( "\n  debugger check failed (%s:%d): %s", __FILE__, __LINE__, #cond ); } } while ( 0 )

class TestTarget
{
public:
	TestTarget() : m_quit( false ) {}
	~TestTarget() { stop(); }

	// host driven: the "firmware" loads the program itself and calls
	// onTick(counter) over and over, like an embedded main loop would
	void start( const std::vector<uint8_t>* hostProgram = nullptr )
	{
		if ( hostProgram )
		{
			m_hostProgram = *hostProgram;
		}
		m_thread = std::thread( [this]() { run(); } );
	}

	void stop()
	{
		if ( m_thread.joinable() )
		{
			m_quit = true;
			m_pipe.close();
			m_thread.join();
		}
	}

	WrenchDebugPipe& pipe() { return m_pipe; }
	std::atomic<int> hostCalls{ 0 };
	bool allowUpload = true; // set before start()
	int entryWait = -1; // >= 0 arms breakOnEntry( entryWait ) before the host runs its program
	uint32_t linkTimeout = 0; // setLinkTimeout() value

private:
	static void print( WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr )
	{
		for ( int i = 0; i < argn; ++i )
		{
			WRValue::MallocStrScoped text( argv[i] );
			((WrenchDebugServer*)usr)->print( text, (int)text.size() );
		}
	}

	static void println( WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr )
	{
		print( c, argv, argn, retVal, usr );
		((WrenchDebugServer*)usr)->print( "\n" );
	}

	void run()
	{
		WRState* w = wr_newState( 256 );
		wr_loadAllLibs( w );
		WrenchDebugServer server( w, m_pipe.server() );
		server.setAbortOnDisconnect( true );
		server.setAllowUpload( allowUpload );
		server.setLinkTimeout( linkTimeout );
		wr_registerFunction( w, "print", print, &server );
		wr_registerFunction( w, "println", println, &server );

		bool hostDriven = !m_hostProgram.empty();
		if ( hostDriven )
		{
			server.setHostDriven( true );
			server.loadBytes( m_hostProgram.data(), (int)m_hostProgram.size() );
			if ( entryWait >= 0 )
			{
				server.breakOnEntry( (uint32_t)entryWait );
			}
			wr_executeContext( server.context() );
		}

		int counter = 0;
		while ( !m_quit )
		{
			server.tick();
			if ( hostDriven && server.context() )
			{
				WRValue arg;
				arg.init( counter++ );
				wr_callFunction( server.context(), "onTick", &arg, 1 );
				++hostCalls;
				std::this_thread::sleep_for( std::chrono::milliseconds(1) );
			}
			else
			{
				m_pipe.server()->idle();
			}
		}

		server.detach();
		wr_destroyState( w );
	}

	WrenchDebugPipe m_pipe;
	std::thread m_thread;
	std::atomic<bool> m_quit;
	std::vector<uint8_t> m_hostProgram;
};

bool compile( const char* source, std::vector<uint8_t>& out, WrenchDebugProgram& program )
{
	unsigned char* bytes = 0;
	int len = 0;
	WRstr err;
	if ( wr_compile(source, (int)strlen(source), &bytes, &len, &err,
									WR_INCLUDE_GLOBALS | WR_EMBED_DEBUG_CODE | WR_EMBED_SOURCE_CODE) != WR_ERR_None )
	{
		printf( "\n  compile failed: %s", err.c_str() );
		return false;
	}
	out.assign( bytes, bytes + len );
	g_free( bytes );
	return program.parse( out.data(), (uint32_t)out.size() );
}

// wait for the next event of a type, collecting output on the way
bool waitFor( WrenchDebugClient& client, WrenchDebugEvent::Type type, WrenchDebugEvent& e, std::string* output = nullptr, int timeoutMs = 5000 )
{
	auto start = std::chrono::steady_clock::now();
	while ( std::chrono::steady_clock::now() - start < std::chrono::milliseconds(timeoutMs) )
	{
		if ( !client.waitEvent(e, 50) )
		{
			continue;
		}
		if ( e.type == WrenchDebugEvent::Output && output )
		{
			*output += e.text;
		}
		if ( e.type == type )
		{
			return true;
		}
	}
	return false;
}

std::string evalText( WrenchDebugClient& client, const WrenchDebugProgram& program, const char* expr )
{
	std::vector<WrenchDebugStackFrame> frames;
	client.getStack( frames, &program, 0 );
	WrenchDebugEvaluator ev( client, program, frames, 0 );
	WrenchDebugValue v;
	std::string error;
	if ( !ev.evaluate(expr, v, error) )
	{
		return "error: " + error;
	}
	return ev.describe( v, 0 );
}

const char* c_program =
	"var g = 10;\n" // 1
	"var s = \"hello\";\n" // 2
	"var arr[] = { 1, 2.5, \"x\" };\n" // 3
	"struct Pt\n" // 4
	"{\n" // 5
	"  var x;\n" // 6
	"  var y = 7;\n" // 7
	"};\n" // 8
	"var p = new Pt;\n" // 9
	"var h = { \"a\":1, 2:\"two\" };\n" // 10
	"function add( a, b )\n" // 11
	"{\n" // 12
	"  var c = a + b;\n" // 13
	"  return c;\n" // 14
	"}\n" // 15
	"function outer( n )\n" // 16
	"{\n" // 17
	"  var r = add( n, 1 );\n" // 18
	"  return r * 2;\n" // 19
	"}\n" // 20
	"var total = 0;\n" // 21
	"for( var i=0; i<3; ++i )\n" // 22
	"{\n" // 23
	"  total += outer( i );\n" // 24
	"}\n" // 25
	"println( \"total=\", total );\n" // 26
	"g = 99;\n"; // 27

void testServerDriven()
{
	std::vector<uint8_t> bytecode;
	WrenchDebugProgram program;
	DCHECK( compile(c_program, bytecode, program) );
	DCHECK( program.lines().count(13) == 1 );
	DCHECK( program.breakableLine(12) == 13 );

	TestTarget target;
	target.start();
	WrenchDebugClient client( target.pipe().client() );

	WrenchDebugTargetInfo info;
	DCHECK( client.connect(info) );
	DCHECK( info.protocol == WRD_PROTOCOL_VERSION );
	DCHECK( info.state == WRD_StateNoProgram );
	DCHECK( client.upload(bytecode) );
	DCHECK( client.refreshInfo(info) );
	DCHECK( info.programHash == program.hash() );

	// the target hands back exactly what we uploaded
	std::vector<uint8_t> readBack;
	DCHECK( client.readSection(WRD_SectionBytecode, readBack) );
	DCHECK( readBack == bytecode );

	WrenchDebugEvent e;
	std::string output;
	DCHECK( client.launch(true) );
	DCHECK( waitFor(client, WrenchDebugEvent::Stopped, e, &output) );
	DCHECK( e.reason == WRD_StopEntry && e.line == 1 );

	// breakpoint inside a nested call
	DCHECK( client.setBreakpoints({ 13 }) );
	DCHECK( client.resume() );
	DCHECK( waitFor(client, WrenchDebugEvent::Stopped, e, &output) );
	DCHECK( e.reason == WRD_StopBreakpoint && e.line == 13 );

	std::vector<WrenchDebugStackFrame> frames;
	DCHECK( client.getStack(frames, &program, e.line) );
	DCHECK( frames.size() == 3 );
	if ( frames.size() == 3 )
	{
		DCHECK( frames[0].name == "add" && frames[0].line == 13 );
		DCHECK( frames[1].name == "outer" && frames[1].line == 18 );
		DCHECK( frames[2].function == -1 && frames[2].line == 24 );
	}

	std::vector<WrenchDebugValue> locals;
	DCHECK( client.getLocals(0, locals) );
	DCHECK( locals.size() == 3 );
	if ( locals.size() == 3 )
	{
		DCHECK( locals[0].tag == WRD_ValInt && locals[0].i == 0 );
		DCHECK( locals[1].tag == WRD_ValInt && locals[1].i == 1 );
	}
	DCHECK( client.getLocals(1, locals) && locals.size() == 2 && locals[0].i == 0 );

	// step over the assignment, c becomes visible
	DCHECK( client.stepOver() );
	DCHECK( waitFor(client, WrenchDebugEvent::Stopped, e, &output) );
	DCHECK( e.reason == WRD_StopStep && e.line == 14 );
	DCHECK( client.getLocals(0, locals) && locals.size() == 3 && locals[2].i == 1 );

	// step out lands on the next line of the caller
	DCHECK( client.setBreakpoints({}) );
	DCHECK( client.stepOut() );
	DCHECK( waitFor(client, WrenchDebugEvent::Stopped, e, &output) );
	DCHECK( e.line == 19 );
	DCHECK( client.getStack(frames, &program, e.line) && frames.size() == 2 && frames[0].name == "outer" );
	DCHECK( evalText(client, program, "r") == "1" );
	DCHECK( evalText(client, program, "n + 10") == "10" );

	// step in from a line without calls stays in step semantics
	DCHECK( client.stepIn() );
	DCHECK( waitFor(client, WrenchDebugEvent::Stopped, e, &output) );
	DCHECK( client.getStack(frames, &program, e.line) && !frames.empty() && frames[0].function == -1 );

	// run to the end of the loop
	DCHECK( client.setBreakpoints({ 27 }) );
	DCHECK( client.resume() );
	DCHECK( waitFor(client, WrenchDebugEvent::Stopped, e, &output) );
	DCHECK( e.line == 27 );
	DCHECK( output == "total=12\n" );
	if ( output != "total=12\n" )
	{
		printf( "\n  output was [%s]", output.c_str() );
	}

	std::vector<WrenchDebugValue> globals;
	DCHECK( client.getGlobals(globals) );
	int totalSlot = program.globalSlot( "total" );
	DCHECK( totalSlot >= 0 && (size_t)totalSlot < globals.size() && globals[totalSlot].i == 12 );

	DCHECK( evalText(client, program, "arr[1]") == "2.5" );
	DCHECK( evalText(client, program, "arr[2]") == "x" );
	DCHECK( evalText(client, program, "p.y") == "7" );
	DCHECK( evalText(client, program, "h[\"a\"]") == "1" );
	DCHECK( evalText(client, program, "h[2]") == "two" );
	DCHECK( evalText(client, program, "s") == "hello" );
	DCHECK( evalText(client, program, "total > 10 && g == 10") == "1" );
	DCHECK( evalText(client, program, "arr[7]").find("out of range") != std::string::npos );
	DCHECK( evalText(client, program, "nope").find("no variable") != std::string::npos );

	// writes
	{
		DCHECK( client.getStack(frames, &program, e.line) );
		WrenchDebugEvaluator ev( client, program, frames, 0 );
		WrenchDebugValue result;
		std::string error;
		DCHECK( ev.assign("g", "42", result, error) && result.i == 42 );
		DCHECK( ev.assign("arr[0]", "\"replaced\"", result, error) && result.isString() );
		DCHECK( ev.assign("p.x", "1.5", result, error) && result.tag == WRD_ValFloat );
		DCHECK( ev.assign("h[\"a\"]", "g + 1", result, error) && result.i == 43 );
	}
	DCHECK( evalText(client, program, "g") == "42" );
	DCHECK( evalText(client, program, "arr[0]") == "replaced" );
	DCHECK( evalText(client, program, "p.x") == "1.5" );
	DCHECK( evalText(client, program, "h[\"a\"]") == "43" );

	// children carry names
	{
		WrenchDebugValue pv;
		WrenchDebugLocation where;
		std::string error;
		DCHECK( client.getStack(frames, &program, e.line) );
		WrenchDebugEvaluator ev( client, program, frames, 0 );
		DCHECK( ev.resolve("p", pv, where, error) && pv.tag == WRD_ValStruct );
		std::vector<WrenchDebugChild> kids;
		uint32_t total = 0;
		DCHECK( client.getChildren(pv, 0, pv.count, kids, total, &program) );
		DCHECK( kids.size() == 2 && kids[0].name == "x" && kids[1].name == "y" );
	}

	DCHECK( client.resume() );
	DCHECK( waitFor(client, WrenchDebugEvent::Ended, e, &output) );
	DCHECK( e.error == WR_ERR_None && !(e.flags & WRD_EndedAborted) );

	// globals stay readable after the program ends
	DCHECK( client.getGlobals(globals) );
	int gSlot = program.globalSlot( "g" );
	DCHECK( gSlot >= 0 && (size_t)gSlot < globals.size() && globals[gSlot].i == 99 );

	// a second launch starts from fresh globals
	DCHECK( client.setBreakpoints({ 26 }) );
	DCHECK( client.launch(false) );
	DCHECK( waitFor(client, WrenchDebugEvent::Stopped, e, &output) );
	DCHECK( e.line == 26 );
	DCHECK( evalText(client, program, "g") == "10" );
	DCHECK( client.terminate() );
	DCHECK( waitFor(client, WrenchDebugEvent::Ended, e, &output) );
	DCHECK( (e.flags & WRD_EndedAborted) != 0 );

	target.stop();
}

void testPauseAndErrors()
{
	const char* spin =
		"var n = 0;\n" // 1
		"while( 1 )\n" // 2
		"{\n" // 3
		"  n = n + 1;\n" // 4
		"}\n"; // 5

	std::vector<uint8_t> bytecode;
	WrenchDebugProgram program;
	DCHECK( compile(spin, bytecode, program) );

	TestTarget target;
	target.start();
	WrenchDebugClient client( target.pipe().client() );
	WrenchDebugTargetInfo info;
	DCHECK( client.connect(info) );
	DCHECK( client.upload(bytecode) );
	DCHECK( client.launch(false) );

	std::this_thread::sleep_for( std::chrono::milliseconds(100) );
	WrenchDebugEvent e;
	DCHECK( client.pause() );
	DCHECK( waitFor(client, WrenchDebugEvent::Stopped, e) );
	DCHECK( e.reason == WRD_StopPause && e.line == 4 );
	std::string n = evalText( client, program, "n" );
	DCHECK( atoi(n.c_str()) > 0 );

	// requests while running are refused cleanly
	DCHECK( client.resume() );
	std::vector<WrenchDebugValue> globals;
	DCHECK( !client.getGlobals(globals) );

	DCHECK( client.terminate() );
	DCHECK( waitFor(client, WrenchDebugEvent::Ended, e) );
	DCHECK( (e.flags & WRD_EndedAborted) != 0 && e.error == WR_ERR_debugger_terminated );

	// runtime errors stop with the frames still intact
	const char* failing =
		"function inner( v )\n" // 1
		"{\n" // 2
		"  missing( v );\n" // 3
		"}\n" // 4
		"inner( 5 );\n"; // 5
	DCHECK( compile(failing, bytecode, program) );
	DCHECK( client.upload(bytecode) );
	DCHECK( client.launch(false) );
	DCHECK( waitFor(client, WrenchDebugEvent::Stopped, e) );
	DCHECK( e.reason == WRD_StopError && e.error == WR_ERR_function_not_found && e.line == 3 );
	DCHECK( e.missingFunction == wr_hashStr("missing") );
	DCHECK( wr_describeMissingFunction(e.error, e.missingFunction, failing, nullptr) == "'missing' is not provided by the target" );
	std::vector<WrenchDebugStackFrame> frames;
	DCHECK( client.getStack(frames, &program, e.line) && frames.size() == 2 && frames[0].name == "inner" );
	DCHECK( evalText(client, program, "v") == "5" );
	DCHECK( client.resume() );
	DCHECK( waitFor(client, WrenchDebugEvent::Ended, e) );
	DCHECK( e.error == WR_ERR_function_not_found && e.missingFunction == wr_hashStr("missing") );

	target.stop();
}

// protocol edge cases: sequence numbers wrapping, stops announced again,
// relaunch with fresh globals, steps interrupted by a breakpoint, link timeout
void testProtocolRobustness()
{
	const char* source =
		"var count;\n" // 1
		"count = count + 1;\n" // 2
		"function inner( v )\n" // 3
		"{\n" // 4
		"  var r = v * 2;\n" // 5
		"  return r;\n" // 6
		"}\n" // 7
		"var x = inner( 3 );\n" // 8
		"var y = x + 1;\n" // 9
		"var z = 0;\n"; // 10
	std::vector<uint8_t> bytecode;
	WrenchDebugProgram program;
	DCHECK( compile(source, bytecode, program) );

	TestTarget target;
	target.start();
	WrenchDebugClient client( target.pipe().client() );
	WrenchDebugTargetInfo info;
	DCHECK( client.connect(info) );
	DCHECK( client.upload(bytecode) );
	WrenchDebugEvent e;

	// a Continue whose sequence number repeats an older control request
	// after the counter wrapped is still a new request
	DCHECK( client.launch(true) );
	DCHECK( waitFor(client, WrenchDebugEvent::Stopped, e) && e.reason == WRD_StopEntry );
	for ( int i = 0; i < 254; ++i )
	{
		client.refreshInfo( info );
	}
	DCHECK( client.resume() );
	DCHECK( waitFor(client, WrenchDebugEvent::Ended, e) && e.error == WR_ERR_None );

	// attaching (hello) or pausing while halted announces the stop again
	DCHECK( client.launch(true) );
	DCHECK( waitFor(client, WrenchDebugEvent::Stopped, e) );
	DCHECK( client.connect(info) && info.state == WRD_StateStopped );
	DCHECK( waitFor(client, WrenchDebugEvent::Stopped, e) && e.reason == WRD_StopEntry );
	DCHECK( client.pause() );
	DCHECK( waitFor(client, WrenchDebugEvent::Stopped, e) && e.reason == WRD_StopEntry );
	DCHECK( client.terminate() );
	DCHECK( waitFor(client, WrenchDebugEvent::Ended, e) );

	// a launch after a terminated run starts with fresh globals
	DCHECK( client.setBreakpoints({ 9 }) );
	DCHECK( client.launch(false) );
	DCHECK( waitFor(client, WrenchDebugEvent::Stopped, e) && e.line == 9 );
	DCHECK( evalText(client, program, "count") == "1" );
	DCHECK( client.terminate() );
	DCHECK( waitFor(client, WrenchDebugEvent::Ended, e) );
	DCHECK( client.launch(false) );
	DCHECK( waitFor(client, WrenchDebugEvent::Stopped, e) && e.line == 9 );
	DCHECK( evalText(client, program, "count") == "1" );
	DCHECK( client.terminate() );
	DCHECK( waitFor(client, WrenchDebugEvent::Ended, e) );

	// stepping over a call that hits a breakpoint: the client may let the
	// breakpoint go (false condition) and the step carries on
	DCHECK( client.setBreakpoints({ 8 }) );
	DCHECK( client.launch(false) );
	DCHECK( waitFor(client, WrenchDebugEvent::Stopped, e) && e.line == 8 );
	DCHECK( client.setBreakpoints({ 5 }) );
	DCHECK( client.stepOver() );
	DCHECK( waitFor(client, WrenchDebugEvent::Stopped, e) && e.reason == WRD_StopBreakpoint && e.line == 5 );
	DCHECK( client.resume(true) );
	DCHECK( waitFor(client, WrenchDebugEvent::Stopped, e) && e.reason == WRD_StopStep && e.line == 9 );
	DCHECK( client.setBreakpoints({}) );
	DCHECK( client.resume() );
	DCHECK( waitFor(client, WrenchDebugEvent::Ended, e) );
	target.stop();

	// a halted target lets go of a client it no longer hears from
	TestTarget quiet;
	quiet.linkTimeout = 300;
	quiet.start();
	WrenchDebugClient silent( quiet.pipe().client() );
	DCHECK( silent.connect(info) );
	DCHECK( silent.upload(bytecode) );
	DCHECK( silent.launch(true) );
	DCHECK( waitFor(silent, WrenchDebugEvent::Stopped, e) );
	std::this_thread::sleep_for( std::chrono::milliseconds(700) );
	DCHECK( waitFor(silent, WrenchDebugEvent::Ended, e, nullptr, 2000) );
	quiet.stop();
}

// firmware starts its script itself with breakOnEntry() armed: the script
// waits on its first line until a debugger attaches
void testBreakOnEntry()
{
	const char* firmware =
		"var ticks = 0;\n" // 1
		"function onTick( count )\n" // 2
		"{\n" // 3
		"  ticks = ticks + 1;\n" // 4
		"  return count;\n" // 5
		"}\n"; // 6
	std::vector<uint8_t> bytecode;
	WrenchDebugProgram program;
	DCHECK( compile(firmware, bytecode, program) );

	TestTarget target;
	target.entryWait = 5000;
	target.start( &bytecode );
	std::this_thread::sleep_for( std::chrono::milliseconds(200) );
	DCHECK( target.hostCalls == 0 ); // still inside the global code, waiting

	WrenchDebugClient client( target.pipe().client() );
	WrenchDebugTargetInfo info;
	DCHECK( client.connect(info) );
	DCHECK( info.state == WRD_StateStopped );
	WrenchDebugEvent e;
	DCHECK( waitFor(client, WrenchDebugEvent::Stopped, e) );
	DCHECK( e.reason == WRD_StopEntry && e.line == 1 );
	DCHECK( target.hostCalls == 0 );

	// breakpoints set while halted catch the firmware's first call
	DCHECK( client.setBreakpoints({ 4 }) );
	DCHECK( client.resume() );
	DCHECK( waitFor(client, WrenchDebugEvent::Stopped, e) );
	DCHECK( e.reason == WRD_StopBreakpoint && e.line == 4 );
	DCHECK( evalText(client, program, "count") == "0" );
	DCHECK( client.setBreakpoints({}) );
	DCHECK( client.resume() );
	client.detach();
	target.stop();

	// nobody attaches in time: the script runs on
	TestTarget alone;
	alone.entryWait = 50;
	alone.start( &bytecode );
	auto start = std::chrono::steady_clock::now();
	while ( alone.hostCalls == 0 && std::chrono::steady_clock::now() - start < std::chrono::seconds(3) )
	{
		std::this_thread::sleep_for( std::chrono::milliseconds(10) );
	}
	DCHECK( alone.hostCalls > 0 );
	alone.stop();
}

// firmware that refuses uploads keeps its own program, which can still be
// debugged
void testUploadRefused()
{
	const char* firmware =
		"var ticks = 0;\n" // 1
		"function onTick( count )\n" // 2
		"{\n" // 3
		"  ticks = ticks + 1;\n" // 4
		"  return count;\n" // 5
		"}\n"; // 6
	const char* other = "var x = 1;\n";

	std::vector<uint8_t> bytecode;
	std::vector<uint8_t> otherBytecode;
	WrenchDebugProgram program;
	WrenchDebugProgram otherProgram;
	DCHECK( compile(other, otherBytecode, otherProgram) );
	DCHECK( compile(firmware, bytecode, program) );

	TestTarget target;
	target.allowUpload = false;
	target.start( &bytecode );
	WrenchDebugClient client( target.pipe().client() );
	WrenchDebugTargetInfo info;
	DCHECK( client.connect(info) );
	DCHECK( (info.caps & WRD_CapLoad) == 0 && (info.caps & WRD_CapHostDriven) != 0 );

	DCHECK( !client.upload(otherBytecode) );
	DCHECK( client.lastError() == "the target does not accept uploaded programs" );
	DCHECK( client.refreshInfo(info) && info.programHash == program.hash() );

	// attach still works on the firmware's program
	WrenchDebugEvent e;
	DCHECK( client.setBreakpoints({ 4 }) );
	DCHECK( waitFor(client, WrenchDebugEvent::Stopped, e) );
	DCHECK( e.reason == WRD_StopBreakpoint && e.line == 4 );
	DCHECK( client.setBreakpoints({}) );
	DCHECK( client.resume() );

	client.detach();
	target.stop();
}

// the local target imitates the host application described by wrench.json:
// only its libraries, declared natives answer their "returns" value and
// anything else fails like it would in the application
void testProjectStandIn()
{
	std::error_code ec;
	std::filesystem::path dir = std::filesystem::temp_directory_path( ec ) / "wrench_project_test";
	std::filesystem::create_directories( dir, ec );
	std::string projectFile = (dir / "wrench.json").u8string();
	FILE* f = fopen( projectFile.c_str(), "wb" );
	DCHECK( f != nullptr );
	if ( !f )
	{
		return;
	}
	const char* json = R"json({
	"libraries": [ "math" ],
	"functions": [
		{ "name": "app::version", "params": [], "returns": 3 },
		{ "name": "greet", "params": [ "who" ], "doc": "says hello", "returns": "hi" },
		"log( values... )"
	]
})json";
	fputs( json, f );
	fclose( f );

	WrenchProject project;
	std::string error;
	DCHECK( wr_projectForScript((dir / "app.w").u8string(), "", project, error) && project.loaded() );
	DCHECK( project.path() == projectFile );
	DCHECK( project.native("greet") && project.native("greet")->minArgs == 1 && project.native("greet")->maxArgs == 1 );
	DCHECK( project.native("log") && project.native("log")->maxArgs == -1 );
	DCHECK( project.hasLibrary("math") && !project.hasLibrary("string") && !project.hasLibrary("host") );
	DCHECK( WrenchProject::libraryOf("str::concat") == "string" && WrenchProject::libraryOf("std::serialize") == "serialize" );

	const char* good =
		"var v = app::version();\n" // 1
		"var s = greet( \"you\" );\n" // 2
		"var m = math::sqrt( 16.0 );\n" // 3
		"log( 1, 2, 3 );\n" // 4
		"var done = 1;\n"; // 5
	std::vector<uint8_t> bytecode;
	WrenchDebugProgram program;
	DCHECK( compile(good, bytecode, program) );

	WrenchLocalTarget target;
	target.start( 0, &project );
	WrenchDebugClient client( target.clientTransport() );
	WrenchDebugTargetInfo info;
	DCHECK( client.connect(info) );
	DCHECK( client.upload(bytecode) );
	DCHECK( client.setBreakpoints({ 5 }) );
	DCHECK( client.launch(false) );
	WrenchDebugEvent e;
	std::string output;
	DCHECK( waitFor(client, WrenchDebugEvent::Stopped, e, &output) );
	DCHECK( e.reason == WRD_StopBreakpoint && e.line == 5 );
	DCHECK( evalText(client, program, "v") == "3" );
	DCHECK( evalText(client, program, "s").find("hi") != std::string::npos );
	DCHECK( atof(evalText(client, program, "m").c_str()) == 4.0 );
	DCHECK( output.find("app::version is provided by the host application, here it returns 3") != std::string::npos );
	DCHECK( client.resume() );
	DCHECK( waitFor(client, WrenchDebugEvent::Ended, e) && e.error == WR_ERR_None );

	// undeclared natives and libraries the host does not load fail
	const char* undeclared =
		"var a = 1;\n" // 1
		"openWindow( a );\n"; // 2
	DCHECK( compile(undeclared, bytecode, program) );
	DCHECK( client.upload(bytecode) );
	DCHECK( client.launch(false) );
	DCHECK( waitFor(client, WrenchDebugEvent::Stopped, e) );
	DCHECK( e.reason == WRD_StopError && e.error == WR_ERR_function_not_found && e.line == 2 );
	DCHECK( wr_describeMissingFunction(e.error, e.missingFunction, undeclared, &project) == "'openWindow' is not provided by the target" );
	DCHECK( client.resume() );
	DCHECK( waitFor(client, WrenchDebugEvent::Ended, e) );

	const char* noLibrary =
		"var t = str::concat( \"a\", \"b\" );\n"; // 1
	DCHECK( compile(noLibrary, bytecode, program) );
	DCHECK( client.upload(bytecode) );
	DCHECK( client.launch(false) );
	DCHECK( waitFor(client, WrenchDebugEvent::Stopped, e) );
	DCHECK( e.reason == WRD_StopError && e.error == WR_ERR_lib_function_not_found );
	DCHECK( wr_describeMissingFunction(e.error, e.missingFunction, noLibrary, &project)
			== "'str::concat' is not provided by the target (its 'string' library is not loaded there)" );
	DCHECK( client.resume() );
	DCHECK( waitFor(client, WrenchDebugEvent::Ended, e) );

	target.stop();
	std::filesystem::remove_all( dir, ec );
}

void testHostDriven()
{
	const char* firmware =
		"var ticks = 0;\n" // 1
		"function onTick( count )\n" // 2
		"{\n" // 3
		"  ticks = ticks + 1;\n" // 4
		"  var twice = count * 2;\n" // 5
		"  return twice;\n" // 6
		"}\n"; // 7

	std::vector<uint8_t> bytecode;
	WrenchDebugProgram program;
	DCHECK( compile(firmware, bytecode, program) );

	TestTarget target;
	target.start( &bytecode );
	WrenchDebugClient client( target.pipe().client() );
	WrenchDebugTargetInfo info;
	DCHECK( client.connect(info) );
	DCHECK( (info.caps & WRD_CapHostDriven) != 0 );
	DCHECK( info.programHash == program.hash() );

	// attach without uploading: break inside a function the host calls
	WrenchDebugEvent e;
	DCHECK( client.setBreakpoints({ 5 }) );
	DCHECK( waitFor(client, WrenchDebugEvent::Stopped, e) );
	DCHECK( e.reason == WRD_StopBreakpoint && e.line == 5 );

	std::vector<WrenchDebugStackFrame> frames;
	DCHECK( client.getStack(frames, &program, e.line) );
	DCHECK( frames.size() == 1 && frames[0].name == "onTick" );
	int calls = target.hostCalls;
	std::this_thread::sleep_for( std::chrono::milliseconds(50) );
	DCHECK( calls == target.hostCalls ); // the firmware is halted

	DCHECK( client.stepOver() );
	DCHECK( waitFor(client, WrenchDebugEvent::Stopped, e) );
	DCHECK( e.line == 6 );
	std::string count = evalText( client, program, "count" );
	std::string twice = evalText( client, program, "twice" );
	DCHECK( atoi(twice.c_str()) == 2 * atoi(count.c_str()) );

	// pause while the firmware is between calls
	DCHECK( client.setBreakpoints({}) );
	DCHECK( client.resume() );
	std::this_thread::sleep_for( std::chrono::milliseconds(30) );
	DCHECK( client.pause() );
	DCHECK( waitFor(client, WrenchDebugEvent::Stopped, e) );
	DCHECK( e.reason == WRD_StopPause );

	// detach lets the firmware run on
	DCHECK( client.detach() );
	calls = target.hostCalls;
	std::this_thread::sleep_for( std::chrono::milliseconds(50) );
	DCHECK( target.hostCalls > calls );

	target.stop();
}

const char* c_watchProgram =
	"var g = 0;\n" // 1
	"var arr[] = { 1, 2, 3 };\n" // 2
	"var h = { \"k\": 1, 5: 0 };\n" // 3
	"var s = \"abc\";\n" // 4
	"struct P { var x; };\n" // 5
	"var p = new P;\n" // 6
	"function bump( n )\n" // 7
	"{\n" // 8
	"  var v = n;\n" // 9
	"  v = v + 1;\n" // 10
	"  v = v * 2;\n" // 11
	"  return v;\n" // 12
	"}\n" // 13
	"g = 1;\n" // 14
	"arr[1] = 20;\n" // 15
	"arr[0] = 1;\n" // 16
	"h.k = 7;\n" // 17
	"h[5] = 9;\n" // 18
	"s[1] = 'x';\n" // 19
	"p.x = 3;\n" // 20
	"g = bump( 3 );\n" // 21
	"g = g + 0;\n" // 22
	"println( \"done\" );\n"; // 23

// the target watch id for a variable path, -1 when it cannot be watched
int watchExpr( WrenchDebugClient& client, const WrenchDebugProgram& program, const char* expr, int frame = 0, std::string* error = nullptr )
{
	std::vector<WrenchDebugStackFrame> frames;
	client.getStack( frames, &program, 0 );
	WrenchDebugEvaluator ev( client, program, frames, frame );
	WrenchDebugWatchPath path;
	WrenchDebugValue value;
	std::string why;
	int id = 0;
	if ( !ev.watchPath(expr, path, value, why) )
	{
		if ( error )
		{
			*error = why;
		}
		return -1;
	}
	if ( !client.addWatch(path, id) )
	{
		if ( error )
		{
			*error = client.lastError();
		}
		return -1;
	}
	return id;
}

void testWatchpoints()
{
	std::vector<uint8_t> bytecode;
	WrenchDebugProgram program;
	DCHECK( compile(c_watchProgram, bytecode, program) );

	TestTarget target;
	target.start();
	WrenchDebugClient client( target.pipe().client() );
	WrenchDebugTargetInfo info;
	DCHECK( client.connect(info) );
	DCHECK( (info.caps & WRD_CapWatch) && info.maxWatches == WRENCH_DEBUG_MAX_WATCHES );
	DCHECK( client.upload(bytecode) );

	WrenchDebugEvent e;
	std::string output;
	DCHECK( client.launch(true) );
	DCHECK( waitFor(client, WrenchDebugEvent::Stopped, e, &output) && e.reason == WRD_StopEntry );

	// a global: the stop is on the line after the write
	DCHECK( watchExpr(client, program, "g") == 1 );
	DCHECK( client.resume() );
	DCHECK( waitFor(client, WrenchDebugEvent::Stopped, e, &output) );
	DCHECK( e.reason == WRD_StopWatch && e.watch == 1 && e.line == 15 );

	// an element, a member of a hash table, an integer key; then the target
	// is full until one goes
	DCHECK( watchExpr(client, program, "arr[1]") == 2 );
	DCHECK( watchExpr(client, program, "h.k") == 3 );
	DCHECK( watchExpr(client, program, "h[5]") == 4 );
	std::string error;
	DCHECK( watchExpr(client, program, "s[1]", 0, &error) == -1 && error.find("no room") != std::string::npos );
	DCHECK( client.removeWatch(1) );
	DCHECK( watchExpr(client, program, "s[1]") == 1 );

	DCHECK( client.resume() );
	DCHECK( waitFor(client, WrenchDebugEvent::Stopped, e, &output) );
	DCHECK( e.reason == WRD_StopWatch && e.watch == 2 && e.line == 16 );
	DCHECK( evalText(client, program, "arr[1]") == "20" );

	// writing the value it already has is no change
	DCHECK( client.resume() );
	DCHECK( waitFor(client, WrenchDebugEvent::Stopped, e, &output) );
	DCHECK( e.reason == WRD_StopWatch && e.watch == 3 && e.line == 18 );

	DCHECK( client.resume() );
	DCHECK( waitFor(client, WrenchDebugEvent::Stopped, e, &output) );
	DCHECK( e.reason == WRD_StopWatch && e.watch == 4 && e.line == 19 );

	// a struct member takes the place of the integer key
	DCHECK( client.removeWatch(4) );
	DCHECK( watchExpr(client, program, "p.x") == 4 );

	// a character of a string
	DCHECK( client.resume() );
	DCHECK( waitFor(client, WrenchDebugEvent::Stopped, e, &output) );
	DCHECK( e.reason == WRD_StopWatch && e.watch == 1 && e.line == 20 );

	DCHECK( client.resume() );
	DCHECK( waitFor(client, WrenchDebugEvent::Stopped, e, &output) );
	DCHECK( e.reason == WRD_StopWatch && e.watch == 4 && e.line == 21 );

	// what the debugger itself writes while stopped is no change either
	{
		std::vector<WrenchDebugStackFrame> frames;
		DCHECK( client.getStack(frames, &program, e.line) );
		WrenchDebugEvaluator ev( client, program, frames, 0 );
		WrenchDebugValue result;
		DCHECK( ev.assign("arr[1]", "5", result, error) );
	}
	DCHECK( client.setBreakpoints({ 10 }) );
	DCHECK( client.resume() );
	DCHECK( waitFor(client, WrenchDebugEvent::Stopped, e, &output) );
	DCHECK( e.reason == WRD_StopBreakpoint && e.line == 10 );

	// paths that name no variable
	DCHECK( watchExpr(client, program, "g + 1", 0, &error) == -1 && error.find("not a variable") != std::string::npos );
	DCHECK( watchExpr(client, program, "arr[7]", 0, &error) == -1 && error.find("out of range") != std::string::npos );

	// a local lives as long as its call
	DCHECK( client.removeWatch(0) );
	DCHECK( client.setBreakpoints({}) );
	DCHECK( watchExpr(client, program, "v") == 1 );
	DCHECK( client.resume() );
	DCHECK( waitFor(client, WrenchDebugEvent::Stopped, e, &output) );
	DCHECK( e.reason == WRD_StopWatch && e.watch == 1 && e.line == 11 );
	DCHECK( evalText(client, program, "v") == "4" );
	DCHECK( client.resume() );
	DCHECK( waitFor(client, WrenchDebugEvent::Stopped, e, &output) );
	DCHECK( e.reason == WRD_StopWatch && e.line == 12 );
	DCHECK( client.resume() );
	DCHECK( waitFor(client, WrenchDebugEvent::WatchRemoved, e, &output) && e.watch == 1 );
	DCHECK( waitFor(client, WrenchDebugEvent::Ended, e, &output) && e.error == 0 );
	DCHECK( output == "done\n" );

	target.stop();
}

// a for loop written over several lines steps through each part on its own
// line: body, increment, condition, body again
void testForLoopLines()
{
	const char* source =
		"var s = 0;\n" // 1
		"for( var i = 0;\n" // 2
		"     i < 2;\n" // 3
		"     ++i )\n" // 4
		"{\n" // 5
		"  s += i;\n" // 6
		"}\n" // 7
		"s = s + 1;\n"; // 8
	std::vector<uint8_t> bytecode;
	WrenchDebugProgram program;
	DCHECK( compile(source, bytecode, program) );
	DCHECK( program.lines().count(4) == 1 && program.lines().count(3) == 1 );

	TestTarget target;
	target.start();
	WrenchDebugClient client( target.pipe().client() );
	WrenchDebugTargetInfo info;
	DCHECK( client.connect(info) );
	DCHECK( client.upload(bytecode) );
	DCHECK( client.setBreakpoints({ 6 }) );
	DCHECK( client.launch(false) );

	WrenchDebugEvent e;
	DCHECK( waitFor(client, WrenchDebugEvent::Stopped, e) && e.line == 6 );
	DCHECK( client.setBreakpoints({}) );
	int expected[] = { 4, 3, 6, 4, 3, 8 };
	for ( int line : expected )
	{
		DCHECK( client.stepOver() );
		DCHECK( waitFor(client, WrenchDebugEvent::Stopped, e) && e.reason == WRD_StopStep && e.line == line );
	}
	DCHECK( client.resume() );
	DCHECK( waitFor(client, WrenchDebugEvent::Ended, e) );
	target.stop();
}

void testLinkNoise()
{
	// framing survives garbage, truncated frames and bad CRCs
	uint8_t buf[300];
	WrenchDebugDecoder decoder( buf, sizeof(buf) );
	uint8_t frame[64];
	frame[WRD_FRAME_HEADER] = 0x42;
	uint32_t len = wr_debugFinishFrame( frame, WRD_MsgPing, 7, 1 );

	const uint8_t garbage[] = { 0x00, 0xA5, 0x57, 0x01, 0xFF, 0xFF, 0xA5, 0x13, 0xA5 };
	int frames = 0;
	uint32_t now = 1000;
	for ( uint8_t b : garbage )
	{
		frames += decoder.feed( b, now ) ? 1 : 0;
	}
	// the stalled partial frame is dropped after the timeout
	now += WRD_FRAME_TIMEOUT_MS + 1;
	for ( uint32_t i = 0; i < len; ++i )
	{
		frames += decoder.feed( frame[i], now ) ? 1 : 0;
	}
	DCHECK( frames == 1 && decoder.frame().seq == 7 && decoder.frame().payload[0] == 0x42 );

	frame[WRD_FRAME_HEADER] ^= 1; // CRC no longer matches
	for ( uint32_t i = 0; i < len; ++i )
	{
		DCHECK( !decoder.feed(frame[i], now) );
	}
	DCHECK( decoder.crcErrors() == 1 );

	// a whole session over a link that corrupts traffic toward the client
	std::vector<uint8_t> bytecode;
	WrenchDebugProgram program;
	DCHECK( compile(c_program, bytecode, program) );

	TestTarget target;
	target.pipe().corruptEvery( 701 );
	target.start();
	WrenchDebugClient client( target.pipe().client() );
	client.setTimeout( 150, 8 );
	WrenchDebugTargetInfo info;
	DCHECK( client.connect(info) );
	DCHECK( client.upload(bytecode) );
	DCHECK( client.setBreakpoints({ 27 }) );
	DCHECK( client.launch(false) );

	WrenchDebugEvent e;
	bool stopped = false;
	for ( int i = 0; i < 40 && !stopped; ++i )
	{
		// events can be lost on a noisy link, poll the state instead
		if ( client.waitEvent(e, 50) && e.type == WrenchDebugEvent::Stopped )
		{
			stopped = true;
		}
		else if ( client.refreshInfo(info) && info.state == WRD_StateStopped )
		{
			stopped = true;
		}
	}
	DCHECK( stopped );

	int good = 0;
	for ( int i = 0; i < 20; ++i )
	{
		std::vector<WrenchDebugValue> globals;
		if ( client.getGlobals(globals) && globals.size() == program.functions()[0].locals.size() )
		{
			++good;
		}
	}
	DCHECK( good == 20 );

	client.terminate();
	target.stop();
}

}

bool testDebugger()
{
	printf( "test [-][debugger protocol]: " );
	fflush( stdout );
	g_checks = 0;
	g_failed = 0;

	testLinkNoise();
	testServerDriven();
	testWatchpoints();
	testForLoopLines();
	testPauseAndErrors();
	testHostDriven();
	testUploadRefused();
	testBreakOnEntry();
	testProtocolRobustness();
	testProjectStandIn();

	if ( g_failed )
	{
		printf( "\nFAIL (%d of %d checks)\n", g_failed, g_checks );
		return false;
	}
	printf( "PASS (%d checks)\n", g_checks );
	return true;
}

#endif
