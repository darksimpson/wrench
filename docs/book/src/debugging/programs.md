# Several files, programs and modules

A wrench program can be made of several files, a target can run several
programs side by side, and a program can load modules while it runs. The
debugger handles all three. This page explains what you see in each case
and ends with a walkthrough of `examples/debug_demo`, which shows all of
them in a few minutes.

## Programs made of several files

A script that includes others ([`include "file.w"`](../language/include.md))
is debugged as one program:

* Breakpoints work in every file of the program. In VS Code you set them in
  the file's editor; in `wrench dbg`, `b 12` and `l 12` mean the file of the
  selected frame (the main file while nothing is stopped), and
  `b util.w:12` or `l lib/util.w:12` name another file by the end of its
  path.
* The call stack shows each frame in its own file. `wrench dbg` writes
  locations in an included file as "line 8 of stats.w".
* `-debugsource` embeds the text of every file, so attaching without the
  sources shows all of them.
* Includes are found the way the compiler finds them: next to the
  including file, then in the `"include"` folders of
  [`wrench.json`](../vscode/wrench-json.md) (and the `-I` folders given to
  `wrench c`, `p`, `r` and `ch`).
* The debug information names each included file relative to the main
  file's folder, `lib/stats.w`, so an image holds no path of the machine it
  was built on (only a file on another drive is named by its absolute
  path). VS Code looks for such a file next to the program, then in the
  `"include"` folders of `wrench.json`, and opens it from there. Compile
  errors still name a file by the path it was read from.

When the program starts, `wrench dbg` says how many files it has:

```text
main.w: 24 lines, 4 functions. 'h' for help.
includes 2 more files, 'b file.w:line' and 'l file.w:line' reach them
```

Included files are compiled in place of their `include` and the target
counts lines through the whole text; the debugger maps those lines back to
files with a table in the debug symbols
([Debug information](../internals/debug-info.md)). You never see the
combined numbering, except in the [wire protocol](protocol.md).

### Private globals

A global declared `private` in an included file belongs to that file, like
a `static` in C; two files can each have their own `count`. The debugger
shows such a global with its file: `count@stats.w`. (When two included
files share a base name, the compiler adds a number: `count@util.w#2`.)

* In an expression evaluated in a frame of that file, plain `count` means
  the file's own `count`.
* From any other frame, `::count@stats.w` names it, and plain `count` does
  not find it.
* Add to Watch and Break on Value Change in VS Code use the `::count@stats.w`
  form, so they work from any frame.

The console session in [Demo 1](#demo-1-several-files-and-a-module) below
shows all three.

## Several programs per server

One `WrenchDebugServer` debugs up to `WRENCH_DEBUG_MAX_PROGRAMS` (4 by
default, about 64 bytes each) programs at once:

* **Program 0** is the first context the firmware attaches. It is the one a
  debugger launches, restarts and replaces with uploads.
* **Attached contexts**: firmware that runs more scripts in contexts of
  its own hands each one to the same server:

  ```cpp
  WRContext* ui = wr_newContext( w, uiBytecode, uiSize );
  int program = debugger->attach( ui, "ui.w" ); // -1 when all are taken
  wr_executeContext( ui );
  ...
  debugger->detach( ui ); // or wr_destroyContext( ui )
  ```

  The server does not own an attached context: destroying it is enough to
  stop debugging it.
* **Imported modules**: a module a debugged program loads becomes a
  program too, for as long as its importer lives (see
  [below](#imported-modules)).

Breakpoints, stepping, watchpoints and variables work in every program.
Each has its own globals and its own line numbers; locals are reached
through stack frames, which name their program.

The target is single threaded like the firmware: while one program is
halted, nothing else runs, the other programs wait. A step only ends on
its own stack: while you step through program 0, a call the firmware
makes into another attached context does not end the step there
([details](breakpoints.md#a-step-only-ends-on-its-own-stack)).

### In VS Code

* Each program with a stack of its own (program 0 and each attached
  context) is a thread in the Call Stack view: `wrench` for program 0, the
  name given to `attach()` for the others. A module runs on its importer's
  thread.
* The Globals scope of a frame shows the globals of the frame's program:
  "Globals" for program 0, "Globals of sensor.w" for another.
* The Modules view (the `...` menu of Run and Debug) lists all programs.
* A file of another program that is on this machine opens from there and
  takes breakpoints like any other.
* When program 0 ends on a target that runs attached contexts, the session
  goes on with them: "The program finished, the target keeps running its
  other programs."

### In wrench dbg

`programs` lists them; `>` marks the program of the selected frame:

```text
(wdb) programs
 0  main.w  (3 files)
>1  sensor.w
 2  import 2  (imported by program 0)
```

Locations in a program other than program 0 carry its name: "line 11 in
sensor.w", "line 3 in import 1". `b sensor.w:11` sets a breakpoint in
another program's file. When the firmware runs contexts of its own, `c`
while nothing is stopped waits for one of them to stop (Ctrl+C pauses).
The debugger also announces programs as they come and go:

```text
[program 1: import 1 imported by program 0]
[program 1 (import 1) is gone]
```

### Trying it on a PC

`wrench dbgserver -context sensor.w main.w` attaches a context for
`sensor.w` beside program 0, runs its global code and calls its `tick()`
ten times a second, as firmware would. Up to three `-context` options are
allowed (with the default four programs).

## Imported modules

A program can load more code while it runs, with
[`sys::importCompile()` or `sys::importByteCode()`](../language/import.md).
When the importer is debugged, the module becomes a program of its own,
until the importer is reset or destroyed:

* `sys::importCompile()` compiles the module with debug information and
  its source while a debugger is attached, so it can always be debugged.
* A module given to `sys::importByteCode()` is debuggable when it was
  compiled with `-debug`, and shows its source when compiled with
  `-debugsource`.
* A module compiled from a string has no file. VS Code shows it as a
  read-only document "import 1 (embedded source)", which takes breakpoints
  like a file; `wrench dbg` calls it "import 1".
* A call from a program into a function of a module it imported shows both
  in the call stack, and stepping goes in and out of the module. The stack
  goes through up to `WRENCH_DEBUG_MAX_IMPORT_CALLS` (8) nested calls
  into modules; deeper ones work but the stack ends at the module.
* A module's breakpoints outlive it: when the importer is reset (a new
  launch) the module goes, its breakpoints are parked on the target by the
  module's bytecode hash, and they are back the moment the same module is
  imported again.
* When all program slots are taken, a module runs undebugged.

A module imported for the first time cannot take breakpoints before its
global code runs, since its source arrives with it. Break in the importer
on the import line and step in, or set the breakpoints once it is loaded
and run the program again.

## The debug demo

`examples/debug_demo` is a short tour of all of this. Build wrench first
(`win32/wrench.sln` in Release on Windows, `make` on Linux), install the
extension built from the same sources, and open the folder as a VS Code
window of its own (`code examples/debug_demo`) so VS Code finds its
`.vscode` folder.

| File | What it is |
|---|---|
| `main.w` | the program: includes `shapes.w` and `stats.w`, compiles a module from a string with `sys::importCompile()` and calls into it |
| `shapes.w` | included from next to `main.w`: `struct Box`, `boxArea()` |
| `lib/stats.w` | included through `"include": [ "lib" ]` in `wrench.json`; its globals are `private` |
| `sensor.w` | a second program for `wrench dbgserver -context` |
| `wrench.json` | the host: all libraries, `println`, the include folder |
| `.vscode/launch.json`, `tasks.json` | two launch configurations, and the task that starts the debug server |

### Demo 1: several files and a module

In Run and Debug pick **Demo 1: main.w in a local VM** and press F5.

1. It stops on entry in `lib/stats.w`, line 2: the included files'
   globals are the first code the program runs.
2. Set breakpoints on line 9 of `shapes.w` and line 8 of `lib/stats.w`,
   then continue. The call stack shows `boxArea` in `shapes.w` over
   `<global>` in `main.w`; hover over `b` or expand it under Locals.
3. Stopped in `stats.w`, open Globals: the private variables show as
   `count@stats.w` and `sum@stats.w`. Hovering over `count` in `stats.w`
   works; in a frame of `main.w` only `::count@stats.w` names it.
4. Right-click `sum@stats.w`, **Break on Value Change**, remove the line
   breakpoints and continue: it stops wherever `sum` changes.
5. Remove that one too (Breakpoints view), set a breakpoint on line 23 of
   `main.w`, continue and Step Into. `label()` opens as the read-only
   document "import 1 (embedded source)", which takes breakpoints like a
   file. The Modules view lists `main.w` and `import 1`.
6. Continue: the Debug Console shows `total = 68 cm2` and `average = 22`.

The same in the console, `wrench dbg main.w` in that folder:

```text
host application described by D:\Temp\wrench\examples\debug_demo\wrench.json
connected to local, wrench 8.0.0, max packet 1024 bytes
main.w: 24 lines, 4 functions. 'h' for help.
includes 2 more files, 'b file.w:line' and 'l file.w:line' reach them
(wdb:idle) b stats.w:8
breakpoint at line 8 of stats.w
(wdb:idle) r

[breakpoint] line 8 of stats.w
in statsAdd, line 8 of stats.w
     6  {
     7  	count += 1;
*>   8  	sum += value;
     9  	return sum;
    10  }
(wdb) bt
>#0  statsAdd  line 8 of stats.w
 #1  <global>  line 15
(wdb) globals
globals:
  count@stats.w = 1
  sum@stats.w = 0
  sizes = [2, 3, 4, 5, 6, 7]
  info = {"boxes": 3, "unit": cm2}
  total = 0
  i = 0
  b = {width: 2, height: 3}
  area = 6
  module = 0
(wdb) p count
count = 1
(wdb) f 1
in <global>, line 15
    13  	var b = new Box( sizes[i * 2], sizes[i * 2 + 1] );
    14  	var area = boxArea( b );
 >  15  	total = statsAdd( area );
    16  }
    17  
(wdb) p count
count: no variable named 'count'
(wdb) p ::count@stats.w
::count@stats.w = 1
(wdb) b stats.w:8
breakpoint removed from line 8 of stats.w
(wdb) b 23
breakpoint at line 23
(wdb) c

[program 1: import 1 imported by program 0]

[breakpoint] line 23
in <global>, line 23
    21  sys::importCompile( module );
    22  
*>  23  println( label("total", total), " ", info["unit"] );
    24  println( label("average", statsAverage()) );
(wdb) s

[step] line 3 in import 1
in label, line 3 in import 1
     1  function label( name, value )
     2  {
 >   3  	var text = name + " = " + value;
     4  	return text;
     5  }
(wdb) bt
>#0  label  line 3 in import 1
 #1  <global>  line 23
(wdb) programs
 0  main.w  (3 files)
>1  import 1  (imported by program 0)
(wdb) c
total = 68 cm2
average = 22

[ended, returned 0]
(wdb:idle) q
```

Plain `count` finds the private global in a frame of `stats.w`, but not
after `f 1` selected the frame in `main.w`; `::count@stats.w` works from
both. `b 23` meant `main.w` because the selected frame was in `main.w`:
without `f 1`, it would have meant line 23 of `stats.w` ("no code at or
after 23"). The call stack after Step Into goes from the module's `label`
back into `main.w`: one stack, two programs.

### Demo 2: two programs on one target

Pick **Demo 2: main.w on dbgserver, beside sensor.w** and press F5. The task
**wrench dbgserver** starts first in a terminal and keeps running:

```bash
wrench dbgserver -target tcp:5050 -context sensor.w -v main.w
```

It acts like a board on the network that runs `sensor.w` itself; VS Code
uploads `main.w` to it, as it would to an ESP32 over WiFi.

1. `main.w` stops on entry as in Demo 1. The Call Stack has two threads:
   `wrench` (`main.w`) and `sensor.w`.
2. Set a breakpoint on line 11 of `sensor.w` and continue. The target is
   single threaded like firmware, so `sensor.w` waits while `main.w` is
   stopped. `main.w` runs to its end, the Debug Console says the target
   keeps running its other programs, and `sensor.w` stops in `tick()`.
3. Globals there is "Globals of sensor.w": `readings` and `history`, not
   the globals of `main.w`. Continue a few times and watch them change.
4. Restart the session (Ctrl+Shift+F5): `main.w` is uploaded and runs once
   more, while `readings` goes on counting, because `sensor.w` belongs to
   the target. The module `main.w` imports is "import 2" here; program 1
   is `sensor.w`.
5. To end it, stop the debug session (Shift+F5), then the task (Terminal,
   Terminate Task). The server stays up between sessions until then.

[The console debugger](console.md#debugging-a-remote-target) shows the
same target from `wrench dbg`.
