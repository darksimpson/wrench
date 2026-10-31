# Errors and robustness

This page covers how wrench reports errors to the host, what it guarantees
when bytecode or scripts misbehave, how to carry on after an error, and
where a debugger can step in. The full list of codes is in
[Error codes](../reference/error-codes.md).

## How errors are reported

Every error is a `WRError` code, from the same enumeration for the compiler
and the VM:

* The compile functions return it ([Compiling scripts](compiling.md)),
  with a message, a line and a column on request.
* Calls into the VM (`wr_run()`, `wr_executeContext()`,
  `wr_callFunction()`, `wr_continue()`) return null on failure and leave
  the code in the state:

```cpp
WRError wr_getLastError( WRState* w );
inline uint32_t wr_getMissingFunctionHash( WRState* w );
extern const char* c_errStrings[];
```

`wr_getLastError()` returns the error of the last call into the VM of that
state; each call starts by clearing it. A null result with
`WR_ERR_None` means the call yielded rather than failed: check
`wr_getYieldInfo()`.

After `WR_ERR_function_not_found` or `WR_ERR_lib_function_not_found`,
`wr_getMissingFunctionHash()` returns the hash of the name the script
called (`wr_hashStr( "name" )`, `wr_hashStr( "lib::name" )`), so the host
can say which one is missing.

`c_errStrings[ code ]` is the name of a code, `"WR_ERR_stack_overflow"`,
for messages and logs. It exists in builds with the compiler and covers the
codes up to `WR_ERR_integer_literal_too_large`; do not index it with your
own `WR_USER` codes.

Codes from `WR_USER` (100) to 255 are free for natives to report their own
failures ([Errors from natives](natives.md#errors-from-natives)).

## Runtime errors you will meet

| Error | Cause |
|---|---|
| `WR_ERR_function_not_found` | the script called a function that is neither in the script nor registered |
| `WR_ERR_lib_function_not_found` | the same for a `lib::name` call, often a library not loaded |
| `WR_ERR_stack_overflow` | recursion too deep or an expression too big for the stack size |
| `WR_ERR_malloc_failed` | an allocation failed, with `WRENCH_HANDLE_MALLOC_FAIL` |
| `WR_ERR_bad_bytecode_CRC` | the image is damaged, truncated or from a version this VM does not run ([Images from other versions](compiling.md#images-from-other-versions)) |
| `WR_ERR_unknown_opcode` | the image holds an opcode this VM does not know |
| `WR_ERR_division_by_zero` | with `WRENCH_TRAP_DIVISION_BY_ZERO` only |
| `WR_ERR_hash_table_size_exceeded` | an assignment to a new key of a hash table that is full at its largest size |
| `WR_ERR_yield_in_native_call` | code a native called back into tried to yield |
| `WR_ERR_debugger_terminated` | a debugger ended the script |
| `WR_ERR_wrench_function_not_found` | the host called a function the script does not have |
| `WR_ERR_run_must_be_called_by_itself_first` | the host called a function before the global code finished |
| `WR_ERR_execute_function_zero_called_more_than_once` | `wr_executeContext()` again without `wr_resetContext()`, or `wr_callFunction()` with a null `WRFunction*` after the global code ended |
| `WR_ERR_context_not_yielded` | `wr_continue()` on a context that is not yielded |
| `WR_USER` and up | set by a native |

## An example

```cpp
// build with WRENCH_HANDLE_MALLOC_FAIL defined
#include "wrench.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// an allocator that can be told to fail
static bool g_failAllocations = false;

static void* testAlloc( size_t size )
{
	return g_failAllocations ? 0 : malloc( size );
}

static void testFree( void* p )
{
	free( p );
}

static const char* errorName( WRError err )
{
	return (err >= 0 && err <= WR_ERR_integer_literal_too_large) ? c_errStrings[err] : "user error";
}

static unsigned char* compile( const char* source, int* size )
{
	unsigned char* bytecode = 0;
	WRstr message;
	WRError err = wr_compile( source, (int)strlen(source), &bytecode, size, &message );
	if ( err != WR_ERR_None )
	{
		printf( "compile: %s", message.c_str() );
		return 0;
	}
	return bytecode;
}

static const char* source =
	"var list[] = { 1, 2, 3 };\n"
	"function grow( n ) { var a[] = { }; for ( var i = 0; i < n; ++i ) { a[i] = \"item \" + i; } return a._count; }\n"
	"function deep( n ) { return n <= 0 ? 0 : 1 + deep( n - 1 ); }\n"
	"function callMissing() { return sensor::read( 3 ); }\n";

int main()
{
	wr_setGlobalAllocator( testAlloc, testFree ); // before any other wrench call

	WRState* w = wr_newState( 64 );
	int size = 0;
	unsigned char* bytecode = compile( source, &size );
	WRContext* context = wr_run( w, bytecode, size, true );

	WRValue n;
	n.setInt( 10 );
	WRValue* r = wr_callFunction( context, "grow", &n, 1 );
	printf( "grow( 10 ) = %d\n", r ? r->asInt() : -1 );

	g_failAllocations = true;
	r = wr_callFunction( context, "grow", &n, 1 );
	g_failAllocations = false;
	printf( "grow with no memory: %s\n", r ? "ok" : errorName(wr_getLastError(w)) );

	n.setInt( 1000 );
	r = wr_callFunction( context, "deep", &n, 1 );
	printf( "deep( 1000 ): %s\n", r ? "ok" : errorName(wr_getLastError(w)) );

	r = wr_callFunction( context, "callMissing" );
	printf( "callMissing: %s, missing is sensor::read: %s\n", r ? "ok" : errorName(wr_getLastError(w)),
			wr_getMissingFunctionHash(w) == wr_hashStr("sensor::read") ? "yes" : "no" );

	// the context is still usable after a failed call
	n.setInt( 3 );
	r = wr_callFunction( context, "grow", &n, 1 );
	printf( "grow( 3 ) after the errors = %d\n", r ? r->asInt() : -1 );

	// a damaged image is refused before anything runs
	bytecode = compile( source, &size );
	bytecode[size / 2] ^= 0x40;
	printf( "damaged image valid: %d\n", (int)wr_isBytecodeValid(bytecode, size) );
	WRContext* bad = wr_run( w, bytecode, size, true ); // frees the image although it fails
	printf( "wr_run: %s, %s\n", bad ? "context" : "null", errorName(wr_getLastError(w)) );

	wr_destroyState( w );
	return 0;
}
```

Output:

```text
grow( 10 ) = 10
grow with no memory: WR_ERR_malloc_failed
deep( 1000 ): WR_ERR_stack_overflow
callMissing: WR_ERR_lib_function_not_found, missing is sensor::read: yes
grow( 3 ) after the errors = 3
damaged image valid: 0
wr_run: null, WR_ERR_bad_bytecode_CRC
```

## Recovering after an error

* **A failed function call** leaves the context usable. Its globals keep
  whatever the failed code had stored so far; the next call starts on a
  fresh stack. Objects the failed call allocated are collected as usual.
* **A failed global code run under `wr_run()`** destroys the context (and
  frees the image if the context owned it); `wr_run()` returns null.
* **A failed `wr_executeContext()`** leaves the context alive but not
  callable, since its global code never finished. Fix what was wrong (a
  missing native, a seeded global) and run it again after
  `wr_resetContext()`, or destroy it.
* **A failed resume** (`wr_continue()`) ends the yielded call; the context
  is no longer yielded.
* **A failed compile** returns no image and allocates nothing you must
  free.

The state itself is never damaged by a script error; other contexts are not
affected.

With `WRENCH_HANDLE_MALLOC_FAIL`, the failure flag `g_mallocFailed` (one
per thread) is cleared at the start of each call into the VM, so the call
after a `WR_ERR_malloc_failed` runs normally once memory is available
again, as `grow( 3 )` does in the example. A call a native makes back into
its own context keeps the flag: the native's failure still counts.

## What is guaranteed

wrench is meant to run scripts it did not write on devices that must not
crash. The VM checks, at a small cost:

* **The image.** A checksum over the whole image, which also encodes the
  major version, is verified before a context is made; a truncated image or
  one too small for its own header is refused. Opcodes the VM does not know
  fail with `WR_ERR_unknown_opcode`.
* **The stack.** Every push is checked against the stack size, and calls
  check that their arguments and frame fit, so deep recursion or a huge
  expression fails with `WR_ERR_stack_overflow` instead of writing past the
  stack. `WRENCH_UNPROTECTED_STACK` removes the push check for scripts known
  to fit.
* **Calls.** A call to a missing function fails cleanly; natives get
  exactly the arguments the script passed and must check `argn`.
* **Containers.** Array indexes are bounded (`WRENCH_MAX_ARRAY_ELEMENTS`),
  a hash table that is full at its largest size fails the assignment
  with `WR_ERR_hash_table_size_exceeded`, containers that contain
  themselves are handled by the collector, printing and serialization.
* **Arithmetic.** Integer overflow wraps, shifts are taken modulo 32,
  division and modulo by zero give 0, `INT_MIN / -1` does not trap. With
  `WRENCH_TRAP_DIVISION_BY_ZERO`, `/`, `%`, `/=` and `%=` by zero fail
  with `WR_ERR_division_by_zero` instead, in every build including
  `WRENCH_COMPACT`.
* **Memory.** With `WRENCH_HANDLE_MALLOC_FAIL`, a failed allocation stops
  the script with `WR_ERR_malloc_failed` instead of dereferencing null.
  Without it, wrench assumes allocations succeed.
* **Serialization.** `wr_deserialize()` rejects malformed and truncated
  input and limits nesting (`WRENCH_MAX_SERIAL_DEPTH`).
* **The compiler.** Deep nesting fails with `WR_ERR_nesting_too_deep`
  instead of exhausting the native stack; the limits of the bytecode format
  (255 globals, 255 functions, 64 KB of code, 65535 byte strings, 255
  arguments) are errors, not silent corruption.

What is not guaranteed:

* An image whose checksum matches but whose content was crafted or
  corrupted on purpose can still make the VM read out of range: opcodes and
  operands are validated as they execute, not at load time. Accept bytecode
  only from sources you trust, or compile it yourself.
* A native can do anything. The checks above stop at your code.
* Without time slices a script that loops forever never returns
  ([Time slices](scheduling.md#time-slices)).

## Debugging errors

With a debug server attached, the VM stops on a runtime error before
returning it, with the script's frames intact, so the debugger shows the
line, the call stack and the variables at the point of failure. Messages
about missing functions name the function. See
[Debugging](../debugging/overview.md); for the device side,
[Adding the debugger to your firmware](../debugging/firmware.md).
