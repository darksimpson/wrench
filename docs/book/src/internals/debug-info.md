# Debug information

This page explains what `-debug` and `-debugsource` add to a program, how
the debugger turns it into lines, names and files, and what it costs in
size and speed. Read it when you decide how to build the images for a
device, or when you work on the debugger. Using the debugger is covered in
[Debugging](../debugging/overview.md).

## The two options

| Option | Compiler flags | Adds |
|---|---|---|
| `-debug` | `WR_EMBED_DEBUG_CODE` | line markers and call markers in the code, the symbols section |
| `-debugsource` | `WR_EMBED_DEBUG_CODE` and `WR_EMBED_SOURCE_CODE` | the same, and the text of the main file and of every included file |

In the API the flags go to `wr_compile()` or `wr_compileWithOptions()`
(see [Compiling scripts](../embedding/compiling.md)). The debugger compiles
with both when it uploads a program; you choose for the images you build
into firmware yourself. A program built without either still runs under a
debug server, but it cannot stop at breakpoints or step, and the debugger
says so.

`-debugsource` is what makes a target self describing: a debugger can
attach without the `.w` files (`wrench dbg -attach` without a file, VS Code
attach without `"program"`) and read program and source from the target.
With `-debug` alone the debugger needs the same source files; it compiles
them with the flags the target's image has and warns when the result is
not the image the target runs.

## Line markers

With `WR_EMBED_DEBUG_CODE` the compiler puts a `DebugInfo` instruction in
front of the code of each statement: the opcode and a 16 bit codeword.

| Codeword | Kind | Payload |
|---|---|---|
| `0x4000` to `0x7FFF` | line marker | the line, 14 bits |
| `0xC000` to `0xFFFF` | high line bits | bits 14 and up of the next line marker's line, for lines above 16383 |
| `0x0000` + n | call marker | the call that follows calls function unit n of this program |
| `0x0E00` | call marker | the call that follows goes by name hash: a native, a library function or a function of a module |

Rules the compiler follows:

* A line marker is left out when the previous marker was for the same
  line, so several statements on one line share one marker. A call to a
  native or library function gets a line marker of its own line before
  its call marker.
* Every pass of a loop meets a line marker, also for `while ( 1 ) {}` and
  `for ( ;; ) {}`, so a debugger can pause any loop. The loop's own line
  gets a marker where each pass begins.
* A call marker between two markers of one line does not start the line
  again, so a breakpoint on `println( f() )` stops once.

The type `0x8000` (`WRD_Return`) is defined in `src/debug/debug.h` but not
emitted; the debug server tracks calls and returns by the stack frames.

The disassembler decodes the markers; a listing is in
[Compiling, disassembling and assembling](tools.md#line-markers-and-call-markers).

### What the VM does with them

In a VM built with `WRENCH_INCLUDE_DEBUG_CODE`, `DebugInfo` calls the
debug server attached to the context, `onDebugInfo()`, with the code
position, the frame and the stack. The server:

* records the line, and checks breakpoints, watchpoints and the step in
  progress against it;
* blocks right there while the debugger has the program stopped, serving
  requests for stacks and variables from the live frames;
* reads the link to the debugger every `WRENCH_DEBUG_POLL_MS` milliseconds
  (20 by default) while code runs, looking at the clock every
  `WRENCH_DEBUG_POLL_LINES` lines (8), so a pause request arrives within a
  few lines.

A context with no server attached skips the marker. A VM built without
`WRENCH_INCLUDE_DEBUG_CODE` skips it always, so a debug image runs there
too. How the server fits into firmware is in
[Adding the debugger to your firmware](../debugging/firmware.md).

## The symbols section

The section sits after the globals table (see
[The bytecode image](bytecode.md#the-debug-symbols)): a 4 byte hash of the
main source text, a 2 byte size, then the symbols. Inside, all numbers are
little endian and all names end with a zero byte.

| Size | Field |
|---|---|
| 2 | number of units: the global code and every function |
| per unit | 1 byte number of variables, 1 byte number of parameters, the unit's name, then each variable's name |
| optional | the file table |

Unit 0 is `::global`; its variables are the globals, written `::name`. A
function's variables are its parameters first, then its locals. A struct
is a function too: its variables are the constructor's parameters and the
members. The names of the members serve the debugger in another way as
well: struct instances and hash tables store hashes of member names, and
the debugger hashes every name in the symbols to show `p.width` instead of
a number.

A `private` global is written with the file it belongs to,
`::count@stats.w`, which is also how the debugger shows and evaluates it
(see [Several files: include and private](../language/include.md)).

### The file table

The compiler numbers the lines of a program made of several files as one
sequence, the virtual lines: an included file's lines take the place of
its `include`. The line markers carry virtual lines, so the protocol and
the target stay line based. The file table maps them back:

| Size | Field |
|---|---|
| 2 | number of files |
| per file | its name, then 4 bytes: the length of its text in the embedded source, 0 when not embedded |
| 2 | number of segments |
| 10 per segment | 4 bytes first virtual line, 2 bytes file number, 4 bytes line in that file |

A segment says: from this virtual line on, the lines belong to this file,
starting at this line; it runs until the next segment. The table is left
out for a source compiled without a name and without includes, which is
one file whose virtual lines are its lines.

`examples/debug_demo/main.w` includes `shapes.w` on line 4 and `stats.w`
(from `lib/`) on line 5. Compiled with `-debug`, its file table holds:

| Files | |
|---|---|
| 0 | `main.w` |
| 1 | `shapes.w` |
| 2 | `lib/stats.w` |

| Segment | First virtual line | File | Line |
|---|---|---|---|
| 0 | 1 | 0 | 1 |
| 1 | 4 | 1 | 1 |
| 2 | 15 | 0 | 4 |
| 3 | 16 | 2 | 1 |
| 4 | 35 | 0 | 5 |

An included file is named by its path relative to the folder of the main
file, wherever it was found: `stats.w` came through the `"include"`
folder `lib` of `wrench.json` and is named `lib/stats.w`. Only a file on
another drive than the main file is named by its absolute path. The
names are part of the image, so an image built on one machine carries no
paths of that machine, and a debugger on another machine finds the files
next to the program or in the project's include folders (see
[Programs made of several files](../debugging/programs.md#programs-made-of-several-files)).
A compile error names an included file by the path it was read from.

### The embedded source

With `WR_EMBED_SOURCE_CODE` the texts follow the symbols section, the main
file first and then every included file in the order of the file table
(see [The bytecode image](bytecode.md#the-embedded-source)). The lengths in
the file table cut the block into files. A program without a file table
has one text, the whole block.

## How the debugger builds its line table

The host side (`WrenchDebugProgram::parse()` in
`src/debug/debug_client.cpp`) reads the image the same way for a local
`.w` it compiled, a `.bin` and an image read back from a target:

1. It checks the checksum, reads the header, the function table and the
   optional sections, and finds where the code starts and ends.
2. It gives every unit a code range: the global code up to the first
   function, each function up to the next one.
3. It walks each range instruction by instruction, using the operand sizes
   of the disassembler, and records every line marker as a pair of code
   offset and line (adding the high bits of a preceding `0xC000` codeword).
   Bytes that cannot be an instruction end the walk of a range: a struct's
   member table lies between the code of one function and the next.
4. It sorts the pairs by offset. The set of lines with markers is the set
   of lines a breakpoint can be on.

With it:

* the line of a code position (a stack frame, a stop) is the last marker
  at or before that position within its unit;
* a breakpoint on a line without code moves to the next line that has one;
  in a file with includes it skips over the lines of an included file and
  goes on in the next part of the same file;
* a virtual line becomes a file and a line through the segments, and back.

The debug server on the target needs none of this: it only compares the
line of each marker with its breakpoint list, so it stays small and does
not allocate.

## What it costs

Size, measured with `wrench c` on three programs:

| Program | Plain | `-debug` | `-debugsource` |
|---|---|---|---|
| `hello.w` (11 lines, 1 function) | 65 bytes | 153 bytes | 278 bytes |
| a benchmark (15 lines, 1 function) | 111 bytes | 212 bytes | 421 bytes |
| `examples/debug_demo/main.w` with its two includes | 451 bytes | 813 bytes | 2037 bytes |

`-debug` about doubles a small program: 3 bytes per line marker and per
call marker, plus the names. `-debugsource` adds the text as it is,
comments included.

None of it costs RAM in the VM: the symbols and the source stay in the
image, which can be in flash (see
[Running from ROM](bytecode.md#running-from-rom)).

Speed: each marker the VM meets is one more instruction. A loop of 30
million passes plus a recursive `fib( 30 )`, run with `wrench rb` (no
debugger attached) on the Windows Release build, took 591 to 669 ms
plain and 695 to 735 ms compiled with `-debug`, three runs each, so
between 5 and 25 percent more. With a debugger attached, every marker also
runs the server's checks and its periodic look at the link.

> [!TIP]
> Build the images a device runs in the field without debug information,
> or with `-debug` only where you want to attach in the field: a VM with
> the debug server still runs plain images at full speed.
