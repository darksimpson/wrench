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

#include "debug/debugger.h"
#include "utils/wrench_debug_host.h"
#include "utils/wrench_debug_eval.h"

#include <signal.h>
#include <atomic>
#include <algorithm>

namespace
{

std::atomic<bool> g_interrupt( false );

void onInterrupt( int )
{
	g_interrupt = true;
	signal( SIGINT, onInterrupt );
}

const char* stopReasonText( uint8_t reason )
{
	switch ( reason )
	{
		case WRD_StopEntry: return "entry";
		case WRD_StopBreakpoint: return "breakpoint";
		case WRD_StopStep: return "step";
		case WRD_StopPause: return "paused";
		case WRD_StopError: return "runtime error";
		case WRD_StopWatch: return "watch";
		default: return "stopped";
	}
}

std::vector<std::string> splitLines( const std::string& text )
{
	std::vector<std::string> lines;
	std::string cur;
	for ( size_t i = 0; i < text.size(); ++i )
	{
		char c = text[i];
		if ( c == '\r' )
		{
			if ( i + 1 < text.size() && text[i + 1] == '\n' )
			{
				++i;
			}
			lines.push_back( cur );
			cur.clear();
		}
		else if ( c == '\n' )
		{
			lines.push_back( cur );
			cur.clear();
		}
		else
		{
			cur += c;
		}
	}
	if ( !cur.empty() )
	{
		lines.push_back( cur );
	}
	return lines;
}

std::string trim( const std::string& s )
{
	size_t a = s.find_first_not_of( " \t\r\n" );
	if ( a == std::string::npos )
	{
		return std::string();
	}
	size_t b = s.find_last_not_of( " \t\r\n" );
	return s.substr( a, b - a + 1 );
}

class CliDebugger
{
public:
	CliDebugger( FILE* in ) : m_in( in ) {}
	int run( const char* sourcePath, const char* target, bool attach, const char* projectPath );

private:
	bool loadProgram( std::string& error );
	bool fetchProgram();
	bool start();
	void waitForStop();
	void onStopped( const WrenchDebugEvent& e );
	void showLocation();
	void showBacktrace();
	void showVariables( bool locals, bool globals );
	void showDisplays();
	void listSource( int center, int radius );
	void toggleBreakpoint( int line );
	bool syncBreakpoints();
	void addWatch( const std::string& expr );
	void removeWatch( const std::string& arg );
	void listWatches();
	void onWatchRemoved( int id );
	std::string watchName( int id ) const;
	void printExpression( const std::string& expr );
	void setExpression( const std::string& text );
	void help();
	bool refreshStack();

	FILE* m_in;
	std::string m_sourcePath;
	WrenchTransportSpec m_spec;
	bool m_attach = false;
	WrenchClientLink m_link;
	std::unique_ptr<WrenchDebugClient> m_client;
	WrenchDebugProgram m_program;
	WrenchProject m_project;
	std::string m_source;
	bool m_sourceIsBytecode = false;
	std::vector<std::string> m_lines;
	std::vector<int> m_breakpoints;
	std::map<int, std::string> m_watches; // target watch id, expression
	std::vector<std::string> m_displays;
	std::vector<WrenchDebugStackFrame> m_frames;
	int m_frame = 0;
	int m_stopLine = 0;
	bool m_stopped = false;
	bool m_running = false;
	bool m_hex = false;
};

// a .w file is compiled, a .bin is used as it is with its embedded source
bool CliDebugger::loadProgram( std::string& error )
{
	std::vector<uint8_t> bytecode;
	std::string source;
	bool isBytecode = false;
	if ( !wr_loadForDebug(m_sourcePath, bytecode, source, error, &isBytecode) )
	{
		return false;
	}
	if ( !m_program.parse(bytecode.data(), (uint32_t)bytecode.size(), &error) )
	{
		return false;
	}
	m_lines = splitLines( source );
	m_source = source;
	m_sourceIsBytecode = isBytecode;
	if ( isBytecode && source.empty() )
	{
		printf( "%s carries no source (compile it with -debugsource), only stacks and variables are available\n", m_sourcePath.c_str() );
	}
	return true;
}

// attach without a file: symbols and source come from the target
bool CliDebugger::fetchProgram()
{
	std::vector<uint8_t> remote;
	std::string error;
	if ( !m_client->readSection(WRD_SectionBytecode, remote) || !m_program.parse(remote.data(), (uint32_t)remote.size(), &error) )
	{
		printf( "cannot read the program from the target: %s\n", error.empty() ? m_client->lastError().c_str() : error.c_str() );
		return false;
	}
	m_source = m_program.source();
	m_lines = splitLines( m_source );
	printf( "read the program from the target (%u bytes, %s)\n", (unsigned)remote.size(),
					m_program.hasSource() ? "source embedded" : "no source, compile with -debugsource to see it" );
	return true;
}

bool CliDebugger::syncBreakpoints()
{
	if ( !m_client->setBreakpoints(m_breakpoints) )
	{
		printf( "breakpoints not set: %s\n", m_client->lastError().c_str() );
		return false;
	}
	return true;
}

bool CliDebugger::start()
{
	std::string error;
	if ( !m_link.open(m_spec, error, 5000, m_project.loaded() ? &m_project : nullptr) )
	{
		printf( "%s\n", error.c_str() );
		return false;
	}

	m_client.reset( new WrenchDebugClient(m_link.transport()) );
	if ( m_spec.kind == WrenchTransportSpec::Serial )
	{
		m_client->setTimeout( 2000, 3 );
	}

	WrenchDebugTargetInfo info;
	if ( !m_client->connect(info) )
	{
		printf( "no answer from target (%s): %s\n", m_spec.describe().c_str(), m_client->lastError().c_str() );
		return false;
	}
	printf( "connected to %s, wrench %s, max packet %u bytes\n",
					m_spec.describe().c_str(), info.wrenchVersion.c_str(), (unsigned)info.maxPayload );

	if ( m_attach && m_sourcePath.empty() )
	{
		if ( !fetchProgram() )
		{
			return false;
		}
	}
	else if ( m_attach )
	{
		// the firmware may have been built with other flags (-debug without
		// the source): compile the local source the same way so the code
		// offsets, and the build hash, match
		if ( info.programHash != m_program.hash() && info.compilerFlags != m_program.compilerFlags()
				&& (info.compilerFlags & WR_EMBED_DEBUG_CODE) && !m_sourceIsBytecode )
		{
			std::vector<uint8_t> matching;
			std::string source;
			std::string error;
			WrenchDebugProgram rebuilt;
			if ( wr_compileWithFlags(m_sourcePath, matching, source, error, info.compilerFlags)
					&& rebuilt.parse(matching.data(), (uint32_t)matching.size(), &error) )
			{
				m_program = rebuilt;
			}
		}
		if ( info.programHash != m_program.hash() )
		{
			printf( "warning: the target runs a different build of %s than the local one\n", m_sourcePath.c_str() );
		}
	}
	else if ( !(info.caps & WRD_CapLoad) )
	{
		printf( "the target does not accept uploaded programs (its firmware called setAllowUpload(false)), use -attach\n" );
		return false;
	}
	else
	{
		if ( !m_client->upload(m_program.bytecode()) )
		{
			printf( "upload failed: %s\n", m_client->lastError().c_str() );
			return false;
		}
	}

	return syncBreakpoints();
}

bool CliDebugger::refreshStack()
{
	m_frames.clear();
	if ( !m_client->getStack(m_frames, &m_program, m_stopLine) )
	{
		printf( "cannot read the stack: %s\n", m_client->lastError().c_str() );
		return false;
	}
	if ( m_frame >= (int)m_frames.size() )
	{
		m_frame = 0;
	}
	return true;
}

void CliDebugger::onStopped( const WrenchDebugEvent& e )
{
	m_stopped = true;
	m_running = false;
	m_stopLine = e.line;
	m_frame = 0;
	if ( e.reason == WRD_StopError )
	{
		printf( "\n*** runtime error %s at line %d\n", wr_debugErrorText(e.error).c_str(), e.line );
		std::string missing = wr_describeMissingFunction( e.error, e.missingFunction, m_source, m_project.loaded() ? &m_project : nullptr );
		if ( !missing.empty() )
		{
			printf( "*** %s\n", missing.c_str() );
		}
	}
	else if ( e.reason == WRD_StopWatch )
	{
		printf( "\n[watch %d: %s changed] line %d\n", e.watch, watchName(e.watch).c_str(), e.line );
	}
	else
	{
		printf( "\n[%s] line %d\n", stopReasonText(e.reason), e.line );
	}
	refreshStack();
	showLocation();
	if ( e.reason == WRD_StopWatch && m_watches.count(e.watch) )
	{
		printExpression( m_watches[e.watch] );
	}
	showDisplays();
}

std::string CliDebugger::watchName( int id ) const
{
	auto it = m_watches.find( id );
	return it == m_watches.end() ? std::string( "?" ) : it->second;
}

void CliDebugger::onWatchRemoved( int id )
{
	printf( "\n[watch %d: %s went out of scope]\n", id, watchName(id).c_str() );
	m_watches.erase( id );
}

void CliDebugger::addWatch( const std::string& expr )
{
	if ( !m_stopped || m_frames.empty() )
	{
		printf( "watches are set while stopped: variables are looked up in the selected frame\n" );
		return;
	}
	WrenchDebugEvaluator ev( *m_client, m_program, m_frames, m_frame );
	WrenchDebugWatchPath path;
	WrenchDebugValue value;
	std::string error;
	if ( !ev.watchPath(expr, path, value, error) )
	{
		printf( "cannot watch %s: %s\n", expr.c_str(), error.c_str() );
		return;
	}
	int id = 0;
	if ( !m_client->addWatch(path, id) )
	{
		printf( "cannot watch %s: %s\n", expr.c_str(), m_client->lastError().c_str() );
		return;
	}
	m_watches[id] = expr;
	printf( "watch %d: %s = %s\n", id, expr.c_str(), ev.describe(value).c_str() );
}

void CliDebugger::removeWatch( const std::string& arg )
{
	if ( arg.empty() )
	{
		if ( m_client->removeWatch(0) )
		{
			m_watches.clear();
			printf( "all watches removed\n" );
		}
		else
		{
			printf( "%s\n", m_client->lastError().c_str() );
		}
		return;
	}
	int id = atoi( arg.c_str() );
	if ( !m_watches.count(id) )
	{
		printf( "no watch %s\n", arg.c_str() );
		return;
	}
	if ( !m_client->removeWatch(id) )
	{
		printf( "%s\n", m_client->lastError().c_str() );
		return;
	}
	printf( "watch %d (%s) removed\n", id, m_watches[id].c_str() );
	m_watches.erase( id );
}

void CliDebugger::listWatches()
{
	if ( m_watches.empty() )
	{
		printf( "no watches\n" );
	}
	for ( const auto& w : m_watches )
	{
		printf( "  %d: %s\n", w.first, w.second.c_str() );
	}
}

void CliDebugger::waitForStop()
{
	m_running = true;
	m_stopped = false;
	g_interrupt = false;
	bool pauseSent = false;

	for ( ;; )
	{
		if ( g_interrupt )
		{
			g_interrupt = false;
			if ( pauseSent || !m_client->pause() )
			{
				// the target does not stop (code without debug information,
				// or no answer): give the prompt back instead of hanging
				printf( "\n(the target did not stop, no longer waiting for it)\n" );
				return;
			}
			printf( "\n(pausing, Ctrl+C again to stop waiting)\n" );
			pauseSent = true;
		}

		if ( !m_client->linkUp() )
		{
			m_running = false;
			m_stopped = false;
			printf( "\n[target disconnected]\n" );
			return;
		}

		WrenchDebugEvent e;
		if ( !m_client->waitEvent(e, 100) )
		{
			continue;
		}

		switch ( e.type )
		{
			case WrenchDebugEvent::Output:
			{
				fwrite( e.text.data(), 1, e.text.size(), stdout );
				fflush( stdout );
				break;
			}
			case WrenchDebugEvent::Stopped:
			{
				onStopped( e );
				return;
			}
			case WrenchDebugEvent::Ended:
			{
				m_running = false;
				m_stopped = false;
				if ( e.flags & WRD_EndedAborted )
				{
					printf( "\n[terminated]\n" );
				}
				else if ( e.error )
				{
					printf( "\n[ended with %s near line %d]\n", wr_debugErrorText(e.error).c_str(), e.line );
					std::string missing = wr_describeMissingFunction( e.error, e.missingFunction, m_source, m_project.loaded() ? &m_project : nullptr );
					if ( !missing.empty() )
					{
						printf( "[%s]\n", missing.c_str() );
					}
				}
				else
				{
					printf( "\n[ended, returned %s]\n", e.value.display().c_str() );
				}
				return;
			}
			case WrenchDebugEvent::Loaded:
			{
				break;
			}
			case WrenchDebugEvent::WatchRemoved:
			{
				onWatchRemoved( e.watch );
				break;
			}
			case WrenchDebugEvent::Disconnected:
			{
				m_running = false;
				m_stopped = false;
				printf( "\n[target disconnected]\n" );
				return;
			}
		}
	}
}

void CliDebugger::listSource( int center, int radius )
{
	int first = (std::max)( 1, center - radius );
	int last = (std::min)( (int)m_lines.size(), center + radius );
	int current = (!m_frames.empty() && m_stopped) ? m_frames[m_frame].line : -1;
	for ( int l = first; l <= last; ++l )
	{
		bool bp = std::find( m_breakpoints.begin(), m_breakpoints.end(), l ) != m_breakpoints.end();
		printf( "%c%s%4d  %s\n", bp ? '*' : ' ', l == current ? ">" : " ", l, m_lines[l - 1].c_str() );
	}
}

void CliDebugger::showLocation()
{
	if ( m_frames.empty() )
	{
		return;
	}
	const WrenchDebugStackFrame& f = m_frames[m_frame];
	printf( "in %s, line %d\n", f.name.c_str(), f.line );
	if ( !m_lines.empty() )
	{
		listSource( f.line, 2 );
	}
}

void CliDebugger::showBacktrace()
{
	for ( size_t i = 0; i < m_frames.size(); ++i )
	{
		printf( "%c#%d  %s  line %d\n", (int)i == m_frame ? '>' : ' ', (int)i, m_frames[i].name.c_str(), m_frames[i].line );
	}
}

void CliDebugger::showVariables( bool locals, bool globals )
{
	WrenchDebugEvaluator ev( *m_client, m_program, m_frames, m_frame );

	if ( locals && !m_frames.empty() && m_frames[m_frame].function >= 0 )
	{
		std::vector<WrenchDebugValue> values;
		if ( m_client->getLocals(m_frame, values) )
		{
			printf( "locals:\n" );
			for ( size_t i = 0; i < values.size(); ++i )
			{
				printf( "  %s = %s\n", m_program.localName(m_frames[m_frame].function, (int)i).c_str(),
								ev.describe(values[i]).c_str() );
			}
		}
	}

	if ( globals )
	{
		std::vector<WrenchDebugValue> values;
		if ( m_client->getGlobals(values) )
		{
			printf( "globals:\n" );
			for ( size_t i = 0; i < values.size(); ++i )
			{
				printf( "  %s = %s\n", m_program.globalName((int)i).c_str(), ev.describe(values[i]).c_str() );
			}
		}
		else
		{
			printf( "globals unavailable: %s\n", m_client->lastError().c_str() );
		}
	}
}

void CliDebugger::showDisplays()
{
	for ( const std::string& d : m_displays )
	{
		printExpression( d );
	}
}

void CliDebugger::printExpression( const std::string& expr )
{
	WrenchDebugEvaluator ev( *m_client, m_program, m_frames, m_frame );
	WrenchDebugValue v;
	std::string error;
	if ( !ev.evaluate(expr, v, error) )
	{
		printf( "%s: %s\n", expr.c_str(), error.c_str() );
		return;
	}
	std::string text = (m_hex && v.tag == WRD_ValInt) ? v.display( true ) : ev.describe( v, 2 );
	printf( "%s = %s\n", expr.c_str(), text.c_str() );
}

void CliDebugger::setExpression( const std::string& text )
{
	size_t eq = WrenchDebugEvaluator::findAssignment( text );
	if ( eq == std::string::npos )
	{
		printf( "usage: set <variable> = <value>\n" );
		return;
	}
	WrenchDebugEvaluator ev( *m_client, m_program, m_frames, m_frame );
	WrenchDebugValue result;
	std::string error;
	std::string path = trim( text.substr(0, eq) );
	if ( !ev.assign(path, trim(text.substr(eq + 1)), result, error) )
	{
		printf( "cannot set %s: %s\n", path.c_str(), error.c_str() );
		return;
	}
	printf( "%s = %s\n", path.c_str(), ev.describe(result).c_str() );
}

void CliDebugger::toggleBreakpoint( int line )
{
	// a breakpoint asked for on a line without code sits on the next line
	// with code: toggling the same request again removes it from there
	int actual = m_program.breakableLine( line );
	auto it = std::find( m_breakpoints.begin(), m_breakpoints.end(), line );
	if ( it == m_breakpoints.end() && actual >= 0 )
	{
		it = std::find( m_breakpoints.begin(), m_breakpoints.end(), actual );
	}
	if ( it != m_breakpoints.end() )
	{
		printf( "breakpoint removed from line %d\n", *it );
		m_breakpoints.erase( it );
	}
	else
	{
		if ( actual < 0 )
		{
			printf( "no code at or after line %d\n", line );
			return;
		}
		if ( actual != line )
		{
			printf( "line %d has no code, using line %d\n", line, actual );
		}
		m_breakpoints.push_back( actual );
		printf( "breakpoint at line %d\n", actual );
	}
	syncBreakpoints();
}

void CliDebugger::help()
{
	printf( "r, run            launch the program (or restart it)\n"
					"c, continue       continue running\n"
					"n, next           step over\n"
					"s, step           step into\n"
					"o, finish         step out of the current function\n"
					"b <line>          toggle a breakpoint, 'b' alone lists them\n"
					"watch <var>       stop when a variable, element or member changes: x, a[2], s.member\n"
					"                  ('watch' alone lists them)\n"
					"unwatch [n]       remove watch n, or all of them\n"
					"bt, where         show the call stack\n"
					"f <n>             select stack frame n\n"
					"l [line]          list source around the current line or 'line'\n"
					"p <expr>          print an expression: x, a[2], s.member, h[\"key\"], x + 1 > 3\n"
					"set <var> = <v>   change a variable\n"
					"locals, globals   show variables\n"
					"d <expr>          display an expression at every stop, again to remove\n"
					"x                 toggle hex display of integers\n"
					"kill              terminate the program\n"
					"q, quit           detach and exit\n"
					"Ctrl+C pauses a running program.\n" );
}

int CliDebugger::run( const char* sourcePath, const char* target, bool attach, const char* projectPath )
{
	m_sourcePath = sourcePath ? sourcePath : "";
	m_attach = attach;

	std::string error;
	if ( !m_spec.parse(target ? target : "local", error) )
	{
		printf( "%s\n", error.c_str() );
		return -1;
	}
	if ( m_sourcePath.empty() && (!m_attach || m_spec.kind == WrenchTransportSpec::Local) )
	{
		printf( "without a file the program comes from the target: use -attach with -target tcp:... or serial:...\n" );
		return -1;
	}

	// a project applies to a file, or when named explicitly
	if ( (!m_sourcePath.empty() || projectPath) && !wr_projectForScript(m_sourcePath, projectPath ? projectPath : "", m_project, error) )
	{
		printf( "%s\n", error.c_str() );
		return -1;
	}
	if ( m_project.loaded() )
	{
		printf( "host application described by %s\n", m_project.path().c_str() );
	}
	if ( !m_sourcePath.empty() && !loadProgram(error) )
	{
		printf( "%s\n", error.c_str() );
		return -1;
	}
	if ( !start() )
	{
		return -1;
	}
	if ( !m_program.hasDebugCode() )
	{
		printf( "the program was compiled without debug information (-debug or -debugsource): it runs, but cannot stop at breakpoints or step\n" );
	}

	printf( "%s: %d lines, %d functions. 'h' for help.\n", m_sourcePath.empty() ? "target program" : m_sourcePath.c_str(),
					(int)m_lines.size(), (int)m_program.functions().size() - 1 );

	signal( SIGINT, onInterrupt );

	char buf[1024];
	for ( ;; )
	{
		// collect output and events that arrived while we were idle
		WrenchDebugEvent e;
		while ( m_client->waitEvent(e, 0) )
		{
			if ( e.type == WrenchDebugEvent::Output )
			{
				fwrite( e.text.data(), 1, e.text.size(), stdout );
			}
			else if ( e.type == WrenchDebugEvent::Stopped )
			{
				onStopped( e );
			}
			else if ( e.type == WrenchDebugEvent::Ended )
			{
				m_running = false;
				m_stopped = false;
				printf( "\n[ended%s%s]\n", e.error ? " with " : "", e.error ? wr_debugErrorText(e.error).c_str() : "" );
			}
			else if ( e.type == WrenchDebugEvent::WatchRemoved )
			{
				onWatchRemoved( e.watch );
			}
			else if ( e.type == WrenchDebugEvent::Disconnected )
			{
				m_running = false;
				m_stopped = false;
				printf( "\n[target disconnected]\n" );
			}
		}

		printf( m_stopped ? "(wdb) " : "(wdb:idle) " );
		fflush( stdout );
		if ( !fgets(buf, sizeof(buf), m_in) )
		{
			break;
		}
		if ( m_in != stdin )
		{
			printf( "%s", buf ); // echo scripted commands
		}

		std::string line = trim( buf );
		if ( line.empty() )
		{
			continue;
		}
		size_t sp = line.find( ' ' );
		std::string cmd = line.substr( 0, sp );
		std::string arg = sp == std::string::npos ? std::string() : trim( line.substr(sp + 1) );

		if ( cmd == "q" || cmd == "quit" )
		{
			break;
		}
		else if ( cmd == "h" || cmd == "help" || cmd == "?" )
		{
			help();
		}
		else if ( cmd == "r" || cmd == "run" )
		{
			if ( m_stopped || m_running )
			{
				m_client->terminate();
				WrenchDebugEvent ended;
				while ( m_client->waitEvent(ended, 2000) && ended.type != WrenchDebugEvent::Ended )
				{
				}
			}
			m_stopped = false;
			if ( !m_client->launch(false) )
			{
				printf( "launch failed: %s\n", m_client->lastError().c_str() );
				continue;
			}
			waitForStop();
		}
		else if ( cmd == "c" || cmd == "continue" )
		{
			if ( !m_stopped )
			{
				// nothing to continue: waiting would hang until Ctrl+C
				printf( "not stopped, 'r' runs the program\n" );
				continue;
			}
			if ( !m_client->resume() )
			{
				printf( "%s\n", m_client->lastError().c_str() );
				continue;
			}
			waitForStop();
		}
		else if ( cmd == "n" || cmd == "next" || cmd == "s" || cmd == "step" || cmd == "o" || cmd == "finish" )
		{
			if ( !m_stopped )
			{
				printf( "not stopped\n" );
				continue;
			}
			bool ok = (cmd[0] == 'n') ? m_client->stepOver() : (cmd[0] == 's') ? m_client->stepIn() : m_client->stepOut();
			if ( !ok )
			{
				printf( "%s\n", m_client->lastError().c_str() );
				continue;
			}
			waitForStop();
		}
		else if ( cmd == "b" || cmd == "break" )
		{
			if ( arg.empty() )
			{
				for ( int l : m_breakpoints )
				{
					printf( "  line %d: %s\n", l, (l >= 1 && l <= (int)m_lines.size()) ? trim(m_lines[l - 1]).c_str() : "" );
				}
			}
			else
			{
				toggleBreakpoint( atoi(arg.c_str()) );
			}
		}
		else if ( cmd == "watch" )
		{
			if ( arg.empty() )
			{
				listWatches();
			}
			else
			{
				addWatch( arg );
			}
		}
		else if ( cmd == "unwatch" )
		{
			removeWatch( arg );
		}
		else if ( cmd == "bt" || cmd == "where" || cmd == "w" )
		{
			showBacktrace();
		}
		else if ( cmd == "f" || cmd == "frame" )
		{
			int f = atoi( arg.c_str() );
			if ( f < 0 || f >= (int)m_frames.size() )
			{
				printf( "no frame %d\n", f );
			}
			else
			{
				m_frame = f;
				showLocation();
			}
		}
		else if ( cmd == "l" || cmd == "list" )
		{
			if ( m_lines.empty() )
			{
				printf( "no source, the program was compiled without -debugsource\n" );
				continue;
			}
			int center = arg.empty() ? ((m_stopped && !m_frames.empty()) ? m_frames[m_frame].line : 5) : atoi( arg.c_str() );
			listSource( center, 5 );
		}
		else if ( cmd == "p" || cmd == "print" )
		{
			printExpression( arg );
		}
		else if ( cmd == "set" )
		{
			setExpression( arg );
		}
		else if ( cmd == "locals" )
		{
			showVariables( true, false );
		}
		else if ( cmd == "globals" || cmd == "g" )
		{
			showVariables( false, true );
		}
		else if ( cmd == "d" || cmd == "display" )
		{
			auto it = std::find( m_displays.begin(), m_displays.end(), arg );
			if ( it != m_displays.end() )
			{
				m_displays.erase( it );
			}
			else if ( !arg.empty() )
			{
				m_displays.push_back( arg );
				printExpression( arg );
			}
		}
		else if ( cmd == "x" )
		{
			m_hex = !m_hex;
			printf( "hex %s\n", m_hex ? "on" : "off" );
		}
		else if ( cmd == "kill" )
		{
			if ( !m_client->terminate() )
			{
				printf( "%s\n", m_client->lastError().c_str() );
				m_stopped = m_stopped && m_client->linkUp();
				continue;
			}
			if ( m_stopped || m_running )
			{
				waitForStop();
			}
		}
		else
		{
			printf( "unknown command '%s', 'h' for help\n", cmd.c_str() );
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
	signal( SIGINT, SIG_DFL );
	return 0;
}

}

int wr_debugCliMain( const char* sourcePath, const char* target, bool attach, FILE* in, const char* projectPath )
{
	CliDebugger cli( in ? in : stdin );
	return cli.run( sourcePath, target, attach, projectPath );
}

#endif
