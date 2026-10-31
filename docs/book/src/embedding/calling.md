# Calling script functions

This page shows how the host calls functions of a loaded script: by name,
by hash or through a pre-fetched pointer, with arguments and a return value.
It also covers what must happen first, yielded functions, natives that call
back into the script, and errors.

## The calls

```cpp
WRValue* wr_callFunction( WRContext* context, const char* functionName, const WRValue* argv =0, const int argn =0 );
WRValue* wr_callFunction( WRContext* context, const int32_t hash, const WRValue* argv =0, const int argn =0 );
WRValue* wr_callFunction( WRContext* context, WRFunction* function, const WRValue* argv =0, const int argn =0 );

WRFunction* wr_getFunction( WRContext* context, const char* functionName );
WRFunction* wr_getFunction( WRContext* context, const uint32_t functionHash );

uint32_t wr_hashStr( const char* dat, uint32_t serial=0 );
```

* By name: the name is hashed on every call, then looked up.
* By hash: `wr_hashStr( "name" )` computed once saves the hashing.
* By `WRFunction*`: `wr_getFunction()` looks the function up once; the call
  itself then costs almost nothing beyond running the function. The pointer
  stays valid as long as the context exists (also across
  `wr_resetContext()`).

Each call runs the function to its end and returns a pointer to its return
value, or null when the call failed or the function yielded.

`argv` points to `argn` values that become the function's parameters.
Missing arguments are 0 and extra ones are dropped. Integers
and floats are made with `setInt()` / `setFloat()`, strings with
`wr_makeString()` in the context you call ([Values](values.md)).

## What must have run first

A context's functions can be called once its global code has run to the
end: after `wr_run()`, or `wr_newContext()` followed by
`wr_executeContext()`. Before that, and while the global code is yielded,
`wr_callFunction()` by name or hash fails with
`WR_ERR_run_must_be_called_by_itself_first`. The reason is that the global
code may set up the globals the functions use.

## An example

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

// twice( x ): calls the script function double( x ) and adds 1, a native
// that calls back into the script that called it
static void twice( WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr )
{
	WRValue arg;
	arg.setInt( argn > 0 ? argv[0].asInt() : 0 );
	WRValue* doubled = wr_callFunction( c, "double", &arg, 1 );
	if ( doubled )
	{
		retVal.setInt( doubled->asInt() + 1 );
	}
}

static const char* source =
	"function double( x ) { return x * 2; }\n"
	"function greet( name ) { return \"hello \" + name; }\n"
	"function sum( a, b, c ) { return a + b + c; }\n"
	"function useTwice( x ) { return twice( x ); }\n"
	"function steps()\n"
	"{\n"
	"	var got = yield( 1 );\n"
	"	print( \"steps got \", got, \"\\n\" );\n"
	"	return 99;\n"
	"}\n"
	"function bad() { return missing( 1 ); }\n";

int main()
{
	WRState* w = wr_newState();
	wr_loadAllLibs( w );
	wr_registerFunction( w, "print", print );
	wr_registerFunction( w, "twice", twice );

	unsigned char* bytecode;
	int size;
	if ( wr_compile(source, (int)strlen(source), &bytecode, &size) != WR_ERR_None )
	{
		return 1;
	}

	// a context whose global code has not run yet
	WRContext* fresh = wr_newContext( w, bytecode, size );
	WRValue* r = wr_callFunction( fresh, "double" );
	printf( "before running: %s, error %d\n", r ? "result" : "null", (int)wr_getLastError(w) );
	wr_destroyContext( fresh );

	WRContext* context = wr_run( w, bytecode, size, true );

	// by name
	WRValue arg;
	arg.setInt( 21 );
	r = wr_callFunction( context, "double", &arg, 1 );
	printf( "double( 21 ) = %d\n", r->asInt() );

	// by hash, computed once
	const int32_t greetHash = (int32_t)wr_hashStr( "greet" );
	WRValue name;
	wr_makeString( context, &name, "host" );
	r = wr_callFunction( context, greetHash, &name, 1 );
	char text[64];
	printf( "greet = %s\n", r->asString(text, sizeof(text)) );

	// by function pointer, looked up once
	WRFunction* sum = wr_getFunction( context, "sum" );
	WRValue three[3];
	three[0].setInt( 1 );
	three[1].setFloat( 2.5f );
	three[2].setInt( 3 );
	r = wr_callFunction( context, sum, three, 3 );
	printf( "sum = %g\n", r->asFloat() );

	// fewer arguments than parameters: the rest are 0
	r = wr_callFunction( context, sum, three, 1 );
	printf( "sum( 1 ) = %d\n", r->asInt() );

	// a library function through the same call
	WRValue angle;
	angle.setFloat( 0.0f );
	r = wr_callFunction( context, "math::cos", &angle, 1 );
	printf( "math::cos( 0 ) = %g\n", r ? r->asFloat() : -1.0f );

	// a native that calls back into the script
	arg.setInt( 5 );
	r = wr_callFunction( context, "useTwice", &arg, 1 );
	printf( "useTwice( 5 ) = %d\n", r->asInt() );

	// a function that yields, then continues
	r = wr_callFunction( context, "steps" );
	WRValue* args = 0;
	int argc = 0;
	WRValue* reply = 0;
	if ( !r && wr_getYieldInfo(context, &args, &argc, &reply) )
	{
		printf( "steps yielded %d value(s), first %d\n", argc, args[0].asInt() );
		reply->setInt( 7 ); // what yield() returns in the script
		r = wr_continue( context );
	}
	printf( "steps returned %d\n", r ? r->asInt() : -1 );

	// errors
	r = wr_callFunction( context, "nothere" );
	printf( "nothere: %s, error %d\n", r ? "result" : "null", (int)wr_getLastError(w) );
	r = wr_callFunction( context, "bad" );
	printf( "bad: %s, error %d, missing hash %s\n", r ? "result" : "null", (int)wr_getLastError(w),
		wr_getMissingFunctionHash(w) == wr_hashStr("missing") ? "is missing" : "?" );

	wr_destroyState( w );
	return 0;
}
```

Output:

```text
before running: null, error 28
double( 21 ) = 42
greet = hello host
sum = 6.5
sum( 1 ) = 1
math::cos( 0 ) = 1
useTwice( 5 ) = 11
steps yielded 1 value(s), first 1
steps got 7
steps returned 99
nothere: null, error 31
bad: null, error 2, missing hash is missing
```

The error numbers are `WR_ERR_run_must_be_called_by_itself_first` (28),
`WR_ERR_wrench_function_not_found` (31) and `WR_ERR_function_not_found`
(2); the last one comes from inside the script, which called a function
nobody provides.

## Return values

The returned pointer points into the context's stack. It is valid until the
next call into the same context (any `wr_callFunction()`, `wr_continue()` or
`wr_executeContext()`), which reuses the slot. Copy what you need first:

```cpp
int a = wr_callFunction( context, "double", &arg, 1 )->asInt(); // copied out
```

An int or a float is copied by `asInt()` / `asFloat()`. A string, array or
hash table the function returns belongs to the context and lives as long
as something in the script still refers to it; to keep it, copy the value
into a variable of yours and call `wr_keepValue()` on it
([Strings and their lifetime](values.md#strings-and-their-lifetime)), copy
its contents out (`asString()`, `asMallocString()`), or store it in a
global of the script. `wr_returnValueFromLastCall( context )` fetches the same value
again.

A function without a `return` statement returns 0.

## Library functions

`wr_callFunction()` by name or hash also finds library functions
(`"math::cos"` above) and calls them with the same arguments. This lets the
host treat natives, library functions and script functions alike behind
one name. `wr_getFunction()` does not find library functions.

Functions of modules the context imported are found by name or hash as
well.

## Yielded functions

A script function that calls `yield()` returns null to the host with the
context in the yielded state; `wr_getYieldInfo()` returns true and gives
the values passed to `yield()` and the slot for the value `yield()` will
return. `wr_continue()` resumes it. [Yield, time slices and the
scheduler](scheduling.md) covers this in detail.

> [!WARNING]
> While a context is yielded, *any* `wr_callFunction()` on it resumes the
> yielded function and ignores the function and the arguments you asked
> for. Calling `"z"` while `"y"` is yielded runs the rest of `y` and returns
> `y`'s result. Check `wr_getYieldInfo()` first, or keep functions that
> yield in a context of their own.

## Calling back from a native

A native may call `wr_callFunction()` on the context that called it, as
`twice()` does above. The nested call runs above the frames of the running
script and returns to the native, which then returns to the script. Some
rules apply:

* The nested call must not yield: `yield()` in it fails with
  `WR_ERR_yield_in_native_call`, and time slices are off for it.
* If the nested call fails, `w->err` is set and the outer script stops
  with that error when the native returns. A native that handles the
  failure clears `c->w->err = WR_ERR_None` before returning
  ([Errors from natives](natives.md#errors-from-natives)).
* `wr_resetContext()` and `wr_destroyContext()` on the running context are
  not allowed inside a native.

## Errors

`wr_callFunction()` returns null and sets the state's error:

| Error | Cause |
|---|---|
| `WR_ERR_run_must_be_called_by_itself_first` | the global code has not finished |
| `WR_ERR_wrench_function_not_found` | no script function (or library function) of that name |
| `WR_ERR_cannot_call_function_context_yielded` | a library function was called on a yielded context, or an imported function tried to yield |
| `WR_ERR_stack_overflow` | the arguments do not fit the stack, or the function recursed too deep |
| `WR_ERR_execute_function_zero_called_more_than_once` | a null `WRFunction*` after the global code ended (see below) |
| any runtime error | the function failed: a missing native, a native that set an error, out of memory |

Read it with `wr_getLastError( w )`. The context survives a failed call and
the next call starts afresh. See [Errors and robustness](errors.md).

A null `WRFunction*` stands for the global code: `wr_executeContext()` runs
it that way, and `wr_continue()` resumes through it. Once the global code
has ended, `wr_callFunction()` with a null function, such as the result of
a `wr_getFunction()` that found nothing, fails with
`WR_ERR_execute_function_zero_called_more_than_once` and runs nothing.
Still check the result of `wr_getFunction()` before calling through it, to
report the missing function by name.
