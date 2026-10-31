# Building the extension

This page explains how to build the VS Code extension from
`tools/vscode-wrench` into a `.vsix` file, with the `wrench` executables
bundled, and how to test it. You need it when you change the extension or
the tool, or want a `.vsix` for your team.

The extension itself is small: `src/extension.ts` starts `wrench lsp` and
registers `wrench dap` as the debug adapter, and adds the commands; the
grammar, the language configuration and the `wrench.json` schema are data
files. Everything else is done by the `wrench` executable, so a `.vsix` is
only as new as the executables it bundles.

## What you need

* Node.js and npm (CI uses Node 20).
* A Windows Release build of wrench, `win32/Release/wrench.exe`.
* A static Linux build, `wrench` in the repository root, made on Linux or
  in WSL.

How to build both is in
[Source layout and building from source](../internals/building.md).

## Step by step

1. Build the Windows executable:

   ```bash
   msbuild win32/wrench.sln -p:Configuration=Release -p:Platform=x86 -m
   ```

2. Build the Linux executable, static and stripped so it runs on any
   distribution, in the repository root on Linux or in WSL:

   ```bash
   make clean
   make -j8 LDFLAGS='-static -s'
   ```

3. In `tools/vscode-wrench`, install the packages (once, and after
   `package.json` changes; `npm ci` installs exactly what
   `package-lock.json` lists):

   ```bash
   npm install
   ```

4. Copy the executables into the extension:

   ```bash
   npm run bundle-exe
   ```

   Output (the paths shortened):

   ```text
   bundled ...\win32\Release\wrench.exe -> bin/win32-ia32
   bundled ...\wrench -> bin/linux-x64
   ```

5. Compile the TypeScript and package:

   ```bash
   npm run package
   ```

   It lists the files it packs and ends with (the path shortened):

   ```text
   DONE  Packaged: ...\tools\vscode-wrench\wrench-0.1.0.vsix (213 files, 2.27 MB)
   ```

Install the result as described in [Trying it out](quick-try.md).

## The npm scripts

| Script | Runs | Does |
|---|---|---|
| `compile` | `tsc -p ./` | compiles `src/extension.ts` to `out/extension.js` |
| `watch` | `tsc -watch -p ./` | the same, again on every change |
| `bundle-exe` | `node scripts/bundle-exe.js` | copies the executables into `bin/` |
| `package` | `npm run compile`, then `vsce package --allow-missing-repository` | writes `wrench-<version>.vsix` |

`package` takes whatever `bin/` holds at that moment; it neither builds nor
copies executables. Without `bin/` the `.vsix` has no executable and relies
on `wrench.executablePath` or the `PATH`.

## Bundling executables

`scripts/bundle-exe.js` copies each executable into
`bin/<platform>-<arch>/`, replacing what that folder held. It reads the platform and the architecture from
the file itself (the PE header of a Windows executable, the ELF header of a
Linux one), so the folder always matches what the extension looks for
(see [Where the extension finds wrench](quick-try.md#where-the-extension-finds-wrench)):

| Executable | Folder |
|---|---|
| Windows, 32 bit x86 (what `win32/wrench.sln` builds) | `win32-ia32` |
| Windows, x64 or ARM64 | `win32-x64`, `win32-arm64` |
| Linux, x86-64, x86, ARM or ARM64 | `linux-x64`, `linux-ia32`, `linux-arm`, `linux-arm64` |

Without arguments it takes `win32/Release/wrench.exe` and `wrench` in the
repository root, skips one that does not exist, and fails only when
neither does. With arguments it takes exactly the files given,
and fails when one is missing or not a Windows or Linux executable:

```bash
npm run bundle-exe -- ../../win32/Release/wrench.exe ../../wrench path/to/wrench-arm64
```

The folders of other platforms and architectures already in `bin/` stay,
so a build for another architecture is added by passing only that
executable. Every argument is checked before `bin/` is touched: a run that
fails leaves `bin/` as it was. At the end the script warns when `bin/` has
no Windows or no Linux build.

A `.vsix` loses the execute permission of the Linux file; the extension
sets it again when it first runs the bundled copy.

## What goes into the .vsix

`.vscodeignore` leaves out the TypeScript sources, `scripts/`,
`tsconfig.json`, source maps and `.vscode/`. The package holds
`package.json`, the README, the license, the grammar, the language
configuration, the schema, `out/extension.js`, `bin/` and the runtime
packages (`vscode-languageclient` and what it needs). `vsce` warns that
the extension is not bundled into one JavaScript file; that is harmless.

The extension's version is `version` in `package.json` (0.1.0); it is
independent of the version of wrench and not published to the Marketplace.
`--allow-missing-repository` is there because `package.json` names no
repository.

## Testing

The extension's own logic is thin; the tests drive the executable it
bundles, the same way VS Code does:

```bash
node tools/test/dap_test.js win32/Release/wrench.exe
node tools/test/cli_test.js win32/Release/wrench.exe
node tools/test/lsp_test.js win32/Release/wrench.exe
```

The grammar has a test that tokenizes sample code with the TextMate engine
VS Code uses (`vscode-textmate` and `vscode-oniguruma`, development
packages of the extension). In `tools/vscode-wrench`, after `npm install`:

```bash
node scripts/test-grammar.js
```

Output:

```text
PASS grammar (35 checks)
```

`tools/test/extension_test.js` checks the extension without VS Code: the
launch attributes in `package.json` against the adapter, the `wrench.json`
schema, the command lines the commands run, and `scripts/bundle-exe.js`.
It loads the compiled extension, so run it after `npm run compile`, in
`tools/vscode-wrench`:

```bash
node ../test/extension_test.js ../../win32/Release/wrench.exe
```

Run the Node suites against the Linux executable on a Linux machine with
Node, passing `./wrench`. What the suites cover is in
[The test suite](../internals/testing.md#the-node-suites). CI runs the
three suites, the grammar test and the extension test on Windows on every
push, after `npm ci` and `npm run compile`.

Finally, install the `.vsix` and go through
[the demo folder](quick-try.md#the-demo-folder): it touches includes,
modules, `wrench.json`, a remote target and several programs at once.
