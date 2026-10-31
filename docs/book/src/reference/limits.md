# Limits

Wrench keeps its bytecode and its VM small by using 8 and 16 bit fields
wherever they suffice. This page lists every limit that follows from that,
with the error you get when you go past it and what to do instead. Most
scripts never come near any of them.

## Program size

| What | Limit | When exceeded |
|---|---|---|
| bytecode of one program (one image) | 64KB (65535 bytes) | compile error `WR_ERR_code_too_large` |
| functions and structs in one program | 255 | `WR_ERR_too_many_functions` |
| globals in one program | 255 | `WR_ERR_too_many_locals` |
| source text | none | |
| jump distance (`if`, loops, `goto`, `switch`) | none within the 64KB | |

The 64KB limit comes from the 16 bit offsets of functions and struct
tables in the image. Jumps are not limited: a jump that does not fit in 16
bits is compiled in a longer form automatically. A program that grows past
64KB can be split into [modules](../language/import.md), each up to 64KB,
loaded with `sys::importByteCode()`; there is no limit on the number of
modules.

Every `for ( v : container )` loop uses one hidden global, which counts
toward the 255.

## Functions and structs

| What | Limit | When exceeded |
|---|---|---|
| arguments and locals of one function together | 254 | `WR_ERR_too_many_locals` |
| members of one struct, with its parameters | 254 | `WR_ERR_too_many_locals` |
| values in a positional struct initializer `new S { 1, 2, ... }` | 255 | `WR_ERR_too_many_locals` |
| arguments in one call | 255 | `WR_ERR_too_many_arguments` |
| goto labels, enum constants, cases of a switch | no fixed limit | a `switch` whose jump table outgrows 32KB is `WR_ERR_switch_construction_error` |

## Values

| What | Limit | When exceeded |
|---|---|---|
| int | 32 bit signed, -2147483648 to 2147483647 | arithmetic wraps; a literal over 32 bits is `WR_ERR_integer_literal_too_large` (literals up to 4294967295, `0xFFFFFFFF`, compile and wrap) |
| float | 32 bit IEEE (C `float`), about 7 significant digits | `inf` and `nan` as in C |
| character | 8 bit, 0 to 255 | no wide characters |
| string literal | 65535 bytes | `WR_ERR_string_too_long` |
| array, also a string built at run time | 2097151 elements (`WRENCH_MAX_ARRAY_ELEMENTS`) | writes past it are ignored, reads give 0 |
| hash table | 49157 entries | runtime error `WR_ERR_hash_table_size_exceeded` |

Array element references carry the index in 21 bits, so
`WRENCH_MAX_ARRAY_ELEMENTS` cannot be raised; it can be lowered to stop a
script from allocating huge arrays.

A hash table grows through a fixed list of prime sizes, the largest of
which is 49157. A table that is full cannot take more keys: an
assignment of a new key, or `hash::add()`, stops the script with
`WR_ERR_hash_table_size_exceeded`. Keys it already holds can still be
read and changed. Keep large data in arrays.

## Nesting

| What | Limit | When exceeded |
|---|---|---|
| statements, expressions and initializers nested in each other | 256 levels (`WRENCH_MAX_PARSE_DEPTH`) | `WR_ERR_nesting_too_deep` |
| includes nested in each other | 16 levels (`WRENCH_MAX_INCLUDE_DEPTH`) | `WR_ERR_include_too_deep` |
| containers nested in each other | none; the garbage collector marks any depth with a fixed mark stack (`WRENCH_GC_MARK_STACK_SIZE`) | |
| serialized containers nested in each other | 64 levels (`WRENCH_MAX_SERIAL_DEPTH`), see [Serialization](../library/serialize.md) | |

Each level of parsing costs native stack in the compiler (around 2KB on a
desktop build); lower `WRENCH_MAX_PARSE_DEPTH` for a compiler that runs on
a small task stack.

## The value stack

| What | Limit | When exceeded |
|---|---|---|
| value stack of a context | `WRENCH_DEFAULT_STACK_SIZE` entries, 64 unless changed; the host passes another size to `wr_newState()` | runtime error `WR_ERR_stack_overflow` |

The stack holds the arguments, locals and temporaries of the functions
being called (not containers or strings, which live on the heap). Each
entry is a `WRValue`: 8 bytes on a 32 bit target, 16 on a 64 bit one. A
call takes roughly its arguments plus its locals plus one entry, so a
function with one argument and no locals recurses about 30 deep on the
default stack and about 60 deep in the command line tool, which uses 128
entries.

The stack is checked on every push unless the host is built with
`WRENCH_UNPROTECTED_STACK`; then running out of stack overwrites the
memory behind it. See [Small targets](../embedding/small-targets.md).

## Debugging

| What | Limit |
|---|---|
| programs one debug server follows (attached contexts and imported modules) | 4 (`WRENCH_DEBUG_MAX_PROGRAMS`) |
| line numbers in debug information | none (lines above 16383 use a longer encoding) |

## Errors

| What | Limit |
|---|---|
| error codes | 8 bit: wrench's own below 100, user codes 101 to 255 (see [Error codes](error-codes.md)) |

## Libraries

Library functions have limits of their own (the largest `io::read()`,
format widths of `str::format()` and so on); they are listed on the
library pages, see the [Standard library](../library/index.md).
