# Passing data: containers, globals, user data

Arguments and return values carry data through calls
([Calling script functions](calling.md)). This page covers the other ways
data moves between the host and scripts: globals the host reads and writes,
containers the host builds around its own memory, pointers to host objects
that natives can reach, struct instances made by the host, copies between
contexts, serialization and the `msg::` mailbox.

## A complete example

The pieces below are explained one by one after it.

```cpp
#include "wrench.h"

#include <stdio.h>
#include <string.h>

static void print( WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr )
{
	char buf[256];
	for ( int i = 0; i < argn; ++i )
	{
		printf( "%s", argv[i].asString(buf, sizeof(buf)) );
	}
}

// the application object every native can reach through the state
struct App
{
	int ticks;
};

// tick(): counts in the App the state carries
static void tick( WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr )
{
	App* app = (App*)wr_getStateContext( c->w );
	retVal.setInt( ++app->ticks );
}

// whoami(): the name the host gave this context
static void whoami( WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr )
{
	const char* name = (const char*)wr_getLocalContext( c );
	wr_makeString( c, &retVal, name ? name : "?" );
}

static WRContext* load( WRState* w, const char* source, bool run )
{
	unsigned char* bytecode;
	int size;
	WRstr message;
	if ( wr_compile(source, (int)strlen(source), &bytecode, &size, &message) != WR_ERR_None )
	{
		printf( "%s", message.c_str() );
		return 0;
	}
	WRContext* context = wr_newContext( w, bytecode, size, true );
	if ( context && run )
	{
		wr_executeContext( context );
	}
	return context;
}

static const char* configScript =
	"blank limit;\n" // set by the host before the global code runs
	"var scale = 1;\n"
	"var readings[] = { 1, 2, 3 };\n"
	"var status = 0;\n"
	"print( whoami(), \": limit \", limit, \", scale \", scale, \"\\n\" );\n"
	"function report( sensor )\n"
	"{\n"
	"	print( sensor.name, \" \", sensor.value, \" raw[0] \", sensor.raw[0], \"\\n\" );\n"
	"	sensor.value = sensor.value * 2;\n"
	"	sensor.raw[1] = 0x55;\n"
	"	status = tick();\n"
	"	return readings._count;\n"
	"}\n"
	"struct Point( x, y ) { var sum = x + y; }\n";

int main()
{
	App app = { 0 };
	WRState* w = wr_newState();
	wr_registerFunction( w, "print", print );
	wr_registerFunction( w, "tick", tick );
	wr_registerFunction( w, "whoami", whoami );
	wr_setStateContext( w, &app );

	// globals: seed one before the global code runs
	WRContext* context = load( w, configScript, false );
	wr_setLocalContext( context, (void*)"config" );
	WRValue* limit = wr_getGlobalRef( context, "limit" );
	WRValue* scale = wr_getGlobalRef( context, "scale" );
	limit->setInt( 40 );
	scale->setInt( 99 ); // overwritten: the global code assigns 1
	wr_executeContext( context );

	// a container built by the host, passed as an argument
	char raw[4] = { 1, 2, 3, 4 };
	WRValue value;
	value.setInt( 21 );
	WRValue sensor;
	wr_makeContainer( &sensor );
	wr_addIntToContainer( &sensor, "id", 7 );
	wr_addValueToContainer( &sensor, "value", &value ); // the script sees the host's WRValue
	wr_addArrayToContainer( &sensor, "raw", raw, sizeof(raw) );
	WRValue name;
	wr_makeString( context, &name, "probe" );
	wr_addValueToContainer( &sensor, "name", &name );

	WRValue* count = wr_callFunction( context, "report", &sensor, 1 );
	printf( "report returned %d, value now %d, raw[1] now 0x%02X, id %d\n",
			count ? count->asInt() : -1, value.asInt(), (unsigned char)raw[1],
			wr_getValueFromContainer(sensor, "id")->asInt() );
	wr_destroyContainer( &sensor );

	// reading and changing globals
	WrenchValue readings( context, "readings" );
	printf( "readings has %d, status %d, ticks %d\n", readings.arraySize(),
			wr_getGlobalRef(context, "status")->asInt(), app.ticks );
	readings[5].setInt( 60 ); // grows the array
	printf( "readings has %d now\n", readings.arraySize() );

	// a struct instanced by the host
	WRValue point;
	WRValue xy[2];
	xy[0].setInt( 3 );
	xy[1].setInt( 4 );
	if ( wr_instanceStruct(&point, context, "Point", xy, 2) )
	{
		printf( "Point( 3, 4 ).sum = %d\n", point.indexStruct("sum")->asInt() );
	}

	// a value copied into a second context
	WRContext* other = load( w, "var copy; function show() { print( whoami(), \": \", copy[0], \" \", copy._count, \"\\n\" ); }", true );
	wr_setLocalContext( other, (void*)"other" );
	wr_copyValue( other, *wr_getGlobalRef(other, "copy"), *wr_getGlobalRef(context, "readings") );
	wr_destroyContext( context );
	wr_callFunction( other, "show" );

	wr_destroyState( w );
	return 0;
}
```

Output:

```text
config: limit 40, scale 1
probe 21 raw[0] 1
report returned 3, value now 42, raw[1] now 0x55, id 7
readings has 3, status 1, ticks 1
readings has 6 now
Point( 3, 4 ).sum = 7
other: 1 6
```

## Globals

```cpp
WRValue* wr_getGlobalRef( WRContext* context, const char* label );
```

returns a pointer to a global variable of the context, by name (`"limit"`
or `"::limit"`), or null when there is none. The pointer is the variable
itself: writing through it changes what the script sees, and it stays valid
for the life of the context. It works only for images compiled with
`WR_INCLUDE_GLOBALS` (the default of `wr_compile()` and of the `wrench`
tool; `-noglobals` leaves the names out), and does not find `private`
globals.

The `WrenchValue` helper wraps the same pointer and converts on access:
`readings[5]` above grows the script's array to six elements
([The WrenchValue helper](values.md#the-wrenchvalue-helper)).

A global that holds an array or hash table can be read and changed through
`indexArray()` and `indexHash()` like any value ([Values](values.md)).

### Seeding globals before the global code runs

Create the context with `wr_newContext()`, write the globals, then run
`wr_executeContext()`. Only a global declared with `blank` keeps what the
host stored; `blank` also makes an initializer a compile error, which
documents that the host fills it in. Any `var` sets its global when the
global code runs: `var scale = 1;` sets 1 (`scale` above) and `var count;`
sets 0.

```wrench
blank limit; // set by the host
var scale = 1; // set by the script, whatever the host stored
var count; // 0, whatever the host stored
```

## Containers built by the host

```cpp
WRValue& wr_makeContainer( WRValue* val, const uint16_t sizeHint =0 );
void wr_destroyContainer( WRValue* val );

void wr_addValueToContainer( WRValue* container, const char* name, WRValue* value );
void wr_addArrayToContainer( WRValue* container, const char* name, char* array, const uint32_t size );
void wr_addFloatToContainer( WRValue* container, const char* name, const float f );
void wr_addIntToContainer( WRValue* container, const char* name, const int i );

WRValue* wr_getValueFromContainer( WRValue const& container, const char* name );
```

A host container is a hash table that belongs to the host, not to a
context: no garbage collector frees it, and you free it with
`wr_destroyContainer()`. Scripts use it like a struct (`sensor.value`) or a
hash table (`sensor["value"]`). Its members can be:

* an int or a float, stored in the container (`wr_addIntToContainer()`,
  `wr_addFloatToContainer()`);
* a `WRValue` of yours (`wr_addValueToContainer()`): the container refers
  to it, so the script reads and writes your variable (`value` became 42
  above). It must outlive the container;
* a raw byte array of yours (`wr_addArrayToContainer()`): the script indexes
  it like an array of bytes and writes go straight into your memory
  (`raw[1]` above). It must outlive the container too.

This is the cheapest way to expose a block of memory, a register file or a
configuration structure to scripts: nothing is copied.

`wr_getValueFromContainer()` returns a member by name, or null when the
container has no member of that name; looking does not add one.

> [!NOTE]
> A string member made with `wr_makeString()` belongs to the context it was
> made in, and that context's garbage collector does not see references
> from host containers. Make such strings right before the call that uses
> them, as above, or keep them alive with `wr_keepValue()` while the
> container holds them ([Strings and their lifetime](values.md#strings-and-their-lifetime)).

## User pointers

```cpp
void wr_setStateContext( WRState* w, void* ctx );
void* wr_getStateContext( WRState* w );
void wr_setLocalContext( WRContext* c, void* ctx );
void* wr_getLocalContext( WRContext* c );
```

Each state and each context carries one pointer for your own use; wrench
never touches it. A native reaches the state's pointer through
`wr_getStateContext( c->w )` (or `c->w->ctx`) and the calling context's
through `wr_getLocalContext( c )` (or `c->ctxLocal`). Library functions,
which have no `usr` argument, use these. Typical uses: the application
object on the state, and on each context the device or the session it
scripts.

## Structs made by the host

```cpp
WRValue* wr_instanceStruct( WRValue* val, WRContext* context, const char* name, const WRValue* argv =0, const int argn =0 );
```

creates an instance of the script's struct `name`, as `new name( ... )`
would, passing `argv` to its constructor parameters. It returns `val`, or
null when the context has no such struct. The context's global code must
have run. The instance is kept as if by `wr_keepValue()`: it is not
collected while the context lives (it is freed when the context is
destroyed or reset), so the host can keep it in a C++ variable and pass it
to script functions repeatedly. `wr_releaseValue()` lets the collector free
it once nothing in the script refers to it. Read its members with
`indexStruct()`.

## Values from another context

Strings, arrays and hash tables belong to the context that created them and
are freed by its garbage collector. Two contexts of a state do not share
them (a context and the modules it imported do). Before handing such a
value to another context, as an argument or by storing it in that context's
globals, copy it:

```cpp
bool wr_copyValue( WRContext* context, WRValue& dest, const WRValue& src );
```

makes `dest` a deep copy of `src` owned by `context`. Ints and floats are
copied as they are. It returns false, and clears `dest`, for what cannot be
copied: a struct instance, a raw host array, an array of more than 65535
elements, a container that contains itself. In the example the copy in
`other` survives the destruction of the context it came from.

## Serialization

```cpp
bool wr_serialize( char** buf, int* len, const WRValue& value );
bool wr_deserialize( WRContext* context, WRValue& value, const char* buf, const int len );
```

`wr_serialize()` turns a value, containers included, into a byte buffer you
can store or send; free the buffer with `wr_free()`. `wr_deserialize()`
rebuilds the value in a context; malformed or truncated input makes it
return false. The format is endian neutral. Scripts have the same through
`std::serialize` and `std::deserialize` ([Serialization](../library/serialize.md)).
`wr_copyValue()` is a serialize and deserialize in one.

## The msg:: mailbox

The `msg::` library ([Messages and time](../library/msg-time.md)) keeps a
table of keyed values in the state, so contexts of one state, and the host,
can leave values for each other: `msg::write( "key", value )` in one,
`msg::read( "key" )` in another (see the example in
[running code](running.md#several-contexts-in-one-state)).

The mailbox holds ints, floats and strings as they are. A string is
copied, and the copy belongs to the state, not to the context that wrote
it: it lives until the message is overwritten or cleared, or the state is
destroyed. Containers and struct instances are not stored (`msg::write`
returns -1 for them); pass richer data through globals, arguments or
`wr_copyValue()`.

The host reaches the mailbox with

```cpp
bool wr_msgWrite( WRState* w, const char* key, const WRValue& value );
bool wr_msgWriteString( WRState* w, const char* key, const char* data, const int len =-1 );
const WRValue* wr_msgPeek( WRState* w, const char* key );
bool wr_msgRead( WRContext* context, const char* key, WRValue& value, const bool clear =false );
bool wr_msgClear( WRState* w, const char* key );
```

| Function | Does |
|---|---|
| `wr_msgWrite()` | stores an int, a float or a string; false for anything else, or when out of memory, and the mailbox is unchanged then |
| `wr_msgWriteString()` | stores `len` bytes of `data` as a string, -1 for up to its terminating zero |
| `wr_msgPeek()` | the stored value, or null. A string reads with `array( &len )` and is zero terminated; the pointer is valid until the mailbox changes |
| `wr_msgRead()` | the value as written, a string as a new string of `context` (like `wr_makeString()`); false, and an int 0, when there is none. `clear` removes the message |
| `wr_msgClear()` | removes the message; false when there was none |

`key` is the string a script uses as the key: the host's `"mode"` is the
script's `msg::read( "mode" )`. None of these functions is thread safe;
call them from the thread that runs the state.

```cpp
// the host leaves values for the scripts
WRValue mode;
mode.setInt( 3 );
wr_msgWrite( w, "mode", mode );
wr_msgWriteString( w, "name", "pump" );

// and reads what a script left with msg::write( "status", "ready" )
const WRValue* status = wr_msgPeek( w, "status" );
unsigned int len = 0;
const char* text = status ? (const char*)status->array( &len ) : 0;
printf( "status %s (%u bytes)\n", text ? text : "none", len );
wr_msgClear( w, "status" );
```

## Choosing a way

| You want to | Use |
|---|---|
| give a script one value per call | arguments of `wr_callFunction()` |
| configure a script before it starts | a `blank` global, set after `wr_newContext()` |
| read results a script keeps | `wr_getGlobalRef()`, `WrenchValue` |
| expose host memory without copying | a host container with `wr_addArrayToContainer()` / `wr_addValueToContainer()` |
| let natives reach host objects | the `usr` pointer, `wr_setStateContext()`, `wr_setLocalContext()` |
| hand a script a struct it defines | `wr_instanceStruct()` |
| move data between contexts | `wr_copyValue()`, or `msg::` for numbers and strings |
| store or transmit values | `wr_serialize()` / `wr_deserialize()` |
