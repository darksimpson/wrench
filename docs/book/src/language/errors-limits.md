# Errors and limits

This page explains what happens when a script is wrong: compile errors,
runtime errors, the many cases that are deliberately not errors, and the
mistakes people coming from C or JavaScript make most often. It ends with
a summary of the limits of the language. Every error code is listed in
[Error codes](../reference/error-codes.md), every limit in
[Limits](../reference/limits.md).

## Compile errors

The compiler stops at the first error and reports one error code, the
line and the column. The command line tool prints the message the
compiler returns:

<!-- check:as=sensor.w -->
```wrench
var x = 5;
var y = x +;
```

Output:

```text
sensor.w:2
err: WR_ERR_bad_expression[9]
2     var y = x +;
                 ^
compile error [WR_ERR_bad_expression]
```

The first line names the file and the line in it (for an error in an
included file, that file; see [include](include.md#errors-in-included-files)).
The next line is the error code with its number; then the source line,
with a caret under the place where the compiler noticed the problem,
which is often right after the actual mistake.

A host gets the same information from the compiler API: the error code is
the return value of `wr_compile()`, the message comes back in its `errMsg`
argument, and `wr_compileEx()` / `wr_compileWithOptions()` also return the
line (from 1), the column (from 0) and the file. The compiler itself
prints nothing. See [Compiling scripts](../embedding/compiling.md).

The most frequent compile errors:

| Error | Usual cause |
|---|---|
| `WR_ERR_var_not_seen_before_label` | a name used without `var`, a typo, or a global declared below the function that uses it |
| `WR_ERR_bad_expression` | a missing operand or operator, two values in a row (`a b`) |
| `WR_ERR_unexpected_token` | a missing `)`, `;`, `}` or `,`, `var a, b;` |
| `WR_ERR_empty_parens` | an empty condition: `if ()`, `while ()` |
| `WR_ERR_integer_literal_too_large` | an int literal above 4294967295 (`0xFFFFFFFF`) |
| `WR_ERR_bad_character_literal` | more than one character between single quotes: `'ab'` |
| `WR_ERR_unexpected_EOF` | an unclosed block, comment or string at the end of the file |
| `WR_ERR_not_assignable` | assigning to a call result, a literal or a constant |
| `WR_ERR_function_redefined` | two functions or structs with one name, maybe in two files |

## Runtime errors

A runtime error stops the script at the instruction that failed. Code
after it does not run, and the call the host made (`wr_run()`,
`wr_callFunction()`, `wr_continue()`) returns a null pointer; the host reads
the code with `wr_getLastError()`. The command line tool prints it:

```wrench
println( "before" );
var reading = readSensor( 3 ); // no such function in this host
println( "after" );
```

Output:

```text
before
err: WR_ERR_function_not_found
```

Errors a script can run into:

| Error | When |
|---|---|
| `WR_ERR_function_not_found` | calling a function that neither the script nor the host defines |
| `WR_ERR_lib_function_not_found` | calling a library function (`lib::name()`) the host did not load |
| `WR_ERR_library_constant_not_loaded` | reading a library constant (`lib::NAME`) the host did not register, or a misspelled `Enum::NAME` |
| `WR_ERR_stack_overflow` | recursion too deep, or too many locals and temporaries for the stack |
| `WR_ERR_hash_not_found` | `new S { name = ... }`, or assigning `s.name`, with a name that is not a member of the struct |
| `WR_ERR_hash_table_size_exceeded` | adding an entry to a hash table that holds 49157 already |
| `WR_ERR_struct_not_exported` | `new` of a module function that was not declared `export` |
| `WR_ERR_cannot_call_function_context_yielded` | a module yields |
| `WR_ERR_yield_in_native_call` | script code called from a native yields |
| `WR_ERR_division_by_zero` | `/`, `%`, `/=` or `%=` by 0, only in hosts built with `WRENCH_TRAP_DIVISION_BY_ZERO` |
| `WR_ERR_malloc_failed` | out of memory, only in hosts built with `WRENCH_HANDLE_MALLOC_FAIL` |
| any code a native sets | a native or library function that fails sets an error code, and the script stops with it |

Wrench has no exceptions: a script cannot catch a runtime error. Check
what may fail before doing it, for example with
[`sys::isFunction()`](../library/sys.md#sysisfunction) before calling a
native that some hosts do not have. What the host does after an error
(report it, reset the context, run it again) is described in
[Errors and robustness](../embedding/errors.md).

### Stopping on purpose: sys::halt

`sys::halt( code )` stops the script with an error code of its own
choosing, a number from 101 to 255 (above `WR_USER`, 100). Any other
number stops it with `WR_ERR_USER_err_out_of_range`. The host sees the
code from `wr_getLastError()` and can tell it from wrench's own errors,
which are all below 100. See [sys](../library/sys.md#syshalt).

<!-- check:runtime -->
```wrench
var config = 0;
if ( !config )
{
	sys::halt( 120 ); // the host reads 120: "no configuration"
}
println( "never printed" );
```

`wrench r` prints such a code as `err: user error 120`.

## Things that are not errors

Wrench is built for small devices and avoids runtime checks that cost
time and code. These all run without an error, and are worth knowing:

| Code | Result |
|---|---|
| `x / 0`, `x % 0` | 0 |
| `2147483647 + 1` | -2147483648, ints wrap |
| a missing argument | 0 |
| an extra argument | ignored |
| reading a member that does not exist, or a member of a number | 0 |
| reading an array element or string character past the end | 0, nothing changes |
| reading a missing hash table key | 0, nothing is added |
| an array or string index that is a float | truncated to an int |
| `"ab" + 'c'` | `"ab99"` |
| `"\0491"` | one character, `\0NNN` is decimal |
| `if ( a = 5 )` | assigns 5 to `a`, the condition is true |

## Common mistakes

Coming from C or JavaScript, these catch people most often:

* **Forgetting `var`.** Every variable needs it, including loop variables
  (`for ( var i = 0; ... )`).
* **Globals below functions.** A function sees only the globals declared
  above it in the source. Declare globals at the top.
* **One name per `var`.** `var a = 1, b = 2;` does not compile.
* **Block scope without hiding.** A name declared in `{ }` or in a loop
  header is gone after the block (`WR_ERR_variable_out_of_scope`), and a
  block cannot declare a name that is still in view
  (`WR_ERR_variable_redeclared`).
* **Changing a parameter to return a value.** Parameters are values: a
  function that changes one changes its own copy, not the caller's
  variable. Return the new value instead (see
  [How arguments are passed](functions.md#how-arguments-are-passed)).
* **Shared containers.** `var b = a;` does not copy an array, a hash
  table, a struct instance or a string.
* **`&&` and `||` give 1 or 0**, not one of their operands as in
  JavaScript; `x || "default"` is 1.
* **Precedence of `&` and `==`.** `x & 1 == 1` is `x & (1 == 1)`, as in
  C; write `(x & 1) == 1`.
* **Character literals are numbers.** `'a'` is 97; `"x" + 'a'` is `"x97"`.
* **Functions are not values.** `var f = handler; f();` calls a function
  named `f`.

## Limits

| What | Limit |
|---|---|
| bytecode of one program | 64KB |
| globals of a program | 255 |
| arguments and locals of one function, members of one struct | 254 |
| functions and structs of a program | 255 |
| arguments of one call | 255 |
| string literal | 65535 bytes |
| array | 2097151 elements |
| hash table | 49157 entries |
| nesting of statements and expressions | 256 levels |
| nesting of includes | 16 levels |
| value stack | 64 entries by default, set by the host |
| int | 32 bit signed, wraps |
| float | 32 bit IEEE |

Exceeding a compile time limit is a compile error naming it
(`WR_ERR_code_too_large`, `WR_ERR_too_many_locals`, ...). The details,
the errors and the ways around each limit (modules for code size, a
larger stack for recursion) are in [Limits](../reference/limits.md).
