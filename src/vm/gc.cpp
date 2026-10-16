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

//------------------------------------------------------------------------------
void WRContext::markBase( WRGCBase* svb )
{
	// mark before descending so a container that (indirectly) holds itself
	// does not recurse forever
	svb->m_flags |= GCFlag_Marked;

	if ( svb->m_type == SV_VALUE )
	{
		WRValue* top = ((WRGCObject*)svb)->m_Vdata + ((WRGCObject*)svb)->m_size;

		for( WRValue* V = ((WRGCObject*)svb)->m_Vdata; V<top; ++V )
		{
			mark( V );
		}
	}
	else if ( svb->m_type == SV_HASH_TABLE )
	{
		// hash table points one WRGCBase size PAST the actual pointer,
		// recover it and mark it
		((WRGCBase*)(((WRGCObject*)svb)->m_Vdata) - 1)->m_flags |= GCFlag_Marked;

		for( uint32_t i=0; i<((WRGCObject*)svb)->m_mod; ++i )
		{
			if ( ((WRGCObject*)svb)->m_hashTable[i] != WRENCH_NULL_HASH )
			{
				uint32_t item = i<<1;
				mark( ((WRGCObject*)svb)->m_Vdata + item++ );
				mark( ((WRGCObject*)svb)->m_Vdata + item );
			}
		}
	}
	else if ( svb->m_type == SV_HASH_ENTRY )
	{
		// mark the referenced table so it is not collected
		((WRGCBase*)svb)->m_referencedTable->m_flags |= GCFlag_Marked;
	}

	svb->m_flags |= GCFlag_Marked;
}

//------------------------------------------------------------------------------
void WRContext::mark( WRValue* s )
{
	if ( IS_CONTAINER_MEMBER(s->xtype) )
	{
		// An array member points at the WRValue holding the array, a hash
		// table member straight at the table's storage block. A raw array
		// (host memory, nothing to keep alive) is recognized first: its
		// value's type byte reads WR_EX, while the same bytes of a storage
		// block are the low byte of an aligned pointer (never 3). Only then
		// may the first byte be read as the block type, a raw array's
		// first byte is part of an arbitrary host pointer
		if ( s->r->type == WR_EX && IS_RAW_ARRAY(s->r->xtype) )
		{
			return;
		}

		// a hash table member: keep that exact block alive; if the table
		// grew since, it is a superseded block whose entries forward to the
		// new one. (an array's WRValue starts with an aligned pointer, so
		// it never reads as SV_HASH_INTERNAL)
		if ( s->vb->m_type == SV_HASH_INTERNAL )
		{
			for( WRGCBase* block = s->vb; block && !(block->m_flags & GCFlag_Marked); block = block->m_referencedTable )
			{
				block->m_flags |= GCFlag_Marked;
			}
		}
		else if ( IS_EXARRAY_TYPE(s->r->xtype) )
		{
			// we don't mark this type, but we might mark it's target
			mark( s->r );
		}
		return;
	}

	if ( !IS_EXARRAY_TYPE(s->xtype) || (s->va->m_flags & GCFlag_Marked) )
	{
		return;
	}

	assert( !IS_RAW_ARRAY(s->xtype) );

	markBase( s->vb );
}

// free everything in the list that was not marked, clear the marks of the rest
static void wr_sweepGC( WRGCBase** list )
{
	WRGCBase* current = *list;
	WRGCBase* prev = 0;
	while( current )
	{
		// if set, clear it
		if ( current->m_flags & GCFlag_Marked )
		{
			current->m_flags &= ~GCFlag_Marked;
			prev = current;
			current = current->m_nextGC;
		}
		// otherwise free it as unreferenced
		else
		{
			current->clear();

			if ( prev == 0 )
			{
				*list = current->m_nextGC;
				g_free( current );
				current = *list;
			}
			else
			{
				prev->m_nextGC = current->m_nextGC;
				g_free( current );
				current = prev->m_nextGC;
			}
		}
	}
}

//------------------------------------------------------------------------------
void WRContext::gc( WRValue* stackTop )
{
	if ( allocatedMemoryHint < w->allocatedMemoryLimit )
	{
		return;
	}

	allocatedMemoryHint = 0;

	if ( !stackTop )
	{
		// zero stacktop means collect EVERYTHING this context owns that its
		// globals do not reach (wr_destroyContext zeroes them first)
		WRValue* globalSpace = (WRValue *)(this + 1); // globals are allocated directly after this context
		for( unsigned int i=0; i<globals; ++i, ++globalSpace )
		{
			mark( globalSpace );
		}
		wr_sweepGC( &svAllocated );
		return;
	}

	// a context and the contexts it imported share one stack and hand
	// objects to each other, so they are collected as one group: mark from
	// the whole shared stack and every member's globals and permanent
	// objects, then sweep every member (which also clears every mark set)
	WRContext* member = this;
	int guard = 0;
	do
	{
		for( WRGCBase* a=member->svAllocated; a; a = a->m_nextGC )
		{
			if ( (a->m_flags & GCFlag_Perm) && !(a->m_flags & GCFlag_Marked) )
			{
				markBase( a );
			}
		}

		WRValue* globalSpace = (WRValue *)(member + 1);
		for( unsigned int i=0; i<member->globals; ++i, ++globalSpace )
		{
			mark( globalSpace );
		}

		member = member->imported;

	} while( member && member != this && ++guard < 256 );

	// everything below the top belongs to the group, including frames of a
	// call that re-entered this context from native code
	for( WRValue* s=stack; s<stackTop; ++s )
	{
		mark( s );
	}

	member = this;
	guard = 0;
	do
	{
		wr_sweepGC( &member->svAllocated );
		member->allocatedMemoryHint = 0;
		member = member->imported;

	} while( member && member != this && ++guard < 256 );
}

//------------------------------------------------------------------------------
WRGCObject* WRContext::getSVA( int size, WRGCObjectType type, bool init )
{
#ifdef WRENCH_HANDLE_MALLOC_FAIL
	if ( g_mallocFailed )
	{
		return 0;
	}
#endif

	WRGCObject* ret = (WRGCObject*)g_malloc( sizeof(WRGCObject) );

#ifdef WRENCH_HANDLE_MALLOC_FAIL
	if ( !ret )
	{
		g_mallocFailed = true;
		return 0;
	}
#endif

	memset( (unsigned char*)ret, 0, sizeof(WRGCObject) );
	int allocated = ret->init( size, type, init );

#ifdef WRENCH_HANDLE_MALLOC_FAIL
	if ( g_mallocFailed )
	{
		if ( ret->m_data )
		{
			ret->clear();
		}
		g_free( ret );
		return 0;
	}
#endif

	// a hash table already linked its storage block behind itself in
	// init(), keep that chain intact
	WRGCBase* last = ret;
	while( last->m_nextGC )
	{
		last = last->m_nextGC;
	}
	last->m_nextGC = svAllocated;
	svAllocated = ret;

	allocatedMemoryHint += allocated + sizeof(WRGCObject);

	if ( (int)type >= SV_VALUE )
	{
		ret->m_creatorContext = this;
	}

	return ret;
}
