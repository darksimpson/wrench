# Overview

Wrench comes with a standard library: math, strings, containers, files,
serialization, sockets, messages between scripts and a few system
functions, plus device libraries for Arduino boards. This page lists the
libraries, says which ones a host loads and how, and explains the rules
all library functions share. Script authors find each function on the
library's own page; people embedding wrench should read
[Loading libraries](#loading-libraries).

```wrench
var root = math::sqrt( 2 );
var text = str::format( "%d items, root %s", 3, root );
println( text, " (", str::strlen( text ), " characters)" );
```

Output:

```text
3 items, root 1.41421 (21 characters)
```

## The libraries

Library functions are named `namespace::function`. A namespace can be
filled by more than one library, and a library can fill more than one
namespace.

| Page | Names | Loader | `wrench.json` | Runs on |
|---|---|---|---|---|
| [math](math.md) | `math::` | `wr_loadMathLib` | `math` | everywhere |
| [str](str.md) | `str::` | `wr_loadStringLib` | `string` | everywhere |
| [std](std.md) | `std::rand`, `std::srand`, `std::time`, `time::ms` | `wr_loadStdLib` | `std` | everywhere; `time::ms` and `std::time` need a known clock (PC, Arduino, ESP-IDF, STM32 HAL), see their pages |
| [sys](sys.md) | `sys::` | `wr_loadSysLib` | `sys` | everywhere |
| [Containers](containers.md) | `array::`, `hash::`, `list::`, `queue::`, `stack::` | `wr_loadContainerLib` | `container` | everywhere |
| [Files: io](io.md) | `io::` files, `io::O_...`, `io::SEEK_...` | `wr_loadIOLib` | `io` | with a file system flag: Windows, Linux, SPIFFS, LittleFS |
| [Messages and time](msg-time.md) | `msg::` | `wr_loadMessageLib` | `msg` | everywhere |
| [Serialization](serialize.md) | `std::serialize`, `std::deserialize` | `wr_loadSerializeLib` | `serialize` | everywhere |
| [Networking: tcp](tcp.md) | `tcp::` | `wr_loadTCPLib` | `tcp` | with a TCP flag: Windows, Linux |
| [debug](debug.md) | `debug::` | `wr_loadDebugLib` | `debug` | everywhere; sends only under a debugger |
| [Devices](devices.md) | `std::delay`, `io::` pins, `wire::`, `lcd::`, `fastled::` | `wr_loadAllArduinoLibs`, `wr_loadFastLEDLib` and others | none | Arduino builds only |

The "everywhere" libraries need nothing but a C++ compiler and the C
library; `math::` also links the C math library. The platform specific
ones depend on build flags, described on their pages and in
[Build flags](../reference/build-flags.md):

* `io::` files: `WRENCH_WIN32_FILE_IO`, `WRENCH_LINUX_FILE_IO`,
  `WRENCH_SPIFFS_FILE_IO` or `WRENCH_LITTLEFS_FILE_IO`. Without one,
  `wr_loadIOLib` registers nothing.
* `tcp::`: `WRENCH_WIN32_TCP` or `WRENCH_LINUX_TCP`. Without one,
  `wr_loadTCPLib` registers nothing.
* `str::format` and friends: `WRENCH_FLOAT_SPRINTF` to let the C library
  produce the digits of floats.
* `debug::`: `WRENCH_INCLUDE_DEBUG_CODE`, or the functions do nothing.
* Device libraries: `ARDUINO`, and `WRENCH_ARDUINO_FASTLED` for FastLED.

## print and println are not library functions

`print` and `println`, used in every example of this book, are not part
of the standard library. They are natives that the host registers with
`wr_registerFunction`; the command line tool (`wrench r`), the debug
targets and the examples all do. Your own host has them only if it
registers them, and it decides where their output goes: a console, a
serial port, a log.

```cpp
void println( WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr )
{
	char buf[128];
	for( int i = 0; i < argn; ++i )
	{
		printf( "%s", argv[i].asString( buf, sizeof(buf) ) );
	}
	printf( "\n" );
}

wr_registerFunction( w, "println", println );
```

The library has its own output functions that need no registration:
[`str::printf`](str.md#strprintf) writes to standard output (or the
serial port on boards with a file system backend), and
[`debug::print`](debug.md) writes to an attached debugger.

## Loading libraries

Nothing is loaded by default: a new state knows no library functions. The
host loads the libraries its scripts may use, once per state, before
running any script:

```cpp
WRState* w = wr_newState();
wr_loadAllLibs( w ); // every standard library
```

`wr_loadAllLibs( w )` calls, in this order, `wr_loadMathLib`,
`wr_loadStdLib`, `wr_loadIOLib`, `wr_loadStringLib`,
`wr_loadMessageLib`, `wr_loadSysLib`, `wr_loadSerializeLib`,
`wr_loadDebugLib`, `wr_loadTCPLib` and `wr_loadContainerLib`. It does not
load the device libraries; call `wr_loadAllArduinoLibs` and
`wr_loadFastLEDLib` yourself (see [Devices](devices.md)).

A host may load only some libraries instead. Every loader is declared in
`wrench.h` and can be called on its own:

```cpp
WRState* w = wr_newState();
wr_loadMathLib( w );
wr_loadStringLib( w );
wr_loadContainerLib( w );
```

Reasons to leave libraries out:

* Control: a script cannot touch files or sockets if `io::` and `tcp::`
  are not loaded. This is the way to sandbox scripts you do not trust.
* Memory: each registered function takes an entry in the state's function
  table (RAM). A library that is never loaded costs nothing at run time.
* Code size: on small targets, the linker drops a library's code only if
  nothing references its loader. `wr_loadAllLibs` references all of them.

Loading the same library twice is harmless: a function registered again
replaces the earlier entry. Libraries that share a namespace (`std::`
gets functions from three loaders) use different names and do not
collide.

### When a script calls a library that is not loaded

The compiler does not check library calls; it does not know what the host
will load. A call to a library function that is not registered stops the
script at run time with `WR_ERR_lib_function_not_found`:

```wrench
println( "start" );
var v = gpio::read( 3 );
println( "not reached" );
```

Output (the last line is printed by `wrench r`):

```text
start
err: WR_ERR_lib_function_not_found
```

A host can install a handler with `wr_setOnLibCallbackNotFound()` that
runs instead and lets the script continue, and a script can test for a
function first with [`sys::isFunction`](sys.md#sysisfunction). To let the
language server in VS Code warn about calls the application does not
provide, list the loaded libraries in `wrench.json` under `"libraries"`,
using the names in the table above (or `"all"`); see
[Describing your host application: wrench.json](../vscode/wrench-json.md).

### Adding your own

A library function is a C function registered under a `namespace::name`
with `wr_registerLibraryFunction()`, and a library constant is registered
with `wr_registerLibraryConstant()`; the standard libraries are built
exactly that way (`src/lib/*.cpp`). How to write them is explained in
[Native functions and libraries](../embedding/natives.md).

## Rules shared by all library functions

Library functions are called like any function, with the namespace in
front: `math::sqrt( 2 )`. Scripts cannot declare namespaces of their own:
a call `name::function()` always goes to the functions the host
registered. (The members of a named enum are written `Color::RED` too;
those are constants of the script, see
[Enums and constants](../language/enums.md).)

Library functions are written for speed and small code, and they follow
the same conventions:

* **No argument checking errors.** A call with too few arguments, or with
  arguments of the wrong kind, does not stop the script. The function does
  nothing and returns the int 0, or a documented default (-1 for "not
  found" in some functions). Extra arguments are ignored.
* **Numbers are converted.** Where a function wants an int, a float is
  truncated; where it wants a float, an int is converted.
* **Text is not parsed.** A string passed where a number is expected is
  not read as a number: its hash is used, which is a meaningless large
  number. Convert with [`str::tol`](str.md#strtol) first.
* **Containers are changed in place.** Functions that modify an array or
  hash table change the container itself, and functions that create one
  (`array::clear`, `hash::clear`, `list::clear` ...) replace the value of
  the variable passed to them.
* **Results are new values.** Strings returned by `str::` functions are
  new strings; the arguments are not changed.

```wrench
println( math::sqrt(), " ", str::left( 5, 1 ), " ", math::abs( "5" ), " ", math::abs( str::tol( "-5" ) ) );
```

Output:

```text
0 0 806133952 5
```

Library constants such as `io::O_CREAT` are read like variables but
cannot be assigned; `io::O_CREAT = 1;` is a compile error
(`WR_ERR_not_assignable`).

Some library functions block: `io::getline`, `tcp::accept`,
`tcp::receive` and `std::delay` wait for input or time to pass, and while
they wait the VM runs nothing else. See
[Yield and cooperative tasks](../language/yield.md) for running several
scripts side by side, and
[Blocking code, yield and polling](../debugging/blocking.md) for how this
interacts with the debugger.
