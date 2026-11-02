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

// Explicit two's-complement wrap semantics for integer negate.
inline int32_t wr_ineg_wrap( const int32_t a ) { return (int32_t)(0u - (uint32_t)a); }

//------------------------------------------------------------------------------
bool wr_concatStringCheck( WRValue* to, WRValue* from, WRValue* target )
{
	const int c_bufSize = 20;
	char buf[c_bufSize + 1];
	char* str = buf;
	unsigned int len;

	if ( IS_ARRAY(to->xtype) && to->va->m_type == SV_CHAR && !(to->va->m_flags & GCFlag_NoContext) )
	{
		if ( IS_ARRAY(from->xtype) && from->va->m_type == SV_CHAR )
		{
			str = from->va->m_SCdata;
			len = from->va->m_size;
		}
		else
		{
			from->asString( buf, c_bufSize, &len );
		}

		WRGCObject* cat = to->va->m_creatorContext->getSVA(to->va->m_size + len, SV_CHAR, false);

#ifdef WRENCH_HANDLE_MALLOC_FAIL
		if ( !cat )
		{
			target->p2 = INIT_AS_INT;
			return false;
		}
#endif

		memcpy( cat->m_SCdata, to->va->m_SCdata, to->va->m_size );
		memcpy( cat->m_SCdata + to->va->m_size, str, len );

		target->p2 = INIT_AS_ARRAY;
		target->va = cat;
	}
	else if ( IS_ARRAY(from->xtype) && from->va->m_type == SV_CHAR && !(from->va->m_flags & GCFlag_NoContext) )
	{
		to->asString( buf, c_bufSize, &len );

		WRGCObject* cat = from->va->m_creatorContext->getSVA(from->va->m_size + len, SV_CHAR, false);

#ifdef WRENCH_HANDLE_MALLOC_FAIL
		if ( !cat )
		{
			target->p2 = INIT_AS_INT;
			return false;
		}
#endif

		memcpy( cat->m_SCdata, str, len );
		memcpy( cat->m_SCdata + len, from->va->m_SCdata, from->va->m_size );

		target->p2 = INIT_AS_ARRAY;
		target->va = cat;
	}
	else
	{
		return false;
	}

	return true;
}

//------------------------------------------------------------------------------
// += where 'to' is an element of a container (h["k"] += "b") and either side
// a string: the sum goes into the element, 'to' only points at it. False
// when it is not that case, the numeric path applies then
bool wr_concatStringToElement( WRValue* to, WRValue* from )
{
	if ( !IS_CONTAINER_MEMBER(to->xtype) )
	{
		return false;
	}

	// copies: deref() can hand back the element itself, which receives the sum
	WRValue V = to->deref();
	WRValue F = from->deref();
	if ( !wr_concatStringCheck(&V, &F, &V) )
	{
		return false;
	}

	wr_valueToEx( to, &V );
	*from = V;
	return true;
}

//------------------------------------------------------------------------------
static bool wr_isString( const WRValue& V )
{
	return V.xtype == WR_EX_ARRAY && V.va->m_type == SV_CHAR;
}

//------------------------------------------------------------------------------
// the order of two strings, by their bytes (a prefix comes first): <0, 0 or
// >0 in "order". False when they are not both strings
bool wr_stringOrder( const WRValue* a, const WRValue* b, int* order )
{
	// one at a time: deref() may hand out a scratch value (never a string)
	if ( !wr_isString(a->deref()) )
	{
		return false;
	}
	const WRGCObject* A = a->deref().va;
	const WRValue& B = b->deref();
	if ( !wr_isString(B) )
	{
		return false;
	}

	uint32_t len = (A->m_size < B.va->m_size) ? A->m_size : B.va->m_size;
	int diff = len ? memcmp( A->m_Cdata, B.va->m_Cdata, len ) : 0;
	*order = diff ? diff : ((A->m_size < B.va->m_size) ? -1 : (A->m_size > B.va->m_size ? 1 : 0));
	return true;
}

//------------------------------------------------------------------------------
// whether the value (or the variable or element it refers to) is an int or
// a float: a number is never equal to a string, an array, a hash table or a
// struct, whose hash it might happen to be
bool wr_holdsNumber( const WRValue* value )
{
	return value->deref().type <= WR_FLOAT;
}

//------------------------------------------------------------------------------
bool wr_growValueArray( WRGCObject* va, uint32_t newMinIndex )
{
	const size_t size_of = (va->m_type == SV_CHAR) ? 1 : sizeof(WRValue);

	if ( newMinIndex < va->m_size )
	{
		return true;
	}

	// increase size to accommodate new element
	const size_t size_el = va->m_size * size_of;
	const size_t elements = (size_t)newMinIndex + 1;

	if ( (newMinIndex == (uint32_t)-1)
			|| (elements > ((size_t)-1) / size_of)
			|| (va->m_type == SV_VALUE && newMinIndex >= WRENCH_MAX_ARRAY_ELEMENTS) )
	{
#ifdef WRENCH_HANDLE_MALLOC_FAIL
		g_mallocFailed = true;
#endif
		return false;
	}

	// an earlier growth left room behind m_size: use it, cleared (it may
	// hold elements that were removed since)
	if ( (va->m_flags & GCFlag_Slack) && elements <= (size_t)va->m_size + va->m_mod )
	{
		memset( va->m_Cdata + size_el, 0, (elements * size_of) - size_el );
		va->m_mod = (uint16_t)(va->m_mod - (elements - va->m_size));
		va->m_size = (uint32_t)elements;
		return true;
	}

	// grow by about half again, so appending in a loop is not quadratic; the
	// spare count lives in m_mod, which a struct uses for its member table:
	// an object with m_mod in use and no slack flag grows exactly
	bool slack = (va->m_flags & GCFlag_Slack) || va->m_mod == 0;
	size_t allocate = elements;
	if ( slack )
	{
		size_t extra = elements / 2;
		if ( extra > 0xFFFF )
		{
			extra = 0xFFFF;
		}
		if ( va->m_type == SV_VALUE && elements + extra > WRENCH_MAX_ARRAY_ELEMENTS )
		{
			extra = WRENCH_MAX_ARRAY_ELEMENTS - elements;
		}
		if ( elements + extra > ((size_t)-1) / size_of )
		{
			extra = 0;
		}
		allocate = elements + extra;
	}

	// create new array to hold the data, and g_free the existing one
	uint8_t* grown = (uint8_t *)g_malloc( allocate * size_of );
	if ( !grown )
	{
#ifdef WRENCH_HANDLE_MALLOC_FAIL
		g_mallocFailed = true;
#endif
		return false;
	}

	if ( size_el )
	{
		memcpy( grown, va->m_Cdata, size_el );
	}
	g_free( va->m_Cdata );
	va->m_Cdata = grown;

	va->m_size = (uint32_t)elements;
	if ( slack )
	{
		va->m_mod = (uint16_t)(allocate - elements);
		va->m_flags |= GCFlag_Slack;
	}

	// clear the new entries and the spare room
	memset( va->m_Cdata + size_el, 0, (allocate * size_of) - size_el );

	return true;
}

// scratch values singleValue() and deref() return: per thread (see
// WRENCH_THREAD_LOCAL), held as raw words since a thread local object must
// not need a constructor, which WRValue has
static WRENCH_THREAD_LOCAL uintptr_t s_temp1Words[2];
static WRENCH_THREAD_LOCAL uintptr_t s_temp2Words[2];

//------------------------------------------------------------------------------
WRValue& WRValue::singleValue() const
{
	(void)sizeof( char[(sizeof(WRValue) == sizeof(s_temp1Words)) ? 1 : -1] );
	WRValue& s_temp1 = *(WRValue*)s_temp1Words;
	if ( (s_temp1 = deref()).type > WR_FLOAT )
	{
		s_temp1.ui = s_temp1.getHash();
		s_temp1.p2 = INIT_AS_INT;
	}

	return s_temp1;
}

//------------------------------------------------------------------------------
WRValue& WRValue::deref() const
{
	if ( type == WR_REF )
	{
		return r->deref();
	}

	if (!IS_CONTAINER_MEMBER(xtype))
	{
		return const_cast<WRValue&>(*this);
	}

	WRValue& s_temp2 = *(WRValue*)s_temp2Words;
	s_temp2.p2 = INIT_AS_INT;
	unsigned int s = DECODE_ARRAY_ELEMENT_FROM_P2(p2);

	if (IS_RAW_ARRAY_TARGET(r))
	{
		s_temp2.ui =(s < (uint32_t)(EX_RAW_ARRAY_SIZE_FROM_P2(r->p2))) ? (uint32_t)(unsigned char)(r->c[s]) : 0;
	}
	else if (vb->m_type == SV_HASH_INTERNAL)
	{
		return ((WRValue*)(vb + 1))[s].deref();
	}
	else if (s < r->va->m_size)
	{
		if (r->va->m_type == SV_VALUE)
		{
			return r->va->m_Vdata[s];
		}
		else
		{
			s_temp2.ui = (uint32_t)(unsigned char)r->va->m_Cdata[s];
		}
	}

	return s_temp2;
}

// containers can hold themselves (h[1] = h): past this depth a nested
// container contributes a constant instead of being descended into
#define WR_HASH_MAX_DEPTH 8

static uint32_t wr_hashOfValue( const WRValue* value, int depth );

//------------------------------------------------------------------------------
uint32_t WRValue::getHashEx() const
{
	return wr_hashOfValue( this, 0 );
}

static uint32_t wr_hashOfValue( const WRValue* value, int depth )
{
	if ( value->type <= WR_FLOAT )
	{
		return value->ui;
	}
	if ( depth > WR_HASH_MAX_DEPTH )
	{
		return 0x9E3779B9;
	}
	return value->getHashLevel( depth );
}

uint32_t WRValue::getHashLevel( const int depth ) const
{
	// QUICKLY return the easy answers, that's why this code looks a bit convoluted
	if (type == WR_REF)
	{
		return wr_hashOfValue( r, depth + 1 );
	}

	if (IS_CONTAINER_MEMBER(xtype))
	{
		return wr_hashOfValue( &deref(), depth + 1 );
	}
	else if (xtype == WR_EX_ARRAY)
	{
		if (va->m_type == SV_CHAR)
		{
			return wr_hash(va->m_Cdata, va->m_size);
		}
		else if (va->m_type == SV_VALUE)
		{
			return wr_hash(va->m_Vdata, va->m_size * sizeof(WRValue));
		}
	}
	else if (xtype == WR_EX_HASH_TABLE)
	{
		// start with a hash of the key hashes
		uint32_t hash = wr_hash(va->m_hashTable, va->hashSlots() * sizeof(uint32_t));

		// hash each element, positionally dependant
		for (uint32_t e = 0; e < va->hashSlots(); ++e)
		{
			if (va->m_hashTable[e] != WRENCH_NULL_HASH)
			{
				uint32_t h = e << 16 | wr_hashOfValue( va->m_Vdata + (e << 1), depth + 1 );
				hash = wr_hash(&h, 4, hash);
			}
		}
		return hash;
	}
	else if (xtype == WR_EX_STRUCT)
	{
		uint32_t hash = wr_hash((char*)&va->m_hashTable, sizeof(void*)); // this is in ROM and must be identical for the struct to be the same (same namespace)

		for (uint32_t m = 0; m < va->m_mod; ++m)
		{
			const uint8_t* offset = va->m_ROMHashTable + (m * 5);
			if ((uint32_t)READ_32_FROM_PC(offset) != WRENCH_NULL_HASH)
			{
				uint32_t h = wr_hashOfValue( va->m_Vdata + READ_8_FROM_PC(offset + 4), depth + 1 );
				hash = wr_hash(&h, 4, hash);
			}
		}

		return hash;
	}

	return 0;
}

//------------------------------------------------------------------------------
void wr_valueToEx( const WRValue* ex, WRValue* value )
{
	if ( IS_HASH_TABLE(ex->xtype) )
	{
		ex->deref() = value->deref();
	}
	else
	{
		uint32_t s = DECODE_ARRAY_ELEMENT_FROM_P2(ex->p2);

		if ( IS_ARRAY(ex->xtype) )
		{
			if ( s >= ex->va->m_size )
			{
				if ( !wr_growValueArray(ex->va, s) )
				{
					return; // grow failed, don't write out of bounds
				}
				ex->va->m_creatorContext->allocatedMemoryHint += s * ((ex->va->m_type == SV_CHAR) ? 1 : sizeof(WRValue));
			}

			if ( ex->va->m_type == SV_CHAR )
			{
				ex->va->m_Cdata[s] = value->ui;
			}
			else
			{
				WRValue* V = ex->va->m_Vdata + s;
				wr_assign[(V->type<<2)+value->type](V, value);
			}
		}
		else if ( IS_CONTAINER_MEMBER(ex->xtype) && IS_RAW_ARRAY_TARGET(ex->r) )
		{
			ex->r->c[s] = value->ui;
		}
		else if ( IS_CONTAINER_MEMBER(ex->xtype) && ex->vb->m_type != SV_HASH_INTERNAL && IS_ARRAY(ex->r->xtype) && ex->r->va->m_type == SV_CHAR )
		{
			// a character of a string: deref() would hand out a copy of it
			if ( s < ex->r->va->m_size )
			{
				ex->r->va->m_Cdata[s] = value->ui;
			}
		}
		else
		{
			ex->deref() = value->deref();
		}
	}
}

//------------------------------------------------------------------------------
void wr_addLibraryCleanupFunction( WRState* w, void (*function)(WRState* w, void* param), void* param )
{
	WRLibraryCleanup* entry = (WRLibraryCleanup *)g_malloc(sizeof(WRLibraryCleanup));
#ifdef WRENCH_HANDLE_MALLOC_FAIL
	if ( !entry )
	{
		g_mallocFailed = true;
		w->err = WR_ERR_malloc_failed;
		return;
	}
#endif
	
	entry->cleanupFunction = function;
	entry->param = param;
	entry->next = w->libCleanupFunctions;
	w->libCleanupFunctions = entry;
}

//------------------------------------------------------------------------------
void wr_countOfArrayElement( WRValue* array, WRValue* target )
{
	array = &array->deref();
				
	if ( IS_EXARRAY_TYPE(array->xtype) )
	{
		target->i = array->va->m_size;
	}
	else if ( IS_EX_RAW_ARRAY_TYPE(array->xtype) )
	{
		target->i = EX_RAW_ARRAY_SIZE_FROM_P2(array->p2);
	}
	else
	{
		target->i = 0;
	}

	target->p2 = INIT_AS_INT;
}

//------------------------------------------------------------------------------
// put from into TO as a hash table, if to is not a hash table, make it
// one, leave the result in target
void wr_assignToHashTable( WRContext* c, WRValue* index, WRValue* value, WRValue* table )
{
	if ( value->type == WR_REF )
	{
		wr_assignToHashTable( c, index, value->r, table );
		return;
	}
	
	if ( table->type == WR_REF )
	{
		wr_assignToHashTable( c, index, value, table->r );
		return;
	}

	if ( index->type == WR_REF )
	{
		wr_assignToHashTable( c, index->r, value, table );
		return;
	}

	if ( table->xtype != WR_EX_HASH_TABLE )
	{
		table->va = c->getSVA( 0, SV_HASH_TABLE, false );
#ifdef WRENCH_HANDLE_MALLOC_FAIL
		if ( !table->va )
		{
			table->p2 = INIT_AS_INT;
			return;
		}
#endif
		table->p2 = INIT_AS_HASH_TABLE;
	}

	WRValue *entry = (WRValue *)table->va->get( index->getHash() );
	if ( entry == wr_fullHashTableSink() )
	{
		c->w->err = WR_ERR_hash_table_size_exceeded;
		return;
	}

	*entry++ = *value;
	*entry = *index;
}

//------------------------------------------------------------------------------
bool doLogicalNot_X( WRValue* value ) { return value->i == 0; } // float also uses 0x0000000 as 'zero'
bool doLogicalNot_R( WRValue* value ) { return wr_LogicalNot[ value->r->type ]( value->r ); }
bool doLogicalNot_E( WRValue* value )
{
	WRValue& V = value->singleValue();
	return wr_LogicalNot[ V.type ]( &V );
}
WRReturnSingleFunc wr_LogicalNot[4] = 
{
	doLogicalNot_X,  doLogicalNot_X,  doLogicalNot_R,  doLogicalNot_E
};


//------------------------------------------------------------------------------
void doNegate_I( WRValue* value, WRValue* target ) { target->p2 = INIT_AS_INT; target->i = wr_ineg_wrap( value->i ); }
void doNegate_F( WRValue* value, WRValue* target ) { target->p2 = INIT_AS_FLOAT; target->f = -value->f; }
void doNegate_E( WRValue* value, WRValue* target )
{
	WRValue& V = value->singleValue();
	return wr_negate[ V.type ]( &V, target );
}
void doNegate_R( WRValue* value, WRValue* target )
{
	wr_negate[ value->r->type ]( value->r, target );
}

WRSingleTargetFunc wr_negate[4] = 
{
	doNegate_I,  doNegate_F,  doNegate_R,  doNegate_E
};

//------------------------------------------------------------------------------
uint32_t doBitwiseNot_I( WRValue* value ) { return ~value->ui; }
uint32_t doBitwiseNot_F( WRValue* value )
{
	// of the int the float truncates to, as the other integer operators;
	// out of range values clamp
	const float f = value->f;
	int32_t i = !(f == f) ? 0 : (f >= 2147483647.0f ? 0x7FFFFFFF : (f <= -2147483648.0f ? (int32_t)0x80000000 : (int32_t)f));
	return ~(uint32_t)i;
}
uint32_t doBitwiseNot_R( WRValue* value ) { return wr_bitwiseNot[ value->r->type ]( value->r ); }
uint32_t doBitwiseNot_E( WRValue* value )
{
	WRValue& V = value->singleValue();
	return wr_bitwiseNot[ V.type ]( &V );
}
WRUint32Call wr_bitwiseNot[4] = 
{
	doBitwiseNot_I,  doBitwiseNot_F,  doBitwiseNot_R,  doBitwiseNot_E
};

//------------------------------------------------------------------------------
void pushIterator_X( WRValue* on, WRValue* to ) { on->init(); }
void pushIterator_R( WRValue* on, WRValue* to ) { wr_pushIterator[on->r->type]( on->r, to ); }
void pushIterator_E( WRValue* on, WRValue* to )
{
	if ( on->xtype == WR_EX_ARRAY || on->xtype == WR_EX_HASH_TABLE )
	{
		to->va = on->va;
		to->p2 = INIT_AS_ITERATOR;
	}
}

WRVoidFunc wr_pushIterator[4] =
{
	pushIterator_X, pushIterator_X, pushIterator_R, pushIterator_E
};

//------------------------------------------------------------------------------
void doAssign_IF_E( WRValue* to, WRValue* from )
{
	*to = from->deref();
}

//------------------------------------------------------------------------------
void doAssign_E_X( WRValue* to, WRValue* from )
{
	WRValue& V = from->deref();
	if ( IS_CONTAINER_MEMBER(to->xtype) )
	{
		wr_valueToEx( to, &V );
	}
	else
	{
		*to = V;
	}
}
void doAssign_R_E( WRValue* to, WRValue* from ) { wr_assign[(to->r->type<<2)|WR_EX](to->r, from); }
void doAssign_R_R( WRValue* to, WRValue* from ) { wr_assign[(to->r->type<<2)|from->r->type](to->r, from->r); }
void doAssign_E_R( WRValue* to, WRValue* from ) { wr_assign[(WR_EX<<2)|from->r->type](to, from->r); }
void doAssign_R_I( WRValue* to, WRValue* from ) { wr_assign[(to->r->type<<2)|WR_INT](to->r, from); }
void doAssign_R_F( WRValue* to, WRValue* from ) { wr_assign[(to->r->type<<2)|WR_FLOAT](to->r, from); }
void doAssign_I_R( WRValue* to, WRValue* from ) { wr_assign[(WR_INT<<2)|from->r->type](to, from->r); }
void doAssign_F_R( WRValue* to, WRValue* from ) { wr_assign[(WR_FLOAT<<2)|from->r->type](to, from->r); }
void doAssign_X_X( WRValue* to, WRValue* from ) { *to = *from; }
WRVoidFunc wr_assign[16] = 
{
	doAssign_X_X,  doAssign_X_X,  doAssign_I_R,  doAssign_IF_E,
	doAssign_X_X,  doAssign_X_X,  doAssign_F_R,  doAssign_IF_E,
	doAssign_R_I,  doAssign_R_F,  doAssign_R_R,   doAssign_R_E,
	doAssign_E_X,  doAssign_E_X,  doAssign_E_R,   doAssign_E_X,
};
