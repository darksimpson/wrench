# Wrench review notes (October 2026)

Two reviews of the compiler, VM, libraries, debugger and tools found a number
of real defects. Most are fixed (see CHANGELIST, section "next"); each fix
has a regression check in `tests/029_library_fixes.w`,
`tests/030_compiler_fixes.w`, `tests/031_vm_fixes.w`,
`tests/032_audit_fixes.w`, `tests/033_library_audit.w`, the internal tests
of `wrench t`, the debugger test suite or the tool tests in `tools/test`.
This page records what was fixed in the second review, what is still open
and what would be worth improving next.

## How the code is tested now

* `wrench t` runs every test file twice: as before, and compiled with debug
  information under an attached debug server, so the debugger hooks cannot
  change program behaviour unnoticed. Output mismatches fail the run.
* Internal tests cover host API corners and compiler limits (too many
  arguments, deep nesting, stores kept with debug information).
* The debugger has an end to end suite (`[debugger protocol]`, 507 checks)
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

* GitHub Actions (`.github/workflows/ci.yml`) runs all of this on every push
  and pull request: Linux default, compact, really compact and sanitizer
  builds, Windows Debug and Release with the node suites, and the single file
  build checked with GCC and MSVC. The single file build is no longer kept
  in git; CI publishes it with every release.

Worth adding: a fuzzer for `wr_compile` and `wr_deserialize` (both take
untrusted input).

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

## Open issues: language and compiler, done

* `&&` and `||` short circuit: the right side is parsed into the operator
  node and compiled behind a branch, so `if ( i < a._count && a[i] == x )`
  no longer reads (and grows) `a[i]` out of range. They still yield 1 or 0.
  `tests/034_short_circuit.w` covers order, values, precedence, ternaries,
  loops and guards.
* Jumps farther than 16 bits: a jump found too far is recorded by its source
  position and the source compiled again with that jump in a long form
  (the extended opcode `OX_RelativeJump32`, conditional branches reach one
  placed next to them); programs whose jumps fit compile in one pass,
  unchanged. Switch cases out of reach of the table go through trampolines
  after it, and a switch past 32KB of code no longer stores truncated
  positions. Old images still run, the new opcode only appears where a jump
  needs it.
* The one byte opcode space was full (255 values with the opcodes of this
  round). The prefix `O_Extended` opens a second table: the next byte is a
  `WRExtendedOpcode`, at one byte and one dispatch more, for rarely executed
  opcodes; `OX_RelativeJump32` is the first. New single byte opcodes go after
  `O_Extended` so its value stays fixed; rare ones belong in the second table.
* Assigning to a literal, a library constant, a call result or the result of
  an operation (`3 = 4`, `io::O_CREAT = 1`, `f() = 1`, `a + b = 7`, `5++`) is
  `WR_ERR_not_assignable`, and so is assigning to a member or element of a
  call result (`f().x = 3`, `f()[0] = 1`, also `(f()).x = 3`), which used to
  change a copy. Parentheses around a variable and the ternary stay
  assignable, they write through (`(t).x = 1`, `(c ? a : b).x = 1`).
* A goto label defined twice in a function is `WR_ERR_bad_goto_label`.
* The compiler prints nothing; the message comes back through `errMsg` and
  the command line tool prints it. It stops at the first error, so there is
  only ever one message per run.
* `\0NNN` escapes stay decimal (`"\097"` is `a`); the website documents the
  escapes now.
* Reading a member or element of a value that is no container turned that
  variable into an empty hash table or array, losing its value (`s.length`
  wiped a string). Reads now give 0 and leave the value alone; only the
  member and element steps of an assignment target create containers (new
  opcodes `O_StackIndexHashCreate` and `O_IndexCreate`). Images from older
  compilers lack `WR_READS_KEEP_VALUES` in their flags and keep the old
  behaviour. `"abc".length` is `WR_ERR_member_of_literal`.
* The disassembler and the debug client's line table walk took the
  `LLValues` / `LGValues` / `GLValues` / `GGValues` opcodes for one byte;
  they have two operand bytes.

## Open issues: VM, done

`tests/036_vm_robustness.w` and the internal test
`[vm: callback errors, bad opcodes, copies, stack, threads]` cover this round.

* The value stack is checked on every push by default
  (`WRENCH_PROTECT_STACK_FROM_OVERFLOW` is now defined unless
  `WRENCH_UNPROTECTED_STACK` is): a deep expression or a call with many
  arguments fails with `WR_ERR_stack_overflow` instead of overwriting the
  memory behind the stack. `WRENCH_UNPROTECTED_STACK` drops the check for
  scripts known to fit; CI builds and tests that configuration too.
* Arrays grow with spare room (half their size, at most 65535 elements),
  kept in `m_mod` and marked by `GCFlag_Slack`, so appending in a loop is
  amortized linear. Room reused after `array::truncate` reads as zeros.
  Structs keep their exact size (their `m_mod` holds the member table).
* The hash table key equal to the empty marker `WRENCH_NULL_HASH`
  (0xABABABAB, the int -1414812757) is stored in one extra slot behind the
  table (`hashSlots()` is `m_mod + 1` for hash tables); iteration,
  printing, serialization, the GC and the debugger walk it.
* The scratch values behind `deref()` / `singleValue()` and
  `g_mallocFailed` are thread local (`WRENCH_THREAD_LOCAL`) on Windows,
  Linux, macOS and ESP-IDF, so states on different threads or cores do not
  race; bare metal targets keep plain statics, and defining
  `WRENCH_THREAD_LOCAL` overrides the choice.
* A native callback or library function that sets `w->err` stops the
  script with that error, also when the error comes from a call back into
  the script that failed; clearing `w->err` in the callback goes on.
* `wr_returnValueFromLastCall()` returns the resolved value, never a
  reference into a finished frame.
* An opcode the VM does not know (the unused values 253 to 255, or an
  unknown extended opcode) fails with `WR_ERR_unknown_opcode` in both
  interpreters instead of being undefined behaviour.
* GC marking uses a small fixed mark stack (`WRENCH_GC_MARK_STACK_SIZE`, 32
  entries) instead of recursion; containers that do not fit are rescanned
  afterwards, so a structure of any depth is marked with bounded native
  stack.
* Values handed between unrelated contexts: `wrench.h` documents that a
  string, array or hash table belongs to the context that created them,
  and `wr_copyValue()` makes a deep copy owned by another context.

## Found in the VM round, done

`tests/037_strings_and_empty_tables.w` covers both.

* Writing a character of a string (`s[1] = 'z'`, `s[0] += 1`, `s[1]++`)
  changed a scratch copy: the element reference into a character array
  fell through to `deref()`, which hands out a copy of the character. Every
  string was affected, not only ones from literals. A write past the end
  grew the string with zeros and lost the character; it is stored now.
  Strings stay shared on assignment like arrays (`var t = s;` is a second
  name for the same string, `t += "!"` builds a new one).
* `{ }` compiled to the int 0, so a copy of it (`var g = e;`) was not the
  same container once either was written. It is an empty array after
  `var a[] =` and an empty hash table anywhere else (as `{ : }`).

## Open issues: debugger, done

* Watchpoints: a variable, element or member (`x`, `a[2]`, `p.member`,
  `h["key"]`, `s[0]`) is followed again from its root at every line marker
  and the target stops on the line after a change (stop reason 5, new
  requests AddWatch / RemoveWatch, event WatchRemoved, capability bit 0x10:
  no protocol version change, older clients and targets ignore it). VS Code
  sets them with Break on Value Change (conditions and hit counts on the
  host), `wrench dbg` with `watch` / `unwatch`. A watched local goes away
  when its call returns. Column breakpoints were left out: several
  statements on one line are rare and every marker costs code.
* The `for` increment entry was out of date: an increment on its own line
  has its own line marker. A test steps through a loop written over several
  lines.
* `wrench dbg` pings a halted remote target every second while waiting at
  its prompt (a thread, the client handed over under a lock), so a target
  with `setLinkTimeout` no longer lets go while the user reads variables.
  `wrench dbgserver -linktimeout <ms>` sets the timeout on a PC target.

## Modules: include and private, done

`tests/038_include.w` (with `tests/include/`), the internal test
`[include and private]`, debugger, VS Code adapter, language server and
grammar tests cover this.

* `include "file.w"` at the top level compiles that file in place, once per
  program (repeats and cycles are skipped). It is expanded before parsing
  (`src/cc/include.cpp`), so parser, long jumps and line markers see one
  text; nothing is copied when nothing is included. The compiler opens no
  files: `wr_compileWithOptions()` takes an include callback. The host tools
  look next to the including file, then in `-I` folders (`wrench c`, `p`,
  `r`, `ch`), then in the `"include"` folders of `wrench.json`. Errors name
  the file and its own line (`WR_ERR_include_not_found`, `_not_supported`,
  `_not_at_top_level`, `_too_deep` are new). `sys::importCompile` and
  `wr_compile` have no callback: include fails there.
* Lines are numbered through the expanded text; the debug symbols end with
  a file table (names, segments, embedded text lengths) and
  `-debugsource` embeds every file. The target and the protocol stay
  line based; the debug client maps virtual lines to files
  (`WrenchDebugProgram::locate`, `virtualLine`, `breakableLine(file, line)`).
  VS Code: breakpoints in every file, frames in their files, embedded
  sources per file. `wrench dbg`: `b file.w:line`, `l file.w:line`. The
  language server compiles with includes (open editors count with their
  unsaved text), shows an error of an included file on the include that
  brings it in, and counts what included files define.
* `private var` / `private function` at the top level: the name is its
  file's own (hash of name and file, like C `static`); code in that file
  finds it before a shared name, calls are linked per call site's file. A
  private global is not found by `wr_getGlobalRef()`, a private function not
  by name from the host or another file. The debugger shows `count@util.w`
  and evaluates a name in the stopped frame's file.
* Found on the way: a block comment ending in `**/` did not end; a call
  between two markers of one line made a breakpoint there stop twice
  (`println( f() )`), call markers no longer start the line again.

## Several programs per debug server, done

The internal test `[debugger protocol]` (attached contexts, an imported
module, its breakpoints over a rerun), the console debugger and VS Code
adapter tests (`import.w`, `ticker.w` run by `dbgserver -context`) cover
this.

* One `WrenchDebugServer` debugs up to `WRENCH_DEBUG_MAX_PROGRAMS` (4)
  programs: contexts the firmware hands over with `attach( context, name )`
  / `detach()` (the first is program 0, the one uploads replace), and the modules a
  debugged program imports with `sys::importCompile` or
  `sys::importByteCode`. A destroyed context stops being debugged (a hook in
  `wr_destroyContextEx`). Protocol version 2 was extended in place, it was
  not released: GetPrograms, ProgramAdded / ProgramRemoved, a program number
  on ReadProgram, SetBreakpoints, GetGlobals, SetGlobal, AddWatch, stack
  frames and Stopped.
* Imported modules run with the line hook now: `sys::importCompile`
  compiles them with debug information and source while a debugger is
  attached. The VM records each call into an import (caller, pc, frame), so
  a stack walk goes on from the module into its caller and steps go in and
  out. A step ends only on its own stack. A module's breakpoints are kept
  by bytecode hash on the target and are back when the next run imports it.
* VS Code: a thread per program with its own stack (a module runs on its
  importer's), the Modules view, module sources by reference (stable over a
  re-import), breakpoints bound in every program with the file, Globals of
  the frame's program, watchpoints in other programs. `wrench dbg`:
  `programs`, "line 3 in import 1", `b file.w:line` in any program, `c`
  waits while firmware contexts run. `wrench dbgserver -context file.w`
  attaches more scripts and calls their `tick()`.
* Found on the way: `WRstr::insert` grew its buffer by the inserted length
  only, so building a long text piece by piece (the disassembler on a large
  program) was quadratic; it grows by half now.

## Libraries and tools, done

* The old serial helpers (`utils/win32_comm.cpp`, `linux_comm.cpp`,
  `arduino_comm.cpp`, `lib/std_serial.h`) are gone: nothing called them and
  scripts could not reach them. `WRENCH_WIN32_SERIAL` / `WRENCH_LINUX_SERIAL`
  still switch on the debugger's serial transport.
* `WRstr::format()` emptied the string before formatting (the C++11 path,
  every current build), so `s.format( "<%s>", s.c_str() )` lost the text;
  it formats first now, like appending. Internal test `[WRstr format with
  its own text]`.
* The language server reads the files a document includes: hover, go to
  definition, completion and signature help reach what they declare at the
  top level, a private name stays its file's own. A variable only ever
  given `new S` has the type S: completion after `p.` offers the members of
  S, and references and rename of `p.member` follow S. Without such a type
  it stays as it was (every struct's members, a link only when one struct
  has the member).
* `examples/arduino/debug` (an empty sketch with bytecode in an old format)
  is gone; `examples/debug_targets/arduino_serial` replaces it.

## Debug server API: the firmware loads and runs, done

* The server mixed loading and running scripts with debugging them:
  `loadBytes()` made program 0's context and owned it, `tick()` ran a
  launched program itself, and an upload swapped the context under the
  firmware, which therefore had to ask `context()` again every time. Now
  the server runs no script code. The firmware makes contexts
  (`wr_newContext()`), hands them over with `attach()` and runs them;
  `loadBytes()`, `context()` and `ready()` are gone.
* A launch or an upload is a request to the firmware: `tick()` returns
  `WRD_TickRunRequested` / `WRD_TickProgramUploaded`, the firmware takes
  the bytes with `takeUpload()`, swaps its context and runs it. Firmware
  says it does so with `setLaunchable( true )`; without it a debugger can
  only attach (launches and uploads are refused instead of hanging).
* `wr_resetContext()` (new, VM): fresh globals, the modules it imported
  destroyed, the same pointer. A restart works for any program 0 now; it
  used to be refused for a context the firmware made.
* The end of program 0's run reaches the debugger through a VM hook in
  `wr_executeContext()` / `wr_continue()`, whoever started the run (a run
  the firmware started on its own, `dbgserver -hold` for example, used to
  end silently).
* The examples, `wrench dbgserver`, the local target of `wrench dap` /
  `dbg` and the test harness are firmware like any other (the host tools
  share `WrenchHostProgram`). Debugger checks: restarting a firmware
  program in place, a target that is not launchable; internal test
  `[wr_resetContext runs the global code again]`.

## Blocking scripts and natives, done

* `while ( 1 ) {}`, `for ( ;; ) {}` and `do {} while ( 1 );` compiled with
  debug information had no line marker on their way round (a literal or
  missing condition emits none, an empty body neither): the server never
  polled, a debugger could not pause or terminate them. A loop whose
  condition emitted no line marker now gets one for the loop's line where
  each pass begins (`pushLoopMarker()`).
* Polling was every 64 line markers, so slow lines (a native that waits 30
  ms) stretched it to seconds. Now every `WRENCH_DEBUG_POLL_MS` (20); the
  clock is read every `WRENCH_DEBUG_POLL_LINES` (8) markers, a clock read
  per VM instruction would cost more than the instructions.
* Nothing runs line markers inside a native: `WrenchDebugServer::poll()`
  lets a native that blocks service the link while it waits (the same
  limits as polling between lines; pause and terminate land on the next
  line).
* Terminate aborted a yielded program 0 only in its global code, and with
  the next script code of any context. Now any yielded call of program 0,
  and only that context (`abortContext`), also in the stop loop of another
  program; a reset or release of the context drops it.
* Debugger checks: pause and terminate in the three empty loops, pause
  latency with slow lines, requests answered inside a polling native, a
  terminated yielded call of a host driven firmware.

## Open issues

Language and compiler:

* A program is limited to 64KB of bytecode (`WR_ERR_code_too_large`): function
  offsets and other fixups in the image are 16 bit. Lifting it would change
  the bytecode format; jumps and switches inside that limit are not limited
  any more.
* Assigning to a member or element of a variable that holds a string or a
  number still replaces it with a container (`var n = 5; n.k = 1;`); only
  reads were changed. A runtime error there was considered and left out.
* Reading an element past the end of an array grows the array.
* Namespaces were deferred. A design that fits: `namespace x { ... }` at the
  top level prefixes the names declared in it (`x::name`), unqualified names
  inside look in the namespace first; `x::f()` calls compile as library
  calls today, the linker would retarget them in place (both 5 bytes) and a
  library call that finds nothing would look in imported modules.
* A private function cannot be reached through its name as a value (`var f
  = helper;` holds the shared name's hash); `private struct` is not
  supported.

VM:

* Calling a function by its name as a value (`var f = name; f();`) is not
  supported (see 7.0.1 notes).
* Opcodes are validated only as they execute: a corrupt image whose CRC
  matches can still read operands out of range before reaching an unknown
  opcode. A validation pass at load time would make images from untrusted
  sources safe.

Debugger:

* Stepping granularity is the source line; `stopOnEntry` for an attached
  host-driven target means "the next line the firmware executes". Left as
  it is: finer markers would cost code size in every debug build.
* A module imported for the first time cannot have breakpoints set before
  its global code runs (its source is only known once it is there); break
  in the importer and step in, or set them and run again.

Libraries:

* The SPIFFS / LittleFS file backend and the FastLED library were fixed from
  reading only; they have not been compiled for ESP32 in this round.

Tools:

* The language server's find references and rename stay within one file,
  and types are only the evident ones (`new S`): a variable given the
  result of a call, a parameter or a member gets no type.


## Found while writing the book, done

Writing the user documentation (docs/book) turned up the bugs below; each
fix has a test (tests/039_compiler_book_fixes.w, 040_vm_book_fixes.w,
041_library_book_fixes.w, internal tests, debugger checks, the node suites
and tools/test/extension_test.js). CHANGELIST, 8.0.0, lists them for users.

* Version 8.0.0: the unreleased opcodes and image flag needed a new major
  version, which salts the image CRC: 7.x VMs refuse 8.0 images, the 8.0 VM
  runs 7.x images that lack WR_READS_KEEP_VALUES (tests/v7_image.bin).
* Compiler: a struct with parameters and no member crashed, a member named
  like a parameter declared nothing; `new` only as the right side of an
  assignment; empty conditions compiled; `var a[n]` made no array; nested
  initializers had to come last in a hash literal; `enum { A B }`;
  `var = 1;`; literal errors reported as unexpected EOF; nested call
  argument errors; absolute include paths in images; a return in the middle
  of the global code left no Stop behind it, so later host calls ran on
  into the rest of the global code.
* VM: arguments aliased the caller's variable and natives read them late;
  a module calling its importer crashed; float keys and indexes; `h.name`
  replaced stored keys; reads created entries (new images only now); a full
  hash table overwrote entries; `~` on floats; string ordering by hash;
  numbers equal to strings; `._exists` on arrays; unknown struct members;
  `wr_callFunction( c, 0 )` re-ran the global code; the sticky malloc
  failure flag; the division trap; `wr_getValueFromContainer` inserting;
  no way to keep host-held values from the collector (`wr_keepValue`);
  scheduler ids; `wr_runCommand` ignoring runtime errors; `+=` with a string
  on an element of a container (`h["k"] += "b"`) left it unchanged or
  stored a hash (both interpreters, `wr_concatStringToElement`).
* Libraries: `sys::isFunction` for imported functions, `str::mid` of "",
  `str::format` (now C printf, without the C library unless
  WRENCH_FLOAT_SPRINTF), `time::ms` / `std::time` on boards, tcp failure
  values, io (`O_WRONLY`, `getline`, `deleteFile`, `write`), `msg::` storing
  hashes (now values, with a host API), `str::isalnum`, loader declarations,
  `wire::write`, `lcd::`, the empty `wr_loadEsp32Lib` (removed), a global
  rand seed.
* Tools: CLI exit codes and error names, the command word anywhere in the
  arguments, `ca`, `release`, dbg leaving without a Detach, `r` while halted
  in a firmware call, the idle prompt, `._count` in the evaluator, option
  checks, dbgserver listening on every interface (now loopback by default),
  VS Code attach defaults and messages, the wrench.json schema, bundle-exe,
  Show Disassembly, the disassembler (member tables, GlobalStop, line
  markers, symbols), the stale image in examples/arduino/hello_world.

Still open from that round:

* `._remove()` returns the container, so removals chain; a 1 / 0 result
  would need a new opcode, the compiler builds `{}` with this one.
* A module cannot call functions of its importer
  (WR_ERR_function_not_found): the importer's global code is still running
  when the module's runs. Modules can call each other and the host.
* Member syntax on a key that does not exist yet stores the key's hash, the
  name itself is not in the bytecode.
* A `wrench dbg` without -attach on a target that refuses uploads detaches
  now, but its Hello has already consumed a `-hold` halt; avoiding that
  needs a protocol change.
* `WR_ERR_tried_to_load_non_resolvable`, `WR_ERR_array_must_be_indexed`,
  `WR_ERR_scontext_not_found` and `WR_ERR_switch_with_no_cases` are never
  set; `c_errStrings` is not there with WRENCH_WITHOUT_COMPILER.
* glibc 2.41 prints `%#g` wrong when rounding carries into a new digit;
  wrench without WRENCH_FLOAT_SPRINTF prints the C standard result.
* The website (www/index.html) is behind the code in many places; the book
  is the reference.
