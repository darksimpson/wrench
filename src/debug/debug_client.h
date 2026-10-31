#ifndef _DEBUG_CLIENT_H
#define _DEBUG_CLIENT_H
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

// Host side of the wrench debugger: talks to a WrenchDebugServer over any
// WrenchDebugTransport and understands the program's debug symbols. Uses the
// C++ standard library, so it is meant for PCs, not for targets.

#ifdef WRENCH_INCLUDE_DEBUG_CLIENT

#include "wrench.h"

#include <string>
#include <vector>
#include <deque>
#include <map>
#include <set>
#include <mutex>
#include <condition_variable>
#include <functional>

// symbols, source and line table of one compiled program
class WrenchDebugProgram
{
public:
	struct Function
	{
		std::string name; // "::global" for unit 0
		int arguments = 0;
		std::vector<std::string> locals; // arguments first
		uint32_t codeStart = 0;
		uint32_t codeEnd = 0;
	};

	struct LineEntry
	{
		uint32_t pcOffset;
		int line;
	};

	// a program can be compiled from several files (include "file"): the
	// target and the line table count lines through all of them ("virtual"
	// lines); the file table maps them back. A program without one is a
	// single file, file 0, whose lines are the virtual lines
	struct SourceFile
	{
		std::string name; // as the compiler resolved it, empty if unknown
		std::string text; // when embedded (-debugsource)
		bool hasText = false;
	};

	bool parse( const uint8_t* bytecode, uint32_t len, std::string* error = 0 );
	void clear();

	bool valid() const { return !m_bytecode.empty(); }
	const std::vector<uint8_t>& bytecode() const { return m_bytecode; }
	uint32_t hash() const { return m_hash; }
	uint8_t compilerFlags() const { return m_flags; }
	bool hasDebugCode() const { return (m_flags & WR_EMBED_DEBUG_CODE) != 0; }
	uint32_t sourceHash() const { return m_sourceHash; }
	const std::string& source() const { return m_source; }
	bool hasSource() const { return m_hasSource; }

	// functions[0] is global code, functions[n] is VM function n - 1
	const std::vector<Function>& functions() const { return m_functions; }
	std::string functionName( int vmFunction ) const;
	std::string globalName( int slot ) const;
	std::string localName( int vmFunction, int slot ) const;
	// file >= 0: a private global of that file wins over a shared one
	int globalSlot( const std::string& name, int file = -1 ) const;
	int localSlot( int vmFunction, const std::string& name ) const;
	int functionForPc( uint32_t pcOffset ) const; // -1 is global code

	// line of the last line marker at or before pcOffset within its function
	int lineForPc( uint32_t pcOffset ) const;

	// lines that carry a line marker, breakpoints only bind to these
	const std::set<int>& lines() const { return m_lines; }
	// nearest breakable line at or after 'line', -1 if none
	int breakableLine( int line ) const;

	// struct member name from its hash, empty if unknown
	std::string memberName( uint32_t hash ) const;

	// the files the program was compiled from, the main one first
	const std::vector<SourceFile>& files() const { return m_files; }
	// which file and line of it a virtual line is
	void locate( int virtualLine, int& file, int& line ) const;
	// the virtual line of a file's line, -1 if that line is not part of the
	// program (a file included twice is there once)
	int virtualLine( int file, int line ) const;
	// nearest breakable line of 'file' at or after its 'line', as a virtual
	// line, -1 if the file has none there
	int breakableLine( int file, int line ) const;

	static std::string displayName( const std::string& label );

private:
	void buildLineTable();

	std::vector<uint8_t> m_bytecode;
	uint32_t m_hash = 0;
	uint8_t m_flags = 0;
	uint32_t m_sourceHash = 0;
	bool m_hasSource = false;
	std::string m_source;
	uint32_t m_codeStart = 0;
	uint32_t m_codeEnd = 0;
	std::vector<Function> m_functions;
	std::vector<LineEntry> m_lineTable;
	std::set<int> m_lines;
	std::map<uint32_t, std::string> m_members;

	struct Segment
	{
		int virtualLine; // from this virtual line on
		int file; // lines come from this file
		int line; // starting at this line of it
	};
	std::vector<SourceFile> m_files;
	std::vector<Segment> m_segments;
};

// a decoded value as reported by the target
struct WrenchDebugValue
{
	uint8_t tag = WRD_ValInt;
	int32_t i = 0;
	float f = 0;
	uint32_t handle = 0;
	uint32_t count = 0; // elements, or total string length
	std::string text; // string preview
	uint8_t other = 0;

	bool expandable() const;
	bool isString() const { return tag == WRD_ValString; }
	std::string display( bool hex = false ) const;
	std::string typeName() const;

	static WrenchDebugValue makeInt( int32_t v );
	static WrenchDebugValue makeFloat( float v );
	static WrenchDebugValue makeString( const std::string& v );
};

struct WrenchDebugChild
{
	std::string name; // index, member or key text
	WrenchDebugValue key; // hash tables only
	WrenchDebugValue value;
	uint32_t index = 0; // position used by setChild()
};

struct WrenchDebugStackFrame
{
	uint8_t kind = WRD_FrameGlobal;
	int function = -1; // VM function index, -1 for global code
	uint32_t pcOffset = 0;
	int line = 0;
	int slots = 0; // arguments + locals
	int program = 0; // whose code it runs (a call into an imported module)
	std::string name;
};

// what the target says about one of its programs (GetPrograms)
struct WrenchDebugProgramInfo
{
	int id = 0;
	uint8_t flags = 0; // WRD_Program...
	int importer = -1; // the program that imported it, -1 for none
	uint32_t hash = 0;
	uint32_t bytecodeLen = 0;
	uint8_t compilerFlags = 0;
	uint32_t symbolsLen = 0;
	uint32_t sourceLen = 0;
	int globals = 0;
	int functions = 0;
	std::string name; // given by the firmware, may be empty
};

// the symbols of a target's programs, by program number
class WrenchDebugPrograms
{
public:
	// null when the program is not known
	const WrenchDebugProgram* get( int program ) const;
	const WrenchDebugProgramInfo* info( int program ) const;

	// a pinned program keeps these symbols whatever the target says (the
	// local file a debugger was started with, even when the target runs
	// another build of it)
	void set( int program, const WrenchDebugProgram& symbols, bool pinned = false );
	void clear();
	std::vector<int> ids() const;

	// the target's list of programs: ones gone are dropped, new or changed
	// ones get their symbols read from the target. A program already known
	// with the same hash keeps its symbols (a local compile of program 0)
	bool sync( class WrenchDebugClient& client, std::string* error = nullptr );

	// a name for people: the firmware's, else the file the program was
	// compiled from, else "program n" / "import n"
	std::string displayName( int program ) const;

private:
	std::map<int, WrenchDebugProgram> m_symbols;
	std::map<int, WrenchDebugProgramInfo> m_info;
	std::vector<int> m_pinned;
};

struct WrenchDebugEvent
{
	enum Type
	{
		Stopped,
		Output,
		Ended,
		Loaded,
		Disconnected,
		WatchRemoved,
		ProgramAdded,
		ProgramRemoved,
	};

	Type type = Stopped;
	uint8_t reason = 0; // WrenchDebugStopReason
	int line = 0;
	uint8_t error = 0; // WRError
	uint8_t flags = 0; // WRD_Ended...
	uint32_t missingFunction = 0; // name hash for *_function_not_found errors
	int watch = 0; // WRD_StopWatch and WatchRemoved: which watch
	int program = 0; // Stopped: where; ProgramAdded / ProgramRemoved: which
	uint32_t programHash = 0; // Loaded: the bytecode hash of the new program
	std::string text;
	WrenchDebugValue value;
};

// where a watched value lives (see WrenchDebugWatchRoot): a global slot, or
// a local slot of a stack frame, then elements and members below it
struct WrenchDebugWatchStep
{
	uint8_t kind = WRD_WatchIndex; // WrenchDebugWatchStepKind
	uint32_t value = 0; // element position, or key / member name hash
};

struct WrenchDebugWatchPath
{
	bool global = true;
	int program = 0; // globals: whose
	int frame = 0; // locals: frame index of the current stop
	int slot = 0;
	std::vector<WrenchDebugWatchStep> steps;
};

struct WrenchDebugTargetInfo
{
	uint16_t protocol = 0;
	std::string wrenchVersion;
	uint16_t maxPayload = 0;
	uint8_t caps = 0;
	uint16_t maxBreakpoints = 0;
	uint8_t maxWatches = 0; // 0: the target has no watchpoints
	uint8_t maxPrograms = 1;

	uint8_t state = 0;
	uint32_t programHash = 0;
	uint32_t bytecodeLen = 0;
	uint8_t compilerFlags = 0;
	uint32_t symbolsLen = 0;
	uint32_t sourceLen = 0;
	uint16_t globals = 0;
	uint8_t functions = 0;
	int lastLine = -1;
};

class WrenchDebugClient
{
public:
	explicit WrenchDebugClient( WrenchDebugTransport* transport );

	// handshake and target information
	bool connect( WrenchDebugTargetInfo& info );
	bool refreshInfo( WrenchDebugTargetInfo& info );

	// send a program to the target, progress gets (sent, total)
	bool upload( const std::vector<uint8_t>& bytecode, std::function<void(uint32_t, uint32_t)> progress = nullptr );

	// fetch a program of the target (to attach without local files)
	bool readSection( uint8_t section, std::vector<uint8_t>& out, int program = 0 );

	// the programs the target debugs: program 0 (loaded or uploaded),
	// attached contexts, modules they imported
	bool getPrograms( std::vector<WrenchDebugProgramInfo>& programs );

	bool launch( bool stopOnEntry );
	// resumeStep: continue the step a breakpoint interrupted, if any (the
	// client evaluated the breakpoint and decided not to stop)
	bool resume( bool resumeStep = false );
	bool pause();
	bool stepIn();
	bool stepOver();
	bool stepOut();
	bool terminate();
	bool detach();
	bool ping();

	// replaces one program's breakpoints (virtual lines)
	bool setBreakpoints( const std::vector<int>& lines, int program = 0 );

	// replaces the breakpoints parked for a program that is not on the target
	// now (by its hash): they are set when it comes, before any of its code
	// runs (needs WRD_CapParkedBreakpoints)
	bool setParkedBreakpoints( const std::vector<int>& lines, uint32_t programHash );

	// watchpoints: the target stops (WRD_StopWatch) on the line after one
	// that changed the value. A local can only be named while stopped, and
	// goes away (a WatchRemoved event) when its call returns
	bool addWatch( const WrenchDebugWatchPath& path, int& id );
	bool removeWatch( int id ); // 0 removes all

	// stack, innermost frame first; names and lines filled from 'program'
	bool getStack( std::vector<WrenchDebugStackFrame>& frames, const WrenchDebugProgram* program, int topLine );
	// the same, each frame named from the symbols of its own program
	bool getStack( std::vector<WrenchDebugStackFrame>& frames, const WrenchDebugPrograms& programs, int topLine );

	bool getLocals( int frame, std::vector<WrenchDebugValue>& values );
	bool getGlobals( std::vector<WrenchDebugValue>& values, int program = 0 );
	bool getChildren( const WrenchDebugValue& parent, uint32_t first, uint32_t count,
										std::vector<WrenchDebugChild>& children, uint32_t& total,
										const WrenchDebugProgram* program );
	bool getString( const WrenchDebugValue& value, std::string& out, uint32_t maxLen = 65536 );

	bool setLocal( int frame, int slot, const WrenchDebugValue& value, WrenchDebugValue& result );
	bool setGlobal( int slot, const WrenchDebugValue& value, WrenchDebugValue& result, int program = 0 );
	bool setChild( const WrenchDebugValue& parent, uint32_t index, const WrenchDebugValue& value, WrenchDebugValue& result );

	// next event, waiting up to timeoutMs (0 polls). Returns false on timeout
	bool waitEvent( WrenchDebugEvent& event, int timeoutMs );

	const std::string& lastError() const { return m_error; }
	bool linkUp() const { return m_linkUp; }

	// per request timeout and retry policy, slow UART links want more
	void setTimeout( int ms, int retries ) { m_timeoutMs = ms; m_retries = retries; }

	static const char* errorName( uint8_t code );

private:
	bool request( uint8_t type, const std::vector<uint8_t>& payload, std::vector<uint8_t>* reply );
	bool control( uint8_t type );
	void poll( int timeoutMs, bool replyOnly = false );
	void handleFrame( const WrenchDebugFrame& frame );
	static bool readValue( WrenchDebugReader& in, WrenchDebugValue& v );
	static void writeValue( std::vector<uint8_t>& out, const WrenchDebugValue& v );

	WrenchDebugTransport* m_transport;
	std::vector<uint8_t> m_rx;
	WrenchDebugDecoder m_decoder;
	uint8_t m_seq = 0;
	uint16_t m_maxPayload = 256;
	int m_timeoutMs = 3000;
	int m_retries = 2;
	bool m_linkUp = true;
	std::string m_error;

	// reply for the request in flight
	uint8_t m_waitSeq = 0;
	bool m_haveReply = false;
	uint8_t m_replyType = 0;
	std::vector<uint8_t> m_reply;

	std::deque<WrenchDebugEvent> m_events;
};

// two connected in-process endpoints, so a client and a server can run in
// different threads of one program (local debugging, tests)
class WrenchDebugPipe
{
public:
	class End : public WrenchDebugTransport
	{
	public:
		bool write( const uint8_t* data, uint32_t len ) override;
		int read( uint8_t* data, uint32_t maxLen ) override;
		uint32_t millis() override;
		void idle() override;
		bool connected() override;

		WrenchDebugPipe* pipe = nullptr;
		int side = 0;
	};

	WrenchDebugPipe();

	WrenchDebugTransport* client() { return &m_ends[0]; }
	WrenchDebugTransport* server() { return &m_ends[1]; }

	// optional fault injection for tests: every Nth byte toward the client
	// is corrupted
	void corruptEvery( int n ) { m_corruptEvery = n; }

	void close();

private:
	friend class End;
	std::mutex m_lock;
	std::condition_variable m_signal;
	std::deque<uint8_t> m_queues[2]; // [0] toward client, [1] toward server
	End m_ends[2];
	bool m_closed = false;
	int m_corruptEvery = 0;
	uint32_t m_byteCount = 0;
};

#endif

#endif
