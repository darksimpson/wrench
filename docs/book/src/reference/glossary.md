# Glossary

The terms this book uses with a specific meaning, in alphabetical order,
each with the page that explains it.

**attach**
: Debugging what a target already runs, without uploading or starting
  anything; the opposite of a launch. See
  [Launch configurations](../vscode/launch.md#launch-and-attach).

**breakOnEntry**
: A call of the debug server that halts the next script code on its first
  line and waits there until a debugger attaches; `wrench dbgserver -hold`
  does the same. See [Adding the debugger to your firmware](../debugging/firmware.md).

**bytecode image**
: The block of bytes the compiler produces and the VM runs, the `.bin`
  file of `wrench c`: header, function table, optional symbols and source,
  code and checksum. See [The bytecode image](../internals/bytecode.md).

**call marker**
: A `DebugInfo` instruction before a call that tells the debugger which
  function is called. See [Debug information](../internals/debug-info.md#line-markers).

**checksum**
: The last 4 bytes of a bytecode image, a hash of the rest plus the major
  version of the compiler that made it; an image whose checksum does not
  match this VM's major version (or, for an older image, the one before)
  is not loaded. See
  [The bytecode image](../internals/bytecode.md#the-checksum).

**context**
: One loaded program in a state, with its own globals and (unless it is a
  module) its own stack; `wr_run()` and `wr_newContext()` create one. See
  [States, contexts and running code](../embedding/running.md).

**DAP**
: The Debug Adapter Protocol between VS Code and a debug adapter; `wrench
  dap` implements it. See [Launch configurations](../vscode/launch.md).

**debug server**
: The `WrenchDebugServer` that runs next to the VM on a target and talks to
  a debugger over a transport. See
  [Adding the debugger to your firmware](../debugging/firmware.md).

**debug symbols**
: The section of a bytecode image compiled with `-debug` or `-debugsource`
  that names functions, variables and files. See
  [Debug information](../internals/debug-info.md#the-symbols-section).

**dbgserver**
: `wrench dbgserver`, the command line tool acting as a debug target on a
  PC, over TCP or a serial port. See
  [Command line tool](cli.md#dbgserver-a-debug-target-on-this-pc).

**embedded source**
: The text of a program's files stored in its bytecode image by
  `-debugsource`, so a debugger can show it without the `.w` files. See
  [Debug information](../internals/debug-info.md#the-embedded-source).

**firmware**
: The program of a device (an ESP32, an STM32, an Arduino board) that
  embeds the wrench VM and runs scripts. See
  [Debug targets: the PC and devices](../debugging/targets.md).

**garbage collector**
: The part of the VM that frees strings, arrays, hash tables and struct
  instances nothing refers to any more; it only runs when a script
  allocates. See [The virtual machine](../internals/vm.md#garbage-collection).

**host**
: The application or firmware that embeds wrench, registers natives and
  runs scripts. See [How wrench is put together](../embedding/architecture.md).

**host driven**
: A program whose firmware keeps calling its functions after the global
  code has finished, so the debug session goes on; set with
  `setHostDriven( true )`. See
  [Adding the debugger to your firmware](../debugging/firmware.md).

**include**
: `include "file.w"` at the top level compiles another file into the
  program at compile time, once. See
  [Several files: include and private](../language/include.md).

**launch**
: A debug session that compiles a program, uploads it to the target and
  starts it; the opposite of attach. See
  [Launch configurations](../vscode/launch.md#launch-and-attach).

**launchable**
: A target whose firmware called `setLaunchable( true )` and so lets a
  debugger launch, restart and upload program 0. See
  [Adding the debugger to your firmware](../debugging/firmware.md).

**library constant**
: A named constant a host registers, used as `lib::NAME`. See
  [Native functions and libraries](../embedding/natives.md).

**library function**
: A function called with a namespace, `math::sin()` or your own
  `app::log()`, registered with `wr_registerLibraryFunction()`; it works
  directly on the VM's stack. See
  [Native functions and libraries](../embedding/natives.md).

**line marker**
: A `DebugInfo` instruction with a line number in front of a statement's
  code, emitted with `-debug`; the debugger stops and steps on them. See
  [Debug information](../internals/debug-info.md#line-markers).

**local target**
: The VM inside `wrench dap` or `wrench dbg` that runs a program on the PC
  when no other target is named; it imitates the host of `wrench.json`.
  See [Describing your host application](../vscode/wrench-json.md#the-local-debug-target).

**LSP**
: The Language Server Protocol between an editor and a language server;
  `wrench lsp` implements it. See [Editing features](../vscode/editing.md).

**module**
: Code a running program loads with `sys::importCompile()`,
  `sys::importByteCode()` or `wr_import()`; it runs on its importer's stack
  and the importer can call its functions. See
  [Modules: importing code at run time](../language/import.md).

**native**
: A C or C++ function a host registers with `wr_registerFunction()` and
  scripts call by its plain name. See
  [Native functions and libraries](../embedding/natives.md).

**private**
: A top level `var` or `function` marked `private` belongs to its file,
  like `static` in C. See [Several files: include and private](../language/include.md).

**program**
: A script compiled into one bytecode image, with the files it includes.
  To a debug server, a program is also one debugged context, numbered from
  0; program 0 is the one a debugger can launch and upload. See
  [Several files, programs and modules](../debugging/programs.md).

**raw array**
: An array of the host's memory handed to a script, which the script reads
  and writes in place and the garbage collector leaves alone. See
  [Passing data: containers, globals, user data](../embedding/data.md).

**single file build**
: `wrench.h` and `wrench.cpp`, the library in two files, generated from
  `src/` and attached to every release. See
  [Releases and the single-file build](../internals/releases.md).

**state**
: A `WRState`, made by `wr_newState()`: the natives, libraries and
  contexts that belong together. See
  [States, contexts and running code](../embedding/running.md).

**target**
: Where a debugged program runs: the local VM, a PC running `wrench
  dbgserver` or your application, or a device. See
  [Debug targets: the PC and devices](../debugging/targets.md).

**time slice**
: With `WRENCH_TIME_SLICES`, a budget of jumps after which the VM yields
  on its own, so one script cannot block the others. See
  [Yield, time slices and the scheduler](../embedding/scheduling.md).

**transport**
: The byte link between a debugger and a debug server: TCP, a serial
  port, or an in-process pipe for the local target. See
  [Launch configurations](../vscode/launch.md#transports) and
  [Wire protocol reference](../debugging/protocol.md).

**unit**
: In the compiler and the image, the global code (unit 0) or one function
  (unit n is function n - 1). In the language, `unit` is another word for
  `function`. See [The virtual machine](../internals/vm.md#units).

**upload**
: Sending a compiled program from the debugger to a target, which replaces
  its program 0; a target may refuse it. See
  [Launch configurations](../vscode/launch.md#upload).

**virtual line**
: A line number in a program made of several files, counted across the
  included files as one sequence; the file table maps it back to a file
  and line. See [Debug information](../internals/debug-info.md#the-file-table).

**watchpoint**
: A variable, element or member the debugger watches: the program stops
  on the line after it changes. See
  [Breakpoints, stepping and watchpoints](../debugging/breakpoints.md).

**wrench.json**
: The file that describes a host application to the tools: its natives,
  libraries and include folders. See
  [Describing your host application: wrench.json](../vscode/wrench-json.md).

**WRValue**
: The type of every value in the VM and the API: two machine words
  holding an int, a float or a reference to an object. See
  [Values: WRValue](../embedding/values.md).

**yield**
: `yield()` in a script returns control to the host, which can continue
  the script later where it stopped. See
  [Yield and cooperative tasks](../language/yield.md).
