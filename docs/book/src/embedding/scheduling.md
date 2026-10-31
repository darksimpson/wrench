# Yield, time slices and the scheduler

A call into the VM runs until the script returns, unless the script
yields or a time slice runs out. This page shows how the host handles
yielded scripts, how to bound the time one call may take, the round robin
scheduler that comes with wrench, and patterns for a host main loop.

## Yield from a script

A script calls `yield()` to hand control back to the host in the middle of
a function (or of the global code), with any number of values:

```wrench
function steps()
{
	var got = yield( 1 );
	print( "steps got ", got, "\n" );
	return 99;
}
```

The call into the VM returns null, and the context is *yielded*: its
frames are kept, waiting to be resumed. [Yield and cooperative
tasks](../language/yield.md) covers the script side.

```cpp
bool wr_getYieldInfo( WRContext* context, WRValue** args =0, int* argnum =0, WRValue** returnValue =0 );
WRValue* wr_continue( WRContext* context );
```

`wr_getYieldInfo()` returns true while the context is yielded. It points
`args` at the first of the `argnum` values passed to `yield()`, and
`returnValue` at the slot whose value `yield()` returns in the script when
it is resumed (0 unless you set it). `wr_continue()` resumes the script and
returns like the original call: the return value when it finishes, null
when it yields again or fails. On a context that is not yielded it fails
with `WR_ERR_context_not_yielded`.

```cpp
WRValue* r = wr_callFunction( context, "steps" );
WRValue* args = 0;
int argc = 0;
WRValue* reply = 0;
if ( !r && wr_getYieldInfo(context, &args, &argc, &reply) )
{
	printf( "steps yielded %d value(s), first %d\n", argc, args[0].asInt() );
	reply->setInt( 7 ); // what yield() returns in the script
	r = wr_continue( context );
}
```

This fragment is part of the example in
[Calling script functions](calling.md#an-example); it prints
`steps yielded 1 value(s), first 1`, then the script prints `steps got 7`
and returns 99.

Rules that follow from the design:

* A yielded context is resumed, not called: any `wr_callFunction()` on it
  continues the yielded function and ignores what you asked for
  ([Yielded functions](calling.md#yielded-functions)). One context runs one
  thing at a time; use one context per independent task.
* If the global code yields, `wr_run()` returns the context yielded, and
  the context's functions cannot be called until `wr_continue()` has
  finished the global code.
* Code that a native called back into cannot yield
  (`WR_ERR_yield_in_native_call`), and neither can an imported module's
  code; the native or the import would have to be suspended too.
* A yielded context holds its frames on its stack. Destroying or resetting
  it drops them.

## Time slices

A script with an endless loop, or one that runs long, would keep the
host from doing anything else. With `WRENCH_TIME_SLICES` defined, the VM
counts and stops:

```cpp
void wr_setInstructionsPerSlice( WRState* w, const int instructions );
void wr_forceYield( WRState* w );
int wr_slicesUsedLastCall( WRState* w );
```

* `wr_setInstructionsPerSlice( w, n )` limits every call into the VM of
  that state to `n` jumps and branches, then forces a yield. 0, the
  default, means no limit. Only jumps and branches are counted, the
  instructions every loop must execute, so the check costs one decrement
  on those and nothing elsewhere; straight line code between two jumps is
  never interrupted, and neither is deep recursion without a loop.
* A forced yield looks to the host like a `yield()` without values:
  `wr_getYieldInfo()` returns true with `argnum` 0 and a null
  `returnValue`. Resume with `wr_continue()`, which gets a fresh slice.
* `wr_forceYield( w )` makes the running script yield at its next jump or
  branch, for example from a timer interrupt or a watchdog thread. It
  writes two fields of the state without locking.
* `wr_slicesUsedLastCall( w )` returns how many jumps and branches the last
  call into the VM executed (in its last slice), useful to see how much of
  its budget a script uses. It has no meaning with no limit set.

Calls a native makes back into the script, and the global code of imported
modules, run without a limit and resume the caller's slice afterwards.

```cpp
// build with WRENCH_TIME_SLICES defined
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

static const char* source =
	"function work( n )\n"
	"{\n"
	"	var sum = 0;\n"
	"	for ( var i = 0; i < n; ++i )\n"
	"	{\n"
	"		sum += i;\n"
	"	}\n"
	"	return sum;\n"
	"}\n";

int main()
{
	WRState* w = wr_newState();
	wr_registerFunction( w, "print", print );

	unsigned char* bytecode;
	int size;
	if ( wr_compile(source, (int)strlen(source), &bytecode, &size) != WR_ERR_None )
	{
		return 1;
	}
	WRContext* context = wr_run( w, bytecode, size, true );

	// at most 1000 jumps and branches per call into the VM
	wr_setInstructionsPerSlice( w, 1000 );

	WRValue n;
	n.setInt( 5000 );
	WRValue* result = wr_callFunction( context, "work", &n, 1 );
	int slices = 1;
	while ( !result && wr_getYieldInfo(context) )
	{
		// the host does other work here, then resumes the script
		result = wr_continue( context );
		++slices;
	}
	printf( "work( 5000 ) = %d in %d slices\n", result ? result->asInt() : -1, slices );

	// back to unlimited
	wr_setInstructionsPerSlice( w, 0 );
	result = wr_callFunction( context, "work", &n, 1 );
	printf( "unlimited: %d\n", result ? result->asInt() : -1 );

	wr_destroyState( w );
	return 0;
}
```

Output:

```text
work( 5000 ) = 12497500 in 6 slices
unlimited: 12497500
```

`WRENCH_TIME_SLICES` adds the counters to `WRState`, so it must be defined
for every file that includes `wrench.h` ([Build flags](../reference/build-flags.md)).

## The scheduler

`WrenchScheduler`, also built with `WRENCH_TIME_SLICES`, runs several
scripts round robin, each in its own context of one state:

```cpp
class WrenchScheduler
{
public:
	WrenchScheduler( const int stackSizePerThread = WRENCH_DEFAULT_STACK_SIZE );
	~WrenchScheduler();

	WRState* state() const;

	void tick( const int instructionsPerSlice =1000 );

	int addThread( const uint8_t* byteCode, const int size, const int instructionsThisSlice =1000, const bool takeOwnership =false );
	bool removeTask( const int taskId );

	int lastErr() const;
	int lastErrTaskId() const;
	void clearErr();
};
```

* The scheduler creates its own state; register natives on `state()`.
* `addThread()` creates a context for the image and runs its global code
  for a first slice. It returns a task id, 0 when the script already
  finished in that slice (nothing is kept), or -1 when it failed. Ids
  count from 1 in each scheduler, so two schedulers hand out the same
  ids.
* `tick()` gives every task one slice, resuming each where it stopped.
  Tasks that finish or fail are destroyed; a failure is recorded in
  `lastErr()` and `lastErrTaskId()`.
* `removeTask()` destroys a task before it finishes.

Tasks are the global code of their image: a task is a script that loops
(and yields, or is sliced) for as long as it should run.

```cpp
// build with WRENCH_TIME_SLICES defined
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

static int addTask( WrenchScheduler& scheduler, const char* source )
{
	unsigned char* bytecode;
	int size;
	if ( wr_compile(source, (int)strlen(source), &bytecode, &size) != WR_ERR_None )
	{
		return -1;
	}
	// runs the global code for the first slice; 0 means it already finished
	return scheduler.addThread( bytecode, size, 2, true );
}

int main()
{
	WrenchScheduler scheduler; // owns its own WRState
	wr_registerFunction( scheduler.state(), "print", print );

	int a = addTask( scheduler, "for ( var i = 0; i < 6; ++i ) { print( \"A\", i, \" \" ); }" );
	int b = addTask( scheduler, "for ( var i = 0; i < 6; ++i ) { print( \"B\", i, \" \" ); }" );
	int c = addTask( scheduler, "print( \"C done \" );" );
	printf( "\ntasks %d %d %d\n", a, b, c );

	for ( int round = 1; round <= 3; ++round )
	{
		scheduler.tick( 2 ); // each task runs for at most 2 jumps
		printf( "\nafter tick %d\n", round );
	}

	if ( scheduler.lastErr() )
	{
		printf( "task %d failed with %d\n", scheduler.lastErrTaskId(), scheduler.lastErr() );
	}
	return 0;
}
```

Output:

```text
A0 A1 B0 B1 C done 
tasks 1 2 0
B2 B3 A2 A3 
after tick 1
B4 B5 A4 A5 
after tick 2

after tick 3
```

Each tick visits the newest task first. Task C finished during
`addThread()` and got the id 0. A real slice is in the hundreds or
thousands; 2 only makes the interleaving visible.

## Patterns for the main loop

**Event driven.** The script defines handlers, the host calls them when
something happens and they return quickly. No yield, no slices. This is
the simplest and the most common:

```wrench
function onButton( id )
{
	led( id, 1 );
}
```

**Periodic.** The host calls a function such as `loop()` again and again,
and the function returns how long to wait until the next call. The Arduino
and ESP32 examples work this way:

```cpp
if ( scriptReady && (int32_t)(millis() - nextCall) >= 0 )
{
	WRValue* ret = wr_callFunction( script, "loop" );
	int delayMs = ret ? ret->asInt() : 1000;
	nextCall = millis() + (delayMs > 0 ? delayMs : 1);
}
```

**Cooperative.** A script that is easier to write as a sequence ("open the
valve, wait 5 s, close it") yields where it waits, with what it waits for:

```wrench
function sequence()
{
	valve( 1 );
	yield( 5000 ); // ask the host to resume in 5000 ms
	valve( 0 );
}
```

The host reads the yielded value with `wr_getYieldInfo()`, notes when to
resume and calls `wr_continue()` then. Give each such sequence a context
of its own.

**Sliced.** Scripts you do not trust to return quickly (user written ones,
or ones with real loops) run with a slice limit; the host resumes them
between its own work. The scheduler above does this for whole programs.

Whichever pattern you use, the host loop is also where the debug server is
serviced: call `tick()` once per pass, while no script runs
([Adding the debugger to your firmware](../debugging/firmware.md)). A loop
that waits should wait with a timeout, so that `tick()` keeps running while
nothing happens ([Several states, threads and RTOS tasks](threads.md)).
