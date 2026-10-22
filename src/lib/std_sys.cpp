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
// returns : 0 - function  does not exist
//         : 1 - function is native (callback)
//         : 2 - function is in wrench (was in source code)
void wr_isFunction( WRValue* stackTop, const int argn, WRContext* c )
{
	if ( argn > 0 )
	{
		WRValue* arg = stackTop - argn;
		uint32_t hash = arg->getHash();
		WRValue* entry = c->w->globalRegistry.exists( hash, false );
		if ( entry && (entry->p2 & INIT_AS_LIB_CONST) != INIT_AS_LIB_CONST ) // constants are no functions
		{
			stackTop->i = 1;
		}
		else if ( c->registry.exists(hash, false) )
		{
			stackTop->i = 2;
		}
	}
}

//------------------------------------------------------------------------------
// returns 0 when the code was imported, -1 when it is not a string or not
// valid bytecode for this version (the script keeps running either way)
void wr_importByteCode( WRValue* stackTop, const int argn, WRContext* c )
{
	stackTop->init();
	stackTop->i = -1;

	if ( argn > 0 )
	{
		unsigned int len;
		char* data = (char *)((stackTop - argn)->array( &len, SV_CHAR ));
		if ( !data || !wr_isBytecodeValid((const uint8_t*)data, len) )
		{
			return;
		}

		uint8_t* import = (uint8_t*)g_malloc( len );
		if ( !import )
		{
#ifdef WRENCH_HANDLE_MALLOC_FAIL
			g_mallocFailed = true;
#endif
			return;
		}

		memcpy( (char*)import, data, len );

		const WRError before = (WRError)c->w->err;
		if ( wr_import(c, import, len, true) )
		{
			stackTop->i = 0;
		}
		else if ( before == WR_ERR_None && c->w->err == WR_ERR_bad_bytecode_CRC )
		{
			// the image passed the CRC but its header does not fit: that is
			// invalid bytecode too, report it instead of stopping the script
			c->w->err = WR_ERR_None;
		}
	}
}

//------------------------------------------------------------------------------
void wr_importCompile( WRValue* stackTop, const int argn, WRContext* c )
{
	if ( argn > 0 )
	{
		unsigned int len;
		char* data = (char *)((stackTop - argn)->array( &len, SV_CHAR ));
		if ( data )
		{
			// under a debugger the module is compiled to be debugged as well
			uint8_t flags = WR_INCLUDE_GLOBALS;
#ifdef WRENCH_INCLUDE_DEBUG_CODE
			if ( c->debugServer && c->debugServer->clientAttached() )
			{
				flags |= WR_EMBED_DEBUG_CODE | WR_EMBED_SOURCE_CODE;
			}
#endif
			unsigned char* out;
			int outlen;
			if ( (stackTop->i = wr_compile(data, len, &out, &outlen, 0, flags)) == WR_ERR_None )
			{
				wr_import( c, out, outlen, true );
			}
		}
	}
}

//------------------------------------------------------------------------------
void wr_halt( WRValue* stackTop, const int argn, WRContext* c )
{
	if ( argn > 0 )
	{
		unsigned int e = (stackTop - argn)->asInt();
		c->w->err = (e <= (unsigned int)WR_USER || e > (unsigned int)WR_ERR_LAST)
					? (unsigned int)WR_ERR_USER_err_out_of_range : e;
	}
}

//------------------------------------------------------------------------------
void wr_loadSysLib( WRState* w )
{
	wr_registerLibraryFunction( w, "sys::isFunction", wr_isFunction );
	wr_registerLibraryFunction( w, "sys::importByteCode", wr_importByteCode );
	wr_registerLibraryFunction( w, "sys::importCompile", wr_importCompile );
	wr_registerLibraryFunction( w, "sys::halt", wr_halt ); // halts execution and sets w->err to whatever was passed                 
														   // NOTE: value must be between WR_USER and WR_ERR_LAST

}
