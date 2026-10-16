#ifndef _WRENCH_JSON_H
#define _WRENCH_JSON_H
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

// Minimal JSON value for the protocol servers (DAP, LSP). Host only.

#include <string>
#include <vector>
#include <utility>
#include <stdint.h>

class Json
{
public:
	enum Type
	{
		Null,
		Bool,
		Number,
		String,
		Array,
		Object,
	};

	Json() : m_type( Null ) {}
	Json( std::nullptr_t ) : m_type( Null ) {}
	Json( bool b ) : m_type( Bool ), m_bool( b ) {}
	Json( int v ) : m_type( Number ), m_number( v ) {}
	Json( int64_t v ) : m_type( Number ), m_number( (double)v ) {}
	Json( uint32_t v ) : m_type( Number ), m_number( v ) {}
	Json( double v ) : m_type( Number ), m_number( v ) {}
	Json( const char* s ) : m_type( String ), m_string( s ? s : "" ) {}
	Json( const std::string& s ) : m_type( String ), m_string( s ) {}

	static Json array() { Json j; j.m_type = Array; return j; }
	static Json object() { Json j; j.m_type = Object; return j; }

	Type type() const { return m_type; }
	bool isNull() const { return m_type == Null; }
	bool isObject() const { return m_type == Object; }
	bool isArray() const { return m_type == Array; }
	bool isString() const { return m_type == String; }
	bool isNumber() const { return m_type == Number; }
	bool isBool() const { return m_type == Bool; }

	// typed reads with defaults when the type does not match
	bool asBool( bool def = false ) const { return m_type == Bool ? m_bool : def; }
	double asNumber( double def = 0 ) const { return m_type == Number ? m_number : def; }
	int asInt( int def = 0 ) const { return m_type == Number ? (int)m_number : def; }
	const std::string& asString() const;
	std::string asString( const std::string& def ) const { return m_type == String ? m_string : def; }

	// object access, a missing key reads as null
	const Json& operator[]( const char* key ) const;
	const Json& operator[]( const std::string& key ) const { return (*this)[key.c_str()]; }
	bool has( const char* key ) const;
	Json& set( const std::string& key, const Json& value );
	const std::vector<std::pair<std::string, Json> >& members() const { return m_object; }

	// array access
	size_t size() const { return m_type == Array ? m_array.size() : (m_type == Object ? m_object.size() : 0); }
	const Json& operator[]( size_t index ) const;
	const Json& operator[]( int index ) const { return (*this)[(size_t)index]; }
	Json& push( const Json& value );
	const std::vector<Json>& items() const { return m_array; }

	std::string dump() const;
	static bool parse( const std::string& text, Json& out, std::string* error = 0 );

private:
	void dumpTo( std::string& out ) const;

	Type m_type;
	bool m_bool = false;
	double m_number = 0;
	std::string m_string;
	std::vector<Json> m_array;
	std::vector<std::pair<std::string, Json> > m_object;
};

#endif
