# Several states, threads and RTOS tasks

This page is for hosts with more than one thread: desktop applications with
worker threads, and firmware on an RTOS such as FreeRTOS on the ESP32. It
gives the rule wrench needs, the script task pattern that follows from it,
and notes on memory and watchdogs.

## The rule: one state, one thread

wrench has no locks. A state, its contexts and every value in them may be
used by one thread at a time, and in practice by one thread only: the one
that created the state. That covers every API call taking a `WRState*`, a
`WRContext*` or a `WRValue*` of that state, and the debug server attached
to it.

Different states are independent and may run on different threads or cores
at the same time. The little state the VM keeps outside a `WRState` (a
scratch value, the allocation failure flag) is thread local on Windows,
Linux, macOS and ESP-IDF (`WRENCH_THREAD_LOCAL`), so states on different
threads do not race. On bare metal targets those are plain statics, which
is fine as long as only one state runs at a time (no scripts from an
interrupt handler while the main loop runs one). The seed of `std::rand`
belongs to the state (`WRState::randSeed`), so states on different threads
draw their own random numbers. `wr_loadStdLib()` starts it at the global
`wr_Seed`, which is the seed of a separate generator for the host
(`wr_randEx( from, to )`); that one is shared and not thread safe.

Things that can be shared between threads:

* bytecode images: the VM only reads them, so one image in flash or memory
  can be loaded by states on several threads at once;
* the allocator set with `wr_setGlobalAllocator()`, if it is thread safe
  (`malloc()` is);
* nothing else.

## The script task pattern

The simplest design that follows from the rule: one thread (or RTOS task)
owns everything wrench, and the other threads only talk to it through a
queue.

* The script thread creates the state, registers the natives, loads the
  contexts, creates and owns the debug server, and runs the host loop.
* Other threads (interrupt handlers, network callbacks, sensor tasks) never
  call wrench. They post small events (an id and a value) to the script
  thread's queue.
* The script thread waits on the queue *with a timeout*, calls a script
  function for each event, and between events services whatever must run
  periodically: the debugger's `tick()`, periodic script calls, a
  watchdog.
* Natives that need another thread's work post a request and return; the
  answer comes back later as an event.

The timeout matters: a thread blocked forever on an empty queue cannot
answer a debugger, and the debugger link is serviced only by `tick()` in
that loop (and while a script runs).

A PC version of the pattern with standard C++ threads:

```cpp
// the script thread pattern on a PC: one thread owns the state, the others
// only post events to it (C++11)
#include "wrench.h"

#include <stdio.h>
#include <string.h>

#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

struct Event
{
	int source;
	int value;
};

// the only thing the other threads touch
class EventQueue
{
public:
	void post( const Event& e )
	{
		std::lock_guard<std::mutex> lock( m_mutex );
		m_events.push_back( e );
		m_wake.notify_one();
	}

	// waits up to timeoutMs for an event, false when none came
	bool wait( Event& e, int timeoutMs )
	{
		std::unique_lock<std::mutex> lock( m_mutex );
		if ( !m_wake.wait_for(lock, std::chrono::milliseconds(timeoutMs), [this] { return !m_events.empty(); }) )
		{
			return false;
		}
		e = m_events.front();
		m_events.pop_front();
		return true;
	}

private:
	std::mutex m_mutex;
	std::condition_variable m_wake;
	std::deque<Event> m_events;
};

static void print( WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr )
{
	char buf[256];
	for ( int i = 0; i < argn; ++i )
	{
		printf( "%s", argv[i].asString(buf, sizeof(buf)) );
	}
}

static const char* source =
	"var total = 0;\n"
	"function onEvent( source, value )\n"
	"{\n"
	"	total += value;\n"
	"	return total;\n"
	"}\n";

// owns the state and the context, nothing else calls wrench
static void scriptThread( EventQueue* queue, int expected )
{
	WRState* w = wr_newState();
	wr_registerFunction( w, "print", print );

	unsigned char* bytecode;
	int size;
	if ( wr_compile(source, (int)strlen(source), &bytecode, &size) != WR_ERR_None )
	{
		wr_destroyState( w );
		return;
	}
	WRContext* context = wr_run( w, bytecode, size, true );
	WRFunction* onEvent = wr_getFunction( context, "onEvent" );

	int handled = 0;
	int idle = 0;
	while ( handled < expected )
	{
		Event e;
		if ( !queue->wait(e, 50) )
		{
			++idle; // the place to service a debugger link or a watchdog
			continue;
		}
		WRValue args[2];
		args[0].setInt( e.source );
		args[1].setInt( e.value );
		wr_callFunction( context, onEvent, args, 2 );
		++handled;
	}

	printf( "handled %d events, total %d\n", handled, wr_getGlobalRef(context, "total")->asInt() );
	wr_destroyState( w );
}

static void producer( EventQueue* queue, int source, int count )
{
	for ( int i = 1; i <= count; ++i )
	{
		Event e = { source, i };
		queue->post( e );
	}
}

int main()
{
	EventQueue queue;
	std::thread script( scriptThread, &queue, 300 );
	std::thread a( producer, &queue, 1, 100 );
	std::thread b( producer, &queue, 2, 100 );
	std::thread c( producer, &queue, 3, 100 );
	a.join();
	b.join();
	c.join();
	script.join();
	return 0;
}
```

Output:

```text
handled 300 events, total 15150
```

Build it with `-pthread` on Linux. On FreeRTOS the queue is
`xQueueCreate()` / `xQueueSend()` / `xQueueReceive()` with a timeout in
ticks; [Quick start: ESP32 with ESP-IDF](quickstart-esp32.md) shows that
version.

## Several states

Use several states when scripts must really run in parallel, on different
cores or threads, or must be isolated from each other completely (separate
natives, separate mailbox). Each state then has its own script thread and
its own queue. Scripts of different states cannot see each other's values
at all; exchange data as plain numbers or serialized buffers
([Serialization](data.md#serialization)) through your own queues.

Within one thread, several contexts of one state are usually the better
choice: they share the registered natives and cost only their own memory
([Several contexts in one state](running.md#several-contexts-in-one-state)).

## Memory

All allocations go through the global allocator (`wr_setGlobalAllocator()`,
`malloc()` by default), from whichever thread runs the state. On an ESP32
with PSRAM, an allocator that takes memory from PSRAM keeps large scripts
and their data out of the internal RAM (ESP-IDF code, not compiled for this
book):

```cpp
#include "esp_heap_caps.h"

static void* psramAlloc( size_t size )
{
	return heap_caps_malloc( size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT );
}

// first thing in app_main(), before any other wrench call
wr_setGlobalAllocator( psramAlloc, heap_caps_free );
```

The allocator is global to the whole program, not per state, and must be
set before wrench allocates anything: memory is freed with the allocator
that is set at the time. PSRAM is slower than internal RAM; values on the
stack and the VM's working state are touched on every instruction, so a
script runs noticeably slower when its context is in PSRAM. A wrapper that
puts small blocks into internal RAM and large ones into PSRAM is a good
compromise. See also [Small targets](small-targets.md#the-allocator).

Give the script task a native stack that fits the VM, the buffers of your
natives, one more VM level for each native that calls back into a script
or call into an imported module, and the compiler if the task compiles
source ([Compiling](compiling.md#builds-without-the-compiler)). Script
functions calling each other do not use native stack; they use the
context's value stack. The ESP-IDF example gives its task 8 KB.

## Watchdogs

Nothing in the VM yields to other tasks or feeds a watchdog while script
code runs. A call into a script that runs longer than the watchdog period
starves the idle task of its core, and the task watchdog fires, debugger
attached or not. The ways out:

* keep calls short: event handlers and periodic functions that return,
  which is the pattern above;
* define `WRENCH_TIME_SLICES` and set a slice limit, so a long call returns
  yielded; then delay for a tick and `wr_continue()` it
  ([Time slices](scheduling.md#time-slices));
* give scripts a native that sleeps (`vTaskDelay()`) for them to call in
  long loops;
* adjust the task watchdog for the script task.

While a debugger holds a script halted at a breakpoint, the VM loops inside
the call waiting for the debugger and calls the transport's `idle()` on
every pass. Make `idle()` sleep a tick (the ESP-IDF transports call
`vTaskDelay( 1 )`), so the idle task runs and the watchdog stays quiet
during a debug session.
