# Scenarios

Five complete cases, each with its code and what happens step by step:
event driven ESP32-S3 firmware that keeps its scripts in PSRAM, a desktop
application, a field build, firmware debugged without its sources, and the
debug demo with two programs on one target. They build on
[Adding the debugger to your firmware](firmware.md) and
[Blocking code, yield and polling](blocking.md).

## 1. Event driven ESP32-S3 firmware with PSRAM

The firmware: an ESP32-S3 with PSRAM. Other tasks (sensors, the network, a
button interrupt) produce events; a script decides what to do with each.
The script is compiled on the PC and stored in a file system on the
board's flash; the firmware loads it at boot, into PSRAM, together with
everything else wrench allocates. A debugger can attach over WiFi at any
time, and may upload and restart the script.

The ESP-IDF code below follows `examples/debug_targets/esp32_idf` and
uses its `TcpDebugTransport` and `wifiStart()`; it has not been built for
a device in this form. A PC version of the same loop (a thread posting
events, a TCP transport on localhost) was built and debugged to check the
behaviour described here.

### The script

```wrench
// event handler run by the firmware
var count = 0;
var last = 0;
var log[] = { 0, 0, 0, 0 };

function onEvent( type, value )
{
	count += 1;
	last = value * 2;
	log[count % 4] = last;
	if ( type == 2 )
	{
		println( "alarm ", value );
	}
	return last;
}

println( "script loaded" );
```

Run on its own (`wrench r event.w`) it only runs the global code.

Output:

```text
script loaded
```

It is compiled with its source, so a debugger can attach without the
file, and copied to the board's storage (a SPIFFS, LittleFS or FAT
partition image, or an upload of your own):

```bash
wrench c -debugsource event.w main.bin
```

### The firmware

```cpp
#include "wrench.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <stdio.h>

// TcpDebugTransport and wifiStart() as in examples/debug_targets/esp32_idf/main/main.cpp
static TcpDebugTransport debugLink;
static WrenchDebugServer* debugger = 0;

struct ScriptEvent
{
	int32_t type;
	int32_t value;
};

// other tasks post here, only the script task receives
static QueueHandle_t events = 0;

// everything wrench allocates comes from PSRAM
static void* psramAlloc( size_t size )
{
	return heap_caps_malloc( size, MALLOC_CAP_SPIRAM );
}

static void psramFree( void* p )
{
	heap_caps_free( p );
}

static void print( WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr )
{
	for ( int i = 0; i < argn; ++i )
	{
		WRValue::MallocStrScoped text( argv[i] );
		if ( text )
		{
			debugger->print( text, (int)text.size() );
		}
	}
}

static void println( WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr )
{
	print( c, argv, argn, retVal, usr );
	debugger->print( "\n" );
}

// the compiled script from storage, in a buffer from wr_malloc (PSRAM)
static uint8_t* loadScript( const char* path, int& size )
{
	FILE* f = fopen( path, "rb" );
	if ( !f )
	{
		return 0;
	}
	fseek( f, 0, SEEK_END );
	size = (int)ftell( f );
	fseek( f, 0, SEEK_SET );
	uint8_t* bytes = (uint8_t*)wr_malloc( size > 0 ? size : 1 );
	if ( bytes && fread(bytes, 1, size, f) != (size_t)size )
	{
		wr_free( bytes );
		bytes = 0;
	}
	fclose( f );
	return bytes;
}

// the one task that owns the state, the context, the server and the transport
static void scriptTask( void* )
{
	debugLink.begin( 5050 );

	WRState* w = wr_newState();
	wr_registerFunction( w, "print", print );
	wr_registerFunction( w, "println", println );

	debugger = new WrenchDebugServer( w, &debugLink );
	debugger->setHostDriven( true );
	debugger->setLaunchable( true );

	int size = 0;
	uint8_t* bytes = loadScript( "/storage/main.bin", size );
	WRContext* script = bytes ? wr_newContext( w, bytes, size, true ) : 0;
	if ( script )
	{
		debugger->attach( script, "event.w" );
	}

	// the global code sets the script up; onEvent() can be called once it
	// ran to the end
	bool ready = script && wr_executeContext( script );

	for ( ;; )
	{
		int ticks = debugger->tick();
		if ( ticks & WRD_TickProgramUploaded )
		{
			uint8_t* upload;
			int uploadSize;
			if ( debugger->takeUpload(upload, uploadSize) )
			{
				wr_destroyContext( script );
				script = wr_newContext( w, upload, uploadSize, true );
				debugger->attach( script, "event.w" );
				ready = false;
			}
		}
		if ( (ticks & WRD_TickRunRequested) && script )
		{
			wr_resetContext( script );
			ready = wr_executeContext( script ) != 0;
		}

		if ( !ready )
		{
			// no program that can take events: they wait in the queue
			vTaskDelay( pdMS_TO_TICKS(10) );
			continue;
		}

		// wait for an event, but never so long that the debugger goes unanswered
		ScriptEvent e;
		if ( xQueueReceive(events, &e, pdMS_TO_TICKS(10)) != pdTRUE )
		{
			continue;
		}

		WRValue args[2];
		wr_makeInt( &args[0], e.type );
		wr_makeInt( &args[1], e.value );
		if ( !wr_callFunction(script, "onEvent", args, 2) )
		{
			ESP_LOGW( "wrench", "onEvent failed: %d", (int)wr_getLastError(w) );
		}
	}
}

// from any other task: post an event, never touch wrench
void postEvent( int32_t type, int32_t value )
{
	ScriptEvent e = { type, value };
	xQueueSend( events, &e, 0 ); // dropped when the queue is full
}

extern "C" void app_main( void )
{
	// before wrench allocates anything
	wr_setGlobalAllocator( psramAlloc, psramFree );

	// mount the storage partition at /storage here
	wifiStart();

	events = xQueueCreate( 32, sizeof(ScriptEvent) );
	xTaskCreate( scriptTask, "wrench", 8192, NULL, 5, NULL );
}
```

### What happens

**At boot.** `wr_setGlobalAllocator()` comes first, before anything is
allocated: from then on the state, the context, the script's arrays and
strings, the debug server's state (about 4 KB with the default sizes) and
any program a debugger uploads come from PSRAM. `loadScript()` reads the
file into a buffer from `wr_malloc()`, and `wr_newContext( ..., true )`
takes it over: the context frees it with `wr_free()` when it is destroyed.

**The global code.** `wr_executeContext()` runs the script's top level
once: it creates `count`, `last` and `log`, and prints "script loaded"
(which goes nowhere: `print()` only sends while a debugger is attached).
Functions cannot be called before that. A `wr_callFunction()` on a context
whose global code has not run fails with
`WR_ERR_run_must_be_called_by_itself_first`; the `ready` flag keeps the
loop from trying.

**The loop.** The script task waits for events at most 10 ms at a time
and calls `tick()` between. An event becomes a call of `onEvent( type,
value )`. Nothing else in the firmware touches wrench:
`postEvent()` only queues. This is [the one-task rule](firmware.md#the-one-task-rule).

**A debugger attaches.** The script already runs; you attach, in VS Code
with:

```json
{
	"type": "wrench",
	"request": "attach",
	"name": "Attach to the S3",
	"program": "${workspaceFolder}/event.w",
	"transport": "tcp",
	"host": "wrench-target",
	"port": 5050
}
```

The next `tick()` answers. The target is between calls, so there is no
stack, but Globals can already be read. Set a breakpoint on line 9, `last
= value * 2;`.

**An event hits the breakpoint.** The next event calls `onEvent()`. At
line 9 the VM calls the server, which halts the script there: the script
task is now inside `wr_callFunction()`, in the server's stop loop, reading
the link and calling `idle()` (a `vTaskDelay( 1 )`, so the idle task runs
and the task watchdog stays quiet). VS Code shows the stop; Locals has
`type` and `value`. Other tasks keep running and keep posting events; the
queue fills, and once it is full `postEvent()` drops them.

**Continue.** `onEvent()` returns to the firmware with `value * 2`. There
is no "ended" event: only the end of program 0's global code is reported
as the end of a run. The thread in VS Code shows as running, and the next
event stops at line 9 again.

**Stepping out of the call.** Step Over on line 15, `return last;`, returns
to the firmware; the step is still pending and ends on the first line of
the next `onEvent()` call (line 8). Step Out of `onEvent()` has no script
caller to land in; it ends only at a breakpoint or a pause. See
[Steps across calls from the firmware](breakpoints.md#steps-across-calls-from-the-firmware).

### Restarting in the middle of debugging

A launch configuration (with `"request": "launch"` and the same transport)
uploads a fresh build and runs it; Restart in VS Code terminates the
program and does that again. What that means for this firmware depends on
where the script task is at that moment.

**Idle between events.** The usual case. The terminate finds no script
code running and does nothing. The upload arrives through `tick()`: once
it is complete, the server stops debugging the old program, and `tick()`
returns `WRD_TickProgramUploaded`. The firmware destroys the old context
(its PSRAM is freed), makes a new one of the upload and attaches it as
program 0; `ready` is false, so events wait in the queue. The debugger sets
its breakpoints for the new program and launches it: `tick()` returns
`WRD_TickRunRequested`, the firmware resets the context and runs its global
code, and from then on events reach the new `onEvent()`. Both flags can
come in the same `tick()`; the upload is handled first.

**Halted in `onEvent()`.** The terminate arrives in the stop loop. The
script ends on the spot: `wr_callFunction()` returns 0 and
`wr_getLastError()` is `WR_ERR_debugger_terminated`, which the firmware
logs and goes on. The session ends with a detach, which clears the
breakpoints; the next session's upload and launch are carried out in
`tick()` as above. `r` in `wrench dbg` terminates and launches without
detaching; it takes the breakpoints off the target first, so the next
event cannot halt at one before the launch request arrives, and sets them
again once the new run halts on its first line.

**Running in `onEvent()`.** A long handler (a loop, or a native that waits
and calls `poll()`) is inside the VM: an upload or a launch that arrives
now is refused as busy ("upload failed: target is busy executing"). A
terminate is not: it ends the call at the next poll, within about 20 ms
(after the native returns, if one is waiting), and the following upload
goes through `tick()`.

**Old pointers.** After an upload the firmware works with a new context.
Anything taken from the old one, a `WRContext*` kept elsewhere, a
`WRFunction*` from `wr_getFunction()`, a `WRValue*` from
`wr_getGlobalRef()`, points into freed memory. After `wr_resetContext()`
the context pointer stays valid, but the globals were cleared and what
they held is freed. This firmware calls `onEvent` by name and keeps only
`script`, which the loop updates; firmware that caches more must refresh
it after every swap and every run.

**Pending events.** Events queued for the old program are delivered to the
new one once its global code ran. If they make no sense to it, empty the
queue when you swap programs (`xQueueReset( events )` after the attach).

**Restart in VS Code.** For a launch configuration, Restart terminates and
launches again, uploading the current build; with `"upload": false` it
restarts the program the board holds, without uploading. For an attach
configuration, Restart disconnects and attaches again; the program is
never uploaded or run again (see
[Stop, disconnect and restart](vscode.md#stop-disconnect-and-restart) for
a call that is halted at that moment).

## 2. A desktop application with wrench.json

The application: a Windows editor that embeds wrench for user macros. It
registers its own natives, loads only the math and string libraries, and
runs macros on a worker thread.

Describe the application in a `wrench.json` next to the macros:

```json
{
	"libraries": [ "math", "string" ],
	"functions": [
		{ "name": "openDoc", "params": [ "path", "mode?" ], "doc": "Opens a document, returns its handle", "returns": 1 },
		{ "name": "ui::alert", "params": [ "text" ], "doc": "Shows a message box" },
		"log( values... )"
	]
}
```

`libraries` lists what the application loads (missing or `"all"` means
every library). `functions` lists the natives it registers with
`wr_registerFunction()` (`name`) or `wr_registerLibraryFunction()`
(`lib::name`), as objects or signature strings: a parameter ending in `?`
is optional, one ending in `...` takes any number of arguments, and
`returns` is what the local VM answers in its place (default 0). List the
functions of modules the macros import at run time too, and `print` /
`println` if the application has them. The language server uses the same
file to check calls (`"undeclaredCalls"`: `warning`, the default, `error`
or `ignore`); an `"include"` list adds folders for `include "file"`. The full format is in
[Describing your host application](../vscode/wrench-json.md).

A macro that uses them:

```wrench
var doc = openDoc( "a.txt" );
log( "opened ", doc );
ui::alert( "done" );
ui::confirm( "really?" );
var x = math::sqrt( 16 );
```

**Debugging locally first.** F5 on a macro runs it in the adapter's VM,
which imitates the application from that file: only math and string are
loaded, `openDoc()` returns 1, `ui::alert()` and `log()` return 0, each
saying so once in the Debug Console. A call to anything else stops with a
runtime error naming the function, as the application would fail. The
macro's logic can be stepped through before the application is involved.
The same with `wrench dbg -project wrench.json macro.w` (without
`-project` the nearest `wrench.json` is found by itself), where `r` runs it
and `c` lets the error end it:

```text
host application described by wrench.json
connected to local, wrench 8.0.0, max packet 1024 bytes
macro.w: 5 lines, 0 functions. 'h' for help.
(wdb:idle) r
[wrench] openDoc is provided by the host application, here it returns 1
[wrench] log is provided by the host application, here it returns 0
[wrench] ui::alert is provided by the host application, here it returns 0

*** runtime error WR_ERR_lib_function_not_found at line 4
*** 'ui::confirm' is not provided by the target
in <global>, line 4
     2  log( "opened ", doc );
     3  ui::alert( "done" );
 >   4  ui::confirm( "really?" );
     5  var x = math::sqrt( 16 );
(wdb) c

[ended with WR_ERR_lib_function_not_found near line 4]
['ui::confirm' is not provided by the target]
(wdb:idle) q
```

See [Describing your host application](../vscode/wrench-json.md).

**Debugging inside the application.** Build the application with
`WRENCH_INCLUDE_DEBUG_CODE` and `WRENCH_WIN32_TCP`, and give the worker
thread that runs the macros a debug server on localhost:

```cpp
#include "wrench.h"

// all of this lives on the macro thread
static WrenchDebugTcpServerTransport debugLink;
static WrenchDebugServer* debugger = 0;

void macroThread( WRState* w, const unsigned char* code, int codeLen )
{
	debugLink.listen( 5050, true ); // this machine only
	debugger = new WrenchDebugServer( w, &debugLink );
	debugger->setHostDriven( true );

	// code compiled with WR_EMBED_DEBUG_CODE | WR_EMBED_SOURCE_CODE
	WRContext* macro = wr_newContext( w, code, codeLen );
	debugger->attach( macro, "macro.w" );
	wr_executeContext( macro );

	for ( ;; )
	{
		debugger->tick();
		// wait (with a timeout) for the UI to ask for a macro call, then
		// wr_callFunction( macro, ... )
	}
}
```

```json
{
	"type": "wrench",
	"request": "attach",
	"name": "Attach to the editor",
	"program": "${workspaceFolder}/macro.w",
	"transport": "tcp",
	"host": "127.0.0.1",
	"port": 5050
}
```

What happens:

* Attach connects to the running application; breakpoints stop the macro
  thread inside the call the application made. The UI thread is not
  involved and keeps painting; if it waits for the macro's result, it
  waits as long as you look at the breakpoint.
* The application's real natives run. A call to a native the application
  lacks fails with `WR_ERR_function_not_found`; the debugger looks the
  name hash up in the macro's source and in `wrench.json` and says which:
  "'ui::confirm' is not provided by the target".
* Stopping the session detaches; the macro runs on. Without
  `setLaunchable( true )` the application refuses launches and uploads,
  which is usually right for an application: it decides which macros run.
* `listen( 5050, true )` binds to the loopback interface only. Without it,
  anyone on the network could connect.

The application can use the missing function's hash itself: after
`WR_ERR_function_not_found` or `WR_ERR_lib_function_not_found`,
`wr_getMissingFunctionHash()` returns `wr_hashStr()` of the name that was
called. A not-found handler (`wr_setOnCallbackNotFound`,
`wr_setOnLibCallbackNotFound`) may answer some names itself and fail the
call for others by setting `w->err`.

## 3. A field build

The product ships with its script in flash. Service technicians should be
able to look into a misbehaving unit over its service UART: attach, set
breakpoints, read variables, restart the script. Nobody should be able to
load a different program.

```cpp
debugger = new WrenchDebugServer( w, &debugLink );
debugger->setHostDriven( true );

// a debugger may restart the program in flash, never replace it
debugger->setLaunchable( true );
debugger->setAllowUpload( false );

// a UART cannot tell when the laptop goes away: let go of a halted
// script after 5 s of silence
debugger->setLinkTimeout( 5000 );

script = wr_newContext( w, blink_bytecode, blink_bytecodeSize );
debugger->attach( script, "blink.w" );
```

The main loop is the one from
[setLaunchable()](firmware.md#letting-a-debugger-launch-and-upload-setlaunchable);
the `WRD_TickProgramUploaded` branch can stay, it is never taken.

What a technician sees:

* An attach configuration (or `wrench dbg -target serial:COM5 -attach`)
  works as on any target. With the script compiled with `-debugsource`,
  no files are needed at all (see [scenario 4](#4-debugging-firmware-without-the-sources-at-hand)).
* A launch configuration with upload on is refused before anything is
  sent:

  ```text
  the target does not accept uploaded programs (its firmware called setAllowUpload(false)): use "request": "attach" to debug the program it runs, or "upload": false to launch that program again
  ```

  With `"upload": false` the launch restarts the program in flash, with
  fresh globals, and stops on entry if asked to. In `wrench dbg`, `r` in an
  `-attach` session does the same.
* The laptop crashes, or the cable is pulled, while the script is halted
  at a breakpoint. Without a link timeout the unit would stay halted until
  someone connects again. With `setLinkTimeout( 5000 )` it treats five
  seconds of silence as a disconnect: breakpoints are cleared and the
  script runs on. VS Code and `wrench dbg` ping a halted target every
  second, so the timeout never strikes during a session.

A unit that must also survive a script that hangs needs a watchdog; feed
it in the transport's `idle()` so a long look at a breakpoint does not
reset the unit ([Watchdogs](firmware.md#watchdogs-and-other-tasks)).

## 4. Debugging firmware without the sources at hand

A board from the field runs a script nobody has the exact source of. If
it was built with `-debugsource`, the bytecode carries the text of every
file, and the debugger can work from that alone:

```bash
wrench ch -debugsource script/blink.w blink.h blink
```

**From VS Code**, attach without `program`:

```json
{
	"type": "wrench",
	"request": "attach",
	"name": "Attach, source from the board",
	"transport": "serial",
	"serialPort": "COM5",
	"baudRate": 115200
}
```

The adapter reads the bytecode, the symbols and the source over the link
and shows the source as a read-only document. Lines always match: it is
exactly what the device runs. Breakpoints, stepping and evaluation work as
with a `.w` file.

**From the console**, attach without a file:

```text
> wrench dbg -target tcp:127.0.0.1:5178 -attach
connected to tcp 127.0.0.1:5178, wrench 8.0.0, max packet 1024 bytes
read the program from the target (1037 bytes, source embedded)
target program: 36 lines, 3 functions. 'h' for help.
```

[The console debugger](console.md#attaching-without-the-source) shows the
whole session.

**From a local copy** of the compiled program: give the `.bin` as
`program` (VS Code) or as the file (`wrench dbg main.bin`). It is used as
it is: an attach compares it with what the target runs, a launch uploads
it unchanged, and the source shown is the embedded one.

A program compiled with `-debug` only still gives call stacks, variables
and breakpoints by line number, but no source:

```text
The target program carries no source (compile with -debugsource), only stacks and variables are available.
```

Without debug information at all it runs, but cannot stop.

## 5. The debug demo: two programs on one target

`examples/debug_demo` runs `wrench dbgserver` as a stand-in for a board
that runs a program of its own (`sensor.w`, called ten times a second) and
takes uploads of another (`main.w`, which includes two files and compiles
a module at run time):

```bash
wrench dbgserver -target tcp:5050 -context sensor.w -v main.w
```

Step by step:

1. The server preloads `main.w` as program 0 and attaches `sensor.w` as
   program 1: "attached sensor.w as program 1, global code: WR_ERR_None".
   It runs `sensor.w`'s global code and then calls its `tick()` every
   100 ms, like firmware with a context of its own.
2. VS Code (or `wrench dbg`) connects, uploads `main.w` and launches it,
   stopping on entry. The target now has three programs once `main.w`
   imports its module: `main.w`, `sensor.w` and "import 2".
3. A breakpoint in `sensor.w` waits while `main.w` is halted: the target is
   single threaded. When `main.w` ends, the session stays open, because
   the target keeps running `sensor.w`, and the breakpoint hits in
   `tick()`.
4. Restarting uploads `main.w` again and runs it from the start; `sensor.w`
   keeps its globals, since it belongs to the target and is never
   restarted by a debugger.

[Several files, programs and modules](programs.md#the-debug-demo) walks
through both demos in VS Code; [The console debugger](console.md#debugging-a-remote-target)
shows the same target from `wrench dbg`.
