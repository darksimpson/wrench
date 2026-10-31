# Enums and constants

Wrench has three kinds of named constants: the built in `true`, `false`
and `null`, enum constants declared in the script, and library constants
such as `io::O_CREAT` that the host provides. This page covers all three.

## Built in constants

| Name | Value |
|---|---|
| `true` | the int 1 |
| `false` | the int 0 |
| `null` | the int 0 |

They are plain ints: `true + true` is 2, and `null` is not a separate
"no value" (see [No value: 0](types.md#no-value-0)).

## enum

`enum` declares names for numbers. The compiler replaces each name with
its value, like a `#define` in C: an enum constant takes no memory and no
global slot.

```wrench
enum
{
	IDLE,
	RUNNING,
	STOPPED = 10,
	FAILED,
	ERROR = -1
};

println( IDLE, " ", RUNNING, " ", STOPPED, " ", FAILED, " ", ERROR );
```

Output:

```text
0 1 10 11 -1
```

* Without a value, a constant is one more than the int constant before
  it; the first is 0.
* A value is a literal: an int (decimal, hex, binary, octal), a float, or
  either with a leading `-`. An expression such as `1 + 1` is
  `WR_ERR_bad_label`; an earlier constant is allowed, since it stands for
  a literal.
* A float constant does not move the counter: in `enum { X = 2.5, Y }`,
  `Y` is 1 (its position).
* The entries are separated by commas; a missing or doubled comma is
  `WR_ERR_unexpected_token`. A comma after the last entry and the `;`
  after the closing brace are optional.

## Named enums

An enum with a name puts its constants under that name. They are written
`Name::CONSTANT`, and two named enums may use the same constant names:

```wrench
enum Color { RED, GREEN, BLUE }
enum Fruit { RED = 10, GREEN = 20 }

var c = Color::BLUE;
println( Color::RED, " ", c, " ", Fruit::RED );

switch ( c )
{
	case Color::RED:
		println( "red" );
		break;
	case Color::BLUE:
		println( "blue" );
		break;
}
```

Output:

```text
0 2 10
blue
```

The bare `RED` is not defined by a named enum. A misspelled qualified
name such as `Color::PURPLE` compiles (it looks like a library constant)
and stops the script at run time with `WR_ERR_library_constant_not_loaded`.

## Scope and order

* An enum at the top level declares constants for the rest of the
  program; an enum inside a function declares constants for that function
  only.
* A constant is known from its declaration on: using it on an earlier
  line, also inside a function declared earlier, is
  `WR_ERR_var_not_seen_before_label`. Put enums at the top of the file.
* Constants can be used anywhere a literal can, including `case` labels.

## Constants cannot change

A constant is a value, not a variable:

| Code | Result |
|---|---|
| `IDLE = 3;`, `IDLE++;` | `WR_ERR_not_assignable` |
| `var IDLE = 3;` | `WR_ERR_not_assignable` (also inside a function) |
| `function f( IDLE )` | `WR_ERR_bad_label` |
| the same name in two anonymous enums | `WR_ERR_bad_label` |
| the same name twice in one named enum, or in two enums of the same name | `WR_ERR_constant_redefined` |

Member names are not affected: `p.IDLE` is the member `IDLE`, and a struct
may declare a member `var IDLE;`. Quoted text is not affected either:
`"IDLE"` stays a string.

## Library constants

A host registers constants with `wr_registerLibraryConstant()` under a
qualified name, `library::NAME`. The standard `io` library has
`io::O_RDONLY`, `io::O_CREAT`, `io::SEEK_SET` and others (see
[io](../library/io.md)):

```wrench
println( io::O_CREAT, " ", io::SEEK_END );
```

Output:

```text
256 2
```

Library constants are looked up when the line runs, by the hash of their
name: a script compiles without knowing them, and a constant the host did
not register stops the script with `WR_ERR_library_constant_not_loaded`.
They cannot be assigned to (`WR_ERR_not_assignable`) or called as
functions. How a host defines them is described in
[Native functions and libraries](../embedding/natives.md).
