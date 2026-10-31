# Build flags

Every compile-time define that changes how wrench is built: its default,
what it does, and where it applies. For advice on choosing them see
[Small targets](../embedding/small-targets.md).

## Where to set them

* On the compiler command line: `-DWRENCH_COMPACT` (GCC, Clang),
  `/DWRENCH_COMPACT` (MSVC), `target_compile_definitions()` in CMake.
* In a file named `wrench_config.h` anywhere on the include path, usually
  next to `wrench.h`. `wrench.h` includes it before anything else if the
  compiler supports `__has_include` (all current GCC, Clang and MSVC
  versions do). This is the way for the Arduino IDE, ESP-IDF components and
  STM32Cube projects, where passing defines to library sources is awkward.

Set them the same way for `wrench.cpp` and for every file that includes
`wrench.h`. Several flags change the layout of `WRState`, `WRContext` or
other types (marked "layout" below); a mismatch between files breaks the
program in ways that are hard to find. A `wrench_config.h` next to
`wrench.h` guarantees that.

| Flag | Default | Effect |
|---|---|---|
| `WRENCH_NO_CONFIG_HEADER` | not defined | do not look for `wrench_config.h` |

## Size and speed

| Flag | Default | Effect | Platforms |
|---|---|---|---|
| `WRENCH_WITHOUT_COMPILER` | not defined | leaves the compiler out; the compile functions return `WR_ERR_compiler_not_loaded`, `c_errStrings` and the disassembler are gone | all, for targets that only run bytecode |
| `WRENCH_COMPACT` | not defined | smaller interpreter: no unrolled loops or inlined paths, shared code through function pointers; about 45% less VM code, somewhat slower | all |
| `WRENCH_REALLY_COMPACT` | not defined | also replaces the computed goto dispatch with a `switch`; a few KB smaller, slower again; defines `WRENCH_COMPACT` | all |
| `WRENCH_UNALIGNED_READS` | not defined | reads 16 and 32 bit operands of the code stream with one load; only on little endian targets that allow unaligned loads (x86, Cortex-M3 and later), never where they fault (Cortex-M0, ESP32 Xtensa) | little endian |
| `READ_8_FROM_PC( P )`, `READ_16_FROM_PC( P )`, `READ_32_FROM_PC( P )` | defined by wrench for the detected byte order | how the VM reads the code stream; define your own for memories that need special access, they must match the target's byte order | special memories |

The interpreter uses computed goto dispatch (`WRENCH_JUMPTABLE_INTERPRETER`,
set inside `vm.cpp`) with GCC and Clang, and a `switch` with MSVC and with
`WRENCH_REALLY_COMPACT`. It is not a user setting.

## Limits

| Flag | Default | Effect |
|---|---|---|
| `WRENCH_DEFAULT_STACK_SIZE` | 64 | the stack size `wr_newState()` uses without an argument, in values (8 bytes each on 32-bit targets) |
| `WRENCH_MAX_ARRAY_ELEMENTS` | `0x1FFFFF` | the largest array a script can create; indexes past it read 0 and writes are dropped. Cannot be raised: element references hold the index in 21 bits |
| `WRENCH_MAX_PARSE_DEPTH` | 256 | how deeply the compiler lets statements, expressions and initializers nest before `WR_ERR_nesting_too_deep`; each level costs about 2 KB of native stack on a desktop build, lower it for a compiler on a small task stack |
| `WRENCH_MAX_INCLUDE_DEPTH` | 16 | how deeply `include` may nest before `WR_ERR_include_too_deep` |
| `WRENCH_GC_MARK_STACK_SIZE` | 32 | entries of the collector's mark stack, on the native stack (one pointer each); deeper data still works, with extra passes |
| `WRENCH_MAX_SERIAL_DEPTH` | 64 | how deeply `wr_serialize()` / `wr_deserialize()` descend into nested containers |
| `WRENCH_MAX_IO_READ` | `0x100000` | the largest single `io::read()` request honoured (Windows and Linux file I/O) |
| `WRENCH_DEFAULT_ALLOCATED_MEMORY_GC_HINT` | 4000 | bytes a context allocates before its collector runs; change it at run time with `wr_setAllocatedMemoryGCHint()` (the define is not guarded by `#ifndef`, so redefining it gives a warning) |

## Safety and behaviour

| Flag | Default | Effect |
|---|---|---|
| `WRENCH_UNPROTECTED_STACK` | not defined | removes the stack overflow check on every push; an overflow then overwrites the memory behind the stack |
| `WRENCH_PROTECT_STACK_FROM_OVERFLOW` | defined unless `WRENCH_UNPROTECTED_STACK` is | set by `wrench.h`, not by you; turns the check on |
| `WRENCH_HANDLE_MALLOC_FAIL` | not defined | checks every allocation; a failure stops the script with `WR_ERR_malloc_failed`. Adds a test after each instruction that may allocate. Declares `g_mallocFailed`, which every call into the VM clears at its start |
| `WRENCH_TRAP_DIVISION_BY_ZERO` | not defined | `/`, `%`, `/=` and `%=` by zero (int or float; a `%` by a float that truncates to 0 counts) fail with `WR_ERR_division_by_zero` instead of giving 0, in every interpreter including `WRENCH_COMPACT` |
| `WRENCH_TIME_SLICES` | not defined | layout. Counts jumps and branches and forces a yield after the limit set with `wr_setInstructionsPerSlice()`; enables `wr_forceYield()`, `wr_slicesUsedLastCall()` and `WrenchScheduler` |
| `WRENCH_FLOAT_SPRINTF` | not defined | `%f`, `%e` and `%g` in `str::format` and friends are formatted by the C library's `snprintf`, a sizeable amount of code on small targets where nothing else uses it; without it wrench formats them itself, from the exact value of the float, as a correct C library does |
| `WRENCH_THREAD_LOCAL` | `__declspec(thread)` with MSVC, `__thread` with other compilers on Windows, Linux, macOS and ESP-IDF; empty elsewhere | the storage class of the VM's few statics (scratch values, `g_mallocFailed`); define it (empty to turn it off) to choose yourself |

## Files and network

| Flag | Default | Effect | Platforms |
|---|---|---|---|
| `WRENCH_WIN32_FILE_IO` | not defined | the `io::` library with Windows file functions | Windows |
| `WRENCH_LINUX_FILE_IO` | not defined | the `io::` library with POSIX file functions | Linux |
| `WRENCH_SPIFFS_FILE_IO` | not defined | the `io::` library on SPIFFS | Arduino cores with SPIFFS (ESP32, ESP8266) |
| `WRENCH_LITTLEFS_FILE_IO` | not defined | the `io::` library on LittleFS | Arduino cores with LittleFS |
| `WRENCH_FS_STDOUT` | `Serial` | the Arduino `Print` object `str::printf` writes to with the SPIFFS or LittleFS backend | Arduino |
| `WRENCH_WIN32_TCP` | not defined | the `tcp::` library, and the debugger's `WrenchDebugTcpServerTransport` / `WrenchDebugTcpClientTransport`; links Winsock (`Ws2_32.lib`) | Windows |
| `WRENCH_LINUX_TCP` | not defined | the same with BSD sockets | Linux |
| `WRENCH_WIN32_SERIAL` | not defined | the debugger's `WrenchDebugSerialTransport` on COM ports | Windows |
| `WRENCH_LINUX_SERIAL` | not defined | the same on `/dev/tty...` devices | Linux |

Define at most one of the file I/O flags. Without any, `wr_loadIOLib()`
registers nothing and `str::printf` writes to `stdout`. There is no flag for
a custom file backend; the functions a backend provides are declared in
`src/lib/std_io_defs.h`.

## Devices

| Flag | Default | Effect |
|---|---|---|
| `ARDUINO` | set by the Arduino toolchain | includes `Arduino.h`, builds and declares the Arduino libraries (`wr_loadAllArduinoLibs()`, `wr_loadArduinoWireLib()` and the others) and, with the debugger, `WrenchDebugStreamTransport` |
| `WRENCH_ARDUINO_FASTLED` | not defined | builds the `fastled::` library and declares `wr_loadFastLEDLib()`; needs the FastLED library |
| `WR_ARDUINO_LCD` | not defined | the name of a display object with the LiquidCrystal interface that `lcd::` drives, for example `#define WR_ARDUINO_LCD lcd`; without it `wr_loadArduinoLCDLib()` registers nothing (see [Devices](../library/devices.md)) |
| `WR_FASTLED_DATA_PIN` | 6 | the data pin of the LED strip, fixed at compile time by FastLED |
| `ARDUINO_BIG_ENDIAN`, `ARDUINO_LITTLE_ENDIAN` | not defined | force the byte order where wrench cannot detect it |

`WRENCH_BIG_ENDIAN` or `WRENCH_LITTLE_ENDIAN` is defined by `wrench.h` from
the compiler's predefined macros; an unknown platform stops the build with
an error.

## Debugger

| Flag | Default | Effect |
|---|---|---|
| `WRENCH_INCLUDE_DEBUG_CODE` | not defined | layout. The VM's debug hooks and `WrenchDebugServer`, the target side of the debugger. Code not compiled with `WR_EMBED_DEBUG_CODE` runs at full speed regardless |
| `WRENCH_INCLUDE_DEBUG_CLIENT` | not defined | the host side (`WrenchDebugClient`, used by `wrench dbg` and `wrench dap`); C++17 and the standard library, PCs only, and only in builds from `src/`: the single file build has the protocol and the transports but not the client |
| `WRENCH_DEBUG_MAX_PAYLOAD` | 1024 | the largest message either side sends; the server keeps two buffers of this size. The client adapts to the target's value. 128 to 256 on small MCUs |
| `WRENCH_DEBUG_MAX_BREAKPOINTS` | 32 | line breakpoints the target holds, 4 bytes each |
| `WRENCH_DEBUG_MAX_HANDLES` | 64 | containers being inspected per stop, 12 bytes each |
| `WRENCH_DEBUG_MAX_WATCHES` | 4 | watchpoints; every line marker checks each one |
| `WRENCH_DEBUG_MAX_WATCH_STEPS` | 4 | how deep a watch may reach into containers (`a[1].k` is two steps) |
| `WRENCH_DEBUG_MAX_PROGRAMS` | 4 | programs one server debugs: attached contexts and the modules they import, about 60 bytes each |
| `WRENCH_DEBUG_PROGRAM_NAME` | 24 | bytes kept of a name given to `attach()` |
| `WRENCH_DEBUG_MAX_IMPORT_CALLS` | 8 | nested calls into imported modules that show their callers in a stack trace |
| `WRENCH_DEBUG_POLL_MS` | 20 | while script code runs, the link is polled for pause requests and breakpoint changes once this many milliseconds have passed |
| `WRENCH_DEBUG_POLL_LINES` | 8 | the clock is read every this many source lines to decide whether to poll |

`WRENCH_DEBUG_HAS_TCP` and `WRENCH_DEBUG_HAS_SERIAL` are set by `wrench.h`
when a TCP or serial flag is defined. The firmware side is described in
[Adding the debugger to your firmware](../debugging/firmware.md).

## Defined by wrench

These are set by the headers; read them, do not define them.

| Macro | Meaning |
|---|---|
| `WRENCH_VERSION_MAJOR`, `WRENCH_VERSION_MINOR`, `WRENCH_VERSION_BUILD` | the version, 8.0.0 for this book; the major version is part of every bytecode checksum |
| `WRENCH_PREVIOUS_IMAGE_VERSION` | 7, the major version before this one; the VM also runs its images (see [Images from other versions](../embedding/compiling.md#images-from-other-versions)) |
| `WRENCH_COMBINED` | set at the top of the single file build's `wrench.h` |
| `WRENCH_NULL_HASH` | `0xABABABAB`, the marker of an empty hash table slot |

## Used by the tools only

`STR_FILE_OPERATIONS`, defined before including `wrench.h`, gives `WRstr`
working `fileToBuffer()` and `bufferToFile()` methods (the command line tool
uses them). `WRENCH_CLI_DEBUG`, `WRENCH_ENABLE_CROSS_MODULE_EXTERNAL_TEST`
and `WIN32_C17` are switches of the command line tool and its tests and have
no effect on the library.

## The builds the project tests

CI compiles the single file build with these combinations, so they are
known to build: none; `WRENCH_INCLUDE_DEBUG_CODE` with the Linux TCP,
serial and file flags; `WRENCH_COMPACT`; `WRENCH_COMPACT`
`WRENCH_REALLY_COMPACT` `WRENCH_INCLUDE_DEBUG_CODE`;
`WRENCH_WITHOUT_COMPILER` `WRENCH_COMPACT` `WRENCH_REALLY_COMPACT`;
`WRENCH_UNPROTECTED_STACK` `WRENCH_HANDLE_MALLOC_FAIL` `WRENCH_TIME_SLICES`;
and with MSVC the default, the debugger with the Windows TCP, serial and
file flags, and the really compact build. The test suite runs on the
default, compact, really compact and unprotected stack builds.
