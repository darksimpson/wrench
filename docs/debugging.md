# Debugging wrench scripts

Wrench scripts can be debugged at source level wherever they run: in a VM
on the PC, or on a device over a serial port or TCP. The same pieces are
used in every case: VS Code talks the Debug Adapter Protocol to
`wrench dap`, which (like the console debugger `wrench dbg`) drives a
`WrenchDebugClient`. The client talks the wrench debug protocol over UART,
TCP or an in-process pipe to a `WrenchDebugServer` that runs next to the VM
executing the script.

* **WrenchDebugServer** (target side, `debug/debug_server.cpp`) is small,
  plain C++98, and hooks into the VM through the `O_DebugInfo` line markers
  the compiler emits. It allocates its state once when it is created.
  After that, breakpoints, stepping, pausing and inspecting values allocate
  nothing; only an upload (a buffer of the program's size, then a context
  for it), launching a program that already ran (a fresh context) and
  setting a variable to a string (the string) do.
* **WrenchDebugClient** (host side, `debug/debug_client.cpp`) speaks the
  protocol, reads the program's symbols and builds its line table.
* **wrench dbg**, **wrench dap** and **wrench lsp** are host tools built on
  top of it; `wrench dbgserver` turns a PC into a debug target.

## Building

Debug support is controlled by two defines:

| Define | What it builds | Where |
|---|---|---|
| `WRENCH_INCLUDE_DEBUG_CODE` | VM hooks and `WrenchDebugServer` | targets, any compiler |
| `WRENCH_INCLUDE_DEBUG_CLIENT` | client, evaluator, host tools | PCs (C++17, standard library), builds from `src/` only |

The `win32/wrench.sln` project and the GNU makefile define both. Embedded
projects only need the first, typically through a `wrench_config.h` next to
`wrench.h`.

The host side exists only in builds from the sources in `src/`: the single-file
build (`wrench.h` and `wrench.cpp` from a release, or `make dist`)
leaves `debug/debug_client.h`, `debug/debug_client.cpp` and the host tools
out by design. It carries the server, the transports and the protocol code;
defining `WRENCH_INCLUDE_DEBUG_CLIENT` with it only switches on the
transports and the protocol, there is no `WrenchDebugClient` in it. Build
host tools from `src/`.

Scripts are debuggable when compiled with debug information:

* `wr_compile(..., WR_INCLUDE_GLOBALS | WR_EMBED_DEBUG_CODE)` adds line
  markers and symbol names.
* `WR_EMBED_SOURCE_CODE` additionally embeds the source text, so a debugger
  can attach to a device without the original files.
* On the command line: `wrench c -debug file.w file.bin` or
  `wrench c -debugsource file.w file.bin` (`ch` makes a C header instead).

Code without debug information runs at full speed even with the server
present. With it, each source line costs one extra dispatch and a couple of
compares while no breakpoint is active.

## Debugging from VS Code

Install the extension from `tools/vscode-wrench` (see its README:
`npm run bundle-exe` copies the `wrench` executables into the extension,
`npm run package` then builds the `.vsix` from what is there; it does not
build or bundle executables itself). The bundled builds are a 32-bit
Windows executable (`bin/win32-ia32`, it runs on 64-bit Windows too) and a
Linux x86-64 one (`bin/linux-x64`). Then press F5 in a `.w` file, or add
configurations to `launch.json`:

```json
{
	"type": "wrench",
	"request": "launch",
	"name": "Debug locally",
	"program": "${file}",
	"stopOnEntry": false
}
```

| Setting | Meaning |
|---|---|
| `program` | the `.w` file, or a `.bin` compiled with `-debugsource` (see below); default `${file}` |
| `request: attach` | debug what the target runs, no upload; `program` may be omitted to use the source embedded in the target's bytecode |
| `stopOnEntry` | stop on the first line (launch); pause as soon as the target executes script code (attach). Default false |
| `transport` | `local` (default for launch), `tcp` (default for attach) or `serial` |
| `host`, `port` | TCP target, default `127.0.0.1`, 5050 |
| `serialPort`, `baudRate` | serial target, e.g. `COM5` / `/dev/ttyUSB0`, default 115200 |
| `target` | shorthand for the above: `local`, `tcp:host:port`, `serial:port[:baud]` |
| `upload` | launch only: send the freshly compiled program before running (default true) |
| `project` | the `wrench.json` describing the host application; empty (default) uses the nearest one above the program, `"none"` ignores it (see below) |
| `timeout` | ms to wait for each reply from the target before resending (default 3000, on a serial link 2000 with more retries), raise it for slow links |
| `connectTimeout` | ms to wait for a TCP connection (default 5000) |
| `trace` | log the Debug Adapter Protocol traffic to the adapter's stderr (default false) |
| `noDebug` | set by VS Code for Run Without Debugging (Ctrl+F5): breakpoints are not sent, `stopOnEntry` is ignored and a runtime error does not halt, it is reported when the program ends |

Features: breakpoints (moved to the next line with code), conditional
breakpoints (`n > 3 && name == "x"`), hit counts (`5`, `>= 10`, `% 3`),
logpoints (`value is {v}`), break on runtime errors with the stack intact,
pause, step in / over / out, call stack, locals and globals with lazily
expanded arrays, hash tables and structs, editing variables, watch and hover
expressions (`a[2]`, `p.member`, `h["key"]`, arithmetic and comparisons),
assignments in the debug console (`x = 5`), completion in the debug console.

Script output from `print` / `println` appears in the debug console. On a
device, route it there with `WrenchDebugServer::print()`.

## Debugging without the .w files

A program compiled with `-debugsource` carries its source (`wrench c
-debugsource main.w main.bin`, `wrench ch -debugsource ...`, or
`WR_EMBED_SOURCE_CODE` for `wr_compile`). Two ways to debug it when the
`.w` files are not at hand:

- From the target: attach without `program` (VS Code) or without a file
  (`wrench dbg -attach -target ...`). The debugger reads bytecode, symbols
  and source over the debug link and shows the source as a read-only
  document. Lines always match, it is exactly what the device runs.
- From a local copy: give the `.bin` as `program` (VS Code) or as the file
  (`wrench dbg main.bin`). It is used as it is: `launch` uploads it
  unchanged, `attach` compares it with what the target runs, the source
  shown is the embedded one.

Breakpoints, stepping and evaluation work the same as with a `.w` file. A
program compiled with `-debug` only still gives stacks and variables but no
source. Without debug information at all it runs, but cannot stop.

## Debugging from the console

Run in the local VM, upload and run on a device, debug what a device
already runs, and the same without the source file (see above):

```
wrench dbg file.w
wrench dbg -target tcp:192.168.1.50:5050 file.w
wrench dbg -target serial:COM5:115200 -attach file.w
wrench dbg file.bin
wrench dbg -target tcp:192.168.1.50:5050 -attach
```

Commands: `r` run/restart, `c` continue, `n` next, `s` step in, `o` step
out, `b <line>` toggle breakpoint, `bt` call stack, `f <n>` select frame,
`l [line]` list, `p <expr>` print, `set <var> = <value>`, `locals`,
`globals`, `d <expr>` display at every stop, `x` hex, `kill`, `q`. Ctrl+C
pauses a running program.

## PC as a target

```
wrench dbgserver -target tcp:5050 [-v] [-noupload] [-hold] [file.w | file.bin]
wrench dbgserver -target serial:COM9:115200 file.w
```

The server preloads the file if given (a client can still upload another
program unless `-noupload` is given), executes launches, and accepts a new
client after a disconnect. `-hold` starts the preloaded program right away,
halted on its first line until a debugger attaches (see below).

Without a `wrench.json` (see "Scripts for a host application" below), the
server stands in for a generic device: native functions a device script
expects but the PC lacks return 0 and are reported once in the console.
With one, it imitates the application it describes instead.

## Halting a script until a debugger attaches

To debug a script from its very first line on a device that starts it by
itself, arm the server before running the script:

```cpp
WRContext* context = debugger->loadBytes( bytecode, bytecodeSize );
debugger->breakOnEntry(); // or breakOnEntry( 30000 ) to give up after 30 s
wr_executeContext( context );
```

The next script code that runs, the global code here or any function the
firmware calls, stops on its first line. Without a debugger attached it
waits there and keeps servicing the link until one connects. The debugger
then finds the script halted on entry, can set breakpoints, and continues it
whenever you are ready. Attach from VS Code (`"request": "attach"`, with or
without `program`) or with `wrench dbg -attach`. With a timeout the script
runs on when nobody attached in time. It applies once; arm it again for the
next time. Only code compiled with debug information can halt.

While it waits, the firmware is blocked inside `wr_executeContext` (or
`wr_callFunction`), as at any breakpoint; `WrenchDebugTransport::idle()`
runs meanwhile. `wrench dbgserver -hold file.w` does the same on a PC.

## Refusing uploads

A debugger may send a new program, which replaces the one the target runs
until the next reset. Firmware that must only run its own program (a field
build that still keeps the debugger for diagnosis) calls

```cpp
debugger->setAllowUpload( false );
```

The server then answers upload requests with "uploads disabled" and no
longer advertises the capability. Everything else keeps working: attach,
breakpoints, stepping, variables, and launching the loaded program again.
VS Code reports a `launch` that would upload with a hint to use `attach`
or `"upload": false`; `wrench dbg` suggests `-attach`. `wrench dbgserver
-noupload` behaves the same way for trying it out on a PC.

## Scripts for a host application (wrench.json)

An application that embeds wrench registers its own natives and often loads
only some standard libraries, or none. Describe that in a `wrench.json` next
to the scripts (any folder above them works too):

```json
{
	"libraries": [ "math", "string" ],
	"functions": [
		{ "name": "openDoc", "params": [ "path", "mode?" ], "doc": "Opens a document, returns its handle", "returns": 1 },
		{ "name": "ui::alert", "params": [ "text" ], "doc": "Shows a message box" },
		"log( values... )"
	],
	"undeclaredCalls": "warning"
}
```

- `libraries`: what the application loads, out of math, std, io, string,
  msg, sys, serialize, debug, tcp and container. Missing or `"all"` means
  `wr_loadAllLibs`.
- `functions`: natives registered with `wr_registerFunction` (`name`) or
  `wr_registerLibraryFunction` (`lib::name`), as objects or signature
  strings. A parameter ending in `?` is optional, one ending in `...` takes
  any number of arguments; without `params` the argument count is not
  checked. `returns` is the value the local debug target answers instead
  (default 0). Also list functions of modules the scripts import at run
  time, and `print` / `println` if the application has them (the wrench
  tools provide those, an application does not have to).
- `undeclaredCalls`: `warning` (default), `error` or `ignore`.

What uses it:

- The language server completes and documents the declared functions,
  goes to their declaration, shows parameter hints, and leaves out the
  libraries the application does not load. It warns about calls to
  functions that are neither declared nor defined in the script, calls of
  library functions the application does not load, and wrong argument
  counts. VS Code validates `wrench.json` itself and creates one with
  **Wrench: Describe the Host Application**.
- The local debug target (`wrench dap` launches with `"transport": "local"`
  and `wrench dbg`) loads only the listed libraries and answers declared
  natives with their `returns` value, saying once per function that it
  stands in for the application. Script logic can be stepped through
  without the application. Anything else fails with
  `WR_ERR_function_not_found` just as it would in the application.
- `wrench dbgserver` does the same when started for a script with a
  `wrench.json`.
- The launch option `"project"` (`-project <file>` on the command line)
  names a specific file, `"none"` ignores it.

Whatever the target, a runtime error about a missing function names it:
"'ui::alert' is not provided by the target". The target sends the hash of
the name and the debugger looks it up in the source and in `wrench.json`.

The most faithful way to debug is still inside the application itself:
build it with `WRENCH_INCLUDE_DEBUG_CODE` and `WRENCH_WIN32_TCP` (or
`WRENCH_LINUX_TCP`), give its state a debug server and attach to it:

```cpp
static WrenchDebugTcpServerTransport link;
link.listen( 5050, true ); // loopback only
static WrenchDebugServer debugger( w, &link );
debugger.setHostDriven( true );
WRContext* context = debugger.loadBytes( code, codeLen ); // compiled with WR_EMBED_DEBUG_CODE

// in the idle or timer handler, never inside a script callback
debugger.tick();
```

```json
{ "type": "wrench", "request": "attach", "program": "${workspaceFolder}/main.w", "transport": "tcp", "host": "127.0.0.1", "port": 5050 }
```

While the debugger holds a script at a breakpoint the thread running it
waits in the VM, so run scripts off the UI thread if the window must keep
painting.

Host code can use the same information: after `WR_ERR_function_not_found`
or `WR_ERR_lib_function_not_found`, `wr_getMissingFunctionHash()` returns
`wr_hashStr()` of the name that was called. A not-found handler
(`wr_setOnCallbackNotFound`, `wr_setOnLibCallbackNotFound`) may answer some
names and still fail the call for others by setting `w->err`.

## Targets

See [examples/debug_targets](../examples/debug_targets/README.md) for
Arduino, ESP32 (WiFi and ESP-IDF) and STM32 HAL integrations and for the
`WrenchDebugTransport` interface.

When the debugger goes away, the server clears the breakpoints and lets a
halted script run on (or terminates it after `setAbortOnDisconnect( true )`),
but only once it knows: after a Detach, or when the transport reports the
lost link (`read()` returns -1 or `connected()` turns false, as TCP does).
A UART cannot tell. By default, if the host crashes or the cable is pulled
while the script is halted, it stays halted, servicing the link, until a
debugger connects again. `WrenchDebugServer::setLinkTimeout( ms )` (0, the
default, waits forever) makes a halted target let go of a client it has not
heard from for that long, as if it had disconnected. `wrench dap` (VS Code)
pings a halted target every second to stay within it; `wrench dbg` does
not, so use a generous value, or none, with the console debugger.

While script code runs, the server polls the link every
`WRENCH_DEBUG_POLL_INTERVAL` source lines (64 by default), so a pause
request reaches even an endless loop. That polling does not yield or call
`WrenchDebugTransport::idle()`, which only runs while the script is halted:
watchdogs and task scheduling on the target see a long running script call
just as they would without a debugger.

## The wire protocol

Version 2. All integers are little endian.

### Framing

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | 0xA5 |
| 1 | 1 | 0x57 |
| 2 | 1 | message type |
| 3 | 1 | sequence number: requests 1..255, replies echo it, events 0 |
| 4 | 2 | payload length |
| 6 | n | payload |
| 6+n | 2 | CRC16-CCITT (poly 0x1021, init 0xFFFF) over bytes 2 .. 6+n-1 |

Receivers hunt for the sync bytes, drop frames longer than their buffer or
with a bad CRC, and abandon a frame that stalls for 500 ms. A lost reply is
recovered by the client resending the request with the same sequence number.
A control request (launch, continue, pause, steps, terminate, load end,
detach) that repeats the request right before it, same type and sequence
number, is a retransmission: the server answers it with the first reply
again (ok or the same error) without acting twice.

### Requests (client to server)

| Type | Name | Payload | Reply payload |
|---|---|---|---|
| 0x01 | Hello | u16 protocol version | u16 version, u8 major, u8 minor, u8 build, u16 max payload, u8 capabilities, u16 max breakpoints |
| 0x02 | GetInfo | | u8 state, u32 program hash, u32 bytecode length, u8 compiler flags, u32 symbols length, u32 source length, u16 globals, u8 functions, i32 last line |
| 0x03 | LoadBegin | u32 size | |
| 0x04 | LoadData | u32 offset, bytes | |
| 0x05 | LoadEnd | | u32 program hash |
| 0x06 | ReadProgram | u8 section (0 bytecode, 1 symbols, 2 source), u32 offset, u16 max | u32 total, u16 n, n bytes |
| 0x07 | SetBreakpoints | u16 count, i32 line x count (replaces all) | |
| 0x08 | Launch | u8 flags (1 = stop on entry) | |
| 0x09 | Continue | optional u8 flags (1 = carry on with the step a breakpoint interrupted) | |
| 0x0A | Pause | | |
| 0x0B | StepIn | | |
| 0x0C | StepOver | | |
| 0x0D | StepOut | | |
| 0x0E | Terminate | | |
| 0x10 | GetStack | u16 first, u16 max | u16 total, u16 n, n x (u8 kind, u8 function, u32 pc offset, u16 frame base, u16 slots) |
| 0x11 | GetLocals | u16 frame, u16 first, u16 count | u16 total, u16 n, n values |
| 0x12 | GetGlobals | u16 first, u16 count | u16 total, u16 n, n values |
| 0x13 | GetChildren | u32 handle, u32 first, u16 count | u32 total, u16 n, children |
| 0x14 | SetLocal | u16 frame, u16 slot, value | new value |
| 0x15 | SetGlobal | u16 slot, value | new value |
| 0x16 | SetChild | u32 handle, u32 index, value | new value |
| 0x17 | Ping | | |
| 0x18 | Detach | | clears breakpoints, resumes, ends the session |

Replies are type 0x80 (ok, payload as listed) or 0x81 (error, u8 code: 1
unknown request, 2 bad request, 3 no program, 4 not stopped, 5 bad frame, 6
bad slot, 7 bad handle, 8 no memory, 9 bad bytecode, 10 too many
breakpoints, 11 busy, 12 version, 13 unsupported, 14 uploads disabled by
the firmware).

A session starts with Hello. Until the server has accepted one, it answers
every request other than Hello and Ping with error 2 (bad request); the
exception is a retransmitted control request (for example a resent Detach
that ended the previous session), which gets its first answer again. A Hello
with a protocol version other than the server's gets error 12; an accepted
one clears the breakpoints and any step in progress. Detach, a link the
transport reports as lost, or, while halted, silence longer than the
firmware's link timeout (`setLinkTimeout`, off by default; any bytes
received, a Ping for example, restart it) ends the session.

Capabilities in the Hello reply (u8, bits):

| Bit | Value | Meaning |
|---|---|---|
| 0 | 0x01 | load: accepts uploaded bytecode (LoadBegin / LoadData / LoadEnd); left out when the firmware called `setAllowUpload( false )` |
| 1 | 0x02 | launch: can execute the program itself (always set) |
| 2 | 0x04 | set value: accepts SetLocal / SetGlobal / SetChild (always set) |
| 3 | 0x08 | host driven: the firmware calls into the script itself (`setHostDriven( true )`), so the session goes on after the global code ends |

State in the GetInfo reply:

| Value | State |
|---|---|
| 0 | no program |
| 1 | loaded: a program is present, not executing |
| 2 | running: executing inside the VM |
| 3 | stopped: halted inside the VM, waiting for the client |
| 4 | ended: the last launch finished |

The compiler flags byte in GetInfo is the third byte of the bytecode, the
`WrenchCompilerFlags` (`wrench.h`) the program was compiled with:

| Bit | Flag | Meaning |
|---|---|---|
| 0x01 | `WR_INCLUDE_GLOBALS` | a table of global name hashes (for `wr_getGlobalRef()`) |
| 0x02 | `WR_EMBED_DEBUG_CODE` | line markers and the symbols section; without it the program cannot stop |
| 0x04 | `WR_EMBED_SOURCE_CODE` | the symbols section and the source text (ReadProgram section 2) |
| 0x08 | `WR_NON_STRICT_VAR` | compiled without strict `var` (`-nostrict`) |

Stack frames are innermost first. Kind 0 is global code, kind 1 a function
whose index refers to the bytecode's function table; the client maps pc
offsets to lines with its line table. GetGlobals and GetChildren also work
while the program is not running.

### Values

A value is a u8 tag followed by its data:

| Tag | Kind | Data |
|---|---|---|
| 0 | int | i32 |
| 1 | float | f32 bits |
| 2 | string | u32 handle, u32 length, u16 n, n bytes (a prefix, fetch the rest with GetChildren) |
| 3 | array | u32 handle, u32 count |
| 4 | hash | u32 handle, u32 count |
| 5 | struct | u32 handle, u32 count |
| 6 | raw | u32 handle, u32 size (native byte array) |
| 7 | other | u8 type |

Handles stay valid until the program resumes. Children of an array are
values; of a struct `u32 member hash, value` (names come from the symbols);
of a hash table `key value, value`; of a string or raw array plain bytes.
Values sent to the target (Set*) are int, float or string (`u16 n, bytes`).

### Events (server to client, sequence 0)

| Type | Name | Payload |
|---|---|---|
| 0xC0 | Stopped | u8 reason (0 entry, 1 breakpoint, 2 step, 3 pause, 4 error), i32 line, u8 wrench error, u32 hash of the missing function name (function_not_found errors, else 0) |
| 0xC1 | Output | text |
| 0xC2 | Ended | u8 wrench error, u8 flags (1 aborted, 2 host keeps running), i32 last line, u32 missing function hash (as for Stopped), value |
| 0xC3 | Loaded | u32 program hash |

Events are not acknowledged. So that a lost Stopped event (or a debugger that
attaches to a target already halted) cannot leave the client believing the
program runs, a halted server sends Stopped again, with the original reason,
line and error, after answering a Hello or a Pause.

### Line markers

The compiler emits `O_DebugInfo` followed by a 16 bit codeword; the top two
bits are the type. `0x4000 | line` marks the start of a line, `0xC000 |
high` carries bits 14 and up of the next line number for sources longer
than 16383 lines, `0x0000 | n` and `0x8000` are older call markers the
server ignores. Stepping compares frame depth (from the VM's frame base), so
it works for code entered through `wr_callFunction` from native code just
like for calls inside the script.
