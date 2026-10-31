# States, contexts and running code

This page explains the two objects every host works with, `WRState` and
`WRContext`, how a program is loaded and run, how its life ends, and who
owns the bytecode. Read it before calling into scripts from your own main
loop.

## The state

```cpp
WRState* wr_newState( int stackSize =WRENCH_DEFAULT_STACK_SIZE );
void wr_destroyState( WRState* w );
```

A state is one independent instance of wrench. Everything registered in it
(natives, library functions, library constants) is visible to all of its
contexts, and so is the `msg::` mailbox. Two states share nothing, which is
why each thread or RTOS task that runs scripts needs a state of its own
([Several states, threads and RTOS tasks](threads.md)).

`stackSize` is the number of values in the stack of each context the state
creates; it is not shared. A value is 8 bytes on a 32-bit target and 16
bytes on a 64-bit one. The default, `WRENCH_DEFAULT_STACK_SIZE`, is 64. The
stack holds call frames, arguments, locals and temporaries, not the
contents of strings, arrays and hash tables, which live on the heap; most
scripts are fine with 32 to 64 entries. A script that needs more fails
with `WR_ERR_stack_overflow` (see [Errors and robustness](errors.md)).

`wr_destroyState()` destroys every context still alive, frees the bytecode
the contexts own and everything the scripts allocated, and runs the cleanup
functions of loaded libraries.

## Contexts

A context is one loaded program: a bytecode image plus its globals, its
stack and the objects its code allocated. The simplest way to get one is
`wr_run()`:

```cpp
WRContext* wr_run( WRState* w,
				   const unsigned char* block,
				   const int blockSize,
				   const bool takeOwnershipOfBlock =false,
				   const bool destroyContext =false );
```

It checks the image, creates a context and runs the global code (the
statements outside functions) to the end. It returns the context, ready for
`wr_callFunction()`, or null when the image is invalid or the global code
failed; `wr_getLastError()` says why, and the context is already destroyed.
If the global code yields, `wr_run()` returns the context in the yielded
state ([Yield, time slices and the scheduler](scheduling.md)).

`wr_run()` is two calls in one, which you can also make yourself:

```cpp
WRContext* wr_newContext( WRState* w, const unsigned char* block, const int blockSize, bool takeOwnership =false );
WRValue* wr_executeContext( WRContext* context );
```

`wr_newContext()` only loads: it checks the image and allocates the
context, but runs nothing. Between the two calls you can attach a debugger
to the context, give it a user pointer, or store values into its globals
before the global code sees them
([Passing data](data.md#seeding-globals-before-the-global-code-runs)).
`wr_executeContext()` runs the global code and returns its return value
(0 unless the global code says `return x;`), or null on an error or a
yield. Unlike `wr_run()` it leaves a failed context alive.

The global code runs once per context. A second `wr_executeContext()` fails
with `WR_ERR_execute_function_zero_called_more_than_once` unless the
context was reset in between:

```cpp
bool wr_resetContext( WRContext* context );
```

`wr_resetContext()` returns the context to the state right after
`wr_newContext()`: its globals are cleared (what they held is freed), the
modules it imported are destroyed and a pending yield is dropped. The
pointer stays valid, so a debugger attachment, a user pointer and your own
references keep working. It fails (returns false) for an imported context
and while a native of the context is running.

For a run and nothing else there are two shortcuts:

* `wr_runOnce( w, block, size )` runs the global code and destroys the
  context right away (a macro around `wr_run()`).
* `wr_runCommand( w, source, size )` compiles source text, runs it once and
  frees everything. It returns false only when the source does not compile.

## Several contexts in one state

The example below loads one image into two contexts, runs a third program
next to them and resets one of them.

```cpp
#include "wrench.h"

#include <stdio.h>
#include <string.h>

static void print( WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr )
{
	char buf[256];
	for ( int i = 0; i < argn; ++i )
	{
		printf( "%s", argv[i].asString(buf, sizeof(buf)) );
	}
}

static const char* counterSource =
	"var count = 0;\n"
	"print( \"global code runs\\n\" );\n"
	"function bump() { count++; msg::write( \"last\", count ); return count; }\n";

static const char* watcherSource =
	"function check() { return msg::read( \"last\" ); }\n";

int main()
{
	WRState* w = wr_newState( 64 ); // every context of this state gets a 64 entry stack
	wr_loadAllLibs( w );
	wr_registerFunction( w, "print", print );

	unsigned char* counter;
	int counterSize;
	unsigned char* watcher;
	int watcherSize;
	if ( wr_compile(counterSource, (int)strlen(counterSource), &counter, &counterSize) != WR_ERR_None
		 || wr_compile(watcherSource, (int)strlen(watcherSource), &watcher, &watcherSize) != WR_ERR_None )
	{
		return 1;
	}

	// two contexts made from one image: the bytecode is shared, the globals are not
	WRContext* one = wr_newContext( w, counter, counterSize );
	WRContext* two = wr_newContext( w, counter, counterSize );
	wr_executeContext( one );
	wr_executeContext( two );

	wr_callFunction( one, "bump" );
	wr_callFunction( one, "bump" );
	wr_callFunction( two, "bump" );
	printf( "one: %d, two: %d\n", wr_getGlobalRef(one, "count")->asInt(), wr_getGlobalRef(two, "count")->asInt() );

	// a third context reads what the last bump() left in the state's mailbox
	WRContext* three = wr_run( w, watcher, watcherSize, true );
	printf( "watcher sees %d\n", wr_callFunction(three, "check")->asInt() );

	// running the global code a second time needs a reset
	if ( !wr_executeContext(one) )
	{
		printf( "again: error %d\n", (int)wr_getLastError(w) );
	}
	wr_resetContext( one );
	wr_executeContext( one );
	printf( "after reset one: %d\n", wr_getGlobalRef(one, "count")->asInt() );

	wr_destroyContext( two ); // or leave it to wr_destroyState()
	wr_destroyState( w );

	// counter was not handed over (takeOwnership false): it is still ours
	wr_free( counter );
	return 0;
}
```

Output:

```text
global code runs
global code runs
one: 2, two: 1
watcher sees 1
again: error 48
global code runs
after reset one: 0
```

Error 48 is `WR_ERR_execute_function_zero_called_more_than_once`.

What the contexts of one state share, and what they keep to themselves:

| Shared by all contexts of a state | Per context |
|---|---|
| natives, library functions and constants | globals |
| the `msg::` mailbox | value stack (`stackSize` values) |
| the state's user pointer (`wr_setStateContext()`) | strings, arrays, hash tables and their garbage collection |
| the garbage collector threshold | the context's user pointer (`wr_setLocalContext()`) |
| the last error (`wr_getLastError()`) | yield state |
| the time slice settings | modules it imported (`sys::importCompile`, `wr_import()`) |

Strings, arrays and hash tables belong to the context that created them.
Passing one to a function of another context, or storing it there, needs a
copy: `wr_copyValue()` ([Passing data](data.md#values-from-another-context)).
Integers and floats can be passed freely.

## Destroying a context

```cpp
void wr_destroyContext( WRContext* context );
```

frees the context, the modules it imported, the objects its scripts
allocated and, if the context owns it, the bytecode. A null pointer is
ignored. Destroying contexts one by one is optional: `wr_destroyState()`
destroys whatever is left.

Do not destroy a context from inside one of its own natives, and do not use
any pointer into it afterwards: return values, global references and
strings it created are gone with it.

## Who owns the bytecode

`takeOwnership` (the fourth argument of `wr_run()`, `wr_newContext()` and
`wr_import()`) decides who frees the image:

* `false` (the default): the image stays yours. It must stay valid and
  unchanged for as long as any context made from it exists, because the VM
  reads it in place. Free it after the last of those contexts is destroyed.
  This is the mode for images in flash, in ROM, in a static array, or shared
  by several contexts.
* `true`: the context frees the image with `wr_free()` when it is
  destroyed, and also right away when loading fails. The image must come
  from `wr_malloc()`, which is what `wr_compile()` returns and what the
  debugger's `takeUpload()` hands over. Only one context may own an image.

> [!WARNING]
> Never give one block to two contexts with `takeOwnership` set, and never
> free a block yourself after handing it over: both free it twice. A block
> from `malloc()` may be handed over only while the allocator is the default
> one ([Small targets](small-targets.md#the-allocator)).

## Running from flash or ROM

Because the VM never writes to the image and never copies it, bytecode in
read-only memory runs as it is:

```cpp
#include "blink.h" // made by: wrench ch blink.w blink.h blink

WRContext* context = wr_newContext( w, blink_bytecode, blink_bytecodeSize );
```

`wrench ch` writes the image as a `const unsigned char` array, which the
toolchain places in flash on microcontrollers. Leave `takeOwnership` false.
The RAM a context needs is its own structure, its globals, its function
table and its stack; [Small targets](small-targets.md) has the figures.

On AVR a `const` array is copied to RAM at startup unless it is declared
`PROGMEM`, and the VM reads the image through ordinary pointers, so there
the image costs RAM as well; on ARM, ESP32 and most other targets `const`
data stays in flash. The `READ_8_FROM_PC`, `READ_16_FROM_PC` and
`READ_32_FROM_PC` macros, through which the VM reads the code stream, can
be redefined for memories that need special access; see
[Build flags](../reference/build-flags.md).

## Imports

A running script can load more code into its own context with the `sys::`
library (`sys::importCompile`, `sys::importByteCode`, see
[Modules](../language/import.md)); the host does the same with

```cpp
WRContext* wr_import( WRContext* context, const unsigned char* block, const int blockSize, bool takeOwnership =false );
```

The imported code runs its global code at once, shares the parent's stack
and garbage collection, and its functions become callable from the parent,
from scripts and through `wr_callFunction()` on the parent. The new code
cannot see the parent's globals or functions: it calls its own functions,
those of the other modules and the state's natives and library functions,
and a call to a function of the importing script fails with
`WR_ERR_function_not_found`. An imported context lives until its parent is
destroyed or reset.
