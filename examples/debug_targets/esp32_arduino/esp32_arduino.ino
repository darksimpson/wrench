/*
	wrench debug target on an ESP32 (Arduino core).

	Two transports, chosen with DEBUG_OVER_WIFI below:
		0 (default): Serial (the USB port / UART0), the debugger connects
			with "transport": "serial"
		1: joins a WiFi network and listens on TCP port 5050, the debugger
			connects with "transport": "tcp" to the address the sketch prints
			on Serial at startup; Serial stays free for your own logging

	Setup:
		1. copy src/wrench.cpp and src/wrench.h from the wrench repository into
			this folder (wrench_config.h here switches the debugger on). They
			must be a current build that contains the debugger: regenerate src/
			first with "make release" (Linux) or "wrench release discrete_src src",
			see ../README.md
		2. generate the embedded program, from this folder:
				wrench ch -debugsource ../script/blink.w blink.h blink
		3. choose DEBUG_OVER_WIFI, for WiFi set WIFI_SSID / WIFI_PASSWORD
		4. build and flash; over Serial close the serial monitor afterwards
		5. in VS Code:
				{ "type": "wrench", "request": "launch", "program": "${workspaceFolder}/script/blink.w",
					"transport": "serial", "serialPort": "COM5", "baudRate": 115200 }
			or
				{ "type": "wrench", "request": "launch", "program": "${workspaceFolder}/script/blink.w",
					"transport": "tcp", "host": "<board ip>", "port": 5050 }
			"launch" uploads and runs a fresh build; "attach" debugs what runs.

	Over Serial the port carries the debug protocol: script output goes to
	the debug console only and the sketch prints nothing else. The protocol
	skips stray bytes (boot messages), but keep Tools, Core Debug Level at
	None so the core does not log there.
*/

#include "wrench.h"
#include "blink.h"

#define DEBUG_OVER_WIFI 0

#define DEBUG_BAUD 115200

#define WIFI_SSID "your-network"
#define WIFI_PASSWORD "your-password"
#define DEBUG_PORT 5050

#ifndef LED_BUILTIN
#define LED_BUILTIN 2
#endif

#if DEBUG_OVER_WIFI

#include <WiFi.h>

// one debugger at a time; a new connection is accepted once the previous
// one is gone
class WiFiDebugTransport : public WrenchDebugTransport
{
public:
	explicit WiFiDebugTransport( uint16_t port ) : m_server( port ) {}

	void begin()
	{
		m_server.begin();
		m_server.setNoDelay( true );
	}

	bool write( const uint8_t* data, uint32_t len )
	{
		while ( len && m_client.connected() )
		{
			size_t sent = m_client.write( data, len );
			if ( sent == 0 )
			{
				return false;
			}
			data += sent;
			len -= (uint32_t)sent;
		}
		return len == 0;
	}

	int read( uint8_t* data, uint32_t maxLen )
	{
		if ( !m_client.connected() )
		{
			if ( m_hadClient )
			{
				// report the loss once so the server releases a halted script
				m_hadClient = false;
				m_client.stop();
				return -1;
			}
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
			// core 3.x deprecates available() for accepting connections
			WiFiClient next = m_server.accept();
#else
			WiFiClient next = m_server.available();
#endif
			if ( next )
			{
				m_client = next;
				m_client.setNoDelay( true );
				m_hadClient = true;
			}
			return 0;
		}

		int available = m_client.available();
		if ( available <= 0 )
		{
			return 0;
		}
		if ( (uint32_t)available > maxLen )
		{
			available = (int)maxLen;
		}
		return m_client.read( data, available );
	}

	uint32_t millis() { return (uint32_t)::millis(); }

	// let the WiFi stack run and keep the task watchdog fed while halted
	void idle() { delay( 1 ); }

	bool connected() { return m_client.connected(); }

private:
	WiFiServer m_server;
	WiFiClient m_client;
	bool m_hadClient = false;
};

static WiFiDebugTransport debugLink( DEBUG_PORT );

#else

// the stream transport from wrench.h on Serial; while a script is halted
// delay() instead of yield(), so lower priority tasks and the idle task
// watchdog keep running
class SerialDebugTransport : public WrenchDebugStreamTransport
{
public:
	SerialDebugTransport() : WrenchDebugStreamTransport( Serial ) {}

	void idle() { delay( 1 ); }
};

static SerialDebugTransport debugLink;

#endif

static WRState* w = 0;
static WrenchDebugServer* debugger = 0;
static uint32_t nextCall = 0;

// own messages go to Serial only when it is not the debug link
static void logLine( const char* text )
{
#if DEBUG_OVER_WIFI
	Serial.println( text );
#endif
}

static void print( WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr )
{
	for ( int i = 0; i < argn; ++i )
	{
		WRValue::MallocStrScoped text( argv[i] );
		if ( text )
		{
#if DEBUG_OVER_WIFI
			Serial.print( (const char*)text );
#endif
			debugger->print( text, (int)text.size() );
		}
	}
}

static void println( WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr )
{
	print( c, argv, argn, retVal, usr );
#if DEBUG_OVER_WIFI
	Serial.println();
#endif
	debugger->print( "\n" );
}

static void led( WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr )
{
	if ( argn > 0 )
	{
		digitalWrite( LED_BUILTIN, argv[0].asInt() ? HIGH : LOW );
	}
}

void setup()
{
	pinMode( LED_BUILTIN, OUTPUT );

#if DEBUG_OVER_WIFI
	Serial.begin( 115200 );
	WiFi.mode( WIFI_STA );
	WiFi.begin( WIFI_SSID, WIFI_PASSWORD );
	while ( WiFi.status() != WL_CONNECTED )
	{
		delay( 250 );
	}
	Serial.print( "wrench debugger on " );
	Serial.print( WiFi.localIP() );
	Serial.print( ":" );
	Serial.println( DEBUG_PORT );

	debugLink.begin();
#else
	// room for a whole upload packet (WRENCH_DEBUG_MAX_PAYLOAD) while the
	// sketch is busy, must be set before begin()
	Serial.setRxBufferSize( 2048 );
	Serial.begin( DEBUG_BAUD );
#endif

	w = wr_newState();
	wr_loadAllLibs( w );
	wr_registerFunction( w, "print", print );
	wr_registerFunction( w, "println", println );
	wr_registerFunction( w, "led", led );

	debugger = new WrenchDebugServer( w, &debugLink );
	debugger->setHostDriven( true );

	WRContext* context = debugger->loadBytes( blink_bytecode, blink_bytecodeSize );

	// uncomment to halt the script on its first line until a debugger
	// attaches (a timeout in ms lets it run on without one)
	// debugger->breakOnEntry();

	if ( context )
	{
		wr_executeContext( context );
	}
}

void loop()
{
	int events = debugger->tick();
	if ( events & WRD_TickProgramLoaded )
	{
		logLine( "new program uploaded by the debugger" );
	}

	if ( debugger->ready() && (int32_t)(millis() - nextCall) >= 0 )
	{
		WRValue* ret = wr_callFunction( debugger->context(), "loop" );
		int delayMs = ret ? ret->asInt() : 1000;
		nextCall = millis() + (delayMs > 0 ? delayMs : 1);
	}

	delay( 1 );
}
