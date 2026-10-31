/*
	wrench debug target over the USB / UART serial port, for any Arduino core
	(ESP32, ESP8266, RP2040, STM32duino, SAMD, Mega...).

	Setup:
		1. copy wrench.cpp and wrench.h, the single file build, into this
			folder (wrench_config.h here switches the debugger on): download
			them from a release or generate them with "make dist",
			see ../README.md
		2. generate the embedded program, from this folder:
				wrench ch -debugsource ../script/blink.w blink.h blink
		3. build and flash, close the serial monitor
		4. in VS Code use a launch configuration like
				{ "type": "wrench", "request": "attach", "program": "${workspaceFolder}/script/blink.w",
					"transport": "serial", "serialPort": "COM5", "baudRate": 115200 }
			("attach" debugs the flashed image, "launch" uploads a fresh build)

	The serial port carries the debug protocol, so do not Serial.print()
	anything else: script output goes to the debug console through print().
*/

#include "wrench.h"
#include "blink.h"

#ifndef LED_BUILTIN
#define LED_BUILTIN 2
#endif

static WrenchDebugStreamTransport debugLink( Serial );
static WRState* w = 0;
static WrenchDebugServer* debugger = 0;
static WRContext* script = 0;
static bool scriptReady = false; // its global code ran to the end, loop() may be called
static uint32_t nextCall = 0;

// script output: to the debugger console when one is attached
static void print( WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr )
{
	char buf[64];
	for ( int i = 0; i < argn; ++i )
	{
		debugger->print( argv[i].asString(buf, sizeof(buf) - 1) );
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
		digitalWrite( LED_BUILTIN, argv[0].asInt() ? HIGH : LOW );
	}
}

// the script's global code; it stops on its first line when the debugger
// launched it that way (or breakOnEntry() is armed)
static void runScript()
{
	scriptReady = script && wr_executeContext( script );
	nextCall = millis();
}

void setup()
{
#if defined(ARDUINO_ARCH_ESP32) || defined(ARDUINO_ARCH_ESP8266)
	// the default receive buffer (256 bytes) is smaller than one frame
	// (WRENCH_DEBUG_MAX_PAYLOAD plus 8 bytes framing), so an upload would
	// overflow it while the sketch is busy in the script. Must come before
	// begin(). Other cores have no such call. Their USB CDC ports (SAMD,
	// RP2040...) are usually flow controlled; on a hardware UART with a small
	// buffer lower WRENCH_DEBUG_MAX_PAYLOAD in wrench_config.h or enlarge the
	// core's buffer (SERIAL_RX_BUFFER_SIZE on AVR and STM32duino)
	Serial.setRxBufferSize( 2048 );
#endif
	Serial.begin( 115200 );
	pinMode( LED_BUILTIN, OUTPUT );

	w = wr_newState();
	wr_registerFunction( w, "print", print );
	wr_registerFunction( w, "println", println );
	wr_registerFunction( w, "led", led );

	debugger = new WrenchDebugServer( w, &debugLink );

	// the firmware calls loop() itself, keep the debug session alive after
	// the global code has finished
	debugger->setHostDriven( true );

	// a debugger may restart the script and upload new ones, loop() below
	// carries that out
	debugger->setLaunchable( true );

	// run the flashed image; a debugger may replace it later
	script = wr_newContext( w, blink_bytecode, blink_bytecodeSize );
	debugger->attach( script, "blink.w" );

	// uncomment to halt the script on its first line until a debugger
	// attaches (a timeout in ms lets it run on without one)
	// debugger->breakOnEntry();

	runScript();
}

void loop()
{
	// service the debugger, then do what it asked for: a new program in
	// place of the old one, a run from the start
	int events = debugger->tick();
	if ( events & WRD_TickProgramUploaded )
	{
		uint8_t* bytes;
		int size;
		if ( debugger->takeUpload(bytes, size) )
		{
			wr_destroyContext( script );
			script = wr_newContext( w, bytes, size, true );
			debugger->attach( script, "blink.w" );
			scriptReady = false;
		}
	}
	if ( events & WRD_TickRunRequested )
	{
		wr_resetContext( script );
		runScript();
	}

	// call into the script whenever its global code has run
	if ( scriptReady && (int32_t)(millis() - nextCall) >= 0 )
	{
		WRValue* ret = wr_callFunction( script, "loop" );
		int delayMs = ret ? ret->asInt() : 1000;
		nextCall = millis() + (delayMs > 0 ? delayMs : 1);
	}
}
