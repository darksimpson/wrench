# Serialization

`std::serialize` turns a value, including nested arrays and hash tables,
into a string of bytes; `std::deserialize` rebuilds the value from those
bytes. Use them to save script state to a file, send structured data over
a link, or keep data across runs.

The host loads both with `wr_loadSerializeLib( w )` (or
`wr_loadAllLibs( w )`); in `wrench.json` the library is `serialize`. It
runs on every platform and needs no build flags. The same format is
available to C++ as `wr_serialize()` and `wr_deserialize()`, declared in
`wrench.h`.

```wrench
var settings = { "name": "probe", "rate": 50, "gain": 1.5, "pins": { 4, 5, 12 } };
var bytes = std::serialize( settings );
println( "serialized to ", bytes._count, " bytes" );
var copy = std::deserialize( bytes );
println( copy.name, " ", copy.rate, " ", copy.gain, " ", copy.pins[2], " ", copy.pins._count );
```

Output:

```text
serialized to 86 bytes
probe 50 1.5 12 3
```

## Functions

| Function | Arguments | Returns | Summary |
|---|---|---|---|
| `std::serialize` | `value` | string, or 0 | the value as bytes |
| `std::deserialize` | `bytes` | value, or 0 | the value rebuilt from bytes |

## std::serialize

`std::serialize( value )` returns a string holding the bytes of `value`.
The string is binary: it contains zero bytes and is not meant to be
printed, but it can be written with [`io::writeFile`](io.md#iowritefile),
sent with [`tcp::send`](tcp.md#tcpsend) or stored anywhere a string goes.

What can be serialized:

* ints and floats;
* strings of up to 65535 characters;
* arrays of up to 65535 elements, whose elements are any of these;
* hash tables, whose keys and values are any of these.

Containers can nest up to 64 levels (`WRENCH_MAX_SERIAL_DEPTH`, a build
setting). Everything else makes the whole call fail and return the int 0:
a struct instance, a raw array or other host data, a container that
contains itself, a string or array that is too long, nesting that is too
deep, or a call without an argument.

```wrench
struct Point
{
	var x;
	var y;
};
var p = new Point;
var big[70000];
var self[] = { 1 };
self[0] = self;
println( std::serialize( p ), " ", std::serialize( big ), " ", std::serialize( self ), " ", std::serialize() );
println( std::deserialize( "garbage" ), " ", std::deserialize( "" ), " ", std::deserialize( 5 ), " ", std::deserialize() );
```

Output:

```text
0 0 0 0
0 0 0 0
```

To save a struct, copy its members into a hash table first.

## std::deserialize

`std::deserialize( bytes )` rebuilds the value that `std::serialize`
turned into `bytes` and returns it: the same types, the same nesting, the
same keys. Hash tables are rebuilt by inserting every entry, so their
counts and lookups are right.

It returns the int 0 when `bytes` is not a string, is empty, is not
serialized data, or is cut short or damaged. Every length in the input is
checked before memory is allocated, so a broken or hostile input fails
cleanly instead of crashing or allocating huge buffers. Extra bytes after
the value are ignored.

Because the int 0 is also a valid serialized value, a result of 0 alone
does not prove failure; serialize a container (a hash table of settings,
for example) and check that the result is one.

```wrench
println( std::deserialize( std::serialize( 42 ) ), " ", std::deserialize( std::serialize( -2.25 ) ), " ", std::deserialize( std::serialize( "text" ) ) );
var nested[] = { 1, { 2, 3 }, "s" };
var n2 = std::deserialize( std::serialize( nested ) );
println( n2[1][1], " ", n2[2] );
var h = { 1: "one", "k": 2 };
var h2 = std::deserialize( std::serialize( h ) );
println( h2[1], " ", h2.k, " ", hash::count( h2 ) );
```

Output:

```text
42 -2.25 text
3 s
one 2 2
```

## Keeping state in a file

This program counts how often it has run; the state survives between runs
in `state.bin`:

```wrench
var state = { "runs": 1, "last": "boot" };
var old = io::readFile( "state.bin" );
if ( old )
{
	state = std::deserialize( old );
}
state.runs = state.runs + 1;
io::writeFile( "state.bin", std::serialize( state ) );
println( "runs: ", state.runs );
```

Output of the first and the second run:

```text
runs: 2
```

```text
runs: 3
```

[Networking: tcp](tcp.md) has an example that sends a serialized hash
table over a socket.

## The format

The format is small and fixed; you only need it to read or write the
bytes outside wrench. Every value starts with a type byte; numbers are
little endian on every platform, so data written on one machine reads
back on any other, whatever its byte order or word size.

| Value | Bytes |
|---|---|
| int | `0x00`, then the 32-bit value |
| float | `0x01`, then the 32-bit IEEE 754 single |
| string | `0x03 0xA0 0x05`, a 16-bit length, then the characters |
| array | `0x03 0xA0 0x04`, a 16-bit element count, then each element as a value |
| hash table | `0x03 0xE0`, a 16-bit slot count, then per slot `0x00` (empty) or `0x01` followed by the entry's value and then its key, both as values |

```wrench
function dump( s )
{
	var text = "";
	for( var i = 0; i < s._count; ++i )
	{
		text += " " + s[i];
	}
	println( s._count, " bytes:", text );
}
dump( std::serialize( 7 ) );
dump( std::serialize( 1.5 ) );
dump( std::serialize( "ab" ) );
var a[] = { 1, 2 };
dump( std::serialize( a ) );
```

Output:

```text
5 bytes: 0 7 0 0 0
5 bytes: 1 0 0 192 63
7 bytes: 3 160 5 2 0 97 98
15 bytes: 3 160 4 2 0 0 1 0 0 0 0 2 0 0 0
```

A hash table is written slot by slot, empty slots included, so its size
depends on the table's capacity as well as on its entries: a table of 100
ints serializes to 1198 bytes, one with a single entry to 18. The data
has no header or version number of its own; store one next to it if your
format may change.
