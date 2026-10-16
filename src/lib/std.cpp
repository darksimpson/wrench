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
const int32_t wr_randEx( const int32_t from, const int32_t to )
{
	if ( wr_Seed == 0 )
	{
		wr_Seed = 0xA5EED; // zero is a fixed point of the generator
	}

	const int32_t k = wr_Seed / 127773;
	wr_Seed = 16807 * (wr_Seed - k * 127773) - 2836 * k;

	if ( from >= to )
	{
		return from;
	}

	// unsigned math: the span of a full 32-bit range does not fit an int
	// and wraps to 0, which must not be used as a divisor
	const uint32_t range = (uint32_t)to - (uint32_t)from + 1u;
	uint32_t r = (uint32_t)wr_Seed;
	if ( range == 0 )
	{
		// the generator only yields 31 bits, mix in a second draw
		const int32_t k2 = wr_Seed / 127773;
		wr_Seed = 16807 * (wr_Seed - k2 * 127773) - 2836 * k2;
		r ^= ((uint32_t)wr_Seed) << 16;
		return (int32_t)(r + (uint32_t)from);
	}

	return (int32_t)((r % range) + (uint32_t)from);
}

//------------------------------------------------------------------------------
void wr_std_rand( WRValue* stackTop, const int argn, WRContext* c )
{
	if ( argn > 0 )
	{
		WRValue* args = stackTop - argn;

		stackTop->i = argn > 1 ?
					  wr_randEx(args[0].asInt(), args[1].asInt())
							   : wr_randEx( 0, args[0].asInt() );
	}
}

//------------------------------------------------------------------------------
void wr_std_srand( WRValue* stackTop, const int argn, WRContext* c )
{
	if ( argn == 1 )
	{
		wr_Seed = (uint32_t)((stackTop - 1)->asInt());
	}
}

#if defined(__arm__) || defined(_WIN32) || defined(__linux__) || defined(__MINGW32__) || defined(__APPLE__) || defined(__MINGW64__) || defined(__clang__)
#include <time.h>
//------------------------------------------------------------------------------
void wr_std_time( WRValue* stackTop, const int argn, WRContext* c )
{
	stackTop->p2 = INIT_AS_INT;
	stackTop->i = (int32_t)time(0);
}
#else
//------------------------------------------------------------------------------
void wr_std_time( WRValue* stackTop, const int argn, WRContext* c )
{
}
#endif

#ifdef __linux__
#include <unistd.h>
#include <sys/time.h>
#endif
#ifdef _WIN32
#include <Windows.h>
#endif

//------------------------------------------------------------------------------
void wr_milliseconds(WRValue* stackTop, const int argn, WRContext* c )
{
	stackTop->p2 = INIT_AS_INT;

#ifdef _WIN32
	stackTop->ui = (uint32_t)GetTickCount();
#endif
#ifdef __linux__
	struct timeval tv;
	gettimeofday( &tv, NULL );
	stackTop->ui = (uint32_t)((tv.tv_usec/1000) + (tv.tv_sec * 1000));
#endif
}


//------------------------------------------------------------------------------
void wr_loadStdLib( WRState* w )
{
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
