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
static bool wr_isBlankInitializerToken( WRstr const& token )
{
	return token == "="
		|| token == "+="
		|| token == "-="
		|| token == "*="
		|| token == "/="
		|| token == "%="
		|| token == "|="
		|| token == "&="
		|| token == "^="
		|| token == ">>="
		|| token == "<<=";
}

//------------------------------------------------------------------------------
static bool wr_expressionHasBlankDeclaration( WRarray<WRExpressionContext>& context, int depth )
{
	for( int i=0; i<depth; ++i )
	{
		if ( context[i].blankSeen )
		{
			return true;
		}
	}

	return false;
}

// true when the node completes an operand: a value, a postfix operator, or an
// && / || node (its right side is embedded in it)
static bool wr_completesOperand( const WRExpressionContext& node )
{
	return node.type != EXTYPE_OPERATION
			|| (node.operation
				&& (node.operation->type == WR_OPER_POST || node.operation->type == WR_OPER_LOGICAL));
}

// the operator a token stands for when it follows an operand
static const WROperation* wr_infixOperation( WRstr const& token )
{
	for( int i=0; c_operations[i].token; ++i )
	{
		if ( token == c_operations[i].token
				&& (c_operations[i].type == WR_OPER_BINARY
					|| c_operations[i].type == WR_OPER_BINARY_COMMUTE
					|| c_operations[i].type == WR_OPER_TERNARY
					|| c_operations[i].type == WR_OPER_LOGICAL) )
		{
			return c_operations + i;
		}
	}
	return 0;
}

//------------------------------------------------------------------------------
char WRCompilationContext::parseExpressionBody( WRExpression& expression )
{
	int depth = 0;
	char end = 0;

	m_newHashValue = 0;

	for(;;)
	{
		WRValue& value = expression.context[depth].value;
		WRstr& token = expression.context[depth].token;

		expression.context[depth].bytecode.clear();
		expression.context[depth].bytecode2.clear(); // reset stale ternary false-branch bytecode in this working slot
		expression.context[depth].setLocalSpace( expression.bytecode.localSpace, expression.bytecode.isStructSpace );
		if ( !getToken(expression.context[depth]) )
		{
			return 0;
		}

		if ( value.type != WR_REF )
		{
			if ( (depth > 0) 
				 && ((expression.context[depth - 1].type == EXTYPE_LABEL) 
					 || (expression.context[depth - 1].type == EXTYPE_LITERAL)) )
			{
				// two labels/literals cannot follow each other
				m_err = WR_ERR_bad_expression;
				return 0;
			}

			// it's a literal
			expression.context[depth].type = EXTYPE_LITERAL;
			expression.context[depth].temporary = true; // a literal is a value, not a variable
			if ( (uint8_t)value.type == WR_COMPILER_LITERAL_STRING )
			{
				expression.context[depth].literalString = *(WRstr*)value.p;
			}
			
			++depth;
			continue;
		}

		if ( token == ";" || token == ")" || token == "}" || token == "," || token == "]" || token == ":" )
		{
			end = token[0];
			break;
		}

		pushDebug( WRD_LineNumber, expression.bytecode, getSourcePosition() );
		
		if ( token == "var" )
		{
			expression.context[depth].varSeen = true;
			expression.allowFunctionNameHashLiteral = true;
			continue;
		}
		else if ( token == "blank" )
		{
			expression.context[depth].varSeen = true;
			expression.context[depth].blankSeen = true;
			expression.allowFunctionNameHashLiteral = true;
			continue;
		}
		else if ( token == "{" )
		{
			depth = parseInitializer( expression, depth ) + 1;
			if ( depth == 0 )
			{
				return false;
			}

			continue;
		}

		if ( wr_isBlankInitializerToken(token)
			 && wr_expressionHasBlankDeclaration(expression.context, depth) )
		{
			m_err = WR_ERR_blank_variables_cannot_be_initialized;
			return 0;
		}

		// the right side of && or || ends in front of an operator that binds
		// as loosely or looser, which the enclosing expression then takes
		if ( expression.stopPrecedence
				&& !m_quoted
				&& depth > 0
				&& wr_completesOperand(expression.context[depth - 1]) )
		{
			const WROperation* infix = wr_infixOperation( token );
			if ( infix && infix->precedence >= expression.stopPrecedence )
			{
				m_loadedToken = token;
				m_loadedValue = value;
				m_loadedQuoted = m_quoted;
				end = c_endAtOperator;
				break;
			}
		}

		if ( !m_quoted && (token == "&&" || token == "||") )
		{
			// short circuit: the right side is parsed into the operator node
			// and only runs when the left side does not decide the result
			if ( depth == 0 || !wr_completesOperand(expression.context[depth - 1]) )
			{
				m_err = WR_ERR_bad_expression;
				return 0;
			}

			if ( !operatorFound(token, expression.context, depth) )
			{
				m_err = WR_ERR_bad_expression;
				return 0;
			}

			expression.context[depth].bytecode.clear(); // the node holds the right side only
			int precedence = expression.context[depth].operation->precedence;

			WRstr& token2 = expression.context[depth].token;
			WRValue& value2 = expression.context[depth].value;

			if ( !getToken(expression.context[depth]) )
			{
				m_err = WR_ERR_unexpected_EOF;
				return 0;
			}

			WRExpression rightSide( expression.bytecode.localSpace, expression.bytecode.isStructSpace );
			rightSide.allowFunctionNameHashLiteral = expression.allowFunctionNameHashLiteral;
			rightSide.allowLabelDeclarations = false;
			rightSide.stopPrecedence = precedence;
			rightSide.context[0].token = token2;
			rightSide.context[0].value = value2;
			m_loadedToken = token2;
			m_loadedValue = value2;
			m_loadedQuoted = m_quoted;

			char rightEnd = parseExpression( rightSide );
			if ( !rightEnd || m_err )
			{
				m_err = m_err ? m_err : WR_ERR_unexpected_EOF;
				return 0;
			}

			if ( !rightSide.bytecode.all.size() )
			{
				m_err = WR_ERR_bad_expression;
				return 0;
			}

			expression.context[depth].bytecode = rightSide.bytecode;
			++depth;

			if ( rightEnd == c_endAtOperator )
			{
				continue; // the operator that ended it is parsed next
			}

			end = rightEnd;
			break;
		}

		if ( !m_quoted && token == "?" )
		{
			// ternary is parsed as a single operator node with recursive true/false expression branches.
			// a postfix operator (i++ ? a : b) or an && / || completes its operand
			if ( depth == 0 || !wr_completesOperand(expression.context[depth - 1]) )
			{
				m_err = WR_ERR_bad_expression;
				return 0;
			}

			if ( !operatorFound(token, expression.context, depth) )
			{
				m_err = WR_ERR_bad_expression;
				return 0;
			}

			WRstr& token2 = expression.context[depth].token;
			WRValue& value2 = expression.context[depth].value;

			if ( !getToken(expression.context[depth]) )
			{
				m_err = WR_ERR_unexpected_EOF;
				return 0;
			}

			WRExpression ifTrue( expression.bytecode.localSpace, expression.bytecode.isStructSpace );
			ifTrue.allowFunctionNameHashLiteral = expression.allowFunctionNameHashLiteral;
			ifTrue.allowLabelDeclarations = false;
			ifTrue.context[0].token = token2;
			ifTrue.context[0].value = value2;
			m_loadedToken = token2;
			m_loadedValue = value2;
			m_loadedQuoted = m_quoted;

			if ( parseExpression(ifTrue) != ':' || m_err )
			{
				m_err = m_err ? m_err : WR_ERR_unexpected_token;
				return 0;
			}

			if ( !ifTrue.bytecode.all.size() )
			{
				m_err = WR_ERR_bad_expression;
				return 0;
			}

			expression.context[depth].bytecode = ifTrue.bytecode;

			if ( !getToken(expression.context[depth]) )
			{
				m_err = WR_ERR_unexpected_EOF;
				return 0;
			}

			WRExpression ifFalse( expression.bytecode.localSpace, expression.bytecode.isStructSpace );
			ifFalse.allowFunctionNameHashLiteral = expression.allowFunctionNameHashLiteral;
			ifFalse.allowLabelDeclarations = false;
			ifFalse.context[0].token = token2;
			ifFalse.context[0].value = value2;
			m_loadedToken = token2;
			m_loadedValue = value2;
			m_loadedQuoted = m_quoted;

			end = parseExpression(ifFalse);
			if ( !end || m_err )
			{
				m_err = m_err ? m_err : WR_ERR_unexpected_EOF;
				return 0;
			}

			if ( !ifFalse.bytecode.all.size() )
			{
				m_err = WR_ERR_bad_expression;
				return 0;
			}

			expression.context[depth].bytecode2 = ifFalse.bytecode;
			++depth;
			break;
		}
		
		if ( operatorFound(token, expression.context, depth) )
		{
			++depth;
			continue;
		}
		
		if ( !m_quoted && token == "new" )
		{
			if ( (depth < 2) || 
				 (expression.context[depth - 1].operation
				  && expression.context[ depth - 1 ].operation->opcode != O_Assign) )
			{
				m_err = WR_ERR_unexpected_token;
				return 0;
			}

			if ( (depth < 2) 
				 || (expression.context[depth - 2].operation
					 && expression.context[ depth - 2 ].operation->opcode != O_Index) )
			{
				m_err = WR_ERR_unexpected_token;
				return 0;
			}
			
			WRstr& token2 = expression.context[depth].token;
			WRValue& value2 = expression.context[depth].value;

			bool isGlobal;
			WRstr prefix;
			bool isLibConstant;

			if ( !getToken(expression.context[depth]) )
			{
				m_err = WR_ERR_bad_expression;
				return 0;
			}

			if ( !isValidLabel(token2, isGlobal, prefix, isLibConstant) || isLibConstant )
			{
				m_err = WR_ERR_bad_expression;
				return 0;
			}

			WRstr functionName = token2;
			uint32_t hash = wr_hashStr( functionName );
			
			if ( !getToken(expression.context[depth]) )
			{
				m_err = WR_ERR_bad_expression;
				return 0;
			}

			if ( !m_quoted && token2 == ";" )
			{
				if ( !parseCallFunction(expression, functionName, depth, false) )
				{
					return 0;
				}

				m_loadedToken = ";";
				m_loadedValue.p2 = INIT_AS_REF;
				m_loadedQuoted = m_quoted;
			}
			else if ( !m_quoted && token2 == "(" )
			{
				if ( !parseCallFunction(expression, functionName, depth, true) )
				{
					return 0;
				}

				if ( !getToken(expression.context[depth]) )
				{
					m_err = WR_ERR_unexpected_EOF;
					return 0;
				}

				if ( token2 != "{" )
				{
					m_loadedToken = token2;
					m_loadedValue = value2;
					m_loadedQuoted = m_quoted;
				}
			}
			else if (!m_quoted && token2 == "{")
			{
				if ( !parseCallFunction(expression, functionName, depth, false) )
				{
					return 0;
				}
				token2 = "{";
			}
			else if ( !m_quoted && token2 == "[" )
			{
				// must be "array" directive
				if ( !getToken(expression.context[depth], "]") )
				{
					m_err = WR_ERR_unexpected_token;
					return 0;
				}

				expression.context.remove( depth - 1, 1 ); // knock off the equate
				depth--;

				WRstr& token3 = expression.context[depth].token;
				WRValue& value3 = expression.context[depth].value;

				if ( !getToken(expression.context[depth], "{") )
				{
					m_err = WR_ERR_unexpected_token;
					return 0;
				}

				uint16_t initializer = 0;

				if ( !m_quoted && token3 == "{" )
				{
					for(;;)
					{
						if ( !getToken(expression.context[depth]) )
						{
							m_err = WR_ERR_unexpected_EOF;
							return 0;
						}

						if ( !m_quoted && token3 == "}" )
						{
							break;
						}

						if ( !parseCallFunction(expression, functionName, depth, false) )
						{
							return 0;
						}

						if (!m_quoted && token3 != "{")
						{
							m_err = WR_ERR_unexpected_token;
							return 0;
						}

						if ( !pushObjectTable(expression.context[depth], expression.bytecode.localSpace, hash) )
						{
							m_err = m_err ? m_err : WR_ERR_bad_expression;
							return 0;
						}

						pushOpcode( expression.context[depth].bytecode, O_AssignToArrayAndPop );

						unsigned char data[2];
						wr_pack16( initializer, data );
						++initializer;

						pushData( expression.context[depth].bytecode, data, 2 );

						if ( !getToken(expression.context[depth]) )
						{
							m_err = WR_ERR_unexpected_EOF;
							return 0;
						}

						if ( !m_quoted && token3 == "," )
						{
							continue;
						}
						else if ( token3 != "}" )
						{
							m_err = WR_ERR_unexpected_token;
							return 0;
						}
						else
						{
							break;
						}
					}
					
					if ( !getToken(expression.context[depth], ";") )
					{
						m_err = WR_ERR_unexpected_token;
						return 0;
					}

					WRstr t("@init");
					for( int o=0; c_operations[o].token; o++ )
					{
						if ( t == c_operations[o].token )
						{
							expression.context[depth].type = EXTYPE_OPERATION;
							expression.context[depth].operation = c_operations + o;
							break;
						}
					}

					m_loadedToken = token3;
					m_loadedValue = value3;
					m_loadedQuoted = m_quoted;
				}
				else if ( token3 != ";" ) // token2 dangles: remove() reallocated the context
				{
					m_err = WR_ERR_unexpected_token;
					return 0;
				}

				++depth;
				continue;
			}
			else if ( token2 != "{" )
			{
				m_err = WR_ERR_bad_expression;
				return 0;
			}

			if ( !pushObjectTable(expression.context[depth], expression.bytecode.localSpace, hash) )
			{
				m_err = m_err ? m_err : WR_ERR_bad_expression;
				return 0;
			}

			++depth;
			continue;
		}

		if ( !m_quoted && token == "(" )
		{
			// might be cast, call or sub-expression
			
			if ( (depth > 0) &&
				 (expression.context[depth - 1].type == EXTYPE_LABEL
				 || expression.context[depth - 1].type == EXTYPE_LIB_CONSTANT) )
			{
				// always only a call
				expression.context[depth - 1].type = EXTYPE_LABEL;
				
				--depth;
				if ( !parseCallFunction(expression, expression.context[depth].token, depth, true) )
				{
					return 0;
				}
			}
			else if ( !getToken(expression.context[depth]) )
			{
				m_err = WR_ERR_unexpected_EOF;
				return 0;
			}
			else if (token == ")")
			{
				m_err = WR_ERR_empty_parens;
				return 0;
			}
			else if ( token == "int" )
			{
				if ( !getToken(expression.context[depth], ")") )
				{
					m_err = WR_ERR_bad_expression;
					return 0;
				}

				operatorFound( WRstr("@i"), expression.context, depth );
			}
			else if ( token == "float" )
			{
				if ( !getToken(expression.context[depth], ")") )
				{
					m_err = WR_ERR_bad_expression;
					return 0;
				}

				operatorFound( WRstr("@f"), expression.context, depth );
			}
			else if (depth < 0)
			{
				m_err = WR_ERR_bad_expression;
				return 0;
			}
			else
			{
				WRExpression nex( expression.bytecode.localSpace, expression.bytecode.isStructSpace );
				nex.context[0].token = token;
				nex.context[0].value = value;
				m_loadedToken = token;
				m_loadedValue = value;
				m_loadedQuoted = m_quoted;
				if ( parseExpression(nex) != ')' )
				{
					m_err = m_err ? m_err : WR_ERR_unexpected_token;
					return 0;
				}


				if ( depth > 0 && expression.context[depth - 1].operation
					 && expression.context[depth - 1].operation->opcode == O_Remove )
				{
					--depth;
					expression.context[depth].bytecode = nex.bytecode;
					operatorFound( WRstr("._remove"), expression.context, depth );
				}
				else if ( depth > 0 && expression.context[depth - 1].operation
						  && expression.context[depth - 1].operation->opcode == O_HashEntryExists )
				{
					--depth;
					expression.context[depth].bytecode = nex.bytecode;
					operatorFound( WRstr("._exists"), expression.context, depth );
				}
				else
				{
					expression.context[depth].type = EXTYPE_BYTECODE_RESULT;
					expression.context[depth].bytecode = nex.bytecode;
					// (t) is still t, (f()), (a + b) or (5) is still a value
					expression.context[depth].temporary = nex.context.count() == 1 && nex.context[0].temporary;
				}
			}

			++depth;
			continue;
		}

		if ( !m_quoted && token == "[" )
		{
			WRExpression nex( expression.bytecode.localSpace, expression.bytecode.isStructSpace );
			nex.context[0].token = token;
			nex.context[0].value = value;

			if ( parseExpression(nex) != ']' )
			{
				m_err = m_err ? m_err : WR_ERR_unexpected_EOF;
				return 0;
			}

			if ( nex.bytecode.all.size() == 0 )
			{
				operatorFound( WRstr("@[]"), expression.context, depth );
				expression.context[depth].bytecode.all.clear();
				expression.context[depth].bytecode.opcodes.clear();
				pushOpcode( expression.context[depth].bytecode, O_LiteralZero );
				pushOpcode( expression.context[depth].bytecode, O_InitArray );
			}
			else 
			{
				expression.context[depth].bytecode = nex.bytecode;
				operatorFound( WRstr("@[]"), expression.context, depth );
			}

			++depth;
			continue;
		}

		bool isGlobal;
		WRstr prefix;
		bool isLibConstant;

		if ( isValidLabel(token, isGlobal, prefix, isLibConstant) )
		{
			if ( (depth > 0) 
				&& ((expression.context[depth - 1].type == EXTYPE_LABEL) 
					|| (expression.context[depth - 1].type == EXTYPE_LITERAL)
					|| (expression.context[depth - 1].type == EXTYPE_LIB_CONSTANT)) )
			{
				// two labels/literals cannot follow each other
				m_err = WR_ERR_bad_expression;
				return 0;
			}

			WRstr label = token;

			if ( depth == 0 && !m_parsingFor && expression.allowLabelDeclarations )
			{
				if ( !getToken(expression.context[depth]) )
				{
					m_err = WR_ERR_unexpected_EOF;
					return 0;
				}

				if ( m_parsingNew )
				{
					if ( token == "=" )
					{
						m_newHashValue = wr_hashStr( label );

						continue;
//						getToken(expression.context[depth]) );
//						--depth;
						
					}
				}
				
				if ( !m_quoted && token == ":" )
				{
					// a label may appear once per function: earlier statements
					// already moved theirs into the unit's bytecode
					uint32_t hash = wr_hashStr( label );
					WRBytecode& unitCode = m_units[m_unitTop].bytecode;
					for( unsigned int i=0; i<unitCode.jumpOffsetTargets.count(); ++i )
					{
						if ( unitCode.jumpOffsetTargets[i].gotoHash == hash )
						{
							m_err = WR_ERR_bad_goto_label;
							return false;
						}
					}
					
					int index = addRelativeJumpTarget( expression.bytecode );
					expression.bytecode.jumpOffsetTargets[index].gotoHash = hash;
					expression.bytecode.jumpOffsetTargets[index].offset = expression.bytecode.all.size() + 1;

					// this always return a value
					pushOpcode( expression.bytecode, O_LiteralZero );

					
					return ';';
				}
				else
				{
					m_loadedToken = token;
					m_loadedValue = value;
					m_loadedQuoted = m_quoted;
				}
			}
			

			expression.context[depth].type = isLibConstant ? EXTYPE_LIB_CONSTANT : EXTYPE_LABEL;
			expression.context[depth].temporary = isLibConstant;
			expression.context[depth].token = label;
			expression.context[depth].global = isGlobal;
			expression.context[depth].prefix = prefix;
			++depth;

			continue;
		}

		m_err = WR_ERR_bad_expression;
		return 0;
	}

	// trim to the number of committed expression nodes, which also allows ternary to consume the outer delimiter cleanly.
	expression.context.setCount( depth );

	if ( depth == 2
		 && expression.lValue
		 && expression.context[0].type == EXTYPE_LABEL
		 && expression.context[1].type == EXTYPE_OPERATION
		 && expression.context[1].operation->opcode == O_Index
		 && expression.context[1].bytecode.opcodes.size() > 0 )
	{
		unsigned int i = expression.context[1].bytecode.opcodes.size() - 1;
		unsigned int a = expression.context[1].bytecode.all.size() - 1;
		WROpcode o = (WROpcode)(expression.context[1].bytecode.opcodes[ i ]);
		
		switch( o )
		{
			case O_Index:
			{
				expression.context[1].bytecode.all[a] = O_InitArray;
				break;
			}

			case O_IndexLiteral16:
			{
				if (a > 1)
				{
					expression.context[1].bytecode.all[a - 2] = O_LiteralInt16;
					pushOpcode(expression.context[1].bytecode, O_InitArray);
				}
				break;
			}
			
			case O_IndexLiteral8:
			{
				if (a > 0)
				{
					expression.context[1].bytecode.all[a - 1] = O_LiteralInt8;
					pushOpcode(expression.context[1].bytecode, O_InitArray);
				}
				break;
			}

			default: break;
		}
	}

	markWriteChains( expression );
	resolveExpression( expression );

	return end;
}

#endif
