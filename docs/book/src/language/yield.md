# Yield and cooperative tasks

`yield()` suspends a script in the middle of its work and hands control
back to the host, which resumes it later exactly where it stopped. This
page describes `yield()` from the script's side: what the host sees, how
values travel both ways, and where yielding is not allowed. The host side
(resuming, time slices, the scheduler) is described in
[Yield, time slices and the scheduler](../embedding/scheduling.md).

## yield in a script

`yield( values... )` is a built in call. It stops the script and returns
from the host call that is running it (`wr_run()`, `wr_callFunction()` or
`wr_continue()`). Everything is kept: the position, the local variables
of every function on the call stack, the globals. When the host calls
`wr_continue()`, the script goes on after the `yield`, and `yield()`
returns the value the host provided (0 if it provided none).

```wrench
println( "waiting for a reading" );
var value = yield( "need-reading" );
println( "got ", value );
```

Output:

```text
waiting for a reading
```

`wrench r` does not resume a yielded script, so the program above ends
at the `yield`. A host that resumes it, giving 20 as the reading:

```cpp
WRContext* context = wr_run( w, code, len, true );

int reading = 20;
WRValue* args;
int argn;
WRValue* returnValue;
while ( wr_getYieldInfo( context, &args, &argn, &returnValue ) )
{
	// args[0] .. args[argn - 1] are the values passed to yield()
	wr_makeInt( returnValue, reading++ ); // what yield() returns in the script
	wr_continue( context );
}
```

prints

```text
waiting for a reading
got 20
```

* `yield()` takes any number of arguments, of any type; the host reads
  them with `wr_getYieldInfo()`. They tell the host why the script
  stopped: what it waits for, how long it wants to sleep, a value it
  produced.
* The value of the `yield()` expression is whatever the host stores in
  `*returnValue` before it continues, 0 by default.
* Calling `wr_callFunction()` on a yielded context also resumes it,
  ignoring the function and the arguments passed.
* `yield` is not a function: a script function cannot be named `yield`
  (`WR_ERR_bad_label`).

## Where yield works

`yield()` works in the global code and in any script function the host
called, at any depth of nested calls:

```wrench
function waitFor( what )
{
	println( "waiting for ", what );
	return yield( what );
}

function step()
{
	var a = waitFor( "a" );
	var b = waitFor( "b" );
	return a + b;
}

println( "result ", step() );
```

Output:

```text
waiting for a
```

A host that answers each yield with 1 sees the script print
`waiting for b` after the first `wr_continue()` and `result 2` after the
second.

## Where yield is not allowed

| Situation | Result |
|---|---|
| Script code that a native function called back into (a native calls `wr_callFunction()`, the script function yields) | `WR_ERR_yield_in_native_call`, the script stops |
| The global code of a module being imported, or a module function called by its importer | `WR_ERR_cannot_call_function_context_yielded` (see [Modules](import.md#yield-in-modules)) |

The reason is the same in both cases: the host would have to resume C
code or a second context that are no longer running.

## Typical uses

* **Waiting without blocking.** A script on a device asks for something
  and lets the firmware run until it is there: `var line = yield( "read-line" );`.
* **Sleeping.** `yield( 500 );` tells the host "call me again in 500 ms";
  the host keeps the context aside and continues it later.
* **Cooperative tasks.** Several scripts, each in its own context, run
  long loops that `yield()` regularly; the host continues them in turn.
  The `WrenchScheduler` class does this round robin.
* **Generators.** A function produces values one at a time with
  `yield( value )`, and the host collects them.

A host can also suspend a script that never calls `yield()`: built with
`WRENCH_TIME_SLICES`, the VM yields on its own after a number of jumps
(loop iterations). The script does not notice, and `wr_getYieldInfo()`
reports no values and no return value then. See
[Yield, time slices and the scheduler](../embedding/scheduling.md).

## Debugging

A yielded script is not stopped from the debugger's point of view: the
host runs other code until it continues the script. Breakpoints after the
`yield` hit when it does. See
[Blocking code, yield and polling](../debugging/blocking.md).
