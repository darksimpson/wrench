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

#ifdef WRENCH_INCLUDE_DEBUG_CLIENT

#include "utils/wrench_project.h"

#include <filesystem>
#include <stdio.h>
#include <ctype.h>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

static std::wstring wr_widen( const std::string& text, UINT codePage )
{
	if ( text.empty() )
	{
		return std::wstring();
	}
	int n = MultiByteToWideChar( codePage, 0, text.data(), (int)text.size(), 0, 0 );
	std::wstring wide( n > 0 ? n : 0, L'\0' );
	if ( n > 0 )
	{
		MultiByteToWideChar( codePage, 0, text.data(), (int)text.size(), &wide[0], n );
	}
	return wide;
}

static std::string wr_narrowUtf8( const std::wstring& wide )
{
	if ( wide.empty() )
	{
		return std::string();
	}
	int n = WideCharToMultiByte( CP_UTF8, 0, wide.data(), (int)wide.size(), 0, 0, 0, 0 );
	std::string text( n > 0 ? n : 0, '\0' );
	if ( n > 0 )
	{
		WideCharToMultiByte( CP_UTF8, 0, wide.data(), (int)wide.size(), &text[0], n, 0, 0 );
	}
	return text;
}
#endif

FILE* wr_openFile( const std::string& utf8Path, const char* mode )
{
#if defined(_WIN32)
	return _wfopen( wr_widen(utf8Path, CP_UTF8).c_str(), wr_widen(mode, CP_UTF8).c_str() );
#else
	return fopen( utf8Path.c_str(), mode );
#endif
}

std::string wr_utf8FromArg( const char* arg )
{
	if ( !arg )
	{
		return std::string();
	}
#if defined(_WIN32)
	return wr_narrowUtf8( wr_widen(arg, CP_ACP) );
#else
	return arg;
#endif
}

namespace
{

const char* const c_libraryNames[] =
{
	"math", "std", "io", "string", "msg", "sys", "serialize", "debug", "tcp", "container", 0
};

bool readFile( const std::string& path, std::string& out )
{
	out.clear();
	FILE* f = wr_openFile( path, "rb" );
	if ( !f )
	{
		return false;
	}
	char buf[8192];
	size_t n;
	while ( (n = fread(buf, 1, sizeof(buf), f)) > 0 )
	{
		out.append( buf, n );
	}
	fclose( f );
	return true;
}

bool identStart( char c )
{
	return isalpha( (unsigned char)c ) || c == '_';
}

bool identChar( char c )
{
	return isalnum( (unsigned char)c ) || c == '_';
}

// "name" or "ns::name"
bool validName( const std::string& name )
{
	size_t p = 0;
	for ( int part = 0; part < 2; ++part )
	{
		if ( p >= name.size() || !identStart(name[p]) )
		{
			return false;
		}
		while ( p < name.size() && identChar(name[p]) )
		{
			++p;
		}
		if ( p == name.size() )
		{
			return true;
		}
		if ( part == 1 || name.compare(p, 2, "::") != 0 )
		{
			return false;
		}
		p += 2;
	}
	return false;
}

std::string trim( const std::string& s )
{
	size_t a = s.find_first_not_of( " \t\r\n" );
	if ( a == std::string::npos )
	{
		return std::string();
	}
	size_t b = s.find_last_not_of( " \t\r\n" );
	return s.substr( a, b - a + 1 );
}

// "name( a, b? )" into its parts
bool parseSignature( const std::string& text, WrenchNative& n, std::string& error )
{
	size_t open = text.find( '(' );
	n.name = trim( text.substr(0, open) );
	if ( open == std::string::npos )
	{
		return true;
	}
	size_t close = text.find( ')', open );
	if ( close == std::string::npos || trim(text.substr(close + 1)).size() )
	{
		error = "'" + text + "' is not a signature like name( a, b )";
		return false;
	}
	n.paramsKnown = true;
	std::string inner = text.substr( open + 1, close - open - 1 );
	size_t s = 0;
	while ( s <= inner.size() )
	{
		size_t c = inner.find( ',', s );
		std::string part = trim( inner.substr(s, c == std::string::npos ? std::string::npos : c - s) );
		if ( !part.empty() )
		{
			n.params.push_back( part );
		}
		if ( c == std::string::npos )
		{
			break;
		}
		s = c + 1;
	}
	return true;
}

// where a function is declared in wrench.json text: the value of a "name"
// key ("name": "f"), or a signature string ("f( a )"). Plain prefix matches
// would land on parameter names or keys
size_t findDeclaration( const std::string& text, const std::string& name, bool signature, size_t from )
{
	std::string quoted = "\"" + name;
	for ( size_t at = text.find(quoted, from); at != std::string::npos; at = text.find(quoted, at + 1) )
	{
		size_t after = at + quoted.size();
		char next = after < text.size() ? text[after] : 0;
		if ( signature )
		{
			// "f" or "f(" or "f (" opening a string entry
			size_t p = after;
			while ( p < text.size() && (text[p] == ' ' || text[p] == '\t') )
			{
				++p;
			}
			if ( next == '"' || (p < text.size() && text[p] == '(') )
			{
				return at;
			}
			continue;
		}
		if ( next != '"' )
		{
			continue;
		}
		// preceded by "name" :
		size_t p = at;
		while ( p > 0 && (text[p - 1] == ' ' || text[p - 1] == '\t' || text[p - 1] == '\r' || text[p - 1] == '\n') )
		{
			--p;
		}
		if ( p == 0 || text[p - 1] != ':' )
		{
			continue;
		}
		--p;
		while ( p > 0 && (text[p - 1] == ' ' || text[p - 1] == '\t' || text[p - 1] == '\r' || text[p - 1] == '\n') )
		{
			--p;
		}
		if ( p >= 6 && text.compare(p - 6, 6, "\"name\"") == 0 )
		{
			return at;
		}
	}
	return std::string::npos;
}

void lineColumn( const std::string& text, size_t offset, int& line, int& column )
{
	line = 0;
	column = 0;
	for ( size_t i = 0; i < offset && i < text.size(); ++i )
	{
		if ( text[i] == '\n' )
		{
			++line;
			column = 0;
		}
		else if ( (text[i] & 0xC0) != 0x80 )
		{
			++column;
		}
	}
}

}

std::string WrenchNative::paramName( const std::string& param )
{
	std::string p = param;
	if ( p.size() >= 3 && p.compare(p.size() - 3, 3, "...") == 0 )
	{
		p = p.substr( 0, p.size() - 3 );
	}
	else if ( !p.empty() && p.back() == '?' )
	{
		p.pop_back();
	}
	p = trim( p );
	return p.empty() ? std::string( "..." ) : p;
}

std::string WrenchNative::signature() const
{
	if ( !paramsKnown )
	{
		return name + "( ... )";
	}
	std::string s = name + "(";
	for ( size_t i = 0; i < params.size(); ++i )
	{
		s += (i ? ", " : " ") + params[i];
	}
	return s + (params.empty() ? ")" : " )");
}

std::string WrenchProject::find( const std::string& scriptPath )
{
	// u8path throws on bytes that are not UTF-8
	try
	{
		std::error_code ec;
		std::filesystem::path dir = std::filesystem::absolute( std::filesystem::u8path(scriptPath), ec ).parent_path();
		if ( ec )
		{
			return std::string();
		}
		for ( int depth = 0; depth < 64 && !dir.empty(); ++depth )
		{
			std::filesystem::path candidate = dir / "wrench.json";
			if ( std::filesystem::is_regular_file(candidate, ec) )
			{
				return candidate.u8string();
			}
			std::filesystem::path parent = dir.parent_path();
			if ( parent == dir )
			{
				break;
			}
			dir = parent;
		}
	}
	catch ( const std::exception& )
	{
	}
	return std::string();
}

bool WrenchProject::load( const std::string& path, std::string& error )
{
	*this = WrenchProject();

	std::string text;
	if ( !readFile(path, text) )
	{
		error = "cannot read " + path;
		return false;
	}

	Json root;
	std::string parseError;
	if ( !Json::parse(text, root, &parseError) || !root.isObject() )
	{
		error = path + ": " + (parseError.empty() ? std::string( "expected a JSON object" ) : parseError);
		return false;
	}

	// include "file" search folders, relative to this file's folder
	const Json& include = root["include"];
	if ( include.isArray() )
	{
		std::filesystem::path base = std::filesystem::u8path( path ).parent_path();
		for ( const Json& dir : include.items() )
		{
			if ( !dir.isString() )
			{
				error = path + ": \"include\" lists folders as strings";
				return false;
			}
			std::filesystem::path p = std::filesystem::u8path( dir.asString() );
			m_includePaths.push_back( (p.is_absolute() ? p : base / p).lexically_normal().generic_u8string() );
		}
	}
	else if ( !include.isNull() )
	{
		error = path + ": \"include\" is a list of folders";
		return false;
	}

	const Json& libraries = root["libraries"];
	if ( libraries.isArray() )
	{
		m_allLibraries = false;
		for ( const Json& l : libraries.items() )
		{
			std::string name = l.asString( "" );
			if ( name == "all" )
			{
				m_allLibraries = true;
				continue;
			}
			bool known = false;
			for ( const char* const* n = c_libraryNames; *n; ++n )
			{
				known = known || name == *n;
			}
			if ( !known )
			{
				error = path + ": unknown library '" + name + "', expected math, std, io, string, msg, sys, serialize, debug, tcp, container or all";
				return false;
			}
			m_libraries.push_back( name );
		}
	}
	else if ( !libraries.isNull() && libraries.asString("") != "all" )
	{
		error = path + ": \"libraries\" must be a list of library names or \"all\"";
		return false;
	}

	std::string undeclared = root["undeclaredCalls"].asString( "warning" );
	if ( undeclared == "warning" )
	{
		m_undeclared = Warning;
	}
	else if ( undeclared == "error" )
	{
		m_undeclared = Error;
	}
	else if ( undeclared == "ignore" )
	{
		m_undeclared = Ignore;
	}
	else
	{
		error = path + ": \"undeclaredCalls\" must be \"warning\", \"error\" or \"ignore\"";
		return false;
	}

	const Json& functions = root["functions"];
	if ( !functions.isNull() && !functions.isArray() )
	{
		error = path + ": \"functions\" must be a list";
		return false;
	}

	size_t searchFrom = text.find( "\"functions\"" );
	for ( const Json& f : functions.items() )
	{
		WrenchNative n;
		if ( f.isString() )
		{
			if ( !parseSignature(f.asString(), n, error) )
			{
				error = path + ": " + error;
				return false;
			}
		}
		else if ( f.isObject() )
		{
			n.name = trim( f["name"].asString("") );
			const Json& params = f["params"];
			if ( params.isArray() )
			{
				n.paramsKnown = true;
				for ( const Json& p : params.items() )
				{
					n.params.push_back( trim(p.asString("")) );
				}
			}
			n.doc = f["doc"].asString( "" );
			n.returns = f["returns"];
		}
		else
		{
			error = path + ": every entry of \"functions\" is an object or a signature string";
			return false;
		}

		if ( !validName(n.name) )
		{
			error = path + ": '" + n.name + "' is not a function name (name or lib::name)";
			return false;
		}
		if ( native(n.name) )
		{
			error = path + ": '" + n.name + "' is declared twice";
			return false;
		}

		n.hash = wr_hashStr( n.name.c_str() );
		if ( n.paramsKnown )
		{
			n.minArgs = 0;
			n.maxArgs = 0;
			bool optional = false;
			for ( const std::string& p : n.params )
			{
				if ( p.size() >= 3 && p.compare(p.size() - 3, 3, "...") == 0 )
				{
					n.maxArgs = -1;
					break;
				}
				++n.maxArgs;
				if ( !p.empty() && p.back() == '?' )
				{
					optional = true;
				}
				else if ( !optional )
				{
					++n.minArgs;
				}
			}
		}

		// point editors at the declaration
		size_t at = findDeclaration( text, n.name, f.isString(), searchFrom == std::string::npos ? 0 : searchFrom );
		if ( at != std::string::npos )
		{
			lineColumn( text, at + 1, n.line, n.column );
			searchFrom = at + 1;
		}

		m_natives.push_back( n );
	}

	m_path = path;
	return true;
}

const WrenchNative* WrenchProject::native( const std::string& name ) const
{
	for ( const WrenchNative& n : m_natives )
	{
		if ( n.name == name )
		{
			return &n;
		}
	}
	return nullptr;
}

const WrenchNative* WrenchProject::native( uint32_t hash ) const
{
	for ( const WrenchNative& n : m_natives )
	{
		if ( n.hash == hash )
		{
			return &n;
		}
	}
	return nullptr;
}

std::string WrenchProject::libraryOf( const std::string& function )
{
	if ( function == "print" || function == "println" )
	{
		return "host";
	}
	size_t colons = function.find( "::" );
	if ( colons == std::string::npos || colons == 0 )
	{
		return std::string();
	}
	std::string ns = function.substr( 0, colons );
	if ( ns == "math" || ns == "io" || ns == "msg" || ns == "sys" || ns == "debug" || ns == "tcp" )
	{
		return ns;
	}
	if ( ns == "str" )
	{
		return "string";
	}
	if ( ns == "array" || ns == "hash" || ns == "list" || ns == "queue" || ns == "stack" )
	{
		return "container";
	}
	if ( ns == "time" )
	{
		return "std";
	}
	if ( ns == "std" )
	{
		return (function == "std::serialize" || function == "std::deserialize") ? "serialize" : "std";
	}
	return std::string();
}

const char* const* WrenchProject::libraryNames()
{
	return c_libraryNames;
}

bool WrenchProject::hasLibrary( const std::string& library ) const
{
	if ( library == "host" )
	{
		return false; // print and println count as natives here
	}
	if ( m_allLibraries )
	{
		return true;
	}
	for ( const std::string& l : m_libraries )
	{
		if ( l == library )
		{
			return true;
		}
	}
	return false;
}

void WrenchProject::loadLibraries( WRState* w ) const
{
	if ( m_allLibraries )
	{
		wr_loadAllLibs( w );
		return;
	}
	for ( const std::string& l : m_libraries )
	{
		if ( l == "math" ) { wr_loadMathLib( w ); }
		else if ( l == "std" ) { wr_loadStdLib( w ); }
		else if ( l == "io" ) { wr_loadIOLib( w ); }
		else if ( l == "string" ) { wr_loadStringLib( w ); }
		else if ( l == "msg" ) { wr_loadMessageLib( w ); }
		else if ( l == "sys" ) { wr_loadSysLib( w ); }
		else if ( l == "serialize" ) { wr_loadSerializeLib( w ); }
		else if ( l == "debug" ) { wr_loadDebugLib( w ); }
		else if ( l == "tcp" ) { wr_loadTCPLib( w ); }
		else if ( l == "container" ) { wr_loadContainerLib( w ); }
	}
}

bool wr_projectForScript( const std::string& scriptPath, const std::string& explicitPath, WrenchProject& project, std::string& error )
{
	project = WrenchProject();
	if ( explicitPath == "none" )
	{
		return true;
	}
	std::string path = explicitPath.empty() ? WrenchProject::find( scriptPath ) : explicitPath;
	if ( path.empty() )
	{
		return true;
	}
	return project.load( path, error );
}

std::string wr_functionNameForHash( uint32_t hash, const std::string& source, const WrenchProject* project )
{
	if ( project )
	{
		if ( const WrenchNative* n = project->native(hash) )
		{
			return n->name;
		}
	}

	// identifiers in the source, namespace qualified ones included
	size_t p = 0;
	while ( p < source.size() )
	{
		char c = source[p];
		if ( c == '/' && p + 1 < source.size() && source[p + 1] == '/' )
		{
			size_t e = source.find( '\n', p );
			p = (e == std::string::npos) ? source.size() : e;
			continue;
		}
		if ( c == '/' && p + 1 < source.size() && source[p + 1] == '*' )
		{
			size_t e = source.find( "*/", p + 2 );
			p = (e == std::string::npos) ? source.size() : e + 2;
			continue;
		}
		if ( c == '"' || c == '\'' )
		{
			size_t e = p + 1;
			while ( e < source.size() && source[e] != c && source[e] != '\n' )
			{
				e += (source[e] == '\\') ? 2 : 1;
			}
			p = e + 1;
			continue;
		}
		if ( !identStart(c) || (p > 0 && identChar(source[p - 1])) )
		{
			++p;
			continue;
		}
		size_t e = p;
		while ( e < source.size() && identChar(source[e]) )
		{
			++e;
		}
		if ( e + 2 < source.size() && source[e] == ':' && source[e + 1] == ':' && identStart(source[e + 2]) )
		{
			e += 2;
			while ( e < source.size() && identChar(source[e]) )
			{
				++e;
			}
		}
		std::string name = source.substr( p, e - p );
		if ( wr_hashStr(name.c_str()) == hash )
		{
			return name;
		}
		p = e;
	}
	return std::string();
}

std::string wr_describeMissingFunction( uint8_t error, uint32_t hash, const std::string& source, const WrenchProject* project )
{
	if ( error != WR_ERR_function_not_found && error != WR_ERR_lib_function_not_found )
	{
		return std::string();
	}
	std::string name = wr_functionNameForHash( hash, source, project );
	if ( name.empty() )
	{
		char text[64];
		snprintf( text, sizeof(text), "the function with name hash 0x%08X", (unsigned)hash );
		return std::string( text ) + " is not provided by the target";
	}

	std::string library = WrenchProject::libraryOf( name );
	if ( !library.empty() && library != "host" )
	{
		return "'" + name + "' is not provided by the target (its '" + library + "' library is not loaded there)";
	}
	return "'" + name + "' is not provided by the target";
}

#endif
