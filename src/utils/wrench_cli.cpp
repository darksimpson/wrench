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

#define STR_FILE_OPERATIONS
#include "wrench.h"

#include "utils/simple_args.h"

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <ctype.h>
#include <thread>
#include "utils/wrench_include_host.h"
#ifdef _WIN32
#include <direct.h>
#else
#include <unistd.h>
#endif

// the test scripts run with their folder as the current directory, so the
// scratch files they write stay there
static bool cliGetDir( char* dir, int size )
{
#ifdef _WIN32
	return _getcwd( dir, size ) != 0;
#else
	return getcwd( dir, size ) != 0;
#endif
}

static bool cliSetDir( const char* dir )
{
#ifdef _WIN32
	return _chdir( dir ) == 0;
#else
	return chdir( dir ) == 0;
#endif
}

#ifdef WRENCH_INCLUDE_DEBUG_CLIENT
#include "debug/debugger.h"
#include "utils/wrench_debug_host.h"
int wr_dapMain( int argn, char* argv[] );
int wr_lspMain( int argn, char* argv[] );
bool testDebugger();
#endif

#ifdef WRENCH_INCLUDE_DEBUG_CODE
// a link nobody is on, so the server only runs its hooks
class WrenchDebugNullTransport : public WrenchDebugTransport
{
public:
	bool write( const uint8_t* data, uint32_t len ) { return true; }
	int read( uint8_t* data, uint32_t maxLen ) { return 0; }
	uint32_t millis() { return 0; }
};
#endif

WRContext* g_context = 0;

int runTests( const char* testDir, int number );
static WRstr g_testDir; // the folder with test_files.txt, while runTests runs
void testGlobalValues( WRState* w );
void setup();
void eventMain();
void testImport();
void testHalt();
void testTimeSlices();
void testScheduler();
void testStackOverflow();
void testYield2();
void testHostSeededGlobalValue( WRState* w );
void testBlankHostSeededGlobals( WRState* w );
void testD();
void testCallFunctionHashLibraryFallback();
void testFunctionNameHashAsArgument();
void testNamedEnumUnqualifiedError();
void testBlankVariablesCannotBeInitialized();
void testFunctionNotFoundErrors();
void testLibFunctionNotFoundErrors();
void testOnCallbackNotFound();
void testOnLibCallbackNotFound();
void testDropClobberedInitializerStores();
void testDeepHashTableWithWrenchValue();
void testStateContextOpaquePointer();
#ifndef WRENCH_WITHOUT_COMPILER
void testCompilerArrayCopiesAndRemoval();
#endif
#ifdef WRENCH_HANDLE_MALLOC_FAIL
void testArrayMallocFailureHandling();
bool wr_arrayInsertEx( WRValue* A, const unsigned int where, const unsigned int count, WRValue* stackTop, WRContext* c );
#endif
void LEAKtest();
void C3test();
#ifdef WIN32_C17
int TESTmain();
#endif

struct MemBlock
{
	int32_t size;
};

int g_devMemoryAllocated = 0;
//------------------------------------------------------------------------------
void* devAlloc( size_t size )
{
	MemBlock* m = (MemBlock *)malloc( size + sizeof(MemBlock) );
	m->size = (int32_t)size;
	g_devMemoryAllocated += size;

#ifdef WRENCH_CLI_DEBUG
//	printf( "%d allocated[%d]\n", (int)size, g_devMemoryAllocated );
#else
//	printf( "%d allocated[%d] hint[%d]\n", (int)size, g_devMemoryAllocated, g_context ? g_context->allocatedMemoryHint : 0 );
#endif

	return m + 1;
}

//------------------------------------------------------------------------------
void devFree(void* ptr)
{
	if (ptr)
	{
		MemBlock* m = (MemBlock*)ptr - 1;
		g_devMemoryAllocated -= m->size;
		free(m);
	}
}

#ifdef WRENCH_HANDLE_MALLOC_FAIL
int g_allocationsBeforeFailure = -1;
int g_failAllocatorCalls = 0;
int g_zeroByteAllocatorCalls = 0;

//------------------------------------------------------------------------------
void* failAfterNAlloc( size_t size )
{
	++g_failAllocatorCalls;

	if ( g_allocationsBeforeFailure == 0 )
	{
		return 0;
	}

	if ( g_allocationsBeforeFailure > 0 )
	{
		--g_allocationsBeforeFailure;
	}

	return malloc( size );
}

//------------------------------------------------------------------------------
void failAfterNFree( void* ptr )
{
	free( ptr );
}

//------------------------------------------------------------------------------
void* rejectZeroByteAlloc( size_t size )
{
	if ( !size )
	{
		++g_zeroByteAllocatorCalls;
		return 0;
	}

	return malloc( size );
}

//------------------------------------------------------------------------------
void testArrayMallocFailureHandling()
{
	WRContext* context = (WRContext*)malloc( sizeof(WRContext) );
	memset( (unsigned char*)context, 0, sizeof(WRContext) );

	wr_setGlobalAllocator( failAfterNAlloc, failAfterNFree );
	g_allocationsBeforeFailure = 1;
	g_mallocFailed = false;

	assert( !context->getSVA(1, SV_VALUE, true) );
	assert( !context->svAllocated );
	assert( g_mallocFailed );

	g_allocationsBeforeFailure = -1;
	g_failAllocatorCalls = 0;
	assert( !context->getSVA(0, SV_HASH_TABLE, false) );
	assert( !g_failAllocatorCalls );

	wr_setGlobalAllocator( rejectZeroByteAlloc, failAfterNFree );
	g_zeroByteAllocatorCalls = 0;
	g_mallocFailed = false;

	WRGCObject* emptyString = context->getSVA(0, SV_CHAR, false);
	assert( emptyString );
	assert( emptyString->m_size == 0 );
	assert( emptyString->m_Cdata );

	WRGCObject* emptyArray = context->getSVA(0, SV_VALUE, true);
	assert( emptyArray );
	assert( emptyArray->m_size == 0 );
	assert( emptyArray->m_Vdata );
	assert( !g_zeroByteAllocatorCalls );
	assert( !g_mallocFailed );

	while ( context->svAllocated )
	{
		WRGCBase* allocated = context->svAllocated;
		context->svAllocated = allocated->m_nextGC;
		allocated->clear();
		g_free( allocated );
	}

	wr_setGlobalAllocator( &malloc, &free );
	g_mallocFailed = false;

	WRGCObject* va = (WRGCObject*)malloc( sizeof(WRGCObject) );
	memset( (unsigned char*)va, 0, sizeof(WRGCObject) );
	assert( va->init(2, SV_VALUE, true) == (int)(2 * sizeof(WRValue)) );

	WRValue array;
	array.p2 = INIT_AS_ARRAY;
	array.va = va;

	WRValue index;
	index.init( 4 );

	WRValue target;
	target.init( 123 );

	unsigned char* old = va->m_Cdata;

	wr_setGlobalAllocator( failAfterNAlloc, failAfterNFree );
	g_allocationsBeforeFailure = 0;

	wr_index[(WR_INT << 2) | WR_EX]( context, &index, &array, &target );
	assert( g_mallocFailed );
	assert( va->m_Cdata == old );
	assert( va->m_size == 2 );
	assert( target.asInt() == 0 );

	g_mallocFailed = false;
	assert( !array.indexArray(context, 5, true) );
	assert( g_mallocFailed );
	assert( va->m_Cdata == old );
	assert( va->m_size == 2 );

	WRValue stackTop;
	stackTop.init();

	g_mallocFailed = false;
	assert( !wr_arrayInsertEx(&array, 1, 1, &stackTop, context) );
	assert( g_mallocFailed );
	assert( va->m_Cdata == old );
	assert( va->m_size == 2 );

	wr_setGlobalAllocator( &malloc, &free );
	g_mallocFailed = false;
	va->clear();
	free( va );
	free( context );
}
#endif

//------------------------------------------------------------------------------
#ifndef WRENCH_WITHOUT_COMPILER
void testCompilerArrayCopiesAndRemoval()
{
	WRarray<int> source;
	source.append() = 10;
	source.append() = 20;
	source.append() = 30;
	source.pop();

	WRarray<int> copied( source );
	assert( copied.count() == 2 );
	assert( copied[0] == 10 );
	assert( copied[1] == 20 );

	WRarray<int> assigned;
	assigned = source;
	assert( assigned.count() == 2 );
	assert( assigned[0] == 10 );
	assert( assigned[1] == 20 );

	WRarray<int> values;
	values.append() = 10;
	values.append() = 20;
	values.append() = 30;
	values.append() = 40;

	assert( values.remove(2, 100) == 2 );
	assert( values[0] == 10 );
	assert( values[1] == 20 );
	assert( values.remove(1, 0) == 2 );
	assert( values.remove(0, 100) == 0 );
}
#endif

//------------------------------------------------------------------------------
void blobToHeader( WRstr const& blob, WRstr const& variableName, WRstr& header )
{
	header.format( "#ifndef _%s_H\n"
				   "#define _%s_H\n"
				   "\n"
				   "/******* wrench bytcode automatically generated header *******/\n\n"
				   "const int %s_bytecodeSize=%d;\n"
				   "const unsigned char %s_bytecode[]=\n"
				   "{\n",
				   variableName.c_str(),
				   variableName.c_str(),
				   variableName.c_str(),
				   (int)blob.size(),
				   variableName.c_str() );

	unsigned int p=0;
	while( p < blob.size() )
	{
		header += "\t";
		for( int i=0; i<16 && p<blob.size() ; i++ )
		{
			header.appendFormat( "0x%02X, ", (unsigned char)blob[p++] );
		}

		header.appendFormat( "// %d\n", p );
	}

	header += "};\n\n"
			  "#endif\n";
}

//------------------------------------------------------------------------------
void blobToAssemblyInc( WRstr const& blob, WRstr const& variableName, WRstr& header )
{
	header.format( "\n;******* wrench bytcode automatically generated include file *******\n\n"
				   ".export %s_bytecode\n"
				   "%s_bytecode: .export %s_bytecode\n",
				   variableName.c_str(),
				   variableName.c_str(),
				   variableName.c_str() );

	unsigned int p=0;
	while( p < blob.size() )
	{
		header += "\t.byte ";
		for( int i=0; i<16 && p<blob.size() ; i++ )
		{
			header.appendFormat( "$%02X, ", (unsigned char)blob[p++] );
		}

		header.shave( 2 );
		header.appendFormat( " ; %d\n", p );
	}

	header += "\n\n";
}

//------------------------------------------------------------------------------
// smush all these files together to make wrench.cpp, in this order!
const char* sourceOrder[]=
{
	"/utils/utils.h",
	"/utils/serializer.h",
	"/utils/simple_ll.h",
	"/utils/simple_args.h",
	"/vm/vm.h",
	"/utils/opcode.h",
	"/cc/opcode_stream.h",
	"/cc/cc.h",
	"/debug/debug_protocol.h",
	"/debug/debug.h",
	"/lib/std_serial.h",
	"/lib/std_tcp.h",
	"/lib/std_io_defs.h",
	"/lib/std_tcp.h",
	"/cc/cc.cpp",
	"/cc/link.cpp",
	"/cc/expression.cpp",
	"/cc/optimizer.cpp",
	"/cc/token.cpp",
	"/cc/include.cpp",
	"/vm/gc_object.cpp",
	"/vm/gc.cpp",
	"/vm/vm.cpp",
	"/utils/utils.cpp",
	"/utils/serializer.cpp",
	"/debug/debug.cpp",
	"/debug/disassemble.cpp",
	"/debug/debug_protocol.cpp",
	"/debug/debug_server.cpp",
	"/debug/debug_transport.cpp",
	"/vm/scheduler.cpp",
	"/vm/operations.cpp",
	"/vm/operations_compact.cpp",
	"/vm/operations_non_compact.cpp",
	"/vm/index.cpp",
	"/lib/std.cpp",
	"/lib/std_io.cpp",
	"/lib/std_tcp.cpp",
	"/lib/std_io_little_spiffs.cpp",
	"/lib/std_string.cpp",
	"/lib/std_math.cpp",
	"/lib/std_msg.cpp",
	"/lib/std_sys.cpp",
	"/lib/std_serialize.cpp",
	"/lib/std_container.cpp",
	"/lib/debug_lib.cpp",
	"/lib/esp32_lib.cpp",
	"/lib/arduino_lib.cpp",
	"/lib/fastled_lib.cpp",
	"/utils/arduino_comm.cpp",
	"/utils/linux_comm.cpp",
	""
};

//------------------------------------------------------------------------------
int usage()
{
	printf(
	"version: %d.%d\n"
	"usage: wrench <command> [options] [arguments]\n"
	"\n"
	"compiling and running:\n"
	"\n"
	"c [compile options] <infile> <outfile>\n"
	"  compile infile and write its bytecode to outfile\n"
	"\n"
	"ch [compile options] <infile> <outfile> <name>\n"
	"  compile infile and write it to outfile as a header for #include that\n"
	"  defines const int <name>_bytecodeSize and\n"
	"  const unsigned char <name>_bytecode[]\n"
	"\n"
/*
	"ca [infile] [out file] [name]\n"
	"  compile infile and output as an inc file for assembly of name \"out file\"\n"
	"  the exported constants will be:\n"
	"  [name]_bytecode and\n"
	"  [name]_bytecodeSize = $xxxx\n"
	"\n"
*/
	"p [compile options] <infile>\n"
	"  compile infile and print its disassembly\n"
	"\n"
	"r [compile options] <infile>\n"
	"  compile infile and run it\n"
	"\n"
	"compile options of c, ch, p and r, given before the file arguments:\n"
	"  -debug\n"
	"    embed debug code, needed to debug the program\n"
	"  -debugsource\n"
	"    embed debug code and a copy of the source\n"
	"  -noglobals\n"
	"    leave out the names of globals (4 bytes each), wr_getGlobalRef()\n"
	"    cannot find them then\n"
	"  -nostrict\n"
	"    do not require 'var' to declare variables\n"
	"  -I <folder>\n"
	"    also look for include \"file\" in folder (after the including\n"
	"    file's own folder, before the \"include\" folders of wrench.json),\n"
	"    may be given more than once\n"
	"\n"
	"rb <bytecode file>\n"
	"  run a compiled program\n"
	"\n"
	"d <bytecode file>\n"
	"  print the disassembly of a compiled program\n"
	"\n"
#ifdef WRENCH_INCLUDE_DEBUG_CLIENT
	"debugging:\n"
	"\n"
	"dbg [-target T] [-attach] [-project P] [file]\n"
	"  interactive console debugger. file is a .w source or a .bin compiled\n"
	"  with -debugsource\n"
	"  -target T\n"
	"    local (default), tcp:<host>:<port> or serial:<port>[:<baud>]\n"
	"  -attach\n"
	"    debug what the target already runs; without file the program and\n"
	"    its source are read from the target\n"
	"  -project P\n"
	"    the wrench.json that describes the host application to imitate,\n"
	"    default the nearest one, none to ignore it\n"
	"\n"
	"dbgserver -target T [-noupload] [-hold] [-linktimeout MS] [-context F] [-v] [-project P] [file]\n"
	"  run as a debug target that a debugger connects to, optionally\n"
	"  preloaded with file (source or bytecode)\n"
	"  -target T\n"
	"    tcp:<port> or serial:<port>[:<baud>]\n"
	"  -context F\n"
	"    also run script F in a context of its own, a program the debugger\n"
	"    sees beside file, like attach(); its global code runs at start, then\n"
	"    its tick() is called ten times a second. May be given up to 3 times\n"
	"  -noupload\n"
	"    refuse programs sent by a debugger, like setAllowUpload(false)\n"
	"  -hold\n"
	"    start file at once, halted on its first line until a debugger\n"
	"    attaches, like breakOnEntry()\n"
	"  -linktimeout MS\n"
	"    while halted, let go of a debugger not heard from for MS\n"
	"    milliseconds, like setLinkTimeout()\n"
	"  -v\n"
	"    report attaches, detaches, uploads and program ends\n"
	"  -project P\n"
	"    as for dbg\n"
	"\n"
	"dap\n"
	"  Debug Adapter Protocol server on stdin/stdout, started by VS Code\n"
	"\n"
	"lsp\n"
	"  Language Server Protocol server on stdin/stdout, started by VS Code\n"
	"\n"
#endif
	"other:\n"
	"\n"
	"v\n"
	"  show the version and this help\n"
	"\n"
	"t [testdir] [number]\n"
	"  run the test scripts listed in testdir/test_files.txt (default: tests),\n"
	"  then the internal tests; number runs only that entry of the list\n"
	"\n"
	"release <fromdir> <todir>\n"
	"  write the single file build (wrench.h, wrench.cpp) into the existing\n"
	"  folder todir, made from the sources in fromdir (src)\n"
	"\n",
	(int)WRENCH_VERSION_MAJOR, (int)WRENCH_VERSION_MINOR );
	return -1;
}

//------------------------------------------------------------------------------
static void println( WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr )
{
	for( int i=0; i<argn; ++i )
	{
		char outbuf[64000];
		argv[i].asString( outbuf, 64000 );
		printf( "%s", outbuf );
	}
	printf( "\n" );

	retVal.i = 20; // for argument-return testing
}

//------------------------------------------------------------------------------
static void print( WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr )
{
	for( int i=0; i<argn; ++i )
	{
		char outbuf[64000];
		argv[i].asString( outbuf, 64000 );
		printf( "%s", outbuf );
	}
}

struct Test
{
	int i;
};

WRState* gw = 0;
uint32_t flags = 0;

// compile the source file 'path' (its text in 'code') with the compile
// options, includes found next to the including file and in the -I folders
static int compileSourceFile( const char* path, WRstr const& code, int argn, char* argv[], unsigned char** out, int* outLen, WRstr* errMsg )
{
	WrenchIncludeResolver includes;
	includes.searchPaths = wr_includeOptions( argn, argv );
	std::string name = WrenchIncludeResolver::normalize( path );
	includes.setMain( name );
#ifdef WRENCH_INCLUDE_DEBUG_CLIENT
	// then the folders the script's wrench.json lists
	WrenchProject project;
	std::string projectError;
	if ( wr_projectForScript(name, "", project, projectError) && project.loaded() )
	{
		includes.searchPaths.insert( includes.searchPaths.end(), project.includePaths().begin(), project.includePaths().end() );
	}
#endif

	WRCompileOptions options;
	options.flags = (uint8_t)flags;
	options.name = name.c_str();
	options.include = WrenchIncludeResolver::callback;
	options.includeUser = &includes;
	return wr_compileWithOptions( code, code.size(), out, outLen, options, errMsg );
}

//------------------------------------------------------------------------------
int main( int argn, char* argv[] )
{
//	wr_setGlobalAllocator( devAlloc, devFree );
	
	assert( sizeof(WRValue) == 2*sizeof(void*) );
	assert( sizeof(float) == 4 );
	assert( sizeof(unsigned char) == 1 );

	C3test();
//	LEAKtest();

	if ( argn <= 1 )
	{
		return usage();
	}
	
	WRstr command = SimpleArgs::get( argn, argv, 1 );

	// globals are named in the bytecode unless -noglobals, -debugsource
	// implies -debug as the help text promises
	flags = (SimpleArgs::get(argn, argv, "-nostrict") ? WR_NON_STRICT_VAR : 0)
			| (SimpleArgs::get(argn, argv, "-noglobals") ? 0 : WR_INCLUDE_GLOBALS)
			| (SimpleArgs::get(argn, argv, "-debug") ? WR_EMBED_DEBUG_CODE : 0)
			| (SimpleArgs::get(argn, argv, "-debugsource") ? (WR_EMBED_DEBUG_CODE | WR_EMBED_SOURCE_CODE) : 0);

#ifdef WRENCH_INCLUDE_DEBUG_CLIENT
	// tool modes are matched on the command word only, their arguments may
	// contain anything
	if ( command == "dbg" || command == "dbgserver" )
	{
		// options in any order: -target and -project take a value, the other
		// options are switches, the one remaining argument is the file
		std::string target;
		std::string projectPath;
		std::string file;
		bool attach = false;
		bool verbose = false;
		bool noUpload = false;
		bool hold = false;
		uint32_t linkTimeout = 0;
		std::vector<std::string> contexts;
		for ( int a = 2; a < argn; ++a )
		{
			std::string arg = argv[a];
			if ( arg == "-context" && command == "dbgserver" )
			{
				if ( a + 1 >= argn )
				{
					printf( "%s needs a value\n", arg.c_str() );
					return usage();
				}
				contexts.push_back( wr_utf8FromArg(argv[++a]) );
			}
			else if ( arg == "-linktimeout" )
			{
				if ( a + 1 >= argn )
				{
					printf( "%s needs a value\n", arg.c_str() );
					return usage();
				}
				linkTimeout = (uint32_t)strtoul( argv[++a], 0, 10 );
			}
			else if ( arg == "-target" || arg == "-project" )
			{
				if ( a + 1 >= argn )
				{
					printf( "%s needs a value\n", arg.c_str() );
					return usage();
				}
				(arg == "-target" ? target : projectPath) = wr_utf8FromArg( argv[++a] );
			}
			else if ( arg == "-attach" )
			{
				attach = true;
			}
			else if ( arg == "-v" )
			{
				verbose = true;
			}
			else if ( arg == "-noupload" )
			{
				noUpload = true;
			}
			else if ( arg == "-hold" )
			{
				hold = true;
			}
			else if ( !arg.empty() && arg[0] == '-' )
			{
				printf( "unknown option %s\n", arg.c_str() );
				return usage();
			}
			else if ( file.empty() )
			{
				file = wr_utf8FromArg( argv[a] );
			}
			else
			{
				printf( "more than one file given (%s and %s)\n", file.c_str(), arg.c_str() );
				return usage();
			}
		}

		if ( command == "dbg" )
		{
			// attaching needs no file: program and source come from the target
			if ( file.empty() && !attach )
			{
				return usage();
			}
			return wr_debugCliMain( file.empty() ? 0 : file.c_str(), target.empty() ? "local" : target.c_str(), attach, 0,
				projectPath.empty() ? 0 : projectPath.c_str() );
		}

		if ( target.empty() )
		{
			return usage();
		}
		WrenchTransportSpec spec;
		std::string error;
		if ( !spec.parse(target, error) )
		{
			printf( "%s\n", error.c_str() );
			return -1;
		}

		// imitate the host application when there is a wrench.json for the
		// program (or one is named), else stand in for any host
		WrenchProject project;
		if ( (!file.empty() || !projectPath.empty()) && !wr_projectForScript(file, projectPath, project, error) )
		{
			printf( "%s\n", error.c_str() );
			return -1;
		}
		return wr_debugServerMain( spec, file.empty() ? 0 : file.c_str(), verbose, project.loaded() ? &project : 0, !noUpload, hold, linkTimeout, contexts );
	}
	else if ( command == "dap" )
	{
		return wr_dapMain( argn, argv );
	}
	else if ( command == "lsp" )
	{
		return wr_lspMain( argn, argv );
	}
#endif

	if ( SimpleArgs::get(argn, argv, "t") )
	{
		// wrench t [testdir] [number], in either order: a number picks one
		// entry of the list, anything else names the test folder
		const char* testDir = "tests";
		int number = 0;
		for( int a = 2; a < argn; ++a )
		{
			if ( isdigit((unsigned char)argv[a][0]) )
			{
				number = atoi( argv[a] );
			}
			else
			{
				testDir = argv[a];
			}
		}
		int err = runTests( testDir, number );
		printf("\n");
		return err;
	}
	else if ( SimpleArgs::get(argn, argv, "p") )
	{
		// this is ALPHA and doesn't tell you much yet :)
		
		WRstr infile;
		infile.fileToBuffer( SimpleArgs::get(argn, argv, -1) );

		unsigned char* out;
		int outLen;
		WRstr errMsg;
		int err = compileSourceFile( SimpleArgs::get(argn, argv, -1), infile, argn, argv, &out, &outLen, &errMsg );
		
		if ( err )
		{
#ifdef WRENCH_WITHOUT_COMPILER
			printf( "compile error [#%d]\n", err );
#else
			printf( "%scompile error [%s]\n", errMsg.c_str(), c_errStrings[err] );
#endif
			return -1;
		}

		char* listing = 0;
		wr_disassemble( out, outLen, &listing );
		printf("%s\n", listing );
		wr_free( listing );
	}
	else if ( SimpleArgs::get(argn, argv, "d") )
	{
		const char* filename = SimpleArgs::get(argn, argv, -1);
		WRstr bytes;
		if ( !bytes.fileToBuffer(filename) )
		{
			printf( "Could not open bytecode file [%s]\n", filename );
			return usage();
		}

		char* listing = 0;
		wr_disassemble( (const uint8_t*)bytes.c_str(), bytes.size(), &listing );
		printf( "%s\n", listing );
		wr_free( listing );
	}
	else if ( SimpleArgs::get(argn, argv, "r") )
	{
#ifndef WRENCH_WITHOUT_COMPILER
		WRstr infile;
		if ( !infile.fileToBuffer(SimpleArgs::get(argn, argv, -1)) )
		{
			printf( "Could not open source file [%s]\n", SimpleArgs::get(argn, argv, -1) );
			return usage();
		}

		unsigned char* out;
		int outLen;
		
		WRstr errMsg;
		int err = compileSourceFile( SimpleArgs::get(argn, argv, -1), infile, argn, argv, &out, &outLen, &errMsg );

//		WRstr str;
//		wr_asciiDump( out, outLen, str );
//		printf( "Code Image: %d:\n%s\n", outLen, str.c_str() );
		
		if ( err )
		{
			printf( "%scompile error [%s]\n", errMsg.c_str(), c_errStrings[err] );
			return -1;
		}

		gw = wr_newState( 128 );
		wr_loadAllLibs(gw);
		wr_registerFunction( gw, "println", println );
		wr_registerFunction( gw, "print", print );

		wr_registerLibraryConstant( gw, "rgb::count", 6 );
		
		wr_run( gw, out, outLen );

		wr_free( out );

		if ( wr_getLastError(gw) )
		{
			printf( "err: %s\n", c_errStrings[(int)wr_getLastError(gw)] );
		}

		wr_destroyState( gw );
#else
		printf( "compiler not included in this build\n" );
		return usage();
#endif
	}
	else if ( SimpleArgs::get(argn, argv, "rb") )
	{
		WRstr bytes;
		if ( !bytes.fileToBuffer(SimpleArgs::get(argn, argv, -1)) )
		{
			printf( "Could not open bytecode [%s]\n", SimpleArgs::get(argn, argv, -1) );
			return usage();
		}

		gw = wr_newState( 128 );
		wr_loadAllLibs(gw);
		wr_registerFunction( gw, "println", println );
		wr_registerFunction( gw, "print", print );

		wr_run( gw, (const unsigned char *)bytes.c_str(), bytes.size() );
		if ( wr_getLastError( gw ) )
		{
#ifndef WRENCH_WITHOUT_COMPILER
			printf( "err: %s\n", c_errStrings[(int)wr_getLastError(gw)] );
#else
			printf( "err: %d\n", (int)wr_getLastError(gw) );
#endif
		}

		wr_destroyState( gw );
	}
	else if ( SimpleArgs::get(argn, argv, "c") )
	{
#ifndef WRENCH_WITHOUT_COMPILER
		if ( argn < 4 )
		{
			printf( "[%d]\n", argn );
			return usage();
		}

		unsigned char* out;
		int outLen;

		WRstr code;
		if ( !code.fileToBuffer(SimpleArgs::get(argn, argv, -2)) )
		{
			printf( "Could not open [%s]\n", SimpleArgs::get(argn, argv, -2) );
			return usage();
		}

		WRstr errMsg;
		int err = compileSourceFile( SimpleArgs::get(argn, argv, -2), code, argn, argv, &out, &outLen, &errMsg );

		if ( err )
		{
			printf( "%scompile error [%s]\n", errMsg.c_str(), c_errStrings[err] );
			return -1;
		}

		WRstr str;
		str.set( (const char*)out, outLen );
		
//		printf( "%d:\n%s\n", outLen, str.c_str() );

		WRstr outname( SimpleArgs::get(argn, argv, -1) );
		
		if ( !str.bufferToFile(outname) )
		{
			printf( "could not write to [%s]\n", outname.c_str() );
		}

		printf( "%s -> %s\n", SimpleArgs::get(argn, argv, -2), outname.c_str() );

		wr_free( out );
#else
		printf( "compiler not included in this build\n" );
		return usage();
#endif
	}
	else if ( SimpleArgs::get(argn, argv, "ch") )
	{
#ifndef WRENCH_WITHOUT_COMPILER
		if ( argn < 4 )
		{
			printf( "[%d]\n", argn );
			return usage();
		}
		
		unsigned char* out;
		int outLen;

		WRstr code;
		if ( !code.fileToBuffer(SimpleArgs::get(argn, argv, -3)) )
		{
			printf( "Could not open [%s]\n", SimpleArgs::get(argn, argv, -3) );
			return usage();
		}

		WRstr errMsg;
		int err = compileSourceFile( SimpleArgs::get(argn, argv, -3), code, argn, argv, &out, &outLen, &errMsg );
		
		if ( err )
		{
			printf( "%scompile error [%s]\n", errMsg.c_str(), c_errStrings[err] );
			return -1;
		}

		WRstr str;
		wr_asciiDump( out, outLen, str );
		printf( "%d:\n%s\n", outLen, str.c_str() );
		
		WRstr outname( SimpleArgs::get(argn, argv, -2) );
		
		blobToHeader( WRstr((char *)out, outLen), SimpleArgs::get(argn, argv, -1), code );
		
		if ( !code.bufferToFile(outname) )
		{
			printf( "could not write to [%s]\n", outname.c_str() );
		}
		else
		{
			printf( "%s -> %s as %s\n", SimpleArgs::get(argn, argv, -3), outname.c_str(), SimpleArgs::get(argn, argv, -1) );
		}

		wr_free( out );
#else
		printf( "compiler not included in this build\n" );
		return usage();
#endif
	}
	else if ( SimpleArgs::get(argn, argv, "release") )
	{
		WRstr version;
		version.format( "%d.%d.%d", WRENCH_VERSION_MAJOR, WRENCH_VERSION_MINOR, WRENCH_VERSION_BUILD );
		version.bufferToFile( "version.txt" );

		WRstr out = "#ifdef _MSC_VER\n"
					"#pragma warning(push)\n"
					"#pragma warning(disable:4201)\n"
					"#pragma warning(disable:4996)\n"
					"#endif\n"
					"#include \"wrench.h\"\n";
		WRstr name;		
		WRstr read;
		for( int s=0; sourceOrder[s][0]; ++s )
		{
			name = SimpleArgs::get(argn, argv, -2);
			name += sourceOrder[s];
			if ( !read.fileToBuffer(name) )
			{
				printf( "error reading source [%s]\n", name.c_str() );
				return -1;
			}
			out += read;
		}

		out += "#ifdef _MSC_VER\n"
			   "#pragma warning(pop)\n"
			   "#endif\n";

		name = SimpleArgs::get(argn, argv, -1);
		name += "/wrench.cpp";
		out.bufferToFile( name );

		name = SimpleArgs::get(argn, argv, -2);
		name += "/wrench.h";
		if ( !read.fileToBuffer(name) )
		{
			printf( "error 2 reading [%s]\n", name.c_str() );
			return -1;
		}
		out = "#define WRENCH_COMBINED\n";
		out += read;

	
		name = SimpleArgs::get(argn, argv, -1);
		name += "/wrench.h";
		out.bufferToFile( name );
	}
	else
	{
		return usage();
	}

	return 0;
}

#ifndef WRENCH_WITHOUT_COMPILER
//------------------------------------------------------------------------------
void emit( WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr )
{
	for( int i=0; i<argn; ++i )
	{
		char buf[256];
		((WRstr*)usr)->appendFormat( "%s", argv[i].asString(buf, 256) );
	}

	retVal.i = 20; // for argument-return testing
}

//------------------------------------------------------------------------------
void emitln( WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr )
{
	for( int i=0; i<argn; ++i )
	{
		char buf[256];
		((WRstr*)usr)->appendFormat( "%s\n", argv[i].asString(buf, 256) );
	}

	retVal.i = 20; // for argument-return testing
}

#ifdef WRENCH_TIME_SLICES
//------------------------------------------------------------------------------
int countStringHits( const WRstr& haystack, const char* needle )
{
	int count = 0;
	const int needleLen = (int)strlen( needle );
	if ( needleLen <= 0 )
	{
		return 0;
	}

	const char* p = haystack.c_str();
	while( (p = strstr( p, needle )) )
	{
		++count;
		p += needleLen;
	}
	return count;
}
#endif

//------------------------------------------------------------------------------
void checkIsRawArray( WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr )
{
	int len = -1;
	assert( argv[0].isRawArray(&len) );
	assert( !argv[0].isWrenchArray() );
	assert( !argv[0].isHashTable() );
	assert( !argv[0].isString() );
	if (len != argv[1].asInt()) { assert(0); }
}

//------------------------------------------------------------------------------
void checkIsWrenchArray( WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr )
{
	int len = -1;
	assert( !argv[0].isRawArray() );
	assert( argv[0].isWrenchArray(&len) );
	assert( !argv[0].isHashTable() );
	assert( !argv[0].isString() );
	if (len != argv[1].asInt())	{ assert(0); }
}

//------------------------------------------------------------------------------
void checkIsString( WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr )
{
	int len = -1;
	assert( !argv[0].isRawArray() );
	assert( !argv[0].isWrenchArray() );
	assert( !argv[0].isHashTable() );
	assert( argv[0].isString(&len) );
	if (len != argv[1].asInt()) { assert(0); }
}

//------------------------------------------------------------------------------
void checkIsHashTable( WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr )
{
	int len = -1;
	assert( !argv->isRawArray() );
	assert( !argv->isWrenchArray() );
	assert( argv->isHashTable(&len) );
	assert( !argv->isString() );
	if (len != argv[1].asInt()) { assert(0); }
}

//------------------------------------------------------------------------------
void checkIter( WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr )
{
#if (__cplusplus <= 199711L)
	for ( WRValue::Iterator it = argv->begin(); it != argv->end(); ++it )
	{
		WRIteratorEntry const& E = *it;
#else
	for( WRIteratorEntry const& E : *argv )
	{
#endif
		if ( E.value->getHash() != argv[E.index+1].getHash() ) { assert(0); }
	}
}

//------------------------------------------------------------------------------
void checkStruct( WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr )
{
	WRValue* V = argv->indexStruct("Id");
	assert( V );
	assert( V->i == 20 );
	V->i = 20;
}

//------------------------------------------------------------------------------
void testStructs()
{
	const char* structProg =
	   "struct Frame( B )\n"
		"{\n"
		"	var Id = 35;\n"
		"	var fr = B;\n"
		"	var dd = Id + 10;\n"
		"}\n"
		""					
		"function isit( st )\n"
		"{\n"
		"	var local = st;\n"
		"	local.Id = 36;\n"
		"	return local.Id;\n"
		"}\n";

	WRState* w = wr_newState(20);

	unsigned char* outBytes;
	int outLen;
	wr_compile( structProg, strlen(structProg), &outBytes, &outLen );

	WRContext* c = wr_run( w, outBytes, outLen );

	WRValue s;
	WRValue* V = wr_instanceStruct( &s, c, "Frame" );

	if ( V ) // fixes "not using variable" warning
	{
		assert( V->indexStruct("Id")->ui == 35 );
		assert( V->indexStruct("fr")->ui == 0 );
		assert( V->indexStruct("dd")->ui == 45 );
		assert( !V->indexStruct("nuthin") );
		assert( wr_callFunction( c, "isit", V, 1 )->ui == 36 );
	}
	else
	{
		assert(0);
	}

	wr_destroyState( w );
	wr_free( outBytes );
}

//------------------------------------------------------------------------------
static void hashLibSum2( WRValue* stackTop, const int argn, WRContext* context )
{
	(void)context;
	if ( argn != 2 )
	{
		stackTop->init();
		return;
	}

	wr_makeInt( stackTop, (stackTop - 2)->asInt() + (stackTop - 1)->asInt() );
}

//------------------------------------------------------------------------------
static void hashLibFail( WRValue* stackTop, const int argn, WRContext* context )
{
	(void)stackTop;
	(void)argn;
	context->w->err = WR_ERR_division_by_zero;
}

static int32_t g_lastCallbackHashArg = 0;
static int g_seenCallbackHashArg = 0;
static int g_blankSeedCallbackSeen = 0;
static int g_blankSeedCallbackOk = 0;
static uint32_t g_missingCallbackValueHash = 0;
static uint32_t g_missingCallbackPopHash = 0;
static int g_missingCallbackValueSeen = 0;
static int g_missingCallbackPopSeen = 0;
static uint32_t g_missingLibValueHash = 0;
static uint32_t g_missingLibPopHash = 0;
static int g_missingLibValueSeen = 0;
static int g_missingLibPopSeen = 0;

//------------------------------------------------------------------------------
static void captureHashArg( WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr )
{
	(void)c;
	(void)usr;
	retVal.init();
	if ( argn > 0 )
	{
		g_lastCallbackHashArg = argv[0].asInt();
		g_seenCallbackHashArg = 1;
	}
}

//------------------------------------------------------------------------------
static void checkBlankSeededGlobals( WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr )
{
	(void)c;
	(void)usr;
	retVal.init();
	g_blankSeedCallbackSeen = 1;
	g_blankSeedCallbackOk = (argn == 3)
		&& (argv[0].asInt() == 101)
		&& (argv[1].asInt() == 202)
		&& (argv[2].asInt() == 303);
}

//------------------------------------------------------------------------------
static void onMissingCallback( const uint32_t signature, WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr )
{
	(void)c;
	(void)usr;

	if ( signature == g_missingCallbackValueHash )
	{
		assert( argn == 2 );
		assert( argv[0].asInt() == 7 );
		assert( argv[1].asInt() == 8 );
		wr_makeInt( &retVal, 42 );
		g_missingCallbackValueSeen = 1;
		return;
	}

	if ( signature == g_missingCallbackPopHash )
	{
		assert( argn == 1 );
		assert( argv[0].asInt() == 9 );
		retVal.init();
		g_missingCallbackPopSeen = 1;
		return;
	}

	assert( 0 );
}

//------------------------------------------------------------------------------
static void onMissingLibCallback( const uint32_t signature, WRValue* stackTop, const int argn, WRContext* context )
{
	(void)context;

	if ( signature == g_missingLibValueHash )
	{
		assert( argn == 2 );
		assert( (stackTop - 2)->asInt() == 4 );
		assert( (stackTop - 1)->asInt() == 6 );
		wr_makeInt( stackTop, 55 );
		g_missingLibValueSeen = 1;
		return;
	}

	if ( signature == g_missingLibPopHash )
	{
		assert( argn == 1 );
		assert( (stackTop - 1)->asInt() == 11 );
		g_missingLibPopSeen = 1;
		return;
	}

	assert( 0 );
}

//------------------------------------------------------------------------------
struct OpaqueStateContext
{
	int magic;
	int writes;
};

//------------------------------------------------------------------------------
static void ctxReadMagic( WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr )
{
	(void)argv;
	(void)argn;
	(void)usr;
	OpaqueStateContext* data = (OpaqueStateContext*)c->w->ctx;
	if ( !data )
	{
		retVal.init();
		return;
	}
	wr_makeInt( &retVal, data->magic );
}

//------------------------------------------------------------------------------
static void ctxAccumulate( WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr )
{
	(void)usr;
	OpaqueStateContext* data = (OpaqueStateContext*)c->w->ctx;
	if ( !data )
	{
		retVal.init();
		return;
	}

	int add = (argn > 0) ? argv[0].asInt() : 1;
	data->writes += add;
	wr_makeInt( &retVal, data->writes );
}

//------------------------------------------------------------------------------
void testCallFunctionHashLibraryFallback()
{
	WRState* w = wr_newState( 64 );
	if ( !w )
	{
		assert(0);
		return;
	}
	wr_registerLibraryFunction( w, "lib::sum2", hashLibSum2 );
	wr_registerLibraryFunction( w, "lib::fail", hashLibFail );

	unsigned char* out = 0;
	int outLen = 0;
	if ( wr_compile( "", 0, &out, &outLen, 0, WR_INCLUDE_GLOBALS ) != WR_ERR_None )
	{
		assert(0);
		wr_destroyState( w );
		return;
	}

	WRContext* c = wr_run( w, out, outLen, true );
	if ( !c )
	{
		assert(0);
		wr_destroyState( w );
		return;
	}

	WRValue args[2];
	wr_makeInt( &args[0], 7 );
	wr_makeInt( &args[1], 11 );

	WRValue* r = wr_callFunction( c, wr_hashStr("lib::sum2"), args, 2 );
	if ( !r || r->asInt() != 18 || wr_getLastError( w ) != WR_ERR_None )
	{
		assert(0);
		wr_destroyState( w );
		return;
	}

	WRValue* errRet = wr_callFunction( c, wr_hashStr("lib::fail"), 0, 0 );
	if ( errRet || wr_getLastError( w ) != WR_ERR_division_by_zero )
	{
		assert(0);
		wr_destroyState( w );
		return;
	}

	// Verify stale error from previous call does not leak into success path.
	WRValue* r2 = wr_callFunction( c, wr_hashStr("lib::sum2"), args, 2 );
	if ( !r2 || r2->asInt() != 18 || wr_getLastError( w ) != WR_ERR_None )
	{
		assert(0);
		wr_destroyState( w );
		return;
	}

	wr_destroyState( w );
}

//------------------------------------------------------------------------------
void testFunctionNameHashAsArgument()
{
	WRState* w = wr_newState( 64 );
	if ( !w )
	{
		assert(0);
		return;
	}

	g_lastCallbackHashArg = 0;
	g_seenCallbackHashArg = 0;
	wr_registerFunction( w, "print", captureHashArg );

	const char* src =
		"print( fname );\n"
		"function fname() { }\n";

	unsigned char* out = 0;
	int outLen = 0;
	WRstr errMsg;
	if ( wr_compile(src, (int)strlen(src), &out, &outLen, &errMsg, WR_INCLUDE_GLOBALS) != WR_ERR_None )
	{
		assert(0);
		wr_destroyState( w );
		return;
	}

	WRContext* c = wr_run( w, out, outLen, true );
	if ( !c || wr_getLastError( w ) != WR_ERR_None )
	{
		assert(0);
		wr_destroyState( w );
		return;
	}

	const int32_t expected = (int32_t)wr_hashStr( "fname" );
	if ( !g_seenCallbackHashArg || g_lastCallbackHashArg != expected )
	{
		assert(0);
		wr_destroyState( w );
		return;
	}

	wr_destroyState( w );
}

//------------------------------------------------------------------------------
// Confirm that a named-enum member used WITHOUT its namespace qualifier is a
// compile error (WR_ERR_var_not_seen_before_label), not a silent hash literal.
// This guards against regression of the bug where println(RED) would compile
// and output wr_hashStr("RED") instead of failing.
void testNamedEnumUnqualifiedError()
{
	printf( "test [-][named enum member without qualifier is a compile error]:\n" );

	// A script that declares a named enum then uses the bare member name.
	// The bare 'RED' must not be accepted as a function-hash literal.
	const char* src = "enum Color { RED, GREEN, BLUE }\n"
					  "println( RED );\n";

	unsigned char* out = 0;
	int outLen = 0;
	WRError result = wr_compile( src, (int)strlen(src), &out, &outLen, 0, WR_INCLUDE_GLOBALS );
	free( out );

	assert( result == WR_ERR_var_not_seen_before_label );
	if ( result != WR_ERR_var_not_seen_before_label )
	{
		printf( "WR_ERR_var_not_seen_before_label expected but NOT EMITTED\n" );
	}
}

//------------------------------------------------------------------------------
void testBlankVariablesCannotBeInitialized()
{
	printf( "test [-][blank variables cannot be initialized]:\n" );

	const char* src[] =
	{
		"blank X = 1;\n",
		"blank var X = 1;\n",
		"var blank X = 1;\n",
		0
	};

	for( int i=0; src[i]; ++i )
	{
		unsigned char* out = 0;
		int outLen = 0;
		WRError result = wr_compile( src[i], (int)strlen(src[i]), &out, &outLen, 0, WR_INCLUDE_GLOBALS );
		free( out );

		assert( result == WR_ERR_blank_variables_cannot_be_initialized );
		if ( result != WR_ERR_blank_variables_cannot_be_initialized )
		{
			printf( "WR_ERR_blank_variables_cannot_be_initialized expected but NOT EMITTED\n" );
		}
	}
}

//------------------------------------------------------------------------------
void testFunctionNotFoundErrors()
{
	printf( "test [-][missing functions set WR_ERR_function_not_found]:\n" );

	{
		WRState* w = wr_newState( 64 );
		if ( !w )
		{
			assert(0);
			return;
		}

		const char* src = "missingPop(9);\n";
		unsigned char* out = 0;
		int outLen = 0;
		if ( wr_compile( src, (int)strlen(src), &out, &outLen, 0, WR_INCLUDE_GLOBALS ) != WR_ERR_None )
		{
			assert(0);
			wr_destroyState( w );
			return;
		}

		WRContext* c = wr_run( w, out, outLen, true );
		if ( c || wr_getLastError( w ) != WR_ERR_function_not_found )
		{
			assert(0);
			wr_destroyState( w );
			return;
		}

		wr_destroyState( w );
	}

	{
		WRState* w = wr_newState( 64 );
		if ( !w )
		{
			assert(0);
			return;
		}

		const char* src = "function run() { return missingValue(7, 8); }\n";
		unsigned char* out = 0;
		int outLen = 0;
		if ( wr_compile( src, (int)strlen(src), &out, &outLen, 0, WR_INCLUDE_GLOBALS ) != WR_ERR_None )
		{
			assert(0);
			wr_destroyState( w );
			return;
		}

		WRContext* c = wr_run( w, out, outLen, true );
		if ( !c || wr_getLastError( w ) != WR_ERR_None )
		{
			assert(0);
			wr_destroyState( w );
			return;
		}

		WRValue* r = wr_callFunction( c, "run" );
		if ( r || wr_getLastError( w ) != WR_ERR_function_not_found )
		{
			assert(0);
			wr_destroyState( w );
			return;
		}

		wr_destroyState( w );
	}
}

static void failingNotFound( const uint32_t signature, WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr )
{
	if ( signature == wr_hashStr("known") )
	{
		retVal.init( 5 );
		return;
	}
	c->w->err = WR_ERR_function_not_found;
}

static void failingLibNotFound( const uint32_t signature, WRValue* stackTop, const int argn, WRContext* context )
{
	context->w->err = WR_ERR_lib_function_not_found;
}

// not-found handlers may answer some names and still fail others, the VM
// remembers which name was missing
bool testNotFoundHandlerCanFail()
{
	printf( "test [-][not-found handlers can fail the call]: " );
	bool ok = true;
	const char* sources[] =
	{
		"var a = known(); var b = unknownThing( a );",
		"var a = known(); var b = app::open( a );",
	};
	uint32_t expectedHash[] = { wr_hashStr("unknownThing"), wr_hashStr("app::open") };
	WRError expectedError[] = { WR_ERR_function_not_found, WR_ERR_lib_function_not_found };

	for ( int i = 0; i < 2; ++i )
	{
		WRState* w = wr_newState( 64 );
		wr_setOnCallbackNotFound( w, failingNotFound );
		wr_setOnLibCallbackNotFound( w, failingLibNotFound );
		unsigned char* out = 0;
		int outLen = 0;
		if ( wr_compile(sources[i], (int)strlen(sources[i]), &out, &outLen, 0, WR_INCLUDE_GLOBALS) != WR_ERR_None )
		{
			ok = false;
			wr_destroyState( w );
			continue;
		}
		WRContext* c = wr_run( w, out, outLen, true );
		ok = ok && !c && wr_getLastError( w ) == expectedError[i] && wr_getMissingFunctionHash( w ) == expectedHash[i];
		wr_destroyState( w );
	}

	// a handler that answers keeps the program running
	{
		WRState* w = wr_newState( 64 );
		wr_setOnCallbackNotFound( w, failingNotFound );
		const char* src = "var a = known() + 1;";
		unsigned char* out = 0;
		int outLen = 0;
		WRContext* c = 0;
		if ( wr_compile(src, (int)strlen(src), &out, &outLen, 0, WR_INCLUDE_GLOBALS) == WR_ERR_None )
		{
			c = wr_run( w, out, outLen, true );
		}
		WRValue* a = c ? wr_getGlobalRef( c, "a" ) : 0;
		ok = ok && c && wr_getLastError( w ) == WR_ERR_None && a && a->asInt() == 6;
		wr_destroyState( w );
	}

	printf( ok ? "PASS\n" : "FAIL\n" );
	return ok;
}

static WRError compileOnly( WRstr const& src, uint8_t flags =WR_INCLUDE_GLOBALS, WRstr* listing =0 )
{
	unsigned char* out = 0;
	int outLen = 0;
	WRError err = wr_compile( src.c_str(), src.size(), &out, &outLen, 0, flags );
	if ( listing && err == WR_ERR_None )
	{
		wr_disassemble( out, outLen, *listing );
	}
	free( out );
	return err;
}

static WRstr joined( const char* a, WRstr const& b, const char* c )
{
	WRstr s( a );
	s += b;
	s += c;
	return s;
}

static WRstr repeated( const char* open, int count, const char* middle, const char* close )
{
	WRstr s;
	for( int i=0; i<count; ++i )
	{
		s += open;
	}
	s += middle;
	for( int i=0; i<count; ++i )
	{
		s += close;
	}
	return s;
}

bool testCompilerLimits()
{
	printf( "test [-][compiler limits are errors, not crashes]:\n" );
	bool ok = true;

	// the argument count is one byte
	WRstr args;
	for( int i=0; i<255; ++i )
	{
		args.appendFormat( "%s%d", i ? "," : "", i );
	}
	ok = ok && compileOnly( joined("function f() { return 0; } f(", args, ");") ) == WR_ERR_None;
	ok = ok && compileOnly( joined("function f() { return 0; } f(", args, ",255);") ) == WR_ERR_too_many_arguments;

	// so is a struct initializer position
	WRstr values;
	for( int i=0; i<256; ++i )
	{
		values.appendFormat( "%s%d", i ? "," : "", i );
	}
	ok = ok && compileOnly( joined("struct S { var a; } var s = new S { ", values, " };") ) == WR_ERR_too_many_locals;

	// nesting is bounded
	ok = ok && compileOnly( joined("var x = ", repeated("(", 100, "1", ")"), ";") ) == WR_ERR_None;
	ok = ok && compileOnly( joined("var x = ", repeated("(", 5000, "1", ")"), ";") ) == WR_ERR_nesting_too_deep;
	ok = ok && compileOnly( joined("var x = ", repeated("{", 5000, "1", "}"), ";") ) == WR_ERR_nesting_too_deep;
	ok = ok && compileOnly( repeated("{", 5000, "x = 1;", "}") ) == WR_ERR_nesting_too_deep;
	ok = ok && compileOnly( repeated("if(1)", 5000, "x = 1;", "") ) == WR_ERR_nesting_too_deep;

	// with debug info a store that the next statement overwrites stays, so a
	// stop between the two shows it
	WRstr listing;
	ok = ok && compileOnly( "function f() { var a = 1;\na = 2;\nreturn a; }", WR_INCLUDE_GLOBALS | WR_EMBED_DEBUG_CODE, &listing ) == WR_ERR_None;
	ok = ok && listing.find( "imm=0x01" ) != WRstr::npos;

	// a literal, a call result or a computed value cannot be assigned to
	const char* notAssignable[] =
	{
		"3 = 4;",
		"5++;",
		"++3;",
		"\"s\" += 1;",
		"var a = 1; var b = 2; a + b = 7;",
		"var a = 1; -a = 5;",
		"function f() { return 3; } f() = 4;",
		"struct S { var x; } var t = new S; function ft() { return t; } ft().x = 4;",
		"struct S { var x; } var t = new S; function ft() { return t; } ft().x++;",
		"var arr[] = { 1 }; function fa() { return arr; } fa()[0] = 9;",
		"var h = { \"k\":1 }; function fh() { return h; } fh().k += 8;",
		"(5) = 3;",
		"struct S { var x; } var t = new S; function ft() { return t; } (ft()).x = 4;",
		"io::O_CREAT = 3;",
		0
	};
	for( int i=0; notAssignable[i]; ++i )
	{
		ok = ok && compileOnly( notAssignable[i] ) == WR_ERR_not_assignable;
	}

	// variables, members and elements still are, also through parentheses
	// or a ternary, which write through
	ok = ok && compileOnly( "struct S { var x; } var t = new S; var u = new S; var c = 1;"
							"var arr[] = { 1, 2 }; var h = { \"k\":1 }; var m = { \"a\": { \"b\": 1 } };"
							"(t).x = 2; (c ? t : u).x = 3; arr[1] = 5; h.k = 6; t.x++; arr[0] += 2; m.a.b = 9; (c) = 2;" ) == WR_ERR_None;

	// a literal has no members, its count and characters stay readable
	ok = ok && compileOnly( "var n = \"abc\".length;" ) == WR_ERR_member_of_literal;
	ok = ok && compileOnly( "var n = 'c'.x;" ) == WR_ERR_member_of_literal;
	ok = ok && compileOnly( "var n = io::O_CREAT.x;" ) == WR_ERR_member_of_literal;
	ok = ok && compileOnly( "var n = \"abc\"._count; var c = \"abc\"[1];" ) == WR_ERR_None;

	// a goto label once per function
	ok = ok && compileOnly( "var i = 0;\nfirst:\ni++;\nfirst:\ni++;\n" ) == WR_ERR_bad_goto_label;
	ok = ok && compileOnly( "function f() { here: return 1; } function g() { here: return 2; }" ) == WR_ERR_None;

	printf( ok ? "PASS\n" : "FAIL\n" );
	return ok;
}

static WRstr manyStatements( int count )
{
	WRstr s;
	for( int i=0; i<count; ++i )
	{
		s += "a = a + 3;\n";
	}
	return s;
}

// compiles and runs the program, then compares an integer global
static bool globalAfterRun( WRstr const& src, const char* global, int expected )
{
	unsigned char* out = 0;
	int outLen = 0;
	if ( wr_compile(src.c_str(), src.size(), &out, &outLen, 0, WR_INCLUDE_GLOBALS) != WR_ERR_None )
	{
		return false;
	}

	WRState* w = wr_newState();
	WRContext* context = wr_run( w, out, outLen, true );
	WRValue* value = context ? wr_getGlobalRef( context, global ) : 0;
	bool ok = value && value->asInt() == expected;
	wr_destroyState( w );
	return ok;
}

bool testLongJumps()
{
	printf( "test [-][jumps and switches farther than 16 bits]: " );
	bool ok = true;

	// 9000 statements are about 54KB of code to jump over
	WRstr big = manyStatements( 9000 );
	WRstr half = manyStatements( 4500 );
	WRstr part = manyStatements( 3500 );
	WRstr small = manyStatements( 1500 );

	WRstr src;
	src = "var a = 0; var s = 0; for( var i = 0; i < 2; ++i ) { if ( i == 1 ) { continue; }\n";
	src += big;
	src += "s++; }";
	ok = ok && globalAfterRun( src, "a", 27000 ) && globalAfterRun( src, "s", 1 );

	WRstr listing;
	ok = ok && compileOnly( src, WR_INCLUDE_GLOBALS, &listing ) == WR_ERR_None;
	ok = ok && listing.find( "RelativeJump32" ) != WRstr::npos;

	src = "var a = 0; var c = 0; if ( c ) {\n";
	src += half;
	src += "} else { a = -1; }\nif ( !c ) {\n";
	src += half;
	src += "}";
	ok = ok && globalAfterRun( src, "a", 13499 );

	src = "var a = 0; var n = 0; while ( n < 2 ) { n++; if ( n == 3 ) { break; }\n";
	src += big;
	src += "}";
	ok = ok && globalAfterRun( src, "a", 54000 );

	src = "var a = 0; var arr[] = { 1, 2 }; var e; for( e : arr ) {\n";
	src += big;
	src += "}";
	ok = ok && globalAfterRun( src, "a", 54000 );

	src = "var a = 0; goto skip;\n";
	src += big;
	src += "skip:\na = a + 1;";
	ok = ok && globalAfterRun( src, "a", 1 );

	// far cases reached through trampolines after the switch table
	src = "var a = 0; var k = 0; var r = 0; while( k < 3 ) { switch( k ) { case 0: r += 1;\n";
	src += part;
	src += "break; case 1: r += 10;\n";
	src += part;
	src += "break; default: r += 100;\n";
	src += small;
	src += "} k++; }";
	ok = ok && globalAfterRun( src, "r", 111 );

	src = "var a = 0; var r = 0; var h = 7777; switch( h ) { case 7777: r = 7;\n";
	src += big;
	src += "break; case 12345: r = 9; break; }";
	ok = ok && globalAfterRun( src, "r", 7 );

	// a near jump stays 16 bits
	ok = ok && compileOnly( "var a = 0; while( a < 3 ) { a++; }", WR_INCLUDE_GLOBALS, &listing ) == WR_ERR_None;
	ok = ok && listing.find( "RelativeJump32" ) == WRstr::npos;

	printf( ok ? "PASS\n" : "FAIL\n" );
	return ok;
}

static void robustFail( WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr )
{
	c->w->err = (WRError)(WR_USER + 1);
}

static void robustLibFail( WRValue* stackTop, const int argn, WRContext* context )
{
	context->w->err = (WRError)(WR_USER + 2);
}

// calls the script function "inner", which fails; usr set means the
// callback handles that and clears the error
static void robustRelay( WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr )
{
	wr_callFunction( c, "inner" );
	if ( usr )
	{
		c->w->err = WR_ERR_None;
		retVal.init( 4 );
	}
}

// runs the program with its first code bytes replaced (the image carries
// no globals and no functions, so its code starts at byte 3)
static bool runPatchedImage( const char* src, const unsigned char* patch, int patchLen, WRError expected )
{
	unsigned char* out = 0;
	int outLen = 0;
	if ( wr_compile(src, (int)strlen(src), &out, &outLen, 0, 0) != WR_ERR_None )
	{
		return false;
	}
	if ( out[1] != 0 || outLen < 3 + patchLen + 4 )
	{
		free( out );
		return false;
	}

	memcpy( out + 3, patch, patchLen );
	uint32_t crc = wr_hash_read8( out, outLen - 4 ) + WRENCH_VERSION_MAJOR;
	out[outLen - 4] = (unsigned char)crc;
	out[outLen - 3] = (unsigned char)(crc >> 8);
	out[outLen - 2] = (unsigned char)(crc >> 16);
	out[outLen - 1] = (unsigned char)(crc >> 24);

	WRState* w = wr_newState();
	WRContext* c = wr_run( w, out, outLen, true );
	bool ok = !c && wr_getLastError( w ) == expected;
	wr_destroyState( w );
	return ok;
}

static WRContext* runSource( WRState* w, const char* src )
{
	unsigned char* out = 0;
	int outLen = 0;
	if ( wr_compile(src, (int)strlen(src), &out, &outLen, 0, WR_INCLUDE_GLOBALS) != WR_ERR_None )
	{
		return 0;
	}
	return wr_run( w, out, outLen, true );
}

static const char* c_threadScript =
	"var total = 0;\n"
	"for( var i = 0; i < 3000; ++i ) { var h = { \"k\": \"v\" + i }; var a[] = { i, h }; total += a[0] + h.k._count; }\n";

static void runThreadScript( const unsigned char* image, int imageLen, int* total )
{
	WRState* w = wr_newState();
	WRContext* c = wr_run( w, image, imageLen );
	WRValue* value = c ? wr_getGlobalRef( c, "total" ) : 0;
	*total = value ? value->asInt() : -1;
	wr_destroyState( w );
}

// include "file" from a table of names and texts
struct IncludeTable
{
	const char* const* entries; // name, text, name, text, ..., 0
	int calls;
};

static bool tableInclude( const char* name, const char* includer, WRstr& source, WRstr& resolved, void* usr )
{
	IncludeTable* table = (IncludeTable*)usr;
	++table->calls;
	for( int i = 0; table->entries[i]; i += 2 )
	{
		if ( !strcmp(table->entries[i], name) )
		{
			source = table->entries[i + 1];
			resolved = name;
			return true;
		}
	}
	return false;
}

static WRError compileWithTable( const char* source, const char* const* entries, int* line = 0, WRstr* file = 0, WRstr* message = 0,
	uint8_t flags = WR_INCLUDE_GLOBALS, unsigned char** image = 0, int* imageLen = 0 )
{
	IncludeTable table;
	table.entries = entries;
	table.calls = 0;
	WRCompileOptions options;
	options.flags = flags;
	options.name = "main.w";
	options.include = tableInclude;
	options.includeUser = &table;
	unsigned char* out = 0;
	int outLen = 0;
	WRError err = wr_compileWithOptions( source, (int)strlen(source), &out, &outLen, options, message, line, 0, file );
	if ( image )
	{
		*image = out;
		*imageLen = outLen;
	}
	else
	{
		free( out );
	}
	return err;
}

static int runWithTable( const char* source, const char* const* entries, const char* global )
{
	unsigned char* out = 0;
	int outLen = 0;
	if ( compileWithTable(source, entries, 0, 0, 0, WR_INCLUDE_GLOBALS, &out, &outLen) != WR_ERR_None )
	{
		return -1;
	}
	WRState* w = wr_newState();
	WRContext* c = wr_run( w, out, outLen, true );
	WRValue* v = c ? wr_getGlobalRef( c, global ) : 0;
	int result = v ? v->asInt() : -1;
	wr_destroyState( w );
	return result;
}

bool testIncludes()
{
	printf( "test [-][include and private]: " );
	bool ok = true;

	const char* files[] =
	{
		"a.w", "include \"b.w\";\nvar fromA = 1;\nfunction twice( x ) { return x * 2; }\n",
		"b.w", "include \"a.w\";\nvar fromB = 2;\n",
		"bad.w", "var fine = 1;\n\nvar broken = ;\n",
		"nested.w", "function f()\n{\n\tinclude \"a.w\";\n}\n",
		"loop.w", "include \"loop2.w\";\n",
		"loop2.w", "include \"loop3.w\";\n",
		"loop3.w", "var deep = 1;\n",
		"priv.w", "private var hidden = 5;\nprivate function secret() { return 3; }\nfunction reveal() { return hidden + secret(); }\n",
		0
	};

	// each file once, cycles included
	ok = ok && runWithTable( "include \"a.w\";\ninclude \"b.w\";\ninclude \"a.w\";\nvar r = twice( fromA + fromB );", files, "r" ) == 6;

	// an error in an included file names that file and its own line
	int line = 0;
	WRstr file;
	WRstr message;
	ok = ok && compileWithTable( "var x = 1;\ninclude \"bad.w\";\n", files, &line, &file, &message ) == WR_ERR_bad_expression;
	ok = ok && file == "bad.w" && line == 3 && strstr( message.c_str(), "bad.w:3" ) != 0;

	// and one after an include keeps its file's line numbers
	ok = ok && compileWithTable( "include \"a.w\";\n\nvar y = ;\n", files, &line, &file ) == WR_ERR_bad_expression;
	ok = ok && file == "main.w" && line == 3;

	ok = ok && compileWithTable( "var q = 1;\ninclude \"missing.w\";\n", files, &line, &file ) == WR_ERR_include_not_found;
	ok = ok && line == 2 && file == "main.w";
	ok = ok && compileWithTable( "include \"nested.w\";\n", files, &line, &file ) == WR_ERR_include_not_at_top_level;
	ok = ok && file == "nested.w" && line == 3;
	ok = ok && compileWithTable( "if ( 1 ) { include \"a.w\"; }\n", files ) == WR_ERR_include_not_at_top_level;

	// include in comments and strings is no include
	ok = ok && runWithTable( "// include \"missing.w\"\n/* include \"missing.w\" **/\nvar s = \"include \\\"missing.w\\\"\";\nvar r = 4;", files, "r" ) == 4;

	// without a callback include fails, everything else compiles as before
	unsigned char* out = 0;
	int outLen = 0;
	ok = ok && wr_compile( "include \"a.w\";", 14, &out, &outLen ) == WR_ERR_include_not_supported;
	ok = ok && wr_compile( "var include = 1; var r = include + 1;", 37, &out, &outLen ) == WR_ERR_None;
	free( out );

	// nesting deeper than WRENCH_MAX_INCLUDE_DEPTH
	{
		static char names[WRENCH_MAX_INCLUDE_DEPTH + 2][16];
		static char texts[WRENCH_MAX_INCLUDE_DEPTH + 2][32];
		const char* chain[2 * (WRENCH_MAX_INCLUDE_DEPTH + 2) + 1];
		int n = 0;
		for( int i = 0; i <= WRENCH_MAX_INCLUDE_DEPTH; ++i )
		{
			snprintf( names[i], sizeof(names[i]), "d%d.w", i );
			snprintf( texts[i], sizeof(texts[i]), "include \"d%d.w\";", i + 1 );
			chain[n++] = names[i];
			chain[n++] = texts[i];
		}
		chain[n] = 0;
		ok = ok && compileWithTable( "include \"d0.w\";", chain ) == WR_ERR_include_too_deep;
	}

	// private names: the file's own, invisible elsewhere, not found by the host
	ok = ok && runWithTable( "include \"priv.w\";\nvar hidden = 10;\nvar r = reveal() + hidden;", files, "r" ) == 18;
	ok = ok && runWithTable( "include \"priv.w\";\nvar r = 0;\nfunction secret() { return 40; }\nr = secret() + reveal();", files, "r" ) == 48;
	ok = ok && runWithTable( "include \"priv.w\";\nvar r = 1;", files, "hidden" ) == -1;
	ok = ok && compileWithTable( "private x = 1;", files ) == WR_ERR_unexpected_token;
	ok = ok && compileWithTable( "private struct S { var a; };", files ) == WR_ERR_unexpected_token;
	ok = ok && compileWithTable( "private function f() {}\nprivate function f() {}", files ) == WR_ERR_function_redefined;

	printf( ok ? "PASS\n" : "FAIL\n" );
	return ok;
}

bool testVmRobustness()
{
	printf( "test [-][vm: callback errors, bad opcodes, copies, stack, threads]: " );
	bool ok = true;

	// a callback or library function that sets an error stops the script
	{
		WRState* w = wr_newState();
		wr_registerFunction( w, "fail", robustFail );
		wr_registerLibraryFunction( w, "robust::fail", robustLibFail );
		ok = ok && !runSource( w, "var a = 1; fail(); a = 2;" ) && wr_getLastError( w ) == (WRError)(WR_USER + 1);
		ok = ok && !runSource( w, "var a = 1; robust::fail(); a = 2;" ) && wr_getLastError( w ) == (WRError)(WR_USER + 2);
		wr_destroyState( w );
	}

	// a call back into the script that failed stops the outer call too,
	// unless the callback clears the error (calling back is possible once
	// wr_run() is done)
	{
		const char* src = "function inner() { return missing(); } function outer() { return relay() + 1; }";
		WRState* w = wr_newState();
		wr_registerFunction( w, "relay", robustRelay );
		WRContext* c = runSource( w, src );
		ok = ok && c && !wr_callFunction( c, "outer" ) && wr_getLastError( w ) == WR_ERR_function_not_found;
		wr_destroyState( w );

		w = wr_newState();
		wr_registerFunction( w, "relay", robustRelay, (void*)1 );
		c = runSource( w, src );
		WRValue* r = c ? wr_callFunction( c, "outer" ) : 0;
		ok = ok && r && r->asInt() == 5 && wr_getLastError( w ) == WR_ERR_None;
		wr_destroyState( w );
	}

	// the last return value comes back resolved, not as a reference
	{
		WRState* w = wr_newState();
		WRContext* c = runSource( w, "var g = 7; var h = { \"k\": 9 }; function f() { return g; } function f2() { return h.k; }" );
		ok = ok && c && wr_callFunction( c, "f" );
		WRValue* v = c ? wr_returnValueFromLastCall( c ) : 0;
		ok = ok && v && v->type == WR_INT && v->i == 7;
		ok = ok && c && wr_callFunction( c, "f2" );
		v = c ? wr_returnValueFromLastCall( c ) : 0;
		ok = ok && v && v->type == WR_INT && v->i == 9;
		wr_destroyState( w );
	}

	// opcodes the VM does not know fail the run instead of jumping anywhere
	{
		const char* src = "var a = 1; a = a + 2; a = a * 3;";
		const unsigned char placeholder253[] = { 253 };
		const unsigned char placeholder254[] = { 254 };
		const unsigned char last[] = { 255 };
		const unsigned char extended[] = { O_Extended, 200 };
		ok = ok && runPatchedImage( src, placeholder253, 1, WR_ERR_unknown_opcode );
		ok = ok && runPatchedImage( src, placeholder254, 1, WR_ERR_unknown_opcode );
		ok = ok && runPatchedImage( src, last, 1, WR_ERR_unknown_opcode );
		ok = ok && runPatchedImage( src, extended, 2, WR_ERR_unknown_opcode );
	}

	// a value copied into another context lives on after its source is gone
	{
		const char* fromSource =
			"var t = { \"k\": \"text\", 3: 4.5 }; var arr[] = { 1, \"two\", 3 }; var n = 12;"
			"struct S { var x; } var s = new S;";
		const char* toSource =
			"var t; var arr; var n; var s = 5;"
			"function check() { for( var i = 0; i < 2000; ++i ) { var junk = \"junk\" + i; }"
			"return t[\"k\"] == \"text\" && t[3] == 4.5 && t._count == 2 && arr[1] == \"two\" && arr._count == 3 && n == 12 ? 1 : 0; }";
		WRState* w = wr_newState();
		WRContext* from = runSource( w, fromSource );
		WRContext* to = runSource( w, toSource );
		ok = ok && from && to;
		if ( from && to )
		{
			ok = ok && wr_copyValue( to, *wr_getGlobalRef(to, "t"), *wr_getGlobalRef(from, "t") );
			ok = ok && wr_copyValue( to, *wr_getGlobalRef(to, "arr"), *wr_getGlobalRef(from, "arr") );
			ok = ok && wr_copyValue( to, *wr_getGlobalRef(to, "n"), *wr_getGlobalRef(from, "n") );

			// a struct cannot be copied, the destination is cleared
			WRValue* s = wr_getGlobalRef( to, "s" );
			ok = ok && !wr_copyValue( to, *s, *wr_getGlobalRef(from, "s") ) && s->type == WR_INT && s->i == 0;

			wr_destroyContext( from );
			WRValue* r = wr_callFunction( to, "check" );
			ok = ok && r && r->asInt() == 1;
		}
		wr_destroyState( w );
	}

	// pushing deeper than the stack (here the arguments of one call) fails
	// the run instead of overwriting the memory behind it
	{
		WRstr src( "function f() { return 3; } var a = 1; var b = f( a" );
		for( int i=1; i<60; ++i )
		{
			src += ", a";
		}
		src += " );";
#ifdef WRENCH_PROTECT_STACK_FROM_OVERFLOW
		WRState* small = wr_newState( 32 );
		ok = ok && !runSource( small, src.c_str() ) && wr_getLastError( small ) == WR_ERR_stack_overflow;
		wr_destroyState( small );
#endif

		WRState* w = wr_newState();
		WRContext* c = runSource( w, src.c_str() );
		WRValue* b = c ? wr_getGlobalRef( c, "b" ) : 0;
		ok = ok && b && b->asInt() == 3;
		wr_destroyState( w );
	}

	// states running at the same time on different threads
	{
		unsigned char* image = 0;
		int imageLen = 0;
		if ( wr_compile(c_threadScript, (int)strlen(c_threadScript), &image, &imageLen, 0, WR_INCLUDE_GLOBALS) == WR_ERR_None )
		{
			int totals[4] = { 0, 0, 0, 0 };
			std::thread threads[4];
			for( int i=0; i<4; ++i )
			{
				threads[i] = std::thread( runThreadScript, image, imageLen, totals + i );
			}
			// 0 + 1 + ... + 2999, plus the lengths of "v0" ... "v2999"
			int expected = 4498500 + 10 * 2 + 90 * 3 + 900 * 4 + 2000 * 5;
			for( int i=0; i<4; ++i )
			{
				threads[i].join();
				ok = ok && totals[i] == expected;
			}
			free( image );
		}
		else
		{
			ok = false;
		}
	}

	printf( ok ? "PASS\n" : "FAIL\n" );
	return ok;
}

//------------------------------------------------------------------------------
void testLibFunctionNotFoundErrors()
{
	printf( "test [-][missing library functions set WR_ERR_lib_function_not_found]:\n" );

	{
		WRState* w = wr_newState( 64 );
		if ( !w )
		{
			assert(0);
			return;
		}

		const char* src = "libmissing::pop(11);\n";
		unsigned char* out = 0;
		int outLen = 0;
		if ( wr_compile( src, (int)strlen(src), &out, &outLen, 0, WR_INCLUDE_GLOBALS ) != WR_ERR_None )
		{
			assert(0);
			wr_destroyState( w );
			return;
		}

		WRContext* c = wr_run( w, out, outLen, true );
		if ( c || wr_getLastError( w ) != WR_ERR_lib_function_not_found )
		{
			assert(0);
			wr_destroyState( w );
			return;
		}

		wr_destroyState( w );
	}

	{
		WRState* w = wr_newState( 64 );
		if ( !w )
		{
			assert(0);
			return;
		}

		const char* src = "function run() { return libmissing::value(4, 6); }\n";
		unsigned char* out = 0;
		int outLen = 0;
		if ( wr_compile( src, (int)strlen(src), &out, &outLen, 0, WR_INCLUDE_GLOBALS ) != WR_ERR_None )
		{
			assert(0);
			wr_destroyState( w );
			return;
		}

		WRContext* c = wr_run( w, out, outLen, true );
		if ( !c || wr_getLastError( w ) != WR_ERR_None )
		{
			assert(0);
			wr_destroyState( w );
			return;
		}

		WRValue* r = wr_callFunction( c, "run" );
		if ( r || wr_getLastError( w ) != WR_ERR_lib_function_not_found )
		{
			assert(0);
			wr_destroyState( w );
			return;
		}

		wr_destroyState( w );
	}
}

//------------------------------------------------------------------------------
void testOnCallbackNotFound()
{
	printf( "test [-][onCallbackNotFound handles missing functions]:\n" );

	WRState* w = wr_newState( 64 );
	if ( !w )
	{
		assert(0);
		return;
	}

	g_missingCallbackValueHash = wr_hashStr( "missingValue" );
	g_missingCallbackPopHash = wr_hashStr( "missingPop" );
	g_missingCallbackValueSeen = 0;
	g_missingCallbackPopSeen = 0;
	wr_setOnCallbackNotFound( w, onMissingCallback );

	const char* src =
		"missingPop(9);\n"
		"function run() { return missingValue(7, 8); }\n";

	unsigned char* out = 0;
	int outLen = 0;
	if ( wr_compile( src, (int)strlen(src), &out, &outLen, 0, WR_INCLUDE_GLOBALS ) != WR_ERR_None )
	{
		assert(0);
		wr_destroyState( w );
		return;
	}

	WRContext* c = wr_run( w, out, outLen, true );
	if ( !c || wr_getLastError( w ) != WR_ERR_None || !g_missingCallbackPopSeen )
	{
		assert(0);
		wr_destroyState( w );
		return;
	}

	WRValue* r = wr_callFunction( c, "run" );
	if ( !r || r->asInt() != 42 || wr_getLastError( w ) != WR_ERR_None || !g_missingCallbackValueSeen )
	{
		assert(0);
		wr_destroyState( w );
		return;
	}

	wr_destroyState( w );
}

//------------------------------------------------------------------------------
void testOnLibCallbackNotFound()
{
	printf( "test [-][onLibCallbackNotFound handles missing library functions]:\n" );

	WRState* w = wr_newState( 64 );
	if ( !w )
	{
		assert(0);
		return;
	}

	g_missingLibValueHash = wr_hashStr( "libmissing::value" );
	g_missingLibPopHash = wr_hashStr( "libmissing::pop" );
	g_missingLibValueSeen = 0;
	g_missingLibPopSeen = 0;
	wr_setOnLibCallbackNotFound( w, onMissingLibCallback );

	const char* src =
		"libmissing::pop(11);\n"
		"function run() { return libmissing::value(4, 6); }\n";

	unsigned char* out = 0;
	int outLen = 0;
	if ( wr_compile( src, (int)strlen(src), &out, &outLen, 0, WR_INCLUDE_GLOBALS ) != WR_ERR_None )
	{
		assert(0);
		wr_destroyState( w );
		return;
	}

	WRContext* c = wr_run( w, out, outLen, true );
	if ( !c || wr_getLastError( w ) != WR_ERR_None || !g_missingLibPopSeen )
	{
		assert(0);
		wr_destroyState( w );
		return;
	}

	WRValue* r = wr_callFunction( c, "run" );
	if ( !r || r->asInt() != 55 || wr_getLastError( w ) != WR_ERR_None || !g_missingLibValueSeen )
	{
		assert(0);
		wr_destroyState( w );
		return;
	}

	wr_destroyState( w );
}

//------------------------------------------------------------------------------
void testDropClobberedInitializerStores()
{
	printf( "test [-][drop initializer immediately clobbered by assign]:\n" );

	const char* src = "var g = 1; g = 2;\n"
					  "var gh = \"head\";\n"
					  "function f() { var a = 1; a = 2; return a + g; }\n"
					  "function s(tail) { var h = gh + tail; h = \"head\"; h += tail; return h; }\n"
					  "function compareNumber(q) { q = 3; q = 4 < q; return q; }\n"
					  "function compareEmpty(text) { text = \"\"; text = \"\" == text; return text; }\n"
					  "function multiply(v) { v = 10; v = v * 2; return v; }\n"
					  "function branch(v) { if (v > 127) { v = 255 - v; } v = v * 2; return v; }\n";

	unsigned char* out = 0;
	int outLen = 0;
	WRError result = wr_compile( src, (int)strlen(src), &out, &outLen, 0, WR_INCLUDE_GLOBALS );
	if ( result != WR_ERR_None )
	{
		assert(0);
		return;
	}

	WRstr listing;
	wr_disassemble( out, outLen, listing );
	assert( listing.find( "LiteralInt8ToGlobal            g[0x00] imm=0x01" ) == WRstr::npos );
	assert( listing.find( "LiteralInt8ToLocal             l[0x00] imm=0x01" ) == WRstr::npos );
	assert( listing.find( "LiteralInt8ToGlobal            g[0x00] imm=0x02" ) != WRstr::npos );
	assert( listing.find( "LiteralInt8ToLocal             l[0x00] imm=0x02" ) != WRstr::npos );

	WRState* w = wr_newState();
	WRContext* context = wr_run( w, out, outLen, true );
	if ( !context )
	{
		assert(0);
		wr_destroyState( w );
		return;
	}

	WRValue* ret = wr_callFunction( context, "f" );
	if ( !ret || ret->asInt() != 4 )
	{
		assert(0);
		wr_destroyState( w );
		return;
	}

	WRValue arg;
	wr_makeString( context, &arg, "tail", 4 );
	ret = wr_callFunction( context, "s", &arg, 1 );
	char tailBuf[32];
	if ( !ret || WRstr(ret->asString(tailBuf, sizeof(tailBuf))) != "headtail" )
	{
		assert(0);
		wr_destroyState( w );
		return;
	}

	arg.init( 100 );
	ret = wr_callFunction( context, "compareNumber", &arg, 1 );
	if ( !ret || ret->asInt() != 0 )
	{
		assert(0);
		wr_destroyState( w );
		return;
	}

	wr_makeString( context, &arg, "occupied", 8 );
	ret = wr_callFunction( context, "compareEmpty", &arg, 1 );
	if ( !ret || ret->asInt() != 1 )
	{
		assert(0);
		wr_destroyState( w );
		return;
	}

	arg.init( 0 );
	ret = wr_callFunction( context, "multiply", &arg, 1 );
	if ( !ret || ret->asInt() != 20 )
	{
		assert(0);
		wr_destroyState( w );
		return;
	}

	arg.init( 128 );
	ret = wr_callFunction( context, "branch", &arg, 1 );
	if ( !ret || ret->asInt() != 254 )
	{
		assert(0);
		wr_destroyState( w );
		return;
	}

	wr_destroyState( w );
}

//------------------------------------------------------------------------------
void testDeepHashTableWithWrenchValue()
{
	WRState* w = wr_newState();

	const char* src = "var root;\n"
					  "function readDeep() { return root.a.b.c.d; }\n";

	unsigned char* out = 0;
	int outLen = 0;
	if ( wr_compile(src, (int)strlen(src), &out, &outLen) != WR_ERR_None )
	{
		assert(0);
		wr_destroyState( w );
		return;
	}

	WRContext* c = wr_run( w, out, outLen, true );
	if ( !c || wr_getLastError( w ) != WR_ERR_None )
	{
		assert(0);
		wr_destroyState( w );
		return;
	}

	// Build root["a"]["b"]["c"]["d"] from C++ side using WrenchValue helper.
	WrenchValue root( c, "root" );
	assert( root.isValid() );
	root.addHashTableValue( "a" );

	WRValue* a = root.getHashTableValue( "a" );
	assert( a );
	WrenchValue aRef( c, a );
	aRef.addHashTableValue( "b" );

	WRValue* b = aRef.getHashTableValue( "b" );
	assert( b );
	WrenchValue bRef( c, b );
	WRValue* c3 = bRef.addHashTableValue( "c" );

	assert( c3 );
	WrenchValue cRef( c, c3 );
	cRef.addHashTableValue( "d" );

	WRValue* d = cRef.getHashTableValue( "d" );
	assert( d );
	WrenchValue dRef( c, d );
	*dRef.Int() = 4242;

	WRValue* r = wr_callFunction( c, "readDeep" );
	if ( !r || r->asInt() != 4242 || wr_getLastError( w ) != WR_ERR_None )
	{
		assert(0);
		wr_destroyState( w );
		return;
	}

	wr_destroyState( w );
}

//------------------------------------------------------------------------------
void testStateContextOpaquePointer()
{
	WRState* w = wr_newState( 64 );
	if ( !w )
	{
		assert(0);
		return;
	}

	wr_registerFunction( w, "ctxReadMagic", ctxReadMagic );
	wr_registerFunction( w, "ctxAccumulate", ctxAccumulate );

	const char* mainScript = "function readMagic() { return ctxReadMagic(); }\n"
							 "function addToState(v) { return ctxAccumulate(v); }\n";

	const char* importScript = "function importedReadMagic() { return ctxReadMagic(); }\n";

	unsigned char* mainOut = 0;
	unsigned char* importOut = 0;
	int mainLen = 0;
	int importLen = 0;
	if ( wr_compile(mainScript, (int)strlen(mainScript), &mainOut, &mainLen) != WR_ERR_None )
	{
		assert(0);
		wr_destroyState( w );
		return;
	}
	
	if ( wr_compile(importScript, (int)strlen(importScript), &importOut, &importLen) != WR_ERR_None )
	{
		assert(0);
		wr_free( mainOut );
		wr_destroyState( w );
		return;
	}

	OpaqueStateContext first = { 111, 0 };
	wr_setStateContext( w, &first );

	WRContext* mainCtx = wr_run( w, mainOut, mainLen, true );
	if ( !mainCtx )
	{
		assert(0);
		wr_free( importOut );
		wr_destroyState( w );
		return;
	}

	WRValue* r = wr_callFunction( mainCtx, "readMagic" );
	if ( !r || r->asInt() != 111 )
	{
		assert(0);
		wr_free( importOut );
		wr_destroyState( w );
		return;
	}

	WRValue addArg;
	wr_makeInt( &addArg, 7 );
	WRValue* addRes = wr_callFunction( mainCtx, "addToState", &addArg, 1 );
	if ( !addRes || addRes->asInt() != 7 || first.writes != 7 )
	{
		assert(0);
		wr_free( importOut );
		wr_destroyState( w );
		return;
	}

	first.magic = 222;
	r = wr_callFunction( mainCtx, "readMagic" );
	if ( !r || r->asInt() != 222 )
	{
		assert(0);
		wr_free( importOut );
		wr_destroyState( w );
		return;
	}

	OpaqueStateContext second = { 9001, 10 };
	wr_setStateContext( w, &second );

	r = wr_callFunction( mainCtx, "readMagic" );
	if ( !r || r->asInt() != 9001 )
	{
		assert(0);
		wr_free( importOut );
		wr_destroyState( w );
		return;
	}

	wr_makeInt( &addArg, 5 );
	addRes = wr_callFunction( mainCtx, "addToState", &addArg, 1 );
	if ( !addRes || addRes->asInt() != 15 || second.writes != 15 || first.writes != 7 )
	{
		assert(0);
		wr_free( importOut );
		wr_destroyState( w );
		return;
	}

	WRContext* importCtx = wr_import( mainCtx, importOut, importLen, true );
	if ( !importCtx )
	{
		assert(0); // the import took the block, even though it failed
		wr_destroyState( w );
		return;
	}
	WRValue* ir = wr_callFunction( importCtx, "importedReadMagic" );
	if ( !ir || ir->asInt() != 9001 )
	{
		assert(0);
		wr_destroyState( w );
		return;
	}

	wr_destroyState( w );
}
#endif


//------------------------------------------------------------------------------
int runTests( const char* testDir, int number )
{
	int err = 0;

	// keep the log intact up to the last line if a test crashes
	setvbuf( stdout, 0, _IONBF, 0 );

#ifndef WRENCH_WITHOUT_COMPILER
	g_testDir = testDir;
	WRstr listName;
	listName.format( "%s/test_files.txt", testDir );
	FILE* tfile = fopen( listName.c_str(), "r" );
	if ( !tfile )
	{
		printf( "cannot open [%s]: run from the repository root, or name the test folder: wrench t <testdir>\n", listName.c_str() );
		return -1;
	}

	char previousDir[4096];
	if ( !cliGetDir(previousDir, sizeof(previousDir)) || !cliSetDir(testDir) )
	{
		printf( "cannot change into the test folder [%s]\n", testDir );
		fclose( tfile );
		return -1;
	}

	WRstr code;
	WRstr codeName;

	WRValue container;
	WRValue integer;
	char someArray[10];
	char byte;
	char byte2;
	char* someBigArray = (char *)wr_malloc( 0x1FFFFF );

	// tests rewrite members of the container, so every run gets a fresh one
	auto buildContainer = [&]( bool destroyOld )
	{
		if ( destroyOld )
		{
			wr_destroyContainer( &container );
		}
		wr_makeContainer( &container );

		wr_addFloatToContainer( &container, "_f", 20.02f );

		wr_addIntToContainer( &container, "_i", 1001 );
		wr_addIntToContainer( &container, "_j", 1001 );

		wr_makeInt( &integer, 0 );
		wr_addValueToContainer( &container, "integer", &integer );

		strcpy( someArray, "hello" );
		wr_addArrayToContainer( &container, "name", someArray, 10 );

		someBigArray[0] = 10;
		someBigArray[0x99] = (char)0x88;
		someBigArray[10000] = 20;
		someBigArray[100000] = 30;
		someBigArray[0x1FFFFE] = 40;

		byte = (char)0x99;
		byte2 = (char)0x99;
		wr_addArrayToContainer( &container, "b", &byte, 1 );
		wr_addArrayToContainer( &container, "c", &byte2, 1 );

		wr_addArrayToContainer( &container, "big", someBigArray, 0x1FFFFF );
	};
	buildContainer( false );

	WRState* w = wr_newState( 128 );

	wr_loadAllLibs( w );
	wr_setAllocatedMemoryGCHint( w, 0 );

	// library constants for enum interaction tests (012_enums.c)
	wr_registerLibraryConstant( w, "TestLC::LIBVAL",  (int32_t)100 ); // never overridden by enum
	wr_registerLibraryConstant( w, "TestLC::BOTH",    (int32_t)200 ); // shadowed by enum after its declaration
	wr_registerLibraryConstant( w, "TestMix::LIBONLY",(int32_t)77  ); // lib-only member in a mixed namespace

	char buf[256];
	int fileNumber = 0;
	int failures = 0;
	WRstr errMsg;

#ifdef WRENCH_INCLUDE_DEBUG_CODE
	const int testPasses = 2;
	WrenchDebugNullTransport nullLink;
	WrenchDebugServer idleDebugger( w, &nullLink );
#else
	const int testPasses = 1;
#endif

	unsigned char* out;
	int outLen;
	while( fgets(buf, 255, tfile) && (err==0) )
	{
		codeName = buf;
		codeName.trim();
		if ( !codeName.size() || codeName.c_str()[0] == '#' )
		{
			continue; // blank line or comment
		}

		++fileNumber; // entries count from 1, "wrench t <n>" runs the n-th
		if ( !number || (number == fileNumber) )
		{
			WRstr expect;

			if ( code.fileToBuffer(codeName) )
			{
				unsigned int t = 0;

				for( ; (t < code.size()) && (code[t] != '~'); ++t );

				t += 2;

				for( ; t<code.size() && (code[t] != '~'); ++t )
				{
					expect += code[t];
				}

				// pass 1 repeats the test compiled with debug code under an
				// attached (idle) debug server, which must not change results
				for( int pass = 0; pass < testPasses && err == 0; ++pass )
				{
					printf( "test [%d][%s]%s: ", fileNumber, codeName.c_str(), pass ? "[debug]" : "" );
					buildContainer( true );

					uint8_t compileFlags = WR_INCLUDE_GLOBALS;
					if ( pass )
					{
						compileFlags |= WR_EMBED_DEBUG_CODE | WR_EMBED_SOURCE_CODE;
					}
					// scripts may include files next to them
					WrenchIncludeResolver includes;
					std::string scriptName = WrenchIncludeResolver::normalize( codeName.c_str() );
					includes.setMain( scriptName );
					WRCompileOptions options;
					options.flags = compileFlags;
					options.name = scriptName.c_str();
					options.include = WrenchIncludeResolver::callback;
					options.includeUser = &includes;
					err = wr_compileWithOptions( code, code.size(), &out, &outLen, options, &errMsg );
					
					if ( err )
					{
	#ifdef WRENCH_WITHOUT_COMPILER
						printf( "compile error [#%d]\n", err );
	#else
						printf( "%scompile error [%s]\n", errMsg.c_str(), c_errStrings[err] );
	#endif
						return -1;
					}
	
					WRstr logger;
					wr_registerFunction( w, "print", emit, &logger );
					wr_registerFunction( w, "println", emitln, &logger );
					
					wr_registerFunction( w, "checkIsWrenchArray", checkIsWrenchArray );
					wr_registerFunction( w, "checkIsRawArray", checkIsRawArray );
					wr_registerFunction( w, "checkIsString", checkIsString );
					wr_registerFunction( w, "checkIsHashTable", checkIsHashTable );
					wr_registerFunction( w, "checkIter", checkIter );
					wr_registerFunction( w, "checkStruct", checkStruct );
	
					WRContext* context = 0;
					wr_destroyContext( context ); // make sure this doesn't crash
#ifdef WRENCH_INCLUDE_DEBUG_CODE
					// same as wr_run(), with a debugger attached on pass 1
					context = wr_newContext( w, out, outLen );
					if ( context )
					{
						if ( pass )
						{
							idleDebugger.attach( context );
						}
						if ( !wr_executeContext(context) && !context->yield_pc )
						{
							idleDebugger.detach();
							wr_destroyContext( context );
							context = 0;
						}
					}
#else
					context = wr_run( w, out, outLen );
#endif
	
					err = wr_getLastError( w );
	
					int args;
					WRValue* firstArg;
					WRValue* returnValue;
					while( wr_getYieldInfo(context, &firstArg, &args, &returnValue) )
					{
						if ( args > 0 )
						{
							*returnValue = *firstArg;
						}
						wr_continue( context );
					}
	
					if ( context && !(err = wr_getLastError(w)) )
					{
						integer.i = 2456;
						someArray[1] = 'e';
	
						WRValue* V = wr_callFunction( context, "userCheck", &container, 1 );
						if ( V && V->i == 77 )
						{
							assert( integer.i == 56789 );
							assert( someArray[1] == 'c' );
						}
	
						char testString[12] = "test string";
						WRValue val;
						wr_makeString( context, &val, testString, 11 );
	
						assert( val.isString() );
						
						wr_callFunction( context, "stringCheck", &val, 1 );
						wr_callFunction( context, "stringCheck", &val, 1 );
						wr_callFunction( context, "stringCheck", &val, 1 );
						wr_callFunction( context, "stringCheck", &val, 1 );
	
						V = wr_callFunction( context, "arrayCheck" );
						if ( V )
						{
							V->isWrenchArray();
							assert( V->isWrenchArray() );
							char someString[256] = { 0 };
							someString[0] = someString[0]; // kill warning
							assert( WRstr(V->indexArray(context, 0, false)->asString(someString)) == "some string" );
							assert( V->indexArray(context, 1, false)->asFloat() == 0.0f );
							assert( V->indexArray(context, 2, false)->asInt() == 0);
							assert( !V->indexArray(context, 3, false)  );
							assert( V->indexArray(context, 3, true)->asInt() == 0  );
	
							assert( wr_getValueFromContainer(container, "_i")->asInt() == 1001 );
						}
	
						WRValue s;
						wr_makeString( context, &s, "test string" );
						wr_callFunction( context, "stringCheck", &s, 1 );
					}
	
					if ( err )
					{
						printf( "execute error [%s]\n", c_errStrings[err] );
					}
					else if ( logger != expect )
					{
						printf( "error: expected\n"
								"-----------------------------\n"
								"%s\n"
								"saw:-------------------------\n"
								"%s"
								"\n"
								"-----------------------------\n",
								expect.c_str(),
								logger.c_str() );
						++failures;
					}
					else
					{
						printf( "PASS\n" );
					}

#ifdef WRENCH_INCLUDE_DEBUG_CODE
					idleDebugger.detach();
#endif
					wr_free( out );
					wr_destroyContext( context );
				}
			}
			else
			{
				printf( "test [%d][%s]: FILE NOT FOUND\n", fileNumber, codeName.c_str() );
				++failures;
			}
		}
	}

	cliSetDir( previousDir );

#ifdef WIN32_C17
	TESTmain();
#endif

	testD();
	testCallFunctionHashLibraryFallback();
	testFunctionNameHashAsArgument();
	testNamedEnumUnqualifiedError();
	testBlankVariablesCannotBeInitialized();
	testFunctionNotFoundErrors();
	testLibFunctionNotFoundErrors();
	testOnCallbackNotFound();
	testOnLibCallbackNotFound();
	if ( !testNotFoundHandlerCanFail() )
	{
		++failures;
	}
	if ( !testCompilerLimits() )
	{
		++failures;
	}
	if ( !testLongJumps() )
	{
		++failures;
	}
	if ( !testVmRobustness() )
	{
		++failures;
	}
	if ( !testIncludes() )
	{
		++failures;
	}
	testDropClobberedInitializerStores();
	testDeepHashTableWithWrenchValue();
	testStateContextOpaquePointer();
	testCompilerArrayCopiesAndRemoval();
#ifdef WRENCH_ENABLE_CROSS_MODULE_EXTERNAL_TEST
	printf( "test [x][src/utils/test_wrench_cross_module_globals.cpp]: " );
	// the log goes next to the test scripts, like their scratch files
	WRstr crossModuleLog;
	crossModuleLog.format( "%s/_cross_module_test.log", testDir );
	WRstr crossModuleCommand;
#ifdef _WIN32
	crossModuleCommand.format( ".\\test_wrench_cross_module_globals.exe > \"%s\" 2>&1", crossModuleLog.c_str() );
#else
	crossModuleCommand.format( "./test_wrench_cross_module_globals > \"%s\" 2>&1", crossModuleLog.c_str() );
#endif
	int crossModuleResult = system( crossModuleCommand.c_str() );
	if ( crossModuleResult != 0 )
	{
		printf( "FAIL (exit=%d, see %s)\n", crossModuleResult, crossModuleLog.c_str() );
		err = err ? err : -1;
	}
	else
	{
		printf( "PASS\n" );
	}
#endif
	testTimeSlices();
	testImport();
	testHalt();
	testScheduler();
	testStackOverflow();
	testYield2();
	testStructs();
	setup();

	testGlobalValues( w );
	testHostSeededGlobalValue( w );
	testBlankHostSeededGlobals( w );

	wr_destroyState( w );

	wr_free( someBigArray );
	wr_destroyContainer( &container );

#ifdef WRENCH_HANDLE_MALLOC_FAIL
	testArrayMallocFailureHandling();
#endif

#ifdef WRENCH_INCLUDE_DEBUG_CLIENT
	if ( !number && !testDebugger() )
	{
		++failures;
	}
#endif

	fclose( tfile );

	if ( failures )
	{
		printf( "\n%d test(s) FAILED\n", failures );
	}
#endif
	return err ? err : (failures ? -1 : 0);
}

//------------------------------------------------------------------------------
void testGlobalValues( WRState* w )
{
	char globalTestCode[] = "var global_one = 10;\n"
							"var global_two = 20;\n"
							"var global_three = 30;\n"
							"var global_four = 40;\n"
							"var N = 50;\n"
							"var AB = 60;\n"
							
							"function test1() { global_two = 25; }\n"
							"function test2() { return global_two == 35; }\n"
							"function test3() { global_three = 5.3; }\n"
							"function test4() { return global_three == 45; }\n"
							"function test5() { return global_three == 4.5; }\n"
							
							"function test6() { return global_four[7] == 8; }\n"
							"function test7() { return global_four[17] == 18; }\n"
							"function test8() { return global_four[17] == 19.3; }\n"
							"function test9() { return global_four[2] = 200; }\n"
							"function test10() { return global_four[32] = 3200; }\n";

	unsigned char* out;
	int outLen;
	WRstr errMsg;
	int err = wr_compile( globalTestCode, (int)strlen(globalTestCode), &out, &outLen, &errMsg, WR_INCLUDE_GLOBALS|WR_NON_STRICT_VAR );
	if ( err )
	{
		assert(0);
		printf("used");
	}

	WRContext* gc = wr_run( w, out, outLen );

	WRValue* g = wr_getGlobalRef( gc, "does_not_exist" );
	if ( g )
	{
		assert(0);
		printf("used");
	}

	g = wr_getGlobalRef( gc, "N" );
	assert( g && g->i == 50 );
	g = wr_getGlobalRef( gc, "::N" );
	assert( g && g->i == 50 );
	g = wr_getGlobalRef( gc, "AB" );
	assert( g && g->i == 60 );
	g = wr_getGlobalRef( gc, "::AB" );
	assert( g && g->i == 60 );

	g = wr_getGlobalRef( gc, "global_one" );
	assert( g && g->i == 10 );
	g = wr_getGlobalRef( gc, "::global_two" );
	assert( g && g->i == 20 );
	g = wr_getGlobalRef( gc, "global_three" );
	assert( g && g->i == 30 );
	g = wr_getGlobalRef( gc, "::global_four" );
	assert( g && g->i == 40 );

	WrenchValue wv1( gc, "global_one" );
	assert( wv1.isValid()
			&& *wv1.Int() == 10
			&& *(int *)wv1 == 10 );

	WrenchValue wv2( gc, "global_two" );
	wr_callFunction( gc, "test1" );
	assert( *wv2.Int() == 25 );

	*wv2.Int() = 35;
	assert( wr_callFunction(gc, "test2")->i );

	WrenchValue wv3( gc, "global_three" );
	assert( *(int *)wv3 == 30 );
	wr_callFunction(gc, "test3");
	assert( *(float *)wv3 == 5.3f );

	*(int *)wv3 = 45;
	assert( wr_callFunction(gc, "test4")->i );
	*(float *)wv3 = 4.5f;
	assert( wr_callFunction(gc, "test5")->i );

	*(float *)wv3 = 4.6f;
	assert( wr_callFunction(gc, "test5")->i == 0 );

	WrenchValue wv4( gc, "global_four" );
	
	assert( *(int *)wv4 == 40 );

	wr_makeInt( wv4.asArrayMember(7), 8 );
	assert( wr_callFunction(gc, "test6")->i );

	wr_makeInt( wv4.asArrayMember(17), 18 );
	assert( wr_callFunction(gc, "test7")->i );

	wv4.asArrayMember(17)->setFloat( 19.3f );
	assert( wr_callFunction(gc, "test8")->i );

	wv4.asArrayMember(2)->setInt( 200 );
	assert( wr_callFunction(gc, "test9") );

	wv4.asArrayMember(32)->setInt( 3200 );
	assert( wr_callFunction(gc, "test10") );

	wr_free( out );
}

#ifndef WRENCH_WITHOUT_COMPILER
//------------------------------------------------------------------------------
void testHostSeededGlobalValue( WRState* w )
{
	// read outside assert(): release builds drop the whole expression
	WRstr code;
	WRstr path;
	path.format( "%s/024_numbers.w", g_testDir.c_str() );
	if ( !code.fileToBuffer(path) )
	{
		printf( "test [-][host seeded global]: cannot read [%s]\n", path.c_str() );
		assert(0);
		return;
	}

	unsigned char* out = 0;
	int outLen = 0;
	WRstr errMsg;
	int err = wr_compile( code, code.size(), &out, &outLen, &errMsg, WR_INCLUDE_GLOBALS );
	if ( err )
	{
		assert(0);
		return;
	}

	WRstr logger;
	wr_registerFunction( w, "print", emit, &logger );
	wr_registerFunction( w, "println", emitln, &logger );

	WRContext* context = wr_newContext( w, out, outLen, true );
	if ( !context )
	{
		assert(0);
		return;
	}

	WRValue* n = wr_getGlobalRef( context, "N" );
	if ( !n )
	{
		assert(0);
		wr_destroyContext( context );
		return;
	}
	wr_makeInt( n, 123456789 );

	WRValue* result = wr_executeContext( context );
	if ( !result || wr_getLastError( w ) != 0 || logger.size() != 0 )
	{
		assert(0);
		wr_destroyContext( context );
		return;
	}

	result = wr_callFunction( context, "hostSeededGlobalN" );
	if ( !result || result->asInt() != 123456789 )
	{
		assert(0);
		wr_destroyContext( context );
		return;
	}

	wr_destroyContext( context );
}

//------------------------------------------------------------------------------
void testBlankHostSeededGlobals( WRState* w )
{
	const char* code =
		"blank A;\n"
		"blank var B;\n"
		"var blank C;\n"
		"checkBlankSeededGlobals(A, B, C);\n";

	unsigned char* out = 0;
	int outLen = 0;
	WRstr errMsg;
	int err = wr_compile( code, (int)strlen(code), &out, &outLen, &errMsg, WR_INCLUDE_GLOBALS );
	if ( err )
	{
		assert(0);
		return;
	}

	g_blankSeedCallbackSeen = 0;
	g_blankSeedCallbackOk = 0;
	wr_registerFunction( w, "checkBlankSeededGlobals", checkBlankSeededGlobals );

	WRContext* context = wr_newContext( w, out, outLen, true );
	if ( !context )
	{
		assert(0);
		return;
	}

	WRValue* a = wr_getGlobalRef( context, "A" );
	WRValue* b = wr_getGlobalRef( context, "B" );
	WRValue* c = wr_getGlobalRef( context, "C" );
	if ( !a || !b || !c )
	{
		assert(0);
		wr_destroyContext( context );
		return;
	}

	wr_makeInt( a, 101 );
	wr_makeInt( b, 202 );
	wr_makeInt( c, 303 );

	WRValue* result = wr_executeContext( context );
	if ( !result || wr_getLastError( w ) != 0 || !g_blankSeedCallbackSeen || !g_blankSeedCallbackOk )
	{
		assert(0);
		wr_destroyContext( context );
		return;
	}

	wr_destroyContext( context );
}
#endif

//------------------------------------------------------------------------------
void testStackOverflow()
{
#ifdef WRENCH_PROTECT_STACK_FROM_OVERFLOW
	const char* recurse = "function oops() { var a = 100; oops(); }\n"
						  "oops();\n";
	
	WRState* w = wr_newState( 10 );
	
	uint8_t* out;
	int outLen;
	wr_compile( recurse, strlen(recurse), &out, &outLen );

	assert( wr_run(w, out, outLen, true) == 0 );
	assert( w->err == WR_ERR_stack_overflow );

	wr_destroyState( w );
#endif	
}

//------------------------------------------------------------------------------
void testScheduler()
{
#ifdef WRENCH_TIME_SLICES
	const char* loop1 = "for(;;) { println(\"1\"); }";
	const char* loop2 = "for(;;) { println(\"2\"); }";
	const char* loop3 = "println(\"once\");";
	const char* loop4 = "for(;;) { println(\"4\"); }";
	const char* loop5 = "for(;;) { println(\"5\"); }";

	WrenchScheduler scheduler( 8 );

	WRstr logger;
	wr_registerFunction( scheduler.state(), "println", emitln, &logger );

	uint8_t* out;
	int outLen;

	wr_compile( loop1, strlen(loop1), &out, &outLen );
	scheduler.addThread( out, outLen, 10, true );
	scheduler.tick(10);

	wr_compile( loop2, strlen(loop2), &out, &outLen );
	scheduler.addThread( out, outLen, 10, true );
	scheduler.tick(10);

	wr_compile( loop3, strlen(loop3), &out, &outLen );
	scheduler.addThread( out, outLen, 10, true );
	scheduler.tick(10);

	wr_compile( loop4, strlen(loop4), &out, &outLen );
	scheduler.addThread( out, outLen, 10, true );
	scheduler.tick(10);

	wr_compile( loop5, strlen(loop5), &out, &outLen );
	scheduler.addThread( out, outLen, 10, true );
	scheduler.tick(10);

	// Stress many short-lived yielded tasks finishing in a single tick.
	const char* shortYield = "println(\"start\"); yield(0); println(\"end\");";
	wr_compile( shortYield, strlen(shortYield), &out, &outLen );
	int ids[64];
	for( int i=0; i<64; ++i )
	{
		ids[i] = scheduler.addThread( out, outLen, 1000, false );
		assert( ids[i] > 0 );
	}
	assert( countStringHits( logger, "start\n" ) >= 64 );

	scheduler.tick(1000);
	assert( countStringHits( logger, "end\n" ) >= 64 );
	for( int i=0; i<64; ++i )
	{
		assert( !scheduler.removeTask(ids[i]) );
	}
	wr_free( out );

	// Validate removeTask behavior on active yielded tasks.
	const char* foreverYield = "for(;;) { yield(0); }";
	wr_compile( foreverYield, strlen(foreverYield), &out, &outLen );
	int idA = scheduler.addThread( out, outLen, 1000, false );
	int idB = scheduler.addThread( out, outLen, 1000, false );
	assert( idA > 0 && idB > 0 );

	assert( scheduler.removeTask(idA) );
	assert( !scheduler.removeTask(idA) );
	assert( scheduler.removeTask(idB) );
	assert( !scheduler.removeTask(idB) );
	assert( !scheduler.removeTask(0x12345678) );
	wr_free( out );

#endif
}

//------------------------------------------------------------------------------
void testTimeSlices()
{
#ifdef WRENCH_TIME_SLICES
	const char* loop = "for(;;) {}";

	WRState* w = wr_newState( 16 );

	wr_setInstructionsPerSlice( w, 10 );

	unsigned char* out;
	int outlen;
	wr_compile( loop, strlen(loop), &out, &outlen );
	WRContext* context = wr_run( w, out, outlen, true );
	for( int y=0; y<10; ++y )
	{
		assert( !wr_continue(context) );
		assert( wr_getYieldInfo(context) );
	}
	
	wr_destroyState( w );
#endif
}

//------------------------------------------------------------------------------
void testHalt()
{
	const char* haltMe = "sys::halt( 101 );";
	const char* haltMe2 = "sys::halt( 1 );";

	WRState* w = wr_newState( 16 );
	wr_loadAllLibs(w);

	unsigned char* out;
	int outlen;
	wr_compile( haltMe, strlen(haltMe), &out, &outlen );
	wr_run( w, out, outlen, true );
	assert( w->err == 101 );

	wr_compile( haltMe2, strlen(haltMe2), &out, &outlen );
	wr_run( w, out, outlen, true );
	assert( w->err == WR_ERR_USER_err_out_of_range );

	wr_destroyState( w );
}

//------------------------------------------------------------------------------
WRstr g_importBuf;
static void importln( WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr )
{
	for( int i=0; i<argn; ++i )
	{
		unsigned int len = 0;
		char* str = argv[i].asMallocString( &len );
		g_importBuf += str;
		wr_free( str );
	}
	g_importBuf += "\n";
}

void set_( WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr ) {}


//------------------------------------------------------------------------------
void testD()
{
	const char d[] = "function tick( elapsed ){ f(); } function f( state ) { state.a = 0; state.b = 0; state.c = 0; }\n";

	WRState* w = wr_newState();
	wr_registerFunction( w, "print", print );
	wr_registerFunction( w, "rgb", set_ );
	wr_registerFunction( w, "mode", set_ );
	wr_registerFunction( w, "flame_rgb", set_ );
	wr_registerFunction( w, "flame_speed", set_ );
	wr_registerFunction( w, "flame_brightness", set_ );
	wr_registerLibraryConstant( w, "rgb::count", (int32_t)5 );
	wr_Seed = 187280232;
	wr_loadStdLib( w );



	unsigned char* outBytes = 0;
	int outLen;
	WRContext* context = 0;
	WRFunction* function = 0;


	WRstr errorMessage;
	int err = wr_compile( d, strlen(d), &outBytes, &outLen, &errorMessage );
	if ( err )
	{
		printf( "%d:\n%s\n", err, errorMessage.c_str() );
	}
	else
	{
			context = wr_run( w, outBytes, outLen ); // load and run the code!
		if ( !context )
		{
			printf( "null context\n" );
		}
			else
			{
				function = wr_getFunction( context, "tick" );
				if ( !function )
				{
					assert(0);
				}
			}
		}


	if ( function )
	{
		for( int i=0; i<10000; ++i )
		{
			WRValue v( 2 );
			wr_callFunction( context, function, &v, 1 );
		}
	}

	if ( outBytes )
	{
		wr_free( outBytes );
	}
	wr_destroyState( w );
}

const char exampleYield[]=
" // Example Wrench Script\n"
" //   All scripts will 'override' a `run` function	\n"
"													\n"
"function run( values_hashtable )					\n"
"{													\n"
"   for(;;)											\n"
"   {												\n"
"      values_hashtable[\"random_number\"] += 1;	\n"
"   }												\n"
" }													\n";

#include <time.h>
//------------------------------------------------------------------------------
void testYield2()
{
#ifdef WRENCH_TIME_SLICES
	WRState* state = wr_newState(); // create a state (only need to do this once)

	// make sure we only execute 10 instructions at any time
	wr_setInstructionsPerSlice( state, 10 );

	// compile a script
	unsigned char* out;
	int outLen;
	int err = wr_compile( exampleYield, strlen(exampleYield), &out, &outLen );
	if ( err )
	{
		// oops! something wrong with the code:
		printf( "err compiling [%s]\n", c_errStrings[err] );
		wr_destroyState( state );
		return;
	}

	// now that the script is compiled into bytecode (stored in
	// out/outLen) we need to "run" it, this loads it into a context.
	// for the case of your example, it won't do anything but load the
	// function signature and return, since nothing actually happens in
	// the code in global scope.
	
	WRContext* context = wr_run( state, out, outLen );
	if ( !context )
	{
		// must have been an error processing the script?
		printf( "err compiling [%s]\n", c_errStrings[(int)wr_getLastError(state)] );
		wr_destroyState( state );
		return;
	}

	time_t future = time(0) + 2;

	while( time(0) < future ) // run for 2 seconds
	{
		// the VM will automatically continue on yield, this can be
		// called multiple times.. need to make this clear in the docs..
		WRValue* retval = wr_callFunction( context, "run" ); 
		
		if ( retval )
		{
			// the function returned, so it completed, do something
			// with it...

			// and we're out
			break;
		}
		
		if ( !wr_getYieldInfo(context) )
		{
			// something went wrong! this is not yielded
			printf( "non-yield [%s]\n", c_errStrings[(int)wr_getLastError(state)] );
			break;
		}
		else
		{
//			printf( "function forced to yield\n" );
		}
	}

	wr_free( out );
	wr_destroyState( state );
#endif
}


const char* importMe = "export function imported(a) \n"
					   "{                    \n"
					   "    return a + 0xAE;    \n"
					   "}                    \n"
					   "export struct S1(n) { var a = 10; var b = 15; var c=n; };\n"
					   "\n"
					   "struct S0 { var c = 5; };\n";


const char* baseMe = "yield();\n"
					 "sys::importCompile( \"export struct S2 { var b = 20; };\" );\n"
					 "println( imported(1) );\n"
					 "imported(1);\n"
					 "var s1 = new S1;\n"
					 "println( s1.a );\n"
					 "var s2 = new S2;\n"
					 "println( s2.b );\n"
					 "var s0 = new S1;\n"
					 "s0 = new S1[]{};\n"
					 "s0 = new S1[]{{}};\n"
					 "s0 = new S1(99);\n"
					 "println(s0.d);\n"
					 "s0 = new S1{c = 201};\n"
					 "println(s0.a);\n"
					 "println(s0.b);\n"
					 "println(s0.c);\n"
					 "s0 = new S1{55,66};\n"
					 "println(s0.a);\n"
					 "println(s0.b);\n"
					 "println(s0.c);\n"
					 "var s0 = new S0;\n"
					 ;

//------------------------------------------------------------------------------
void testImport()
{
	WRState* w = wr_newState( 128 );
	wr_loadAllLibs(w);

	g_importBuf.clear();
	wr_registerFunction( w, "println", importln );

	unsigned char* out1;
	int out1len;
	unsigned char* out2;
	int out2len;

//	WRstr base;
//	base.fileToBuffer( "test.c" );
//	wr_compile( base, base.size(), &out1, &out1len );

//	wr_compile( "export struct S2 { var b = 20; };", strlen("export struct S2 { var b = 20; };"), &out1, &out1len );
//	wr_free( out1 );
	
	wr_compile( baseMe, strlen(baseMe), &out1, &out1len );
	wr_compile( importMe, strlen(importMe), &out2, &out2len );

	WRContext* context = wr_run(w, out1, out1len, true);
	if ( context )
	{
		wr_import( context, out2, out2len, true );

		wr_continue( context );
	}

	assert( g_importBuf ==
			"175\n"
			"10\n"
			"20\n"
			"0\n"
			"10\n"
			"15\n"
			"201\n"
			"55\n"
			"66\n"
			"0\n" );
	
	wr_destroyState( w );
}


// COMPLETE EXAMPLE MUST WORK

#include "wrench.h"

const int Pbasic_bytecodeSize=54;
const unsigned char Pbasic_bytecode[]=
{
	0x01, 0x00, 0x00, 0x04, 0x0D, 0x00, 0x48, 0x65, 0x6C, 0x6C, 0x6F, 0x20, 0x57, 0x6F, 0x72, 0x6C, // 16
	0x64, 0x21, 0x0A, 0x06, 0x01, 0x88, 0x8A, 0x37, 0x16, 0xCF, 0x00, 0x00, 0xAF, 0x0A, 0x70, 0x00, // 32
	0x0F, 0x09, 0x20, 0x00, 0x06, 0x01, 0x88, 0x8A, 0x37, 0x16, 0x90, 0x00, 0x37, 0xEF, 0x09, 0x02, // 48
	0xEF, 0x14, 0x95, 0xF8, 0x8F, 0x5A, // 54
};

void logBlank( WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr ) { }


void log2( WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr )
{
	char buf[512];
	for( int i=0; i<argn; ++i )
	{
		printf( "%s", argv[i].asString(buf, 512) );
	}
}

const char* wrenchCode = "print( \"Hello World!\\n\" ); "
						 "for( var i=0; i<10; i++ )     "
						 "{                             "
						 "    log( i );                 "
						 "}                             ";

void setup()
{
//	Serial.begin( 115200 );
//	delay( 2000 ); // wait for link to come up for sample

	WRState* w = wr_newState(); // create the state

	wr_registerFunction( w, "print", logBlank ); // bind a function

	unsigned char* outBytes; // compiled code is alloc'ed
	int outLen;

	int err = wr_compile( wrenchCode, (int)strlen(wrenchCode), &outBytes, &outLen );
	
	assert( err == 0 );
	
	if ( err == 0 )
	{
		wr_run( w, outBytes, outLen, true, true ); // load and run the code!
	}

	assert( wr_run(w, Pbasic_bytecode, Pbasic_bytecodeSize) );

	wr_destroyState( w );
}

void loop()
{}




inline void LEAKprint(WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr)
{
	char buf[1024];
	for (int i = 0; i < argn; ++i)
	{
		printf( "%s", argv[i].asString(buf, 1024) );
	}
}

const char* LEAKwrenchCode = "function leak() { print( \"Hello \" + \"World!\\n\" ); }";

void LEAKtest()
{
	WRState* w = wr_newState();

	wr_registerFunction( w, "print", LEAKprint );

	unsigned char* outBytes = 0;
	int outLen;

	wr_compile( LEAKwrenchCode, (int)strlen(LEAKwrenchCode), &outBytes, &outLen );

	WRContext* ctx = wr_run( w, outBytes, outLen, true );

	g_devMemoryAllocated = 0;
	
	for(int i=0;i<10;++i)
	{
		wr_callFunction( ctx, "leak" );
	}

	wr_destroyState( w );
}



#ifdef WIN32_C17

#include <iostream>
#include <map>
#include <string.h>
#include "wrench.h"

using namespace std;


int TESTmain()
{
	WRState* wr_state = wr_newState(20);
	wr_registerFunction(wr_state, "print", print);

	std::map<string, int> values;
	values["temperature"] = 80;
	values["humidity"] = 11;

	string code = "function wrenchfunction(values){"
				  "print(\"humidity:\");print( values[\"humidity\"] );"
				  "print(\"temperature:\");print( values[\"temperature\"] );"
				  "values[\"humidity\"] += 222;"
				  "print(\"humidity:\");print( values[\"humidity\"] );"
				  "print(\"temperature:\");print( values[\"temperature\"] );"
				  "return values;}";

	//Transform Values
	WRValue wrench_values;
	wr_makeContainer(&wrench_values);

	for (const auto& [id, value] : values) {
		WRValue* wr_value = new WRValue();
		wr_makeInt(wr_value, value);
		wr_addValueToContainer(&wrench_values, id.c_str(), wr_value);
	}

	//Compile and Run
	unsigned char* outBytes;
	int outLen;

	const char* wrenchCode = code.c_str();

	wr_compile(wrenchCode, strlen(wrenchCode), &outBytes, &outLen);
	WRContext* context = wr_run(wr_state, outBytes, outLen, true);

	wr_callFunction(context, "wrenchfunction", &wrench_values, 1);

	//Update Values and Free Memory
	for (WRIteratorEntry const& member : wrench_values)
	{
		char key[156];
		values[(member.key->asString(key, 156))] = member.value->asInt();
		delete member.value;
	}

	WRstr out;
	for (const auto& [key, value] : values) {
		out.appendFormat("%s%d", key.c_str(), value);
	}

	assert(out == "humidity233temperature80");
	wr_destroyContainer(&wrench_values);
	return 0;
}
#endif


//
// Created by T.Arnold on 19.12.2024.
//

#ifndef WRENCHWRAPPER_H
#define WRENCHWRAPPER_H
//#include <animations/Splash.h>

//#include "wrench/wrench.h"
//#include "ControlManager.h"

class ControlManager
{
public:
	void set_status(WRstr status) {}
	const WRstr& get_status() const { return status; }
	int get_controls() { return 0; }
	void set_controls( int c ) {}
	void stop_animation() {}
	bool is_animation_running() { return false; }
	void reset() {}

	WRstr status;

};

class MatrixManager
{
public:
	float get_current_tps() { return 0.0; }
	void set_tps( float f ) {}
	void set( int x, int y, int z ) {}
	void off( int x, int y ) {}
	void fill( int x ){}
	void clear() {}
	void line( int x, int y, int a, int b, int l ) {}
	void rect( int a, int b, int c, int d, int e, bool f = false  ) {}
	void circle( int a, int b, int c, int d ) {}
	void number( int a, int b, int c, int d ) {}
};

struct ControlElements
{
	ControlManager* cm;
	MatrixManager* mm;
};
namespace wrench_wrapper
{
	inline void print(WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr)
	{
		char buf[128];
		for( int i=0; i<argn; ++i )
		{
			printf( "%s\n", argv[i].asString(buf, 128) );
		}
	}

	inline void set_status(WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr)
	{
		if (argn == 0) return;
		ControlElements* ce = static_cast<ControlElements*>(usr);

		char buf[128];
		WRstr status = "";
		for (int i=0;i<argn;i++) {
			status += argv[i].asString(buf, 128);
		}
		ce->cm->set_status(status);

	}

	inline void get_status(WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr)
	{
		ControlElements* ce = static_cast<ControlElements*>(usr);
		wr_makeString(c, &retVal, ce->cm->get_status().c_str(), strlen(ce->cm->get_status().c_str()));
	}

	inline void get_current_tps(WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr)
	{
		ControlElements* ce = static_cast<ControlElements*>(usr);
		wr_makeFloat(&retVal, ce->mm->get_current_tps());
	}

	inline void set_tps(WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr)
	{
		if (argn != 1) return;
		ControlElements* ce = static_cast<ControlElements*>(usr);
		ce->mm->set_tps(argv[0].asFloat());
		wr_makeInt(&retVal, 1);
	}

	inline void get_controls(WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr)
	{
		ControlElements* ce = static_cast<ControlElements*>(usr);
		wr_makeInt(&retVal, ce->cm->get_controls());
	}

	inline void set_controls(WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr)
	{
		if (argn != 1) return;
		ControlElements* ce = static_cast<ControlElements*>(usr);
		ce->cm->set_controls(argv[0].asInt());
		wr_makeInt(&retVal, 1);
	}

	inline void reset_controls(WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr)
	{
		ControlElements* ce = static_cast<ControlElements*>(usr);
		ce->cm->reset();
		wr_makeInt(&retVal, 1);
	}

	inline void is_animation_running(WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr)
	{
		ControlElements* ce = static_cast<ControlElements*>(usr);
		wr_makeInt(&retVal, ce->cm->is_animation_running());

	}

	/*void run_animation(WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr) {
	if (argn != 2) return;
	ControlElements* ce = static_cast<ControlElements*>(usr);
	Animation* anim = new Animation(); // Assuming Animation has a default constructor
	ce->cm->run_animation(anim, argv[0].asInt(), argv[1].asInt());
	wr_makeInt(&retVal, 1);
	}
*/


	inline void stop_animation(WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr)
	{
		ControlElements* ce = static_cast<ControlElements*>(usr);
		ce->cm->stop_animation();
		wr_makeInt(&retVal, 1);
	}

	inline void set_pixel(WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr)
	{
		if (argn != 3) return;
		ControlElements* ce = static_cast<ControlElements*>(usr);
		ce->mm->set(argv[0].asInt(), argv[1].asInt(), argv[2].asInt());
		wr_makeInt(&retVal, 1);
	}

	inline void off_pixel(WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr)
	{
		if (argn != 2) return;
		ControlElements* ce = static_cast<ControlElements*>(usr);
		ce->mm->off(argv[0].asInt(), argv[1].asInt());
		wr_makeInt(&retVal, 1);
	}

	inline void fill_matrix(WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr)
	{
		if (argn != 1) return;
		ControlElements* ce = static_cast<ControlElements*>(usr);
		ce->mm->fill(argv[0].asInt());
		wr_makeInt(&retVal, 1);
	}

	inline void clear_matrix(WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr)
	{
		ControlElements* ce = static_cast<ControlElements*>(usr);
		ce->mm->clear();
		wr_makeInt(&retVal, 1);
	}

	inline void draw_line(WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr)
	{
		if (argn != 5) return;
		ControlElements* ce = static_cast<ControlElements*>(usr);
		ce->mm->line(argv[0].asInt(), argv[1].asInt(), argv[2].asInt(), argv[3].asInt(), argv[4].asInt());
		wr_makeInt(&retVal, 1);
	}

	inline void draw_rect_filled(WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr)
	{
		if (argn != 5) return;
		ControlElements* ce = static_cast<ControlElements*>(usr);
		ce->mm->rect(argv[0].asInt(), argv[1].asInt(), argv[2].asInt(), argv[3].asInt(), argv[4].asInt(),true); //make fillable controlable
		wr_makeInt(&retVal, 1);
	}

	inline void draw_rect(WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr)
	{
		if (argn != 5) return;
		ControlElements* ce = static_cast<ControlElements*>(usr);
		ce->mm->rect(argv[0].asInt(), argv[1].asInt(), argv[2].asInt(), argv[3].asInt(), argv[4].asInt()); //make fillable controlable
		wr_makeInt(&retVal, 1);
	}

	inline void draw_circle(WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr)
	{
		if (argn != 4) return;
		ControlElements* ce = static_cast<ControlElements*>(usr);
		ce->mm->circle(argv[0].asInt(), argv[1].asInt(), argv[2].asInt(), argv[3].asInt());
		wr_makeInt(&retVal, 1);
	}

	inline void draw_number(WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr)
	{
		if (argn != 4) return;
		ControlElements* ce = static_cast<ControlElements*>(usr);
		ce->mm->number(argv[0].asInt(), argv[1].asInt(), argv[2].asInt(), argv[3].asInt());
		wr_makeInt(&retVal, 1);
	}

	//animations
	inline void run_animation_splash(WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr)
	{
//		if (argn != 6) return;
//		auto ce = static_cast<ControlElements*>(usr);
//		auto anim = new Splash(argv[0].asInt(), argv[1].asInt(), argv[2].asInt(), argv[3].asInt() > 0);
//		ce->cm->run_animation(anim, argv[4].asInt(), argv[5].asInt());
//		wr_makeInt(&retVal, 1);

	}

	inline void wrench_random(WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr)
	{
		if (argn != 2) return;

		wr_makeInt(&retVal, wr_randEx(argv[0].asInt(), argv[1].asInt()));
	}

	static void register_wrench_functions(WRState* w, ControlElements* ce)
	{
		wr_registerFunction(w, "print", wrench_wrapper::print, &ce); // bind a function

		wr_registerFunction(w, "set_status", wrench_wrapper::set_status, ce);
		wr_registerFunction(w, "get_status", wrench_wrapper::get_status, ce);
		wr_registerFunction(w, "get_controls", wrench_wrapper::get_controls, ce);
		wr_registerFunction(w, "set_controls", wrench_wrapper::set_controls, ce);
		wr_registerFunction(w, "get_current_tps", wrench_wrapper::get_current_tps, ce);
		wr_registerFunction(w, "set_tps", wrench_wrapper::set_tps, ce);
		wr_registerFunction(w, "reset_controls", wrench_wrapper::reset_controls, ce);
		wr_registerFunction(w, "is_animation_running", wrench_wrapper::is_animation_running, ce);
		// wr_registerFunction(w, "run_animation", wrench_wrapper::run_animation, ce);
		wr_registerFunction(w, "stop_animation", wrench_wrapper::stop_animation, ce);

		wr_registerFunction(w, "set", wrench_wrapper::set_pixel, ce);
		wr_registerFunction(w, "off", wrench_wrapper::off_pixel, ce);
		wr_registerFunction(w, "fill", wrench_wrapper::fill_matrix, ce);
		wr_registerFunction(w, "clear", wrench_wrapper::clear_matrix, ce);
		wr_registerFunction(w, "line", wrench_wrapper::draw_line, ce);
		wr_registerFunction(w, "rect", wrench_wrapper::draw_rect, ce);
		wr_registerFunction(w, "rect_filled", wrench_wrapper::draw_rect_filled, ce);
		wr_registerFunction(w, "circle", wrench_wrapper::draw_circle, ce);
		wr_registerFunction(w, "number", wrench_wrapper::draw_number, ce);

		//animations
		wr_registerFunction(w, "run_animation_splash", wrench_wrapper::run_animation_splash, ce);

		//utils
		wr_registerFunction(w, "random", wrench_wrapper::wrench_random, ce);
	}
}

void C3test()
{
	WRState* w = wr_newState();

	ControlElements ce;
	
	wrench_wrapper::register_wrench_functions( w, &ce );

	wr_destroyState( w );
}

#endif //WRENCHWRAPPER_H
