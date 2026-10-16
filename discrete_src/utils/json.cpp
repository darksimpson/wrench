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

#ifdef WRENCH_INCLUDE_DEBUG_CLIENT

#include "utils/json.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

static const Json s_null;
static const std::string s_empty;

const std::string& Json::asString() const
{
	return m_type == String ? m_string : s_empty;
}

const Json& Json::operator[]( const char* key ) const
{
	if ( m_type == Object )
	{
		for ( const auto& m : m_object )
		{
			if ( m.first == key )
			{
				return m.second;
			}
		}
	}
	return s_null;
}

bool Json::has( const char* key ) const
{
	if ( m_type == Object )
	{
		for ( const auto& m : m_object )
		{
			if ( m.first == key )
			{
				return true;
			}
		}
	}
	return false;
}

Json& Json::set( const std::string& key, const Json& value )
{
	if ( m_type != Object )
	{
		*this = object();
	}
	for ( auto& m : m_object )
	{
		if ( m.first == key )
		{
			m.second = value;
			return *this;
		}
	}
	m_object.push_back( std::make_pair(key, value) );
	return *this;
}

const Json& Json::operator[]( size_t index ) const
{
	if ( m_type == Array && index < m_array.size() )
	{
		return m_array[index];
	}
	return s_null;
}

Json& Json::push( const Json& value )
{
	if ( m_type != Array )
	{
		*this = array();
	}
	m_array.push_back( value );
	return *this;
}

static void wr_jsonEscape( const std::string& s, std::string& out )
{
	out += '"';
	for ( unsigned char c : s )
	{
		switch ( c )
		{
			case '"': out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\n': out += "\\n"; break;
			case '\r': out += "\\r"; break;
			case '\t': out += "\\t"; break;
			case '\b': out += "\\b"; break;
			case '\f': out += "\\f"; break;
			default:
			{
				if ( c < 0x20 )
				{
					char buf[8];
					snprintf( buf, sizeof(buf), "\\u%04x", c );
					out += buf;
				}
				else
				{
					out += (char)c;
				}
			}
		}
	}
	out += '"';
}

void Json::dumpTo( std::string& out ) const
{
	switch ( m_type )
	{
		case Null: out += "null"; break;
		case Bool: out += m_bool ? "true" : "false"; break;
		case Number:
		{
			char buf[32];
			if ( !isfinite(m_number) )
			{
				out += "0";
			}
			else if ( m_number == floor(m_number) && fabs(m_number) < 9e15 )
			{
				snprintf( buf, sizeof(buf), "%lld", (long long)m_number );
				out += buf;
			}
			else
			{
				snprintf( buf, sizeof(buf), "%.17g", m_number );
				out += buf;
			}
			break;
		}
		case String: wr_jsonEscape( m_string, out ); break;
		case Array:
		{
			out += '[';
			for ( size_t i = 0; i < m_array.size(); ++i )
			{
				if ( i )
				{
					out += ',';
				}
				m_array[i].dumpTo( out );
			}
			out += ']';
			break;
		}
		case Object:
		{
			out += '{';
			for ( size_t i = 0; i < m_object.size(); ++i )
			{
				if ( i )
				{
					out += ',';
				}
				wr_jsonEscape( m_object[i].first, out );
				out += ':';
				m_object[i].second.dumpTo( out );
			}
			out += '}';
			break;
		}
	}
}

std::string Json::dump() const
{
	std::string out;
	dumpTo( out );
	return out;
}

namespace
{

struct JsonParser
{
	const std::string& s;
	size_t p = 0;
	std::string error;
	int depth = 0;

	explicit JsonParser( const std::string& text ) : s( text ) {}

	void skip()
	{
		while ( p < s.size() && (s[p] == ' ' || s[p] == '\t' || s[p] == '\r' || s[p] == '\n') )
		{
			++p;
		}
	}

	bool fail( const char* what )
	{
		if ( error.empty() )
		{
			error = std::string( what ) + " at offset " + std::to_string( p );
		}
		return false;
	}

	static void appendUtf8( std::string& out, uint32_t cp )
	{
		if ( cp < 0x80 )
		{
			out += (char)cp;
		}
		else if ( cp < 0x800 )
		{
			out += (char)(0xC0 | (cp >> 6));
			out += (char)(0x80 | (cp & 0x3F));
		}
		else if ( cp < 0x10000 )
		{
			out += (char)(0xE0 | (cp >> 12));
			out += (char)(0x80 | ((cp >> 6) & 0x3F));
			out += (char)(0x80 | (cp & 0x3F));
		}
		else
		{
			out += (char)(0xF0 | (cp >> 18));
			out += (char)(0x80 | ((cp >> 12) & 0x3F));
			out += (char)(0x80 | ((cp >> 6) & 0x3F));
			out += (char)(0x80 | (cp & 0x3F));
		}
	}

	bool hex4( uint32_t& v )
	{
		if ( p + 4 > s.size() )
		{
			return fail( "short unicode escape" );
		}
		v = 0;
		for ( int i = 0; i < 4; ++i )
		{
			char c = s[p++];
			v <<= 4;
			if ( c >= '0' && c <= '9' )
			{
				v |= (uint32_t)(c - '0');
			}
			else if ( c >= 'a' && c <= 'f' )
			{
				v |= (uint32_t)(c - 'a' + 10);
			}
			else if ( c >= 'A' && c <= 'F' )
			{
				v |= (uint32_t)(c - 'A' + 10);
			}
			else
			{
				return fail( "bad unicode escape" );
			}
		}
		return true;
	}

	bool parseString( std::string& out )
	{
		++p; // opening quote
		while ( p < s.size() )
		{
			char c = s[p++];
			if ( c == '"' )
			{
				return true;
			}
			if ( c != '\\' )
			{
				out += c;
				continue;
			}
			if ( p >= s.size() )
			{
				break;
			}
			char e = s[p++];
			switch ( e )
			{
				case '"': out += '"'; break;
				case '\\': out += '\\'; break;
				case '/': out += '/'; break;
				case 'b': out += '\b'; break;
				case 'f': out += '\f'; break;
				case 'n': out += '\n'; break;
				case 'r': out += '\r'; break;
				case 't': out += '\t'; break;
				case 'u':
				{
					uint32_t cp = 0;
					if ( !hex4(cp) )
					{
						return false;
					}
					if ( cp >= 0xD800 && cp <= 0xDBFF )
					{
						// a high surrogate must be followed by a low one,
						// anything else becomes the replacement character
						uint32_t lo = 0;
						if ( p + 6 <= s.size() && s[p] == '\\' && s[p + 1] == 'u' )
						{
							size_t save = p;
							p += 2;
							if ( !hex4(lo) )
							{
								return false;
							}
							if ( lo < 0xDC00 || lo > 0xDFFF )
							{
								p = save; // not a pair, the next escape stands on its own
								lo = 0;
							}
						}
						cp = lo ? 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00) : 0xFFFD;
					}
					else if ( cp >= 0xDC00 && cp <= 0xDFFF )
					{
						cp = 0xFFFD; // a lone low surrogate
					}
					appendUtf8( out, cp );
					break;
				}
				default: return fail( "bad escape" );
			}
		}
		return fail( "unterminated string" );
	}

	bool parseValue( Json& out )
	{
		if ( ++depth > 200 )
		{
			return fail( "nesting too deep" );
		}
		skip();
		if ( p >= s.size() )
		{
			return fail( "unexpected end" );
		}

		char c = s[p];
		bool ok = true;
		if ( c == '{' )
		{
			++p;
			out = Json::object();
			skip();
			if ( p < s.size() && s[p] == '}' )
			{
				++p;
			}
			else
			{
				for ( ;; )
				{
					skip();
					if ( p >= s.size() || s[p] != '"' )
					{
						ok = fail( "expected key" );
						break;
					}
					std::string key;
					if ( !parseString(key) )
					{
						ok = false;
						break;
					}
					skip();
					if ( p >= s.size() || s[p] != ':' )
					{
						ok = fail( "expected ':'" );
						break;
					}
					++p;
					Json v;
					if ( !parseValue(v) )
					{
						ok = false;
						break;
					}
					out.set( key, v );
					skip();
					if ( p < s.size() && s[p] == ',' )
					{
						++p;
						continue;
					}
					if ( p < s.size() && s[p] == '}' )
					{
						++p;
						break;
					}
					ok = fail( "expected ',' or '}'" );
					break;
				}
			}
		}
		else if ( c == '[' )
		{
			++p;
			out = Json::array();
			skip();
			if ( p < s.size() && s[p] == ']' )
			{
				++p;
			}
			else
			{
				for ( ;; )
				{
					Json v;
					if ( !parseValue(v) )
					{
						ok = false;
						break;
					}
					out.push( v );
					skip();
					if ( p < s.size() && s[p] == ',' )
					{
						++p;
						continue;
					}
					if ( p < s.size() && s[p] == ']' )
					{
						++p;
						break;
					}
					ok = fail( "expected ',' or ']'" );
					break;
				}
			}
		}
		else if ( c == '"' )
		{
			std::string str;
			ok = parseString( str );
			out = Json( str );
		}
		else if ( s.compare(p, 4, "true") == 0 )
		{
			p += 4;
			out = Json( true );
		}
		else if ( s.compare(p, 5, "false") == 0 )
		{
			p += 5;
			out = Json( false );
		}
		else if ( s.compare(p, 4, "null") == 0 )
		{
			p += 4;
			out = Json();
		}
		else if ( c == '-' || (c >= '0' && c <= '9') )
		{
			const char* start = s.c_str() + p;
			char* end = 0;
			double v = strtod( start, &end );
			if ( end == start )
			{
				ok = fail( "bad number" );
			}
			else
			{
				p += (size_t)(end - start);
				out = Json( v );
			}
		}
		else
		{
			ok = fail( "unexpected character" );
		}

		--depth;
		return ok;
	}
};

}

bool Json::parse( const std::string& text, Json& out, std::string* error )
{
	JsonParser parser( text );
	if ( !parser.parseValue(out) )
	{
		if ( error )
		{
			*error = parser.error;
		}
		return false;
	}
	parser.skip();
	if ( parser.p != text.size() )
	{
		if ( error )
		{
			*error = "trailing characters";
		}
		return false;
	}
	return true;
}

#endif
