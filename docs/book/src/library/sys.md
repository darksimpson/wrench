# sys

The `sys::` library lets a script look at and extend the program it runs
in: test whether a function exists, load more code while running, and
stop with an error code of its own. Read it if your scripts load modules
or must check what the host provides.

The host loads it with `wr_loadSysLib( w )` (or `wr_loadAllLibs( w )`);
in `wrench.json` its name is `sys`. It runs on every platform;
`sys::importCompile` also needs the compiler in the build (it is left out
with `WRENCH_WITHOUT_COMPILER`).

## Functions

| Function | Arguments | Returns | Summary |
|---|---|---|---|
| `sys::isFunction` | `name` | int | 1 native or library function, 2 script function, 0 neither |
| `sys::importCompile` | `source` | int | compile a source string and import it; 0 or the compile error |
| `sys::importByteCode` | `code` | int | import compiled bytecode; 0 or -1 |
| `sys::halt` | `code` | does not return | stop the script with error `code` |

## sys::isFunction

`sys::isFunction( name )` tells whether calling `name` would find
something:

| Result | Meaning |
|---|---|
| 1 | a function the host registered: a native (`wr_registerFunction`) or a library function such as `math::sin` |
| 2 | a function defined in the script that is running, or in a module it imported (also before its first call) |
| 0 | neither; also for a library constant such as `io::O_CREAT`, and without an argument |

Use it to adapt a script to hosts that provide different natives, instead
of failing with `WR_ERR_function_not_found`:

```wrench
function helper()
{
	return 1;
}
println( sys::isFunction( "helper" ), " ", sys::isFunction( "println" ), " ", sys::isFunction( "math::sin" ) );
println( sys::isFunction( "nothing" ), " ", sys::isFunction( "io::O_CREAT" ), " ", sys::isFunction() );
if ( sys::isFunction( "led" ) )
{
	led( 1 );
}
else
{
	println( "no led() in this host" );
}
```

Output:

```text
2 1 1
0 0 0
no led() in this host
```

A function of an imported module is a script function too:

```wrench
println( sys::isFunction( "area" ) );
sys::importCompile( "function area( w, h ) { return w * h; }" );
println( sys::isFunction( "area" ), " ", area( 2, 3 ) );
```

Output:

```text
0
2 6
```

## sys::importCompile

`sys::importCompile( source )` compiles the string `source` and imports
the result into the running context: its global code runs, and its
functions and structs become callable from the script. How importing
works, what a module can see and what it shares with its importer is
explained in [Modules: importing code at run time](../language/import.md).

It returns the compiler's error code as an int: 0 (`WR_ERR_None`) when
the module was compiled and imported, otherwise the number of the compile
error (see [Error codes](../reference/error-codes.md)). A compile error
does not stop the script. Without an argument, or with an argument that is
not a string, it does nothing and returns 0. In a build without the
compiler every call returns 1, `WR_ERR_compiler_not_loaded`.

```wrench
var source = "function area( w, h ) { return w * h; }";
var err = sys::importCompile( source );
println( "import: ", err );
println( "area: ", area( 3, 4 ) );
println( "bad source: ", sys::importCompile( "function broken( {" ) );
```

Output:

```text
import: 0
area: 12
bad source: 10
```

The module is compiled with its globals named (`WR_INCLUDE_GLOBALS`).
While a debugger is attached to the context it is also compiled with
debug information and its source, so you can step into it; see
[Several files, programs and modules](../debugging/programs.md).

## sys::importByteCode

`sys::importByteCode( code )` imports a module that was already compiled,
for example with `wrench c` on a PC, and handed to the script as a string
(read from a file, received over a link, stored in a message). Compiling
ahead saves the compiler's code and memory on the target.

It returns 0 when the code was imported, and -1 when `code` is not a
string, is not valid bytecode for this version of wrench (the image and
its CRC are checked first), or memory runs out. A bad image never stops
the script. The function copies the bytes, so the string can be
discarded afterwards.

```bash
wrench c greet.w greet.bin
```

where `greet.w` is:

<!-- check:bin=greet.bin -->
```wrench
function greet( name )
{
	return "hello, " + name;
}
```

and the program that imports it:

```wrench
var code = io::readFile( "greet.bin" );
println( "import: ", sys::importByteCode( code ) );
println( greet( "module" ) );
println( "not bytecode: ", sys::importByteCode( "text" ), " ", sys::importByteCode( 5 ) );
```

Output:

```text
import: 0
hello, module
not bytecode: -1 -1
```

## sys::halt

`sys::halt( code )` stops the script at once with the error `code`, as if
the VM had failed. Nothing after it runs; the host sees the code from
`wr_getLastError()` and decides what to do (see
[Errors and robustness](../embedding/errors.md)).

The code is converted to int and must be a user error, 101 to 255 (above
`WR_USER`, at most `WR_ERR_LAST`). Any other value, including 100, stops
the script with `WR_ERR_USER_err_out_of_range` instead. Without an
argument `sys::halt` does nothing and the script goes on.

```wrench
println( "checking" );
sys::halt( 5 );
println( "not reached" );
```

Output (the last line is printed by `wrench r`):

```text
checking
err: WR_ERR_USER_err_out_of_range
```

A script stops with its own code, and the host tells the codes apart:

```wrench
var calibrated = 0;
if ( !calibrated )
{
	sys::halt( 150 ); // the host knows 150 as "not calibrated"
}
```

`wrench r` reports it as `err: user error 150`; a host checks the code:

```cpp
wr_run( w, code, codeLen );
WRError err = wr_getLastError( w );
if ( err > WR_USER )
{
	printf( "script stopped itself with code %d\n", (int)err );
}
```
