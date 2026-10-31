# Command line tool

This page lists every command and option of the `wrench` executable, with an
example and its output. It is a reference; the pages it links to explain
the background.

The tool is built from the sources in `src/` (see
[Source layout and building from source](../internals/building.md)): on
Windows it is `win32/Release/wrench.exe`, on Linux `wrench` in the
repository root. The VS Code extension carries a copy of its own (see
[Trying it out](../vscode/quick-try.md)).

## Overview

```text
wrench <command> [options] [arguments]
```

| Command | What it does |
|---|---|
| [`c`](#c-compile-to-a-file) | compile a source file to a bytecode file |
| [`ch`](#ch-compile-to-a-c-header) | compile a source file to a C header holding the bytecode |
| [`p`](#p-print-the-disassembly-of-a-source-file) | compile and print the disassembly |
| [`r`](#r-compile-and-run) | compile and run |
| [`rb`](#rb-run-a-bytecode-file) | run a compiled program |
| [`d`](#d-disassemble-a-bytecode-file) | print the disassembly of a compiled program |
| [`dbg`](#dbg-console-debugger) | interactive console debugger |
| [`dbgserver`](#dbgserver-a-debug-target-on-this-pc) | run as a debug target that a debugger connects to |
| [`dap`](#dap-and-lsp-protocol-servers) | Debug Adapter Protocol server for VS Code |
| [`lsp`](#dap-and-lsp-protocol-servers) | Language Server Protocol server for VS Code |
| [`v`](#v-version-and-help) | show the version and the help text |
| [`t`](#t-run-the-test-suite) | run the test suite |
| [`release`](#release-write-the-single-file-build) | write the single file build |

Only the first argument is the command; the arguments after it are
options and files, and a file may be named like a command
(`wrench c prog.w p` writes the file `p`). Running `wrench` without a
command, or with a command it does not know, prints the help text and
fails. The first line of the help text is the version.

## Exit codes

| Code | When |
|---|---|
| 0 | success |
| 1 | the program stopped with a runtime error (`r`, `rb`) |
| 2 | compile error (`c`, `ch`, `p`, `r`) |
| -1 (255 on Linux) | a bad command line, an unknown command or option, or a file that cannot be read or written |

`wrench v` exits with 0. `wrench t` exits with -1 when a test failed; see
[t: run the test suite](#t-run-the-test-suite).

The examples on this page use this program, `hello.w`:

```wrench
var count = 3;

function add( a, b )
{
	return a + b;
}

for ( var i = 0; i < count; ++i )
{
	println( add( i, 10 ) );
}
```

## Compile options

`c`, `ch`, `p` and `r` compile a source file first. These options change how
it is compiled; give them before the file arguments.

| Option | Effect |
|---|---|
| `-debug` | embed debug code (a line marker per statement and the debug symbols), needed to debug the program |
| `-debugsource` | as `-debug`, and embed a copy of the source (of every included file too) |
| `-noglobals` | leave out the table of global names (4 bytes per global); `wr_getGlobalRef()` cannot find globals then |
| `-nostrict` | non-strict mode, for older scripts: an assignment declares an unknown variable, no block scope, a second `var` assigns (see [Non-strict mode](../language/variables.md#non-strict-mode)) |
| `-I <folder>` | also look for `include "file"` in this folder; may be given more than once; `-I<folder>` without the space works too |

An `include "file.w"` is searched for next to the file that includes it,
then in the `-I` folders in the order given, then in the `"include"`
folders of the [`wrench.json`](../vscode/wrench-json.md) nearest to the
main source file. The language side of `include` is explained in
[Several files: include and private](../language/include.md).

What `-debug` and `-debugsource` add to the bytecode, and what it costs, is
described in [Debug information](../internals/debug-info.md). Without
`-noglobals` every image carries the global names; the compiler flag
behind it is `WR_INCLUDE_GLOBALS` (see
[Compiling scripts](../embedding/compiling.md)).

A compile error prints the file and line, the error code and the source
line with a marker, then `compile error [...]`, and the tool exits with 2:

```bash
wrench c broken.w broken.bin
```

Output:

```text
broken.w:2
err: WR_ERR_bad_expression[9]
2     var b = a +;
                 ^
compile error [WR_ERR_bad_expression]
```

The error codes are listed in [Error codes](error-codes.md).

## c: compile to a file

```text
wrench c [compile options] <infile> <outfile>
```

Compiles `infile` and writes the bytecode image to `outfile`.

```bash
wrench c hello.w hello.bin
wrench c -debugsource hello.w hello_src.bin
```

Output:

```text
hello.w -> hello.bin
hello.w -> hello_src.bin
```

`hello.bin` is 65 bytes, `hello_src.bin` 278. A `.bin` is what
`wr_run()` takes, what you put into a device's flash, and what
[`rb`](#rb-run-a-bytecode-file), [`d`](#d-disassemble-a-bytecode-file),
`dbg` and `dbgserver` accept. Its layout is described in
[The bytecode image](../internals/bytecode.md).

## ch: compile to a C header

```text
wrench ch [compile options] <infile> <outfile> <name>
```

Compiles `infile` and writes `outfile` as a header that defines
`const int <name>_bytecodeSize` and `const unsigned char <name>_bytecode[]`,
for firmware that keeps its script in flash. It also prints the size and a
hex dump of the image before the summary line.

```bash
wrench ch hello.w hello.h hello
```

Output:

```text
65:
0x00000000: 02 01 11 00 00 39 00 74 12 39 3B 02 00 03 04 7A : .....9.t.9;....z
0x00000010: 7A C4 94 85 BE ED CF 00 03 CF 01 00 84 00 01 17 : z...............
0x00000020: 09 20 01 AF 0A 07 02 00 02 58 08 06 01 A6 F8 BF : .........X......
0x00000030: 18 90 01 37 E8 09 02 EF 14 DC 01 00 13 B3 06 FD : ...7............
0x00000040: 5C                                              : \

hello.w -> hello.h as hello
```

`hello.h`:

```cpp
#ifndef _hello_H
#define _hello_H

/******* wrench bytcode automatically generated header *******/

const int hello_bytecodeSize=65;
const unsigned char hello_bytecode[]=
{
	0x02, 0x01, 0x11, 0x00, 0x00, 0x39, 0x00, 0x74, 0x12, 0x39, 0x3B, 0x02, 0x00, 0x03, 0x04, 0x7A, // 16
	0x7A, 0xC4, 0x94, 0x85, 0xBE, 0xED, 0xCF, 0x00, 0x03, 0xCF, 0x01, 0x00, 0x84, 0x00, 0x01, 0x17, // 32
	0x09, 0x20, 0x01, 0xAF, 0x0A, 0x07, 0x02, 0x00, 0x02, 0x58, 0x08, 0x06, 0x01, 0xA6, 0xF8, 0xBF, // 48
	0x18, 0x90, 0x01, 0x37, 0xE8, 0x09, 0x02, 0xEF, 0x14, 0xDC, 0x01, 0x00, 0x13, 0xB3, 0x06, 0xFD, // 64
	0x5C, // 65
};

#endif
```

Pass `hello_bytecode` and `hello_bytecodeSize` to `wr_run()`; the VM runs
the image where it is, see [Running from ROM](../internals/bytecode.md#running-from-rom).

## p: print the disassembly of a source file

```text
wrench p [compile options] <infile>
```

Compiles `infile` and prints its disassembly, the same listing
[`d`](#d-disassemble-a-bytecode-file) prints for a compiled file. Nothing is
written. `wrench p -debug hello.w` shows the line markers as well. How to
read a listing is explained in
[Compiling, disassembling and assembling](../internals/tools.md).

## r: compile and run

```text
wrench r [compile options] <infile>
```

Compiles `infile` and runs it in a new state with all standard libraries
loaded and two natives of the tool, `print()` and `println()`, which write
to standard output. It also registers the library constant `rgb::count`
(6).

```bash
wrench r hello.w
```

Output:

```text
10
11
12
```

A runtime error prints `err:` and the error code, and the tool exits
with 1. Other native functions are not there, so a script written for a
device fails where it calls one:

```bash
wrench r native.w
```

Output:

```text
err: WR_ERR_function_not_found
```

A code the program set itself with `sys::halt()` (101 to 255) has no
name and prints as `err: user error 120`.

## rb: run a bytecode file

```text
wrench rb <bytecode file>
```

Runs a compiled program in the same environment as `r` (without
`rgb::count`). A damaged image, or one built by a major version of wrench
this VM does not run, fails its check with `err: WR_ERR_bad_bytecode_CRC`
(see [Images from other versions](../embedding/compiling.md#images-from-other-versions)).

```bash
wrench rb hello.bin
```

Output:

```text
10
11
12
```

## d: disassemble a bytecode file

```text
wrench d <bytecode file>
```

Prints the header, the globals table, the function table and the code of
every function of a compiled program.

```bash
wrench d hello.bin
```

Output:

```text
Wrench Bytecode Disassembly
===========================
globals : 0x02
units   : 0x01
flags   : 0x11
code    : [0x0016..0x003D)

Globals Table
-------------
  g[0x00] hash=0xC47A7A04
  g[0x01] hash=0xEDBE8594

Function Table
--------------
  idx  hash         args frame baseAdj offset
  0x00 0x3B391274 0x02 0x00  0x03   0x0039

unit global @0x0016
  0016: CF 00 03                            LiteralInt8ToGlobal            g[0x00] imm=0x03
  0019: CF 01 00                            LiteralInt8ToGlobal            g[0x01] imm=0x00
  001C: 84 00 01 17 09                      GGCompareLTBZ8                 g[0x00] g[0x01] rel8=0x17 pad=0x09 ->0x0036
  0021: 20 01                               LoadFromGlobal                 g[0x01]
  0023: AF 0A                               LiteralInt8                    0x0A
  0025: 07 02 00 02 58                      CallFunctionByIndex            argc=0x02 fnIdx=0x00 pcSkip=0x02
  002A: 08                                  PushIndexFunctionReturnValue
  002B: 06 01 A6 F8 BF 18                   CallFunctionByHashAndPop       argc=0x01 hash=0x18BFF8A6
  0031: 90 01                               IncGlobal                      g[0x01]
  0033: 37 E8 09                            RelativeJump8                  rel8=0xE8 pad=0x09 ->0x001C
  0036: 02                                  LiteralZero
  0037: EF                                  GlobalStop
  0038: 14                                  Stop

unit 0 hash=0x3B391274 args=2 frame=0 baseAdj=3 @0x0039
  0039: DC 01 00                            LLBinaryAddition               l[0x01] l[0x00]
  003C: 13                                  Return
```

The listing is walked through line by line in
[Compiling, disassembling and assembling](../internals/tools.md#reading-a-listing).

## dbg: console debugger

```text
wrench dbg [-target T] [-attach] [-project P] [file]
```

An interactive debugger in the console. `file` is a `.w` source, or a
`.bin` compiled with `-debugsource` (the source shown is the one embedded
in it). The commands it takes at its `(wdb)` prompt are described in
[The console debugger](../debugging/console.md).

| Option | Effect |
|---|---|
| `-target T` | where the program runs: `local` (default, a VM inside the tool), `tcp:<host>:<port>` or `serial:<port>[:<baud>]` |
| `-attach` | debug what the target already runs instead of uploading `file`; without `file` the program and its source are read from the target |
| `-project P` | the `wrench.json` that describes the host application to imitate; default the nearest one above `file`, `none` ignores it |

Options and the file may come in any order. Without `-attach` a file is
required. The options of `dbgserver` (`-context`, `-linktimeout`,
`-noupload`, `-hold`, `-v`) are refused with "unknown option ... for
dbg". When the debugger ends, with `q` or at the end of its input, it
detaches from the target, which goes on running.

```bash
wrench dbg hello.w
```

With the commands `b 5`, `r`, `bt`, `p a` and `c` typed at the prompt:

```text
connected to local, wrench 8.0.0, max packet 1024 bytes
hello.w: 11 lines, 1 functions. 'h' for help.
(wdb:idle) breakpoint at line 5
(wdb:idle)
[breakpoint] line 5
in add, line 5
     3  function add( a, b )
     4  {
*>   5  	return a + b;
     6  }
     7
(wdb) >#0  add  line 5
 #1  <global>  line 10
(wdb) a = 0
(wdb) 10

[breakpoint] line 5
```

A local target imitates the host application described by `wrench.json`
(see [Describing your host application](../vscode/wrench-json.md)).
Without one it loads all standard libraries and `print`/`println`, and a
call to any other native stops with a runtime error that names it:

```text
*** runtime error WR_ERR_function_not_found at line 1
*** 'led' is not provided by the target
```

## dbgserver: a debug target on this PC

```text
wrench dbgserver -target T [-noupload] [-hold] [-linktimeout MS] [-context F] [-v] [-project P] [file]
```

Runs a debug target in the console, the way firmware with a
`WrenchDebugServer` does: debuggers (`wrench dbg`, VS Code) connect to it,
upload programs, launch them and attach to them. `file` (a `.w` source or a
`.bin`) is preloaded as program 0; a source file is compiled with
`-debugsource`. The server runs until you stop it with Ctrl+C.

| Option | Effect |
|---|---|
| `-target T` | required: `tcp:<port>` (listen on this port of 127.0.0.1, this machine only), `tcp:<address>:<port>` (listen on that IPv4 address of this machine, `tcp:0.0.0.0:<port>` on every interface) or `serial:<port>[:<baud>]` |
| `-context F` | also run script `F` in a context of its own, a program the debugger sees beside `file`: its global code runs at start, then its `tick()` is called ten times a second. Up to 3 times, needs `file` |
| `-noupload` | refuse programs sent by a debugger, like `setAllowUpload( false )` |
| `-hold` | start `file` at once, halted on its first line until a debugger attaches, like `breakOnEntry()`; needs `file` |
| `-linktimeout MS` | while halted, let go of a debugger not heard from for `MS` milliseconds, like `setLinkTimeout()` |
| `-v` | report attaches, detaches, uploads and program ends |
| `-project P` | as for `dbg`. Without a `wrench.json` the server answers 0 for any native a script calls and says so once; with one, only the declared natives answer |

```bash
wrench dbgserver -target tcp:5077 -context sensor.w -v hello.w
```

Output, while `wrench dbg -target tcp:127.0.0.1:5077 hello.w` connects,
runs `hello.w` and quits:

```text
wrench debug server listening on tcp 127.0.0.1:5077 (this machine only)
loaded hello.w (278 bytes)
attached sensor.w as program 1, global code: WR_ERR_None
client attached
program uploaded
10
11
12
program finished: WR_ERR_None
client detached
```

With `-hold` and `-noupload`, a debugger attaches to the program the server
started:

```bash
wrench dbgserver -target tcp:5078 -hold -noupload hello_src.bin
wrench dbg -target tcp:127.0.0.1:5078 -attach
```

The debugger reads program and source from the target:

```text
connected to tcp 127.0.0.1:5078, wrench 8.0.0, max packet 1024 bytes
read the program from the target (278 bytes, source embedded)
target program: 11 lines, 1 functions. 'h' for help.

[entry] line 1
in <global>, line 1
 >   1  var count = 3;
```

Scenarios that use `dbgserver` are in
[Debug targets: the PC and devices](../debugging/targets.md) and
[Scenarios](../debugging/scenarios.md).

## dap and lsp: protocol servers

```text
wrench dap
wrench lsp
```

`dap` is a Debug Adapter Protocol server and `lsp` a Language Server
Protocol server, both on standard input and output. VS Code starts them;
you do not run them by hand. The extension and what they do are described
in [Editing features](../vscode/editing.md) and
[Launch configurations](../vscode/launch.md).

## v: version and help

```text
wrench v
```

Prints the version (major and minor) and the help text:

```text
version: 8.0
usage: wrench <command> [options] [arguments]
...
```

The help text ends with the [exit codes](#exit-codes). `wrench v` exits
with 0. The full version, with the build number, is in `version.txt` and
in the first line of `wrench dbg` (`wrench 8.0.0`).

## t: run the test suite

```text
wrench t [testdir] [number]
```

Runs the test scripts listed in `testdir/test_files.txt` (default `tests`),
each twice (plain, then compiled with debug code under an attached debug
server), then the internal tests and the debugger protocol checks. A
number runs only that entry of the list (and leaves out the debugger
protocol checks); the two arguments may come in either order.

```bash
wrench t tests 34
```

Output (the start):

```text
test [34][034_short_circuit.w]: PASS
test [34][034_short_circuit.w][debug]: PASS
```

The scripts run with the test folder as the current directory, and some
write scratch files there. A test script that does not compile is
reported as a failure and the suite goes on with the next one. When any
test failed, the run ends with "N test(s) FAILED" and exits with -1.
What the suite covers and how to add a test is in
[The test suite](../internals/testing.md).

## release: write the single file build

```text
wrench release <fromdir> <todir>
```

Writes `wrench.h` and `wrench.cpp`, the single file build, into the
folder `todir`, made from the sources in `fromdir` (normally `src`).
`todir` is created when it does not exist (its parent folder must
exist). It also writes the version
from `fromdir/wrench.h` into `version.txt` in the folder that holds
`fromdir` (the repository root for `release src dist`).

```bash
wrench release src dist
```

This prints nothing when it succeeds. A source file that cannot be read,
or a file that cannot be written, prints a message naming it and the
tool exits with -1. See
[Releases and the single-file build](../internals/releases.md).
