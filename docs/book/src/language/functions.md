# Functions

This page covers declaring and calling functions: arguments, return
values, recursion, calling natives and library functions, and what
functions cannot do (they are not values). Scope rules for the variables
inside a function are on [Variables and scope](variables.md).

## Declaring and calling

```wrench
function area( w, h )
{
	return w * h;
}

println( area( 3, 4 ) );
```

Output:

```text
12
```

A function is declared at the top level with `function`, a name, a
parameter list and a block. A function without parameters may leave out
the parentheses: `function tick { ... }` is the same as
`function tick() { ... }`. `unit` is a synonym of `function`.

Two functions with the same name are a compile error
(`WR_ERR_function_redefined`), and so are two parameters with the same
name (`WR_ERR_duplicate_parameter`). There is no overloading.

A function can be called before its declaration in the source: the
compiler links calls by name once it has seen the whole program.

## Arguments

A function can be called with any number of arguments, whatever its
declaration says. Missing arguments are 0, extra ones are ignored:

```wrench
function f( a, b )
{
	return a + b;
}
println( f( 2, 3 ), " ", f( 2 ), " ", f(), " ", f( 2, 3, 4 ) );
```

Output:

```text
5 2 0 5
```

There is no syntax for default values; test for 0 instead:

```wrench
function blink( times )
{
	if ( !times )
	{
		times = 1;
	}
	return times;
}
println( blink(), " ", blink( 3 ) );
```

Output:

```text
1 3
```

There are no variable argument lists for script functions: a function
sees only its named parameters. Native functions (such as `println`)
receive every argument passed.

A call passes at most 255 arguments (`WR_ERR_too_many_arguments`), and
a function has at most 254 parameters and locals together.

### How arguments are passed

Arguments are values. A function gets the value of each argument, read
where the call is made, and changing a parameter (`++`, `+=`, assigning
another value) changes only the function's own copy:

```wrench
function next( n )
{
	n += 1;
	return n;
}
var count = 1;
println( next( count ), " ", count );
```

Output:

```text
2 1
```

Strings, arrays, hash tables and struct instances are shared (see
[Values and types](types.md#the-types)): the value passed is the container,
not a copy of it, so a function that changes an element of an array it
was given changes the caller's array.

```wrench
function fill( list )
{
	list[0] = 42;
}
var data[] = { 1, 2 };
fill( data );
println( data[0] );
```

Output:

```text
42
```

Every argument is read before the call runs, in order, so
`println( i, " ", i++ )` prints the old value of `i` twice, and a function
that changes a global sees the new value only through the global, not
through a parameter it was given.

## Return values

`return value;` returns any value: a number, a string, a container, a
struct instance. `return;`, and reaching the end of the function, return
0.

```wrench
function minMax( list )
{
	var lo = list[0];
	var hi = list[0];
	for ( var v : list )
	{
		if ( v < lo )
		{
			lo = v;
		}
		if ( v > hi )
		{
			hi = v;
		}
	}
	var result = { "min": lo, "max": hi };
	return result;
}
var values[] = { 4, 9, 1, 7 };
var r = minMax( values );
println( r.min, " ", r.max );
```

Output:

```text
1 9
```

To return several values, return an array, a hash table or a struct
instance, as above.

## Recursion

```wrench
function fact( n )
{
	if ( n <= 1 )
	{
		return 1;
	}
	return n * fact( n - 1 );
}
println( fact( 10 ) );
```

Output:

```text
3628800
```

Each call uses room on the VM's value stack for its arguments, locals and
temporaries. The stack is small by design (64 entries by default, the
command line tool uses 128), so deep recursion ends with the runtime
error `WR_ERR_stack_overflow`:

```wrench
function forever( n )
{
	return forever( n + 1 );
}
forever( 0 );
println( "not reached" );
```

Output:

```text
err: WR_ERR_stack_overflow
```

The host chooses the stack size with `wr_newState()`; see
[Limits](../reference/limits.md).

## Calling natives and library functions

The host makes C functions callable from scripts. They are called like
script functions:

* *natives*, registered with `wr_registerFunction()`, have plain names
  (`println`, `led`);
* *library functions*, registered with `wr_registerLibraryFunction()`,
  have a qualified name `library::function` (`math::sqrt`,
  `str::format`). The standard libraries are of this kind, see the
  [Standard library](../library/index.md).

```wrench
println( math::sqrt( 16 ), " ", str::format( "%03d", 7 ) );
```

Output:

```text
4 007
```

Calls are resolved when they run: a script calls a function by the hash
of its name, so a script compiles even if the host does not provide a
function it calls. If nothing of that name exists when the call runs, the
script stops with `WR_ERR_function_not_found` (a native or script
function) or `WR_ERR_lib_function_not_found` (a library function):

```wrench
println( "before" );
led( 1 );
println( "after" );
```

Output:

```text
before
err: WR_ERR_function_not_found
```

[`sys::isFunction()`](../library/sys.md#sysisfunction) tells whether a
name can be called, so a script can adapt to the host it runs in. Writing
natives is described in [Native functions and libraries](../embedding/natives.md).

A script function and a native of the same name: the script function
wins.

## Functions are not values

A function cannot be stored in a variable and called through it. The name
of a script function used as a value compiles to the 32 bit hash of the
name, an int:

```wrench
function onTimer()
{
	return 1;
}
var handler = onTimer;
println( handler == onTimer );
handler(); // calls a function named "handler", not onTimer
```

Output:

```text
1
err: WR_ERR_function_not_found
```

A host can take such a hash and call the function with
`wr_callFunction( context, hash, ... )`, which is how scripts hand
callbacks to natives (see [Calling script functions](../embedding/calling.md)).
A bare name used as a value must be a script function of the program;
any other unknown name is `WR_ERR_var_not_seen_before_label`. In
[non-strict mode](variables.md#non-strict-mode) a bare name that is not a
variable becomes a new variable (0), even the name of a function.

## No nested functions

Functions are declared at the top level only (a function declared inside
a top level block such as an `if` is still a top level function). A
function inside a function is `WR_ERR_statement_expected`. There are no
closures and no member functions.
