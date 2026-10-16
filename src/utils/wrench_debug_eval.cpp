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

#include "utils/wrench_debug_eval.h"

#include <ctype.h>
#include <math.h>

namespace
{

enum TokenKind
{
	TokEnd,
	TokName,
	TokInt,
	TokFloat,
	TokString,
	TokOp,
};

struct Token
{
	TokenKind kind = TokEnd;
	std::string text;
	int32_t i = 0;
	float f = 0;
};

bool tokenize( const std::string& s, std::vector<Token>& out, std::string& error )
{
	size_t p = 0;
	while ( p < s.size() )
	{
		unsigned char c = (unsigned char)s[p];
		if ( isspace(c) )
		{
			++p;
			continue;
		}

		Token t;
		if ( isalpha(c) || c == '_' || (c == ':' && p + 1 < s.size() && s[p + 1] == ':') )
		{
			size_t start = p;
			if ( c == ':' )
			{
				p += 2;
			}
			while ( p < s.size() && (isalnum((unsigned char)s[p]) || s[p] == '_' || (s[p] == ':' && p + 1 < s.size() && s[p + 1] == ':')) )
			{
				p += (s[p] == ':') ? 2 : 1;
			}
			t.kind = TokName;
			t.text = s.substr( start, p - start );
		}
		else if ( isdigit(c) || (c == '.' && p + 1 < s.size() && isdigit((unsigned char)s[p + 1])) )
		{
			size_t start = p;
			bool isFloat = false;
			if ( c == '0' && p + 1 < s.size() && (s[p + 1] == 'x' || s[p + 1] == 'X') )
			{
				p += 2;
				while ( p < s.size() && isxdigit((unsigned char)s[p]) )
				{
					++p;
				}
				t.kind = TokInt;
				t.i = (int32_t)strtoul( s.substr(start + 2, p - start - 2).c_str(), 0, 16 );
			}
			else
			{
				while ( p < s.size() && (isdigit((unsigned char)s[p]) || s[p] == '.' || s[p] == 'e' || s[p] == 'E'
								|| ((s[p] == '-' || s[p] == '+') && (s[p - 1] == 'e' || s[p - 1] == 'E'))) )
				{
					if ( s[p] == '.' || s[p] == 'e' || s[p] == 'E' )
					{
						isFloat = true;
					}
					++p;
				}
				if ( p < s.size() && (s[p] == 'f' || s[p] == 'F') )
				{
					isFloat = true;
					++p;
				}
				std::string num = s.substr( start, p - start );
				if ( isFloat )
				{
					t.kind = TokFloat;
					t.f = (float)atof( num.c_str() );
				}
				else
				{
					t.kind = TokInt;
					t.i = (int32_t)strtoll( num.c_str(), 0, 10 );
				}
			}
		}
		else if ( c == '"' || c == '\'' )
		{
			char quote = (char)c;
			++p;
			while ( p < s.size() && s[p] != quote )
			{
				char ch = s[p++];
				if ( ch == '\\' && p < s.size() )
				{
					char e = s[p++];
					switch ( e )
					{
						case 'n': ch = '\n'; break;
						case 'r': ch = '\r'; break;
						case 't': ch = '\t'; break;
						case '0': ch = '\0'; break;
						default: ch = e; break;
					}
				}
				t.text += ch;
			}
			if ( p >= s.size() )
			{
				error = "unterminated string";
				return false;
			}
			++p;
			t.kind = TokString;
		}
		else
		{
			static const char* ops[] = { "&&", "||", "==", "!=", "<=", ">=", "<", ">", "+", "-", "*", "/", "%",
																	"!", "(", ")", "[", "]", ".", 0 };
			t.kind = TokOp;
			for ( int o = 0; ops[o]; ++o )
			{
				size_t len = strlen( ops[o] );
				if ( s.compare(p, len, ops[o]) == 0 )
				{
					t.text = ops[o];
					p += len;
					break;
				}
			}
			if ( t.text.empty() )
			{
				error = std::string( "unexpected character '" ) + (char)c + "'";
				return false;
			}
		}
		out.push_back( t );
	}

	Token end;
	out.push_back( end );
	return true;
}

bool isNumber( const WrenchDebugValue& v )
{
	return v.tag == WRD_ValInt || v.tag == WRD_ValFloat;
}

float asFloat( const WrenchDebugValue& v )
{
	return v.tag == WRD_ValFloat ? v.f : (float)v.i;
}

}

struct WrenchDebugEvaluator::Parser
{
	WrenchDebugEvaluator& ev;
	std::vector<Token> toks;
	size_t pos = 0;
	std::string error;

	// location of the last plain path parsed, for assignment
	WrenchDebugLocation lastWhere;
	bool lastWasPath = false;
	int depth = 0;

	explicit Parser( WrenchDebugEvaluator& e ) : ev( e ) {}

	const Token& peek() const { return toks[pos]; }
	bool isOp( const char* op ) const { return peek().kind == TokOp && peek().text == op; }

	bool expect( const char* op )
	{
		if ( !isOp(op) )
		{
			error = std::string( "expected '" ) + op + "'";
			return false;
		}
		++pos;
		return true;
	}

	bool parseExpression( WrenchDebugValue& out ) { return parseOr( out ); }

	bool parseOr( WrenchDebugValue& out )
	{
		if ( !parseAnd(out) )
		{
			return false;
		}
		while ( isOp("||") )
		{
			++pos;
			WrenchDebugValue rhs;
			if ( !parseAnd(rhs) )
			{
				return false;
			}
			out = WrenchDebugValue::makeInt( truthy(out) || truthy(rhs) );
			lastWasPath = false;
		}
		return true;
	}

	bool parseAnd( WrenchDebugValue& out )
	{
		if ( !parseEquality(out) )
		{
			return false;
		}
		while ( isOp("&&") )
		{
			++pos;
			WrenchDebugValue rhs;
			if ( !parseEquality(rhs) )
			{
				return false;
			}
			out = WrenchDebugValue::makeInt( truthy(out) && truthy(rhs) );
			lastWasPath = false;
		}
		return true;
	}

	bool compare( const WrenchDebugValue& a, const WrenchDebugValue& b, const std::string& op, WrenchDebugValue& out )
	{
		int c = 0;
		if ( a.isString() && b.isString() )
		{
			std::string sa, sb;
			ev.m_client.getString( a, sa );
			ev.m_client.getString( b, sb );
			c = sa < sb ? -1 : (sa > sb ? 1 : 0);
		}
		else if ( isNumber(a) && isNumber(b) )
		{
			if ( a.tag == WRD_ValInt && b.tag == WRD_ValInt )
			{
				c = a.i < b.i ? -1 : (a.i > b.i ? 1 : 0);
			}
			else
			{
				float fa = asFloat( a );
				float fb = asFloat( b );
				c = fa < fb ? -1 : (fa > fb ? 1 : 0);
			}
		}
		else if ( op == "==" || op == "!=" )
		{
			// containers compare by identity
			c = (a.tag == b.tag && a.handle == b.handle && a.handle != 0) ? 0 : 1;
		}
		else
		{
			error = "cannot compare " + a.typeName() + " with " + b.typeName();
			return false;
		}

		bool r = false;
		if ( op == "==" )
		{
			r = c == 0;
		}
		else if ( op == "!=" )
		{
			r = c != 0;
		}
		else if ( op == "<" )
		{
			r = c < 0;
		}
		else if ( op == "<=" )
		{
			r = c <= 0;
		}
		else if ( op == ">" )
		{
			r = c > 0;
		}
		else if ( op == ">=" )
		{
			r = c >= 0;
		}
		out = WrenchDebugValue::makeInt( r ? 1 : 0 );
		return true;
	}

	// == and != bind looser than < <= > >=, as in C and wrench
	bool parseEquality( WrenchDebugValue& out )
	{
		if ( !parseRelational(out) )
		{
			return false;
		}
		while ( isOp("==") || isOp("!=") )
		{
			std::string op = toks[pos++].text;
			WrenchDebugValue rhs;
			if ( !parseRelational(rhs) || !compare(out, rhs, op, out) )
			{
				return false;
			}
			lastWasPath = false;
		}
		return true;
	}

	bool parseRelational( WrenchDebugValue& out )
	{
		if ( !parseAdd(out) )
		{
			return false;
		}
		while ( isOp("<") || isOp("<=") || isOp(">") || isOp(">=") )
		{
			std::string op = toks[pos++].text;
			WrenchDebugValue rhs;
			if ( !parseAdd(rhs) || !compare(out, rhs, op, out) )
			{
				return false;
			}
			lastWasPath = false;
		}
		return true;
	}

	bool arith( WrenchDebugValue& a, const WrenchDebugValue& b, const std::string& op )
	{
		if ( op == "+" && (a.isString() || b.isString()) )
		{
			std::string sa, sb;
			if ( a.isString() )
			{
				ev.m_client.getString( a, sa );
			}
			else
			{
				sa = a.display();
			}
			if ( b.isString() )
			{
				ev.m_client.getString( b, sb );
			}
			else
			{
				sb = b.display();
			}
			a = WrenchDebugValue::makeString( sa + sb );
			return true;
		}

		if ( !isNumber(a) || !isNumber(b) )
		{
			error = "operator " + op + " needs numbers";
			return false;
		}

		if ( a.tag == WRD_ValInt && b.tag == WRD_ValInt )
		{
			int64_t x = a.i;
			int64_t y = b.i;
			int64_t r = 0;
			if ( op == "+" )
			{
				r = x + y;
			}
			else if ( op == "-" )
			{
				r = x - y;
			}
			else if ( op == "*" )
			{
				r = x * y;
			}
			else if ( op == "/" )
			{
				r = y ? x / y : 0;
			}
			else if ( op == "%" )
			{
				r = y ? x % y : 0;
			}
			a = WrenchDebugValue::makeInt( (int32_t)r );
			return true;
		}

		float x = asFloat( a );
		float y = asFloat( b );
		float r = 0;
		if ( op == "+" )
		{
			r = x + y;
		}
		else if ( op == "-" )
		{
			r = x - y;
		}
		else if ( op == "*" )
		{
			r = x * y;
		}
		else if ( op == "/" )
		{
			r = y != 0 ? x / y : 0;
		}
		else if ( op == "%" )
		{
			r = y != 0 ? fmodf( x, y ) : 0;
		}
		a = WrenchDebugValue::makeFloat( r );
		return true;
	}

	bool parseAdd( WrenchDebugValue& out )
	{
		if ( !parseMul(out) )
		{
			return false;
		}
		while ( isOp("+") || isOp("-") )
		{
			std::string op = toks[pos++].text;
			WrenchDebugValue rhs;
			if ( !parseMul(rhs) || !arith(out, rhs, op) )
			{
				return false;
			}
			lastWasPath = false;
		}
		return true;
	}

	bool parseMul( WrenchDebugValue& out )
	{
		if ( !parseUnary(out) )
		{
			return false;
		}
		while ( isOp("*") || isOp("/") || isOp("%") )
		{
			std::string op = toks[pos++].text;
			WrenchDebugValue rhs;
			if ( !parseUnary(rhs) || !arith(out, rhs, op) )
			{
				return false;
			}
			lastWasPath = false;
		}
		return true;
	}

	// every nesting (parentheses, brackets, unary operators) comes through
	// here, so this bounds the recursion
	bool parseUnary( WrenchDebugValue& out )
	{
		if ( ++depth > 200 )
		{
			error = "expression nested too deeply";
			--depth;
			return false;
		}
		bool ok = parseUnaryLevel( out );
		--depth;
		return ok;
	}

	bool parseUnaryLevel( WrenchDebugValue& out )
	{
		if ( isOp("!") )
		{
			++pos;
			if ( !parseUnary(out) )
			{
				return false;
			}
			out = WrenchDebugValue::makeInt( truthy(out) ? 0 : 1 );
			lastWasPath = false;
			return true;
		}
		if ( isOp("-") )
		{
			++pos;
			if ( !parseUnary(out) )
			{
				return false;
			}
			if ( out.tag == WRD_ValInt )
			{
				out.i = (int32_t)(0u - (uint32_t)out.i);
			}
			else if ( out.tag == WRD_ValFloat )
			{
				out.f = -out.f;
			}
			else
			{
				error = "cannot negate " + out.typeName();
				return false;
			}
			lastWasPath = false;
			return true;
		}
		return parsePostfix( out );
	}

	bool parsePostfix( WrenchDebugValue& out )
	{
		WrenchDebugLocation where;
		bool path = false;

		const Token& t = peek();
		if ( t.kind == TokInt )
		{
			out = WrenchDebugValue::makeInt( t.i );
			++pos;
		}
		else if ( t.kind == TokFloat )
		{
			out = WrenchDebugValue::makeFloat( t.f );
			++pos;
		}
		else if ( t.kind == TokString )
		{
			out = WrenchDebugValue::makeString( t.text );
			++pos;
		}
		else if ( t.kind == TokName )
		{
			std::string name = t.text;
			++pos;
			bool global = name.size() > 2 && name[0] == ':' && name[1] == ':';
			if ( !ev.lookupName(global ? name.substr(2) : name, global, out, where, error) )
			{
				return false;
			}
			path = true;
		}
		else if ( isOp("(") )
		{
			++pos;
			if ( !parseExpression(out) || !expect(")") )
			{
				return false;
			}
		}
		else
		{
			error = t.kind == TokEnd ? "unexpected end of expression" : "unexpected '" + t.text + "'";
			return false;
		}

		for ( ;; )
		{
			if ( isOp(".") )
			{
				++pos;
				if ( peek().kind != TokName )
				{
					error = "expected a member name after '.'";
					return false;
				}
				WrenchDebugValue key = WrenchDebugValue::makeString( peek().text );
				++pos;
				WrenchDebugValue next;
				if ( !ev.child(out, key, true, next, where, error) )
				{
					return false;
				}
				out = next;
			}
			else if ( isOp("[") )
			{
				++pos;
				bool savedPath = lastWasPath;
				WrenchDebugLocation savedWhere = lastWhere;
				WrenchDebugValue key;
				if ( !parseExpression(key) || !expect("]") )
				{
					return false;
				}
				lastWasPath = savedPath;
				lastWhere = savedWhere;
				WrenchDebugValue next;
				if ( !ev.child(out, key, false, next, where, error) )
				{
					return false;
				}
				out = next;
			}
			else
			{
				break;
			}
		}

		lastWasPath = path;
		lastWhere = where;
		return true;
	}
};

bool WrenchDebugEvaluator::truthy( const WrenchDebugValue& v )
{
	switch ( v.tag )
	{
		case WRD_ValInt: return v.i != 0;
		case WRD_ValFloat: return v.f != 0;
		case WRD_ValString: return v.count != 0;
		default: return v.handle != 0;
	}
}

size_t WrenchDebugEvaluator::findAssignment( const std::string& text )
{
	int nesting = 0;
	char quote = 0;
	for ( size_t i = 0; i < text.size(); ++i )
	{
		char c = text[i];
		if ( quote )
		{
			if ( c == '\\' )
			{
				++i;
			}
			else if ( c == quote )
			{
				quote = 0;
			}
			continue;
		}
		if ( c == '"' || c == '\'' )
		{
			quote = c;
		}
		else if ( c == '(' || c == '[' )
		{
			++nesting;
		}
		else if ( c == ')' || c == ']' )
		{
			--nesting;
		}
		else if ( c == '=' && nesting == 0 && i > 0 )
		{
			char before = text[i - 1];
			bool doubled = i + 1 < text.size() && text[i + 1] == '=';
			if ( doubled )
			{
				++i; // ==
				continue;
			}
			if ( before != '=' && before != '!' && before != '<' && before != '>' )
			{
				return i;
			}
		}
	}
	return std::string::npos;
}

bool WrenchDebugEvaluator::lookupName( const std::string& name, bool forceGlobal, WrenchDebugValue& result,
																			WrenchDebugLocation& where, std::string& error )
{
	if ( !forceGlobal && m_frame >= 0 && (size_t)m_frame < m_frames.size() && m_frames[m_frame].function >= 0 )
	{
		int slot = m_program.localSlot( m_frames[m_frame].function, name );
		if ( slot >= 0 )
		{
			std::vector<WrenchDebugValue> locals;
			if ( !m_client.getLocals(m_frame, locals) )
			{
				error = m_client.lastError();
				return false;
			}
			if ( (size_t)slot < locals.size() )
			{
				result = locals[slot];
				where.kind = WrenchDebugLocation::Local;
				where.frame = m_frame;
				where.slot = slot;
				return true;
			}
		}
	}

	int slot = m_program.globalSlot( name );
	if ( slot >= 0 )
	{
		std::vector<WrenchDebugValue> globals;
		if ( !m_client.getGlobals(globals) )
		{
			error = m_client.lastError();
			return false;
		}
		if ( (size_t)slot < globals.size() )
		{
			result = globals[slot];
			where.kind = WrenchDebugLocation::Global;
			where.slot = slot;
			return true;
		}
	}

	error = "no variable named '" + name + "'";
	return false;
}

bool WrenchDebugEvaluator::child( const WrenchDebugValue& parent, const WrenchDebugValue& key, bool member,
																	WrenchDebugValue& result, WrenchDebugLocation& where, std::string& error )
{
	if ( parent.tag == WRD_ValArray || parent.tag == WRD_ValRaw || (parent.tag == WRD_ValString && !member) )
	{
		if ( key.tag != WRD_ValInt )
		{
			error = "index must be an integer";
			return false;
		}
		if ( key.i < 0 || (uint32_t)key.i >= parent.count )
		{
			error = "index " + std::to_string( key.i ) + " out of range (size " + std::to_string( parent.count ) + ")";
			return false;
		}
		std::vector<WrenchDebugChild> kids;
		uint32_t total = 0;
		if ( !m_client.getChildren(parent, (uint32_t)key.i, 1, kids, total, &m_program) || kids.empty() )
		{
			error = m_client.lastError();
			return false;
		}
		result = kids[0].value;
		where.kind = (parent.tag == WRD_ValArray) ? WrenchDebugLocation::Child : WrenchDebugLocation::None;
		where.parent = parent;
		where.index = (uint32_t)key.i;
		return true;
	}

	if ( parent.tag == WRD_ValStruct || parent.tag == WRD_ValHash )
	{
		std::vector<WrenchDebugChild> kids;
		uint32_t total = 0;
		if ( !m_client.getChildren(parent, 0, parent.count, kids, total, &m_program) )
		{
			error = m_client.lastError();
			return false;
		}

		std::string keyText;
		if ( key.isString() )
		{
			m_client.getString( key, keyText );
		}

		for ( const WrenchDebugChild& c : kids )
		{
			bool match = false;
			if ( parent.tag == WRD_ValStruct )
			{
				match = key.isString() && c.name == keyText;
			}
			else if ( key.isString() && c.key.isString() )
			{
				std::string k;
				m_client.getString( c.key, k );
				match = (k == keyText);
			}
			else if ( key.tag == WRD_ValInt && c.key.tag == WRD_ValInt )
			{
				match = (key.i == c.key.i);
			}
			else if ( isNumber(key) && isNumber(c.key) )
			{
				match = asFloat( key ) == asFloat( c.key );
			}

			if ( match )
			{
				result = c.value;
				where.kind = WrenchDebugLocation::Child;
				where.parent = parent;
				where.index = c.index;
				return true;
			}
		}

		error = (parent.tag == WRD_ValStruct ? "no member " : "no key ") + (key.isString() ? keyText : key.display());
		return false;
	}

	error = "a " + parent.typeName() + " has no elements";
	return false;
}

bool WrenchDebugEvaluator::evaluate( const std::string& expression, WrenchDebugValue& result, std::string& error )
{
	Parser p( *this );
	if ( !tokenize(expression, p.toks, error) )
	{
		return false;
	}
	if ( !p.parseExpression(result) )
	{
		error = p.error;
		return false;
	}
	if ( p.peek().kind != TokEnd )
	{
		error = "unexpected '" + p.peek().text + "'";
		return false;
	}
	return true;
}

bool WrenchDebugEvaluator::resolve( const std::string& path, WrenchDebugValue& result, WrenchDebugLocation& where, std::string& error )
{
	Parser p( *this );
	if ( !tokenize(path, p.toks, error) )
	{
		return false;
	}
	if ( !p.parsePostfix(result) )
	{
		error = p.error;
		return false;
	}
	if ( p.peek().kind != TokEnd || !p.lastWasPath )
	{
		error = "'" + path + "' is not a variable";
		return false;
	}
	where = p.lastWhere;
	return true;
}

bool WrenchDebugEvaluator::store( const WrenchDebugLocation& where, const WrenchDebugValue& value, WrenchDebugValue& result, std::string& error )
{
	if ( value.tag != WRD_ValInt && value.tag != WRD_ValFloat && value.tag != WRD_ValString )
	{
		error = "only numbers and strings can be assigned";
		return false;
	}

	WrenchDebugValue v = value;
	if ( v.isString() && v.count > v.text.size() )
	{
		// a string fetched from the target, send the whole thing back
		m_client.getString( value, v.text );
		v.count = (uint32_t)v.text.size();
	}

	bool ok = false;
	switch ( where.kind )
	{
		case WrenchDebugLocation::Local: ok = m_client.setLocal( where.frame, where.slot, v, result ); break;
		case WrenchDebugLocation::Global: ok = m_client.setGlobal( where.slot, v, result ); break;
		case WrenchDebugLocation::Child: ok = m_client.setChild( where.parent, where.index, v, result ); break;
		default:
		{
			error = "this value cannot be changed";
			return false;
		}
	}

	if ( !ok )
	{
		error = m_client.lastError();
	}
	return ok;
}

bool WrenchDebugEvaluator::assign( const std::string& path, const std::string& valueText, WrenchDebugValue& result, std::string& error )
{
	WrenchDebugValue current;
	WrenchDebugLocation where;
	if ( !resolve(path, current, where, error) )
	{
		return false;
	}

	WrenchDebugValue value;
	if ( !evaluate(valueText, value, error) )
	{
		return false;
	}
	return store( where, value, result, error );
}

std::string WrenchDebugEvaluator::interpolate( const std::string& message )
{
	std::string out;
	size_t p = 0;
	while ( p < message.size() )
	{
		if ( message[p] == '{' )
		{
			size_t close = message.find( '}', p + 1 );
			if ( close != std::string::npos )
			{
				std::string expr = message.substr( p + 1, close - p - 1 );
				WrenchDebugValue v;
				std::string error;
				if ( evaluate(expr, v, error) )
				{
					out += describe( v, 0 );
				}
				else
				{
					out += "<" + error + ">";
				}
				p = close + 1;
				continue;
			}
		}
		out += message[p++];
	}
	return out;
}

std::string WrenchDebugEvaluator::describe( const WrenchDebugValue& v, int depth )
{
	if ( v.isString() )
	{
		std::string full;
		m_client.getString( v, full, 4096 );
		WrenchDebugValue copy = WrenchDebugValue::makeString( full );
		copy.count = v.count;
		return depth == 0 ? full : copy.display();
	}

	if ( depth > 0 && v.expandable() && v.count <= 16 )
	{
		std::vector<WrenchDebugChild> kids;
		uint32_t total = 0;
		if ( m_client.getChildren(v, 0, v.count, kids, total, &m_program) )
		{
			std::string out = (v.tag == WRD_ValArray || v.tag == WRD_ValRaw) ? "[" : "{";
			for ( size_t i = 0; i < kids.size(); ++i )
			{
				if ( i )
				{
					out += ", ";
				}
				if ( v.tag == WRD_ValStruct )
				{
					out += kids[i].name + ": ";
				}
				else if ( v.tag == WRD_ValHash )
				{
					out += kids[i].key.display() + ": ";
				}
				out += describe( kids[i].value, depth - 1 );
			}
			out += (v.tag == WRD_ValArray || v.tag == WRD_ValRaw) ? "]" : "}";
			return out;
		}
	}

	return v.display();
}

#endif
