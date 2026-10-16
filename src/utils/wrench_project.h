#ifndef _WRENCH_PROJECT_H
#define _WRENCH_PROJECT_H
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

// wrench.json describes the host application scripts run in: the native
// functions it registers and the standard libraries it loads. The language
// server, the local debug target and dbgserver read it so they all see the
// same functions as the real host. It is found by walking up from the
// folder of the script.
//
// {
// 	"libraries": [ "math", "string" ],
// 	"functions": [
// 		{ "name": "openDoc", "params": [ "path", "mode?" ], "doc": "Opens a document", "returns": 1 },
// 		{ "name": "ui::alert", "params": [ "text" ] },
// 		"log( values... )"
// 	],
// 	"undeclaredCalls": "warning"
// }
//
// "libraries": standard libraries the host loads (math, std, io, string,
// msg, sys, serialize, debug, tcp, container), "all" or missing for every
// one.
// "functions": objects or signature strings. A parameter ending in '?' is
// optional, one ending in "..." takes any number of arguments. Without
// "params" the argument count is not checked. "returns" is what the local
// debug target answers in place of the real function (default 0).
// "undeclaredCalls": "warning" (default), "error" or "ignore" for calls to
// anything that is neither declared nor a script function.

#ifdef WRENCH_INCLUDE_DEBUG_CLIENT

#include "wrench.h"
#include "utils/json.h"

#include <string>
#include <vector>

struct WrenchNative
{
	std::string name; // "openDoc" or "ui::alert"
	std::vector<std::string> params; // as written, with '?' and "..." marks
	bool paramsKnown = false;
	std::string doc;
	Json returns;
	uint32_t hash = 0;
	int minArgs = 0;
	int maxArgs = -1; // -1 for no limit

	// where it is declared in wrench.json, 0 based
	int line = 0;
	int column = 0;

	// "ui::alert( text )"
	std::string signature() const;

	// parameter name without its marks
	static std::string paramName( const std::string& param );
};

class WrenchProject
{
public:
	enum Severity
	{
		Ignore,
		Warning,
		Error,
	};

	// nearest wrench.json in the folder of 'scriptPath' or above, empty if none
	static std::string find( const std::string& scriptPath );

	bool load( const std::string& path, std::string& error );
	bool loaded() const { return !m_path.empty(); }
	const std::string& path() const { return m_path; }

	const std::vector<WrenchNative>& natives() const { return m_natives; }
	const WrenchNative* native( const std::string& name ) const;
	const WrenchNative* native( uint32_t hash ) const;

	// the standard library a function name belongs to: "math" for math::sin,
	// "host" for print and println (the wrench tools provide those, a host
	// application has to declare them), empty for any other name
	static std::string libraryOf( const std::string& function );

	// every standard library name, null terminated
	static const char* const* libraryNames();

	bool hasLibrary( const std::string& library ) const;
	bool allLibraries() const { return m_allLibraries; }

	// register the standard libraries the host has with 'w'
	void loadLibraries( WRState* w ) const;

	Severity undeclaredCalls() const { return m_undeclared; }

private:
	std::string m_path;
	std::vector<WrenchNative> m_natives;
	std::vector<std::string> m_libraries;
	bool m_allLibraries = true;
	Severity m_undeclared = Warning;
};

// Paths in the host tools are UTF-8 (DAP and LSP send UTF-8). On Windows a
// plain fopen() would read them in the ANSI code page, so files open through
// this, and command line arguments (ANSI there) go through wr_utf8FromArg
FILE* wr_openFile( const std::string& utf8Path, const char* mode );
std::string wr_utf8FromArg( const char* arg );

// the project for a script: 'explicitPath' when given ("none" turns the
// search off), else the nearest wrench.json. Returns false with 'error' set
// when the file exists but is not valid; project.loaded() tells whether a
// project applies
bool wr_projectForScript( const std::string& scriptPath, const std::string& explicitPath, WrenchProject& project, std::string& error );

// the name behind a function hash, from the identifiers in 'source' and the
// project's natives. Empty when nothing matches
std::string wr_functionNameForHash( uint32_t hash, const std::string& source, const WrenchProject* project );

// "'ui::alert' is not provided by the target" for function_not_found and
// lib_function_not_found errors, empty for other errors
std::string wr_describeMissingFunction( uint8_t error, uint32_t hash, const std::string& source, const WrenchProject* project );

#endif

#endif
