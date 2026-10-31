# Debugger demo

A few minutes with the VS Code debugger: a program made of several files, a
module it compiles while it runs, and a second program the target runs
beside it.

| File | What it is |
|---|---|
| `main.w` | the program: includes `shapes.w` and `stats.w`, compiles a module from a string with `sys::importCompile` and calls into it |
| `shapes.w` | included from next to `main.w`: `struct Box`, `boxArea()` |
| `lib/stats.w` | included through `"include": [ "lib" ]` in `wrench.json`; its globals are `private` |
| `sensor.w` | a second program for `wrench dbgserver -context`: the server calls its `tick()` ten times a second |
| `wrench.json` | the host the scripts run in: all libraries, `println`, the include folder |
| `.vscode/` | the two launch configurations and the task that starts the debug server |

## Before you start

1. Build wrench: `win32/wrench.sln` in Release on Windows, `make` in the
   repository root on Linux. Demo 2 runs the debug server from there.
2. Install the extension built from the same sources:
   `tools/vscode-wrench/wrench-<version>.vsix` (Extensions view, `...` menu,
   Install from VSIX). An older one lacks several of the features below.
3. Open this folder as a window of its own, so VS Code finds `.vscode/`:

   ```
   code examples/debug_demo
   ```

## Demo 1: several files and a module

In Run and Debug pick **Demo 1: main.w in a local VM** and press F5.

1. It stops on entry in `lib/stats.w`, line 2: the included files' globals
   are the first code the program runs.
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
   file. The Modules view (`...` menu of Run and Debug) lists `main.w` and
   `import 1`.
6. Continue: the Debug Console shows `total = 68 cm2` and `average = 22`.

## Demo 2: two programs on one target

Pick **Demo 2: main.w on dbgserver, beside sensor.w** and press F5. The task
**wrench dbgserver** starts first in a terminal and keeps running:

```
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
   the target. The module `main.w` imports is "import 2" here, program 1 is
   `sensor.w`.
5. To end it, stop the debug session (Shift+F5), then the task (Terminal,
   Terminate Task). The server stays up between sessions until then.

The same target works with the console debugger:

```
wrench dbg -target tcp:127.0.0.1:5050 main.w
```

`programs` lists the programs, `b sensor.w:11` sets the breakpoint, `r` runs
`main.w`, `c` waits for the next stop.
