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

#ifdef WRENCH_WITHOUT_COMPILER

//------------------------------------------------------------------------------
void wr_disassemble( const uint8_t* bytecode, const unsigned int len, WRstr& out, const bool includeComments )
{
	(void)bytecode;
	(void)len;
	(void)includeComments;
	out = "disassembler unavailable in WRENCH_WITHOUT_COMPILER build\n";
}

//------------------------------------------------------------------------------
void wr_disassemble( const uint8_t* bytecode, const unsigned int len, char** out, unsigned int* outLen )
{
	WRstr listing;
	wr_disassemble( bytecode, len, listing, true );
	listing.release( out, outLen );
}

#else

//------------------------------------------------------------------------------
bool wr_startsWith( const char* s, const char* p )
{
	while( *p )
	{
		if ( *s++ != *p++ )
		{
			return false;
		}
	}
	return true;
}

//------------------------------------------------------------------------------
bool wr_endsWith( const char* s, const char* suffix )
{
	unsigned int ls = (unsigned int)strlen(s);
	unsigned int le = (unsigned int)strlen(suffix);
	return (ls >= le) && (strncmp(s + (ls - le), suffix, le) == 0);
}

//------------------------------------------------------------------------------
bool wr_contains( const char* s, const char* needle )
{
	return strstr(s, needle) ? true : false;
}

//------------------------------------------------------------------------------
const char* wr_safeOpcodeName( const uint8_t op )
{
	return (op < O_HASH_PLACEHOLDER) ? c_opcodeName[op] : "<meta>";
}

// the name of the instruction at "opPtr - 1": an extended one is named by its
// second byte
static const char* wr_instructionName( const uint8_t op, const uint8_t* opPtr, const uint8_t* end )
{
	if ( op == O_Extended && opPtr < end && opPtr[0] < OX_LAST )
	{
		return c_extendedOpcodeName[opPtr[0]];
	}
	return wr_safeOpcodeName( op );
}

//------------------------------------------------------------------------------
int wr_operandSizeForOpcode( const uint8_t* opPtr, const uint8_t* end, const uint8_t op )
{
	switch( op )
	{
		case O_LiteralZero:
		case O_PushIndexFunctionReturnValue:
		case O_AssignToHashTableAndPop:
		case O_Remove:
		case O_HashEntryExists:
		case O_PopOne:
		case O_ReturnZero:
		case O_Return:
		case O_Stop:
		case O_Dereference:
		case O_Index:
		case O_IndexSkipLoad:
		case O_IndexCreate:
		case O_CountOf:
		case O_HashOf:
		case O_BinaryRightShiftSkipLoad:
		case O_BinaryLeftShiftSkipLoad:
		case O_BinaryAndSkipLoad:
		case O_BinaryOrSkipLoad:
		case O_BinaryXORSkipLoad:
		case O_BinaryModSkipLoad:
		case O_BinaryMultiplication:
		case O_BinarySubtraction:
		case O_BinaryDivision:
		case O_BinaryRightShift:
		case O_BinaryLeftShift:
		case O_BinaryMod:
		case O_BinaryOr:
		case O_BinaryXOR:
		case O_BinaryAnd:
		case O_BinaryAddition:
		case O_BitwiseNOT:
		case O_LogicalAnd:
		case O_LogicalOr:
		case O_CompareLE:
		case O_CompareGE:
		case O_CompareGT:
		case O_CompareLT:
		case O_CompareEQ:
		case O_CompareNE:
		case O_PostIncrement:
		case O_PostDecrement:
		case O_PreIncrement:
		case O_PreDecrement:
		case O_PreIncrementAndPop:
		case O_PreDecrementAndPop:
		case O_Assign:
		case O_AssignAndPop:
		case O_SubtractAssign:
		case O_AddAssign:
		case O_ModAssign:
		case O_MultiplyAssign:
		case O_DivideAssign:
		case O_ORAssign:
		case O_ANDAssign:
		case O_XORAssign:
		case O_RightShiftAssign:
		case O_LeftShiftAssign:
		case O_SubtractAssignAndPop:
		case O_AddAssignAndPop:
		case O_ModAssignAndPop:
		case O_MultiplyAssignAndPop:
		case O_DivideAssignAndPop:
		case O_ORAssignAndPop:
		case O_ANDAssignAndPop:
		case O_XORAssignAndPop:
		case O_RightShiftAssignAndPop:
		case O_LeftShiftAssignAndPop:
		case O_LogicalNot:
		case O_Negate:
		case O_ToInt:
		case O_ToFloat:
		case O_InitArray:
		{
			return 0;
		}

		case O_LiteralInt8:
		case O_IndexLiteral8:
		case O_GlobalStop:
		case O_Yield:
		case O_AssignToObjectTableByOffset:
		case O_StackSwap:
		case O_LoadFromLocal:
		case O_LoadFromGlobal:
		case O_AssignToGlobalAndPop:
		case O_AssignToLocalAndPop:
		case O_IncGlobal:
		case O_DecGlobal:
		case O_IncLocal:
		case O_DecLocal:
		case O_BinaryAdditionAndStoreGlobal:
		case O_BinarySubtractionAndStoreGlobal:
		case O_BinaryMultiplicationAndStoreGlobal:
		case O_BinaryDivisionAndStoreGlobal:
		case O_BinaryAdditionAndStoreLocal:
		case O_BinarySubtractionAndStoreLocal:
		case O_BinaryMultiplicationAndStoreLocal:
		case O_BinaryDivisionAndStoreLocal:
		case O_GSCompareEQ:
		case O_LSCompareEQ:
		case O_GSCompareNE:
		case O_LSCompareNE:
		case O_GSCompareGE:
		case O_LSCompareGE:
		case O_GSCompareLE:
		case O_LSCompareLE:
		case O_GSCompareGT:
		case O_LSCompareGT:
		case O_GSCompareLT:
		case O_LSCompareLT:
		{
			return 1;
		}

		case O_LiteralInt16:
		case O_IndexLiteral16:
		case O_AssignToArrayAndPop:
		case O_RelativeJump:
		case O_BZ:
		case O_CompareBEQ:
		case O_CompareBNE:
		case O_CompareBGE:
		case O_CompareBLE:
		case O_CompareBGT:
		case O_CompareBLT:
		case O_BLA:
		case O_BLO:
		case O_NewObjectTable:
		case O_DebugInfo:
		case O_RelativeJump8:
		case O_BZ8:
		case O_CompareBEQ8:
		case O_CompareBNE8:
		case O_CompareBGE8:
		case O_CompareBLE8:
		case O_CompareBGT8:
		case O_CompareBLT8:
		case O_BLA8:
		case O_BLO8:
		case O_LiteralInt8ToGlobal:
		case O_LiteralInt8ToLocal:
		case O_IndexLocalLiteral8:
		case O_IndexGlobalLiteral8:
		case O_SwapTwoToTop:
		case O_GPushIterator:
		case O_LPushIterator:
		case O_LLValues: // two variable indexes
		case O_LGValues:
		case O_GLValues:
		case O_GGValues:
		{
			return 2;
		}

		case O_LiteralInt16ToGlobal:
		case O_LiteralInt16ToLocal:
		case O_IndexLocalLiteral16:
		case O_IndexGlobalLiteral16:
		{
			return 3;
		}

		case O_CallFunctionByIndex:
		{
			// args, function index, then a skip byte measured from itself
			// that steps over what is left of the original hash call
			if ( opPtr + 3 > end )
			{
				return -1;
			}
			uint8_t skip = READ_8_FROM_PC(opPtr + 2);
			return skip ? 2 + (int)skip : 3;
		}

		case O_LiteralInt32:
		case O_LiteralFloat:
		case O_LoadLibConstant:
		case O_AssignToObjectTableByHash:
		case O_StackIndexHash:
		case O_StackIndexHashCreate:
		{
			return 4;
		}

		case O_Extended:
		{
			// the second opcode byte, then its own operands
			if ( opPtr >= end )
			{
				return -1;
			}
			switch( opPtr[0] )
			{
				case OX_RelativeJump32:
				{
					return 5;
				}

				default:
				{
					return -1;
				}
			}
		}

		case O_LiteralInt32ToLocal:
		case O_LiteralInt32ToGlobal:
		case O_LiteralFloatToGlobal:
		case O_LiteralFloatToLocal:
		case O_CallFunctionByHash:
		case O_CallFunctionByHashAndPop:
		case O_CallLibFunction:
		case O_CallLibFunctionAndPop:
		case O_GlobalIndexHash:
		case O_LocalIndexHash:
		{
			return 5;
		}

		case O_LiteralString:
		{
			if ( opPtr + 2 > end )
			{
				return -1;
			}
			uint16_t l = (uint16_t)READ_16_FROM_PC(opPtr);
			return 2 + (int)l;
		}

		case O_Switch:
		{
			if ( opPtr + 2 > end )
			{
				return -1;
			}
			uint16_t mod = (uint16_t)READ_16_FROM_PC(opPtr);
			return 4 + (int)(mod * 6U);
		}

		case O_SwitchLinear:
		{
			if ( opPtr + 1 > end )
			{
				return -1;
			}
			uint8_t count = READ_8_FROM_PC(opPtr);
			return 3 + (int)(count * 2U);
		}

		default:
		{
			break;
		}
	}

	// Optimized families with regular encodings.
	const char* name = wr_safeOpcodeName(op);

	if ( wr_startsWith(name, "GG") || wr_startsWith(name, "LL") || wr_startsWith(name, "GL") || wr_startsWith(name, "LG") )
	{
		if ( wr_contains(name, "NextKeyValueOrJump") )
		{
			return 5; // idx, idx, dest, rel16
		}
		if ( wr_contains(name, "BZ8") )
		{
			return 4; // idx, idx, rel8, pad8
		}
		if ( wr_contains(name, "BZ") )
		{
			return 4; // idx, idx, rel16
		}
		return 2; // idx, idx
	}

	if ( wr_startsWith(name, "GS") || wr_startsWith(name, "LS") )
	{
		if ( wr_contains(name, "BZ8") )
		{
			return 3; // idx, rel8, pad8
		}
		if ( wr_contains(name, "BZ") )
		{
			return 3; // idx, rel16
		}
		return 1; // idx
	}

	if ( wr_startsWith(name, "GNextValueOrJump") || wr_startsWith(name, "LNextValueOrJump") )
	{
		return 4; // iterIdx, destIdx, rel16
	}

	if ( wr_startsWith(name, "LocalBZ8") || wr_startsWith(name, "GlobalBZ8") )
	{
		return 3; // idx, rel8, pad8
	}

	if ( wr_startsWith(name, "LocalBZ") || wr_startsWith(name, "GlobalBZ") )
	{
		return 3;
	}

	if ( wr_startsWith(name, "GGBinary") || wr_startsWith(name, "GLBinary") || wr_startsWith(name, "LGBinary") || wr_startsWith(name, "LLBinary") )
	{
		return 2;
	}

	return 0;
}

//------------------------------------------------------------------------------
void wr_appendBytes( WRstr& out, const uint8_t* p, int n )
{
	for( int i=0; i<n; ++i )
	{
		out.appendFormat( "%02X ", (unsigned int)p[i] );
	}
	for( int i=n; i<12; ++i )
	{
		out += "   ";
	}
}

//------------------------------------------------------------------------------
void wr_appendEscapedBytes( WRstr& out, const uint8_t* p, int n )
{
	for( int i=0; i<n; ++i )
	{
		uint8_t c = p[i];
		if ( c == '\\' || c == '"' )
		{
			out += '\\';
			out += (char)c;
		}
		else if ( c >= 32 && c < 127 )
		{
			out += (char)c;
		}
		else
		{
			out.appendFormat( "\\x%02X", (unsigned int)c );
		}
	}
}

//------------------------------------------------------------------------------
void wr_appendRawOperandHex( WRstr& ops, const uint8_t* opPtr, int opLen )
{
	for( int i=0; i<opLen; ++i )
	{
		if ( i )
		{
			ops += " ";
		}
		ops.appendFormat( "0x%02X", (unsigned int)opPtr[i] );
	}
}

//------------------------------------------------------------------------------
void wr_decodeOperands( const uint8_t op,
							const uint8_t* opPtr,
							int opLen,
							int instOffset,
							WRstr& ops,
							WRstr& note )
{
	const char* name = c_opcodeName[op];
	ops.clear();
	note.clear();

	if ( opLen <= 0 )
	{
		return;
	}

	if ( op == O_CallFunctionByHash || op == O_CallFunctionByHashAndPop || op == O_CallLibFunction || op == O_CallLibFunctionAndPop )
	{
		ops.appendFormat( "argc=0x%02X hash=0x%08X", (unsigned int)opPtr[0], (unsigned int)READ_32_FROM_PC(opPtr + 1) );
		return;
	}
	if ( op == O_CallFunctionByIndex )
	{
		ops.appendFormat( "argc=0x%02X fnIdx=0x%02X pcSkip=0x%02X",
						  (unsigned int)opPtr[0], (unsigned int)opPtr[1], (unsigned int)opPtr[2] );
		return;
	}
	if ( op == O_LoadLibConstant || op == O_AssignToObjectTableByHash || op == O_StackIndexHash || op == O_StackIndexHashCreate )
	{
		ops.appendFormat( "hash=0x%08X", (unsigned int)READ_32_FROM_PC(opPtr) );
		return;
	}

	if ( (op == O_LLValues || op == O_LGValues || op == O_GLValues || op == O_GGValues) && opLen >= 2 )
	{
		char first = (op == O_LLValues || op == O_LGValues) ? 'l' : 'g';
		char second = (op == O_LLValues || op == O_GLValues) ? 'l' : 'g';
		ops.appendFormat( "%c[0x%02X] %c[0x%02X]", first, (unsigned int)opPtr[0], second, (unsigned int)opPtr[1] );
		return;
	}

	if ( op == O_GlobalIndexHash || op == O_LocalIndexHash )
	{
		char scope = (op == O_GlobalIndexHash) ? 'g' : 'l';
		ops.appendFormat( "%c[0x%02X] keyHash=0x%08X", scope, (unsigned int)opPtr[0], (unsigned int)READ_32_FROM_PC(opPtr + 1) );
		return;
	}

	if ( op == O_LiteralString && opLen >= 2 )
	{
		uint16_t l = (uint16_t)READ_16_FROM_PC(opPtr);
		ops.appendFormat( "len=0x%04X", (unsigned int)l );
		wr_appendEscapedBytes( note, opPtr + 2, (int)l );
		return;
	}

	if ( op == O_LiteralInt32 || op == O_LiteralInt32ToLocal || op == O_LiteralInt32ToGlobal )
	{
		if ( op == O_LiteralInt32 )
		{
			ops.appendFormat( "imm=0x%08X", (unsigned int)READ_32_FROM_PC(opPtr) );
		}
		else
		{
			char scope = (op == O_LiteralInt32ToGlobal) ? 'g' : 'l';
			ops.appendFormat( "%c[0x%02X] imm=0x%08X", scope, (unsigned int)opPtr[0], (unsigned int)READ_32_FROM_PC(opPtr + 1) );
		}
		return;
	}

	if ( op == O_LiteralFloat || op == O_LiteralFloatToGlobal || op == O_LiteralFloatToLocal )
	{
		union { uint32_t u; float f; } v;
		if ( op == O_LiteralFloat )
		{
			v.u = (uint32_t)READ_32_FROM_PC(opPtr);
			ops.appendFormat( "imm=0x%08X", (unsigned int)v.u );
		}
		else
		{
			v.u = (uint32_t)READ_32_FROM_PC(opPtr + 1);
			char scope = (op == O_LiteralFloatToGlobal) ? 'g' : 'l';
			ops.appendFormat( "%c[0x%02X] imm=0x%08X", scope, (unsigned int)opPtr[0], (unsigned int)v.u );
		}
		note.appendFormat( "float=%g", v.f );
		return;
	}

	if ( op == O_Extended && opLen >= 5 && opPtr[0] == OX_RelativeJump32 )
	{
		// relative to the vector, which follows the two opcode bytes
		int rel32 = (int)READ_32_FROM_PC(opPtr + 1);
		ops.appendFormat( "rel32=0x%08X ->0x%04X", (unsigned int)rel32, (unsigned int)(instOffset + 2 + rel32) );
		return;
	}

	if ( op == O_RelativeJump || op == O_BZ
		 || (wr_startsWith(name, "CompareB") && !wr_endsWith(name, "8"))
		 || (wr_startsWith(name, "BLA") && !wr_endsWith(name, "8"))
		 || (wr_startsWith(name, "BLO") && !wr_endsWith(name, "8")) )
	{
		int rel = (int)READ_16_FROM_PC(opPtr);
		ops.appendFormat( "rel=0x%04X ->0x%04X", (unsigned int)(uint16_t)rel, (unsigned int)(instOffset + 1 + rel) );
		return;
	}

	if ( op == O_RelativeJump8 || op == O_BZ8
		 || ((wr_startsWith(name, "CompareB") || wr_startsWith(name, "BLA") || wr_startsWith(name, "BLO")) && wr_endsWith(name, "8")) )
	{
		int rel8 = (int)(int8_t)READ_8_FROM_PC(opPtr);
		if ( opLen >= 2 )
		{
			ops.appendFormat( "rel8=0x%02X pad=0x%02X ->0x%04X",
							  (unsigned int)READ_8_FROM_PC(opPtr),
							  (unsigned int)READ_8_FROM_PC(opPtr + 1),
							  (unsigned int)(instOffset + 1 + rel8) );
		}
		else
		{
			ops.appendFormat( "rel8=0x%02X ->0x%04X",
							  (unsigned int)READ_8_FROM_PC(opPtr),
							  (unsigned int)(instOffset + 1 + rel8) );
		}
		return;
	}

	if ( op == O_LocalBZ || op == O_GlobalBZ )
	{
		int16_t rel = READ_16_FROM_PC(opPtr + 1);
		char scope = (op == O_GlobalBZ) ? 'g' : 'l';
		ops.appendFormat( "%c[0x%02X] rel=0x%04X ->0x%04X",
						  scope, (unsigned int)opPtr[0], (unsigned int)(uint16_t)rel, (unsigned int)(instOffset + 2 + rel) );
		return;
	}
	if ( op == O_LocalBZ8 || op == O_GlobalBZ8 )
	{
		int rel8 = (int)(int8_t)opPtr[1];
		char scope = (op == O_GlobalBZ8) ? 'g' : 'l';
		ops.appendFormat( "%c[0x%02X] rel8=0x%02X pad=0x%02X ->0x%04X",
						  scope, (unsigned int)opPtr[0], (unsigned int)opPtr[1], (unsigned int)opPtr[2], (unsigned int)(instOffset + 2 + rel8) );
		return;
	}

	if ( op == O_Switch && opLen >= 4 )
	{
		uint16_t buckets = (uint16_t)READ_16_FROM_PC(opPtr);
		int16_t d = READ_16_FROM_PC(opPtr + 2);
		ops.appendFormat( "buckets=0x%04X defaultRel=0x%04X ->0x%04X",
						  (unsigned int)buckets,
						  (unsigned int)(uint16_t)d,
						  (unsigned int)(instOffset + 1 + d) );
		return;
	}

	if ( op == O_SwitchLinear && opLen >= 3 )
	{
		uint8_t count = READ_8_FROM_PC(opPtr);
		int16_t d = READ_16_FROM_PC(opPtr + 1);
		ops.appendFormat( "count=0x%02X defaultRel=0x%04X ->0x%04X",
						  (unsigned int)count,
						  (unsigned int)(uint16_t)d,
						  (unsigned int)(instOffset + 2 + d) );
		return;
	}

	if ( wr_startsWith(name, "GG") || wr_startsWith(name, "LL") || wr_startsWith(name, "GL") || wr_startsWith(name, "LG") )
	{
		char a = (name[0] == 'G') ? 'g' : 'l';
		char b = (name[1] == 'G') ? 'g' : 'l';
		if ( wr_contains(name, "NextKeyValueOrJump") )
		{
			int16_t rel = READ_16_FROM_PC(opPtr + 3);
			ops.appendFormat( "%c[0x%02X] %c[0x%02X] g[0x%02X] rel=0x%04X ->0x%04X",
							  a, (unsigned int)opPtr[0], b, (unsigned int)opPtr[1], (unsigned int)opPtr[2],
							  (unsigned int)(uint16_t)rel, (unsigned int)(instOffset + 4 + rel) );
			return;
		}
		if ( wr_contains(name, "BZ") )
		{
			if ( wr_endsWith(name, "BZ8") )
			{
				int rel8 = (int)(int8_t)opPtr[2];
				ops.appendFormat( "%c[0x%02X] %c[0x%02X] rel8=0x%02X pad=0x%02X ->0x%04X",
								  a, (unsigned int)opPtr[0], b, (unsigned int)opPtr[1], (unsigned int)opPtr[2], (unsigned int)opPtr[3],
								  (unsigned int)(instOffset + 3 + rel8) );
			}
			else
			{
				int16_t rel = READ_16_FROM_PC(opPtr + 2);
				ops.appendFormat( "%c[0x%02X] %c[0x%02X] rel=0x%04X ->0x%04X",
								  a, (unsigned int)opPtr[0], b, (unsigned int)opPtr[1], (unsigned int)(uint16_t)rel,
								  (unsigned int)(instOffset + 3 + rel) );
			}
			return;
		}
		ops.appendFormat( "%c[0x%02X] %c[0x%02X]", a, (unsigned int)opPtr[0], b, (unsigned int)opPtr[1] );
		return;
	}

	if ( wr_startsWith(name, "GS") || wr_startsWith(name, "LS") )
	{
		char a = name[0];
		if ( wr_contains(name, "BZ") )
		{
			if ( wr_endsWith(name, "BZ8") )
			{
				int rel8 = (int)(int8_t)opPtr[1];
				ops.appendFormat( "%c[0x%02X] rel8=0x%02X pad=0x%02X ->0x%04X", a, (unsigned int)opPtr[0], (unsigned int)opPtr[1], (unsigned int)opPtr[2],
								  (unsigned int)(instOffset + 2 + rel8) );
			}
			else
			{
				int16_t rel = READ_16_FROM_PC(opPtr + 1);
				ops.appendFormat( "%c[0x%02X] rel=0x%04X ->0x%04X", a, (unsigned int)opPtr[0], (unsigned int)(uint16_t)rel,
								  (unsigned int)(instOffset + 2 + rel) );
			}
			return;
		}
		ops.appendFormat( "%c[0x%02X]", a, (unsigned int)opPtr[0] );
		return;
	}

	if ( wr_startsWith(name, "GNextValueOrJump") || wr_startsWith(name, "LNextValueOrJump") )
	{
		int16_t rel = READ_16_FROM_PC(opPtr + 2);
		ops.appendFormat( "%c[0x%02X] g[0x%02X] rel=0x%04X ->0x%04X",
						  name[0], (unsigned int)opPtr[0], (unsigned int)opPtr[1], (unsigned int)(uint16_t)rel,
						  (unsigned int)(instOffset + 3 + rel) );
		return;
	}

	if ( wr_startsWith(name, "Local") || wr_contains(name, "Local") || op == O_AssignToLocalAndPop || op == O_IncLocal || op == O_DecLocal )
	{
		if ( opLen >= 1 && (opLen == 1 || opLen == 2 || opLen == 3) )
		{
			if ( opLen == 1 ) ops.appendFormat( "l[0x%02X]", (unsigned int)opPtr[0] );
			else if ( opLen == 2 ) ops.appendFormat( "l[0x%02X] imm=0x%02X", (unsigned int)opPtr[0], (unsigned int)opPtr[1] );
			else ops.appendFormat( "l[0x%02X] imm=0x%04X", (unsigned int)opPtr[0], (unsigned int)READ_16_FROM_PC(opPtr + 1) );
			return;
		}
	}
	if ( wr_startsWith(name, "Global") || wr_contains(name, "Global") || op == O_AssignToGlobalAndPop || op == O_IncGlobal || op == O_DecGlobal )
	{
		if ( opLen >= 1 && (opLen == 1 || opLen == 2 || opLen == 3) )
		{
			if ( opLen == 1 ) ops.appendFormat( "g[0x%02X]", (unsigned int)opPtr[0] );
			else if ( opLen == 2 ) ops.appendFormat( "g[0x%02X] imm=0x%02X", (unsigned int)opPtr[0], (unsigned int)opPtr[1] );
			else ops.appendFormat( "g[0x%02X] imm=0x%04X", (unsigned int)opPtr[0], (unsigned int)READ_16_FROM_PC(opPtr + 1) );
			return;
		}
	}

	wr_appendRawOperandHex( ops, opPtr, opLen );
}

//------------------------------------------------------------------------------
// the symbols block of a debug image (see WRCompilationContext::link): u16
// units, each u8 locals, u8 arguments, its name and its locals' names (zero
// terminated); then, when there are files, u16 files, each its name and u32
// length of its embedded text, and u16 segments, each u32 virtual line, u16
// file, u32 line
struct WRDisassemblySymbols
{
	const uint8_t* begin;
	const uint8_t* end;
};

//------------------------------------------------------------------------------
static bool wr_skipString( const uint8_t*& p, const uint8_t* end )
{
	while( p < end && *p )
	{
		++p;
	}
	if ( p >= end )
	{
		return false;
	}
	++p;
	return true;
}

//------------------------------------------------------------------------------
// the entry of unit (0 the global code, 1.. the functions), 0 if there is none
static const uint8_t* wr_symbolsUnit( const WRDisassemblySymbols& symbols, const int unit )
{
	if ( !symbols.begin || symbols.begin + 2 > symbols.end )
	{
		return 0;
	}
	int units = (int)(uint16_t)READ_16_FROM_PC( symbols.begin );
	const uint8_t* p = symbols.begin + 2;
	for( int u=0; u<units; ++u )
	{
		if ( p + 2 > symbols.end )
		{
			return 0;
		}
		if ( u == unit )
		{
			return p;
		}
		int locals = p[0];
		p += 2;
		for( int s=0; s<=locals; ++s ) // its name, then the locals
		{
			if ( !wr_skipString(p, symbols.end) )
			{
				return 0;
			}
		}
	}
	return 0;
}

//------------------------------------------------------------------------------
// name of a unit, or of its local slot, 0 when the symbols do not say
static const char* wr_symbolsName( const WRDisassemblySymbols& symbols, const int unit, const int local =-1 )
{
	const uint8_t* p = wr_symbolsUnit( symbols, unit );
	if ( !p || local >= (int)p[0] )
	{
		return 0;
	}
	p += 2;
	for( int s=-1; s<local; ++s )
	{
		if ( !wr_skipString(p, symbols.end) )
		{
			return 0;
		}
	}
	const uint8_t* name = p;
	return wr_skipString( p, symbols.end ) ? (const char*)name : 0;
}

//------------------------------------------------------------------------------
// the file table after the units, 0 when there is none
static const uint8_t* wr_symbolsFiles( const WRDisassemblySymbols& symbols )
{
	int units = (symbols.begin && symbols.begin + 2 <= symbols.end) ? (int)(uint16_t)READ_16_FROM_PC( symbols.begin ) : 0;
	const uint8_t* p = units ? wr_symbolsUnit( symbols, units - 1 ) : 0;
	if ( !p )
	{
		return 0;
	}
	int locals = p[0];
	p += 2;
	for( int s=0; s<=locals; ++s )
	{
		if ( !wr_skipString(p, symbols.end) )
		{
			return 0;
		}
	}
	return (p + 2 <= symbols.end) ? p : 0;
}

//------------------------------------------------------------------------------
// the segments after the file table, count set to their number
static const uint8_t* wr_symbolsSegments( const WRDisassemblySymbols& symbols, int& count )
{
	count = 0;
	const uint8_t* p = wr_symbolsFiles( symbols );
	if ( !p )
	{
		return 0;
	}
	int files = (int)(uint16_t)READ_16_FROM_PC( p );
	p += 2;
	for( int f=0; f<files; ++f )
	{
		if ( !wr_skipString(p, symbols.end) || p + 4 > symbols.end )
		{
			return 0;
		}
		p += 4;
	}
	if ( p + 2 > symbols.end )
	{
		return 0;
	}
	count = (int)(uint16_t)READ_16_FROM_PC( p );
	p += 2;
	if ( p + count * 10 > symbols.end )
	{
		count = 0;
		return 0;
	}
	return p;
}

//------------------------------------------------------------------------------
// "name:line" of a virtual line that lies in an included file
static void wr_appendIncludedLine( WRstr& note, const WRDisassemblySymbols& symbols, const int virtualLine )
{
	int count = 0;
	const uint8_t* segments = wr_symbolsSegments( symbols, count );
	int file = 0;
	int line = virtualLine;
	for( int s=0; s<count; ++s )
	{
		const uint8_t* seg = segments + s * 10;
		int start = (int)READ_32_FROM_PC( seg );
		if ( start > virtualLine )
		{
			break;
		}
		file = (int)(uint16_t)READ_16_FROM_PC( seg + 4 );
		line = (int)READ_32_FROM_PC( seg + 6 ) + (virtualLine - start);
	}
	if ( file == 0 )
	{
		return;
	}
	const uint8_t* p = wr_symbolsFiles( symbols ) + 2;
	for( int f=0; f<file; ++f )
	{
		if ( !wr_skipString(p, symbols.end) )
		{
			return;
		}
		p += 4;
	}
	const uint8_t* name = p;
	if ( wr_skipString(p, symbols.end) )
	{
		note.appendFormat( "%s:%d", (const char*)name, line );
	}
}

//------------------------------------------------------------------------------
static void wr_disassembleSymbols( WRstr& out, const uint8_t* base, const WRDisassemblySymbols& symbols, const uint32_t sourceHash )
{
	out.appendFormat( "Debug Symbols @0x%04X (0x%04X bytes)\n",
					  (unsigned int)(symbols.begin - base),
					  (unsigned int)(symbols.end - symbols.begin) );
	out += "-------------\n";
	out.appendFormat( "  source hash=0x%08X\n", (unsigned int)sourceHash );

	int units = (symbols.begin + 2 <= symbols.end) ? (int)(uint16_t)READ_16_FROM_PC( symbols.begin ) : 0;
	for( int u=0; u<units; ++u )
	{
		const uint8_t* entry = wr_symbolsUnit( symbols, u );
		const char* name = wr_symbolsName( symbols, u );
		if ( !entry || !name )
		{
			out += "  (truncated)\n";
			return;
		}
		out.appendFormat( "  unit %d %s args=%u locals:", u, name, (unsigned int)entry[1] );
		for( int l=0; l<(int)entry[0]; ++l )
		{
			const char* local = wr_symbolsName( symbols, u, l );
			out.appendFormat( " %s", local ? local : "?" );
		}
		out += "\n";
	}

	const uint8_t* p = wr_symbolsFiles( symbols );
	if ( p )
	{
		int files = (int)(uint16_t)READ_16_FROM_PC( p );
		p += 2;
		for( int f=0; f<files; ++f )
		{
			const uint8_t* name = p;
			if ( !wr_skipString(p, symbols.end) || p + 4 > symbols.end )
			{
				out += "  (truncated)\n";
				return;
			}
			out.appendFormat( "  file %d %s source=0x%08X bytes\n", f, (const char*)name, (unsigned int)READ_32_FROM_PC(p) );
			p += 4;
		}
		int count = 0;
		const uint8_t* seg = wr_symbolsSegments( symbols, count );
		for( int s=0; s<count; ++s, seg += 10 )
		{
			out.appendFormat( "  lines from %d: file %d line %d\n",
							  (int)READ_32_FROM_PC( seg ),
							  (int)(uint16_t)READ_16_FROM_PC( seg + 4 ),
							  (int)READ_32_FROM_PC( seg + 6 ) );
		}
	}
	out += "\n";
}

//------------------------------------------------------------------------------
// an O_DebugInfo codeword; lineHigh carries the upper bits a LineHigh word
// gives the next line marker
static void wr_decodeDebugInfo( const uint16_t codeword, const WRDisassemblySymbols& symbols, int& lineHigh, WRstr& ops, WRstr& note )
{
	ops.clear();
	note.clear();
	int payload = codeword & WRD_PayloadMask;
	switch( codeword & WRD_TypeMask )
	{
		case WRD_LineNumber:
		{
			int line = payload | lineHigh;
			lineHigh = 0;
			ops.appendFormat( "line %d", line );
			wr_appendIncludedLine( note, symbols, line );
			break;
		}

		case WRD_LineHigh:
		{
			lineHigh = payload << 14;
			ops.appendFormat( "line high bits=0x%04X", (unsigned int)payload );
			break;
		}

		case WRD_Return:
		{
			ops += "return";
			break;
		}

		default: // WRD_FunctionCall
		{
			if ( payload == WRD_ExternalFunction )
			{
				ops += "call external";
			}
			else if ( payload == WRD_GlobalStopFunction )
			{
				ops += "call global stop";
			}
			else
			{
				// the unit number: function index + 1
				ops.appendFormat( "call fnIdx=0x%02X", (unsigned int)(payload - 1) );
				const char* name = wr_symbolsName( symbols, payload );
				if ( name )
				{
					note += name;
				}
			}
			break;
		}
	}
}

//------------------------------------------------------------------------------
void wr_disassembleRange( WRstr& out,
						  const uint8_t* base,
						  const uint8_t* begin,
						  const uint8_t* end,
						  const bool includeComments,
						  const WRDisassemblySymbols& symbols )
{
	int lineHigh = 0;
	const uint8_t* pc = begin;
	while( pc < end )
	{
		const uint8_t* inst = pc;
		uint8_t op = READ_8_FROM_PC(pc++);
		if ( op >= O_LAST || op >= O_HASH_PLACEHOLDER )
		{
			out.appendFormat( "  %04X: %02X                                  <non-opcode data>\n",
							  (unsigned int)(inst - base),
							  (unsigned int)op );
			out.appendFormat( "  ....: skipping raw bytes [0x%04X..0x%04X)\n",
							  (unsigned int)(inst - base),
							  (unsigned int)(end - base) );
			break;
		}

		// GlobalStop has no operand: the VM steps over the Stop after it
		// (which the compiler always emits) only to leave its stop location
		// there
		int opLen = (op == O_GlobalStop) ? 0 : wr_operandSizeForOpcode( pc, end, op );
		if ( opLen < 0 || (pc + opLen) > end )
		{
			out.appendFormat( "  %04X: %02X                                  %-30s ; truncated\n",
							  (unsigned int)(inst - base),
							  (unsigned int)op,
							  wr_safeOpcodeName(op) );
			out.appendFormat( "  ....: skipping raw bytes [0x%04X..0x%04X)\n",
							  (unsigned int)(inst - base),
							  (unsigned int)(end - base) );
			break;
		}

		WRstr ops;
		WRstr note;
		if ( op == O_DebugInfo && opLen == 2 )
		{
			wr_decodeDebugInfo( (uint16_t)READ_16_FROM_PC(pc), symbols, lineHigh, ops, note );
		}
		else
		{
			wr_decodeOperands( op, pc, opLen, (int)(inst - base), ops, note );
		}

		out.appendFormat( "  %04X: ", (unsigned int)(inst - base) );
		wr_appendBytes( out, inst, opLen + 1 );
		out.appendFormat( "%-30s ", wr_instructionName(op, pc, end) );
		out.appendFormat( "%-44s", ops.c_str() );
		if ( includeComments && note.size() && op != O_LiteralString )
		{
			out.appendFormat( " ; %s", note.c_str() );
		}
		out += "\n";
		if ( op == O_LiteralString && note.size() )
		{
			out += "        string: \"";
			out += note;
			out += "\"\n";
		}

		pc += opLen;
	}
}

//------------------------------------------------------------------------------
// where the code of a unit that starts at 'start' ends: at the next unit, or
// at the member table of a struct (a unit 'new' refers to), which sits right
// before that struct's code
static const uint8_t* wr_unitEnd( const WRContext* context, const uint8_t* start, const uint8_t* codeEnd )
{
	const uint8_t* end = codeEnd;
	for( int i=0; i<context->numLocalFunctions; ++i )
	{
		const WRFunction* f = context->localFunctions + i;
		const uint8_t* next = context->bottom + f->functionOffset;
		if ( next > start && next < end )
		{
			end = next;
		}
		const uint8_t* table = context->bottom + f->namespaceOffset;
		if ( f->namespaceOffset && table > start && table < end )
		{
			end = table;
		}
	}
	return end;
}

//------------------------------------------------------------------------------
// the member table of a struct, data between two units: u8 members, u8
// arguments, u16 slots, each slot u32 member name hash and u8 member index
static void wr_disassembleMemberTable( WRstr& out, const WRContext* context, const int index, const uint8_t* codeEnd, const WRDisassemblySymbols& symbols )
{
	const WRFunction* f = context->localFunctions + index;
	const uint8_t* table = context->bottom + f->namespaceOffset;
	if ( table + 4 > codeEnd )
	{
		return;
	}
	unsigned int members = table[0];
	unsigned int arguments = table[1];
	unsigned int slots = (uint16_t)READ_16_FROM_PC( table + 2 );
	const char* name = wr_symbolsName( symbols, index + 1 );
	out.appendFormat( "unit %d%s%s member table @0x%04X: members=%u args=%u slots=%u (data)\n",
					  index,
					  name ? " " : "",
					  name ? name : "",
					  (unsigned int)(table - context->bottom),
					  members,
					  arguments,
					  slots );
	out.appendFormat( "  %04X: ", (unsigned int)(table - context->bottom) );
	wr_appendBytes( out, table, 4 );
	out += "\n";
	const uint8_t* slot = table + 4;
	for( unsigned int s=0; s<slots && slot + 5 <= codeEnd; ++s, slot += 5 )
	{
		out.appendFormat( "  %04X: ", (unsigned int)(slot - context->bottom) );
		wr_appendBytes( out, slot, 5 );
		uint32_t hash = (uint32_t)READ_32_FROM_PC( slot );
		if ( hash == WRENCH_NULL_HASH )
		{
			out += "empty\n";
			continue;
		}
		const char* member = wr_symbolsName( symbols, index + 1, (int)(arguments + slot[4]) );
		out.appendFormat( "hash=0x%08X -> member 0x%02X%s%s\n",
						  (unsigned int)hash,
						  (unsigned int)slot[4],
						  member ? " " : "",
						  member ? member : "" );
	}
	out += "\n";
}

//------------------------------------------------------------------------------
void wr_disassemble( const uint8_t* bytecode, const unsigned int len, WRstr& out, const bool includeComments )
{
	out.clear();

	WRState* w = wr_newState();
	if ( !w )
	{
		out = "err: wr_newState() failed\n";
		return;
	}

	WRContext* context = wr_newContext( w, bytecode, (int)len, false ); // linked, so wr_destroyState frees it
	if ( !context )
	{
		out.appendFormat( "err: context failed (%s)\n", c_errStrings[wr_getLastError(w)] );
		wr_destroyState( w );
		return;
	}

	const uint8_t* codeEnd = context->bottom + context->bottomSize - 4; // trailing CRC
	uint8_t compilerFlags = READ_8_FROM_PC( context->bottom + 2 );
	out += "Wrench Bytecode Disassembly\n";
	out += "===========================\n";
	out.appendFormat( "globals : 0x%02X\n", (unsigned int)context->globals );
	out.appendFormat( "units   : 0x%02X\n", (unsigned int)context->numLocalFunctions );
	out.appendFormat( "flags   : 0x%02X\n", (unsigned int)compilerFlags );
	out.appendFormat( "code    : [0x%04X..0x%04X)\n\n",
					  (unsigned int)(context->codeStart - context->bottom),
					  (unsigned int)(codeEnd - context->bottom) );

	// the sections between the tables and the code, as the VM skips them:
	// the names of the globals, the debug symbols, the embedded source
	const uint8_t* globalNames = context->bottom + 3 + (context->numLocalFunctions * WR_FUNCTION_CORE_SIZE);
	const uint8_t* section = globalNames;
	if ( compilerFlags & WR_INCLUDE_GLOBALS )
	{
		section += context->globals * 4;
	}
	WRDisassemblySymbols symbols;
	symbols.begin = 0;
	symbols.end = 0;
	uint32_t sourceHash = 0;
	if ( (compilerFlags & (WR_EMBED_DEBUG_CODE | WR_EMBED_SOURCE_CODE)) && section + 6 <= context->codeStart )
	{
		sourceHash = (uint32_t)READ_32_FROM_PC( section );
		uint16_t symbolsSize = (uint16_t)READ_16_FROM_PC( section + 4 );
		symbols.begin = section + 6;
		symbols.end = symbols.begin + symbolsSize;
		if ( symbols.end > context->codeStart )
		{
			symbols.end = context->codeStart;
		}
		section = symbols.end;
	}
	const uint8_t* source = ((compilerFlags & WR_EMBED_SOURCE_CODE) && section + 4 <= context->codeStart) ? section : 0;

	if ( compilerFlags & WR_INCLUDE_GLOBALS )
	{
		out += "Globals Table\n";
		out += "-------------\n";
		for( unsigned int g=0; g<context->globals; ++g )
		{
			out.appendFormat( "  g[0x%02X] hash=0x%08X", g, (unsigned int)READ_32_FROM_PC(globalNames + g * 4) );
			const char* name = wr_symbolsName( symbols, 0, (int)g );
			if ( name )
			{
				out.appendFormat( " %s", name );
			}
			out += "\n";
		}
		out += "\n";
	}

	if ( context->numLocalFunctions )
	{
		out += "Function Table\n";
		out += "--------------\n";
		out += "  idx  hash         args frame baseAdj offset\n";
		for( int i=0; i<context->numLocalFunctions; ++i )
		{
			const WRFunction* f = context->localFunctions + i;
			out.appendFormat( "  0x%02X 0x%08X 0x%02X 0x%02X  0x%02X   0x%04X",
							  i,
							  (unsigned int)f->hash,
							  (unsigned int)f->arguments,
							  (unsigned int)f->frameSpaceNeeded,
							  (unsigned int)f->frameBaseAdjustment,
							  (unsigned int)f->functionOffset );
			const char* name = wr_symbolsName( symbols, i + 1 );
			if ( name )
			{
				out.appendFormat( " %s", name );
			}
			out += "\n";
		}
		out += "\n";
	}

	if ( symbols.begin )
	{
		wr_disassembleSymbols( out, context->bottom, symbols, sourceHash );
	}
	if ( source )
	{
		out.appendFormat( "Embedded Source @0x%04X (0x%08X bytes, not listed)\n\n",
						  (unsigned int)(source + 4 - context->bottom),
						  (unsigned int)READ_32_FROM_PC(source) );
	}

	const uint8_t* globalStart = context->codeStart;
	out.appendFormat( "unit global @0x%04X\n", (unsigned int)(globalStart - context->bottom) );
	wr_disassembleRange( out, context->bottom, globalStart, wr_unitEnd(context, globalStart, codeEnd), includeComments, symbols );
	out += "\n";

	// in the order of the image: a struct's member table comes right
	// before its code
	for( const uint8_t* from = context->codeStart; ; )
	{
		int next = -1;
		const uint8_t* nextStart = codeEnd;
		for( int i=0; i<context->numLocalFunctions; ++i )
		{
			const uint8_t* start = context->bottom + context->localFunctions[i].functionOffset;
			if ( start > from && start < nextStart )
			{
				next = i;
				nextStart = start;
			}
		}
		if ( next < 0 )
		{
			break;
		}
		from = nextStart;

		const WRFunction* f = context->localFunctions + next;
		if ( f->namespaceOffset )
		{
			wr_disassembleMemberTable( out, context, next, codeEnd, symbols );
		}

		const char* name = wr_symbolsName( symbols, next + 1 );
		out.appendFormat( "unit %d%s%s hash=0x%08X args=%u frame=%u baseAdj=%u @0x%04X\n",
						  next,
						  name ? " " : "",
						  name ? name : "",
						  (unsigned int)f->hash,
						  (unsigned int)f->arguments,
						  (unsigned int)f->frameSpaceNeeded,
						  (unsigned int)f->frameBaseAdjustment,
						  (unsigned int)(nextStart - context->bottom) );

		wr_disassembleRange( out, context->bottom, nextStart, wr_unitEnd(context, nextStart, codeEnd), includeComments, symbols );
		out += "\n";
	}

	wr_destroyState( w );
}

//------------------------------------------------------------------------------
void wr_disassemble( const uint8_t* bytecode, const unsigned int len, char** out, unsigned int* outLen )
{
	WRstr listing;
	wr_disassemble( bytecode, len, listing, true );
	listing.release( out, outLen );
}

#endif
