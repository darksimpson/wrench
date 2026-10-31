# What wrench is

Wrench is a scripting language and the compiler and virtual machine that run
it, written for programs where memory is scarce and speed matters. This
page explains what it offers, where it fits and where it does not, and how
its pieces relate.

## A small language for tight places

The language looks like C with the type names removed:

```wrench
struct Reading( s, v )
{
	var sensor = s;
	var value = v;
};

var limits = { "temperature":80, "humidity":60 };

function check( r )
{
	if ( r.value > limits[r.sensor] )
	{
		println( r.sensor, " too high: ", r.value );
		return 1;
	}
	return 0;
}

var hot = new Reading( "temperature", 85 );
var dry = new Reading( "humidity", 40 );
check( hot );
check( dry );
```

Output:

```text
temperature too high: 85
```

Values carry their type at run time: 32 bit integers, floats, strings,
arrays, hash tables and structs. There are functions, `if`, `while`, `for`
and `for` over containers, `do`, `switch` (a real jump table), `goto`,
enums, programs made of several files, and modules compiled while a program
runs. The [tour](tour.md) shows all of it in a few minutes.

## Why it exists

Wrench was written for an embedded system with 32 KB of RAM, most of it
needed for other work. The established interpreters compiled for that chip,
but needed far more memory than it had to actually run scripts. Wrench is
the answer to that constraint, while staying approachable for people who
are not language experts: the scripts are meant to be written by users of a
device or an application, not only by its developers.

## What you get

* **Small.** The virtual machine needs a few kilobytes of RAM and fits in
  the flash of an Arduino class microcontroller. The compiler is optional on
  the target: compile on a PC and ship bytecode.
* **Compact, portable bytecode.** A compiled program is a small image that
  runs on any target regardless of endianness and word size, and can run
  directly from ROM or flash without being copied to RAM.
* **Fast.** A register based virtual machine with an optimizing compiler.
  The project's own comparisons (in `www/perf` in the repository) put it at
  or above the speed of the standard (non JIT) Lua interpreter.
* **Two source files.** The whole library ships as `wrench.h` and
  `wrench.cpp`, plain C++98, with no other dependencies. Every release
  carries them, built and checked by CI.
* **Made to be extended.** Your program registers native functions that
  scripts call, calls script functions in return, reads and writes script
  globals and passes containers both ways. Calls in either direction cost
  little.
* **Several scripts at once.** One state runs any number of contexts, each
  with its own globals; scripts can yield, and the host resumes them, for
  cooperative multitasking.
* **Memory under control.** Numbers need no allocation at all. Strings and
  containers created at run time are garbage collected, and the collector
  only works once a configurable amount of memory has been allocated, so
  scripts that only compute with numbers never trigger it.
* **A real debugger.** Breakpoints, stepping, call stacks, variables,
  watchpoints and runtime error stops, on the PC or on the device itself
  over a serial port, USB or TCP. The debugger costs nothing unless it is
  compiled in.
* **Tools.** A command line tool compiles, runs, disassembles and debugs;
  a Visual Studio Code extension adds editing support (errors as you type,
  completion, go to definition) and debugging.
* **Tested.** A test suite runs every script twice, with and without debug
  instrumentation, plus internal API tests and debugger protocol checks, on
  Windows and in several Linux configurations, including address and
  undefined behaviour sanitizers.

## Where it fits, and where it does not

Wrench is a good fit when:

* scripts must run on a microcontroller, or in very little memory anywhere
* users of your product write the scripts, and C-like syntax suits them
* you want to update behaviour without reflashing firmware: send new
  bytecode instead
* the host must stay in control: it decides what scripts may call, when
  they run and how long

It is not the right tool when a JIT compiled language is available and
memory is plentiful (it will be faster), or when scripts need a large
ecosystem of existing libraries.

## The pieces

| Piece | What it does | Read more |
|---|---|---|
| Compiler | turns wrench source into a bytecode image, on the PC or on the target | [Compiling scripts](../embedding/compiling.md) |
| Virtual machine | runs bytecode images, inside your program | [States, contexts and running code](../embedding/running.md) |
| Standard library | math, strings, containers, files, messages, and device libraries | [Standard library](../library/index.md) |
| Command line tool `wrench` | compile, run, disassemble, test, debug | [Command line tool](../reference/cli.md) |
| Debug server | lives next to the VM in your program or firmware | [Adding the debugger to your firmware](../debugging/firmware.md) |
| `wrench dbg`, `wrench dap`, `wrench lsp` | console debugger, VS Code debug adapter and language server | [Debugging](../debugging/overview.md) |
| VS Code extension | editing and debugging in Visual Studio Code | [Trying it out](../vscode/quick-try.md) |

## License

Wrench is free software under the MIT license, written by Curt Hartung with
contributions from others. The single file packaging was inspired by
[Wren](https://wren.io).
