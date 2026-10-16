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

// "wrench lsp": Language Server Protocol server on stdin/stdout.
// Diagnostics come from the real compiler, everything else from a tolerant
// lexer and symbol indexer that keeps working on broken code.

#include "wrench.h"

#ifdef WRENCH_INCLUDE_DEBUG_CLIENT

#include "utils/json.h"
#include "utils/json_stream.h"
#include "utils/wrench_project.h"

#include <string>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <filesystem>
#include <chrono>
#include <ctype.h>

namespace
{

struct WrenchLibraryEntry
{
	const char* name;
	const char* signature;
	const char* doc;
	int kind; // 0 function, 1 constant, 2 member function, 3 keyword
};

const WrenchLibraryEntry c_library[] =
{
#include "utils/wrench_lsp_library.inc"
	{ 0, 0, 0, 0 }
};

struct Position
{
	int line = 0;
	int character = 0;
};

struct Range
{
	Position start;
	Position end;
};

// local path of a file URI, empty for other schemes:
// file:///c%3A/work/a.w gives c:/work/a.w, file://server/share/a.w gives the
// UNC path //server/share/a.w
std::string uriToPath( const std::string& uri )
{
	if ( uri.compare(0, 7, "file://") != 0 )
	{
		return std::string();
	}
	size_t pathStart = uri.find( '/', 7 );
	if ( pathStart == std::string::npos )
	{
		pathStart = uri.size();
	}
	std::string authority = uri.substr( 7, pathStart - 7 );
	std::string authorityLower = authority;
	std::transform( authorityLower.begin(), authorityLower.end(), authorityLower.begin(), ::tolower );
	std::string path;
	if ( !authority.empty() )
	{
		// "localhost" names this machine: file://localhost/c:/a.w is c:/a.w;
		// Windows reaches its own shares (file://localhost/c%24/a.w) as UNC
		bool local = authorityLower == "localhost";
#ifdef _WIN32
		bool drive = pathStart + 2 < uri.size() && isalpha((unsigned char)uri[pathStart + 1])
			&& (uri[pathStart + 2] == ':' || uri.compare(pathStart + 2, 3, "%3A") == 0 || uri.compare(pathStart + 2, 3, "%3a") == 0);
		local = local && drive;
#endif
		if ( !local )
		{
			path = "//" + authority;
		}
	}
	for ( size_t i = pathStart; i < uri.size(); ++i )
	{
		if ( uri[i] == '%' && i + 2 < uri.size() && isxdigit((unsigned char)uri[i + 1]) && isxdigit((unsigned char)uri[i + 2]) )
		{
			path += (char)strtol( uri.substr(i + 1, 2).c_str(), 0, 16 );
			i += 2;
		}
		else
		{
			path += uri[i];
		}
	}
	// "/c:/work" on Windows
	if ( path.size() > 2 && path[0] == '/' && isalpha((unsigned char)path[1]) && path[2] == ':' )
	{
		path = path.substr( 1 );
	}
	return path;
}

std::string pathToUri( const std::string& path )
{
	std::string p = path;
	std::replace( p.begin(), p.end(), '\\', '/' );
	std::string uri = (p.size() > 1 && p[1] == ':') ? "file:///" : "file://";
	if ( p.size() > 2 && p[0] == '/' && p[1] == '/' && p[2] != '/' )
	{
		// UNC path, the server becomes the authority
		uri = "file:";
	}
	for ( char c : p )
	{
		if ( isalnum((unsigned char)c) || strchr("/-_.~:", c) || (unsigned char)c >= 0x80 )
		{
			uri += c;
		}
		else
		{
			char hex[4];
			snprintf( hex, sizeof(hex), "%%%02X", (unsigned char)c );
			uri += hex;
		}
	}
	return uri;
}

bool before( const Position& a, const Position& b )
{
	return a.line < b.line || (a.line == b.line && a.character < b.character);
}

bool contains( const Range& r, const Position& p )
{
	return !before( p, r.start ) && !before( r.end, p );
}

Json positionJson( const Position& p )
{
	Json j = Json::object();
	j.set( "line", p.line );
	j.set( "character", p.character );
	return j;
}

Json rangeJson( const Range& r )
{
	Json j = Json::object();
	j.set( "start", positionJson(r.start) );
	j.set( "end", positionJson(r.end) );
	return j;
}

enum TokenKind
{
	TokIdent,
	TokNumber,
	TokString,
	TokChar,
	TokOp,
	TokComment,
};

struct Token
{
	TokenKind kind;
	std::string text;
	Range range; // whole token
	size_t offset;
	size_t end;
};

// UTF-16 code units of one UTF-8 lead byte, 0 for continuation bytes
int utf16Units( unsigned char c )
{
	if ( (c & 0xC0) == 0x80 )
	{
		return 0;
	}
	return (c >= 0xF0) ? 2 : 1;
}

void lex( const std::string& s, std::vector<Token>& out )
{
	out.clear();
	size_t p = 0;
	int line = 0;
	int col = 0;

	auto advance = [&]( size_t n )
	{
		for ( size_t i = 0; i < n && p < s.size(); ++i, ++p )
		{
			if ( s[p] == '\n' )
			{
				++line;
				col = 0;
			}
			else
			{
				col += utf16Units( (unsigned char)s[p] );
			}
		}
	};

	while ( p < s.size() )
	{
		unsigned char c = (unsigned char)s[p];
		if ( isspace(c) )
		{
			advance( 1 );
			continue;
		}

		Token t;
		t.offset = p;
		t.range.start.line = line;
		t.range.start.character = col;

		if ( c == '/' && p + 1 < s.size() && s[p + 1] == '/' )
		{
			t.kind = TokComment;
			size_t e = s.find( '\n', p );
			advance( (e == std::string::npos ? s.size() : e) - p );
		}
		else if ( c == '/' && p + 1 < s.size() && s[p + 1] == '*' )
		{
			t.kind = TokComment;
			size_t e = s.find( "*/", p + 2 );
			advance( (e == std::string::npos ? s.size() : e + 2) - p );
		}
		else if ( isalpha(c) || c == '_' || (c == ':' && p + 2 < s.size() && s[p + 1] == ':' && (isalpha((unsigned char)s[p + 2]) || s[p + 2] == '_')) )
		{
			// identifiers include namespace qualifiers: math::sin, ::g, Color::RED
			t.kind = TokIdent;
			size_t e = p;
			for ( ;; )
			{
				if ( e < s.size() && (isalnum((unsigned char)s[e]) || s[e] == '_') )
				{
					++e;
				}
				else if ( e + 2 < s.size() && s[e] == ':' && s[e + 1] == ':' && (isalpha((unsigned char)s[e + 2]) || s[e + 2] == '_') )
				{
					e += 2;
				}
				else
				{
					break;
				}
			}
			advance( e - p );
		}
		else if ( isdigit(c) || (c == '.' && p + 1 < s.size() && isdigit((unsigned char)s[p + 1])) )
		{
			t.kind = TokNumber;
			size_t e = p;
			while ( e < s.size() && (isalnum((unsigned char)s[e]) || s[e] == '.') )
			{
				++e;
			}
			advance( e - p );
		}
		else if ( c == '"' || c == '\'' )
		{
			t.kind = (c == '"') ? TokString : TokChar;
			size_t e = p + 1;
			while ( e < s.size() && s[e] != (char)c && s[e] != '\n' )
			{
				e += (s[e] == '\\') ? 2 : 1;
			}
			if ( e < s.size() && s[e] == (char)c )
			{
				++e;
			}
			advance( (e > s.size() ? s.size() : e) - p );
		}
		else
		{
			t.kind = TokOp;
			static const char* ops[] = { "<<=", ">>=", "==", "!=", "<=", ">=", "&&", "||", "++", "--", "+=", "-=", "*=", "/=",
																	"%=", "&=", "|=", "^=", "<<", ">>", "::", 0 };
			size_t len = 1;
			for ( int o = 0; ops[o]; ++o )
			{
				size_t l = strlen( ops[o] );
				if ( s.compare(p, l, ops[o]) == 0 )
				{
					len = l;
					break;
				}
			}
			advance( len );
		}

		t.end = p;
		t.text = s.substr( t.offset, t.end - t.offset );
		t.range.end.line = line;
		t.range.end.character = col;
		out.push_back( t );
	}
}

enum SymbolKind
{
	SymFunction,
	SymStruct,
	SymEnum,
	SymEnumMember,
	SymGlobal,
	SymLocal,
	SymParam,
	SymMember,
};

struct Symbol
{
	SymbolKind kind;
	std::string name; // as written at the declaration, enum members qualified
	Range nameRange;
	Range fullRange;
	int scope = -1; // owning function/struct symbol, -1 global
	int parent = -1; // struct of a member, enum of a member
	std::vector<std::string> params;
	std::string doc;
	std::string value; // enum member value text
};

struct Reference
{
	int symbol;
	Range range; // the name part only (no leading ::)
	bool declaration;
};

// deepest nesting the compiler is trusted with: it recurses once per
// bracket, statement nested without braces and ternary, on the stack of the
// main thread (a Windows debug build overflows its 1 MB at about 400 levels)
const int c_maxNesting = 200;

struct Document
{
	std::string uri;
	std::string text;
	int version = 0;
	bool dirty = true; // diagnostics out of date
	bool stale = true; // index out of date

	std::vector<Token> tokens;
	std::vector<Symbol> symbols;
	std::vector<Reference> refs;
	std::vector<std::pair<int, int> > folds; // line ranges
	int nesting = 0; // estimated recursion depth of the compiler
	int nestingToken = -1; // token where the nesting first goes over c_maxNesting

	void index();
	int tokenAt( const Position& p ) const; // code token covering p, -1 for none

private:
	int findLocal( int scope, const std::string& name ) const;
	int findGlobal( const std::string& name ) const;
	std::string commentBefore( size_t tokenIndex ) const;
	int addSymbol( SymbolKind kind, const Token& t, int scope, int parent = -1 );
	void measureNesting( const std::vector<size_t>& code );

	std::vector<char> m_declToken; // per token, set when it declares a symbol
	std::map<std::string, int> m_globals; // first global scope symbol of each name
	std::map<std::pair<int, std::string>, int> m_locals; // first local of each scope and name
};

int Document::addSymbol( SymbolKind kind, const Token& t, int scope, int parent )
{
	Symbol s;
	s.kind = kind;
	s.name = t.text;
	s.nameRange = t.range;
	s.fullRange = t.range;
	s.scope = scope;
	s.parent = parent;
	symbols.push_back( s );
	int index = (int)symbols.size() - 1;

	if ( scope == -1 && kind != SymParam )
	{
		m_globals.insert( std::make_pair(t.text, index) );
	}
	if ( kind == SymLocal || kind == SymParam || kind == SymMember || kind == SymEnumMember )
	{
		m_locals.insert( std::make_pair(std::make_pair(scope, t.text), index) );
	}

	// t may be a modified copy, the offset still names the token
	auto it = std::lower_bound( tokens.begin(), tokens.end(), t.offset, []( const Token& a, size_t offset ) { return a.offset < offset; } );
	if ( it != tokens.end() && it->offset == t.offset )
	{
		m_declToken[it - tokens.begin()] = 1;
	}

	Reference r;
	r.symbol = index;
	r.range = t.range;
	r.declaration = true;
	refs.push_back( r );
	return index;
}

std::string Document::commentBefore( size_t i ) const
{
	// doc comment: comments directly above the declaration
	std::vector<std::string> lines;
	int expectLine = tokens[i].range.start.line - 1;
	while ( i > 0 && tokens[i - 1].kind == TokComment && tokens[i - 1].range.end.line >= expectLine
					&& tokens[i - 1].range.end.line < tokens[i].range.start.line )
	{
		--i;
		std::string c = tokens[i].text;
		if ( c.compare(0, 2, "//") == 0 )
		{
			c = c.substr( 2 );
		}
		else if ( c.size() >= 4 )
		{
			c = c.substr( 2, c.size() - 4 );
		}
		// separator lines carry no meaning
		if ( c.find_first_not_of("-=*/ \t\r") != std::string::npos )
		{
			lines.insert( lines.begin(), c );
		}
		expectLine = tokens[i].range.start.line - 1;
	}

	std::string doc;
	for ( std::string& l : lines )
	{
		size_t a = l.find_first_not_of( " \t*" );
		size_t b = l.find_last_not_of( " \t\r" );
		if ( a != std::string::npos )
		{
			doc += l.substr( a, b - a + 1 ) + "\n";
		}
	}
	return doc;
}

int Document::findLocal( int scope, const std::string& name ) const
{
	auto it = m_locals.find( std::make_pair(scope, name) );
	return it == m_locals.end() ? -1 : it->second;
}

int Document::findGlobal( const std::string& name ) const
{
	auto it = m_globals.find( name );
	return it == m_globals.end() ? -1 : it->second;
}

int Document::tokenAt( const Position& p ) const
{
	// the last token starting at or before p, or the one before it when p
	// sits between two tokens ("name|(" belongs to name)
	auto it = std::upper_bound( tokens.begin(), tokens.end(), p, []( const Position& pos, const Token& t ) { return before(pos, t.range.start); } );
	int last = (int)(it - tokens.begin()) - 1;
	for ( int i = last - 1; i <= last; ++i )
	{
		if ( i >= 0 && tokens[i].kind != TokComment && contains(tokens[i].range, p) )
		{
			return i;
		}
	}
	return -1;
}

void Document::measureNesting( const std::vector<size_t>& code )
{
	// every bracket costs the compiler a recursion level, so do statements
	// nested without braces (if ( a ) while ( b ) ..., else if chains) and
	// ternaries (a ? b : c ? d : e)
	struct Level
	{
		int chain;
		int ternary;
	};
	std::vector<Level> levels( 1, Level{ 0, 0 } );
	int brackets = 0;
	int extra = 0; // chain and ternary summed over the open levels
	nesting = 0;
	nestingToken = -1;

	for ( size_t k = 0; k < code.size(); ++k )
	{
		const Token& t = tokens[code[k]];
		const Token* prev = k > 0 ? &tokens[code[k - 1]] : nullptr;
		if ( t.kind == TokOp && (t.text == "(" || t.text == "[" || t.text == "{") )
		{
			++brackets;
			levels.push_back( Level{ 0, 0 } );
		}
		else if ( t.kind == TokOp && (t.text == ")" || t.text == "]" || t.text == "}") )
		{
			if ( levels.size() > 1 )
			{
				extra -= levels.back().chain + levels.back().ternary;
				levels.pop_back();
				--brackets;
			}
		}
		else if ( t.kind == TokOp && t.text == "?" )
		{
			++levels.back().ternary;
			++extra;
		}
		else if ( t.kind == TokOp && (t.text == ";" || t.text == ",") )
		{
			extra -= levels.back().ternary;
			levels.back().ternary = 0;
		}
		else if ( t.kind == TokIdent && (t.text == "if" || t.text == "else" || t.text == "while" || t.text == "for" || t.text == "do") )
		{
			bool afterElse = prev && prev->kind == TokIdent && prev->text == "else";
			bool body = prev && ((prev->kind == TokOp && prev->text == ")") || (prev->kind == TokIdent && prev->text == "do"));
			Level& level = levels.back();
			if ( t.text == "else" || body )
			{
				++level.chain;
				++extra;
			}
			else if ( !afterElse )
			{
				// a new statement starts a new chain
				extra += 1 - level.chain;
				level.chain = 1;
			}
		}

		int depth = brackets + extra;
		if ( depth > nesting )
		{
			nesting = depth;
		}
		if ( depth > c_maxNesting && nestingToken < 0 )
		{
			nestingToken = (int)code[k];
		}
	}
}

void Document::index()
{
	stale = false;
	lex( text, tokens );
	symbols.clear();
	refs.clear();
	folds.clear();
	m_globals.clear();
	m_locals.clear();
	m_declToken.assign( tokens.size(), 0 );

	// code tokens only, comments kept separately for docs
	std::vector<size_t> code;
	for ( size_t i = 0; i < tokens.size(); ++i )
	{
		if ( tokens[i].kind != TokComment )
		{
			code.push_back( i );
		}
		else if ( tokens[i].range.end.line > tokens[i].range.start.line )
		{
			folds.push_back( std::make_pair(tokens[i].range.start.line, tokens[i].range.end.line) );
		}
	}

	measureNesting( code );

	auto tok = [&]( size_t k ) -> const Token* { return k < code.size() ? &tokens[code[k]] : nullptr; };
	auto is = [&]( size_t k, const char* text ) { const Token* t = tok( k ); return t && t->kind == TokOp && t->text == text; };
	auto isWord = [&]( size_t k, const char* text ) { const Token* t = tok( k ); return t && t->kind == TokIdent && t->text == text; };

	// pass 1: declarations
	std::vector<std::pair<int, Position> > braceStack; // owning symbol of the block, open position
	int scope = -1;
	int pendingOwner = -1; // function/struct whose body opens next
	std::vector<int> scopeStack;
	std::vector<int> tokScope( code.size(), -1 ); // function/struct body each code token is in

	auto fill = [&]( size_t from, size_t to, int owner )
	{
		for ( size_t f = from; f <= to && f < code.size(); ++f )
		{
			tokScope[f] = owner;
		}
	};

	for ( size_t k = 0; k < code.size(); ++k )
	{
		const Token& t = *tok( k );
		tokScope[k] = scope;

		if ( t.kind == TokOp && t.text == "{" )
		{
			braceStack.push_back( std::make_pair(pendingOwner, t.range.start) );
			if ( pendingOwner >= 0 )
			{
				scopeStack.push_back( scope );
				scope = pendingOwner;
				pendingOwner = -1;
			}
			continue;
		}

		if ( t.kind == TokOp && t.text == "}" )
		{
			if ( !braceStack.empty() )
			{
				std::pair<int, Position> open = braceStack.back();
				braceStack.pop_back();
				if ( t.range.end.line > open.second.line )
				{
					folds.push_back( std::make_pair(open.second.line, t.range.end.line) );
				}
				if ( open.first >= 0 )
				{
					symbols[open.first].fullRange.end = t.range.end;
					scope = scopeStack.empty() ? -1 : scopeStack.back();
					if ( !scopeStack.empty() )
					{
						scopeStack.pop_back();
					}
				}
			}
			continue;
		}

		if ( t.kind != TokIdent )
		{
			continue;
		}

		// "unit" is the compiler's synonym for "function"
		if ( (t.text == "function" || t.text == "unit" || t.text == "struct") && tok(k + 1) && tok(k + 1)->kind == TokIdent )
		{
			size_t first = k;
			int s = addSymbol( t.text == "struct" ? SymStruct : SymFunction, *tok(k + 1), -1 );
			symbols[s].fullRange.start = t.range.start;
			symbols[s].doc = commentBefore( code[k] );
			k += 1;
			if ( is(k + 1, "(") )
			{
				k += 2;
				while ( k < code.size() && !is(k, ")") )
				{
					if ( tok(k)->kind == TokIdent && tok(k)->text != "var" )
					{
						symbols[s].params.push_back( tok(k)->text );
						addSymbol( SymParam, *tok(k), s );
					}
					++k;
				}
			}
			fill( first + 1, k, s );
			pendingOwner = s;
			continue;
		}

		if ( t.text == "enum" )
		{
			int owner = -1;
			size_t j = k + 1;
			if ( tok(j) && tok(j)->kind == TokIdent )
			{
				owner = addSymbol( SymEnum, *tok(j), scope );
				symbols[owner].fullRange.start = t.range.start;
				symbols[owner].doc = commentBefore( code[k] );
				++j;
			}
			if ( !is(j, "{") )
			{
				continue;
			}
			size_t open = j;
			++j;
			while ( j < code.size() && !is(j, "}") )
			{
				if ( tok(j)->kind == TokIdent && (is(j + 1, ",") || is(j + 1, "=") || is(j + 1, "}")) )
				{
					Token member = *tok( j );
					if ( owner >= 0 )
					{
						member.text = symbols[owner].name + "::" + member.text;
					}
					int m = addSymbol( SymEnumMember, member, owner >= 0 ? -1 : scope, owner );
					// keep the reference on the member name alone
					symbols[m].nameRange = tok( j )->range;
					refs.back().range = tok( j )->range;
					if ( is(j + 1, "=") )
					{
						size_t v = j + 2;
						while ( v < code.size() && !is(v, ",") && !is(v, "}") )
						{
							symbols[m].value += tok( v )->text;
							++v;
						}
					}
					j = j + 1;
					while ( j < code.size() && !is(j, ",") && !is(j, "}") )
					{
						++j;
					}
					if ( is(j, ",") )
					{
						++j;
					}
					continue;
				}
				++j;
			}
			if ( owner >= 0 && j < code.size() )
			{
				symbols[owner].fullRange.end = tok( j )->range.end;
			}
			if ( j < code.size() && tok(j)->range.end.line > tok(open)->range.start.line )
			{
				folds.push_back( std::make_pair(tok(open)->range.start.line, tok(j)->range.end.line) );
			}
			fill( k + 1, j, scope );
			k = j;
			continue;
		}

		if ( t.text == "var" || t.text == "blank" )
		{
			size_t j = k + 1;
			while ( isWord(j, "var") || isWord(j, "blank") )
			{
				++j;
			}
			if ( tok(j) && tok(j)->kind == TokIdent )
			{
				Token decl = *tok( j );
				SymbolKind kind = SymGlobal;
				if ( decl.text.compare(0, 2, "::") == 0 )
				{
					// var ::g declares a global, also inside a function
					decl.text = decl.text.substr( 2 );
					decl.range.start.character += 2;
				}
				else if ( scope >= 0 )
				{
					kind = symbols[scope].kind == SymStruct ? SymMember : SymLocal;
				}
				if ( decl.text.find("::") == std::string::npos
						&& (kind == SymGlobal ? findGlobal(decl.text) : findLocal(scope, decl.text)) < 0 )
				{
					int s = addSymbol( kind, decl, kind == SymGlobal ? -1 : scope, kind == SymMember ? scope : -1 );
					symbols[s].doc = commentBefore( code[k] );
				}
			}
			fill( k + 1, j, scope );
			k = j;
			continue;
		}
	}

	// a body still open at the end (a function being typed) runs to the end
	for ( const std::pair<int, Position>& open : braceStack )
	{
		if ( open.first >= 0 && !code.empty() )
		{
			symbols[open.first].fullRange.end = tok( code.size() - 1 )->range.end;
		}
	}

	// implicit globals: first assignment at the top level (non strict code)
	int depth = 0;
	for ( size_t k = 0; k < code.size(); ++k )
	{
		const Token& t = *tok( k );
		if ( is(k, "{") )
		{
			++depth;
		}
		else if ( is(k, "}") )
		{
			--depth;
		}
		else if ( depth == 0 && t.kind == TokIdent && is(k + 1, "=") && (k == 0 || (!isWord(k - 1, "var") && !is(k - 1, ".")))
							&& t.text.find("::") == std::string::npos && findGlobal(t.text) < 0 )
		{
			addSymbol( SymGlobal, t, -1 );
		}
	}

	// name lookups for pass 2
	std::map<std::string, int> visible; // first function, struct, enum or anonymous global enum member
	std::map<std::string, int> globalVars; // first global variable
	std::map<std::string, int> qualified; // Enum::MEMBER, the last one wins
	std::map<std::string, int> enums; // last enum of each name
	std::map<std::string, int> structs; // first struct of each name
	std::map<std::string, std::pair<int, int> > members; // first struct member of each name, how many structs have one
	for ( size_t s = 0; s < symbols.size(); ++s )
	{
		const Symbol& sym = symbols[s];
		if ( sym.kind == SymFunction || sym.kind == SymStruct || sym.kind == SymEnum || (sym.kind == SymEnumMember && sym.parent < 0 && sym.scope == -1) )
		{
			visible.insert( std::make_pair(sym.name, (int)s) );
		}
		if ( sym.kind == SymGlobal )
		{
			globalVars.insert( std::make_pair(sym.name, (int)s) );
		}
		if ( sym.kind == SymEnumMember )
		{
			qualified[sym.name] = (int)s;
		}
		if ( sym.kind == SymEnum )
		{
			enums[sym.name] = (int)s;
		}
		if ( sym.kind == SymStruct )
		{
			structs.insert( std::make_pair(sym.name, (int)s) );
		}
		if ( sym.kind == SymMember )
		{
			auto it = members.find( sym.name );
			if ( it == members.end() )
			{
				members[sym.name] = std::make_pair( (int)s, 1 );
			}
			else
			{
				++it->second.second;
			}
		}
	}

	// pass 2: resolve every identifier to a declaration
	const size_t none = (size_t)-1;
	std::vector<size_t> openParens; // code index of each open ( and [
	std::vector<size_t> matching( code.size(), none ); // open ( or [ of each closing one
	struct Brace
	{
		int initializer; // struct of new S { member = value }, -2 unknown struct, -1 other braces
		bool array; // new S[] { {...}, {...} }, the inner braces initialize
		size_t parens; // open ( and [ when the brace opened
	};
	std::vector<Brace> braces;

	auto newStruct = [&]( size_t k ) -> int
	{
		auto it = structs.find( tok(k)->text );
		return it == structs.end() ? -2 : it->second;
	};

	for ( size_t k = 0; k < code.size(); ++k )
	{
		const Token& t = *tok( k );
		if ( t.kind == TokOp )
		{
			if ( t.text == "(" || t.text == "[" )
			{
				openParens.push_back( k );
			}
			else if ( (t.text == ")" || t.text == "]") && !openParens.empty() )
			{
				matching[k] = openParens.back();
				openParens.pop_back();
			}
			else if ( t.text == "{" )
			{
				Brace b;
				b.initializer = -1;
				b.array = false;
				b.parens = openParens.size();
				size_t call = (is(k - 1, ")") && matching[k - 1] != none) ? matching[k - 1] : none;
				if ( k >= 2 && tok(k - 1)->kind == TokIdent && isWord(k - 2, "new") )
				{
					b.initializer = newStruct( k - 1 );
				}
				else if ( call != none && call >= 2 && tok(call - 1)->kind == TokIdent && isWord(call - 2, "new") )
				{
					b.initializer = newStruct( call - 1 );
				}
				else if ( k >= 4 && is(k - 1, "]") && is(k - 2, "[") && tok(k - 3)->kind == TokIdent && isWord(k - 4, "new") )
				{
					b.initializer = newStruct( k - 3 );
					b.array = true;
				}
				else if ( !braces.empty() && braces.back().array && (is(k - 1, "{") || is(k - 1, ",")) )
				{
					b.initializer = braces.back().initializer;
				}
				braces.push_back( b );
			}
			else if ( t.text == "}" && !braces.empty() )
			{
				if ( openParens.size() > braces.back().parens )
				{
					openParens.resize( braces.back().parens );
				}
				braces.pop_back();
			}
			continue;
		}

		if ( t.kind != TokIdent || m_declToken[code[k]] )
		{
			continue;
		}

		int target = -1;
		Range nameRange = t.range;
		std::string name = t.text;

		if ( !braces.empty() && !braces.back().array && braces.back().initializer != -1 && braces.back().parens == openParens.size()
				&& is(k + 1, "=") && (is(k - 1, "{") || is(k - 1, ",")) )
		{
			// new S { member = value } names a member of S, never a variable
			int s = braces.back().initializer;
			int member = s >= 0 ? findLocal( s, name ) : -1;
			if ( member >= 0 && symbols[member].kind == SymMember )
			{
				target = member;
			}
		}
		else if ( name.size() > 2 && name[0] == ':' && name[1] == ':' )
		{
			name = name.substr( 2 );
			nameRange.start.character += 2;
			target = findGlobal( name );
		}
		else if ( name.find("::") != std::string::npos )
		{
			// Enum::MEMBER, the reference covers the member part
			auto it = qualified.find( name );
			if ( it != qualified.end() )
			{
				target = it->second;
				nameRange.start.character += (int)(name.rfind("::") + 2);
			}
			else
			{
				// the enum name in front of ::
				std::string head = name.substr( 0, name.find("::") );
				auto e = enums.find( head );
				if ( e != enums.end() )
				{
					target = e->second;
					nameRange.end.character = nameRange.start.character + (int)head.size();
				}
			}
		}
		else if ( k > 0 && is(k - 1, ".") )
		{
			// without types the struct is only known when a single struct has
			// a member of this name; guessing would let rename edit another
			// struct's members
			auto it = members.find( name );
			if ( it != members.end() && it->second.second == 1 )
			{
				target = it->second.first;
			}
		}
		else
		{
			int inScope = tokScope[k];
			auto v = visible.find( name );
			auto g = globalVars.find( name );
			int type = v == visible.end() ? -1 : v->second;
			int global = g == globalVars.end() ? -1 : g->second;
			if ( inScope < 0 )
			{
				target = (type >= 0 && (global < 0 || type < global)) ? type : global;
			}
			else
			{
				// inside a function the compiler falls back to a global declared
				// earlier when no local of that name exists yet
				int local = findLocal( inScope, name );
				bool earlierGlobal = symbols[inScope].kind == SymFunction && global >= 0
					&& before( symbols[global].nameRange.start, t.range.start );
				bool localLater = local >= 0 && symbols[local].kind == SymLocal && before( t.range.start, symbols[local].nameRange.start );
				if ( local >= 0 && !(localLater && earlierGlobal) )
				{
					target = local;
				}
				else if ( earlierGlobal )
				{
					target = global;
				}
				else
				{
					target = type;
				}
			}
		}

		if ( target >= 0 )
		{
			Reference r;
			r.symbol = target;
			r.range = nameRange;
			r.declaration = false;
			refs.push_back( r );
		}
	}
}

class LspServer
{
public:
	int run();

private:
	void dispatch( const Json& message );
	void reply( const Json& id, const Json& result );
	void replyError( const Json& id, int code, const std::string& message );
	void notify( const char* method, const Json& params );

	Document* doc( const Json& params );
	void publishDiagnostics( Document& d );

	void onInitialize( const Json& msg );
	void onHover( const Json& msg );
	void onDefinition( const Json& msg );
	void onReferences( const Json& msg, bool highlights );
	void onDocumentSymbol( const Json& msg );
	void onCompletion( const Json& msg );
	void onSignatureHelp( const Json& msg );
	void onRename( const Json& msg, bool prepare );
	void onFolding( const Json& msg );

	int symbolAt( Document& d, const Position& p, Range* range = nullptr );
	const Token* tokenAt( Document& d, const Position& p );
	const WrenchLibraryEntry* library( const std::string& name, int kind = -1 );
	std::string describe( const Document& d, int symbol );
	int scopeAt( const Document& d, const Position& p );

	// wrench.json of the document's host application, null if there is none
	// (or it is broken, then 'error' says why)
	const WrenchProject* projectFor( const Document& d, std::string* error = nullptr );
	static bool available( const WrenchLibraryEntry* e, const WrenchProject* project );
	static std::string describeNative( const WrenchNative& n );
	void checkCalls( const Document& d, const WrenchProject& project, Json& diagnostics );

	struct CachedProject
	{
		std::filesystem::file_time_type stamp;
		WrenchProject project;
		std::string error;
	};

	JsonStdioStream m_stream;
	std::map<std::string, Document> m_docs;
	std::map<std::string, CachedProject> m_projects;
	bool m_shutdown = false;
	bool m_exit = false;
	bool m_nonStrict = false;
	std::chrono::steady_clock::time_point m_lastChange;
};

void LspServer::reply( const Json& id, const Json& result )
{
	Json r = Json::object();
	r.set( "jsonrpc", "2.0" );
	r.set( "id", id );
	r.set( "result", result );
	m_stream.send( r );
}

void LspServer::replyError( const Json& id, int code, const std::string& message )
{
	Json e = Json::object();
	e.set( "code", code );
	e.set( "message", message );
	Json r = Json::object();
	r.set( "jsonrpc", "2.0" );
	r.set( "id", id );
	r.set( "error", e );
	m_stream.send( r );
}

void LspServer::notify( const char* method, const Json& params )
{
	Json n = Json::object();
	n.set( "jsonrpc", "2.0" );
	n.set( "method", method );
	n.set( "params", params );
	m_stream.send( n );
}

Document* LspServer::doc( const Json& params )
{
	auto it = m_docs.find( params["textDocument"]["uri"].asString() );
	if ( it == m_docs.end() )
	{
		return nullptr;
	}
	if ( it->second.stale )
	{
		it->second.index();
	}
	return &it->second;
}

const WrenchLibraryEntry* LspServer::library( const std::string& name, int kind )
{
	static std::multimap<std::string, const WrenchLibraryEntry*> s_byName;
	if ( s_byName.empty() )
	{
		for ( const WrenchLibraryEntry* e = c_library; e->name; ++e )
		{
			s_byName.insert( std::make_pair(std::string(e->name), e) );
		}
	}
	// multimap keeps entries of equal names in table order
	auto range = s_byName.equal_range( name );
	for ( auto it = range.first; it != range.second; ++it )
	{
		if ( kind < 0 || it->second->kind == kind )
		{
			return it->second;
		}
	}
	return nullptr;
}

const WrenchProject* LspServer::projectFor( const Document& d, std::string* error )
{
	if ( error )
	{
		error->clear();
	}
	std::string path = uriToPath( d.uri );
	std::string file = path.empty() ? std::string() : WrenchProject::find( path );
	if ( file.empty() )
	{
		return nullptr;
	}

	std::error_code ec;
	std::filesystem::file_time_type stamp = std::filesystem::last_write_time( std::filesystem::u8path(file), ec );
	auto it = m_projects.find( file );
	if ( it == m_projects.end() || it->second.stamp != stamp )
	{
		CachedProject cached;
		cached.stamp = stamp;
		if ( !cached.project.load(file, cached.error) )
		{
			cached.project = WrenchProject();
		}
		it = m_projects.insert_or_assign( file, cached ).first;
	}
	if ( error )
	{
		*error = it->second.error;
	}
	return it->second.project.loaded() ? &it->second.project : nullptr;
}

bool LspServer::available( const WrenchLibraryEntry* e, const WrenchProject* project )
{
	if ( !project || e->kind >= 2 )
	{
		return true;
	}
	std::string library = WrenchProject::libraryOf( e->name );
	return library.empty() || project->hasLibrary( library );
}

std::string LspServer::describeNative( const WrenchNative& n )
{
	std::string md = "```wrench\nfunction " + n.signature() + "\n```\n---\n";
	if ( !n.doc.empty() )
	{
		md += n.doc + "\n\n";
	}
	return md + "*Provided by the host application (wrench.json)*";
}

// calls the host application will not be able to serve
void LspServer::checkCalls( const Document& d, const WrenchProject& project, Json& diagnostics )
{
	std::vector<size_t> code;
	for ( size_t i = 0; i < d.tokens.size(); ++i )
	{
		if ( d.tokens[i].kind != TokComment )
		{
			code.push_back( i );
		}
	}
	auto tok = [&]( size_t k ) -> const Token* { return k < code.size() ? &d.tokens[code[k]] : nullptr; };
	auto isOp = [&]( size_t k, const char* text ) { const Token* t = tok( k ); return t && t->kind == TokOp && t->text == text; };

	auto report = [&]( const Token& t, int severity, const char* id, const std::string& message )
	{
		Json diag = Json::object();
		diag.set( "range", rangeJson(t.range) );
		diag.set( "severity", severity );
		diag.set( "source", "wrench" );
		diag.set( "code", id );
		diag.set( "message", message );
		diagnostics.push( diag );
	};

	int undeclaredSeverity = project.undeclaredCalls() == WrenchProject::Error ? 1 : 2;

	// script functions, and variables that may hold a function
	std::set<std::string> scriptNames;
	for ( const Symbol& s : d.symbols )
	{
		if ( s.kind != SymMember )
		{
			scriptNames.insert( s.name );
		}
	}

	for ( size_t k = 0; k + 1 < code.size(); ++k )
	{
		const Token& t = *tok( k );
		if ( t.kind != TokIdent || !isOp(k + 1, "(") )
		{
			continue;
		}
		if ( k > 0 )
		{
			const Token& prev = *tok( k - 1 );
			if ( (prev.kind == TokOp && prev.text == ".")
					|| (prev.kind == TokIdent && (prev.text == "function" || prev.text == "unit" || prev.text == "struct" || prev.text == "new")) )
			{
				continue;
			}
		}

		std::string name = t.text;
		if ( name.compare(0, 2, "::") == 0 )
		{
			name = name.substr( 2 );
		}

		if ( library(name, 3) )
		{
			continue; // if, while, yield...
		}

		if ( scriptNames.count(name) )
		{
			continue;
		}

		if ( const WrenchNative* n = project.native(name) )
		{
			if ( !n->paramsKnown )
			{
				continue;
			}

			// arguments at the top level of the parentheses
			int args = 0;
			int depth = 0;
			bool any = false;
			for ( size_t j = k + 2; j < code.size(); ++j )
			{
				const Token& a = *tok( j );
				if ( a.kind == TokOp && (a.text == "(" || a.text == "[" || a.text == "{") )
				{
					++depth;
				}
				else if ( a.kind == TokOp && (a.text == ")" || a.text == "]" || a.text == "}") )
				{
					if ( depth == 0 )
					{
						break;
					}
					--depth;
				}
				else if ( a.kind == TokOp && a.text == ";" && depth == 0 )
				{
					break;
				}
				else if ( a.kind == TokOp && a.text == "," && depth == 0 )
				{
					++args;
					continue;
				}
				any = true;
			}
			if ( any )
			{
				++args;
			}

			if ( args < n->minArgs || (n->maxArgs >= 0 && args > n->maxArgs) )
			{
				std::string expected;
				if ( n->maxArgs < 0 )
				{
					expected = "at least " + std::to_string( n->minArgs );
				}
				else if ( n->minArgs == n->maxArgs )
				{
					expected = std::to_string( n->minArgs );
				}
				else
				{
					expected = std::to_string( n->minArgs ) + " to " + std::to_string( n->maxArgs );
				}
				bool one = n->maxArgs == 1 && n->minArgs == 1;
				report( t, 2, "argument-count", n->name + " takes " + expected + (one ? " argument" : " arguments")
						+ ", this call passes " + std::to_string(args) );
			}
			continue;
		}

		const WrenchLibraryEntry* e = library( name, 0 );
		std::string lib = WrenchProject::libraryOf( name );
		if ( e && available(e, &project) )
		{
			continue;
		}
		if ( project.undeclaredCalls() == WrenchProject::Ignore )
		{
			continue;
		}
		if ( e && lib == "host" )
		{
			report( t, undeclaredSeverity, "undeclared-call", "'" + name + "' comes from the wrench tools; declare it in wrench.json if the host application provides it" );
		}
		else if ( e )
		{
			report( t, undeclaredSeverity, "library-not-loaded", "'" + name + "' needs the '" + lib + "' library, which wrench.json does not list" );
		}
		else if ( !lib.empty() )
		{
			report( t, undeclaredSeverity, "undeclared-call", "'" + name + "' is not a function of the '" + lib + "' library" );
		}
		else
		{
			report( t, undeclaredSeverity, "undeclared-call", "'" + name + "' is neither a function of this script nor declared in wrench.json" );
		}
	}
}

void LspServer::publishDiagnostics( Document& d )
{
	if ( d.stale )
	{
		d.index();
	}
	d.dirty = false;

	Json diagnostics = Json::array();

	unsigned char* out = 0;
	int outLen = 0;
	WRstr message;
	int line = 0;
	int column = 0;
	WRError err = WR_ERR_None;
	if ( d.nesting > c_maxNesting )
	{
		// the compiler recurses per nesting level and would overflow the
		// stack, taking the server down with it
		Range r;
		if ( d.nestingToken >= 0 )
		{
			r = d.tokens[d.nestingToken].range;
		}
		Json diag = Json::object();
		diag.set( "range", rangeJson(r) );
		diag.set( "severity", 2 );
		diag.set( "source", "wrench" );
		diag.set( "code", "nesting" );
		diag.set( "message", "nesting too deep to check (" + std::to_string(d.nesting) + " levels, the limit is "
			+ std::to_string(c_maxNesting) + "); the compiler may run out of stack on this code" );
		diagnostics.push( diag );
	}
	else
	{
		uint8_t flags = WR_INCLUDE_GLOBALS | (m_nonStrict ? WR_NON_STRICT_VAR : 0);
		err = wr_compileEx( d.text.c_str(), (int)d.text.size(), &out, &outLen, &message, flags, &line, &column );
		g_free( out );
	}

	if ( err != WR_ERR_None )
	{
		// the compiler reports the column just past the offending token, in
		// bytes; tokens count UTF-16 units
		Range r;
		r.start.line = line > 0 ? line - 1 : 0;
		r.start.character = 0;
		r.end = r.start;
		size_t lineStart = 0;
		for ( int l = 0; l < r.start.line && lineStart != std::string::npos; ++l )
		{
			lineStart = d.text.find( '\n', lineStart );
			if ( lineStart != std::string::npos )
			{
				++lineStart;
			}
		}
		if ( lineStart != std::string::npos && column > 0 )
		{
			int units = 0;
			for ( size_t i = lineStart; i < d.text.size() && i < lineStart + (size_t)column && d.text[i] != '\n'; ++i )
			{
				units += utf16Units( (unsigned char)d.text[i] );
			}
			column = units;
		}
		const Token* best = nullptr;
		for ( const Token& t : d.tokens )
		{
			if ( t.kind == TokComment || t.range.start.line != r.start.line )
			{
				continue;
			}
			if ( t.range.start.character < column || !best )
			{
				best = &t;
			}
		}
		std::string nearText;
		if ( best )
		{
			r = best->range;
			nearText = best->text;

			// the compiler only notices a problem when it reads the next token;
			// if that token opens a line the mistake sits at the end of the
			// previous one (missing operand, missing ';'...)
			size_t index = (size_t)(best - d.tokens.data());
			size_t prev = index;
			while ( prev > 0 && d.tokens[prev - 1].kind == TokComment )
			{
				--prev;
			}
			bool firstOnLine = prev == 0 || d.tokens[prev - 1].range.end.line < best->range.start.line;
			if ( firstOnLine && prev > 0 )
			{
				r = d.tokens[prev - 1].range;
			}
		}
		else
		{
			r.end.character = 1000;
		}

		std::string text = c_errStrings[err];
		if ( text.compare(0, 7, "WR_ERR_") == 0 )
		{
			text = text.substr( 7 );
		}
		std::replace( text.begin(), text.end(), '_', ' ' );
		if ( !nearText.empty() && nearText.size() < 40 )
		{
			text += " (near '" + nearText + "')";
		}

		Json diag = Json::object();
		diag.set( "range", rangeJson(r) );
		diag.set( "severity", 1 );
		diag.set( "source", "wrench" );
		diag.set( "code", c_errStrings[err] );
		diag.set( "message", text );
		diagnostics.push( diag );
	}

	std::string projectError;
	const WrenchProject* project = projectFor( d, &projectError );
	if ( project )
	{
		checkCalls( d, *project, diagnostics );
	}
	else if ( !projectError.empty() )
	{
		Range r;
		Json diag = Json::object();
		diag.set( "range", rangeJson(r) );
		diag.set( "severity", 2 );
		diag.set( "source", "wrench" );
		diag.set( "code", "project" );
		diag.set( "message", "wrench.json is ignored: " + projectError );
		diagnostics.push( diag );
	}

	Json params = Json::object();
	params.set( "uri", d.uri );
	params.set( "version", d.version );
	params.set( "diagnostics", diagnostics );
	notify( "textDocument/publishDiagnostics", params );
}

void LspServer::onInitialize( const Json& msg )
{
	const Json& options = msg["params"]["initializationOptions"];
	m_nonStrict = options["nonStrict"].asBool( false );

	Json sync = Json::object();
	sync.set( "openClose", true );
	sync.set( "change", 1 ); // full text
	Json save = Json::object();
	save.set( "includeText", false );
	sync.set( "save", save );

	Json caps = Json::object();
	caps.set( "textDocumentSync", sync );
	caps.set( "hoverProvider", true );
	caps.set( "definitionProvider", true );
	caps.set( "referencesProvider", true );
	caps.set( "documentHighlightProvider", true );
	caps.set( "documentSymbolProvider", true );
	caps.set( "foldingRangeProvider", true );

	Json rename = Json::object();
	rename.set( "prepareProvider", true );
	caps.set( "renameProvider", rename );

	Json completion = Json::object();
	Json triggers = Json::array();
	triggers.push( "." );
	triggers.push( ":" );
	completion.set( "triggerCharacters", triggers );
	caps.set( "completionProvider", completion );

	Json signature = Json::object();
	Json sigTriggers = Json::array();
	sigTriggers.push( "(" );
	sigTriggers.push( "," );
	signature.set( "triggerCharacters", sigTriggers );
	caps.set( "signatureHelpProvider", signature );

	Json info = Json::object();
	info.set( "name", "wrench-lsp" );
	char version[32];
	snprintf( version, sizeof(version), "%d.%d.%d", WRENCH_VERSION_MAJOR, WRENCH_VERSION_MINOR, WRENCH_VERSION_BUILD );
	info.set( "version", version );

	Json result = Json::object();
	result.set( "capabilities", caps );
	result.set( "serverInfo", info );
	reply( msg["id"], result );
}

const Token* LspServer::tokenAt( Document& d, const Position& p )
{
	int i = d.tokenAt( p );
	return i < 0 ? nullptr : &d.tokens[i];
}

int LspServer::symbolAt( Document& d, const Position& p, Range* range )
{
	for ( const Reference& r : d.refs )
	{
		if ( contains(r.range, p) )
		{
			if ( range )
			{
				*range = r.range;
			}
			return r.symbol;
		}
	}
	return -1;
}

int LspServer::scopeAt( const Document& d, const Position& p )
{
	int scope = -1;
	for ( size_t s = 0; s < d.symbols.size(); ++s )
	{
		const Symbol& sym = d.symbols[s];
		if ( (sym.kind == SymFunction || sym.kind == SymStruct) && contains(sym.fullRange, p) && before(sym.nameRange.end, p) )
		{
			scope = (int)s;
		}
	}
	return scope;
}

std::string LspServer::describe( const Document& d, int symbol )
{
	const Symbol& s = d.symbols[symbol];
	std::string sig;
	switch ( s.kind )
	{
		case SymFunction:
		case SymStruct:
		{
			sig = (s.kind == SymFunction ? "function " : "struct ") + s.name + "(";
			for ( size_t i = 0; i < s.params.size(); ++i )
			{
				sig += (i ? ", " : " ") + s.params[i];
			}
			sig += s.params.empty() ? ")" : " )";
			break;
		}
		case SymEnum: sig = "enum " + s.name; break;
		case SymEnumMember: sig = "enum " + s.name + (s.value.empty() ? std::string() : " = " + s.value); break;
		case SymGlobal: sig = "var ::" + s.name + "  (global)"; break;
		case SymLocal: sig = "var " + s.name + "  (local in " + d.symbols[s.scope].name + ")"; break;
		case SymParam: sig = s.name + "  (parameter of " + d.symbols[s.scope].name + ")"; break;
		case SymMember: sig = "var " + s.name + "  (member of struct " + d.symbols[s.scope].name + ")"; break;
	}

	std::string md = "```wrench\n" + sig + "\n```";
	if ( !s.doc.empty() )
	{
		md += "\n---\n" + s.doc;
	}
	return md;
}

void LspServer::onHover( const Json& msg )
{
	Document* d = doc( msg["params"] );
	if ( !d )
	{
		reply( msg["id"], Json() );
		return;
	}

	Position p;
	p.line = msg["params"]["position"]["line"].asInt();
	p.character = msg["params"]["position"]["character"].asInt();

	Range range;
	std::string markdown;
	int s = symbolAt( *d, p, &range );
	if ( s >= 0 )
	{
		markdown = describe( *d, s );
	}
	else if ( const Token* t = tokenAt(*d, p) )
	{
		range = t->range;
		std::string word = t->text;
		// member functions are written ._name
		size_t ti = (size_t)(t - d->tokens.data());
		if ( ti > 0 && d->tokens[ti - 1].text == "." && word[0] == '_' )
		{
			word = "." + word;
		}
		const WrenchProject* project = projectFor( *d );
		const WrenchNative* native = project ? project->native( word.compare(0, 2, "::") == 0 ? word.substr(2) : word ) : nullptr;
		if ( native )
		{
			markdown = describeNative( *native );
		}
		else if ( const WrenchLibraryEntry* e = library(word) )
		{
			markdown = std::string( "```wrench\n" ) + e->signature + "\n```\n---\n" + e->doc;
			if ( !available(e, project) )
			{
				std::string lib = WrenchProject::libraryOf( e->name );
				markdown += lib == "host"
					? "\n\n**Not declared in wrench.json**, the host application may not provide it."
					: "\n\n**Not available**: wrench.json does not list the '" + lib + "' library.";
			}
		}
	}

	if ( markdown.empty() )
	{
		reply( msg["id"], Json() );
		return;
	}

	Json contents = Json::object();
	contents.set( "kind", "markdown" );
	contents.set( "value", markdown );
	Json result = Json::object();
	result.set( "contents", contents );
	result.set( "range", rangeJson(range) );
	reply( msg["id"], result );
}

void LspServer::onDefinition( const Json& msg )
{
	Document* d = doc( msg["params"] );
	Position p;
	p.line = msg["params"]["position"]["line"].asInt();
	p.character = msg["params"]["position"]["character"].asInt();
	int s = d ? symbolAt( *d, p ) : -1;
	if ( s < 0 )
	{
		// host functions are declared in wrench.json
		const Token* t = d ? tokenAt( *d, p ) : nullptr;
		const WrenchProject* project = t ? projectFor( *d ) : nullptr;
		const WrenchNative* native = (project && t->kind == TokIdent)
			? project->native( t->text.compare(0, 2, "::") == 0 ? t->text.substr(2) : t->text ) : nullptr;
		if ( !native )
		{
			reply( msg["id"], Json() );
			return;
		}
		Range r;
		r.start.line = native->line;
		r.start.character = native->column;
		r.end = r.start;
		r.end.character += (int)native->name.size();
		Json loc = Json::object();
		loc.set( "uri", pathToUri(project->path()) );
		loc.set( "range", rangeJson(r) );
		reply( msg["id"], loc );
		return;
	}
	Json loc = Json::object();
	loc.set( "uri", d->uri );
	loc.set( "range", rangeJson(d->symbols[s].nameRange) );
	reply( msg["id"], loc );
}

void LspServer::onReferences( const Json& msg, bool highlights )
{
	Document* d = doc( msg["params"] );
	Position p;
	p.line = msg["params"]["position"]["line"].asInt();
	p.character = msg["params"]["position"]["character"].asInt();
	bool includeDecl = msg["params"]["context"]["includeDeclaration"].asBool( true );
	int s = d ? symbolAt( *d, p ) : -1;

	Json result = Json::array();
	if ( s >= 0 )
	{
		for ( const Reference& r : d->refs )
		{
			if ( r.symbol != s || (!highlights && r.declaration && !includeDecl) )
			{
				continue;
			}
			Json j = Json::object();
			if ( highlights )
			{
				j.set( "range", rangeJson(r.range) );
				j.set( "kind", r.declaration ? 3 : 2 );
			}
			else
			{
				j.set( "uri", d->uri );
				j.set( "range", rangeJson(r.range) );
			}
			result.push( j );
		}
	}
	reply( msg["id"], result );
}

void LspServer::onDocumentSymbol( const Json& msg )
{
	Document* d = doc( msg["params"] );
	Json result = Json::array();
	if ( !d )
	{
		reply( msg["id"], result );
		return;
	}

	// LSP SymbolKind values
	auto kindOf = []( SymbolKind k ) -> int
	{
		switch ( k )
		{
			case SymFunction: return 12;
			case SymStruct: return 23;
			case SymEnum: return 10;
			case SymEnumMember: return 22;
			case SymMember: return 8;
			default: return 13;
		}
	};

	auto entry = [&]( const Symbol& s ) -> Json
	{
		Json j = Json::object();
		std::string name = s.name;
		if ( s.kind == SymEnumMember && s.parent >= 0 )
		{
			name = name.substr( name.rfind("::") + 2 );
		}
		j.set( "name", name );
		j.set( "kind", kindOf(s.kind) );
		j.set( "range", rangeJson(s.fullRange) );
		j.set( "selectionRange", rangeJson(s.nameRange) );
		if ( s.kind == SymFunction || s.kind == SymStruct )
		{
			std::string detail = "(";
			for ( size_t i = 0; i < s.params.size(); ++i )
			{
				detail += (i ? ", " : "") + s.params[i];
			}
			j.set( "detail", detail + ")" );
		}
		return j;
	};

	// children of each symbol in one pass: locals and members by scope,
	// enum members by their enum
	std::vector<std::vector<size_t> > childrenOf( d->symbols.size() );
	for ( size_t c = 0; c < d->symbols.size(); ++c )
	{
		const Symbol& ch = d->symbols[c];
		int owner = -1;
		if ( ch.scope >= 0 && ch.kind != SymParam )
		{
			owner = ch.scope;
		}
		else if ( ch.parent >= 0 && d->symbols[ch.parent].kind == SymEnum )
		{
			owner = ch.parent;
		}
		if ( owner >= 0 )
		{
			childrenOf[owner].push_back( c );
		}
	}

	for ( size_t i = 0; i < d->symbols.size(); ++i )
	{
		const Symbol& s = d->symbols[i];
		bool topLevel = (s.kind == SymFunction || s.kind == SymStruct || s.kind == SymEnum || s.kind == SymGlobal)
										|| (s.kind == SymEnumMember && s.parent < 0 && s.scope < 0);
		if ( !topLevel )
		{
			continue;
		}

		Json j = entry( s );
		Json children = Json::array();
		for ( size_t c : childrenOf[i] )
		{
			children.push( entry(d->symbols[c]) );
		}
		if ( children.size() )
		{
			j.set( "children", children );
		}
		result.push( j );
	}
	reply( msg["id"], result );
}

void LspServer::onCompletion( const Json& msg )
{
	Document* d = doc( msg["params"] );
	Json items = Json::array();
	if ( !d )
	{
		reply( msg["id"], items );
		return;
	}

	Position p;
	p.line = msg["params"]["position"]["line"].asInt();
	p.character = msg["params"]["position"]["character"].asInt();

	// text of the line up to the cursor
	size_t lineStart = 0;
	for ( int l = 0; l < p.line && lineStart != std::string::npos; ++l )
	{
		lineStart = d->text.find( '\n', lineStart );
		if ( lineStart != std::string::npos )
		{
			++lineStart;
		}
	}
	std::string prefixLine;
	if ( lineStart != std::string::npos )
	{
		size_t lineEnd = d->text.find( '\n', lineStart );
		std::string line = d->text.substr( lineStart, lineEnd == std::string::npos ? std::string::npos : lineEnd - lineStart );
		int units = 0;
		size_t i = 0;
		for ( ; i < line.size() && units < p.character; ++i )
		{
			units += utf16Units( (unsigned char)line[i] );
		}
		prefixLine = line.substr( 0, i );
	}

	// nothing to offer inside comments and strings
	auto it = std::upper_bound( d->tokens.begin(), d->tokens.end(), p, []( const Position& pos, const Token& t ) { return before(pos, t.range.start); } );
	if ( it != d->tokens.begin() )
	{
		const Token& t = *(it - 1);
		bool closed = false;
		if ( t.kind == TokString || t.kind == TokChar )
		{
			// the lexer's walk: escapes skip a character
			size_t e = 1;
			while ( e < t.text.size() && t.text[e] != t.text[0] )
			{
				e += (t.text[e] == '\\') ? 2 : 1;
			}
			closed = e < t.text.size();
		}
		else if ( t.kind == TokComment && t.text.compare(0, 2, "/*") == 0 )
		{
			closed = t.text.size() >= 4 && t.text.compare(t.text.size() - 2, 2, "*/") == 0;
		}
		bool quoted = t.kind == TokString || t.kind == TokChar || t.kind == TokComment;
		if ( quoted && before(t.range.start, p) && (before(p, t.range.end) || (!closed && !before(t.range.end, p))) )
		{
			reply( msg["id"], items );
			return;
		}
	}

	// a single ':' ends a case label, a ternary or a hash key; only "::"
	// starts a name
	size_t len = prefixLine.size();
	if ( len > 0 && prefixLine[len - 1] == ':' && (len < 2 || prefixLine[len - 2] != ':') )
	{
		reply( msg["id"], items );
		return;
	}

	size_t start = prefixLine.size();
	while ( start > 0 && (isalnum((unsigned char)prefixLine[start - 1]) || prefixLine[start - 1] == '_' || prefixLine[start - 1] == ':') )
	{
		--start;
	}
	std::string word = prefixLine.substr( start );
	bool afterDot = start > 0 && prefixLine[start - 1] == '.';
	if ( afterDot )
	{
		// members follow a name, a call or an index: not a number (1.5), not
		// the start of an expression (.5)
		size_t object = start - 1;
		bool valid = object > 0 && (prefixLine[object - 1] == ')' || prefixLine[object - 1] == ']');
		if ( !valid )
		{
			size_t nameStart = object;
			while ( nameStart > 0 && (isalnum((unsigned char)prefixLine[nameStart - 1]) || prefixLine[nameStart - 1] == '_') )
			{
				--nameStart;
			}
			valid = nameStart < object && !isdigit( (unsigned char)prefixLine[nameStart] );
		}
		if ( !valid )
		{
			reply( msg["id"], items );
			return;
		}
	}

	std::set<std::string> seen;
	auto add = [&]( const std::string& label, int kind, const std::string& detail, const std::string& documentation,
									const std::string& insert = std::string() )
	{
		if ( !seen.insert(label).second )
		{
			return;
		}
		Json item = Json::object();
		item.set( "label", label );
		item.set( "kind", kind );
		if ( !detail.empty() )
		{
			item.set( "detail", detail );
		}
		if ( !documentation.empty() )
		{
			Json docs = Json::object();
			docs.set( "kind", "markdown" );
			docs.set( "value", documentation );
			item.set( "documentation", docs );
		}
		if ( !insert.empty() )
		{
			item.set( "insertText", insert );
		}
		items.push( item );
	};

	if ( afterDot )
	{
		// member functions and struct members
		for ( const WrenchLibraryEntry* e = c_library; e->name; ++e )
		{
			if ( e->kind == 2 )
			{
				add( e->name + 1, 2, e->signature, e->doc );
			}
		}
		for ( const Symbol& s : d->symbols )
		{
			if ( s.kind == SymMember )
			{
				add( s.name, 5, "member of " + d->symbols[s.scope].name, std::string() );
			}
		}
		reply( msg["id"], items );
		return;
	}

	const WrenchProject* project = projectFor( *d );

	size_t colons = word.rfind( "::" );
	if ( colons != std::string::npos && colons > 0 )
	{
		// namespace members: host functions, library functions and enum members
		std::string ns = word.substr( 0, colons + 2 );
		if ( project )
		{
			for ( const WrenchNative& n : project->natives() )
			{
				if ( n.name.compare(0, ns.size(), ns) == 0 )
				{
					add( n.name.substr(ns.size()), 3, n.signature(), describeNative(n) );
				}
			}
		}
		for ( const WrenchLibraryEntry* e = c_library; e->name; ++e )
		{
			if ( (e->kind == 0 || e->kind == 1) && strncmp(e->name, ns.c_str(), ns.size()) == 0 && available(e, project) )
			{
				add( e->name + ns.size(), e->kind == 0 ? 3 : 21, e->signature, e->doc );
			}
		}
		for ( const Symbol& s : d->symbols )
		{
			if ( s.kind == SymEnumMember && s.name.compare(0, ns.size(), ns) == 0 )
			{
				add( s.name.substr(ns.size()), 20, s.value.empty() ? std::string() : "= " + s.value, std::string() );
			}
		}
		reply( msg["id"], items );
		return;
	}

	int scope = scopeAt( *d, p );
	bool globalsOnly = word.compare( 0, 2, "::" ) == 0;

	if ( scope >= 0 && !globalsOnly )
	{
		for ( const Symbol& s : d->symbols )
		{
			if ( s.scope == scope && (s.kind == SymLocal || s.kind == SymParam || s.kind == SymMember) )
			{
				add( s.name, 6, s.kind == SymParam ? "parameter" : "local", std::string() );
			}
			if ( s.kind == SymEnumMember && s.parent < 0 && s.scope == scope )
			{
				add( s.name, 20, "enum", std::string() );
			}
		}
	}

	for ( size_t i = 0; i < d->symbols.size(); ++i )
	{
		const Symbol& s = d->symbols[i];
		if ( s.kind == SymGlobal )
		{
			// a function sees a global declared above it by its plain name too
			// (unless a local hides it), ::name always works; struct bodies
			// only see members
			bool plain = scope < 0 || (d->symbols[scope].kind == SymFunction && before(s.nameRange.start, p));
			if ( plain && !globalsOnly )
			{
				add( s.name, 6, "global", describe(*d, (int)i) );
			}
			if ( scope >= 0 || globalsOnly )
			{
				add( "::" + s.name, 6, "global", describe(*d, (int)i), "::" + s.name );
			}
		}
		else if ( globalsOnly )
		{
			continue;
		}
		else if ( s.kind == SymFunction || s.kind == SymStruct )
		{
			add( s.name, s.kind == SymFunction ? 3 : 22, s.kind == SymFunction ? "function" : "struct", describe(*d, (int)i) );
		}
		else if ( s.kind == SymEnum )
		{
			add( s.name, 13, "enum", std::string() );
		}
		else if ( s.kind == SymEnumMember && s.parent < 0 && s.scope < 0 )
		{
			add( s.name, 20, "enum", std::string() );
		}
	}

	if ( !globalsOnly )
	{
		std::set<std::string> namespaces;
		std::set<std::string> hostNamespaces;
		if ( project )
		{
			for ( const WrenchNative& n : project->natives() )
			{
				size_t c = n.name.find( "::" );
				if ( c == std::string::npos )
				{
					add( n.name, 3, n.signature(), describeNative(n) );
				}
				else
				{
					hostNamespaces.insert( n.name.substr(0, c) );
				}
			}
		}
		for ( const WrenchLibraryEntry* e = c_library; e->name; ++e )
		{
			if ( !available(e, project) )
			{
				continue;
			}
			if ( e->kind == 3 )
			{
				add( e->name, 14, e->signature, e->doc );
			}
			else if ( e->kind == 0 && !strstr(e->name, "::") )
			{
				add( e->name, 3, e->signature, e->doc );
			}
			else if ( e->kind <= 1 )
			{
				std::string full = e->name;
				namespaces.insert( full.substr(0, full.find("::")) );
			}
		}
		for ( const std::string& ns : hostNamespaces )
		{
			add( ns, 9, "host application", std::string() );
		}
		for ( const std::string& ns : namespaces )
		{
			add( ns, 9, "library", std::string() );
		}
	}

	reply( msg["id"], items );
}

void LspServer::onSignatureHelp( const Json& msg )
{
	Document* d = doc( msg["params"] );
	if ( !d )
	{
		reply( msg["id"], Json() );
		return;
	}

	Position p;
	p.line = msg["params"]["position"]["line"].asInt();
	p.character = msg["params"]["position"]["character"].asInt();

	// walk back over the tokens before the cursor to the open parenthesis
	int depth = 0;
	int argument = 0;
	int callee = -1;
	int last = -1;
	for ( size_t i = 0; i < d->tokens.size(); ++i )
	{
		if ( before(d->tokens[i].range.start, p) )
		{
			last = (int)i;
		}
	}
	for ( int i = last; i >= 0; --i )
	{
		const Token& t = d->tokens[i];
		if ( t.kind != TokOp )
		{
			continue;
		}
		if ( t.text == ")" || t.text == "]" || t.text == "}" )
		{
			++depth;
		}
		else if ( t.text == "(" || t.text == "[" || t.text == "{" )
		{
			if ( depth == 0 )
			{
				if ( t.text == "(" && i > 0 && d->tokens[i - 1].kind == TokIdent )
				{
					callee = i - 1;
				}
				break;
			}
			--depth;
		}
		else if ( t.text == "," && depth == 0 )
		{
			++argument;
		}
		else if ( t.text == ";" && depth == 0 )
		{
			break;
		}
	}

	if ( callee < 0 )
	{
		reply( msg["id"], Json() );
		return;
	}

	std::string name = d->tokens[callee].text;
	std::string label;
	std::string documentation;
	std::vector<std::string> params;

	for ( const Symbol& s : d->symbols )
	{
		if ( (s.kind == SymFunction || s.kind == SymStruct) && s.name == name )
		{
			label = name + "(";
			for ( size_t i = 0; i < s.params.size(); ++i )
			{
				label += (i ? ", " : " ") + s.params[i];
			}
			label += s.params.empty() ? ")" : " )";
			params = s.params;
			documentation = s.doc;
		}
	}

	const WrenchProject* project = label.empty() ? projectFor( *d ) : nullptr;
	const WrenchNative* native = project ? project->native( name.compare(0, 2, "::") == 0 ? name.substr(2) : name ) : nullptr;
	if ( native )
	{
		label = native->signature();
		documentation = native->doc;
		for ( const std::string& prm : native->params )
		{
			params.push_back( prm );
		}
		if ( !native->paramsKnown )
		{
			params.push_back( "..." );
		}
	}

	if ( label.empty() )
	{
		const WrenchLibraryEntry* e = library( name, 0 );
		if ( !e )
		{
			reply( msg["id"], Json() );
			return;
		}
		label = e->signature;
		documentation = e->doc;
		// parameters are the comma separated words between the parentheses
		size_t open = label.find( '(' );
		size_t close = label.rfind( ')' );
		if ( open != std::string::npos && close != std::string::npos && close > open )
		{
			std::string inner = label.substr( open + 1, close - open - 1 );
			size_t s = 0;
			while ( s <= inner.size() )
			{
				size_t c = inner.find( ',', s );
				std::string part = inner.substr( s, c == std::string::npos ? std::string::npos : c - s );
				size_t a = part.find_first_not_of( " []" );
				size_t b = part.find_last_not_of( " []" );
				if ( a != std::string::npos )
				{
					params.push_back( part.substr(a, b - a + 1) );
				}
				if ( c == std::string::npos )
				{
					break;
				}
				s = c + 1;
			}
		}
	}

	Json paramJson = Json::array();
	for ( const std::string& prm : params )
	{
		Json j = Json::object();
		j.set( "label", prm );
		paramJson.push( j );
	}

	Json sig = Json::object();
	sig.set( "label", label );
	if ( !documentation.empty() )
	{
		Json docs = Json::object();
		docs.set( "kind", "markdown" );
		docs.set( "value", documentation );
		sig.set( "documentation", docs );
	}
	sig.set( "parameters", paramJson );

	Json sigs = Json::array();
	sigs.push( sig );
	Json result = Json::object();
	result.set( "signatures", sigs );
	result.set( "activeSignature", 0 );
	result.set( "activeParameter", params.empty() ? 0 : (std::min)(argument, (int)params.size() - 1) );
	reply( msg["id"], result );
}

void LspServer::onRename( const Json& msg, bool prepare )
{
	Document* d = doc( msg["params"] );
	Position p;
	p.line = msg["params"]["position"]["line"].asInt();
	p.character = msg["params"]["position"]["character"].asInt();
	Range range;
	int s = d ? symbolAt( *d, p, &range ) : -1;

	if ( prepare )
	{
		if ( s < 0 )
		{
			reply( msg["id"], Json() );
			return;
		}
		reply( msg["id"], rangeJson(range) );
		return;
	}

	std::string newName = msg["params"]["newName"].asString( "" );
	bool valid = !newName.empty() && (isalpha((unsigned char)newName[0]) || newName[0] == '_');
	for ( char c : newName )
	{
		valid = valid && (isalnum((unsigned char)c) || c == '_');
	}
	if ( s < 0 || !valid )
	{
		replyError( msg["id"], -32602, s < 0 ? "nothing to rename here" : "not a valid wrench identifier" );
		return;
	}
	// the library table lists every reserved word of the compiler
	if ( library(newName, 3) )
	{
		replyError( msg["id"], -32602, "'" + newName + "' is a reserved word" );
		return;
	}

	Json edits = Json::array();
	for ( const Reference& r : d->refs )
	{
		if ( r.symbol == s )
		{
			Json e = Json::object();
			e.set( "range", rangeJson(r.range) );
			e.set( "newText", newName );
			edits.push( e );
		}
	}
	Json changes = Json::object();
	changes.set( d->uri, edits );
	Json result = Json::object();
	result.set( "changes", changes );
	reply( msg["id"], result );
}

void LspServer::onFolding( const Json& msg )
{
	Document* d = doc( msg["params"] );
	Json result = Json::array();
	if ( d )
	{
		for ( const auto& f : d->folds )
		{
			Json j = Json::object();
			j.set( "startLine", f.first );
			j.set( "endLine", f.second );
			result.push( j );
		}
	}
	reply( msg["id"], result );
}

void LspServer::dispatch( const Json& msg )
{
	if ( msg.has("wrenchParseError") )
	{
		// the stream could not parse a body; JSON-RPC answers with a null id
		replyError( Json(), -32700, "Parse error: " + msg["wrenchParseError"].asString("") );
		return;
	}

	std::string method = msg["method"].asString();
	bool isRequest = msg.has( "id" );

	if ( method == "initialize" )
	{
		onInitialize( msg );
	}
	else if ( method == "initialized" )
	{
	}
	else if ( method == "shutdown" )
	{
		m_shutdown = true;
		reply( msg["id"], Json() );
	}
	else if ( method == "exit" )
	{
		m_exit = true;
	}
	else if ( method == "textDocument/didOpen" )
	{
		const Json& td = msg["params"]["textDocument"];
		Document& d = m_docs[td["uri"].asString()];
		d.uri = td["uri"].asString();
		d.text = td["text"].asString();
		d.version = td["version"].asInt();
		publishDiagnostics( d );
	}
	else if ( method == "textDocument/didChange" )
	{
		auto it = m_docs.find( msg["params"]["textDocument"]["uri"].asString() );
		const Json& changes = msg["params"]["contentChanges"];
		if ( it != m_docs.end() && changes.size() )
		{
			it->second.text = changes[changes.size() - 1]["text"].asString();
			it->second.version = msg["params"]["textDocument"]["version"].asInt();
			it->second.dirty = true;
			it->second.stale = true;
			m_lastChange = std::chrono::steady_clock::now();
		}
	}
	else if ( method == "textDocument/didSave" )
	{
		auto it = m_docs.find( msg["params"]["textDocument"]["uri"].asString() );
		if ( it != m_docs.end() )
		{
			publishDiagnostics( it->second );
		}
	}
	else if ( method == "textDocument/didClose" )
	{
		std::string uri = msg["params"]["textDocument"]["uri"].asString();
		m_docs.erase( uri );
		Json params = Json::object();
		params.set( "uri", uri );
		params.set( "diagnostics", Json::array() );
		notify( "textDocument/publishDiagnostics", params );
	}
	else if ( method == "workspace/didChangeConfiguration" )
	{
		const Json& settings = msg["params"]["settings"]["wrench"];
		if ( settings.isObject() )
		{
			m_nonStrict = settings["nonStrict"].asBool( m_nonStrict );
			for ( auto& d : m_docs )
			{
				publishDiagnostics( d.second );
			}
		}
	}
	else if ( method == "workspace/didChangeWatchedFiles" )
	{
		// a wrench.json changed: forget cached projects, recheck everything
		m_projects.clear();
		for ( auto& d : m_docs )
		{
			publishDiagnostics( d.second );
		}
	}
	else if ( method == "textDocument/hover" )
	{
		onHover( msg );
	}
	else if ( method == "textDocument/definition" )
	{
		onDefinition( msg );
	}
	else if ( method == "textDocument/references" )
	{
		onReferences( msg, false );
	}
	else if ( method == "textDocument/documentHighlight" )
	{
		onReferences( msg, true );
	}
	else if ( method == "textDocument/documentSymbol" )
	{
		onDocumentSymbol( msg );
	}
	else if ( method == "textDocument/completion" )
	{
		onCompletion( msg );
	}
	else if ( method == "textDocument/signatureHelp" )
	{
		onSignatureHelp( msg );
	}
	else if ( method == "textDocument/rename" )
	{
		onRename( msg, false );
	}
	else if ( method == "textDocument/prepareRename" )
	{
		onRename( msg, true );
	}
	else if ( method == "textDocument/foldingRange" )
	{
		onFolding( msg );
	}
	else if ( isRequest )
	{
		replyError( msg["id"], -32601, "method not supported: " + method );
	}
}

int LspServer::run()
{
	m_stream.startReader();
	while ( !m_exit )
	{
		Json message;
		if ( m_stream.next(message, 100) )
		{
			dispatch( message );
		}
		else if ( m_stream.closed() )
		{
			break;
		}

		// recompile edited documents once typing pauses
		auto now = std::chrono::steady_clock::now();
		if ( now - m_lastChange > std::chrono::milliseconds(250) )
		{
			for ( auto& d : m_docs )
			{
				if ( d.second.dirty )
				{
					publishDiagnostics( d.second );
				}
			}
		}
	}
	return m_shutdown ? 0 : 1;
}

}

int wr_lspMain( int argn, char* argv[] )
{
	LspServer server;
	return server.run();
}

#endif
