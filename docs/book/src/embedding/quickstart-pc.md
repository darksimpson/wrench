# Quick start: Windows and Linux

This page puts wrench into a C++ program on a PC: it compiles a script from
a string and from a file, calls a script function, gives the script a
native function, and shows the build commands for GCC, Clang, MSVC and
CMake.

## Get the two files

wrench is distributed as a single file build: `wrench.h` and `wrench.cpp`.
Take them from the assets of a release on the
[Releases page](https://github.com/darksimpson/wrench/releases), or
generate them from a checkout (see [Getting the tools](../start/tools.md)):

```bash
wrench release src dist
```

Put both files into your project, for example:

```text
myapp/
	main.cpp
	wrench/
		wrench.h
		wrench.cpp
```

There is nothing else to install and nothing to configure: the defaults
build the compiler, the VM and the standard library.

## A complete program

The program registers two natives, `print()` and `scale()`, compiles a
script held in a string, calls the script's function `area()` and reads its
global `calls`. If a file name is given on the command line it compiles and
runs that file as well.

```cpp
#include "wrench.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// print( ... ): the script's way to write to stdout
static void print( WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr )
{
	char buf[256];
	for ( int i = 0; i < argn; ++i )
	{
		printf( "%s", argv[i].asString(buf, sizeof(buf)) );
	}
}

// scale( x ): x times the factor the host registered the function with
static void scale( WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr )
{
	float factor = *(float*)usr;
	retVal.setFloat( argn > 0 ? argv[0].asFloat() * factor : 0.0f );
}

// compile source text and run its global code; the context stays alive
// so its functions can be called afterwards
static WRContext* load( WRState* w, const char* name, const char* source, int size )
{
	unsigned char* bytecode = 0;
	int bytecodeSize = 0;
	WRstr message;
	WRError err = wr_compile( source, size, &bytecode, &bytecodeSize, &message );
	if ( err != WR_ERR_None )
	{
		printf( "%s: compile error %d\n%s", name, (int)err, message.c_str() );
		return 0;
	}

	// takeOwnership: the context frees the bytecode when it is destroyed
	WRContext* context = wr_run( w, bytecode, bytecodeSize, true );
	if ( !context )
	{
		printf( "%s: runtime error %d\n", name, (int)wr_getLastError(w) );
	}
	return context;
}

// read a whole file into memory, 0 when it cannot be read
static char* readFile( const char* path, int* size )
{
	FILE* file = fopen( path, "rb" );
	if ( !file )
	{
		return 0;
	}
	fseek( file, 0, SEEK_END );
	long length = ftell( file );
	fseek( file, 0, SEEK_SET );
	char* text = (char*)malloc( length + 1 );
	if ( text && fread(text, 1, length, file) != (size_t)length )
	{
		free( text );
		text = 0;
	}
	fclose( file );
	if ( text )
	{
		text[length] = 0;
		*size = (int)length;
	}
	return text;
}

static const char* source =
	"var calls = 0;\n"
	"function area( w, h )\n"
	"{\n"
	"	calls++;\n"
	"	return w * h;\n"
	"}\n"
	"print( \"loaded, scale( 2 ) is \", scale( 2 ), \"\\n\" );\n";

int main( int argn, char** argv )
{
	WRState* w = wr_newState(); // one state: natives, contexts, memory
	wr_loadAllLibs( w ); // math::, str::, std:: ... for the scripts

	static float factor = 1.5f;
	wr_registerFunction( w, "print", print );
	wr_registerFunction( w, "scale", scale, &factor );

	// a script from a string
	WRContext* context = load( w, "built-in", source, (int)strlen(source) );
	if ( context )
	{
		WRValue args[2];
		args[0].setInt( 6 );
		args[1].setFloat( 2.5f );
		WRValue* result = wr_callFunction( context, "area", args, 2 );
		if ( result )
		{
			printf( "area( 6, 2.5 ) = %g\n", result->asFloat() );
		}

		WRValue* calls = wr_getGlobalRef( context, "calls" );
		printf( "calls = %d\n", calls ? calls->asInt() : -1 );
	}

	// a script from a file named on the command line
	if ( argn > 1 )
	{
		int size = 0;
		char* text = readFile( argv[1], &size );
		if ( !text )
		{
			printf( "cannot read %s\n", argv[1] );
		}
		else
		{
			load( w, argv[1], text, size );
			free( text );
		}
	}

	wr_destroyState( w ); // frees every context and what they allocated
	return 0;
}
```

A script file to try it with, `hello.w`:

```wrench
// hello.w
var total = 0;
for ( var i = 1; i <= 4; ++i )
{
	total += i;
}
print( "hello from a file, total ", total, ", sqrt( 2 ) ", math::sqrt( 2 ), "\n" );
```

Output of `./quickstart hello.w`:

```text
loaded, scale( 2 ) is 3
area( 6, 2.5 ) = 15
calls = 1
hello from a file, total 10, sqrt( 2 ) 1.41421
```

With a file that does not compile, `broken.w`:

<!-- check:error -->
```wrench
var a = 1;
var b = a + ;
```

the compiler's message says where:

```text
broken.w: compile error 9
line:2
err: WR_ERR_bad_expression[9]
2     var b = a + ;
                  ^
```

## What the program does

1. `wr_newState()` creates the engine. Its argument, the stack size of
   each context, defaults to `WRENCH_DEFAULT_STACK_SIZE` (64 values).
2. `wr_loadAllLibs()` registers the standard library; a program that wants
   only some of it calls the individual loaders
   ([Native functions and libraries](natives.md#the-standard-library)).
3. `wr_registerFunction()` makes `print` and `scale` callable from every
   context of the state. `scale` gets a pointer to `factor` as its `usr`
   argument.
4. `wr_compile()` turns the source into a bytecode block allocated with
   `wr_malloc()`. It returns `WR_ERR_None` (0) or an error code, and fills
   the optional `WRstr` with a readable message.
5. `wr_run()` creates a context for the block and runs its global code. With
   `takeOwnership` set to `true` the context frees the block when it is
   destroyed, so the program never frees it. `wr_run()` returns the context,
   or null when the global code failed.
6. `wr_callFunction()` calls `area()` with two arguments. The result is a
   pointer to a value inside the context, valid until the next call into it.
7. `wr_getGlobalRef()` finds the global `calls` by name. It works because
   `wr_compile()` keeps the names of globals by default
   (`WR_INCLUDE_GLOBALS`).
8. `wr_destroyState()` destroys every context, frees the bytecode they own
   and everything the scripts allocated.

The following pages take each step further:
[running code](running.md), [compiling](compiling.md),
[calling](calling.md), [values](values.md) and [natives](natives.md).

## Building

wrench needs a C++ compiler; the VM and the compiler are C++98 and build
with any later standard too. No libraries beyond the C and C++ runtime are
needed, and no defines: every option has a default.

GCC on Linux (and MinGW on Windows):

```bash
g++ -O2 -Wall -I wrench main.cpp wrench/wrench.cpp -o quickstart
```

Clang takes the same arguments:

```bash
clang++ -O2 -Wall -I wrench main.cpp wrench/wrench.cpp -o quickstart
```

MSVC, in a Developer Command Prompt (x86 or x64):

```bash
cl /nologo /O2 /EHsc /D_CRT_SECURE_NO_WARNINGS /I wrench main.cpp wrench\wrench.cpp /Fe:quickstart.exe
```

`_CRT_SECURE_NO_WARNINGS` only silences MSVC's advice to replace `fopen()`
in the example.

CMake:

```cmake
cmake_minimum_required(VERSION 3.10)
project(quickstart CXX)

add_library(wrench STATIC wrench/wrench.cpp)
target_include_directories(wrench PUBLIC wrench)

add_executable(quickstart main.cpp)
target_link_libraries(quickstart PRIVATE wrench)
```

`wrench.cpp` compiles slowly at high optimization (it is one large
translation unit); keeping it in a library of its own, as above, means it
is compiled once.

> [!NOTE]
> Build options go on the compiler command line (`-DWRENCH_TIME_SLICES`,
> `/DWRENCH_TIME_SLICES`, `target_compile_definitions(wrench PUBLIC ...)`)
> or into a `wrench_config.h` next to `wrench.h`. They must be the same for
> `wrench.cpp` and for every file that includes `wrench.h`, because some of
> them change the layout of `WRState` and `WRContext`; with CMake, make
> them `PUBLIC` on the library. See [Build flags](../reference/build-flags.md).

## Next steps

* Ship compiled bytecode instead of source: [Compiling scripts](compiling.md).
* Run several programs side by side: [States, contexts and running code](running.md).
* Keep a long running script from blocking your program:
  [Yield, time slices and the scheduler](scheduling.md).
* Debug the scripts in VS Code: [Debugging](../debugging/overview.md).
