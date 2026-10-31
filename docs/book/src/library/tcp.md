# Networking: tcp

The `tcp::` library gives scripts plain TCP/IP sockets over IPv4: listen
and accept, connect, send, receive and wait for data. Read it if a script
on a PC talks to another program or device over the network.

The host loads it with `wr_loadTCPLib( w )` (or `wr_loadAllLibs( w )`);
in `wrench.json` its name is `tcp`. It is only compiled in with one of
these build flags (see [Build flags](../reference/build-flags.md)):

| Flag | Platform | Notes |
|---|---|---|
| `WRENCH_WIN32_TCP` | Windows (Winsock 2) | `wr_loadTCPLib` calls `WSAStartup`; the library links `Ws2_32.lib` through a `#pragma` (MSVC) |
| `WRENCH_LINUX_TCP` | Linux and other POSIX systems | sends use `MSG_NOSIGNAL` where it exists, a closed peer does not raise `SIGPIPE` |
| neither | | `wr_loadTCPLib` registers nothing; calls stop the script with `WR_ERR_lib_function_not_found` |

There is no backend for Arduino, ESP32 or other microcontroller network
stacks; register natives of your own there (see
[Native functions and libraries](../embedding/natives.md)). `wrench.exe`
is built with `WRENCH_WIN32_TCP` on Windows and `WRENCH_LINUX_TCP` on
Linux.

> [!WARNING]
> `tcp::accept`, `tcp::connect` and `tcp::receive` block: the script, and
> the host thread running it, wait until they finish. Use `tcp::peek` with
> a timeout to wait for data without blocking forever, and call the
> blocking functions from a host that can afford to wait.

## Functions

| Function | Arguments | Returns | Summary |
|---|---|---|---|
| `tcp::bind` | `port` | socket or -1 | listen on `port` on all interfaces |
| `tcp::accept` | `socket` | socket or -1 | wait for and accept a connection |
| `tcp::connect` | `address, port` | socket or -1 | connect to a host |
| `tcp::send` | `socket, data` | int | send data; bytes sent or -1 |
| `tcp::receive` | `socket [, max]` | string or 0 | receive up to `max` bytes (default 8192) |
| `tcp::peek` | `socket [, timeoutMs]` | int | bytes waiting, 0 if none, -1 if closed or failed |
| `tcp::close` | `socket` | nothing | close a socket |

A socket is an int. Functions that create one return -1 when they fail;
check before using the result.

## A connection on one machine

This program plays both sides on the loopback address: it listens,
connects to itself, and passes data both ways. A connection to a
listening socket completes before `tcp::accept` is called, so one script
can do this without threads.

```wrench
var server = tcp::bind( 47811 );
var client = tcp::connect( "127.0.0.1", 47811 );
var conn = tcp::accept( server );
println( "sockets ok: ", server >= 0 && client >= 0 && conn >= 0 );
println( "waiting: ", tcp::peek( conn ) );
println( "sent: ", tcp::send( client, "ping" ) );
println( "waiting: ", tcp::peek( conn, 1000 ) );
println( "received: ", tcp::receive( conn ) );
tcp::send( conn, 12345 );
println( "in pieces: ", tcp::receive( client, 3 ), " ", tcp::receive( client, 100 ) );
tcp::close( client );
println( "after close: ", tcp::peek( conn, 1000 ), " ", tcp::receive( conn ) );
tcp::close( conn );
tcp::close( server );
```

Output:

```text
sockets ok: 1
waiting: 0
sent: 4
waiting: 4
received: ping
in pieces: 123 45
after close: -1 0
```

## tcp::bind

`tcp::bind( port )` creates a TCP socket, binds it to `port` on every
local IPv4 address and starts listening (backlog 5). It returns the
listening socket, or -1 when the port is in use or the socket cannot be
created. Without an argument it returns -1 and does nothing.

The socket is bound without `SO_REUSEADDR`, so on Linux a port that was
used a moment ago can be refused for a while after its server closed.

## tcp::accept

`tcp::accept( socket )` waits until a client connects to the listening
`socket` and returns the new connection's socket, or -1 on an error or
without an argument. It blocks until a connection arrives. Use the returned socket for `send` and
`receive`; the listening socket keeps accepting more.

## tcp::connect

`tcp::connect( address, port )` connects to `port` on `address` and
returns the socket, or -1 when the name cannot be resolved or the
connection is refused. `address` is a dotted IPv4 address (`"10.0.0.7"`)
or a host name, which is looked up (IPv4 results only). It blocks until
the connection is made or fails. With a missing argument or an empty
address it returns -1 and does nothing.

```wrench
println( tcp::connect( "127.0.0.1", 1 ), " ", tcp::connect( "no.such.host.invalid", 80 ) );
var s = tcp::bind( 47812 );
println( tcp::bind( 47812 ) );
tcp::close( s );
println( tcp::peek( -1 ), " ", tcp::send( -1, "x" ) );
println( tcp::connect( "", 80 ), " ", tcp::bind(), " ", tcp::accept(), " ", tcp::peek() );
```

Output:

```text
-1 -1
-1
-1 -1
-1 -1 -1 -1
```

## tcp::send

`tcp::send( socket, data )` sends `data` and returns the number of bytes
the system accepted, or -1 on an error (for example a closed connection).
A string is sent as its bytes, zero bytes included; any other value is
converted to text first, so `tcp::send( s, 12345 )` sends the five
characters `12345`. Send containers with
[`std::serialize`](serialize.md). With fewer than two arguments it
returns 0 and sends nothing.

The data goes out in one `send()` call. For large amounts compare the
result with the length and send the rest.

## tcp::receive

`tcp::receive( socket [, max] )` waits for data and returns what arrived,
at most `max` bytes, as a string. `max` defaults to 8192; values of 0 or
less mean 8192 as well, and it is capped at 1 MB. The call returns as
soon as some data is there, so a message can arrive in pieces, or several
messages in one string: TCP is a stream, not a sequence of messages.

It returns the int 0 when the peer has closed the connection, or on an
error. There is no timeout: on a connection where nothing arrives it
waits forever. Call `tcp::peek` first if the script must not hang.

## tcp::peek

`tcp::peek( socket [, timeoutMs] )` waits up to `timeoutMs` milliseconds
(default 0, return at once; negative waits without limit) for data, and
returns:

* the number of bytes that can be received without waiting, when there
  are some;
* 0 when nothing arrived within the timeout;
* -1 when the connection was closed by the peer, the socket is invalid or
  an error occurred.

Without an argument it returns -1, like for an invalid socket.

<!-- check:skip -->
```wrench
// fragment: poll a connection 'conn' without blocking the script for long
var n = tcp::peek( conn, 50 );
if ( n > 0 )
{
	var data = tcp::receive( conn, n );
	println( "got ", data._count, " bytes" );
}
else if ( n < 0 )
{
	println( "connection closed" );
	tcp::close( conn );
}
```

## tcp::close

`tcp::close( socket )` closes the socket (`closesocket` on Windows,
`close` on Linux) and returns nothing. Close accepted and connected
sockets as well as listening ones: the library does not close sockets
when a script or state ends.

## Sending structured data

`std::serialize` turns a hash table into bytes that `tcp::send` can carry
and `std::deserialize` rebuilds on the other side:

```wrench
var server = tcp::bind( 47813 );
var client = tcp::connect( "127.0.0.1", 47813 );
var conn = tcp::accept( server );
var reading = { "id": 7, "temp": 21.5, "tags": { "a", "b" } };
var bytes = std::serialize( reading );
println( "sent ", tcp::send( client, bytes ), " of ", bytes._count );
var got = std::deserialize( tcp::receive( conn ) );
println( got.id, " ", got.temp, " ", got.tags[1] );
tcp::close( client );
tcp::close( conn );
tcp::close( server );
```

Output:

```text
sent 62 of 62
7 21.5 b
```

This works here because the data arrives in one piece. Over a real
network, send the length first (or end each message with a marker) and
receive until the whole message is there before deserializing it.
