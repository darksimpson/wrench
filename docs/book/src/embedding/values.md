# Values: WRValue

Every value that crosses between the host and a script is a `WRValue`:
arguments of natives, arguments and results of calls, globals, elements of
arrays. This page shows how to read and write them, how strings, arrays
and hash tables look from C++, and how long the pointers you get stay
valid.

## What a WRValue is

A `WRValue` is two machine words: 8 bytes on a 32-bit target, 16 on a
64-bit one. It can be created on the stack freely; making an int or a float
allocates nothing. Its type is one of:

| Script type | Test | Read |
|---|---|---|
| integer (32 bit) | `isInt()` | `asInt()` |
| float (32 bit) | `isFloat()` | `asFloat()` |
| string | `isString( &len )` | `asString()`, `asMallocString()`, `array()` |
| array | `isWrenchArray( &len )` | `indexArray()`, iteration |
| hash table | `isHashTable( &count )` | `indexHash()`, iteration |
| struct instance | `isStruct()` | `indexStruct( "member" )` |
| raw host array | `isRawArray( &len )` | `array( &len, ... )` |

Values handed to natives can also be *references*: to a variable, or to an
element of an array or a hash table. That is how a script passes something
a native can change. The getters and the `is...()` tests all look through
references, so always use them instead of the data members.

> [!IMPORTANT]
> Never read the union members (`i`, `f`, `p`, `r`...) of a value you did
> not make yourself. They mean different things for different types and
> for references. Use the methods.

## Reading values

```cpp
int asInt() const;
float asFloat() const;
char* asString( char* string, unsigned int maxLen =0, unsigned int* strLen =0 ) const;
char* asMallocString( unsigned int* strLen =0 ) const;
char* technicalAsString( char* string, unsigned int maxLen, bool valuesInHex =false, unsigned int* strLen =0 ) const;
void* array( unsigned int* len =0, char arrayType =SV_CHAR ) const;
int arraySize() const;
uint32_t getHash() const;
WRValue& deref() const;
```

* `asInt()` and `asFloat()` convert between the two number types (a float
  is truncated). On a string, array or hash table they return its hash
  (see `getHash()`), not a number parsed from text, so test the type first.
* `asString()` writes a value as text into your buffer of `maxLen` bytes,
  terminator included, and returns the buffer. Numbers are formatted,
  strings copied (cut to fit); an array or hash table also comes out as its
  hash, use `technicalAsString()` or iterate to show the contents. `maxLen`
  0 means "unlimited"; avoid it.
* `asMallocString()` returns the text in a new buffer from `wr_malloc()`,
  sized to fit, which you free with `wr_free()`. The `MallocStrScoped`
  helper frees it for you at the end of the block:

  ```cpp
  WRValue::MallocStrScoped text( argv[0] );
  if ( text )
  {
  	fwrite( (const char*)text, 1, text.size(), stdout );
  }
  ```

* `array()` returns a pointer to the characters of a string (the default
  `SV_CHAR`), or null for anything else. The characters are not zero
  terminated; `len` receives the count. The pointer is valid while the
  string is.
* `technicalAsString()` prints a value the way a debugger would, contents of
  containers included (`[ 1, 2 ]`, `{ "a":1 }`, `"ab"`), for logs.
* `getHash()` is the hash the VM uses for the value: for an int its value,
  for a string the hash of its characters. Hash table keys are looked up
  with it.
* `deref()` gives the value a reference points to (or the value itself).
  Assigning `retVal = argv[0].deref();` copies a value instead of a
  reference.

## Writing values

```cpp
void setInt( const int I );
void setFloat( const float F );
void setString( WRContext* context, const char* data, const int len =0 );
WRValue* init(); // the integer 0

WRValue& wr_makeInt( WRValue* val, int i );
WRValue& wr_makeFloat( WRValue* val, float f );
WRValue& wr_makeString( WRContext* context, WRValue* val, const char* data, const int len =0 );
```

`setInt()` and `setFloat()` make a number and need nothing else. A string
lives in a context, so `setString()` and `wr_makeString()` need the context
that will use it: the one you pass it to, or, in a native, the context the
native was called from. `len` 0 means `strlen( data )`; with a length the
data may contain zero bytes.

Writing through a value a native received changes what the script sees
when the value is a reference: `argv[0].indexArray( c, 2, true )->setInt( 5 )`
changes element 2 of the script's array.

## Strings and their lifetime

A string made by the host is an object of its context like any string a
script makes, and the context's garbage collector frees it once nothing
refers to it. The collector sees the script's globals, the stack of a call
in progress and the containers reachable from those; it does not see
`WRValue`s in your C++ variables. So:

* a string you make and pass as an argument right away is safe for the
  duration of that call;
* a string returned to a script (`wr_makeString( c, &retVal, ... )` in a
  native) is safe, the script holds it;
* a string you keep in a C++ variable between calls may be freed by any
  later call into that context, unless you keep it alive with
  `wr_keepValue()`, store it in a global of the script, or make it again
  when you need it.

```cpp
bool wr_keepValue( const WRValue& value );
bool wr_releaseValue( const WRValue& value );
```

`wr_keepValue()` makes a string, array, hash table or struct instance a
root of its context's collector: it stays alive, with everything it refers
to, while the host holds it in its own variables. `wr_releaseValue()` lets
it go again, and the next collection frees it if nothing in the script
refers to it. Keeps are not counted, so one release undoes any number of
keeps. Destroying or resetting the context frees a kept value anyway. Both
return false, and do nothing, for a value that needs no keeping: an int, a
float, a raw host array, or a container made with `wr_makeContainer()`
(a string stored in such a container does need it).

The collector runs when a context has allocated more than the threshold
(`wr_setAllocatedMemoryGCHint()`, 4000 bytes by default) since its last
run, at the start of a call and when a script allocates.

## Arrays and hash tables

```cpp
WRValue* indexArray( WRContext* context, const uint32_t index, const bool create ) const;
WRValue* indexHash( WRContext* context, const uint32_t hash, const bool create ) const;
WRValue* indexStruct( const char* label ) const;
```

* `indexArray()` returns element `index` of an array, or null when the
  value is no array or the index is past the end. With `create` true it
  grows the array to reach `index`, and turns a value that is no array into
  one.
* `indexHash()` looks up the key whose `getHash()` is `hash` (for a string
  key, `wr_hashStr( "key" )`). With `create` true it adds the key, and turns
  a value that is no hash table into one.
* `indexStruct()` returns a member of a struct instance by name, or null.

Iteration walks the elements of an array or the entries of a hash table:

```cpp
for ( WRValue::Iterator it = v.begin(); it != v.end(); ++it )
{
	const WRIteratorEntry& e = *it;
	// arrays: e.index and e.value; hash tables: e.key and e.value;
	// strings: e.index and e.character
}
```

With C++11 the same loop is `for ( const WRIteratorEntry& e : v )`.

A complete program with natives that inspect, fill and look up values:

```cpp
#include "wrench.h"

#include <stdio.h>
#include <string.h>

// describe( v ): prints what kind of value it got
static void describe( WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr )
{
	for ( int i = 0; i < argn; ++i )
	{
		const WRValue& v = argv[i];
		int n = 0;
		char text[64];
		if ( v.isInt() )
		{
			printf( "int %d\n", v.asInt() );
		}
		else if ( v.isFloat() )
		{
			printf( "float %g\n", v.asFloat() );
		}
		else if ( v.isString(&n) )
		{
			printf( "string of %d: %s\n", n, v.asString(text, sizeof(text)) );
		}
		else if ( v.isWrenchArray(&n) )
		{
			printf( "array of %d:", n );
			for ( WRValue::Iterator it = v.begin(); it != v.end(); ++it )
			{
				printf( " [%d]=%s", (*it).index, (*it).value->asString(text, sizeof(text)) );
			}
			printf( "\n" );
		}
		else if ( v.isHashTable(&n) )
		{
			printf( "hash table of %d:", n );
			for ( WRValue::Iterator it = v.begin(); it != v.end(); ++it )
			{
				char key[32];
				printf( " %s=%s", (*it).key->asString(key, sizeof(key)), (*it).value->asString(text, sizeof(text)) );
			}
			printf( "\n" );
		}
		else if ( v.isStruct() )
		{
			WRValue* x = v.indexStruct( "x" );
			printf( "struct, x = %d\n", x ? x->asInt() : -1 );
		}
		else
		{
			printf( "something else: %s\n", v.technicalAsString(text, sizeof(text)) );
		}
	}
}

// fill( a, n ): makes a an array of n elements 0, 10, 20 ... in place
static void fill( WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr )
{
	if ( argn < 2 )
	{
		return;
	}
	int n = argv[1].asInt();
	for ( int i = 0; i < n; ++i )
	{
		WRValue* element = argv[0].indexArray( c, i, true );
		if ( element )
		{
			element->setInt( i * 10 );
		}
	}
}

// lookup( table, key ): the value of key in a hash table, or -1
static void lookup( WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr )
{
	retVal.setInt( -1 );
	if ( argn < 2 )
	{
		return;
	}
	WRValue* found = argv[0].indexHash( c, argv[1].getHash(), false );
	if ( found )
	{
		retVal = found->deref(); // copy the value, not a reference into the table
	}
}

// shout( s ): the text in a malloc'ed copy, upper cased into a new string
static void shout( WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr )
{
	if ( argn < 1 )
	{
		return;
	}
	WRValue::MallocStrScoped text( argv[0] ); // freed at the end of the block
	if ( !text )
	{
		return;
	}
	char* upper = (char*)wr_malloc( text.size() + 1 );
	for ( unsigned int i = 0; i <= text.size(); ++i )
	{
		const char ch = ((const char*)text)[i];
		upper[i] = (ch >= 'a' && ch <= 'z') ? (char)(ch - 32) : ch;
	}
	wr_makeString( c, &retVal, upper, (int)text.size() );
	wr_free( upper );
}

static const char* source =
	"struct P { var x = 3; var y; }\n"
	"var p = new P;\n"
	"var pair[] = { 1, \"two\" };\n"
	"var h = { \"a\": 1, \"b\": 2.5 };\n"
	"describe( 7, 0.25, \"text\", pair, h, p );\n"
	"var list[] = { };\n"
	"fill( list, 4 );\n"
	"describe( list );\n"
	"describe( lookup( h, \"b\" ), lookup( h, \"zz\" ), shout( \"quiet\" ) );\n";

int main()
{
	WRState* w = wr_newState();
	wr_registerFunction( w, "describe", describe );
	wr_registerFunction( w, "fill", fill );
	wr_registerFunction( w, "lookup", lookup );
	wr_registerFunction( w, "shout", shout );

	unsigned char* bytecode;
	int size;
	WRstr message;
	if ( wr_compile(source, (int)strlen(source), &bytecode, &size, &message) != WR_ERR_None )
	{
		printf( "%s", message.c_str() );
		return 1;
	}
	wr_run( w, bytecode, size, true );
	printf( "error %d\n", (int)wr_getLastError(w) );
	wr_destroyState( w );
	return 0;
}
```

Output:

```text
int 7
float 0.25
string of 4: text
array of 2: [0]=1 [1]=two
hash table of 2: a=1 b=2.5
struct, x = 3
array of 4: [0]=0 [1]=10 [2]=20 [3]=30
float 2.5
int -1
string of 5: QUIET
error 0
```

## The WrenchValue helper

`WrenchValue` wraps a global (by name) or any value, and converts it on
access:

```cpp
WrenchValue( WRContext* context, const char* label );
WrenchValue( WRContext* context, WRValue* value );

bool isValid() const;
int32_t* Int(); // converts the value to an int if it is not one
float* Float(); // converts the value to a float if it is not one
WRValue& operator[]( const int index ); // converts to an array, grows it
WRValue* asArrayMember( const int index );
int arraySize() const;
WRValue* addHashTableValue( const char* key ); // converts to a hash table
WRValue* getHashTableValue( const char* key ); // null when not found
```

`Int()` and `Float()` return pointers into the value, so
`*limit.Int() = 40;` writes the global directly. A pointer returned by
`addHashTableValue()` or `getHashTableValue()` becomes stale when the table
grows; fetch it again after adding keys. See
[Passing data](data.md#globals) for an example.

## Lifetime of pointers into a context

| Pointer | Valid until |
|---|---|
| return value of `wr_callFunction()`, `wr_continue()`, `wr_executeContext()` | the next call into that context |
| `argv` of a native, the `retVal` reference | the native returns |
| `wr_getGlobalRef()` result | the context is destroyed (globals do not move; `wr_resetContext()` clears the value but keeps the slot) |
| element of an array (`indexArray()`) | the array grows or is collected |
| entry of a hash table (`indexHash()`, `getHashTableValue()`) | the table grows or is collected |
| characters from `array()` | the string is changed or collected |
| a string, array or hash table value held in C++ | the next garbage collection that finds nothing in the script referring to it, unless kept with `wr_keepValue()` |
| anything from a context | `wr_destroyContext()` or `wr_destroyState()` |

When in doubt, copy numbers out with `asInt()` / `asFloat()` and text with
`asString()`, and look values up again after calling into the script.
