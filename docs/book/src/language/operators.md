# Operators

This page lists every operator of wrench with its precedence, and the
rules that differ from C: what may be assigned to, short circuit
evaluation, and string concatenation. The arithmetic rules for ints and
floats are on [Values and types](types.md#arithmetic-int-and-float).

## Precedence

Operators bind as in C. Higher rows bind tighter; operators in one row
have the same precedence.

| Level | Operators | Grouping |
|---|---|---|
| 1 | `a++` `a--` `a[i]` `a.m` `f(...)` `._count` `._exists()` `._remove()` `._hash` | left to right |
| 2 | `++a` `--a` `-a` `!a` `~a` `(int)a` `(float)a` | right to left |
| 3 | `*` `/` `%` | left to right |
| 4 | `+` `-` | left to right |
| 5 | `<<` `>>` | left to right |
| 6 | `<` `<=` `>` `>=` | left to right |
| 7 | `==` `!=` | left to right |
| 8 | `&` | left to right |
| 9 | `^` | left to right |
| 10 | `\|` | left to right |
| 11 | `&&` | left to right |
| 12 | `\|\|` | left to right |
| 13 | `? :` | right to left |
| 14 | `=` `+=` `-=` `*=` `/=` `%=` `&=` `\|=` `^=` `<<=` `>>=` | right to left |

```wrench
println( 2 + 3 * 4, " ", 1 << 2 + 1, " ", 6 & 3 == 3, " ", 1 | 2 ^ 3 & 4 );
println( 10 - 4 - 3, " ", 1 + 5 % 3, " ", -7 % 3 );
```

Output:

```text
14 8 0 3
3 3 -1
```

As in C, `&`, `^` and `|` bind more loosely than `==`: `6 & 3 == 3` is
`6 & (3 == 3)`, which is 0. Use parentheses when you mix them.

There is no unary `+`, no comma operator (the comma separates arguments,
initializer elements and the parts of a `for`), and no `**`; use
`math::pow()` for powers.

## Arithmetic operators

| Operator | Meaning |
|---|---|
| `a + b` | addition; with a string on either side, concatenation (see below) |
| `a - b` | subtraction |
| `a * b` | multiplication |
| `a / b` | division; integer division when both are ints |
| `a % b` | remainder of integer division |
| `-a` | negation |

Integer results wrap around at 32 bits. Dividing by zero gives 0 (see
[Division by zero](types.md#division-by-zero)).

## Bit operators

| Operator | Meaning |
|---|---|
| `a & b` | and |
| `a \| b` | or |
| `a ^ b` | exclusive or |
| `~a` | complement |
| `a << n` | shift left |
| `a >> n` | arithmetic shift right (the sign bit is copied) |

They work on 32 bit ints. A float operand is truncated to an int first
(`~2.5` is `~2`, -3). The shift count is
taken modulo 32, so `1 << 32` is 1 and `1 << 33` is 2.

```wrench
println( 5 & 3, " ", 5 | 3, " ", 5 ^ 3, " ", ~5, " ", 1 << 4, " ", -16 >> 2, " ", 1 << 31 );
```

Output:

```text
1 7 6 -6 16 -4 -2147483648
```

## Comparisons

`==`, `!=`, `<`, `<=`, `>` and `>=` give the int 1 when true and 0 when
false. Ints and floats compare as numbers, strings and containers compare
by content with `==` and `!=`, and strings order by their bytes with `<`,
`<=`, `>` and `>=`. A number is never equal to a string or a container.
The full table, including the float tolerance, is on
[Values and types](types.md#comparisons).

Comparisons do not chain: `1 < 2 < 3` is `(1 < 2) < 3`, that is `1 < 3`.

## Logical operators

| Operator | Result |
|---|---|
| `!a` | 1 when `a` is false, else 0 |
| `a && b` | 1 when both are true, else 0 |
| `a \|\| b` | 1 when either is true, else 0 |

The result is always the int 1 or 0, never one of the operands (unlike
JavaScript): `0 || 7` is 1.

`&&` and `||` *short circuit*: the right side is evaluated only when the
left side does not decide the result. A guard protects what follows it:

```wrench
var a[] = { 5, 6, 7 };
var i = 10;
if ( i < a._count && a[i] == 5 )
{
	println( "found" );
}
println( "a still has ", a._count, " elements" );

function ready()
{
	println( "ready() called" );
	return 1;
}
function retry()
{
	println( "retry() called" );
	return 1;
}
var ok = ready() || retry(); // retry() does not run
println( ok );
```

Output:

```text
a still has 3 elements
ready() called
1
```

The guard keeps `a[10]` from being read at all (it would give 0, see
[Reading past the end](containers.md#reading-past-the-end)).

## Conditional operator

`condition ? whenTrue : whenFalse` evaluates the condition, then only the
branch it selects:

```wrench
var level = 7;
var label = level > 5 ? "high" : "low";
println( label );
println( 0 ? 1 : 2 ? 3 : 4 ); // groups as 0 ? 1 : (2 ? 3 : 4)
var r = level ? 10 : 20 + 1; // the + belongs to the false branch
println( r );
```

Output:

```text
high
3
10
```

It binds more loosely than `||` and more tightly than assignment, and it
groups from the right. Put a nested conditional in parentheses for
readability. A conditional may be the target of a member assignment:
`(useA ? a : b).x = 1;` writes into `a` or `b`.

## Increment and decrement

`++` and `--` add or subtract 1, before (`++a`, the new value is the
result) or after (`a++`, the old value is the result) the use, as in C.
They work on variables, array elements, members and characters of a
string, ints and floats:

```wrench
var i = 5;
var a = i++;
var b = ++i;
println( a, " ", b, " ", i );
var f = 1.5;
f++;
var h = { };
h.hits++;
println( f, " ", h.hits );
```

Output:

```text
5 7 7
2.5 1
```

## Assignment

`=` stores a value; the assignment itself is an expression whose value is
the stored value. Assignments group from the right, so `a = b = c` stores
`c` in both, and so do the compound assignments: `a += b *= 2` first
doubles `b`, then adds it to `a`.

```wrench
var a = 1;
var b = 2;
var c = 3;
a = b = c;
println( a, b, c );
a += b *= 2;
println( a, " ", b );
var y = (a += 1) * 2;
println( a, " ", y );
```

Output:

```text
333
9 6
10 20
```

The compound assignments are `+=`, `-=`, `*=`, `/=`, `%=`, `&=`, `|=`,
`^=`, `<<=` and `>>=`. `a op= b` is `a = a op b` with `a` evaluated once.

### What can be assigned to

Only a variable, an element (`a[i]`), a member (`s.m`) or a character of a
string can be assigned to, incremented or decremented. Parentheses around
one and the conditional operator keep it assignable. Anything else is the
compile error `WR_ERR_not_assignable`, because the change would be lost:

<!-- check:error -->
```wrench
function getPoint()
{
	var p = { "x": 1 };
	return p;
}
getPoint().x = 1; // WR_ERR_not_assignable: assign the result to a variable first
```

| Target | Allowed |
|---|---|
| `x = 1`, `a[2] = 1`, `s.m = 1`, `(x) = 1`, `(c ? a : b).m = 1` | yes |
| `3 = 4`, `5++` | no, a literal |
| `io::O_CREAT = 1`, an enum constant | no, a constant |
| `f() = 1`, `f().x = 1`, `f()[0] = 1` | no, a call result |
| `a + b = 7` | no, the result of an operation |

A member read from a literal, such as `"abc".length`, is the compile
error `WR_ERR_member_of_literal`.

Assigning to an element or member of a variable that holds no container
yet turns it into one (`var h; h.k = 5;` makes `h` a hash table); reading
never does. See [Arrays and hash tables](containers.md).

## String concatenation

`+` with a string on either side converts the other operand to text and
joins them. Evaluation goes from left to right, so numbers added before
the first string are added as numbers:

```wrench
println( "n=" + 5 + ", f=" + 2.5 );
println( 5 + 5 + "x", " ", "x" + 5 + 5 );
var s = "count: ";
s += 3;
println( s );
```

Output:

```text
n=5, f=2.5
10x x55
count: 3
```

A character literal is an int, so `"ab" + 'c'` is `"ab99"`; see
[Strings](strings.md#concatenation). `+` on two arrays or hash tables
gives 0.

## Container intrinsics

`._count`, `._exists( key )`, `._remove( key )` and `._hash` look like
members but are operators of the compiler; they are described in
[Arrays and hash tables](containers.md#intrinsics).

## Casts

`(int)value` and `(float)value` convert a number; see
[Conversions](types.md#conversions).
