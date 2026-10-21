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

// include "file" is handled before parsing: the text of the file replaces
// the directive, so the parser, line markers and long jump bookkeeping all
// see one source. Nothing is copied unless something is included

static bool wr_isNameChar( char c )
{
	return isalnum( (unsigned char)c ) || c == '_';
}

WRError WRSourceSet::expand( const char* source, const int size, const WRCompileOptions& options )
{
	m_source = source;
	m_size = size;
	m_files.clear();
	m_segments.clear();
	m_expanded.clear();
	m_virtualLine = 1;
	m_sawInclude = false;
	m_errFile = 0;
	m_errLine = 0;

	WRSourceFile& main = m_files.append();
	main.name = options.name ? options.name : "";

	WRError err = expandFile( 0, source, size, 0, options );
	if ( !err && m_sawInclude )
	{
		m_files[0].text.set( source, (unsigned int)size ); // kept for the debugger
	}
	return err;
}

void WRSourceSet::append( const char* text, int size )
{
	if ( size <= 0 )
	{
		return;
	}
	m_expanded.append( text, (unsigned int)size );
	for( int i=0; i<size; ++i )
	{
		if ( text[i] == '\n' )
		{
			++m_virtualLine;
		}
	}
}

void WRSourceSet::addSegment( int file, int line )
{
	WRSourceSegment* last = m_segments.tail();
	if ( last && last->virtualLine == m_virtualLine )
	{
		// nothing came from the previous one (an empty file)
		last->file = file;
		last->line = line;
		return;
	}
	WRSourceSegment& s = m_segments.append();
	s.virtualLine = m_virtualLine;
	s.file = file;
	s.line = line;
}

void WRSourceSet::locate( int virtualLine, int& file, int& line ) const
{
	file = 0;
	line = virtualLine;
	for( unsigned int i=0; i<m_segments.count(); ++i )
	{
		const WRSourceSegment& s = m_segments[i];
		if ( s.virtualLine > virtualLine )
		{
			break;
		}
		file = s.file;
		line = s.line + (virtualLine - s.virtualLine);
	}
}

void WRSourceSet::fileText( int file, const char*& text, int& size ) const
{
	if ( file == 0 && !m_sawInclude )
	{
		text = m_source;
		size = m_size;
		return;
	}
	text = m_files[file].text.c_str();
	size = (int)m_files[file].text.size();
}

// copies 'text' to the expanded source (once anything was included),
// replacing each top level include "name" with the file it names. Comments
// and string or character literals are skipped like the tokenizer skips
// them, braces tell the top level from function bodies, struct bodies and
// blocks
WRError WRSourceSet::expandFile( int file, const char* text, int size, int depth, const WRCompileOptions& options )
{
	addSegment( file, 1 );

	int line = 1;
	int braces = 0;
	int copied = 0; // text before this is in m_expanded already (or needs not be)
	int pos = 0;
	while( pos < size )
	{
		char c = text[pos];

		if ( c == '\n' )
		{
			++line;
			++pos;
		}
		else if ( c == '/' && pos + 1 < size && text[pos + 1] == '/' )
		{
			while( pos < size && text[pos] != '\n' )
			{
				++pos;
			}
		}
		else if ( c == '/' && pos + 1 < size && text[pos + 1] == '*' )
		{
			pos += 2;
			while( pos < size )
			{
				if ( text[pos] == '*' && pos + 1 < size && text[pos + 1] == '/' )
				{
					pos += 2;
					break;
				}
				if ( text[pos] == '\n' )
				{
					++line;
				}
				++pos;
			}
		}
		else if ( c == '"' || c == '\'' )
		{
			++pos;
			while( pos < size && text[pos] != c )
			{
				if ( text[pos] == '\\' && pos + 1 < size )
				{
					++pos;
				}
				if ( text[pos] == '\n' )
				{
					++line;
				}
				++pos;
			}
			++pos;
		}
		else if ( c == '{' )
		{
			++braces;
			++pos;
		}
		else if ( c == '}' )
		{
			if ( braces )
			{
				--braces;
			}
			++pos;
		}
		else if ( wr_isNameChar(c) )
		{
			int start = pos;
			while( pos < size && wr_isNameChar(text[pos]) )
			{
				++pos;
			}
			if ( pos - start != 7 || strncmp(text + start, "include", 7) )
			{
				continue;
			}

			// include "name": anything else is the name include
			int at = pos;
			int directiveLines = 0;
			while( at < size && (text[at] == ' ' || text[at] == '\t' || text[at] == '\r' || text[at] == '\n') )
			{
				directiveLines += (text[at] == '\n') ? 1 : 0;
				++at;
			}
			if ( at >= size || text[at] != '"' )
			{
				continue;
			}
			int nameStart = ++at;
			while( at < size && text[at] != '"' && text[at] != '\n' )
			{
				++at;
			}
			if ( at >= size || text[at] != '"' )
			{
				continue; // an unterminated string, the parser reports it
			}
			WRstr name( text + nameStart, (unsigned int)(at - nameStart) );
			++at;
			int end = at;
			while( end < size && (text[end] == ' ' || text[end] == '\t') )
			{
				++end;
			}
			if ( end < size && text[end] == ';' )
			{
				at = end + 1;
			}

			m_errFile = file;
			m_errLine = line;
			if ( braces )
			{
				return WR_ERR_include_not_at_top_level;
			}
			if ( !options.include )
			{
				return WR_ERR_include_not_supported;
			}
			if ( depth >= WRENCH_MAX_INCLUDE_DEPTH )
			{
				return WR_ERR_include_too_deep;
			}

			WRstr source;
			WRstr resolved;
			if ( !options.include(name.c_str(), m_files[file].name.c_str(), source, resolved, options.includeUser) )
			{
				return WR_ERR_include_not_found;
			}

			// everything up to the directive, which itself goes away
			m_sawInclude = true;
			append( text + copied, start - copied );
			copied = at;
			line += directiveLines;

			bool seen = false;
			for( unsigned int f=0; f<m_files.count() && !seen; ++f )
			{
				seen = (m_files[f].name == resolved);
			}
			if ( seen )
			{
				if ( directiveLines )
				{
					addSegment( file, line ); // the lines the directive spanned are gone
				}
				pos = at;
				continue;
			}

			// the file starts on a line of its own and ends with one
			if ( m_expanded.size() && m_expanded[m_expanded.size() - 1] != '\n' )
			{
				append( "\n", 1 );
			}
			int index = (int)m_files.count();
			WRSourceFile& added = m_files.append();
			added.name = resolved;
			added.text = source;
			WRError err = expandFile( index, source.c_str(), (int)source.size(), depth + 1, options );
			if ( err )
			{
				return err;
			}
			if ( m_expanded.size() && m_expanded[m_expanded.size() - 1] != '\n' )
			{
				append( "\n", 1 );
			}
			addSegment( file, line );
			pos = at;
		}
		else
		{
			++pos;
		}
	}

	if ( m_sawInclude )
	{
		append( text + copied, size - copied );
	}
	return WR_ERR_None;
}

#endif
