# Variables and scope

This page explains how to declare variables, where a name is visible and
how global and local variables differ. Read it before writing functions:
the scope rules of wrench are close to C's but not the same. Older
scripts that do not follow them compile in
[non-strict mode](#non-strict-mode).

## Declaring a variable

`var` declares a variable, optionally with an initial value:

```wrench
var count = 10;
var ratio = 0.5;
var name = "pump";
var empty;
println( count, " ", ratio, " ", name, " ", empty );
```

Output:

```text
10 0.5 pump 0
```

A variable declared without a value is 0. One `var` declares one name:
`var a = 1, b = 2;` is a compile error, write two statements.

Arrays have their own declaration forms, `var a[10];` and
`var a[] = { 1, 2, 3 };`, described in
[Arrays and hash tables](containers.md).

A name can be declared again only once its first declaration is out of
view, see [Declaring a name again](#declaring-a-name-again).

## Declare before use

Every variable is declared with `var` before it is used. Using an
undeclared name is the compile error `WR_ERR_var_not_seen_before_label`,
which catches typing mistakes:

<!-- check:error -->
```wrench
var counter = 0;
countr = counter + 1; // typo: WR_ERR_var_not_seen_before_label
```

The one exception is the bare name of a script function used as a value,
which compiles to the hash of its name (see
[Functions](functions.md#functions-are-not-values)).

## Global and local variables

A variable declared at the top level is *global*: it exists for the whole
life of the program, keeps its value between calls into the script, and
the host can read and write it (`wr_getGlobalRef()`).

A variable declared inside a function is *local*: it exists while the
function runs, starts at 0 on every call, and is gone when the function
returns. Function arguments are locals too.

```wrench
var calls = 0; // global

function count()
{
	var local; // 0 on every call
	local++;
	calls++;
	return local;
}

count();
println( count(), " ", calls );
```

Output:

```text
1 2
```

### Inside a function

A name used inside a function is looked up in this order:

1. a local of the function (an argument or a `var` of the function);
2. a global declared **above** the function in the source.

`var` inside a function always makes a local, even when a global of the
same name exists; the local then hides the global for the rest of the
function. `::name` always means the global:

```wrench
var g = 20;
var n = 30;

function foo()
{
	var n = 2;  // a local n, hides the global n
	g = 31;     // no local g: the global
	::n = 40;   // the global n
	println( "local n ", n );
}

foo();
println( "global g ", g, ", global n ", n );
```

Output:

```text
local n 2
global g 31, global n 40
```

> [!IMPORTANT]
> A function only sees the globals declared before it in the source
> (including included files that come before it). A global declared
> below the function is the compile error
> `WR_ERR_var_not_seen_before_label` there, also with `::`. Declare
> globals at the top of the file, before the functions.

<!-- check:error -->
```wrench
function report()
{
	return total; // error: total is declared below
}
var total = 3;
```

### Block scope

A name declared inside a block is in view only in that block and the
blocks inside it, as in C. Using it after the block ended is
the compile error `WR_ERR_variable_out_of_scope`:

<!-- check:error -->
```wrench
if ( 1 )
{
	var x = 2;
	println( x );
}
println( x ); // error: x belonged to the block above
```

Every `{ }` is a block, and so are the body of a loop and the branches of
an `if` without braces. The header of a `for` belongs to the loop: the
`i` of `for ( var i = 0; ... )` is gone after the loop. The parameters of
a function and the global code are the outermost blocks. The cases of a
`switch` share the block of the switch, unless a case has braces of its
own. A variable the code after a block needs is declared before it:

```wrench
function firstNegative( list )
{
	var found = -1;
	for ( var i = 0; i < list._count; ++i )
	{
		if ( list[i] < 0 )
		{
			found = i;
			break;
		}
	}
	return found;
}
println( firstNegative( { 4, 7, -2, 5 } ) );
```

Output:

```text
2
```

A `var` in a block of the global code is a global that only the code of
that block sees; the host still finds it with `wr_getGlobalRef()`.
`var ::name` names the global explicitly and declares a global of the
whole program wherever it is written, in a block or in a function. It is
in view everywhere after it, and it may be written more than once for one
global:

```wrench
if ( 1 )
{
	var ::limit = 2;
}
println( limit );
```

Output:

```text
2
```

A `var` without a value is `var x = 0;`: it sets 0 every time it runs,
so a `var count;` in a loop body starts each pass from 0. Only `blank`
sets nothing.

### Declaring a name again

A block cannot hide a name that is in view: a second `var` for it, in the
same block or in a block inside it, is the compile error
`WR_ERR_variable_redeclared`. Parameters count as declared names.

<!-- check:error -->
```wrench
function total( list )
{
	var sum = 0;
	for ( var v : list )
	{
		var sum = v; // error: the sum above is in view
	}
	return sum;
}
```

Once the block of a name has ended, the name can be declared again, as in
two loops in a row:

```wrench
var r = 0;
for ( var i = 0; i < 3; ++i ) { r += i; }
for ( var i = 10; i < 12; ++i ) { r += i; }
println( r );
```

Output:

```text
24
```

Two included files that declare the same global are
`WR_ERR_variable_redeclared` too. Write `private var` in each file if
each one needs its own (see
[Several files: include and private](include.md)).

The scope exists only for the compiler. Names of blocks that ended share
storage: a function has one slot per name, however many blocks declare it,
and that slot counts once toward the limit of locals.

### Struct bodies

The body of a struct is a function whose locals become members, with one
difference: a bare name in a struct body never means a global. Write
`::name` to read a global there (see [Structs](structs.md)).

## Blank declarations

`blank` declares a variable without giving it a value in the script. The
three spellings are the same:

```wrench
blank speed;
blank var limit;
var blank mode;
println( speed, " ", limit, " ", mode );
```

Output:

```text
0 0 0
```

A host can fill such globals before the global code runs (with
`wr_newContext()`, `wr_getGlobalRef()` and then `wr_executeContext()`),
and the script reads them as configuration; see
[Passing data](../embedding/data.md). A `blank` variable cannot have an
initializer: `blank speed = 10;` and `blank speed += 1;` are the compile
error `WR_ERR_blank_variables_cannot_be_initialized`.

## Private globals

At the top level, `private var` makes a global that belongs to its source
file, like `static` in C. It matters for programs made of several files,
see [Several files: include and private](include.md).

## Names and limits

Variable names follow the rules of [Program structure](structure.md#source-files-and-statements).
A program has at most 255 globals (each `for ( v : container )` loop
also uses a hidden global) and a function at most 254 arguments and
locals together. Going over is `WR_ERR_too_many_locals`; see
[Limits](../reference/limits.md).

## Non-strict mode

Scripts written for older versions of wrench may not follow the rules
above. They compile in non-strict mode: the compiler flag
`WR_NON_STRICT_VAR` (see [Compiling scripts](../embedding/compiling.md)),
the option `-nostrict` of the command line tool, or the setting
`wrench.nonStrict` in VS Code. It changes three things:

* An assignment to an unknown name declares it: a global at the top
  level, a local inside a function. A bare unknown name used as a value
  is a variable too, not the hash of a function name.
* There is no block scope: a name is in view from its first declaration
  to the end of its function (or of the program, for a global).
* A second `var` for a name is never an error: it is an assignment to the
  variable that exists.

```bash
wrench r -nostrict old_script.w
```

Everything else is the same in both modes, `var x;` sets 0 there too.
New scripts should not use non-strict mode.
