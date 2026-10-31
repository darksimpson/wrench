# Compiling scripts

This page covers turning wrench source into bytecode: the compile functions
of the API and their flags, error positions, the include callback, compiling
on the PC with the `wrench` tool to ship bytecode, and builds that leave
the compiler out.

## The compile functions

```cpp
WRError wr_compile( const char* source,
					const int size,
					unsigned char** out,
					int* outLen,
					WRstr* errMsg =0,
					const uint8_t compilerOptionFlags = WR_INCLUDE_GLOBALS );

WRError wr_compileEx( const char* source,
						const int size,
						unsigned char** out,
						int* outLen,
						WRstr* errMsg,
						const uint8_t compilerOptionFlags,
						int* errorLine,
						int* errorColumn );

WRError wr_compileWithOptions( const char* source,
								const int size,
								unsigned char** out,
								int* outLen,
								const WRCompileOptions& options,
								WRstr* errMsg =0,
								int* errorLine =0,
								int* errorColumn =0,
								WRstr* errorFile =0 );
```

All three compile `size` bytes of `source` (it need not be zero
terminated) and return `WR_ERR_None` (0) on success, with `*out` pointing
to a new bytecode block of `*outLen` bytes. The block comes from
`wr_malloc()`: free it with `wr_free()`, or hand it to a context with
`takeOwnership` ([who owns the bytecode](running.md#who-owns-the-bytecode)).
On failure `*out` is null and the return value is the error.

* `wr_compile()` is the everyday call.
* `wr_compileEx()` adds the position of an error: a 1-based line and a
  0-based column right after the token where the compiler gave up.
* `wr_compileWithOptions()` adds a file name for the source and the include
  callback, and reports the file an error is in.

The compiler prints nothing. When `errMsg` is given it receives a message
with the position, the error name and the line with a marker:

```text
line:2
err: WR_ERR_bad_expression[9]
2     var b = a + ;
                  ^
```

It stops at the first error, so there is only ever one message.

## Compiler flags

`compilerOptionFlags` (or `WRCompileOptions::flags`) is an OR of:

| Flag | Default | Effect |
|---|---|---|
| `WR_INCLUDE_GLOBALS` | on | stores a 4 byte hash per global; `wr_getGlobalRef()` and the debugger find globals by name only with it |
| `WR_EMBED_DEBUG_CODE` | off | a line marker per statement and a symbol table; needed by the debugger; costs space, and the markers take a little time even with no debugger |
| `WR_EMBED_SOURCE_CODE` | off | stores the source text (of every included file too) so a debugger can show it without the files; implies nothing else, use it with `WR_EMBED_DEBUG_CODE` |
| `WR_NON_STRICT_VAR` | off | non-strict mode for older scripts: an assignment declares an unknown variable, no block scope, a second `var` assigns (see [Non-strict mode](../language/variables.md#non-strict-mode)) |
| `WR_READS_KEEP_VALUES` | always | set by the compiler in every image, tells the VM how to read members of non-containers; passing it changes nothing |

What the flags cost, for this six line script:

```wrench
var count = 0;
function step( n )
{
	count += n;
	return count;
}
```

| Flags | Image size |
|---|---|
| none | 32 bytes |
| `WR_INCLUDE_GLOBALS` | 36 bytes |
| `WR_INCLUDE_GLOBALS` + `WR_EMBED_DEBUG_CODE` | 81 bytes |
| `WR_INCLUDE_GLOBALS` + `WR_EMBED_DEBUG_CODE` + `WR_EMBED_SOURCE_CODE` | 151 bytes |

Code compiled without `WR_EMBED_DEBUG_CODE` runs at full speed even in a
build with the debugger compiled in; it cannot be stopped in.

## Includes and the include callback

`include "file.w";` at the top level of a script compiles that file in
place, once per program however often it is included
([Several files](../language/include.md)). The compiler opens no files
itself; it asks a callback you provide:

```cpp
typedef bool (*WR_INCLUDE_CALLBACK)( const char* name, const char* includer, WRstr& source, WRstr& resolved, void* usr );

struct WRCompileOptions
{
	uint8_t flags; // WrenchCompilerFlags
	const char* name; // file name of the source: messages, includes, debugger. May be 0
	WR_INCLUDE_CALLBACK include; // 0: include "file" fails with WR_ERR_include_not_supported
	void* includeUser; // passed to include
	WRCompileOptions() : flags( WR_INCLUDE_GLOBALS ), name( 0 ), include( 0 ), includeUser( 0 ) {}
};
```

The callback gets the name as written in the script and the resolved name
of the including file (`options.name` for the main source). It fills
`source` with the file's text and `resolved` with a name that identifies
the file, and returns true; false means there is no such file
(`WR_ERR_include_not_found`). The same file must always resolve to the same
name: the compiler uses it to skip files it already included, and errors
and the debugger show it.

`wr_compile()` and `wr_compileEx()` have no callback, so a script with
`include` fails with `WR_ERR_include_not_supported` there. So does
`sys::importCompile` in scripts.

This program serves includes from a table in memory, the way firmware
without a file system would:

```cpp
#include "wrench.h"

#include <stdio.h>
#include <string.h>

// files the scripts may include, a table instead of a file system
static const char* const files[] =
{
	"util.w", "function clamp( v, lo, hi ) { return v < lo ? lo : (v > hi ? hi : v); }\n",
	"config.w", "include \"util.w\";\nvar limit = clamp( 250, 0, 100 );\n",
	"bad.w", "var ok = 1;\nvar broken = ok + ;\n",
	0
};

// the include callback: find "name", give its text and a stable name
static bool findInclude( const char* name, const char* includer, WRstr& source, WRstr& resolved, void* usr )
{
	const char* const* table = (const char* const*)usr;
	for ( int i = 0; table[i]; i += 2 )
	{
		if ( !strcmp(table[i], name) )
		{
			source = table[i + 1];
			resolved = name;
			return true;
		}
	}
	return false;
}

static WRError compile( const char* source, const WRCompileOptions& options, unsigned char** out, int* outLen )
{
	WRstr message;
	WRstr file;
	int line = 0;
	int column = 0;
	WRError err = wr_compileWithOptions( source, (int)strlen(source), out, outLen, options, &message, &line, &column, &file );
	if ( err != WR_ERR_None )
	{
		printf( "error %d in %s line %d column %d\n%s", (int)err, file.c_str(), line, column, message.c_str() );
	}
	return err;
}

int main()
{
	const char* program = "include \"config.w\";\ninclude \"util.w\";\nvar result = clamp( limit + 5, 0, 102 );\n";

	WRCompileOptions options; // flags default to WR_INCLUDE_GLOBALS
	options.name = "main.w";
	options.include = findInclude;
	options.includeUser = (void*)files;

	unsigned char* bytecode = 0;
	int size = 0;
	if ( compile(program, options, &bytecode, &size) == WR_ERR_None )
	{
		WRState* w = wr_newState();
		WRContext* context = wr_run( w, bytecode, size, true );
		printf( "result = %d\n", wr_getGlobalRef(context, "result")->asInt() );
		wr_destroyState( w );
	}

	// an error inside an included file names that file and its line
	compile( "var a = 1;\ninclude \"bad.w\";\n", options, &bytecode, &size );

	// without a callback include is an error
	WRstr message;
	WRError err = wr_compile( program, (int)strlen(program), &bytecode, &size, &message );
	printf( "wr_compile: error %d\n", (int)err );

	// wr_compileEx: line and column without the options structure
	int line = 0;
	int column = 0;
	const char* typo = "var x = 1;\nx = x +* 2;\n";
	err = wr_compileEx( typo, (int)strlen(typo), &bytecode, &size, 0, WR_INCLUDE_GLOBALS, &line, &column );
	printf( "wr_compileEx: error %d at line %d column %d\n", (int)err, line, column );

	// what the flags cost
	const char* text = "var count = 0;\nfunction step( n )\n{\n\tcount += n;\n\treturn count;\n}\n";
	const uint8_t flags[] =
	{
		0,
		WR_INCLUDE_GLOBALS,
		WR_INCLUDE_GLOBALS | WR_EMBED_DEBUG_CODE,
		WR_INCLUDE_GLOBALS | WR_EMBED_DEBUG_CODE | WR_EMBED_SOURCE_CODE,
	};
	const char* names[] = { "0", "globals", "globals + debug", "globals + debug + source" };
	for ( int i = 0; i < 4; ++i )
	{
		if ( wr_compile(text, (int)strlen(text), &bytecode, &size, 0, flags[i]) == WR_ERR_None )
		{
			printf( "%-26s %3d bytes, valid %d\n", names[i], size, (int)wr_isBytecodeValid(bytecode, size) );
			wr_free( bytecode );
		}
	}
	return 0;
}
```

Output:

```text
result = 102
error 9 in bad.w line 2 column 19
bad.w:2
err: WR_ERR_bad_expression[9]
2     var broken = ok + ;
                        ^
wr_compile: error 65
wr_compileEx: error 9 at line 2 column 11
0                           32 bytes, valid 1
globals                     36 bytes, valid 1
globals + debug             81 bytes, valid 1
globals + debug + source   151 bytes, valid 1
```

`config.w` and the main program both include `util.w`; it is compiled
once. Error 65 is `WR_ERR_include_not_supported`. The command line tool's
own callback looks next to the including file, then in the folders given
with `-I`, then in the `include` folders of a `wrench.json`
([Describing your host application](../vscode/wrench-json.md)).

## Checking and reading images

```cpp
bool wr_isBytecodeValid( const uint8_t* bytecode, const unsigned int len, uint32_t* hash =0 );
void wr_disassemble( const uint8_t* bytecode, const unsigned int len, WRstr& out, const bool includeComments =true );
void wr_disassemble( const uint8_t* bytecode, const unsigned int len, char** out, unsigned int* outLen =0 );
```

`wr_isBytecodeValid()` checks the checksum at the end of an image, which
also encodes the major version of wrench, and can return it as a hash that
identifies the image. Use it before storing an image you received.
`wr_run()` and `wr_newContext()` make the same check and fail with
`WR_ERR_bad_bytecode_CRC`.

`wr_disassemble()` writes a readable listing of an image, the same as
`wrench d`. The `char**` form allocates the text with `wr_malloc()`; free it
with `wr_free()`.

## Compiling on the PC and shipping bytecode

On a microcontroller you compile on the PC and put only the bytecode on the
device. The `wrench` command line tool does it
([Command line tool](../reference/cli.md)):

```bash
wrench c blink.w blink.bin           # a binary image, for a file system or an upload
wrench ch blink.w blink.h blink      # a C header with the image as an array
```

The compile options come before the file names: `-debug`
(`WR_EMBED_DEBUG_CODE`), `-debugsource` (debug code and source),
`-noglobals` (leaves out `WR_INCLUDE_GLOBALS`, which the tool sets by
default), `-nostrict` (`WR_NON_STRICT_VAR`) and `-I folder` for includes.

`wrench ch` writes a header like this (here for `examples/basic.w`):

```cpp
#ifndef _basic_H
#define _basic_H

/******* wrench bytcode automatically generated header *******/

const int basic_bytecodeSize=68;
const unsigned char basic_bytecode[]=
{
	0x01, 0x00, 0x11, 0x94, 0x85, 0xBE, 0xED, 0x04, 0x0D, 0x00, 0x48, 0x65, 0x6C, 0x6C, 0x6F, 0x20, // 16
	0x57, 0x6F, 0x72, 0x6C, 0x64, 0x21, 0x0A, 0x06, 0x01, 0x88, 0x8A, 0x37, 0x16, 0xCF, 0x00, 0x00, // 32
	0xAF, 0x0A, 0x70, 0x00, 0x0F, 0x09, 0x20, 0x00, 0x06, 0x01, 0x88, 0x8A, 0x37, 0x16, 0x90, 0x00, // 48
	0x37, 0xEF, 0x09, 0x04, 0x01, 0x00, 0x0A, 0x06, 0x01, 0x88, 0x8A, 0x37, 0x16, 0x02, 0xEF, 0x14, // 64
	0x84, 0x69, 0x4A, 0x59, // 68
};

#endif
```

which the firmware includes and runs in place:

```cpp
WRContext* context = wr_run( w, basic_bytecode, basic_bytecodeSize );
```

> [!IMPORTANT]
> Compile with a `wrench` tool of the same version as the `wrench.cpp` in
> your firmware, and regenerate the headers whenever you update wrench.
> The bytecode format can change between major versions; see
> [Images from other versions](#images-from-other-versions) for what a VM
> accepts.

Images are endian neutral and independent of the word size, so one image
runs on the PC, on a 32-bit microcontroller and on a big endian target
alike.

## Images from other versions

The checksum at the end of an image is salted with the major version of
the compiler that made it. A wrench 8.0 VM runs:

* images of its own major version, 8;
* images of the major version before (`WRENCH_PREVIOUS_IMAGE_VERSION`, 7),
  with the behaviour they were compiled for. Such an image lacks the image
  flag `WR_READS_KEEP_VALUES`, and in it reading a hash table key that is
  not there, or an element past the end of an array or string, creates the
  entry or grows the container, and reading an element of a value that is
  not a container turns the value into one.

Everything else fails with `WR_ERR_bad_bytecode_CRC`. A 7.x VM refuses
8.0 images. Images made by unreleased builds before 8.0 carry the flag
with the old salt; the 8.0 VM refuses them, and they must be compiled
again.

## Builds without the compiler

Define `WRENCH_WITHOUT_COMPILER` for targets that only run bytecode. The
compile functions then remain, but return `WR_ERR_compiler_not_loaded`
without doing anything (so does `sys::importCompile` in scripts),
`wr_disassemble()` returns a line saying it is unavailable, and the error
name table `c_errStrings` is left out: print error numbers instead.
A linker that removes unused code drops most of the compiler anyway when it
is never called; the define makes sure, and saves the rest.

The compiler also needs much more memory than the VM while it runs: it
builds the whole program in heap memory and recurses on nesting
(`WRENCH_MAX_PARSE_DEPTH` levels, about 2 KB of native stack each on a
desktop build). Compiling on a microcontroller is possible on larger parts
such as the ESP32, given a task stack to match, but shipping bytecode is the
better default.
