# Debug targets

These examples run a `WrenchDebugServer` next to the VM so a host debugger
(VS Code through `wrench dap`, or the console debugger `wrench dbg`) can
debug scripts on the device: breakpoints, stepping, pause, call stack,
variables, editing values, runtime error stops, console output.

| Example | Board / SDK | Link |
|---|---|---|
| `arduino_serial` | any Arduino core (ESP32, ESP8266, RP2040, SAMD, STM32duino, Mega) | `Serial` (UART / USB CDC) |
| `esp32_arduino` | ESP32, Arduino core | Serial, or TCP over WiFi (`DEBUG_OVER_WIFI` in the sketch) |
| `esp32_idf` | ESP32, ESP-IDF | UART, or TCP over WiFi (`idf.py menuconfig`, "Wrench debug target") |
| `stm32_hal` | STM32Cube HAL | UART with an interrupt driven receive ring |

## Preparing an example

Each example needs three generated or copied files next to its sources:
`wrench.cpp`, `wrench.h` and `blink.h`. They go into the example folder,
except for `esp32_idf`, where the sources live in `main/`:

| Example | Put `wrench.cpp`, `wrench.h`, `blink.h` into |
|---|---|
| `arduino_serial` | `arduino_serial/` |
| `esp32_arduino` | `esp32_arduino/` |
| `esp32_idf` | `esp32_idf/main/` |
| `stm32_hal` | `stm32_hal/`, or your Cube project's source folder together with the other files |

1. `wrench.cpp` and `wrench.h` are the single-file build. Download both from
   the assets of a release on the repository's Releases page (any release
   made after the debugger was added), or generate them from `discrete_src/` in the
   repository root:

   ```
   make dist
   ```

   on Linux (builds the `wrench` tool and writes `dist/`), or with an
   existing `wrench` executable on any platform:

   ```
   wrench release discrete_src dist
   ```

   Then copy `wrench.cpp` and `wrench.h` into the folder above (git ignores
   them there).

2. All examples run `script/blink.w`. Generate its header, here from this
   folder (`examples/debug_targets`):

   ```
   wrench ch -debugsource script/blink.w arduino_serial/blink.h blink
   wrench ch -debugsource script/blink.w esp32_arduino/blink.h blink
   wrench ch -debugsource script/blink.w esp32_idf/main/blink.h blink
   wrench ch -debugsource script/blink.w stm32_hal/blink.h blink
   ```

   The comment at the top of each example gives the same command relative
   to that example's own folder. Use a `wrench` executable of the same
   version as the single file build (bytecode formats can change between
   versions).

Each example carries a `wrench_config.h` (for `esp32_idf` in `main/`) that
`wrench.h` picks up automatically through `__has_include`, so no compiler
flags are needed. With a compiler that lacks `__has_include`, pass its
defines as compiler flags instead.

`esp32_idf` needs ESP-IDF v5.0 or later. On IDF 5.3 and later its `driver`
component dependency is a deprecated umbrella for `esp_driver_uart` and
`esp_driver_gpio`; it is kept so the example still builds on 5.0 to 5.2.

## How it fits together

```c++
WRState* w = wr_newState();
// register your native functions...

// bytes in and out, see "Writing a transport"
static MyTransport link;
WrenchDebugServer debugger( w, &link );

// your firmware calls into the script itself
debugger.setHostDriven( true );

// the image in flash, then its global code once
WRContext* ctx = debugger.loadBytes( bytecode, bytecodeSize );
wr_executeContext( ctx );

for ( ;; )
{
	// service the debugger
	debugger.tick();

	// a program is loaded and its global code has run
	if ( debugger.ready() )
	{
		wr_callFunction( debugger.context(), "loop" );
	}
}
```

* `tick()` must be called regularly while no script runs. It answers the
  debugger and, when the debugger *launches* a program, runs its global code.
* While script code runs, the VM polls the link every
  `WRENCH_DEBUG_POLL_INTERVAL` source lines (64 by default), so the debugger
  can pause the script and change breakpoints even in an endless script
  loop. Polling only reads the link: it does not yield to other tasks or
  feed a watchdog (`idle()` is not called while script code runs), so a
  long running call still trips whatever watchdog or scheduling rules the
  firmware has, debugger attached or not. The comment at the top of
  `esp32_idf/main/main.cpp` covers the ESP-IDF task watchdog.
* At a breakpoint the VM blocks inside the call that hit it and keeps
  servicing the link until the debugger resumes. Your firmware is halted
  meanwhile; `WrenchDebugTransport::idle()` runs in that loop (feed a
  watchdog or let other tasks run there).
* The debugger may upload a new program. The server then replaces its
  context, which is why the loop asks `context()` every time instead of
  keeping the pointer. `tick()` returns `WRD_TickProgramLoaded` when that
  happens. `debugger.setAllowUpload( false )` refuses uploads for builds
  that must only run their own program; attaching and stepping still work.
* When the server learns that the debugger is gone, it clears the
  breakpoints and a halted script continues (`setAbortOnDisconnect( true )`
  terminates it instead). It learns that from an explicit detach (ending
  the debug session in VS Code, which after a `launch` also terminates the
  script first, or `q` in `wrench dbg`), or from a transport that reports a
  lost link: `read()` returning -1 or `connected()` turning false, as the
  TCP transports do when the connection closes. A UART cannot tell: by
  default, if the host crashes or the cable is pulled while the script is
  halted, the target stays halted (and keeps servicing the link) until a
  debugger connects again and continues or detaches.
  `debugger.setLinkTimeout( ms )` makes a halted target treat a debugger it
  has not heard from for that long as disconnected. VS Code pings a halted
  target every second, so a few seconds are enough there; the console
  debugger `wrench dbg` does not ping, so with it use a generous value or
  none.
* `debugger.breakOnEntry()` before running the script halts it on its first
  line until a debugger attaches, to debug from the very start (each example
  has the line, commented out). `breakOnEntry( 30000 )` gives up after 30 s.
* Only code compiled with debug information (`-debug` or `-debugsource`)
  stops anywhere. `-debugsource` also embeds the source, so the debugger can
  attach without the source files.

## Writing a transport

```c++
class MyTransport : public WrenchDebugTransport
{
public:
	// send all bytes, false when the link failed
	bool write( const uint8_t* data, uint32_t len );

	// never block: bytes read, 0 when nothing is waiting, -1 when the link is lost
	int read( uint8_t* data, uint32_t maxLen );

	// monotonic milliseconds
	uint32_t millis();

	// optional: called while waiting (halted at a breakpoint, waiting for a
	// debugger), not while script code runs
	void idle();

	// optional: false while no peer is attached; links that cannot tell
	// (UART) keep the default, true
	bool connected();
};
```

The protocol frames every message with sync bytes, a length and a CRC16, so
a UART link that drops or corrupts bytes recovers on its own, and other
output on the same line (boot messages, logs) is skipped. Still, do not
print to the debug port yourself; send script output through
`debugger.print()` so it shows in the debug console.

## Memory

The server allocates its state once, from the heap in its constructor
(`malloc`, or the allocator given to `wr_setGlobalAllocator()`): about
`2 x WRENCH_DEBUG_MAX_PAYLOAD` bytes for its frame buffers plus
`12 x WRENCH_DEBUG_MAX_HANDLES` and `4 x WRENCH_DEBUG_MAX_BREAKPOINTS` (on a
32-bit target). Defaults are 1024 / 64 / 32. A small MCU is fine with:

```c++
#define WRENCH_DEBUG_MAX_PAYLOAD 128
#define WRENCH_DEBUG_MAX_BREAKPOINTS 8
#define WRENCH_DEBUG_MAX_HANDLES 16
```

Uploading a program needs a RAM buffer of the bytecode size; debugging a
program that runs from flash (attach) needs none. Setting a variable to a
string from the debugger allocates that string in the VM, and launching a
program that already ran builds a fresh context for it, as loading one
does. Breakpoints, stepping, pausing and inspecting values allocate
nothing.

## Connecting

VS Code (with the Wrench extension):

```json
{
	"type": "wrench", "request": "attach", "name": "Attach over serial",
	"program": "${workspaceFolder}/script/blink.w",
	"transport": "serial", "serialPort": "COM5", "baudRate": 115200
}
```

```json
{
	"type": "wrench", "request": "launch", "name": "Upload over WiFi",
	"program": "${workspaceFolder}/script/blink.w",
	"transport": "tcp", "host": "192.168.1.50", "port": 5050
}
```

Console:

```
wrench dbg -target serial:COM5:115200 -attach script/blink.w
wrench dbg -target tcp:192.168.1.50:5050 script/blink.w
```

`attach` debugs the image the board already runs (and warns if it differs
from the local source); `launch` uploads the fresh build and runs it.

Both ESP32 examples debug over the serial port by default and over WiFi
when switched to it. Over WiFi they log their address at startup
(`esp32_arduino` on the serial monitor, `esp32_idf` as "debug server at
192.168.1.50:5050" in `idf.py monitor`). The ESP-IDF one also registers the
DHCP hostname `wrench-target`, which many routers resolve, so
`"host": "wrench-target"` may work instead of the address.

On ESP32 and ESP8266, `arduino_serial` and `esp32_arduino` enlarge the
serial receive buffer with `Serial.setRxBufferSize()`: the default 256
bytes are less than one frame with the payload size they use.

## Testing without hardware

`wrench dbgserver -target tcp:5050 script/blink.w` (or
`-target serial:COM9:115200` on one end of a virtual serial pair) acts as a
target on the PC.
