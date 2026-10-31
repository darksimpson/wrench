# Debugging in VS Code

This page walks through a debug session in Visual Studio Code with the
wrench extension: running a script in the local VM, breakpoints, stepping,
variables, the debug console, and attaching to a target that already runs.
Install the extension first, see [Trying it out](../vscode/quick-try.md);
every launch option is listed in
[Launch configurations](../vscode/launch.md).

The debugger is the `wrench` executable itself, started as `wrench dap`.
A `.vsix` built with `npm run bundle-exe` carries a 32-bit Windows build
(`bin/win32-ia32`, it runs on 64-bit Windows too) and a Linux x86-64 one
(`bin/linux-x64`); elsewhere set `wrench.executablePath` to a `wrench` you
built. See [Building the extension](../vscode/building.md).

## Debugging a script locally

Open a `.w` file and press F5. Without a `launch.json` the extension
debugs the active file in a VM inside the debug adapter (`wrench dap`), the
same as this configuration:

```json
{
	"type": "wrench",
	"request": "launch",
	"name": "Debug locally",
	"program": "${file}",
	"stopOnEntry": false
}
```

The adapter compiles the program with debug information and its source,
loads it into its own VM and runs it. `"stopOnEntry": true` halts it on its
first line, where you can set breakpoints before anything runs. Output of
`print` and `println` appears in the Debug Console.

The local VM stands in for the application the script is written for. With
a [`wrench.json`](../vscode/wrench-json.md) next to the script (or in a
folder above it) it loads only the libraries listed there, and the natives
declared there answer with their `"returns"` value, saying once per
function:

```text
[wrench] openDoc is provided by the host application, here it returns 1
[wrench] log is provided by the host application, here it returns 0
```

A call to anything else fails with a runtime error that names the
function, "'ui::confirm' is not provided by the target", as it would in
the application ([scenario 2](scenarios.md#2-a-desktop-application-with-wrenchjson)
has the whole example). Without a `wrench.json` the local VM has all standard
libraries and `print` / `println`, and nothing else.

## Breakpoints

Click in the gutter to set a breakpoint. A breakpoint on a line without
code (a comment, a brace, an empty line) moves to the next line that has
some, and VS Code shows it there. A breakpoint that cannot be placed stays
hollow, and hovering over it tells you why:

| Message | Meaning |
|---|---|
| no code at or after this line | nothing after it in that file has code |
| only the programs being debugged and the files they include can hold breakpoints | the file is not part of any program the target debugs |
| the module is not loaded now | the file belongs to a module that is not imported at the moment |
| the target has no room for more breakpoints | the target holds `WRENCH_DEBUG_MAX_BREAKPOINTS` (32 by default) in all |

Right-click a breakpoint, **Edit Breakpoint**, to make it conditional:

* **Expression**: stops only when the expression is true, for example
  `count > 3 && name == "x"`. The expression is evaluated like a watch, see
  [Evaluating expressions](breakpoints.md#evaluating-expressions).
* **Hit Count**: stops only on some hits. The hits are counted after the
  condition, if there is one.

  | Hit count | Stops on hit |
  |---|---|
  | `5` | 5 and every one after it |
  | `>= 5`, `> 4` | 5 and later |
  | `== 5` | 5 only |
  | `< 3`, `<= 2` | 1 and 2 |
  | `% 3` | 3, 6, 9 ... |

* **Log Message**: a logpoint. Instead of stopping it writes the message to
  the Debug Console, with every `{expression}` replaced by its value:
  `total is {total}, twice {total * 2}` prints `total is 21, twice 42`.

Conditions, hit counts and logpoints are evaluated by the adapter on the
PC: the target halts at the line, the adapter reads what it needs and lets
the program go on. That takes a round trip over the link for every hit,
which is quick locally but noticeable on a slow serial link in a busy
loop.

The full rules, including how breakpoints bind to lines and what happens
to them when a program is replaced, are in
[Breakpoints, stepping and watchpoints](breakpoints.md).

## Stepping

When the program stops, the toolbar steps:

| Command | Key | Goes to |
|---|---|---|
| Continue | F5 | the next breakpoint, watchpoint or error |
| Step Over | F10 | the next line of this function, or of its caller after a return |
| Step Into | F11 | the next line that runs, also inside a called function |
| Step Out | Shift+F11 | the first line after the current function returns |
| Pause | F6 | the line the program is executing |

Stepping works line by line; a line with several statements is one step.
See [Stepping](breakpoints.md#stepping) for steps across calls the
firmware makes into the script.

## Call stack

The Call Stack view lists the frames of the stopped program, innermost
first. Each function frame carries its name; the global code of a program
is `<global>`. A frame in an included file opens that file, a frame in a
module compiled from a string opens it as a read-only document. Click a
frame to see its variables and to evaluate expressions in it.

## Variables

The Variables view has two scopes for the selected frame:

* **Locals**: the arguments and local variables of the function (none for
  global code).
* **Globals**: the globals of the frame's program. For a program other than
  the main one it is called "Globals of sensor.w", with the name the
  firmware gave that program.

Arrays, hash tables and structs expand when you open them, a page at a
time, so a large container costs nothing until you look at it. Strings show
their first 48 characters there and in hovers; the Watch view and the
Debug Console fetch the whole text. A private global of an included file
shows as `count@stats.w`, see
[Several files, programs and modules](programs.md).

To change a value, double-click it (or **Set Value** in its context menu)
and type an expression: `42`, `"text"`, `limit * 2`. The new value must be
a number or a string: a variable that holds an array can be overwritten
with one, but no container can be assigned.
**Copy as Expression** and **Add to Watch** work for every element and
member, they carry its full path (`::orders[1].qty` for an element of a
global).

## Watch and hover

Expressions in the Watch view are evaluated at every stop, in the selected
frame. Hovering over a name in the editor while stopped evaluates it the
same way. Both understand variables, elements and members, arithmetic,
comparisons and logic:

```text
orders[1].qty * orders[1].price
h["key"]
count > 3 && last != 0
::total
```

A name means the local of the selected frame first, then a global of its
program; `::name` always means the global. The full syntax is in
[Evaluating expressions](breakpoints.md#evaluating-expressions).

## Debug console

While the program is stopped, the Debug Console evaluates what you type in
the selected frame and prints the result. An assignment changes the
variable:

```text
total = total + 5
orders[0].qty = 12
name = "bolt"
```

Completion (Ctrl+Space) offers the locals of the selected frame and the
globals of its program. While the program runs the console answers "the
program is running, pause it first".

## Runtime errors

The Breakpoints view has an entry **Runtime errors**, on by default. With
it, a runtime error stops the program on the line where it happens, before
the VM unwinds anything: the call stack, the locals and the globals are
those of the failing moment. The editor shows the error, for example:

```text
Runtime error: WR_ERR_function_not_found, 'led' is not provided by the target
```

Continuing lets the VM fail as it would without a debugger: global code
ends with the error, a function the firmware called returns it to the
firmware. With the entry switched off the program does not stop; when its
global code fails the Debug Console reports the error:

```text
Program failed with WR_ERR_function_not_found near line 2: 'led' is not provided by the target
```

## Watchpoints

Right-click a variable, an element or a member in the Variables view and
choose **Break on Value Change**. The program then stops on the line after
the one that changed the value. A watchpoint can have a condition and a hit
count like a breakpoint (edit it in the Breakpoints view). A watchpoint on
a local goes away, greyed out, when its function returns. The target holds
four watchpoints by default. See
[Watchpoints](breakpoints.md#watchpoints) for what counts as a change.

## Run without debugging

**Run Without Debugging** (Ctrl+F5) runs the same configuration without
stopping: breakpoints are not sent, `stopOnEntry` is ignored, and a runtime
error does not halt the program; it is reported when the program ends.
Output still appears in the Debug Console.

## Stop, disconnect and restart

How a session ends depends on its request:

| | Launch | Attach |
|---|---|---|
| Stop (Shift+F5) | terminates the program, then disconnects | disconnects; the program runs on with its breakpoints cleared |
| Restart (Ctrl+Shift+F5) | terminates the program and launches it again; with upload on, the current build is uploaded first | disconnects and attaches again; nothing is uploaded or run again |

The adapter terminates the program when VS Code asks it to: always in a
launch session, in an attach session only when the disconnect asks to
terminate the debuggee. Terminating ends the script code that runs or is
halted at that moment, on its next line; a function the firmware called
then returns 0 with `WR_ERR_debugger_terminated`. The firmware's program
stays loaded either way, and only a launch runs its global code again.

Code compiled without debug information never looks at the debugger, so
it cannot be told to end. Stopping a local session while such code runs
takes one short try and then ends the session at once; the program ends
with the adapter.

To restart the program a target already holds without uploading anything,
use a launch configuration with `"upload": false`. That is also the only
way to launch on a target that refuses uploads (see
[Refusing uploads](firmware.md#refusing-uploads)).

When the program ends by itself the session normally ends too. Two cases
keep it open, because the target goes on running script code:

* The firmware calls into the script itself after the global code
  (`setHostDriven( true )`, most firmware does): the Debug Console says
  "Global code finished, the target keeps running its own calls." and
  breakpoints in functions the firmware calls still stop.
* The target runs other programs beside this one: "The program finished,
  the target keeps running its other programs."

## Attaching to a target

To debug what a device, `wrench dbgserver` or an application runs now,
without uploading or restarting it, use an attach configuration:

```json
{
	"type": "wrench",
	"request": "attach",
	"name": "Attach to the board",
	"program": "${workspaceFolder}/script/blink.w",
	"transport": "serial",
	"serialPort": "COM5",
	"baudRate": 115200
}
```

```json
{
	"type": "wrench",
	"request": "attach",
	"name": "Attach over TCP",
	"program": "${workspaceFolder}/main.w",
	"transport": "tcp",
	"host": "192.168.1.50",
	"port": 5050
}
```

An attach configuration without `"transport"` uses `"tcp"`, and `"host"`
defaults to `127.0.0.1`. A TCP configuration without `"port"` fails with
"attach over tcp needs the "port" attribute: the tcp port the target
listens on (e.g. 5050), or a "target" like "tcp:127.0.0.1:5050"", and a
serial one without `"serialPort"` with "attach over serial needs the
"serialPort" attribute (e.g. COM3 or /dev/ttyUSB0)".
`"target": "tcp:192.168.1.50:5050"` or
`"target": "serial:COM5:115200"` is a shorter way to say the same. For slow
links raise `"timeout"` (ms to wait for each reply before resending:
3000 by default, 2000 with more retries on serial) and `"connectTimeout"`
(ms to wait for a TCP connection, 5000); `"trace": true` logs the Debug
Adapter Protocol traffic to the adapter's stderr.

`program` is the local copy of what the target runs. The adapter compiles
it the way the target's program was compiled and compares the two; when
they differ the Debug Console warns:

```text
Warning: the target runs a different build than D:\fw\script\blink.w, lines may not match.
```

Leave `program` out to debug from the program the target holds: the
adapter reads its bytecode, symbols and source over the link and shows the
source as a read-only document. That needs the program compiled with
`-debugsource`; with `-debug` only you get stacks and variables but no
source. See [Debugging without the source files](scenarios.md#4-debugging-firmware-without-the-sources-at-hand).

`"stopOnEntry": true` in an attach configuration pauses the program at the
next line it executes. A target halted by `breakOnEntry()` reports its stop
as soon as the session starts.

## Threads and modules

A target can debug several programs at once: contexts the firmware runs
side by side and modules a script imports at run time. In VS Code:

* Every program with a stack of its own is a thread in the Call Stack view:
  `wrench` for program 0, and one per other context, named by the
  firmware. A module runs on the thread of the program that imported it.
* All programs stop together: the target is single threaded, so while one
  program is halted, the others wait.
* The Modules view (in the `...` menu of Run and Debug) lists every
  program.
* Breakpoints work in every program's files. A module compiled from a
  string opens as a read-only document "import 1 (embedded source)", which
  takes breakpoints too.

[Several files, programs and modules](programs.md) has the details and a
walkthrough of `examples/debug_demo`.

## Messages from the adapter

The Debug Console also carries the adapter's own messages:

| Message | Meaning |
|---|---|
| Connected to tcp 127.0.0.1:5050 (wrench 8.0.0) | the target answered; its wrench version |
| Imitating the host application described by ...\wrench.json | the local VM follows that file |
| The target loaded a new program. | the firmware attached a new program 0 that is not the one this session uploaded, for example after another debugger's upload |
| The program was compiled without debug information (-debug or -debugsource): it runs, but cannot stop at breakpoints or step. | as it says |
| Breakpoint condition 'count >' failed: unexpected end of expression | the condition does not evaluate; the breakpoint stops as if it had none |
| Lost the connection to the target. | the link went down; the session ends |

[Troubleshooting](troubleshooting.md) lists the errors a launch or an
attach can fail with.
