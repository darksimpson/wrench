# Values and types

This page lists the kinds of values a wrench variable can hold, how to
write them as literals, and the rules for mixing them: integer and float
arithmetic, conversions, comparisons and what counts as true. It is the
reference for "what does this expression give".

## Dynamic typing

Variables have no declared type. A variable holds one value at a time,
and the value carries its type; assigning a value of another type
replaces it:

```wrench
var x = 5;
println( x );
x = 2.5;
println( x );
x = "now a string";
println( x );
```

Output:

```text
5
2.5
now a string
```

## The types

| Type | Holds | Copied on assignment |
|---|---|---|
| int | a 32 bit signed integer, -2147483648 to 2147483647 | yes, the value |
| float | a 32 bit IEEE float (C `float`) | yes, the value |
| string | a sequence of 8 bit characters | no, shared ([Strings](strings.md)) |
| array | numbered elements of any type | no, shared ([Arrays and hash tables](containers.md)) |
| hash table | keys of any type mapped to values | no, shared ([Arrays and hash tables](containers.md)) |
| struct instance | named members, made with `new` | no, shared ([Structs](structs.md)) |

There is no boolean, character or null type: comparisons give the ints 1
and 0, a character is an int, and "no value" is the int 0.

Functions are not values. The name of a function used as a value gives a
32 bit hash of the name (an int), which a native function can use to call
it back through the host API; a script cannot call a function through a
variable. See [Functions](functions.md#functions-are-not-values).

## Integer literals

| Form | Example | Value |
|---|---|---|
| decimal | `255` | 255 |
| hexadecimal, `0x` | `0xFF` | 255 |
| binary, `0b` | `0b1010` | 10 |
| octal, a leading `0` | `017` | 15 |
| character | `'A'` | 65, see [Strings](strings.md#character-literals) |

The `x` and `b` of the prefixes are lower case; hex digits may be either
case. There is no digit separator. A literal must fit in 32 bits: values
from 2147483648 to 4294967295 wrap to negative numbers (`0xFFFFFFFF` is
-1, `0x80000000` is -2147483648), and a literal with more than 32 bits,
in any base, is the compile error `WR_ERR_integer_literal_too_large`.

A leading `0` makes a number octal only if all its digits are octal and
no `.` or exponent follows: `010` is 8, but `09.5` is the float 9.5.

## Float literals

A number with a `.`, an exponent or an `f` suffix is a float:

```wrench
println( 1.5, " ", .25, " ", 2.5e-3, " ", 1e3, " ", 3E2, " ", 10f, " ", 1.f );
```

Output:

```text
1.5 0.25 0.0025 1000 300 10 1
```

A float that holds a whole number prints without a decimal point (`1e3`
prints as `1000`), but it stays a float.

`true` is the int 1, `false` and `null` are the int 0.

## Arithmetic: int and float

The rule is the one of C:

* Both operands int: the operation is done in 32 bit integers and the
  result is an int. Overflow wraps around, it is never an error.
* Either operand float: the int is converted to float and the result is
  a float.

```wrench
println( 7 / 2, " ", 7.0 / 2, " ", 7 / 2.0 );
println( 3 * 1.5, " ", 2147483647 + 1 );
println( -7 / 2, " ", -7 % 2 );
```

Output:

```text
3 3.5 3.5
4.5 -2147483648
-3 -1
```

Integer division truncates toward zero, and `%` takes the sign of the
left operand, as in C.

The integer operators `%`, `&`, `|`, `^`, `~`, `<<` and `>>` always work
on ints: a float operand is truncated to an int first, and the result is
an int.

```wrench
println( 7.5 % 2, " ", 6.9 & 3, " ", 1.9 << 2 );
```

Output:

```text
1 2 4
```

### Division by zero

Dividing by zero is not an error by default: the result is 0 for ints and
for floats, and `x % 0` is 0. `-2147483648 / -1` is -2147483648 and
`-2147483648 % -1` is 0. A host built with `WRENCH_TRAP_DIVISION_BY_ZERO`
stops the script with `WR_ERR_division_by_zero` instead, for `/`, `%`,
`/=` and `%=` (see
[Build flags](../reference/build-flags.md)).

```wrench
var zero = 0;
println( 10 / zero, " ", 10.0 / zero, " ", 10 % zero );
```

Output:

```text
0 0 0
```

### Assignment changes the type

A compound assignment stores the result, whatever its type, so an int
variable becomes a float when a float takes part:

```wrench
var n = 7;
n /= 2;
println( n );
n += 0.5;
println( n );
```

Output:

```text
3
3.5
```

## Conversions

The casts `(int)` and `(float)` convert a number:

```wrench
var divisor = 1000;
println( 10 / divisor );
println( 10 / (float)divisor );
println( (int)3.9, " ", (int)-3.9, " ", (float)7 / 2 );
```

Output:

```text
0
0.01
3 -3 3.5
```

`(int)` truncates toward zero. A cast converts the value it is applied
to, it does not change the variable: after `10 / (float)divisor`,
`divisor` is still an int. Write `divisor = (float)divisor;` to change
the variable.

A cast does not parse text: `(int)"42"` is not 42 (it gives a number
derived from the string's internal hash). Use
[`str::tol()`](../library/str.md#strtol) to read a number from a string.
The other way round, `+` with a string turns a number into text (see
[Strings](strings.md#concatenation)).

## Truth

Conditions (`if`, `while`, `for`, `!`, `&&`, `||`, `?:`) treat a value as
false when it is the int 0 or the float 0.0, and as true otherwise. Every
string, array, hash table and struct instance is true, even an empty one.

```wrench
function truth( v )
{
	if ( v )
	{
		return "T";
	}
	return "F";
}
var e = { };
println( truth( 0 ), truth( 1 ), truth( -1 ), truth( 0.0 ), truth( 0.5 ) );
println( truth( "" ), truth( "0" ), truth( e ) );
```

Output:

```text
FTTFT
TTT
```

## No value: 0

A variable that was declared without a value, a missing argument, a
function that ends without `return`, a struct member nobody set and a
member read from something that is no container all give the int 0.
`null` is only another way to write 0.

```wrench
var nothing;
function f( a, b )
{
	return b;
}
println( nothing, " ", f( 1 ), " ", nothing == null );
```

Output:

```text
0 0 1
```

## Comparisons

`==`, `!=`, `<`, `<=`, `>` and `>=` give the int 1 or 0.

| Operands | `==` and `!=` | `<`, `<=`, `>`, `>=` |
|---|---|---|
| int and int | exact | numeric |
| int and float | as floats, with the float tolerance below | numeric, as floats |
| float and float | equal when they differ by at most 0.00005% of the left operand | numeric |
| string and string | same characters | by bytes, as C `strcmp()` |
| two arrays, two hash tables | same contents | not meaningful |
| two struct instances | same struct and equal members | not meaningful |
| a number and a string or container | never equal: `"3" == 3` is 0 | not meaningful |

Float equality has a tolerance because floats round: `0.1 + 0.2 == 0.3`
is 1 in wrench.

```wrench
println( 3 == 3.0, " ", 0.1 + 0.2 == 0.3, " ", "abc" == "abc", " ", "3" == 3 );
var a[] = { 1, 2 };
var b[] = { 1, 2 };
println( a == b );
```

Output:

```text
1 1 1 0
1
```

Strings order by their bytes, as unsigned values: a shorter string that
starts the longer one comes first, and upper case letters come before
lower case ones.

```wrench
println( "aa" < "b", " ", "ab" < "abc", " ", "B" < "a", " ", "abc" <= "abc" );
```

Output:

```text
1 1 1 1
```

`<` and `>` on containers, or between a number and a string, compile but
compare internal hashes; their result means nothing.

## Printing values

`print()` and `println()` of the command line tool (and
`str::format("%s")`) turn a value into text the same way:

* ints in decimal;
* floats with up to six significant digits and at most five decimals,
  trailing zeros removed (`1.0 / 3` prints `0.33333`, `1e10` prints
  `1e+10`, `0.0000001` prints `0`);
* strings as their characters, up to the first zero byte;
* arrays, hash tables and struct instances as a number. Print their
  elements yourself, or use [`str::tprint()`](../library/str.md#strtprint)
  to see what a value holds.

## Telling types apart

The language has no `typeof`. A script usually knows what it stores; when
it must check, `str::tprint()` shows a value with its type for debugging.
Host code checks types with `WRValue::isString()`, `isWrenchArray()`,
`isHashTable()`, `isStruct()` and the others, see
[Values: WRValue](../embedding/values.md).
