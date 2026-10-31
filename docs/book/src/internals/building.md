# Source layout and building from source

This page is for people who build wrench from its sources, change it or
contribute: where everything is, how to build the `wrench` tool on Windows
and Linux, and the code style the project follows. To use wrench in your
own program you only need the two files of the single file build (see
[Releases and the single-file build](releases.md)).

## Source layout

| Folder | What it holds |
|---|---|
| `src/wrench.h` | the one header of the library: build options, the API, the value and context types |
| `src/cc` | the compiler: tokenizer, parser and code generator, expressions, optimizer, includes, linker |
| `src/vm` | the virtual machine: the interpreter (`vm.cpp`), operator tables (`operations*.cpp`), indexing, the garbage collector, the scheduler |
| `src/lib` | the standard library (`std*.cpp`), the `debug::` library and the device libraries for Arduino and FastLED |
| `src/debug` | the debugger: wire protocol, the debug server that runs next to the VM, transports (TCP, serial), the host side client, the console debugger, the disassembler |
| `src/utils` | the rest: context creation and image checks (`utils.cpp`), the serializer, and the host tools: the command line (`wrench_cli.cpp`), the debug adapter (`wrench_dap.cpp`), the language server (`wrench_lsp.cpp`), `wrench.json` (`wrench_project.cpp`), JSON, the debugger self test |
| `tests` | the test scripts and their list, see [The test suite](testing.md) |
| `tools/test` | end to end tests of `wrench dap`, `wrench dbg`, `wrench lsp`, the other commands of the command line and the VS Code extension, in JavaScript for Node |
| `tools/vscode-wrench` | the VS Code extension, see [Building the extension](../vscode/building.md) |
| `tools/bookcheck` | the checker that runs the examples of this book (`dotnet run --project tools/bookcheck`), see [The test suite](testing.md#continuous-integration) |
| `examples` | `basic.cpp` and `multi_context.cpp` (host programs), `arduino/hello_world`, `debug_targets` (firmware with the debugger for Arduino, ESP32, ESP-IDF and STM32), `debug_demo` (a VS Code tour) |
| `win32` | the Visual Studio solution and project |
| `www` | the old project website and its performance comparisons with other interpreters |
| `docs` | this book (`docs/book`) and the project's working notes |
| `.github/workflows` | continuous integration, see [The test suite](testing.md#continuous-integration) |

The library proper (compiler, VM, libraries and the debug server) is the
part that goes into firmware. It is plain C++98, uses no standard library
containers and builds for any target with a C++ compiler. The host tools
(the debug client, the console debugger, the debug adapter, the language
server) need C++17 and the standard library; they are built only with
`WRENCH_INCLUDE_DEBUG_CLIENT` and are not part of the single file build.

## Building on Windows

Open `win32/wrench.sln` in Visual Studio 2022 (platform toolset v143) and
build it, or from a Developer Command Prompt:

```bash
msbuild win32/wrench.sln -p:Configuration=Release -p:Platform=x86 -m -v:minimal -warnaserror
```

* The configurations are `Debug` and `Release`. The executable lands in
  `win32/Debug/wrench.exe` or `win32/Release/wrench.exe`.
* The solution platform is `x86`, which builds the project's `Win32`
  platform: a 32 bit executable that also runs on 64 bit Windows. There is
  no x64 configuration.
* Both configurations define `WRENCH_INCLUDE_DEBUG_CODE`,
  `WRENCH_INCLUDE_DEBUG_CLIENT`, `WRENCH_WIN32_TCP`, `WRENCH_WIN32_SERIAL`
  and `WRENCH_WIN32_FILE_IO`, and compile as C++17 with warning level 3.
  CI builds both with `-warnaserror`.

Debug builds keep the `assert()` checks of the internal tests; run the
test suite with both (see [The test suite](testing.md)).

## Building on Linux

`GNUmakefile` in the repository root builds `wrench` there, with objects in
`objs_linux/`. It needs GNU make and g++.

```bash
make -j8
./wrench t
```

| Target | Builds |
|---|---|
| `all`, `wrench` | the `wrench` tool (the default) |
| `test` (also `tests`, `tested`) | `wrench`, then the cross module globals test program from `src/utils/test_wrench_cross_module_globals.cpp`; runs it and `./wrench t` |
| `valgrind` | `wrench_v`, the same objects linked with `-ggdb` |
| `dist` (also `release`) | the single file build in `dist/` |
| `dist-check` | `dist`, then compiles it in several configurations and runs `examples/basic.cpp` against it |
| `clean` | removes objects, executables and `dist/` |

The variables, each with its default, can be set on the command line:

| Variable | Default | Use |
|---|---|---|
| `OPT` | `-O3` | optimization and debug information |
| `COMPACT` | empty | build options for the library, for example `-DWRENCH_COMPACT` or `"-DWRENCH_COMPACT -DWRENCH_REALLY_COMPACT"` |
| `DEBUG` | `-DWRENCH_INCLUDE_DEBUG_CODE -DWRENCH_INCLUDE_DEBUG_CLIENT` | the debug server and the host tools |
| `PLATFORM` | `-DWRENCH_LINUX_FILE_IO -DWRENCH_LINUX_SERIAL -DWRENCH_LINUX_TCP` | file I/O, serial and TCP for Linux |
| `C_VER` | `-std=c++98` | the language standard of the library |
| `HOST_C_VER` | `-std=c++17` | the language standard of the host tools |
| `PERF` | empty | extra compiler flags, for profiling |
| `LDFLAGS` | empty | extra linker flags |

Everything compiles with `-Wall -Werror`. The build options themselves
are described in [Build flags](../reference/build-flags.md).

> [!IMPORTANT]
> The objects do not depend on the variables. After changing `OPT`,
> `COMPACT` or another variable, run `make clean` first, or old objects
> built with the old flags are linked in.

The configurations CI builds:

```bash
make COMPACT=-DWRENCH_COMPACT
make COMPACT="-DWRENCH_COMPACT -DWRENCH_REALLY_COMPACT"
make COMPACT=-DWRENCH_UNPROTECTED_STACK
make OPT="-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined" LDFLAGS="-fsanitize=address,undefined"
```

The last one runs the tests under AddressSanitizer and
UndefinedBehaviorSanitizer; CI sets `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`
so any finding fails the run.

### A Linux binary for any distribution

The copy of `wrench` bundled with the VS Code extension is linked
statically and stripped, so it runs on any x86-64 distribution:

```bash
make clean
make -j8 LDFLAGS='-static -s'
```

The linker warns that `getaddrinfo` in a static program needs the shared
libraries of the glibc it was linked with at run time. That function is
only used to look up a host name for a TCP debug target
(`-target tcp:<host>:<port>`); a numeric address such as `127.0.0.1` works
everywhere.

On Windows, build it in WSL with a Linux distribution that has g++ and
make, in the repository folder under `/mnt`.

## Code style

The sources follow one style; contributions should too.

* Indentation with tabs, never spaces.
* Braces on their own lines, and braces around every block, also a single
  statement:

  ```cpp
  if ( count == 0 )
  {
  	return 0;
  }
  ```

* Spaces inside the parentheses of calls and control statements:
  `if ( x )`, `wr_run( w, out, outLen )`. Older code also has the compact
  form `for( int i=0; i<n; ++i )`; new code uses the spaced one.
* Names: `wr_` for functions of the C style API (`wr_newState()`), `WR`
  for its types (`WRValue`, `WRContext`), `Wrench` for the classes of the
  debugger and the host tools (`WrenchDebugServer`), `m_` for members,
  `g_` for globals, `c_` for constant tables.
* The library and the debug server are C++98 and allocate nothing they do
  not need (the debug server allocates nothing at all); the host tools are
  C++17.
* Comments say why, not what; plain ASCII in comments, strings and
  messages.
* New behaviour gets a test (see [The test suite](testing.md)) and a line
  in the `CHANGELIST` (see [Releases](releases.md#the-changelist)).

The book itself has its own rules for text and examples, in
`docs/book/STYLE.md`.
