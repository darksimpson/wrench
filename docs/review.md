# Wrench review notes (October 2026)

Two reviews of the compiler, VM, libraries, debugger and tools found a number
of real defects. Most are fixed (see CHANGELIST, section "next"); each fix
has a regression check in `tests/029_library_fixes.c`,
`tests/030_compiler_fixes.c`, `tests/031_vm_fixes.c`,
`tests/032_audit_fixes.c`, `tests/033_library_audit.c`, the internal tests
of `wrench t`, the debugger test suite or the tool tests in `tools/test`.
This page records what was fixed in the second review, what is still open
and what would be worth improving next.

## How the code is tested now

* `wrench t` runs every test file twice: as before, and compiled with debug
  information under an attached debug server, so the debugger hooks cannot
  change program behaviour unnoticed. Output mismatches fail the run.
* Internal tests cover host API corners and compiler limits (too many
  arguments, deep nesting, stores kept with debug information).
* The debugger has an end to end suite (`[debugger protocol]`, 253 checks)
  driving a real server thread through the protocol, including a link that
  corrupts bytes, sequence number wrap, lost stop events, relaunch, step
  resumption and link timeouts.
* `tools/test/dap_test.js`, `cli_test.js` and `lsp_test.js` drive
  `wrench dap`, `wrench dbg` and `wrench lsp` the way VS Code and a user do;
  `tools/vscode-wrench/scripts/test-grammar.js` tokenizes with VS Code's
  TextMate engine.
* The suite passes on Windows (MSVC, Debug and Release, no warnings) and
  Linux (GCC, C++98 core, `-Wall -Werror`) with the default build,
  `WRENCH_COMPACT` and `WRENCH_REALLY_COMPACT`, and under AddressSanitizer
  plus UndefinedBehaviorSanitizer with leak checking. The node suites pass
  against both the Windows and the static Linux binary.

Worth adding: running the sanitizer build in CI, and a fuzzer for
`wr_compile` and `wr_deserialize` (both take untrusted input).

## Second review: what was fixed

Memory safety:

* `continue` inside `foreach` jumped outside the program; unplaced jump
  targets are now a link error instead of a wild jump.
* Defining a function inside a global block used a stale reference into the
  growing unit table.
* A member read from a temporary table or struct (`mk()["k"]`) pointed into
  an object the collector could free.
* Adding two containers left a stale stack value as the result.
* A script function called from a native callback could yield
  (`WR_ERR_yield_in_native_call` now), imports keep the caller's time slice.
* Calling a library constant as a function jumped to its value.
* Windows CRT invalid parameter handler killed the process from `io::`.
* `str::tprint` overflowed a stack buffer; hashing or printing a container
  that holds itself recursed without end.
* Debugger value handles survived program replacement and host driven runs.
* Hash tables: wrong counts after growth, slot 0 overwritten past the largest
  prime, allocation failures leaving a typed container without data.
* Compiler: a dangling token reference in `new S[]`, unbounded recursion on
  deep nesting (now `WR_ERR_nesting_too_deep`, limit `WRENCH_MAX_PARSE_DEPTH`),
  octal and decimal escapes reading past the source, comment recursion.
* `wr_callFunction` checks the stack before copying arguments; the
  disassembler leaked its context.

Compiler:

* `var t = cout + 1;` compiled the unknown name as a hash: in strict mode a
  bare unknown name used as a value must name a script function now.
* Binary and octal literals with bit 31 set were clamped, overflowing decimal
  literals are an error.
* `foreach` past line 16383 with `-debug`, a nested `{ }` ending the enclosing
  block, a ternary after a postfix operator, `f().x` and `(s).x`, a variable
  as a hash initializer key, `case (5):`, constant folding of `-(-128)` and
  `-(-32768)`, struct initializers past 255 values, calls with more than 255
  arguments (`WR_ERR_too_many_arguments`).
* With `-debug` a store that the next statement overwrites is kept, so a stop
  between the two shows it; line markers no longer leak across functions on
  one line.
* `link()` freed its fixup list on error returns.

VM and libraries:

* Float operands in `%=`, `|=`, `&=`, `^=`, `<<=` and `>>=`; signed overflow in
  `*=`, `++` and `--`; `_exists` on a temporary table; library calls by hash
  from `wr_callFunction` into imported modules.
* `str::sprintf` sharing its destination, `io::deleteFile`, `io::readFile`
  sizes, `io::writeFile` with empty data, serializer accounting, validation and
  growth, `sys::importByteCode` on bad input, `sys::isFunction` for constants,
  `%f` without float support, `str::tol`, `debug::print` without debug code,
  the SPIFFS/LittleFS backend, the FastLED library.

Debugger and tools:

* Retransmission detection compares type and sequence number (a wrapped
  counter dropped commands); a stopped target re-announces the stop on Hello
  and Pause; relaunch rebuilds globals; `setLinkTimeout` releases a target
  whose host vanished (the adapter pings while stopped); a breakpoint hit
  during step over or out can resume the step.
* VS Code adapter: false conditions and logpoints no longer cancel a step,
  malformed requests and bodies get error replies instead of a crash,
  attaching recompiles with the target's compiler flags, path matching is
  normalized.
* `wrench dbg`: no hang on `c` while not stopped or `kill` after a lost link,
  breakpoints moved to the next code line can be removed, options in any
  order.
* Windows paths outside the ANSI code page, serial names and IPv6 hosts
  containing `:`, the evaluator's `<`/`>` precedence and `=` inside strings,
  error names past the table, the local target thread exiting, Linux serial
  baud rates, connecting to a closed port, a duplicate listener on Windows.
* Language server: globals used bare inside functions, member resolution per
  struct, the `unit` keyword, deep nesting, incremental indexing, positions
  after non-ASCII text, rename validation, completion triggers, definition in
  `wrench.json`, UNC paths, stale disassembly, the grammar's `default:`.
* Examples, protocol documentation, comments and packaging notes; the code
  style of the new work.

## Open issues

Language and compiler:

* `&&` and `||` evaluate both operands (no short circuit): `if ( p && p.x )`
  still evaluates `p.x`. Either document it prominently or compile them to
  conditional jumps (`O_BLA` / `O_BLO` exist).
* Relative jumps are 16 bit, so a single function, loop or switch case larger
  than about 32KB of bytecode fails with `WR_ERR_bad_goto_location`. A long
  jump opcode would lift that.
* Assigning to a literal (`3 = 4;`) compiles to an assignment into a
  temporary without an error.
* A goto label defined twice is not reported, the first one wins.
* Assigning to a member of a computed value (`f().x = 3`, `(t).x = 3`)
  changes a copy: the result replaces the only stack slot that holds the
  container, so a reference into it would not keep it alive. Reading works.
  A member of a non-container (`"abc".length`) reads as 0.
* A character escape `\0NNN` is decimal (`"\097"` is `a`), unlike C's octal.
  This is the documented behaviour and kept.
* The compiler `printf`s its error message to stdout in addition to
  returning it; library users on a device may not want that (the tools move
  stdout aside while they speak LSP or DAP).

VM:

* Without `WRENCH_PROTECT_STACK_FROM_OVERFLOW` the VM does not check its value
  stack on every push. A deep expression (around 120 nested initializers with
  the CLI's 128 entry stack) overruns it and corrupts memory. With the define
  every push is checked and fails with `WR_ERR_stack_overflow`.
* Arrays grow to exactly the size needed, so appending in a loop is
  quadratic; a capacity field would make it amortized linear.
* A hash key equal to the internal empty marker `WRENCH_NULL_HASH`
  (0xABABABAB, also the int -1414812757) cannot be stored.
* `WRValue::deref()` and `singleValue()` return shared static temporaries,
  and `g_mallocFailed` is a global: two `WRState`s running on different
  cores (ESP32 dual core, RTOS tasks) race on them. Returning copies and
  moving the flag into `WRState` would make the VM reentrant across states.
* Errors set by native callbacks (`w->err`) are not checked after the call,
  the script keeps running with a stale error code (not-found handlers are
  checked).
* `wr_returnValueFromLastCall()` returns the raw return slot, which can be a
  reference into a finished frame; it should return a dereferenced copy.
* An unknown opcode from corrupt bytecode is undefined behaviour
  (`__assume(0)` in the switch interpreter). Bytecode is CRC checked on load,
  but a cheap validation pass would make it safe to run images from untrusted
  sources.
* GC marking is recursive. Cycles are handled, but a very deep structure (a
  long linked list built from nested arrays) can exhaust a small native stack
  on a microcontroller; an explicit mark stack would bound it.
* Values handed between unrelated contexts by native code (not imports) can
  keep GC mark bits set in another context's objects; imports are collected
  as one group, other contexts are not.
* Calling a function by its name as a value (`var f = name; f();`) is not
  supported (see 7.0.1 notes).

Debugger:

* One debugged context per `WrenchDebugServer`; a firmware running several
  scripts needs one server (and link) per context, or a multiplexing
  extension of the protocol.
* Breakpoints are per line; there are no data breakpoints (watchpoints) and
  no column breakpoints for several statements on one line.
* Stepping granularity is the source line; `stopOnEntry` for an attached
  host-driven target means "the next line the firmware executes".
* `for` loop increments are attributed to the last line of the loop body.
* Programs are single source files; the line table has no file index.
* `wrench dbg` does not ping while the target is stopped, so a target that
  set `setLinkTimeout` releases itself while the user reads variables for
  longer than the timeout. VS Code pings every second.

Libraries:

* The old serial helpers (`utils/win32_comm.cpp`, `linux_comm.cpp`,
  `arduino_comm.cpp`, `lib/std_serial.h`) are no longer used by the debugger
  and have known gaps (short write timeouts, DTR/RTS forced low, the Linux
  version is a stub, the Arduino one lacks close). Remove them or turn them
  into a script-visible serial library.
* The SPIFFS / LittleFS file backend and the FastLED library were fixed from
  reading only; they have not been compiled for ESP32 in this round.
* `WRstr::format()` called with its own `c_str()` as an argument reads freed
  memory.

Tools:

* The language server analyses one file at a time and does not infer types,
  so member completion after `.` offers the members of every struct.
* `src/wrench.cpp|h` and the copies in `examples/arduino` are generated from
  `discrete_src` by `wrench release discrete_src src/.` and were not
  regenerated as part of this work, so the examples that build from `src/`
  still use the old single file build.
