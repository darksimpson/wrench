# Troubleshooting

Symptoms, the messages that come with them, and what to do. The messages
are quoted as `wrench dbg` and VS Code print them; most come from the
same client code, so both front ends say the same.

## The debugger cannot connect

```text
cannot connect to tcp 192.168.1.50:5050
cannot open serial COM5 @115200
```

The PC could not open the link at all. For TCP: wrong address or port,
the board is not on the network yet (the WiFi examples log their address
when they get one), a firewall, or another debugger is connected (the
ready-made TCP transports serve one client at a time). For serial: wrong
port name, or another program holds the port. Close serial monitors
(`idf.py monitor`, the Arduino serial monitor) before debugging.

```text
no answer from target (serial COM5 @115200): target did not answer
no answer from the wrench target on serial COM5 @115200: target did not answer
```

The link opened, but nothing answered the hello (the first line is
`wrench dbg`, the second VS Code). The client sends it at least four
times, waiting 2 s (serial) or 3 s (TCP, or the `"timeout"` of the launch
configuration) each time. Causes:

* **Nothing services the link.** The firmware must call `tick()` whenever
  no script runs. Firmware that blocks between calls (a queue wait without
  timeout, a long `delay()`) or a native that blocks without `poll()` is
  deaf meanwhile. See [Blocking code, yield and polling](blocking.md).
* **Code without debug information is running.** It has no line markers,
  so nothing polls while it runs; a long loop in it keeps the debugger out
  until it ends.
* **Wrong baud rate**, or a serial port that carries something else: the
  ESP-IDF console on UART0, core debug output on the ESP32 Arduino core,
  your own `Serial.print()`. The protocol skips stray bytes, but a port
  that prints constantly or runs at a different speed never delivers a
  frame.
* **The board reset when the port opened** and is still booting. The PC
  serial transport raises DTR and RTS when it opens the port, and boards
  with an auto-reset circuit on those lines (classic Arduino boards reset
  on DTR) restart; the hello is resent, give it a moment or connect
  again. Firmware that should then halt for the debugger can use
  `breakOnEntry()` with a timeout.
* **The firmware was built without `WRENCH_INCLUDE_DEBUG_CODE`**, or never
  created the server.

```text
debug protocol version mismatch
```

The target speaks another protocol version: the firmware and the PC tools
come from different wrench versions. Rebuild one of them.

## Launch and attach are refused

```text
the target runs its own program and takes no launches (its firmware did not call setLaunchable(true)), use -attach
```

In VS Code: "...: use "request": "attach" to debug it". The firmware does
not carry out launches; attach instead, or add the launch handling to the
firmware ([setLaunchable()](firmware.md#letting-a-debugger-launch-and-upload-setlaunchable)).
A target that does not advertise launches answers a launch request with
"not supported by the target".

```text
the target does not accept uploaded programs (its firmware called setAllowUpload(false)), use -attach
```

In VS Code: "...: use "request": "attach" to debug the program it runs, or
"upload": false to launch that program again". The firmware refuses
uploads on purpose. Attach, or launch with `"upload": false` to restart
the program it holds.

```text
launch failed: no program loaded on the target
```

The firmware attached no context and nothing was uploaded: there is no
program 0 to run.

```text
attach over tcp needs the "port" attribute: the tcp port the target listens on (e.g. 5050), or a "target" like "tcp:127.0.0.1:5050"
launch over serial needs the "serialPort" attribute (e.g. COM3 or /dev/ttyUSB0)
attach needs a tcp or serial target
expected tcp:<host>:<port> or tcp:<port>
launch needs "program", a .w file or a .bin compiled with -debugsource
```

VS Code configuration errors: a TCP configuration needs `"port"` (the host
defaults to `127.0.0.1`, and an attach to `"tcp"`), a serial one
`"serialPort"`; an attach cannot use the local VM; a `"target"` string must
have one of the forms in [Target strings](../vscode/launch.md#target-strings);
and a launch needs `"program"`.

## Target is busy executing

```text
upload failed: target is busy executing
launch failed: target is busy executing
```

A launch or an upload must be carried out by the firmware, between script
calls. While script code runs, or is halted at a breakpoint, the firmware
is inside the VM and the target refuses both. Typical cases:

* The firmware runs a long call (a busy handler, a native that waits and
  polls). Terminate it first, or try again when it returned.
* A breakpoint in a function the firmware calls often hits again between
  the terminate and the launch. Remove the breakpoint or continue, then
  run again. (`r` in `wrench dbg` takes its breakpoints off the target
  before it terminates, and asks again for up to three seconds while the
  target is busy.)

## Breakpoints

| Message (VS Code: hover over the breakpoint) | Cause and fix |
|---|---|
| no code at or after this line | nothing from this line to the end of the file has code; move it |
| only the programs being debugged and the files they include can hold breakpoints | the file is in no program the target debugs: another project's file, an include of a different program, or a stale copy |
| the module is not loaded now | the file belongs to a module that is not imported at the moment; it binds once the module is loaded (see [Imported modules](programs.md#imported-modules)) |
| the target has no room for more breakpoints | more than `WRENCH_DEBUG_MAX_BREAKPOINTS` (32) over all programs; remove some, or raise the limit in the firmware |

`wrench dbg` says "no code at or after line 37" and "breakpoints not set:
too many breakpoints for the target".

A breakpoint that is verified but never hits:

* The program was compiled without debug information. The debuggers warn:
  "the program was compiled without debug information (-debug or
  -debugsource): it runs, but cannot stop at breakpoints or step".
* The code never runs: the firmware does not call that function, or calls
  it in another context than the one you think (each attached context is a
  program of its own).
* The target runs a different build, so the line is somewhere else in its
  program (see the next section).
* In `wrench dbg`, `b 23` while stopped in an included file means line 23
  of that file. Select a frame of the main file with `f`, or write
  `b main.w:23`.

## The target runs a different build

```text
warning: the target runs a different build of blink.w than the local one
Warning: the target runs a different build than D:\fw\script\blink.w, lines may not match.
```

An attach compares the local file, compiled the way the target's program
was compiled, with what the target runs. They differ when the file was
edited after the firmware was built, or the firmware holds a program
uploaded earlier. Breakpoints may then sit on the wrong lines. Rebuild and
flash, launch to upload the local build, or attach without a file to
debug from the source embedded in the target (needs `-debugsource`).

```text
upload failed: target rejected the bytecode
```

The uploaded program failed the target's check: it was compiled by a
different wrench version than the firmware's, or the transfer was damaged.

## Stops come late, or not at all

* **A pause or a stop only happens after a native returns.** Inside a
  native nothing polls; a native that blocks for long should call
  `poll()` while it waits. Even then the pause lands on the next line
  after the native returns. See [Natives that block](blocking.md#natives-that-block).
* **A loop cannot be paused** and requests time out while it runs: the loop
  was compiled without debug information. In `wrench dbg`, a second Ctrl+C
  gives the prompt back; in VS Code the pause fails with "target did not
  answer".
* **Step Out never ends** in a function the firmware called: there is no
  script caller to return to. Continue or pause; see
  [Steps across calls from the firmware](breakpoints.md#steps-across-calls-from-the-firmware).
* **Step Over skips the rest of a line** and lands on the next line or the
  loop condition: a step that returns from a function ends at the caller's
  next line marker, which comes after the call.
* **Conditional breakpoints and logpoints slow the program down**: every
  hit halts the target for a round trip, also when the condition is false.

## The session ends too early

* **The session ends when the global code ends**, although the firmware
  goes on calling into the script: the firmware did not call
  `setHostDriven( true )`.
* **"Lost the connection to the target."** (VS Code) or
  **"[target disconnected]"** (`wrench dbg`): the TCP connection closed or
  the serial port went away. The target lets a halted script run on when
  its transport reports the loss.

## The target stays halted after the debugger is gone

Over a UART the target cannot tell that the PC crashed or the cable was
pulled. A halted script stays halted, servicing the link, until a debugger
connects again (connect and continue, or detach). Firmware for unattended
devices sets a link timeout: `setLinkTimeout( 5000 )` lets a halted
script go after five seconds without a byte from the debugger. Both
debuggers ping a halted remote target every second, so a session never
runs into it. See [When the debugger goes away](firmware.md#when-the-debugger-goes-away).

## Watchdog resets

* **While halted at a breakpoint**: the server loops calling the
  transport's `idle()`. Feed the watchdog there, or let the idle task run
  (`vTaskDelay( 1 )` on FreeRTOS, `delay( 1 )` on the ESP32 Arduino core).
  The examples do this.
* **While a script runs long**: the debugger changes nothing here; a long
  call trips the watchdog as it would without a debugger. Split the work,
  use `yield()` or time slices, or configure the watchdog
  ([Watchdogs](firmware.md#watchdogs-and-other-tasks)).

## Serial links lose data

Symptoms: uploads fail with "target did not answer" halfway, large
variables take long or fail, requests are resent.

* **The receive buffer is smaller than a frame.** A frame is up to
  `WRENCH_DEBUG_MAX_PAYLOAD + 8` bytes, and the firmware may be busy in a
  script while one arrives. On ESP32 and ESP8266 call
  `Serial.setRxBufferSize( 2048 )` before `Serial.begin()`; on AVR and
  STM32duino enlarge `SERIAL_RX_BUFFER_SIZE`; on a bare UART receive into
  an interrupt driven ring (`examples/debug_targets/stm32_hal`).
* **Or make frames smaller**: `WRENCH_DEBUG_MAX_PAYLOAD 128` or 256 in
  `wrench_config.h`. The client adapts to the target's size.
* **The link is slow**: raise `"timeout"` in the VS Code launch
  configuration for slow or busy links.

Frames with a bad checksum are dropped and the request is sent again, so
occasional noise costs time, not correctness.

## Variables and expressions

| Message | Meaning |
|---|---|
| target is not stopped | stacks and locals need a halted program; globals need one that is halted or not running |
| the program is running, pause it first | the VS Code debug console only evaluates while stopped |
| value is no longer valid | the program ran since the value was read, or more containers were opened in this stop than `WRENCH_DEBUG_MAX_HANDLES` (64) |
| no variable named 'x' | not a local of the selected frame nor a global of its program; a private global needs `::x@file.w` from other files |
| only numbers and strings can be assigned | the new value of a `set` or an assignment is a container |
| target is out of memory | the target could not allocate the upload buffer or a string being set |

## Missing functions

```text
*** runtime error WR_ERR_function_not_found at line 2
*** 'led' is not provided by the target
```

The script called a native the target does not register. The debugger
names it by looking up the hash the target sends in the script's source
and in `wrench.json`; "(its 'math' library is not loaded there)" is added
when the function belongs to a library the target does not load. A name it
cannot find shows as "the function with name hash 0x... is not provided
by the target". On the local VM, declare the application's natives in
[`wrench.json`](../vscode/wrench-json.md); on a device, register them in
the firmware.
