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
#ifndef _SERIALIZER_H
#define _SERIALIZER_H
/*------------------------------------------------------------------------------*/

struct WRValue;
struct WRContext;
class WRValueSerializer;

// maximum nesting of containers that will be serialized or deserialized,
// deeper (or self-referencing) values are refused instead of recursing
// until the native stack runs out
#ifndef WRENCH_MAX_SERIAL_DEPTH
#define WRENCH_MAX_SERIAL_DEPTH 64
#endif

//------------------------------------------------------------------------------
bool wr_serializeEx( WRValueSerializer& serializer, const WRValue& val, const int depth =0 );
bool wr_deserializeEx( WRValue& value, WRValueSerializer& serializer, WRContext* context, const int depth =0 );

//------------------------------------------------------------------------------
class WRValueSerializer
{
public:

	WRValueSerializer() : m_pos(0), m_size(0), m_reserved(0), m_buf(0), m_failed(false) {}
	WRValueSerializer( const char* data, const unsigned int size ) : m_pos(0), m_size(size), m_reserved(0), m_failed(false)
	{
		m_buf = (char*)g_malloc( size ? size : 1 );
		if ( !m_buf )
		{
			m_size = 0;
			m_failed = true;
#ifdef WRENCH_HANDLE_MALLOC_FAIL
			g_mallocFailed = true;
#endif
		}
		else if ( size > 0 )
		{
			memcpy( m_buf, data, size );
		}
	}
	
	~WRValueSerializer() { g_free(m_buf); }

	void getOwnership( char** buf, int* len )
	{
		*buf = m_buf;
		*len = m_pos;
		m_buf = 0;
	}
	
	int size() const { return m_pos; }
	const char* data() const { return m_buf; }

	// true if a write could not be stored (allocation failure)
	bool failed() const { return m_failed; }

	// reading: bytes left to read that are not already promised to
	// elements of a container whose storage was allocated earlier
	unsigned int unreserved() const
	{
		const unsigned int left = m_size - m_pos;
		return (left > m_reserved) ? (left - m_reserved) : 0;
	}

	// reading: a container of 'count' elements of at least 'minBytes'
	// each is about to be allocated; refuse it unless the input can still
	// hold all of them, then promise those bytes to it. This keeps the
	// total allocation proportional to the input size, however deep the
	// containers are nested
	bool reserve( const unsigned int count, const unsigned int minBytes )
	{
		if ( minBytes && count > unreserved() / minBytes )
		{
			return false;
		}
		m_reserved += count * minBytes;
		return true;
	}

	// reading: an element promised by reserve() starts now, from here on
	// its bytes are counted by the read position instead
	void release( const unsigned int bytes )
	{
		m_reserved = (m_reserved > bytes) ? (m_reserved - bytes) : 0;
	}

	bool read( char* data, const int size )
	{
		if ( size <= 0 )
		{
			return true;
		}

		if ( m_pos + size > m_size )
		{
			return false;
		}

		memcpy( data, m_buf + m_pos, size );
		m_pos += size;
		return true;
	}

	void write( const char* data, const int size )
	{
		if ( size <= 0 || m_failed )
		{
			return;
		}

		if ( m_pos + size > m_size )
		{
			// grow geometrically (at least double, and at least what this
			// write needs) so serializing n bytes costs O(n) copying; only
			// commit the new size once the buffer really exists
			const unsigned int needed = m_pos + (unsigned int)size;
			unsigned int newSize = (m_size < 32) ? 64 : m_size * 2;
			if ( newSize < m_size || newSize < needed ) // overflow, or one big write
			{
				newSize = needed;
			}
			char* newBuf = (needed < m_pos) ? 0 : (char*)g_malloc( newSize );
			if ( !newBuf )
			{
#ifdef WRENCH_HANDLE_MALLOC_FAIL
				g_mallocFailed = true;
#endif
				m_failed = true;
				return;
			}
			if ( m_pos > 0 )
			{
				memcpy( newBuf, m_buf, m_pos );
			}
			g_free( m_buf );
			m_buf = newBuf;
			m_size = newSize;
		}

		memcpy( m_buf + m_pos, data, size );
		m_pos += size;
	}

private:

	unsigned int m_pos;
	unsigned int m_size;
	unsigned int m_reserved;
	char* m_buf;
	bool m_failed;
};

#endif
