# Program structure

This page describes what a wrench source file is made of and how a
program runs: statements, comments, global code and functions, units, and
the list of reserved words. Read it first if you are new to the language.

## Source files and statements

A wrench program is a text file, by convention with the extension `.w`.
The compiler reads 8 bit characters; names, keywords and punctuation are
ASCII, and string literals may hold any byte (see [Strings](strings.md)).

A program is a sequence of statements. Like C, a statement ends with a
semicolon and a block `{ ... }` groups statements wherever one statement
is expected. Line breaks and indentation have no meaning.

```wrench
var total = 0;
for ( var i = 1; i <= 4; ++i )
{
	total += i;
}
println( "total ", total );
```

Output:

```text
total 10
```

An empty statement (a lone `;`) is allowed and does nothing.

Names (of variables, functions, structs, enum constants and goto labels)
start with a letter or `_` and go on with letters, digits and `_`. They are
case sensitive: `count`, `Count` and `COUNT` are three names.

## Comments

```wrench
// a line comment runs to the end of the line
var a = 1; /* a block comment
              may span lines */
var b = 2; /* block comments do not nest */
println( a + b );
```

Output:

```text
3
```

A block comment ends at the first `*/`, so `/* a /* b */` is one comment
and the text after it is code again.

## Global code and functions

Statements at the top level of the file, outside any function, are the
*global code*. Functions are declared with `function`, structs with
`struct`; both may appear anywhere at the top level, before or after the
code that uses them.

```wrench
println( "global code starts" );
greet( "world" );

function greet( name )
{
	println( "hello ", name );
}

println( "global code ends" );
```

Output:

```text
global code starts
hello world
global code ends
```

A function declaration is not a statement that runs: it defines the
function for the whole program, which is why `greet()` can be called on a
line above its definition. Functions and structs are declared at the top
level only. A function inside a function, or a struct inside a function,
is a compile error (`WR_ERR_statement_expected`).

## How a program runs

A wrench program has two phases:

1. The host loads the compiled program (`wr_run()`). Its global code runs
   once, from top to bottom. Global variables keep their values after the
   global code has finished.
2. The host then calls functions of the script whenever it wants to
   (`wr_callFunction()`): a callback on a button press, a `loop()` every
   few milliseconds, a handler for a message. Each call runs that function
   with the globals as the global code left them.

```wrench
// runs once, when the host loads the program
var presses = 0;
println( "ready" );

// the host calls this every time the button is pressed
function onPress()
{
	presses++;
	println( "pressed ", presses, " times" );
}
```

Output:

```text
ready
```

`wrench r` only runs the global code, so this program prints `ready` and
ends. In an application, the host keeps the program loaded and calls
`onPress()` as often as it likes; see
[Calling script functions](../embedding/calling.md).

A `return` in the global code ends the global code early:

```wrench
println( "checking" );
if ( 1 )
{
	return;
}
println( "never printed" );
```

Output:

```text
checking
```

The code after such a `return` never runs; functions the host calls
later run only their own code.

Global code can also be suspended with `yield()` and resumed by the host,
see [Yield and cooperative tasks](yield.md).

## Everything is a unit

Inside the compiler, functions and structs are the same thing, a *unit*:
a block of code with its own local variables.

* A function is a unit that is called and returns a value.
* A struct is a unit that is run once when an instance is made with
  `new`; its local variables become the members of the instance, and its
  return value is thrown away.
* The global code is unit 0, the unit that runs when the program loads.

This is why a struct can take arguments like a function and run any code
while it initializes its members, and why `new` works on a function too:

```wrench
function makeColor()
{
	var red = 255;
	var green = 128;
	var blue = 0;
}

var c = new makeColor;
println( c.red, " ", c.green, " ", c.blue );
```

Output:

```text
255 128 0
```

`unit` is a synonym of `function`. The differences between `struct` and
`function` are small and covered in [Structs](structs.md). Member
functions (methods) do not exist: a struct holds data, functions take the
struct as an argument.

## Keywords

These words are reserved and cannot be used as names:

| Keyword | Meaning |
|---|---|
| `var` | declares a variable, see [Variables and scope](variables.md) |
| `blank` | declares a variable without an initializer |
| `function`, `unit` | declares a function, see [Functions](functions.md) |
| `struct`, `new` | declares a struct and makes an instance, see [Structs](structs.md) |
| `export` | marks a function as usable with `new` from an importing program, see [Modules](import.md) |
| `enum` | declares constants, see [Enums and constants](enums.md) |
| `if`, `else`, `while`, `do`, `for`, `switch`, `case`, `default`, `break`, `continue`, `goto`, `return` | control flow, see [Control flow](control-flow.md) |
| `int`, `float` | only in the casts `(int)` and `(float)`, see [Values and types](types.md) |
| `true`, `false`, `null` | the constants 1, 0 and 0 |

These words have a meaning in one place and are ordinary names elsewhere:

| Word | Special where |
|---|---|
| `include` | `include "file.w"` at the top level, see [Several files](include.md) |
| `private` | at the start of a top level `var` or `function`, see [Several files](include.md) |
| `yield` | a call `yield(...)`; a function cannot be named `yield`, see [Yield](yield.md) |

Library functions and constants have qualified names such as
`math::sqrt` or `io::O_CREAT`; they are not keywords, they come from the
libraries the host loads (see the [standard library](../library/index.md)).

## Running a script

The command line tool compiles and runs a file in one step:

```bash
wrench r hello.w
```

It loads every standard library and provides `print()` and `println()`,
which print their arguments one after the other (`println()` adds a line
break). These two are not part of the language: an application provides
whatever functions it wants its scripts to call. The other commands
(compiling to bytecode, disassembling, debugging) are described in
[Command line tool](../reference/cli.md).
