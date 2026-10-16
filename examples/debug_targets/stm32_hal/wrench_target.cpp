/*
	wrench debug target for STM32 with the HAL drivers.

	Setup:
		1. add src/wrench.cpp, src/wrench.h, wrench_config.h, wrench_target.h and
			this file to the project (C++ enabled, -fno-exceptions -fno-rtti fine).
			wrench.cpp and wrench.h must be a current build that contains the
			debugger: regenerate src/ first with "make release" (Linux) or
			"wrench release discrete_src src", see ../README.md. wrench.h finds
			wrench_config.h through __has_include; with a compiler that lacks it
			pass its defines as compiler flags instead
		2. from this folder: wrench ch -debugsource ../script/blink.w blink.h blink
			(keep blink.h next to this file or on the include path)
		3. configure a UART at 115200 8N1 with its global interrupt enabled
		4. hook it up as shown in wrench_target.h
		5. in VS Code:
				{ "type": "wrench", "request": "attach", "program": "${workspaceFolder}/script/blink.w",
					"transport": "serial", "serialPort": "COM7", "baudRate": 115200 }

	Reception runs one byte per interrupt into a ring buffer, so nothing is
	lost while the VM executes or the debugger is halted (see arm() for the
	one case where the HAL refuses to re-arm).

	The debug server allocates its state (frame buffers and tables, about
	2 x WRENCH_DEBUG_MAX_PAYLOAD plus a few hundred bytes with the sizes in
	wrench_config.h) with malloc, or whatever wr_setGlobalAllocator() set,
	so the heap in the linker script must have room for it besides the VM.
*/

#include "wrench_target.h"
#include "wrench.h"
#include "blink.h"

#include <new>

#ifndef WRENCH_LED_PORT
#define WRENCH_LED_PORT GPIOA
#define WRENCH_LED_PIN GPIO_PIN_5
#endif

// orders the ring buffer accesses against the index updates. The receive
// interrupt runs on the same core as the reader, which sees its own
// accesses in program order, so keeping the compiler from reordering them
// is enough; CMSIS provides __COMPILER_BARRIER() from CMSIS 5 on, older
// versions get a GCC barrier or a full __DMB()
#if defined(__COMPILER_BARRIER)
#define WRENCH_RING_BARRIER() __COMPILER_BARRIER()
#elif defined(__GNUC__)
#define WRENCH_RING_BARRIER() __asm__ volatile ( "" ::: "memory" )
#else
#define WRENCH_RING_BARRIER() __DMB()
#endif

class StmUartDebugTransport : public WrenchDebugTransport
{
public:
	void begin( UART_HandleTypeDef* huart )
	{
		m_uart = huart;
		m_head = 0;
		m_tail = 0;
		arm();
	}

	// interrupt context
	void received()
	{
		uint32_t head = m_head;
		uint32_t next = (head + 1) % RING_SIZE;
		if ( next != m_tail )
		{
			m_ring[head] = m_byte;
			// the byte must be in the ring before the reader sees the new head
			WRENCH_RING_BARRIER();
			m_head = next;
		}
		// a full ring drops the byte, the protocol CRC catches it
		arm();
	}

	// interrupt context: overrun / framing errors stop reception until re-armed
	void error()
	{
		arm();
	}

	bool write( const uint8_t* data, uint32_t len )
	{
		bool ok = true;
		while ( len )
		{
			uint16_t chunk = (uint16_t)(len > 0xFFFF ? 0xFFFF : len);
			if ( HAL_UART_Transmit(m_uart, (uint8_t*)data, chunk, 100 + chunk) != HAL_OK )
			{
				ok = false;
				break;
			}
			data += chunk;
			len -= chunk;
		}
		// the transmit may have kept the interrupt from re-arming reception
		rearmIfNeeded();
		return ok;
	}

	int read( uint8_t* data, uint32_t maxLen )
	{
		rearmIfNeeded();

		uint32_t n = 0;
		uint32_t tail = m_tail;
		while ( n < maxLen && tail != m_head )
		{
			// read the head before the byte it covers
			WRENCH_RING_BARRIER();
			data[n++] = m_ring[tail];
			tail = (tail + 1) % RING_SIZE;
		}
		// the bytes must be copied out before the interrupt may reuse them
		WRENCH_RING_BARRIER();
		m_tail = tail;
		return (int)n;
	}

	uint32_t millis() { return HAL_GetTick(); }

	bool owns( UART_HandleTypeDef* huart ) const { return huart == m_uart; }

private:
	enum { RING_SIZE = 512 };

	// HAL_UART_Receive_IT() fails with HAL_BUSY while the handle is locked,
	// which older HAL versions do for the whole of a blocking
	// HAL_UART_Transmit(). Called from the receive interrupt during write()
	// that leaves reception stopped, so remember it and re-arm from the main
	// context (read() or the end of write()). Bytes that arrive meanwhile
	// are lost; the client resends the request
	void arm()
	{
		m_armed = HAL_UART_Receive_IT( m_uart, (uint8_t*)&m_byte, 1 ) == HAL_OK;
	}

	// main context only. Not armed means no reception is in progress, so
	// the receive interrupt cannot run concurrently. While a reception is
	// still running (an error the HAL does not abort reception for) the call
	// returns HAL_BUSY without side effects and is retried
	void rearmIfNeeded()
	{
		if ( !m_armed )
		{
			arm();
		}
	}

	UART_HandleTypeDef* m_uart = 0;
	volatile uint8_t m_byte = 0;
	volatile bool m_armed = false;
	volatile uint8_t m_ring[RING_SIZE];
	volatile uint32_t m_head = 0;
	volatile uint32_t m_tail = 0;
};

static StmUartDebugTransport debugLink;
static WRState* w = 0;
static WrenchDebugServer* debugger = 0;
static uint32_t nextCall = 0;

static void print( WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr )
{
	char buf[48];
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
		HAL_GPIO_WritePin( WRENCH_LED_PORT, WRENCH_LED_PIN, argv[0].asInt() ? GPIO_PIN_SET : GPIO_PIN_RESET );
	}
}

extern "C" void wrench_target_init( UART_HandleTypeDef* huart )
{
	debugLink.begin( huart );

	w = wr_newState();
	wr_registerFunction( w, "print", print );
	wr_registerFunction( w, "println", println );
	wr_registerFunction( w, "led", led );

	// placement new into static storage avoids operator new (and with it the
	// C++ runtime's allocation and exception support). It does not make the
	// debugger heap free: the constructor still allocates the server's state
	// with g_malloc, see the note at the top. The union aligns the storage
	// for any member the object may have (a plain byte array could start on
	// an odd address and fault on Cortex-M0)
	static union
	{
		uint8_t bytes[sizeof(WrenchDebugServer)];
		void* alignPointer;
		double alignDouble;
		long alignLong;
	} serverStorage;
	debugger = new ( serverStorage.bytes ) WrenchDebugServer( w, &debugLink );
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

extern "C" void wrench_target_poll( void )
{
	debugger->tick();
	if ( debugger->ready() && (int32_t)(HAL_GetTick() - nextCall) >= 0 )
	{
		WRValue* ret = wr_callFunction( debugger->context(), "loop" );
		int delayMs = ret ? ret->asInt() : 1000;
		nextCall = HAL_GetTick() + (delayMs > 0 ? delayMs : 1);
	}
}

extern "C" void wrench_target_uart_rx( UART_HandleTypeDef* huart )
{
	if ( debugLink.owns(huart) )
	{
		debugLink.received();
	}
}

extern "C" void wrench_target_uart_error( UART_HandleTypeDef* huart )
{
	if ( debugLink.owns(huart) )
	{
		debugLink.error();
	}
}
