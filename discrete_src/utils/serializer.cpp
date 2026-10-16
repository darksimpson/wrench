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
bool wr_serializeEx( WRValueSerializer& serializer, const WRValue& val, const int depth )
{
	char temp;
	uint16_t temp16;
	uint32_t temp32;

	if ( depth > WRENCH_MAX_SERIAL_DEPTH )
	{
		return false; // too deep, or the value contains itself
	}

	const WRValue& value = val.deref();

	serializer.write( &(temp = value.type), 1 );

	switch( (uint8_t)value.type )
	{
		case WR_INT:
		case WR_FLOAT:
		{
			temp32 = wr_x32( value.ui );
			serializer.write( (char *)&temp32, 4 );
			return true;
		}

		case WR_EX:
		{
			serializer.write( &(temp = value.xtype), 1 );

			switch( (uint8_t)value.xtype )
			{
				case WR_EX_ARRAY:
				{
					if ( value.va->m_size > 0xFFFF )
					{
						return false; // does not fit the 16-bit length field
					}

					serializer.write( &(temp = value.va->m_type), 1 );
					temp16 = wr_x16( (uint16_t)value.va->m_size );
					serializer.write( (char*)&temp16, 2 );

					if ( value.va->m_type == SV_CHAR )
					{
						serializer.write( value.va->m_SCdata, value.va->m_size );
						return true;
					}
					else if ( value.va->m_type == SV_VALUE )
					{
						for( uint32_t i=0; i<value.va->m_size; ++i )
						{
							if ( !wr_serializeEx(serializer, value.va->m_Vdata[i], depth + 1) )
							{
								return false;
							}
						}

						return true;
					}

					return false;
				}

				case WR_EX_HASH_TABLE:
				{
					temp16 = wr_x16( value.va->m_mod );
					serializer.write( (char *)&temp16, 2 );

					for( uint32_t i=0; i<value.va->m_mod; ++i )
					{
						if ( value.va->m_hashTable[i] == WRENCH_NULL_HASH )
						{
							serializer.write( &(temp = 0), 1 );
						}
						else
						{
							serializer.write( &(temp = 1), 1 );

							if ( !wr_serializeEx(serializer, value.va->m_Vdata[i<<1], depth + 1)
								|| !wr_serializeEx(serializer, value.va->m_Vdata[(i<<1) + 1], depth + 1) )
							{
								return false;
							}
						}
					}

					return true;
				}

				default: break;
			}
		}
	}

	return false;
}

// smallest encoding of any value: an int or float is a type byte and four
// data bytes, an array header is type, extended type, element type and a
// 16-bit count, a hash table header is type, extended type, a 16-bit size
// and at least one slot flag
static const unsigned int c_minSerializedValue = 5;

// getSVA() only reports an allocation failure when
// WRENCH_HANDLE_MALLOC_FAIL is defined, so also look at the storage itself;
// a failed object is left empty so the gc never walks its elements
static bool wr_allocatedOk( WRGCObject* va )
{
	if ( !va )
	{
		return false;
	}

	if ( !va->m_data )
	{
		va->m_size = 0;
		return false;
	}

	return true;
}

//------------------------------------------------------------------------------
static bool wr_deserializeValue( WRValue& value, WRValueSerializer& serializer, WRContext* context, const int depth )
{
	char temp;
	uint16_t temp16;

	if ( depth > WRENCH_MAX_SERIAL_DEPTH )
	{
		return false;
	}

	if ( !serializer.read(&temp , 1) )
	{
		return false;
	}

	switch( (uint8_t)temp )
	{
		case WR_INT:
		case WR_FLOAT:
		{
			uint32_t temp32;
			if ( !serializer.read((char *)&temp32, 4) )
			{
				return false;
			}
			value.p2 = (int)temp;
			value.ui = wr_x32( temp32 );
			return true;
		}

		case WR_EX:
		{
			if ( !serializer.read(&temp, 1) )
			{
				return false;
			}

			switch( (uint8_t)temp )
			{
				case WR_EX_ARRAY:
				{
					if ( !serializer.read(&temp, 1)  // type
						 || !serializer.read((char *)&temp16, 2) ) // size
					{
						return false;
					}

					temp16 = wr_x16( temp16 );

					switch( (uint8_t)temp )
					{
						case SV_CHAR:
						{
							// the characters must all be in the input, check
							// before allocating anything
							if ( !serializer.reserve(temp16, 1) )
							{
								return false;
							}
							serializer.release( temp16 );

							value.va = context->getSVA( temp16, SV_CHAR, false );
							if ( !wr_allocatedOk(value.va) )
							{
								return false;
							}
							value.p2 = INIT_AS_ARRAY;

							return serializer.read( value.va->m_SCdata, temp16 );
						}

						case SV_VALUE:
						{
							// every element takes at least c_minSerializedValue
							// bytes of input, refuse counts the input can not
							// possibly hold before allocating them
							if ( !serializer.reserve(temp16, c_minSerializedValue) )
							{
								return false;
							}

							value.va = context->getSVA( temp16, SV_VALUE, false );
							if ( !wr_allocatedOk(value.va) )
							{
								return false;
							}
							value.p2 = INIT_AS_ARRAY;

							// cleared, so a partial read never leaves garbage
							// values for the gc to follow
							for( uint16_t i=0; i<temp16; ++i )
							{
								value.va->m_Vdata[i].init();
							}

							for( uint16_t i=0; i<temp16; ++i )
							{
								serializer.release( c_minSerializedValue );
								if ( !wr_deserializeEx(value.va->m_Vdata[i], serializer, context, depth + 1) )
								{
									return false;
								}
							}

							return true;
						}

						default: break;
					}

					break;
				}

				case WR_EX_HASH_TABLE:
				{
					if ( !serializer.read((char *)&temp16, 2) ) // size
					{
						return false;
					}

					temp16 = wr_x16( temp16 );

					uint16_t largestMod = 0;
					for( int p=0; c_primeTable[p]; ++p )
					{
						largestMod = c_primeTable[p];
					}

					if ( temp16 == 0 || temp16 > largestMod )
					{
						return false; // corrupt table size
					}

					// every slot carries at least its used/empty flag byte
					if ( !serializer.reserve(temp16, 1) )
					{
						return false;
					}

					value.va = context->getSVA( temp16 - 1, SV_HASH_TABLE, false );
					if ( !value.va || !value.va->m_hashTable )
					{
						return false;
					}
					value.p2 = INIT_AS_HASH_TABLE;

					// init() stored the size hint in m_size, but for a hash
					// table it is the entry count, which the inserts below
					// rebuild
					value.va->m_size = 0;

					// re-insert every entry by its key rather than trusting
					// the stored positions, this restores the entry count
					// and can never index past the allocated table
					for( uint16_t i=0; i<temp16; ++i )
					{
						serializer.release( 1 );
						if ( !serializer.read(&temp, 1) )
						{
							return false;
						}

						if ( temp == 0 )
						{
							continue;
						}
						else if ( temp != 1 )
						{
							return false;
						}

						WRValue entryValue;
						WRValue entryKey;
						entryValue.init();
						entryKey.init();
						if ( !wr_deserializeEx(entryValue, serializer, context, depth + 1)
							|| !wr_deserializeEx(entryKey, serializer, context, depth + 1) )
						{
							return false;
						}

						WRValue* entry = (WRValue*)value.va->get( entryKey.getHash() );
#ifdef WRENCH_HANDLE_MALLOC_FAIL
						if ( g_mallocFailed )
						{
							return false;
						}
#endif
						entry[0] = entryValue;
						entry[1] = entryKey;
					}

					return true;
				}

				default: break;
			}
		}
	}

	return false;
}

bool wr_deserializeEx( WRValue& value, WRValueSerializer& serializer, WRContext* context, const int depth )
{
	if ( !wr_deserializeValue(value, serializer, context, depth) )
	{
		value.init(); // never leave a half-built or garbage value behind
		return false;
	}

	return true;
}

//------------------------------------------------------------------------------
bool wr_serialize( char** buf, int* len, const WRValue& value )
{
	WRValueSerializer S;
	if ( !wr_serializeEx(S, value) || S.failed() )
	{
		return false;
	}

	S.getOwnership( buf, len );
	return true;
}

//------------------------------------------------------------------------------
bool wr_deserialize( WRContext* context, WRValue& value, const char* buf, const int len )
{
	if ( !buf || len <= 0 )
	{
		value.init();
		return false;
	}

	WRValueSerializer S( buf, len );
	if ( S.failed() )
	{
		value.init();
		return false;
	}

	return wr_deserializeEx( value, S, context );
}
