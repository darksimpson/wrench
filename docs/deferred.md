# Deferred changes and next tasks

Changes that were decided on but put off, usually because they break host
code or older scripts and belong in a major release, and the tasks planned
next. Each entry says why it was deferred and what it touches. Open defects
and ideas still under discussion are in `review.md` (section "Open
issues").

## Reorganize the examples

The `examples` folder grew a piece at a time and has no common layout:
host programs at the top (`basic.cpp`, `basic.h`, `basic.w`,
`multi_context.cpp`, built by `make_examples.sh` against the single file
build), `arduino/hello_world`, the firmware with the debugger in
`debug_targets` (Arduino serial, ESP32 Arduino, ESP-IDF, STM32 HAL, with a
shared `script/blink.w`) and the VS Code tour `debug_demo`. Give it one
structure (for example by kind: embedding, boards, debugging), a README
that lists every example with what it shows and how to build or run it,
and update the book pages that point into `examples/` (17 today). CI does
not build any of them yet; building the host programs there would keep
them from going stale.

## Rename cc to wc

The compiler's folder `src/cc` and its main files `cc.cpp` and `cc.h`
become `src/wc`, `wc.cpp` and `wc.h` (with `git mv`, so history follows).
It touches:

* `src/wrench.h` (`#include "cc/cc.h"`) and the list of source files the
  single file build is made from in `src/utils/wrench_cli.cpp`;
* `GNUmakefile` (sources and object names), `win32/wrench.vcxproj` and
  `wrench.vcxproj.filters`;
* the book (`internals/building.md`, `internals/bytecode.md`,
  `internals/tools.md`), `review.md` and this page.

The single file build (`wrench.h` and `wrench.cpp`) does not change for
hosts, nor do the names in the code. Check that `make dist`, the Windows
solution and CI still build.

## Check and clean up the code style of all sources

Bring every source file to the style in the book (`internals/building.md`,
"Code style"): tab indentation, braces on their own lines and around every
block, spaces inside parentheses, plain ASCII, no vertical alignment with
spaces, no pseudographics in comments. A rough count over the 70 tracked
files in `src` (October 2026):

* about 1000 lines indented with spaces, in five files
  (`utils/test_wrench_cross_module_globals.cpp` holds most of them,
  then `wrench.h`, `vm/vm.h`, `vm/vm.cpp` and `cc/cc.cpp`), and about 370
  lines with tabs followed by spaces (mostly continuation lines aligned
  under the line above);
* about 480 lines with trailing white space, in 29 files;
* the older compact form `for(` (about 340) and `if(` (about 45);
* single statement `if` / `for` / `while` without braces (about 40) and
  opening braces at the end of a line (about 60), counted by pattern, so
  some are false hits;
* non-ASCII characters on two lines of
  `utils/test_wrench_cross_module_globals.cpp`.

Also look at the tools outside `src` (`tools/vscode-wrench/src`,
`tools/test`, `tools/bookcheck`) and the examples, and at comments that
say what instead of why.

Plan: reformat by machine where it is safe (white space, indentation,
`for(` and `if(`) and fix braces and alignment by hand; keep formatting
apart from other changes, one commit per area (compiler, VM, libraries,
debugger, tools), so a review sees only formatting. Once clean, a
`.editorconfig` (tabs, no trailing white space) and a check in CI keep it
that way. The single file build is made from these files and changes with
them; build it and run every suite after each step.

## Rename WR_ERR_var_not_seen_before_label

"Label" is the compiler's old word for a name (`isValidLabel()`,
`EXTYPE_LABEL`), not a `goto` label, so the error name misleads: it means a
name was used before any `var` declared it (a typo, a global used in a
function above its declaration, an enum constant used before its enum). It
now sits next to `WR_ERR_variable_redeclared` and
`WR_ERR_variable_out_of_scope`.

Plan:

* Rename it to `WR_ERR_variable_not_declared` in `src/wrench.h`, keeping the
  same value, and keep `WR_ERR_var_not_seen_before_label` as an alias of it
  for a release so host code that tests for it still compiles.
* Change the string in `c_errStrings` (`src/utils/utils.cpp`): the command
  line, the language server diagnostics and the debugger show the new name.
* Update the uses in `src/cc/cc.cpp` and the tests in
  `src/utils/wrench_cli.cpp`, and the book (`reference/error-codes.md`,
  `language/variables.md`, `functions.md`, `enums.md`, `import.md`,
  `errors-limits.md`).
* Note the rename and the alias in CHANGELIST; drop the alias in the next
  major release.

Deferred because the name is public API: hosts compare against the enum and
may match the printed string.

## Remove non-strict mode

Non-strict mode (`WR_NON_STRICT_VAR`, `wrench -nostrict`, the VS Code
setting `wrench.nonStrict`) keeps the rules of older scripts: an assignment
declares an unknown variable, names have no block scope and a second `var`
assigns. The intent is to remove it completely once older scripts have had
time to move to the current rules, so the compiler and the language server
keep one set of rules.

Plan:

* First release: compiling with `WR_NON_STRICT_VAR` still works but
  `wrench` prints a warning that the mode will go away; the book says so in
  "Non-strict mode" (`language/variables.md`).
* The release that removes it:
  * `src/wrench.h`: `WR_NON_STRICT_VAR` stays defined, is ignored and is
    documented as reserved, so bit 3 is never given another meaning and
    hosts that pass it still compile.
  * `src/cc/cc.cpp`, `cc.h`: drop `m_nonStrict` and the paths behind it (the
    implicit declaration on assignment, the skipped block checks). The
    `new S { a = 1 }` initializer uses the same member today to stop member
    names being taken as variables; it needs its own flag.
  * `src/utils/wrench_cli.cpp`: drop `-nostrict` (or accept it with an error
    that explains the removal) and the non-strict cases of
    `testRedeclaration()` and the other internal tests.
  * `src/utils/wrench_lsp.cpp`: drop `m_nonStrict`, the non-strict indexing
    and the configuration handling for it; `tools/test/lsp_test.js` and
    `extension_test.js` lose their non-strict checks.
  * `tools/vscode-wrench`: remove `wrench.nonStrict` from `package.json`,
    `src/extension.ts`, `src/commands.ts` and the README.
  * Book: remove the "Non-strict mode" section and the references to it
    (`reference/cli.md`, `reference/error-codes.md`,
    `embedding/compiling.md`, `embedding/api-reference.md`,
    `internals/bytecode.md`, `internals/tools.md`, `debugging/protocol.md`,
    `start/tools.md`, `vscode/editing.md`, `language/functions.md`).
  * CHANGELIST: list it as a breaking change, with how to update a script
    (declare every variable with `var`, move a name used after its block
    out of the block or declare it as `var ::name`, turn a repeated `var`
    into an assignment).

Deferred because older scripts compiled with `-nostrict` would stop
compiling; they need a release with the warning first.
