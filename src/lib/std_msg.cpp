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

// The mailbox (WRState::mailbox) holds ints, floats and strings under the
// hash of their key. A string is a copy that belongs to the state, not to
// the context that wrote it: an SV_CHAR object of its own, linked into the
// chain of blocks of the mailbox so wr_destroyState() frees whatever is
// still stored. Overwriting or removing a message frees its string at once.

//------------------------------------------------------------------------------
// a copy of 'len' bytes, null terminated for the host (wr_msgPeek)
static WRGCObject* wr_msgNewString( WRState* w, const char* data, const unsigned int len )
{
	WRGCObject* s = (WRGCObject*)g_malloc( sizeof(WRGCObject) );
	if ( !s )
	{
		return 0;
	}

	memset( (unsigned char*)s, 0, sizeof(WRGCObject) );
	if ( !s->init(len + 1, SV_CHAR, false) )
	{
		g_free( s );
		return 0;
	}

	memcpy( s->m_Cdata, data, len );
	s->m_Cdata[len] = 0;
	s->m_size = len;

	s->m_nextGC = w->mailbox.m_nextGC;
	w->mailbox.m_nextGC = s;
	return s;
}

//------------------------------------------------------------------------------
// frees what the slot owns and leaves it an int 0
static void wr_msgFreeSlot( WRState* w, WRValue* slot )
{
	if ( slot->xtype == WR_EX_ARRAY )
	{
		for( WRGCBase** link = &w->mailbox.m_nextGC; *link; link = &((*link)->m_nextGC) )
		{
			if ( *link == slot->va )
			{
				*link = slot->va->m_nextGC;
				break;
			}
		}

		slot->va->clear();
		g_free( slot->va );
	}

	slot->init();
}

//------------------------------------------------------------------------------
// stores an int, a float or a string (copied), 0 on success, -1 for any
// other value (containers, structs, raw arrays) or when out of memory; the
// mailbox is unchanged then
static int wr_msgStore( WRState* w, const uint32_t hash, const WRValue& value )
{
	const WRValue& v = value.deref();

	WRGCObject* str = 0;
	if ( v.xtype == WR_EX_ARRAY && v.va->m_type == SV_CHAR )
	{
		if ( !(str = wr_msgNewString(w, v.va->m_SCdata, v.va->m_size)) )
		{
			return -1;
		}
	}
	else if ( v.type != WR_INT && v.type != WR_FLOAT )
	{
		return -1;
	}

	// read the value before the slot is touched: it may be the slot itself
	const uint32_t bits = v.ui;
	const uint32_t type = v.type;

	WRValue* slot = w->mailbox.getAsRawValueHashTable( hash );
	wr_msgFreeSlot( w, slot );

	if ( str )
	{
		slot->va = str;
		slot->p2 = INIT_AS_ARRAY;
	}
	else
	{
		slot->ui = bits;
		slot->p2 = (type == WR_FLOAT) ? INIT_AS_FLOAT : INIT_AS_INT;
	}

	return 0;
}

//------------------------------------------------------------------------------
// the message as a value of 'c', a string is copied into a new string of
// 'c'; false when out of memory (out is an int 0 then)
static bool wr_msgLoad( WRContext* c, const WRValue* slot, WRValue& out )
{
	if ( slot->xtype != WR_EX_ARRAY )
	{
		out = *slot;
		return true;
	}

	WRGCObject* str = c->getSVA( slot->va->m_size, SV_CHAR, false );
	if ( !str )
	{
		out.init();
		return false;
	}

	memcpy( str->m_Cdata, slot->va->m_Cdata, slot->va->m_size );
	out.va = str;
	out.p2 = INIT_AS_ARRAY;
	return true;
}

//------------------------------------------------------------------------------
// removes the message, false when there was none
static bool wr_msgRemove( WRState* w, const uint32_t hash )
{
	WRValue* slot = w->mailbox.exists( hash, false );
	if ( !slot )
	{
		return false;
	}

	wr_msgFreeSlot( w, slot );
	w->mailbox.exists( hash, true );
	return true;
}

//------------------------------------------------------------------------------
bool wr_msgWrite( WRState* w, const char* key, const WRValue& value )
{
	return wr_msgStore( w, wr_hashStr(key), value ) == 0;
}

//------------------------------------------------------------------------------
bool wr_msgWriteString( WRState* w, const char* key, const char* data, const int len )
{
	WRGCObject* str = wr_msgNewString( w, data, (len < 0) ? (unsigned int)strlen(data) : (unsigned int)len );
	if ( !str )
	{
		return false;
	}

	WRValue* slot = w->mailbox.getAsRawValueHashTable( wr_hashStr(key) );
	wr_msgFreeSlot( w, slot );
	slot->va = str;
	slot->p2 = INIT_AS_ARRAY;
	return true;
}

//------------------------------------------------------------------------------
const WRValue* wr_msgPeek( WRState* w, const char* key )
{
	return w->mailbox.exists( wr_hashStr(key), false );
}

//------------------------------------------------------------------------------
bool wr_msgRead( WRContext* context, const char* key, WRValue& value, const bool clear )
{
	const uint32_t hash = wr_hashStr( key );
	WRValue* slot = context->w->mailbox.exists( hash, false );
	if ( !slot )
	{
		value.init();
		return false;
	}

	const bool loaded = wr_msgLoad( context, slot, value );
	if ( clear )
	{
		wr_msgRemove( context->w, hash );
	}

	return loaded;
}

//------------------------------------------------------------------------------
bool wr_msgClear( WRState* w, const char* key )
{
	return wr_msgRemove( w, wr_hashStr(key) );
}

//------------------------------------------------------------------------------
// key   : anything hashable
// clear : [OPTIONAL] default 0/'false'
//
// returns : the value as it was written (a string as a new copy), 0 when
//           there is none
void wr_mboxRead( WRValue* stackTop, const int argn, WRContext* c )
{
	stackTop->init();

	if ( argn > 0 )
	{
		WRValue* args = stackTop - argn;
		const uint32_t hash = args[0].getHash();
		const int clear = (argn > 1) ? args[1].asInt() : 0;

		// read first, removing clears the slot
		WRValue* msg = c->w->mailbox.exists( hash, false );
		if ( msg )
		{
			wr_msgLoad( c, msg, *stackTop );
			if ( clear )
			{
				wr_msgRemove( c->w, hash );
			}
		}
	}
}

//------------------------------------------------------------------------------
// key  : anything hashable
// value: an int, a float or a string
//
// returns : 0 when stored, -1 when the value cannot be stored (a container
//           or struct) or a value is missing
void wr_mboxWrite( WRValue* stackTop, const int argn, WRContext* c )
{
	int result = -1;
	if ( argn > 1 )
	{
		WRValue* args = stackTop - argn;
		result = wr_msgStore( c->w, args[0].getHash(), args[1] );
	}

	stackTop->init();
	stackTop->i = result;
}

//------------------------------------------------------------------------------
// key  : anything hashable
void wr_mboxClear( WRValue* stackTop, const int argn, WRContext* c )
{
	if ( argn > 0 )
	{
		wr_msgRemove( c->w, (stackTop - argn)->getHash() );
	}
}

//------------------------------------------------------------------------------
// key  : anything hashable
//
// return : true/false
void wr_mboxPeek( WRValue* stackTop, const int argn, WRContext* c )
{
	if ( argn > 0
		&& c->w->mailbox.exists((stackTop - argn)->getHash(), false) )
	{
		stackTop->i = 1;
	}
}

//------------------------------------------------------------------------------
void wr_loadMessageLib( WRState* w )
{
	wr_registerLibraryFunction( w, "msg::read", wr_mboxRead ); // read (true to clear, false to leave)
	wr_registerLibraryFunction( w, "msg::write", wr_mboxWrite ); // write message
	wr_registerLibraryFunction( w, "msg::clear", wr_mboxClear ); // remove if exists
	wr_registerLibraryFunction( w, "msg::peek", wr_mboxPeek ); // does the message exist?
}
