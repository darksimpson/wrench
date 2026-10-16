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

#if defined(WRENCH_INCLUDE_DEBUG_CODE) || defined(WRENCH_INCLUDE_DEBUG_CLIENT)

uint16_t wr_debugCrc16( const uint8_t* data, uint32_t len, uint16_t crc )
{
	for ( uint32_t i = 0; i < len; ++i )
	{
		crc ^= (uint16_t)data[i] << 8;
		for ( int b = 0; b < 8; ++b )
		{
			if ( crc & 0x8000 )
			{
				crc = (uint16_t)((crc << 1) ^ 0x1021);
			}
			else
			{
				crc = (uint16_t)(crc << 1);
			}
		}
	}
	return crc;
}

void WrenchDebugWriter::u8( uint8_t v )
{
	if ( m_overflow || m_len + 1 > m_cap )
	{
		m_overflow = true;
		return;
	}
	m_buf[m_len++] = v;
}

void WrenchDebugWriter::u16( uint16_t v )
{
	if ( m_overflow || m_len + 2 > m_cap )
	{
		m_overflow = true;
		return;
	}
	m_buf[m_len++] = (uint8_t)v;
	m_buf[m_len++] = (uint8_t)(v >> 8);
}

void WrenchDebugWriter::u32( uint32_t v )
{
	if ( m_overflow || m_len + 4 > m_cap )
	{
		m_overflow = true;
		return;
	}
	m_buf[m_len++] = (uint8_t)v;
	m_buf[m_len++] = (uint8_t)(v >> 8);
	m_buf[m_len++] = (uint8_t)(v >> 16);
	m_buf[m_len++] = (uint8_t)(v >> 24);
}

void WrenchDebugWriter::f32( float v )
{
	uint32_t bits;
	memcpy( &bits, &v, 4 );
	u32( bits );
}

void WrenchDebugWriter::bytes( const void* data, uint32_t len )
{
	if ( m_overflow || len > m_cap - m_len )
	{
		m_overflow = true;
		return;
	}
	if ( len )
	{
		memcpy( m_buf + m_len, data, len );
	}
	m_len += len;
}

uint8_t* WrenchDebugWriter::reserve( uint32_t len )
{
	if ( m_overflow || len > m_cap - m_len )
	{
		m_overflow = true;
		return 0;
	}
	uint8_t* ret = m_buf + m_len;
	m_len += len;
	return ret;
}

uint8_t WrenchDebugReader::u8()
{
	if ( m_bad || m_pos + 1 > m_len )
	{
		m_bad = true;
		return 0;
	}
	return m_buf[m_pos++];
}

uint16_t WrenchDebugReader::u16()
{
	if ( m_bad || m_pos + 2 > m_len )
	{
		m_bad = true;
		return 0;
	}
	uint16_t v = (uint16_t)(m_buf[m_pos] | ((uint16_t)m_buf[m_pos + 1] << 8));
	m_pos += 2;
	return v;
}

uint32_t WrenchDebugReader::u32()
{
	if ( m_bad || m_pos + 4 > m_len )
	{
		m_bad = true;
		return 0;
	}
	uint32_t v = (uint32_t)m_buf[m_pos]
							| ((uint32_t)m_buf[m_pos + 1] << 8)
							| ((uint32_t)m_buf[m_pos + 2] << 16)
							| ((uint32_t)m_buf[m_pos + 3] << 24);
	m_pos += 4;
	return v;
}

float WrenchDebugReader::f32()
{
	uint32_t bits = u32();
	float f;
	memcpy( &f, &bits, 4 );
	return f;
}

const uint8_t* WrenchDebugReader::bytes( uint32_t len )
{
	if ( m_bad || len > m_len - m_pos )
	{
		m_bad = true;
		return 0;
	}
	const uint8_t* ret = m_buf + m_pos;
	m_pos += len;
	return ret;
}

// decoder states
enum
{
	WRD_DecSync0 = 0,
	WRD_DecSync1,
	WRD_DecHeader,
	WRD_DecBody,
};

void WrenchDebugDecoder::init( uint8_t* buffer, uint32_t capacity )
{
	m_buf = buffer;
	m_cap = capacity;
	m_pos = 0;
	m_expected = 0;
	m_lastByteMs = 0;
	m_state = WRD_DecSync0;
	m_crcErrors = 0;
	m_framingErrors = 0;
	memset( &m_frame, 0, sizeof(m_frame) );
}

bool WrenchDebugDecoder::feed( uint8_t byte, uint32_t nowMs )
{
	if ( m_state != WRD_DecSync0 && (uint32_t)(nowMs - m_lastByteMs) > WRD_FRAME_TIMEOUT_MS )
	{
		// the rest of a frame never arrived, start hunting again
		++m_framingErrors;
		m_state = WRD_DecSync0;
	}
	m_lastByteMs = nowMs;

	switch ( m_state )
	{
		case WRD_DecSync0:
		{
			if ( byte == WRD_SYNC0 )
			{
				m_state = WRD_DecSync1;
			}
			return false;
		}

		case WRD_DecSync1:
		{
			if ( byte == WRD_SYNC1 )
			{
				m_state = WRD_DecHeader;
				m_pos = 0;
			}
			else if ( byte != WRD_SYNC0 )
			{
				m_state = WRD_DecSync0;
			}
			return false;
		}

		case WRD_DecHeader:
		{
			// buffer layout mirrors the frame minus the sync bytes:
			// type, seq, len lo, len hi, payload, crc lo, crc hi
			m_buf[m_pos++] = byte;
			if ( m_pos == 4 )
			{
				uint32_t len = (uint32_t)m_buf[2] | ((uint32_t)m_buf[3] << 8);
				if ( len + 6 > m_cap )
				{
					++m_framingErrors;
					m_state = WRD_DecSync0;
					return false;
				}
				m_expected = len + 6;
				m_state = WRD_DecBody;
			}
			return false;
		}

		case WRD_DecBody:
		{
			m_buf[m_pos++] = byte;
			if ( m_pos < m_expected )
			{
				return false;
			}

			m_state = WRD_DecSync0;

			uint32_t len = m_expected - 6;
			uint16_t crc = wr_debugCrc16( m_buf, len + 4 );
			uint16_t got = (uint16_t)(m_buf[len + 4] | ((uint16_t)m_buf[len + 5] << 8));
			if ( crc != got )
			{
				++m_crcErrors;
				return false;
			}

			m_frame.type = m_buf[0];
			m_frame.seq = m_buf[1];
			m_frame.length = (uint16_t)len;
			m_frame.payload = m_buf + 4;
			return true;
		}
	}

	m_state = WRD_DecSync0;
	return false;
}

uint32_t wr_debugFinishFrame( uint8_t* out, uint8_t type, uint8_t seq, uint16_t payloadLen )
{
	out[0] = WRD_SYNC0;
	out[1] = WRD_SYNC1;
	out[2] = type;
	out[3] = seq;
	out[4] = (uint8_t)payloadLen;
	out[5] = (uint8_t)(payloadLen >> 8);

	uint16_t crc = wr_debugCrc16( out + 2, 4 + (uint32_t)payloadLen );
	out[WRD_FRAME_HEADER + payloadLen] = (uint8_t)crc;
	out[WRD_FRAME_HEADER + payloadLen + 1] = (uint8_t)(crc >> 8);

	return WRD_FRAME_OVERHEAD + payloadLen;
}

#endif
