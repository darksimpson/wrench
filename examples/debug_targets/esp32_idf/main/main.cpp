/*
	wrench debug target for ESP-IDF (no Arduino core).

	Two transports are shown, chosen in idf.py menuconfig under
	"Wrench debug target":
		UartDebugTransport (default): UART0, the debugger connects with
			"transport": "serial"
		TcpDebugTransport: joins a WiFi network as a station and listens with
			lwIP sockets, the debugger connects with "transport": "tcp" to the
			address the board logs at startup (port 5050 by default)

	Needs ESP-IDF v5.0 or later (UART_SCLK_DEFAULT, esp_netif). On IDF 5.3
	and later the "driver" component in main/CMakeLists.txt is a deprecated
	umbrella for esp_driver_uart and esp_driver_gpio; it is kept so the
	example still builds on 5.0 to 5.2.

	Setup, all commands run in this example's folder (esp32_idf/):
		1. copy wrench.cpp and wrench.h, the single file build, into main/
			(main/wrench_config.h switches the debugger on): download them from
			a release or generate them with "make dist", see ../README.md
		2. wrench ch -debugsource ../script/blink.w main/blink.h blink
		3. idf.py menuconfig: "Wrench debug target", for WiFi enable TCP and
			enter the network name and password
		4. idf.py build flash monitor

	UART0 is also the IDF console. Over WiFi that is where the board reports
	its address. With the UART transport the protocol skips log lines, but it
	is cleaner to move the console away (Component config, ESP System
	Settings, Channel for console output: None or another UART) and to close
	the monitor before debugging.

	Watchdog: the wrench task runs at priority 5 and nothing in the VM
	yields while script code executes. Between calls the task sleeps
	(vTaskDelay below) and while the debugger holds the script halted the
	transports' idle() sleeps, so the idle tasks run and the task watchdog
	stays quiet. A script that loops endlessly inside one call (or simply
	runs for longer than CONFIG_ESP_TASK_WDT_TIMEOUT_S, 5 s by default)
	starves the idle task of its core and the task watchdog reports it (it
	only logs unless "Invoke panic handler on Task Watchdog timeout" is set).
	The debugger can still pause such a loop, it polls the link from inside
	the VM. To keep a long running call from tripping the watchdog either
	define WRENCH_TIME_SLICES in main/wrench_config.h and call
	wr_setInstructionsPerSlice(): a call that runs too long then returns
	early with the context yielded, and the firmware resumes it with
	wr_continue() after a vTaskDelay() (ready stays false while the context
	is yielded, so the loop in wrenchTask needs that extra branch; see
	wr_getYieldInfo() in wrench.h). Or give the script a native that calls
	vTaskDelay(), or adjust the task watchdog in menuconfig (Component
	config, ESP System Settings).
*/

#include "wrench.h"
#include "blink.h"

#include "sdkconfig.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

#include <fcntl.h>
#include <errno.h>
#include <string.h>

#ifdef CONFIG_WRENCH_DEBUG_TCP
#define USE_TCP 1
#else
#define USE_TCP 0
#endif

#if USE_TCP
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"

#define DEBUG_PORT CONFIG_WRENCH_DEBUG_PORT
#define WIFI_SSID CONFIG_WRENCH_WIFI_SSID
#define WIFI_PASSWORD CONFIG_WRENCH_WIFI_PASSWORD
#define WIFI_HOSTNAME CONFIG_WRENCH_WIFI_HOSTNAME
#endif

#define DEBUG_UART UART_NUM_0
#define DEBUG_BAUD 115200
#define LED_GPIO GPIO_NUM_2

static uint32_t nowMs()
{
	return (uint32_t)(esp_timer_get_time() / 1000);
}

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

	// called while the script is halted (breakpoint, step, waiting for a
	// debugger): sleep a tick so lower priority tasks and the idle task run
	// and the task watchdog stays quiet. Not called while script code runs
	void idle() { vTaskDelay( 1 ); }

private:
	uart_port_t m_port;
};

class TcpDebugTransport : public WrenchDebugTransport
{
public:
	bool begin( int port )
	{
		m_listen = socket( AF_INET, SOCK_STREAM, IPPROTO_IP );
		if ( m_listen < 0 )
		{
			return false;
		}
		int on = 1;
		setsockopt( m_listen, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on) );

		struct sockaddr_in addr = {};
		addr.sin_family = AF_INET;
		addr.sin_port = htons( port );
		addr.sin_addr.s_addr = htonl( INADDR_ANY );
		if ( bind(m_listen, (struct sockaddr*)&addr, sizeof(addr)) != 0 || listen(m_listen, 1) != 0 )
		{
			close( m_listen );
			m_listen = -1;
			return false;
		}
		fcntl( m_listen, F_SETFL, fcntl(m_listen, F_GETFL, 0) | O_NONBLOCK );
		return true;
	}

	bool write( const uint8_t* data, uint32_t len )
	{
		while ( len && m_client >= 0 )
		{
			int sent = send( m_client, data, len, 0 );
			if ( sent > 0 )
			{
				data += sent;
				len -= (uint32_t)sent;
			}
			else if ( sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) )
			{
				vTaskDelay( 1 );
			}
			else
			{
				dropClient();
			}
		}
		return len == 0;
	}

	int read( uint8_t* data, uint32_t maxLen )
	{
		if ( m_client < 0 )
		{
			int c = accept( m_listen, NULL, NULL );
			if ( c >= 0 )
			{
				int on = 1;
				setsockopt( c, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on) );
				fcntl( c, F_SETFL, fcntl(c, F_GETFL, 0) | O_NONBLOCK );
				m_client = c;
			}
			return 0;
		}

		int got = recv( m_client, data, maxLen, 0 );
		if ( got > 0 )
		{
			return got;
		}
		if ( got < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) )
		{
			return 0;
		}
		dropClient();
		return -1;
	}

	uint32_t millis() { return nowMs(); }

	// while halted: sleep a tick, as in UartDebugTransport
	void idle() { vTaskDelay( 1 ); }

	bool connected() { return m_client >= 0; }

private:
	void dropClient()
	{
		if ( m_client >= 0 )
		{
			close( m_client );
			m_client = -1;
		}
	}

	int m_listen = -1;
	int m_client = -1;
};

#if USE_TCP

static const char* TAG = "wrench";
static EventGroupHandle_t wifiEvents;
static const EventBits_t WIFI_CONNECTED = 1 << 0;

static void onNetworkEvent( void* arg, esp_event_base_t base, int32_t id, void* data )
{
	if ( base == WIFI_EVENT && id == WIFI_EVENT_STA_START )
	{
		esp_wifi_connect();
	}
	else if ( base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED )
	{
		// keep trying forever, a debugger simply reconnects once the
		// network is back; the listening socket survives the outage
		xEventGroupClearBits( wifiEvents, WIFI_CONNECTED );
		wifi_event_sta_disconnected_t* event = (wifi_event_sta_disconnected_t*)data;
		ESP_LOGW( TAG, "WiFi disconnected (reason %d), reconnecting", (int)event->reason );
		esp_wifi_connect();
	}
	else if ( base == IP_EVENT && id == IP_EVENT_STA_GOT_IP )
	{
		ip_event_got_ip_t* event = (ip_event_got_ip_t*)data;
		ESP_LOGI( TAG, "debug server at " IPSTR ":%d (%s)", IP2STR(&event->ip_info.ip), DEBUG_PORT, WIFI_HOSTNAME );
		xEventGroupSetBits( wifiEvents, WIFI_CONNECTED );
	}
}

// join the network from menuconfig as a station. Waits a while for an
// address but starts the script without one: the debug server listens on
// every interface, so it is reachable as soon as the link comes up
static void wifiStart()
{
	// the WiFi driver keeps its calibration data in NVS
	esp_err_t err = nvs_flash_init();
	if ( err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND )
	{
		ESP_ERROR_CHECK( nvs_flash_erase() );
		err = nvs_flash_init();
	}
	ESP_ERROR_CHECK( err );

	ESP_ERROR_CHECK( esp_netif_init() );
	ESP_ERROR_CHECK( esp_event_loop_create_default() );
	esp_netif_t* netif = esp_netif_create_default_wifi_sta();
	esp_netif_set_hostname( netif, WIFI_HOSTNAME );

	wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK( esp_wifi_init(&init) );

	wifiEvents = xEventGroupCreate();
	ESP_ERROR_CHECK( esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, onNetworkEvent, NULL, NULL) );
	ESP_ERROR_CHECK( esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, onNetworkEvent, NULL, NULL) );

	// the fields are fixed size and need no terminator when full
	wifi_config_t config = {};
	memcpy( config.sta.ssid, WIFI_SSID, strnlen(WIFI_SSID, sizeof(config.sta.ssid)) );
	memcpy( config.sta.password, WIFI_PASSWORD, strnlen(WIFI_PASSWORD, sizeof(config.sta.password)) );
	config.sta.threshold.authmode = WIFI_PASSWORD[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

	ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
	ESP_ERROR_CHECK( esp_wifi_set_config(WIFI_IF_STA, &config) );
	ESP_ERROR_CHECK( esp_wifi_start() );

	// modem sleep holds incoming packets for up to a beacon interval, which
	// makes every step in the debugger feel sluggish
	esp_wifi_set_ps( WIFI_PS_NONE );

	ESP_LOGI( TAG, "connecting to WiFi network '%s'", WIFI_SSID );
	EventBits_t bits = xEventGroupWaitBits( wifiEvents, WIFI_CONNECTED, pdFALSE, pdTRUE, pdMS_TO_TICKS(15000) );
	if ( !(bits & WIFI_CONNECTED) )
	{
		ESP_LOGW( TAG, "no WiFi connection yet, starting anyway" );
	}
}

static TcpDebugTransport debugLink;

#else

static UartDebugTransport debugLink( DEBUG_UART );

#endif

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

static void wrenchTask( void* )
{
#if USE_TCP
	if ( !debugLink.begin(DEBUG_PORT) )
	{
		ESP_LOGE( TAG, "cannot listen on port %d", DEBUG_PORT );
	}
#else
	debugLink.begin( DEBUG_BAUD );
#endif

	gpio_reset_pin( LED_GPIO );
	gpio_set_direction( LED_GPIO, GPIO_MODE_OUTPUT );

	WRState* w = wr_newState();
	wr_registerFunction( w, "print", print );
	wr_registerFunction( w, "println", println );
	wr_registerFunction( w, "led", led );

	debugger = new WrenchDebugServer( w, &debugLink );
	debugger->setHostDriven( true );
	// a debugger may restart the script and upload new ones, the loop below
	// carries that out
	debugger->setLaunchable( true );

	WRContext* script = wr_newContext( w, blink_bytecode, blink_bytecodeSize );
	debugger->attach( script, "blink.w" );

	// uncomment to halt the script on its first line until a debugger
	// attaches (a timeout in ms lets it run on without one)
	// debugger->breakOnEntry();

	// ready: the global code ran to the end, loop() may be called
	bool ready = script && wr_executeContext( script );

	uint32_t nextCall = 0;
	for ( ;; )
	{
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
				ready = false;
			}
		}
		if ( events & WRD_TickRunRequested )
		{
			// from the start, with fresh globals; it stops on its first line
			// when the debugger launched it that way
			wr_resetContext( script );
			ready = script && wr_executeContext( script );
			nextCall = nowMs();
		}

		if ( ready && (int32_t)(nowMs() - nextCall) >= 0 )
		{
			WRValue* ret = wr_callFunction( script, "loop" );
			int delayMs = ret ? ret->asInt() : 1000;
			nextCall = nowMs() + (delayMs > 0 ? delayMs : 1);
		}
		vTaskDelay( 1 );
	}
}

extern "C" void app_main( void )
{
#if USE_TCP
	wifiStart();
#endif

	// the debug server keeps its frame buffers in its heap allocated state,
	// not on this stack; 8 KB covers the VM, the natives, the UART / lwIP
	// calls and the server's request handling (a few hundred bytes of
	// locals). Not pinned to a core, see the watchdog note above
	xTaskCreate( wrenchTask, "wrench", 8192, NULL, 5, NULL );
}
