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

#include "utils/json_stream.h"

#include <stdio.h>
#include <stdlib.h>
#include <new>

#if defined(_WIN32)
#include <io.h>
#include <fcntl.h>
#else
#include <unistd.h>
#endif

JsonStdioStream::JsonStdioStream()
{
#if defined(_WIN32)
	_setmode( _fileno(stdin), _O_BINARY );
	_setmode( _fileno(stdout), _O_BINARY );
	m_out = _dup( _fileno(stdout) );
	_dup2( _fileno(stderr), _fileno(stdout) );
#else
	m_out = dup( fileno(stdout) );
	dup2( fileno(stderr), fileno(stdout) );
#endif
}

void JsonStdioStream::startReader()
{
	m_reader = std::thread( [this]() { readLoop(); } );
	m_reader.detach();
}

bool JsonStdioStream::next( Json& message, int timeoutMs )
{
	std::unique_lock<std::mutex> lock( m_lock );
	if ( m_queue.empty() && !m_closed )
	{
		m_signal.wait_for( lock, std::chrono::milliseconds(timeoutMs) );
	}
	if ( m_queue.empty() )
	{
		return false;
	}
	message = m_queue.front();
	m_queue.pop_front();
	return true;
}

bool JsonStdioStream::closed()
{
	std::lock_guard<std::mutex> guard( m_lock );
	return m_closed && m_queue.empty();
}

void JsonStdioStream::send( const Json& message )
{
	std::string body = message.dump();
	std::string frame = "Content-Length: " + std::to_string( body.size() ) + "\r\n\r\n" + body;
	if ( m_trace )
	{
		fprintf( stderr, "<- %s\n", body.c_str() );
	}

	std::lock_guard<std::mutex> guard( m_writeLock );
	const char* p = frame.data();
	size_t left = frame.size();
	while ( left )
	{
#if defined(_WIN32)
		int n = _write( m_out, p, (unsigned int)left );
#else
		ssize_t n = write( m_out, p, left );
#endif
		if ( n <= 0 )
		{
			break;
		}
		p += n;
		left -= (size_t)n;
	}
}

// no editor sends anything near this; a bigger length is a broken or
// hostile peer and must not make us allocate it
static const size_t c_maxBody = 64u * 1024u * 1024u;
static const size_t c_maxHeaderLine = 8192;

void JsonStdioStream::queueParseError( const std::string& text )
{
	// the dispatcher decides how to answer (LSP: error -32700 with a null id)
	Json message = Json::object();
	message.set( "wrenchParseError", text );
	std::lock_guard<std::mutex> guard( m_lock );
	m_queue.push_back( message );
	m_signal.notify_all();
}

void JsonStdioStream::readLoop()
{
	for ( ;; )
	{
		size_t length = 0;
		bool haveLength = false;
		std::string line;

		for ( ;; )
		{
			int c = fgetc( stdin );
			if ( c == EOF )
			{
				finish();
				return;
			}
			if ( c != '\n' )
			{
				if ( line.size() >= c_maxHeaderLine )
				{
					// not a header at all, the stream cannot be trusted
					finish();
					return;
				}
				line += (char)c;
				continue;
			}
			if ( !line.empty() && line.back() == '\r' )
			{
				line.pop_back();
			}
			if ( line.empty() )
			{
				if ( haveLength )
				{
					break;
				}
				continue;
			}
			if ( line.compare(0, 15, "Content-Length:") == 0 )
			{
				length = (size_t)strtoul( line.c_str() + 15, 0, 10 );
				haveLength = true;
			}
			line.clear();
		}

		if ( length > c_maxBody )
		{
			// skip it in pieces, then report it
			char sink[4096];
			size_t left = length;
			while ( left )
			{
				size_t n = fread( sink, 1, left < sizeof(sink) ? left : sizeof(sink), stdin );
				if ( n == 0 )
				{
					finish();
					return;
				}
				left -= n;
			}
			queueParseError( "message of " + std::to_string(length) + " bytes is too large" );
			continue;
		}

		std::string body;
		try
		{
			body.assign( length, '\0' );
		}
		catch ( const std::bad_alloc& )
		{
			finish();
			return;
		}

		size_t got = 0;
		while ( got < length )
		{
			size_t n = fread( &body[got], 1, length - got, stdin );
			if ( n == 0 )
			{
				finish();
				return;
			}
			got += n;
		}

		if ( m_trace )
		{
			fprintf( stderr, "-> %s\n", body.c_str() );
		}

		Json message;
		std::string error;
		if ( Json::parse(body, message, &error) )
		{
			std::lock_guard<std::mutex> guard( m_lock );
			m_queue.push_back( message );
			m_signal.notify_all();
		}
		else
		{
			queueParseError( error.empty() ? std::string("invalid JSON") : error );
		}
	}
}

void JsonStdioStream::finish()
{
	std::lock_guard<std::mutex> guard( m_lock );
	m_closed = true;
	m_signal.notify_all();
}

#endif
