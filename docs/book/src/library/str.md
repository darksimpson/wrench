# str

The `str::` library works on strings: length, searching, cutting and
joining, trimming, case, number parsing and C style formatting with
`str::format`, `str::sprintf` and `str::printf`. Read it together with
[Strings](../language/strings.md), which explains what a string is in
wrench (an array of characters), literals, escapes, indexing and `+`.

The host loads it with `wr_loadStringLib( w )` (or `wr_loadAllLibs( w )`);
in `wrench.json` its name is `string`. It runs on every platform. Two
build flags change it:

* `WRENCH_FLOAT_SPRINTF` makes `%f`, `%g` and `%e` use the C library's
  `snprintf` for the digits of a float; see [Floats](#floats).
* The file system flags decide where `str::printf` writes; see
  [str::printf](#strprintf).

```wrench
var line = "  temp=21.5  ";
var t = str::trim( line );
var eq = str::chr( t, '=' );
var key = str::left( t, eq );
var value = str::mid( t, eq + 1 );
println( "[", key, "] [", value, "]" );
println( str::format( "%s is %s degrees", str::toupper( key ), value ) );
```

Output:

```text
[temp] [21.5]
TEMP is 21.5 degrees
```

## Functions

| Function | Arguments | Returns | Summary |
|---|---|---|---|
| `str::strlen` | `s` | int | length of a string, 0 for anything else |
| `str::chr` | `s, ch` | int | index of the first `ch` in `s`, or -1 |
| `str::isspace` | `ch` | int | non-zero if `ch` is white space |
| `str::isdigit` | `ch` | int | non-zero if `ch` is `0` to `9` |
| `str::isalpha` | `ch` | int | non-zero if `ch` is a letter |
| `str::isalnum` | `ch` | int | non-zero if `ch` is a letter or a digit |
| `str::tolower` | `s` or `ch` | string or int | lower case copy of a string, or of a character code |
| `str::toupper` | `s` or `ch` | string or int | upper case copy of a string, or of a character code |
| `str::mid` | `s, start [, count]` | string | `count` characters from `start` (default: to the end) |
| `str::substr` | `s, start [, count]` | string | same as `str::mid` |
| `str::left` | `s, count` | string | the first `count` characters |
| `str::trunc` | `s, count` | string | same as `str::left` |
| `str::right` | `s, count` | string | the last `count` characters |
| `str::insert` | `s, ins, pos` | string | `s` with `ins` inserted at `pos` |
| `str::concat` | `a, b` | string | `a` followed by `b` |
| `str::trim` | `s` | string | without leading and trailing white space |
| `str::trimleft` | `s` | string | without leading white space |
| `str::trimright` | `s` | string | without trailing white space |
| `str::tol` | `s [, base]` | int | parse an integer, like C `strtol` |
| `str::format` | `fmt, ...` | string | format like C `sprintf`, return the string |
| `str::sprintf` | `dest, fmt, ...` | int | format into variable `dest`, return its length |
| `str::printf` | `fmt, ...` | int | format and write to standard output |
| `str::tprint` | `value` | nothing | print the internal form of a value |

Every function that returns a string returns a new one; the arguments are
never changed (`str::sprintf` replaces what its first argument holds, see
below).

> [!NOTE]
> A character is an int, its code: `'a'` is 97. Indexing a string gives
> one (`s[0]`), and the `ch` arguments above take one. A one character
> string such as `"a"` is not a character; passed as `ch` it is reduced to
> its hash and matches nothing.

## Length and characters

### str::strlen

`str::strlen( s )` returns the number of characters in the string `s`.
For anything that is not a string, or without an argument, it returns 0.
`s._count` gives the same length (see [Strings](../language/strings.md)).

```wrench
var s = "hello";
println( str::strlen( s ), " ", s._count, " ", str::strlen( "" ), " ", str::strlen( 42 ) );
```

Output:

```text
5 5 0 0
```

### str::chr

`str::chr( s, ch )` returns the index of the first character `ch` in `s`,
or -1 when it does not occur, when `s` is not a string or when an argument
is missing. Like C `strchr`, but it returns an index, not a pointer.

```wrench
println( str::chr( "hello", 'l' ), " ", str::chr( "hello", 'z' ), " ", str::chr( "hello", 104 ) );
println( str::chr( "hello", "h" ) );
```

Output:

```text
2 -1 0
-1
```

### str::isspace, str::isdigit, str::isalpha, str::isalnum

`str::isspace( ch )`, `str::isdigit( ch )`, `str::isalpha( ch )` and
`str::isalnum( ch )` call the C functions of the same name on the character code `ch` (reduced to 0
to 255). They return non-zero for true and 0 for false. The non-zero value
is whatever the C library returns, it differs between platforms, so test
the result for truth or compare it with 0, never with 1. Called without
exactly one argument they return 0.

```wrench
println( str::isdigit( '7' ) != 0, " ", str::isalpha( '7' ) != 0, " ", str::isspace( '\t' ) != 0, " ", str::isalnum( '_' ) != 0 );
println( str::isdigit( '7' ), " ", str::isspace( ' ' ) );
var s = "a1 b2";
var digits = 0;
for( var i = 0; i < s._count; ++i )
{
	if ( str::isdigit( s[i] ) )
	{
		++digits;
	}
}
println( digits, " digits" );
```

Output (the second line is from Windows):

```text
1 0 1 0
4 8
2 digits
```

White space is space, tab, newline, carriage return, vertical tab and form
feed. `str::isalnum` accepts exactly the characters that `str::isalpha`
or `str::isdigit` accepts.

### str::tolower, str::toupper

Given a string, `str::tolower` and `str::toupper` return a copy with every
letter converted; other characters stay as they are. Given an int, they
convert that one character code and return the new code (codes outside 0
to 255 are returned unchanged). Any other argument, a float for example,
returns 0.

```wrench
println( str::toupper( "Hello, World 1!" ), " ", str::tolower( "Hello, World 1!" ) );
println( str::toupper( 'q' ), " ", str::tolower( 'Q' ), " ", str::toupper( 2.5 ) );
println( str::format( "%c", str::toupper( 'q' ) ) );
```

Output:

```text
HELLO, WORLD 1! hello, world 1!
81 113 0
Q
```

## Cutting and joining

### str::mid

`str::mid( s, start [, count] )` returns `count` characters of `s`
starting at index `start`; without `count` it returns everything from
`start` to the end. `str::substr` is the same function under another
name.

* A `count` past the end is cut to the end.
* A `start` at or past the end gives an empty string.
* Both numbers are treated as unsigned: a negative `start` is past the end
  (empty string), a negative `count` means "to the end".
* An empty `s` gives an empty string.
* A `s` that is not a string, or fewer than two arguments, return the int
  0, not an empty string.

```wrench
var s = "hello world";
println( "[", str::mid( s, 6, 3 ), "] [", str::mid( s, 6 ), "] [", str::substr( s, 0, 5 ), "]" );
println( "[", str::mid( s, 6, 100 ), "] [", str::mid( s, 20, 2 ), "] [", str::mid( s, -1, 2 ), "] [", str::mid( s, 1, -1 ), "]" );
println( "[", str::mid( "", 0, 2 ), "] [", str::mid( s ), "] [", str::mid( 42, 0 ), "]" );
```

Output:

```text
[wor] [world] [hello]
[world] [] [] [ello world]
[] [0] [0]
```

### str::left, str::trunc, str::right

`str::left( s, count )` returns the first `count` characters of `s`,
`str::right( s, count )` the last `count`. `str::trunc` is the same
function as `str::left`. A `count` larger than the string returns all of
it; `count` is treated as unsigned, so a negative one returns all of it
too. A `s` that is not a string or a missing argument returns the int 0.

```wrench
var s = "hello";
println( "[", str::left( s, 2 ), "] [", str::trunc( s, 4 ), "] [", str::right( s, 3 ), "]" );
println( "[", str::left( s, 10 ), "] [", str::right( s, 10 ), "] [", str::left( s, 0 ), "] [", str::left( s, -1 ), "]" );
```

Output:

```text
[he] [hell] [llo]
[hello] [hello] [] [hello]
```

### str::insert

`str::insert( s, ins, pos )` returns `s` with the string `ins` inserted
before index `pos`. A `pos` past the end (or negative, which is treated as
unsigned) appends. Both strings and all three arguments are required,
otherwise the result is the int 0.

### str::concat

`str::concat( a, b )` returns the two strings joined. Only two arguments
are used, extra ones are ignored, and both must be strings: a number makes
the result the int 0. The `+` operator joins strings too and converts
numbers to text, so it is usually the better choice.

```wrench
println( "[", str::insert( "helloworld", ", ", 5 ), "] [", str::insert( "abc", "X", 0 ), "] [", str::insert( "abc", "X", 99 ), "]" );
println( "[", str::concat( "ab", "cd" ), "] [", str::concat( "a", "b", "c" ), "] [", str::concat( "ab", 5 ), "]" );
println( "[", "ab" + 5, "]" );
```

Output:

```text
[hello, world] [Xabc] [abcX]
[abcd] [ab] [0]
[ab5]
```

### str::trim, str::trimleft, str::trimright

`str::trim( s )` returns `s` without white space at both ends,
`str::trimleft( s )` only at the start, `str::trimright( s )` only at the
end. White space is what `str::isspace` accepts, including `\r` and `\n`,
so `str::trimright` also removes a line ending. A string of only white
space becomes the empty string; a value that is not a string returns the
int 0.

```wrench
var s = " \t padded \r\n";
println( "[", str::trim( s ), "] [", str::trimleft( "  x  " ), "] [", str::trimright( "  x  " ), "] [", str::trim( "   " ), "]" );
```

Output:

```text
[padded] [x  ] [  x] []
```

## Numbers from text

### str::tol

`str::tol( s [, base] )` parses an integer at the start of the string `s`,
like C `strtol`, and returns it. `base` defaults to 10.

* Leading white space is skipped, then an optional `+` or `-`.
* Digits are read as long as they are valid in `base`; the first other
  character ends the number. Letters are digits from 10 up (`a` or `A` is
  10, `z` is 35), so bases 2 to 36 work.
* Base 16 accepts and skips a `0x` or `0X` prefix.
* Base 0 picks the base from the text: `0x` means 16, a leading `0` means
  8, anything else 10.
* No digits, or an invalid base (1, negative, above 36), give 0.
* The result is clamped to the int range, -2147483648 to 2147483647, on
  every platform.
* A number argument is converted to text first, so a float is truncated.
  Call it with one or two arguments; otherwise it returns 0.

```wrench
println( str::tol( "123" ), " ", str::tol( "  -42 apples" ), " ", str::tol( "+7" ), " ", str::tol( "abc" ), " ", str::tol( "" ) );
println( str::tol( "ff", 16 ), " ", str::tol( "0x1F", 16 ), " ", str::tol( "101", 2 ), " ", str::tol( "z", 36 ) );
println( str::tol( "0x1F", 0 ), " ", str::tol( "017", 0 ), " ", str::tol( "17", 0 ), " ", str::tol( "017" ) );
println( str::tol( "99999999999" ), " ", str::tol( "-99999999999" ), " ", str::tol( 3.7 ), " ", str::tol( "12", 1 ) );
```

Output:

```text
123 -42 7 0 0
255 31 5 35
31 15 17 17
2147483647 -2147483648 3 0
```

Because "no number" and "the number 0" both return 0, check the text
first (`str::isdigit( s[0] )`) when the difference matters. There is no
function that parses a float.

## Formatting

### str::format, str::sprintf, str::printf

The three functions format text the way C `sprintf` does; they share one
formatter and differ only in where the result goes.

* `str::format( fmt, ... )` returns the formatted string. With no
  arguments, or a `fmt` that is not a string, it returns the int 0.
* `str::sprintf( dest, fmt, ... )` stores the formatted string in the
  variable `dest` and returns its length. It needs at least `dest` and
  `fmt`, otherwise it does nothing and returns 0. `dest` gets a new string:
  other variables that held the same string keep the old one, and `dest`
  may also be one of the arguments.
* `str::printf( fmt, ... )` writes the formatted string to standard output
  and returns the number of characters written.

```wrench
var name = "probe";
var s = str::format( "%s: %d readings, %x hex", name, 12, 255 );
println( s );
var out;
var n = str::sprintf( out, "[%5d]", 42 );
println( out, " has ", n, " characters" );
var written = str::printf( "%s %d\n", "printed", 7 );
println( written );
```

Output:

```text
probe: 12 readings, ff hex
[   42] has 7 characters
printed 7
10
```

#### Conversions

| Specifier | Argument | Output |
|---|---|---|
| `%d`, `%i` | int | signed decimal |
| `%u` | int | unsigned decimal (the 32 bits read as unsigned) |
| `%x`, `%X` | int | hexadecimal, lower or upper case digits |
| `%o` | int | octal |
| `%b` | int | binary (not in C) |
| `%p` | int | `0x` and upper case hexadecimal digits (not C's pointer format) |
| `%c` | int | the character with that code |
| `%s` | any | a string as it is; any other value converted to text |
| `%f`, `%F` | float | fixed point, 6 decimals unless a precision is given |
| `%e`, `%E` | float | one digit, 6 decimals and an exponent: `-2.250000e+00` |
| `%g`, `%G` | float | 6 significant digits in the shorter of the two forms, trailing zeros dropped |
| `%%` | none | a `%` |

Every integer is 32 bits. A float argument to an integer conversion is
truncated (`%d` of 2.9 is 2); a string argument gives its hash value. An
int argument to a float conversion is converted (`%F` of 100 is
`100.000000`). A `%s` of an array or hash table prints the container's
hash, not its contents.

```wrench
println( str::format( "%d|%i|%u|%u", 42, -42, 42, -1 ) );
println( str::format( "%x|%X|%o|%b|%p", 255, 255, 8, 5, 255 ) );
println( str::format( "%c%c|%s|%s|%s|%%", 'o', 107, "text", 12, 1.5 ) );
println( str::format( "%f|%g|%e|%F", 3.14159, 0.5, -2.25, 100 ) );
```

Output:

```text
42|-42|42|4294967295
ff|FF|10|101|0xFF
ok|text|12|1.5|%
3.141590|0.5|-2.250000e+00|100.000000
```

#### Flags, width and precision

A specifier is `%`, then optional flags, an optional width, an optional
precision, optional length modifiers, and the conversion character, the
same order as in C.

| Part | Meaning |
|---|---|
| `-` | left justify, pad on the right with spaces |
| `+` | print a `+` before a positive `%d`, `%i` or float |
| space | print a space before a positive `%d`, `%i` or float |
| `0` | pad numbers with zeros after the sign and the prefix; ignored for `%s` and `%c`, and for an integer with a precision |
| `#` | prefix `0x` (`%x`), `0X` (`%X`) or `0b` (`%b`) on a value that is not 0; a leading `0` on `%o`; floats always get a decimal point, and `%g` keeps its trailing zeros |
| width, `5` | pad to at least this many characters; the sign and the prefix count toward it |
| `.` and digits | precision: the minimum number of digits of an integer (`%.0d` of 0 prints nothing), the decimals of `%f` and `%e`, the significant digits of `%g`, the maximum length of `%s` |
| `*` | instead of the width or the precision digits: take them from the next argument; a negative width means `-`, a negative precision means none |
| `l`, `h`, `L`, `z`, `j`, `t`, `q` | length modifiers, skipped: `%lld` reads the same 32 bit int as `%d` |

Widths and precisions above 4096 are cut to 4096.

```wrench
println( "[", str::format( "%5d|%-5d|%05d", 42, 42, 42 ), "]" );
println( "[", str::format( "%5d|%05d|%-5d", -42, -42, -42 ), "]" );
println( "[", str::format( "%#x|%#X|%08x|%#010x|%x", 255, 255, 255, 255, -1 ), "]" );
println( "[", str::format( "%5o|%08b|%10p", 8, 5, 255 ), "]" );
println( "[", str::format( "%10s|%-10s|%010s", "hi", "hi", "hi" ), "]" );
println( "[", str::format( "%+d|% d|%+5d|%5.3d|%.0d|%#x|%#o|%#b", 5, 6, 7, 7, 0, 0, 8, 5 ), "]" );
println( "[", str::format( "%.2s|%3c|%-3c|%ld|%lld|%zu", "hello", 'z', 'z', 1, 2, 3 ), "]" );
println( "[", str::format( "%*d|%-*d|%.*f|%*.*s", 5, 1, 4, 2, 2, 3.14159, 6, 2, "abcdef" ), "]" );
```

Output:

```text
[   42|42   |00042]
[  -42|-0042|-42  ]
[0xff|0XFF|000000ff|0x000000ff|ffffffff]
[   10|00000101|      0xFF]
[        hi|hi        |        hi]
[+5| 6|   +7|  007||0|010|0b101]
[he|  z|z  |1|2|3]
[    1|2   |3.14|    ab]
```

#### Floats

The float conversions print what C prints for the same float. Without
the build flag `WRENCH_FLOAT_SPRINTF` (the default, see
[Build flags](../reference/build-flags.md)) wrench computes the digits
itself, from the exact binary value of the float, rounding a tie to the
even digit; that keeps the C library's float formatter out of small
firmware. With the flag, the C library's `snprintf` produces the digits
(at most 80 decimals) and wrench adds the width and the padding.

Two things follow from the exact value:

* A float is 32 bits, so most decimal fractions are not exact: `%.10f` of
  0.1 is `0.1000000015`, and `%.3f` of 2.0005 is `2.000`, because the
  float nearest to 2.0005 is a little below it.
* Infinity prints as `inf` (`INF` for `%F`, `%E`, `%G`) and is padded
  with spaces, never zeros. Not a number prints as `nan` (`-nan` when
  its sign bit is set).

Each float specifier consumes exactly one argument, so the specifiers
after it stay matched with their arguments.

```wrench
println( "[", str::format( "%f|%.2f|%8.3f|%-8.1f|%08.2f", 3.14159, 3.14159, 3.14159, -2.5, -2.5 ), "]" );
println( "[", str::format( "%g|%e|%.3e|%G|%E", 0.5, -2.25, 1234.5, 0.00001, 12345.678 ), "]" );
println( "[", str::format( "%g|%g|%#g|%.10f|%.3f", 100000.0, 1000000.0, 1.0, 0.1, 2.0005 ), "]" );
var huge = 1e30 * 1e30;
println( "[", str::format( "%f|%E|%05f", huge, -huge, huge ), "]" );
```

Output:

```text
[3.141590|3.14|   3.142|-2.5    |-0002.50]
[0.5|-2.250000e+00|1.234e+03|1E-05|1.234568E+04]
[100000|1e+06|1.00000|0.1000000015|2.000]
[inf|-INF|  inf]
```

With `WRENCH_FLOAT_SPRINTF` the output is the same on a host whose C
library rounds correctly (glibc does).

#### Missing, extra and invalid arguments

* A specifier without an argument left prints 0 (an empty string for
  `%s`, nothing for `%c`); extra arguments are ignored.
* An unknown conversion character (`%k`) prints nothing and consumes no
  argument.

```wrench
println( "[", str::format( "%d %d %s|", 1 ), "]" );
println( "[", str::format( "%d", 2.9 ), "] [", str::format( "%d", -2.9 ), "] [", str::format( "%x", "a" ), "]" );
println( "[", str::format( "%k%d", 5 ), "] [", str::format( "%c|" ), "]" );
println( "[", str::format( "no specifiers", 1, 2 ), "] [", str::format(), "] [", str::format( 5 ), "]" );
println( str::format( "%99999d", 1 )._count );
```

Output:

```text
[1 0 |]
[2] [-2] [e40c292c]
[5] [|]
[no specifiers] [0] [0]
4096
```

### str::printf

`str::printf` writes the formatted text in one piece. Where it goes
depends on the file system flag the library was built with:

| Build | Destination |
|---|---|
| `WRENCH_WIN32_FILE_IO`, `WRENCH_LINUX_FILE_IO` | the process's standard output (descriptor 1), after flushing C `stdout` so it stays in order with what the host printed |
| `WRENCH_SPIFFS_FILE_IO`, `WRENCH_LITTLEFS_FILE_IO` | the Arduino `Print` object `WRENCH_FS_STDOUT`, `Serial` unless you define another |
| none of them | C `stdout` (`fwrite`); on a board without a C library console this goes nowhere |

It returns the number of characters written; an empty result writes
nothing and returns 0.

## str::tprint

`str::tprint( value )` prints the internal form of a value to standard
output (C `printf`, whatever the file system flags): a blank line, the
value, and the value again with ints in hexadecimal. Containers are
printed with their contents. It is a debugging aid for people working on
wrench itself; use `println` or `str::format` in programs. Without an
argument it prints nothing.

```wrench
str::tprint( 5 );
var a[] = { 1, "x" };
str::tprint( a );
```

Output:

```text

5
0x00000005

ref: [ 1, "x" ]
ref: [ 0x00000001, "x" ]
```
