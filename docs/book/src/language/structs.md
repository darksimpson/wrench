# Structs

A struct groups named values, its members, into one object. This page
shows how to declare structs, make instances with `new` (with arguments
and initializers), arrays of structs, member access and what instances
share. Wrench structs hold data only: there are no methods and no `this`.

## Declaring a struct

```wrench
struct Point
{
	var x;
	var y = 10;
};

var p = new Point;
println( p.x, " ", p.y );
p.x = 3;
println( p.x + p.y );
```

Output:

```text
0 10
13
```

Every `var` of the struct body is a member; a member without a value
starts at 0. The `;` after the closing brace is optional. Structs are
declared at the top level, like functions, and can be used before their
declaration in the source.

## The body is a constructor

A struct is a function whose local variables are kept (see
[Everything is a unit](structure.md#everything-is-a-unit)). `new` runs its
body from the top, and every variable the body declares, wherever it is
declared, becomes a member of the new instance. The body can run any code:

```wrench
struct Sensor( name, limit )
{
	var label = "sensor " + name;
	var max = limit;
	if ( !max )
	{
		max = 100;
	}
	var readings[] = { };
	println( "made ", label );
}

var a = new Sensor( "t1", 50 );
var b = new Sensor( "t2" );
println( a.max, " ", b.max, " ", b.readings._count );
```

Output:

```text
made sensor t1
made sensor t2
50 100 0
```

Rules for the body:

* The struct's parameters (`name`, `limit` above) are arguments of the
  constructor, not members: `a.name` is 0. Copy a parameter into a member
  with a `var`.
* A `var` may have the name of a parameter; it declares a member of that
  name. In its initializer the name is still the parameter, after it the
  name is the member: `var value = value;` keeps the argument as the
  member `value`. Without an initializer (`var value;`) the member starts
  at 0 like any other.
* A bare name in the body never means a global, use `::name` to read one.
  Functions can be called as usual.
* Arguments work as for functions: missing ones are 0, extra ones are
  ignored.
* A struct without members gives 0 from `new`, with or without
  parameters: `struct R( a ) { }` makes no instance.

```wrench
struct Reading( sensor, value )
{
	var sensor = sensor;
	var value = value * 2;
}

var r = new Reading( "t1", 4 );
println( r.sensor, " ", r.value, " ", r._count );
```

Output:

```text
t1 8 2
```

## Making instances with new

| Form | Meaning |
|---|---|
| `new S` or `new S()` | run the body without arguments |
| `new S( a, b )` | run the body with arguments |
| `new S { x = 1, y = 2 }` | then set members by name |
| `new S { 1, 2 }` | then set members by position, in declaration order |
| `new S( a ) { y = 2 }` | arguments and an initializer together |
| `new S[] { { ... }, { ... } }` | an array of instances, see below |

The initializer runs after the body, so it overrides what the body set:

```wrench
struct T( n )
{
	var a = 11;
	var b = 15;
	var c = n;
}

var t1 = new T( 1 );
var t2 = new T( 2 ) { b = 201, a = 101 };
var t3 = new T { 55, 66 };
println( t1.a, " ", t1.b, " ", t1.c );
println( t2.a, " ", t2.b, " ", t2.c );
println( t3.a, " ", t3.b, " ", t3.c );
```

Output:

```text
11 15 1
101 201 2
55 66 0
```

* An initializer uses names or positions, not both
  (`WR_ERR_new_assign_by_label_or_offset_not_both`).
* By position, values past the last member are ignored.
* By name, a name that is not a member stops the script with
  `WR_ERR_hash_not_found`.
* The values of an initializer are expressions; they see the variables of
  the code around the `new`.

`new` is an expression and can be used wherever a value can: as an
argument, after `return`, in a ternary, or followed by a member:

```wrench
struct Reading( sensor, value )
{
	var sensor = sensor;
	var value = value;
}
function check( r )
{
	println( r.sensor, " ", r.value );
}
function make( v )
{
	return new Reading( "t", v );
}
check( new Reading( "t", 85 ) );
println( make( 90 ).value, " ", new Reading( "u", 7 ).sensor );
```

Output:

```text
t 85
90 u
```

Any function can be used with `new`; `struct` makes the intent clear and
always exports the unit for importing programs (see
[Modules](import.md#structs-and-new)).

## Arrays of structs

`new S[] { ... }` makes an array with one instance per inner initializer.
Each element runs the body, then its initializer (by position or by
name); `{ }` keeps the body's values.

```wrench
struct Keyword
{
	var text;
	var token = -1;
}

var keywords = new Keyword[]
{
	{ "if", 1 },
	{ "else", 2 },
	{ text = "while" },
	{ }
};
println( keywords._count, " ", keywords[1].text, " ", keywords[2].token, " ", keywords[3].token );
var none = new Keyword[] { };
println( none._count );
```

Output:

```text
4 else -1 -1
0
```

`new S[] { ... }` used as a value, such as an argument or a return value,
gives a new array; `new S[] { }` an empty one. Assigned to a variable,
`x = new S[] { ... }`, it fills `x` in place: when `x` already holds an
array, the instances are written into that array from index 0, and
elements past them stay.

## Members

`instance.member` reads and writes a member; `instance["member"]` does the
same with the name as a string.

* Reading a name that is not a member gives 0.
* Assigning to a name that is not a member stops the script with
  `WR_ERR_hash_not_found`: the members of an instance are fixed by its
  struct.
* `instance._count` is the number of members.
* `for` each loops and `._exists` do not work on instances.

```wrench
struct Point
{
	var x;
	var y;
}
var p = new Point { x = 1, y = 2 };
println( p["y"], " ", p.z, " ", p._count );
p.z = 5;
println( p.z );
```

Output:

```text
2 0 2
err: WR_ERR_hash_not_found
```

Members hold any value, including arrays, hash tables and other
instances (`line.start.x`).

## Instances are shared

Assigning an instance or passing it to a function does not copy it, as
with arrays (see [Containers are shared](containers.md#containers-are-shared)):

```wrench
struct Counter
{
	var n;
}
function bump( c )
{
	c.n++;
}
var c = new Counter;
var alias = c;
bump( c );
bump( alias );
println( c.n );
```

Output:

```text
2
```

There is no copy operator. Make a new instance and copy the members, or
use `std::deserialize( std::serialize( instance ) )` for a deep copy.

## Comparing instances

`a == b` is 1 when both are instances of the same struct and all their
members are equal, even when they are two different instances. Instances
of two different structs are never equal.

## Methods and this

There are no member functions and no `this`. Write functions that take the
instance as their first argument, as `bump( c )` above does.
`tests/016_this.w` in the repository keeps a possible future syntax for
member functions in comments; it is not part of the language.

## Limits

A struct has at most 254 members (with its parameters), and an
initializer at most 255 values; see [Limits](../reference/limits.md).
Struct instances, like other containers, live until the garbage collector
finds nothing refers to them.
