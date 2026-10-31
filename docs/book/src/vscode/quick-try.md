# Trying it out

This page gets the wrench extension for Visual Studio Code installed and
running in a few minutes: install the `.vsix`, open the demo folder of the
repository, and debug a program locally and on a debug target. It also
says where the extension finds the `wrench` executable that does the work.

The extension adds syntax highlighting, errors as you type, completion,
navigation and a debugger for `.w` files. Everything except the
highlighting is done by the `wrench` executable: `wrench lsp` for editing,
`wrench dap` for debugging. The editor therefore uses the real compiler,
and debugs with the same client as the console debugger.

## Installing the extension

The extension is a file `wrench-<version>.vsix`, built from
`tools/vscode-wrench` in the repository (see
[Building the extension](building.md)); after a build it is
`tools/vscode-wrench/wrench-0.1.0.vsix`. It needs VS Code 1.82 or later.

In VS Code:

1. Open the Extensions view (Ctrl+Shift+X).
2. Open the `...` menu at the top of the view and choose
   **Install from VSIX...**.
3. Pick the `.vsix` file.

Or from a terminal:

```bash
code --install-extension tools/vscode-wrench/wrench-0.1.0.vsix
```

Installing a newer `.vsix` the same way replaces the old one; reload the
window afterwards. Use a `.vsix` built from the same sources as the rest
of your wrench: the extension and the executable inside it belong
together.

## Where the extension finds wrench

When it starts, and for every debug session, the extension looks for the
executable in this order:

1. the setting `wrench.executablePath`, if it is not empty.
   `${workspaceFolder}` in it is replaced by the first folder of the
   workspace;
2. a copy bundled in the extension, in `bin/<platform>-<arch>/`: on
   Windows it tries the native architecture, then `win32-x64`, then
   `win32-ia32`; elsewhere only the native one;
3. `wrench.exe` (Windows) or `wrench` on the `PATH`.

The `.vsix` built by the project bundles two copies:

| Folder | Build | Runs on |
|---|---|---|
| `bin/win32-ia32` | `win32/Release/wrench.exe`, 32 bit | Windows, 32 and 64 bit |
| `bin/linux-x64` | a static, stripped Linux build | any x86-64 Linux distribution |

On other systems, for example macOS or Linux on ARM, build wrench there
(see [Source layout and building from source](../internals/building.md))
and set `wrench.executablePath` to it. A `.vsix` packaged without the
bundled copies relies on the setting or the `PATH`.

Before it starts the language server, the extension tries to run
`wrench v` with what it found. If it cannot run the executable it shows a warning, "Wrench: cannot run ...",
with a button that opens the setting; highlighting still works, checking
and debugging do not. Changing `wrench.executablePath` restarts the
language server.

## The demo folder

`examples/debug_demo` in the repository is a ready folder to try the
debugger on:

| File | What it is |
|---|---|
| `main.w` | the program: includes `shapes.w` and `lib/stats.w`, compiles a module from a string with `sys::importCompile` and calls it |
| `shapes.w` | `struct Box` and `boxArea()`, included from next to `main.w` |
| `lib/stats.w` | included through `"include": [ "lib" ]` in `wrench.json`; its globals are `private` |
| `sensor.w` | a second program for the debug server of Demo 2; its `tick()` runs ten times a second |
| `wrench.json` | the host the scripts run in: all libraries, `println`, the include folder |
| `.vscode/launch.json` | the two launch configurations |
| `.vscode/tasks.json` | the task that starts the debug server for Demo 2 |
| `README.md` | the tour step by step |

Open the folder as a window of its own, so VS Code finds its `.vscode`
folder:

```bash
code examples/debug_demo
```

Open `main.w`. The editor already checks it: hover over `boxArea` or
`statsAdd` (declared in the included files), type `b.` inside the loop to
see the members of `Box`, or put a typo into a line to see the error. The
editing features are described in [Editing features](editing.md).

### Demo 1: a local VM

In the Run and Debug view pick **Demo 1: main.w in a local VM** and press
F5. The configuration is the simplest one there is:

```json
{
	"type": "wrench",
	"request": "launch",
	"name": "Demo 1: main.w in a local VM",
	"program": "${workspaceFolder}/main.w",
	"stopOnEntry": true
}
```

The program runs in a VM inside the debug adapter. Things to look at:

* It stops on entry in `lib/stats.w`: the globals of the included files
  are the first code the program runs.
* Breakpoints work in every file of the program. Set one in `boxArea()` in
  `shapes.w` and continue: the call stack shows `boxArea` in `shapes.w`
  over `<global>` in `main.w`.
* In `lib/stats.w` the Globals scope shows the private variables as
  `count@stats.w` and `sum@stats.w`. Right-click one and choose
  **Break on Value Change** to stop wherever it changes.
* Step into `label()`: it opens as a read-only document "import 1
  (embedded source)", the module the program compiled while running.
* The Debug Console shows what the program prints: `total = 68 cm2` and
  `average = 22`.

### Demo 2: a debug target

Pick **Demo 2: main.w on dbgserver, beside sensor.w** and press F5. It
needs wrench built in the repository (`win32/wrench.sln` in Release on
Windows, `make` on Linux), because its task runs that executable:

```bash
wrench dbgserver -target tcp:5050 -context sensor.w -v main.w
```

The server stands in for a board on the network that runs `sensor.w`
itself; VS Code connects over TCP and uploads `main.w`, as it would to an
ESP32 over WiFi. The task's problem matcher holds the debug session back
until the server prints that it attached `sensor.w`.

Things to look at:

* The Call Stack shows two threads, one per program: `main.w` and
  `sensor.w`.
* A breakpoint in `tick()` of `sensor.w` stops there once `main.w` is
  done, with "Globals of sensor.w" in the variables.
* Restart (Ctrl+Shift+F5) uploads `main.w` again, while `sensor.w` keeps
  its state: it belongs to the target, not to the session.
* Stopping the session leaves the server running; end it with **Terminal:
  Terminate Task**.

The README of the folder has the full tour. Debugging in VS Code is
covered in depth in [Debugging in VS Code](../debugging/vscode.md); every
launch attribute is in [Launch configurations](launch.md).

## Debugging your own file

Without a `launch.json`, F5 in a `.w` file debugs it in a local VM: the
extension fills in a launch configuration with `"program": "${file}"`.
To keep configurations, open Run and Debug, **create a launch.json file**,
and pick from the snippets (see
[Launch configurations](launch.md#snippets)).
