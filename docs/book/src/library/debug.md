# debug

The `debug::` library sends formatted text from a script to an attached
debugger: the Debug Console in VS Code, or the console of `wrench dbg`.
Use it for trace output that should show up while you debug and cost
nothing otherwise. It is not the debugger itself; for breakpoints,
stepping and the debug server see the [Debugging overview](../debugging/overview.md).

The host loads it with `wr_loadDebugLib( w )` (or `wr_loadAllLibs( w )`);
in `wrench.json` its name is `debug`. The functions are always registered,
on every platform, so scripts can call them in any build:

* In a build without `WRENCH_INCLUDE_DEBUG_CODE` they do nothing.
* With debug code, they send only when the context runs under a debug
  server (`WrenchDebugServer::attach()`, see
  [Adding the debugger to your firmware](../debugging/firmware.md)) and a
  debugger has connected to it. Otherwise the text is dropped.

Neither function writes to standard output or a serial port by itself;
for that use `println` or [`str::printf`](str.md#strprintf).

## Functions

| Function | Arguments | Returns | Summary |
|---|---|---|---|
| `debug::print` | `fmt, ...` | nothing | format and send to the debugger |
| `debug::println` | `fmt, ...` | nothing | format, add a line ending and send |

## debug::print, debug::println

`debug::print( fmt, ... )` formats its arguments like
[`str::format`](str.md#formatting) (the same specifiers, flags and
widths) and sends the result to the debugger as program output.
`debug::println` does the same and sends a line ending (`\r\n`) after it.

* The first argument is the format. If it is not a string it is
  converted to text, so `debug::println( 42 )` sends `42`.
* The formatted text is cut at 199 characters per call.
* Without arguments nothing is sent.
* Both return nothing (the int 0).

```wrench
var temperature = 21;
debug::print( "temp=%d", temperature );
debug::println( " (%s)", "ok" );
debug::println( "%d items", 3 );
println( "script output" );
```

Run with `wrench r`, the `debug::` lines print nothing:

```text
script output
```

Run in the console debugger (`wrench dbg trace.w`, then `r` to run), the
debugger shows them together with the program's own output:

```text
connected to local, wrench 8.0.0, max packet 1024 bytes
trace.w: 5 lines, 0 functions. 'h' for help.
(wdb:idle) temp=21 (ok)
3 items
script output

[ended, returned 0]
(wdb:idle)
```

Because the text travels over the debug link, long or frequent messages
slow a target that talks to the debugger over a serial line. Long texts
are split into packets of `WRENCH_DEBUG_MAX_PAYLOAD` bytes.
