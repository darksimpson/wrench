# Native functions and libraries

Natives are the C++ functions scripts call; they are how a script reaches
your application or your hardware. This page covers plain natives, library
functions and constants, the handlers for names nothing provides, loading
the standard library, failing a call, and what a native must not do.

## Natives

```cpp
typedef void (*WR_C_CALLBACK)( WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr );

void wr_registerFunction( WRState* w, const char* name, WR_C_CALLBACK function, void* usr =0 );
```

`wr_registerFunction()` makes `name` callable from every context of the
state, as an ordinary function: `setSpeed( 1200 )`. The native receives:

| Parameter | Meaning |
|---|---|
| `c` | the context the call comes from; `c->w` is the state |
| `argv`, `argn` | the arguments, as many as the script passed (may be 0, `argv` may then be null) |
| `retVal` | the return value, the integer 0 unless you set it |
| `usr` | the pointer given to `wr_registerFunction()` |

Scripts may pass any number of arguments of any type, so check `argn` and
read arguments with the getters (`asInt()`, `asFloat()`, `asString()`...,
see [Values](values.md)). Set the result with `retVal.setInt()`,
`retVal.setFloat()`, or `wr_makeString( c, &retVal, text )` for a string,
which is created in the calling context.

Registering a name again replaces the function. A native and a script
function of the same name do not clash: a script calls its own function.

## Library functions

```cpp
typedef void (*WR_LIB_CALLBACK)( WRValue* stackTop, const int argn, WRContext* context );

void wr_registerLibraryFunction( WRState* w, const char* signature, WR_LIB_CALLBACK function );
void wr_registerLibraryConstant( WRState* w, const char* signature, const int32_t i );
void wr_registerLibraryConstant( WRState* w, const char* signature, const float f );
```

A library function has a name of the form `lib::name` and a leaner calling
convention, the one the standard library uses. The arguments lie on the
VM's stack right below `stackTop`, and the return value goes into
`*stackTop` itself, which starts as the integer 0:

| Slot | Holds |
|---|---|
| `stackTop[0]` | the return value |
| `stackTop[-1]` | the last argument |
| `stackTop - argn` | the first argument |

```cpp
WRValue* args = stackTop - argn; // args[0] is the first argument
```

There is no user pointer; use `wr_getStateContext( context->w )` or
`wr_getLocalContext( context )` instead ([Passing data](data.md#user-pointers)).
A library function is not faster to write, but a call to it does less work
than a call to a native.

A library constant is a named number scripts read like a variable
(`unit::MAX`). It cannot be assigned to.

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

// a device the natives work on, handed over as the usr pointer
struct Motor
{
	const char* name;
	int speed;
};

// setSpeed( rpm ): returns the speed actually set
static void setSpeed( WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr )
{
	Motor* motor = (Motor*)usr;
	if ( argn < 1 )
	{
		return; // retVal stays the integer 0
	}
	int rpm = argv[0].asInt();
	if ( rpm < 0 || rpm > 3000 )
	{
		c->w->err = (WRError)(WR_USER + 1); // stops the script
		return;
	}
	motor->speed = rpm;
	retVal.setInt( rpm );
}

// label( s ): returns s in brackets, a new string in the calling context
static void label( WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr )
{
	char text[64] = "[]";
	if ( argn > 0 )
	{
		char inner[48];
		snprintf( text, sizeof(text), "[%s]", argv[0].asString(inner, sizeof(inner)) );
	}
	wr_makeString( c, &retVal, text );
}

// unit::clamp( value, low, high ): a library function, arguments below stackTop
static void unitClamp( WRValue* stackTop, const int argn, WRContext* c )
{
	if ( argn != 3 )
	{
		return; // the return value, *stackTop, is already the integer 0
	}
	WRValue* args = stackTop - argn;
	float v = args[0].asFloat();
	float low = args[1].asFloat();
	float high = args[2].asFloat();
	wr_makeFloat( stackTop, v < low ? low : (v > high ? high : v) );
}

// called for every function name nothing provides
static void notFound( const uint32_t signature, WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr )
{
	if ( signature == wr_hashStr("legacyBeep") )
	{
		printf( "(legacyBeep ignored)\n" );
		return; // the call succeeds and returns 0
	}
	c->w->err = WR_ERR_function_not_found; // everything else fails as usual
}

static const char* source =
	"print( \"speed \", setSpeed( 1200 ), \"\\n\" );\n"
	"print( label( \"ok\" ), \" \", label( 42 ), \"\\n\" );\n"
	"print( \"clamp \", unit::clamp( 7.5, 0, 5 ), \" max \", unit::MAX, \"\\n\" );\n"
	"legacyBeep( 3 );\n"
	"setSpeed( 5000 );\n"
	"print( \"not reached\\n\" );\n";

int main()
{
	Motor motor = { "left", 0 };

	WRState* w = wr_newState();
	wr_registerFunction( w, "print", print );
	wr_registerFunction( w, "setSpeed", setSpeed, &motor );
	wr_registerFunction( w, "label", label );
	wr_registerLibraryFunction( w, "unit::clamp", unitClamp );
	wr_registerLibraryConstant( w, "unit::MAX", (int32_t)3000 );
	wr_setOnCallbackNotFound( w, notFound );

	unsigned char* bytecode;
	int size;
	if ( wr_compile(source, (int)strlen(source), &bytecode, &size) == WR_ERR_None )
	{
		WRContext* context = wr_run( w, bytecode, size, true );
		int err = (int)wr_getLastError( w );
		printf( "context %s, error %d (WR_USER + %d), motor %s at %d\n",
				context ? "alive" : "gone", err, err - WR_USER, motor.name, motor.speed );
	}

	wr_destroyState( w );
	return 0;
}
```

Output:

```text
speed 1200
[ok] [42]
clamp 5 max 3000
(legacyBeep ignored)
context gone, error 101 (WR_USER + 1), motor left at 1200
```

`setSpeed( 5000 )` failed the script with a user error, so the last line
never ran and `wr_run()` destroyed the context.

## Names nothing provides

```cpp
typedef void (*WR_C_CALLBACK_NOT_FOUND)( const uint32_t Signature, WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr );
typedef void (*WR_LIB_CALLBACK_NOT_FOUND)( const uint32_t Signature, WRValue* stackTop, const int argn, WRContext* context );

void wr_setOnCallbackNotFound( WRState* w, WR_C_CALLBACK_NOT_FOUND onCallbackNotFound );
void wr_setOnLibCallbackNotFound( WRState* w, WR_LIB_CALLBACK_NOT_FOUND onLibCallbackNotFound );
```

A script that calls a function nobody provides fails with
`WR_ERR_function_not_found` (or `WR_ERR_lib_function_not_found` for a
`lib::name`), and `wr_getMissingFunctionHash()` tells which name it was.
With a handler installed, the handler is called instead, with the hash of
the name (`wr_hashStr( "name" )`, or `wr_hashStr( "lib::name" )`) and the
arguments; the call succeeds with whatever the handler leaves in `retVal`
(or `*stackTop`). A handler can still fail the call by setting `c->w->err`,
as `notFound()` above does for every other name. `usr` is always null for
the not-found handler.

Handlers are useful for stand-ins (a simulator without the hardware
natives), for logging, and for dispatching many names through one table.

## Errors from natives

A native or library function fails the call by setting the state's error:

```cpp
c->w->err = (WRError)(WR_USER + 1);
```

The script stops right after the native returns, the VM returns null to the
host, and `wr_getLastError()` gives the code. Codes from `WR_USER` (100) up
to 255 are yours; `WR_ERR_...` codes may be used too. An attached debugger
stops on the error with the script's stack intact.

The same applies when a native calls back into the script and that call
fails: the error is still set when the native returns, and the outer script
stops too. A native that handles the failure itself clears it:

```cpp
WRValue* r = wr_callFunction( c, "inner" );
if ( !r )
{
	c->w->err = WR_ERR_None; // handled here, the script goes on
	retVal.setInt( -1 );
}
```

Out of range arguments are often better answered with a return value the
script can test (0, -1, an empty string) than with an error, which ends the
whole call. Keep errors for what the script cannot recover from.

## The standard library

`wr_loadAllLibs()` registers everything below; each loader can also be
called on its own, so a target loads only what it uses. The functions are
described in the [Standard library](../library/index.md).

| Loader | Registers | Notes |
|---|---|---|
| `wr_loadSysLib()` | `sys::` | imports, `sys::halt`, `sys::isFunction` |
| `wr_loadMathLib()` | `math::` | uses `<math.h>` |
| `wr_loadStdLib()` | `std::rand`, `std::srand`, `std::time`, `time::ms` | `std::rand` draws from the state's own seed; `time::ms` reads the board's or the system's millisecond clock ([Messages and time](../library/msg-time.md)) |
| `wr_loadIOLib()` | `io::` | does nothing unless a file I/O flag is defined ([Build flags](../reference/build-flags.md)) |
| `wr_loadStringLib()` | `str::` | floats are formatted without the C library unless `WRENCH_FLOAT_SPRINTF` is defined |
| `wr_loadMessageLib()` | `msg::` | the state's mailbox, also reached by the host ([The msg:: mailbox](data.md#the-msg-mailbox)) |
| `wr_loadSerializeLib()` | `std::serialize`, `std::deserialize` | |
| `wr_loadDebugLib()` | `debug::print`, `debug::println` | output to an attached debugger, nothing otherwise |
| `wr_loadTCPLib()` | `tcp::` | does nothing unless `WRENCH_WIN32_TCP` or `WRENCH_LINUX_TCP` is defined |
| `wr_loadContainerLib()` | `array::`, `hash::`, `list::`, `queue::`, `stack::` | |
| `wr_loadAllArduinoLibs()` | `lcd::`, `io::pinMode` and friends, `std::delay`, `wire::` | declared only when `ARDUINO` is defined, not part of `wr_loadAllLibs()`; `lcd::` needs `WR_ARDUINO_LCD`, see below |
| `wr_loadFastLEDLib()` | `fastled::` | Arduino; declared only when `WRENCH_ARDUINO_FASTLED` is defined, not part of `wr_loadAllLibs()` |

The Arduino loaders (`wr_loadAllArduinoLibs()`, its other name
`wr_loadArduinoLib()`, and `wr_loadArduinoSTDLib()`,
`wr_loadArduinoIOLib()`, `wr_loadArduinoWireLib()`,
`wr_loadArduinoLCDLib()`) exist when `ARDUINO` is defined, which the
Arduino framework does on every board. `lcd::` drives a display object of
yours with the LiquidCrystal interface (`begin`, `setCursor`, `print`):
name it in `wrench_config.h`,

```cpp
#include <LiquidCrystal.h>
extern LiquidCrystal lcd;
#define WR_ARDUINO_LCD lcd
```

Without `WR_ARDUINO_LCD`, `wr_loadArduinoLCDLib()` registers nothing and a
script's `lcd::` calls fail with `WR_ERR_lib_function_not_found`. The
functions are described in [Devices](../library/devices.md).

A library costs nothing until it is loaded: registering adds entries to the
state's registry, and code a linker sees no call to is dropped. `print()`
and `println()` are not part of any library; every host provides its own,
since only it knows where output goes.

## What a native must not do

* **Call into the debugger's `tick()`.** It runs only from the host loop,
  while no script runs. A native that blocks may call the server's `poll()`
  instead ([Blocking code, yield and polling](../debugging/blocking.md)).
* **Yield.** There is no way to suspend the script from inside a native,
  and script code a native calls back into fails with
  `WR_ERR_yield_in_native_call` if it yields. To wait for something, let
  the script yield itself (`yield()` in wrench) and resume it from the host
  loop when the event arrives ([Scheduling](scheduling.md)).
* **Destroy or reset the context that called it**, or destroy the state.
  Return, and do it from the host loop.
* **Keep pointers into the call** after returning: `argv` and `retVal` live
  on the VM stack. Copy what you need ([Values](values.md#lifetime-of-pointers-into-a-context)).
* **Block for long.** Nothing else runs in the state meanwhile: no other
  context, no debugger, and on a microcontroller no watchdog feeding in
  that task. Start the work and let the script poll for the result, or
  yield.
* **Touch the state from another thread.** A native runs on the thread
  that called into the VM; it may hand work to other threads, but those
  must not call wrench ([Threads](threads.md)).

Calling back into the script that called it is allowed
([Calling back from a native](calling.md#calling-back-from-a-native)).
