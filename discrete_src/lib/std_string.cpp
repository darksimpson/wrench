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

	enum
	{
		zeroPad         = 1<<0,
		negativeJustify = 1<<1,
		secondPass      = 1<<2,
		negativeSign    = 1<<3,
		parsingSigned   = 1<<4,
		altRep			= 1<<5,
		inPrecision = 1<<6,
	};

	const unsigned int c_maxColumns = 4096; // cap absurd field widths

	unsigned int pos = 0;
	const char* fmtend = fmt + fmtsize;
#ifdef WRENCH_FLOAT_SPRINTF
	const char* specStart = fmt;
#endif

	int listPtr = 0;

resetState:

	char padChar = ' ';
	unsigned int columns = 0;
	char flags = 0;

	for(;;)
	{
		if ( fmt >= fmtend )
		{
			goto sprintf_end;
		}

		char c = *fmt++;

		if ( !(secondPass & flags) )
		{
			if ( c != '%' ) // literal
			{
				addChar( outbuf, c, pos, outsize );
			}
			else // possibly % format specifier
			{
				flags |= secondPass;
#ifdef WRENCH_FLOAT_SPRINTF
				specStart = fmt - 1;
#endif
			}
		}
		else if ( (flags & inPrecision) && c >= '0' && c <= '9' )
		{
			// precision digits do not count toward the width; only a
			// float conversion uses them (it re-reads the whole specifier)
		}
		else if ( c >= '0' && c <= '9' ) // width
		{
			columns *= 10;
			columns += c - '0';
			if ( !columns ) // leading zero
			{
				flags |= zeroPad;
				padChar = '0';
			}
			else if ( columns > c_maxColumns )
			{
				columns = c_maxColumns;
			}
		}
		else if ( c == '#' )
		{
			flags |= altRep;
		}
		else if ( c == '-' ) // left-justify
		{
			flags |= negativeJustify;
		}
		else if ( c == 'l' || c == 'h' || c == 'L' || c == 'z' || c == 'j' || c == 't' )
		{
			// length modifiers are accepted and ignored, every integer is 32 bits
		}
		else if ( c == '.' ) // precision follows
		{
			flags |= inPrecision;
		}
		else if ( c == 'c' ) // character
		{
			if ( listPtr < argn )
			{
				addChar( outbuf, (char)(args[listPtr++].asInt()), pos, outsize );
			}
			goto resetState;
		}
		else if ( c == '%' ) // literal %
		{
			addChar( outbuf, c, pos, outsize );
			goto resetState;
		}
		else // string or integer
		{
			// large enough for 32 binary digits, or any single value
			// formatted as text
			const int c_bufSize = 34;
			char buf[c_bufSize];

			const char *ptr; // pointer to first char of integer
			unsigned int len;

			if ( c == 's' ) // string
			{
				buf[0] = 0;
				ptr = buf;
				len = 0;
				if ( listPtr < argn )
				{
					ptr = (char *)args[listPtr].array(&len);
					if ( !ptr )
					{
						// not a string, format the value as text instead
						unsigned int valueLen = 0;
						args[listPtr].asString( buf, c_bufSize, &valueLen );
						ptr = buf;
						len = valueLen;
					}
					++listPtr;
				}

				padChar = ' '; // in case some joker uses a 0 in their column spec

copyToString:
				{
					const bool hexPrefix = (c == 'p') || ((flags & altRep) && (c == 'x' || c == 'X'));

					// the sign and prefix count toward the field width
					unsigned int total = len;
					if ( flags & negativeSign )
					{
						++total;
					}
					if ( hexPrefix )
					{
						total += 2;
					}

					// Right-justify: spaces go before the sign
					if ( !(flags & negativeJustify) && padChar != '0' )
					{
						for ( ; columns > total; columns-- )
						{
							addChar( outbuf, ' ', pos, outsize );
						}
					}

					if ( flags & negativeSign )
					{
						addChar( outbuf, '-', pos, outsize );
					}

					if ( hexPrefix )
					{
						addChar( outbuf, '0', pos, outsize );
						addChar( outbuf, 'x', pos, outsize );
					}

					// zeros go after the sign
					if ( !(flags & negativeJustify) )
					{
						for ( ; columns > total; columns-- )
						{
							addChar( outbuf, padChar, pos, outsize );
						}
					}

					for (unsigned int l = 0; l < len; ++l )
					{
						addChar( outbuf, *ptr++, pos, outsize );
					}

					// Left-justify
					for ( ; columns > total; columns-- )
					{
						addChar( outbuf, ' ', pos, outsize );
					}
				}

				goto resetState;
			}
			else
			{
				unsigned char base;
				unsigned char width;
				unsigned int val;

				if ( c == 'd' || c == 'i' )
				{
					flags |= parsingSigned;
					goto parseDecimal;
				}
#ifdef WRENCH_FLOAT_SPRINTF
				else if ( c == 'f' || c == 'g' || c == 'e' || c == 'F' || c == 'G' || c == 'E' )
				{
					// rebuild the specifier from the '%' through this
					// character, leaving out length modifiers
					char floatSpec[32];
					char floatOut[64];
					unsigned int specLen = 0;
					bool specFits = true;
					for( const char* s = specStart; s < fmt; ++s )
					{
						if ( *s == 'l' || *s == 'h' || *s == 'L' || *s == 'z' || *s == 'j' || *s == 't' )
						{
							continue;
						}
						if ( specLen >= sizeof(floatSpec) - 1 )
						{
							specFits = false;
							break;
						}
						floatSpec[specLen++] = *s;
					}
					floatSpec[specLen] = 0;

					const float floatValue = (listPtr < argn) ? args[listPtr].asFloat() : 0.f;
					if ( listPtr < argn )
					{
						++listPtr;
					}

					if ( specFits )
					{
						// suck in whatever lib we need for this
						int chars = snprintf( floatOut, sizeof(floatOut), floatSpec, floatValue );

						// snprintf reports the length it wanted, not what fit
						if ( chars < 0 )
						{
							chars = 0;
						}
						else if ( chars > (int)sizeof(floatOut) - 1 )
						{
							chars = (int)sizeof(floatOut) - 1;
						}

						for( int j=0; j<chars; ++j )
						{
							addChar( outbuf, floatOut[j], pos, outsize );
						}
					}
					goto resetState;
				}
#else
				else if ( c == 'f' || c == 'g' || c == 'e' || c == 'F' || c == 'G' || c == 'E' )
				{
					// without the C library float formatter the value is
					// printed the way wrench prints any float, but it is
					// always consumed so the specifiers after it still line
					// up with their arguments
					unsigned int valueLen = 0;
					if ( listPtr < argn )
					{
						valueLen = (unsigned int)wr_ftoa( args[listPtr++].asFloat(), buf, c_bufSize - 1 );
					}
					buf[valueLen] = 0;
					ptr = buf;
					len = valueLen;

					// the sign goes before any zero padding
					if ( len && *ptr == '-' )
					{
						flags |= negativeSign;
						++ptr;
						--len;
					}

					goto copyToString;
				}
#endif
				else if ( c == 'u' ) // decimal
				{
parseDecimal:
					base  = 10;
					width = 10;
					goto convertBase;
				}
				else if ( c == 'b' ) // binary
				{
					base  = 2;
					width = 32;
					goto convertBase;
				}
				else if ( c == 'o' ) // octal
				{
					base  = 8;
					width = 11;
					goto convertBase;
				}
				else if ( c == 'x' || c == 'X' || c == 'p' ) // hexadecimal or pointer (pointer is treated as 'X')
				{
					base = 16;
					width = 8;
convertBase:
					if ( listPtr < argn )
					{
						val = args[listPtr++].asInt();
					}
					else
					{
						val = 0;
					}

					if ( (flags & parsingSigned) && (val & 0x80000000) )
					{
						flags |= negativeSign;
						val = 0u - val; // magnitude, also correct for INT_MIN
					}

					// Convert to given base, filling buffer backwards from least to most significant
					char* p = buf + width;
					*p = 0;
					ptr = p; // keep track of one past left-most non-zero digit
					do
					{
						char d = val % base;
						val /= base;

						if ( d )
						{
							ptr = p;
						}

						d += '0';
						if ( d > '9' ) // handle bases higher than 10
						{
							d += 'A' - ('9' + 1);
							if ( c == 'x' ) // lowercase
							{
								d += 'a' - 'A';
							}
						}

						*--p = d;

					} while ( p != buf );

					--ptr; // was one past char we want
					len = (buf + width) - ptr;
					goto copyToString;
				}
				else // invalid format specifier
				{
					goto resetState;
				}
			}
		}
	}

sprintf_end:

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
	
	if( !data || len <= 0 )
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
