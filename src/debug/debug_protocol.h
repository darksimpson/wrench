#ifndef _DEBUG_PROTOCOL_H
#define _DEBUG_PROTOCOL_H
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

/*
	Wrench debug wire protocol, version 2.

	Every message travels in a frame:

		[0] 0xA5 sync
		[1] 0x57 sync ('W')
		[2] message type
		[3] sequence number (requests: 1..255, replies echo it, events use 0)
		[4..5] payload length, little endian
		[6..] payload
		[last 2] CRC16-CCITT (poly 0x1021, init 0xFFFF) over type, seq, length and
						payload, little endian

	The decoder hunts for the sync bytes, so a UART link recovers from noise or
	dropped bytes after at most one lost frame. Frames whose length exceeds the
	receiver limit or whose CRC does not match are discarded.

	All multi-byte payload fields are little endian regardless of host order.
*/

#if defined(WRENCH_INCLUDE_DEBUG_CODE) || defined(WRENCH_INCLUDE_DEBUG_CLIENT)

#define WRD_PROTOCOL_VERSION 2

#define WRD_SYNC0 0xA5
#define WRD_SYNC1 0x57
#define WRD_FRAME_OVERHEAD 8

// largest payload either side will send or accept, the target can lower this
// to save RAM (it costs two buffers of this size), the client adapts to the
// value the target reports in its hello reply
#ifndef WRENCH_DEBUG_MAX_PAYLOAD
#define WRENCH_DEBUG_MAX_PAYLOAD 1024
#endif

// a partially received frame is abandoned if no byte arrives for this long
#define WRD_FRAME_TIMEOUT_MS 500

enum WrenchDebugMessage
{
	// requests, client to server
	WRD_MsgHello = 0x01, // u16 protocol version
	WRD_MsgGetInfo = 0x02, // -
	WRD_MsgLoadBegin = 0x03, // u32 total size
	WRD_MsgLoadData = 0x04, // u32 offset, bytes
	WRD_MsgLoadEnd = 0x05, // -
	WRD_MsgReadProgram = 0x06, // u8 section, u32 offset, u16 max length
	WRD_MsgSetBreakpoints = 0x07, // u16 count, i32 line * count (replaces all)
	WRD_MsgLaunch = 0x08, // u8 flags (WRD_LaunchStopOnEntry)
	WRD_MsgContinue = 0x09, // optional u8 flags (WRD_ContinueResumeStep)
	WRD_MsgPause = 0x0A, // -
	WRD_MsgStepIn = 0x0B, // -
	WRD_MsgStepOver = 0x0C, // -
	WRD_MsgStepOut = 0x0D, // -
	WRD_MsgTerminate = 0x0E, // -
	WRD_MsgGetStack = 0x10, // u16 first frame, u16 max frames
	WRD_MsgGetLocals = 0x11, // u16 frame, u16 first slot, u16 count
	WRD_MsgGetGlobals = 0x12, // u16 first slot, u16 count
	WRD_MsgGetChildren = 0x13, // u32 handle, u32 first, u16 count
	WRD_MsgSetLocal = 0x14, // u16 frame, u16 slot, value
	WRD_MsgSetGlobal = 0x15, // u16 slot, value
	WRD_MsgSetChild = 0x16, // u32 handle, u32 index, value
	WRD_MsgPing = 0x17, // -
	WRD_MsgDetach = 0x18, // - clear breakpoints and continue
	WRD_MsgAddWatch = 0x19, // u8 root (WrenchDebugWatchRoot), u16 frame, u16 slot, u8 steps, (u8 kind, u32 value) * steps; reply u8 watch id
	WRD_MsgRemoveWatch = 0x1A, // u8 watch id, 0 removes all

	// replies, server to client (seq echoes the request)
	WRD_MsgReplyOk = 0x80, // request specific payload
	WRD_MsgReplyError = 0x81, // u8 WrenchDebugError

	// unsolicited events, server to client (seq 0)
	WRD_MsgEventStopped = 0xC0, // u8 reason, i32 line, u8 wrench error, u32 missing function hash, u8 watch id
	WRD_MsgEventOutput = 0xC1, // bytes
	WRD_MsgEventEnded = 0xC2, // u8 wrench error, u8 flags, i32 line, u32 missing function hash, value
	WRD_MsgEventLoaded = 0xC3, // u32 bytecode hash (program changed on target)
	WRD_MsgEventWatchRemoved = 0xC4, // u8 watch id (its frame returned, or the program changed)
};

enum WrenchDebugError
{
	WRD_ErrNone = 0,
	WRD_ErrUnknownRequest,
	WRD_ErrBadRequest,
	WRD_ErrNoProgram,
	WRD_ErrNotStopped,
	WRD_ErrBadFrame,
	WRD_ErrBadSlot,
	WRD_ErrBadHandle,
	WRD_ErrNoMemory,
	WRD_ErrBadBytecode,
	WRD_ErrTooManyBreakpoints,
	WRD_ErrBusy,
	WRD_ErrVersion,
	WRD_ErrUnsupported,
	WRD_ErrUploadDisabled, // the firmware turned uploads off (setAllowUpload)
	WRD_ErrTooManyWatches,
	WRD_ErrBadWatch, // a watch path whose root does not exist
};

enum WrenchDebugStopReason
{
	WRD_StopEntry = 0,
	WRD_StopBreakpoint,
	WRD_StopStep,
	WRD_StopPause,
	WRD_StopError,
	WRD_StopWatch, // a watched value changed, the stop event names the watch
};

// a watch names a value by where it lives: a global or a local of one frame,
// then elements (WRD_WatchIndex: array, string or raw array position) and
// members (WRD_WatchKey: the hash of a hash table key or struct member name)
// below it. The target follows the path again at every line marker
enum WrenchDebugWatchRoot
{
	WRD_WatchGlobal = 0,
	WRD_WatchLocal = 1,
};

enum WrenchDebugWatchStepKind
{
	WRD_WatchIndex = 0,
	WRD_WatchKey = 1,
};

enum WrenchDebugState
{
	WRD_StateNoProgram = 0,
	WRD_StateLoaded, // program present, not executing
	WRD_StateRunning, // executing inside the VM
	WRD_StateStopped, // halted inside the VM waiting for the client
	WRD_StateEnded, // last launch finished
};

enum WrenchDebugSection
{
	WRD_SectionBytecode = 0,
	WRD_SectionSymbols,
	WRD_SectionSource,
};

enum WrenchDebugFlags
{
	WRD_LaunchStopOnEntry = 1 << 0,

	WRD_ContinueResumeStep = 1 << 0, // continue the step a breakpoint interrupted, if any

	WRD_EndedAborted = 1 << 0, // terminated by the debugger
	WRD_EndedHostDriven = 1 << 1, // target keeps running host code after this

	WRD_CapLoad = 1 << 0, // target accepts uploaded bytecode
	WRD_CapLaunch = 1 << 1, // target can execute the program itself
	WRD_CapSetValue = 1 << 2,
	WRD_CapHostDriven = 1 << 3, // host firmware calls into the script itself
	WRD_CapWatch = 1 << 4, // watchpoints, the hello reply ends with u8 max watches
};

enum WrenchDebugFrameKind
{
	WRD_FrameGlobal = 0,
	WRD_FrameFunction = 1,
};

// encoded value tags (see WrenchDebugValueWriter / WRDebugValue)
enum WrenchDebugValueTag
{
	WRD_ValInt = 0, // i32
	WRD_ValFloat = 1, // f32 bits
	WRD_ValString = 2, // u32 handle, u32 total length, u16 n, n bytes
	WRD_ValArray = 3, // u32 handle, u32 count
	WRD_ValHash = 4, // u32 handle, u32 count
	WRD_ValStruct = 5, // u32 handle, u32 count
	WRD_ValRaw = 6, // u32 handle, u32 size
	WRD_ValOther = 7, // u8 xtype
};

uint16_t wr_debugCrc16( const uint8_t* data, uint32_t len, uint16_t crc = 0xFFFF );

// fixed-capacity little-endian writer, never allocates. Overflow sets a flag
// and further writes are dropped so callers can check once at the end
class WrenchDebugWriter
{
public:
	WrenchDebugWriter( uint8_t* buf, uint32_t capacity ) : m_buf( buf ), m_cap( capacity ), m_len( 0 ), m_overflow( false ) {}

	void u8( uint8_t v );
	void u16( uint16_t v );
	void u32( uint32_t v );
	void i32( int32_t v ) { u32( (uint32_t)v ); }
	void f32( float v );
	void bytes( const void* data, uint32_t len );

	// reserve space now, fill it in later
	uint8_t* reserve( uint32_t len );

	uint32_t size() const { return m_len; }
	uint32_t remaining() const { return m_overflow ? 0 : m_cap - m_len; }
	bool overflow() const { return m_overflow; }
	uint8_t* data() const { return m_buf; }
	void truncate( uint32_t len ) { if ( len < m_len ) { m_len = len; m_overflow = false; } }

private:
	uint8_t* m_buf;
	uint32_t m_cap;
	uint32_t m_len;
	bool m_overflow;
};

// bounds checked little-endian reader. Reading past the end returns zero and
// sets a flag
class WrenchDebugReader
{
public:
	WrenchDebugReader( const uint8_t* buf, uint32_t len ) : m_buf( buf ), m_len( len ), m_pos( 0 ), m_bad( false ) {}

	uint8_t u8();
	uint16_t u16();
	uint32_t u32();
	int32_t i32() { return (int32_t)u32(); }
	float f32();
	const uint8_t* bytes( uint32_t len );

	uint32_t remaining() const { return m_bad ? 0 : m_len - m_pos; }
	uint32_t position() const { return m_pos; }
	bool bad() const { return m_bad; }

private:
	const uint8_t* m_buf;
	uint32_t m_len;
	uint32_t m_pos;
	bool m_bad;
};

// one decoded message. payload points into the decoder buffer and stays valid
// until the next call to feed()
struct WrenchDebugFrame
{
	uint8_t type;
	uint8_t seq;
	uint16_t length;
	const uint8_t* payload;
};

// incremental frame decoder, feed it bytes as they arrive
class WrenchDebugDecoder
{
public:
	WrenchDebugDecoder() { init( 0, 0 ); }
	WrenchDebugDecoder( uint8_t* buffer, uint32_t capacity ) { init( buffer, capacity ); }
	void init( uint8_t* buffer, uint32_t capacity );

	// consume one byte, returns true when a complete valid frame is ready in
	// frame(). 'nowMs' is used to drop stalled partial frames
	bool feed( uint8_t byte, uint32_t nowMs );

	const WrenchDebugFrame& frame() const { return m_frame; }
	void reset() { m_state = 0; m_pos = 0; }

	uint32_t crcErrors() const { return m_crcErrors; }
	uint32_t framingErrors() const { return m_framingErrors; }

private:
	uint8_t* m_buf;
	uint32_t m_cap;
	uint32_t m_pos;
	uint32_t m_expected;
	uint32_t m_lastByteMs;
	uint8_t m_state;
	uint32_t m_crcErrors;
	uint32_t m_framingErrors;
	WrenchDebugFrame m_frame;
};

// writes the frame header and CRC around a payload already placed at
// out + WRD_FRAME_HEADER. Returns the total frame size
#define WRD_FRAME_HEADER 6
uint32_t wr_debugFinishFrame( uint8_t* out, uint8_t type, uint8_t seq, uint16_t payloadLen );

#endif

#endif
