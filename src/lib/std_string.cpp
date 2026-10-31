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

#include <string.h>
#include <ctype.h>

//------------------------------------------------------------------------------
int wr_strlenEx( WRValue* val )
{
	if ( val->type == WR_REF )
	{
		return wr_strlenEx( val->r );
	}

	return (val->xtype == WR_EX_ARRAY && val->va->m_type == SV_CHAR) ? val->va->m_size :  0;
}

//------------------------------------------------------------------------------
void wr_strlen( WRValue* stackTop, const int argn, WRContext* c )
{
	stackTop->i = argn == 1 ? wr_strlenEx( stackTop - 1 ) : 0;
}

//------------------------------------------------------------------------------
inline void addChar( char* out, char c, unsigned int& pos, const unsigned int max )
{
	if ( pos < max )
	{
		out[pos] = c;
	}
	
	++pos;
}

//------------------------------------------------------------------------------
// one conversion of a format, as C printf reads it:
// %[flags][width][.precision][length]conversion
struct WRFormatSpec
{
	unsigned int width;
	int precision; // -1 when none was given
	char conversion;
	bool left; // '-'
	bool plus; // '+'
	bool space; // ' '
	bool zero; // '0'
	bool alt; // '#'
};

// widths and precisions are capped, absurd ones would only waste memory
#define WR_FORMAT_MAX_COLUMNS 4096

//------------------------------------------------------------------------------
// writes everything of a field before a body of 'bodyLen' characters: the
// padding, the sign (0 for none), a prefix ("0x") and 'zeros' zeros; with
// 'zeroPad' the padding is zeros after the sign and the prefix. Returns the
// number of spaces that go after the body (left justified)
static unsigned int wr_fieldStart( char* out, unsigned int& pos, const unsigned int max, const WRFormatSpec& spec, const char sign, const char* prefix, unsigned int zeros, const unsigned int bodyLen, const bool zeroPad )
{
	unsigned int total = zeros + bodyLen + (sign ? 1 : 0);
	for( const char* p = prefix; p && *p; ++p )
	{
		++total;
	}

	unsigned int pad = (spec.width > total) ? spec.width - total : 0;
	if ( !spec.left )
	{
		if ( zeroPad )
		{
			zeros += pad;
		}
		else
		{
			for( ; pad; --pad )
			{
				addChar( out, ' ', pos, max );
			}
		}
		pad = 0;
	}

	if ( sign )
	{
		addChar( out, sign, pos, max );
	}

	for( const char* p = prefix; p && *p; ++p )
	{
		addChar( out, *p, pos, max );
	}

	for( ; zeros; --zeros )
	{
		addChar( out, '0', pos, max );
	}

	return pad;
}

//------------------------------------------------------------------------------
static void wr_fieldEnd( char* out, unsigned int& pos, const unsigned int max, unsigned int pad )
{
	for( ; pad; --pad )
	{
		addChar( out, ' ', pos, max );
	}
}

//------------------------------------------------------------------------------
// d i u x X o, and b (binary) and p (pointer: "0x" and upper case hex
// digits) which C does not have like this
static void wr_formatInt( char* out, unsigned int& pos, const unsigned int max, const WRFormatSpec& spec, uint32_t val )
{
	const char c = spec.conversion;

	char sign = 0;
	if ( c == 'd' || c == 'i' )
	{
		if ( val & 0x80000000 )
		{
			sign = '-';
			val = 0u - val; // the magnitude, also right for INT_MIN
		}
		else if ( spec.plus )
		{
			sign = '+';
		}
		else if ( spec.space )
		{
			sign = ' ';
		}
	}

	unsigned int base = 10;
	if ( c == 'o' )
	{
		base = 8;
	}
	else if ( c == 'x' || c == 'X' || c == 'p' )
	{
		base = 16;
	}
	else if ( c == 'b' )
	{
		base = 2;
	}

	const bool nonZero = val != 0;

	// least significant first; a precision of 0 prints no digit for 0
	char digits[32];
	unsigned int len = 0;
	if ( nonZero || spec.precision != 0 )
	{
		do
		{
			const unsigned int d = val % base;
			val /= base;
			digits[len++] = (char)((d < 10) ? ('0' + d) : (((c == 'x') ? 'a' : 'A') + d - 10));
		} while( val );
	}

	// the precision is the minimum number of digits
	unsigned int zeros = (spec.precision > (int)len) ? (unsigned int)spec.precision - len : 0;

	const char* prefix = 0;
	if ( c == 'p' )
	{
		prefix = "0x";
	}
	else if ( spec.alt )
	{
		if ( c == 'o' )
		{
			// the first digit is a zero
			if ( !zeros && (!len || digits[len - 1] != '0') )
			{
				zeros = 1;
			}
		}
		else if ( nonZero )
		{
			prefix = (c == 'x') ? "0x" : (c == 'X') ? "0X" : (c == 'b') ? "0b" : 0;
		}
	}

	// with a precision the 0 flag is ignored
	const unsigned int pad = wr_fieldStart( out, pos, max, spec, sign, prefix, zeros, len, spec.zero && spec.precision < 0 );
	while( len )
	{
		addChar( out, digits[--len], pos, max );
	}
	wr_fieldEnd( out, pos, max, pad );
}

//------------------------------------------------------------------------------
// s and c: 'len' characters of 'text', a precision cuts a string
static void wr_formatText( char* out, unsigned int& pos, const unsigned int max, const WRFormatSpec& spec, const char* text, unsigned int len )
{
	if ( spec.conversion == 's' && spec.precision >= 0 && (unsigned int)spec.precision < len )
	{
		len = (unsigned int)spec.precision;
	}

	const unsigned int pad = wr_fieldStart( out, pos, max, spec, 0, 0, 0, len, false );
	for( unsigned int l = 0; l < len; ++l )
	{
		addChar( out, text[l], pos, max );
	}
	wr_fieldEnd( out, pos, max, pad );
}

#ifdef WRENCH_FLOAT_SPRINTF

// the C library formats the number, the field is done here
#define WR_FORMAT_MAX_FLOAT_PRECISION 80

//------------------------------------------------------------------------------
// f F e E g G
static void wr_formatFloat( char* out, unsigned int& pos, const unsigned int max, const WRFormatSpec& spec, const float value )
{
	char cspec[8];
	int s = 0;
	cspec[s++] = '%';
	if ( spec.plus )
	{
		cspec[s++] = '+';
	}
	if ( spec.space )
	{
		cspec[s++] = ' ';
	}
	if ( spec.alt )
	{
		cspec[s++] = '#';
	}
	cspec[s++] = '.';
	cspec[s++] = '*';
	cspec[s++] = spec.conversion;
	cspec[s] = 0;

	int precision = (spec.precision < 0) ? 6 : spec.precision;
	if ( precision > WR_FORMAT_MAX_FLOAT_PRECISION )
	{
		precision = WR_FORMAT_MAX_FLOAT_PRECISION;
	}

	// suck in whatever lib we need for this; the largest float with the
	// largest precision fits
	char buf[128];
	int len = snprintf( buf, sizeof(buf), cspec, precision, (double)value );
	if ( len < 0 )
	{
		len = 0;
	}
	else if ( len > (int)sizeof(buf) - 1 )
	{
		len = (int)sizeof(buf) - 1;
	}

	const char* body = buf;
	char sign = 0;
	if ( len && (buf[0] == '-' || buf[0] == '+' || buf[0] == ' ') )
	{
		sign = buf[0];
		++body;
		--len;
	}

	// infinity and nan are padded with spaces
	const bool finite = (value == value) && (value - value == 0);

	const unsigned int pad = wr_fieldStart( out, pos, max, spec, sign, 0, 0, (unsigned int)len, spec.zero && finite );
	for( int l = 0; l < len; ++l )
	{
		addChar( out, body[l], pos, max );
	}
	wr_fieldEnd( out, pos, max, pad );
}

#else

// Without the C library the digits come from the exact value of the float,
// so the result is what a correct C library prints (glibc does), rounded
// half to even. A float is m * 2^e with an integer m below 2^24 and e from
// -149 to 104: its decimal digits are those of the integer m * 2^e, or of
// m * 5^-e moved -e places to the right of the point. That is at most 113
// digits

#define WR_FLOAT_DIGITS 116

//------------------------------------------------------------------------------
// the decimal digits of m * 2^e ('0' to '9', most significant first, no
// leading zero) and the number of digits before the point; 0 has none
static int wr_floatToDigits( uint32_t m, const int e, char* digits, int& point )
{
	// least significant first while multiplying
	int n = 0;
	for( ; m; m /= 10 )
	{
		digits[n++] = (char)(m % 10);
	}

	if ( !n )
	{
		point = 1;
		return 0;
	}

	const unsigned int factor = (e > 0) ? 2 : 5;
	for( int k = (e > 0) ? e : -e; k > 0; --k )
	{
		unsigned int carry = 0;
		for( int i = 0; i < n; ++i )
		{
			const unsigned int v = (unsigned int)digits[i] * factor + carry;
			digits[i] = (char)(v % 10);
			carry = v / 10;
		}
		if ( carry )
		{
			digits[n++] = (char)carry;
		}
	}

	for( int i = 0; i < n / 2; ++i )
	{
		const char t = digits[i];
		digits[i] = digits[n - 1 - i];
		digits[n - 1 - i] = t;
	}
	for( int i = 0; i < n; ++i )
	{
		digits[i] += '0';
	}

	point = n + ((e < 0) ? e : 0);
	return n;
}

//------------------------------------------------------------------------------
// keeps the first 'keep' digits, rounded half to even; a carry out of the
// first digit adds a digit in front (and moves the point)
static void wr_roundDigits( char* digits, int& n, int& point, const int keep )
{
	if ( keep >= n )
	{
		return;
	}

	bool up = false;
	if ( keep >= 0 )
	{
		const char next = digits[keep];
		if ( next > '5' )
		{
			up = true;
		}
		else if ( next == '5' )
		{
			// exactly half rounds to the even digit (nothing kept is 0, even)
			up = (keep > 0) && ((digits[keep - 1] - '0') & 1);
			for( int i = keep + 1; i < n && !up; ++i )
			{
				up = digits[i] != '0';
			}
		}
	}

	n = (keep > 0) ? keep : 0;

	if ( up )
	{
		int i = n - 1;
		for( ; i >= 0 && digits[i] == '9'; --i )
		{
			digits[i] = '0';
		}

		if ( i >= 0 )
		{
			++digits[i];
		}
		else
		{
			memmove( digits + 1, digits, n );
			digits[0] = '1';
			++n;
			++point;
		}
	}
}

//------------------------------------------------------------------------------
// the i-th digit, zeros on both sides of the ones there are
static char wr_digitAt( const char* digits, const int n, const int i )
{
	return (i >= 0 && i < n) ? digits[i] : '0';
}

//------------------------------------------------------------------------------
// f F e E g G
static void wr_formatFloat( char* out, unsigned int& pos, const unsigned int max, const WRFormatSpec& spec, const float value )
{
	uint32_t bits;
	memcpy( &bits, &value, sizeof(bits) );

	const char c = spec.conversion;
	const bool upper = (c == 'F' || c == 'E' || c == 'G');
	const char sign = (bits & 0x80000000) ? '-' : spec.plus ? '+' : spec.space ? ' ' : 0;
	const int exponent = (int)((bits >> 23) & 0xFF);
	uint32_t m = bits & 0x7FFFFF;

	if ( exponent == 0xFF )
	{
		const char* text = m ? (upper ? "NAN" : "nan") : (upper ? "INF" : "inf");
		const unsigned int pad = wr_fieldStart( out, pos, max, spec, sign, 0, 0, 3, false );
		for( int l = 0; l < 3; ++l )
		{
			addChar( out, text[l], pos, max );
		}
		wr_fieldEnd( out, pos, max, pad );
		return;
	}

	int e = -149; // a denormal
	if ( exponent )
	{
		m |= 0x800000;
		e = exponent - 150;
	}

	char digits[WR_FLOAT_DIGITS];
	int point;
	int n = wr_floatToDigits( m, e, digits, point );

	int precision = (spec.precision < 0) ? 6 : spec.precision;
	bool expForm = (c == 'e' || c == 'E');
	bool trim = false;
	if ( c == 'g' || c == 'G' )
	{
		// 'precision' significant digits, in the form the exponent of the
		// rounded value picks; trailing zeros are dropped unless '#'
		if ( !precision )
		{
			precision = 1;
		}
		wr_roundDigits( digits, n, point, precision );
		const int x = n ? point - 1 : 0;
		if ( x < -4 || x >= precision )
		{
			expForm = true;
			precision -= 1;
		}
		else
		{
			precision -= 1 + x;
		}
		trim = !spec.alt;
	}
	else if ( expForm )
	{
		wr_roundDigits( digits, n, point, precision + 1 );
	}
	else
	{
		wr_roundDigits( digits, n, point, point + precision );
	}

	// where the fraction starts, and how much of it is printed
	const int first = expForm ? 1 : point;
	int fraction = precision;
	if ( trim )
	{
		while( fraction > 0 && wr_digitAt(digits, n, first + fraction - 1) == '0' )
		{
			--fraction;
		}
	}
	const bool dot = fraction > 0 || spec.alt;

	const int x = n ? point - 1 : 0;
	const unsigned int ax = (x < 0) ? -x : x;

	unsigned int bodyLen = (unsigned int)fraction + (dot ? 1 : 0);
	if ( expForm )
	{
		bodyLen += 1 + 2 + ((ax >= 100) ? 3 : 2); // d e+XX
	}
	else
	{
		bodyLen += (point > 0) ? (unsigned int)point : 1;
	}

	const unsigned int pad = wr_fieldStart( out, pos, max, spec, sign, 0, 0, bodyLen, spec.zero );

	if ( expForm )
	{
		addChar( out, wr_digitAt(digits, n, 0), pos, max );
	}
	else if ( point <= 0 )
	{
		addChar( out, '0', pos, max );
	}
	else
	{
		for( int i = 0; i < point; ++i )
		{
			addChar( out, wr_digitAt(digits, n, i), pos, max );
		}
	}

	if ( dot )
	{
		addChar( out, '.', pos, max );
	}

	for( int i = 0; i < fraction; ++i )
	{
		addChar( out, wr_digitAt(digits, n, first + i), pos, max );
	}

	if ( expForm )
	{
		addChar( out, upper ? 'E' : 'e', pos, max );
		addChar( out, (x < 0) ? '-' : '+', pos, max );
		if ( ax >= 100 )
		{
			addChar( out, (char)('0' + ax / 100), pos, max );
		}
		addChar( out, (char)('0' + (ax / 10) % 10), pos, max );
		addChar( out, (char)('0' + ax % 10), pos, max );
	}

	wr_fieldEnd( out, pos, max, pad );
}

#endif

//------------------------------------------------------------------------------
// formats like C sprintf: the conversions d i u x X o c s f F e E g G and %%,
// with the flags - + space 0 #, a width and a precision (both may be *),
// plus b (binary) and p (as "0x" and upper case hex). Length modifiers (l,
// h, ...) are skipped, every int is 32 bits. A conversion without an
// argument takes 0 (c prints nothing), an unknown one prints nothing.
// Writes at most 'outsize' characters, returns the length of the whole
// result
int wr_sprintfEx( char* outbuf,
				  const unsigned int outsize,
				  const char* fmt,
				  const unsigned int fmtsize,
				  WRValue* args,
				  const int argn )
{
	if ( !fmt )
	{
		return 0;
	}

	unsigned int pos = 0;
	const char* fmtend = fmt + fmtsize;
	int listPtr = 0;

	while( fmt < fmtend )
	{
		const char c = *fmt++;
		if ( c != '%' )
		{
			addChar( outbuf, c, pos, outsize );
			continue;
		}

		WRFormatSpec spec;
		spec.width = 0;
		spec.precision = -1;
		spec.conversion = 0;
		spec.left = false;
		spec.plus = false;
		spec.space = false;
		spec.zero = false;
		spec.alt = false;

		for( ; fmt < fmtend; ++fmt )
		{
			if ( *fmt == '-' )
			{
				spec.left = true;
			}
			else if ( *fmt == '+' )
			{
				spec.plus = true;
			}
			else if ( *fmt == ' ' )
			{
				spec.space = true;
			}
			else if ( *fmt == '0' )
			{
				spec.zero = true;
			}
			else if ( *fmt == '#' )
			{
				spec.alt = true;
			}
			else
			{
				break;
			}
		}

		if ( fmt < fmtend && *fmt == '*' )
		{
			// a negative width from the argument is the '-' flag
			++fmt;
			int width = (listPtr < argn) ? args[listPtr++].asInt() : 0;
			if ( width < 0 )
			{
				spec.left = true;
				width = (width < -WR_FORMAT_MAX_COLUMNS) ? WR_FORMAT_MAX_COLUMNS : -width;
			}
			spec.width = (width > WR_FORMAT_MAX_COLUMNS) ? WR_FORMAT_MAX_COLUMNS : (unsigned int)width;
		}
		else
		{
			for( ; fmt < fmtend && *fmt >= '0' && *fmt <= '9'; ++fmt )
			{
				spec.width = spec.width * 10 + (*fmt - '0');
				if ( spec.width > WR_FORMAT_MAX_COLUMNS )
				{
					spec.width = WR_FORMAT_MAX_COLUMNS;
				}
			}
		}

		if ( fmt < fmtend && *fmt == '.' )
		{
			++fmt;
			spec.precision = 0;
			if ( fmt < fmtend && *fmt == '*' )
			{
				// a negative precision from the argument is none at all
				++fmt;
				const int precision = (listPtr < argn) ? args[listPtr++].asInt() : 0;
				spec.precision = (precision < 0) ? -1 : (precision > WR_FORMAT_MAX_COLUMNS) ? WR_FORMAT_MAX_COLUMNS : precision;
			}
			else
			{
				for( ; fmt < fmtend && *fmt >= '0' && *fmt <= '9'; ++fmt )
				{
					spec.precision = spec.precision * 10 + (*fmt - '0');
					if ( spec.precision > WR_FORMAT_MAX_COLUMNS )
					{
						spec.precision = WR_FORMAT_MAX_COLUMNS;
					}
				}
			}
		}

		// length modifiers are accepted and ignored, every integer is 32 bits
		while( fmt < fmtend && (*fmt == 'l' || *fmt == 'h' || *fmt == 'L' || *fmt == 'z' || *fmt == 'j' || *fmt == 't' || *fmt == 'q') )
		{
			++fmt;
		}

		if ( fmt >= fmtend )
		{
			break; // the format ends inside the specifier
		}

		spec.conversion = *fmt++;

		switch( spec.conversion )
		{
			case '%':
			{
				addChar( outbuf, '%', pos, outsize );
				break;
			}

			case 'c':
			{
				if ( listPtr < argn )
				{
					const char ch = (char)args[listPtr++].asInt();
					wr_formatText( outbuf, pos, outsize, spec, &ch, 1 );
				}
				break;
			}

			case 's':
			{
				// large enough for any single value formatted as text
				char buf[34];
				const char* text = buf;
				unsigned int len = 0;
				if ( listPtr < argn )
				{
					text = (const char*)args[listPtr].array( &len );
					if ( !text )
					{
						// not a string, format the value as text instead
						args[listPtr].asString( buf, sizeof(buf), &len );
						text = buf;
					}
					++listPtr;
				}
				wr_formatText( outbuf, pos, outsize, spec, text, len );
				break;
			}

			case 'd':
			case 'i':
			case 'u':
			case 'x':
			case 'X':
			case 'o':
			case 'b':
			case 'p':
			{
				const uint32_t val = (listPtr < argn) ? (uint32_t)args[listPtr++].asInt() : 0;
				wr_formatInt( outbuf, pos, outsize, spec, val );
				break;
			}

			case 'f':
			case 'F':
			case 'e':
			case 'E':
			case 'g':
			case 'G':
			{
				// always consumed, so the specifiers after it line up with
				// their arguments
				const float val = (listPtr < argn) ? args[listPtr++].asFloat() : 0.f;
				wr_formatFloat( outbuf, pos, outsize, spec, val );
				break;
			}

			default:
			{
				break; // not a conversion, print nothing for it
			}
		}
	}

	return pos;
}


#ifndef WR_FORMAT_TRY_SIZE
#define WR_FORMAT_TRY_SIZE 80
#endif

//------------------------------------------------------------------------------
unsigned int wr_doSprintf( WRValue& to, WRValue& fmt, WRValue* args, const int argn, WRContext* c )
{
	unsigned int fmtBufferSize = 0;
	char* fmtbuffer = (char*)fmt.array( &fmtBufferSize, SV_CHAR );
	if ( !fmtbuffer )
	{
		return 0;
	}

	// always format into a new string and only then make 'to' refer to it:
	// the string 'to' held may be shared with other variables, and it may
	// be one of the arguments (or the format) being read right now
	char tmpbuf[WR_FORMAT_TRY_SIZE];
	const unsigned int size = wr_sprintfEx( tmpbuf, WR_FORMAT_TRY_SIZE, fmtbuffer, fmtBufferSize, args, argn );

	WRGCObject* out = c->getSVA( size, SV_CHAR, false );
#ifdef WRENCH_HANDLE_MALLOC_FAIL
	if ( !out )
	{
		to.init();
		return 0;
	}
#endif

	if ( size <= WR_FORMAT_TRY_SIZE )
	{
		memcpy( out->m_Cdata, tmpbuf, size );
	}
	else
	{
		// did not fit the temp buffer, format again straight into the
		// new string
		wr_sprintfEx( (char *)out->m_Cdata, size, fmtbuffer, fmtBufferSize, args, argn );
	}

	to.p2 = INIT_AS_ARRAY;
	to.va = out;

	return size;
}


//------------------------------------------------------------------------------
void wr_format( WRValue* stackTop, const int argn, WRContext* c )
{
	if( argn > 0 )
	{
		WRValue* args = stackTop - argn;
		wr_doSprintf( *stackTop, args->deref(), args + 1, argn - 1, c );
	}
}

//------------------------------------------------------------------------------
void wr_sprintf( WRValue* stackTop, const int argn, WRContext* c )
{
	if( argn > 1 )
	{
		WRValue* args = stackTop - argn;
		stackTop->i = wr_doSprintf( args->deref(), args[1].deref(), args + 2, argn - 2, c );
	}
}

//------------------------------------------------------------------------------
void wr_printf( WRValue* stackTop, const int argn, WRContext* c )
{
	unsigned int chars = 0;
	if( argn > 0 )
	{
		WRValue* args = stackTop - argn;
		if ( (chars = wr_doSprintf( *stackTop, args->deref(), args + 1, argn - 1, c )) )
		{
#if !defined(WRENCH_WIN32_FILE_IO) && !defined(WRENCH_LINUX_FILE_IO) && !defined(WRENCH_SPIFFS_FILE_IO) && !defined(WRENCH_LITTLEFS_FILE_IO)
			// the formatted array is not null terminated
			fwrite( stackTop->va->m_Cdata, 1, stackTop->va->m_size, stdout );
#else
			wr_stdout( (const char*)stackTop->va->m_Cdata, stackTop->va->m_size );
#endif
		}
	}

	stackTop->p2 = WR_INT;
	stackTop->i = chars;
}

//------------------------------------------------------------------------------
void wr_isspace( WRValue* stackTop, const int argn, WRContext* c )
{
	if ( argn == 1 )
	{
		stackTop->i = (int)isspace( (unsigned char)(stackTop - 1)->asInt() );
	}
}

//------------------------------------------------------------------------------
void wr_isalpha( WRValue* stackTop, const int argn, WRContext* c )
{
	if ( argn == 1 )
	{
		stackTop->i = (int)isalpha( (unsigned char)(stackTop - 1)->asInt() );
	}
}

//------------------------------------------------------------------------------
void wr_isdigit( WRValue* stackTop, const int argn, WRContext* c )
{
	if ( argn == 1 )
	{
		stackTop->i = (int)isdigit( (unsigned char)(stackTop - 1)->asInt() );
	}
}

//------------------------------------------------------------------------------
void wr_isalnum( WRValue* stackTop, const int argn, WRContext* c )
{
	if ( argn == 1 )
	{
		stackTop->i = (int)isalnum( (unsigned char)(stackTop - 1)->asInt() );
	}
}

//------------------------------------------------------------------------------
void wr_mid( WRValue* stackTop, const int argn, WRContext* c )
{
	if ( argn < 2 )
	{
		return;
	}

	WRValue* args = stackTop - argn;
	unsigned int len;
	const char* data = (char *)(args[0].array(&len));

	// an empty string gives an empty string, like left/right/trim
	if( !data )
	{
		return;
	}
	
	unsigned int start = args[1].asInt();

	unsigned int chars = 0;
	if ( start < len )
	{
		chars = (argn > 2) ? args[2].asInt() : len;
		if ( chars > (len - start) )
		{
			chars = len - start;
		}
	}
	
	stackTop->va = c->getSVA( chars, SV_CHAR, false );

	#ifdef WRENCH_HANDLE_MALLOC_FAIL
	if ( !stackTop->va )
	{
		return;
	}
	#endif
	
	stackTop->p2 = INIT_AS_ARRAY;
	memcpy( stackTop->va->m_Cdata, data + start, chars );
}

//------------------------------------------------------------------------------
void wr_strchr( WRValue* stackTop, const int argn, WRContext* c )
{
	stackTop->i = -1;

	if ( argn < 2 )
	{
		return;
	}

	WRValue* args = stackTop - argn;

	unsigned int len;
	const char* str = (const char*)(args[0].array(&len));
	if ( !str )
	{
		return;
	}
	
	char ch = (char)args[1].asInt();
	for( unsigned int i=0; i<len; ++i )
	{
		if ( str[i] == ch )
		{
			stackTop->i = i;
			break;
		}
	}
}

//------------------------------------------------------------------------------
void wr_tolower( WRValue* stackTop, const int argn, WRContext* c )
{
	if ( argn == 1 )
	{
		WRValue& value = (stackTop - 1)->deref();
		if ( value.xtype == WR_EX_ARRAY && value.va->m_type == SV_CHAR )
		{
			stackTop->va = c->getSVA( value.va->m_size, SV_CHAR, false );

#ifdef WRENCH_HANDLE_MALLOC_FAIL
			if ( !stackTop->va )
			{
				return;
			}
#endif
			stackTop->p2 = INIT_AS_ARRAY;

			for( uint32_t i=0; i<value.va->m_size; ++i )
			{
				stackTop->va->m_SCdata[i] = (char)tolower( (unsigned char)value.va->m_SCdata[i] );
			}
		}
		else if ( value.type == WR_INT )
		{
			const int ch = (stackTop - 1)->asInt();
			stackTop->i = (ch >= 0 && ch <= 255) ? (int)tolower( ch ) : ch; // ctype is only defined for unsigned char values
		}
	}
}

//------------------------------------------------------------------------------
void wr_toupper( WRValue* stackTop, const int argn, WRContext* c )
{
	if ( argn == 1 )
	{
		WRValue& value = (stackTop - 1)->deref();
		if ( value.xtype == WR_EX_ARRAY && value.va->m_type == SV_CHAR )
		{
			stackTop->va = c->getSVA( value.va->m_size, SV_CHAR, false );
#ifdef WRENCH_HANDLE_MALLOC_FAIL
			if ( !stackTop->va )
			{
				return;
			}
#endif
			stackTop->p2 = INIT_AS_ARRAY;

			for( uint32_t i=0; i<value.va->m_size; ++i )
			{
				stackTop->va->m_SCdata[i] = (char)toupper( (unsigned char)value.va->m_SCdata[i] );
			}
		}
		else if ( value.type == WR_INT )
		{
			const int ch = (stackTop - 1)->asInt();
			stackTop->i = (ch >= 0 && ch <= 255) ? (int)toupper( ch ) : ch;
		}

	}
}

//------------------------------------------------------------------------------
// strtol() for a string that is not null terminated and may be of any
// length; the result is clamped to 32 bits on every platform (like strtol
// where long is 32 bits). Returns 0 for an invalid base or no digits
static int32_t wr_strToL( const char* s, const unsigned int len, int base )
{
	unsigned int pos = 0;
	while( pos < len && isspace((unsigned char)s[pos]) )
	{
		++pos;
	}

	bool negative = false;
	if ( pos < len && (s[pos] == '-' || s[pos] == '+') )
	{
		negative = (s[pos] == '-');
		++pos;
	}

	if ( base < 0 || base == 1 || base > 36 )
	{
		return 0;
	}

	// a "0x" prefix only counts when a hex digit follows it
	const bool hexPrefix = (pos + 2 < len)
		&& s[pos] == '0'
		&& (s[pos + 1] == 'x' || s[pos + 1] == 'X')
		&& isxdigit( (unsigned char)s[pos + 2] );

	if ( base == 0 )
	{
		if ( hexPrefix )
		{
			base = 16;
			pos += 2;
		}
		else if ( pos < len && s[pos] == '0' )
		{
			base = 8;
		}
		else
		{
			base = 10;
		}
	}
	else if ( base == 16 && hexPrefix )
	{
		pos += 2;
	}

	// magnitude limit: 2147483647, or 2147483648 for a negative result
	const uint32_t limit = negative ? 0x80000000u : 0x7FFFFFFFu;
	uint32_t value = 0;
	bool overflow = false;
	for( ; pos < len; ++pos )
	{
		const char ch = s[pos];
		int digit;
		if ( ch >= '0' && ch <= '9' )
		{
			digit = ch - '0';
		}
		else if ( ch >= 'a' && ch <= 'z' )
		{
			digit = ch - 'a' + 10;
		}
		else if ( ch >= 'A' && ch <= 'Z' )
		{
			digit = ch - 'A' + 10;
		}
		else
		{
			break;
		}

		if ( digit >= base )
		{
			break;
		}

		if ( !overflow )
		{
			if ( value > (limit - (uint32_t)digit) / (uint32_t)base )
			{
				overflow = true; // keep consuming digits, the result saturates
				value = limit;
			}
			else
			{
				value = value * (uint32_t)base + (uint32_t)digit;
			}
		}
	}

	return negative ? (int32_t)(0u - value) : (int32_t)value;
}

void wr_tol( WRValue* stackTop, const int argn, WRContext* c )
{
	if ( argn != 1 && argn != 2 )
	{
		return;
	}

	WRValue* args = stackTop - argn;
	const int base = (argn == 2) ? args[1].asInt() : 10;

	unsigned int len = 0;
	const char* str = (const char*)args[0].array( &len, SV_CHAR );
	if ( str )
	{
		stackTop->i = wr_strToL( str, len, base );
	}
	else
	{
		// a number: parse its text form, which always fits this buffer
		char buf[34];
		unsigned int numLen = 0;
		args[0].asString( buf, sizeof(buf), &numLen );
		stackTop->i = wr_strToL( buf, numLen, base );
	}
}

//------------------------------------------------------------------------------
void wr_concat( WRValue* stackTop, const int argn, WRContext* c )
{
	if ( argn < 2 )
	{
		return;
	}

	WRValue* args = stackTop - argn;
	unsigned int len1 = 0;
	unsigned int len2 = 0;
	const char* data1 = (const char*)args[0].array( &len1 );
	const char* data2 = (const char*)args[1].array( &len2 );

	if( !data1 || !data2 )
	{
		return;
	}

	stackTop->va = c->getSVA( len1 + len2, SV_CHAR, false );
#ifdef WRENCH_HANDLE_MALLOC_FAIL
	if ( !stackTop->va )
	{
		return;
	}
#endif
	stackTop->p2 = INIT_AS_ARRAY;
	memcpy( stackTop->va->m_Cdata, data1, len1 );
	memcpy( stackTop->va->m_Cdata + len1, data2, len2 );
}

//------------------------------------------------------------------------------
void wr_left( WRValue* stackTop, const int argn, WRContext* c )
{
	if ( argn < 2 )
	{
		return;
	}

	WRValue* args = stackTop - argn;
	unsigned int len = 0;
	const char* data = (const char*)args[0].array(&len );
	if ( !data )
	{
		return;
	}
	unsigned int chars = args[1].asInt();

	if ( chars > len )
	{
		chars = len;
	}
	
	stackTop->va = c->getSVA( chars, SV_CHAR, false );
#ifdef WRENCH_HANDLE_MALLOC_FAIL
	if ( !stackTop->va )
	{
		return;
	}
#endif
	stackTop->p2 = INIT_AS_ARRAY;

	memcpy( stackTop->va->m_Cdata, data, chars );
}

//------------------------------------------------------------------------------
void wr_right( WRValue* stackTop, const int argn, WRContext* c )
{
	if ( argn < 2 )
	{
		return;
	}

	WRValue* args = stackTop - argn;
	unsigned int len = 0;
	const char* data = (const char*)args[0].array(&len );
	if ( !data )
	{
		return;
	}
	unsigned int chars = args[1].asInt();

	if ( chars > len )
	{
		chars = len;
	}

	stackTop->va = c->getSVA( chars, SV_CHAR, false );
#ifdef WRENCH_HANDLE_MALLOC_FAIL
	if ( !stackTop->va )
	{
		return;
	}
#endif
	stackTop->p2 = INIT_AS_ARRAY;
	memcpy( stackTop->va->m_Cdata, data + (len - chars), chars );
}

//------------------------------------------------------------------------------
void wr_trimright( WRValue* stackTop, const int argn, WRContext* c )
{
	WRValue* args = stackTop - argn;
	const char* data;
	int len = 0;
	
	if ( argn < 1 || ((data = (const char*)args->array((unsigned int *)&len)) == 0) )
	{
		return;
	}

	while( --len >= 0 && isspace((unsigned char)data[len]) )
	{
	}

	++len;

	stackTop->va = c->getSVA( len, SV_CHAR, false );
#ifdef WRENCH_HANDLE_MALLOC_FAIL
	if ( !stackTop->va )
	{
		return;
	}
#endif
	stackTop->p2 = INIT_AS_ARRAY;

	memcpy( stackTop->va->m_Cdata, data, len );
}

//------------------------------------------------------------------------------
void wr_trimleft( WRValue* stackTop, const int argn, WRContext* c )
{
	WRValue* args = stackTop - argn;
	const char* data;
	unsigned int len = 0;
	
	if ( argn < 1 || ((data = (const char*)args->array(&len)) == 0) )
	{
		return;
	}

	unsigned int marker = 0;
	while( marker < len && isspace((unsigned char)data[marker]) ) { ++marker; }

	stackTop->va = c->getSVA( len - marker, SV_CHAR, false );
#ifdef WRENCH_HANDLE_MALLOC_FAIL
	if ( !stackTop->va )
	{
		return;
	}
#endif
	stackTop->p2 = INIT_AS_ARRAY;

	memcpy( stackTop->va->m_Cdata, data + marker, len - marker );
}

//------------------------------------------------------------------------------
void wr_trim( WRValue* stackTop, const int argn, WRContext* c )
{
	WRValue* args = stackTop - argn;
	const char* data;
	int len = 0;
	if ( argn < 1 || ((data = (const char*)args->array((unsigned int*)&len)) == 0) )
	{
		return;
	}

	int marker = 0;
	while( marker < len && isspace((unsigned char)data[marker]) ) { ++marker; }

	while( --len >= marker && isspace((unsigned char)data[len]) )
	{
	}

	++len;

	stackTop->va = c->getSVA( len - marker, SV_CHAR, false );
#ifdef WRENCH_HANDLE_MALLOC_FAIL
	if ( !stackTop->va )
	{
		return;
	}
#endif
	stackTop->p2 = INIT_AS_ARRAY;

	memcpy( stackTop->va->m_Cdata, data + marker, len - marker );
}

//------------------------------------------------------------------------------
void wr_insert( WRValue* stackTop, const int argn, WRContext* c )
{
	if ( argn < 3 )
	{
		return;
	}

	WRValue* args = stackTop - argn;
	unsigned int len1 = 0;
	unsigned int len2 = 0;
	const char* data1 = (const char*)args[0].array( &len1 );
	const char* data2 = (const char*)args[1].array( &len2 );

	if( !data1 || !data2 )
	{
		return;
	}
	
	unsigned int pos = args[2].asInt();
	if ( pos >= len1 )
	{
		pos = len1;
	}

	unsigned int newlen = len1 + len2;
	stackTop->va = c->getSVA( newlen, SV_CHAR, false );
#ifdef WRENCH_HANDLE_MALLOC_FAIL
	if ( !stackTop->va )
	{
		return;
	}
#endif
	stackTop->p2 = INIT_AS_ARRAY;

	memcpy( stackTop->va->m_Cdata, data1, pos );
	memcpy( stackTop->va->m_Cdata + pos, data2, len2 );
	memcpy( stackTop->va->m_Cdata + pos + len2, data1 + pos, len1  - pos );
}

//------------------------------------------------------------------------------
void wr_tprint( WRValue* stackTop, const int argn, WRContext* c )
{
	if ( argn )
	{
		// on the heap: 4KB is too much for small task stacks
		const unsigned int size = 4096;
		char* buf = (char*)g_malloc( size );
		if ( !buf )
		{
			return;
		}
		(stackTop - argn)->technicalAsString( buf, size, false );
		printf( "\n%s\n",  buf );
		(stackTop - argn)->technicalAsString( buf, size, true );
		printf( "%s\n",  buf );
		g_free( buf );
	}
}


//------------------------------------------------------------------------------
void wr_loadStringLib( WRState* w )
{
	wr_registerLibraryFunction( w, "str::strlen", wr_strlen );

	wr_registerLibraryFunction( w, "str::sprintf", wr_sprintf );
	wr_registerLibraryFunction( w, "str::format", wr_format );
	
	wr_registerLibraryFunction( w, "str::printf", wr_printf );
	wr_registerLibraryFunction( w, "str::isspace", wr_isspace );
	wr_registerLibraryFunction( w, "str::isdigit", wr_isdigit );
	wr_registerLibraryFunction( w, "str::isalpha", wr_isalpha );
	wr_registerLibraryFunction( w, "str::isalnum", wr_isalnum );
	wr_registerLibraryFunction( w, "str::mid", wr_mid );
	wr_registerLibraryFunction( w, "str::chr", wr_strchr );
	wr_registerLibraryFunction( w, "str::tolower", wr_tolower );
	wr_registerLibraryFunction( w, "str::toupper", wr_toupper );
	wr_registerLibraryFunction( w, "str::tol", wr_tol );
	wr_registerLibraryFunction( w, "str::concat", wr_concat );
	wr_registerLibraryFunction( w, "str::left", wr_left );
	wr_registerLibraryFunction( w, "str::trunc", wr_left );
	wr_registerLibraryFunction( w, "str::right", wr_right );
	wr_registerLibraryFunction( w, "str::substr", wr_mid );
	wr_registerLibraryFunction( w, "str::trimright", wr_trimright );
	wr_registerLibraryFunction( w, "str::trimleft", wr_trimleft );
	wr_registerLibraryFunction( w, "str::trim", wr_trim );
	wr_registerLibraryFunction( w, "str::insert", wr_insert );

	wr_registerLibraryFunction( w, "str::tprint", wr_tprint );

}
