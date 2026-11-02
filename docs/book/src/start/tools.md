# Getting the tools

This page tells you where to get wrench, how to build the `wrench` command
line tool on Windows and Linux, what the tool does, and where the VS Code
extension comes in. You need the tool to compile scripts and to debug them;
to embed wrench in a program you need only two source files.

## The single file build

The whole library, compiler, VM, standard library and debug server, comes
as two files: `wrench.h` and `wrench.cpp`. Add both to your program and you
have everything ([Quick start: Windows and Linux](../embedding/quickstart-pc.md)).

* **From a release.** Every release on the
  [Releases page](https://github.com/darksimpson/wrench/releases) has
  `wrench.h` and `wrench.cpp` attached. CI generates them from `src/`,
  compiles them with GCC and MSVC in several configurations, and runs an
  example against them before publishing.
* **From a checkout.** The single file build is not kept in git. Generate it
  from the sources in `src/`, with a `wrench` tool you built:

  ```bash
  wrench release src dist
  ```

  writes `dist/wrench.h` and `dist/wrench.cpp`, creating `dist` when it
  is missing. On Linux, `make dist` builds the tool and does the same. The
  command also writes `version.txt` into the folder that holds `src`.

Use the files and the `wrench` tool from the same version: bytecode the tool
compiles is checked against the version of the VM that runs it.

The sources in `src/` are the same code split into folders (`cc` the
compiler, `vm` the interpreter, `lib` the libraries, `debug` the debugger,
`utils` the rest); [Source layout and building from
source](../internals/building.md) describes them.

## Building the command line tool

Every release also has the tool built: `wrench-windows-x86.exe` for
Windows, and `wrench-linux-x64.tar.gz` for Linux, a static build that runs
on any distribution (`tar -xzf wrench-linux-x64.tar.gz` gives `wrench`).
To build it yourself:

The tool includes the host side of the debugger and the language server,
so it needs a C++17 compiler; the library itself stays C++98.

**Windows.** Open `win32/wrench.sln` in Visual Studio 2022, choose the
`Release` configuration and the `x86` platform (`Win32` in the project),
and build. The tool is `win32/Release/wrench.exe`. From a Developer
Command Prompt:

```bash
msbuild win32/wrench.sln -p:Configuration=Release -p:Platform=x86
```

**Linux.** In the repository root:

```bash
make
```

builds `./wrench` with GCC, with file I/O, TCP and serial support, the
debugger and the language server. Other targets: `make test` runs the test
suite, `make dist` writes the single file build to `dist/`, `make clean`
removes the build. `make LDFLAGS=-static` gives a binary that runs on any
distribution.

Check the build with `wrench t`, which runs the test scripts in `tests/`
and the internal tests ([The test suite](../internals/testing.md)).

## What the tool does

Run `wrench` without arguments for the full help. The commands, in short;
[Command line tool](../reference/cli.md) has every option.

**c** compiles a script into a bytecode file: `wrench c main.w main.bin`.
Options before the file names: `-debug` (line information for the
debugger), `-debugsource` (that and the source), `-noglobals`, `-nostrict`,
`-I folder` for includes.

**ch** compiles a script into a C header with the bytecode as a
`const unsigned char` array, for firmware: `wrench ch main.w main.h main`
defines `main_bytecode` and `main_bytecodeSize`.

**p** compiles a script and prints the disassembly of the result.

**r** compiles a script and runs it, with the standard library and
`print()` / `println()`: the quickest way to try a script.

**rb** runs a compiled bytecode file; **d** prints its disassembly.

**dbg** is the console debugger: for a script on a local VM
(`wrench dbg main.w`), or on a device over TCP or a serial port
(`-target tcp:host:port`, `-target serial:COM5:115200`), uploading the
program or attaching to the one the device runs
([The console debugger](../debugging/console.md)).

**dbgserver** makes the PC a debug target, as a device would be: it runs a
VM with a debug server on a TCP port or serial port, for testing debuggers
and host setups without hardware.

**dap** is the Debug Adapter Protocol server and **lsp** the Language
Server Protocol server. VS Code starts them; you do not run them yourself.

**t** runs the test suite, **v** shows the version and the help, and
**release** writes the single file build, as above.

## The VS Code extension

`tools/vscode-wrench` is a VS Code extension for wrench: highlighting,
compiler errors as you type, completion, navigation, and a debugger for
local, TCP and serial targets. It talks to the `wrench` tool (`wrench lsp`
and `wrench dap`). [Trying it out](../vscode/quick-try.md) gets it running.
