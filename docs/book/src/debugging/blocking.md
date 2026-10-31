# Blocking code, yield and polling

A debugger can only act when something on the target reads the link. This
page explains where that happens, and goes through every way a script or
the firmware can block: long loops, empty endless loops, natives that
wait, firmware that waits, `yield()` and time slices, and code without
debug information. A table at the end sums it up.

## How the link is serviced

The target is single threaded: the server reads the link only from inside
the code that runs the scripts. There are four places:

| Where | When | What it answers |
|---|---|---|
| `tick()` | the firmware calls it whenever no script code runs | everything, including launches and uploads |
| the VM's line hook | at line markers while script code runs, every `WRENCH_DEBUG_POLL_MS` | everything except launches and uploads, which are refused as busy |
| the stop loop | while a script is halted (breakpoint, step, pause, error, waiting for a debugger after `breakOnEntry()`) | everything except launches and uploads; calls `idle()` between reads |
| `poll()` | a native function calls it while it waits | like the line hook |

While script code runs, the VM calls the server at every line marker. The
server counts lines and reads the clock every `WRENCH_DEBUG_POLL_LINES` (8)
of them; once `WRENCH_DEBUG_POLL_MS` (20) milliseconds have passed since
the last poll, it reads the link and answers what arrived. So a pause, a
terminate or a new breakpoint takes effect within about 20 ms plus the
time of the line that is running, and short lines cost only a counter. The
polling reads the link and nothing else: it does not yield, sleep or call
`idle()`.

While a script is halted, the server loops: read the link, check the link
timeout, call `WrenchDebugTransport::idle()`. That loop runs inside the VM,
inside the `wr_executeContext()` or `wr_callFunction()` the firmware made;
the firmware waits there until the debugger lets the script go.

## Busy loops

```wrench
var n = 0;
while ( n < 100000000 )
{
	n += 1;
}
```

A long computation keeps the CPU, with or without a debugger. The debugger
can pause it, set breakpoints in it and terminate it, since every pass
meets a line marker and the line hook polls. What it does not do is give
other tasks a turn: a task watchdog that expects the script's task to
sleep now and then, or lower priority tasks that need the core, see the
same long call they would see without a debugger. Split such work, yield
([below](#yield-and-time-slices)), or configure the watchdog for it.

## Empty endless loops

```wrench
while ( 1 ) {}
```

Loops with nothing in them, `while ( 1 ) {}`, `for ( ;; ) {}` and
`do {} while ( 1 );`, also meet a line marker on every pass when compiled
with debug information: the compiler puts one for the loop's line where
each pass begins. A debugger can pause such a loop (it stops on the loop's
line within about 20 ms) and terminate it.

## Natives that block

Inside a native function nothing polls. A native that waits for long, on a
semaphore, a socket or a delay, leaves the debugger unanswered: requests
time out on the PC ("target did not answer") and a pause has no effect
until the native returns.

The native can service the link while it waits by calling `poll()`:

```cpp
static SemaphoreHandle_t dataReady; // given by an interrupt or another task

// waitData(): the script waits here until new data is there
static void waitData( WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr )
{
	while ( xSemaphoreTake(dataReady, pdMS_TO_TICKS(10)) != pdTRUE )
	{
		debugger->poll();
	}
}
```

`poll()` answers requests like the line hook does. A pause or a terminate
is answered at once, but takes effect on the script's next line, after the
native returns: on a PC target, a pause sent while such a native waited
1.5 s was answered after 6 ms, and the script stopped on the line after
the call 1.2 s later, when the native returned. Breakpoints and
watchpoints set meanwhile apply from that line on.

Rules for `poll()`:

* Call it only from inside a native the script called, on the task that
  runs the script. Outside script code, call `tick()` instead.
* Launches and uploads are refused as busy while it runs, as they are
  whenever script code runs: the firmware is inside the VM.
* A native that may wait forever should give up after a while and return
  (with a value that tells the script to ask again): a terminate cannot
  end the script while the native still waits.

## Firmware that waits between calls

The same applies to the firmware itself. Firmware that blocks until
something happens, for example on a queue with an infinite timeout, does
not call `tick()` meanwhile, and the debugger gets no answer until the
next event. Wait with a timeout and call `tick()` each time round:

```cpp
for ( ;; )
{
	debugger->tick();

	Event e;
	if ( xQueueReceive(events, &e, pdMS_TO_TICKS(10)) == pdTRUE )
	{
		// call into the script
	}
}
```

10 ms keeps the debugger responsive and costs next to nothing.
[Scenario 1](scenarios.md#1-event-driven-esp32-s3-firmware-with-psram) is a
complete example.

## Yield and time slices

A script that calls [`yield()`](../language/yield.md) hands control back
to the firmware. The call returns 0 to the firmware with the context
yielded, and the firmware continues it later with `wr_continue()`. In
between, no script code runs: the firmware's `tick()` serves the debugger,
and launches and uploads are possible.

With `WRENCH_TIME_SLICES` and `wr_setInstructionsPerSlice()` the VM
yields by itself after a number of instructions, so a long call returns
early and the firmware resumes it later; see
[Yield, time slices and the scheduler](../embedding/scheduling.md). For
the debugger it is the same as a `yield()`.

What the debugger can do with a yielded program:

* **Inspect it.** Globals can be read and set while it is yielded (the
  firmware is between calls). The stack of the yielded call cannot be
  inspected: it is not halted, only parked.
* **Stop in it.** Breakpoints set while it is yielded take effect when the
  firmware continues it.
* **Terminate it.** A terminate for a call of program 0 that yielded, its
  global code or a function the firmware called, ends it on its next line
  once the firmware continues it: `wr_continue()` returns 0 and
  `wr_getLastError()` is `WR_ERR_debugger_terminated`. Only that call of
  program 0 is ended; other contexts are not touched. A `wr_resetContext()`
  of the context in the meantime cancels the terminate.
* **Launch it again.** `wr_resetContext()` throws a yielded call away, so a
  launch while program 0 is yielded starts it from the beginning.

The end of program 0's global code reaches the debugger when it really
ends: a global code that yields is over when the `wr_continue()` that
finishes it returns, and the VM reports it from there.

## Code without debug information

Code compiled without `-debug` or `-debugsource` (without
`WR_EMBED_DEBUG_CODE`) has no line markers. It runs at full speed, even
with a debugger attached, and the server is never called while it runs:

* It cannot stop at breakpoints, be stepped through or paused.
* While it runs, nothing polls the link: a long loop in such code leaves
  the debugger unanswered, exactly like a blocking native. A pause request
  gets "target did not answer"; in `wrench dbg` a second Ctrl+C gives the
  prompt back.
* A terminate cannot end it.
* A runtime error in it still halts the program when a debugger is
  attached, but without markers the debugger cannot tell the line; it
  shows the last line that had one.

The same holds for a module without debug information that a debugged
program imports. The debuggers warn when they load such a program:

```text
the program was compiled without debug information (-debug or -debugsource): it runs, but cannot stop at breakpoints or step
```

## Summary

| Situation | Debugger answers | Pause | Terminate | What to do |
|---|---|---|---|---|
| firmware between calls, calling `tick()` | yes | at the next line the firmware runs | nothing to end (a yielded call: see below) | call `tick()` often |
| firmware blocked between calls without `tick()` | no | no | no | wait with a timeout, `tick()` each time |
| script running, debug information | yes, every 20 ms | within about 20 ms | on the next line | nothing |
| empty endless loop, debug information | yes | yes, on the loop line | yes | nothing |
| script halted at a stop | yes, `idle()` runs | already stopped | yes | feed watchdogs in `idle()` |
| native blocking, without `poll()` | no | after it returns | after it returns | call `poll()` while waiting |
| native blocking, with `poll()` | yes | after it returns | after it returns | return now and then if it may wait forever |
| program 0 yielded | yes, through `tick()` | at the next line once continued | on the next line once continued (`WR_ERR_debugger_terminated`) | nothing |
| code without debug information running | no | no | no | compile with `-debug` |
