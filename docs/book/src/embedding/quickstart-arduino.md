# Quick start: Arduino

This page runs a wrench script on an Arduino board: the files a sketch
needs, `wrench_config.h`, compiling the script on the PC into a header,
running it from flash, a native that drives the LED, and what it costs in
memory. It follows `examples/arduino/hello_world` and
`examples/debug_targets/arduino_serial` in the repository.

> [!NOTE]
> The sketch on this page was compiled and run on a PC against a stand-in
> for `Arduino.h`, not built for a board here. It uses the same calls as the
> examples in the repository.

## What you need

* the Arduino IDE (or `arduino-cli`, or PlatformIO) and your board's core;
* the `wrench` command line tool on the PC ([Getting the tools](../start/tools.md));
* the single file build, `wrench.h` and `wrench.cpp`, of the same version
  as the tool.

## The sketch folder

The Arduino IDE compiles only the files in the sketch's folder, so the
single file build goes there, next to the sketch:

```text
blink_sketch/
	blink_sketch.ino
	wrench.h
	wrench.cpp
	wrench_config.h
	blink.h
	blink.w
```

`blink.w` is the script, `blink.h` the bytecode generated from it. The IDE
ignores `.w` files; keeping the source next to the sketch is a convenience.

## wrench_config.h

`wrench.h` includes a file named `wrench_config.h` if one is on the include
path, which for a sketch means the sketch folder. It is where build flags
go on platforms where passing `-D` options to library sources is awkward,
as it is in the Arduino IDE. For a small board that only runs bytecode:

```cpp
// picked up automatically by wrench.h when it sits next to it
#define WRENCH_WITHOUT_COMPILER
#define WRENCH_COMPACT
#define WRENCH_DEFAULT_STACK_SIZE 32
```

* `WRENCH_WITHOUT_COMPILER`: scripts are compiled on the PC.
* `WRENCH_COMPACT`: a smaller VM at some cost in speed;
  `WRENCH_REALLY_COMPACT` saves a few KB more.
* `WRENCH_DEFAULT_STACK_SIZE`: the value stack of each context, 8 bytes per
  entry; `wr_newState()` without an argument uses it.

The lookup needs a compiler with `__has_include`, which every current
Arduino core has. `WRENCH_NO_CONFIG_HEADER` turns it off. All flags are
listed in [Build flags](../reference/build-flags.md).

## The script

```wrench
// blink.w: the firmware calls loop() and waits as long as it returns
var count = 0;

function loop()
{
	count = count + 1;
	led( count % 2 );
	if ( count % 10 == 0 )
	{
		println( "blinked ", count, " times" );
	}
	return 500;
}

println( "script started" );
```

The global code runs once, when the sketch loads the script. After that
the sketch calls `loop()`, which returns the number of milliseconds until
it wants to be called again.

## Compile it on the PC

```bash
wrench ch blink.w blink.h blink
```

writes `blink.h` with two definitions, `blink_bytecodeSize` and
`blink_bytecode[]`, the image as a `const unsigned char` array (109 bytes
for this script). Run the command again whenever the script changes, and
whenever you update wrench: the image must come from a tool of the same
version as `wrench.cpp`, or the VM refuses it with
`WR_ERR_bad_bytecode_CRC` ([Compiling scripts](compiling.md#compiling-on-the-pc-and-shipping-bytecode)).

`wrench r blink.w` runs the global code on the PC as a quick check; it
prints `script started` (`loop()` is not called, and `led()` exists only in
the sketch).

## The sketch

```cpp
#include <Arduino.h>
#include "wrench.h"
#include "blink.h"

#ifndef LED_BUILTIN
#define LED_BUILTIN 13
#endif

static WRState* w = 0;
static WRContext* script = 0;
static uint32_t nextCall = 0;

static void print( WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr )
{
	char buf[32];
	for ( int i = 0; i < argn; ++i )
	{
		Serial.print( argv[i].asString(buf, sizeof(buf)) );
	}
}

static void println( WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr )
{
	print( c, argv, argn, retVal, usr );
	Serial.print( "\r\n" );
}

static void led( WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr )
{
	if ( argn > 0 )
	{
		digitalWrite( LED_BUILTIN, argv[0].asInt() ? HIGH : LOW );
	}
}

void setup()
{
	Serial.begin( 115200 );
	pinMode( LED_BUILTIN, OUTPUT );

	w = wr_newState(); // WRENCH_DEFAULT_STACK_SIZE from wrench_config.h
	wr_registerFunction( w, "print", print );
	wr_registerFunction( w, "println", println );
	wr_registerFunction( w, "led", led );

	// runs the global code from flash, the context stays for loop()
	script = wr_run( w, blink_bytecode, blink_bytecodeSize );
	if ( !script )
	{
		Serial.print( "script failed, error " );
		Serial.println( (int)wr_getLastError(w) );
	}
}

void loop()
{
	if ( script && (int32_t)(millis() - nextCall) >= 0 )
	{
		WRValue* ret = wr_callFunction( script, "loop" );
		int delayMs = ret ? ret->asInt() : 1000;
		nextCall = millis() + (delayMs > 0 ? delayMs : 1);
	}
}
```

On the serial monitor at 115200 baud:

```text
script started
blinked 10 times
blinked 20 times
```

and the LED toggles every half second.

How it works:

* `setup()` creates the state once and registers three natives: `print()`
  and `println()` write to `Serial`, `led()` sets the LED. The script can
  call only what is registered.
* `wr_run()` makes a context for the image and runs its global code. The
  image stays in flash; `takeOwnership` is left false because it is not
  heap memory.
* The sketch's `loop()` calls the script's `loop()` when it is due, and
  never blocks, so the rest of the sketch keeps running between calls.
  The `(int32_t)(millis() - nextCall)` comparison stays correct when
  `millis()` wraps around.

On an Arduino the standard library is not loaded unless you call the
loaders. `wr_loadAllArduinoLibs()` adds `io::pinMode`, `io::digitalWrite`,
`io::analogRead`, `std::delay` and `wire::` functions for scripts that
drive pins themselves, and `lcd::` when `wrench_config.h` names your
display with `WR_ARDUINO_LCD`; `wr_loadStdLib()` adds `std::rand` and
`time::ms`, which reads `millis()` ([Native functions and libraries](natives.md#the-standard-library),
[Devices](../library/devices.md)).

## Memory footprint

On a 32-bit board this sketch needs, besides the code: about 150 bytes for
the state, 96 bytes for the context, 8 bytes per global and 8 per stack
entry (256 for 32 entries), and what the script allocates; well under 1 KB
of heap. [Small targets](small-targets.md) has measured figures and the
flags that make the VM smaller.

On AVR boards (Uno, Nano, Mega) RAM is the limit:

* Use `WRENCH_COMPACT` or `WRENCH_REALLY_COMPACT` and
  `WRENCH_WITHOUT_COMPILER`, and a stack of 20 to 32 entries
  (`examples/arduino/hello_world` uses 20).
* On AVR a `const` array is copied into RAM at startup like any other
  initialized data, so the image costs its size in RAM there too. Keep
  scripts short; compile without `-debug` and with `-noglobals` if the
  sketch never looks up globals by name.
* The debugger does not fit an Uno; a Mega can run it with small buffers
  (see below).

32-bit boards (ESP32, ESP8266, RP2040, SAMD, STM32) have room for all of
it, including the debugger.

## Debugging on the board

`examples/debug_targets/arduino_serial` is the same sketch with a
`WrenchDebugServer` on the USB serial port, for any Arduino core. Its
`wrench_config.h` switches the debugger on:

```cpp
#define WRENCH_INCLUDE_DEBUG_CODE
```

with smaller buffers on AVR (`WRENCH_DEBUG_MAX_PAYLOAD 128`, eight
breakpoints, 16 handles, a stack of 48). The sketch creates the server on
`Serial` through `WrenchDebugStreamTransport`, attaches the script's
context, and calls the server's `tick()` at the top of `loop()`. Generate
its header with `-debugsource` so the debugger has line information and the
source. The serial port then carries the debug protocol, so the sketch must
not print to it directly; `print()` sends to the debugger's console
instead.

The firmware side is explained in
[Adding the debugger to your firmware](../debugging/firmware.md), and
connecting from VS Code or the console in
[Debug targets](../debugging/targets.md).
