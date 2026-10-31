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
#include <assert.h>

//------------------------------------------------------------------------------
const char* c_reserved[] =
{
	"enum",
	"break",
	"case",
	"continue",
	"default",
	"do",
	"else",
	"false",
	"float",
	"for",
	"if",
	"int",
	"return",
	"switch",
	"true",
	"function",
	"while",
	"new",
	"null",
	"struct",
	"goto",
	"export",
	"unit",
	"blank",
	""
};

//------------------------------------------------------------------------------
WRError WRCompilationContext::compile( const char* source,
									   const int size,
									   unsigned char** out,
									   int* outLen,
									   WRstr* errorMsg,
									   const uint8_t compilerOptionFlags )
{
	m_source = source;
	m_sourceLen = size;
	m_errLine = 0;
	m_errColumn = 0;
	m_lineScanPos = 0;
	m_lineScanLine = 1;

	*outLen = 0;
	*out = 0;

	m_lastParam = 0;
	m_lastCode = 0;
	m_lineMarkers = 0;

	m_pos = 0;
	m_err = WR_ERR_None;
	m_tokenErr = WR_ERR_None;
	m_EOF = false;
	m_parsingFor = false;
	m_parsingNew = false;
	m_unitTop = 0;
	m_foreachHash = 0;
	m_parseDepth = 0;
	m_newLongJumps = false; // m_longJumps and m_lastLongJumpPass come from the caller
	m_jumpKeyPos = -1;
	m_jumpKeyIndex = 0;

	m_units.setCount(1);
	m_namesAsHashes.setCount(0);
	m_privateGlobals.clear();
	m_privateFunctions.clear();
	m_privateNext = false;
	m_privateArmed = false;
	m_memberShadows.clear();
	m_declaredNames.clear();
	m_openBlocks.clear();
	m_openBlocks.append() = 0;
	m_nextBlock = 1;
	m_nameErrorPos = -1;

	m_loadedValue.p2 = INIT_AS_REF;

	m_addDebugSymbols = compilerOptionFlags & WR_EMBED_DEBUG_CODE;
	m_embedSourceCode = compilerOptionFlags & WR_EMBED_SOURCE_CODE;
	m_embedGlobalSymbols = compilerOptionFlags & WR_INCLUDE_GLOBALS;
	m_nonStrict = (compilerOptionFlags & WR_NON_STRICT_VAR) != 0;

	do
	{
		WRExpressionContext ex;
		WRstr& token = ex.token;
		WRValue& value = ex.value;
		if ( !getToken(ex) )
		{
			break;
		}

		m_loadedToken = token;
		m_loadedValue = value;
		m_loadedQuoted = m_quoted;
		
		parseStatement( 0, ';', O_GlobalStop, false ); // top level statements share the outermost block

	} while ( !m_EOF && (m_err == WR_ERR_None) );

	if ( m_tokenErr != WR_ERR_None )
	{
		m_err = m_tokenErr;
	}

	if ( m_err == WR_ERR_None )
	{
		checkNamesAsHashes();
	}

	WRstr msg;

	if ( m_err != WR_ERR_None )
	{
		if ( m_nameErrorPos >= 0 )
		{
			m_pos = m_nameErrorPos; // where the name was resolved, parsing may have gone on
		}
		int onChar;
		int onLine;
		WRstr line;
		getSourcePosition( onLine, onChar, &line );
		m_errLine = onLine;
		m_errColumn = onChar;

		// the line of the file it is in, named when the source has a name
		int file = 0;
		int fileLine = onLine;
		if ( m_sources )
		{
			m_sources->locate( onLine, file, fileLine );
		}
		if ( m_sources && m_sources->fileName(file).size() )
		{
			msg.format( "%s:%d\n", m_sources->fileName(file).c_str(), fileLine );
		}
		else
		{
			msg.format( "line:%d\n", fileLine );
		}
		msg.appendFormat( "err: %s[%d]\n", c_errStrings[m_err], m_err );
		msg.appendFormat( "%-5d %s\n", fileLine, line.c_str() );

		for( int i=0; i<onChar; i++ )
		{
			msg.appendFormat(" ");
		}

		msg.appendFormat( "     ^\n" );

		// the message is only returned, the caller decides whether and where
		// it is shown
		if ( errorMsg )
		{
			(*errorMsg) = msg;
		}

		return m_err;
	}

	// the end of the global code returns 0, even after a return statement:
	// a jump over that return (if ( x ) { return 1; }) lands here
	pushOpcode( m_units[0].bytecode, O_LiteralZero );
	pushOpcode( m_units[0].bytecode, O_GlobalStop );
	pushOpcode( m_units[0].bytecode, O_Stop );

	link( out, outLen, compilerOptionFlags );

	if ( !m_err && m_newLongJumps )
	{
		// the image has 16-bit jumps that do not reach, it is only good for
		// finding them: the caller compiles again with them in long form
		g_free( *out );
		*out = 0;
		*outLen = 0;
		if ( m_lastLongJumpPass )
		{
			m_err = WR_ERR_bad_goto_location;
		}
	}

	if ( m_err )
	{
		if ( errorMsg )
		{
			errorMsg->format( "link error: %s[%d]\n", c_errStrings[m_err], m_err );
		}
	}

	return m_err;
}

// the file of the source being parsed (0 without includes)
int WRCompilationContext::currentFile()
{
	if ( !m_sources || !m_sources->hasIncludes() )
	{
		return 0;
	}
	int file = 0;
	int line = 0;
	m_sources->locate( getSourcePosition(), file, line );
	return file;
}

// a private name as it is kept: "x@util.w", with the file's index too when
// two included files share a base name
WRstr WRCompilationContext::privateLabel( const WRstr& name, int file )
{
	WRstr tag = "main";
	if ( m_sources && m_sources->fileName(file).size() )
	{
		const char* full = m_sources->fileName( file ).c_str();
		const char* base = full;
		for( const char* c = full; *c; ++c )
		{
			if ( *c == '/' || *c == '\\' )
			{
				base = c + 1;
			}
		}
		tag = base;
		for( unsigned int f=0; f<m_sources->fileCount(); ++f )
		{
			const char* other = m_sources->fileName( f ).c_str();
			size_t otherLen = strlen( other );
			size_t baseLen = strlen( base );
			if ( (int)f == file || otherLen < baseLen || strcmp(other + otherLen - baseLen, base) )
			{
				continue;
			}
			char before = (otherLen == baseLen) ? '/' : other[otherLen - baseLen - 1];
			if ( before == '/' || before == '\\' )
			{
				tag.appendFormat( "#%d", file );
				break;
			}
		}
	}
	WRstr label = name;
	label += "@";
	label += tag;
	return label;
}

int WRCompilationContext::findPrivate( const WRarray<WRPrivateName>& names, uint32_t hash, int file ) const
{
	for( unsigned int i=0; i<names.count(); ++i )
	{
		if ( names[i].hash == hash && names[i].file == file )
		{
			return names[i].index;
		}
	}
	return -1;
}

// "var p" starting a statement in a struct with a parameter p: p becomes a
// member. The statement declares it under a hidden name, so p in the
// initializer still reads the parameter
bool WRCompilationContext::shadowParameter( WRExpression& expression, WRstr& label )
{
	if ( m_unitTop == 0 || !expression.bytecode.isStructSpace )
	{
		return false;
	}

	uint32_t hash = wr_hashStr( label );
	WRBytecode& unitCode = m_units[m_unitTop].bytecode;
	for( unsigned int a=0; a<m_units[m_unitTop].arguments && a<unitCode.localSpace.count(); ++a )
	{
		if ( unitCode.localSpace[a].hash == hash )
		{
			WRMemberShadow& shadow = m_memberShadows.append();
			shadow.argument = (int)a;
			shadow.label = label;
			label += "@member";
			return true;
		}
	}

	return false;
}

// after the statement: the member takes the name, the parameter keeps its
// slot (and its label for the debugger) under a hash no name has
void WRCompilationContext::applyMemberShadows( WRBytecode& bytecode )
{
	for( unsigned int s=0; s<m_memberShadows.count(); ++s )
	{
		WRstr hidden = m_memberShadows[s].label;
		hidden += "@member";
		uint32_t hiddenHash = wr_hashStr( hidden );
		for( unsigned int i=0; i<bytecode.localSpace.count(); ++i )
		{
			if ( bytecode.localSpace[i].hash == hiddenHash )
			{
				WRstr argument = m_memberShadows[s].label;
				argument += "@argument";
				bytecode.localSpace[m_memberShadows[s].argument].hash = wr_hashStr( argument );
				bytecode.localSpace[i].hash = wr_hashStr( m_memberShadows[s].label );
				bytecode.localSpace[i].label = m_memberShadows[s].label;
				break;
			}
		}
	}

	m_memberShadows.clear();
}

// An unknown name may still stand for a function's hash
// ("var f = callback;", "register( callback );"); once every function is
// known, anything else is a misspelled variable
void WRCompilationContext::checkNamesAsHashes()
{
	for( unsigned int n = 0; n < m_namesAsHashes.count(); ++n )
	{
		uint32_t hash = wr_hashStr( m_namesAsHashes[n].name );
		bool known = false;
		for( unsigned int u = 1; u < m_units.count() && !known; ++u )
		{
			known = m_units[u].hash == hash;
		}
		for( unsigned int p = 0; p < m_privateFunctions.count() && !known; ++p )
		{
			known = m_privateFunctions[p].hash == hash;
		}
		if ( !known )
		{
			m_pos = m_namesAsHashes[n].pos; // report it where it was written
			m_err = WR_ERR_var_not_seen_before_label;
			return;
		}
	}
}

//------------------------------------------------------------------------------
WRError wr_compile( const char* source,
					const int size,
					unsigned char** out,
					int* outLen,
					WRstr* errMsg,
					const uint8_t compilerOptionFlags )
{
	return wr_compileEx( source, size, out, outLen, errMsg, compilerOptionFlags, 0, 0 );
}

WRError wr_compileEx( const char* source,
	const int size,
	unsigned char** out,
	int* outLen,
	WRstr* errMsg,
	const uint8_t compilerOptionFlags,
	int* errorLine,
	int* errorColumn )
{
	WRCompileOptions options;
	options.flags = compilerOptionFlags;
	return wr_compileWithOptions( source, size, out, outLen, options, errMsg, errorLine, errorColumn, 0 );
}

WRError wr_compileWithOptions( const char* source,
	const int size,
	unsigned char** out,
	int* outLen,
	const WRCompileOptions& options,
	WRstr* errMsg,
	int* errorLine,
	int* errorColumn,
	WRstr* errorFile )
{
	assert( sizeof(float) == 4 );
	assert( sizeof(int) == 4 );
	assert( sizeof(char) == 1 );
	assert( O_LAST <= 255 ); // every opcode, the compile-time placeholders included, is one byte

	*out = 0;
	*outLen = 0;

	WRSourceSet sources;
	WRError err = sources.expand( source, size, options );
	if ( err )
	{
		const WRstr& file = sources.fileName( sources.errorFile() );
		if ( errMsg )
		{
			errMsg->format( "%s:%d\nerr: %s[%d]\n", file.size() ? file.c_str() : "line", sources.errorLine(), c_errStrings[err], err );
		}
		if ( errorLine )
		{
			*errorLine = sources.errorLine();
		}
		if ( errorColumn )
		{
			*errorColumn = 0;
		}
		if ( errorFile )
		{
			*errorFile = file;
		}
		return err;
	}

	// a program whose jumps all fit 16 bits compiles in one pass; each further
	// pass emits the jumps found too far in a long form, which can push other
	// jumps out of reach, so a few passes may be needed
	const int maxPasses = 8;
	WRarray<uint32_t> longJumps;
	for( int pass = 0; pass < maxPasses; ++pass )
	{
		// a fresh compiler context per pass, completely unloaded when complete
		WRCompilationContext comp;
		comp.m_sources = &sources;
		comp.m_longJumps = longJumps;
		comp.m_lastLongJumpPass = (pass == maxPasses - 1);

		err = comp.compile( sources.text(), sources.size(), out, outLen, errMsg, options.flags );

		int file = 0;
		int line = comp.m_errLine;
		if ( line )
		{
			sources.locate( comp.m_errLine, file, line );
		}
		if ( errorLine )
		{
			*errorLine = line;
		}
		if ( errorColumn )
		{
			*errorColumn = comp.m_errColumn;
		}
		if ( errorFile )
		{
			*errorFile = sources.fileName( file );
		}

		if ( err || !comp.m_newLongJumps )
		{
			break;
		}

		longJumps = comp.m_longJumps;
	}

	return err;
}

//------------------------------------------------------------------------------
void streamDump( WROpcodeStream const& stream )
{
	WRstr str;
	wr_asciiDump( stream, stream.size(), str );
	printf( "%d:\n%s\n", stream.size(), str.c_str() );
}

//------------------------------------------------------------------------------
bool WRCompilationContext::isValidLabel( WRstr& token, bool& isGlobal, WRstr& prefix, bool& isLibConstant )
{
	isLibConstant = false;
	
	prefix.clear();

	if ( !token.size() || (!isalpha(token[0]) && token[0] != '_' && token[0] != ':') ) // non-zero size and start with alpha or '_' ?
	{
		return false;
	}

	isGlobal = false;

	if ( token[0] == ':' )
	{
		if ( (token.size() > 2) && (token[1] == ':') )
		{
			isGlobal = true;
		}
		else
		{
			return false;
		}
	}

	for( unsigned int i=0; c_reserved[i][0]; ++i )
	{
		if ( token == c_reserved[i] )
		{
			return false;
		}
	}

	bool foundColon = false;
	for( unsigned int i=0; i<token.size(); i++ ) // entire token alphanumeric or '_'?
	{
		if ( token[i] == ':' )
		{
			if ( token[++i] != ':' )
			{
				m_err = WR_ERR_unexpected_token;
				return false;
			}

			foundColon = true;

			if ( i != 1 )
			{
				for( unsigned int p=0; p<i - 1 && p<token.size(); ++p )
				{
					prefix += token[p];
				}

				for( unsigned int t=2; (t+prefix.size())<token.size(); ++t)
				{
					token[t-2] = token[t+prefix.size()];
				}
				token.shave( prefix.size() + 2 );
				i -= (prefix.size() + 1);
			}
			
			isGlobal = true;
			continue;
		}
		
		if ( !isalnum(token[i]) && token[i] != '_' )
		{
			return false;
		}
	}

	if ( foundColon && token[token.size() - 1] == ':' )
	{
		return false;
	}
	
	if ( foundColon && token[0] != ':' )
	{
		isLibConstant = true;
	}

	return true;
}


//------------------------------------------------------------------------------
void WRCompilationContext::pushDebug( uint16_t code, WRBytecode& bytecode, int param )
{
	if ( !m_addDebugSymbols )
	{
		return;
	}

	if ( code == m_lastCode && param == m_lastParam )
	{
		return;
	}

	// a call marker between two markers of one line does not make the line
	// start again: a breakpoint there would stop twice (println( f() ))
	if ( code != WRD_FunctionCall )
	{
		m_lastParam = param;
		m_lastCode = code;
	}

	if ( code == WRD_LineNumber && param > WRD_PayloadMask )
	{
		// lines past 16383 carry their upper bits in a prefix codeword
		pushOpcode( bytecode, O_DebugInfo );
		uint16_t high = WRD_LineHigh | (uint16_t)((param >> 14) & WRD_PayloadMask);
		unsigned char highData[2];
		pushData( bytecode, wr_pack16(high, highData), 2 );
	}
	
	pushOpcode( bytecode, O_DebugInfo );

	uint16_t codeword = code | ((uint16_t)param & WRD_PayloadMask);
	unsigned char data[2];
	pushData( bytecode, wr_pack16(codeword, data), 2 );

	if ( code == WRD_LineNumber )
	{
		++m_lineMarkers;
	}
}

//------------------------------------------------------------------------------
// every pass of a loop must meet a line marker, or a debugger could never
// pause it: a condition that emitted none since markersBefore (a literal,
// none at all: while ( 1 ), for ( ;; )) gets one for the loop's line where
// each pass begins
void WRCompilationContext::pushLoopMarker( WRBytecode& bytecode, int markersBefore, int line )
{
	if ( m_addDebugSymbols && m_lineMarkers == markersBefore )
	{
		m_lastCode = 0;
		m_lastParam = -1;
		pushDebug( WRD_LineNumber, bytecode, line );
	}
}

//------------------------------------------------------------------------------
int WRCompilationContext::getSourcePosition()
{
	// called for every token, so count lines incrementally: the scanner
	// almost always moves forward between calls
	if ( m_pos < m_lineScanPos )
	{
		m_lineScanPos = 0;
		m_lineScanLine = 1;
	}

	for( ; m_lineScanPos < m_pos && m_lineScanPos < m_sourceLen; ++m_lineScanPos )
	{
		if ( m_source[m_lineScanPos] == '\n' )
		{
			++m_lineScanLine;
		}
	}

	return m_lineScanLine;
}

//------------------------------------------------------------------------------
int WRCompilationContext::getSourcePosition( int& onLine, int& onChar, WRstr* line )
{
	onChar = 0;
	onLine = 1;

	for( int p = 0; line && p<m_sourceLen && m_source[p] != '\n'; p++ )
	{
		(*line) += (char)m_source[p];
	}

	for( int i=0; i<m_pos; ++i )
	{
		if ( m_source[i] == '\n' )
		{
			onLine++;
			onChar = 0;

			if( line )
			{
				line->clear();
				for( int p = i+1; p<m_sourceLen && m_source[p] != '\n'; p++ )
				{
					(*line) += (char)m_source[p];
				}
			}
		}
		else
		{
			onChar++;
		}
	}

	return onLine;
}

//------------------------------------------------------------------------------
int WRCompilationContext::addRelativeJumpTarget( WRBytecode& bytecode )
{
	BytecodeJumpOffset& J = bytecode.jumpOffsetTargets.append();
	J.references.clear();
	J.keys.clear();
	J.offset = -1; // not yet placed
	J.gotoHash = 0;
	return bytecode.jumpOffsetTargets.count() - 1;
}

//------------------------------------------------------------------------------
void WRCompilationContext::setRelativeJumpTarget( WRBytecode& bytecode, int relativeJumpTarget )
{
	bytecode.jumpOffsetTargets[relativeJumpTarget].offset = bytecode.all.size();
	bytecode.opcodes.clear(); // basic-block boundary: keyhole must not merge across it
}

// a jump is known by where in the source it was emitted, which stays the same
// when an earlier jump changes size in the next pass
uint32_t WRCompilationContext::nextJumpKey()
{
	if ( m_pos != m_jumpKeyPos )
	{
		m_jumpKeyPos = m_pos;
		m_jumpKeyIndex = 0;
	}
	return ((uint32_t)m_pos << 8) | (uint32_t)(m_jumpKeyIndex++ & 0xFF);
}

bool WRCompilationContext::isLongJump( uint32_t key )
{
	for( unsigned int i=0; i<m_longJumps.count(); ++i )
	{
		if ( m_longJumps[i] == key )
		{
			return true;
		}
	}
	return false;
}

void WRCompilationContext::needLongJump( uint32_t key )
{
	if ( !isLongJump(key) )
	{
		m_longJumps.append() = key;
	}
	m_newLongJumps = true;
}

// a jump farther than 16 bits: an unconditional one becomes the extended
// OX_RelativeJump32, a conditional one keeps its form and reaches a long jump
// next to it
//     branch tramp; jump over; tramp: jump32 target; over:
void WRCompilationContext::addLongJumpSource( WRBytecode& bytecode, WROpcode opcode, int relativeJumpTarget, const unsigned char* data, const int dataSize )
{
	int over = -1;
	if ( opcode != O_RelativeJump || data )
	{
		int tramp = addRelativeJumpTarget( bytecode );
		over = addRelativeJumpTarget( bytecode );
		addNearJumpSource( bytecode, opcode, tramp, data, dataSize, c_nearJumpKey );
		addNearJumpSource( bytecode, O_RelativeJump, over, 0, 0, c_nearJumpKey );
		setRelativeJumpTarget( bytecode, tramp );
	}

	pushOpcode( bytecode, O_Extended );
	unsigned char extended = OX_RelativeJump32;
	pushData( bytecode, &extended, 1 );
	bytecode.jumpOffsetTargets[relativeJumpTarget].references.append() = bytecode.all.size();
	bytecode.jumpOffsetTargets[relativeJumpTarget].keys.append() = c_nearJumpKey;
	pushData( bytecode, "\0\0\0\0", 4 ); // 32-bit relative vector

	if ( over != -1 )
	{
		setRelativeJumpTarget( bytecode, over );
	}
}

//------------------------------------------------------------------------------
void WRCompilationContext::addRelativeJumpSourceEx( WRBytecode& bytecode, WROpcode opcode, int relativeJumpTarget, const unsigned char* data, const int dataSize )
{
	uint32_t key = nextJumpKey();
	if ( isLongJump(key) )
	{
		addLongJumpSource( bytecode, opcode, relativeJumpTarget, data, dataSize );
	}
	else
	{
		addNearJumpSource( bytecode, opcode, relativeJumpTarget, data, dataSize, key );
	}
}

//------------------------------------------------------------------------------
// add a jump FROM with whatever opcode is supposed to do it
void WRCompilationContext::addRelativeJumpSource( WRBytecode& bytecode, WROpcode opcode, int relativeJumpTarget )
{
	addRelativeJumpSourceEx( bytecode, opcode, relativeJumpTarget, 0, 0 );
}

// a jump with a 16-bit relative vector (resolveRelativeJumps picks 8 bits
// when it fits); "data" goes between the opcode and the vector
void WRCompilationContext::addNearJumpSource( WRBytecode& bytecode, WROpcode opcode, int relativeJumpTarget, const unsigned char* data, const int dataSize, uint32_t key )
{
	if ( data )
	{
		pushOpcode( bytecode, opcode );

		int dataOffset = bytecode.all.size();
		pushData( bytecode, data, dataSize );

		bytecode.jumpOffsetTargets[relativeJumpTarget].references.append() = dataOffset;
		bytecode.jumpOffsetTargets[relativeJumpTarget].keys.append() = key;

		pushData( bytecode, "\t\t", 2 ); // 16-bit relative vector
		return;
	}

	pushOpcode( bytecode, opcode );

	int offset = bytecode.all.size();
	switch( bytecode.opcodes[bytecode.opcodes.size() - 1] )
	{
		case O_GSCompareEQBZ:
		case O_LSCompareEQBZ:
		case O_GSCompareNEBZ:
		case O_LSCompareNEBZ:
		case O_GSCompareGEBZ:
		case O_LSCompareGEBZ:
		case O_GSCompareLEBZ:
		case O_LSCompareLEBZ:
		case O_GSCompareGTBZ:
		case O_LSCompareGTBZ:
		case O_GSCompareLTBZ:
		case O_LSCompareLTBZ:
		case O_LocalBZ:
		case O_GlobalBZ:
		{
			--offset;
			break;
		}

		case O_LLCompareLTBZ:
		case O_LLCompareGTBZ:
		case O_LLCompareLEBZ:
		case O_LLCompareGEBZ:
		case O_LLCompareEQBZ:
		case O_LLCompareNEBZ:
		case O_GGCompareLTBZ:
		case O_GGCompareGTBZ:
		case O_GGCompareLEBZ:
		case O_GGCompareGEBZ:
		case O_GGCompareEQBZ:
		case O_GGCompareNEBZ:
		{
			offset -= 2;
			break;
		}

		default: break;
	}
	
	bytecode.jumpOffsetTargets[relativeJumpTarget].references.append() = offset;
	bytecode.jumpOffsetTargets[relativeJumpTarget].keys.append() = key;
	pushData( bytecode, "\t\t", 2 );
}

//------------------------------------------------------------------------------
void WRCompilationContext::resolveRelativeJumps( WRBytecode& bytecode )
{
	for( unsigned int j=0; j<bytecode.jumpOffsetTargets.count(); ++j )
	{
		if ( bytecode.jumpOffsetTargets[j].offset < 0 )
		{
			continue; // target not placed yet, will be resolved by a later pass
		}

		for( unsigned int t=0; t<bytecode.jumpOffsetTargets[j].references.count(); ++t )
		{
			int32_t diff = bytecode.jumpOffsetTargets[j].offset - bytecode.jumpOffsetTargets[j].references[t];

			int offset = bytecode.jumpOffsetTargets[j].references[t];
			WROpcode o = (WROpcode)bytecode.all[offset - 1];
			bool no8version = false;

			if ( wr_isLongJumpVector(bytecode.all, offset) )
			{
				wr_pack32( (uint32_t)diff, bytecode.all.p_str(offset) );
				continue;
			}

			switch( o )
			{
				case O_GSCompareEQBZ8:
				case O_LSCompareEQBZ8:
				case O_GSCompareNEBZ8:
				case O_LSCompareNEBZ8:
				case O_GSCompareGEBZ8:
				case O_LSCompareGEBZ8:
				case O_GSCompareLEBZ8:
				case O_LSCompareLEBZ8:
				case O_GSCompareGTBZ8:
				case O_LSCompareGTBZ8:
				case O_GSCompareLTBZ8:
				case O_LSCompareLTBZ8:
				case O_GSCompareEQBZ:
				case O_LSCompareEQBZ:
				case O_GSCompareNEBZ:
				case O_LSCompareNEBZ:
				case O_GSCompareGEBZ:
				case O_LSCompareGEBZ:
				case O_GSCompareLEBZ:
				case O_LSCompareLEBZ:
				case O_GSCompareGTBZ:
				case O_LSCompareGTBZ:
				case O_GSCompareLTBZ:
				case O_LSCompareLTBZ:
				case O_LocalBZ:
				case O_LocalBZ8:
				case O_GlobalBZ:
				case O_GlobalBZ8:
				{
					--diff; // these instructions are offset
					break;
				}

				case O_LLCompareLTBZ8:
				case O_LLCompareGTBZ8:
				case O_LLCompareLEBZ8:
				case O_LLCompareGEBZ8:
				case O_LLCompareEQBZ8:
				case O_LLCompareNEBZ8:
				case O_GGCompareLTBZ8:
				case O_GGCompareGTBZ8:
				case O_GGCompareLEBZ8:
				case O_GGCompareGEBZ8:
				case O_GGCompareEQBZ8:
				case O_GGCompareNEBZ8:
				case O_LLCompareLTBZ:
				case O_LLCompareGTBZ:
				case O_LLCompareLEBZ:
				case O_LLCompareGEBZ:
				case O_LLCompareEQBZ:
				case O_LLCompareNEBZ:
				case O_GGCompareLTBZ:
				case O_GGCompareGTBZ:
				case O_GGCompareLEBZ:
				case O_GGCompareGEBZ:
				case O_GGCompareEQBZ:
				case O_GGCompareNEBZ:
				{
					diff -= 2;
					break;
				}

				case O_GGNextKeyValueOrJump:
				case O_GLNextKeyValueOrJump:
				case O_LGNextKeyValueOrJump:
				case O_LLNextKeyValueOrJump:
				{
					no8version = true;
					diff -= 3;
					break;
				}

				case O_GNextValueOrJump:
				case O_LNextValueOrJump:
				{
					no8version = true;
					diff -= 2;
					break;
				}

				default:
					break;
			}

			
			if ( (diff < 128) && (diff > -129) && !no8version )
			{
				switch( o )
				{
					case O_RelativeJump: *bytecode.all.p_str(offset - 1) = O_RelativeJump8; break;
					case O_BZ: *bytecode.all.p_str(offset - 1) = O_BZ8; break;

					case O_CompareBEQ: *bytecode.all.p_str(offset - 1) = O_CompareBEQ8; break;
					case O_CompareBNE: *bytecode.all.p_str(offset - 1) = O_CompareBNE8; break;
					case O_CompareBGE: *bytecode.all.p_str(offset - 1) = O_CompareBGE8; break;
					case O_CompareBLE: *bytecode.all.p_str(offset - 1) = O_CompareBLE8; break;
					case O_CompareBGT: *bytecode.all.p_str(offset - 1) = O_CompareBGT8; break;
					case O_CompareBLT: *bytecode.all.p_str(offset - 1) = O_CompareBLT8; break;

					case O_GSCompareEQBZ: *bytecode.all.p_str(offset - 1) = O_GSCompareEQBZ8; ++offset; break;
					case O_LSCompareEQBZ: *bytecode.all.p_str(offset - 1) = O_LSCompareEQBZ8; ++offset; break;
					case O_GSCompareNEBZ: *bytecode.all.p_str(offset - 1) = O_GSCompareNEBZ8; ++offset; break;
					case O_LSCompareNEBZ: *bytecode.all.p_str(offset - 1) = O_LSCompareNEBZ8; ++offset; break;
					case O_GSCompareGEBZ: *bytecode.all.p_str(offset - 1) = O_GSCompareGEBZ8; ++offset; break;
					case O_LSCompareGEBZ: *bytecode.all.p_str(offset - 1) = O_LSCompareGEBZ8; ++offset; break;
					case O_GSCompareLEBZ: *bytecode.all.p_str(offset - 1) = O_GSCompareLEBZ8; ++offset; break;
					case O_LSCompareLEBZ: *bytecode.all.p_str(offset - 1) = O_LSCompareLEBZ8; ++offset; break;
					case O_GSCompareGTBZ: *bytecode.all.p_str(offset - 1) = O_GSCompareGTBZ8; ++offset; break;
					case O_LSCompareGTBZ: *bytecode.all.p_str(offset - 1) = O_LSCompareGTBZ8; ++offset; break;
					case O_GSCompareLTBZ: *bytecode.all.p_str(offset - 1) = O_GSCompareLTBZ8; ++offset; break;
					case O_LSCompareLTBZ: *bytecode.all.p_str(offset - 1) = O_LSCompareLTBZ8; ++offset; break;
										  
					case O_LLCompareLTBZ: *bytecode.all.p_str(offset - 1) = O_LLCompareLTBZ8; offset += 2; break;
					case O_LLCompareGTBZ: *bytecode.all.p_str(offset - 1) = O_LLCompareGTBZ8; offset += 2; break;
					case O_LLCompareLEBZ: *bytecode.all.p_str(offset - 1) = O_LLCompareLEBZ8; offset += 2; break;
					case O_LLCompareGEBZ: *bytecode.all.p_str(offset - 1) = O_LLCompareGEBZ8; offset += 2; break;
					case O_LLCompareEQBZ: *bytecode.all.p_str(offset - 1) = O_LLCompareEQBZ8; offset += 2; break;
					case O_LLCompareNEBZ: *bytecode.all.p_str(offset - 1) = O_LLCompareNEBZ8; offset += 2; break;
					case O_GGCompareLTBZ: *bytecode.all.p_str(offset - 1) = O_GGCompareLTBZ8; offset += 2; break;
					case O_GGCompareGTBZ: *bytecode.all.p_str(offset - 1) = O_GGCompareGTBZ8; offset += 2; break;
					case O_GGCompareLEBZ: *bytecode.all.p_str(offset - 1) = O_GGCompareLEBZ8; offset += 2; break;
					case O_GGCompareGEBZ: *bytecode.all.p_str(offset - 1) = O_GGCompareGEBZ8; offset += 2; break;
					case O_GGCompareEQBZ: *bytecode.all.p_str(offset - 1) = O_GGCompareEQBZ8; offset += 2; break;
					case O_GGCompareNEBZ: *bytecode.all.p_str(offset - 1) = O_GGCompareNEBZ8; offset += 2; break;

					case O_BLA: *bytecode.all.p_str(offset - 1) = O_BLA8; break;
					case O_BLO: *bytecode.all.p_str(offset - 1) = O_BLO8; break;

					case O_LocalBZ: *bytecode.all.p_str(offset - 1) = O_LocalBZ8; ++offset; break;
					case O_GlobalBZ: *bytecode.all.p_str(offset - 1) = O_GlobalBZ8; ++offset; break;

					// no work to be done
					case O_RelativeJump8:
					case O_BZ8:
					case O_BLA8:
					case O_BLO8:
					case O_CompareBLE8:
					case O_CompareBGE8:
					case O_CompareBGT8:
					case O_CompareBLT8:
					case O_CompareBEQ8:
					case O_CompareBNE8:
						break;
						
					case O_GSCompareEQBZ8:
					case O_LSCompareEQBZ8:
					case O_GSCompareNEBZ8:
					case O_LSCompareNEBZ8:
					case O_GSCompareGEBZ8:
					case O_LSCompareGEBZ8:
					case O_GSCompareLEBZ8:
					case O_LSCompareLEBZ8:
					case O_GSCompareGTBZ8:
					case O_LSCompareGTBZ8:
					case O_GSCompareLTBZ8:
					case O_LSCompareLTBZ8:
					case O_LocalBZ8:
					case O_GlobalBZ8:
						++offset;
						break;

					case O_LLCompareLTBZ8:
					case O_LLCompareGTBZ8:
					case O_LLCompareLEBZ8:
					case O_LLCompareGEBZ8:
					case O_LLCompareEQBZ8:
					case O_LLCompareNEBZ8:
					case O_GGCompareLTBZ8:
					case O_GGCompareGTBZ8:
					case O_GGCompareLEBZ8:
					case O_GGCompareGEBZ8:
					case O_GGCompareEQBZ8:
					case O_GGCompareNEBZ8:
					{
						offset += 2;
						break;
					}

					default:
						m_err = WR_ERR_compiler_panic;
						return;
				}

				*bytecode.all.p_str(offset) = (int8_t)diff;
			}
			else
			{
				if ( diff < -32768 || diff > 32767 )
				{
					// too far for 16 bits: the next pass emits this jump long
					uint32_t key = (t < bytecode.jumpOffsetTargets[j].keys.count())
									? bytecode.jumpOffsetTargets[j].keys[t]
									: c_nearJumpKey;
					if ( key == c_nearJumpKey )
					{
						m_err = WR_ERR_bad_goto_location;
						return;
					}
					needLongJump( key );
					continue;
				}
				switch( o )
				{
					// check to see if any were pushed into 16-bit land
					// that were previously optimized
					case O_RelativeJump8: *bytecode.all.p_str(offset - 1) = O_RelativeJump; break;
					case O_BZ8: *bytecode.all.p_str(offset - 1) = O_BZ; break;
					case O_CompareBEQ8: *bytecode.all.p_str(offset - 1) = O_CompareBEQ; break;
					case O_CompareBNE8: *bytecode.all.p_str(offset - 1) = O_CompareBNE; break;
					case O_CompareBGE8: *bytecode.all.p_str(offset - 1) = O_CompareBGE; break;
					case O_CompareBLE8: *bytecode.all.p_str(offset - 1) = O_CompareBLE; break;
					case O_CompareBGT8: *bytecode.all.p_str(offset - 1) = O_CompareBGT; break;
					case O_CompareBLT8: *bytecode.all.p_str(offset - 1) = O_CompareBLT; break;
					case O_GSCompareEQBZ8: *bytecode.all.p_str(offset - 1) = O_GSCompareEQBZ; ++offset; break;
					case O_LSCompareEQBZ8: *bytecode.all.p_str(offset - 1) = O_LSCompareEQBZ; ++offset; break;
					case O_GSCompareNEBZ8: *bytecode.all.p_str(offset - 1) = O_GSCompareNEBZ; ++offset; break;
					case O_LSCompareNEBZ8: *bytecode.all.p_str(offset - 1) = O_LSCompareNEBZ; ++offset; break;
					case O_GSCompareGEBZ8: *bytecode.all.p_str(offset - 1) = O_GSCompareGEBZ; ++offset; break;
					case O_LSCompareGEBZ8: *bytecode.all.p_str(offset - 1) = O_LSCompareGEBZ; ++offset; break;
					case O_GSCompareLEBZ8: *bytecode.all.p_str(offset - 1) = O_GSCompareLEBZ; ++offset; break;
					case O_LSCompareLEBZ8: *bytecode.all.p_str(offset - 1) = O_LSCompareLEBZ; ++offset; break;
					case O_GSCompareGTBZ8: *bytecode.all.p_str(offset - 1) = O_GSCompareGTBZ; ++offset; break;
					case O_LSCompareGTBZ8: *bytecode.all.p_str(offset - 1) = O_LSCompareGTBZ; ++offset; break;
					case O_GSCompareLTBZ8: *bytecode.all.p_str(offset - 1) = O_GSCompareLTBZ; ++offset; break;
					case O_LSCompareLTBZ8: *bytecode.all.p_str(offset - 1) = O_LSCompareLTBZ; ++offset; break;
					case O_LocalBZ8: *bytecode.all.p_str(offset - 1) = O_LocalBZ; ++offset; break;
					case O_GlobalBZ8: *bytecode.all.p_str(offset - 1) = O_GlobalBZ; ++offset; break;

					case O_LLCompareLTBZ8: *bytecode.all.p_str(offset - 1) = O_LLCompareLTBZ; offset += 2; break;
					case O_LLCompareGTBZ8: *bytecode.all.p_str(offset - 1) = O_LLCompareGTBZ; offset += 2; break;
					case O_LLCompareLEBZ8: *bytecode.all.p_str(offset - 1) = O_LLCompareLEBZ; offset += 2; break;
					case O_LLCompareGEBZ8: *bytecode.all.p_str(offset - 1) = O_LLCompareGEBZ; offset += 2; break;
					case O_LLCompareEQBZ8: *bytecode.all.p_str(offset - 1) = O_LLCompareEQBZ; offset += 2; break;
					case O_LLCompareNEBZ8: *bytecode.all.p_str(offset - 1) = O_LLCompareNEBZ; offset += 2; break;
					case O_GGCompareLTBZ8: *bytecode.all.p_str(offset - 1) = O_GGCompareLTBZ; offset += 2; break;
					case O_GGCompareGTBZ8: *bytecode.all.p_str(offset - 1) = O_GGCompareGTBZ; offset += 2; break;
					case O_GGCompareLEBZ8: *bytecode.all.p_str(offset - 1) = O_GGCompareLEBZ; offset += 2; break;
					case O_GGCompareGEBZ8: *bytecode.all.p_str(offset - 1) = O_GGCompareGEBZ; offset += 2; break;
					case O_GGCompareEQBZ8: *bytecode.all.p_str(offset - 1) = O_GGCompareEQBZ; offset += 2; break;
					case O_GGCompareNEBZ8: *bytecode.all.p_str(offset - 1) = O_GGCompareNEBZ; offset += 2; break;

					case O_GGNextKeyValueOrJump:
					case O_GLNextKeyValueOrJump:
					case O_LGNextKeyValueOrJump:
					case O_LLNextKeyValueOrJump:
					{
						offset += 3;
						break;
					}

					case O_GNextValueOrJump:
					case O_LNextValueOrJump:
					{
						offset += 2;
						break;
					}

					case O_BLA8: *bytecode.all.p_str(offset - 1) = O_BLA; break;
					case O_BLO8: *bytecode.all.p_str(offset - 1) = O_BLO; break;

					 // no work to be done, already visited
					case O_RelativeJump:
					case O_BZ:
					case O_BLA:
					case O_BLO:
					case O_CompareBLE:
					case O_CompareBGE:
					case O_CompareBGT:
					case O_CompareBLT:
					case O_CompareBEQ:
					case O_CompareBNE:
						break;

					case O_GSCompareEQBZ:
					case O_LSCompareEQBZ:
					case O_GSCompareNEBZ:
					case O_LSCompareNEBZ:
					case O_GSCompareGEBZ:
					case O_LSCompareGEBZ:
					case O_GSCompareLEBZ:
					case O_LSCompareLEBZ:
					case O_GSCompareGTBZ:
					case O_LSCompareGTBZ:
					case O_GSCompareLTBZ:
					case O_LSCompareLTBZ:
					case O_LocalBZ:
					case O_GlobalBZ:
						++offset;
						break;

					case O_LLCompareLTBZ:
					case O_LLCompareGTBZ:
					case O_LLCompareLEBZ:
					case O_LLCompareGEBZ:
					case O_LLCompareEQBZ:
					case O_LLCompareNEBZ:
					case O_GGCompareLTBZ:
					case O_GGCompareGTBZ:
					case O_GGCompareLEBZ:
					case O_GGCompareGEBZ:
					case O_GGCompareEQBZ:
					case O_GGCompareNEBZ:
					{
						offset += 2;
						break;
					}
						
					default:
					{
						m_err = WR_ERR_compiler_panic;
						return;
					}
				}
				
				wr_pack16( diff, bytecode.all.p_str(offset) );
			}
		}
	}
}

//------------------------------------------------------------------------------
void WRCompilationContext::pushLiteral( WRBytecode& bytecode, WRExpressionContext& context )
{
	WRValue& value = context.value;
	unsigned char data[4];
	
	if ( value.type == WR_INT && value.i == 0 )
	{
		pushOpcode( bytecode, O_LiteralZero );
	}
	else if ( value.type == WR_INT )
	{
		if ( (value.i <= 127) && (value.i >= -128) )
		{
			pushOpcode( bytecode, O_LiteralInt8 );
			unsigned char be = (char)value.i;
			pushData( bytecode, &be, 1 );
		}
		else if ( (value.i <= 32767) && (value.i >= -32768) )
		{
			pushOpcode( bytecode, O_LiteralInt16 );
			int16_t be = value.i;
			pushData( bytecode, wr_pack16(be, data), 2 );
		}
		else
		{
			pushOpcode( bytecode, O_LiteralInt32 );
			pushData( bytecode, wr_pack32(value.i, data), 4 );
		}
	}
	else if ( (uint8_t)value.type == WR_COMPILER_LITERAL_STRING )
	{
		if ( context.literalString.size() > 0xFFFF )
		{
			// the length is stored in 16 bits
			m_err = WR_ERR_string_too_long;
			return;
		}

		pushOpcode( bytecode, O_LiteralString );
		int16_t be = context.literalString.size();
		pushData( bytecode, wr_pack16(be, data), 2 );
		for( unsigned int i=0; i<context.literalString.size(); ++i )
		{
			pushData( bytecode, context.literalString.c_str(i), 1 );
		}
	}
	else
	{
		pushOpcode( bytecode, O_LiteralFloat );
		int32_t be = value.i;
		pushData( bytecode, wr_pack32(be, data), 4 );
	}
}

//------------------------------------------------------------------------------
void WRCompilationContext::pushLibConstant( WRBytecode& bytecode, WRExpressionContext& context )
{
	pushOpcode( bytecode, O_LoadLibConstant );
	unsigned char data[4];
	uint32_t hash = wr_hashStr(context.prefix + "::" + context.token);
	pushData( bytecode, wr_pack32(hash, data), 4 );
}

//------------------------------------------------------------------------------
// Returns true if 'token' is the unqualified short-name of a named-enum member
// (i.e. a constantValues entry of the form "Namespace::token" exists).
// Used to catch "RED" when the user meant "Color::RED".
bool WRCompilationContext::isNamedEnumMember( WRstr const& token )
{
	for ( int u = 0; u <= m_unitTop; ++u )
	{
		for ( unsigned int c = 0; c < m_units[u].constantValues.count(); ++c )
		{
			const WRstr& label = m_units[u].constantValues[c].label;
			// named-enum labels always contain "::" (anonymous ones never do)
			const char* sep = strstr( label.c_str(), "::" );
			if ( sep && strcmp( sep + 2, token.c_str() ) == 0 )
			{
				return true;
			}
		}
	}
	return false;
}

//------------------------------------------------------------------------------
int WRCompilationContext::addLocalSpaceLoad( WRBytecode& bytecode, WRstr& token, bool addOnly, bool varSeen, bool allowFunctionNameHashLiteral )
{
	if ( m_unitTop == 0 )
	{
		return addGlobalSpaceLoad( bytecode, token, addOnly, varSeen, allowFunctionNameHashLiteral );
	}

	uint32_t hash = wr_hashStr(token);
	
	unsigned int i=0;
		
	for( ; i<bytecode.localSpace.count(); ++i )
	{
		if ( bytecode.localSpace[i].hash == hash )
		{
			break;
		}
	}

	if ( i >= bytecode.localSpace.count() )
	{
		if ( !addOnly )
		{
			WRstr t2;
			if ( varSeen || (token[0] == ':' && token[1] == ':') )
			{
				t2 = token;
			}
			else
			{
				t2.format("::%s", token.c_str());
			}

			uint32_t ghash = wr_hashStr(t2);

			// was NOT found locally which is possible for a "global" if
			// the argument list names it, now check global with the global
			// hash
			if ( !bytecode.isStructSpace || varSeen ) // structs and explicit 'var' are immune to this
			{
				// a private global of this file first
				int priv = findPrivate( m_privateGlobals, ghash, currentFile() );
				if ( priv >= 0 )
				{
					if ( !nameInView(0, m_units[0].bytecode.localSpace[priv].hash) )
					{
						return 0;
					}
					pushOpcode( bytecode, O_LoadFromGlobal );
					unsigned char c = (unsigned char)priv;
					pushData( bytecode, &c, 1 );
					return priv;
				}

				for (unsigned int j = 0; j < m_units[0].bytecode.localSpace.count(); ++j)
				{
					if (m_units[0].bytecode.localSpace[j].hash == ghash)
					{
						if ( !nameInView(0, ghash) )
						{
							return 0;
						}
						pushOpcode(bytecode, O_LoadFromGlobal);
						unsigned char c = j;
						pushData(bytecode, &c, 1);
						return j;
					}
				}
			}
		}

			if ( !m_nonStrict && !varSeen )
			{
				if ( !addOnly && allowFunctionNameHashLiteral && !isNamedEnumMember(token) )
				{
					unsigned char data[4];
					wr_pack32( wr_hashStr(token), data );
					pushOpcode( bytecode, O_LiteralInt32 );
					pushData( bytecode, data, 4 );
					WRNameAsHash& named = m_namesAsHashes.append();
					named.name = token;
					named.pos = m_pos;
					return 0;
				}
				nameError( WR_ERR_var_not_seen_before_label );
				return 0;
			}
	}

	if ( !addOnly
		 && (varSeen ? !declareName(m_unitTop, hash) : !nameInView(m_unitTop, hash)) )
	{
		return 0;
	}

	bytecode.localSpace[i].hash = hash;
	bytecode.localSpace[i].label = token;
	
	if ( !addOnly )
	{
		pushOpcode( bytecode, O_LoadFromLocal );
		unsigned char c = i;
		pushData( bytecode, &c, 1 );
	}
	
	return i;
}

//------------------------------------------------------------------------------
int WRCompilationContext::addGlobalSpaceLoad( WRBytecode& bytecode, WRstr& token, bool addOnly, bool varSeen, bool allowFunctionNameHashLiteral )
{
	uint32_t hash;
	WRstr t2;
	if ( token[0] == ':' && token[1] == ':' )
	{
		t2 = token;
	}
	else
	{
		t2.format( "::%s", token.c_str() );
	}
	hash = wr_hash( t2, t2.size() );

	unsigned int i=0;

	// a private global of this file hides a shared one of the same name; a
	// new private one has its own hash, so it is never mistaken for that
	int file = currentFile();
	int priv = findPrivate( m_privateGlobals, hash, file );
	if ( priv >= 0 )
	{
		i = (unsigned int)priv;
		t2 = m_units[0].bytecode.localSpace[i].label;
		hash = m_units[0].bytecode.localSpace[i].hash;
	}
	else if ( varSeen && m_privateNext )
	{
		i = m_units[0].bytecode.localSpace.count();
		WRPrivateName& p = m_privateGlobals.append();
		p.file = file;
		p.hash = hash;
		p.index = (int)i;
		t2 = privateLabel( t2, file );
		hash = wr_hash( t2, t2.size() );
	}
	else
	{
		for( ; i<m_units[0].bytecode.localSpace.count(); ++i )
		{
			if ( m_units[0].bytecode.localSpace[i].hash == hash )
			{
				break;
			}
		}
	}

	if ( !m_nonStrict && !varSeen && i >= m_units[0].bytecode.localSpace.count() )
	{
		if ( !addOnly && allowFunctionNameHashLiteral && !isNamedEnumMember(token) )
		{
			unsigned char data[4];
			wr_pack32( wr_hashStr(token), data );
			pushOpcode( bytecode, O_LiteralInt32 );
			pushData( bytecode, data, 4 );
			WRNameAsHash& named = m_namesAsHashes.append();
			named.name = token;
			named.pos = m_pos;
			return 0;
		}
		nameError( WR_ERR_var_not_seen_before_label );
		return 0;
	}

	if ( !addOnly && varSeen && token[0] == ':' && token[1] == ':' )
	{
		// "var ::g" (in a block or a function) is a global of the whole
		// program, in view everywhere after it; it may be declared so more
		// than once, it names the global explicitly
		WRDeclaredName* name = findDeclaredName( 0, hash );
		if ( !name )
		{
			name = &m_declaredNames.append();
			name->unit = 0;
			name->hash = hash;
		}
		name->block = 0;
	}
	else if ( !addOnly
			  && (varSeen ? !declareName(0, hash) : !nameInView(0, hash)) )
	{
		return 0;
	}

	m_units[0].bytecode.localSpace[i].hash = hash;
	m_units[0].bytecode.localSpace[i].label = t2;

	if ( !addOnly )
	{
		pushOpcode( bytecode, O_LoadFromGlobal );
		unsigned char c = i;
		pushData( bytecode, &c, 1 );
	}

	return i;
}

//------------------------------------------------------------------------------
void WRCompilationContext::loadExpressionContext( WRExpression& expression, int depth, int operation )
{
	if ( operation > 0
		 && expression.context[operation].operation
		 && expression.context[operation].operation->opcode == O_HASH_PLACEHOLDER
		 && depth > operation )
	{
		unsigned char buf[4];
		wr_pack32( wr_hash( expression.context[depth].token,
							expression.context[depth].token.size()),
				   buf );
		pushOpcode( expression.bytecode, O_LiteralInt32 );
		pushData( expression.bytecode, buf, 4 );
	}
	else
	{
		switch( expression.context[depth].type )
		{
			case EXTYPE_LIB_CONSTANT:
			{
				pushLibConstant( expression.bytecode, expression.context[depth] );
				break;
			}
			
			case EXTYPE_LITERAL:
			{
				pushLiteral( expression.bytecode, expression.context[depth] );
				break;
			}

			case EXTYPE_LABEL_AND_NULL:
			case EXTYPE_LABEL:
			{
					if ( expression.context[depth].global )
					{
						addGlobalSpaceLoad( expression.bytecode,
											expression.context[depth].token,
											false,
											expression.context[depth].varSeen,
											expression.allowFunctionNameHashLiteral );
					}
					else
					{
						addLocalSpaceLoad( expression.bytecode,
										   expression.context[depth].token,
										   false,
										   expression.context[depth].varSeen,
										   expression.allowFunctionNameHashLiteral );
					}

				if ( expression.context[depth].type == EXTYPE_LABEL_AND_NULL
					 && !expression.context[depth].blankSeen )
				{
					expression.bytecode.all += O_InitVar;
					expression.bytecode.opcodes += O_InitVar;
				}
						
				break;
			}

			case EXTYPE_BYTECODE_RESULT:
			{
				appendBytecode( expression.bytecode,
								expression.context[depth].bytecode );
				break;
			}

			case EXTYPE_RESOLVED:
			case EXTYPE_OPERATION:
			default:
			{
				break;
			}
		}
	}

	expression.pushToStack( depth ); // make sure stack knows something got loaded on top of it
	
	expression.context[depth].type = EXTYPE_RESOLVED; // this slot is now a resolved value sitting on the stack
}

//------------------------------------------------------------------------------
void WRExpression::swapWithTop( int stackPosition, bool addOpcodes )
{
	if ( stackPosition == 0 )
	{
		return;
	}
	
	unsigned int currentTop = (unsigned int)-1;
	unsigned int swapWith = (unsigned int)-1;
	for( unsigned int i=0; i<context.count(); ++i )
	{
		if ( context[i].stackPosition == stackPosition )
		{
			swapWith = i;
		}
		
		if ( context[i].stackPosition == 0 )
		{
			currentTop = i;
		}
	}

	assert( (currentTop != (unsigned int)-1) && (swapWith != (unsigned int)-1) );

	if ( addOpcodes )
	{
		unsigned char pos = stackPosition + 1;
		WRCompilationContext::pushOpcode( bytecode, O_StackSwap );
		WRCompilationContext::pushData( bytecode, &pos, 1 );
	}
	context[currentTop].stackPosition = stackPosition;
	context[swapWith].stackPosition = 0;
}

//------------------------------------------------------------------------------
void WRCompilationContext::resolveExpression( WRExpression& expression )
{
	if ( expression.context.count() == 1 ) // single expression is trivial to resolve, load it!
	{
		loadExpressionContext( expression, 0, 0 );
		return;
	}

	unsigned int resolves = 1;
	unsigned int startedWith = expression.context.count();

	for( int p=0; p<c_highestPrecedence && expression.context.count() > 1; ++p )
	{
		// left to right operations
		for( int o=0; (unsigned int)o < expression.context.count(); ++o )
		{
			if ( (expression.context[o].type != EXTYPE_OPERATION)
				 || expression.context[o].operation->precedence > p
				 || !expression.context[o].operation->leftToRight )
			{
				continue;
			}

			resolves += resolveExpressionEx( expression, o, p );
			if (m_err)
			{
				return;
			}
			o = (unsigned int)-1;
		}

		// right to left operations
		for( int o = expression.context.count() - 1; o >= 0; --o )
		{
			if ( (expression.context[o].type != EXTYPE_OPERATION)
				 || expression.context[o].operation->precedence > p
				 || expression.context[o].operation->leftToRight )
			{
				continue;
			}

			resolves += resolveExpressionEx( expression, o, p );
			if (m_err)
			{
				return;
			}
			o = expression.context.count();
		}
	}

	if ( startedWith && (resolves != startedWith) )
	{
		m_err = WR_ERR_bad_expression;
		return;
	}
}

static bool wr_isAssignmentOpcode( const WROpcode opcode )
{
	switch( opcode )
	{
		case O_Assign:
		case O_AddAssign:
		case O_SubtractAssign:
		case O_ModAssign:
		case O_MultiplyAssign:
		case O_DivideAssign:
		case O_ORAssign:
		case O_ANDAssign:
		case O_XORAssign:
		case O_RightShiftAssign:
		case O_LeftShiftAssign:
		case O_PreIncrement:
		case O_PreDecrement:
		case O_PostIncrement:
		case O_PostDecrement:
		{
			return true;
		}

		default:
		{
			return false;
		}
	}
}

// a variable, a member or an element can be assigned; a literal, a call
// result or the result of an operation is a value, a change to it would be
// lost silently
static bool wr_isAssignable( const WRExpressionContext& target )
{
	if ( target.temporary )
	{
		return false;
	}
	return !( target.stackPosition == -1
				&& (target.type == EXTYPE_LITERAL || target.type == EXTYPE_LIB_CONSTANT) );
}

// an element step [i] whose value may become a container: its trailing index
// opcode gets the creating form (the literal forms are unfolded first)
static void wr_makeElementCreate( WRExpressionContext& element )
{
	WRBytecode& b = element.bytecode;
	if ( !b.opcodes.size() || element.createsContainer )
	{
		return;
	}

	unsigned int i = b.opcodes.size() - 1;
	unsigned int a = b.all.size() - 1;
	switch( (WROpcode)b.opcodes[i] )
	{
		case O_Index:
		{
			b.all[a] = O_IndexCreate;
			b.opcodes[i] = O_IndexCreate;
			break;
		}

		case O_IndexLiteral8:
		{
			b.all[a - 1] = O_LiteralInt8;
			b.opcodes[i] = O_LiteralInt8;
			b.all += O_IndexCreate;
			b.opcodes += O_IndexCreate;
			break;
		}

		case O_IndexLiteral16:
		{
			b.all[a - 2] = O_LiteralInt16;
			b.opcodes[i] = O_LiteralInt16;
			b.all += O_IndexCreate;
			b.opcodes += O_IndexCreate;
			break;
		}

		default:
		{
			return; // no other form is built at this point
		}
	}
	element.createsContainer = true;
}

static bool wr_isMemberStep( const WRExpressionContext& c )
{
	return c.type == EXTYPE_OPERATION && c.operation && c.operation->opcode == O_HASH_PLACEHOLDER;
}

static bool wr_isElementStep( const WRExpressionContext& c )
{
	return c.type == EXTYPE_OPERATION && c.operation && c.operation->opcode == O_Index;
}

// the member and element steps that lead to an assignment target (a.b[i] in
// "a.b[i] = v", "a.b[i]++", "++a.b[i]") may create containers; every other
// member or element access is a read, which leaves its value alone
void WRCompilationContext::markWriteChains( WRExpression& expression )
{
	WRarray<WRExpressionContext>& c = expression.context;
	int count = c.count();
	for( int o=0; o<count; ++o )
	{
		if ( c[o].type != EXTYPE_OPERATION
				|| !c[o].operation
				|| !wr_isAssignmentOpcode(c[o].operation->opcode) )
		{
			continue;
		}

		if ( c[o].operation->type == WR_OPER_PRE )
		{
			// the target follows: an operand, then members and elements
			int j = o + 2;
			for(;;)
			{
				if ( j < count && wr_isMemberStep(c[j]) )
				{
					c[j].createsContainer = true;
					j += 2;
				}
				else if ( j < count && wr_isElementStep(c[j]) )
				{
					wr_makeElementCreate( c[j] );
					++j;
				}
				else
				{
					break;
				}
			}
		}
		else
		{
			// the target precedes, walk back to its base
			int j = o - 1;
			while( j >= 0 )
			{
				if ( wr_isElementStep(c[j]) )
				{
					wr_makeElementCreate( c[j] );
					--j;
				}
				else if ( c[j].type != EXTYPE_OPERATION && j >= 2 && wr_isMemberStep(c[j - 1]) )
				{
					c[j - 1].createsContainer = true;
					j -= 2;
				}
				else
				{
					break;
				}
			}
		}
	}
}

//------------------------------------------------------------------------------
unsigned int WRCompilationContext::resolveExpressionEx( WRExpression& expression, int o, int p )
{
	unsigned int ret = 0;
	switch( expression.context[o].operation->type )
	{
		case WR_OPER_PRE:
		{
			ret = 1;

			if ( (unsigned int)(o + 1) >= expression.context.count() )
			{
				m_err = WR_ERR_bad_expression;
				return 0;
			}

			if ( wr_isAssignmentOpcode(expression.context[o].operation->opcode)
					&& !wr_isAssignable(expression.context[o + 1]) )
			{
				m_err = WR_ERR_not_assignable;
				return 0;
			}

			if ( expression.context[o + 1].stackPosition == -1 )
			{
				loadExpressionContext( expression, o + 1, 0 ); // load argument
			}
			else if ( expression.context[o + 1].stackPosition != 0 )
			{
				expression.swapWithTop( expression.context[o + 1].stackPosition );
			}

			appendBytecode( expression.bytecode, expression.context[o].bytecode );  // apply operator
			expression.context.remove( o, 1 ); // knock off operator
			expression.pushToStack(o);
			expression.context[o].temporary = true; // the result is a value
			break;
		}

		case WR_OPER_BINARY_COMMUTE:
		{
			// for operations like + and * the operands can be in any
			// order, so don't go to any great lengths to shift the stack
			// around for them

			ret = 2;

			if( o == 0 )
			{
				m_err = WR_ERR_bad_expression;
				return 0;
			}

			int second = o + 1; // push first
			int first = o - 1;  // push second
			// so
			// 1 - first
			// [oper]
			// 0 - second

			if ( expression.context[second].stackPosition == -1 )
			{
				if ( expression.context[first].stackPosition == -1 )
				{
					loadExpressionContext( expression, first, o ); 
				}
				else if ( expression.context[first].stackPosition != 0 )
				{
					// otherwise swap first to the top and load the second
					expression.swapWithTop( expression.context[first].stackPosition );
				}

				loadExpressionContext( expression, second, o );
			}
			else if ( expression.context[first].stackPosition == -1 )
			{
				if ( expression.context[second].stackPosition != 0 )
				{
					expression.swapWithTop( expression.context[second].stackPosition );
				}

				// just load the second to top
				loadExpressionContext( expression, first, o );
			}
			else if ( expression.context[second].stackPosition == 1 )
			{
				expression.swapWithTop( expression.context[first].stackPosition );
			}
			else if ( expression.context[first].stackPosition == 1 )
			{
				expression.swapWithTop( expression.context[second].stackPosition );
			}
			else
			{
				// first and second are both loaded but neither
				// is in the correct position

				int f = expression.context[first].stackPosition;
				int s = expression.context[second].stackPosition;
				int s2 = (s == 0) ? f : s; // where 'second' sits after swap(0,f)
				WRCompilationContext::pushOpcode( expression.bytecode, O_SwapTwoToTop );
				unsigned char pos = f + 1;
				WRCompilationContext::pushData( expression.bytecode, &pos, 1 );
				pos = s2 + 1;
				WRCompilationContext::pushData( expression.bytecode, &pos, 1 );

				// mirror the VM exactly: swap(0,f) then swap(1,s2)
				expression.swapWithTop( f, false );
				expression.swapPositions( 1, s2 );
			}

			appendBytecode( expression.bytecode, expression.context[o].bytecode ); // apply operator

			expression.context.remove( o - 1, 2 ); // knock off operator and arg
			expression.pushToStack(o - 1);
			expression.context[o - 1].temporary = true; // the result is a value

			break;
		}

		case WR_OPER_BINARY:
		{
			ret = 2;

			if( o == 0 )
			{
				m_err = WR_ERR_bad_expression;
				return 0;
			}

			int second = o + 1; // push first
			int first = o - 1;  // push second
			bool useAlt = false;

			if ( wr_isAssignmentOpcode(expression.context[o].operation->opcode)
					&& !wr_isAssignable(expression.context[first]) )
			{
				m_err = WR_ERR_not_assignable;
				return 0;
			}

			// a member of a variable is a variable, of a value a value; every
			// other operation yields a value
			bool resultTemporary = true;
			if ( expression.context[o].operation->opcode == O_HASH_PLACEHOLDER )
			{
				resultTemporary = !wr_isAssignable( expression.context[first] );

				// "abc".length: always a mistake, a literal has no members
				if ( expression.context[first].stackPosition == -1
						&& (expression.context[first].type == EXTYPE_LITERAL
							|| expression.context[first].type == EXTYPE_LIB_CONSTANT) )
				{
					m_err = WR_ERR_member_of_literal;
					return 0;
				}

				if ( expression.context[o].createsContainer )
				{
					// assigned to: the base goes on the stack and the creating
					// member access replaces it
					if ( expression.context[first].stackPosition == -1 )
					{
						loadExpressionContext( expression, first, o );
					}
					else if ( expression.context[first].stackPosition != 0 )
					{
						expression.swapWithTop( expression.context[first].stackPosition );
					}

					unsigned char hashData[4];
					pushOpcode( expression.bytecode, O_StackIndexHashCreate );
					pushData( expression.bytecode,
								wr_pack32(wr_hash(expression.context[second].token, expression.context[second].token.size()), hashData),
								4 );

					expression.context.remove( o - 1, 2 );
					expression.pushToStack( o - 1 );
					expression.context[o - 1].temporary = resultTemporary;
					break;
				}
			}
			// so
			// 1 - first
			// [oper]
			// 0 - second

			if ( expression.context[second].stackPosition == -1 )
			{
				if ( expression.context[first].stackPosition == -1 )
				{
					if ( expression.context[o].operation->opcode == O_HASH_PLACEHOLDER
							&& expression.context[first].type != EXTYPE_LABEL
							&& expression.context[first].type != EXTYPE_LABEL_AND_NULL )
					{
						// the value is computed (f().x, (s).x): evaluate it, then
						// literal + swap, which the optimizer turns into a stack index
						loadExpressionContext( expression, first, o );
						loadExpressionContext( expression, second, o );
						pushOpcode( expression.bytecode, O_StackSwap );
						unsigned char pos = 2;
						pushData( expression.bytecode, &pos, 1 );
					}
					else
					{
						loadExpressionContext( expression, second, o ); // nope, grab 'em
						loadExpressionContext( expression, first, o );
					}
				}
				else
				{
					// first is in the stack somewhere, we need
					// to swap it to the top, then load, then
					// swap top values
					expression.swapWithTop( expression.context[first].stackPosition );

					loadExpressionContext( expression, second, o );

					if ( expression.context[o].operation->alt == O_LAST )
					{
						expression.swapWithTop( 1 );
					}
					else // otherwise top shuffle is NOT required because we have an equal-but-opposite operation
					{
						useAlt = true;
					}
				}
			}
			else if ( expression.context[first].stackPosition == -1 )
			{
				// perfect, swap up the second, then load the
				// first

				expression.swapWithTop( expression.context[second].stackPosition );

				loadExpressionContext( expression, first, o );
			}
			else if ( expression.context[second].stackPosition == 1 )
			{
				// second is already where its supposed to be,
				// swap first to top if it's not there already
				if ( expression.context[first].stackPosition != 0 )
				{
					expression.swapWithTop( expression.context[first].stackPosition );
				}
			}
			else if ( expression.context[second].stackPosition == 0 )
			{
				// second is on top of the stack, swap with
				// next level down then swap first up


				if ( expression.context[first].stackPosition == 1 )
				{
					// lucky! a simple single-swap will put them in the
					// right order (turns out this is common)
					expression.swapWithTop( 1 );
				}
				else
				{
					// can still do it in one opcode
					
					WRCompilationContext::pushOpcode( expression.bytecode, O_SwapTwoToTop );
					unsigned char pos = expression.context[first].stackPosition + 1;
					WRCompilationContext::pushData( expression.bytecode, &pos, 1 );
					// after swap(0,f) the old top (second) sits at f; bring it to 1
					WRCompilationContext::pushData( expression.bytecode, &pos, 1 );
					
					expression.swapWithTop( 1, false );
					expression.swapWithTop( expression.context[first].stackPosition, false );
				}
			}
			else
			{
				// first and second are both loaded but neither is in the correct position

				int f = expression.context[first].stackPosition;
				int s = expression.context[second].stackPosition;
				WRCompilationContext::pushOpcode( expression.bytecode, O_SwapTwoToTop );
				unsigned char pos = f + 1;
				WRCompilationContext::pushData( expression.bytecode, &pos, 1 );
				pos = s + 1;
				WRCompilationContext::pushData( expression.bytecode, &pos, 1 );

				// mirror the VM exactly: swap(0,f) then swap(1,s)
				expression.swapWithTop( f, false );
				expression.swapPositions( 1, s );
			}

			if ( useAlt )
			{
				pushOpcode( expression.bytecode, expression.context[o].operation->alt );
			}
			else
			{
				appendBytecode( expression.bytecode, expression.context[o].bytecode ); // apply operator
			}

			expression.context.remove( o - 1, 2 ); // knock off operator and arg
			expression.pushToStack(o - 1);
			expression.context[o - 1].temporary = resultTemporary;

			break;
		}

		case WR_OPER_POST:
		{
			ret = 1;

			if( o == 0 )
			{
				m_err = WR_ERR_bad_expression;
				return 0;
			}

			WROpcode postOpcode = expression.context[o].operation->opcode;
			if ( wr_isAssignmentOpcode(postOpcode) && !wr_isAssignable(expression.context[o - 1]) )
			{
				m_err = WR_ERR_not_assignable;
				return 0;
			}

			// an element of a variable is a variable, of a value a value;
			// declarations and markers leave it alone, the rest yield values
			bool resultTemporary = true;
			if ( postOpcode == O_Index )
			{
				resultTemporary = !wr_isAssignable( expression.context[o - 1] );
			}
			else if ( postOpcode == O_InitArray || postOpcode == O_LAST )
			{
				resultTemporary = expression.context[o - 1].temporary;
			}

			if ( expression.context[o - 1].stackPosition == -1 )
			{
				loadExpressionContext( expression, o - 1, o ); // load argument
			}
			else if ( expression.context[o - 1].stackPosition != 0 )
			{
				expression.swapWithTop( expression.context[o - 1].stackPosition );
			}

			appendBytecode( expression.bytecode, expression.context[o].bytecode );
			expression.context.remove( o, 1 ); // knock off operator
			expression.pushToStack( o - 1 );
			expression.context[o - 1].temporary = resultTemporary;

			break;
		}

		case WR_OPER_TERNARY:
		{
			ret = 1;

			if ( o == 0 )
			{
				m_err = WR_ERR_bad_expression;
				return 0;
			}

			// ternary consumes the condition, branches with O_BZ, and leaves only the selected arm's value on the stack.
			if ( expression.context[o - 1].stackPosition == -1 )
			{
				loadExpressionContext( expression, o - 1, o );
			}
			else if ( expression.context[o - 1].stackPosition != 0 )
			{
				expression.swapWithTop( expression.context[o - 1].stackPosition );
			}

			int conditionFalseMarker = addRelativeJumpTarget( expression.bytecode );
			addRelativeJumpSource( expression.bytecode, O_BZ, conditionFalseMarker );

			appendBytecode( expression.bytecode, expression.context[o].bytecode );

			int conditionTrueMarker = addRelativeJumpTarget( expression.bytecode );
			addRelativeJumpSource( expression.bytecode, O_RelativeJump, conditionTrueMarker );

			setRelativeJumpTarget( expression.bytecode, conditionFalseMarker );
			appendBytecode( expression.bytecode, expression.context[o].bytecode2 );
			setRelativeJumpTarget( expression.bytecode, conditionTrueMarker );

			expression.context.remove( o, 1 );
			expression.pushToStack( o - 1 );
			// the arms decide what the result is, (c ? a : b).x = 1 writes
			// through to a or b
			expression.context[o - 1].temporary = false;

			break;
		}

		case WR_OPER_LOGICAL:
		{
			ret = 1;

			if ( o == 0 )
			{
				m_err = WR_ERR_bad_expression;
				return 0;
			}

			// the left side is on the stack, the node holds the right side;
			// the result is 1 or 0 as with the old opcodes
			if ( expression.context[o - 1].stackPosition == -1 )
			{
				loadExpressionContext( expression, o - 1, o );
			}
			else if ( expression.context[o - 1].stackPosition != 0 )
			{
				expression.swapWithTop( expression.context[o - 1].stackPosition );
			}

			unsigned char one = 1;
			int falseMarker = addRelativeJumpTarget( expression.bytecode );
			int endMarker = addRelativeJumpTarget( expression.bytecode );

			if ( expression.context[o].operation->opcode == O_LogicalAnd )
			{
				// left; BZ false; right; BZ false; 1; jump end; false: 0; end:
				addRelativeJumpSource( expression.bytecode, O_BZ, falseMarker );
				appendBytecode( expression.bytecode, expression.context[o].bytecode );
				addRelativeJumpSource( expression.bytecode, O_BZ, falseMarker );
			}
			else
			{
				// left; BZ right; 1; jump end; right: right; BZ false; 1; jump end; false: 0; end:
				int rightMarker = addRelativeJumpTarget( expression.bytecode );
				addRelativeJumpSource( expression.bytecode, O_BZ, rightMarker );
				pushOpcode( expression.bytecode, O_LiteralInt8 );
				pushData( expression.bytecode, &one, 1 );
				addRelativeJumpSource( expression.bytecode, O_RelativeJump, endMarker );

				setRelativeJumpTarget( expression.bytecode, rightMarker );
				appendBytecode( expression.bytecode, expression.context[o].bytecode );
				addRelativeJumpSource( expression.bytecode, O_BZ, falseMarker );
			}

			pushOpcode( expression.bytecode, O_LiteralInt8 );
			pushData( expression.bytecode, &one, 1 );
			addRelativeJumpSource( expression.bytecode, O_RelativeJump, endMarker );
			setRelativeJumpTarget( expression.bytecode, falseMarker );
			pushOpcode( expression.bytecode, O_LiteralZero );
			setRelativeJumpTarget( expression.bytecode, endMarker );

			expression.context.remove( o, 1 );
			expression.pushToStack( o - 1 );
			expression.context[o - 1].temporary = true; // the result is a value

			break;
		}
	}

	return ret;
}

//------------------------------------------------------------------------------
bool WRCompilationContext::operatorFound( WRstr const& token, WRarray<WRExpressionContext>& context, int depth )
{
	for( int i=0; c_operations[i].token; i++ )
	{
		if ( token == c_operations[i].token )
		{
			if ( c_operations[i].type == WR_OPER_PRE
				 && depth > 0
				 && (context[depth-1].type != EXTYPE_OPERATION 
					 || (context[depth - 1].type == EXTYPE_OPERATION &&
							(context[depth - 1].operation->type != WR_OPER_BINARY
							&& context[depth - 1].operation->type != WR_OPER_BINARY_COMMUTE
							&& context[depth - 1].operation->type != WR_OPER_PRE))) )
			{
				continue;
			}
			
			context[depth].operation = c_operations + i;
			context[depth].type = EXTYPE_OPERATION;

			// ternary stores full branch bytecode on the node and does not emit a standalone opcode here.
			if ( c_operations[i].opcode != O_LAST )
			{
				pushOpcode( context[depth].bytecode, c_operations[i].opcode );
			}

			return true;
		}
	}

	return false;
}

// how many simple arguments of a call wait for the next one (see
// parseCallFunction); more cost a Dereference each, nothing else
static const int c_waitingMax = 4;

// an argument that is one load or one literal changes nothing; "reference"
// tells a variable or a member (a reference the call reads late) from a literal
static bool wr_isSimpleArgument( WRBytecode& b, bool* reference )
{
	const unsigned int size = b.all.size();
	*reference = false;
	if ( !size || b.gotoSource.count() || b.jumpOffsetTargets.count() )
	{
		return false;
	}

	switch( (WROpcode)b.all[0] )
	{
		case O_LoadFromLocal:
		case O_LoadFromGlobal:
		{
			*reference = true;
			return size == 2;
		}

		case O_LocalIndexHash:
		case O_GlobalIndexHash:
		{
			*reference = true;
			return size == 6;
		}

		case O_LiteralZero:
		{
			return size == 1;
		}

		case O_LiteralInt8:
		{
			return size == 2;
		}

		case O_LiteralInt16:
		{
			return size == 3;
		}

		case O_LiteralInt32:
		case O_LiteralFloat:
		{
			return size == 5;
		}

		case O_LiteralString:
		{
			return size >= 3 && size == 3 + ((unsigned int)b.all[1] | ((unsigned int)b.all[2] << 8));
		}

		default:
		{
			return false;
		}
	}
}

// appends the arguments that waited, reading the references among them
// first when an argument that may change them follows
void WRCompilationContext::flushWaitingArguments( WRBytecode& bytecode, WRBytecode* waiting, const bool* reference, int& count, bool readNow )
{
	for( int i = 0; i < count; ++i )
	{
		if ( readNow && reference[i] )
		{
			waiting[i].opcodes += O_Dereference;
			waiting[i].all += O_Dereference;
		}
		appendBytecode( bytecode, waiting[i] );
		waiting[i].clear();
	}
	count = 0;
}

bool WRCompilationContext::parseCallFunction( WRExpression& expression, WRstr functionName, int depth, bool parseArguments )
{
	WRstr prefix = expression.context[depth].prefix;

	expression.context[depth].type = EXTYPE_BYTECODE_RESULT;
	expression.context[depth].temporary = true; // a returned value, not a variable
	
	unsigned char argsPushed = 0;

	if ( parseArguments )
	{
		WRstr& token2 = expression.context[depth].token;
		WRValue& value2 = expression.context[depth].value;

		WRBytecode waiting[c_waitingMax]; // simple arguments not appended yet
		bool waitingReference[c_waitingMax];
		int waitingCount = 0;

		for(;;)
		{
			if ( !getToken(expression.context[depth]) )
			{
				m_err = WR_ERR_unexpected_EOF;
				return 0;
			}

			if ( !m_quoted && token2 == ")" )
			{
				break;
			}

			if ( argsPushed == 255 ) // the count is one byte in the bytecode
			{
				m_err = WR_ERR_too_many_arguments;
				return 0;
			}
			++argsPushed;

			WRExpression nex( expression.bytecode.localSpace, expression.bytecode.isStructSpace );
			nex.allowFunctionNameHashLiteral = true;
			nex.context[0].token = token2;
			nex.context[0].value = value2;
			m_loadedToken = token2;
			m_loadedValue = value2;
			m_loadedQuoted = m_quoted;
			
			char end = parseExpression( nex );

			if ( nex.bytecode.opcodes.size() > 0 )
			{
				uint8_t op = nex.bytecode.opcodes[nex.bytecode.opcodes.size() - 1];
				if ( op == O_Index
					 || op == O_IndexSkipLoad
					 || op == O_IndexLiteral8
					 || op == O_IndexLiteral16
					 || op == O_IndexLocalLiteral8
					 || op == O_IndexGlobalLiteral8
					 || op == O_IndexLocalLiteral16
					 || op == O_IndexGlobalLiteral16 )
				{
					nex.bytecode.opcodes += O_Dereference;
					nex.bytecode.all += O_Dereference;
				}
			}

			// a variable or a member is read where the call is made: an
			// argument after it may change it (i++, an assignment, a call).
			// The simple arguments (one load or literal, they change nothing)
			// wait until such an argument comes and the ones that are
			// references are read before it, or until the last argument
			bool reference = false;
			bool simple = wr_isSimpleArgument( nex.bytecode, &reference );
			if ( !simple
				 && end == ','
				 && nex.bytecode.opcodes.size() > 0
				 && !(prefix.size() && argsPushed == 1) )
			{
				// a member of a member, or a reference a larger expression
				// ends in, followed by another argument: read now
				uint8_t op = nex.bytecode.opcodes[nex.bytecode.opcodes.size() - 1];
				if ( op == O_StackIndexHash || op == O_LoadFromLocal || op == O_LoadFromGlobal )
				{
					nex.bytecode.opcodes += O_Dereference;
					nex.bytecode.all += O_Dereference;
				}
			}

			if ( simple )
			{
				if ( waitingCount == c_waitingMax )
				{
					flushWaitingArguments( expression.context[depth].bytecode, waiting, waitingReference, waitingCount, true );
				}
				appendBytecode( waiting[waitingCount], nex.bytecode );
				// the first argument of a library function stays the variable
				// (array::clear and hash::clear replace it)
				waitingReference[waitingCount++] = reference && !(prefix.size() && argsPushed == 1);
			}
			else
			{
				flushWaitingArguments( expression.context[depth].bytecode, waiting, waitingReference, waitingCount, true );
				appendBytecode( expression.context[depth].bytecode, nex.bytecode );
			}

			if ( end == ')' )
			{
				break;
			}
			else if ( end != ',' )
			{
				// keep the error of an argument that failed to parse
				m_err = m_err ? m_err : WR_ERR_unexpected_token;
				return 0;
			}
		}

		// nothing after them changes them
		flushWaitingArguments( expression.context[depth].bytecode, waiting, waitingReference, waitingCount, false );
	}

	pushDebug( WRD_LineNumber, expression.context[depth].bytecode, getSourcePosition() );
	pushDebug( WRD_FunctionCall, expression.context[depth].bytecode, WRD_ExternalFunction );

	if ( prefix.size() )
	{
		prefix += "::";
		prefix += functionName;

		unsigned char buf[4];
		wr_pack32( wr_hashStr(prefix), buf );

		pushOpcode( expression.context[depth].bytecode, O_CallLibFunction );
		pushData( expression.context[depth].bytecode, &argsPushed, 1 ); // arg #
		pushData( expression.context[depth].bytecode, buf, 4 ); // hash id

		// normal lib will copy down the result, otherwise
		// ignore it, it will be AFTER args
	}
	else
	{
		// push the number of args
		uint32_t hash = wr_hashStr( functionName );

		unsigned int i=0;
		for( ; i<expression.context[depth].bytecode.functionSpace.count(); ++i )
		{
			if ( expression.context[depth].bytecode.functionSpace[i].hash == hash )
			{
				break;
			}
		}

		expression.context[depth].bytecode.functionSpace[i].references.append() = getBytecodePosition( expression.context[depth].bytecode );
		expression.context[depth].bytecode.functionSpace[i].files.append() = currentFile(); // a private function there is the one
		expression.context[depth].bytecode.functionSpace[i].hash = hash;

		static const uint32_t c_yieldHash = wr_hashStr("yield");
		if ( hash == c_yieldHash )
		{
			pushOpcode( expression.context[depth].bytecode, O_Yield );
			pushData( expression.context[depth].bytecode, &argsPushed, 1 );
		}
		else
		{
			pushOpcode( expression.context[depth].bytecode, O_FUNCTION_CALL_PLACEHOLDER );
			pushData( expression.context[depth].bytecode, &argsPushed, 1 );
			pushData( expression.context[depth].bytecode, "XXXX", 4 ); // TBD opcode plus index, OR hash if index was not found
		}
		
		// hash will copydown result same as lib, unless
		// copy/pop which case does nothing

		// normal call does nothing special, to preserve
		// the return value a call to copy-down must be
		// inserted
	}

	return true;
}

//------------------------------------------------------------------------------
bool WRCompilationContext::pushObjectTable( WRExpressionContext& context,
											WRarray<WRNamespaceLookup>& localSpace,
											uint32_t hash )
{
	WRstr& token2 = context.token;
	WRValue& value2 = context.value;

	bool byHash = false;
	bool byOrder = false;
	
	unsigned int i=0;
	for( ; i<context.bytecode.unitObjectSpace.count(); ++i )
	{
		if ( context.bytecode.unitObjectSpace[i].hash == hash )
		{
			break;
		}
	}

	pushOpcode( context.bytecode, O_NewObjectTable );

	context.bytecode.unitObjectSpace[i].references.append() = getBytecodePosition( context.bytecode );
	context.bytecode.unitObjectSpace[i].hash = hash;

	pushData( context.bytecode, "\0\0", 2 );

	context.type = EXTYPE_BYTECODE_RESULT;

	if ( !m_quoted && token2 == "{" )
	{
		unsigned char offset = 0;
		for(;;)
		{
			WRExpression nex( localSpace, context.bytecode.isStructSpace );
			nex.context[0].token = token2;
			nex.context[0].value = value2;

			m_parsingNew = true;

			bool v = m_nonStrict;
			m_nonStrict = true; // the member names of new S { a = 1 } are not variables
			char end = parseExpression( nex );
			m_nonStrict = v;

			m_parsingNew = false;

			if ( nex.bytecode.all.size() )
			{
				appendBytecode( context.bytecode, nex.bytecode );

				if ( m_newHashValue )
				{
					if ( byOrder )
					{
						m_err = WR_ERR_new_assign_by_label_or_offset_not_both;
						return false;
					}
					byHash = true;
					
					pushOpcode( context.bytecode, O_AssignToObjectTableByHash );
					uint8_t dat[4];
					pushData( context.bytecode, wr_pack32(m_newHashValue, dat), 4 );
				}
				else
				{
					if ( byHash )
					{
						m_err = WR_ERR_new_assign_by_label_or_offset_not_both;
						return false;
					}
					byOrder = true;

					if ( offset == 255 ) // a struct has at most 254 members
					{
						m_err = WR_ERR_too_many_locals;
						return false;
					}

					pushOpcode( context.bytecode, O_AssignToObjectTableByOffset );
					pushData( context.bytecode, &offset, 1 );
				}
			}

			++offset;

			if ( end == '}' )
			{
				break;
			}
			else if ( end != ',' )
			{
				m_err = WR_ERR_unexpected_token;
				return false;
			}
		}
	}

	return true;
}

bool WRCompilationContext::enterNesting()
{
	if ( ++m_parseDepth > WRENCH_MAX_PARSE_DEPTH )
	{
		--m_parseDepth;
		m_err = WR_ERR_nesting_too_deep;
		return false;
	}
	return true;
}

int WRCompilationContext::parseInitializer( WRExpression& expression, int depth )
{
	if ( !enterNesting() )
	{
		return -1;
	}
	int ret = parseInitializerBody( expression, depth );
	--m_parseDepth;
	return ret;
}

char WRCompilationContext::parseExpression( WRExpression& expression )
{
	if ( !enterNesting() )
	{
		return 0;
	}
	char ret = parseExpressionBody( expression );
	--m_parseDepth;
	return ret;
}

bool WRCompilationContext::parseStatement( int unitIndex, char end, WROpcode opcodeToReturn, bool newBlock )
{
	if ( !enterNesting() )
	{
		return false;
	}
	if ( newBlock )
	{
		m_openBlocks.append() = m_nextBlock++;
	}
	bool ret = parseStatementBody( unitIndex, end, opcodeToReturn );
	if ( newBlock )
	{
		m_openBlocks.pop();
	}
	--m_parseDepth;
	return ret;
}

//------------------------------------------------------------------------------
WRCompilationContext::WRDeclaredName* WRCompilationContext::findDeclaredName( int unit, uint32_t hash )
{
	for( unsigned int n=0; n<m_declaredNames.count(); ++n )
	{
		if ( m_declaredNames[n].unit == unit && m_declaredNames[n].hash == hash )
		{
			return &m_declaredNames[n];
		}
	}
	return 0;
}

//------------------------------------------------------------------------------
bool WRCompilationContext::isBlockOpen( int block )
{
	for( unsigned int b=0; b<m_openBlocks.count(); ++b )
	{
		if ( m_openBlocks[b] == block )
		{
			return true;
		}
	}
	return false;
}

//------------------------------------------------------------------------------
bool WRCompilationContext::declareName( int unit, uint32_t hash )
{
	WRDeclaredName* name = findDeclaredName( unit, hash );
	if ( !name )
	{
		name = &m_declaredNames.append();
		name->unit = unit;
		name->hash = hash;
	}
	else if ( !m_nonStrict && isBlockOpen(name->block) )
	{
		nameError( WR_ERR_variable_redeclared );
		return false;
	}

	name->block = *m_openBlocks.tail();
	return true;
}

//------------------------------------------------------------------------------
bool WRCompilationContext::nameInView( int unit, uint32_t hash )
{
	WRDeclaredName* name = m_nonStrict ? 0 : findDeclaredName( unit, hash );
	if ( name && !isBlockOpen(name->block) )
	{
		nameError( WR_ERR_variable_out_of_scope );
		return false;
	}
	return true;
}

//------------------------------------------------------------------------------
void WRCompilationContext::nameError( WRError error )
{
	if ( m_err == WR_ERR_None )
	{
		m_err = error;
		m_nameErrorPos = m_pos;
	}
}

//------------------------------------------------------------------------------
int WRCompilationContext::parseInitializerBody( WRExpression& expression, int depth )
{
	bool inHashTable = false;
	bool inArray = false;
	bool declaredArray = false;

	// start a value ... was something declared in front of us?
	if ( depth == 3
		 && expression.context[0].type == EXTYPE_LABEL
		 && expression.context[1].type == EXTYPE_OPERATION
		 && expression.context[1].operation->opcode
		 && expression.context[1].operation->opcode == O_Index )
	{
		expression.context.remove( depth - 2, 1 );
		depth -= 1;
		declaredArray = true;
	}

	WRValue& value = expression.context[depth].value;
	WRstr& token = expression.context[depth].token;

	expression.context[depth].type = EXTYPE_BYTECODE_RESULT;
	expression.context[depth].temporary = true;

	pushOpcode( expression.context[depth].bytecode, O_LiteralZero ); // push the value we're going to create

	// knock off the initial create
	uint16_t initializer = 0;
	unsigned char idat[2];
	bool afterNested = false;
	for(;;)
	{
		if ( !getToken(expression.context[depth]) )
		{
			m_err = WR_ERR_unexpected_EOF;
			return -1;
		}

		if ( afterNested )
		{
			// a nested initializer, element or value, is followed by ',' or
			// the closing '}'
			afterNested = false;
			if ( !m_quoted && token == '}' )
			{
				break;
			}
			if ( m_quoted || token != ',' )
			{
				m_err = (!m_quoted && token == ':') ? WR_ERR_hash_table_invalid_key : WR_ERR_unexpected_token;
				return -1;
			}
			continue;
		}

		bool subTableValue = false;

		if ( !m_quoted && token == '{' )
		{
			subTableValue = true;
			inArray = true;
			if ( inHashTable )
			{
				m_err = WR_ERR_array_declaration_in_hash;
				return -1;
			}

			if ( parseInitializer(expression, depth) == -1 )
			{
				return -1;
			}

			pushOpcode( expression.context[depth].bytecode, O_AssignToArrayAndPop );
			wr_pack16( initializer++, idat );
			pushData( expression.context[depth].bytecode, idat, 2 );

			afterNested = true;
			continue;
		}
	
		if ( !m_quoted && token == ',' )
		{
			if ( initializer == 0 )
			{
				m_err = WR_ERR_bad_expression;
				return -1;
			}
		}
		else
		{
			m_loadedToken = token;
			m_loadedValue = value;
			m_loadedQuoted = m_quoted;
		}

		WRExpression val( expression.bytecode.localSpace, expression.bytecode.isStructSpace );
		val.allowLabelDeclarations = false; // a key may be a variable: { k : 5 }
		val.context[0].token = token;
		val.context[0].value = value;

		char end = parseExpression( val );
		if ( end == ':' )
		{
			bool nullKey = false;
			
			// we are in a hash table!
			inHashTable = true;
			if ( inArray )
			{
				m_err = WR_ERR_hash_declaration_in_array;
				return -1;
			}

			if ( !val.bytecode.all.size() )
			{
				nullKey = true;
				pushOpcode( val.bytecode, O_LiteralZero );
			}
			
			if ( subTableValue ) // sub-table keys don't make sense
			{
				m_err = WR_ERR_hash_table_invalid_key;
				return -1;
			}

			appendBytecode( expression.context[depth].bytecode, val.bytecode );
			
			if ( !getToken(expression.context[depth]) )
			{
				m_err = WR_ERR_unexpected_EOF;
				return -1;
			}

			if ( !m_quoted && token == '{' )
			{
				if ( parseInitializer(expression, depth) == -1 )
				{
					return -1;
				}

				pushOpcode( expression.context[depth].bytecode, O_AssignToHashTableAndPop );
				afterNested = true;
				continue;
			}
			else
			{
				m_loadedToken = token;
				m_loadedValue = value;
				m_loadedQuoted = m_quoted;
				
				WRExpression key( expression.bytecode.localSpace, expression.bytecode.isStructSpace );
				key.allowLabelDeclarations = false;
				key.context[0].token = token;
				key.context[0].value = value;
				end = parseExpression( key );
				
				if ( key.bytecode.all.size() ) // nor do null keys
				{
					if ( nullKey )
					{
						m_err = WR_ERR_hash_table_invalid_key;
						return -1;
					}
					
					appendBytecode( expression.context[depth].bytecode, key.bytecode );
					pushOpcode( expression.context[depth].bytecode, O_AssignToHashTableAndPop );
				}
				else
				{
					if ( nullKey )
					{
						// null key AND value means null table, just create a zero-size table, by indexing as
						// hash, which creates the table, then removing the entry we just added, nulling it
						pushOpcode( expression.context[depth].bytecode, O_LiteralZero );
						pushOpcode( expression.context[depth].bytecode, O_AssignToHashTableAndPop );
						pushOpcode( expression.context[depth].bytecode, O_LiteralZero );
						pushOpcode( expression.context[depth].bytecode, O_Remove );
					}
					else
					{
						pushOpcode( expression.context[depth].bytecode, O_LiteralZero );
						pushOpcode( expression.context[depth].bytecode, O_AssignToHashTableAndPop );
					}
				}
			}
		}
		else
		{
			if ( val.bytecode.all.size() )
			{
				inArray = true;
				if ( inHashTable )
				{
					m_err = WR_ERR_array_declaration_in_hash;
					return -1;
				}

				appendBytecode( expression.context[depth].bytecode, val.bytecode );
				pushOpcode( expression.context[depth].bytecode, O_AssignToArrayAndPop );
				wr_pack16( initializer++, idat );
				pushData( expression.context[depth].bytecode, idat, 2 );
			}
		}
		
		if ( end == '}' )
		{
			break;
		}
		else if ( end == ',' )
		{
			continue;
		}
		else
		{
			m_err = WR_ERR_bad_expression;
			return -1;
		}
	}

	// "{ }" holds nothing that tells an array from a hash table: it is an
	// empty array after "var a[] =", an empty hash table anywhere else (as
	// "{ : }"), never the 0 it starts as, which a copy would not share
	if ( initializer == 0 && !inHashTable )
	{
		pushOpcode( expression.context[depth].bytecode, O_LiteralZero );
		if ( declaredArray )
		{
			pushOpcode( expression.context[depth].bytecode, O_InitArray );
		}
		else
		{
			pushOpcode( expression.context[depth].bytecode, O_LiteralZero );
			pushOpcode( expression.context[depth].bytecode, O_AssignToHashTableAndPop );
			pushOpcode( expression.context[depth].bytecode, O_LiteralZero );
			pushOpcode( expression.context[depth].bytecode, O_Remove );
		}
	}

	return depth;
}


//------------------------------------------------------------------------------
bool WRCompilationContext::parseUnit( bool isStruct, int parentUnitIndex )
{
	int previousIndex = m_unitTop;
	m_unitTop = m_units.count();
	WRBlock unitBlock( *this ); // the parameters, the body is a block in it

	if ( parentUnitIndex && isStruct )
	{
		m_err = WR_ERR_struct_in_struct;
		return false;
	}

	bool isGlobal;

	WRExpressionContext ex;
	WRstr& token = ex.token;
	WRstr prefix;
	bool isLibConstant;

	// get the function name; "yield" is built in, calls to it never reach a
	// user function of that name
	if ( !getToken(ex)
		 || !isValidLabel(token, isGlobal, prefix, isLibConstant)
		 || isGlobal
			|| isLibConstant
			|| token == "yield" )
	{
		m_err = WR_ERR_bad_label;
		return false;
	}

	uint32_t unitHash = wr_hash( token, token.size() );

	// "private function": kept under its name with its file, calls written
	// in that file find it first (see link)
	if ( m_privateNext )
	{
		m_privateNext = false;
		if ( isStruct )
		{
			m_err = WR_ERR_unexpected_token;
			return false;
		}
		int file = currentFile();
		WRstr label = privateLabel( token, file );
		WRPrivateName& p = m_privateFunctions.append();
		p.file = file;
		p.hash = unitHash;
		p.index = m_unitTop;
		unitHash = wr_hash( label, label.size() );
	}

	for( int u=1; u<m_unitTop; ++u )
	{
		if ( m_units[u].hash == unitHash )
		{
			// calls would silently keep going to the first definition
			m_err = WR_ERR_function_redefined;
			return false;
		}
	}

	m_units[m_unitTop].exportNamespace = m_exportNextUnit;
	m_exportNextUnit = false;
	m_units[m_unitTop].name = token;
	m_units[m_unitTop].hash = unitHash;
	m_units[m_unitTop].bytecode.isStructSpace = isStruct;

	m_units[m_unitTop].parentUnitIndex = parentUnitIndex; // if non-zero, must be called from a new'ed structure!
	
	// get the function name
	if ( getToken(ex, "(") )
	{
		for(;;)
		{
			if ( !getToken(ex) )
			{
				m_err = WR_ERR_unexpected_EOF;
				return false;
			}
			
			if ( !m_quoted && token == ")" )
			{
				break;
			}
			
			if ( !isValidLabel(token, isGlobal, prefix, isLibConstant) || isGlobal || isLibConstant )
			{
				m_err = WR_ERR_bad_label;
				return false;
			}

			uint32_t argumentHash = wr_hashStr( token );
			for( unsigned int a=0; a<m_units[m_unitTop].bytecode.localSpace.count(); ++a )
			{
				if ( m_units[m_unitTop].bytecode.localSpace[a].hash == argumentHash )
				{
					m_err = WR_ERR_duplicate_parameter;
					return false;
				}
			}

			++m_units[m_unitTop].arguments;

			// register the argument on the hash stack
			addLocalSpaceLoad( m_units[m_unitTop].bytecode,
							   token,
							   true,
							   true );
			declareName( m_unitTop, argumentHash );

			if ( !getToken(ex) )
			{
				m_err = WR_ERR_unexpected_EOF;
				return false;
			}

			if ( !m_quoted && token == ")" )
			{
				break;
			}

			if ( token != "," )
			{
				m_err = WR_ERR_unexpected_token;
				return false;
			}
		}

		if ( !getToken(ex, "{") )
		{
			m_err = WR_ERR_unexpected_token;
			return false;
		}
	}
	else if ( token != "{" )
	{
		m_err = WR_ERR_unexpected_token;
		return false;
	}

	// line markers are deduplicated per unit: the body starts its own run
	m_lastCode = 0;
	m_lastParam = -1;

	parseStatement( m_unitTop, '}', O_Return );

	if ( !m_units[m_unitTop].bytecode.opcodes.size()
		 || (m_units[m_unitTop].bytecode.opcodes[m_units[m_unitTop].bytecode.opcodes.size() - 1] != O_Return
			 && m_units[m_unitTop].bytecode.opcodes[m_units[m_unitTop].bytecode.opcodes.size() - 1] != O_ReturnZero) )
	{
		pushOpcode( m_units[m_unitTop].bytecode, O_ReturnZero );
	}

	m_unitTop = previousIndex;

	// and the enclosing unit continues with a fresh one
	m_lastCode = 0;
	m_lastParam = -1;

	return true;
}

//------------------------------------------------------------------------------
bool WRCompilationContext::parseWhile( WROpcode opcodeToReturn )
{
	WRExpressionContext ex;
	WRstr& token = ex.token;
	WRValue& value = ex.value;

	if ( !getToken(ex, "(") )
	{
		m_err = WR_ERR_unexpected_token;
		return false;
	}

	if ( !getToken(ex) )
	{
		m_err = WR_ERR_unexpected_EOF;
		return false;
	}

	WRExpression nex( m_units[m_unitTop].bytecode.localSpace, m_units[m_unitTop].bytecode.isStructSpace );
	nex.context[0].token = token;
	nex.context[0].value = value;
	m_loadedToken = token;
	m_loadedValue = value;
	m_loadedQuoted = m_quoted;

	int line = getSourcePosition();
	int markers = m_lineMarkers;
	if ( parseExpression(nex) != ')' )
	{
		m_err = m_err ? m_err : WR_ERR_unexpected_token;
		return false;
	}

	if ( !nex.bytecode.all.size() ) // "while ()" would test a value never pushed
	{
		m_err = WR_ERR_empty_parens;
		return false;
	}

	*m_continueTargets.push() = addRelativeJumpTarget( m_units[m_unitTop].bytecode );
	setRelativeJumpTarget(m_units[m_unitTop].bytecode, *m_continueTargets.tail() );

	pushLoopMarker( m_units[m_unitTop].bytecode, markers, line );
	appendBytecode( m_units[m_unitTop].bytecode, nex.bytecode );

	*m_breakTargets.push() = addRelativeJumpTarget( m_units[m_unitTop].bytecode );

	addRelativeJumpSource( m_units[m_unitTop].bytecode, O_BZ, *m_breakTargets.tail() );

	if ( !parseStatement(m_unitTop, ';', opcodeToReturn) )
	{
		return false;
	}
	
	addRelativeJumpSource( m_units[m_unitTop].bytecode, O_RelativeJump, *m_continueTargets.tail() );
	setRelativeJumpTarget( m_units[m_unitTop].bytecode, *m_breakTargets.tail() );

	m_continueTargets.pop();
	m_breakTargets.pop();
	
	resolveRelativeJumps( m_units[m_unitTop].bytecode );
	
	return true;
}

//------------------------------------------------------------------------------
bool WRCompilationContext::parseDoWhile( WROpcode opcodeToReturn )
{
	*m_continueTargets.push() = addRelativeJumpTarget( m_units[m_unitTop].bytecode );
	*m_breakTargets.push() = addRelativeJumpTarget( m_units[m_unitTop].bytecode );

	int jumpToTop = addRelativeJumpTarget( m_units[m_unitTop].bytecode );
	setRelativeJumpTarget( m_units[m_unitTop].bytecode, jumpToTop );
	
	if ( !parseStatement(m_unitTop, ';', opcodeToReturn) )
	{
		return false;
	}

	setRelativeJumpTarget(m_units[m_unitTop].bytecode, *m_continueTargets.tail() );

	WRExpressionContext ex;
	WRstr& token = ex.token;
	WRValue& value = ex.value;

	if ( !getToken(ex) )
	{
		m_err = WR_ERR_unexpected_EOF;
		return false;
	}

	if ( token != "while" )
	{
		m_err = WR_ERR_expected_while;
		return false;
	}

	if ( !getToken(ex, "(") )
	{
		m_err = WR_ERR_unexpected_token;
		return false;
	}

	WRExpression nex( m_units[m_unitTop].bytecode.localSpace, m_units[m_unitTop].bytecode.isStructSpace );
	nex.context[0].token = token;
	nex.context[0].value = value;
	//m_loadedToken = token;
	//m_loadedValue = value;

	int line = getSourcePosition();
	int markers = m_lineMarkers;
	if ( parseExpression(nex) != ')' )
	{
		m_err = m_err ? m_err : WR_ERR_unexpected_token;
		return false;
	}

	if ( !nex.bytecode.all.size() ) // "while ()" would test a value never pushed
	{
		m_err = WR_ERR_empty_parens;
		return false;
	}

	if (!getToken(ex, ";"))
	{
		m_err = WR_ERR_unexpected_token;
		return false;
	}
	setRelativeJumpTarget(m_units[m_unitTop].bytecode, *m_continueTargets.tail() );

	pushLoopMarker( m_units[m_unitTop].bytecode, markers, line );
	appendBytecode( m_units[m_unitTop].bytecode, nex.bytecode );

	addRelativeJumpSource( m_units[m_unitTop].bytecode, O_BZ, *m_breakTargets.tail() );
	addRelativeJumpSource( m_units[m_unitTop].bytecode, O_RelativeJump, jumpToTop );
	
	setRelativeJumpTarget( m_units[m_unitTop].bytecode, *m_breakTargets.tail() );

	m_continueTargets.pop();
	m_breakTargets.pop();

	resolveRelativeJumps( m_units[m_unitTop].bytecode );

	return true;
}

//------------------------------------------------------------------------------
bool WRCompilationContext::parseForLoop( WROpcode opcodeToReturn )
{
	WRExpressionContext ex;
	WRstr& token = ex.token;
	WRValue& value = ex.value;

	if ( !getToken(ex, "(") )
	{
		m_err = WR_ERR_unexpected_token;
		return false;
	}

	// create the continue and break reference points
	*m_continueTargets.push() = addRelativeJumpTarget( m_units[m_unitTop].bytecode );
	*m_breakTargets.push() = addRelativeJumpTarget( m_units[m_unitTop].bytecode );


/*
	push iterator
B:
	get next or jump A
	.
	.
	.
	goto B
A:
*/					


/*


[setup]
<- condition point
[condition]
-> false jump break
[code]
<- continue point
[post code]
-> always jump condition
<- break point

*/

	bool foreachPossible = true;
	bool foreachKV = false;
	bool foreachV = false;
	int foreachLoadI = 0;
	unsigned char foreachLoad[4];
	unsigned char g = 0;

	m_parsingFor = true;

	// [setup]
	for(;;)
	{
		if ( !getToken(ex) )
		{
			m_err = WR_ERR_unexpected_EOF;
			return false;
		}

		if ( !m_quoted && token == ";" )
		{
			break;
		}

		WRExpression nex( m_units[m_unitTop].bytecode.localSpace, m_units[m_unitTop].bytecode.isStructSpace );
		nex.context[0].token = token;
		nex.context[0].value = value;
		m_loadedToken = token;
		m_loadedValue = value;
		m_loadedQuoted = m_quoted;

		char end = parseExpression( nex );
		if ( m_err )
		{
			return false;
		}

		if ( foreachPossible )
		{
			if ( foreachLoadI < 3 )
			{
				// a single load, after any debug line markers (two of them,
				// prefix and line, past line 16383)
				unsigned int at = 0;
				while( at + 3 <= nex.bytecode.all.size() && nex.bytecode.all[at] == O_DebugInfo )
				{
					at += 3;
				}

				if ( nex.bytecode.all.size() == at + 2
						&& ((nex.bytecode.all[at] == O_LoadFromLocal) || (nex.bytecode.all[at] == O_LoadFromGlobal)) )
				{
					foreachLoad[foreachLoadI++] = nex.bytecode.all[at];
					foreachLoad[foreachLoadI++] = nex.bytecode.all[at + 1];
				}
				else
				{
					foreachPossible = false;
				}
			}
			else
			{
				foreachPossible = false;
			}
		}

		pushOpcode( nex.bytecode, O_PopOne );

		appendBytecode( m_units[m_unitTop].bytecode, nex.bytecode );
		
		if ( end == ';' )
		{
			foreachPossible = false;
			break;
		}
		else if ( end == ':' )
		{
			if ( foreachPossible )
			{
				WRExpression nex2( m_units[m_unitTop].bytecode.localSpace, m_units[m_unitTop].bytecode.isStructSpace );
				nex2.context[0].token = token;
				nex2.context[0].value = value;
				end = parseExpression( nex2 );

				// debug line markers (when the container sits on its own
				// line) may precede the single load we are looking for
				unsigned int load = 0;
				while( load + 3 <= nex2.bytecode.all.size() && nex2.bytecode.all[load] == O_DebugInfo )
				{
					load += 3;
				}

				if ( end == ')'
						&& nex2.bytecode.all.size() == load + 2
						&& (nex2.bytecode.all[load] == O_LoadFromLocal || nex2.bytecode.all[load] == O_LoadFromGlobal) )
				{

					WRstr T;
					T.format( "foreach:%d", m_foreachHash++ );
					g = (unsigned char)(addGlobalSpaceLoad(m_units[0].bytecode, T, true, true)); // #25 force "var seen" true since we are runtime adding the temporary ourselves

					for( unsigned int d=0; d<load; ++d )
					{
						m_units[m_unitTop].bytecode.all += nex2.bytecode.all[d];
					}

					if ( nex2.bytecode.all[load] == O_LoadFromLocal )
					{
						m_units[m_unitTop].bytecode.all += O_LPushIterator;
					}
					else
					{
						m_units[m_unitTop].bytecode.all += O_GPushIterator;
					}

					m_units[m_unitTop].bytecode.all += nex2.bytecode.all[load + 1];
					pushData( m_units[m_unitTop].bytecode, &g, 1 );

					if ( foreachLoadI == 4 )
					{
						foreachKV = true;
					}
					else if ( foreachLoadI == 2 )
					{
						foreachV = true;
					}
					else
					{
						m_err = WR_ERR_unexpected_token;
						return 0;
					}

					break;
				}
				else
				{
					m_err = WR_ERR_unexpected_token;
					return 0;
				}
			}
			else
			{
				m_err = WR_ERR_unexpected_token;
				return 0;
			}
		}
		else if ( end != ',' )
		{
			m_err = WR_ERR_unexpected_token;
			return 0;
		}
	}

	// <- condition point
	int conditionPoint = addRelativeJumpTarget( m_units[m_unitTop].bytecode );

	setRelativeJumpTarget( m_units[m_unitTop].bytecode, conditionPoint );

	// every iteration jumps back here: give the condition its own line
	// marker (the setup code already used this line) so stepping through a
	// loop visits the loop header each time around
	m_lastCode = 0;
	m_lastParam = -1;

	if ( foreachV || foreachKV )
	{
		if ( foreachV )
		{
			unsigned char load[2] = { foreachLoad[1], g };

			if ( foreachLoad[0] == O_LoadFromLocal )
			{
				addRelativeJumpSourceEx( m_units[m_unitTop].bytecode, O_LNextValueOrJump, *m_breakTargets.tail(), load, 2 );
			}
			else
			{
				addRelativeJumpSourceEx( m_units[m_unitTop].bytecode, O_GNextValueOrJump, *m_breakTargets.tail(), load, 2 );
			}
		}
		else
		{
			unsigned char load[3] = { foreachLoad[1], foreachLoad[3], g };
			
			if ( foreachLoad[0] == O_LoadFromLocal )
			{
				if ( foreachLoad[2] == O_LoadFromLocal )
				{
					addRelativeJumpSourceEx( m_units[m_unitTop].bytecode, O_LLNextKeyValueOrJump, *m_breakTargets.tail(), load, 3 );
				}
				else
				{
					addRelativeJumpSourceEx( m_units[m_unitTop].bytecode, O_LGNextKeyValueOrJump, *m_breakTargets.tail(), load, 3 );
				}
			}
			else
			{
				if ( foreachLoad[2] == O_LoadFromLocal )
				{
					addRelativeJumpSourceEx( m_units[m_unitTop].bytecode, O_GLNextKeyValueOrJump, *m_breakTargets.tail(), load, 3 );
				}
				else
				{
					addRelativeJumpSourceEx( m_units[m_unitTop].bytecode, O_GGNextKeyValueOrJump, *m_breakTargets.tail(), load, 3 );
				}
			}
		}
		
		m_parsingFor = false;

		// [ code ]
		if ( !parseStatement(m_unitTop, ';', opcodeToReturn) )
		{
			return false;
		}

		// <- continue point, falls into the jump back to the next element
		setRelativeJumpTarget( m_units[m_unitTop].bytecode, *m_continueTargets.tail() );
	}
	else
	{
		int line = getSourcePosition();
		int markers = m_lineMarkers;
		if ( !getToken(ex) )
		{
			m_err = WR_ERR_unexpected_EOF;
			return false;
		}

		// [ condition ]
		if ( token != ";" )
		{
			WRExpression nex( m_units[m_unitTop].bytecode.localSpace, m_units[m_unitTop].bytecode.isStructSpace );
			nex.context[0].token = token;
			nex.context[0].value = value;
			m_loadedToken = token;
			m_loadedValue = value;
			m_loadedQuoted = m_quoted;

			if ( parseExpression( nex ) != ';' )
			{
				m_err = m_err ? m_err : WR_ERR_unexpected_token;
				return false;
			}

			pushLoopMarker( m_units[m_unitTop].bytecode, markers, line );
			appendBytecode( m_units[m_unitTop].bytecode, nex.bytecode );

			// -> false jump break
			addRelativeJumpSource( m_units[m_unitTop].bytecode, O_BZ, *m_breakTargets.tail() );
		}
		else
		{
			pushLoopMarker( m_units[m_unitTop].bytecode, markers, line );
		}


		WRExpression post( m_units[m_unitTop].bytecode.localSpace, m_units[m_unitTop].bytecode.isStructSpace );

		// [ post code ]
		for(;;)
		{
			if ( !getToken(ex) )
			{
				m_err = WR_ERR_unexpected_EOF;
				return false;
			}

			if ( !m_quoted && token == ")" )
			{
				break;
			}

			WRExpression nex( m_units[m_unitTop].bytecode.localSpace, m_units[m_unitTop].bytecode.isStructSpace );
			nex.context[0].token = token;
			nex.context[0].value = value;
			m_loadedToken = token;
			m_loadedValue = value;
			m_loadedQuoted = m_quoted;

			char end = parseExpression( nex );
			pushOpcode( nex.bytecode, O_PopOne );

			appendBytecode( post.bytecode, nex.bytecode );

			if ( end == ')' )
			{
				break;
			}
			else if ( end != ',' )
			{
				m_err = WR_ERR_unexpected_token;
				return 0;
			}
		}

		m_parsingFor = false;

		// [ code ]
		if ( !parseStatement(m_unitTop, ';', opcodeToReturn) )
		{
			return false;
		}

		// <- continue point
		setRelativeJumpTarget( m_units[m_unitTop].bytecode, *m_continueTargets.tail() );

		// [post code]
		appendBytecode( m_units[m_unitTop].bytecode, post.bytecode );
	}

	addRelativeJumpSource( m_units[m_unitTop].bytecode, O_RelativeJump, conditionPoint );
	setRelativeJumpTarget( m_units[m_unitTop].bytecode, *m_breakTargets.tail() );

	m_continueTargets.pop();
	m_breakTargets.pop();

	resolveRelativeJumps( m_units[m_unitTop].bytecode );

	return true;
}

//------------------------------------------------------------------------------
bool WRCompilationContext::lookupConstantValue( WRstr& prefix, WRValue* value )
{
	for( unsigned int v=0; v<m_units[m_unitTop].constantValues.count(); ++v )
	{
		if ( m_units[m_unitTop].constantValues[v].label == prefix )
		{
			if ( value )
			{
				*value = m_units[m_unitTop].constantValues[v].value;
			}
			return true;
		}
	}

	for( unsigned int v=0; v<m_units[0].constantValues.count(); ++v )
	{
		if ( m_units[0].constantValues[v].label == prefix )
		{
			if ( value )
			{
				*value = m_units[0].constantValues[v].value;
			}
			return true;
		}
	}

	return false;
}

//------------------------------------------------------------------------------
bool WRCompilationContext::parseEnum( int unitIndex )
{
	WRExpressionContext ex;
	WRstr& token = ex.token;
	WRValue& value = ex.value;

	if ( !getToken(ex) )
	{
		m_err = WR_ERR_unexpected_token;
		return false;
	}

	WRstr enumName;
	if ( !m_quoted && token != "{" )
	{
		bool isGlobal2, isLibConst2;
		WRstr prefix2;
		if ( !isValidLabel(token, isGlobal2, prefix2, isLibConst2) || isGlobal2 || isLibConst2 )
		{
			m_err = WR_ERR_bad_label;
			return false;
		}
		enumName = token;

		if ( !getToken(ex, "{") )
		{
			m_err = WR_ERR_unexpected_token;
			return false;
		}
	}

	int index = 0;

	for(;;)
	{
		if ( !getToken(ex) )
		{
			m_err = WR_ERR_unexpected_token;
			return false;
		}

		if ( !m_quoted && token == "}" )
		{
			break;
		}
		else if ( !m_quoted && token == "," )
		{
			// a comma only separates entries: "{ , A }", "{ A,, B }"
			m_err = WR_ERR_unexpected_token;
			return false;
		}

		bool isGlobal;
		WRstr prefix;
		bool isLibConstant;
		if ( !isValidLabel(token, isGlobal, prefix, isLibConstant) || isGlobal || isLibConstant )
		{
			m_err = WR_ERR_bad_label;
			return false;
		}

		prefix = token;
		
		WRValue defaultValue;
		defaultValue.init();
		defaultValue.i = index++;

		if ( !getToken(ex) )
		{
			m_err = WR_ERR_unexpected_EOF;
			return false;
		}

		bool negative = false;

		if ( !m_quoted && token == "=" )
		{
			if ( !getToken(ex) )
			{
				m_err = WR_ERR_bad_label;
				return false;
			}

			if ( token == '-' )
			{
				negative = true;
				if ( !getToken(ex) )
				{
					m_err = WR_ERR_bad_label;
					return false;
				}
			}

			if ( value.type == WR_REF )
			{
				m_err = WR_ERR_bad_label;
				return false;
			}
		}
		else
		{
			m_loadedToken = token;
			m_loadedValue = value;
			m_loadedQuoted = m_quoted;

			value = defaultValue;
		}

		if ( value.type == WR_INT )
		{
			if ( negative )
			{
				value.i = -value.i;
			}
			
			index = value.i + 1;
		}
		else if (value.type == WR_FLOAT)
		{
			if ( negative )
			{
				value.f = -value.f;
			}
		}
		else
		{
			m_err = WR_ERR_bad_label;
			return false;
		}
		
		WRstr checkLabel = enumName.size() ? (enumName + "::" + prefix) : prefix;
		if ( lookupConstantValue(checkLabel) )
		{
			m_err = WR_ERR_constant_redefined;
			return false;
		}

		ConstantValue& newVal = m_units[m_unitTop].constantValues.append();
		newVal.label = checkLabel;
		newVal.value = value;

		// the entry ends with a comma (also after the last one) or the brace
		if ( !getToken(ex) )
		{
			m_err = WR_ERR_unexpected_EOF;
			return false;
		}

		if ( !m_quoted && token == "}" )
		{
			break;
		}
		else if ( m_quoted || token != "," )
		{
			m_err = WR_ERR_unexpected_token;
			return false;
		}
	}

	if ( getToken(ex) && token != ";" )
	{
		m_loadedToken = token;
		m_loadedValue = value;
		m_loadedQuoted = m_quoted;
	}

	return true;
}

//------------------------------------------------------------------------------
uint32_t WRCompilationContext::getSingleValueHash( const char* end )
{
	WRExpressionContext ex;
	WRstr& token = ex.token;
	WRValue& value = ex.value;
	getToken( ex );

	// "case (5):" any number of parentheses around the value
	int parens = 0;
	while ( !m_quoted && token == "(" )
	{
		++parens;
		if ( !getToken(ex) )
		{
			m_err = WR_ERR_unexpected_EOF;
			return 0;
		}
	}

	// "case -1:" arrives as a separate minus token
	bool negate = false;
	if ( !m_quoted && token == "-" )
	{
		negate = true;
		getToken( ex );
	}

	if ( value.type == WR_REF )
	{
		m_err = WR_ERR_switch_bad_case_hash;
		return 0;
	}

	if ( negate )
	{
		if ( value.type == WR_INT )
		{
			value.i = (int32_t)(0u - (uint32_t)value.i);
		}
		else if ( value.type == WR_FLOAT )
		{
			value.f = -value.f;
		}
		else
		{
			m_err = WR_ERR_switch_bad_case_hash;
			return 0;
		}
	}
	
	uint32_t hash = ((uint8_t)value.type == WR_COMPILER_LITERAL_STRING) ? wr_hashStr(token) : value.getHash();

	for( ; parens > 0; --parens )
	{
		if ( !getToken(ex, ")") )
		{
			m_err = WR_ERR_unexpected_token;
			return 0;
		}
	}

	if ( !getToken(ex, end) )
	{
		m_err = WR_ERR_unexpected_token;
		return 0;
	}

	return hash;
}

/*


    continue target:
    targetswitchLinear
    8-bit max
pc> 16-bit default location
    16 bit case offset
    16 bit case offset
    16 bit case offset
    cases...
    [cases]
    break target:


continue target:
switch ins
16-bit mod
16-bit default location
32 hash case mod 0 : 16 bit case offset
32 hash case mod 1 : 16 bit case offset
32 hash case mod 2 : 16 bit case offset
...
[cases]
break target:

*/

//------------------------------------------------------------------------------
struct WRSwitchCase
{
	uint32_t hash; // hash to get this case
	bool occupied; // null hash is legal and common, must mark occupation of a node with extra flag
	bool defaultCase;
	int jumpOffset; // where to go to get to this case
};

// the 16-bit vector from the switch table (at "base") to a case; a case
// farther away is reached through a long jump placed after the table (at
// tableEnd, c_longJumpSize bytes each), shared by all vectors to the same case
static int wr_switchVector( int target, int base, int tableEnd, WRarray<int>& trampolines )
{
	int vector = target - base;
	if ( vector >= -32768 && vector <= 32767 )
	{
		return vector;
	}

	unsigned int t = 0;
	for( ; t<trampolines.count(); ++t )
	{
		if ( trampolines[t] == target )
		{
			break;
		}
	}
	if ( t == trampolines.count() )
	{
		trampolines.append() = target;
	}
	return (tableEnd + c_longJumpSize*(int)t) - base;
}

static void wr_pushSwitchTrampolines( WRBytecode& bytecode, WRarray<int>& trampolines )
{
	unsigned char data[c_longJumpSize];
	for( unsigned int t=0; t<trampolines.count(); ++t )
	{
		int at = bytecode.all.size();
		data[0] = O_Extended;
		data[1] = OX_RelativeJump32;
		wr_pack32( (uint32_t)(trampolines[t] - (at + 2)), data + 2 ); // relative to the vector
		bytecode.all.append( data, c_longJumpSize );
	}
}

//------------------------------------------------------------------------------
bool WRCompilationContext::parseSwitch( WROpcode opcodeToReturn )
{
	WRExpressionContext ex;
	WRstr& token = ex.token;
	WRValue& value = ex.value;

	if ( !getToken(ex, "(") )
	{
		m_err = WR_ERR_unexpected_token;
		return false;
	}
	
	WRExpression selectionCriteria( m_units[m_unitTop].bytecode.localSpace, m_units[m_unitTop].bytecode.isStructSpace );
	selectionCriteria.context[0].token = token;
	selectionCriteria.context[0].value = value;
	
	if ( parseExpression(selectionCriteria) != ')' )
	{
		m_err = m_err ? m_err : WR_ERR_unexpected_token;
		return false;
	}

	if ( !selectionCriteria.bytecode.all.size() ) // "switch ()" would test a value never pushed
	{
		m_err = WR_ERR_empty_parens;
		return false;
	}

	appendBytecode( m_units[m_unitTop].bytecode, selectionCriteria.bytecode );


	if ( !getToken(ex, "{") )
	{
		m_err = WR_ERR_unexpected_token;
		return false;
	}

	WRarray<WRSwitchCase> cases;
	int defaultOffset = -1;
	WRSwitchCase* swCase = 0; // current case

	int defaultCaseJumpTarget = addRelativeJumpTarget( m_units[m_unitTop].bytecode );

	*m_breakTargets.push() = addRelativeJumpTarget( m_units[m_unitTop].bytecode );

	int selectionLogicPoint = addRelativeJumpTarget( m_units[m_unitTop].bytecode );
	addRelativeJumpSource( m_units[m_unitTop].bytecode, O_RelativeJump, selectionLogicPoint );

	unsigned int startingBytecodeMarker = m_units[m_unitTop].bytecode.all.size();
	
	for(;;)
	{
		if ( !getToken(ex) )
		{
			m_err = WR_ERR_unexpected_EOF;
			return false;
		}

		if( !m_quoted && token == "}" )
		{
			break;
		}

		if ( !m_quoted && token == "case" )
		{
			swCase = &cases.append();
			swCase->jumpOffset = m_units[m_unitTop].bytecode.all.size();
			m_units[m_unitTop].bytecode.opcodes.clear(); // case label is a jump target
			m_units[m_unitTop].lastStatementOverwriteStore.valid = false;
			swCase->hash = getSingleValueHash(":");
			if ( m_err )
			{
				return false;
			}

			swCase->occupied = true;
			swCase->defaultCase = false;

			if ( cases.count() > 1 )
			{
				for( unsigned int h = 0; h < cases.count() - 1 ; ++h )
				{
					if ( cases[h].occupied && !cases[h].defaultCase && (swCase->hash == cases[h].hash) )
					{
						m_err = WR_ERR_switch_duplicate_case;
						return false;
					}
				}
			}
		}
		else if ( !m_quoted && token == "default" )
		{
			if ( defaultOffset != -1 )
			{
				m_err = WR_ERR_switch_duplicate_case;
				return false;
			}

			if ( !getToken(ex, ":") )
			{
				m_err = WR_ERR_unexpected_token;
				return false;
			}

			defaultOffset = m_units[m_unitTop].bytecode.all.size();
			setRelativeJumpTarget( m_units[m_unitTop].bytecode, defaultCaseJumpTarget );
			m_units[m_unitTop].lastStatementOverwriteStore.valid = false;
		}
		else
		{
			if ( swCase == 0 && defaultOffset == -1 )
			{
				m_err = WR_ERR_switch_case_or_default_expected;
				return false;
			}

			m_loadedToken = token;
			m_loadedValue = value;
			m_loadedQuoted = m_quoted;

			// the cases share the block of the switch
			if ( !parseStatement(m_unitTop, ';', opcodeToReturn, false) )
			{
				return false;
			}
		}
	}

	if ( startingBytecodeMarker == m_units[m_unitTop].bytecode.all.size() )
	{
		// no code was added so this is one big null operation, go
		// ahead and null it
		m_units[m_unitTop].bytecode.all.shave(3);
		m_units[m_unitTop].bytecode.opcodes.clear();
			
		pushOpcode(m_units[m_unitTop].bytecode, O_PopOne); // pop off the selection criteria

		m_units[m_unitTop].bytecode.jumpOffsetTargets.pop();
		m_breakTargets.pop();
		return true;
	}
	
	// make sure the last instruction is a break (jump) so the
	// selection logic is skipped at the end of the last case/default
	WRBytecode& switchCode = m_units[m_unitTop].bytecode;
	BytecodeJumpOffset& breakTarget = switchCode.jumpOffsetTargets[*m_breakTargets.tail()];
	int lastBreak = breakTarget.references.count() ? breakTarget.references[breakTarget.references.count() - 1] : -1;
	int codeEnd = (int)switchCode.all.size();
	bool endsWithBreak = lastBreak == codeEnd - 2
							|| (lastBreak == codeEnd - 4 && wr_isLongJumpVector(switchCode.all, lastBreak));

	// a break that ends the code only counts if nothing jumps past it to the
	// end, like the false branch of "if ( x ) { break; }" does
	for( unsigned int t=0; endsWithBreak && t<switchCode.jumpOffsetTargets.count(); ++t )
	{
		if ( switchCode.jumpOffsetTargets[t].offset == (int)switchCode.all.size() )
		{
			endsWithBreak = false;
		}
	}

	if ( !endsWithBreak )
	{
		addRelativeJumpSource( m_units[m_unitTop].bytecode, O_RelativeJump, *m_breakTargets.tail() );
	}

	// selection logic jumps HERE
	setRelativeJumpTarget( m_units[m_unitTop].bytecode, selectionLogicPoint );

	// find the highest hash value, and size an array to that
	unsigned int size = 0;
	for( unsigned int d=0; d<cases.count(); ++d )
	{
		if ( cases[d].defaultCase )
		{
			continue;
		}

		if ( cases[d].hash > size )
		{
			size = cases[d].hash;
		}

		if ( size >= 254 )
		{
			// clamp: a negative case hashes to 0xFFFFFFFF and the ++ below
			// would wrap it to an empty linear table
			size = 254;
			break;
		}
	}

	// first try the easy way

	++size;

	WRSwitchCase* table = 0;
	unsigned char packbuf[4];
	WRarray<int> trampolines; // far cases, see wr_switchVector
	int tableBase = 0;

	if ( size < 254 ) // cases are labeled 0-254, just use a linear jump table
	{
		pushOpcode( m_units[m_unitTop].bytecode, O_SwitchLinear );

		packbuf[0] = size;
		pushData( m_units[m_unitTop].bytecode, packbuf, 1 ); // size

		int currentPos = m_units[m_unitTop].bytecode.all.size();
		tableBase = currentPos;
		int tableEnd = currentPos + 2 + size*2;

		table = (WRSwitchCase *)g_malloc(size * sizeof(WRSwitchCase));
		memset( table, 0, size*sizeof(WRSwitchCase) );

		for( unsigned int i = 0; i<size; ++i ) // for each of the possible entries..
		{
			for( unsigned int hash = 0; hash<cases.count(); ++hash ) // if a hash matches it, populate that table entry
			{
				if ( cases[hash].occupied && !cases[hash].defaultCase && (cases[hash].hash == i) )
				{
					table[cases[hash].hash].jumpOffset = wr_switchVector( cases[hash].jumpOffset, currentPos, tableEnd, trampolines );
					table[cases[hash].hash].occupied = true;
					break;
				}
			}
		}

		// without a default the switch continues after the table and its
		// trampolines
		if ( defaultOffset == -1 )
		{
			defaultOffset = (tableEnd + c_longJumpSize*(int)trampolines.count()) - currentPos;
		}
		else
		{
			defaultOffset = wr_switchVector( defaultOffset, currentPos, tableEnd, trampolines );
		}

		pushData( m_units[m_unitTop].bytecode, wr_pack16(defaultOffset, packbuf), 2 );

		for( unsigned int i=0; i<size; ++i )
		{
			if ( table[i].occupied )
			{
				pushData( m_units[m_unitTop].bytecode, wr_pack16(table[i].jumpOffset, packbuf), 2 );
			}
			else
			{
				pushData( m_units[m_unitTop].bytecode, wr_pack16(defaultOffset, packbuf), 2 );
			}
		}
	}
	else
	{
		pushOpcode( m_units[m_unitTop].bytecode, O_Switch ); // add switch command

		int currentPos = m_units[m_unitTop].bytecode.all.size();
		tableBase = currentPos;

		// find a suitable mod
		uint16_t mod = 1;
		for( ; mod<0x7FFE; ++mod )
		{
			table = (WRSwitchCase *)g_malloc(mod * sizeof(WRSwitchCase));
			memset( table, 0, sizeof(WRSwitchCase)*mod );

			unsigned int c=0;
			for( ; c<cases.count(); ++c )
			{
				if ( cases[c].defaultCase )
				{
					continue;
				}

				if ( table[cases[c].hash % mod].occupied )
				{
					break;
				}

				table[cases[c].hash % mod].hash = cases[c].hash;
				table[cases[c].hash % mod].jumpOffset = cases[c].jumpOffset; // made a vector below
				table[cases[c].hash % mod].occupied = true;
			}

			if ( c >= cases.count() )
			{
				break;
			}
			else
			{
				g_free( table );
				table = 0;
			} 
		}

		if ( mod >= 0x7FFE )
		{
			m_err = WR_ERR_switch_construction_error;
			return false;
		}

		int tableEnd = currentPos + 4 + mod*6;
		for( uint16_t m = 0; m<mod; ++m )
		{
			if ( table[m].occupied )
			{
				table[m].jumpOffset = wr_switchVector( table[m].jumpOffset, currentPos, tableEnd, trampolines );
			}
		}

		// without a default the switch continues after the table and its
		// trampolines
		if ( defaultOffset == -1 )
		{
			defaultOffset = (tableEnd + c_longJumpSize*(int)trampolines.count()) - currentPos;
		}
		else
		{
			defaultOffset = wr_switchVector( defaultOffset, currentPos, tableEnd, trampolines );
		}

		pushData( m_units[m_unitTop].bytecode, wr_pack16(mod, packbuf), 2 ); // mod value
		pushData( m_units[m_unitTop].bytecode, wr_pack16(defaultOffset, packbuf), 2 ); // default offset

		for( uint16_t m = 0; m<mod; ++m )
		{
			pushData( m_units[m_unitTop].bytecode, wr_pack32(table[m].hash, packbuf), 4 );

			if ( !table[m].occupied )
			{
				pushData( m_units[m_unitTop].bytecode, wr_pack16(defaultOffset, packbuf), 2 );
			}
			else
			{
				pushData( m_units[m_unitTop].bytecode, wr_pack16(table[m].jumpOffset, packbuf), 2 );
			}
		}
	}

	g_free( table );

	wr_pushSwitchTrampolines( m_units[m_unitTop].bytecode, trampolines );

	// every vector points into the table or the trampolines after it
	if ( (int)m_units[m_unitTop].bytecode.all.size() - tableBase > 32767 )
	{
		m_err = WR_ERR_switch_construction_error;
		return false;
	}

	setRelativeJumpTarget( m_units[m_unitTop].bytecode, *m_breakTargets.tail() );

	resolveRelativeJumps( m_units[m_unitTop].bytecode );

	m_breakTargets.pop();

	return true;
}

//------------------------------------------------------------------------------
bool WRCompilationContext::parseIf( WROpcode opcodeToReturn )
{
	WRExpressionContext ex;
	WRstr& token = ex.token;
	WRValue& value = ex.value;

	if ( !getToken(ex, "(") )
	{
		m_err = WR_ERR_unexpected_token;
		return false;
	}

	if ( !getToken(ex) )
	{
		m_err = WR_ERR_unexpected_EOF;
		return false;
	}

	WRExpression nex( m_units[m_unitTop].bytecode.localSpace, m_units[m_unitTop].bytecode.isStructSpace );
	nex.context[0].token = token;
	nex.context[0].value = value;
	m_loadedToken = token;
	m_loadedValue = value;
	m_loadedQuoted = m_quoted;

	if ( parseExpression(nex) != ')' || m_err )
	{
		m_err = m_err ? m_err : WR_ERR_unexpected_token;
		return false;
	}

	if ( !nex.bytecode.all.size() ) // "if ()" would test a value never pushed
	{
		m_err = WR_ERR_empty_parens;
		return false;
	}

	appendBytecode( m_units[m_unitTop].bytecode, nex.bytecode );

	int conditionFalseMarker = addRelativeJumpTarget( m_units[m_unitTop].bytecode );
	
	addRelativeJumpSource( m_units[m_unitTop].bytecode, O_BZ, conditionFalseMarker );

	if ( !parseStatement(m_unitTop, ';', opcodeToReturn) )
	{
		return false;
	}

 	if ( !getToken(ex) )
	{
		setRelativeJumpTarget(m_units[m_unitTop].bytecode, conditionFalseMarker);
	}
	else if ( !m_quoted && token == "else" )
	{
		int conditionTrueMarker = addRelativeJumpTarget( m_units[m_unitTop].bytecode ); // when it hits here it will jump OVER this section

		addRelativeJumpSource( m_units[m_unitTop].bytecode, O_RelativeJump, conditionTrueMarker );
		
		setRelativeJumpTarget( m_units[m_unitTop].bytecode, conditionFalseMarker );

		if ( !parseStatement(m_unitTop, ';', opcodeToReturn) )
		{
			return false;
		}
		
		setRelativeJumpTarget( m_units[m_unitTop].bytecode, conditionTrueMarker );
	}
	else
	{
		m_loadedToken = token;
		m_loadedValue = value;
		m_loadedQuoted = m_quoted;
		setRelativeJumpTarget( m_units[m_unitTop].bytecode, conditionFalseMarker );
	}

	resolveRelativeJumps( m_units[m_unitTop].bytecode ); // at least do the ones we added

	return true;
}

//------------------------------------------------------------------------------
bool WRCompilationContext::parseStatementBody( int unitIndex, char end, WROpcode opcodeToReturn )
{
	WRExpressionContext ex;
	bool varSeen = false;
	m_exportNextUnit = false;

	for(;;)
	{
		WRstr& token = ex.token;
//		WRValue& value = ex.value;

		if ( !getToken(ex) ) // if we run out of tokens that's fine as long as we were not waiting for a }
		{
			if ( end == '}' )
			{
				m_err = WR_ERR_unexpected_EOF;
				return false;
			}

			break;
		}

		if ( !m_quoted && token[0] == end )
		{
			break;
		}

		if ( m_exportNextUnit
			 && token != "function"
			 && token != "struct"
			 && token != "unit" )
		{
			m_err = WR_ERR_unexpected_export_keyword;
			break;
		}

		// "private" applies to the one statement after it
		if ( m_privateArmed )
		{
			m_privateArmed = false;
		}
		else
		{
			m_privateNext = false;
		}

		// "private var ..." / "private function ..." at the top level makes
		// the names the file's own; anywhere else private is a plain name
		if ( !m_quoted && token == "private" && unitIndex == 0 && m_parseDepth == 1 )
		{
			if ( !getToken(ex) )
			{
				m_err = WR_ERR_unexpected_EOF;
				return false;
			}
			if ( m_quoted || (token != "var" && token != "function") )
			{
				m_err = WR_ERR_unexpected_token;
				return false;
			}
			m_loadedToken = token;
			m_loadedValue = ex.value;
			m_loadedQuoted = m_quoted;
			m_privateNext = true;
			m_privateArmed = true;
			continue;
		}
		
		if ( !m_quoted && token == "{" )
		{
			m_units[unitIndex].lastStatementOverwriteStore.valid = false;
			if ( !parseStatement(unitIndex, '}', opcodeToReturn) )
			{
				return false;
			}
			if ( end == ';' ) // the block was the single statement
			{
				return true;
			}
			continue; // a nested block, the enclosing one goes on
		}

		if ( !m_quoted && token == "return" )
		{
			m_units[unitIndex].lastStatementOverwriteStore.valid = false;
			if ( !getToken(ex) )
			{
				m_err = WR_ERR_unexpected_EOF;
				return false;
			}

			if ( !m_quoted && token == ";" ) // special case of a null return, add the null
			{
				pushOpcode( m_units[unitIndex].bytecode, O_LiteralZero );
			}
			else
			{
				WRExpression nex( m_units[unitIndex].bytecode.localSpace, m_units[unitIndex].bytecode.isStructSpace );
				nex.context[0].token = token;
				nex.context[0].value = ex.value;
				m_loadedToken = token;
				m_loadedValue = ex.value;
				m_loadedQuoted = m_quoted;

				if ( parseExpression( nex ) != ';')
				{
					m_err = m_err ? m_err : WR_ERR_unexpected_token;
					return false;
				}

				appendBytecode( m_units[unitIndex].bytecode, nex.bytecode );
			}

			pushDebug( WRD_LineNumber, m_units[m_unitTop].bytecode, getSourcePosition() );
			pushOpcode( m_units[unitIndex].bytecode, opcodeToReturn );
			if ( opcodeToReturn == O_GlobalStop )
			{
				// the byte after GlobalStop is where later calls into the
				// context return to: it must be a Stop, also for a return
				// in the middle of the global code
				pushOpcode( m_units[unitIndex].bytecode, O_Stop );
			}
		}
		else if ( !m_quoted && token == "struct" )
		{
			m_units[unitIndex].lastStatementOverwriteStore.valid = false;
			m_exportNextUnit = true; // always export structs
			
			if ( unitIndex != 0 )
			{
				m_err = WR_ERR_statement_expected;
				return false;
			}

			if ( !parseUnit(true, unitIndex) )
			{
				return false;
			}

			m_units[unitIndex].lastStatementOverwriteStore.valid = false;
		}
			else if ( !m_quoted && (token == "function" || token == "unit") )
			{
				m_units[unitIndex].lastStatementOverwriteStore.valid = false;
				if ( unitIndex != 0 )
				{
					m_err = WR_ERR_statement_expected;
				return false;
			}
			
				if ( !parseUnit(false, unitIndex) )
				{
					return false;
				}

				m_units[unitIndex].lastStatementOverwriteStore.valid = false;
			}
		else if ( !m_quoted && token == "if" )
		{
			m_units[unitIndex].lastStatementOverwriteStore.valid = false;
			if ( !parseIf(opcodeToReturn) )
			{
				return false;
			}

			m_units[unitIndex].lastStatementOverwriteStore.valid = false;
		}
		else if ( !m_quoted && token == "while" )
		{
			m_units[unitIndex].lastStatementOverwriteStore.valid = false;
			if ( !parseWhile(opcodeToReturn) )
			{
				return false;
			}

			m_units[unitIndex].lastStatementOverwriteStore.valid = false;
		}
		else if ( !m_quoted && token == "for" )
		{
			m_units[unitIndex].lastStatementOverwriteStore.valid = false;
			WRBlock forBlock( *this ); // a var in the header belongs to the loop
			if ( !parseForLoop(opcodeToReturn) )
			{
				return false;
			}

			m_units[unitIndex].lastStatementOverwriteStore.valid = false;
		}
		else if ( !m_quoted && token == "enum" )
		{
			m_units[unitIndex].lastStatementOverwriteStore.valid = false;
			if ( !parseEnum(unitIndex) )
			{
				return false;
			}

			m_units[unitIndex].lastStatementOverwriteStore.valid = false;
		}
		else if ( !m_quoted && token == "export" )
		{
			m_exportNextUnit = true;
			continue;
		}
		else if ( !m_quoted && token == "switch" )
		{
			m_units[unitIndex].lastStatementOverwriteStore.valid = false;
			WRBlock switchBlock( *this );
			if ( !parseSwitch(opcodeToReturn) )
			{
				return false;
			}

			m_units[unitIndex].lastStatementOverwriteStore.valid = false;
		}
		else if ( !m_quoted && token == "do" )
		{
			m_units[unitIndex].lastStatementOverwriteStore.valid = false;
			if ( !parseDoWhile(opcodeToReturn) )
			{
				return false;
			}

			m_units[unitIndex].lastStatementOverwriteStore.valid = false;
		}
		else if ( !m_quoted && token == "break" )
		{
			m_units[unitIndex].lastStatementOverwriteStore.valid = false;
			if ( !m_breakTargets.count() )
			{
				m_err = WR_ERR_break_keyword_not_in_looping_structure;
				return false;
			}

			addRelativeJumpSource( m_units[unitIndex].bytecode, O_RelativeJump, *m_breakTargets.tail() );
		}
		else if ( !m_quoted && token == "continue" )
		{
			m_units[unitIndex].lastStatementOverwriteStore.valid = false;
			if ( !m_continueTargets.count() )
			{
				m_err = WR_ERR_continue_keyword_not_in_looping_structure;
				return false;
			}

			addRelativeJumpSource( m_units[unitIndex].bytecode, O_RelativeJump, *m_continueTargets.tail() );
		}
		else if ( !m_quoted && token == "goto" )
		{
			m_units[unitIndex].lastStatementOverwriteStore.valid = false;
			if ( !getToken(ex) ) // if we run out of tokens that's fine as long as we were not waiting for a }
			{
				m_err = WR_ERR_unexpected_EOF;
				return false;
			}

			bool isGlobal;
			WRstr prefix;
			bool isLibConstant;
			if ( !isValidLabel(token, isGlobal, prefix, isLibConstant) || isGlobal || isLibConstant )
			{
				m_err = WR_ERR_bad_goto_label;
				return false;
			}

			GotoSource& G = m_units[unitIndex].bytecode.gotoSource.append();
			G.hash = wr_hashStr( token );
			G.offset = m_units[unitIndex].bytecode.all.size();
			G.key = nextJumpKey();
			G.isLong = isLongJump( G.key ); // link() fills in the jump
			pushData( m_units[unitIndex].bytecode, "\0\0\0\0\0\0", G.isLong ? c_longJumpSize : 3 );

			if ( !getToken(ex, ";"))
			{
				m_err = WR_ERR_unexpected_token;
				return false;
			}
		}
		else
		{
			WRExpression nex( m_units[unitIndex].bytecode.localSpace, m_units[unitIndex].bytecode.isStructSpace );
			nex.context[0].varSeen = varSeen;
			nex.context[0].token = token;
			nex.context[0].value = ex.value;
			nex.lValue = true;
			m_loadedToken = token;
			m_loadedValue = ex.value;
			m_loadedQuoted = m_quoted;
			if ( parseExpression(nex) != ';' )
			{
				if ( !m_err )
				{
					m_err = WR_ERR_unexpected_token;
				}
				return false;
			}
			if ( m_err ) // a name error, reported on its own line, not after the next token
			{
				return false;
			}

			if ( nex.bytecode.all.size() == 0 && nex.context.count() == 0 )
			{
				// empty statement ";" -- nothing was pushed so nothing may be popped
				m_units[unitIndex].lastStatementOverwriteStore.valid = false;
			}
			else
			{
				FinalizeStatementBytecode( m_units[unitIndex], nex.bytecode );
			}

			if ( m_memberShadows.count() )
			{
				applyMemberShadows( m_units[unitIndex].bytecode );
			}
		}

		if ( end == ';' ) // single statement
		{
			break;
		}
	}

	return m_err ? false : true;
}

//------------------------------------------------------------------------------
bool WRCompilationContext::checkAsComment( char lead )
{
	char c;
	if ( lead == '/' )
	{
		if ( !getChar(c) )
		{
			m_err = WR_ERR_unexpected_EOF;
			return false;
		}

		if ( c == '/' )
		{
			while( getChar(c) && c != '\n' );
			return true;
		}
		else if ( c == '*' )
		{
			for(;;)
			{
				if ( !getChar(c) )
				{
					m_err = WR_ERR_unexpected_EOF;
					return false;
				}

				if ( c == '*' )
				{
					// "**/" closes too: each '*' may be the one before the '/'
					do
					{
						if ( !getChar(c) )
						{
							m_err = WR_ERR_unexpected_EOF;
							return false;
						}
					} while ( c == '*' );

					if ( c == '/' )
					{
						break;
					}
				}
			}

			return true;
		}

		// else not a comment
	}

	return false;
}

//------------------------------------------------------------------------------
bool WRCompilationContext::readCurlyBlock( WRstr& block )
{
	char c;
	int closesNeeded = 0;

	for(;;)
	{
		if ( !getChar(c) )
		{
			m_err = WR_ERR_unexpected_EOF;
			return false;
		}

		// read past comments
		if ( checkAsComment(c) )
		{
			continue;
		}

		if ( m_err )
		{
			return false;
		}

		block += c;
		
		if ( c == '{' )
		{
			++closesNeeded;
		}
		
		if ( c == '}' )
		{
			if ( closesNeeded == 0 )
			{
				m_err = WR_ERR_unexpected_token;
				return false;
			}
			
			if ( !--closesNeeded )
			{
				break;
			}
		}
	}

	return true;
}


#else // WRENCH_WITHOUT_COMPILER

WRError wr_compile( const char* source,
					const int size,
					unsigned char** out,
					int* outLen,
					WRstr* errMsg,
					const uint8_t compilerOptionFlags )
{
	return WR_ERR_compiler_not_loaded;
}

WRError wr_compileEx( const char* source,
	const int size,
	unsigned char** out,
	int* outLen,
	WRstr* errMsg,
	const uint8_t compilerOptionFlags,
	int* errorLine,
	int* errorColumn )
{
	return WR_ERR_compiler_not_loaded;
}

WRError wr_compileWithOptions( const char* source,
	const int size,
	unsigned char** out,
	int* outLen,
	const WRCompileOptions& options,
	WRstr* errMsg,
	int* errorLine,
	int* errorColumn,
	WRstr* errorFile )
{
	return WR_ERR_compiler_not_loaded;
}

#endif
