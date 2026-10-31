# Control flow

This page covers the statements that decide what runs next: `if`,
`while`, `do`, `for` and its two `foreach` forms, `switch`, `break`,
`continue`, `goto` and `return`. They follow C closely; the differences
are the `foreach` loops and how `switch` matches its cases.

Every body below may be a single statement or a block. The examples
always use blocks, which is the recommended style.

## if and else

```wrench
function classify( x )
{
	if ( x < 0 )
	{
		return "negative";
	}
	else if ( x == 0 )
	{
		return "zero";
	}
	else
	{
		return "positive";
	}
}
println( classify( -3 ), " ", classify( 0 ), " ", classify( 8 ) );
```

Output:

```text
negative zero positive
```

The condition is true when it is not 0 (see
[Truth](types.md#truth)). An `else` belongs to the nearest `if` without
one. The condition must not be empty: `if ()` is the compile error
`WR_ERR_empty_parens`, and so are `while ()`, `do ... while ()` and
`switch ()`. Write `while ( 1 )` for an endless loop.

## while

```wrench
var n = 1;
while ( n < 100 )
{
	n *= 3;
}
println( n );
```

Output:

```text
243
```

## do while

The body runs once before the condition is first tested. The statement
ends with a semicolon.

```wrench
var tries = 0;
do
{
	tries++;
} while ( tries < 3 );
println( tries );
```

Output:

```text
3
```

## for

`for ( setup; condition; step )` works as in C. Each part may be empty;
`for ( ;; )` loops until a `break`. Setup and step may hold several
expressions separated by commas.

```wrench
for ( var i = 0, var j = 5; i < j; ++i, --j )
{
	print( i, j, " " );
}
println();
```

Output:

```text
05 14 23
```

A `var` in the setup belongs to the loop: after the loop the name is out
of view, see [Block scope](variables.md#block-scope). Declare the variable
before the loop to use it afterwards.

## for each: arrays, hash tables, strings

`for ( value : container )` runs the body once per element, with the
element in `value`. `for ( key, value : container )` also gives the key:
the index for arrays and strings, the key for hash tables.

```wrench
var names[] = { "zero", "one", "two" };
for ( var v : names )
{
	print( v, " " );
}
println();

for ( var k, var v : names )
{
	print( k, "=", v, " " );
}
println();

var ages = { "ann": 31, "bob": 27 };
for ( var name, var age : ages )
{
	println( name, " is ", age );
}

var word = "hi";
for ( var c : word )
{
	print( c, " " );
}
println();
```

Output:

```text
zero one two 
0=zero 1=one 2=two 
ann is 31
bob is 27
104 105 
```

The rules:

* The loop variables are plain names. With `var` they are declared on
  the spot, each with its own `var` (`for ( var k, var v : h )`); without
  `var` they must exist already (`for ( k, v : h )`).
  `for ( var k, v : h )` is an error, since `v` is not declared.
* The container must be a variable: `for ( v : list )` works,
  `for ( v : s.list )` or `for ( v : makeList() )` does not (copy it into
  a variable first).
* `value` is a copy: assigning to it does not change the container.
* A hash table is visited in its internal order, not in the order the keys
  were added. The order shown above happens to be the insertion order.
* The loop sees elements added while it runs (an array that grows is
  walked to its new end).
* A string gives its characters as ints.
* Each foreach loop in a program uses one hidden global for its position.

`break` and `continue` work in foreach loops as in any loop.

## switch

```wrench
function describe( x )
{
	switch ( x )
	{
		case 0:
			return "zero";

		case 1:
		case 2:
			return "small";

		case "red":
			return "a color";

		case -1:
		case 2.5:
			return "special";

		default:
			return "something else";
	}
}
println( describe( 0 ), ", ", describe( 2 ), ", ", describe( "red" ), ", ", describe( 2.5 ), ", ", describe( 99 ) );
```

Output:

```text
zero, small, a color, special, something else
```

`switch` evaluates its expression once and jumps straight to the matching
`case`; if none matches it jumps to `default`, and without a `default` it
goes on after the switch. As in C, execution falls through from one case
into the next until a `break` (or a `return`, `continue` or `goto`) leaves
the switch:

```wrench
for ( var i = 0; i < 4; ++i )
{
	switch ( i )
	{
		case 1:
			continue; // the next pass of the for loop
		case 2:
			print( "two " ); // falls through
		case 3:
			print( "two-or-three " );
			break;
		default:
			print( "other " );
	}
	println( i );
}
```

Output:

```text
other 0
two two-or-three 2
two-or-three 3
```

`switch` is not a chain of `if`s:

* A `case` value is a constant written in place: an int, a float, a
  string or character literal, an enum constant, optionally negative and
  in parentheses. It cannot be a variable or an expression
  (`WR_ERR_switch_bad_case_hash`), and two equal cases are
  `WR_ERR_switch_duplicate_case`.
* The compiler builds a jump table, so the switch costs the same however
  many cases it has. Small non-negative ints (0 to 252) index the table
  directly; other values are found by their hash.
* Matching is by hash: an int matches an int case, a string matches a
  string case with the same text, and a float a float case of the same
  value (no tolerance as with `==`). The int 2 does not match `case 2.0:`.
* Statements before the first `case` or `default` are an error
  (`WR_ERR_switch_case_or_default_expected`).
* `break` leaves the switch; `continue` belongs to the enclosing loop.
* A block after a `case` is allowed: `case 2: { ... break; }`.

## break and continue

`break` leaves the innermost `while`, `do`, `for` or `switch`. `continue`
goes on with the next pass of the innermost loop (in a `for`, it runs the
step first). Outside a loop they are compile errors
(`WR_ERR_break_keyword_not_in_looping_structure`,
`WR_ERR_continue_keyword_not_in_looping_structure`). To leave several
loops at once, use `goto` or `return`.

## goto and labels

A label is a name followed by `:` at the start of a statement; `goto name;`
jumps to it.

```wrench
function find( grid, want )
{
	var i; // declared outside the loops, to be used after them
	var j;
	for ( i = 0; i < grid._count; ++i )
	{
		for ( j = 0; j < grid[i]._count; ++j )
		{
			if ( grid[i][j] == want )
			{
				goto found;
			}
		}
	}
	return "missing";
found:
	return "found at " + i + "," + j;
}
var grid[] = { { 1, 2 }, { 3, 4 } };
println( find( grid, 4 ) );
println( find( grid, 9 ) );
```

Output:

```text
found at 1,1
missing
```

Labels belong to their function (or to the global code); a `goto` cannot
leave the function. A label may appear once per function
(`WR_ERR_bad_goto_label`), and a `goto` to a label that does not exist
fails when the program is linked (`WR_ERR_goto_target_not_found`).

## return

`return value;` ends a function and gives `value` to the caller;
`return;` and falling off the end of a function return 0. In the global
code, `return` ends the global code (see
[How a program runs](structure.md#how-a-program-runs)).
