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

#include "utils/wrench_debug_host.h"
#include "utils/wrench_include_host.h"
#include "debug/debug.h"

#include <stdio.h>
#include <set>
#include <chrono>

bool wr_readTextFile( const std::string& path, std::string& out )
{
	out.clear();
	FILE* f = wr_openFile( path, "rb" );
	if ( !f )
	{
		return false;
	}
	char buf[8192];
	size_t n;
	while ( (n = fread(buf, 1, sizeof(buf), f)) > 0 )
	{
		out.append( buf, n );
	}
	fclose( f );
	return true;
}

std::string wr_debugErrorText( uint8_t error )
{
	if ( error < WR_warning_enums_follow )
	{
		return c_errStrings[error];
	}
	if ( error >= WR_USER )
	{
		return "user error " + std::to_string( (int)error );
	}
	return "error " + std::to_string( (int)error );
}

bool wr_compileForDebug( const std::string& path, std::vector<uint8_t>& bytecode,
												std::string& source, std::string& error, bool embedSource, const WrenchProject* project )
{
	uint8_t flags = WR_INCLUDE_GLOBALS | WR_EMBED_DEBUG_CODE | (embedSource ? WR_EMBED_SOURCE_CODE : 0);
	return wr_compileWithFlags( path, bytecode, source, error, flags, project );
}

bool wr_compileWithFlags( const std::string& path, std::vector<uint8_t>& bytecode,
												std::string& source, std::string& error, uint8_t flags, const WrenchProject* project )
{
	bytecode.clear();
	if ( !wr_readTextFile(path, source) )
	{
		error = "cannot read " + path;
		return false;
	}

	WrenchIncludeResolver includes;
	if ( project )
	{
		includes.searchPaths = project->includePaths();
	}
	std::string message;
	WRError err = wr_compileWithIncludes( path, source, flags, &includes, bytecode, &message );
	if ( err != WR_ERR_None )
	{
		// the message starts with the file and line
		error = message.empty() ? path + ": " + c_errStrings[err] : message;
		return false;
	}
	return true;
}

bool wr_loadForDebug( const std::string& path, std::vector<uint8_t>& bytecode,
											std::string& source, std::string& error, bool* isBytecode, const WrenchProject* project )
{
	std::string raw;
	if ( !wr_readTextFile(path, raw) )
	{
		error = "cannot read " + path;
		return false;
	}

	if ( !wr_isBytecodeValid((const uint8_t*)raw.data(), (unsigned int)raw.size()) )
	{
		if ( isBytecode )
		{
			*isBytecode = false;
		}
		return wr_compileForDebug( path, bytecode, source, error, true, project );
	}

	if ( isBytecode )
	{
		*isBytecode = true;
	}
	bytecode.assign( raw.begin(), raw.end() );
	WrenchDebugProgram program;
	std::string parseError;
	if ( !program.parse(bytecode.data(), (uint32_t)bytecode.size(), &parseError) )
	{
		error = path + ": " + parseError;
		return false;
	}
	source = program.source();
	return true;
}

// The debug host stands in for the real host application. Functions the
// project declares answer their "returns" value, so the script logic can be
// stepped through without the application. Anything else fails like it
// would in the application, unless there is no project: then the host is a
// generic stand-in (scripts for a device call led(), digitalWrite()...)
// and answers 0
struct WrenchHostNatives
{
	WrenchDebugServer* server = 0;
	const WrenchProject* project = 0;
	bool stubUndeclared = false;
	std::set<uint32_t> reported;

	// names for messages come from the source embedded in the program
	const uint8_t* namedProgram = 0;
	std::string programSource;
};

static thread_local WrenchHostNatives* t_hostNatives = 0;

static std::string wr_hostNativeName( WrenchHostNatives* h, uint32_t hash )
{
	WrenchDebugServerPrivate* I = h->server ? h->server->I : 0;
	const WrenchDebugProgramSlot* p = I ? &I->programs[0] : 0;
	if ( p && p->bytes && p->bytes != h->namedProgram )
	{
		h->namedProgram = p->bytes;
		WrenchDebugProgram program;
		h->programSource = program.parse( p->bytes, p->bytesLen ) ? program.source() : std::string();
	}
	std::string name = wr_functionNameForHash( hash, h->programSource, h->project );
	if ( name.empty() )
	{
		char text[48];
		snprintf( text, sizeof(text), "a function (name hash 0x%08X)", (unsigned)hash );
		return text;
	}
	return name;
}

static void wr_hostNativeMessage( WrenchHostNatives* h, const std::string& text )
{
	if ( h->server && h->server->clientAttached() )
	{
		h->server->print( text.c_str(), (int)text.size() );
	}
	else
	{
		fputs( text.c_str(), stdout );
		fflush( stdout );
	}
}

static void wr_hostNative( uint32_t hash, WRContext* c, WRValue& ret, WRError notFound )
{
	WrenchHostNatives* h = t_hostNatives;
	const WrenchNative* declared = (h && h->project) ? h->project->native( hash ) : 0;
	if ( !h || (!declared && !h->stubUndeclared) )
	{
		c->w->err = notFound;
		return;
	}

	std::string answer = "0";
	if ( declared )
	{
		const Json& v = declared->returns;
		if ( v.isNumber() )
		{
			double d = v.asNumber();
			if ( d == (double)(int32_t)d )
			{
				wr_makeInt( &ret, (int32_t)d );
			}
			else
			{
				wr_makeFloat( &ret, (float)d );
			}
			answer = v.dump();
		}
		else if ( v.isBool() )
		{
			wr_makeInt( &ret, v.asBool() ? 1 : 0 );
			answer = v.asBool() ? "1" : "0";
		}
		else if ( v.isString() )
		{
			wr_makeString( c, &ret, v.asString().c_str(), (int)v.asString().size() );
			answer = v.dump();
		}
	}

	if ( h->reported.insert(hash).second )
	{
		std::string name = wr_hostNativeName( h, hash );
		if ( declared )
		{
			wr_hostNativeMessage( h, "[wrench] " + name + " is provided by the host application, here it returns " + answer + "\n" );
		}
		else
		{
			wr_hostNativeMessage( h, "[wrench] the script called " + name + ", which this host does not provide, it returns 0\n" );
		}
	}
}

static void wr_hostNativeNotFound( const uint32_t hash, WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr )
{
	wr_hostNative( hash, c, retVal, WR_ERR_function_not_found );
}

static void wr_hostLibNotFound( const uint32_t hash, WRValue* stackTop, const int argn, WRContext* context )
{
	wr_hostNative( hash, context, *stackTop, WR_ERR_lib_function_not_found );
}

// natives must live as long as the state
static void wr_installHostNatives( WRState* w, WrenchHostNatives* natives )
{
	t_hostNatives = natives;
	wr_setOnCallbackNotFound( w, wr_hostNativeNotFound );
	wr_setOnLibCallbackNotFound( w, wr_hostLibNotFound );
}

static void wr_localPrint( WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr )
{
	WrenchDebugServer* server = (WrenchDebugServer*)usr;
	for ( int i = 0; i < argn; ++i )
	{
		WRValue::MallocStrScoped text( argv[i] );
		if ( text )
		{
			server->print( text, (int)text.size() );
		}
	}
}

static void wr_localPrintln( WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr )
{
	wr_localPrint( c, argv, argn, retVal, usr );
	((WrenchDebugServer*)usr)->print( "\n", 1 );
}

WrenchLocalTarget::WrenchLocalTarget()
	: m_shared( std::make_shared<Shared>() )
{
}

WrenchLocalTarget::~WrenchLocalTarget()
{
	stop();
}

bool WrenchLocalTarget::start( int stackSize, const WrenchProject* project )
{
	if ( m_started )
	{
		return true;
	}
	m_shared->project = project ? *project : WrenchProject();
	m_shared->quit = false;
	m_shared->done = false;
	std::shared_ptr<Shared> shared = m_shared;
	m_thread = std::thread( [shared, stackSize]() { run( shared, stackSize ); } );
	m_started = true;
	return true;
}

void WrenchLocalTarget::stop()
{
	if ( !m_started )
	{
		return;
	}
	m_shared->quit = true;
	m_shared->pipe.close();

	// running code with debug information notices the closed pipe within a
	// few lines; code without it cannot be interrupted at all
	for ( int waited = 0; waited < 2000 && !m_shared->done; waited += 10 )
	{
		std::this_thread::sleep_for( std::chrono::milliseconds(10) );
	}
	if ( m_thread.joinable() )
	{
		if ( m_shared->done )
		{
			m_thread.join();
		}
		else
		{
			// still inside the VM: leave it, it keeps its own reference to
			// the shared state, process exit ends it
			m_thread.detach();
		}
	}
	m_started = false;
}

void WrenchLocalTarget::run( std::shared_ptr<Shared> shared, int stackSize )
{
	WRState* w = wr_newState( stackSize > 0 ? stackSize : 512 );
	shared->project.loadLibraries( w );

	WrenchDebugServer server( w, shared->pipe.server() );
	server.setAbortOnDisconnect( true );
	wr_registerFunction( w, "print", wr_localPrint, &server );
	wr_registerFunction( w, "println", wr_localPrintln, &server );

	WrenchHostNatives natives;
	natives.server = &server;
	natives.project = shared->project.loaded() ? &shared->project : 0;
	wr_installHostNatives( w, &natives );

	while ( !shared->quit )
	{
		server.tick();
		shared->pipe.server()->idle();
	}

	server.detach();
	wr_destroyState( w );
	shared->done = true;
}

bool WrenchTransportSpec::parse( const std::string& text, std::string& error )
{
	*this = WrenchTransportSpec();

	if ( text.empty() || text == "local" )
	{
		kind = Local;
		return true;
	}

	// the rest may hold ':' itself (IPv6 hosts, Linux /dev/serial/by-path
	// names), so only the scheme and the trailing number are split off
	size_t colon = text.find( ':' );
	std::string scheme = text.substr( 0, colon );
	std::string rest = (colon == std::string::npos) ? std::string() : text.substr( colon + 1 );

	// "...:<digits>" at the end, the digits are the number
	auto splitNumber = []( const std::string& s, std::string& head, int& number ) -> bool
	{
		size_t last = s.rfind( ':' );
		std::string digits = (last == std::string::npos) ? s : s.substr( last + 1 );
		if ( digits.empty() || digits.size() > 9 || digits.find_first_not_of("0123456789") != std::string::npos )
		{
			return false;
		}
		number = atoi( digits.c_str() );
		head = (last == std::string::npos) ? std::string() : s.substr( 0, last );
		return true;
	};

	if ( scheme == "tcp" )
	{
		kind = Tcp;
		std::string head;
		if ( !splitNumber(rest, head, port) || port <= 0 || port > 65535 )
		{
			error = "expected tcp:<host>:<port> or tcp:<port>";
			return false;
		}
		host = head.empty() ? std::string( "127.0.0.1" ) : head;
		if ( host.size() > 2 && host.front() == '[' && host.back() == ']' )
		{
			host = host.substr( 1, host.size() - 2 ); // [::1]
		}
		return true;
	}

	if ( scheme == "serial" )
	{
		kind = Serial;
		std::string head;
		int number = 0;
		// a trailing ":<digits>" is the baud rate unless nothing is left for
		// the port name
		if ( splitNumber(rest, head, number) && !head.empty() )
		{
			serialPort = head;
			baud = number;
			if ( baud <= 0 )
			{
				error = "bad baud rate";
				return false;
			}
		}
		else
		{
			serialPort = rest;
		}
		if ( serialPort.empty() )
		{
			error = "expected serial:<port>[:<baud>]";
			return false;
		}
		return true;
	}

	error = "unknown transport '" + text + "', use local, tcp:<host>:<port> or serial:<port>[:<baud>]";
	return false;
}

std::string WrenchTransportSpec::describe() const
{
	switch ( kind )
	{
		case Tcp: return "tcp " + host + ":" + std::to_string( port );
		case Serial: return "serial " + serialPort + " @" + std::to_string( baud );
		default: return "local";
	}
}

WrenchClientLink::~WrenchClientLink()
{
	close();
}

void WrenchClientLink::close()
{
	if ( m_local )
	{
		m_local->stop();
		m_local.reset();
	}
	m_owned.reset();
	m_transport = nullptr;
}

bool WrenchClientLink::open( const WrenchTransportSpec& spec, std::string& error, int connectTimeoutMs, const WrenchProject* project )
{
	close();

	if ( spec.kind == WrenchTransportSpec::Local )
	{
		m_local.reset( new WrenchLocalTarget() );
		m_local->start( 0, project );
		m_transport = m_local->clientTransport();
		return true;
	}

	if ( spec.kind == WrenchTransportSpec::Tcp )
	{
#ifdef WRENCH_DEBUG_HAS_TCP
		WrenchDebugTcpClientTransport* tcp = new WrenchDebugTcpClientTransport();
		m_owned.reset( tcp );
		if ( !tcp->connect(spec.host.c_str(), spec.port, connectTimeoutMs) )
		{
			error = "cannot connect to " + spec.describe();
			m_owned.reset();
			return false;
		}
		m_transport = tcp;
		return true;
#else
		error = "built without TCP support";
		return false;
#endif
	}

#ifdef WRENCH_DEBUG_HAS_SERIAL
	WrenchDebugSerialTransport* serial = new WrenchDebugSerialTransport();
	m_owned.reset( serial );
	if ( !serial->open(spec.serialPort.c_str(), spec.baud) )
	{
		error = "cannot open " + spec.describe();
		m_owned.reset();
		return false;
	}
	m_transport = serial;
	return true;
#else
	error = "built without serial support";
	return false;
#endif
}

static WrenchDebugServer* g_dbgServer = 0;
static bool g_dbgVerbose = false;

static void wr_serverPrint( WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr )
{
	for ( int i = 0; i < argn; ++i )
	{
		WRValue::MallocStrScoped text( argv[i] );
		if ( text )
		{
			fwrite( (const char*)text, 1, text.size(), stdout );
			if ( g_dbgServer )
			{
				g_dbgServer->print( text, (int)text.size() );
			}
		}
	}
	fflush( stdout );
}

static void wr_serverPrintln( WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr )
{
	wr_serverPrint( c, argv, argn, retVal, usr );
	fputc( '\n', stdout );
	fflush( stdout );
	if ( g_dbgServer )
	{
		g_dbgServer->print( "\n", 1 );
	}
}

int wr_debugServerMain( const WrenchTransportSpec& spec, const char* programPath, bool verbose, const WrenchProject* project, bool allowUpload, bool hold,
	uint32_t linkTimeoutMs, const std::vector<std::string>& contexts )
{
	if ( contexts.size() >= WRENCH_DEBUG_MAX_PROGRAMS )
	{
		fprintf( stderr, "at most %d contexts beside the program\n", WRENCH_DEBUG_MAX_PROGRAMS - 1 );
		return -1;
	}

	if ( hold && !programPath )
	{
		fprintf( stderr, "-hold needs a program file\n" );
		return -1;
	}

	g_dbgVerbose = verbose;
	std::unique_ptr<WrenchDebugTransport> transport;

	if ( spec.kind == WrenchTransportSpec::Tcp )
	{
#ifdef WRENCH_DEBUG_HAS_TCP
		WrenchDebugTcpServerTransport* tcp = new WrenchDebugTcpServerTransport();
		transport.reset( tcp );
		if ( !tcp->listen(spec.port) )
		{
			fprintf( stderr, "cannot listen on port %d\n", spec.port );
			return -1;
		}
		printf( "wrench debug server listening on tcp port %d\n", spec.port );
#else
		fprintf( stderr, "built without TCP support\n" );
		return -1;
#endif
	}
	else if ( spec.kind == WrenchTransportSpec::Serial )
	{
#ifdef WRENCH_DEBUG_HAS_SERIAL
		WrenchDebugSerialTransport* serial = new WrenchDebugSerialTransport();
		transport.reset( serial );
		if ( !serial->open(spec.serialPort.c_str(), spec.baud) )
		{
			fprintf( stderr, "cannot open %s\n", spec.describe().c_str() );
			return -1;
		}
		printf( "wrench debug server on %s\n", spec.describe().c_str() );
#else
		fprintf( stderr, "built without serial support\n" );
		return -1;
#endif
	}
	else
	{
		fprintf( stderr, "dbgserver needs tcp:<port> or serial:<port>[:<baud>]\n" );
		return -1;
	}
	fflush( stdout );

	WRState* w = wr_newState( 512 );
	if ( project )
	{
		project->loadLibraries( w );
		printf( "imitating the host described by %s\n", project->path().c_str() );
	}
	else
	{
		wr_loadAllLibs( w );
	}
	WrenchDebugServer server( w, transport.get() );
	g_dbgServer = &server;
	server.setAllowUpload( allowUpload );
	if ( !allowUpload )
	{
		printf( "uploads from debuggers are refused\n" );
	}
	server.setLinkTimeout( linkTimeoutMs );
	wr_registerFunction( w, "print", wr_serverPrint );
	wr_registerFunction( w, "println", wr_serverPrintln );
	WrenchHostNatives natives;
	natives.server = &server;
	natives.project = project;
	natives.stubUndeclared = !project;
	wr_installHostNatives( w, &natives );

	if ( programPath )
	{
		// compiled bytecode as is, anything else is compiled as source
		std::vector<uint8_t> bytecode;
		std::string source;
		std::string error;
		if ( !wr_loadForDebug(programPath, bytecode, source, error, nullptr, project) )
		{
			fprintf( stderr, "%s\n", error.c_str() );
			wr_destroyState( w );
			return -1;
		}

		uint8_t* copy = (uint8_t*)g_malloc( bytecode.size() );
		memcpy( copy, bytecode.data(), bytecode.size() );
		if ( !server.loadBytes(copy, (int)bytecode.size(), true) )
		{
			fprintf( stderr, "invalid program %s\n", programPath );
			wr_destroyState( w );
			return -1;
		}
		printf( "loaded %s (%d bytes)\n", programPath, (int)bytecode.size() );
		fflush( stdout );
	}
	else if ( !contexts.empty() )
	{
		fprintf( stderr, "-context needs a program file, it would be program 0 and an upload would replace it\n" );
		wr_destroyState( w );
		return -1;
	}

	// the other scripts, programs 1 and up, as firmware attaches the
	// contexts it runs itself
	std::vector<WRContext*> ticking;
	for ( const std::string& path : contexts )
	{
		std::vector<uint8_t> bytecode;
		std::string source;
		std::string error;
		if ( !wr_loadForDebug(path, bytecode, source, error, nullptr, project) )
		{
			fprintf( stderr, "%s\n", error.c_str() );
			wr_destroyState( w );
			return -1;
		}
		uint8_t* copy = (uint8_t*)g_malloc( bytecode.size() );
		memcpy( copy, bytecode.data(), bytecode.size() );
		WRContext* context = wr_newContext( w, copy, (int)bytecode.size(), true );
		size_t slash = path.find_last_of( "/\\" );
		int program = context ? server.attach( context, path.c_str() + (slash == std::string::npos ? 0 : slash + 1) ) : -1;
		if ( program < 0 )
		{
			fprintf( stderr, "cannot run %s as a context of its own\n", path.c_str() );
			wr_destroyState( w );
			return -1;
		}
		wr_executeContext( context );
		printf( "attached %s as program %d, global code: %s\n", path.c_str(), program, wr_debugErrorText((uint8_t)wr_getLastError(w)).c_str() );
		fflush( stdout );
		if ( wr_getLastError(w) == WR_ERR_None && wr_getFunction(context, "tick") )
		{
			ticking.push_back( context );
		}
	}

	if ( programPath )
	{
		if ( hold )
		{
			// what firmware does with breakOnEntry(): start the script itself,
			// it waits on its first line until a debugger attaches
			server.setHostDriven( true );
			server.breakOnEntry();
			printf( "started, halted on the first line until a debugger attaches\n" );
			fflush( stdout );
			wr_executeContext( server.context() );
			printf( "global code finished: %s\n", wr_debugErrorText((uint8_t)wr_getLastError(w)).c_str() );
			fflush( stdout );
		}
	}

	bool attached = false;
	std::chrono::steady_clock::time_point lastTick = std::chrono::steady_clock::now();
	for ( ;; )
	{
		// the attached scripts' tick(), ten times a second; one that fails
		// is not called again
		std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
		if ( now - lastTick >= std::chrono::milliseconds(100) )
		{
			lastTick = now;
			for ( size_t t = 0; t < ticking.size(); )
			{
				wr_callFunction( ticking[t], "tick" );
				WRError err = wr_getLastError( w );
				if ( err == WR_ERR_None )
				{
					++t;
					continue;
				}
				printf( "a tick() failed: %s, it is not called again\n", wr_debugErrorText((uint8_t)err).c_str() );
				fflush( stdout );
				w->err = WR_ERR_None;
				ticking.erase( ticking.begin() + t );
			}
		}

		int flags = server.tick();
		if ( verbose )
		{
			if ( flags & WRD_TickClientAttached )
			{
				printf( "client attached\n" );
			}
			if ( flags & WRD_TickProgramLoaded )
			{
				printf( "program uploaded\n" );
			}
			if ( flags & WRD_TickProgramRan )
			{
				printf( "program finished: %s\n", wr_debugErrorText((uint8_t)wr_getLastError(w)).c_str() );
			}
			fflush( stdout );
		}
		if ( attached != server.clientAttached() )
		{
			attached = server.clientAttached();
			if ( verbose && !attached )
			{
				printf( "client detached\n" );
				fflush( stdout );
			}
		}
		transport->idle();
	}
}

#endif
