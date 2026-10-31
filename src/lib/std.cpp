/*******************************************************************************
Copyright (c) 2026 Curt Hartung -- curt.hartung@gmail.com

MIT Licence

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*******************************************************************************/

#include "wrench.h"

// standard functions that sort of come up a lot

int32_t wr_Seed = 0xA5EED;

//------------------------------------------------------------------------------
uint32_t wr_hash_read8( const void *dat, const int len )
{
	// fnv-1
	uint32_t hash = 0x811C9DC5;
	const unsigned char* data = (const unsigned char *)dat;

	for( int i=0; i<len; ++i )
	{
		hash ^= (uint32_t)READ_8_FROM_PC(data++);
		hash *= 0x1000193;
	}

	return hash;
}

//------------------------------------------------------------------------------
uint32_t wr_hash( const void *dat, const int len, uint32_t serial )
{
	// fnv-1
	uint32_t hash = serial ? serial : 0x811C9DC5;
	const unsigned char* data = (const unsigned char *)dat;

	for( int i=0; i<len; ++i )
	{
		hash ^= (uint32_t)data[i];
		hash *= 0x1000193;
	}

	return hash;
}

//------------------------------------------------------------------------------
uint32_t wr_hashStr_read8( const char* dat )
{
	uint32_t hash = 0x811C9DC5;
	const char* data = dat;
	while ( *data )
	{
		hash ^= (uint32_t)READ_8_FROM_PC(data++);
		hash *= 0x1000193;
	}

	return hash;
}

//------------------------------------------------------------------------------
uint32_t wr_hashStr( const char* dat, uint32_t serial )
{
	uint32_t hash = serial ? serial : 0x811C9DC5;
	const char* data = dat;
	while ( *data )
	{
		hash ^= (uint32_t)(*data++);
		hash *= 0x1000193;
	}

	return hash;
}

//------------------------------------------------------------------------------
// writes at most 'len' characters plus a terminating null, so 'string'
// must hold len + 1 bytes
int wr_itoa( int i, char* string, size_t len )
{
	char buf[12];
	size_t pos = 0;
	unsigned int val;
	if ( i < 0 )
	{
		if ( pos < len )
		{
			string[pos++] = '-';
		}
		val = 0u - (unsigned int)i; // well defined for INT_MIN too
	}
	else
	{
		val = (unsigned int)i;
	}

	int digit = 0;
	do
	{
		buf[digit++] = (char)((val % 10) + '0');
	} while( val /= 10 );

	while( digit > 0 && pos < len )
	{
		string[pos++] = buf[--digit];
	}
	string[pos] = 0;
	return (int)pos;
}

// number of decimals printed for non-negative 'd': six significant
// digits (like %g, a float only carries about seven, so more would print
// representation noise such as 90.10001 for 90.1) and never more than five
static int wr_ftoaDecimals( double d )
{
	int decimals = 5;
	double limit = 10.0;
	while( decimals > 0 && d >= limit )
	{
		--decimals;
		limit *= 10.0;
	}
	return decimals;
}

// half a unit of the last printed decimal, added to round instead of truncate
static double wr_ftoaHalf( int decimals )
{
	double half = 0.5;
	for( ; decimals > 0; --decimals )
	{
		half /= 10.0;
	}
	return half;
}

// 'd' is non-negative, already rounded and below 2^31: append the integer
// part, a decimal point and 'decimals' digits, then trim trailing zeros
static int wr_ftoaFixed( double d, const int decimals, char* buf )
{
	int pos = 0;
	int i = (int)d;
	d -= i;
	pos += wr_itoa( i, buf, 11 );

	buf[pos++] = '.';

	for( int p=0; p<decimals; ++p )
	{
		d *= 10.0;
		int c = (int)d;
		if ( c > 9 )
		{
			c = 9;
		}
		buf[pos++] = (char)('0' + c);
		d -= c;
	}

	// knock off trailing zeros
	for( --pos; pos > 0 && buf[pos] == '0'; --pos )
	{
	}

	if ( buf[pos] != '.' )
	{
		++pos; // and the decimal point if nothing is left after it
	}

	return pos;
}

//------------------------------------------------------------------------------
// writes at most 'len' characters plus a terminating null, so 'string'
// must hold len + 1 bytes
int wr_ftoa( float f, char* string, size_t len )
{
	char buf[32];
	int pos = 0;
	double d = f;

	if ( d != d )
	{
		buf[pos++] = 'n';
		buf[pos++] = 'a';
		buf[pos++] = 'n';
	}
	else
	{
		if ( d < 0 )
		{
			d = -d;
			buf[pos++] = '-';
		}

		if ( d - d != 0 ) // only infinity fails this
		{
			buf[pos++] = 'i';
			buf[pos++] = 'n';
			buf[pos++] = 'f';
		}
		else if ( d < 2147483647.5 )
		{
			const int decimals = wr_ftoaDecimals( d );
			pos += wr_ftoaFixed( d + wr_ftoaHalf(decimals), decimals, buf + pos );
		}
		else
		{
			// too large for the integer part, use d.ddddde+XX
			int exp10 = 0;
			while( d >= 10.0 )
			{
				d /= 10.0;
				++exp10;
			}

			d += 0.000005;
			if ( d >= 10.0 )
			{
				d /= 10.0;
				++exp10;
			}

			pos += wr_ftoaFixed( d, 5, buf + pos );
			buf[pos++] = 'e';
			buf[pos++] = '+';
			pos += wr_itoa( exp10, buf + pos, 3 );
		}
	}

	size_t n = ((size_t)pos < len) ? (size_t)pos : len;
	memcpy( string, buf, n );
	string[n] = 0;
	return (int)n;
}

//------------------------------------------------------------------------------
// one step of the generator on 'seed', then a number from 'from' to 'to'
static int32_t wr_randNext( int32_t& seed, const int32_t from, const int32_t to )
{
	if ( seed == 0 )
	{
		seed = 0xA5EED; // zero is a fixed point of the generator
	}

	const int32_t k = seed / 127773;
	seed = 16807 * (seed - k * 127773) - 2836 * k;

	if ( from >= to )
	{
		return from;
	}

	// unsigned math: the span of a full 32-bit range does not fit an int
	// and wraps to 0, which must not be used as a divisor
	const uint32_t range = (uint32_t)to - (uint32_t)from + 1u;
	uint32_t r = (uint32_t)seed;
	if ( range == 0 )
	{
		// the generator only yields 31 bits, mix in a second draw
		const int32_t k2 = seed / 127773;
		seed = 16807 * (seed - k2 * 127773) - 2836 * k2;
		r ^= ((uint32_t)seed) << 16;
		return (int32_t)(r + (uint32_t)from);
	}

	return (int32_t)((r % range) + (uint32_t)from);
}

//------------------------------------------------------------------------------
int32_t wr_randEx( const int32_t from, const int32_t to )
{
	return wr_randNext( wr_Seed, from, to );
}

//------------------------------------------------------------------------------
int32_t wr_randEx( WRState* w, const int32_t from, const int32_t to )
{
	return wr_randNext( w->randSeed, from, to );
}

//------------------------------------------------------------------------------
void wr_std_rand( WRValue* stackTop, const int argn, WRContext* c )
{
	if ( argn > 0 )
	{
		WRValue* args = stackTop - argn;

		stackTop->i = argn > 1 ?
					  wr_randEx(c->w, args[0].asInt(), args[1].asInt())
							   : wr_randEx( c->w, 0, args[0].asInt() );
	}
}

//------------------------------------------------------------------------------
void wr_std_srand( WRValue* stackTop, const int argn, WRContext* c )
{
	if ( argn == 1 )
	{
		c->w->randSeed = (stackTop - 1)->asInt();
	}
}

//------------------------------------------------------------------------------
// std::time: seconds. Where the C library has a clock (desktops, ARM
// boards, ESP32 under ESP-IDF or Arduino) it is time(), the Unix time once
// the clock was set (on a board by SNTP or an RTC), before that the seconds
// since the start. Other Arduino boards (AVR) have no clock in their C
// library: there it is the seconds since the board started, from millis()
#if defined(__arm__) || defined(_WIN32) || defined(__linux__) || defined(__MINGW32__) || defined(__APPLE__) || defined(__MINGW64__) || defined(__clang__) || defined(ESP_PLATFORM)
#include <time.h>
//------------------------------------------------------------------------------
void wr_std_time( WRValue* stackTop, const int argn, WRContext* c )
{
	stackTop->p2 = INIT_AS_INT;
	stackTop->i = (int32_t)time(0);
}
#elif defined(ARDUINO)
//------------------------------------------------------------------------------
void wr_std_time( WRValue* stackTop, const int argn, WRContext* c )
{
	stackTop->p2 = INIT_AS_INT;
	stackTop->i = (int32_t)(millis() / 1000);
}
#else
//------------------------------------------------------------------------------
void wr_std_time( WRValue* stackTop, const int argn, WRContext* c )
{
	stackTop->init();
}
#endif

// time::ms: a 32 bit millisecond counter that wraps around
//   Arduino (any board, ESP32 with the Arduino core too): millis(), since
//     the board started
//   ESP-IDF without Arduino: esp_timer_get_time() / 1000, since the start
//   STM32 HAL (USE_HAL_DRIVER): HAL_GetTick(), since HAL_Init()
//   Windows: GetTickCount(), since the system started
//   Linux and macOS: the monotonic clock, setting the time does not move it
//   anything else: 0
#if defined(ARDUINO)
// millis() comes with Arduino.h (wrench.h)
#elif defined(ESP_PLATFORM)
#include "esp_timer.h"
#elif defined(USE_HAL_DRIVER)
extern "C" uint32_t HAL_GetTick( void );
#elif defined(_WIN32)
#include <Windows.h>
#elif defined(__linux__) || defined(__APPLE__)
#include <time.h>
#endif

//------------------------------------------------------------------------------
void wr_milliseconds(WRValue* stackTop, const int argn, WRContext* c )
{
	stackTop->init();

#if defined(ARDUINO)
	stackTop->ui = (uint32_t)millis();
#elif defined(ESP_PLATFORM)
	stackTop->ui = (uint32_t)(esp_timer_get_time() / 1000);
#elif defined(USE_HAL_DRIVER)
	stackTop->ui = (uint32_t)HAL_GetTick();
#elif defined(_WIN32)
	stackTop->ui = (uint32_t)GetTickCount();
#elif defined(__linux__) || defined(__APPLE__)
	struct timespec now;
	if ( clock_gettime(CLOCK_MONOTONIC, &now) == 0 )
	{
		stackTop->ui = (uint32_t)now.tv_sec * 1000u + (uint32_t)(now.tv_nsec / 1000000);
	}
#endif
}


//------------------------------------------------------------------------------
void wr_loadStdLib( WRState* w )
{
	// the scripts of this state start from the host's seed
	w->randSeed = wr_Seed;

	wr_registerLibraryFunction( w, "std::rand", wr_std_rand );
	wr_registerLibraryFunction( w, "std::srand", wr_std_srand );
	wr_registerLibraryFunction( w, "std::time", wr_std_time );

	wr_registerLibraryFunction( w, "time::ms", wr_milliseconds ); // return ms count that always increases
}

//------------------------------------------------------------------------------
void wr_loadAllLibs( WRState* w )
{
	wr_loadMathLib( w );
	wr_loadStdLib( w );
	wr_loadIOLib( w );
	wr_loadStringLib( w );
	wr_loadMessageLib( w );
	wr_loadSysLib( w );
	wr_loadSerializeLib( w );
	wr_loadDebugLib( w );
	wr_loadTCPLib( w );
	wr_loadContainerLib( w );
}
