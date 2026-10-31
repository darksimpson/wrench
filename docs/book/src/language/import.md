# Modules: importing code at run time

A running script can load more code: a source string with
`sys::importCompile()`, or compiled bytecode with
`sys::importByteCode()`. The loaded code is a *module*: its functions and
structs become callable from the script that imported it. This page
explains what a module can see, how its functions are reached, how long
it lives and what can go wrong. To split the source of one program into
files, use [include](include.md) instead, which works at compile time.

## A first module

```wrench
var err = sys::importCompile( "var calls = 0; println( \"module loaded\" ); function twice( x ) { calls++; return x * 2; } function count() { return calls; }" );
println( "import result ", err );
println( twice( 21 ), " ", twice( 5 ), " ", count() );
```

Output:

```text
module loaded
import result 0
42 10 2
```

What happens on an import:

1. The code is compiled (`sys::importCompile()` only) and checked.
2. The module's global code runs once, right away.
3. Its functions and structs can be called and made with `new` from the
   importing script, by name, from then on.

The calls in the importer are written like any other call. They compile
even though the compiler has never seen the module, because calls are
resolved by name when they run (see
[Calling natives and library functions](functions.md#calling-natives-and-library-functions)).
A call that runs before the import, or when the import failed, stops the
script with `WR_ERR_function_not_found`.

## sys::importCompile and sys::importByteCode

| Function | Takes | Returns |
|---|---|---|
| `sys::importCompile( source )` | wrench source text | 0, or the number of the compile error |
| `sys::importByteCode( code )` | a compiled program, as a string of bytes | 0, or -1 when it is not valid bytecode for this version |

A failed import does not stop the script; check the result:

```wrench
var result = sys::importCompile( "var x = ;" );
if ( result != 0 )
{
	println( "module failed to compile, error ", result );
}
```

Output:

```text
module failed to compile, error 9
```

The number is a `WRError` value (9 is `WR_ERR_bad_expression`, see
[Error codes](../reference/error-codes.md)).

`sys::importCompile()` needs the compiler in the build (it is not there
with `WRENCH_WITHOUT_COMPILER`), and the module's source cannot use
`include` (`WR_ERR_include_not_supported`, 65). Precompiled bytecode works
everywhere and loads faster. Compile the module with the command line
tool:

```bash
wrench c sensor.w sensor.bin
```

and load the bytes, here from a file with `io::readFile()` (a firmware
would more likely have them in flash or receive them over a link):

```wrench
var code = io::readFile( "sensor.bin" );
if ( sys::importByteCode( code ) != 0 )
{
	println( "sensor.bin is not a valid module" );
}
```

Both functions are part of the [sys library](../library/sys.md); the host
side of importing (`wr_import()`) is in
[Host API reference](../embedding/api-reference.md).

## What a module can see

A module is compiled as a program of its own. It sees:

* its own globals, functions, structs and enums;
* the functions of the other modules the context imported;
* the natives and libraries of the host.

It does not see the globals of the importing script: a module that names
one does not compile (`WR_ERR_var_not_seen_before_label`), and the
importer cannot read the module's globals either. Data goes in and out
through function arguments and return values; strings, arrays, hash
tables and struct instances can be passed both ways and are shared as
usual.

```wrench
sys::importCompile( "function total( list ) { var t = 0; for ( var v : list ) { t += v; } return t; }" );
var values[] = { 4, 5, 6 };
println( total( values ) );
```

Output:

```text
15
```

It does not see the functions of the script that imported it either: a
module that calls one stops with `WR_ERR_function_not_found`. Pass values
in as arguments, and let the importer call the module, not the other way
round.

```wrench
function helper()
{
	return 5;
}
sys::importCompile( "function first() { return 1; }" );
sys::importCompile( "function second() { return first() + 10; } function third() { return helper(); }" );
println( second() );
println( third() );
```

Output:

```text
11
err: WR_ERR_function_not_found
```

## Name clashes

When the importer and a module, or two modules, define a function of the
same name, a call from the importer goes to:

1. the importer's own function, if it has one;
2. otherwise the function of the module imported last.

Each import is a separate module with its own globals, also when the same
code is imported twice.

## Structs and new

A struct of a module can be made with `new` in the importer. Structs are
always exported for this. A function used as a constructor
(`new makeColor`, see [Everything is a unit](structure.md#everything-is-a-unit))
must be declared with `export` in the module, otherwise `new` stops the
script with `WR_ERR_struct_not_exported`:

```wrench
sys::importCompile( "struct Pair( a, b ) { var first = a; var second = b; } export function Color { var red = 255; var green; }" );
var p = new Pair( 1, 2 );
var c = new Color;
println( p.first, " ", p.second, " ", c.red );
```

Output:

```text
1 2 255
```

`export` is only allowed in front of `function`, `unit` and `struct`
(`WR_ERR_unexpected_export_keyword` otherwise).

## Lifetime

A module lives as long as the context that imported it: there is no way
to unload one module. It is destroyed with that context
(`wr_destroyContext()`), and also when the host restarts the context with
`wr_resetContext()`. Objects passed between a script and its modules are
collected together, so a value returned by a module stays valid.

## Yield in modules

Module code cannot be suspended. A module whose global code calls
`yield()`, or a module function that yields when the importer calls it,
stops the script with `WR_ERR_cannot_call_function_context_yielded`. See
[Yield and cooperative tasks](yield.md).

## Debugging modules

While a debugger is attached, `sys::importCompile()` compiles the module
with debug information and its source, so breakpoints and stepping work
inside it; a module given to `sys::importByteCode()` is debuggable when it
was compiled with `-debugsource`. A module's breakpoints come back when
it is imported again on the next run. See
[Several files, programs and modules](../debugging/programs.md).

## When to use modules

* Code that is only known at run time: user scripts loaded from storage,
  plugins, code received over a link.
* Programs larger than the 64KB limit of one bytecode image (see
  [Limits](../reference/limits.md)): each module may be up to that size.
* Several scripts that should not see each other's globals.

For code that is known when you compile, `include` is simpler: one
program, one image, and the compiler checks the calls between the files.
