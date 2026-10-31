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
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>

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
	void leave();
	void restart();
	void waitForStop( bool restoreBreakpointsOnEntry = false );
	void onIdleEvent( const WrenchDebugEvent& e );
	void onStopped( const WrenchDebugEvent& e );
	void showLocation();
	void showBacktrace();
	void showVariables( bool locals, bool globals );
	void showDisplays();
	void listSource( int program, int file, int center, int radius );
	void toggleBreakpoint( const std::string& arg );
	bool syncBreakpoints();
	void clearTargetBreakpoints();
	void syncPrograms();
	void listPrograms();
	void onProgramEvent( const WrenchDebugEvent& e );
	const WrenchDebugProgram& symbols( int program ) const;
	const std::vector<std::string>& linesOf( int program, int file );
	std::string where( int program, int virtualLine ) const;
	int currentProgram() const;
	int currentFile() const;
	bool parseLocation( const std::string& arg, int& program, int& file, int& line ) const;
	void addWatch( const std::string& expr );
	void removeWatch( const std::string& arg );
	void listWatches();
	void onWatchRemoved( int id );
	std::string watchName( int id ) const;
	void printExpression( const std::string& expr );
	void setExpression( const std::string& text );
	void help();
	bool refreshStack();
	void readerLoop();
	void startReader();
	void stopReader();
	void prompt();
	char* readCommand( char* buf, int size );

	FILE* m_in;
	std::string m_sourcePath;
	WrenchTransportSpec m_spec;
	bool m_attach = false;
	WrenchClientLink m_link;
	std::unique_ptr<WrenchDebugClient> m_client;
	WrenchDebugProgram m_program; // program 0 as built here (or read from the target)
	WrenchDebugPrograms m_programs; // every program the target debugs
	WrenchProject m_project;
	std::string m_source;
	bool m_sourceIsBytecode = false;
	std::map<std::pair<int, int>, std::vector<std::string> > m_fileLines; // text of (program, file)
	std::vector<std::pair<int, int> > m_breakpoints; // (program, virtual line), as the target has them
	struct PendingBreakpoint
	{
		uint32_t hash; // of the program gone
		int line;
		std::string name;
	};
	std::vector<PendingBreakpoint> m_pendingBreakpoints; // set again when the program comes back
	std::map<int, std::string> m_watches; // target watch id, expression
	std::vector<std::string> m_displays;
	std::vector<WrenchDebugStackFrame> m_frames;
	int m_frame = 0;
	int m_stopLine = 0;
	bool m_stopped = false;
	bool m_running = false;
	bool m_hex = false;
	bool m_connected = false; // the target answered the hello

	// the prompt's line is read by a thread of its own, one line when asked
	// for, while this thread keeps serving the link: events (a stop in a
	// function the firmware called) show at once, and a halted remote
	// target, which may give up on a debugger it has not heard from
	// (setLinkTimeout), is pinged
	std::thread m_reader;
	std::mutex m_readLock;
	std::condition_variable m_readWake;
	bool m_readWanted = false;
	bool m_readDone = false;
	bool m_readEof = false;
	bool m_readQuit = false;
	std::string m_readLine;
};

void CliDebugger::readerLoop()
{
	std::unique_lock<std::mutex> lock( m_readLock );
	for ( ;; )
	{
		m_readWake.wait( lock, [this]() { return m_readWanted || m_readQuit; } );
		if ( m_readQuit )
		{
			return;
		}
		m_readWanted = false;
		lock.unlock();
		char buf[1024];
		char* got = fgets( buf, (int)sizeof(buf), m_in );
		lock.lock();
		m_readEof = !got;
		m_readLine = got ? buf : "";
		m_readDone = true;
		m_readWake.notify_all();
	}
}

void CliDebugger::startReader()
{
	m_reader = std::thread( [this]() { readerLoop(); } );
}

// only called while no line is asked for, so the reader is not inside fgets
void CliDebugger::stopReader()
{
	if ( !m_reader.joinable() )
	{
		return;
	}
	{
		std::lock_guard<std::mutex> lock( m_readLock );
		m_readQuit = true;
	}
	m_readWake.notify_all();
	m_reader.join();
}

void CliDebugger::prompt()
{
	printf( m_stopped ? "(wdb) " : "(wdb:idle) " );
	fflush( stdout );
}

// the next command line, null at the end of the input
char* CliDebugger::readCommand( char* buf, int size )
{
	std::unique_lock<std::mutex> lock( m_readLock );
	m_readWanted = true;
	m_readWake.notify_all();
	std::chrono::steady_clock::time_point lastPing = std::chrono::steady_clock::now();
	while ( !m_readDone )
	{
		m_readWake.wait_for( lock, std::chrono::milliseconds(50) );
		if ( m_readDone )
		{
			break;
		}
		lock.unlock();
		bool shown = false;
		WrenchDebugEvent e;
		while ( m_client->waitEvent(e, 0) )
		{
			onIdleEvent( e );
			shown = true;
		}
		std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
		if ( m_stopped && !m_link.isLocal() && m_client->linkUp() && now - lastPing >= std::chrono::seconds(1) )
		{
			m_client->ping();
			lastPing = now;
		}
		if ( shown )
		{
			prompt();
		}
		fflush( stdout );
		lock.lock();
	}
	m_readDone = false;
	if ( m_readEof )
	{
		return nullptr;
	}
	snprintf( buf, (size_t)size, "%s", m_readLine.c_str() );
	return buf;
}

// a .w file is compiled, a .bin is used as it is with its embedded source
bool CliDebugger::loadProgram( std::string& error )
{
	std::vector<uint8_t> bytecode;
	std::string source;
	bool isBytecode = false;
	if ( !wr_loadForDebug(m_sourcePath, bytecode, source, error, &isBytecode, m_project.loaded() ? &m_project : nullptr) )
	{
		return false;
	}
	if ( !m_program.parse(bytecode.data(), (uint32_t)bytecode.size(), &error) )
	{
		return false;
	}
	m_source = source;
	m_fileLines.clear();
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
	m_fileLines.clear();
	printf( "read the program from the target (%u bytes, %s)\n", (unsigned)remote.size(),
					m_program.hasSource() ? "source embedded" : "no source, compile with -debugsource to see it" );
	return true;
}

// the symbols of one of the target's programs, empty when unknown
const WrenchDebugProgram& CliDebugger::symbols( int program ) const
{
	static const WrenchDebugProgram none;
	const WrenchDebugProgram* p = m_programs.get( program );
	return p ? *p : (program == 0 ? m_program : none);
}

// the text of a file of a program; the main file of program 0 from the
// file given when the program carries none
const std::vector<std::string>& CliDebugger::linesOf( int program, int file )
{
	std::pair<int, int> key( program, file );
	auto it = m_fileLines.find( key );
	if ( it != m_fileLines.end() )
	{
		return it->second;
	}
	const std::vector<WrenchDebugProgram::SourceFile>& files = symbols( program ).files();
	std::string text;
	if ( file >= 0 && file < (int)files.size() && files[file].hasText )
	{
		text = files[file].text;
	}
	else if ( program == 0 && file == 0 )
	{
		text = m_source;
	}
	return m_fileLines[key] = splitLines( text );
}

// "line 12" in the main file, "line 3 of util.w" in an included one, and
// "in name" for a program other than program 0
std::string CliDebugger::where( int program, int virtualLine ) const
{
	const WrenchDebugProgram& p = symbols( program );
	int file = 0;
	int line = virtualLine;
	p.locate( virtualLine, file, line );
	std::string text = "line " + std::to_string( line );
	if ( file > 0 && file < (int)p.files().size() )
	{
		const std::string& name = p.files()[file].name;
		size_t slash = name.find_last_of( "/\\" );
		text += " of " + (slash == std::string::npos ? name : name.substr(slash + 1));
	}
	if ( program > 0 )
	{
		text += " in " + m_programs.displayName( program );
	}
	return text;
}

// the program of the selected frame while stopped, else program 0
int CliDebugger::currentProgram() const
{
	if ( !m_stopped || m_frames.empty() || m_frames[m_frame].program < 0 )
	{
		return 0;
	}
	return m_frames[m_frame].program;
}

// the file of the selected frame while stopped, else the main one
int CliDebugger::currentFile() const
{
	if ( !m_stopped || m_frames.empty() )
	{
		return 0;
	}
	int file = 0;
	int line = 0;
	symbols( currentProgram() ).locate( m_frames[m_frame].line, file, line );
	return file;
}

// "12" (a line of the current file) or "util.w:12" (a file of any program,
// by its name or the end of its path)
bool CliDebugger::parseLocation( const std::string& arg, int& program, int& file, int& line ) const
{
	size_t colon = arg.find_last_of( ':' );
	program = currentProgram();
	file = currentFile();
	if ( colon == std::string::npos )
	{
		line = atoi( arg.c_str() );
		return line > 0;
	}
	std::string name = arg.substr( 0, colon );
	line = atoi( arg.c_str() + colon + 1 );
	std::vector<int> ids = m_programs.ids();
	if ( std::find(ids.begin(), ids.end(), 0) == ids.end() )
	{
		ids.insert( ids.begin(), 0 );
	}
	for ( int p : ids )
	{
		const std::vector<WrenchDebugProgram::SourceFile>& files = symbols( p ).files();
		for ( size_t f = 0; f < files.size(); ++f )
		{
			const std::string& full = files[f].name;
			// either may be the longer path: included files are named
			// relative to the main file's folder ("lib/util.w")
			bool match = full == name
				|| (full.size() > name.size() && full.compare(full.size() - name.size(), name.size(), name) == 0
					&& (full[full.size() - name.size() - 1] == '/' || full[full.size() - name.size() - 1] == '\\'))
				|| (f > 0 && !full.empty() && name.size() > full.size() && name.compare(name.size() - full.size(), full.size(), full) == 0
					&& (name[name.size() - full.size() - 1] == '/' || name[name.size() - full.size() - 1] == '\\'));
			if ( match )
			{
				program = p;
				file = (int)f;
				return line > 0;
			}
		}
	}
	return false;
}

// every program's breakpoints to the target
bool CliDebugger::syncBreakpoints()
{
	std::vector<int> ids = m_programs.ids();
	if ( std::find(ids.begin(), ids.end(), 0) == ids.end() )
	{
		ids.push_back( 0 );
	}
	for ( int p : ids )
	{
		std::vector<int> lines;
		for ( const auto& bp : m_breakpoints )
		{
			if ( bp.first == p )
			{
				lines.push_back( bp.second );
			}
		}
		if ( !m_client->setBreakpoints(lines, p) )
		{
			printf( "breakpoints not set: %s\n", m_client->lastError().c_str() );
			return false;
		}
	}
	return true;
}

// the target's programs and their symbols (program 0 from the local file
// while it matches); breakpoints of a program gone wait until a program with
// the same bytecode is there again (a module imported by the next run)
void CliDebugger::syncPrograms()
{
	if ( m_program.valid() && !m_programs.get(0) )
	{
		m_programs.set( 0, m_program, true );
	}
	std::map<int, std::pair<uint32_t, std::string> > before;
	for ( int p : m_programs.ids() )
	{
		if ( const WrenchDebugProgramInfo* info = m_programs.info(p) )
		{
			before[p] = std::make_pair( info->hash, m_programs.displayName(p) );
		}
	}
	std::string error;
	if ( !m_programs.sync(*m_client, &error) )
	{
		printf( "cannot read all programs of the target: %s\n", error.c_str() );
	}
	m_fileLines.clear();

	std::vector<std::pair<int, int> > kept;
	for ( const auto& bp : m_breakpoints )
	{
		const WrenchDebugProgramInfo* info = m_programs.info( bp.first );
		auto was = before.find( bp.first );
		if ( bp.first == 0 || (info && (was == before.end() || was->second.first == info->hash)) )
		{
			kept.push_back( bp );
		}
		else if ( was != before.end() )
		{
			m_pendingBreakpoints.push_back( PendingBreakpoint{ was->second.first, bp.second, was->second.second } );
		}
	}
	m_breakpoints = kept;

	std::vector<PendingBreakpoint> waiting;
	for ( const auto& pending : m_pendingBreakpoints )
	{
		int found = -1;
		for ( int p : m_programs.ids() )
		{
			const WrenchDebugProgramInfo* info = m_programs.info( p );
			if ( p != 0 && info && info->hash == pending.hash )
			{
				found = p;
				break;
			}
		}
		if ( found < 0 )
		{
			waiting.push_back( pending );
		}
		else if ( std::find(m_breakpoints.begin(), m_breakpoints.end(), std::make_pair(found, pending.line)) == m_breakpoints.end() )
		{
			m_breakpoints.push_back( std::make_pair(found, pending.line) );
		}
	}
	m_pendingBreakpoints = waiting;
}

void CliDebugger::onProgramEvent( const WrenchDebugEvent& e )
{
	if ( e.type == WrenchDebugEvent::ProgramAdded )
	{
		syncPrograms();
		const WrenchDebugProgramInfo* info = m_programs.info( e.program );
		printf( "\n[program %d: %s%s]\n", e.program, m_programs.displayName(e.program).c_str(),
				(info && info->importer >= 0) ? (" imported by program " + std::to_string(info->importer)).c_str() : "" );
		syncBreakpoints();
	}
	else
	{
		printf( "\n[program %d (%s) is gone]\n", e.program, m_programs.displayName(e.program).c_str() );
		syncPrograms();
	}
}

void CliDebugger::listPrograms()
{
	for ( int p : m_programs.ids() )
	{
		const WrenchDebugProgramInfo* info = m_programs.info( p );
		const WrenchDebugProgram& s = symbols( p );
		printf( "%c%d  %s", p == currentProgram() ? '>' : ' ', p, m_programs.displayName(p).c_str() );
		if ( info && info->importer >= 0 )
		{
			printf( "  (imported by program %d)", info->importer );
		}
		if ( !s.hasDebugCode() )
		{
			printf( "  (no debug information)" );
		}
		else if ( s.files().size() > 1 )
		{
			printf( "  (%d files)", (int)s.files().size() );
		}
		printf( "\n" );
	}
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
	m_connected = true;
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
			if ( wr_compileWithFlags(m_sourcePath, matching, source, error, info.compilerFlags, m_project.loaded() ? &m_project : nullptr)
					&& rebuilt.parse(matching.data(), (uint32_t)matching.size(), &error) )
			{
				m_program = rebuilt;
				m_fileLines.clear();
			}
		}
		if ( info.programHash != m_program.hash() )
		{
			printf( "warning: the target runs a different build of %s than the local one\n", m_sourcePath.c_str() );
		}
	}
	else if ( !(info.caps & WRD_CapLaunch) )
	{
		printf( "the target runs its own program and takes no launches (its firmware did not call setLaunchable(true)), use -attach\n" );
		return false;
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

	// the target may debug more programs than the one given here
	m_programs.clear();
	syncPrograms();
	return syncBreakpoints();
}

// the end of a session, whichever way it ends once the target answered: a
// remote target is detached (it drops our breakpoints and runs on, not
// halted for a debugger that is gone), the local one ends
void CliDebugger::leave()
{
	stopReader();
	if ( m_connected && m_client && m_client->linkUp() )
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
	m_connected = false;
	m_link.close();
}

// r: run program 0 from its start. A run that is on is terminated first.
// That does not end a function the firmware called (a host driven target),
// and the firmware may call it again: the breakpoints are off the target
// until the new run halts on its first line, so a stop there cannot keep
// the target too busy to launch
void CliDebugger::restart()
{
	bool wasOn = m_stopped || m_running;
	if ( wasOn )
	{
		// only a run of program 0's global code ends with an Ended event
		bool globalCode = m_stopped && !m_frames.empty() && m_frames.back().function < 0 && m_frames.back().program <= 0;
		bool waitEnded = !m_stopped || globalCode;
		clearTargetBreakpoints();
		m_client->terminate();
		m_stopped = false;
		m_running = false;
		m_frames.clear();
		std::chrono::steady_clock::time_point until = std::chrono::steady_clock::now() + std::chrono::seconds( 2 );
		while ( waitEnded && std::chrono::steady_clock::now() < until )
		{
			WrenchDebugEvent e;
			if ( !m_client->waitEvent(e, 100) )
			{
				continue;
			}
			if ( e.type == WrenchDebugEvent::Ended )
			{
				break;
			}
			if ( e.type == WrenchDebugEvent::Stopped )
			{
				// a step or a watch of the run that is going away
				m_client->terminate();
			}
			else
			{
				onIdleEvent( e );
			}
		}
	}

	// a target still inside a call of the firmware takes the launch once
	// that call returned
	std::chrono::steady_clock::time_point until = std::chrono::steady_clock::now() + std::chrono::seconds( 3 );
	while ( !m_client->launch(wasOn) )
	{
		if ( m_client->lastError() != WrenchDebugClient::errorName(WRD_ErrBusy) || std::chrono::steady_clock::now() >= until )
		{
			printf( "launch failed: %s\n", m_client->lastError().c_str() );
			if ( wasOn )
			{
				syncBreakpoints();
			}
			return;
		}
		WrenchDebugEvent e;
		if ( !m_client->waitEvent(e, 100) )
		{
			continue;
		}
		if ( e.type == WrenchDebugEvent::Stopped )
		{
			m_client->terminate();
		}
		else
		{
			onIdleEvent( e );
		}
	}
	waitForStop( wasOn );
}

// every program's breakpoints off the target, ours are kept
void CliDebugger::clearTargetBreakpoints()
{
	std::vector<int> ids = m_programs.ids();
	if ( std::find(ids.begin(), ids.end(), 0) == ids.end() )
	{
		ids.push_back( 0 );
	}
	for ( int p : ids )
	{
		m_client->setBreakpoints( std::vector<int>(), p );
	}
}

bool CliDebugger::refreshStack()
{
	m_frames.clear();
	if ( !m_client->getStack(m_frames, m_programs, m_stopLine) )
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

// an event that came while no command waits for one
void CliDebugger::onIdleEvent( const WrenchDebugEvent& e )
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
	else if ( e.type == WrenchDebugEvent::ProgramAdded || e.type == WrenchDebugEvent::ProgramRemoved )
	{
		onProgramEvent( e );
	}
	else if ( e.type == WrenchDebugEvent::Disconnected )
	{
		m_running = false;
		m_stopped = false;
		printf( "\n[target disconnected]\n" );
	}
	fflush( stdout );
}

void CliDebugger::onStopped( const WrenchDebugEvent& e )
{
	m_stopped = true;
	m_running = false;
	m_stopLine = e.line;
	m_frame = 0;
	bool haveStack = refreshStack();
	if ( m_stopLine <= 0 && haveStack && !m_frames.empty() )
	{
		// a step that stopped where a call returned into the middle of a
		// line: the target has no line marker there, the program's line
		// table has the line of the top frame's pc
		m_stopLine = m_frames[0].line;
	}
	if ( e.reason == WRD_StopError )
	{
		printf( "\n*** runtime error %s at %s\n", wr_debugErrorText(e.error).c_str(), where(e.program, m_stopLine).c_str() );
		std::string missing = wr_describeMissingFunction( e.error, e.missingFunction, m_source, m_project.loaded() ? &m_project : nullptr );
		if ( !missing.empty() )
		{
			printf( "*** %s\n", missing.c_str() );
		}
	}
	else if ( e.reason == WRD_StopWatch )
	{
		printf( "\n[watch %d: %s changed] %s\n", e.watch, watchName(e.watch).c_str(), where(e.program, m_stopLine).c_str() );
	}
	else
	{
		printf( "\n[%s] %s\n", stopReasonText(e.reason), where(e.program, m_stopLine).c_str() );
	}
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
	WrenchDebugEvaluator ev( *m_client, m_programs, m_frames, m_frame );
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

// restoreBreakpointsOnEntry: the run was launched halted on its first line
// without breakpoints on the target (restart()): that stop puts them back
// and goes on, unless one of them is on that very line
void CliDebugger::waitForStop( bool restoreBreakpointsOnEntry )
{
	m_running = true;
	m_stopped = false;
	m_frames.clear(); // a running program has no stack to show
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
				if ( restoreBreakpointsOnEntry && e.reason == WRD_StopEntry )
				{
					restoreBreakpointsOnEntry = false;
					syncBreakpoints();
					if ( std::find(m_breakpoints.begin(), m_breakpoints.end(), std::make_pair((int)e.program, (int)e.line)) == m_breakpoints.end() )
					{
						if ( !m_client->resume() )
						{
							printf( "%s\n", m_client->lastError().c_str() );
							onStopped( e );
							return;
						}
						break;
					}
					WrenchDebugEvent hit = e;
					hit.reason = WRD_StopBreakpoint;
					onStopped( hit );
					return;
				}
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
					printf( "\n[ended with %s near %s]\n", wr_debugErrorText(e.error).c_str(), where(e.program, e.line).c_str() );
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
			case WrenchDebugEvent::ProgramAdded:
			case WrenchDebugEvent::ProgramRemoved:
			{
				onProgramEvent( e );
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

void CliDebugger::listSource( int program, int file, int center, int radius )
{
	const std::vector<std::string>& lines = linesOf( program, file );
	const WrenchDebugProgram& p = symbols( program );
	int first = (std::max)( 1, center - radius );
	int last = (std::min)( (int)lines.size(), center + radius );
	bool here = !m_frames.empty() && m_stopped && currentProgram() == program;
	int current = here ? m_frames[m_frame].line : -1;
	for ( int l = first; l <= last; ++l )
	{
		int v = p.virtualLine( file, l );
		bool bp = v > 0 && std::find( m_breakpoints.begin(), m_breakpoints.end(), std::make_pair(program, v) ) != m_breakpoints.end();
		printf( "%c%s%4d  %s\n", bp ? '*' : ' ', (v > 0 && v == current) ? ">" : " ", l, lines[l - 1].c_str() );
	}
}

void CliDebugger::showLocation()
{
	if ( m_frames.empty() )
	{
		return;
	}
	const WrenchDebugStackFrame& f = m_frames[m_frame];
	int program = f.program < 0 ? 0 : f.program;
	int file = 0;
	int line = f.line;
	symbols( program ).locate( f.line, file, line );
	printf( "in %s, %s\n", f.name.c_str(), where(program, f.line).c_str() );
	if ( !linesOf(program, file).empty() )
	{
		listSource( program, file, line, 2 );
	}
}

void CliDebugger::showBacktrace()
{
	for ( size_t i = 0; i < m_frames.size(); ++i )
	{
		int program = m_frames[i].program < 0 ? 0 : m_frames[i].program;
		printf( "%c#%d  %s  %s\n", (int)i == m_frame ? '>' : ' ', (int)i, m_frames[i].name.c_str(), where(program, m_frames[i].line).c_str() );
	}
}

void CliDebugger::showVariables( bool locals, bool globals )
{
	WrenchDebugEvaluator ev( *m_client, m_programs, m_frames, m_frame );
	int program = currentProgram();
	const WrenchDebugProgram& p = symbols( program );

	if ( locals && !m_frames.empty() && m_frames[m_frame].function >= 0 )
	{
		std::vector<WrenchDebugValue> values;
		if ( m_client->getLocals(m_frame, values) )
		{
			printf( "locals:\n" );
			for ( size_t i = 0; i < values.size(); ++i )
			{
				printf( "  %s = %s\n", p.localName(m_frames[m_frame].function, (int)i).c_str(),
								ev.describe(values[i]).c_str() );
			}
		}
	}

	if ( globals )
	{
		std::vector<WrenchDebugValue> values;
		if ( m_client->getGlobals(values, program) )
		{
			printf( program ? "globals of %s:\n" : "globals:\n", m_programs.displayName(program).c_str() );
			for ( size_t i = 0; i < values.size(); ++i )
			{
				printf( "  %s = %s\n", p.globalName((int)i).c_str(), ev.describe(values[i]).c_str() );
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
	WrenchDebugEvaluator ev( *m_client, m_programs, m_frames, m_frame );
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
	WrenchDebugEvaluator ev( *m_client, m_programs, m_frames, m_frame );
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

void CliDebugger::toggleBreakpoint( const std::string& arg )
{
	int program = 0;
	int file = 0;
	int line = 0;
	if ( !parseLocation(arg, program, file, line) )
	{
		printf( "no such line or file: %s\n", arg.c_str() );
		return;
	}

	// a breakpoint asked for on a line without code sits on the next line
	// with code: toggling the same request again removes it from there
	const WrenchDebugProgram& p = symbols( program );
	int requested = p.virtualLine( file, line );
	int actual = p.breakableLine( file, line );
	auto it = std::find( m_breakpoints.begin(), m_breakpoints.end(), std::make_pair(program, requested) );
	if ( it == m_breakpoints.end() && actual >= 0 )
	{
		it = std::find( m_breakpoints.begin(), m_breakpoints.end(), std::make_pair(program, actual) );
	}
	if ( it != m_breakpoints.end() )
	{
		printf( "breakpoint removed from %s\n", where(program, it->second).c_str() );
		m_breakpoints.erase( it );
	}
	else
	{
		if ( actual < 0 )
		{
			printf( "no code at or after %s\n", requested > 0 ? where(program, requested).c_str() : arg.c_str() );
			return;
		}
		if ( actual != requested )
		{
			printf( "%s has no code, using %s\n", requested > 0 ? where(program, requested).c_str() : arg.c_str(), where(program, actual).c_str() );
		}
		m_breakpoints.push_back( std::make_pair(program, actual) );
		printf( "breakpoint at %s\n", where(program, actual).c_str() );
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
					"b <line>          toggle a breakpoint, 'b' alone lists them; file.w:line\n"
					"                  for a file the program includes\n"
					"watch <var>       stop when a variable, element or member changes: x, a[2], s.member\n"
					"                  ('watch' alone lists them)\n"
					"unwatch [n]       remove watch n, or all of them\n"
					"bt, where         show the call stack\n"
					"f <n>             select stack frame n\n"
					"l [line]          list source around the current line or 'line' (or file.w:line)\n"
					"programs          the programs the target debugs (attached contexts, imports)\n"
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
		leave();
		return -1;
	}
	if ( !m_program.hasDebugCode() )
	{
		printf( "the program was compiled without debug information (-debug or -debugsource): it runs, but cannot stop at breakpoints or step\n" );
	}

	printf( "%s: %d lines, %d functions. 'h' for help.\n", m_sourcePath.empty() ? "target program" : m_sourcePath.c_str(),
					(int)linesOf(0, 0).size(), (int)m_program.functions().size() - 1 );
	if ( m_program.files().size() > 1 )
	{
		printf( "includes %d more files, 'b file.w:line' and 'l file.w:line' reach them\n", (int)m_program.files().size() - 1 );
	}
	if ( m_programs.ids().size() > 1 )
	{
		printf( "the target debugs %d programs ('programs' lists them):\n", (int)m_programs.ids().size() );
		listPrograms();
	}

	signal( SIGINT, onInterrupt );
	startReader();

	char buf[1024];
	for ( ;; )
	{
		// collect output and events that arrived meanwhile
		WrenchDebugEvent e;
		while ( m_client->waitEvent(e, 0) )
		{
			onIdleEvent( e );
		}

		prompt();
		if ( !readCommand(buf, (int)sizeof(buf)) )
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
			restart();
		}
		else if ( cmd == "c" || cmd == "continue" )
		{
			if ( !m_stopped )
			{
				// nothing to continue: waiting would hang until Ctrl+C, unless
				// the firmware runs contexts of its own, which stop when they do
				bool firmwareRuns = false;
				for ( int p : m_programs.ids() )
				{
					const WrenchDebugProgramInfo* info = m_programs.info( p );
					firmwareRuns = firmwareRuns || (p > 0 && info && !(info->flags & WRD_ProgramImported));
				}
				if ( !firmwareRuns )
				{
					printf( "not stopped, 'r' runs the program\n" );
					continue;
				}
				printf( "waiting for a program of the target to stop (Ctrl+C pauses)\n" );
				waitForStop();
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
				for ( const auto& bp : m_breakpoints )
				{
					int file = 0;
					int l = 0;
					symbols( bp.first ).locate( bp.second, file, l );
					const std::vector<std::string>& lines = linesOf( bp.first, file );
					printf( "  %s: %s\n", where(bp.first, bp.second).c_str(), (l >= 1 && l <= (int)lines.size()) ? trim(lines[l - 1]).c_str() : "" );
				}
				for ( const auto& pending : m_pendingBreakpoints )
				{
					printf( "  line %d in %s: pending, set when the program is loaded again\n", pending.line, pending.name.c_str() );
				}
			}
			else
			{
				toggleBreakpoint( arg );
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
			// around the current line, a line of the current file, or file:line
			int program = currentProgram();
			int file = currentFile();
			int center = 5;
			if ( !arg.empty() )
			{
				if ( !parseLocation(arg, program, file, center) )
				{
					printf( "no such line or file: %s\n", arg.c_str() );
					continue;
				}
			}
			else if ( m_stopped && !m_frames.empty() )
			{
				symbols( program ).locate( m_frames[m_frame].line, file, center );
			}
			if ( linesOf(program, file).empty() )
			{
				printf( "no source, the program was compiled without -debugsource\n" );
				continue;
			}
			listSource( program, file, center, 5 );
		}
		else if ( cmd == "programs" )
		{
			listPrograms();
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
			// a function the firmware called ends without an Ended event
			bool firmwareCall = m_stopped && !m_frames.empty() && (m_frames.back().function >= 0 || m_frames.back().program > 0);
			if ( !m_client->terminate() )
			{
				printf( "%s\n", m_client->lastError().c_str() );
				m_stopped = m_stopped && m_client->linkUp();
				continue;
			}
			if ( firmwareCall )
			{
				m_stopped = false;
				m_running = false;
				m_frames.clear();
				printf( "[terminated the call of the firmware]\n" );
			}
			else if ( m_stopped || m_running )
			{
				waitForStop();
			}
		}
		else
		{
			printf( "unknown command '%s', 'h' for help\n", cmd.c_str() );
		}
	}

	leave();
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
