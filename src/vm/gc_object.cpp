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

// getIndexOfHit() for a key a script hash table has no room for
#define WR_HASH_TABLE_FULL 0xFFFFFFFF

// per thread, like the scratch values of operations.cpp
static WRENCH_THREAD_LOCAL uintptr_t s_fullTableSinkWords[4];

//------------------------------------------------------------------------------
WRValue* wr_fullHashTableSink()
{
	(void)sizeof( char[(2 * sizeof(WRValue) == sizeof(s_fullTableSinkWords)) ? 1 : -1] );
	return (WRValue*)s_fullTableSinkWords;
}

//------------------------------------------------------------------------------
int WRGCObject::init( unsigned int size, const WRGCObjectType type, bool clear )
{
	if ( type == SV_VALUE && size > WRENCH_MAX_ARRAY_ELEMENTS )
	{
		// refuse instead of overflowing the byte count below
		size = 0;
#ifdef WRENCH_HANDLE_MALLOC_FAIL
		g_mallocFailed = true;
#endif
	}

	int ret = (m_size = size);

	if ( type == SV_HASH_TABLE || type == SV_VOID_HASH_TABLE )
	{
		m_size = 0; // the size is only a hint here, m_size counts entries
	}

	if ( (m_type = type) == SV_VALUE )
	{
		ret *= sizeof(WRValue);
		const unsigned int allocated = ret ? ret : sizeof(WRValue);
		m_Vdata = (WRValue*)g_malloc( allocated );
		if ( !m_Vdata )
		{
			// never memset or report a size for memory we did not get
			m_size = 0;
#ifdef WRENCH_HANDLE_MALLOC_FAIL
			g_mallocFailed = true;
#endif
			return 0;
		}
		
		if ( clear )
		{
			memset( m_SCdata, 0, allocated );
		}

		ret = allocated;
	}
	else if ( m_type == SV_CHAR )
	{
		const unsigned int allocated = size ? size : 1;
		m_Cdata = (unsigned char*)g_malloc( allocated );
		if ( !m_Cdata )
		{
			m_size = 0;
#ifdef WRENCH_HANDLE_MALLOC_FAIL
			g_mallocFailed = true;
#endif
			return 0;
		}
		if ( clear )
		{
			memset( m_SCdata, 0, allocated );
		}

		ret = allocated;
	}
	else
	{
		growHash( WRENCH_NULL_HASH, size, &ret );
	}

	return ret;
}

//------------------------------------------------------------------------------
WRValue* WRGCObject::getAsRawValueHashTable( const uint32_t hash, int* index )
{
#ifdef WRENCH_COMPACT
	int i = getIndexOfHit( hash, false );
#else
	int i = hash % m_mod;
	if ( m_hashTable[i] != hash )
	{
		if ( m_hashTable[(i = (i + 1) % m_mod)] != hash )
		{
			if ( m_hashTable[(i = (i + 1) % m_mod)] != hash )
			{
				if ( m_hashTable[(i = (i + 1) % m_mod)] != hash )
				{
					i = getIndexOfHit(hash, true);
				}
			}
		}
	}
#endif

	if ( index ) { *index = i; }
	return m_Vdata + i;
}

//------------------------------------------------------------------------------
WRValue* WRGCObject::exists( const uint32_t hash, bool removeIfPresent )
{
	uint32_t index;
	if ( hash == WRENCH_NULL_HASH && m_type == SV_HASH_TABLE )
	{
		// the empty marker as a key lives in the slot after the others
		if ( m_hashTable[m_mod] == WRENCH_NULL_HASH )
		{
			return 0;
		}
		index = m_mod;
		goto foundExists;
	}

	index = hash % m_mod;

	if ( m_hashTable[index] != hash )
	{
		int tries = 3;
		do
		{
			index = (index + 1) % m_mod;
			if ( m_hashTable[index] == hash )
			{
				goto foundExists;
			}

		} while( tries-- );

		// at the largest size entries may sit anywhere (see scanLargest)
		if ( !atLargestSize() )
		{
			return 0;
		}
		int found = scanLargest( hash, false );
		if ( found < 0 )
		{
			return 0;
		}
		index = (uint32_t)found;
	}

foundExists:

	if ( removeIfPresent )
	{
		--m_size;
		m_hashTable[index] = WRENCH_NULL_HASH;

		// a later insert reuses the slot without clearing it, so a
		// removed key must not leave its value (or key) behind
		if ( m_type == SV_HASH_TABLE )
		{
			m_Vdata[index << 1].init();
			m_Vdata[(index << 1) + 1].init();
		}
		else
		{
			m_Vdata[index].init();
		}
	}

	return m_Vdata + ((m_type == SV_HASH_TABLE) ? (index << 1) : index);
}

//------------------------------------------------------------------------------
void* WRGCObject::get( const uint32_t l, int* index )
{
	int s = l < m_size ? l : m_size - 1;
	void* ret = m_Vdata + s;

	if ( m_type == SV_CHAR )
	{
		ret = m_Cdata + s;
	}
	else if ( m_type == SV_HASH_TABLE )
	{
		uint32_t hit = getIndexOfHit( l, false );
		if ( hit == WR_HASH_TABLE_FULL )
		{
			// no room for the key: nothing in the table may be overwritten
			WRValue* sink = wr_fullHashTableSink();
			sink[0].init();
			sink[1].init();
			if ( index ) { *index = 0; }
			return sink;
		}
		s = hit << 1;
		ret = m_Vdata + s;
	}
	else if ( m_type == SV_VOID_HASH_TABLE )
	{
		ret = getAsRawValueHashTable( l, index );
	}
	// else it must be SV_VALUE

	if ( index ) { *index = s; }
	return ret;
}

//------------------------------------------------------------------------------
uint32_t WRGCObject::getIndexOfHit( const uint32_t hash, const bool inserting )
{
	if ( hash == WRENCH_NULL_HASH && m_type == SV_HASH_TABLE )
	{
		// the empty marker as a key: it has its own slot after the others
		if ( m_hashTable[m_mod] == WRENCH_NULL_HASH )
		{
			++m_size;
			m_hashTable[m_mod] = 0; // anything but the marker: occupied
		}
		return m_mod;
	}

	uint32_t index = hash % m_mod;
	if ( m_hashTable[index] == hash )
	{
		return index; // immediate hits should be cheap
	}

	int tries = 3;
	do
	{
		if ( inserting && m_hashTable[index] == WRENCH_NULL_HASH )
		{
			++m_size;
			m_hashTable[index] = hash;
			return index;
		}

		index = (index + 1) % m_mod;

		if ( m_hashTable[index] == hash )
		{
			return index;
		}

	} while( tries-- );

	if ( !inserting && atLargestSize() )
	{
		// entries of a table that cannot grow may sit anywhere
		int found = scanLargest( hash, false );
		if ( found >= 0 )
		{
			return (uint32_t)found;
		}
	}

	return inserting ? growHash(hash) : getIndexOfHit( hash, true );
}

//------------------------------------------------------------------------------
// moves a value and its key to a grown table, the old pair forwards to them
static void wr_moveHashPair( WRValue* to, WRValue* from )
{
	// value
	to->p2 = from->p2;
	to->p = from->p;
	from->p2 = INIT_AS_REF;
	from->r = to;

	// key
	++to;
	++from;
	to->p2 = from->p2;
	to->p = from->p;
	from->p2 = INIT_AS_REF;
	from->r = to;
}

uint32_t WRGCObject::growHash( const uint32_t hash, const uint16_t sizeHint, int* sizeAllocated )
{
	// there was a collision with the passed hash, grow until the
	// collision disappears

	uint16_t start = (sizeHint > m_mod) ? sizeHint : m_mod;
	int t = 0;
	for( ; c_primeTable[t] && c_primeTable[t] <= start ; ++t );
	if ( !c_primeTable[t] && t > 0 && m_mod < c_primeTable[t - 1] )
	{
		--t; // a size hint beyond the list gets the largest size
	}

	for(;;)
	{
tryAgain:
		int newMod = c_primeTable[t];
		if ( !newMod )
		{
			// out of table sizes: the table cannot grow, but it may still
			// have free slots further away than the usual probes reach
			if ( m_hashTable && m_mod )
			{
				int slot = scanLargest( hash, true );
				if ( slot >= 0 )
				{
					return (uint32_t)slot;
				}
			}
			// completely full: a script's table says so (the script fails
			// with WR_ERR_hash_table_size_exceeded), an internal one is
			// like an allocation failure
			if ( m_type == SV_HASH_TABLE )
			{
				return WR_HASH_TABLE_FULL;
			}
#ifdef WRENCH_HANDLE_MALLOC_FAIL
			g_mallocFailed = true;
#endif
			return 0;
		}

		// a script hash table has one slot more (see hashSlots)
		int slots = (m_type == SV_HASH_TABLE) ? newMod + 1 : newMod;

		int newSize;
		if ( m_type == SV_VOID_HASH_TABLE )
		{
			newSize = slots;
		}
		else
		{
			newSize = slots << 1;
		}

		newSize *= sizeof(WRValue);
		newSize += sizeof(WRGCBase);

		int total = slots*sizeof(uint32_t) + newSize;
		if ( sizeAllocated )
		{
			*sizeAllocated = total;
		}

		WRGCBase* base = (WRGCBase*)g_malloc( total );
#ifdef WRENCH_HANDLE_MALLOC_FAIL
		if ( !base )
		{
			g_mallocFailed = true;
			return 0; // congradulations, clobber this one. we're dying it doesn't matter.
		}
#endif

		memset( (unsigned char *)base, 0, total );

		uint32_t* proposed = (uint32_t *)((char*)base + newSize);

		for( int n = 0; n<slots; ++n )
		{
			proposed[n] = WRENCH_NULL_HASH;
		}

		proposed[ hash % newMod ] = hash;

		for( unsigned int h=0; h<m_mod; ++h )
		{
			int tries = 3;
			int newEntry = m_hashTable[h] % newMod;
			for(;;)
			{
				if ( proposed[newEntry] == WRENCH_NULL_HASH )
				{
					proposed[newEntry] = m_hashTable[h];
					break;
				}
				else if ( tries-- )
				{
					newEntry = (newEntry + 1) % newMod;
				}
				else
				{
					g_free( base );
					++t;
					goto tryAgain;
				}
			}
		}

		// accepted, link it into the existing table
		base->m_type = SV_HASH_INTERNAL;

		base->m_nextGC = m_nextGC;
		m_nextGC = base;
		
		WRValue* newValues = (WRValue*)(base + 1);

		uint32_t* oldHashTable = m_hashTable;
		m_hashTable = proposed;
		int oldMod = m_mod;
		m_mod = newMod;

		for( int v=0; v<oldMod; ++v )
		{
			if ( oldHashTable[v] == WRENCH_NULL_HASH )
			{
				continue;
			}

			unsigned int newPos = getIndexOfHit( oldHashTable[v], true );

			if ( m_type == SV_VOID_HASH_TABLE )
			{
				newValues[newPos] = m_Vdata[v];
				m_Vdata[v].r = newValues + newPos;
				m_Vdata[v].p2 = INIT_AS_REF;
			}
			else
			{
				// copy all the new hashes to their new locations
				wr_moveHashPair( newValues + (newPos<<1), m_Vdata + (v<<1) );
			}
		}

		// the slot for the empty marker as a key moves along
		if ( m_type == SV_HASH_TABLE && oldHashTable && oldMod && oldHashTable[oldMod] != WRENCH_NULL_HASH )
		{
			m_hashTable[newMod] = oldHashTable[oldMod];
			wr_moveHashPair( newValues + (newMod<<1), m_Vdata + (oldMod<<1) );
		}

		// the superseded block forwards its entries to this one; link it
		// so a member reference into an old block keeps every block
		// between it and the live one alive (see WRContext::mark)
		if ( m_Vdata )
		{
			((WRGCBase*)m_Vdata - 1)->m_referencedTable = base;
		}

		m_Vdata = newValues;

		// the marker as the hash means no key to add (the first allocation)
		if ( hash == WRENCH_NULL_HASH )
		{
			return 0;
		}

		// the new key was placed in 'proposed' already, so the lookup below
		// hits at once and would not count it
		++m_size;

		return getIndexOfHit( hash, true );
	}
}

// the table is as large as it gets: probing a few slots is no longer enough,
// look through all of them. -1 when the hash is not there (and, inserting,
// no slot is free)
int WRGCObject::scanLargest( const uint32_t hash, const bool inserting )
{
	uint32_t start = hash % m_mod;
	int freeSlot = -1;
	for( uint32_t n = 0; n < m_mod; ++n )
	{
		uint32_t index = (start + n) % m_mod;
		if ( m_hashTable[index] == hash )
		{
			return (int)index;
		}
		if ( freeSlot < 0 && m_hashTable[index] == WRENCH_NULL_HASH )
		{
			freeSlot = (int)index;
		}
	}
	if ( inserting && freeSlot >= 0 )
	{
		++m_size;
		m_hashTable[freeSlot] = hash;
	}
	return inserting ? freeSlot : -1;
}

bool WRGCObject::atLargestSize() const
{
	int t = 0;
	while ( c_primeTable[t + 1] )
	{
		++t;
	}
	return m_mod >= c_primeTable[t];
}
