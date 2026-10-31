# The virtual machine

This page explains how the VM in `src/vm` runs a program: the values, the
stack and its frames, calls into natives and library functions, contexts
and modules, the garbage collector, yields and time slices, the hooks the
debugger uses, and how the opcodes are organized. It is for people who
work on wrench or want to know what their scripts cost. The host API is in
[Embedding wrench](../embedding/architecture.md).

## States, contexts and values

A `WRState` (made by `wr_newState()`) holds what all its programs share:
the registry of natives, library functions and library constants (one hash
table, keyed by the hash of the name), the list of contexts, the stack size
each context gets, and the garbage collection threshold.

A `WRContext` is one loaded program. `wr_newContext()` allocates it in one
piece: the context structure, its globals, a copy of the function table
and, unless it is a module (see below), its stack. It keeps a pointer to
the bytecode, which is not copied (see
[Running from ROM](bytecode.md#running-from-rom)), a registry of its own
functions by name hash, and the list of objects it allocated.

Every value, on the stack, in a global or in a container, is a `WRValue`:
two machine words, 8 bytes on a 32 bit target and 16 on a 64 bit one. The
second word holds the type in its low bits: an int, a float, a reference to
another value, or an extended type whose upper bits say which (array, hash
table, struct, raw array of the host, iterator, container member). Ints and
floats live in the value itself; strings, arrays, hash tables and struct
instances are objects the value points to. The language side is in
[Values and types](../language/types.md), the host side in
[Values: WRValue](../embedding/values.md).

## The interpreter

The whole interpreter is one function, `wr_callFunction()` in
`src/vm/vm.cpp`, a loop that reads an opcode byte and dispatches on it:

* with GCC and Clang through a table of label addresses (computed goto),
  one indirect jump per instruction;
* with MSVC, and with `WRENCH_REALLY_COMPACT` everywhere, through a
  `switch`.

`WRENCH_COMPACT` keeps the dispatch but lets many opcodes share code
(`src/vm/operations_compact.cpp` instead of `operations_non_compact.cpp`),
which saves code size and costs some speed. The trade-offs are in
[Small targets: memory and build flags](../embedding/small-targets.md).

Arithmetic and comparisons go through tables of functions indexed by the
types of both operands (4 types, 16 entries), so `a + b` costs one table
lookup whatever the types are.

## The stack and frames

The VM is a stack machine. Operands are pushed on the context's value
stack, operations work on its top, and a few pointers inside the
interpreter (`register0`, `register1`) hold the operands of the fused
instructions that address variables directly.

A call builds a frame on the same stack:

| Stack, bottom to top | |
|---|---|
| the arguments | `l[0]` and up: what the caller pushed |
| the locals | the function's other variables, set to the int 0 |
| the return vector | the caller's frame base and the code offset to return to |

The frame base points at the first argument, so parameters and locals are
both addressed as `l[n]` from it. The function table entry says how many
of each there are (see [The function table](bytecode.md#the-function-table)).
When the caller passes more arguments than the function has parameters, the
extra ones are dropped; missing ones are the int 0.

A variable or an element passed as an argument is pushed as a reference
to it, the cheapest thing to push. When a script function starts, the VM
replaces every such reference with the value it refers to, so a parameter
is the function's own copy and changing it never reaches the caller's
variable (a container is still shared: the copy refers to the same
object). Where a later argument of the same call may change a variable an
earlier one names (`f( i, i++ )`), the compiler reads the earlier one
first with `Dereference`, so every argument has the value it had where
the call is made. Natives and library functions get the arguments as
they were pushed; see [Native functions and libraries](../embedding/natives.md).

`Return` takes the return address and the caller's frame base from the
vector and leaves the result just above the frame, where the caller picks
it up (`PushIndexFunctionReturnValue`). When the host calls a function with
`wr_callFunction()`, the return address is the `Stop` at the end of the
global code, which hands the result to the host.

The stack has a fixed size per context, `WRENCH_DEFAULT_STACK_SIZE` (64
values) unless `wr_newState()` is given another. It only holds calls,
arguments, locals and temporaries; containers are allocated elsewhere.
By default every push and every call is checked against the end
(`WR_ERR_stack_overflow`); `WRENCH_UNPROTECTED_STACK` leaves the check out.
See [Limits](../reference/limits.md).

## Calls

The compiler and linker pick one of three kinds of call:

| Opcode | Calls | Found |
|---|---|---|
| `CallFunctionByIndex` | a function of the same program | by the linker; the opcode holds the index into the function table |
| `CallFunctionByHash` | a native registered with `wr_registerFunction()`, or a function of an imported module | at run time, by the hash of the name |
| `CallLibFunction` | a library function, a name with `::` (`math::sin`, `str::len`, your own `app::log`) | at run time, by the hash of the full name |

A call by hash looks in the state's registry first, then in the modules
the context imported, then asks the host's not-found handler
(`wr_setOnCallbackNotFound()`); if nothing answers, the program stops with
`WR_ERR_function_not_found` and `wr_getMissingFunctionHash()` tells which
name it was. Library calls work the same way with
`wr_setOnLibCallbackNotFound()` and `WR_ERR_lib_function_not_found`.

The two kinds of host functions see the stack differently:

* a native (`WR_C_CALLBACK`) gets a pointer to its arguments, their
  number, a value for its result and the `usr` pointer it was registered
  with;
* a library function (`WR_LIB_CALLBACK`) gets the stack top, the number of
  arguments below it and the context, and writes its result to the stack
  top. That is cheaper, and is how the standard library is written.

While a host function runs, the context is marked as being in a native: if
it calls back into the script (`wr_callFunction()`), the nested call starts
above the live stack and may not yield. A host function stops the script by
setting `w->err`; the VM then fails the call with that error. Details are
in [Native functions and libraries](../embedding/natives.md).

## Contexts and modules sharing a stack

`wr_import()` loads more bytecode into a running program; the library
functions `sys::importCompile()` and `sys::importByteCode()` call it from a
script (see [Modules: importing code at run time](../language/import.md)).
The module becomes a context of its own, with its own globals and function
table, but it has no stack of its own: it runs on its importer's stack,
starting above whatever the importer has there. Its global code runs once,
right away, to its end; a module may not yield.

The importer then finds the module's functions by name hash, as the second
place a call by hash looks. A module finds its own functions, the
functions of the other modules of its importer and the state's natives,
but not the functions of the script that imported it: such a call fails
with `WR_ERR_function_not_found`. Several contexts made with `wr_newContext()` on
one state, on the other hand, each have their own stack; they share only
the state's natives and library functions, and talk through the `msg::`
library or the host (see
[States, contexts and running code](../embedding/running.md)).

## Garbage collection

Strings, arrays, hash tables and struct instances are objects allocated by
the context that created them and kept on its list. Ints, floats and the
values on the stack are not objects and are never collected. Raw arrays
the host hands to a script stay the host's.

The collector is a mark and sweep collector that runs only at allocation
points (a string literal pushed, an array created, a container grown) and
when the host calls into the context, and only when the context has allocated more than a threshold since the last
collection: `WRENCH_DEFAULT_ALLOCATED_MEMORY_GC_HINT`, 4000 bytes, which
`wr_setAllocatedMemoryGCHint()` changes per state. A script that allocates
nothing never runs it.

A collection:

1. marks everything reachable from the context's globals, from objects
   marked permanent (`GCFlag_Perm`, which `wr_keepValue()` sets for a
   value the host holds), and from every value on the stack below its top,
   following containers into their elements;
2. sweeps: frees every object of the context that was not marked.

The marking uses a fixed size stack of `WRENCH_GC_MARK_STACK_SIZE` (32)
entries instead of recursion, so deep structures cannot exhaust a small
native stack; when it overflows, marking rescans the marked objects until
nothing is left out. A context and the modules it imported hand objects to
each other on their shared stack, so they are collected together, as one
group.

`wr_destroyContext()` frees everything the context owns, `wr_destroyState()`
every context.

## Units

The compiler calls the global code and each function a unit: unit 0 is the
global code, unit n is function n - 1 of the function table. The
disassembler, the debug symbols and the call markers count this way. In the
language, `unit` is another word for `function`
(see [Functions](../language/functions.md)).

## Yield and time slices

`yield( ... )` is not a function call: the linker turns a call of the name
`yield` into the `Yield` opcode. It saves the code position, the stack top
and the frame base in the context and returns to the host with a null
result. The host sees the values passed to `yield()` and sets the value
`yield()` returns with `wr_getYieldInfo()`, and resumes with
`wr_continue()` (or any `wr_callFunction()` on that context, which
continues instead of calling). A script cannot yield while a native it
called is calling back into it (`WR_ERR_yield_in_native_call`), and a
module cannot yield at all.

With `WRENCH_TIME_SLICES` the VM can also yield on its own, so one script
cannot keep the others from running:

* `wr_setInstructionsPerSlice()` sets a budget; 0 (the default) turns it
  off;
* the budget is counted down at jumps and conditional branches, so every
  pass of every loop counts, while straight line code between them is
  free;
* at zero the VM yields as if the script had called `yield()` without
  arguments; `wr_continue()` goes on where it stopped;
* `wr_forceYield()` makes the next counted jump yield, and the class
  `WrenchScheduler` runs several programs round robin with it.

The counting costs a decrement per jump, which is why it is a build flag.
Using these from a host is described in
[Yield, time slices and the scheduler](../embedding/scheduling.md), from a
script in [Yield and cooperative tasks](../language/yield.md).

## The debug hooks

In a build with `WRENCH_INCLUDE_DEBUG_CODE` every context has a pointer to
a `WrenchDebugServer`, null unless the firmware attached one. The VM calls
it at these points, and does nothing more when the pointer is null:

| Hook | When |
|---|---|
| `onDebugInfo()` | a line marker runs (see [Debug information](debug-info.md#what-the-vm-does-with-them)); the server may block there while the program is stopped |
| `onError()` | a runtime error is about to end the call; the frames are still intact, so the debugger can show where and why |
| `onImport()`, `onImportCall()`, `onImportReturn()` | a module is imported, called into and returned from, so the debugger can step into modules and walk the stack through them |
| `onGlobalCodeEnd()` | a context's global code finished or failed (not yielded) |
| `onContextReset()`, `onContextDestroyed()` | `wr_resetContext()` or `wr_destroyContext()` on a debugged context |

The compiler side, the line markers, is only emitted with `-debug` or
`-debugsource`; the server side is described in
[Adding the debugger to your firmware](../debugging/firmware.md).

## Opcode families

There are 253 single byte opcodes (`src/utils/opcode.h`), most of
them variants of a few dozen operations. The compiler picks the most
specific one; the families below cover nearly all of them.

| Family | Examples | What it is |
|---|---|---|
| literals | `LiteralZero`, `LiteralInt8`, `LiteralInt16`, `LiteralInt32`, `LiteralFloat`, `LiteralString` | push a constant; small integers take fewer bytes |
| loads and stores | `LoadFromLocal`, `LoadFromGlobal`, `AssignToGlobalAndPop`, `LiteralInt8ToGlobal` | move values between the stack and variables, or straight from a literal into a variable |
| operand pairs | `LLValues`, `LGValues`, `GLValues`, `GGValues` | point the interpreter's two registers at two variables for the next instruction (the `SkipLoad` variants of the operators) |
| arithmetic and bit operations | `BinaryAddition`, `GGBinaryMultiplication`, `LLBinaryAddition`, `BinaryAdditionAndStoreGlobal` | on the stack, on two variables (`GG`, `GL`, `LG`, `LL`), or with the result stored into a variable |
| compound assignment | `AddAssign`, `AddAssignAndPop`, `IncGlobal`, `PreIncrement` | `+=` and friends, `++` and `--`; `AndPop` when the result is not used |
| comparisons | `CompareLT`, `GSCompareEQ`, `LLCompareGT` | push 1 or 0; `GS` and `LS` compare a global or local with the stack top |
| fused compare and branch | `CompareBLT8`, `LLCompareLTBZ`, `GGCompareLTBZ8`, `GSCompareEQBZ` | compare and jump in one instruction, the core of every `if` and loop condition |
| jumps | `RelativeJump`, `RelativeJump8`, `BZ`, `BZ8`, `LocalBZ8`, `BLA`, `BLO` | unconditional, branch if zero, branch on a variable, and branch on the logical and or the logical or of two values (`BLA`, `BLO`) |
| calls and returns | `CallFunctionByIndex`, `CallFunctionByHash`, `CallLibFunction`, `Return`, `ReturnZero`, `Yield`, `Stop`, `GlobalStop` | see [Calls](#calls) |
| containers | `Index`, `IndexLiteral8`, `StackIndexHash`, `GlobalIndexHash`, `IndexCreate`, `NewObjectTable`, `InitArray`, `CountOf`, `HashOf`, `Remove`, `HashEntryExists` | element and member access, `new`, array initializers, `._count`, `._hash`, `._remove`, `._exists` |
| iteration | `GPushIterator`, `LNextValueOrJump`, `GGNextKeyValueOrJump` | `for ( v : container )` and `for ( k, v : container )` |
| `switch` | `Switch`, `SwitchLinear` | a hashed table or a direct table of case offsets |
| conversions | `ToInt`, `ToFloat`, `Negate`, `LogicalNot`, `BitwiseNOT` | unary operations |
| other | `LoadLibConstant`, `DebugInfo`, `InitVar` | library constants, [line markers](debug-info.md), `InitVar` sets the value on the stack to 0 (the compiler emits none: `var x;` compiles as `var x = 0;`) |

The naming scheme: `G` and `L` name a global and a local operand, in the
order of the operands, `S` the stack top; `BZ` means the instruction also
branches when its result is zero; a trailing `8` or `16` means an 8 or 16
bit operand (a jump offset or a literal), a jump without it has a 16 bit
offset; `AndPop` means the result is dropped.

### The extended opcode prefix

The single byte opcode space is full: the jump table has one entry for each
of the 256 byte values. Opcode `Extended` is a prefix: the byte after it is
a second opcode from a separate table (`WRExtendedOpcode`), with its
operands after that. An extended opcode costs one more byte and one more
dispatch, so the prefix is for rare instructions. The first one is
`RelativeJump32`, a jump whose distance does not fit in 16 bits, which the
compiler emits only for very large functions or `switch` statements.

An unknown opcode, single byte or extended, stops the program with
`WR_ERR_unknown_opcode`.
