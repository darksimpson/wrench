# Error codes

Every error wrench reports is a value of the enum `WRError` in
`wrench.h`. This page lists them all with their number, when they happen
and what usually causes them. How a script experiences errors is
described in [Errors and limits](../language/errors-limits.md), how a
host handles them in [Errors and robustness](../embedding/errors.md).

## Where codes come from

* **Compile time**: `wr_compile()`, `wr_compileEx()` and
  `wr_compileWithOptions()` return the code; the message in `errMsg`
  names it and shows the line. `sys::importCompile()` returns the code to
  the script.
* **Run time**: `wr_run()`, `wr_callFunction()`, `wr_continue()` and
  `wr_executeContext()` return a null pointer, and
  `wr_getLastError( w )` returns the code. The state keeps one error code
  (`WRState::err`, 8 bits); entering the VM again clears it, so read it
  right after the failing call. A null return with `WR_ERR_None` means the
  script yielded (`wr_getYieldInfo()` is true).
* **User codes**: `sys::halt( code )` and native functions may set codes
  above `WR_USER` (100), up to `WR_ERR_LAST` (255).

The table `c_errStrings[]` (declared in `wrench.h`, linked only when used)
holds the names, indexed by code, for the codes below
`WR_warning_enums_follow`. It has no entries for user codes; check the
range before indexing it.

## Compile errors

| # | Code | Meaning |
|---|---|---|
| 1 | `WR_ERR_compiler_not_loaded` | the library was built with `WRENCH_WITHOUT_COMPILER`; `wr_compile()` and `sys::importCompile()` return it |
| 7 | `WR_ERR_unexpected_EOF` | the source ended inside a block, a block comment, a string, a call or an expression |
| 8 | `WR_ERR_unexpected_token` | a token that cannot appear here: a missing `)`, `;` or `{`, `var a, b;`, a missing or doubled comma between enum entries, a nested `{ ... }` in an initializer not followed by `,` or `}`, `private` before something other than `var` or `function` |
| 9 | `WR_ERR_bad_expression` | a malformed expression: a missing operand, two values in a row |
| 10 | `WR_ERR_bad_label` | a name was expected and something else came: a function, struct or parameter name that is a keyword or an enum constant, a function named `yield`, an enum entry that is not a name, an enum value that is not a literal |
| 11 | `WR_ERR_statement_expected` | a function or struct declared inside a function |
| 12 | `WR_ERR_unterminated_string_literal` | the source ended inside a string |
| 13 | `WR_ERR_newline_in_string_literal` | a line break inside a string literal |
| 14 | `WR_ERR_bad_string_escape_sequence` | an unknown escape such as `"\q"`, or `\x` without a hex digit |
| 16 | `WR_ERR_break_keyword_not_in_looping_structure` | `break` outside a loop or switch |
| 17 | `WR_ERR_continue_keyword_not_in_looping_structure` | `continue` outside a loop |
| 18 | `WR_ERR_expected_while` | a `do` body not followed by `while` |
| 19 | `WR_ERR_compiler_panic` | an internal inconsistency of the compiler; please report it with the script |
| 20 | `WR_ERR_constant_redefined` | a constant of a named enum declared twice |
| 21 | `WR_ERR_struct_in_struct` | a struct declared inside a struct (in practice `WR_ERR_statement_expected` comes first) |
| 22 | `WR_ERR_var_not_seen_before_label` | a name that was not declared, or a global used in a function above its declaration, or an enum constant used before its enum |
| 23 | `WR_ERR_unexpected_export_keyword` | `export` before something other than `function`, `unit` or `struct` |
| 24 | `WR_ERR_new_assign_by_label_or_offset_not_both` | a `new S { ... }` initializer mixes `name = value` and positional values |
| 26 | `WR_ERR_empty_parens` | `()` where a value was expected: in an expression, or as the condition of `if ()`, `while ()`, `do ... while ()` or `switch ()` (`for ( ;; )` is allowed) |
| 27 | `WR_ERR_blank_variables_cannot_be_initialized` | `blank x = 1;` or `blank x += 1;` |
| 29 | `WR_ERR_hash_table_size_exceeded` | the member table of a function or struct could not be built; at run time, an assignment or `hash::add()` of a new key to a hash table that is full |
| 30 | `WR_ERR_hash_table_invalid_key` | a hash table initializer with a `{ ... }` as key (`{ {1}: 2 }`), or an empty key with a value |
| 36 | `WR_ERR_hash_declaration_in_array` | a `key: value` entry in an initializer that started as an array, also after nested arrays (`{ {1}, "k": 2 }`) |
| 37 | `WR_ERR_array_declaration_in_hash` | a plain value or a nested array without a key in an initializer that started as a hash table (`{ "a": 1, { 2 } }`) |
| 39 | `WR_ERR_bad_goto_label` | `goto` to something that is not a plain name, or a label defined twice in one function |
| 40 | `WR_ERR_bad_goto_location` | a jump could not be encoded even in its long form; please report it with the script |
| 41 | `WR_ERR_goto_target_not_found` | `goto` to a label that is not in the same function (reported when linking, without a line) |
| 43 | `WR_ERR_switch_case_or_default_expected` | a statement in a `switch` before the first `case` or `default` |
| 44 | `WR_ERR_switch_construction_error` | the jump table of a `switch` could not be built (too large) |
| 45 | `WR_ERR_switch_bad_case_hash` | a `case` value that is not a constant: a variable, an expression |
| 46 | `WR_ERR_switch_duplicate_case` | two equal `case` values, or two `default` |
| 53 | `WR_ERR_too_many_locals` | more than 255 globals, more than 254 arguments and locals in a function or members in a struct, or a positional struct initializer of more than 255 values |
| 54 | `WR_ERR_too_many_functions` | more than 255 functions and structs in a program |
| 55 | `WR_ERR_code_too_large` | the bytecode of the program is larger than 64KB |
| 56 | `WR_ERR_string_too_long` | a string literal longer than 65535 bytes |
| 57 | `WR_ERR_duplicate_parameter` | `function f( a, a )` |
| 58 | `WR_ERR_function_redefined` | two functions or structs of one name, possibly in two included files |
| 60 | `WR_ERR_too_many_arguments` | a call with more than 255 arguments, also when it is nested in another call |
| 61 | `WR_ERR_nesting_too_deep` | statements, expressions or initializers nested deeper than `WRENCH_MAX_PARSE_DEPTH` (256) |
| 62 | `WR_ERR_not_assignable` | assignment, `++` or `--` on a literal, a constant, a call result or the result of an operation |
| 63 | `WR_ERR_member_of_literal` | a member of a literal, such as `"abc".length` |
| 64 | `WR_ERR_include_not_found` | the include callback did not find the file |
| 65 | `WR_ERR_include_not_supported` | `include` compiled without an include callback (`wr_compile()`, `sys::importCompile()`) |
| 66 | `WR_ERR_include_not_at_top_level` | `include` inside braces |
| 67 | `WR_ERR_include_too_deep` | includes nested deeper than `WRENCH_MAX_INCLUDE_DEPTH` (16) |
| 68 | `WR_ERR_bad_character_literal` | a character literal of more than one character, such as `'ab'` |
| 69 | `WR_ERR_integer_literal_too_large` | an integer literal that does not fit in 32 bits, in any base: above 4294967295 or `0xFFFFFFFF` |
| 70 | `WR_ERR_variable_redeclared` | `var x` while an earlier `var x` (or a parameter `x`) is still in view, in the same block or one around it; also the same global in two included files; never in non-strict mode |
| 71 | `WR_ERR_variable_out_of_scope` | a name used after the block that declared it ended, such as the `i` of `for ( var i ... )` after the loop; never in non-strict mode |

## Runtime errors

| # | Code | Meaning |
|---|---|---|
| 2 | `WR_ERR_function_not_found` | the script called a function that neither the script, its imports nor the host define (`wr_getMissingFunctionHash()` gives the name's hash) |
| 3 | `WR_ERR_lib_function_not_found` | the script called a library function (`lib::name()`) that is not registered, or called a library constant as a function |
| 4 | `WR_ERR_hash_not_found` | `new S { name = value }` with a name that is not a member of S, or an assignment `s.name = value` to a member the struct does not have (images compiled by 8.0) |
| 5 | `WR_ERR_library_constant_not_loaded` | the script read a library constant that is not registered (also a misspelled `Enum::NAME`) |
| 6 | `WR_ERR_unknown_opcode` | the bytecode holds a byte that is no opcode: a corrupt image |
| 25 | `WR_ERR_struct_not_exported` | `new` of a function of an imported module that was not declared `export` |
| 28 | `WR_ERR_run_must_be_called_by_itself_first` | `wr_callFunction()` by name or hash on a context whose global code has not run yet |
| 31 | `WR_ERR_wrench_function_not_found` | `wr_callFunction()` by name or hash found no script, imported or library function of that name |
| 34 | `WR_ERR_context_not_yielded` | `wr_continue()` on a context that is not yielded |
| 35 | `WR_ERR_cannot_call_function_context_yielded` | an imported module yielded (in its global code or in a function the importer called), or a library function was called through `wr_callFunction()` on a yielded context |
| 38 | `WR_ERR_stack_overflow` | the value stack is full: recursion too deep, too many locals and temporaries, or a host call with too many arguments |
| 47 | `WR_ERR_bad_bytecode_CRC` | `wr_run()`, `wr_newContext()` or `wr_import()` was given something that is not a valid image for this version: damaged, or compiled by another major version than this one or the one before (see [Images from other versions](../embedding/compiling.md#images-from-other-versions)); `sys::importByteCode()` returns -1 instead |
| 48 | `WR_ERR_execute_function_zero_called_more_than_once` | `wr_executeContext()` on a context whose global code already ran (`wr_resetContext()` it first), or `wr_callFunction()` with a null `WRFunction*` after the global code ended |
| 49 | `WR_ERR_malloc_failed` | an allocation failed: always when loading an image, during execution only with `WRENCH_HANDLE_MALLOC_FAIL` |
| 50 | `WR_ERR_USER_err_out_of_range` | `sys::halt()` with a code that is not between 101 and 255 |
| 51 | `WR_ERR_division_by_zero` | an integer or float division or modulo by zero, only with `WRENCH_TRAP_DIVISION_BY_ZERO` (otherwise the result is 0) |
| 52 | `WR_ERR_debugger_terminated` | the attached debugger ended the program |
| 59 | `WR_ERR_yield_in_native_call` | script code that a native function called back into tried to `yield()` |

A native or library function can stop the script with any code by
setting `w->err`; the script then ends with that code (see
[Native functions and libraries](../embedding/natives.md)).

## Codes that are never reported

These values exist in the enum (and keep the numbers of the others
stable) but nothing in the current code sets them:

| # | Code |
|---|---|
| 15 | `WR_ERR_tried_to_load_non_resolvable` |
| 32 | `WR_ERR_array_must_be_indexed` |
| 33 | `WR_ERR_scontext_not_found` (named `WR_ERR_context_not_found` in `c_errStrings`) |
| 42 | `WR_ERR_switch_with_no_cases` (an empty `switch` compiles) |

## Markers and user codes

| # | Code | Meaning |
|---|---|---|
| 0 | `WR_ERR_None` | no error |
| 72 | `WR_warning_enums_follow` | end of the built in codes; `c_errStrings[]` has names below it |
| 100 | `WR_USER` | user codes are above this value |
| 101 to 254 | | free for `sys::halt()` and natives |
| 255 | `WR_ERR_LAST` | the highest code; also allowed for `sys::halt()` |

`sys::halt( code )` accepts 101 to 255. Natives may set any code, but
codes between 71 and 100 are reserved for wrench's own future errors.
