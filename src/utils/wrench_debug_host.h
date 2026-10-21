#ifndef _WRENCH_DEBUG_HOST_H
#define _WRENCH_DEBUG_HOST_H
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

// Pieces shared by the command line debugger, the debug server command and
// the VS Code debug adapter. Host only.

#ifdef WRENCH_INCLUDE_DEBUG_CLIENT

#include "debug/debug_client.h"
#include "utils/wrench_project.h"

#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <memory>

// compile a source file with everything the debugger needs. Returns false
// with a readable message (including the line) on failure
bool wr_compileForDebug( const std::string& path, std::vector<uint8_t>& bytecode,
												std::string& source, std::string& error, bool embedSource = true );

// the same with explicit compiler flags, to match what a target was built
// with (code offsets differ with and without the embedded source)
bool wr_compileWithFlags( const std::string& path, std::vector<uint8_t>& bytecode,
												std::string& source, std::string& error, uint8_t flags );

// a program to debug from 'path': wrench source is compiled as above,
// compiled bytecode (wrench c -debugsource file.w file.bin) is used as it
// is. 'source' gets the text from the file or from the bytecode, empty when
// the bytecode carries none. 'isBytecode' tells which kind the file was
bool wr_loadForDebug( const std::string& path, std::vector<uint8_t>& bytecode,
											std::string& source, std::string& error, bool* isBytecode = nullptr );

bool wr_readTextFile( const std::string& path, std::string& out );

// name of a WRError, also for user errors (sys::halt(100..255)) that have
// no entry in c_errStrings
std::string wr_debugErrorText( uint8_t error );

// runs a WrenchDebugServer on its own thread over an in-process pipe, so
// local debugging uses exactly the same protocol as a remote target
class WrenchLocalTarget
{
public:
	WrenchLocalTarget();
	~WrenchLocalTarget();

	// project: the host application to imitate (its libraries and natives),
	// null for a plain wrench host
	bool start( int stackSize = 0, const WrenchProject* project = nullptr );
	void stop();

	WrenchDebugTransport* clientTransport() { return m_shared->pipe.client(); }

private:
	// owned jointly with the thread: a script without debug information in
	// an endless loop never returns to check for stop(), the thread is then
	// left behind and must not use freed memory
	struct Shared
	{
		WrenchProject project;
		WrenchDebugPipe pipe;
		std::atomic<bool> quit{ false };
		std::atomic<bool> done{ false };
	};

	static void run( std::shared_ptr<Shared> shared, int stackSize );

	std::shared_ptr<Shared> m_shared;
	std::thread m_thread;
	bool m_started = false;
};

// a parsed transport description:
// local
// tcp:<host>:<port> (client) or tcp:<port> (server side)
// serial:<port>[:<baud>]
struct WrenchTransportSpec
{
	enum Kind
	{
		Local,
		Tcp,
		Serial,
	};

	Kind kind = Local;
	std::string host;
	int port = 0;
	std::string serialPort;
	int baud = 115200;

	bool parse( const std::string& text, std::string& error );
	std::string describe() const;
};

// open the client side of a transport. 'local' starts a WrenchLocalTarget
class WrenchClientLink
{
public:
	~WrenchClientLink();

	bool open( const WrenchTransportSpec& spec, std::string& error, int connectTimeoutMs = 5000, const WrenchProject* project = nullptr );
	void close();

	WrenchDebugTransport* transport() { return m_transport; }
	bool isLocal() const { return m_local != nullptr; }

private:
	std::unique_ptr<WrenchLocalTarget> m_local;
	std::unique_ptr<WrenchDebugTransport> m_owned;
	WrenchDebugTransport* m_transport = nullptr;
};

// "wrench dbgserver": serve a program to remote debuggers over TCP or serial
// project: the host application to imitate, null to answer 0 for any
// native the program calls
// allowUpload: false refuses programs sent by a debugger (setAllowUpload)
// hold: run the preloaded program at once like firmware would, halted on its
// first line until a debugger attaches (breakOnEntry)
// linkTimeoutMs: while halted, let go of a silent debugger after this long
// (setLinkTimeout), 0 waits forever
int wr_debugServerMain( const WrenchTransportSpec& spec, const char* programPath, bool verbose, const WrenchProject* project = nullptr,
	bool allowUpload = true, bool hold = false, uint32_t linkTimeoutMs = 0 );

#endif

#endif
