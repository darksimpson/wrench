# Quick start: ESP32 with ESP-IDF

This page sets up wrench in an ESP-IDF project the way larger firmware
does: a task that owns the scripts, bytecode built in or loaded from
storage into PSRAM, natives, events from other tasks and interrupts through
a queue, the debugger on a UART or over WiFi, and the watchdog. It builds
on `examples/debug_targets/esp32_idf` in the repository.

> [!NOTE]
> The code on this page was compiled on a PC against stand-ins for the
> ESP-IDF headers to check the wrench calls, but not built with ESP-IDF or
> run on a board here. `examples/debug_targets/esp32_idf` is the tested
> starting point; this page extends it with the event queue, PSRAM and
> loading from storage.

## The project

```text
myproject/
	CMakeLists.txt
	main/
		CMakeLists.txt
		main.cpp
		wrench.h
		wrench.cpp
		wrench_config.h
		script.h
		script.w
```

The top level `CMakeLists.txt` is the usual ESP-IDF one:

```cmake
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(myproject)
```

`main/CMakeLists.txt` adds the single file build to the main component:

```cmake
idf_component_register(
	SRCS "main.cpp" "wrench.cpp"
	INCLUDE_DIRS "."
	REQUIRES driver esp_timer
)
```

Add `spiffs` (or a LittleFS component) to `REQUIRES` if images are loaded
from a file system, and the network components if the debugger runs over
WiFi, as the example does. ESP-IDF 5.0 or later is assumed; from 5.3 on,
`driver` can be replaced by `esp_driver_uart esp_driver_gpio`.

`main/wrench_config.h` holds the build flags; `wrench.h` finds it next to
itself:

```cpp
// picked up automatically by wrench.h when it sits next to it
#define WRENCH_INCLUDE_DEBUG_CODE
#define WRENCH_DEBUG_MAX_PAYLOAD 1024
#define WRENCH_DEFAULT_STACK_SIZE 128
```

The ESP32 has room for the compiler, but compiling on the PC and shipping
bytecode is still the better default; add `WRENCH_WITHOUT_COMPILER` to save
the flash. Leave `WRENCH_INCLUDE_DEBUG_CODE` out of production builds that
do not need the debugger. On ESP-IDF the VM's per thread scratch state is
thread local, so states on both cores may run at the same time
([Threads](threads.md)).

## The script

```wrench
// script.w: loop() runs every 100 ms, onEvent() for each event
var presses = 0;

function onEvent( id, value )
{
	if ( id == 1 && value == 0 )
	{
		presses = presses + 1;
		println( "button pressed ", presses, " times" );
	}
}

function loop()
{
	led( presses % 2 );
	return 100;
}

println( "script started" );
```

Compile it into a header, with debug information and source so the
debugger can show it:

```bash
wrench ch -debugsource main/script.w main/script.h script
```

## main.cpp

```cpp
#include "wrench.h"
#include "script.h" // wrench ch -debugsource script.w main/script.h script

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <stdio.h>

#define LED_GPIO GPIO_NUM_2
#define BUTTON_GPIO GPIO_NUM_0

static const char* TAG = "wrench";

// what other tasks and interrupts send to the script task
struct ScriptEvent
{
	int id;
	int value;
};

static QueueHandle_t g_events;

static uint32_t nowMs()
{
	return (uint32_t)(esp_timer_get_time() / 1000);
}

// the debugger's byte link on UART0, as in examples/debug_targets/esp32_idf
class UartDebugTransport : public WrenchDebugTransport
{
public:
	explicit UartDebugTransport( uart_port_t port ) : m_port( port ) {}

	bool begin( int baud )
	{
		uart_config_t config = {};
		config.baud_rate = baud;
		config.data_bits = UART_DATA_8_BITS;
		config.parity = UART_PARITY_DISABLE;
		config.stop_bits = UART_STOP_BITS_1;
		config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
		config.source_clk = UART_SCLK_DEFAULT;
		if ( uart_driver_install(m_port, 4096, 4096, 0, NULL, 0) != ESP_OK )
		{
			return false;
		}
		return uart_param_config( m_port, &config ) == ESP_OK;
	}

	bool write( const uint8_t* data, uint32_t len )
	{
		return uart_write_bytes( m_port, (const char*)data, len ) == (int)len;
	}

	int read( uint8_t* data, uint32_t maxLen )
	{
		size_t buffered = 0;
		uart_get_buffered_data_len( m_port, &buffered );
		if ( buffered == 0 )
		{
			return 0;
		}
		if ( buffered > maxLen )
		{
			buffered = maxLen;
		}
		return uart_read_bytes( m_port, data, buffered, 0 );
	}

	uint32_t millis() { return nowMs(); }

	// while the debugger holds a script halted: let other tasks run
	void idle() { vTaskDelay( 1 ); }

private:
	uart_port_t m_port;
};

static UartDebugTransport debugLink( UART_NUM_0 );
static WrenchDebugServer* debugger = 0;

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

static void led( WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr )
{
	if ( argn > 0 )
	{
		gpio_set_level( LED_GPIO, argv[0].asInt() ? 1 : 0 );
	}
}

// any task may call this, it never touches wrench
void postScriptEvent( int id, int value )
{
	ScriptEvent e = { id, value };
	xQueueSend( g_events, &e, 0 );
}

// the button interrupt forwards to the script task
static void IRAM_ATTR onButton( void* arg )
{
	ScriptEvent e = { 1, gpio_get_level(BUTTON_GPIO) };
	xQueueSendFromISR( g_events, &e, NULL );
}

// large blocks in PSRAM, the rest (and everything when there is no PSRAM)
// in internal RAM
static void* scriptAlloc( size_t size )
{
	void* p = (size >= 256) ? heap_caps_malloc( size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT ) : 0;
	return p ? p : heap_caps_malloc( size, MALLOC_CAP_8BIT );
}

// a compiled image from a mounted file system, 0 when there is none
static unsigned char* loadImage( const char* path, int* size )
{
	FILE* file = fopen( path, "rb" );
	if ( !file )
	{
		return 0;
	}
	fseek( file, 0, SEEK_END );
	long length = ftell( file );
	fseek( file, 0, SEEK_SET );
	unsigned char* image = (unsigned char*)wr_malloc( length );
	if ( image && fread(image, 1, length, file) == (size_t)length && wr_isBytecodeValid(image, length) )
	{
		*size = (int)length;
	}
	else
	{
		wr_free( image );
		image = 0;
	}
	fclose( file );
	return image;
}

static void wrenchTask( void* )
{
	debugLink.begin( 115200 );

	gpio_reset_pin( LED_GPIO );
	gpio_set_direction( LED_GPIO, GPIO_MODE_OUTPUT );
	gpio_set_direction( BUTTON_GPIO, GPIO_MODE_INPUT );
	gpio_set_intr_type( BUTTON_GPIO, GPIO_INTR_ANYEDGE );
	gpio_install_isr_service( 0 );
	gpio_isr_handler_add( BUTTON_GPIO, onButton, NULL );

	WRState* w = wr_newState();
	wr_registerFunction( w, "print", print );
	wr_registerFunction( w, "println", println );
	wr_registerFunction( w, "led", led );

	debugger = new WrenchDebugServer( w, &debugLink );
	debugger->setHostDriven( true ); // the task calls loop() and onEvent() itself
	debugger->setLaunchable( true ); // and carries out launches and uploads below

	// a program from storage if there is one, else the one built in
	int size = 0;
	unsigned char* image = loadImage( "/spiffs/script.bin", &size );
	WRContext* script = image ? wr_newContext( w, image, size, true )
							  : wr_newContext( w, script_bytecode, script_bytecodeSize );
	debugger->attach( script, "script.w" );
	bool ready = script && wr_executeContext( script );
	if ( !ready )
	{
		ESP_LOGW( TAG, "script failed: %d\n", (int)wr_getLastError(w) );
	}

	uint32_t nextCall = nowMs();
	for ( ;; )
	{
		// the debugger, while no script runs
		int requests = debugger->tick();
		if ( requests & WRD_TickProgramUploaded )
		{
			uint8_t* bytes;
			int length;
			if ( debugger->takeUpload(bytes, length) )
			{
				wr_destroyContext( script );
				script = wr_newContext( w, bytes, length, true );
				debugger->attach( script, "script.w" );
				ready = false;
			}
		}
		if ( requests & WRD_TickRunRequested )
		{
			wr_resetContext( script );
			ready = script && wr_executeContext( script );
			nextCall = nowMs();
		}

		// events from other tasks; the timeout keeps tick() running and
		// lets the idle task run, so the task watchdog stays quiet
		ScriptEvent e;
		if ( xQueueReceive(g_events, &e, pdMS_TO_TICKS(10)) == pdTRUE && ready )
		{
			WRValue args[2];
			args[0].setInt( e.id );
			args[1].setInt( e.value );
			wr_callFunction( script, "onEvent", args, 2 );
		}

		// the periodic call
		if ( ready && (int32_t)(nowMs() - nextCall) >= 0 )
		{
			WRValue* ret = wr_callFunction( script, "loop" );
			int delayMs = ret ? ret->asInt() : 1000;
			nextCall = nowMs() + (delayMs > 0 ? delayMs : 1);
		}
	}
}

extern "C" void app_main( void )
{
	wr_setGlobalAllocator( scriptAlloc, heap_caps_free ); // before any other wrench call
	g_events = xQueueCreate( 16, sizeof(ScriptEvent) );

	// mount the file system here if images come from one (esp_vfs_spiffs_register)

	xTaskCreate( wrenchTask, "wrench", 8192, NULL, 5, NULL );
}
```

## The script task

`wrenchTask` owns everything wrench: the state, the context, the debug
server and its transport. No other task calls wrench; that is the rule
wrench needs, since it has no locks ([Threads](threads.md)). The task has
8 KB of stack, which covers the VM, the natives, the UART calls and the
debug server's request handling; the server keeps its frame buffers on the
heap.

Its loop does four things, in an order that matters:

1. `debugger->tick()` services the debug link while no script runs, and
   returns what a debugger asked for: a new program (`takeUpload()`, then
   a new context in place of the old one) or a run from the start
   (`wr_resetContext()` and `wr_executeContext()`).
2. `xQueueReceive()` waits for an event for at most 10 ms. Waiting blocks
   the task, so lower priority tasks and the idle task run.
3. Each event becomes a call of the script's `onEvent()`.
4. `loop()` is called when it is due, with the delay it returned.

## Natives and events

`print()` and `println()` send script output to the debugger's console with
`debugger->print()`; without a debugger attached the text is dropped. A
firmware that logs instead would call `ESP_LOGI()` there. `led()` sets a
GPIO.

Other tasks call `postScriptEvent()`, interrupt handlers use
`xQueueSendFromISR()` as `onButton()` does. Events carry plain numbers;
anything larger is better kept in a buffer the event refers to by index,
since the script task must not share wrench values with other tasks.
Natives that need other tasks' work post a request and return; the result
comes back as an event.

## Loading bytecode

The program built into `script.h` is in flash and runs in place. A device
that gets new scripts without a firmware update loads them from storage:

* `loadImage()` reads a file compiled with `wrench c` into a buffer from
  `wr_malloc()`, checks it with `wr_isBytecodeValid()`, and
  `wr_newContext()` takes ownership of it, so the context frees it.
* `scriptAlloc()` becomes wrench's allocator before anything else
  allocates. Blocks of 256 bytes and more go to PSRAM, smaller ones to
  internal RAM, and everything to internal RAM on a board without PSRAM.
  `heap_caps_free()` frees memory from either. The image, large arrays and
  strings end up in PSRAM; the context with its stack, used on every
  instruction, usually stays internal.
* The file system must be mounted before the task starts
  (`esp_vfs_spiffs_register()` for SPIFFS); the path `/spiffs/script.bin`
  is an example.

Images can also come over the network, from a partition read with
`esp_partition_read()`, or from the debugger, which uploads into RAM with
`takeUpload()`.

## The debugger

The example uses the UART that is also the ESP-IDF console; the protocol
skips log lines, but moving the console to another UART (or to none) in
`idf.py menuconfig` is cleaner. `examples/debug_targets/esp32_idf` also has
a `TcpDebugTransport` for WiFi, chosen in menuconfig, which logs the board's
address at startup. Connect from VS Code with a `"transport": "serial"` or
`"transport": "tcp"` launch configuration, or with
`wrench dbg -target serial:COM5:115200 -attach main/script.w`.

`setHostDriven( true )` tells the debugger that the firmware keeps calling
into the script after the global code has ended, so a debug session does
not end there. `setLaunchable( true )` allows the debugger to restart and
replace program 0, which the loop carries out. The details, including
`breakOnEntry()`, `setLinkTimeout()` and `poll()` for natives that block,
are in [Adding the debugger to your firmware](../debugging/firmware.md).

## The watchdog

Nothing in the VM yields to other tasks while a script runs. The loop above
blocks in `xQueueReceive()` on every pass and the scripts return quickly,
so the idle task runs and the task watchdog stays quiet. A script that runs
longer than the watchdog period inside one call (5 s by default) starves
the idle task of its core and the watchdog reports it. Choose one:

* keep script calls short, as here;
* define `WRENCH_TIME_SLICES` in `wrench_config.h` and set
  `wr_setInstructionsPerSlice( w, ... )`: a long call then returns
  yielded, and the loop resumes it with `wr_continue()` on a later pass
  (keep `ready` false while it is yielded, see
  [Time slices](scheduling.md#time-slices));
* give scripts a native that calls `vTaskDelay()`;
* change the task watchdog settings in menuconfig.

While the debugger holds a script halted, the transport's `idle()` sleeps a
tick on every pass, which keeps the watchdog quiet during a debug session.
More on tasks, cores and memory in
[Several states, threads and RTOS tasks](threads.md).
