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
#ifndef WRENCH_WITHOUT_COMPILER

//------------------------------------------------------------------------------
int wr_strnlen( const char* s, int len )
{
	const char* found = (const char *)memchr( s, '\0', len );
	return found ? (found - s) : len;
}

// the digits of an integer literal into 32 bits ("0x" and "0b" alone are 0).
// False when the value does not fit: strtol/strtoul clamp or wrap
// differently with 32 and 64 bit longs, so the same source would mean other
// numbers
static bool wr_parseLiteral( const char* digits, const unsigned int base, uint32_t& out )
{
	out = 0;
	if ( !digits || !*digits )
	{
		return true;
	}
	uint32_t v = 0;
	for( ; *digits; ++digits )
	{
		char c = *digits;
		unsigned int d;
		if ( c >= '0' && c <= '9' )
		{
			d = (unsigned int)(c - '0');
		}
		else if ( c >= 'a' && c <= 'f' )
		{
			d = (unsigned int)(c - 'a' + 10);
		}
		else if ( c >= 'A' && c <= 'F' )
		{
			d = (unsigned int)(c - 'A' + 10);
		}
		else
		{
			return false;
		}
		if ( d >= base || v > (0xFFFFFFFFu - d) / base )
		{
			return false;
		}
		v = v * base + d;
	}
	out = v;
	return true;
}

//------------------------------------------------------------------------------
bool WRCompilationContext::getToken( WRExpressionContext& ex, const char* expect )
{
	WRValue& value = ex.value;
	WRstr& token = ex.token;
	
	if ( m_loadedToken.size() || (m_loadedValue.type != WR_REF) )
	{
		token = m_loadedToken;
		value = m_loadedValue;
		m_quoted = m_loadedQuoted;
	}
	else
	{
		// after a comment the next token is read from here (a recursive call
		// per comment overflows the stack on long comment blocks)
nextToken:
		m_quoted = false;
		value.p2 = INIT_AS_REF;

		ex.spaceBefore = (m_pos < m_sourceLen) && isspace((unsigned char)m_source[m_pos]);
	
		do
		{
			if ( m_pos >= m_sourceLen )
			{
				m_EOF = true;
				return false;
			}
			
		} while( isspace((unsigned char)m_source[m_pos++]) );

		token = m_source[m_pos - 1];

		unsigned int t=0;
		for( ; c_operations[t].token && strncmp( c_operations[t].token, "@macroBegin", 11); ++t );

		int offset = m_pos - 1;

		// a name right after '.' is a member (p.y), never an enum constant
		int previous = offset - 1;
		while( previous >= 0 && isspace((unsigned char)m_source[previous]) )
		{
			--previous;
		}
		bool memberName = previous >= 0 && m_source[previous] == '.';

		// a name declared with var/blank: inside a struct it is a member
		// (struct P { var y; } next to enum { y }), elsewhere it would
		// silently turn into an assignment to the constant
		bool declaredName = false;
		if ( previous >= 0 && (isalnum((unsigned char)m_source[previous]) || m_source[previous] == '_') )
		{
			int wordStart = previous;
			while( wordStart > 0 && (isalnum((unsigned char)m_source[wordStart - 1]) || m_source[wordStart - 1] == '_') )
			{
				--wordStart;
			}
			int wordLen = previous - wordStart + 1;
			declaredName = (wordLen == 3 && !strncmp(m_source + wordStart, "var", 3))
							|| (wordLen == 5 && !strncmp(m_source + wordStart, "blank", 5));
		}
		if ( declaredName && m_units[m_unitTop].bytecode.isStructSpace )
		{
			memberName = true;
		}

		for( ; c_operations[t].token; ++t )
		{
			int len = wr_strnlen( c_operations[t].token, 20 );
			if ( ((offset + len) < m_sourceLen)
				 && !strncmp(m_source + offset, c_operations[t].token, len) )
			{
				if ( isalnum((unsigned char)m_source[offset+len]) || m_source[offset+len] == '_' )
				{
					continue;
				}
				
				m_pos += len - 1;
				token = c_operations[t].token;
				goto foundMacroToken;
			}
		}

		for( t = 0; !memberName && t<m_units[0].constantValues.count(); ++t )
		{
			int len = m_units[0].constantValues[t].label.size();
			if ( ((offset + len) < m_sourceLen)
				 && !strncmp(m_source + offset, m_units[0].constantValues[t].label.c_str(), len) )
			{
				if ( isalnum((unsigned char)m_source[offset+len]) || m_source[offset+len] == '_' )
				{
					continue;
				}

				if ( declaredName )
				{
					m_err = WR_ERR_constant_redefined;
				}
				m_pos += len - 1;
				token.clear();
				value = m_units[0].constantValues[t].value;
				goto foundMacroToken;
			}
		}

		for( t = 0; !memberName && t<m_units[m_unitTop].constantValues.count(); ++t )
		{
			int len = m_units[m_unitTop].constantValues[t].label.size();
			if ( ((offset + len) < m_sourceLen)
				 && !strncmp(m_source + offset, m_units[m_unitTop].constantValues[t].label.c_str(), len) )
			{
				if ( isalnum((unsigned char)m_source[offset+len]) || m_source[offset+len] == '_' )
				{
					continue;
				}

				if ( declaredName )
				{
					m_err = WR_ERR_constant_redefined;
				}
				m_pos += len - 1;
				token.clear();
				value = m_units[m_unitTop].constantValues[t].value;
				goto foundMacroToken;
			}
		}

		if ( token[0] == '-' )
		{
			if ( m_pos < m_sourceLen )
			{
				/*
				if ( (isdigit((unsigned char)m_source[m_pos]) && !m_LastParsedLabel) || m_source[m_pos] == '.' )
				{
					goto parseAsNumber;
				}
				else*/ if ( m_source[m_pos] == '-' )
				{
					token += '-';
					++m_pos;
				}
				else if ( m_source[m_pos] == '=' )
				{
					token += '=';
					++m_pos;
				}
			}
		}
		else
		{
			m_LastParsedLabel = false;
		
			if ( token[0] == '=' )
			{
				if ( (m_pos < m_sourceLen) && m_source[m_pos] == '=' )
				{
					token += '=';
					++m_pos;
				}
			}
			else if ( token[0] == '!' )
			{
				if ( (m_pos < m_sourceLen) && m_source[m_pos] == '=' )
				{
					token += '=';
					++m_pos;
				}
			}
			else if ( token[0] == '*' )
			{
				if ( (m_pos < m_sourceLen) && m_source[m_pos] == '=' )
				{
					token += '=';
					++m_pos;
				}
			}
			else if ( token[0] == '%' )
			{
				if ( (m_pos < m_sourceLen) && m_source[m_pos] == '=' )
				{
					token += '=';
					++m_pos;
				}
			}
			else if ( token[0] == '<' )
			{
				if ( (m_pos < m_sourceLen) && m_source[m_pos] == '=' )
				{
					token += '=';
					++m_pos;
				}
				else if ( (m_pos < m_sourceLen) && m_source[m_pos] == '<' )
				{
					token += '<';
					++m_pos;

					if ( (m_pos < m_sourceLen) && m_source[m_pos] == '=' )
					{
						token += '=';
						++m_pos;
					}
				}
			}
			else if ( token[0] == '>' )
			{
				if ( (m_pos < m_sourceLen) && m_source[m_pos] == '=' )
				{
					token += '=';
					++m_pos;
				}
				else if ( (m_pos < m_sourceLen) && m_source[m_pos] == '>' )
				{
					token += '>';
					++m_pos;

					if ( (m_pos < m_sourceLen) && m_source[m_pos] == '=' )
					{
						token += '=';
						++m_pos;
					}
				}
			}
			else if ( token[0] == '&' )
			{
				if ( (m_pos < m_sourceLen) && m_source[m_pos] == '&' )
				{
					token += '&';
					++m_pos;
				}
				else if ( (m_pos < m_sourceLen) && m_source[m_pos] == '=' )
				{
					token += '=';
					++m_pos;
				}
			}
			else if ( token[0] == '^' )
			{
				if ( (m_pos < m_sourceLen) && m_source[m_pos] == '=' )
				{
					token += '=';
					++m_pos;
				}
			}
			else if ( token[0] == '|' )
			{
				if ( (m_pos < m_sourceLen) && m_source[m_pos] == '|' )
				{
					token += '|';
					++m_pos;
				}
				else if ( (m_pos < m_sourceLen) && m_source[m_pos] == '=' )
				{
					token += '=';
					++m_pos;
				}
			}
			else if ( token[0] == '+' )
			{
				if ( m_pos < m_sourceLen )
				{
					if ( m_source[m_pos] == '+' )
					{
						token += '+';
						++m_pos;
					}
					else if ( m_source[m_pos] == '=' )
					{
						token += '=';
						++m_pos;
					}
				}
			}
			else if ( token[0] == '\"' || token[0] == '\'' )
			{
				bool single = token[0] == '\'';
				token.clear();
				m_quoted = true;
				
				do
				{
					if (m_pos >= m_sourceLen)
					{
						m_err = WR_ERR_unterminated_string_literal;
						m_EOF = true;
						return false;
					}

					char c = m_source[m_pos];
					if (c == '\"' && !single ) // terminating character
					{
						if ( single )
						{
							m_err = WR_ERR_bad_expression;
							return false;
						}
						++m_pos;
						break;
					}
					else if ( c == '\'' && single )
					{
						if ( !single || (token.size() > 1) )
						{
							m_err = WR_ERR_bad_expression;
							return false;
						}
						++m_pos;
						break;
					}
					else if (c == '\n')
					{
						m_err = WR_ERR_newline_in_string_literal;
						return false;
					}
					else if (c == '\\')
					{
						// check before reading: the backslash may be the last byte
						if (m_pos + 1 >= m_sourceLen)
						{
							m_err = WR_ERR_unterminated_string_literal;
							m_EOF = true;
							return false;
						}
						c = m_source[++m_pos];

						if (c == '\\') // escaped slash
						{
							token += '\\';
						}
						else if (c == '0')
						{
							// \0, \097: the digits after the backslash are a decimal character code
							unsigned int code = 0;
							while ( m_pos < m_sourceLen && isdigit((unsigned char)m_source[m_pos]) )
							{
								code = ((code * 10) + (unsigned int)(m_source[m_pos] - '0')) & 0xFFFF;
								++m_pos;
							}
							token += (char)code;
							--m_pos;
						}
						else if (c == '\"')
						{
							token += '\"';
						}
						else if (c == '\'')
						{
							token += '\'';
						}
						else if (c == 'n')
						{
							token += '\n';
						}
						else if (c == 'r')
						{
							token += '\r';
						}
						else if (c == 't')
						{
							token += '\t';
						}
						else if (c == 'a')
						{
							token += '\a';
						}
						else if (c == 'b')
						{
							token += '\b';
						}
						else if (c == 'f')
						{
							token += '\f';
						}
						else if (c == 'v')
						{
							token += '\v';
						}
						else if (c == 'x' && (m_pos + 1 < m_sourceLen) && isxdigit((unsigned char)m_source[m_pos + 1]))
						{
							// \xH or \xHH
							int hex = 0;
							for( int digits = 0; digits < 2 && (m_pos + 1 < m_sourceLen) && isxdigit((unsigned char)m_source[m_pos + 1]); ++digits )
							{
								char h = m_source[++m_pos];
								hex = (hex << 4) | (isdigit((unsigned char)h) ? (h - '0') : ((tolower((unsigned char)h) - 'a') + 10));
							}
							token += (char)hex;
						}
						else
						{
							m_err = WR_ERR_bad_string_escape_sequence;
							return false;
						}
					}
					else
					{
						token += c;
					}

				} while( ++m_pos < m_sourceLen );

				value.p2 = INIT_AS_INT;
				if ( single )
				{
					// bytes are unsigned everywhere else (string indexing), keep
					// character literals consistent: '\xFF' is 255
					value.ui = token.size() == 1 ? (unsigned char)token[0] : 0;
					token.clear();
				}
				else
				{
					value.p2 = INIT_AS_INT;
					value.type = (WRValueType)WR_COMPILER_LITERAL_STRING;
					value.p = &token;
				}
			}
			else if ( token[0] == '/' ) // might be a comment
			{
				if ( m_pos < m_sourceLen )
				{	
					if ( !isspace((unsigned char)m_source[m_pos]) )
					{
						if ( m_source[m_pos] == '/' )
						{
							for( ; m_pos<m_sourceLen && m_source[m_pos] != '\n'; ++m_pos ) // clear to end EOL
							{
							}

							goto nextToken;
						}
						else if ( m_source[m_pos] == '*' )
						{
							for( ; (m_pos+1)<m_sourceLen
								 && !(m_source[m_pos] == '*' && m_source[m_pos+1] == '/');
								 ++m_pos ); // find end of comment

							if ( (m_pos + 1) >= m_sourceLen )
							{
								m_err = WR_ERR_unexpected_EOF;
								return false;
							}

							m_pos += 2;

							goto nextToken;

						}
						else if ( m_source[m_pos] == '=' )
						{
							token += '=';
							++m_pos;
						}
					}					
					//else // bare '/' 
				}
			}
			else if ( isdigit((unsigned char)token[0])
						|| (token[0] == '.' && (m_pos < m_sourceLen) && isdigit((unsigned char)m_source[m_pos])) )
			{
				if ( m_pos >= m_sourceLen )
				{
					return false;
				}

//parseAsNumber:

				m_LastParsedLabel = true;

				// a leading zero means octal only for a plain run of octal
				// digits: 09.5, 0.5 and 07e2 are decimal numbers
				bool octal = token[0] == '0' && isdigit((unsigned char)m_source[m_pos]);
				if ( octal )
				{
					int scan = m_pos;
					while( scan < m_sourceLen && isdigit((unsigned char)m_source[scan]) )
					{
						if ( m_source[scan] > '7' )
						{
							octal = false;
						}
						++scan;
					}
					if ( scan < m_sourceLen && (m_source[scan] == '.' || m_source[scan] == 'e' || m_source[scan] == 'E') )
					{
						octal = false;
					}
				}

				if ( token[0] == '0' && m_source[m_pos] == 'x' ) // interpret as hex
				{
					token.clear();
					m_pos++;

					for(;;)
					{
						if ( m_pos >= m_sourceLen )
						{
							m_err = WR_ERR_unexpected_EOF;
							return false;
						}

						if ( !isxdigit((unsigned char)m_source[m_pos]) )
						{
							break;
						}

						token += m_source[m_pos++];
					}

					value.p2 = INIT_AS_INT;
					if ( !wr_parseLiteral(token.c_str(), 16, value.ui) )
					{
						m_err = WR_ERR_bad_expression; // more than 32 bits
						return false;
					}
				}
				else if (token[0] == '0' && m_source[m_pos] == 'b' )
				{
					token.clear();
					m_pos++;

					for(;;)
					{
						if ( m_pos >= m_sourceLen )
						{
							m_err = WR_ERR_unexpected_EOF;
							return false;
						}

						if ( m_source[m_pos] != '0' && m_source[m_pos] != '1' )
						{
							break;
						}

						token += m_source[m_pos++];
					}

					value.p2 = INIT_AS_INT;
					if ( !wr_parseLiteral(token.c_str(), 2, value.ui) )
					{
						m_err = WR_ERR_bad_expression; // more than 32 bits
						return false;
					}
				}
				else if ( octal )
				{
					token.clear();

					for(;;)
					{
						if ( m_pos >= m_sourceLen )
						{
							m_err = WR_ERR_unexpected_EOF;
							return false;
						}

						if ( m_source[m_pos] < '0' || m_source[m_pos] > '7' )
						{
							break;
						}

						token += m_source[m_pos++];
					}

					value.p2 = INIT_AS_INT;
					if ( !wr_parseLiteral(token.c_str(), 8, value.ui) )
					{
						m_err = WR_ERR_bad_expression; // more than 32 bits
						return false;
					}
				}
				else
				{
					bool decimal = token[0] == '.';
					for(;;)
					{
						if ( m_pos >= m_sourceLen )
						{
							m_err = WR_ERR_unexpected_EOF;
							return false;
						}

						if ( m_source[m_pos] == '.' )
						{
							if ( decimal )
							{
								return false;
							}

							decimal = true;
						}
						else if ( (m_source[m_pos] == 'e' || m_source[m_pos] == 'E')
									&& (m_pos + 1 < m_sourceLen)
									&& (isdigit((unsigned char)m_source[m_pos + 1])
										|| ((m_source[m_pos + 1] == '-' || m_source[m_pos + 1] == '+')
											&& (m_pos + 2 < m_sourceLen)
											&& isdigit((unsigned char)m_source[m_pos + 2]))) )
						{
							// exponent: 1e3, 2.5e-3
							decimal = true;
							token += m_source[m_pos++];
							if ( m_source[m_pos] == '-' || m_source[m_pos] == '+' )
							{
								token += m_source[m_pos++];
							}
							continue;
						}
						else if ( !isdigit((unsigned char)m_source[m_pos]) )
						{
							if ( m_source[m_pos] == 'f' || m_source[m_pos] == 'F')
							{
								decimal = true;
								m_pos++;
							}

							break;
						}

						token += m_source[m_pos++];
					}

					if ( decimal )
					{
						value.p2 = INIT_AS_FLOAT;
						value.f = (float)atof( token );
					}
					else
					{
						value.p2 = INIT_AS_INT;
						if ( !wr_parseLiteral(token.c_str(), 10, value.ui) )
						{
							m_err = WR_ERR_bad_expression; // more than 32 bits
							return false;
						}
					}
				}
			}
			else if ( token[0] == ':' && m_pos < m_sourceLen && isspace((unsigned char)m_source[m_pos]) )
			{
				
			}
			else if ( isalpha((unsigned char)token[0]) || token[0] == '_' || token[0] == ':' ) // must be a label
			{
				if ( token[0] != ':' || (m_pos < m_sourceLen && m_source[m_pos] == ':') )
				{
					m_LastParsedLabel = true;
					if ( m_pos < m_sourceLen
						 && token[0] == ':'
						 && m_source[m_pos] == ':' )
					{
						token += ':';
						++m_pos;
					}

					for (; m_pos < m_sourceLen; ++m_pos)
					{
						if ( (m_pos + 1) < m_sourceLen
							 && m_source[m_pos] == ':'
							 && m_source[m_pos + 1] == ':'
							 && token.size() > 0 )
						{
							token += "::";
							m_pos ++;
							continue;
						}
						
						if (!isalnum((unsigned char)m_source[m_pos]) && m_source[m_pos] != '_' )
						{
							break;
						}

						token += m_source[m_pos];
					}

					if (token == "true")
					{
						value.p2 = INIT_AS_INT;
						value.i = 1;
						token = "1";
					}
					else if (token == "false" || token == "null" )
					{
						value.p2 = INIT_AS_INT;
						value.i = 0;
						token = "0";
					}
				}
			}
		}

foundMacroToken:
	
		ex.spaceAfter = (m_pos < m_sourceLen) && isspace((unsigned char)m_source[m_pos]);
	}

	m_loadedToken.clear();
	m_loadedQuoted = m_quoted;
	m_loadedValue.p2 = INIT_AS_REF;

	if ( expect && (token != expect) )
	{
		return false;
	}

	return true;
}

#endif
