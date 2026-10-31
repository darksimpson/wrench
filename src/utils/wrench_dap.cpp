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
#include "utils/wrench_include_host.h"

#include <stdio.h>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <memory>
#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
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

// a source breakpoint binds in every program that has its file (a file
// several programs include, a module imported again)
struct Binding
{
	int program = 0;
	int line = 0; // a virtual line of that program (the target's)
};

struct Breakpoint
{
	int id = 0;
	Json source; // as the client named it
	int requestedLine = 0; // a line of that file
	std::vector<Binding> bound;
	uint32_t parkedHash = 0; // in a copy of a program not on the target now: set there by its hash
	int parkedLine = 0; // the virtual line in that program
	bool verified = false;
	std::string message; // why it is not verified
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
	int program = 0; // whose symbols name the variables
	WrenchDebugValue value;
	std::string expr; // Children: how the container is reached (x, a[2].k), empty when unknown
};

// a watchpoint (DAP data breakpoint); its dataId carries the path the
// target follows, so a changed list keeps the watches that stay
struct DataBreakpoint
{
	int id = 0;
	std::string dataId;
	std::string expr;
	int watch = 0; // target watch id, 0 while not set there
	std::string message; // why it is not set
	std::string condition;
	std::string hitCondition;
	int hits = 0;
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

// a data breakpoint's dataId: the watch path, then '|' and the expression it
// came from. "g:3/i2/k12345|::a[2].k" is global slot 3, element 2, member
// hash 12345; a local root is "l:frame:function:slot", the function guards
// against naming a slot of another function later. A root in a program
// other than 0 ends with "@program"
std::string wr_encodeWatch( const WrenchDebugWatchPath& path, int function, const std::string& expr )
{
	std::string id = path.global ? "g:" + std::to_string( path.slot )
		: "l:" + std::to_string( path.frame ) + ":" + std::to_string( function ) + ":" + std::to_string( path.slot );
	if ( path.program > 0 )
	{
		id += "@" + std::to_string( path.program );
	}
	for ( const WrenchDebugWatchStep& s : path.steps )
	{
		id += (s.kind == WRD_WatchIndex) ? "/i" : "/k";
		id += std::to_string( s.value );
	}
	return id + "|" + expr;
}

bool wr_decodeWatch( const std::string& dataId, WrenchDebugWatchPath& path, int& function, std::string& expr )
{
	size_t bar = dataId.find( '|' );
	if ( bar == std::string::npos )
	{
		return false;
	}
	expr = dataId.substr( bar + 1 );
	std::string spec = dataId.substr( 0, bar );

	std::vector<std::string> parts;
	size_t start = 0;
	for ( size_t i = 0; i <= spec.size(); ++i )
	{
		if ( i == spec.size() || spec[i] == '/' )
		{
			parts.push_back( spec.substr(start, i - start) );
			start = i + 1;
		}
	}

	int frame = 0;
	int slot = 0;
	function = -1;
	path.program = 0;
	size_t at = parts[0].find( '@' );
	if ( at != std::string::npos )
	{
		path.program = atoi( parts[0].c_str() + at + 1 );
		parts[0].resize( at );
	}
	if ( sscanf(parts[0].c_str(), "g:%d", &slot) == 1 && parts[0][0] == 'g' )
	{
		path.global = true;
	}
	else if ( sscanf(parts[0].c_str(), "l:%d:%d:%d", &frame, &function, &slot) == 3 )
	{
		path.global = false;
	}
	else
	{
		return false;
	}
	path.frame = frame;
	path.slot = slot;
	path.steps.clear();

	for ( size_t i = 1; i < parts.size(); ++i )
	{
		if ( parts[i].size() < 2 || (parts[i][0] != 'i' && parts[i][0] != 'k') )
		{
			return false;
		}
		WrenchDebugWatchStep step;
		step.kind = (parts[i][0] == 'i') ? WRD_WatchIndex : WRD_WatchKey;
		step.value = (uint32_t)strtoul( parts[i].c_str() + 1, 0, 10 );
		path.steps.push_back( step );
	}
	return true;
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
	void onDataBreakpointInfo( const Json& req );
	void onSetDataBreakpoints( const Json& req );
	void onModules( const Json& req );
	void onControl( const Json& req, const std::string& command );
	void onDisconnect( const Json& req, bool terminateRequest );

	// target side
	void pollTarget();
	void handleStopped( const WrenchDebugEvent& e );
	void handleEnded( const WrenchDebugEvent& e );
	bool sendBreakpoints();
	void bindBreakpoint( Breakpoint& bp );
	Json breakpointJson( const Breakpoint& bp, bool sent );
	void resetStopState();
	bool ensureStack();
	static bool hitConditionMet( const std::string& hitCondition, int hits );
	int newRef( const VarRef& ref );
	Json variableJson( const std::string& name, const WrenchDebugValue& v, const std::string& evaluateName, bool hex, int frame, int program );
	std::string childExpression( const VarRef& parent, const WrenchDebugChild& child );
	bool setWatch( DataBreakpoint& dbp );
	void onWatchRemoved( int watch );
	void syncPrograms();
	void onProgramEvent( const WrenchDebugEvent& e );
	Json moduleJson( int program );
	const WrenchDebugProgram& symbols( int program ) const;
	int threadOf( int program ) const;
	int frameProgram( int frame ) const;
	bool hasSource( int program, int file );
	Json sourceJson( int program, int file );
	int embeddedReference( int program, int file );
	std::string embeddedFile( int program, int file );
	const WrenchDebugProgram* copiedProgram( uint32_t hash );
	std::vector<std::pair<int, int> > filesForSource( const Json& source, bool* known = nullptr );
	void endSession( int exitCode );

	JsonStdioStream m_stream;
	int m_seq = 1;
	bool m_quit = false;

	// session
	bool m_attach = false;
	bool m_uploaded = false; // this session sent program 0 to the target
	bool m_unresponsive = false; // the local target runs code that cannot answer
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
	WrenchDebugProgram m_program; // program 0
	WrenchDebugPrograms m_programs; // every program the target debugs, program 0 is m_program
	std::vector<int> m_threads; // the programs shown as threads (the ones not imported)
	std::string m_sourceText;
	WrenchProject m_project;
	WrenchDebugTargetInfo m_info;

	// the embedded source of a file of a program other than 0, sourceReference
	// FirstEmbedded + index: a module imported again keeps its reference
	struct EmbeddedSource
	{
		uint32_t hash; // of the program
		int file;
	};
	static const int FirstEmbedded = 1000;
	std::vector<EmbeddedSource> m_embedded;
	std::map<std::pair<uint32_t, int>, std::string> m_embeddedFiles; // (program hash, file) written this session
	std::map<uint32_t, std::unique_ptr<WrenchDebugProgram> > m_copiedPrograms; // read back from a copy's folder, null when none
	std::set<uint32_t> m_parkedSent; // program hashes the target holds parked breakpoints for

	std::vector<Breakpoint> m_breakpoints;
	std::vector<DataBreakpoint> m_dataBreakpoints;
	int m_nextBreakpointId = 1;

	// valid while stopped
	int m_stopLine = 0;
	int m_stopProgram = 0;
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

static std::string wr_baseName( const std::string& path )
{
	size_t slash = path.find_last_of( "/\\" );
	return slash == std::string::npos ? path : path.substr( slash + 1 );
}

// a breakpoint is at this line of this program
static bool wr_boundAt( const Breakpoint& bp, int program, int line )
{
	for ( const Binding& b : bp.bound )
	{
		if ( b.program == program && b.line == line )
		{
			return true;
		}
	}
	return false;
}

// two DAP sources naming one file
static bool wr_sameSource( const Json& a, const Json& b )
{
	int ra = a["sourceReference"].asInt( 0 );
	int rb = b["sourceReference"].asInt( 0 );
	if ( ra > 0 || rb > 0 )
	{
		return ra == rb;
	}
	return wr_samePath( a["path"].asString(""), b["path"].asString("") );
}

const WrenchDebugProgram& DapServer::symbols( int program ) const
{
	static const WrenchDebugProgram none;
	if ( program <= 0 )
	{
		return m_program;
	}
	const WrenchDebugProgram* s = m_programs.get( program );
	return s ? *s : none;
}

// the thread a program's code runs on: a module runs on its importer's
// stack, a program of its own (program 0, an attached context) is a thread
int DapServer::threadOf( int program ) const
{
	for ( int guard = 0; guard < WRD_NoProgram && program > 0; ++guard )
	{
		const WrenchDebugProgramInfo* info = m_programs.info( program );
		if ( !info || info->importer < 0 )
		{
			break;
		}
		program = info->importer;
	}
	return (program < 0 ? 0 : program) + 1;
}

int DapServer::frameProgram( int frame ) const
{
	return (frame >= 0 && frame < (int)m_frames.size() && m_frames[frame].program > 0) ? m_frames[frame].program : 0;
}

int DapServer::embeddedReference( int program, int file )
{
	const WrenchDebugProgramInfo* info = m_programs.info( program );
	uint32_t hash = info ? info->hash : 0;
	for ( size_t i = 0; i < m_embedded.size(); ++i )
	{
		if ( m_embedded[i].hash == hash && m_embedded[i].file == file )
		{
			return FirstEmbedded + (int)i;
		}
	}
	m_embedded.push_back( EmbeddedSource{ hash, file } );
	return FirstEmbedded + (int)m_embedded.size() - 1;
}

// an included file of program 0 on this machine: the file table names it
// relative to the main file's folder, it is looked for next to the program,
// then in the project's include folders (as named when it is nowhere)
static std::string wr_includedPath( const std::string& name, const std::string& programPath, const WrenchProject& project )
{
	std::vector<std::string> folders;
	if ( project.loaded() )
	{
		folders = project.includePaths();
	}
	std::string found = WrenchIncludeResolver::locate( name, programPath, folders );
	return found.empty() ? name : found;
}

// a file the target names that is on this machine: an absolute path, or
// any path when the program carries no copy of it
static bool wr_localFile( const WrenchDebugProgram::SourceFile& f )
{
	if ( f.name.empty() )
	{
		return false;
	}
	try
	{
		std::error_code ec;
		std::filesystem::path p = std::filesystem::u8path( f.name );
		return (p.is_absolute() || !f.hasText) && std::filesystem::is_regular_file( p, ec );
	}
	catch ( const std::exception& )
	{
		return false;
	}
}

// Embedded sources (a module compiled at run time, a program read from the
// target) are written to <temp>/wrench-sources/<program hash>/ as read-only
// files, so the client treats them as files. A breakpoint set in a virtual
// document (sourceReference) belongs to its session: the next session opens
// another document, the breakpoint no longer shows there and setting it
// again makes a second one. A file stays: the same text compiles to the same
// hash, so a module finds its file again, and its breakpoints with it
static const char* c_embeddedFolder = "wrench-sources";

static std::string wr_hashFolder( uint32_t hash )
{
	char text[16];
	snprintf( text, sizeof(text), "%08x", (unsigned)hash );
	return text;
}

// the name of each file of a program in its folder: its own, or one for a
// file without a usable name (a module compiled from a string has none); a
// second file of a name gets its index in front
static std::vector<std::string> wr_embeddedNames( const std::vector<WrenchDebugProgram::SourceFile>& files )
{
	std::vector<std::string> names;
	for ( size_t f = 0; f < files.size(); ++f )
	{
		std::string name = wr_baseName( files[f].name );
		if ( name.empty() || name == "." || name == ".." )
		{
			name = f > 0 ? "file" + std::to_string( f ) + ".w" : std::string( "module.w" );
		}
		if ( std::find(names.begin(), names.end(), name) != names.end() )
		{
			name = std::to_string( f ) + "-" + name;
		}
		names.push_back( name );
	}
	return names;
}

// which file of a program a copy's file name is, -1 for none
static int wr_embeddedFileIndex( const std::vector<WrenchDebugProgram::SourceFile>& files, const std::string& name )
{
	std::vector<std::string> names = wr_embeddedNames( files );
	for ( size_t f = 0; f < names.size(); ++f )
	{
		if ( wr_samePath(names[f], name) )
		{
			return (int)f;
		}
	}
	return -1;
}

// the image of a program, kept beside the copies of its files: a later
// session reads it to place breakpoints in a copy before the program is on
// the target again
static const char* c_embeddedImage = "image.bin";

// a read-only file with this content; one there already is written again
// only when it differs (the hash in the folder name names the content, a
// difference means it was changed by hand)
static bool wr_writeReadOnly( const std::filesystem::path& p, const std::string& data )
{
	std::error_code ec;
	std::string existing;
	std::ifstream in( p, std::ios::binary );
	if ( in )
	{
		existing.assign( std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() );
	}
	in.close();
	if ( existing == data && std::filesystem::exists(p, ec) )
	{
		return true;
	}
	std::filesystem::permissions( p, std::filesystem::perms::owner_write, std::filesystem::perm_options::add, ec );
	std::ofstream out( p, std::ios::binary | std::ios::trunc );
	out.write( data.data(), (std::streamsize)data.size() );
	out.close();
	bool ok = !out.fail();
	std::filesystem::permissions( p, std::filesystem::perms::owner_write | std::filesystem::perms::group_write | std::filesystem::perms::others_write,
		std::filesystem::perm_options::remove, ec );
	return ok;
}

static std::filesystem::path wr_embeddedFolder( uint32_t hash, std::error_code& ec )
{
	std::filesystem::path temp = std::filesystem::temp_directory_path( ec );
	return ec ? std::filesystem::path() : temp / c_embeddedFolder / wr_hashFolder( hash );
}

// a path in the folder of embedded sources: the program's hash and the file
// name. Only the last two folders count (the temp folder may be named in
// another form, a short 8.3 name on Windows)
static bool wr_embeddedPathParts( const std::string& path, uint32_t& hash, std::string& name )
{
	if ( path.empty() )
	{
		return false;
	}
	try
	{
		std::filesystem::path p = std::filesystem::u8path( path );
		std::string folder = p.parent_path().filename().u8string();
		if ( p.parent_path().parent_path().filename().u8string() != c_embeddedFolder || folder.size() != 8
			|| folder.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos )
		{
			return false;
		}
		hash = (uint32_t)strtoul( folder.c_str(), nullptr, 16 );
		name = p.filename().u8string();
		return true;
	}
	catch ( const std::exception& )
	{
		return false;
	}
}

// the file an embedded source is shown from, written when this session first
// needs it; empty when it cannot be, the source then goes by sourceReference
std::string DapServer::embeddedFile( int program, int file )
{
	const WrenchDebugProgram& s = symbols( program );
	const std::vector<WrenchDebugProgram::SourceFile>& files = s.files();
	if ( !s.valid() || file < 0 || file >= (int)files.size() || !files[file].hasText )
	{
		return std::string();
	}
	std::pair<uint32_t, int> key( s.hash(), file );
	auto known = m_embeddedFiles.find( key );
	if ( known != m_embeddedFiles.end() )
	{
		return known->second;
	}

	std::string result;
	try
	{
		std::error_code ec;
		std::filesystem::path folder = wr_embeddedFolder( s.hash(), ec );
		if ( !ec )
		{
			std::filesystem::create_directories( folder, ec );
			std::filesystem::path p = folder / std::filesystem::u8path( wr_embeddedNames(files)[file] );
			const std::vector<uint8_t>& image = s.bytecode();
			if ( wr_writeReadOnly(p, files[file].text) )
			{
				wr_writeReadOnly( folder / c_embeddedImage, std::string(image.begin(), image.end()) );
				std::filesystem::path full = std::filesystem::canonical( p, ec );
				result = (ec ? std::filesystem::absolute(p) : full).lexically_normal().u8string();
			}
		}
	}
	catch ( const std::exception& )
	{
		result.clear();
	}
	m_embeddedFiles[key] = result;
	return result;
}

// a program not on the target now, read back from the image beside the
// copies of its files (an earlier session wrote them); null when there is none
const WrenchDebugProgram* DapServer::copiedProgram( uint32_t hash )
{
	auto known = m_copiedPrograms.find( hash );
	if ( known != m_copiedPrograms.end() )
	{
		return known->second.get();
	}
	std::unique_ptr<WrenchDebugProgram> program;
	try
	{
		std::error_code ec;
		std::filesystem::path p = wr_embeddedFolder( hash, ec ) / c_embeddedImage;
		std::ifstream in( p, std::ios::binary );
		if ( !ec && in )
		{
			std::vector<uint8_t> image( (std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>() );
			program.reset( new WrenchDebugProgram );
			if ( !program->parse(image.data(), (uint32_t)image.size()) || program->hash() != hash )
			{
				program.reset();
			}
		}
	}
	catch ( const std::exception& )
	{
		program.reset();
	}
	const WrenchDebugProgram* result = program.get();
	m_copiedPrograms[hash] = std::move( program );
	return result;
}

// a stack frame in this file can show its source
bool DapServer::hasSource( int program, int file )
{
	if ( program == 0 )
	{
		return !m_programPath.empty() || m_program.hasSource() || file > 0;
	}
	const std::vector<WrenchDebugProgram::SourceFile>& files = symbols( program ).files();
	return file >= 0 && file < (int)files.size() && (files[file].hasText || wr_localFile(files[file]));
}

// a file of a program. Program 0's main file is the program (or its
// embedded source), an included one is the file the compiler read, or its
// embedded copy (sourceReference file + 1) when the program came from
// elsewhere. The other programs come from the target: a file of theirs
// that is on this machine is shown from there, any other from its copy
Json DapServer::sourceJson( int program, int file )
{
	Json s = Json::object();
	if ( program > 0 )
	{
		const std::vector<WrenchDebugProgram::SourceFile>& files = symbols( program ).files();
		WrenchDebugProgram::SourceFile f;
		if ( file >= 0 && file < (int)files.size() )
		{
			f = files[file];
		}
		if ( wr_localFile(f) )
		{
			std::string path = f.name;
			try
			{
				std::error_code ec;
				path = std::filesystem::absolute( std::filesystem::u8path(f.name), ec ).lexically_normal().u8string();
			}
			catch ( const std::exception& )
			{
			}
			s.set( "name", wr_baseName(f.name) );
			s.set( "path", path );
			return s;
		}
		std::string name = m_programs.displayName( program );
		if ( file > 0 )
		{
			name = wr_baseName( f.name ) + " of " + name;
		}
		s.set( "name", name + " (embedded source)" );
		std::string copy = embeddedFile( program, file );
		if ( !copy.empty() )
		{
			s.set( "path", copy );
		}
		else
		{
			s.set( "sourceReference", embeddedReference(program, file) );
		}
		return s;
	}

	const std::vector<WrenchDebugProgram::SourceFile>& files = m_program.files();
	std::string included = (file > 0 && file < (int)files.size()) ? files[file].name : std::string();
	if ( m_sourceFromTarget )
	{
		s.set( "name", file == 0 ? m_sourceName : wr_baseName(included) + " (embedded source)" );
		std::string copy = embeddedFile( 0, file );
		if ( !copy.empty() )
		{
			s.set( "path", copy );
		}
		else
		{
			s.set( "sourceReference", file + 1 );
		}
	}
	else if ( file == 0 )
	{
		s.set( "name", wr_baseName(m_programPath) );
		s.set( "path", m_programPath );
	}
	else
	{
		std::string path = wr_includedPath( included, m_programPath, m_project );
		try
		{
			std::error_code ec;
			path = std::filesystem::absolute( std::filesystem::u8path(path), ec ).lexically_normal().u8string();
		}
		catch ( const std::exception& )
		{
		}
		s.set( "name", wr_baseName(included) );
		s.set( "path", path );
	}
	return s;
}

// which files of which programs a DAP source is, as (program, file);
// 'known' is also true for the embedded source of a module not loaded now
std::vector<std::pair<int, int> > DapServer::filesForSource( const Json& source, bool* known )
{
	std::vector<std::pair<int, int> > found;
	int ref = source["sourceReference"].asInt( 0 );
	if ( known )
	{
		*known = false;
	}

	if ( ref >= FirstEmbedded )
	{
		if ( ref - FirstEmbedded < (int)m_embedded.size() )
		{
			const EmbeddedSource& e = m_embedded[ref - FirstEmbedded];
			for ( int p : m_programs.ids() )
			{
				const WrenchDebugProgramInfo* info = m_programs.info( p );
				if ( p > 0 && info && info->hash == e.hash )
				{
					found.push_back( std::make_pair(p, e.file) );
				}
			}
			if ( known )
			{
				*known = true;
			}
		}
		return found;
	}

	// a copy of an embedded source: the files of the programs with its hash
	// (none while that module is not loaded, it binds when it is)
	std::string path = source["path"].asString( "" );
	uint32_t copyHash = 0;
	std::string copyName;
	if ( wr_embeddedPathParts(path, copyHash, copyName) )
	{
		for ( int p : m_programs.ids() )
		{
			const WrenchDebugProgram& s = symbols( p );
			int f = (s.valid() && s.hash() == copyHash) ? wr_embeddedFileIndex( s.files(), copyName ) : -1;
			if ( f >= 0 )
			{
				found.push_back( std::make_pair(p, f) );
			}
		}
		if ( known )
		{
			*known = true;
		}
		return found;
	}

	const std::vector<WrenchDebugProgram::SourceFile>& files = m_program.files();
	if ( m_sourceFromTarget )
	{
		if ( ref >= 1 && ref <= (int)files.size() )
		{
			found.push_back( std::make_pair(0, ref - 1) );
		}
	}
	else if ( m_programPath.empty() || wr_samePath(path, m_programPath) )
	{
		found.push_back( std::make_pair(0, 0) );
	}
	else
	{
		for ( size_t f = 1; f < files.size(); ++f )
		{
			if ( !files[f].name.empty() && wr_samePath(path, wr_includedPath(files[f].name, m_programPath, m_project)) )
			{
				found.push_back( std::make_pair(0, (int)f) );
				break;
			}
		}
	}

	// files of the other programs that are on this machine
	if ( !path.empty() )
	{
		for ( int p : m_programs.ids() )
		{
			const std::vector<WrenchDebugProgram::SourceFile>& theirs = symbols( p ).files();
			for ( size_t f = 0; p > 0 && f < theirs.size(); ++f )
			{
				if ( wr_localFile(theirs[f]) && wr_samePath(path, theirs[f].name) )
				{
					found.push_back( std::make_pair(p, (int)f) );
					break;
				}
			}
		}
	}
	if ( known && !found.empty() )
	{
		*known = true;
	}
	return found;
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
	if ( !m_client->getStack(m_frames, m_programs, m_stopLine) )
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
	caps.set( "supportsDataBreakpoints", true );
	caps.set( "supportsModulesRequest", true );
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
	// (attach has nothing local to attach to, so it defaults to tcp)
	std::string target = args["target"].asString( "" );
	if ( target.empty() )
	{
		std::string transport = args["transport"].asString( attach ? "tcp" : "local" );
		if ( transport == "tcp" )
		{
			int port = args["port"].asInt( 0 );
			if ( port <= 0 || port > 65535 )
			{
				respond( req, false, Json::object(), std::string(attach ? "attach" : "launch")
					+ " over tcp needs the \"port\" attribute: the tcp port the target listens on (e.g. 5050), or a \"target\" like \"tcp:127.0.0.1:5050\"" );
				return;
			}
			target = "tcp:" + args["host"].asString( "127.0.0.1" ) + ":" + std::to_string( port );
		}
		else if ( transport == "serial" )
		{
			if ( args["serialPort"].asString("").empty() )
			{
				respond( req, false, Json::object(), std::string(attach ? "attach" : "launch")
					+ " over serial needs the \"serialPort\" attribute (e.g. COM3 or /dev/ttyUSB0)" );
				return;
			}
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

	// the host application the script is written for: the local target
	// imitates it, error messages use its function names, its include
	// folders serve include "file"
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

	// local symbols, unless attaching without a program file. A compiled
	// program (.bin) is used as it is, its source comes from the bytecode
	std::vector<uint8_t> bytecode;
	bool programIsBytecode = false;
	if ( !m_programPath.empty() )
	{
		std::string source;
		if ( !wr_loadForDebug(m_programPath, bytecode, source, error, &programIsBytecode, m_project.loaded() ? &m_project : nullptr) )
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
				if ( wr_compileWithFlags(m_programPath, matching, source, matchError, m_info.compilerFlags, m_project.loaded() ? &m_project : nullptr)
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
	else if ( !(m_info.caps & WRD_CapLaunch) )
	{
		respond( req, false, Json::object(), "the target runs its own program and takes no launches (its firmware did not call setLaunchable(true)): "
				"use \"request\": \"attach\" to debug it" );
		return;
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
		m_uploaded = true;
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

	// the other programs a target already debugs (contexts its firmware attached)
	syncPrograms();
	m_threads.clear();
	for ( int p : m_programs.ids() )
	{
		if ( threadOf(p) == p + 1 )
		{
			m_threads.push_back( p );
		}
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

	// every program's lines, as many as the target has room for
	std::map<int, std::vector<int> > lines;
	lines[0];
	for ( int p : m_programs.ids() )
	{
		lines[p];
	}
	// and those parked for programs not on the target now, by their hash (the
	// ones sent before and not wanted any more go)
	std::map<uint32_t, std::vector<int> > parked;
	for ( uint32_t hash : m_parkedSent )
	{
		parked[hash];
	}
	int total = 0;
	for ( Breakpoint& bp : m_breakpoints )
	{
		for ( size_t b = 0; bp.verified && !m_noDebug && b < bp.bound.size(); ++b )
		{
			std::vector<int>& those = lines[bp.bound[b].program];
			if ( std::find(those.begin(), those.end(), bp.bound[b].line) != those.end() )
			{
				continue;
			}
			if ( total >= m_info.maxBreakpoints )
			{
				bp.verified = false;
				bp.message = "the target has no room for more breakpoints";
				break;
			}
			those.push_back( bp.bound[b].line );
			++total;
		}
		if ( bp.verified && !m_noDebug && bp.bound.empty() && bp.parkedHash )
		{
			std::vector<int>& those = parked[bp.parkedHash];
			if ( std::find(those.begin(), those.end(), bp.parkedLine) == those.end() )
			{
				if ( total >= m_info.maxBreakpoints )
				{
					bp.verified = false;
					bp.message = "the target has no room for more breakpoints";
					continue;
				}
				those.push_back( bp.parkedLine );
				++total;
			}
		}
	}
	bool ok = true;
	for ( const auto& p : lines )
	{
		ok = m_client->setBreakpoints( p.second, p.first ) && ok;
	}
	m_parkedSent.clear();
	for ( const auto& p : parked )
	{
		ok = m_client->setParkedBreakpoints( p.second, p.first ) && ok;
		if ( !p.second.empty() )
		{
			m_parkedSent.insert( p.first );
		}
	}
	return ok;
}

// where a source breakpoint is in the programs there are now
void DapServer::bindBreakpoint( Breakpoint& bp )
{
	bool known = false;
	std::vector<std::pair<int, int> > files = filesForSource( bp.source, &known );
	bp.bound.clear();
	for ( const auto& f : files )
	{
		const WrenchDebugProgram& s = symbols( f.first );
		int line = s.valid() ? s.breakableLine( f.second, bp.requestedLine ) : -1;
		if ( line > 0 )
		{
			Binding b;
			b.program = f.first;
			b.line = line;
			bp.bound.push_back( b );
		}
	}
	bp.verified = !bp.bound.empty();
	bp.message.clear();
	bp.parkedHash = 0;
	bp.parkedLine = 0;

	// in a copy of a program not on the target now (a module from an earlier
	// session): the image kept beside the copy says which line of that
	// program it is, the target sets it by the program's hash when the
	// program comes, before any of its code runs
	uint32_t hash = 0;
	std::string name;
	if ( !bp.verified && files.empty() && (m_info.caps & WRD_CapParkedBreakpoints)
		&& wr_embeddedPathParts(bp.source["path"].asString(""), hash, name) )
	{
		const WrenchDebugProgram* copied = copiedProgram( hash );
		int file = copied ? wr_embeddedFileIndex( copied->files(), name ) : -1;
		int line = file >= 0 ? copied->breakableLine( file, bp.requestedLine ) : -1;
		if ( line > 0 )
		{
			bp.parkedHash = hash;
			bp.parkedLine = line;
			bp.verified = true;
		}
	}

	if ( !bp.verified )
	{
		bp.message = !files.empty() ? "no code at or after this line"
			: known ? "the module is not loaded now"
			: "only the programs being debugged and the files they include can hold breakpoints";
	}
}

Json DapServer::breakpointJson( const Breakpoint& bp, bool sent )
{
	Json b = Json::object();
	b.set( "id", bp.id );
	b.set( "verified", bp.verified && sent );
	int line = bp.requestedLine;
	Json source = bp.source;
	if ( !bp.bound.empty() )
	{
		int file = 0;
		symbols( bp.bound[0].program ).locate( bp.bound[0].line, file, line );
		source = sourceJson( bp.bound[0].program, file );
	}
	else if ( bp.parkedHash && copiedProgram(bp.parkedHash) )
	{
		int file = 0;
		copiedProgram( bp.parkedHash )->locate( bp.parkedLine, file, line );
	}
	b.set( "line", line - (m_linesStartAt1 ? 0 : 1) );
	// VS Code places a verified breakpoint by the path of the source given
	// here: an embedded source has none, the breakpoint would move to an
	// empty document named "."; without it the client keeps the document
	// the breakpoint was set in
	if ( source["sourceReference"].asInt(0) <= 0 )
	{
		b.set( "source", source );
	}
	if ( !bp.verified )
	{
		b.set( "message", bp.message );
	}
	else if ( !sent )
	{
		b.set( "message", m_client ? m_client->lastError() : std::string("not connected") );
	}
	return b;
}

void DapServer::onSetBreakpoints( const Json& req )
{
	const Json& args = req["arguments"];
	const Json& source = args["source"];
	const Json& requested = args["breakpoints"];

	// this source's breakpoints are replaced, the other sources' stay; hit
	// counts of the ones that survive the update are kept
	std::vector<Breakpoint> old;
	std::vector<Breakpoint> others;
	for ( const Breakpoint& bp : m_breakpoints )
	{
		(wr_sameSource(bp.source, source) ? old : others).push_back( bp );
	}
	m_breakpoints = others;
	size_t first = m_breakpoints.size();
	for ( size_t i = 0; i < requested.size(); ++i )
	{
		const Json& r = requested[i];
		Breakpoint bp;
		bp.source = source;
		bp.requestedLine = r["line"].asInt() + (m_linesStartAt1 ? 0 : 1);
		bp.condition = r["condition"].asString( "" );
		bp.hitCondition = r["hitCondition"].asString( "" );
		bp.logMessage = r["logMessage"].asString( "" );
		bindBreakpoint( bp );
		bp.id = m_nextBreakpointId++;
		for ( const Breakpoint& o : old )
		{
			bool sameLine = (o.bound.empty() || bp.bound.empty()) ? o.requestedLine == bp.requestedLine
				: (o.bound[0].program == bp.bound[0].program && o.bound[0].line == bp.bound[0].line);
			if ( sameLine && o.condition == bp.condition && o.hitCondition == bp.hitCondition )
			{
				bp.hits = o.hits;
			}
		}
		m_breakpoints.push_back( bp );
	}

	bool sent = sendBreakpoints();

	Json result = Json::array();
	for ( size_t i = first; i < m_breakpoints.size(); ++i )
	{
		result.push( breakpointJson(m_breakpoints[i], sent) );
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

void DapServer::onDataBreakpointInfo( const Json& req )
{
	const Json& args = req["arguments"];
	std::string name = args["name"].asString( "" );
	Json body = Json::object();
	body.set( "dataId", Json() );

	if ( !m_client || !(m_info.caps & WRD_CapWatch) )
	{
		body.set( "description", "the target has no watchpoints" );
		respond( req, true, body );
		return;
	}
	if ( !m_stopped || !ensureStack() )
	{
		body.set( "description", "watchpoints are set while the program is stopped" );
		respond( req, true, body );
		return;
	}

	// which variable: a child of a variables list, or an expression
	std::string expr;
	int frame = 0;
	int reference = args["variablesReference"].asInt( 0 );
	if ( reference )
	{
		auto it = m_refs.find( reference );
		if ( it == m_refs.end() )
		{
			body.set( "description", "unknown variable" );
			respond( req, true, body );
			return;
		}
		const VarRef& ref = it->second;
		frame = ref.frame;
		if ( ref.kind == VarRef::Locals )
		{
			expr = name;
		}
		else if ( ref.kind == VarRef::Globals )
		{
			expr = "::" + name;
		}
		else
		{
			std::vector<WrenchDebugChild> kids;
			uint32_t total = 0;
			if ( m_client->getChildren(ref.value, 0, ref.value.count, kids, total, &symbols(ref.program)) )
			{
				for ( const WrenchDebugChild& c : kids )
				{
					if ( c.name == name )
					{
						expr = childExpression( ref, c );
						break;
					}
				}
			}
		}
	}
	else
	{
		expr = name;
		frame = args["frameId"].asInt( 1 ) - 1;
	}
	if ( frame < 0 || frame >= (int)m_frames.size() )
	{
		frame = 0;
	}
	if ( expr.empty() )
	{
		body.set( "description", "cannot tell where " + name + " lives" );
		respond( req, true, body );
		return;
	}

	WrenchDebugEvaluator ev( *m_client, m_programs, m_frames, frame );
	WrenchDebugWatchPath path;
	WrenchDebugValue value;
	std::string error;
	if ( !ev.watchPath(expr, path, value, error) )
	{
		body.set( "description", error );
		respond( req, true, body );
		return;
	}
	if ( !path.global )
	{
		path.program = frameProgram( path.frame );
	}

	body.set( "dataId", wr_encodeWatch(path, m_frames[path.global ? 0 : path.frame].function, expr) );
	body.set( "description", expr );
	Json access = Json::array();
	access.push( "write" );
	body.set( "accessTypes", access );
	body.set( "canPersist", path.global );
	respond( req, true, body );
}

// put one data breakpoint on the target; false (with a message) when it
// cannot go there now
bool DapServer::setWatch( DataBreakpoint& dbp )
{
	dbp.watch = 0;
	WrenchDebugWatchPath path;
	int function = -1;
	if ( !wr_decodeWatch(dbp.dataId, path, function, dbp.expr) )
	{
		dbp.message = "not a wrench watch";
		return false;
	}
	if ( !m_client || !(m_info.caps & WRD_CapWatch) )
	{
		dbp.message = "the target has no watchpoints";
		return false;
	}
	if ( !path.global )
	{
		// a local is named by its frame in the current stop
		if ( !m_stopped || !ensureStack() || path.frame >= (int)m_frames.size() || m_frames[path.frame].function != function
				|| frameProgram(path.frame) != path.program )
		{
			dbp.message = "a local can only be watched while stopped in its function";
			return false;
		}
	}
	int id = 0;
	if ( !m_client->addWatch(path, id) )
	{
		dbp.message = m_client->lastError();
		return false;
	}
	dbp.watch = id;
	dbp.message.clear();
	return true;
}

void DapServer::onSetDataBreakpoints( const Json& req )
{
	const Json& requested = req["arguments"]["breakpoints"];
	std::vector<DataBreakpoint> old;
	old.swap( m_dataBreakpoints );

	// watches that stay keep their place on the target (and their hit
	// counts), the others go first to make room
	std::vector<bool> kept( old.size(), false );
	for ( size_t i = 0; i < requested.size(); ++i )
	{
		const Json& r = requested[i];
		DataBreakpoint dbp;
		dbp.dataId = r["dataId"].asString( "" );
		dbp.condition = r["condition"].asString( "" );
		dbp.hitCondition = r["hitCondition"].asString( "" );
		for ( size_t o = 0; o < old.size(); ++o )
		{
			if ( !kept[o] && old[o].dataId == dbp.dataId && old[o].watch )
			{
				kept[o] = true;
				dbp.id = old[o].id;
				dbp.expr = old[o].expr;
				dbp.watch = old[o].watch;
				if ( old[o].condition == dbp.condition && old[o].hitCondition == dbp.hitCondition )
				{
					dbp.hits = old[o].hits;
				}
				break;
			}
		}
		m_dataBreakpoints.push_back( dbp );
	}
	for ( size_t o = 0; o < old.size(); ++o )
	{
		if ( !kept[o] && old[o].watch && m_client && m_client->linkUp() )
		{
			m_client->removeWatch( old[o].watch );
		}
	}

	Json result = Json::array();
	for ( DataBreakpoint& dbp : m_dataBreakpoints )
	{
		if ( !dbp.id )
		{
			dbp.id = m_nextBreakpointId++;
			setWatch( dbp );
		}
		Json b = Json::object();
		b.set( "id", dbp.id );
		b.set( "verified", dbp.watch != 0 );
		b.set( "description", dbp.expr );
		if ( !dbp.watch )
		{
			b.set( "message", dbp.message );
		}
		result.push( b );
	}

	Json body = Json::object();
	body.set( "breakpoints", result );
	respond( req, true, body );
}

// the target dropped a watch on a local whose call returned
void DapServer::onWatchRemoved( int watch )
{
	for ( DataBreakpoint& dbp : m_dataBreakpoints )
	{
		if ( dbp.watch == watch )
		{
			dbp.watch = 0;
			dbp.message = "its function returned";
			Json b = Json::object();
			b.set( "id", dbp.id );
			b.set( "verified", false );
			b.set( "message", dbp.message );
			Json body = Json::object();
			body.set( "reason", "changed" );
			body.set( "breakpoint", b );
			event( "breakpoint", body );
		}
	}
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

// a thread for each program that runs on a stack of its own: program 0 and
// the contexts the firmware attached (a module runs on its importer's)
void DapServer::onThreads( const Json& req )
{
	Json threads = Json::array();
	Json t = Json::object();
	t.set( "id", 1 );
	t.set( "name", "wrench" );
	threads.push( t );
	for ( int p : m_threads )
	{
		if ( p > 0 )
		{
			Json other = Json::object();
			other.set( "id", p + 1 );
			other.set( "name", m_programs.displayName(p) );
			threads.push( other );
		}
	}
	Json body = Json::object();
	body.set( "threads", threads );
	respond( req, true, body );
}

void DapServer::onModules( const Json& req )
{
	Json modules = Json::array();
	for ( int p : m_programs.ids() )
	{
		modules.push( moduleJson(p) );
	}
	Json body = Json::object();
	body.set( "modules", modules );
	body.set( "totalModules", (int)modules.size() );
	respond( req, true, body );
}

Json DapServer::moduleJson( int program )
{
	Json m = Json::object();
	m.set( "id", program );
	m.set( "name", program == 0 && !m_programPath.empty() ? wr_baseName(m_programPath) : m_programs.displayName(program) );
	if ( program == 0 && !m_programPath.empty() )
	{
		m.set( "path", m_programPath );
	}
	return m;
}

// the target's programs; program 0 keeps the symbols the session started with
void DapServer::syncPrograms()
{
	m_programs.set( 0, m_program, true );
	std::string error;
	if ( m_client && !m_programs.sync(*m_client, &error) )
	{
		output( "Cannot read all programs of the target: " + error + "\n", "important" );
	}
}

// a program came or went: threads, modules and breakpoints follow
void DapServer::onProgramEvent( const WrenchDebugEvent& e )
{
	bool added = e.type == WrenchDebugEvent::ProgramAdded;
	Json gone = moduleJson( e.program );
	syncPrograms();

	Json module = Json::object();
	module.set( "reason", added ? "new" : "removed" );
	module.set( "module", added ? moduleJson(e.program) : gone );
	event( "module", module );

	std::vector<int> threads;
	for ( int p : m_programs.ids() )
	{
		if ( threadOf(p) == p + 1 )
		{
			threads.push_back( p );
		}
	}
	for ( int p : threads )
	{
		if ( std::find(m_threads.begin(), m_threads.end(), p) == m_threads.end() )
		{
			Json t = Json::object();
			t.set( "reason", "started" );
			t.set( "threadId", p + 1 );
			event( "thread", t );
		}
	}
	for ( int p : m_threads )
	{
		if ( std::find(threads.begin(), threads.end(), p) == threads.end() )
		{
			Json t = Json::object();
			t.set( "reason", "exited" );
			t.set( "threadId", p + 1 );
			event( "thread", t );
		}
	}
	m_threads = threads;

	// breakpoints bind in the programs there are now (the target kept a
	// module's own for when it is imported again, this sets them the same)
	std::vector<Breakpoint> before = m_breakpoints;
	for ( Breakpoint& bp : m_breakpoints )
	{
		bindBreakpoint( bp );
	}
	bool sent = sendBreakpoints();
	for ( size_t i = 0; i < m_breakpoints.size(); ++i )
	{
		const Breakpoint& now = m_breakpoints[i];
		bool same = now.verified == before[i].verified && now.bound.size() == before[i].bound.size();
		for ( size_t b = 0; same && b < now.bound.size(); ++b )
		{
			same = now.bound[b].program == before[i].bound[b].program && now.bound[b].line == before[i].bound[b].line;
		}
		if ( !same )
		{
			Json body = Json::object();
			body.set( "reason", "changed" );
			body.set( "breakpoint", breakpointJson(now, sent) );
			event( "breakpoint", body );
		}
	}
}

void DapServer::onStackTrace( const Json& req )
{
	if ( !ensureStack() )
	{
		respond( req, false, Json::object(), m_stopped ? m_client->lastError() : std::string("not stopped") );
		return;
	}

	// the stop is on one thread, the others are not running wrench code now
	if ( req["arguments"]["threadId"].asInt(1) != threadOf(m_stopProgram) )
	{
		Json body = Json::object();
		body.set( "stackFrames", Json::array() );
		body.set( "totalFrames", 0 );
		respond( req, true, body );
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
		int program = frameProgram( i );
		int file = 0;
		int line = f.line;
		symbols( program ).locate( f.line, file, line );
		Json j = Json::object();
		j.set( "id", i + 1 );
		j.set( "name", f.name );
		j.set( "line", line - (m_linesStartAt1 ? 0 : 1) );
		j.set( "column", m_linesStartAt1 ? 1 : 0 );
		if ( hasSource(program, file) )
		{
			j.set( "source", sourceJson(program, file) );
		}
		if ( program > 0 )
		{
			j.set( "moduleId", program );
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
		int program = frameProgram( frame );
		if ( m_frames[frame].function >= 0 )
		{
			VarRef ref;
			ref.kind = VarRef::Locals;
			ref.frame = frame;
			ref.program = program;
			Json s = Json::object();
			s.set( "name", "Locals" );
			s.set( "presentationHint", "locals" );
			s.set( "variablesReference", newRef(ref) );
			s.set( "namedVariables", m_frames[frame].slots );
			s.set( "expensive", false );
			scopes.push( s );
		}

		// the globals of the frame's program
		VarRef ref;
		ref.kind = VarRef::Globals;
		ref.frame = frame;
		ref.program = program;
		Json s = Json::object();
		s.set( "name", program == 0 ? std::string("Globals") : "Globals of " + m_programs.displayName(program) );
		s.set( "variablesReference", newRef(ref) );
		s.set( "expensive", false );
		scopes.push( s );
	}
	Json body = Json::object();
	body.set( "scopes", scopes );
	respond( req, true, body );
}

Json DapServer::variableJson( const std::string& name, const WrenchDebugValue& v, const std::string& evaluateName, bool hex, int frame, int program )
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
		r.expr = evaluateName;
		r.frame = frame;
		r.program = program;
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
			std::string name = symbols( ref.program ).localName( function, (int)i );
			vars.push( variableJson(name, values[i], name, hex, ref.frame, ref.program) );
		}
	}
	else if ( ref.kind == VarRef::Globals )
	{
		std::vector<WrenchDebugValue> values;
		if ( !m_client->getGlobals(values, ref.program) )
		{
			respond( req, false, Json::object(), m_client->lastError() );
			return;
		}
		for ( size_t i = 0; i < values.size(); ++i )
		{
			std::string name = symbols( ref.program ).globalName( (int)i );
			vars.push( variableJson(name, values[i], "::" + name, hex, ref.frame, ref.program) );
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
		if ( !m_client->getChildren(ref.value, start, count, kids, total, &symbols(ref.program)) )
		{
			respond( req, false, Json::object(), m_client->lastError() );
			return;
		}
		for ( const WrenchDebugChild& c : kids )
		{
			vars.push( variableJson(c.name, c.value, childExpression(ref, c), hex, ref.frame, ref.program) );
		}
	}

	Json body = Json::object();
	body.set( "variables", vars );
	respond( req, true, body );
}

// how an element or member is written in an expression: a[2], s.member,
// h["key"] or h[5]; empty when the container itself has no such text
std::string DapServer::childExpression( const VarRef& parent, const WrenchDebugChild& child )
{
	if ( parent.expr.empty() )
	{
		return std::string();
	}

	const WrenchDebugValue& p = parent.value;
	if ( p.tag == WRD_ValArray || p.tag == WRD_ValRaw || p.tag == WRD_ValString )
	{
		return parent.expr + "[" + std::to_string( child.index ) + "]";
	}
	if ( p.tag == WRD_ValStruct )
	{
		return parent.expr + "." + child.name;
	}
	if ( p.tag != WRD_ValHash )
	{
		return std::string();
	}

	const WrenchDebugValue& key = child.key;
	if ( key.tag == WRD_ValInt )
	{
		return parent.expr + "[" + std::to_string( key.i ) + "]";
	}
	if ( key.tag == WRD_ValFloat )
	{
		char text[32];
		snprintf( text, sizeof(text), "%.9g", key.f );
		std::string number = text;
		if ( number.find_first_of(".eE") == std::string::npos )
		{
			number += ".0"; // stays a float key
		}
		return parent.expr + "[" + number + "]";
	}
	if ( key.isString() )
	{
		std::string text = key.text;
		if ( key.count > text.size() && !m_client->getString(key, text) )
		{
			return std::string();
		}
		std::string quoted = "\"";
		for ( char c : text )
		{
			switch ( c )
			{
				case '"': quoted += "\\\""; break;
				case '\\': quoted += "\\\\"; break;
				case '\n': quoted += "\\n"; break;
				case '\r': quoted += "\\r"; break;
				case '\t': quoted += "\\t"; break;
				case '\0': quoted += "\\0"; break;
				default: quoted += c; break;
			}
		}
		return parent.expr + "[" + quoted + "\"]";
	}
	return std::string();
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
	WrenchDebugEvaluator ev( *m_client, m_programs, m_frames, ref.frame );
	WrenchDebugLocation where;
	where.program = ref.program;
	std::string error;

	if ( ref.kind == VarRef::Locals )
	{
		where.kind = WrenchDebugLocation::Local;
		where.frame = ref.frame;
		where.slot = symbols( ref.program ).localSlot( m_frames[ref.frame].function, name );
	}
	else if ( ref.kind == VarRef::Globals )
	{
		where.kind = WrenchDebugLocation::Global;
		where.slot = symbols( ref.program ).globalSlot( name );
	}
	else
	{
		std::vector<WrenchDebugChild> kids;
		uint32_t total = 0;
		if ( m_client->getChildren(ref.value, 0, ref.value.count, kids, total, &symbols(ref.program)) )
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

	Json v = variableJson( name, result, std::string(), false, ref.frame, ref.program );
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

	WrenchDebugEvaluator ev( *m_client, m_programs, m_frames, frame );
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

	Json var = variableJson( expression, v, expression, hex, frame, frameProgram(frame) );
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
	const WrenchDebugProgram& s = symbols( ensureStack() ? frameProgram(frame) : 0 );
	if ( frame >= 0 && frame < (int)m_frames.size() && m_frames[frame].function >= 0
		&& m_frames[frame].function + 1 < (int)s.functions().size() )
	{
		for ( const std::string& l : s.functions()[m_frames[frame].function + 1].locals )
		{
			names.push_back( WrenchDebugProgram::displayName(l) );
		}
	}
	if ( !s.functions().empty() )
	{
		for ( const std::string& g : s.functions()[0].locals )
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
	std::vector<std::pair<int, int> > files = filesForSource( args["source"] );
	int file = files.empty() ? -1 : files[0].second;
	const WrenchDebugProgram& s = symbols( files.empty() ? 0 : files[0].first );
	Json locations = Json::array();
	for ( int v : s.lines() )
	{
		int f = 0;
		int l = 0;
		s.locate( v, f, l );
		if ( f == file && l >= line && l <= endLine )
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
	int file = 0;
	int line = m_stopLine;
	const WrenchDebugProgram& s = symbols( m_stopProgram );
	s.locate( m_stopLine, file, line );
	std::string description = std::string( "wrench runtime error " ) + errorText( m_stopError ) + " at line " + std::to_string( line );
	if ( file > 0 && file < (int)s.files().size() )
	{
		description += " of " + wr_baseName( s.files()[file].name );
	}
	if ( m_stopProgram > 0 )
	{
		description += " in " + m_programs.displayName( m_stopProgram );
	}
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
	// sourceReference n is file n - 1 of program 0, from FirstEmbedded on a
	// file of another program
	int ref = req["arguments"]["sourceReference"].asInt( 1 );
	int program = 0;
	int file = ref - 1;
	if ( ref >= FirstEmbedded )
	{
		Json source = Json::object();
		source.set( "sourceReference", ref );
		std::vector<std::pair<int, int> > files = filesForSource( source );
		if ( files.empty() )
		{
			respond( req, false, Json::object(), "the module is not loaded now" );
			return;
		}
		program = files[0].first;
		file = files[0].second;
	}
	const std::vector<WrenchDebugProgram::SourceFile>& files = symbols( program ).files();
	if ( file < 0 || file >= (int)files.size() || !files[file].hasText )
	{
		respond( req, false, Json::object(), "the program has no embedded source" );
		return;
	}
	Json body = Json::object();
	body.set( "content", files[file].text );
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
	if ( m_client && m_client->linkUp() && !m_unresponsive )
	{
		// the local target answers at once whenever it can; running code
		// without debug information never looks at requests, so one short
		// try is enough there, no retries and no detach after it
		if ( m_link.isLocal() )
		{
			m_client->setTimeout( 300, 0 );
		}
		if ( terminateRequest || terminate || m_link.isLocal() )
		{
			if ( m_client->terminate() )
			{
				WrenchDebugEvent e;
				for ( int i = 0; i < 40 && m_client->waitEvent(e, 50) && e.type != WrenchDebugEvent::Ended; ++i )
				{
				}
			}
			else if ( m_link.isLocal() )
			{
				m_unresponsive = true;
			}
		}
		if ( !terminateRequest && !m_unresponsive )
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
	// a local target stuck in such code is left to the end of the process
	m_link.close( m_unresponsive ? 0 : 2000 );
	m_client.reset();
	m_quit = true;
}

bool DapServer::hitConditionMet( const std::string& hitCondition, int hits )
{
	std::string c = hitCondition;
	c.erase( std::remove(c.begin(), c.end(), ' '), c.end() );
	if ( c.empty() )
	{
		return true;
	}
	int n = 0;
	if ( c.compare(0, 2, ">=") == 0 ) { n = atoi( c.c_str() + 2 ); return hits >= n; }
	if ( c.compare(0, 2, "<=") == 0 ) { n = atoi( c.c_str() + 2 ); return hits <= n; }
	if ( c.compare(0, 2, "==") == 0 ) { n = atoi( c.c_str() + 2 ); return hits == n; }
	if ( c[0] == '>' ) { n = atoi( c.c_str() + 1 ); return hits > n; }
	if ( c[0] == '<' ) { n = atoi( c.c_str() + 1 ); return hits < n; }
	if ( c[0] == '%' ) { n = atoi( c.c_str() + 1 ); return n > 0 && hits % n == 0; }
	n = atoi( c.c_str() );
	return hits >= n;
}

void DapServer::handleStopped( const WrenchDebugEvent& e )
{
	resetStopState();
	m_stopped = true;
	m_running = false;
	m_stopLine = e.line;
	m_stopProgram = (e.program > 0 && e.program != WRD_NoProgram) ? e.program : 0;
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
			if ( !bp.verified || !wr_boundAt(bp, m_stopProgram, e.line) )
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
				WrenchDebugEvaluator ev( *m_client, m_programs, m_frames, 0 );
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
			if ( !hitConditionMet(bp.hitCondition, bp.hits) )
			{
				continue;
			}

			if ( !bp.logMessage.empty() )
			{
				ensureStack();
				WrenchDebugEvaluator ev( *m_client, m_programs, m_frames, 0 );
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
			if ( !bp.verified || !wr_boundAt(bp, m_stopProgram, e.line) )
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
				WrenchDebugEvaluator ev( *m_client, m_programs, m_frames, 0 );
				if ( !ev.evaluate(bp.condition, v, error) || !WrenchDebugEvaluator::truthy(v) )
				{
					continue;
				}
			}
			++bp.hits;
			if ( !bp.logMessage.empty() && hitConditionMet(bp.hitCondition, bp.hits) )
			{
				ensureStack();
				WrenchDebugEvaluator ev( *m_client, m_programs, m_frames, 0 );
				output( ev.interpolate(bp.logMessage) + "\n", "console" );
			}
		}
	}
	else if ( e.reason == WRD_StopWatch )
	{
		// conditions and hit counts are the host's, as for breakpoints
		DataBreakpoint* hit = nullptr;
		for ( DataBreakpoint& dbp : m_dataBreakpoints )
		{
			if ( dbp.watch && dbp.watch == e.watch )
			{
				hit = &dbp;
			}
		}
		if ( hit )
		{
			bool stop = true;
			if ( !hit->condition.empty() && ensureStack() )
			{
				WrenchDebugEvaluator ev( *m_client, m_programs, m_frames, 0 );
				WrenchDebugValue v;
				std::string error;
				if ( !ev.evaluate(hit->condition, v, error) )
				{
					output( "Watchpoint condition '" + hit->condition + "' failed: " + error + "\n", "important" );
				}
				else
				{
					stop = WrenchDebugEvaluator::truthy( v );
				}
			}
			if ( stop )
			{
				++hit->hits;
				stop = hitConditionMet( hit->hitCondition, hit->hits );
			}
			if ( !stop )
			{
				resetStopState();
				if ( m_client->resume(true) )
				{
					m_running = true;
				}
				return;
			}
			hitIds.push( hit->id );
			description = hit->expr + " changed";
		}
		reason = "data breakpoint";
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
	body.set( "threadId", threadOf(m_stopProgram) );
	body.set( "allThreadsStopped", true );
	if ( !description.empty() )
	{
		body.set( "description", description );
		if ( e.reason == WRD_StopError )
		{
			body.set( "text", errorText(e.error) );
		}
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

	// the firmware runs contexts of its own, which stop when they do
	bool firmwareRuns = false;
	for ( int p : m_threads )
	{
		firmwareRuns = firmwareRuns || p > 0;
	}
	if ( firmwareRuns && !m_link.isLocal() )
	{
		output( "The program finished, the target keeps running its other programs.\n" );
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
				// the program this session uploaded is no news
				if ( !m_uploaded || e.programHash != m_program.hash() )
				{
					output( "The target loaded a new program.\n", "important" );
				}
				break;
			}
			case WrenchDebugEvent::WatchRemoved: onWatchRemoved( e.watch ); break;
			case WrenchDebugEvent::ProgramAdded:
			case WrenchDebugEvent::ProgramRemoved: onProgramEvent( e ); break;
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
		{ "dataBreakpointInfo", &DapServer::onDataBreakpointInfo },
		{ "setDataBreakpoints", &DapServer::onSetDataBreakpoints },
		{ "modules", &DapServer::onModules },
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

	if ( m_client && m_client->linkUp() && !m_unresponsive )
	{
		if ( m_link.isLocal() )
		{
			m_client->setTimeout( 300, 0 );
			m_unresponsive = !m_client->terminate();
		}
		else
		{
			m_client->detach();
		}
	}
	m_link.close( m_unresponsive ? 0 : 2000 );
	return 0;
}

}

int wr_dapMain( int argn, char* argv[] )
{
	DapServer server;
	return server.run();
}

#endif
