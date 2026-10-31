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

#include "utils/wrench_include_host.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>

std::string WrenchIncludeResolver::normalize( const std::string& path )
{
	try
	{
		std::string n = std::filesystem::u8path( path ).lexically_normal().generic_u8string();
		return n.empty() ? path : n;
	}
	catch ( const std::exception& )
	{
		return path;
	}
}

std::string WrenchIncludeResolver::identity( const std::string& path ) const
{
	std::string id;
	try
	{
		std::error_code ec;
		std::filesystem::path p = std::filesystem::weakly_canonical( std::filesystem::absolute(std::filesystem::u8path(path), ec), ec );
		id = p.generic_u8string();
	}
	catch ( const std::exception& )
	{
	}
	if ( id.empty() )
	{
		id = normalize( path );
	}
#if defined(_WIN32)
	std::transform( id.begin(), id.end(), id.begin(), [](char c) { return (char)tolower((unsigned char)c); } );
#endif
	return id;
}

bool wr_readHostFile( const std::string& path, std::string& text )
{
	try
	{
		std::ifstream in( std::filesystem::u8path(path), std::ios::binary );
		if ( !in )
		{
			return false;
		}
		std::ostringstream all;
		all << in.rdbuf();
		text = all.str();
		return true;
	}
	catch ( const std::exception& )
	{
		return false;
	}
}

bool WrenchIncludeResolver::readFile( const std::string& path, std::string& text )
{
	auto over = overrides.find( path );
	if ( over != overrides.end() )
	{
		text = over->second;
		return true;
	}
	return wr_readHostFile( path, text );
}

void WrenchIncludeResolver::setMain( const std::string& path )
{
	std::string name = normalize( path );
	m_seen[identity(name)] = name;
	m_mainName = name;
	m_mainDir.clear();
	try
	{
		std::error_code ec;
		m_mainDir = std::filesystem::absolute( std::filesystem::u8path(name), ec ).lexically_normal().parent_path().generic_u8string();
	}
	catch ( const std::exception& )
	{
	}
}

std::string WrenchIncludeResolver::locate( const std::string& name, const std::string& mainPath, const std::vector<std::string>& searchPaths )
{
	try
	{
		std::error_code ec;
		std::filesystem::path p = std::filesystem::u8path( name );
		if ( p.is_absolute() )
		{
			return std::filesystem::is_regular_file( p, ec ) ? name : std::string();
		}
		std::vector<std::filesystem::path> dirs;
		if ( !mainPath.empty() )
		{
			dirs.push_back( std::filesystem::absolute(std::filesystem::u8path(mainPath), ec).parent_path() );
		}
		for ( const std::string& dir : searchPaths )
		{
			dirs.push_back( std::filesystem::u8path(dir) );
		}
		for ( const std::filesystem::path& dir : dirs )
		{
			std::filesystem::path candidate = (dir / p).lexically_normal();
			if ( std::filesystem::is_regular_file(candidate, ec) )
			{
				return candidate.generic_u8string();
			}
		}
	}
	catch ( const std::exception& )
	{
	}
	return std::string();
}

bool WrenchIncludeResolver::resolve( const std::string& name, const std::string& includer, std::string& text, std::string& resolved )
{
	std::vector<std::string> candidates;
	try
	{
		std::filesystem::path p = std::filesystem::u8path( name );
		if ( p.is_absolute() )
		{
			candidates.push_back( name );
		}
		else
		{
			// an included file's name is relative to the main file's folder
			std::filesystem::path from = includer.empty() ? std::filesystem::path() : std::filesystem::u8path( includer );
			if ( !includer.empty() && !from.is_absolute() && normalize(includer) != m_mainName && !m_mainDir.empty() )
			{
				from = std::filesystem::u8path( m_mainDir ) / from;
			}
			from = from.parent_path();
			candidates.push_back( (from / p).generic_u8string() );
			for ( const std::string& dir : searchPaths )
			{
				candidates.push_back( (std::filesystem::u8path(dir) / p).generic_u8string() );
			}
		}
	}
	catch ( const std::exception& )
	{
		return false;
	}

	for ( const std::string& candidate : candidates )
	{
		std::string path = normalize( candidate );
		if ( !readFile(path, text) )
		{
			continue;
		}

		// a file reached under another spelling keeps its first name, the
		// compiler then sees it was included already
		std::string id = identity( path );
		auto seen = m_seen.find( id );
		if ( seen != m_seen.end() )
		{
			resolved = seen->second;
		}
		else
		{
			// named relative to the main file's folder where a relative
			// path leads there (not across drives), else by its absolute
			// path. A relative name that is how the main file was named
			// would read as the main file, it is absolute too
			std::string named = path;
			if ( !m_mainDir.empty() )
			{
				std::error_code ec;
				std::filesystem::path full = std::filesystem::absolute( std::filesystem::u8path(path), ec ).lexically_normal();
				std::filesystem::path relative = full.lexically_relative( std::filesystem::u8path(m_mainDir) );
				if ( !full.empty() )
				{
					named = full.generic_u8string();
				}
				if ( !relative.empty() && relative.generic_u8string() != m_mainName )
				{
					named = relative.generic_u8string();
				}
			}
			m_seen[id] = named;
			m_paths[named] = path;
			resolved = named;
			filesRead.push_back( path );
		}
		return true;
	}
	return false;
}

void WrenchIncludeResolver::localize( std::string& message, std::string& file ) const
{
	auto found = m_paths.find( file );
	if ( found == m_paths.end() || found->second == file )
	{
		return;
	}
	std::string named = file + ":";
	if ( message.compare(0, named.size(), named) == 0 )
	{
		message = found->second + message.substr( file.size() );
	}
	file = found->second;
}

bool WrenchIncludeResolver::callback( const char* name, const char* includer, WRstr& source, WRstr& resolved, void* usr )
{
	WrenchIncludeResolver* self = (WrenchIncludeResolver*)usr;
	std::string text;
	std::string where;
	if ( !self || !self->resolve(name, includer ? includer : "", text, where) )
	{
		return false;
	}
	source.set( text.data(), (unsigned int)text.size() );
	resolved.set( where.data(), (unsigned int)where.size() );
	return true;
}

WRError wr_compileWithIncludes( const std::string& path, const std::string& text, uint8_t flags, WrenchIncludeResolver* includes,
								std::vector<uint8_t>& out, std::string* message, int* line, int* column, std::string* file )
{
	std::string name = WrenchIncludeResolver::normalize( path );
	if ( includes )
	{
		includes->setMain( name );
	}

	WRCompileOptions options;
	options.flags = flags;
	options.name = name.c_str();
	options.include = includes ? WrenchIncludeResolver::callback : 0;
	options.includeUser = includes;

	unsigned char* bytes = 0;
	int len = 0;
	WRstr msg;
	WRstr where;
	WRError err = wr_compileWithOptions( text.c_str(), (int)text.size(), &bytes, &len, options, &msg, line, column, &where );
	out.clear();
	if ( !err )
	{
		out.assign( bytes, bytes + len );
	}
	g_free( bytes );
	std::string said = msg.c_str();
	std::string in = where.c_str();
	if ( err && includes )
	{
		includes->localize( said, in );
	}
	if ( message )
	{
		*message = said;
	}
	if ( file )
	{
		*file = in;
	}
	return err;
}

std::vector<std::string> wr_includeOptions( int argn, char* argv[] )
{
	std::vector<std::string> dirs;
	for ( int a = 1; a < argn; ++a )
	{
		std::string arg = argv[a];
		if ( arg == "-I" && a + 1 < argn )
		{
			dirs.push_back( argv[++a] );
		}
		else if ( arg.size() > 2 && arg.compare(0, 2, "-I") == 0 )
		{
			dirs.push_back( arg.substr(2) );
		}
	}
	return dirs;
}
