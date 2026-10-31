# A tour of the language

This page shows wrench in five minutes, for readers who know C or
JavaScript: each feature with a small program and what it prints. The
[Language guide](../language/structure.md) has the details; the links in
each section go to them.

## Running a script

Put this in a file `hello.w`:

```wrench
println( "hello, wrench" );
```

and run it with the command line tool (see
[Getting the tools](tools.md)):

```bash
wrench r hello.w
```

Output:

```text
hello, wrench
```

`wrench r` compiles the file and runs it. `print()` and `println()` are
provided by the tool, the way an application provides the functions its
scripts may call. A compile error is reported with the file, the line and
an error code instead.

## Variables and values

```wrench
var count = 3;        // an int, 32 bits
var ratio = 0.5;      // a float, 32 bits
var name = "pump";    // a string
var nothing;          // 0
count = count * 2;
println( name, " ", count, " ", ratio * count, " ", nothing );
println( 7 / 2, " ", 7 / 2.0 );
```

Output:

```text
pump 6 3 0
3 3.5
```

Every variable is declared with `var`; a misspelled name is a compile
error. Variables take any type of value. Integer arithmetic stays
integer, as in C. More in [Values and types](../language/types.md) and
[Variables and scope](../language/variables.md).

## Functions

```wrench
function area( w, h )
{
	return w * h;
}

println( area( 3, 4 ), " ", area( 3 ) );
```

Output:

```text
12 0
```

Functions are declared at the top level and can be called before their
declaration. A missing argument is 0, an extra one is ignored. See
[Functions](../language/functions.md).

## Control flow

```wrench
for ( var i = 1; i <= 15; ++i )
{
	if ( i % 15 == 0 )
	{
		print( "FizzBuzz " );
	}
	else if ( i % 5 == 0 )
	{
		print( "Buzz " );
	}
	else if ( i % 3 == 0 )
	{
		print( "Fizz " );
	}
	else
	{
		print( i, " " );
	}
}
println();
```

Output:

```text
1 2 Fizz 4 Buzz Fizz 7 8 Fizz Buzz 11 Fizz 13 14 FizzBuzz 
```

`if`, `while`, `do`, `for`, `switch`, `break`, `continue` and `goto` work
as in C; `&&` and `||` short circuit and give 1 or 0. See
[Control flow](../language/control-flow.md) and
[Operators](../language/operators.md).

## Arrays

```wrench
var primes[] = { 2, 3, 5, 7 };
primes[primes._count] = 11; // append
var sum = 0;
for ( var p : primes )
{
	sum += p;
}
println( primes._count, " primes, sum ", sum );
```

Output:

```text
5 primes, sum 28
```

Arrays grow when you write past their end. `._count` is the length.
`for ( var v : array )` visits each element. See
[Arrays and hash tables](../language/containers.md).

## Hash tables

```wrench
var ages = { "ann": 31, "bob": 27 };
ages["cy"] = 40;
println( ages["bob"], " ", ages["cy"], " ", ages._count );
for ( var name, var age : ages )
{
	println( name, " is ", age );
}
```

Output:

```text
27 40 3
ann is 31
cy is 40
bob is 27
```

A hash table has no order of its own: the loop visits the entries in an
internal order. Keys can be strings, ints or other values; `ages.cy` is a short form of
`ages["cy"]` (with a catch when iterating, see the link below). Arrays and
hash tables are shared, not copied, when assigned or passed to a function.
See [Hash tables](../language/containers.md#hash-tables).

## Structs

```wrench
struct Point( px, py )
{
	var x = px;
	var y = py;
}

var a = new Point( 1, 2 );
var b = new Point { x = 10, y = 20 };
println( a.x + b.x, " ", a.y + b.y );
```

Output:

```text
11 22
```

A struct's body runs as its constructor; its variables are the members.
There are no methods: write functions that take the instance. See
[Structs](../language/structs.md).

## Strings

```wrench
var s = "temperature";
var msg = s + " = " + 21.5 + " C";
println( msg );
println( msg._count, " characters, the first is ", s[0] );
println( str::toupper( s ), " ", str::mid( s, 0, 4 ) );
```

Output:

```text
temperature = 21.5 C
20 characters, the first is 116
TEMPERATURE temp
```

`+` joins strings and converts numbers. A character (`s[0]`, `'t'`) is an
int. The [str library](../library/str.md) cuts, searches and formats. See
[Strings](../language/strings.md).

## Library calls

The libraries a host loads are called with a qualified name,
`library::function`:

```wrench
println( math::sqrt( 2 ), " ", math::pow( 2, 10 ) );
println( str::format( "%5d|%-5s|%x", 42, "ab", 255 ) );
var r = std::rand( 1, 6 ); // a dice roll, 1 to 6
println( r >= 1 && r <= 6 );
```

Output:

```text
1.41421 1024
   42|ab   |ff
1
```

The command line tool loads all standard libraries; an application loads
the ones it wants. See the [Standard library](../library/index.md).

## Several files

`include` puts another file into the program at compile time:

<!-- check:file=util.w -->
```wrench
// util.w
private var calls = 0; // only util.w sees it

function twice( x )
{
	calls++;
	return x * 2;
}
```

<!-- check:as=app.w -->
```wrench
// app.w
include "util.w";
println( twice( 21 ) );
```

Output:

```text
42
```

`private` keeps a global or a function inside its file. See
[Several files: include and private](../language/include.md).

## How a script lives in an application

The global code runs once when the application loads the program; after
that the application calls the script's functions when it needs them:

```wrench
var presses = 0; // runs once, at load

function onButton() // called by the application
{
	presses++;
	return presses;
}
```

An application can also suspend a script with `yield()`
([Yield](../language/yield.md)), load more code at run time
([Modules](../language/import.md)) and debug it on the device
([Debugging](../debugging/overview.md)). Where to go next:

* [Program structure](../language/structure.md) to read the language
  guide in order;
* [How wrench is put together](../embedding/architecture.md) to put
  wrench into your own program.
