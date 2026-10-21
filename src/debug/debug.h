#ifndef _DEBUG_H
#define _DEBUG_H
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

// The compiler emits O_DebugInfo followed by a 16 bit codeword. It is always
// defined so the compiler can produce debug code even if the VM it is built
// into cannot run the debugger.
enum WrenchDebugInfoType
{
	WRD_TypeMask = 0xC000,
	WRD_PayloadMask = 0x3FFF,
	WRD_GlobalStopFunction = 0x0F00,
	WRD_ExternalFunction = 0x0E00,

	WRD_FunctionCall = 0x0000, // payload is function index
	WRD_LineNumber = 0x4000, // 14-bit line number
	WRD_Return = 0x8000, // function has returned
	WRD_LineHigh = 0xC000, // upper bits of the next line number (lines > 16383)
};

#ifdef WRENCH_INCLUDE_DEBUG_CODE

void wr_stackDump( const WRValue* bottom, const WRValue* top, WRstr& out );

enum WrenchDebugStepMode
{
	WRD_StepNone = 0,
	WRD_StepIn,
	WRD_StepOver,
	WRD_StepOut,
	WRD_StepPause,
	WRD_StepEntry,
};

struct WrenchDebugHandle
{
	const void* p;
	uint32_t aux;
	uint8_t kind; // WrenchDebugValueTag
};

// what a watched value looked like when last checked: pointers only take
// part in comparisons, they are never followed
struct WrenchDebugWatchSnapshot
{
	uint8_t kind; // 0 the path leads nowhere, else WR_INT, WR_FLOAT or WR_EX
	uint8_t xtype;
	uint32_t bits;
	const void* p;
	uint32_t count;
};

struct WrenchDebugWatch
{
	uint8_t used;
	uint8_t root; // WrenchDebugWatchRoot
	uint16_t slot;
	const WRValue* frameBase; // a local's frame
	int16_t function; // and the function running in it
	uint8_t stepCount;
	uint8_t stepKind[WRENCH_DEBUG_MAX_WATCH_STEPS]; // WrenchDebugWatchStepKind
	uint32_t step[WRENCH_DEBUG_MAX_WATCH_STEPS];
	WrenchDebugWatchSnapshot last;
};

struct WrenchDebugServerPrivate
{
	WrenchDebugServer* parent;
	WRState* w;
	WrenchDebugTransport* transport;

	// program
	WRContext* context;
	bool ownsContext;
	const uint8_t* bytes;
	uint32_t bytesLen;
	bool ownsBytes;
	uint32_t programHash;
	uint8_t compilerFlags;
	const uint8_t* symbols;
	uint32_t symbolsLen;
	const uint8_t* source;
	uint32_t sourceLen;

	// upload in progress
	uint8_t* upload;
	uint32_t uploadSize;
	uint32_t uploadReceived;

	// execution
	uint8_t state; // WrenchDebugState
	uint8_t stepMode; // WrenchDebugStepMode
	uint32_t stepKey;
	uint32_t pollCounter;
	bool hostDriven;
	bool abortOnDisconnect;
	bool uploadDisabled; // zero (uploads allowed) after the memset
	bool entryBreak; // breakOnEntry() armed
	uint32_t entryWaitMs; // how long to wait for a debugger, 0 forever
	bool helloReceived;
	bool launchPending;
	uint8_t launchFlags;
	bool abortPending;
	bool resume;
	bool inLaunch;
	bool inHook; // polling the link from inside the VM
	int32_t lastLine;
	int32_t lineHigh; // pending WRD_LineHigh prefix
	// retransmission of the previous request (its reply was lost): same type
	// and sequence number as the request right before it
	uint8_t lastRequestSeq;
	uint8_t lastRequestType;
	uint8_t lastControlReply; // reply code of the last control request, 0 ok
	bool processingControl;
	int tickFlags;

	// the current stop, announced again to a client that attaches or pauses
	// while we are halted (its Stopped event may have been lost)
	uint8_t stopReason;
	uint8_t stopError;

	// a step interrupted by a breakpoint, a Continue with WRD_ContinueResumeStep
	// picks it up again (the client decided not to stop there)
	uint8_t interruptedStepMode;
	uint32_t interruptedStepKey;

	// setLinkTimeout(): a halted target gives up on a client it has not
	// heard from for this long (0: never)
	uint32_t linkTimeoutMs;
	uint32_t lastRxMs;

	// script code of the current context has run (even if a terminate or an
	// error cut it short): a launch needs a fresh context
	bool contextRan;

	// where we are stopped
	WRContext* stopContext;
	const unsigned char* stopPc;
	WRValue* stopFrameBase;
	WRValue* stopStackTop;

	int32_t breakpoints[WRENCH_DEBUG_MAX_BREAKPOINTS];
	uint16_t breakpointCount;

	WrenchDebugWatch watches[WRENCH_DEBUG_MAX_WATCHES]; // watch id is index + 1
	uint8_t watchCount;
	uint8_t watchHit; // id of the watch the current stop is for

	WrenchDebugHandle handles[WRENCH_DEBUG_MAX_HANDLES];
	uint16_t handleCount;
	uint16_t handleGeneration;

	// link
	WrenchDebugDecoder decoder;
	uint8_t rx[WRENCH_DEBUG_MAX_PAYLOAD + WRD_FRAME_OVERHEAD];
	uint8_t tx[WRENCH_DEBUG_MAX_PAYLOAD + WRD_FRAME_OVERHEAD];
};

#endif

#endif
