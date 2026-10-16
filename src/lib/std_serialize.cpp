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
void wr_stdSerialize( WRValue* stackTop, const int argn, WRContext* c )
{
	stackTop->init(); // returned as-is when the value can not be serialized

	if ( argn )
	{
		char* buf;
		int len;
		if ( wr_serialize(&buf, &len, *(stackTop - argn) ) )
		{
			stackTop->va = c->getSVA( 0, SV_CHAR, false );
#ifdef WRENCH_HANDLE_MALLOC_FAIL
			if ( !stackTop->va )
			{
				stackTop->init();
				g_free( buf );
				return;
			}
#endif
			stackTop->p2 = INIT_AS_ARRAY;
			g_free( stackTop->va->m_Cdata );
			stackTop->va->m_data = buf;
			stackTop->va->m_size = len;

			// the buffer was handed over instead of allocated by getSVA(),
			// count it like getSVA() would so it can trigger a collection
			c->allocatedMemoryHint += (uint32_t)len;

			// collect here too: a loop that only serializes runs no
			// instruction that would trigger it (the result is on the
			// stack, so it is kept)
			c->gc( stackTop + 1 );
		}
	}
}

//------------------------------------------------------------------------------
void wr_stdDeserialize( WRValue* stackTop, const int argn, WRContext* c )
{
	stackTop->init();

	if ( argn )
	{
		WRValue& V = (stackTop - argn)->deref();
		if ( IS_ARRAY(V.xtype) && V.va->m_type == SV_CHAR )
		{
			if ( !wr_deserialize( c, *stackTop, V.va->m_SCdata, V.va->m_size ) )
			{
				stackTop->init();
			}

			c->gc( stackTop + 1 );
		}
	}
}

//------------------------------------------------------------------------------
void wr_loadSerializeLib( WRState* w )
{
	wr_registerLibraryFunction( w, "std::serialize", wr_stdSerialize );
	wr_registerLibraryFunction( w, "std::deserialize", wr_stdDeserialize );
}
