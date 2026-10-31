# Debug targets: the PC and devices

A target is anything that runs a `WrenchDebugServer` next to its VM: a PC
running `wrench dbgserver`, a board with debug-enabled firmware, or your
own application. This page covers the PC target and its options, the
example firmware for ESP32, Arduino and STM32, the transports, how to
choose between a serial link and TCP, attaching to a device that already
runs, and debugging inside a desktop application. To add the server to
your own firmware, read [Adding the debugger to your firmware](firmware.md).

## The PC as a target

`wrench dbgserver` makes the PC behave like a board: it runs a debug server
on a TCP port or a serial port, and VS Code or `wrench dbg` connect to it
the way they connect to a device.

```text
wrench dbgserver -target T [-noupload] [-hold] [-linktimeout MS] [-context F] [-v] [-project P] [file]
```

| Option | Meaning |
|---|---|
| `-target tcp:<port>` | listen on that TCP port on `127.0.0.1`: debuggers on the same machine only |
| `-target tcp:<address>:<port>` | listen on that IPv4 address of the machine; `tcp:0.0.0.0:<port>` listens on every interface |
| `-target serial:<port>[:<baud>]` | serve one end of a serial line, `COM9` or `/dev/ttyUSB0`, 115200 baud by default |
| `file` | a `.w` source (compiled with debug information and source) or a compiled `.bin` to preload as program 0 |
| `-context F` | also run script `F` in a context of its own, as firmware does with `attach()`; up to 3 times |
| `-hold` | start `file` at once, halted on its first line until a debugger attaches, like `breakOnEntry()` |
| `-noupload` | refuse programs sent by a debugger, like `setAllowUpload( false )` |
| `-linktimeout MS` | while halted, let go of a debugger not heard from for `MS` milliseconds, like `setLinkTimeout()` |
| `-v` | report attaches, detaches, uploads and program ends |
| `-project P` | the `wrench.json` to imitate, by default the nearest one above `file`, `none` ignores it |

The server is launchable: a debugger can launch the preloaded program,
upload another one in its place (unless `-noupload`) and run it. It
accepts the next client after one disconnects, so one server serves any
number of sessions until you stop it with Ctrl+C.

Started in `examples/debug_demo`, with one `wrench dbg` session that
uploads `main.w`, runs it and quits:

```text
> wrench dbgserver -target tcp:5181 -v main.w
wrench debug server listening on tcp 127.0.0.1:5181 (this machine only)
imitating the host described by D:\Temp\wrench\examples\debug_demo\wrench.json
loaded main.w (2037 bytes)
client attached
program uploaded
total = 68 cm2
average = 22
program finished: WR_ERR_None
client detached
```

Script output goes both to the server's console and to the debugger.

Without a `wrench.json` the server stands in for a generic device: natives
a device script expects but the PC lacks (`led()`, `digitalWrite()`)
return 0 and are reported once:

```text
[wrench] the script called led, which this host does not provide, it returns 0
```

With a `wrench.json` it imitates the application that file describes
instead: only its libraries, its declared natives answer their `"returns"`
value, and anything else fails with `WR_ERR_function_not_found`. See
[Describing your host application](../vscode/wrench-json.md).

`-context` attaches more scripts the way firmware attaches contexts it runs
itself. Each runs its global code at start, then its `tick()` function is
called ten times a second; the debugger sees each as a program of its own
(a thread in VS Code). A `tick()` that fails is reported ("a tick() failed:
..., it is not called again") and left alone. A `tick()` the debugger ends
(`kill`, or `r` while it is halted in it) is not a failure: it is called
again on the next round, as firmware would. `-context` needs `file`:
program 0 is the program a debugger replaces with uploads. This is how
`examples/debug_demo` runs two programs on one target, see
[Several files, programs and modules](programs.md#the-debug-demo).

`-hold` starts the preloaded program right away and halts it on its first
line until a debugger attaches, then it goes on calling into the script the
way host driven firmware does:

```text
> wrench dbgserver -target tcp:5178 -hold orders.bin
wrench debug server listening on tcp 127.0.0.1:5178 (this machine only)
loaded orders.bin (1037 bytes)
started, halted on the first line until a debugger attaches
```

Attach with `wrench dbg -target tcp:127.0.0.1:5178 -attach` or an attach
configuration in VS Code; [The console debugger](console.md#attaching-without-the-source)
shows such a session.

A serial target on a PC needs a serial line between two ports: two USB
adapters with crossed wires, or a virtual null-modem pair (com0com on
Windows, `socat` with two pseudo terminals on Linux):

```bash
wrench dbgserver -target serial:COM9:115200 script/blink.w
wrench dbg -target serial:COM8:115200 script/blink.w
```

`-target tcp:<port>` accepts debuggers on the same machine only. To debug
the PC target from another computer, name the address to listen on:
`tcp:192.168.1.20:5050` for one network interface, `tcp:0.0.0.0:5050` for
all of them. The startup line says which:

```text
wrench debug server listening on tcp 0.0.0.0:5050 (every interface)
```

An address that is not one of the machine's IPv4 addresses is refused
("cannot listen on ... port 5050 (an IPv4 address of this machine is
needed)").

> [!WARNING]
> On another address than `127.0.0.1`, anyone who can reach the port can
> upload and run programs on that PC. Open it on trusted networks only, or
> with `-noupload` and a firewall rule.

## Devices

`examples/debug_targets` has firmware for four platforms, all running the
same script `script/blink.w`:

| Example | Board and SDK | Link |
|---|---|---|
| `arduino_serial` | any Arduino core: ESP32, ESP8266, RP2040, SAMD, STM32duino, Mega | `Serial` (UART or USB CDC) |
| `esp32_arduino` | ESP32 with the Arduino core | `Serial`, or TCP over WiFi (`DEBUG_OVER_WIFI` in the sketch) |
| `esp32_idf` | ESP32 with ESP-IDF 5.0 or later | UART, or TCP over WiFi (`idf.py menuconfig`, "Wrench debug target") |
| `stm32_hal` | STM32Cube HAL | a UART with an interrupt driven receive ring |

Each example needs three files next to its sources (in `main/` for
`esp32_idf`):

1. `wrench.cpp` and `wrench.h`, the single-file build: download both from
   a release, or generate them with `make dist` (Linux) or
   `wrench release src dist` (any platform, with an existing `wrench`
   executable).
2. `blink.h`, the script compiled into a C header, from
   `examples/debug_targets`:

   ```bash
   wrench ch -debugsource script/blink.w esp32_idf/main/blink.h blink
   ```

   Use a `wrench` executable of the same version as the single-file build;
   bytecode formats can change between versions.

Each example carries a `wrench_config.h`, which `wrench.h` picks up through
`__has_include`; it switches the debugger on and sizes it for the board.
The comment at the top of each source gives the setup steps for that
board.

The ready-made firmware in the examples is launchable: a debugger can
upload a new script and run it, or attach to the one in flash. Connect from
VS Code with a configuration like these:

```json
{
	"type": "wrench",
	"request": "attach",
	"name": "Attach over serial",
	"program": "${workspaceFolder}/script/blink.w",
	"transport": "serial",
	"serialPort": "COM5",
	"baudRate": 115200
}
```

```json
{
	"type": "wrench",
	"request": "launch",
	"name": "Upload over WiFi",
	"program": "${workspaceFolder}/script/blink.w",
	"transport": "tcp",
	"host": "192.168.1.50",
	"port": 5050
}
```

or from the console:

```bash
wrench dbg -target serial:COM5:115200 -attach script/blink.w
wrench dbg -target tcp:192.168.1.50:5050 script/blink.w
```

Board specific points:

* Over serial, the port carries the debug protocol and nothing else should
  write to it. Script output reaches the debug console through
  `WrenchDebugServer::print()`. The protocol skips stray bytes such as boot
  messages, but on the ESP32 Arduino core set Tools, Core Debug Level to
  None, and with ESP-IDF move the console off UART0 or close
  `idf.py monitor` before debugging.
* On ESP32 and ESP8266 the Arduino examples enlarge the serial receive
  buffer with `Serial.setRxBufferSize( 2048 )` before `Serial.begin()`:
  the default 256 bytes are less than one frame, and an upload would
  overflow it while the sketch is busy in the script.
* Over WiFi the boards log their address at startup (`esp32_arduino` on
  the serial monitor; `esp32_idf` as "debug server at 192.168.1.50:5050"
  in `idf.py monitor`). The ESP-IDF example also registers the DHCP host
  name `wrench-target`, which many routers resolve, and turns WiFi power
  saving off, which otherwise delays every reply by up to a beacon
  interval.
* On AVR boards the debugger needs at least an ATmega2560; the
  `arduino_serial` configuration shrinks the server's buffers there. An
  Uno is too small.

## Transports

The server and the clients move bytes through a `WrenchDebugTransport`.
These implementations come with wrench:

| Transport | Side | Enabled by | Use |
|---|---|---|---|
| `WrenchDebugStreamTransport` | target | building for Arduino (`ARDUINO`) | any `Stream`: `Serial`, `Serial1`, `SerialUSB`, a connected `WiFiClient` |
| `WrenchDebugTcpServerTransport` | target | `WRENCH_WIN32_TCP` or `WRENCH_LINUX_TCP` | a desktop application or `wrench dbgserver`: listens on a port (`listen()`) or on one address and port (`listenOn()`), serves one client at a time |
| `WrenchDebugSerialTransport` | either | `WRENCH_WIN32_SERIAL` or `WRENCH_LINUX_SERIAL` | a serial port on a PC: `COM3` (also `COM10` and up), `/dev/ttyUSB0` |
| `WrenchDebugTcpClientTransport` | client | `WRENCH_WIN32_TCP` or `WRENCH_LINUX_TCP` | connects to a TCP target, used by the host tools |

The examples add their own: `UartDebugTransport` (ESP-IDF UART driver),
`TcpDebugTransport` (lwIP sockets), `WiFiDebugTransport` (Arduino
`WiFiServer`) and `StmUartDebugTransport` (STM32 HAL with an interrupt
ring). Writing one takes five functions, see
[Writing a transport](firmware.md#writing-a-transport).

`WrenchDebugSerialTransport` raises DTR and RTS when it opens a port, since
many USB serial bridges and native USB CDC devices only send while DTR is
set. On Linux it accepts the standard rates from 1200 to 115200 baud and
the higher ones the system defines (230400, 460800, 921600 and so on).

## Choosing serial or TCP

| | Serial (UART, USB CDC) | TCP (WiFi, Ethernet, localhost) |
|---|---|---|
| Setup | a cable, nothing on the network | a network connection and the device's address |
| Speed | 115200 baud moves about 11 KB/s; uploads and large variables take a moment | fast; WiFi power saving can add latency |
| Losing the link | a UART cannot tell: a halted target waits until a debugger comes back, unless `setLinkTimeout()` | the transport reports a closed connection, the target lets a halted script go at once |
| The port | dedicated to the debugger while it is in use | the serial port stays free for logs |
| Noise | frames with a bad checksum are dropped and the request is sent again | none |
| Who can connect | whoever has the cable | anyone on the network: listen on loopback or a trusted network |

Serial is the simplest start on any board and the only choice without a
network. TCP suits boards with WiFi, desktop applications, and long
sessions where the serial port is needed for logging. The client waits
3 s for each reply over TCP and resends twice; over serial it waits 2 s
and resends three times in `wrench dbg` and four times in VS Code
(`"timeout"` in the launch configuration changes the wait).

## Attaching to a running device

Attaching debugs what the device runs now, from wherever it is:

1. Connect with an attach configuration in VS Code, or
   `wrench dbg -target ... -attach [file.w]`.
2. Set breakpoints. The next time the firmware runs that line (its global
   code, or a function it calls) the script stops there.
3. Continue, step, inspect. Stopping or detaching clears the breakpoints
   and lets the script run on.

The device's program must be compiled with `-debug` or `-debugsource` to
stop anywhere. With a local `program` the debugger checks that it matches
the device's build and warns when not; lines may then point to the wrong
code. Without `program` (VS Code) or file (`wrench dbg`) the debugger
reads everything from the device, which needs `-debugsource`.

To catch a script from its very first line on a device that starts it by
itself, the firmware arms `breakOnEntry()` before it runs the script; the
script then waits, halted, until a debugger attaches. See
[Halting a script until a debugger attaches](firmware.md#halting-a-script-until-a-debugger-attaches).

## Debugging inside a desktop application

The most faithful way to debug a script written for an application is
inside that application, with its real natives and data. Build the
application with `WRENCH_INCLUDE_DEBUG_CODE` and `WRENCH_WIN32_TCP` (or
`WRENCH_LINUX_TCP`), give its state a debug server and attach to it over
TCP on localhost:

```cpp
#include "wrench.h"

static WrenchDebugTcpServerTransport debugLink;
static WrenchDebugServer* debugger = 0;
static WRContext* script = 0;

void startScripting( WRState* w, const unsigned char* code, int codeLen )
{
	debugLink.listen( 5050, true ); // loopback only
	debugger = new WrenchDebugServer( w, &debugLink );
	debugger->setHostDriven( true );

	// code compiled with WR_EMBED_DEBUG_CODE (and ideally WR_EMBED_SOURCE_CODE)
	script = wr_newContext( w, code, codeLen );
	debugger->attach( script, "main.w" );
	wr_executeContext( script );
}

// from the application's idle or timer handler, never inside a script callback
void onIdle()
{
	debugger->tick();
}
```

```json
{
	"type": "wrench",
	"request": "attach",
	"name": "Attach to the application",
	"program": "${workspaceFolder}/main.w",
	"transport": "tcp",
	"host": "127.0.0.1",
	"port": 5050
}
```

`listen( port, true )` accepts connections from the same machine only;
`listen( port )` accepts them on every interface, and
`listenOn( "192.168.1.20", port )` on one IPv4 address of the machine
(`"0.0.0.0"` for every interface). Each returns false when the port
cannot be bound. With MSVC the transport links `Ws2_32.lib` by itself; with MinGW add
`-lws2_32`.

While the debugger holds a script at a breakpoint, the thread that runs it
waits inside the VM. When that is the UI thread, the window stops painting
until you continue; run scripts on a thread of their own if the window must
stay alive, and keep the state, the context, the server and the transport
on that one thread (see [The one-task rule](firmware.md#the-one-task-rule)).
[Scenario 2](scenarios.md#2-a-desktop-application-with-wrenchjson) puts
this together with a `wrench.json`.
