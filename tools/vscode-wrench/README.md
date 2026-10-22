# Wrench for Visual Studio Code

Language support and debugging for the [wrench](https://home.workshopfriends.com/wrench/www) scripting language, for scripts that run on a PC or on small targets like ESP32, STM32 and Arduino boards.

Everything is powered by the `wrench` executable (`wrench lsp` and `wrench dap`), so the editor uses the real compiler and the same debug client as the command line.

## Editing

- Syntax highlighting for `.w` and `.wrench` files
- Errors from the wrench compiler as you type (strict `var` mode by default, see `wrench.nonStrict`)
- Hover documentation for your functions, variables and the standard library
- Completion: locals, `::globals`, functions, structs, enums, `math::` / `str::` / ... library members, `._count` style members; after `p.` the members of p's struct when p is only ever given `new S`
- Signature help, go to definition, find references, highlights, rename, outline and folding
- Functions of your own host application from `wrench.json` (see below)
- `include "file.w"`: errors of included files show on the include; hover, completion, signature help and go to definition reach what the included files declare (not their `private` names); search folders come from `"include"` in `wrench.json`

## Debugging

Press F5 in a `.w` file to debug it locally, or add configurations from the snippets in `launch.json`:

| Configuration | What it does |
|---|---|
| `"transport": "local"` (default) | runs the program in a VM inside the debug adapter |
| `"transport": "tcp"`, `host`, `port` | uploads to and debugs a target on the network (ESP32 over WiFi, `wrench dbgserver`) |
| `"transport": "serial"`, `serialPort`, `baudRate` | uploads to and debugs a target on a UART / USB serial port |
| `"request": "attach"` | debugs what a target already runs, without uploading; leave `program` empty to use the source embedded in the target's bytecode |
| `"program": "main.bin"` | debugs a program compiled with `-debugsource` without its `.w` files; the source shown is the one embedded in it |

Supported: breakpoints (moved to the nearest line with code), conditional and hit-count breakpoints, logpoints, break on runtime errors (with the stack still intact), pause, step in / over / out, call stack, locals and globals with expandable arrays, hash tables and structs, editing variables, watch and hover evaluation (`a[2]`, `p.x`, `h["key"]`, `n * 2 > limit`), assignment in the debug console (`x = 5`), debug console completion, watchpoints (Break on Value Change in the variables view, with conditions and hit counts), and programs made of several files (breakpoints and stack frames in included files).

### Scripts for your own application

If wrench is embedded in an application with its own native functions (and maybe without some standard libraries), describe it in a `wrench.json` next to the scripts. **Wrench: Describe the Host Application** creates one; VS Code checks it against a schema while you edit it.

```json
{
	"libraries": [ "math", "string" ],
	"functions": [
		{ "name": "openDoc", "params": [ "path", "mode?" ], "doc": "Opens a document", "returns": 1 },
		"ui::alert( text )"
	]
}
```

- The editor completes and documents those functions, shows their parameters, jumps to their declaration, hides libraries the application does not load, and warns about undeclared calls, unloaded libraries and wrong argument counts (`"undeclaredCalls": "error"` or `"ignore"` changes the warning).
- Local debugging imitates the application: only its libraries are loaded, declared functions answer their `returns` value (default 0), anything else stops with a runtime error.
- Runtime errors name the missing function, on any target.
- The launch option `"project"` picks a specific file, `"none"` ignores it.

To debug inside the application itself, give its VM a `WrenchDebugServer` with a `WrenchDebugTcpServerTransport` and attach to it over TCP; see `docs/debugging.md` in the wrench repository.

### Targets

A target runs a `WrenchDebugServer` next to its VM; see `examples/debug_targets` in the wrench repository for ESP32 (WiFi and serial), STM32 HAL and Arduino sketches. On a PC, `wrench dbgserver -target tcp:5050` (or `serial:COM3:115200`) acts as a target too.

Scripts must be compiled with debug information to be debuggable. The debugger does that itself when it uploads; for firmware images use `wrench c -debugsource file.w file.bin` (the **Wrench: Compile to Bytecode** command).

## Settings

- `wrench.executablePath`: path to `wrench` / `wrench.exe`. Empty uses the copy bundled in the extension, if the `.vsix` was built with one (a 32-bit Windows build that also runs on 64-bit Windows, and a Linux x86-64 build), then the PATH. Set it on other platforms, for example Linux on ARM or macOS, after building wrench there.
- `wrench.nonStrict`: allow variables without `var` when checking.
- `wrench.compileDebugInfo`: embed debug info and source when compiling to bytecode (default on).
- `wrench.trace.server`: trace the communication with the language server in the "Wrench Language Server" output channel: `off` (default), `messages` or `verbose`.

Launch configurations also take `stopOnEntry`, `upload`, `project`, `timeout` (ms per reply), `connectTimeout` (ms for a TCP connection), `target` (`tcp:<host>:<port>`, `serial:<port>[:<baud>]`) and `trace` (log the Debug Adapter Protocol traffic); VS Code's Run Without Debugging (Ctrl+F5) runs the program without stopping. `docs/debugging.md` in the wrench repository lists them all.

## Building the extension

Build both executables first: `win32/wrench.vcxproj` in Release, and the Linux one in the repository root with `make LDFLAGS='-static -s'` (on Linux or in WSL; static so it runs on any distribution). Then:

```
npm install
npm run bundle-exe
npm run package
```

`bundle-exe` copies `win32/Release/wrench.exe` and `../../wrench` into `bin/<platform>-<arch>/`, reading the platform and architecture from each binary, and warns when one of them is missing. The Visual Studio project builds Win32 only, so the Windows executable lands in `bin/win32-ia32` (the extension uses it on 64-bit Windows too); the static Linux build lands in `bin/linux-x64`. Pass paths to bundle other builds, for example `npm run bundle-exe -- path/to/wrench-arm64`.

`package` compiles the extension and produces `wrench-<version>.vsix` from whatever `bin/` holds at that moment; it neither builds nor bundles executables, so run `bundle-exe` first (without it the `.vsix` relies on `wrench.executablePath` or the PATH).
