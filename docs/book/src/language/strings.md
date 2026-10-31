# Strings

This page covers string and character literals with all their escape
sequences, and what the language itself does with strings: indexing,
length, concatenation, comparison and conversion. Searching, cutting and
formatting are library functions, described in [str](../library/str.md).

## String literals

A string literal is text between double quotes. It must end on the line
it starts on (a line break inside it is
`WR_ERR_newline_in_string_literal`); use `\n` for a line break in the
text. Two literals next to each other are not joined, use `+`.

```wrench
var s = "hello";
println( s, " has ", s._count, " characters" );
```

Output:

```text
hello has 5 characters
```

Strings are sequences of 8 bit bytes. There is no character set or
Unicode handling: a UTF-8 text is stored and printed as its bytes, and
`._count` counts bytes. A string may contain zero bytes; printing stops at
the first one.

A literal can be at most 65535 bytes long (`WR_ERR_string_too_long`).

## Escape sequences

| Escape | Character |
|---|---|
| `\n` | line feed (10) |
| `\r` | carriage return (13) |
| `\t` | tab (9) |
| `\a` | bell (7) |
| `\b` | backspace (8) |
| `\f` | form feed (12) |
| `\v` | vertical tab (11) |
| `\\` | backslash |
| `\"` | double quote |
| `\'` | single quote |
| `\xH`, `\xHH` | the character with this hexadecimal code, one or two digits |
| `\0`, `\0NNN` | the character with this **decimal** code; `\0` alone is a zero byte |

Any other character after a backslash is the compile error
`WR_ERR_bad_string_escape_sequence`, and so is `\x` without a hex digit.

> [!IMPORTANT]
> `\0NNN` is decimal, unlike C where it is octal: `"\097"` is `"a"`
> (character 97), not character 79. The escape takes every digit that
> follows, so `"\0491"` is one character (491, which wraps to 235), not
> character 49 followed by `1`. When a digit must follow such an escape,
> write the character with `\x` instead: `"\x31" + "1"`, or split the
> literal.

```wrench
println( "\097\098\099", " ", "\x41\x4a", " ", "tab[\t] quote[\"] backslash[\\]" );
var z = "a\0b";
println( z._count, " ", z );
```

Output:

```text
abc AJ tab[	] quote["] backslash[\]
3 a
```

## Character literals

A character in single quotes is an int, the code of the character, from 0
to 255 (`'\xFF'` is 255, never negative). The same escapes as in strings
work. An empty `''` is 0; more than one character (`'ab'`) is the compile
error `WR_ERR_bad_character_literal`.

```wrench
println( 'A', " ", 'a' + 1, " ", '\n', " ", '\x41', " ", '\065' );
```

Output:

```text
65 98 10 65 65
```

Since a character is an int, it prints as a number and adds as a number:
`"ab" + 'c'` is `"ab99"`. To make a one character string, use
`str::format( "%c", code )` (see [str](../library/str.md)).

## Indexing

`s[i]` is the code of the character at position `i`, from 0. Writing an
element changes that character of the string:

```wrench
var s = "hello";
println( s[1], " ", s[1] == 'e' );
s[0] = 'J';
s[4]++;
println( s );
```

Output:

```text
101 1
Jellp
```

Writing past the end grows the string, filling the gap with zero bytes.
Reading past the end gives 0 and leaves the string as it is, as with
arrays (see [Reading past the end](containers.md#reading-past-the-end)).
An index that is a float is truncated to an int.

`for ( var c : s )` visits the characters as ints, `for ( var i, var c : s )`
with their positions (see [for each](control-flow.md#for-each-arrays-hash-tables-strings)).

## Length

`s._count` is the length in bytes; `str::strlen( s )` is the same as a
library call.

## Concatenation

`+` joins strings. When one side is a string and the other a number, the
number is converted to text first (floats as described in
[Printing values](types.md#printing-values)). `+=` appends.

```wrench
var name = "pump";
var line = name + " " + 3 + " at " + 2.5 + " bar";
println( line );
var log = "";
log += "a";
log += 1;
println( log );
println( 1 + 2 + "x", " ", "x" + 1 + 2 );
```

Output:

```text
pump 3 at 2.5 bar
a1
3x x12
```

Evaluation is left to right, so `1 + 2 + "x"` adds the numbers before the
first string comes in.

## Comparison

`==` and `!=` compare the characters: two strings are equal when they
have the same length and the same bytes, wherever they come from.

```wrench
var a = "abc";
var b = "ab";
b += "c";
println( a == b, " ", a != "abd", " ", a == "ABC" );
```

Output:

```text
1 1 0
```

`<`, `<=`, `>` and `>=` order strings by their bytes, as C `strcmp()`
does: a prefix comes before the longer string, and `"B" < "a"`.

```wrench
var names[] = { "pear", "apple", "fig" };
var first = names[0];
for ( var n : names )
{
	if ( n < first )
	{
		first = n;
	}
}
println( first, " ", "ab" < "abc" );
```

Output:

```text
apple 1
```

A string is never equal to a number: `"3" == 3` is 0.

## Strings are shared

Like arrays, strings are shared on assignment: `var t = s;` gives a second
name for the same string, and a character written through one name shows
through the other. Operations that build a string (`+`, `+=`, the library
functions) make a new string, so `t += "!"` leaves `s` alone.

```wrench
var s = "abc";
var t = s;
t[0] = 'X';
println( s );
t += "!";
println( s, " ", t );
```

Output:

```text
Xbc
Xbc Xbc!
```

To get a copy you can change without touching the original, build a new
string: `var copy = s + "";`.

## Conversion

| From | To | How |
|---|---|---|
| number | string | `"" + n`, or `str::format()` for a chosen format |
| string of digits | int | `str::tol( s )`, `str::tol( s, base )` |
| character code | string | `str::format( "%c", code )` |

A cast such as `(int)"42"` does not parse the text, see
[Conversions](types.md#conversions). The library has no function that
parses a float.

## The str library

Searching, cutting (`str::mid`, `str::left`, `str::right`), trimming,
case conversion, character classes and `sprintf` style formatting are in
the [str library](../library/str.md).
