# Containers: array, hash, list, queue, stack

The container library adds functions to change arrays and hash tables in
place (insert, remove, truncate, clear) and uses arrays to provide a list,
a queue (first in, first out) and a stack (last in, first out). Read
[Arrays and hash tables](../language/containers.md) first: it explains how
containers are created, indexed and iterated, and the built in members
`._count`, `._exists` and `._remove`, which need no library at all.

The host loads all five namespaces with `wr_loadContainerLib( w )` (or
`wr_loadAllLibs( w )`); in `wrench.json` the library is `container`. It
runs on every platform and needs no build flags.

```wrench
var jobs;
queue::clear( jobs );
queue::push( jobs, "read" );
queue::push( jobs, "parse" );
queue::push( jobs, "write" );
while( queue::count( jobs ) )
{
	println( "doing ", queue::pop( jobs ) );
}
```

Output:

```text
doing read
doing parse
doing write
```

## Functions

| Function | Arguments | Returns | Summary |
|---|---|---|---|
| `array::clear` | `a [, size]` | the array | make `a` a new array of `size` zeros (default 0) |
| `array::count` | `a` | int | number of elements |
| `array::insert` | `a, where [, count]` | the array | insert `count` zeros (default 1) before `where` |
| `array::remove` | `a, where [, count]` | the array | remove `count` elements (default 1) from `where` |
| `array::truncate` | `a, size` | the array | cut the array to `size` elements |
| `hash::clear` | `h` | the table | make `h` a new empty hash table |
| `hash::count` | `h` | int | number of entries |
| `hash::add` | `h, item, key` | the item | store `item` under `key` |
| `hash::exists` | `h, key` | int | 1 if `key` is present, else 0 |
| `hash::remove` | `h, key` | int | remove `key`; 1 if it was present, else 0 |
| `list::clear` | `l` | the list | make `l` a new empty list |
| `list::count` | `l` | int | number of elements |
| `list::push`, `list::push_front` | `l, item` | nothing | insert `item` at the front |
| `list::push_back` | `l, item` | nothing | append `item` at the back |
| `list::peek` | `l` | element | the front element, without removing it |
| `list::pop`, `list::pop_front` | `l` | element | remove and return the front element |
| `list::pop_back` | `l` | element | remove and return the back element |
| `queue::clear` | `q` | the queue | make `q` a new empty queue |
| `queue::count` | `q` | int | number of elements |
| `queue::push` | `q, item` | nothing | add `item` |
| `queue::pop` | `q` | element | remove and return the oldest element |
| `queue::peek` | `q` | element | the oldest element, without removing it |
| `stack::clear` | `s` | the stack | make `s` a new empty stack |
| `stack::count` | `s` | int | number of elements |
| `stack::push` | `s, item` | nothing | push `item` |
| `stack::pop` | `s` | element | remove and return the newest element |
| `stack::peek` | `s` | element | the newest element, without removing it |

## How the functions treat their arguments

* The first argument is the container, and the functions change it in
  place: every variable that refers to the same container sees the
  change. The functions that return "the array" (or table, list and so
  on) return that same container, not a copy.
* The `clear` functions are the exception: they make the variable passed
  to them a new, empty container, whatever it held before (an int, a
  string, an array of another kind). Other variables that referred to the
  old container keep it. This is also how you create a list, queue or
  stack: declare a variable and clear it.
* Every other function does nothing when the first argument is not the
  right kind of container (an array of values for `array::`, `list::`,
  `queue::` and `stack::`, a hash table for `hash::`), or when arguments
  are missing, and returns 0. A string is an array of characters, not of
  values: `array::count( "abc" )` is 0; use `"abc"._count`.
* Popping or peeking an empty container returns 0. If 0 can be a real
  element, check the count first.
* Pushed and added items are stored as values: changing the variable you
  pushed afterwards does not change the stored element.

## array

### array::clear

`array::clear( a [, size] )` makes the variable `a` a new array of `size`
elements, all the int 0; without `size` the array is empty. A negative
`size`, or one above `WRENCH_MAX_ARRAY_ELEMENTS` (2097151 by default), is
treated as 0. It returns the new array.

### array::count

`array::count( a )` returns the number of elements of the array `a`, the
same as `a._count`, and 0 for anything that is not an array of values.

### array::insert

`array::insert( a, where [, count] )` inserts `count` elements (default
1), all the int 0, before index `where`, moving the later elements up. A
`where` past the end appends. A negative `where`, or a `count` of 0 or
less, changes nothing. Assign the new elements afterwards.

### array::remove

`array::remove( a, where [, count] )` removes `count` elements (default 1)
starting at index `where` and moves the later elements down. A `count`
reaching past the end removes everything from `where` on. A `where` past
the end (a negative one counts as past the end) or a `count` of 0 or less
changes nothing. The built in member `a._remove( where )` removes one
element the same way.

### array::truncate

`array::truncate( a, size )` cuts the array to `size` elements. It never
grows an array: a `size` at or above the current count changes nothing,
and so does a call without `size`.

```wrench
function show( a )
{
	var text = "";
	for( var i = 0; i < a._count; ++i )
	{
		text += " " + a[i];
	}
	println( "[", text, " ] count ", array::count( a ) );
}
var a[] = { 10, 20, 30, 40, 50 };
array::remove( a, 1 );
show( a );
array::remove( a, 1, 2 );
show( a );
array::insert( a, 1 );
show( a );
array::insert( a, 0, 2 );
show( a );
array::insert( a, 99 );
show( a );
array::truncate( a, 3 );
show( a );
array::clear( a, 4 );
show( a );
array::clear( a );
show( a );
```

Output:

```text
[ 10 30 40 50 ] count 4
[ 10 50 ] count 2
[ 10 0 50 ] count 3
[ 0 0 10 0 50 ] count 5
[ 0 0 10 0 50 0 ] count 6
[ 0 0 10 ] count 3
[ 0 0 0 0 ] count 4
[ ] count 0
```

Out of range arguments, and the other kinds of value:

```wrench
var b[] = { 1, 2, 3 };
array::remove( b, 5 );
array::remove( b, -1 );
array::remove( b, 0, 0 );
array::insert( b, -1 );
array::truncate( b, 10 );
println( array::count( b ) );
array::remove( b, 1, 100 );
println( array::count( b ), " ", b[0] );
println( array::count( 5 ), " ", array::count( "abc" ), " ", "abc"._count );
var n = 5;
array::clear( n, 2 );
println( array::count( n ) );
```

Output:

```text
3
1 1
0 0 3
2
```

The returned array is the same array:

```wrench
var x[] = { 1, 2 };
var y = array::insert( x, 1 );
y[1] = 9;
println( x[0], " ", x[1], " ", x[2] );
```

Output:

```text
1 9 2
```

## hash

### hash::clear

`hash::clear( h )` makes the variable `h` a new, empty hash table and
returns it.

### hash::count

`hash::count( h )` returns the number of entries, the same as `h._count`;
0 for anything that is not a hash table.

### hash::add

`hash::add( h, item, key )` stores `item` under `key`, replacing an entry
with the same key, and returns the stored item. Note the order: the item
comes before the key. It is the same as `h[key] = item`. Keys are ints,
floats and strings (a key is found by its hash, see
[Arrays and hash tables](../language/containers.md)); a float key is
keyed by the float, so `hash::add( h, item, 2.0 )` and `h[2]` are
different entries.

When the table is full (see [Limits](../reference/limits.md)) and `key`
is not in it yet, `hash::add` stops the script with
`WR_ERR_hash_table_size_exceeded`, like an assignment does.

### hash::exists

`hash::exists( h, key )` returns 1 if the table has an entry for `key`,
otherwise 0, the same as `h._exists( key )`.

### hash::remove

`hash::remove( h, key )` removes the entry for `key` and returns 1, or
returns 0 if there was none.

```wrench
var h = { "a": 1, "b": 2 };
println( hash::count( h ) );
hash::add( h, 30, "c" );
println( hash::count( h ), " ", h["c"] );
hash::add( h, 31, "c" );
println( hash::count( h ), " ", h["c"] );
hash::add( h, "five", 5 );
println( h[5] );
println( hash::exists( h, "a" ), " ", hash::exists( h, "zz" ) );
println( hash::remove( h, "a" ), " ", hash::remove( h, "a" ), " ", hash::count( h ) );
hash::clear( h );
println( hash::count( h ) );
```

Output:

```text
2
3 30
3 31
five
1 0
1 0 3
0
```

```wrench
var cfg;
hash::clear( cfg );
hash::add( cfg, 115200, "baud" );
println( cfg.baud, " ", hash::count( cfg ) );
println( hash::count( 5 ), " ", hash::exists( 5, 1 ), " ", hash::add( cfg, 1 ) );
```

Output:

```text
115200 1
0 0 0
```

Float keys:

```wrench
var m;
hash::clear( m );
hash::add( m, "two", 2 );
hash::add( m, "two point oh", 2.0 );
hash::add( m, "half", 0.5 );
println( hash::count( m ), " [", m[2], "] [", m[2.0], "] [", m[0.5], "] ", hash::exists( m, 0.5 ) );
```

Output:

```text
3 [two] [two point oh] [half] 1
```

## list, queue and stack

A list, a queue and a stack are ordinary arrays; the functions only decide
at which end elements go in and come out. The element at index 0 is the
front. You can index, iterate and `._count` them like any array, and the
`array::` functions work on them too.

Inserting at the front moves every element, so `push` and `push_front` on
a long list cost time proportional to its length. Removing from the back
is cheap.

| | Goes in at | Comes out at | `peek` shows |
|---|---|---|---|
| `list::push`, `push_front` | front | | |
| `list::push_back` | back | | |
| `list::pop`, `pop_front` | | front | front (`list::peek`) |
| `list::pop_back` | | back | |
| `queue::push` / `queue::pop` | front | back | back, the oldest |
| `stack::push` / `stack::pop` | front | front | front, the newest |

### list

`list::push( l, item )` and `list::push_front( l, item )` insert `item` at
the front, `list::push_back( l, item )` appends it. `list::pop( l )` and
`list::pop_front( l )` remove and return the front element,
`list::pop_back( l )` the back one. `list::peek( l )` returns the front
element without removing it, and `list::count( l )` the number of
elements.

```wrench
function show( a )
{
	var text = "";
	for( var i = 0; i < a._count; ++i )
	{
		text += " " + a[i];
	}
	println( "[", text, " ]" );
}
var l;
list::clear( l );
list::push( l, 1 );
list::push_front( l, 0 );
list::push_back( l, 2 );
list::push_back( l, 3 );
show( l );
println( list::count( l ), " ", list::peek( l ) );
println( list::pop( l ), " ", list::pop_front( l ), " ", list::pop_back( l ) );
show( l );
```

Output:

```text
[ 0 1 2 3 ]
4 0
0 1 3
[ 2 ]
```

### queue

A queue hands elements out in the order they went in. `queue::push( q,
item )` adds an item, `queue::pop( q )` removes and returns the oldest,
`queue::peek( q )` returns the oldest without removing it. In the array
the newest element is at index 0 and the oldest at the end.

```wrench
var q;
queue::clear( q );
queue::push( q, "first" );
queue::push( q, "second" );
queue::push( q, "third" );
println( queue::peek( q ), " ", queue::count( q ) );
println( queue::pop( q ), " ", queue::pop( q ), " ", queue::count( q ) );
println( q[0] );
```

Output:

```text
first 3
first second 1
third
```

### stack

A stack hands out the newest element first. `stack::push( s, item )` adds
an item, `stack::pop( s )` removes and returns the newest,
`stack::peek( s )` returns the newest without removing it. The newest
element is at index 0.

```wrench
var s;
stack::clear( s );
stack::push( s, "a" );
stack::push( s, "b" );
stack::push( s, "c" );
println( stack::peek( s ), " ", s[0], " ", stack::count( s ) );
println( stack::pop( s ), " ", stack::pop( s ), " ", stack::count( s ) );
```

Output:

```text
c c 3
c b 1
```

### Empty containers and uncleared variables

Popping or peeking an empty container returns 0. Pushing onto a variable
that is not an array does nothing: clear it first.

```wrench
var e;
stack::clear( e );
println( stack::pop( e ), " ", stack::peek( e ), " ", queue::pop( e ), " ", list::pop_back( e ) );
var v = 5;
stack::push( e, v );
v = 6;
println( stack::peek( e ) );
var u;
stack::push( u, 1 );
println( u, " ", stack::count( u ) );
```

Output:

```text
0 0 0 0
5
0 0
```
