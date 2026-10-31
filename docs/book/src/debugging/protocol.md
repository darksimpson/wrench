# Wire protocol reference

This page specifies the protocol between a debug client (`wrench dap`,
`wrench dbg`, or your own) and a `WrenchDebugServer`: framing, sequence
numbers and retransmission, every request with its reply, the events, and
the encodings of values, programs and watch paths. You need it to write a
client or a server of your own, or to read a capture of a link. The
definitions are in `src/debug/debug_protocol.h`, the server in
`src/debug/debug_server.cpp`.

The protocol is version 2. All integers are little endian. Requests go
from the client to the server, which answers each one; events go from the
server to the client unasked.

## Framing

Every message travels in a frame:

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | 0xA5 |
| 1 | 1 | 0x57 (`W`) |
| 2 | 1 | message type |
| 3 | 1 | sequence number: requests 1 to 255, replies echo the request's, events 0 |
| 4 | 2 | payload length n |
| 6 | n | payload |
| 6+n | 2 | CRC16-CCITT (polynomial 0x1021, initial value 0xFFFF) over bytes 2 to 6+n-1: type, sequence, length and payload |

A frame has 8 bytes of overhead. Receivers hunt for the two sync bytes, so
other output on the line (boot messages, logs) is skipped and a damaged
frame costs at most that frame. A receiver drops frames longer than its
buffer or with a bad CRC, and abandons a partial frame when no byte has
arrived for 500 ms.

The largest payload either side sends is the server's
`WRENCH_DEBUG_MAX_PAYLOAD` (1024 by default), which the Hello reply
reports; the client keeps its requests and read sizes within it.

## Sequence numbers and retransmission

The client numbers its requests 1 to 255 and wraps around, skipping 0. It
waits for the reply with the same number; when none comes in time (3 s on
TCP, 2 s on serial by default) it sends the same frame again, with the same
number, a few times before it gives up ("target did not answer").

A lost request is sent again. A lost reply is the delicate case: the
request was carried out. Control requests must not act twice, so the server
recognises a retransmission: a control request (Launch, Continue, Pause,
StepIn, StepOver, StepOut, Terminate, LoadEnd, Detach) with the same type
and sequence number as the request right before it is answered with the
first reply again (ok, or the same error code) without acting. Any other
request in between makes it a new one. Other requests read state or
replace it as a whole (SetBreakpoints), so doing them twice is harmless.

## Sessions

A session starts with Hello. Until the server has accepted one it answers
every request except Hello and Ping with error 2 (bad request); a
retransmitted control request still gets its first answer again (a
resent Detach that ended the previous session, for example). A Hello with
another protocol version gets error 12. An accepted Hello clears the
breakpoints, the watches and any step in progress, and starts the session.

The session ends with:

* Detach: breakpoints and watches are cleared, a halted script resumes;
* a link the transport reports as lost (`read()` returning -1,
  `connected()` false);
* while the script is halted, silence longer than the firmware's link
  timeout (`setLinkTimeout()`, off by default); any bytes received, a Ping
  for example, restart it.

After a lost link or a timeout the server behaves as after a Detach, and
terminates the running or halted script instead of resuming it when the
firmware called `setAbortOnDisconnect( true )`.

## Requests

| Type | Name | Payload | Reply payload |
|---|---|---|---|
| 0x01 | Hello | u16 protocol version | u16 version, u8 wrench major, u8 minor, u8 build, u16 max payload, u8 capabilities, u16 max breakpoints, u8 max watches, u8 max programs |
| 0x02 | GetInfo | | u8 state, then program 0: u32 program hash, u32 bytecode length, u8 compiler flags, u32 symbols length, u32 source length, u16 globals, u8 functions; then i32 last line |
| 0x03 | LoadBegin | u32 size | |
| 0x04 | LoadData | u32 offset, bytes | |
| 0x05 | LoadEnd | | u32 program hash |
| 0x06 | ReadProgram | u8 program, u8 section (0 bytecode, 1 symbols, 2 source), u32 offset, u16 max | u32 total, u16 n, n bytes |
| 0x07 | SetBreakpoints | u8 program, u16 count, count x i32 line; or 0xFF, u32 program hash, u16 count, count x i32 line | |
| 0x08 | Launch | u8 flags (1 stop on entry) | |
| 0x09 | Continue | optional u8 flags (1 carry on with the step a breakpoint interrupted) | |
| 0x0A | Pause | | |
| 0x0B | StepIn | | |
| 0x0C | StepOver | | |
| 0x0D | StepOut | | |
| 0x0E | Terminate | | |
| 0x10 | GetStack | u16 first, u16 max | u16 total, u16 n, n frames |
| 0x11 | GetLocals | u16 frame, u16 first, u16 count | u16 total, u16 n, n values |
| 0x12 | GetGlobals | u8 program, u16 first, u16 count | u16 total, u16 n, n values |
| 0x13 | GetChildren | u32 handle, u32 first, u16 count | u32 total, u16 n, n children |
| 0x14 | SetLocal | u16 frame, u16 slot, value | the new value |
| 0x15 | SetGlobal | u8 program, u16 slot, value | the new value |
| 0x16 | SetChild | u32 handle, u32 index, value | the new value |
| 0x17 | Ping | | |
| 0x18 | Detach | | |
| 0x19 | AddWatch | u8 root (0 global, 1 local), u8 program, u16 frame, u16 slot, u8 n, n x (u8 kind, u32 value) | u8 watch id |
| 0x1A | RemoveWatch | u8 watch id (0 removes all) | |
| 0x1B | GetPrograms | | u8 n, n programs |

Replies have type 0x80 (ok, with the payload listed) or 0x81 (error, one
byte: the error code).

### Details

**Hello.** The capabilities byte is described [below](#capabilities). A
server that is halted when a Hello arrives sends the Stopped event again
after the reply, so a client that attaches to a halted target learns where
it is.

**GetInfo.** The state is one of the [states](#states); "last line" is the
line of the last line marker executed, -1 before any. GetPrograms gives
the same information for every program.

**LoadBegin, LoadData, LoadEnd: uploads.** The client sends the size,
then the bytes in chunks with their offsets (at most the max payload minus
4 per chunk), then LoadEnd. The server keeps them in a buffer from
`wr_malloc()`; LoadBegin of a transfer in progress replaces it. LoadEnd
checks the bytecode (`wr_isBytecodeValid()`), stops debugging the old
program 0 at once (its breakpoints and watches go), keeps the bytes for the
firmware, replies with the program's hash, and makes the next `tick()`
return `WRD_TickProgramUploaded`. The firmware takes the bytes with
`takeUpload()`; the next context it attaches becomes program 0, with the
breakpoints the client set for program 0 in the meantime. The firmware
then attaches the new context and the server sends Loaded. Errors:

| Request | Error |
|---|---|
| any of the three | 14 when the target is not launchable or refuses uploads |
| LoadBegin | 2 for a size below 8; 11 while script code runs or is halted; 8 when the buffer cannot be allocated |
| LoadData | 2 without a transfer, or with an offset and length outside it |
| LoadEnd | 11 while script code runs or is halted; 9 when the transfer is incomplete or the bytecode is invalid (the transfer is dropped) |

**ReadProgram.** Reads a section of a program's image: 0 the whole
bytecode, 1 the debug symbols, 2 the embedded source (every file's text,
one after the other, in the order of the [file table](#programs-made-of-several-files)).
`total` is the section's full length (0 when the program has none), `n`
the bytes in this reply, at most `max` and at most the max payload minus 6.
A program number that names no program is error 2, except program 0,
which answers with total 0.

**SetBreakpoints.** Replaces the breakpoints of one program with the lines
given; the other programs' breakpoints stay. The program does not have to
exist yet: breakpoints for program 0 sent right after an upload wait for
the context the firmware makes of it. Error 10 when the lines do not fit
into `WRENCH_DEBUG_MAX_BREAKPOINTS` beside the other programs'
(parked breakpoints of modules give way), 2 for a program number out of
range. Lines are [virtual lines](#programs-made-of-several-files).

Program 0xFF followed by a program hash replaces the breakpoints parked
for the program with that hash (capability bit 5): they are set when a
program with that hash is added, before any of its code runs. A client
that knows a module from an earlier session (the VS Code adapter keeps
its image) sends its breakpoints this way, so they hit even when the
program calls into the module right after importing it. Hash 0 is error
2.

**Launch.** Asks the firmware to run program 0's global code from the
start. The server replies at once; the run follows when the firmware next
calls `tick()`, which returns `WRD_TickRunRequested`, and the end of the
run comes as an Ended event. With flag 1 the run halts on the first line
of program 0 it executes. Errors: 13 when the target is not launchable,
11 while script code runs or is halted, 3 when there is neither a program
0 nor a completed upload.

**Continue.** Resumes a halted program. It is always ok; when nothing is
halted it only cancels a pending step. Flag 1 is for a client that evaluated a conditional
breakpoint, a hit count or a logpoint and decided not to stop: when the
breakpoint interrupted a step over or step out, the step carries on;
without the flag, any step is cancelled.

**Pause.** Always ok. A running program halts at its next line marker
(reason 3). Nothing running: the next line marker any debugged program
executes halts. Already halted: the server sends Stopped again.

**StepIn, StepOver, StepOut.** Error 4 unless halted. The step starts from
the halted frame's depth and ends on the same stack only, at a line marker
or where a call returns into a frame above the one it started in (a
Stopped with line 0); see [Stepping](breakpoints.md#stepping).

**Terminate.** Always ok. Cancels a launch the firmware has not carried
out yet. Script code that runs or is halted ends on its next line marker:
the VM returns from the call with `WR_ERR_debugger_terminated`. When no
script code runs but program 0 is yielded, its call ends on its next line
once the firmware continues it; other contexts are not touched.

**GetStack.** Frames of the halted program, innermost first. Each frame is
11 bytes:

| Field | Meaning |
|---|---|
| u8 kind | 0 global code, 1 a function |
| u8 function | index into the program's function table (kind 1) |
| u32 pc offset | offset of the halted instruction in the program's bytecode; the client maps it to a line with its line table |
| u16 frame base | the frame's position on its VM stack, 0xFFFF for global code |
| u16 slots | number of arguments and locals |
| u8 program | the program the frame runs in, 0xFF for none |

A walk that reaches the native call by which a program called into a
module it imported goes on in the caller, so one stack can hold frames of
several programs. Error 4 unless halted.

**GetLocals.** Values of a frame's arguments and locals, by slot. Errors 4
unless halted, 5 for a frame that does not exist.

**GetGlobals.** Values of a program's globals, by slot. Works while the
program is halted and while no script code runs; error 4 while script code
runs, 3 for a program that does not exist.

**GetChildren.** The elements of a container value, by its handle. For an
array, a value each; for a struct, `u32 member name hash, value` each
(names come from the symbols); for a hash table, `key value, value` each;
for a string or raw array, plain bytes (`u32 total, u16 n, n bytes`, at
most the max payload minus 6). Errors 4 while script code runs, 7 for a
handle that is no longer valid.

**SetLocal, SetGlobal, SetChild.** Store a value (int, float or string, see
[Values](#values)) and reply with the stored value. A string is created in
the context that owns the variable. Errors: 4 while script code runs, 5
for a frame that is no function frame, 6 for a slot or index out of range,
3 for a program that does not exist, 7 for a stale handle, 13 for a slot
the server cannot write, 2 for an undecodable value.

**AddWatch, RemoveWatch.** See [Watch paths](#watch-paths). AddWatch
replies with a watch id from 1 to `WRENCH_DEBUG_MAX_WATCHES`; ids are
reused. Errors: 13 for more than `WRENCH_DEBUG_MAX_WATCH_STEPS` steps, 2
for a bad root, 3 for a global of a program that does not exist, 16 for a
slot that does not exist, 4 for a local while not halted, 15 when all
watches are taken. RemoveWatch with id 0 removes all; error 2 for an id out
of range.

**Ping.** Ok, nothing else. Clients ping a halted target every second to
keep a link timeout from striking.

**Detach.** Ok; clears breakpoints and watches, cancels a step, resumes a
halted program and ends the session.

### Error codes

| Code | Name | Client message |
|---|---|---|
| 1 | unknown request | target does not know this request |
| 2 | bad request | malformed request |
| 3 | no program | no program loaded on the target |
| 4 | not stopped | target is not stopped |
| 5 | bad frame | no such stack frame |
| 6 | bad slot | no such variable |
| 7 | bad handle | value is no longer valid |
| 8 | no memory | target is out of memory |
| 9 | bad bytecode | target rejected the bytecode |
| 10 | too many breakpoints | too many breakpoints for the target |
| 11 | busy | target is busy executing |
| 12 | version | debug protocol version mismatch |
| 13 | unsupported | not supported by the target |
| 14 | uploads disabled | the target does not accept uploaded programs |
| 15 | too many watches | the target has no room for more watchpoints |
| 16 | bad watch | the target has no such variable to watch |

## Who carries out Launch and LoadEnd

The server runs no script code. Launch and LoadEnd are requests the
firmware carries out: the server records them and reports them through
the flags `tick()` returns, and the firmware runs program 0 or swaps in
the uploaded program when it is between script calls. That is why both are
refused as busy while script code runs or is halted, why the Launch reply
comes before the run, and why the end of a run is an event: the VM reports
the end of program 0's global code to the server, whoever started it. A
target that is not launchable refuses both (error 13 for Launch, 14 for
the upload) and does not advertise the capabilities.

## Capabilities

The capabilities byte in the Hello reply:

| Bit | Value | Meaning |
|---|---|---|
| 0 | 0x01 | load: accepts uploads (LoadBegin, LoadData, LoadEnd); only together with launch, left out when the firmware called `setAllowUpload( false )` |
| 1 | 0x02 | launch: the firmware runs program 0 when asked (`setLaunchable( true )`) |
| 2 | 0x04 | set value: accepts SetLocal, SetGlobal, SetChild (always set) |
| 3 | 0x08 | host driven: the firmware calls into the script itself (`setHostDriven( true )`), so the session goes on after the global code ends |
| 4 | 0x10 | watch: AddWatch and RemoveWatch; the Hello reply's "max watches" is meaningful (always set) |
| 5 | 0x20 | parked breakpoints: SetBreakpoints takes program 0xFF and a program hash (always set) |

The Hello reply always ends with "max watches" and "max programs"; a
client reads "max watches" only when bit 4 is set.

## States

The state in the GetInfo reply:

| Value | State | When |
|---|---|---|
| 0 | no program | no program 0 is attached (also right after an upload, until the firmware attaches the new context) |
| 1 | loaded | a program 0 is present and no script code runs |
| 2 | running | script code of a debugged program executes inside the VM |
| 3 | stopped | halted inside the VM, waiting for the client |
| 4 | ended | the last run of program 0's global code finished |

## Compiler flags

The compiler flags byte (in GetInfo and GetPrograms) is the third byte of
the bytecode, the `WrenchCompilerFlags` the program was compiled with:

| Bit | Flag | Meaning |
|---|---|---|
| 0x01 | `WR_INCLUDE_GLOBALS` | a table of global name hashes (for `wr_getGlobalRef()`) |
| 0x02 | `WR_EMBED_DEBUG_CODE` | line markers and the symbols section; without it the program cannot stop |
| 0x04 | `WR_EMBED_SOURCE_CODE` | the symbols section and the source text (ReadProgram section 2) |
| 0x08 | `WR_NON_STRICT_VAR` | compiled in non-strict mode (`-nostrict`) |

## Values

A value is a u8 tag followed by its data:

| Tag | Kind | Data |
|---|---|---|
| 0 | int | i32 |
| 1 | float | f32 bits |
| 2 | string | u32 handle, u32 length, u16 n, n bytes (the first 48 bytes at most; fetch the rest with GetChildren) |
| 3 | array | u32 handle, u32 count |
| 4 | hash table | u32 handle, u32 count |
| 5 | struct | u32 handle, u32 count |
| 6 | raw array | u32 handle, u32 size (a native byte array) |
| 7 | other | u8 type |

Handles stay valid while the program stays halted, and while it stays idle
between calls; they become invalid when script code runs again. A server
tracks `WRENCH_DEBUG_MAX_HANDLES` containers per stop; past that a
container comes with handle 0, which no request accepts.

Values sent to the target (SetLocal, SetGlobal, SetChild) are an int, a
float, or a string encoded as tag 2 followed by `u16 n, n bytes` (no handle
or length).

## Events

| Type | Name | Payload |
|---|---|---|
| 0xC0 | Stopped | u8 reason, i32 line, u8 wrench error, u32 missing function hash, u8 watch id, u8 program |
| 0xC1 | Output | the text |
| 0xC2 | Ended | u8 wrench error, u8 flags, i32 last line, u32 missing function hash, value |
| 0xC3 | Loaded | u32 program hash |
| 0xC4 | WatchRemoved | u8 watch id |
| 0xC5 | ProgramAdded | u8 program |
| 0xC6 | ProgramRemoved | u8 program |

* **Stopped.** Reason 0 entry, 1 breakpoint, 2 step, 3 pause, 4 runtime
  error, 5 watch. The line is a virtual line of the program given last.
  Line 0 is a step that stopped where a call returned in the middle of a
  line, which has no line marker: the line is the top frame's, found from
  its pc in the line table.
  The wrench error is set for reason 4; the missing function hash is
  `wr_hashStr()` of the name for `WR_ERR_function_not_found` and
  `WR_ERR_lib_function_not_found`, else 0; the watch id is set for reason
  5, else 0.
* **Output.** Text from `WrenchDebugServer::print()`, in chunks of at most
  the max payload. Sent only during a session.
* **Ended.** The end of program 0's global code: the error (0 for none,
  `WR_ERR_debugger_terminated` after a terminate), flags (1 aborted by the
  debugger, 2 the firmware keeps calling into the script), the last line,
  the missing function hash as for Stopped, and the value the global code
  returned (left out when it does not fit). Sent during a session, and for
  a run started by a Launch even when the session ended meanwhile.
* **Loaded.** The firmware attached a new program 0, for example the one
  made of an upload. The hash it carries (`WrenchDebugEvent::programHash`
  in `WrenchDebugClient`) tells a client whether the new program is the
  one it uploaded itself; VS Code announces only another one.
* **WatchRemoved.** A watched local's call returned, or the program a
  watch was in went away.
* **ProgramAdded, ProgramRemoved.** The firmware attached or detached a
  context, a debugged program imported a module, or a context was
  destroyed (also a module whose importer was reset). Program 0 is never
  announced this way: a new program 0 comes as Loaded.

Events are not acknowledged. So that a lost Stopped event, or a client
that attaches to a target already halted, cannot leave the client
believing the program runs, a halted server sends Stopped again, with the
original reason, line and error, after it answers a Hello or a Pause.

## Programs

A server debugs up to its "max programs" (Hello reply) programs; 0xFF
names none. Lines, breakpoints, globals and global watches are per
program, so the requests about them carry a program number; locals are
reached through stack frames, which name their program. GetPrograms
answers one record per program:

| Field | Meaning |
|---|---|
| u8 id | the program number; 0 is the first context the firmware attached (after an upload, the one it made of it) |
| u8 flags | 2 imported by a debugged program, 4 its global code ran (1 is unused) |
| u8 importer | the program that imported it, 0xFF for none |
| u32 hash, u32 bytecode length, u8 compiler flags, u32 symbols length, u32 source length, u16 globals, u8 functions | as in GetInfo |
| u8 n, n bytes | the name the firmware gave `attach()`, may be empty |

When a module goes away its breakpoints are kept by the module's bytecode
hash and set again when a module with the same hash is added; a client can
park breakpoints for a hash the same way before the module was ever there
(SetBreakpoints with program 0xFF). A SetBreakpoints that needs their room
drops them. Other programs'
breakpoints go with them.

## Watch paths

A watch names a value by where it lives, so the server can find it again
at every line marker:

* **Root**: a global slot of a program (root 0, with the program number;
  the frame field is ignored), or a local slot of a frame (root 1, with
  the frame index of the current stop; the program field is ignored).
  Locals need the program halted; the watch then belongs to that call.
* **Steps**: up to `WRENCH_DEBUG_MAX_WATCH_STEPS`, each a u8 kind and a u32
  value. Kind 0 is a position: an array element, a character of a string
  or of a raw array. Kind 1 is a hash: the key of a hash table entry (an
  int key is its own hash, a float its bits, a string `wr_hash()` of its
  bytes) or the name hash of a struct member.

At every line marker the server follows each path from its root and
compares what it finds with the last snapshot: numbers by value, strings
by identity, length and contents, other containers by identity and count,
and whether the path leads anywhere. A change halts the program with
reason 5 on that line, the line after the one that made the change. While
halted, values change only through the client; the server takes new
snapshots before the program resumes, so those writes never count.

## Line markers

The compiler emits `O_DebugInfo` followed by a 16 bit codeword whose top
two bits are its type:

| Codeword | Meaning |
|---|---|
| `0x4000 \| line` | the start of a line (14 bits) |
| `0xC000 \| high` | bits 14 and up of the next line number, for sources longer than 16383 lines |
| `0x0000 \| n`, `0x8000` | older call markers, ignored by the server |

Steps compare frame depth, taken from the VM's frame base, so they work
for code entered through `wr_callFunction()` from native code the same way as
for calls inside the script. [Debug information](../internals/debug-info.md)
describes the symbols section in full.

## Programs made of several files

A program that includes other files is compiled as one text, the included
files in place of their includes, and the line markers count lines through
all of it ("virtual" lines). The target only ever sees these: breakpoints,
stop lines and watches carry virtual lines, so nothing in the protocol
knows about files. The symbols section ends with a file table that maps
them back, written when the source has a name or includes something:

| Field | Meaning |
|---|---|
| u16 files | how many, the main file first |
| per file: name, 0, u32 length | the name the compiler resolved, a zero byte, and the length of its text in the embedded source (0 when not embedded) |
| u16 segments | how many |
| per segment: u32 virtual line, u16 file, u32 line | from that virtual line on, lines come from that file, starting at that line |

With `WR_EMBED_SOURCE_CODE` the source section holds every file's text,
one after the other in the order of the table; ReadProgram section 2
returns the whole block. An image without a table is one file whose lines
are the virtual lines.
