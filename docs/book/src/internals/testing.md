# The test suite

This page describes how wrench is tested: what `wrench t` runs, the Node
suites for the debugger and the language server, what continuous
integration builds, and how to add a test. Read it before you change the
compiler, the VM or the tools.

## wrench t

```bash
wrench t [testdir] [number]
```

`wrench t` runs everything that is built into the tool. Run it from the
repository root (it uses `tests` by default) or name the folder; a number
runs only that entry of the list.

### The test scripts

`tests/test_files.txt` lists the scripts in the order they run, one per
line, relative to the folder; blank lines and lines starting with `#` are
ignored. The entries are numbered from 1 in that order, which is the
number `wrench t tests <n>` takes. A listed script that does not exist,
or that does not compile, counts as a failure, and the run goes on with
the next one.

Each script carries its expected output at the top, between `/*~` and
`~*/`:

```wrench
/*~ done
~*/

// every check prints only when it fails
var count = 7;
if ( count != 7 ) { println( "count ", count ); }
println( "done" );
```

The text after `/*~` and one more character (a space or the line break) up
to the next `~` is the expected output, `done` and a line break here. The
script passes when everything it prints with `print()` and `println()` is
exactly that. Most scripts follow the pattern above: every check prints
only when it fails, and the last line prints `done`.

Every script runs twice:

1. compiled as usual, with `WR_INCLUDE_GLOBALS`;
2. compiled with debug code and source (`WR_EMBED_DEBUG_CODE` and
   `WR_EMBED_SOURCE_CODE`) and run under an attached debug server that
   talks to nobody. This pass, marked `[debug]` in the output, proves that
   line markers and the debugger hooks do not change what a program does.

The scripts run with the test folder as the current directory, so files
they write (`_serialize_test.bin`, `_std_file_test.txt`) land there; they
are in `.gitignore`. They can `include` files next to them (`tests/include`).

The runner gives the scripts all standard libraries, the library constants
`TestLC::LIBVAL`, `TestLC::BOTH` and `TestMix::LIBONLY`, a host container
with values and raw arrays, and natives that check what the VM made:
`checkIsWrenchArray()`, `checkIsRawArray()`, `checkIsString()`,
`checkIsHashTable()`, `checkIter()` and `checkStruct()`. After the global
code it continues a yielded script until it ends, then calls
`userCheck()`, `stringCheck()` and `arrayCheck()` if the script defines
them, to test calls from the host. `src/utils/wrench_cli.cpp` (`runTests()`)
has the details.

| Scripts | Cover |
|---|---|
| `001` to `028` | the language: operators, parsing, control flow, functions, arrays, structs, hash tables, enums, files, `switch`, `yield`, strings, serialization, number corners |
| `029` to `033` | regressions of library, compiler and VM fixes |
| `034` to `038` | short circuit operators, member reads, VM robustness, strings and empty tables, `include` and `private` |
| `039` to `041` | regressions of the compiler (`new` as an expression, struct members named like parameters, `var a[n]`, nested initializers, enum commas), the VM (parameters as values, reads that do not create, float keys, string order) and the library (`sys::isFunction`, `str::format`, `msg::`, `io::`, `tcp::`) |
| `unit_test1.w`, `unit_test2.w` | equality of all value kinds through an imported assertion library (`unit_test_lib.w`), string concatenation with globals |

### Internal tests

After the scripts, the runner calls tests written in C++ (in
`wrench_cli.cpp`): host API corners (not-found handlers, globals seeded by
the host, `wr_copyValue()`, user data), compiler limits that must be
errors and not crashes, jumps and `switch` statements beyond 16 bits, VM
robustness (callback errors, unknown opcodes, stack overflow, threads),
`WRstr::format()`, `wr_resetContext()`, includes and `private`, the
disassembler, modules, full hash tables and images of the version
before, compile errors and include names, `str::format()` against the C
library, the `std::rand` seed per state, the `msg::` host API,
`io::getline()` line ends, time slices, the scheduler and `yield`. Some
report `PASS`, others use `assert()` and print only when something is
wrong, which is why the suite is also run with the Debug build.

### The debugger protocol checks

Last, `[debugger protocol]` (`src/utils/test_debugger.cpp`) drives a real
debug server through the wire protocol from a client on another thread:
a link that corrupts and drops bytes, sequence numbers that wrap, server
and host driven programs, watchpoints, loop lines, included files, several
programs per server, imported modules, pause and runtime errors, refused
uploads, terminating a yielded call, break on entry, malformed requests
and the `wrench.json` stand-ins. It counts its checks. It only runs when
no test number is given.

### The output

```bash
wrench t
```

Output (start and end):

```text
test [1][001_operations.w]: PASS
test [1][001_operations.w][debug]: PASS
test [2][002_parsing.w]: PASS
test [2][002_parsing.w][debug]: PASS
...
test [43][unit_test2.w]: PASS
test [43][unit_test2.w][debug]: PASS
...
test [-][msg:: values and the host mailbox API]: PASS
test [-][io::getline line ends and the end of the input]: PASS
test [-][drop initializer immediately clobbered by assign]:
test [-][debugger protocol]: PASS (507 checks)
```

A script that prints something else shows what was expected and what it
saw; one that does not compile shows the compile error. Both count as
failures, and the run goes on with the next script. When anything
failed, the run ends with "N test(s) FAILED". The tool returns 0 when
everything passed and -1 otherwise. The whole suite takes about fifteen
seconds.

## The Node suites

`tools/test` holds end to end tests of the protocol servers, the command
line and the VS Code extension, written in plain JavaScript for Node (no
packages needed). Each takes the path of the `wrench` executable and
defaults to `win32/Debug/wrench.exe`:

```bash
node tools/test/dap_test.js win32/Release/wrench.exe
node tools/test/cli_test.js win32/Release/wrench.exe
node tools/test/lsp_test.js win32/Release/wrench.exe
node tools/test/extension_test.js win32/Release/wrench.exe
```

| Suite | Drives | Covers |
|---|---|---|
| `dap_test.js` | `wrench dap`, the way VS Code does | launch sessions, breakpoints and steps, data breakpoints, included files, imported modules, runtime errors, `wrench.json` stand-ins, `.bin` programs, refused uploads, attaching to a waiting script, remote attach to a `wrench dbgserver`, contexts beside the program, attach defaults and missing attributes, no notice for the session's own upload, a quick stop of code without debug information, malformed requests |
| `cli_test.js` | `wrench dbg`, with commands on its input, and the other commands of the tool | `.bin` files with and without source, attach that reads the program from the target, refused uploads, pings while stopped, watch commands, modules, attached contexts, bad commands, stops shown at the idle prompt and `r` while halted, the detach of a refused target, `dbgserver` listening on 127.0.0.1 only; the exit codes of `r` and `rb`, the command word, `release`, `wrench t` going on after a compile error |
| `lsp_test.js` | `wrench lsp` | diagnostics, hover, definition, references, rename, symbols, completion, signature help, `wrench.json` checks, includes, and regressions such as non-ASCII columns and nesting too deep to compile |
| `extension_test.js` | the VS Code extension without VS Code | the launch attributes of its manifest, the `wrench.json` schema, the command lines it runs, `scripts/bundle-exe.js` |

`dap_test.js`, `cli_test.js` and `extension_test.js` print `PASS` or
`FAIL` for each test and exit with status 1 when any failed;
`lsp_test.js` runs its checks as one test and prints `PASS lsp` or the
first failure. `extension_test.js` needs the compiled extension: run
`npm run compile` in `tools/vscode-wrench` first. The suites write
temporary files only to the system's temporary folder. Their scripts
are in `tools/test/fixtures`, among them a host application with a
`wrench.json` (`host_app`) and a program with includes (`includes`).

The grammar of the VS Code extension has a test of its own, run inside
`tools/vscode-wrench` after `npm install`:

```bash
node scripts/test-grammar.js
```

It tokenizes sample lines with the TextMate engine VS Code uses and checks
the scopes of the interesting tokens (`PASS grammar (35 checks)`).

## Continuous integration

`.github/workflows/ci.yml` runs on every push to `main`, on pull requests,
on tags `v*` and by hand. Its jobs:

| Job | Does |
|---|---|
| Linux (5 configurations) | builds with `make` and runs `./wrench t` in the configurations default, `COMPACT=-DWRENCH_COMPACT`, compact plus `WRENCH_REALLY_COMPACT`, `WRENCH_UNPROTECTED_STACK`, and AddressSanitizer with UndefinedBehaviorSanitizer (any finding fails) |
| Single file build | `make dist-check`: generates `dist/wrench.h` and `dist/wrench.cpp`, compiles them as C++98 in six configurations, links and runs `examples/basic.cpp`; on a tag, checks that the tag matches `version.txt` |
| Windows | builds Release and Debug with MSBuild and `-warnaserror`, runs `wrench t` with both, checks the examples of this book with `tools/bookcheck`, runs `dap_test.js`, `cli_test.js` and `lsp_test.js` with the Release build, `npm ci`, `npm run compile`, the grammar test and `extension_test.js` for the extension, and compiles the single file build with MSVC in three configurations |
| The Wrench Book | builds this book with mdBook and keeps it as the artifact `wrench-book` |
| Publish release | on a tag only, after the others passed: attaches the single file build and the zipped book to the GitHub release, see [Releases](releases.md) |

The configurations of the Linux job are the `make` lines in
[Source layout and building from source](building.md#building-on-linux).

## Adding a test

A language or library behaviour:

1. Write `tests/0NN_topic.w` (the next free number) with the expected
   output on top, checks that print only on failure, and `println( "done" );`
   at the end.
2. Add its name to `tests/test_files.txt`.
3. Run it alone, then the whole suite, with both builds:

   ```bash
   win32/Release/wrench.exe t tests 42
   win32/Debug/wrench.exe t
   ```

   On Linux, run `make clean` and the sanitizer configuration too.

A host API behaviour or a compiler limit: add a function to
`src/utils/wrench_cli.cpp` that prints `test [-][what it checks]:` and
`PASS`, and call it from `runTests()`; let it return `false` on failure so
the run counts it.

A debugger behaviour: add a test function to `src/utils/test_debugger.cpp`
and call it from `testDebugger()`; for what VS Code or `wrench dbg` see,
add a test to `dap_test.js` or `cli_test.js`. A language server feature
goes into `lsp_test.js`.

Behaviour that changed or got fixed also gets a line in the `CHANGELIST`
(see [Releases](releases.md#the-changelist)).
