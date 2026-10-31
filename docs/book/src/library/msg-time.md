# Messages and time: msg, time

This page covers two small libraries: `msg::`, a mailbox of keyed values
that every context of a state shares, and `time::ms`, a millisecond
counter. Read it when scripts running side by side must signal each
other, or when a script measures time.

| Namespace | Loaded by | `wrench.json` name | Platforms |
|---|---|---|---|
| `msg::` | `wr_loadMessageLib( w )` | `msg` | all |
| `time::ms` | `wr_loadStdLib( w )` | `std` | Arduino, ESP-IDF, STM32 HAL, Windows, Linux, macOS; 0 elsewhere |

Both are part of `wr_loadAllLibs( w )`.

## Functions

| Function | Arguments | Returns | Summary |
|---|---|---|---|
| `msg::write` | `key, value` | int | store an int, float or string under `key`; 0, or -1 when nothing was stored |
| `msg::read` | `key [, clear]` | int, float or string | the value under `key`, or 0; removes it if `clear` is true |
| `msg::peek` | `key` | int | 1 if there is a value under `key`, else 0 |
| `msg::clear` | `key` | nothing | remove the value under `key` |
| `time::ms` | none | int | milliseconds from a running counter |

## msg: a mailbox shared by contexts

A host can run several scripts in one state: each `wr_run()` makes a
context with its own globals, and the contexts run in turn (see
[States, contexts and running code](../embedding/running.md) and
[Yield and cooperative tasks](../language/yield.md)). Contexts cannot see
each other's variables. The `msg::` functions give them one place they all
see: a table of messages that belongs to the state. One context writes a
value under a key, another peeks or reads it.

Typical uses are flags, numbers and short texts: "new data ready", a
mode, a counter, a request code, a name. The table is the `mailbox` member
of the `WRState`. The host reaches the same messages with `wr_msgWrite()`,
`wr_msgWriteString()`, `wr_msgPeek()`, `wr_msgRead()` and `wr_msgClear()`
(see [The msg:: mailbox](../embedding/data.md#the-msg-mailbox)), with the
key given as a string.

```wrench
println( msg::peek( "mode" ), " ", msg::read( "mode" ) );
msg::write( "mode", 3 );
println( msg::peek( "mode" ), " ", msg::read( "mode" ), " ", msg::peek( "mode" ) );
println( msg::read( "mode", true ), " ", msg::peek( "mode" ) );
msg::write( 7, 0 );
println( msg::peek( 7 ), " ", msg::read( 7 ) );
msg::clear( 7 );
println( msg::peek( 7 ) );
```

Output:

```text
0 0
1 3 1
3 0
1 0
0
```

### Keys and values

A key can be any value; it is reduced to its hash, so the string `"mode"`
is the same key in every context, and an int is its own key.

A message holds an int, a float or a string, and reads back as it was
written. A string is copied when it is written: the copy belongs to the
state, not to the context that wrote it, so it stays valid when that
context is gone. Each `msg::read` of a string returns a new string of the
reading context, which the reader may change without changing the
message. Arrays, hash tables and struct instances cannot be stored; pass
an index or a code that means something to both sides instead.

```wrench
println( msg::write( "gain", 2.5 ), " ", msg::write( "name", "probe" ) );
println( msg::read( "gain" ) * 2, " ", msg::read( "name" ) );
var s = msg::read( "name" );
s[0] = 'P';
println( s, " ", msg::read( "name" ) );
var list[] = { 1, 2 };
println( msg::write( "list", list ), " ", msg::peek( "list" ) );
```

Output:

```text
0 0
5 probe
Probe probe
-1 0
```

### msg::write

`msg::write( key, value )` stores `value` (as described above) under
`key`, replacing an earlier message with that key, and returns 0. When
`value` is an array, a hash table, a struct instance or a raw array, when
it is missing, or when there is no memory for a string, it stores nothing,
leaves an earlier message under `key` as it was, and returns -1.

### msg::read

`msg::read( key [, clear] )` returns the message stored under `key`, or 0
when there is none. When `clear` is true (non-zero) the message is removed
after it is read, so the next reader finds nothing; the default is to
leave it. Because a stored 0 and a missing message both read as 0, use
`msg::peek` when the difference matters.

### msg::peek

`msg::peek( key )` returns 1 when a message is stored under `key`, 0
otherwise.

### msg::clear

`msg::clear( key )` removes the message under `key`; a missing key is not
an error.

### Two contexts

In this host, a sensor script and a display script run in separate
contexts of one state and talk through the mailbox:

```cpp
#include "wrench.h"
#include <stdio.h>
#include <string.h>

static void println( WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr )
{
	char buf[256];
	for( int i = 0; i < argn; ++i )
	{
		printf( "%s", argv[i].asString( buf, sizeof(buf) ) );
	}
	printf( "\n" );
}

static WRContext* load( WRState* w, const char* source )
{
	unsigned char* code;
	int len;
	if ( wr_compile( source, (int)strlen(source), &code, &len ) != WR_ERR_None )
	{
		return 0;
	}
	return wr_run( w, code, len, true ); // runs the global code, keeps the context
}

int main()
{
	WRState* w = wr_newState();
	wr_loadMessageLib( w );
	wr_registerFunction( w, "println", println );

	WRContext* sensor = load( w,
		"function sample( v ) { msg::write( \"level\", v ); }" );
	WRContext* display = load( w,
		"function show() { if ( msg::peek( \"level\" ) ) { println( \"level \", msg::read( \"level\", true ) ); } else { println( \"no new level\" ); } }" );

	WRValue arg;
	wr_makeInt( &arg, 17 );
	wr_callFunction( sensor, "sample", &arg, 1 );
	wr_callFunction( display, "show" );
	wr_callFunction( display, "show" );

	wr_destroyState( w );
	return 0;
}
```

Output:

```text
level 17
no new level
```

> [!NOTE]
> The mailbox belongs to one state. Contexts of different states do not
> share it, and the mailbox is not protected against threads: when states
> run on several threads, each has its own mailbox anyway (see
> [Several states, threads and RTOS tasks](../embedding/threads.md)).

## time::ms

`time::ms()` returns a millisecond counter as an int. Take two readings
and subtract them to measure an interval; the counter's starting point
means nothing by itself.

```wrench
var start = time::ms();
var sum = 0;
for( var i = 0; i < 10000; ++i )
{
	sum += i;
}
var elapsed = time::ms() - start;
println( "sum ", sum, ", took less than a second: ", elapsed >= 0 && elapsed < 1000 );
```

Output:

```text
sum 49995000, took less than a second: 1
```

What the counter is depends on the platform:

| Platform (checked in this order) | Source | Counts from |
|---|---|---|
| Arduino (`ARDUINO`), any board, ESP32 with the Arduino core too | `millis()` | the start of the board |
| ESP-IDF without Arduino (`ESP_PLATFORM`) | `esp_timer_get_time() / 1000` | the start of the board |
| STM32 HAL (`USE_HAL_DRIVER`) | `HAL_GetTick()` | `HAL_Init()` |
| Windows (`_WIN32`) | `GetTickCount()`, resolution about 10 to 16 ms | the start of the system |
| Linux and macOS | `clock_gettime( CLOCK_MONOTONIC )`; setting the system clock does not move it | an unspecified point, usually the start of the system |
| anything else | none, always 0 | |

On a platform that is not in the table, register a native of your own
that returns a millisecond counter (see
[Native functions and libraries](../embedding/natives.md)): a script that
waits for a counter stuck at 0 to advance waits forever.

The value is 32 bits wide and wraps around (after about 49.7 days); read
as a wrench int it turns negative half way there. The difference of two
readings is still right, because int subtraction wraps the same way, as
long as the interval is shorter than about 24 days.
