# Summary

[Introduction](introduction.md)

# Getting started

- [What wrench is](start/what-is-wrench.md)
- [A tour of the language](start/tour.md)
- [Getting the tools](start/tools.md)

# Language guide

- [Program structure](language/structure.md)
- [Values and types](language/types.md)
- [Variables and scope](language/variables.md)
- [Operators](language/operators.md)
- [Control flow](language/control-flow.md)
- [Functions](language/functions.md)
- [Arrays and hash tables](language/containers.md)
- [Strings](language/strings.md)
- [Structs](language/structs.md)
- [Enums and constants](language/enums.md)
- [Several files: include and private](language/include.md)
- [Modules: importing code at run time](language/import.md)
- [Yield and cooperative tasks](language/yield.md)
- [Errors and limits](language/errors-limits.md)

# Standard library

- [Overview](library/index.md)
    - [math](library/math.md)
    - [str](library/str.md)
    - [std](library/std.md)
    - [sys](library/sys.md)
    - [Containers: array, hash, list, queue, stack](library/containers.md)
    - [Files: io](library/io.md)
    - [Messages and time: msg, time](library/msg-time.md)
    - [Serialization](library/serialize.md)
    - [Networking: tcp](library/tcp.md)
    - [Devices: Arduino, ESP32 and peripherals](library/devices.md)
    - [debug](library/debug.md)

# Embedding wrench

- [How wrench is put together](embedding/architecture.md)
- [Quick start: Windows and Linux](embedding/quickstart-pc.md)
- [Quick start: Arduino](embedding/quickstart-arduino.md)
- [Quick start: ESP32 with ESP-IDF](embedding/quickstart-esp32.md)
- [States, contexts and running code](embedding/running.md)
- [Compiling scripts](embedding/compiling.md)
- [Calling script functions](embedding/calling.md)
- [Values: WRValue](embedding/values.md)
- [Native functions and libraries](embedding/natives.md)
- [Passing data: containers, globals, user data](embedding/data.md)
- [Yield, time slices and the scheduler](embedding/scheduling.md)
- [Small targets: memory and build flags](embedding/small-targets.md)
- [Errors and robustness](embedding/errors.md)
- [Several states, threads and RTOS tasks](embedding/threads.md)
- [Host API reference](embedding/api-reference.md)

# Debugging

- [Overview](debugging/overview.md)
- [Debugging in VS Code](debugging/vscode.md)
- [The console debugger](debugging/console.md)
- [Debug targets: the PC and devices](debugging/targets.md)
- [Adding the debugger to your firmware](debugging/firmware.md)
- [Breakpoints, stepping and watchpoints](debugging/breakpoints.md)
- [Several files, programs and modules](debugging/programs.md)
- [Blocking code, yield and polling](debugging/blocking.md)
- [Scenarios](debugging/scenarios.md)
- [Troubleshooting](debugging/troubleshooting.md)
- [Wire protocol reference](debugging/protocol.md)

# VS Code extension

- [Trying it out](vscode/quick-try.md)
- [Editing features](vscode/editing.md)
- [Launch configurations](vscode/launch.md)
- [Describing your host application: wrench.json](vscode/wrench-json.md)
- [Building the extension](vscode/building.md)

# Internals

- [Source layout and building from source](internals/building.md)
- [The bytecode image](internals/bytecode.md)
- [Compiling, disassembling and assembling](internals/tools.md)
- [The virtual machine](internals/vm.md)
- [Debug information](internals/debug-info.md)
- [The test suite](internals/testing.md)
- [Releases and the single-file build](internals/releases.md)

# Reference

- [Command line tool](reference/cli.md)
- [Build flags](reference/build-flags.md)
- [Error codes](reference/error-codes.md)
- [Limits](reference/limits.md)
- [Glossary](reference/glossary.md)
