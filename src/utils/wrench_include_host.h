#ifndef _WRENCH_INCLUDE_HOST_H
#define _WRENCH_INCLUDE_HOST_H
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

// The host tools' side of include "file": included files come from disk.
// A relative name is looked for next to the file that includes it, then in
// each search folder in order; an absolute one is used as it is. Names are
// handed to the compiler normalized (forward slashes, no "." or ".."
// steps), relative ones stay relative, so messages and the debugger show
// them as written. Two spellings of one file count as one file.

#include "wrench.h"

#include <string>
#include <vector>
#include <map>

class WrenchIncludeResolver
{
public:
	// searched after the including file's own folder
	std::vector<std::string> searchPaths;

	// texts that take the place of files on disk (an editor's unsaved
	// buffers), by normalized path
	std::map<std::string, std::string> overrides;

	// every file read through this resolver, normalized, in order
	std::vector<std::string> filesRead;

	// a file's normalized name, the way the compiler gets to see it
	static std::string normalize( const std::string& path );

	// for WRCompileOptions::include, usr is the resolver
	static bool callback( const char* name, const char* includer, WRstr& source, WRstr& resolved, void* usr );

	bool resolve( const std::string& name, const std::string& includer, std::string& text, std::string& resolved );

	// tell the resolver which file it compiles first, so a file that
	// includes it does not compile it a second time
	void setMain( const std::string& path );

private:
	bool readFile( const std::string& path, std::string& text );
	std::string identity( const std::string& path ) const;

	// identity of a file (absolute, case folded where the file system
	// ignores case) to the first name it was handed out as
	std::map<std::string, std::string> m_seen;
};

// compile 'text', the contents of file 'path', with includes found by
// 'includes' (0 for none: include "file" is an error). On failure 'message'
// gets the compiler's message and line, column and file say where
WRError wr_compileWithIncludes( const std::string& path, const std::string& text, uint8_t flags, WrenchIncludeResolver* includes,
								std::vector<uint8_t>& out, std::string* message = 0, int* line = 0, int* column = 0, std::string* file = 0 );

// the search folders an -I option list names: "-I dir" and "-Idir", in order
std::vector<std::string> wr_includeOptions( int argn, char* argv[] );

// a whole file by its UTF-8 path, false when it cannot be read
bool wr_readHostFile( const std::string& path, std::string& text );

#endif
