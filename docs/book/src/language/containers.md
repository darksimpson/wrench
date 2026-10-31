# Arrays and hash tables

Wrench has two built in containers: arrays, indexed by position, and hash
tables, indexed by keys of any type. This page shows how to create,
read, grow and shrink them, the intrinsics `._count`, `._exists`,
`._remove` and `._hash`, and the most important rule: containers are
shared, not copied. The `array::` and `hash::` library functions
(and the list, queue and stack helpers built on arrays) are described in
[Containers library](../library/containers.md).

## Arrays

An array holds numbered elements, starting at 0. Elements may be of any
type, mixed, including other arrays.

```wrench
var primes[] = { 2, 3, 5, 7 };
var mixed[] = { "zero", 1, 2.5, { "k": 1 } };
var zeros[4];
println( primes[2], " ", primes._count );
println( mixed[0], " ", mixed[3].k );
println( zeros._count, " ", zeros[3] );
```

Output:

```text
5 4
zero 1
4 0
```

The ways to make an array:

| Form | Result |
|---|---|
| `var a[] = { 1, 2, 3 };` | an array of the listed values |
| `var a = { 1, 2, 3 };` | the same: an initializer without keys is an array |
| `var a[] = { };` | an empty array |
| `var a[8];` | an array of 8 zeros |
| `var a;` then `a[2] = 1;` | an array made by the first assignment, 3 elements |

The size in `var a[8];` may be a variable or an expression (`var a[n];`,
`var a[n * 2];`).

In a declaration with an initializer the number in the brackets is
ignored: `var a[30] = { 8, 9 };` has 2 elements.

### Growing and shrinking

Assigning past the end grows the array; the elements in between are 0.
Appending at `._count` is the usual way to add to an array, and it is
cheap: arrays keep spare room as they grow.

```wrench
var list[] = { };
list[list._count] = "a";
list[list._count] = "b";
list[5] = "f";
println( list._count, " ", list[1], " ", list[3] );
list._remove( 0 );
println( list._count, " ", list[0] );
```

Output:

```text
6 b 0
5 b
```

`._remove( index )` removes one element and moves the ones after it down.
The `array::` library inserts, removes several, clears and truncates (see
[Containers library](../library/containers.md)).

An array holds at most 2097151 elements (`WRENCH_MAX_ARRAY_ELEMENTS`);
writes beyond that and writes to negative indexes are ignored, and reads
there give 0.

An index that is a float is truncated to an int: `a[1.7]` is `a[1]`.

### Reading past the end

Reading an element past the end gives 0 and leaves the array as it is:

```wrench
var a[] = { 1, 2, 3 };
var x = a[10];
println( x, " ", a._count );
```

Output:

```text
0 3
```

Only writes grow an array: an assignment, a compound assignment such as
`a[5] += 1`, or `++` and `--` on an element past the end.

### Nested arrays

```wrench
var grid[] = { { 1, 2, 3 }, { 4, 5, 6 } };
grid[1][2] = 60;
println( grid._count, " ", grid[0]._count, " ", grid[1][2] );
```

Output:

```text
2 3 60
```

## Hash tables

A hash table maps keys to values. Write it with `key : value` pairs; read
and write it with `[key]`. For keys that are strings and valid names,
`table.name` is short for `table["name"]`.

```wrench
var t = { 1: "one", "two": 2, "three": 3.0 };
println( t[1], " ", t["two"], " ", t.three, " ", t._count );
t.four = 4;
t[5] = "five";
println( t._count, " ", t["four"] );
```

Output:

```text
one 2 3 3
5 4
```

A key added with the member syntax (`t.four = 4`) is stored as the hash
of its name, an int. Lookups work both ways (`t.four`, `t["four"]`), but a
`for ( var k, var v : t )` loop shows that key as a number. A key stored as
a string (`t["four"] = 4`, or in an initializer) stays a string when the
member syntax reads or writes it later. Add keys with `t["four"]` in tables
whose keys you iterate.

Keys:

* Ints and strings are the usual keys. The int 1 and the string `"1"` are
  different keys.
* An array, hash table or struct instance can be a key; it is identified
  by its contents, so an equal array finds the same entry.
* A key in an initializer may be any expression, including a variable:
  `{ name : value }` uses the value of `name` as the key.
* A float is a key of its own: `t[2]` and `t[2.0]` are two different
  entries.

A hash table holds at most 49157 entries; an assignment that would add
one more stops the script with `WR_ERR_hash_table_size_exceeded`. See
[Limits](../reference/limits.md).

### Missing keys

Reading a key that is not there gives 0 and adds nothing. To tell a
missing key from a stored 0, test with `._exists( key )`:

```wrench
var t = { "a": 1 };
println( t._exists( "b" ), " ", t._count );
var v = t["b"];
println( v, " ", t._exists( "b" ), " ", t._count );
```

Output:

```text
0 1
0 0 1
```

Writes add the key: an assignment, a compound assignment (`t.hits += 1`
starts from 0) and `++` or `--`.

`._exists( key )` gives 1 or 0. On an array or a string it tells whether
the index is in range: `a._exists( i )` is 1 for `0 <= i < a._count`.

### Removing keys

```wrench
var t = { "a": 1, "b": 2, "c": 3 };
t._remove( "a" );
println( t._exists( "a" ), " ", t._count );
t._remove( "b" )._remove( "c" );
println( t._count );
```

Output:

```text
0 2
0
```

`._remove()` gives the container back, so removals chain. The
`hash::remove()` library function returns whether the key was there.

### Iterating

`for ( var k, var v : table )` visits every entry, `for ( var v : table )`
every value; see [for each](control-flow.md#for-each-arrays-hash-tables-strings).
The order is the internal order of the table, not the order of insertion.

## Initializers

`{ ... }` makes a new container each time it runs:

* values only, `{ 1, 2, 3 }`: an array;
* `key : value` pairs: a hash table;
* `{ }` or `{ : }`: an empty hash table, except right after
  `var a[] =`, where `{ }` is an empty array.

Initializers nest, anywhere in a hash table or an array:

```wrench
var grid[] = { { 1, 2 }, 5, { 3, 4 } };
var config = { "limits": { "min": 10, "max": 90 }, "steps": { 1, 2, 4 }, "name": "pump" };
println( grid[2][0], " ", config.limits.max, " ", config.steps[2], " ", config.name );
```

Output:

```text
3 90 4 pump
```

Each nested `{ }` is followed by `,` or by the closing `}`. The compile
errors of initializers:

| Error | Cause |
|---|---|
| `WR_ERR_hash_declaration_in_array` | a `key : value` pair after values (or after nested arrays) |
| `WR_ERR_array_declaration_in_hash` | a value without a key, or a nested array, in a hash table |
| `WR_ERR_hash_table_invalid_key` | a nested initializer as a key: `{ { 1 }: 2 }` |
| `WR_ERR_unexpected_token` | a missing comma after a nested initializer |

## Containers are shared

Assigning an array, hash table or struct instance to another variable,
passing it to a function or storing it in another container does not copy
it: both names refer to the same container, and a change through one is
seen through the other.

```wrench
var a[] = { 1, 2, 3 };
var b = a;
b[0] = 99;
println( a[0] );

var h = { "x": 1 };
var list[] = { h };
list[0].x = 5;
println( h.x );
```

Output:

```text
99
5
```

Assigning a new container to a variable replaces what the variable refers
to; other names keep the old container. Strings are shared the same way,
see [Strings](strings.md#strings-are-shared).

There is no copy operator. To copy, build a new container in a loop, or
use the serializer for a deep copy:

```wrench
var a[] = { 1, { 2, 3 } };
var copy = std::deserialize( std::serialize( a ) );
copy[1][0] = 9;
println( a[1][0], " ", copy[1][0] );
```

Output:

```text
2 9
```

`==` compares containers by content: two arrays with equal elements are
equal even when they are two arrays.

Containers are freed by the garbage collector when nothing refers to them
any more; a container may hold itself.

## Values that are not containers

Assigning to an element or member creates the container when the variable
holds none yet. Reading never does: an element or member of a value that
is no container is 0, and the value is left alone.

```wrench
var h;
h.speed = 5;      // h becomes a hash table
var a;
a[2] = 1;         // a becomes an array of 3
var s = "abc";
var n = s.length; // 0, s is still "abc"
println( h.speed, " ", a._count, " ", n, " ", s );
```

Output:

```text
5 3 0 abc
```

> [!WARNING]
> An assignment to a member or element of a variable that holds a number
> or a string replaces the value with a new container: after
> `var n = 5; n.k = 1;`, `n` is a hash table and the 5 is gone.

## Intrinsics

These look like members but are operators of the compiler. They work on
any expression, also on the result of a call.

| Intrinsic | Gives |
|---|---|
| `x._count` | the number of elements of an array, entries of a hash table, characters of a string, members of a struct instance; 0 for a number |
| `x._exists( key )` | 1 when the hash table has the key, or the index is in range of the array or string, else 0 |
| `x._remove( key )` | removes an array element (by index) or a hash table entry (by key), and gives the container |
| `x._hash` | the 32 bit hash wrench uses for the value (for an int, the int itself) |

`str::strlen()` is the library way to get a string's length.
