# Deferred changes and next tasks

Changes that were decided on but put off, usually because they break host
code or older scripts and belong in a major release, and the tasks planned
next. Each entry says why it was deferred and what it touches. Open defects
and ideas still under discussion are in `open.md`, finished work in
`done.md`.

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
  `internals/tools.md`), `open.md`, `done.md` and this page.

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

## Check hash hits against the text

Wrench knows names and keys only by their 32-bit FNV-1a hash (`wr_hashStr`
in `lib/std.cpp`; its comment says FNV-1, but the code applies the xor
before the multiply, which is FNV-1a). Most tables keep no text to check a
hit against, so two different names or keys with the same hash count as
one. Colliding pairs are easy to find: a birthday search over random six
letter names finds some in seconds. Examples:

* `jxzigb` and `pinasi`, as bare names;
* `rsvvfp` and `zirlbh` with the `::` prefix, the form globals are hashed in;
* `costarring` and `liquid`, a published FNV-1a pair.

Two hash values also have a meaning of their own, and names can be solved
for them in seconds: `ppkttia` hashes to 0, and `mwrblcy` and `aarpgaT`
hash to `WRENCH_NULL_HASH` (0xABABABAB). See 4b.

By chance, one table needs about 10000 different names or keys for a 1 %
chance of a collision, and about 77000 for even odds. The real risks are
therefore large hash tables of outside data, and input built to collide on
purpose, for example to pass a `cmd == "reboot"` check or to overwrite
another key. The library and native names in `src` do not collide with each
other (179 names checked, November 2026).

Found and confirmed with the pairs above (`wrench r`, November 2026):

| # | Case | What happens now | Proposed fix |
|---|---|---|---|
| 1 | Compiler: two functions or structs, two variables in one scope, two parameters, two struct members or two `switch` cases whose names share a hash | Stops with `WR_ERR_function_redefined`, `WR_ERR_variable_redeclared`, `WR_ERR_duplicate_parameter` or `WR_ERR_switch_duplicate_case`, at a name the user never defined twice | The compiler still has the text (`WRUnitContext::name`, `WRNamespaceLookup::label`, the case constants). When the hashes match and the text differs, report a new `WR_ERR_names_share_hash` at the second name. The language server's diagnostic names the first one too |
| 2 | Compiler: a name used without `var` whose hash matches a declared one: `var jxzigb = 1; return pinasi;` returns 1, and the same with `::` | Reads and writes the other variable, silently | In `addLocalSpaceLoad` and `addGlobalSpaceLoad`, compare the label of the slot found by hash with the token. A mismatch is `WR_ERR_names_share_hash` |
| 3 | Linker: a call to a function the program does not have, whose hash matches one it has: `pinasi()` runs `jxzigb()`. The same binding makes a script function win over a host function whose name collides with it | Calls the other function, silently | `functionSpace` keeps the called name as a label, as `localSpace` does. The link step compares it with `m_units[u].name` before binding the call by index. A mismatch is `WR_ERR_names_share_hash`. Host functions are covered by 9 |
| 4 | Member access: `s.pinasi` on a struct whose member is `jxzigb` | Reads and writes that member, silently (the struct's member table in the image holds hashes only) | The compiler knows the member names of every struct in the program. A `.name` whose hash matches a member's but whose text differs is `WR_ERR_names_share_hash`. Structs of an imported module are covered by 9 |
| 4a | `goto`: a `goto pinasi;` in a function, or at the top level, with only the label `jxzigb:` there. Two colliding labels in one function | Jumps to `jxzigb:` silently. Two labels stop with `WR_ERR_bad_goto_label` at the second one | Labels exist at compile time only: the image holds plain jump offsets, so nothing changes at run time. `GotoSource` and the label's jump target keep the label text beside `hash` and `gotoHash`. The link step (`link.cpp`, the loop that fills in the gotos) compares the text before filling in a jump, and the duplicate check (`expression.cpp`, at the `:` of a label) compares it too. A mismatch is `WR_ERR_names_share_hash`. A label in another function is already not found (`WR_ERR_goto_target_not_found`) |
| 4b | Names whose hash is a reserved value. 0 means "no label" for a jump target (`gotoHash`) and "no function" for `wr_callFunction()` by hash. `WRENCH_NULL_HASH` marks an empty slot | A label `ppkttia:` is dropped when its statement is merged (`appendBytecode()` skips `gotoHash` 0), so `goto ppkttia;` fails with `WR_ERR_goto_target_not_found`. `wr_callFunction( context, "ppkttia" )` fails with `WR_ERR_execute_function_zero_called_more_than_once`. A struct member `mwrblcy` is missing: it reads 0 and a write is lost. A mailbox key `"mwrblcy"` is lost when the mailbox grows: it reads 0 after 60 more keys. The host registry and a context's function registry use the same layout, so their entries can be lost the same way (not tested). Script hash tables keep such a key in a slot of its own and work. Script functions with these names work too, since script calls go by index | `wr_hash()` and `wr_hashStr()` never return 0 or `WRENCH_NULL_HASH`: either result maps to a fixed other value. This fixes names and runtime keys in one place, at compile time and at run time alike; such a name then shares its hash with whatever hashes to the mapped value, which the checks above catch. `._hash` of such a string changes, and an image compiled earlier that uses one of these names must be compiled again (those names did not work before) |
| 5 | `switch`: the value `"pinasi"` takes `case "jxzigb":`, and so do the int 1031256327 (that hash) and a float with the same bits | Jumps by hash only (`O_Switch` in `vm.cpp`), silently | Switches with string cases compile to a new opcode, for example `O_SwitchChecked`. Each table entry also points at its case constant, and the VM takes the vector only when the value has the case's type and, for a string, the same text; otherwise it goes to `default`. Old images keep `O_Switch` and run as before. `O_Switch` and `O_SwitchLinear` with int cases send a value that is not an int to `default`, a byte compare with no format change |
| 6 | `==` and `!=` on containers compare hashes: `singleValue()` turns a container into its hash. `"jxzigb" == "pinasi"` is true, and arrays hash their raw values, so `{ "a", 1 } == { "a", 1 }` is 0 | Wrong both ways, silently | Two strings compare lengths, then bytes. A string does not keep its hash, so comparing hashes first would read both strings in full before comparing a byte. Lengths, then bytes, stops at the first difference. Hash first, then the value, pays only where the hash is already at hand: table keys (7) and `switch` entries (5). Arrays, hash tables and structs compare element by element, with the depth limit `getHash()` uses (`WR_HASH_MAX_DEPTH`). A number and a container stay unequal. Covers every path: `wr_CompareEQ` (`operations_non_compact.cpp`), `Compare_E_E` and friends (`operations_compact.cpp`), and the fused compare-and-branch opcodes |
| 7 | Script hash table keys: `t[a] = 1; t[b] = 2` makes one entry. An int key equal to a string's hash is the same key as that string, and so are `5.0` and `1084227584` | Overwrites and reads the other entry, silently | Search by hash, then check the key: it is already stored next to its value. On a hash hit, compare the stored key's type and, for a string, its text; a mismatch is a different key. Lookup keeps probing; insert takes the next free slot instead of growing, since growing never separates two keys with one hash. Covers `exists()`, `._exists`, `._remove` and `wr_getValueFromContainer()`, whose keys `wr_addToContainerEx()` stores. Cost: one compare per hit, `memcmp` for a string key |
| 7a | Mailbox (`msg::`, `wr_msgWrite()` and the other `wr_msg*()` functions): `msg::write( "pinasi", 2 )` replaces the value of `"jxzigb"` | Replaced silently: the mailbox keeps one value per slot and no key (it is set up like `globalRegistry`, not like a script hash table) | Every mailbox call has the key text, from the script or the host, so the same check works. The mailbox moves to the script hash table layout, with the key beside the value and the key string copied in, and then searches by hash and checks the key as in 7. Cost per entry: one more value and the key's copy. A mailbox holds a few entries, but small targets may want the check behind a define |
| 8 | Host registry (`globalRegistry`): natives, library functions and library constants keyed by hash. A second registration replaces the first. `ccb` and `lcb` share a union in `WRValue`, so a colliding native and library function make the VM call one through the other's type | Replaced silently, and undefined behaviour (a crash) when the kinds differ | Tag the kind in the entry: native, library function or constant (constants already carry `INIT_AS_LIB_CONST`). Registering over an entry of another kind fails: `wr_registerFunction()` and the `wr_registerLibrary*()` functions return `bool`, which keeps hosts compiling. The call opcodes check the kind, not just a non-null pointer. Searching by hash and checking the key does not work here, because a call in the image carries only the hash and the script's name for it is not in the image. Two entries of the same kind look like a re-registration without names, so debug builds (`WRENCH_INCLUDE_DEBUG_CODE`) keep the registered names in a side table and report a different name with the same hash |
| 9 | Beyond one compile: two imported modules with colliding names (the first in the import chain wins), a script name against a host function, and `wr_callFunction( context, "pinasi" )` running `jxzigb` | Silent; the VM has no names here | The language server sees the workspace files, the host functions and constants in `wrench.json` and the library list (`wrench_lsp_library.inc`). It warns on any two different names that share a hash, across functions, globals, struct members, host functions and library names. The book says that names and keys are hashes and what the checks cover (`language/errors-limits.md`, `embedding/api-reference.md`) |
| 10 | Debugger: a program is known by a 32-bit hash of its image, used for the `wrench-sources/<hash>` folder of `wrench dap`, parked breakpoints (`SetBreakpoints` program 0xFF) and the check that skips an upload | Two images with one hash would share a folder and breakpoints (about 1 in 4 billion per pair) | Low priority. Compare `image.bin` with the image before using a folder, and use another name on a mismatch. A parked `SetBreakpoints` carries the image size beside the hash (a new capability bit). The server and the upload check compare both |

Order: the compiler first (1 to 4a: one new error code, no format change),
the reserved hash values (4b: one place in `lib/std.cpp`), then the VM (6, 7
and 7a: behaviour changes only where hashes collide, no format change), the registry (8: return types of the API), `switch` (5: a new
opcode, documented in `internals/bytecode.md`), the language server and the
book (9), and the debugger last (10).

Tests:

* The pairs above in a new runtime test in `tests/` for 4b, 5, 6, 7 and 7a,
  with a comment on how the pairs and the names were found.
* Compile error cases in `wrench_cli.cpp` for 1 to 4a, and a host call of a
  function named `ppkttia` for 4b.
* A registry test for 8 in the internal tests.
* `lsp_test.js` for 9.

Book: `reference/error-codes.md` for the new code,
`language/control-flow.md` (switch), `language/types.md` and
`language/operators.md` (`==` on containers compares content),
`language/containers.md` (keys), `embedding/api-reference.md` (the return
values of registration), `internals/bytecode.md` and `internals/vm.md`, and
CHANGELIST.

Deferred because it touches the compiler, the VM, the API and the bytecode
format at once, and no script has hit a collision yet. Only 5 (the new
opcode) and 8 (the registration return values) change the format or the
API, so those belong in a release; the rest can land one piece at a time.
