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

#ifdef WRENCH_INCLUDE_DEBUG_CODE

// strings shown inline in variable lists are cut to this many bytes, the
// client fetches the rest through the string handle
#define WRD_STRING_PREVIEW 48

struct WrenchDebugFrameInfo
{
	uint8_t kind;
	uint8_t function;
	uint32_t pcOffset;
	WRValue* frameBase;
};

// walks the live VM frames of a stopped context, innermost first
class WrenchDebugFrameWalker
{
public:
	WrenchDebugFrameWalker( WRContext* context, const unsigned char* pc, WRValue* frameBase )
		: m_context( context ), m_pc( pc ), m_frameBase( frameBase ), m_done( context == 0 || pc == 0 ) {}

	bool next( WrenchDebugFrameInfo& info );

private:
	int functionForOffset( uint32_t offset ) const;
	bool onStack( const WRValue* v ) const;

	WRContext* m_context;
	const unsigned char* m_pc;
	WRValue* m_frameBase;
	bool m_done;
};

int WrenchDebugFrameWalker::functionForOffset( uint32_t offset ) const
{
	// global code always precedes the function bodies, so the owner of an
	// offset is the function with the highest start at or below it
	int best = -1;
	uint32_t bestOffset = 0;
	for ( int i = 0; i < m_context->numLocalFunctions; ++i )
	{
		uint32_t start = m_context->localFunctions[i].functionOffset;
		if ( start <= offset && (best < 0 || start >= bestOffset) )
		{
			best = i;
			bestOffset = start;
		}
	}
	return best;
}

bool WrenchDebugFrameWalker::onStack( const WRValue* v ) const
{
	return v >= m_context->stack && v < m_context->stack + m_context->w->stackSize;
}

bool WrenchDebugFrameWalker::next( WrenchDebugFrameInfo& info )
{
	if ( m_done )
	{
		return false;
	}

	if ( m_pc < m_context->bottom || m_pc >= m_context->bottom + m_context->bottomSize )
	{
		m_done = true;
		return false;
	}

	info.pcOffset = (uint32_t)(m_pc - m_context->bottom);
	int func = functionForOffset( info.pcOffset );

	if ( func < 0 || !m_frameBase )
	{
		info.kind = WRD_FrameGlobal;
		info.function = 0;
		info.frameBase = 0;
		m_done = true;
		return true;
	}

	info.kind = WRD_FrameFunction;
	info.function = (uint8_t)func;
	info.frameBase = m_frameBase;

	// the return vector sits right after the arguments and locals
	const WRFunction* f = m_context->localFunctions + func;
	WRValue* returnVector = m_frameBase + f->frameBaseAdjustment - 1;
	if ( !onStack(m_frameBase) || !onStack(returnVector) )
	{
		m_done = true;
		return true;
	}

	intptr_t offset = returnVector->returnOffset;
	if ( offset < 0 || offset >= m_context->bottomSize )
	{
		m_done = true;
		return true;
	}

	const unsigned char* callerPc = m_context->bottom + offset;
	if ( callerPc == m_context->stopLocation )
	{
		// called from native code through wr_callFunction, nothing above us
		m_done = true;
		return true;
	}

	m_pc = callerPc;
	m_frameBase = returnVector->frame;
	if ( m_frameBase && !onStack(m_frameBase) )
	{
		m_done = true;
	}
	return true;
}

// one byte past the frame base is "deeper" than global code, which has no
// frame base, so the key orders frames by depth for stepping
static uint32_t wr_debugDepthKey( WRContext* context, const WRValue* frameBase )
{
	return frameBase ? (uint32_t)(frameBase - context->stack) + 1 : 0;
}

class WrenchDebugServerImpl
{
public:
	static void init( WrenchDebugServerPrivate* I );
	static void pump( WrenchDebugServerPrivate* I );
	static void process( WrenchDebugServerPrivate* I, const WrenchDebugFrame& frame );
	static bool stopLoop( WrenchDebugServerPrivate* I, uint8_t reason, uint8_t err );
	static void announceStop( WrenchDebugServerPrivate* I );
	static bool waitForClient( WrenchDebugServerPrivate* I, uint32_t waitMs );
	static void readProgramInfo( WrenchDebugServerPrivate* I );
	static void releaseProgram( WrenchDebugServerPrivate* I );
	static void runLaunch( WrenchDebugServerPrivate* I );
	static void onDisconnect( WrenchDebugServerPrivate* I );

	static uint8_t* payload( WrenchDebugServerPrivate* I ) { return I->tx + WRD_FRAME_HEADER; }
	static uint32_t missingFunction( WrenchDebugServerPrivate* I, uint8_t err )
	{
		return (err == WR_ERR_function_not_found || err == WR_ERR_lib_function_not_found) ? I->w->missingFunction : 0;
	}
	static void send( WrenchDebugServerPrivate* I, uint8_t type, uint8_t seq, uint32_t len );
	static void replyOk( WrenchDebugServerPrivate* I, uint8_t seq ) { send( I, WRD_MsgReplyOk, seq, 0 ); }
	static void replyError( WrenchDebugServerPrivate* I, uint8_t seq, uint8_t code );

	static void resetHandles( WrenchDebugServerPrivate* I );
	static uint32_t addHandle( WrenchDebugServerPrivate* I, uint8_t kind, const void* p, uint32_t aux );
	static WrenchDebugHandle* findHandle( WrenchDebugServerPrivate* I, uint32_t handle );

	static const WRValue* resolve( const WRValue* v, WRValue& temp );
	static void writeValue( WrenchDebugServerPrivate* I, WrenchDebugWriter& out, const WRValue& value );
	static bool readValue( WrenchDebugServerPrivate* I, WrenchDebugReader& in, WRValue* target, WRContext* context );
	static WRValue* storageOf( WRValue* slot );

	static bool frameAt( WrenchDebugServerPrivate* I, uint16_t index, WrenchDebugFrameInfo& info );
	static void getChildren( WrenchDebugServerPrivate* I, uint8_t seq, WrenchDebugReader& in );
	static WRValue* childSlot( WrenchDebugServerPrivate* I, WrenchDebugHandle* h, uint32_t index );
	static uint32_t hashEntryCount( const WRGCObject* va );
	static int hashSlotForIndex( const WRGCObject* va, uint32_t index );
	static uint32_t structMemberHash( const WRGCObject* va, uint32_t index );

	static void addWatch( WrenchDebugServerPrivate* I, uint8_t seq, WrenchDebugReader& in );
	static const WRValue* followWatch( WrenchDebugServerPrivate* I, const WrenchDebugWatch& watch, WRValue& temp );
	static void snapshot( const WRValue* v, WrenchDebugWatchSnapshot& s );
	static void refreshWatches( WrenchDebugServerPrivate* I );
	static void removeWatch( WrenchDebugServerPrivate* I, int index, bool announce );
	static void clearWatches( WrenchDebugServerPrivate* I, bool localsOnly, bool announce );
	static uint8_t checkWatches( WrenchDebugServerPrivate* I, WRContext* context, const unsigned char* pc, WRValue* frameBase );
};

void WrenchDebugServerImpl::init( WrenchDebugServerPrivate* I )
{
	I->decoder.init( I->rx, sizeof(I->rx) );
	I->state = WRD_StateNoProgram;
	I->stepMode = WRD_StepNone;
	I->lastLine = -1;
}

void WrenchDebugServerImpl::send( WrenchDebugServerPrivate* I, uint8_t type, uint8_t seq, uint32_t len )
{
	if ( len > WRENCH_DEBUG_MAX_PAYLOAD )
	{
		len = 0;
		type = WRD_MsgReplyError;
	}
	if ( I->processingControl && (type == WRD_MsgReplyOk || type == WRD_MsgReplyError) )
	{
		// remembered so a retransmission gets the same answer
		I->lastControlReply = (type == WRD_MsgReplyOk) ? 0 : (len ? payload(I)[0] : WRD_ErrBadRequest);
		I->processingControl = false;
	}
	uint32_t total = wr_debugFinishFrame( I->tx, type, seq, (uint16_t)len );
	I->transport->write( I->tx, total );
}

void WrenchDebugServerImpl::replyError( WrenchDebugServerPrivate* I, uint8_t seq, uint8_t code )
{
	payload( I )[0] = code;
	send( I, WRD_MsgReplyError, seq, 1 );
}

void WrenchDebugServerImpl::resetHandles( WrenchDebugServerPrivate* I )
{
	I->handleCount = 0;
	++I->handleGeneration;
}

uint32_t WrenchDebugServerImpl::addHandle( WrenchDebugServerPrivate* I, uint8_t kind, const void* p, uint32_t aux )
{
	for ( uint16_t i = 0; i < I->handleCount; ++i )
	{
		if ( I->handles[i].p == p && I->handles[i].kind == kind )
		{
			return ((uint32_t)I->handleGeneration << 16) | (uint32_t)(i + 1);
		}
	}

	if ( I->handleCount >= WRENCH_DEBUG_MAX_HANDLES )
	{
		return 0;
	}

	WrenchDebugHandle* h = I->handles + I->handleCount++;
	h->p = p;
	h->aux = aux;
	h->kind = kind;
	return ((uint32_t)I->handleGeneration << 16) | (uint32_t)I->handleCount;
}

WrenchDebugHandle* WrenchDebugServerImpl::findHandle( WrenchDebugServerPrivate* I, uint32_t handle )
{
	uint32_t index = handle & 0xFFFF;
	if ( (handle >> 16) != I->handleGeneration || index == 0 || index > I->handleCount )
	{
		return 0;
	}
	return I->handles + (index - 1);
}

const WRValue* WrenchDebugServerImpl::resolve( const WRValue* v, WRValue& temp )
{
	for ( int guard = 0; guard < 16 && v; ++guard )
	{
		if ( v->type == WR_REF )
		{
			v = v->r;
		}
		else if ( v->type == WR_EX && IS_CONTAINER_MEMBER(v->xtype) )
		{
			// deref() hands back a shared temporary for some members, copy it
			temp = v->deref();
			return &temp;
		}
		else
		{
			return v;
		}
	}
	temp.init();
	return &temp;
}

WRValue* WrenchDebugServerImpl::storageOf( WRValue* slot )
{
	for ( int guard = 0; guard < 16 && slot; ++guard )
	{
		if ( slot->type != WR_REF )
		{
			return (slot->type == WR_EX && IS_CONTAINER_MEMBER(slot->xtype)) ? 0 : slot;
		}
		slot = slot->r;
	}
	return 0;
}

void WrenchDebugServerImpl::writeValue( WrenchDebugServerPrivate* I, WrenchDebugWriter& out, const WRValue& value )
{
	WRValue temp;
	const WRValue* v = resolve( &value, temp );

	if ( v->type == WR_INT )
	{
		out.u8( WRD_ValInt );
		out.i32( v->i );
		return;
	}

	if ( v->type == WR_FLOAT )
	{
		out.u8( WRD_ValFloat );
		out.f32( v->f );
		return;
	}

	if ( v->type == WR_EX )
	{
		if ( v->xtype == WR_EX_ARRAY && v->va )
		{
			if ( v->va->m_type == SV_CHAR )
			{
				uint32_t total = v->va->m_size;
				uint16_t n = (uint16_t)(total > WRD_STRING_PREVIEW ? WRD_STRING_PREVIEW : total);
				out.u8( WRD_ValString );
				out.u32( addHandle(I, WRD_ValString, v->va, 0) );
				out.u32( total );
				out.u16( n );
				out.bytes( v->va->m_Cdata, n );
				return;
			}

			if ( v->va->m_type == SV_VALUE )
			{
				out.u8( WRD_ValArray );
				out.u32( addHandle(I, WRD_ValArray, v->va, 0) );
				out.u32( v->va->m_size );
				return;
			}
		}
		else if ( v->xtype == WR_EX_HASH_TABLE && v->va )
		{
			out.u8( WRD_ValHash );
			out.u32( addHandle(I, WRD_ValHash, v->va, 0) );
			out.u32( hashEntryCount(v->va) );
			return;
		}
		else if ( v->xtype == WR_EX_STRUCT && v->va )
		{
			out.u8( WRD_ValStruct );
			out.u32( addHandle(I, WRD_ValStruct, v->va, 0) );
			out.u32( v->va->m_size );
			return;
		}
		else if ( IS_RAW_ARRAY(v->xtype) )
		{
			uint32_t size = (uint32_t)EX_RAW_ARRAY_SIZE_FROM_P2( v->p2 );
			out.u8( WRD_ValRaw );
			out.u32( addHandle(I, WRD_ValRaw, v->c, size) );
			out.u32( size );
			return;
		}
	}

	out.u8( WRD_ValOther );
	out.u8( v->type == WR_EX ? (uint8_t)v->xtype : (uint8_t)v->type );
}

bool WrenchDebugServerImpl::readValue( WrenchDebugServerPrivate* I, WrenchDebugReader& in, WRValue* target, WRContext* context )
{
	uint8_t tag = in.u8();
	if ( tag == WRD_ValInt )
	{
		int32_t i = in.i32();
		if ( in.bad() )
		{
			return false;
		}
		target->init( (int)i );
		return true;
	}

	if ( tag == WRD_ValFloat )
	{
		float f = in.f32();
		if ( in.bad() )
		{
			return false;
		}
		target->init( f );
		return true;
	}

	if ( tag == WRD_ValString )
	{
		uint16_t n = in.u16();
		const uint8_t* bytes = in.bytes( n );
		if ( in.bad() || !context )
		{
			return false;
		}

		WRValue made;
		made.init();
		wr_makeString( context, &made, n ? (const char*)bytes : "", n );
		if ( made.type != WR_EX )
		{
			return false;
		}
		*target = made;
		return true;
	}

	return false;
}

uint32_t WrenchDebugServerImpl::hashEntryCount( const WRGCObject* va )
{
	uint32_t count = 0;
	for ( uint32_t i = 0; i < va->hashSlots(); ++i )
	{
		if ( va->m_hashTable[i] != WRENCH_NULL_HASH )
		{
			++count;
		}
	}
	return count;
}

int WrenchDebugServerImpl::hashSlotForIndex( const WRGCObject* va, uint32_t index )
{
	uint32_t seen = 0;
	for ( uint32_t i = 0; i < va->hashSlots(); ++i )
	{
		if ( va->m_hashTable[i] != WRENCH_NULL_HASH )
		{
			if ( seen++ == index )
			{
				return (int)i;
			}
		}
	}
	return -1;
}

uint32_t WrenchDebugServerImpl::structMemberHash( const WRGCObject* va, uint32_t index )
{
	// struct members are laid out by index, the compiled-in table maps
	// member name hashes to that index (5 bytes per entry)
	const unsigned char* table = va->m_ROMHashTable;
	for ( uint32_t e = 0; e < va->m_mod; ++e, table += 5 )
	{
		uint32_t hash = (uint32_t)READ_32_FROM_PC( table );
		if ( hash != WRENCH_NULL_HASH && (uint32_t)READ_8_FROM_PC(table + 4) == index )
		{
			return hash;
		}
	}
	return 0;
}

void WrenchDebugServerImpl::addWatch( WrenchDebugServerPrivate* I, uint8_t seq, WrenchDebugReader& in )
{
	WrenchDebugWatch watch;
	memset( (char*)&watch, 0, sizeof(watch) );
	watch.root = in.u8();
	uint16_t frameIndex = in.u16();
	watch.slot = in.u16();
	watch.stepCount = in.u8();
	if ( watch.stepCount > WRENCH_DEBUG_MAX_WATCH_STEPS )
	{
		replyError( I, seq, WRD_ErrUnsupported );
		return;
	}
	for ( uint8_t s = 0; s < watch.stepCount; ++s )
	{
		watch.stepKind[s] = in.u8();
		watch.step[s] = in.u32();
	}
	if ( in.bad() || (watch.root != WRD_WatchGlobal && watch.root != WRD_WatchLocal) )
	{
		replyError( I, seq, WRD_ErrBadRequest );
		return;
	}
	if ( !I->context )
	{
		replyError( I, seq, WRD_ErrNoProgram );
		return;
	}

	if ( watch.root == WRD_WatchGlobal )
	{
		if ( watch.slot >= I->context->globals )
		{
			replyError( I, seq, WRD_ErrBadWatch );
			return;
		}
	}
	else
	{
		// a local belongs to one call: name it while that call is on the stack
		WrenchDebugFrameInfo info;
		if ( I->state != WRD_StateStopped )
		{
			replyError( I, seq, WRD_ErrNotStopped );
			return;
		}
		if ( !frameAt(I, frameIndex, info) || info.kind != WRD_FrameFunction
				|| watch.slot >= I->stopContext->localFunctions[info.function].frameBaseAdjustment - 1 )
		{
			replyError( I, seq, WRD_ErrBadWatch );
			return;
		}
		watch.frameBase = info.frameBase;
		watch.function = info.function;
	}

	int found = -1;
	for ( int i = 0; i < WRENCH_DEBUG_MAX_WATCHES; ++i )
	{
		if ( !I->watches[i].used )
		{
			found = i;
			break;
		}
	}
	if ( found < 0 )
	{
		replyError( I, seq, WRD_ErrTooManyWatches );
		return;
	}

	watch.used = 1;
	WRValue temp;
	snapshot( followWatch(I, watch, temp), watch.last );
	I->watches[found] = watch;
	++I->watchCount;

	WrenchDebugWriter out( payload(I), WRENCH_DEBUG_MAX_PAYLOAD );
	out.u8( (uint8_t)(found + 1) );
	send( I, WRD_MsgReplyOk, seq, out.size() );
}

// the value a watch names now, 0 when its path leads nowhere (an element
// past the end, a key not present, a step into something that is no
// container). Everything is looked up again from the root, so a container
// that grew, moved or was collected since is never touched
const WRValue* WrenchDebugServerImpl::followWatch( WrenchDebugServerPrivate* I, const WrenchDebugWatch& watch, WRValue& temp )
{
	const WRValue* v = 0;
	if ( watch.root == WRD_WatchGlobal )
	{
		if ( !I->context || watch.slot >= I->context->globals )
		{
			return 0;
		}
		v = ((const WRValue*)(I->context + 1)) + watch.slot;
	}
	else
	{
		v = watch.frameBase + watch.slot;
	}

	for ( uint8_t s = 0; s < watch.stepCount; ++s )
	{
		v = resolve( v, temp );
		uint32_t n = watch.step[s];
		bool last = (s + 1 == watch.stepCount);
		if ( v->type != WR_EX )
		{
			return 0;
		}

		if ( v->xtype == WR_EX_ARRAY && v->va )
		{
			if ( watch.stepKind[s] != WRD_WatchIndex || n >= v->va->m_size )
			{
				return 0;
			}
			if ( v->va->m_type == SV_VALUE )
			{
				v = v->va->m_Vdata + n;
				continue;
			}
			if ( v->va->m_type != SV_CHAR || !last )
			{
				return 0;
			}
			int c = (unsigned char)v->va->m_Cdata[n];
			temp.init( c );
			return &temp;
		}

		if ( IS_RAW_ARRAY(v->xtype) )
		{
			if ( watch.stepKind[s] != WRD_WatchIndex || !last || n >= (uint32_t)EX_RAW_ARRAY_SIZE_FROM_P2(v->p2) )
			{
				return 0;
			}
			int c = (unsigned char)v->c[n];
			temp.init( c );
			return &temp;
		}

		if ( v->xtype == WR_EX_HASH_TABLE && v->va )
		{
			// an integer key is its own hash, so both kinds of step work
			v = v->va->exists( n, false );
		}
		else if ( v->xtype == WR_EX_STRUCT && v->va && watch.stepKind[s] == WRD_WatchKey )
		{
			v = wr_valueFromConfirmedStruct( (WRValue*)v, n );
		}
		else
		{
			return 0;
		}

		if ( !v )
		{
			return 0;
		}
	}

	return resolve( v, temp );
}

// numbers by value; strings by identity, length and contents; other
// containers by identity and size (a changed element is caught by watching
// the element)
void WrenchDebugServerImpl::snapshot( const WRValue* v, WrenchDebugWatchSnapshot& s )
{
	memset( (char*)&s, 0, sizeof(s) );
	if ( !v )
	{
		return;
	}

	s.kind = (uint8_t)(v->type + 1);
	if ( v->type == WR_INT || v->type == WR_FLOAT )
	{
		s.bits = v->ui;
		return;
	}
	if ( v->type != WR_EX )
	{
		return;
	}

	s.xtype = (uint8_t)v->xtype;
	if ( IS_RAW_ARRAY(v->xtype) )
	{
		s.p = v->c;
		s.count = (uint32_t)EX_RAW_ARRAY_SIZE_FROM_P2( v->p2 );
	}
	else if ( (v->xtype == WR_EX_ARRAY || v->xtype == WR_EX_HASH_TABLE || v->xtype == WR_EX_STRUCT) && v->va )
	{
		s.p = v->va;
		s.count = v->va->m_size;
		if ( v->xtype == WR_EX_ARRAY && v->va->m_type == SV_CHAR )
		{
			s.bits = wr_hash( v->va->m_Cdata, (int)v->va->m_size );
		}
	}
}

// while stopped only the debugger changes values: take them all again
// before the program goes on, so its own writes do not count as changes
void WrenchDebugServerImpl::refreshWatches( WrenchDebugServerPrivate* I )
{
	for ( int i = 0; i < WRENCH_DEBUG_MAX_WATCHES; ++i )
	{
		if ( I->watches[i].used )
		{
			WRValue temp;
			snapshot( followWatch(I, I->watches[i], temp), I->watches[i].last );
		}
	}
}

void WrenchDebugServerImpl::removeWatch( WrenchDebugServerPrivate* I, int index, bool announce )
{
	if ( !I->watches[index].used )
	{
		return;
	}
	I->watches[index].used = 0;
	--I->watchCount;
	if ( announce && I->helloReceived )
	{
		WrenchDebugWriter out( payload(I), WRENCH_DEBUG_MAX_PAYLOAD );
		out.u8( (uint8_t)(index + 1) );
		send( I, WRD_MsgEventWatchRemoved, 0, out.size() );
	}
}

void WrenchDebugServerImpl::clearWatches( WrenchDebugServerPrivate* I, bool localsOnly, bool announce )
{
	for ( int i = 0; i < WRENCH_DEBUG_MAX_WATCHES; ++i )
	{
		if ( !localsOnly || I->watches[i].root == WRD_WatchLocal )
		{
			removeWatch( I, i, announce );
		}
	}
}

// the id of the first watch whose value changed since it was last taken,
// 0 for none. A local whose call has returned goes away (and the client
// hears about it): the slot belongs to someone else by now
uint8_t WrenchDebugServerImpl::checkWatches( WrenchDebugServerPrivate* I, WRContext* context, const unsigned char* pc, WRValue* frameBase )
{
	uint32_t depth = wr_debugDepthKey( context, frameBase );
	int function = -2;
	for ( int i = 0; i < WRENCH_DEBUG_MAX_WATCHES; ++i )
	{
		WrenchDebugWatch& watch = I->watches[i];
		if ( !watch.used )
		{
			continue;
		}

		if ( watch.root == WRD_WatchLocal )
		{
			uint32_t watchDepth = wr_debugDepthKey( context, watch.frameBase );
			if ( depth == watchDepth && function == -2 )
			{
				// same depth: still that call, or a later one in its place?
				WrenchDebugFrameWalker walker( context, pc, frameBase );
				WrenchDebugFrameInfo info;
				function = (walker.next(info) && info.kind == WRD_FrameFunction) ? info.function : -1;
			}
			if ( depth < watchDepth || (depth == watchDepth && function != watch.function) )
			{
				removeWatch( I, i, true );
				continue;
			}
		}

		WRValue temp;
		WrenchDebugWatchSnapshot now;
		snapshot( followWatch(I, watch, temp), now );
		if ( now.kind != watch.last.kind || now.xtype != watch.last.xtype || now.bits != watch.last.bits
				|| now.p != watch.last.p || now.count != watch.last.count )
		{
			return (uint8_t)(i + 1);
		}
	}
	return 0;
}

bool WrenchDebugServerImpl::frameAt( WrenchDebugServerPrivate* I, uint16_t index, WrenchDebugFrameInfo& info )
{
	if ( I->state != WRD_StateStopped )
	{
		return false;
	}

	WrenchDebugFrameWalker walker( I->stopContext, I->stopPc, I->stopFrameBase );
	for ( uint16_t i = 0; walker.next(info); ++i )
	{
		if ( i == index )
		{
			return true;
		}
	}
	return false;
}

WRValue* WrenchDebugServerImpl::childSlot( WrenchDebugServerPrivate* I, WrenchDebugHandle* h, uint32_t index )
{
	WRGCObject* va = (WRGCObject*)h->p;
	if ( h->kind == WRD_ValArray || h->kind == WRD_ValStruct )
	{
		return index < va->m_size ? va->m_Vdata + index : 0;
	}

	if ( h->kind == WRD_ValHash )
	{
		int slot = hashSlotForIndex( va, index );
		return slot < 0 ? 0 : va->m_Vdata + (slot << 1);
	}

	return 0;
}

void WrenchDebugServerImpl::getChildren( WrenchDebugServerPrivate* I, uint8_t seq, WrenchDebugReader& in )
{
	uint32_t handle = in.u32();
	uint32_t first = in.u32();
	uint16_t count = in.u16();
	if ( in.bad() )
	{
		replyError( I, seq, WRD_ErrBadRequest );
		return;
	}

	WrenchDebugHandle* h = findHandle( I, handle );
	if ( !h )
	{
		replyError( I, seq, WRD_ErrBadHandle );
		return;
	}

	WrenchDebugWriter out( payload(I), WRENCH_DEBUG_MAX_PAYLOAD );
	const WRGCObject* va = (const WRGCObject*)h->p;

	if ( h->kind == WRD_ValString || h->kind == WRD_ValRaw )
	{
		const uint8_t* data = (h->kind == WRD_ValString) ? va->m_Cdata : (const uint8_t*)h->p;
		uint32_t total = (h->kind == WRD_ValString) ? va->m_size : h->aux;
		uint32_t n = 0;
		if ( first < total )
		{
			n = total - first;
			if ( n > count )
			{
				n = count;
			}
			if ( n > WRENCH_DEBUG_MAX_PAYLOAD - 6 )
			{
				n = WRENCH_DEBUG_MAX_PAYLOAD - 6;
			}
		}
		out.u32( total );
		out.u16( (uint16_t)n );
		out.bytes( data + first, n );
		send( I, WRD_MsgReplyOk, seq, out.size() );
		return;
	}

	uint32_t total = (h->kind == WRD_ValHash) ? hashEntryCount( va ) : va->m_size;
	out.u32( total );
	uint8_t* countAt = out.reserve( 2 );
	uint16_t written = 0;

	for ( uint32_t i = first; i < total && written < count; ++i )
	{
		uint32_t mark = out.size();

		if ( h->kind == WRD_ValArray )
		{
			writeValue( I, out, va->m_Vdata[i] );
		}
		else if ( h->kind == WRD_ValStruct )
		{
			out.u32( structMemberHash(va, i) );
			writeValue( I, out, va->m_Vdata[i] );
		}
		else
		{
			int slot = hashSlotForIndex( va, i );
			if ( slot < 0 )
			{
				break;
			}
			writeValue( I, out, va->m_Vdata[(slot << 1) + 1] ); // key
			writeValue( I, out, va->m_Vdata[slot << 1] ); // value
		}

		if ( out.overflow() )
		{
			out.truncate( mark );
			break;
		}
		++written;
	}

	countAt[0] = (uint8_t)written;
	countAt[1] = (uint8_t)(written >> 8);
	send( I, WRD_MsgReplyOk, seq, out.size() );
}

void WrenchDebugServerImpl::readProgramInfo( WrenchDebugServerPrivate* I )
{
	const uint8_t* b = I->bytes;
	I->symbols = 0;
	I->symbolsLen = 0;
	I->source = 0;
	I->sourceLen = 0;
	I->programHash = (uint32_t)READ_32_FROM_PC( b + I->bytesLen - 4 );
	I->compilerFlags = (uint8_t)READ_8_FROM_PC( b + 2 );

	uint32_t globals = (uint8_t)READ_8_FROM_PC( b );
	uint32_t functions = (uint8_t)READ_8_FROM_PC( b + 1 );
	uint32_t pos = 3 + functions * WR_FUNCTION_CORE_SIZE;

	if ( I->compilerFlags & WR_INCLUDE_GLOBALS )
	{
		pos += globals * 4;
	}

	if ( (I->compilerFlags & (WR_EMBED_DEBUG_CODE | WR_EMBED_SOURCE_CODE)) && pos + 6 <= I->bytesLen )
	{
		uint32_t len = (uint16_t)READ_16_FROM_PC( b + pos + 4 );
		if ( pos + 6 + len <= I->bytesLen )
		{
			I->symbols = b + pos + 6;
			I->symbolsLen = len;
		}
		pos += 6 + len;
	}

	if ( (I->compilerFlags & WR_EMBED_SOURCE_CODE) && pos + 4 <= I->bytesLen )
	{
		uint32_t len = (uint32_t)READ_32_FROM_PC( b + pos );
		if ( len <= I->bytesLen - pos - 4 )
		{
			I->source = b + pos + 4;
			I->sourceLen = len;
		}
	}
}

void WrenchDebugServerImpl::releaseProgram( WrenchDebugServerPrivate* I )
{
	// handles point into the program's objects, which are about to go, and
	// watches name its variables
	resetHandles( I );
	clearWatches( I, false, true );

	if ( I->context )
	{
		I->context->debugServer = 0;
		if ( I->ownsContext )
		{
			wr_destroyContext( I->context );
		}
	}
	I->context = 0;
	I->ownsContext = false;
	I->contextRan = false;

	if ( I->ownsBytes )
	{
		g_free( (void*)I->bytes );
	}
	I->bytes = 0;
	I->bytesLen = 0;
	I->ownsBytes = false;
	I->programHash = 0;
	I->symbols = 0;
	I->symbolsLen = 0;
	I->source = 0;
	I->sourceLen = 0;
	I->state = WRD_StateNoProgram;
}

void WrenchDebugServerImpl::onDisconnect( WrenchDebugServerPrivate* I )
{
	// never leave the target halted or trapping for a client that is gone
	I->helloReceived = false;
	I->breakpointCount = 0;
	clearWatches( I, false, false );
	I->stepMode = WRD_StepNone;
	if ( I->state == WRD_StateStopped )
	{
		I->resume = true;
	}
	if ( I->abortOnDisconnect && (I->inLaunch || I->inHook || I->state == WRD_StateStopped) )
	{
		I->abortPending = true;
	}
	I->decoder.reset();
}

void WrenchDebugServerImpl::pump( WrenchDebugServerPrivate* I )
{
	uint8_t chunk[64];
	for ( int rounds = 0; rounds < 64; ++rounds )
	{
		int got = I->transport->read( chunk, sizeof(chunk) );
		if ( got < 0 )
		{
			if ( I->helloReceived )
			{
				onDisconnect( I );
			}
			return;
		}

		if ( got == 0 )
		{
			if ( I->helloReceived && !I->transport->connected() )
			{
				onDisconnect( I );
			}
			return;
		}

		uint32_t now = I->transport->millis();
		I->lastRxMs = now;
		for ( int i = 0; i < got; ++i )
		{
			if ( I->decoder.feed(chunk[i], now) )
			{
				process( I, I->decoder.frame() );
			}
		}
	}
}

void WrenchDebugServerImpl::announceStop( WrenchDebugServerPrivate* I )
{
	WrenchDebugWriter out( payload(I), WRENCH_DEBUG_MAX_PAYLOAD );
	out.u8( I->stopReason );
	out.i32( I->lastLine );
	out.u8( I->stopError );
	out.u32( missingFunction(I, I->stopError) );
	out.u8( I->stopReason == WRD_StopWatch ? I->watchHit : 0 );
	send( I, WRD_MsgEventStopped, 0, out.size() );
}

bool WrenchDebugServerImpl::stopLoop( WrenchDebugServerPrivate* I, uint8_t reason, uint8_t err )
{
	I->state = WRD_StateStopped;
	I->stepMode = WRD_StepNone;
	I->resume = false;
	I->stopReason = reason;
	I->stopError = err;
	I->lastRxMs = I->transport->millis();
	resetHandles( I );

	announceStop( I );

	while ( !I->resume )
	{
		pump( I );
		if ( I->resume )
		{
			break;
		}
		if ( !I->helloReceived )
		{
			// client detached while we were stopped
			I->resume = true;
			break;
		}
		if ( I->linkTimeoutMs && (uint32_t)(I->transport->millis() - I->lastRxMs) >= I->linkTimeoutMs )
		{
			// nothing from the client for too long (a link that cannot
			// report loss, like a UART): behave as if it disconnected
			onDisconnect( I );
			I->resume = true;
			break;
		}
		I->transport->idle();
	}

	I->resume = false;
	I->state = WRD_StateRunning;
	resetHandles( I );
	refreshWatches( I );

	if ( I->abortPending )
	{
		I->abortPending = false;
		return true;
	}
	return false;
}

// inside the VM: service the link until a client says hello. Requests that
// need an idle VM (upload, launch) are refused meanwhile. False when waitMs
// (0 waits forever) ran out first
bool WrenchDebugServerImpl::waitForClient( WrenchDebugServerPrivate* I, uint32_t waitMs )
{
	uint32_t start = I->transport->millis();
	bool wasInHook = I->inHook;
	I->inHook = true;
	while ( !I->helloReceived )
	{
		pump( I );
		if ( I->helloReceived )
		{
			break;
		}
		if ( waitMs && (uint32_t)(I->transport->millis() - start) >= waitMs )
		{
			break;
		}
		I->transport->idle();
	}
	I->inHook = wasInHook;
	return I->helloReceived;
}

void WrenchDebugServerImpl::runLaunch( WrenchDebugServerPrivate* I )
{
	I->launchPending = false;

	if ( !I->context )
	{
		return;
	}

	if ( I->context->stopLocation || I->context->yield_pc || I->contextRan )
	{
		// already ran (maybe cut short by a terminate or an error), globals
		// must start fresh so build a new context
		if ( !I->ownsContext || !I->bytes )
		{
			return;
		}
		resetHandles( I );
		I->context->debugServer = 0;
		wr_destroyContext( I->context );
		I->context = wr_newContext( I->w, I->bytes, (int)I->bytesLen );
		if ( !I->context )
		{
			I->state = WRD_StateNoProgram;
			return;
		}
		I->context->debugServer = I->parent;
		I->tickFlags |= WRD_TickProgramLoaded; // firmware must fetch context() again
	}
	I->contextRan = true;

	// a run that was cut short leaves no frames behind; globals start over
	clearWatches( I, true, true );
	refreshWatches( I );

	I->stepMode = (I->launchFlags & WRD_LaunchStopOnEntry) ? WRD_StepEntry : WRD_StepNone;
	I->abortPending = false;
	I->state = WRD_StateRunning;
	I->inLaunch = true;
	I->lastLine = -1;

	WRValue* ret = wr_executeContext( I->context );
	while ( !ret && I->w->err == WR_ERR_None && I->context->yield_pc )
	{
		// nobody else will resume a script yield in server driven mode
		pump( I );
		if ( I->abortPending )
		{
			I->abortPending = false;
			I->w->err = WR_ERR_debugger_terminated;
			break;
		}
		ret = wr_continue( I->context );
	}

	I->inLaunch = false;
	I->state = WRD_StateEnded;
	I->stepMode = WRD_StepNone;
	resetHandles( I );

	uint8_t err = I->w->err;
	WrenchDebugWriter out( payload(I), WRENCH_DEBUG_MAX_PAYLOAD );
	out.u8( err );
	uint8_t flags = 0;
	if ( err == WR_ERR_debugger_terminated )
	{
		flags |= WRD_EndedAborted;
	}
	if ( I->hostDriven )
	{
		flags |= WRD_EndedHostDriven;
	}
	out.u8( flags );
	out.i32( I->lastLine );
	out.u32( missingFunction(I, err) );
	if ( ret )
	{
		writeValue( I, out, *ret );
	}
	else
	{
		WRValue zero;
		zero.init();
		writeValue( I, out, zero );
	}
	send( I, WRD_MsgEventEnded, 0, out.overflow() ? 10 : out.size() );
	resetHandles( I );

	I->tickFlags |= WRD_TickProgramRan;
}

void WrenchDebugServerImpl::process( WrenchDebugServerPrivate* I, const WrenchDebugFrame& frame )
{
	WrenchDebugReader in( frame.payload, frame.length );
	uint8_t seq = frame.seq;
	bool inVm = I->inLaunch || I->inHook || I->state == WRD_StateStopped;
	bool stopped = (I->state == WRD_StateStopped);

	// a retransmitted control request (its reply was lost) must not act
	// twice. A retransmission repeats the request right before it, same type
	// and sequence number; any other request in between makes it a new one
	// (the 8 bit sequence numbers wrap)
	bool control = frame.type == WRD_MsgLaunch || frame.type == WRD_MsgContinue || frame.type == WRD_MsgPause
		|| frame.type == WRD_MsgStepIn || frame.type == WRD_MsgStepOver || frame.type == WRD_MsgStepOut
		|| frame.type == WRD_MsgTerminate || frame.type == WRD_MsgLoadEnd || frame.type == WRD_MsgDetach;
	bool repeat = seq != 0 && seq == I->lastRequestSeq && frame.type == I->lastRequestType;
	I->lastRequestSeq = seq;
	I->lastRequestType = frame.type;
	I->processingControl = false;
	if ( control )
	{
		if ( repeat )
		{
			if ( I->lastControlReply )
			{
				replyError( I, seq, I->lastControlReply );
			}
			else
			{
				replyOk( I, seq );
			}
			return;
		}
		I->processingControl = true;
	}

	if ( !I->helloReceived && frame.type != WRD_MsgHello && frame.type != WRD_MsgPing )
	{
		replyError( I, seq, WRD_ErrBadRequest );
		return;
	}

	switch ( frame.type )
	{
		case WRD_MsgHello:
		{
			uint16_t version = in.u16();
			if ( version != WRD_PROTOCOL_VERSION )
			{
				replyError( I, seq, WRD_ErrVersion );
				return;
			}

			I->helloReceived = true;
			I->breakpointCount = 0;
			clearWatches( I, false, false );
			I->stepMode = WRD_StepNone;
			I->lastRequestSeq = 0;
			I->lastRequestType = 0;
			I->tickFlags |= WRD_TickClientAttached;

			uint8_t caps = WRD_CapLaunch | WRD_CapSetValue | WRD_CapWatch;
			if ( !I->uploadDisabled )
			{
				caps |= WRD_CapLoad;
			}
			if ( I->hostDriven )
			{
				caps |= WRD_CapHostDriven;
			}

			WrenchDebugWriter out( payload(I), WRENCH_DEBUG_MAX_PAYLOAD );
			out.u16( WRD_PROTOCOL_VERSION );
			out.u8( WRENCH_VERSION_MAJOR );
			out.u8( WRENCH_VERSION_MINOR );
			out.u8( WRENCH_VERSION_BUILD );
			out.u16( WRENCH_DEBUG_MAX_PAYLOAD );
			out.u8( caps );
			out.u16( WRENCH_DEBUG_MAX_BREAKPOINTS );
			out.u8( WRENCH_DEBUG_MAX_WATCHES );
			send( I, WRD_MsgReplyOk, seq, out.size() );

			if ( stopped )
			{
				// a client attaching to a halted target must learn about it
				announceStop( I );
			}
			return;
		}

		case WRD_MsgPing:
		{
			replyOk( I, seq );
			return;
		}

		case WRD_MsgGetInfo:
		{
			WrenchDebugWriter out( payload(I), WRENCH_DEBUG_MAX_PAYLOAD );
			out.u8( I->state );
			out.u32( I->programHash );
			out.u32( I->bytesLen );
			out.u8( I->compilerFlags );
			out.u32( I->symbolsLen );
			out.u32( I->sourceLen );
			out.u16( I->context ? I->context->globals : 0 );
			out.u8( I->context ? I->context->numLocalFunctions : 0 );
			out.i32( I->lastLine );
			send( I, WRD_MsgReplyOk, seq, out.size() );
			return;
		}

		case WRD_MsgLoadBegin:
		{
			uint32_t size = in.u32();
			if ( I->uploadDisabled )
			{
				replyError( I, seq, WRD_ErrUploadDisabled );
				return;
			}
			if ( in.bad() || size < 8 )
			{
				replyError( I, seq, WRD_ErrBadRequest );
				return;
			}
			if ( inVm )
			{
				replyError( I, seq, WRD_ErrBusy );
				return;
			}

			g_free( I->upload );
			I->upload = (uint8_t*)g_malloc( size );
			I->uploadSize = I->upload ? size : 0;
			I->uploadReceived = 0;
			if ( !I->upload )
			{
				replyError( I, seq, WRD_ErrNoMemory );
				return;
			}
			replyOk( I, seq );
			return;
		}

		case WRD_MsgLoadData:
		{
			uint32_t offset = in.u32();
			uint32_t len = in.remaining();
			const uint8_t* data = in.bytes( len );
			if ( I->uploadDisabled )
			{
				replyError( I, seq, WRD_ErrUploadDisabled );
				return;
			}
			if ( in.bad() || !I->upload || offset > I->uploadSize || len > I->uploadSize - offset )
			{
				replyError( I, seq, WRD_ErrBadRequest );
				return;
			}
			memcpy( I->upload + offset, data, len );
			if ( offset + len > I->uploadReceived )
			{
				I->uploadReceived = offset + len;
			}
			replyOk( I, seq );
			return;
		}

		case WRD_MsgLoadEnd:
		{
			if ( I->uploadDisabled )
			{
				replyError( I, seq, WRD_ErrUploadDisabled );
				return;
			}
			if ( inVm )
			{
				replyError( I, seq, WRD_ErrBusy );
				return;
			}
			if ( !I->upload || I->uploadReceived != I->uploadSize
					|| !wr_isBytecodeValid(I->upload, I->uploadSize) )
			{
				g_free( I->upload );
				I->upload = 0;
				I->uploadSize = 0;
				replyError( I, seq, WRD_ErrBadBytecode );
				return;
			}

			uint8_t* bytes = I->upload;
			uint32_t size = I->uploadSize;
			I->upload = 0;
			I->uploadSize = 0;

			if ( !I->parent->loadBytes(bytes, (int)size, true) )
			{
				replyError( I, seq, WRD_ErrBadBytecode );
				return;
			}
			I->tickFlags |= WRD_TickProgramLoaded;

			WrenchDebugWriter out( payload(I), WRENCH_DEBUG_MAX_PAYLOAD );
			out.u32( I->programHash );
			send( I, WRD_MsgReplyOk, seq, out.size() );
			return;
		}

		case WRD_MsgReadProgram:
		{
			uint8_t section = in.u8();
			uint32_t offset = in.u32();
			uint16_t maxLen = in.u16();
			const uint8_t* base = 0;
			uint32_t len = 0;
			if ( section == WRD_SectionBytecode )
			{
				base = I->bytes;
				len = I->bytesLen;
			}
			else if ( section == WRD_SectionSymbols )
			{
				base = I->symbols;
				len = I->symbolsLen;
			}
			else if ( section == WRD_SectionSource )
			{
				base = I->source;
				len = I->sourceLen;
			}

			if ( in.bad() )
			{
				replyError( I, seq, WRD_ErrBadRequest );
				return;
			}

			uint32_t n = 0;
			if ( base && offset < len )
			{
				n = len - offset;
				if ( n > maxLen )
				{
					n = maxLen;
				}
				if ( n > WRENCH_DEBUG_MAX_PAYLOAD - 6 )
				{
					n = WRENCH_DEBUG_MAX_PAYLOAD - 6;
				}
			}

			WrenchDebugWriter out( payload(I), WRENCH_DEBUG_MAX_PAYLOAD );
			out.u32( len );
			out.u16( (uint16_t)n );
			uint8_t* dest = out.reserve( n );
			for ( uint32_t i = 0; dest && i < n; ++i )
			{
				// the image may live in flash that needs special reads
				dest[i] = (uint8_t)READ_8_FROM_PC( base + offset + i );
			}
			send( I, WRD_MsgReplyOk, seq, out.size() );
			return;
		}

		case WRD_MsgSetBreakpoints:
		{
			uint16_t count = in.u16();
			if ( count > WRENCH_DEBUG_MAX_BREAKPOINTS )
			{
				replyError( I, seq, WRD_ErrTooManyBreakpoints );
				return;
			}

			int32_t lines[WRENCH_DEBUG_MAX_BREAKPOINTS];
			for ( uint16_t i = 0; i < count; ++i )
			{
				lines[i] = in.i32();
			}
			if ( in.bad() )
			{
				replyError( I, seq, WRD_ErrBadRequest );
				return;
			}

			memcpy( I->breakpoints, lines, count * sizeof(int32_t) );
			I->breakpointCount = count;
			replyOk( I, seq );
			return;
		}

		case WRD_MsgAddWatch:
		{
			addWatch( I, seq, in );
			return;
		}

		case WRD_MsgRemoveWatch:
		{
			uint8_t id = in.u8();
			if ( in.bad() || id > WRENCH_DEBUG_MAX_WATCHES )
			{
				replyError( I, seq, WRD_ErrBadRequest );
				return;
			}
			if ( id == 0 )
			{
				clearWatches( I, false, false );
			}
			else
			{
				removeWatch( I, id - 1, false );
			}
			replyOk( I, seq );
			return;
		}

		case WRD_MsgLaunch:
		{
			uint8_t flags = in.u8();
			if ( inVm )
			{
				replyError( I, seq, WRD_ErrBusy );
				return;
			}
			if ( !I->context )
			{
				replyError( I, seq, WRD_ErrNoProgram );
				return;
			}
			if ( (I->context->stopLocation || I->context->yield_pc || I->contextRan) && (!I->ownsContext || !I->bytes) )
			{
				replyError( I, seq, WRD_ErrUnsupported );
				return;
			}
			I->launchFlags = flags;
			I->launchPending = true;
			replyOk( I, seq );
			return;
		}

		case WRD_MsgContinue:
		{
			uint8_t flags = in.remaining() ? in.u8() : 0;
			if ( (flags & WRD_ContinueResumeStep) && I->interruptedStepMode != WRD_StepNone )
			{
				// the client looked at a breakpoint inside a step and let it
				// go: the step carries on
				I->stepMode = I->interruptedStepMode;
				I->stepKey = I->interruptedStepKey;
			}
			else
			{
				I->stepMode = WRD_StepNone;
			}
			I->interruptedStepMode = WRD_StepNone;
			I->resume = stopped;
			replyOk( I, seq );
			return;
		}

		case WRD_MsgPause:
		{
			replyOk( I, seq );
			if ( !stopped )
			{
				I->stepMode = WRD_StepPause;
			}
			else
			{
				// already halted: say so again, the first event may be lost
				announceStop( I );
			}
			return;
		}

		case WRD_MsgStepIn:
		case WRD_MsgStepOver:
		case WRD_MsgStepOut:
		{
			if ( !stopped )
			{
				replyError( I, seq, WRD_ErrNotStopped );
				return;
			}
			I->stepMode = (frame.type == WRD_MsgStepIn) ? WRD_StepIn
				: (frame.type == WRD_MsgStepOver) ? WRD_StepOver : WRD_StepOut;
			I->stepKey = wr_debugDepthKey( I->stopContext, I->stopFrameBase );
			I->interruptedStepMode = WRD_StepNone;
			I->resume = true;
			replyOk( I, seq );
			return;
		}

		case WRD_MsgTerminate:
		{
			I->launchPending = false;
			if ( inVm )
			{
				I->abortPending = true;
				I->resume = stopped;
			}
			replyOk( I, seq );
			return;
		}

		case WRD_MsgDetach:
		{
			I->breakpointCount = 0;
			clearWatches( I, false, false );
			I->stepMode = WRD_StepNone;
			I->resume = stopped;
			replyOk( I, seq );
			I->helloReceived = false;
			return;
		}

		case WRD_MsgGetStack:
		{
			uint16_t first = in.u16();
			uint16_t maxFrames = in.u16();
			if ( !stopped )
			{
				replyError( I, seq, WRD_ErrNotStopped );
				return;
			}

			WrenchDebugWriter out( payload(I), WRENCH_DEBUG_MAX_PAYLOAD );
			uint8_t* totalAt = out.reserve( 2 );
			uint8_t* countAt = out.reserve( 2 );
			uint16_t total = 0;
			uint16_t written = 0;

			WrenchDebugFrameWalker walker( I->stopContext, I->stopPc, I->stopFrameBase );
			WrenchDebugFrameInfo info;
			while ( walker.next(info) && total < 0xFFFF )
			{
				if ( total >= first && written < maxFrames && out.remaining() >= 16 )
				{
					out.u8( info.kind );
					out.u8( info.function );
					out.u32( info.pcOffset );
					out.u16( info.frameBase ? (uint16_t)(info.frameBase - I->stopContext->stack) : 0xFFFF );
					out.u16( (info.kind == WRD_FrameFunction)
									? (uint16_t)(I->stopContext->localFunctions[info.function].frameBaseAdjustment - 1) : 0 );
					++written;
				}
				++total;
			}

			totalAt[0] = (uint8_t)total;
			totalAt[1] = (uint8_t)(total >> 8);
			countAt[0] = (uint8_t)written;
			countAt[1] = (uint8_t)(written >> 8);
			send( I, WRD_MsgReplyOk, seq, out.size() );
			return;
		}

		case WRD_MsgGetLocals:
		{
			uint16_t frameIndex = in.u16();
			uint16_t first = in.u16();
			uint16_t count = in.u16();
			WrenchDebugFrameInfo info;
			if ( !stopped )
			{
				replyError( I, seq, WRD_ErrNotStopped );
				return;
			}
			if ( in.bad() || !frameAt(I, frameIndex, info) )
			{
				replyError( I, seq, WRD_ErrBadFrame );
				return;
			}

			uint16_t slots = (info.kind == WRD_FrameFunction)
											? (uint16_t)(I->stopContext->localFunctions[info.function].frameBaseAdjustment - 1) : 0;

			WrenchDebugWriter out( payload(I), WRENCH_DEBUG_MAX_PAYLOAD );
			out.u16( slots );
			uint8_t* countAt = out.reserve( 2 );
			uint16_t written = 0;
			for ( uint32_t s = first; s < slots && written < count; ++s )
			{
				uint32_t mark = out.size();
				writeValue( I, out, info.frameBase[s] );
				if ( out.overflow() )
				{
					out.truncate( mark );
					break;
				}
				++written;
			}
			countAt[0] = (uint8_t)written;
			countAt[1] = (uint8_t)(written >> 8);
			send( I, WRD_MsgReplyOk, seq, out.size() );
			return;
		}

		case WRD_MsgGetGlobals:
		{
			uint16_t first = in.u16();
			uint16_t count = in.u16();
			WRContext* context = stopped ? I->stopContext : I->context;
			if ( in.bad() )
			{
				replyError( I, seq, WRD_ErrBadRequest );
				return;
			}
			if ( !context || (I->state == WRD_StateRunning) )
			{
				replyError( I, seq, context ? WRD_ErrNotStopped : WRD_ErrNoProgram );
				return;
			}

			const WRValue* globals = (const WRValue*)(context + 1);
			WrenchDebugWriter out( payload(I), WRENCH_DEBUG_MAX_PAYLOAD );
			out.u16( context->globals );
			uint8_t* countAt = out.reserve( 2 );
			uint16_t written = 0;
			for ( uint32_t s = first; s < context->globals && written < count; ++s )
			{
				uint32_t mark = out.size();
				writeValue( I, out, globals[s] );
				if ( out.overflow() )
				{
					out.truncate( mark );
					break;
				}
				++written;
			}
			countAt[0] = (uint8_t)written;
			countAt[1] = (uint8_t)(written >> 8);
			send( I, WRD_MsgReplyOk, seq, out.size() );
			return;
		}

		case WRD_MsgGetChildren:
		{
			if ( I->state == WRD_StateRunning )
			{
				replyError( I, seq, WRD_ErrNotStopped );
				return;
			}
			getChildren( I, seq, in );
			return;
		}

		case WRD_MsgSetLocal:
		case WRD_MsgSetGlobal:
		case WRD_MsgSetChild:
		{
			WRValue* slot = 0;
			WRContext* context = stopped ? I->stopContext : I->context;
			if ( I->state == WRD_StateRunning || !context )
			{
				replyError( I, seq, context ? WRD_ErrNotStopped : WRD_ErrNoProgram );
				return;
			}

			if ( frame.type == WRD_MsgSetLocal )
			{
				uint16_t frameIndex = in.u16();
				uint16_t index = in.u16();
				WrenchDebugFrameInfo info;
				if ( !frameAt(I, frameIndex, info) || info.kind != WRD_FrameFunction )
				{
					replyError( I, seq, WRD_ErrBadFrame );
					return;
				}
				if ( index >= I->stopContext->localFunctions[info.function].frameBaseAdjustment - 1 )
				{
					replyError( I, seq, WRD_ErrBadSlot );
					return;
				}
				slot = info.frameBase + index;
			}
			else if ( frame.type == WRD_MsgSetGlobal )
			{
				uint16_t index = in.u16();
				if ( index >= context->globals )
				{
					replyError( I, seq, WRD_ErrBadSlot );
					return;
				}
				slot = ((WRValue*)(context + 1)) + index;
			}
			else
			{
				uint32_t handle = in.u32();
				uint32_t index = in.u32();
				WrenchDebugHandle* h = findHandle( I, handle );
				if ( !h )
				{
					replyError( I, seq, WRD_ErrBadHandle );
					return;
				}
				slot = childSlot( I, h, index );
				if ( !slot )
				{
					replyError( I, seq, WRD_ErrBadSlot );
					return;
				}
			}

			WRValue* target = storageOf( slot );
			if ( !target )
			{
				replyError( I, seq, WRD_ErrUnsupported );
				return;
			}

			WRValue value;
			if ( !readValue(I, in, &value, context) )
			{
				replyError( I, seq, WRD_ErrBadRequest );
				return;
			}
			*target = value;

			WrenchDebugWriter out( payload(I), WRENCH_DEBUG_MAX_PAYLOAD );
			writeValue( I, out, *target );
			send( I, WRD_MsgReplyOk, seq, out.size() );
			return;
		}

		default:
		{
			replyError( I, seq, WRD_ErrUnknownRequest );
			return;
		}
	}
}

WrenchDebugServer::WrenchDebugServer( WRState* w, WrenchDebugTransport* transport )
{
	I = (WrenchDebugServerPrivate*)g_malloc( sizeof(WrenchDebugServerPrivate) );
	if ( !I )
	{
		return;
	}
	memset( (char*)I, 0, sizeof(WrenchDebugServerPrivate) );
	I->parent = this;
	I->w = w;
	I->transport = transport;
	WrenchDebugServerImpl::init( I );
}

WrenchDebugServer::~WrenchDebugServer()
{
	if ( !I )
	{
		return;
	}
	WrenchDebugServerImpl::releaseProgram( I );
	g_free( I->upload );
	g_free( I );
}

int WrenchDebugServer::tick()
{
	if ( !I )
	{
		return 0;
	}

	if ( I->state == WRD_StateRunning && !I->inLaunch )
	{
		// host driven code left the VM since we last looked, anything handed
		// out before may have been changed or collected meanwhile
		I->state = WRD_StateLoaded;
		WrenchDebugServerImpl::resetHandles( I );
	}

	I->stopContext = 0;
	WrenchDebugServerImpl::pump( I );

	if ( I->launchPending )
	{
		WrenchDebugServerImpl::runLaunch( I );
	}

	int flags = I->tickFlags;
	I->tickFlags = 0;
	return flags;
}

WRContext* WrenchDebugServer::loadBytes( const uint8_t* bytes, const int len, const bool takeOwnership )
{
	if ( !I )
	{
		return 0;
	}

	WrenchDebugServerImpl::releaseProgram( I );

	if ( !bytes || len < 8 )
	{
		if ( takeOwnership )
		{
			g_free( (void*)bytes );
		}
		return 0;
	}

	I->bytes = bytes;
	I->bytesLen = (uint32_t)len;
	I->ownsBytes = takeOwnership;

	I->context = wr_newContext( I->w, bytes, len );
	if ( !I->context )
	{
		WrenchDebugServerImpl::releaseProgram( I );
		return 0;
	}

	I->ownsContext = true;
	I->context->debugServer = this;
	WrenchDebugServerImpl::readProgramInfo( I );
	I->state = WRD_StateLoaded;
	I->lastLine = -1;

	if ( I->helloReceived )
	{
		WrenchDebugWriter out( WrenchDebugServerImpl::payload(I), WRENCH_DEBUG_MAX_PAYLOAD );
		out.u32( I->programHash );
		WrenchDebugServerImpl::send( I, WRD_MsgEventLoaded, 0, out.size() );
	}

	return I->context;
}

void WrenchDebugServer::attach( WRContext* context )
{
	if ( !I )
	{
		return;
	}

	WrenchDebugServerImpl::releaseProgram( I );
	if ( !context )
	{
		return;
	}

	I->context = context;
	I->ownsContext = false;
	I->bytes = context->bottom;
	I->bytesLen = (uint32_t)context->bottomSize;
	I->ownsBytes = false;
	context->debugServer = this;
	WrenchDebugServerImpl::readProgramInfo( I );
	I->state = WRD_StateLoaded;
}

void WrenchDebugServer::detach()
{
	if ( I )
	{
		WrenchDebugServerImpl::releaseProgram( I );
	}
}

WRContext* WrenchDebugServer::context() const
{
	return I ? I->context : 0;
}

bool WrenchDebugServer::ready() const
{
	return I && I->context && I->context->stopLocation && !I->context->yield_pc && !I->inLaunch;
}

void WrenchDebugServer::setHostDriven( const bool hostDriven )
{
	if ( I )
	{
		I->hostDriven = hostDriven;
	}
}

void WrenchDebugServer::breakOnEntry( const uint32_t waitMs )
{
	if ( I )
	{
		I->entryBreak = true;
		I->entryWaitMs = waitMs;
	}
}

void WrenchDebugServer::setLinkTimeout( const uint32_t ms )
{
	if ( I )
	{
		I->linkTimeoutMs = ms;
	}
}

void WrenchDebugServer::setAllowUpload( const bool allow )
{
	if ( !I )
	{
		return;
	}
	I->uploadDisabled = !allow;
	if ( !allow )
	{
		// drop a transfer in progress
		g_free( I->upload );
		I->upload = 0;
		I->uploadSize = 0;
		I->uploadReceived = 0;
	}
}

bool WrenchDebugServer::clientAttached() const
{
	return I && I->helloReceived;
}

void WrenchDebugServer::setAbortOnDisconnect( const bool abort )
{
	if ( I )
	{
		I->abortOnDisconnect = abort;
	}
}

void WrenchDebugServer::print( const char* text, const int len )
{
	if ( !I || !I->helloReceived || !text )
	{
		return;
	}

	uint32_t remaining = (len < 0) ? (uint32_t)strlen( text ) : (uint32_t)len;
	while ( remaining )
	{
		uint32_t n = remaining > WRENCH_DEBUG_MAX_PAYLOAD ? WRENCH_DEBUG_MAX_PAYLOAD : remaining;
		memcpy( WrenchDebugServerImpl::payload(I), text, n );
		WrenchDebugServerImpl::send( I, WRD_MsgEventOutput, 0, n );
		text += n;
		remaining -= n;
	}
}

bool WrenchDebugServer::onDebugInfo( WRContext* context, const unsigned char* pc, WRValue* frameBase, WRValue* stackTop )
{
	uint16_t codeword = (uint16_t)READ_16_FROM_PC( pc );
	if ( (codeword & WRD_TypeMask) == WRD_LineHigh )
	{
		I->lineHigh = (int32_t)(codeword & WRD_PayloadMask) << 14;
		return false;
	}
	if ( (codeword & WRD_TypeMask) != WRD_LineNumber )
	{
		return false;
	}

	int32_t line = (codeword & WRD_PayloadMask) | I->lineHigh;
	I->lineHigh = 0;
	I->lastLine = line;
	if ( I->state != WRD_StateStopped && I->state != WRD_StateRunning )
	{
		// script code runs again (firmware calling in): values handed out
		// while it was idle may be changed or collected from here on
		WrenchDebugServerImpl::resetHandles( I );
		I->state = WRD_StateRunning;
		I->contextRan = true;
	}

	uint8_t reason = WRD_StopStep;
	bool stop = false;

	// a watched value changed since the previous line marker: stop on the
	// line after the one that wrote it
	if ( I->watchCount && context == I->context && I->helloReceived )
	{
		uint8_t hit = WrenchDebugServerImpl::checkWatches( I, context, pc, frameBase );
		if ( hit )
		{
			stop = true;
			reason = WRD_StopWatch;
			I->watchHit = hit;
		}
	}

	if ( !stop && I->stepMode != WRD_StepNone )
	{
		uint32_t key = wr_debugDepthKey( context, frameBase );
		switch ( I->stepMode )
		{
			case WRD_StepIn: stop = true; break;
			case WRD_StepOver: stop = key <= I->stepKey; break;
			case WRD_StepOut: stop = key < I->stepKey; break;
			case WRD_StepPause: stop = true; reason = WRD_StopPause; break;
			case WRD_StepEntry: stop = true; reason = WRD_StopEntry; break;
			default: break;
		}
	}

	if ( !stop )
	{
		for ( uint16_t b = 0; b < I->breakpointCount; ++b )
		{
			if ( I->breakpoints[b] == line )
			{
				stop = true;
				reason = WRD_StopBreakpoint;
				break;
			}
		}
	}

	if ( I->entryBreak )
	{
		// breakOnEntry(): halt here, first waiting for a debugger if needed
		I->entryBreak = false;
		if ( I->helloReceived || WrenchDebugServerImpl::waitForClient(I, I->entryWaitMs) )
		{
			stop = true;
			reason = WRD_StopEntry;
		}
	}

	if ( !stop && ++I->pollCounter >= WRENCH_DEBUG_POLL_INTERVAL )
	{
		I->pollCounter = 0;
		I->inHook = true;
		WrenchDebugServerImpl::pump( I );
		I->inHook = false;
		if ( I->stepMode == WRD_StepPause )
		{
			stop = true;
			reason = WRD_StopPause;
		}
	}

	if ( I->abortPending )
	{
		I->abortPending = false;
		return true;
	}

	if ( !stop || !I->helloReceived )
	{
		if ( stop && reason != WRD_StopBreakpoint )
		{
			I->stepMode = WRD_StepNone;
		}
		return false;
	}

	// a breakpoint or watch inside a step over/out: remember the step, the
	// client may decide (condition, hit count, logpoint) not to stop here
	// after all
	if ( (reason == WRD_StopBreakpoint || reason == WRD_StopWatch) && (I->stepMode == WRD_StepOver || I->stepMode == WRD_StepOut) )
	{
		I->interruptedStepMode = I->stepMode;
		I->interruptedStepKey = I->stepKey;
	}
	else
	{
		I->interruptedStepMode = WRD_StepNone;
	}

	I->stopContext = context;
	I->stopPc = pc;
	I->stopFrameBase = frameBase;
	I->stopStackTop = stackTop;

	bool abort = WrenchDebugServerImpl::stopLoop( I, reason, 0 );

	I->stopPc = 0;
	return abort;
}

void WrenchDebugServer::onError( WRContext* context, const unsigned char* pc, WRValue* frameBase, WRValue* stackTop )
{
	if ( !I || !I->helloReceived || I->state == WRD_StateStopped )
	{
		return;
	}

	uint8_t err = I->w->err;
	I->stopContext = context;
	I->stopPc = pc;
	I->stopFrameBase = frameBase;
	I->stopStackTop = stackTop;

	WrenchDebugServerImpl::stopLoop( I, WRD_StopError, err );

	I->stopPc = 0;
	I->stepMode = WRD_StepNone;
	I->w->err = err; // the VM is failing either way
}

#endif
