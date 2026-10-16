#ifndef _WRENCH_DEBUG_EVAL_H
#define _WRENCH_DEBUG_EVAL_H
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

// Host side evaluation of debugger expressions against a stopped target:
//
// x ::g a[3] p.member h["key"] h[k] (variable paths)
// literals: 12 0x1F 1.5 "text"
// operators: ! - * / % + - < <= > >= == != && || and parentheses
//
// Variables are fetched from the target as needed; nothing executes there.

#ifdef WRENCH_INCLUDE_DEBUG_CLIENT

#include "debug/debug_client.h"

#include <string>

// where a value lives, so it can be written back
struct WrenchDebugLocation
{
	enum Kind
	{
		None,
		Local,
		Global,
		Child,
	};

	Kind kind = None;
	int frame = 0;
	int slot = 0;
	WrenchDebugValue parent;
	uint32_t index = 0;
};

class WrenchDebugEvaluator
{
public:
	WrenchDebugEvaluator( WrenchDebugClient& client, const WrenchDebugProgram& program,
												const std::vector<WrenchDebugStackFrame>& frames, int frame )
		: m_client( client ), m_program( program ), m_frames( frames ), m_frame( frame ) {}

	// evaluate any expression
	bool evaluate( const std::string& expression, WrenchDebugValue& result, std::string& error );

	// resolve a plain variable path and report where it lives
	bool resolve( const std::string& path, WrenchDebugValue& result, WrenchDebugLocation& where, std::string& error );

	// assign 'valueText' (a literal or expression) to the path
	bool assign( const std::string& path, const std::string& valueText, WrenchDebugValue& result, std::string& error );

	// write a value to a known location
	bool store( const WrenchDebugLocation& where, const WrenchDebugValue& value, WrenchDebugValue& result, std::string& error );

	// expand {expression} parts of a log message
	std::string interpolate( const std::string& message );

	static bool truthy( const WrenchDebugValue& v );

	// position of the '=' of "path = value": outside strings and brackets
	// and not part of ==, !=, <= or >=. npos when there is none
	static size_t findAssignment( const std::string& text );

	// full text of a value: strings fetched completely, short arrays inline
	std::string describe( const WrenchDebugValue& v, int depth = 1 );

private:
	struct Parser;
	friend struct Parser;

	bool lookupName( const std::string& name, bool forceGlobal, WrenchDebugValue& result,
									WrenchDebugLocation& where, std::string& error );
	bool child( const WrenchDebugValue& parent, const WrenchDebugValue& key, bool member,
							WrenchDebugValue& result, WrenchDebugLocation& where, std::string& error );

	WrenchDebugClient& m_client;
	const WrenchDebugProgram& m_program;
	const std::vector<WrenchDebugStackFrame>& m_frames;
	int m_frame;
};

#endif

#endif
