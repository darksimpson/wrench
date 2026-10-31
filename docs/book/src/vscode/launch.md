# Launch configurations

This page is the reference for wrench launch configurations in
`.vscode/launch.json`: every attribute of `launch` and `attach`, the
transports, the snippets, examples for the usual setups, and what Run
Without Debugging, Stop and Restart do. What you can do in a session
(breakpoints, stepping, watchpoints, several programs) is in
[Debugging in VS Code](../debugging/vscode.md).

A configuration is handed to `wrench dap`, the debug adapter, which the
extension starts with the same executable as the language server (see
[Where the extension finds wrench](quick-try.md#where-the-extension-finds-wrench)).

## Launch and attach

| Request | Does | Use it for |
|---|---|---|
| `launch` | compiles `program` (or takes a compiled `.bin`), uploads it to the target unless `upload` is false, and starts it | your script on a PC, or a device whose firmware lets a debugger run programs |
| `attach` | connects to a target and debugs what it already runs, without uploading or starting anything | firmware that runs its own scripts, a program halted by `breakOnEntry()` or `-hold`, a device in the field |

A `launch` needs a target that accepts launches: the local VM always does,
`wrench dbgserver` does, firmware only when it called
`setLaunchable( true )` (see
[Adding the debugger to your firmware](../debugging/firmware.md)).
Otherwise the session fails with a message that points to `attach`.

## Attributes

### launch

| Attribute | Type | Default | Meaning |
|---|---|---|---|
| `program` | string | (required) | the `.w` file to debug, or a `.bin` compiled with `-debugsource` |
| `stopOnEntry` | boolean | `false` | stop on the first line the program runs |
| `transport` | string | `"local"` | where the program runs: `"local"`, `"tcp"` or `"serial"` |
| `host` | string | `"127.0.0.1"` | with `"tcp"`: the target's host name or address |
| `port` | number | (required with `"tcp"`) | with `"tcp"`: the target's port |
| `serialPort` | string | (required with `"serial"`) | with `"serial"`: `COM3` on Windows, `/dev/ttyUSB0` on Linux |
| `baudRate` | number | `115200` | with `"serial"`: the baud rate |
| `target` | string | | instead of `transport`, `host`, `port`, `serialPort` and `baudRate`, see [Target strings](#target-strings) |
| `upload` | boolean | `true` | upload the program before starting it; `false` starts the program the target already holds |
| `project` | string | `""` | the `wrench.json` describing the host application; empty for the nearest one above `program`, `"none"` for none |
| `timeout` | number | 3000, 2000 on serial | milliseconds to wait for each reply of the target |
| `connectTimeout` | number | `5000` | milliseconds to wait for a TCP connection |
| `trace` | boolean | `false` | log the Debug Adapter Protocol messages to the adapter's standard error |

### attach

| Attribute | Type | Default | Meaning |
|---|---|---|---|
| `program` | string | | the local copy of what the target runs: its `.w` source or a `.bin` compiled with `-debugsource`; leave it out to read the program and its source from the target |
| `stopOnEntry` | boolean | `false` | pause as soon as the target runs script code; a program already halted on its first line stays halted |
| `transport` | string | `"tcp"` | `"tcp"` or `"serial"`; an attach to `"local"` fails |
| `host`, `port`, `serialPort`, `baudRate`, `target` | | | as for launch |
| `project` | string | `""` | the `wrench.json` used to name missing functions in error messages |
| `timeout` | number | 3000, 2000 on serial | as for launch |
| `connectTimeout` | number | `5000` | as for launch (the adapter takes it for attach too) |
| `trace` | boolean | `false` | as for launch |

The defaults in the tables are what the adapter uses when an attribute is
missing. The configuration editor proposes its own values for some of
them while you type (`5050` for `port`, `COM3` for `serialPort`, `3000`
for `timeout`); those only apply when you accept them.

A required attribute that is missing ends the session before it connects,
with a message that names it: "launch over tcp needs the "port"
attribute: the tcp port the target listens on (e.g. 5050), or a "target"
like "tcp:127.0.0.1:5050"", or "attach over serial needs the "serialPort"
attribute (e.g. COM3 or /dev/ttyUSB0)".

VS Code's own attributes (`type`, `request`, `name`, `preLaunchTask`,
`postDebugTask` and so on) work as for any debugger; `type` is `"wrench"`.

## Transports

| Transport | Target | Notes |
|---|---|---|
| `local` | a VM in a thread inside the debug adapter | launch only. It imitates the host application of `wrench.json`, see [project](#project) |
| `tcp` | a `WrenchDebugServer` with a TCP transport: an ESP32 over WiFi, your application on a PC, `wrench dbgserver` | the adapter connects as a client |
| `serial` | a `WrenchDebugServer` on a UART or USB CDC port | longer timeouts and more retries, upload progress in the Debug Console |

### Target strings

`target` names the transport and its parameters in one string, the same
form `wrench dbg -target` takes. When it is present the other transport
attributes are ignored.

| String | Means |
|---|---|
| `local` | the local VM |
| `tcp:<host>:<port>` | TCP, for example `tcp:192.168.1.50:5050`; an IPv6 host in brackets, `tcp:[::1]:5050` |
| `tcp:<port>` | TCP to `127.0.0.1` |
| `serial:<port>` | serial at 115200 baud, for example `serial:COM5` |
| `serial:<port>:<baud>` | serial at that baud rate, for example `serial:/dev/ttyUSB0:921600` |

Port names may contain colons (`/dev/serial/by-path/...`); only a number
after the last colon is taken as the baud rate.

## program

For a launch, `program` is compiled the way the debugger needs it, with
debug code, the source and global names (as `wrench c -debugsource`), with
`include` resolved next to each file and in the `"include"` folders of
`wrench.json`. A compile error ends the launch with the compiler's message.

A `.bin` is recognized by its checksum and used as it is: it must have
been compiled with `-debugsource` to show its source, and with `-debug` at
least to stop at breakpoints. The source shown is the one embedded, from a
read-only copy under `wrench-sources` in the temp folder (in a folder
named by the program's hash, so breakpoints set there stay for the next
sessions of the same build). The adapter says so when the image lacks
either.

For an attach, `program` is optional:

* without it, the adapter reads the image from the target; the target's
  program must have been compiled with `-debugsource` for its source to
  show ("target source"), otherwise only stacks and variables are
  available;
* with a `.w` file, the adapter compiles it with the same flags as the
  target's image (`-debug` alone, for example) so the code positions
  match, and warns when the result is still not the build the target runs:
  "the target runs a different build than ..., lines may not match".

## upload

`upload` (launch only, default `true`) sends the compiled program before
starting it. With `false` the target starts the program it already holds,
for example the one `wrench dbgserver file.w` preloaded; when that is a
different build than `program`, a warning says so.

A target whose firmware called `setAllowUpload( false )` (or
`wrench dbgserver -noupload`) refuses uploads; a launch with `upload` on
then fails and tells you to attach, or to launch with `"upload": false`.

## project

The [`wrench.json`](wrench-json.md) that describes the host application
the script is written for. Empty (the default) takes the nearest
`wrench.json` in the folder of `program` or above it; a path takes that
file; `"none"` ignores any.

* With the local transport the VM imitates that host: only the libraries
  it lists are loaded, the functions it declares answer their `returns`
  value, and a call to anything else stops with a runtime error that names
  the function. The Debug Console says "Imitating the host application
  described by ...".
* Without a `wrench.json`, the local VM loads every library and stops on
  any native it does not have, except `print` and `println`.
* On every transport the file names missing functions in runtime errors
  and gives `include` its search folders.

A `wrench.json` that does not parse is reported in the Debug Console and
ignored.

## stopOnEntry

For a launch, the program stops on the first line it runs, before any of
its code. In a program with includes that is often a line of an included
file, whose globals are set up first.

For an attach, the target is paused at the next line of script code it
runs. A target that waits on its first line already (firmware that called
`breakOnEntry()`, `wrench dbgserver -hold`) shows that stop whatever
`stopOnEntry` says.

## Timeouts and trace

`timeout` is how long the adapter waits for each reply before it repeats
the request: twice on TCP and the local VM, four times on a serial link.
Raise it for slow links or targets that block for long between line
markers. `connectTimeout` only applies to opening a TCP connection.

`trace` writes every Debug Adapter Protocol message the adapter receives
and sends to its standard error, prefixed `<-` and `->`. It is meant for
work on the adapter; the Debug Console shows what you need otherwise.

## Snippets

When you edit `launch.json`, **Add Configuration...** offers four
snippets:

| Snippet | Creates |
|---|---|
| Wrench: Launch locally | a local launch of `${file}` |
| Wrench: Launch on a TCP target | a launch on `tcp` to `192.168.1.50`, port `5050` |
| Wrench: Launch on a serial target | a launch on `serial`, `COM3` at 115200 baud |
| Wrench: Attach to a running target | an attach on `tcp` to `192.168.1.50`, port `5050`, with `"program": "${file}"` |

Creating a `launch.json` from Run and Debug starts with the local launch.
The dropdown of Run and Debug (and **Debug: Select and Start Debugging**)
also offers the four kinds directly under "Wrench", with `127.0.0.1` as
the host.

F5 without any `launch.json` in a `.w` file debugs that file in the local
VM. A launch configuration without `program` is refused before the adapter
starts.

## Examples

A script on the PC, imitating the host described by the nearest
`wrench.json`:

```json
{
	"type": "wrench",
	"request": "launch",
	"name": "Debug the current file",
	"program": "${file}",
	"stopOnEntry": true
}
```

The same without any `wrench.json`, so every library is there:

```json
{
	"type": "wrench",
	"request": "launch",
	"name": "Debug without the host description",
	"program": "${workspaceFolder}/main.w",
	"project": "none"
}
```

A `wrench dbgserver` on this PC, started by a task first (the task of
`examples/debug_demo` shows how to make VS Code wait until the server is
up):

```json
{
	"type": "wrench",
	"request": "launch",
	"name": "main.w on the PC debug target",
	"program": "${workspaceFolder}/main.w",
	"target": "tcp:5050",
	"preLaunchTask": "wrench dbgserver"
}
```

An ESP32 on WiFi whose firmware is launchable:

```json
{
	"type": "wrench",
	"request": "launch",
	"name": "blink.w on the ESP32",
	"program": "${workspaceFolder}/blink.w",
	"transport": "tcp",
	"host": "192.168.1.50",
	"port": 5050
}
```

A board on a USB serial port, with more time per reply:

```json
{
	"type": "wrench",
	"request": "launch",
	"name": "blink.w on the board",
	"program": "${workspaceFolder}/blink.w",
	"target": "serial:/dev/ttyUSB0:115200",
	"timeout": 5000
}
```

A program compiled into the firmware with `-debugsource`, attached without
any local files:

```json
{
	"type": "wrench",
	"request": "attach",
	"name": "Attach to the controller",
	"transport": "serial",
	"serialPort": "COM5",
	"baudRate": 115200
}
```

The same, with the source at hand, for a program compiled with `-debug`
only:

```json
{
	"type": "wrench",
	"request": "attach",
	"name": "Attach with local source",
	"program": "${workspaceFolder}/controller.w",
	"target": "tcp:192.168.1.50:5050",
	"stopOnEntry": true
}
```

A compiled program, launched locally, its embedded source shown:

```json
{
	"type": "wrench",
	"request": "launch",
	"name": "Debug the image",
	"program": "${workspaceFolder}/build/controller.bin"
}
```

Starting again what the target holds, without uploading:

```json
{
	"type": "wrench",
	"request": "launch",
	"name": "Run the loaded program again",
	"program": "${workspaceFolder}/main.w",
	"target": "tcp:5050",
	"upload": false
}
```

More complete setups, with the firmware side, are in
[Scenarios](../debugging/scenarios.md).

## Run Without Debugging, Stop and Restart

**Run Without Debugging** (Ctrl+F5) uses the same configuration with
`noDebug` set: the program is compiled, uploaded and started as usual, but
no breakpoints are sent, `stopOnEntry` is ignored and the program never
stops; a runtime error is reported when it ends. What it prints goes to
the Debug Console.

**Stop** (Shift+F5):

* ends a launched program: the local VM is closed, at once also while it
  runs code compiled without debug information; a remote target ends
  the program it was given and keeps running (its firmware, other
  programs, `wrench dbgserver`);
* detaches from an attached target and leaves its program running, with
  no breakpoints left behind.

**Restart** (Ctrl+Shift+F5): the adapter has no restart of its own, so VS
Code ends the session the way Stop does and starts it again from the
configuration:

* a launch compiles `program` again, uploads it and starts it from the
  beginning, so edits you saved are in; programs the target runs by itself
  keep their state;
* an attach detaches and attaches again; the target's program goes on
  running in between, it is not started again.
