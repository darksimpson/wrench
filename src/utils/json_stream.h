#ifndef _WRENCH_JSON_STREAM_H
#define _WRENCH_JSON_STREAM_H
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

// Content-Length framed JSON messages on stdin/stdout, as used by the
// Debug Adapter Protocol and the Language Server Protocol. Host only.

#include "utils/json.h"

#include <deque>
#include <mutex>
#include <condition_variable>
#include <thread>

class JsonStdioStream
{
public:
	// takes over stdout: the protocol keeps the real handle and anything else
	// printing to stdout (script libraries, the compiler) goes to stderr
	JsonStdioStream();

	void startReader();

	// next incoming message, waits up to timeoutMs. A body that is not valid
	// JSON arrives as an object with only a "wrenchParseError" string
	bool next( Json& message, int timeoutMs );

	// true once stdin is closed and everything was consumed
	bool closed();

	void send( const Json& message );

	// log traffic to stderr
	void setTrace( bool on ) { m_trace = on; }

private:
	void readLoop();
	void finish();
	void queueParseError( const std::string& text );

	int m_out = 1;
	std::thread m_reader;
	std::mutex m_lock;
	std::mutex m_writeLock;
	std::condition_variable m_signal;
	std::deque<Json> m_queue;
	bool m_closed = false;
	bool m_trace = false;
};

#endif
