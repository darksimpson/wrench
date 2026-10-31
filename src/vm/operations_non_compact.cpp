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

#ifndef WRENCH_COMPACT

// Define 32-bit signed integer overflow behavior explicitly as two's-complement wrap.
inline int32_t wr_iadd_wrap( const int32_t a, const int32_t b ) { return (int32_t)((uint32_t)a + (uint32_t)b); }
inline int32_t wr_isub_wrap( const int32_t a, const int32_t b ) { return (int32_t)((uint32_t)a - (uint32_t)b); }
inline int32_t wr_imul_wrap( const int32_t a, const int32_t b ) { return (int32_t)((uint32_t)a * (uint32_t)b); }

// integer operations that are defined for every input: no trap on a zero
// divisor or INT_MIN / -1, shift counts taken modulo 32 like the hardware does
inline int32_t wr_idiv( const int32_t a, const int32_t b ) { return (b == -1) ? (int32_t)(0u - (uint32_t)a) : a / b; }
inline int32_t wr_iop_mod( const int32_t a, const int32_t b ) { return (b == 0 || b == -1) ? 0 : a % b; }
inline int32_t wr_iop_shl( const int32_t a, const int32_t b ) { return (int32_t)((uint32_t)a << (b & 31)); }
inline int32_t wr_iop_shr( const int32_t a, const int32_t b ) { return a >> (b & 31); }
inline int32_t wr_iop_and( const int32_t a, const int32_t b ) { return a & b; }
inline int32_t wr_iop_or( const int32_t a, const int32_t b ) { return a | b; }
inline int32_t wr_iop_xor( const int32_t a, const int32_t b ) { return a ^ b; }

void doVoidFuncBlank( WRValue* to, WRValue* from ) {}

// the integer operators work on truncated floats, as the binary ones do;
// out of range values clamp instead of being undefined behavior
static int32_t wr_truncF( float f )
{
	if ( !(f == f) )
	{
		return 0; // NaN
	}
	if ( f >= 2147483647.0f )
	{
		return 0x7FFFFFFF;
	}
	if ( f <= -2147483648.0f )
	{
		return (int32_t)0x80000000;
	}
	return (int32_t)f;
}

#define X_LOGIC_ASSIGN( NAME, OPERATION ) \
void NAME##Assign_I_F( WRValue* to, WRValue* from ) { to->i = OPERATION( to->i, wr_truncF(from->f) ); }\
void NAME##Assign_F_I( WRValue* to, WRValue* from ) { to->i = OPERATION( wr_truncF(to->f), from->i ); to->p2 = INIT_AS_INT; }\
void NAME##Assign_F_F( WRValue* to, WRValue* from ) { to->i = OPERATION( wr_truncF(to->f), wr_truncF(from->f) ); to->p2 = INIT_AS_INT; }\
void NAME##Assign_E_F( WRValue* to, WRValue* from )\
{\
	WRValue& V = to->singleValue();\
	NAME##Assign[(V.type<<2)|WR_FLOAT]( &V, from );\
	wr_valueToEx( to, &V );\
	*from = V;\
}\
void NAME##Assign_E_I( WRValue* to, WRValue* from )\
{\
	WRValue& V = to->singleValue();\
	\
	NAME##Assign[(V.type<<2)|WR_INT]( &V, from );\
	\
	wr_valueToEx( to, &V );\
	*from = V;\
}\
void NAME##Assign_E_E( WRValue* to, WRValue* from ) \
{\
	WRValue V = from->singleValue();\
	\
	NAME##Assign[(WR_EX<<2)+V.type]( to, &V );\
	*from = to->deref();\
}\
void NAME##Assign_I_E( WRValue* to, WRValue* from )\
{\
	WRValue& V = from->singleValue();\
	NAME##Assign[(WR_INT<<2)+V.type](to, &V);\
	*from = *to;\
}\
void NAME##Assign_E_R( WRValue* to, WRValue* from ) { WRValue temp = *from->r; NAME##Assign[(WR_EX<<2)|temp.type]( to, &temp ); *from = temp; }\
void NAME##Assign_R_E( WRValue* to, WRValue* from ) { NAME##Assign[(to->r->type<<2)|WR_EX](to->r, from); }\
void NAME##Assign_R_R( WRValue* to, WRValue* from ) { WRValue temp = *from->r; NAME##Assign[(to->r->type<<2)|temp.type](to->r, &temp); *from = *to->r; }\
void NAME##Assign_R_I( WRValue* to, WRValue* from ) { NAME##Assign[(to->r->type<<2)|WR_INT](to->r, from); *from = *to->r; }\
void NAME##Assign_I_R( WRValue* to, WRValue* from ) { WRValue temp = *from->r; NAME##Assign[(WR_INT<<2)+temp.type](to, &temp); *from = *to; }\
void NAME##Assign_I_I( WRValue* to, WRValue* from ) { to->i = OPERATION( to->i, from->i ); }\
void NAME##Assign_F_R( WRValue* to, WRValue* from ) { WRValue temp = *from->r; NAME##Assign[(WR_FLOAT<<2)+temp.type](to, &temp); *from = *to; }\
void NAME##Assign_R_F( WRValue* to, WRValue* from ) { NAME##Assign[(to->r->type<<2)|WR_FLOAT](to->r, from); *from = *to->r; }\
void NAME##Assign_F_E( WRValue* to, WRValue* from )\
{\
	WRValue& V = from->singleValue();\
	NAME##Assign[(WR_FLOAT<<2)+V.type](to, &V);\
	*from = *to;\
}\
WRVoidFunc NAME##Assign[16] = \
{\
	NAME##Assign_I_I, NAME##Assign_I_F, NAME##Assign_I_R, NAME##Assign_I_E,\
	NAME##Assign_F_I, NAME##Assign_F_F, NAME##Assign_F_R, NAME##Assign_F_E,\
	NAME##Assign_R_I, NAME##Assign_R_F, NAME##Assign_R_R, NAME##Assign_R_E,\
	NAME##Assign_E_I, NAME##Assign_E_F, NAME##Assign_E_R, NAME##Assign_E_E,\
};\


X_LOGIC_ASSIGN( wr_Mod, wr_iop_mod );
X_LOGIC_ASSIGN( wr_OR, wr_iop_or );
X_LOGIC_ASSIGN( wr_AND, wr_iop_and );
X_LOGIC_ASSIGN( wr_XOR, wr_iop_xor );
X_LOGIC_ASSIGN( wr_RightShift, wr_iop_shr );
X_LOGIC_ASSIGN( wr_LeftShift, wr_iop_shl );


void wr_SubtractAssign_E_I( WRValue* to, WRValue* from )
{
	WRValue& V = to->singleValue();

	wr_SubtractAssign[(V.type<<2)|WR_INT]( &V, from );

	wr_valueToEx( to, &V );
	*from = V;
}
void wr_SubtractAssign_E_F( WRValue* to, WRValue* from )
{
	WRValue& V = to->singleValue();

	wr_SubtractAssign[(V.type<<2)|WR_FLOAT]( &V, from );

	wr_valueToEx( to, &V );
	*from = V;
}
void wr_SubtractAssign_E_E( WRValue* to, WRValue* from ) 
{
	WRValue V = from->singleValue();

	wr_SubtractAssign[(WR_EX<<2)|V.type]( to, &V );
	*from = to->deref();
}
void wr_SubtractAssign_I_E( WRValue* to, WRValue* from )
{
	WRValue& V = from->singleValue();
	wr_SubtractAssign[(WR_INT<<2)|V.type](to, &V);
	*from = *to;
}
void wr_SubtractAssign_F_E( WRValue* to, WRValue* from )
{
	WRValue& V = from->singleValue();
	wr_SubtractAssign[(WR_FLOAT<<2)|V.type](to, &V);
	*from = *to;
}
void wr_SubtractAssign_E_R( WRValue* to, WRValue* from )
{
	WRValue temp = *from->r;
	wr_SubtractAssign[(WR_EX<<2)|temp.type]( to, &temp );
	*from = temp;
}
void wr_SubtractAssign_R_E( WRValue* to, WRValue* from ) { wr_SubtractAssign[(to->r->type<<2)|WR_EX](to->r, from); *from = *to->r; } 
void wr_SubtractAssign_R_R( WRValue* to, WRValue* from ) { WRValue temp = *from->r; wr_SubtractAssign[(to->r->type<<2)|temp.type](to->r, &temp); *from = *to->r; }
void wr_SubtractAssign_R_I( WRValue* to, WRValue* from ) { wr_SubtractAssign[(to->r->type<<2)|WR_INT](to->r, from); *from = *to->r; }
void wr_SubtractAssign_R_F( WRValue* to, WRValue* from ) { wr_SubtractAssign[(to->r->type<<2)|WR_FLOAT](to->r, from); *from = *to->r; }
void wr_SubtractAssign_I_R( WRValue* to, WRValue* from ) { WRValue temp = *from->r; wr_SubtractAssign[(WR_INT<<2)+temp.type](to, &temp); *from = *to; }
void wr_SubtractAssign_F_R( WRValue* to, WRValue* from ) { WRValue temp = *from->r; wr_SubtractAssign[(WR_FLOAT<<2)+temp.type](to, &temp); *from = *to; }
void wr_SubtractAssign_F_F( WRValue* to, WRValue* from ) { to->f -= from->f; }
void wr_SubtractAssign_I_I( WRValue* to, WRValue* from ) { to->i = wr_isub_wrap( to->i, from->i ); }
void wr_SubtractAssign_I_F( WRValue* to, WRValue* from ) { to->p2 = INIT_AS_FLOAT; to->f = (float)to->i - from->f; }
void wr_SubtractAssign_F_I( WRValue* to, WRValue* from ) { to->f -= (float)from->i; }
WRVoidFunc wr_SubtractAssign[16] = 
{
	wr_SubtractAssign_I_I,  wr_SubtractAssign_I_F,  wr_SubtractAssign_I_R,  wr_SubtractAssign_I_E,
	wr_SubtractAssign_F_I,  wr_SubtractAssign_F_F,  wr_SubtractAssign_F_R,  wr_SubtractAssign_F_E,
	wr_SubtractAssign_R_I,  wr_SubtractAssign_R_F,  wr_SubtractAssign_R_R,  wr_SubtractAssign_R_E,
	wr_SubtractAssign_E_I,  wr_SubtractAssign_E_F,  wr_SubtractAssign_E_R,  wr_SubtractAssign_E_E,
};


void wr_MultiplyAssign_E_I( WRValue* to, WRValue* from )
{
	WRValue& V = to->singleValue();

	wr_MultiplyAssign[(V.type<<2)|WR_INT]( &V, from );

	wr_valueToEx( to, &V );
	*from = V;
}
void wr_MultiplyAssign_E_F( WRValue* to, WRValue* from )
{
	WRValue& V = to->singleValue();

	wr_MultiplyAssign[(V.type<<2)|WR_FLOAT]( &V, from );

	wr_valueToEx( to, &V );
	*from = V;
}
void wr_MultiplyAssign_E_E( WRValue* to, WRValue* from ) 
{
	WRValue V = from->singleValue();

	wr_MultiplyAssign[(WR_EX<<2)|V.type]( to, &V );
	*from = to->deref();
}
void wr_MultiplyAssign_I_E( WRValue* to, WRValue* from )
{
	WRValue& V = from->singleValue();
	wr_MultiplyAssign[(WR_INT<<2)|V.type](to, &V);
	*from = *to;
}
void wr_MultiplyAssign_F_E( WRValue* to, WRValue* from )
{
	WRValue& V = from->singleValue();
	wr_MultiplyAssign[(WR_FLOAT<<2)|V.type](to, &V);
	*from = *to;
}
void wr_MultiplyAssign_E_R( WRValue* to, WRValue* from )
{
	WRValue temp = *from->r;
	wr_MultiplyAssign[(WR_EX<<2)|temp.type]( to, &temp );
	*from = temp;
}
void wr_MultiplyAssign_R_E( WRValue* to, WRValue* from ) { wr_MultiplyAssign[(to->r->type<<2)|WR_EX](to->r, from); *from = *to->r; } 
void wr_MultiplyAssign_R_R( WRValue* to, WRValue* from ) { WRValue temp = *from->r; wr_MultiplyAssign[(to->r->type<<2)|temp.type](to->r, &temp); *from = *to->r; }
void wr_MultiplyAssign_R_I( WRValue* to, WRValue* from ) { wr_MultiplyAssign[(to->r->type<<2)|WR_INT](to->r, from); *from = *to->r; }
void wr_MultiplyAssign_R_F( WRValue* to, WRValue* from ) { wr_MultiplyAssign[(to->r->type<<2)|WR_FLOAT](to->r, from); *from = *to->r; }
void wr_MultiplyAssign_I_R( WRValue* to, WRValue* from ) { WRValue temp = *from->r; wr_MultiplyAssign[(WR_INT<<2)+temp.type](to, &temp); *from = *to; }
void wr_MultiplyAssign_F_R( WRValue* to, WRValue* from ) { WRValue temp = *from->r; wr_MultiplyAssign[(WR_FLOAT<<2)+temp.type](to, &temp); *from = *to; }
void wr_MultiplyAssign_F_F( WRValue* to, WRValue* from ) { to->f *= from->f; }
void wr_MultiplyAssign_I_I( WRValue* to, WRValue* from ) { to->ui = to->ui * from->ui; } // unsigned: wraps without undefined behavior
void wr_MultiplyAssign_I_F( WRValue* to, WRValue* from ) { to->p2 = INIT_AS_FLOAT; to->f = (float)to->i * from->f; }
void wr_MultiplyAssign_F_I( WRValue* to, WRValue* from ) { to->f *= (float)from->i; }
WRVoidFunc wr_MultiplyAssign[16] = 
{
	wr_MultiplyAssign_I_I,  wr_MultiplyAssign_I_F,  wr_MultiplyAssign_I_R,  wr_MultiplyAssign_I_E,
	wr_MultiplyAssign_F_I,  wr_MultiplyAssign_F_F,  wr_MultiplyAssign_F_R,  wr_MultiplyAssign_F_E,
	wr_MultiplyAssign_R_I,  wr_MultiplyAssign_R_F,  wr_MultiplyAssign_R_R,  wr_MultiplyAssign_R_E,
	wr_MultiplyAssign_E_I,  wr_MultiplyAssign_E_F,  wr_MultiplyAssign_E_R,  wr_MultiplyAssign_E_E,
};


void wr_DivideAssign_E_I( WRValue* to, WRValue* from )
{
	WRValue& V = to->singleValue();

	wr_DivideAssign[(V.type<<2)|WR_INT]( &V, from );

	wr_valueToEx( to, &V );
	*from = V;
}
void wr_DivideAssign_E_F( WRValue* to, WRValue* from )
{
	WRValue& V = to->singleValue();

	wr_DivideAssign[(V.type<<2)|WR_FLOAT]( &V, from );

	wr_valueToEx( to, &V );
	*from = V;
}
void wr_DivideAssign_E_E( WRValue* to, WRValue* from ) 
{
	WRValue V = from->singleValue();

	wr_DivideAssign[(WR_EX<<2)|V.type]( to, &V );
	*from = to->deref();
}
void wr_DivideAssign_I_E( WRValue* to, WRValue* from )
{
	WRValue& V = from->singleValue();
	wr_DivideAssign[(WR_INT<<2)|V.type](to, &V);
	*from = *to;
}
void wr_DivideAssign_F_E( WRValue* to, WRValue* from )
{
	WRValue& V = from->singleValue();
	wr_DivideAssign[(WR_FLOAT<<2)|V.type](to, &V);
	*from = *to;
}
void wr_DivideAssign_E_R( WRValue* to, WRValue* from )
{
	WRValue temp = *from->r;
	wr_DivideAssign[(WR_EX<<2)|temp.type]( to, &temp );
	*from = temp;
}
void wr_DivideAssign_R_E( WRValue* to, WRValue* from ) { wr_DivideAssign[(to->r->type<<2)|WR_EX](to->r, from); *from = *to->r; } 
void wr_DivideAssign_R_R( WRValue* to, WRValue* from ) { WRValue temp = *from->r; wr_DivideAssign[(to->r->type<<2)|temp.type](to->r, &temp); *from = *to->r; }
void wr_DivideAssign_R_I( WRValue* to, WRValue* from ) { wr_DivideAssign[(to->r->type<<2)|WR_INT](to->r, from); *from = *to->r; }
void wr_DivideAssign_R_F( WRValue* to, WRValue* from ) { wr_DivideAssign[(to->r->type<<2)|WR_FLOAT](to->r, from); *from = *to->r; }
void wr_DivideAssign_I_R( WRValue* to, WRValue* from ) { WRValue temp = *from->r; wr_DivideAssign[(WR_INT<<2)+temp.type](to, &temp); *from = *to; }
void wr_DivideAssign_F_R( WRValue* to, WRValue* from ) { WRValue temp = *from->r; wr_DivideAssign[(WR_FLOAT<<2)+temp.type](to, &temp); *from = *to; }

void wr_DivideAssign_F_F( WRValue* to, WRValue* from )
{
	if ( from->f )
	{
		to->f = to->f / from->f;
	}
	else
	{
		to->i = 0; // with WRENCH_TRAP_DIVISION_BY_ZERO the VM stops before
	}

}
void wr_DivideAssign_I_I( WRValue* to, WRValue* from )
{
	if ( from->i )
	{
		to->i = wr_idiv( to->i, from->i );
	}
	else
	{
		to->i = 0; // with WRENCH_TRAP_DIVISION_BY_ZERO the VM stops before
	}
}
void wr_DivideAssign_I_F( WRValue* to, WRValue* from )
{
	to->p2 = INIT_AS_FLOAT;
	if ( from->f )
	{
		to->f = (float)to->i / from->f;
	}
	else
	{
		to->i = 0; // with WRENCH_TRAP_DIVISION_BY_ZERO the VM stops before
	}
}
void wr_DivideAssign_F_I( WRValue* to, WRValue* from )
{
	if ( from->i )
	{
		to->f = to->f / (float)from->i;
	}
	else
	{
		to->i = 0; // with WRENCH_TRAP_DIVISION_BY_ZERO the VM stops before
	}
}

WRVoidFunc wr_DivideAssign[16] = 
{
	wr_DivideAssign_I_I,  wr_DivideAssign_I_F,  wr_DivideAssign_I_R,  wr_DivideAssign_I_E,
	wr_DivideAssign_F_I,  wr_DivideAssign_F_F,  wr_DivideAssign_F_R,  wr_DivideAssign_F_E,
	wr_DivideAssign_R_I,  wr_DivideAssign_R_F,  wr_DivideAssign_R_R,  wr_DivideAssign_R_E,
	wr_DivideAssign_E_I,  wr_DivideAssign_E_F,  wr_DivideAssign_E_R,  wr_DivideAssign_E_E,
};


void wr_AddAssign_E_I( WRValue* to, WRValue* from )
{
	if ( !wr_concatStringToElement(to, from) && !wr_concatStringCheck( to, from, to) )
	{
		WRValue& V = to->singleValue();
	
		wr_AddAssign[(V.type<<2)|WR_INT]( &V, from );
	
		wr_valueToEx( to, &V );
		*from = V;
	}
}

void wr_AddAssign_E_F( WRValue* to, WRValue* from )
{
	if ( !wr_concatStringToElement(to, from) && !wr_concatStringCheck( to, from, to) )
	{
		WRValue& V = to->singleValue();

		wr_AddAssign[(V.type<<2)|WR_FLOAT]( &V, from );
		
		wr_valueToEx( to, &V );
		*from = V;
	}
}
void wr_AddAssign_E_E( WRValue* to, WRValue* from ) 
{
	if ( IS_CONTAINER_MEMBER(from->xtype) )
	{
		WRValue V = from->deref();

		wr_AddAssign[(WR_EX<<2)|V.type]( to, &V );
		*from = to->deref();
	}
	else if ( !wr_concatStringToElement(to, from) )
	{
		wr_concatStringCheck( to, from, to );
	}
}
void wr_AddAssign_I_E( WRValue* to, WRValue* from )
{
	if ( !wr_concatStringCheck( to, from, to) )
	{
		WRValue& V = from->singleValue();
		wr_AddAssign[(WR_INT<<2)|V.type](to, &V);
		*from = *to;
	}
}
void wr_AddAssign_F_E( WRValue* to, WRValue* from )
{
	if ( !wr_concatStringCheck( to, from, to) )
	{
		WRValue& V = from->singleValue();
		wr_AddAssign[(WR_FLOAT<<2)|V.type](to, &V);
		*from = *to;
	}
}
void wr_AddAssign_E_R( WRValue* to, WRValue* from )
{
	WRValue temp = *from->r;
	wr_AddAssign[(WR_EX<<2)|temp.type]( to, &temp );
	*from = temp;
}
void wr_AddAssign_R_E( WRValue* to, WRValue* from ) { wr_AddAssign[(to->r->type<<2)|WR_EX](to->r, from); *from = *to->r; }
void wr_AddAssign_R_R( WRValue* to, WRValue* from ) { WRValue temp = *from->r; wr_AddAssign[(to->r->type<<2)|temp.type](to->r, &temp); *from = *to->r; }
void wr_AddAssign_R_I( WRValue* to, WRValue* from )
{
	WRValue* lhs = to->r;
	if ( lhs->type == WR_INT )
	{
		lhs->i = wr_iadd_wrap( lhs->i, from->i );
		*from = *lhs;
	}
	else
	{
		wr_AddAssign[(lhs->type<<2)|WR_INT](lhs, from);
		*from = *lhs;
	}
}
void wr_AddAssign_R_F( WRValue* to, WRValue* from ) { wr_AddAssign[(to->r->type<<2)|WR_FLOAT](to->r, from); *from = *to->r; }
void wr_AddAssign_I_R( WRValue* to, WRValue* from ) { WRValue temp = *from->r; wr_AddAssign[(WR_INT<<2)+temp.type](to, &temp); *from = *to; }
void wr_AddAssign_F_R( WRValue* to, WRValue* from ) { WRValue temp = *from->r; wr_AddAssign[(WR_FLOAT<<2)+temp.type](to, &temp); *from = *to; }
void wr_AddAssign_F_F( WRValue* to, WRValue* from ) { to->f += from->f; }
void wr_AddAssign_I_I( WRValue* to, WRValue* from ) { to->i = wr_iadd_wrap( to->i, from->i ); }
void wr_AddAssign_I_F( WRValue* to, WRValue* from ) { to->p2 = INIT_AS_FLOAT; to->f = (float)to->i + from->f; }
void wr_AddAssign_F_I( WRValue* to, WRValue* from ) { to->f += (float)from->i; }
WRVoidFunc wr_AddAssign[16] = 
{
	wr_AddAssign_I_I,  wr_AddAssign_I_F,  wr_AddAssign_I_R,  wr_AddAssign_I_E,
	wr_AddAssign_F_I,  wr_AddAssign_F_F,  wr_AddAssign_F_R,  wr_AddAssign_F_E,
	wr_AddAssign_R_I,  wr_AddAssign_R_F,  wr_AddAssign_R_R,  wr_AddAssign_R_E,
	wr_AddAssign_E_I,  wr_AddAssign_E_F,  wr_AddAssign_E_R,  wr_AddAssign_E_E,
};


//------------------------------------------------------------------------------
#define X_BINARY( NAME, OPERATION, I_I_OP ) \
void NAME##Binary_E_I( WRValue* to, WRValue* from, WRValue* target )\
{\
	WRValue& V = to->singleValue();\
	NAME##Binary[(V.type<<2)|WR_INT](&V, from, target);\
}\
void NAME##Binary_E_F( WRValue* to, WRValue* from, WRValue* target )\
{\
	WRValue& V = to->singleValue();\
	NAME##Binary[(V.type<<2)|WR_FLOAT](&V, from, target);\
}\
void NAME##Binary_E_E( WRValue* to, WRValue* from, WRValue* target )\
{\
	WRValue V1 = to->singleValue();\
	WRValue& V2 = from->singleValue();\
	NAME##Binary[(V1.type<<2)|V2.type](&V1, &V2, target);\
}\
void NAME##Binary_I_E( WRValue* to, WRValue* from, WRValue* target )\
{\
	WRValue& V = from->singleValue();\
	NAME##Binary[(WR_INT<<2)|V.type](to, &V, target);\
}\
void NAME##Binary_F_E( WRValue* to, WRValue* from, WRValue* target )\
{\
	WRValue& V = from->singleValue();\
	NAME##Binary[(WR_FLOAT<<2)|V.type](to, &V, target);\
}\
void NAME##Binary_R_E( WRValue* to, WRValue* from, WRValue* target ) { NAME##Binary[(to->r->type<<2)|WR_EX]( to->r, from, target); }\
void NAME##Binary_E_R( WRValue* to, WRValue* from, WRValue* target ) { NAME##Binary[(WR_EX<<2)+from->r->type](to, from->r, target); }\
void NAME##Binary_I_R( WRValue* to, WRValue* from, WRValue* target ) { WRValue* rhs = from->r; if ( rhs->type == WR_INT ) { target->p2 = INIT_AS_INT; target->i = I_I_OP( to->i, rhs->i ); } else { NAME##Binary[(WR_INT<<2)+rhs->type](to, rhs, target); } }\
void NAME##Binary_R_F( WRValue* to, WRValue* from, WRValue* target ) { NAME##Binary[(to->r->type<<2)|WR_FLOAT](to->r, from, target); }\
void NAME##Binary_R_R( WRValue* to, WRValue* from, WRValue* target ) { NAME##Binary[(to->r->type<<2)|from->r->type](to->r, from->r, target); }\
void NAME##Binary_R_I( WRValue* to, WRValue* from, WRValue* target ) { WRValue* lhs = to->r; if ( lhs->type == WR_INT ) { target->p2 = INIT_AS_INT; target->i = I_I_OP( lhs->i, from->i ); } else { NAME##Binary[(lhs->type<<2)|WR_INT](lhs, from, target); } }\
void NAME##Binary_F_R( WRValue* to, WRValue* from, WRValue* target ) { NAME##Binary[(WR_FLOAT<<2)+from->r->type](to, from->r, target); }\
void NAME##Binary_I_I( WRValue* to, WRValue* from, WRValue* target ) { target->p2 = INIT_AS_INT; target->i = I_I_OP( to->i, from->i ); }\
void NAME##Binary_I_F( WRValue* to, WRValue* from, WRValue* target ) { target->p2 = INIT_AS_FLOAT; target->f = (float)to->i OPERATION from->f; }\
void NAME##Binary_F_I( WRValue* to, WRValue* from, WRValue* target ) { target->p2 = INIT_AS_FLOAT; target->f = to->f OPERATION (float)from->i; }\
void NAME##Binary_F_F( WRValue* to, WRValue* from, WRValue* target ) { target->p2 = INIT_AS_FLOAT; target->f = to->f OPERATION from->f; }\
WRTargetFunc NAME##Binary[16] = \
{\
	NAME##Binary_I_I,  NAME##Binary_I_F,  NAME##Binary_I_R,  NAME##Binary_I_E,\
	NAME##Binary_F_I,  NAME##Binary_F_F,  NAME##Binary_F_R,  NAME##Binary_F_E,\
	NAME##Binary_R_I,  NAME##Binary_R_F,  NAME##Binary_R_R,  NAME##Binary_R_E,\
	NAME##Binary_E_I,  NAME##Binary_E_F,  NAME##Binary_E_R,  NAME##Binary_E_E,\
};\

//X_BINARY( wr_Addition, +, wr_iadd_wrap );  -- broken out so strings work
X_BINARY( wr_Multiply, *, wr_imul_wrap );
X_BINARY( wr_Subtract, -, wr_isub_wrap );
//X_BINARY( wr_Divide, /, wr_idiv_wrap ); -- broken out for divide-by-zero

void wr_DivideBinary_E_I( WRValue* to, WRValue* from, WRValue* target )
{
	WRValue& V = to->singleValue();
	wr_DivideBinary[(V.type<<2)|WR_INT](&V, from, target);
}
void wr_DivideBinary_E_F( WRValue* to, WRValue* from, WRValue* target )
{
	WRValue& V = to->singleValue();
	wr_DivideBinary[(V.type<<2)|WR_FLOAT](&V, from, target);
}
void wr_DivideBinary_E_E( WRValue* to, WRValue* from, WRValue* target )
{
	WRValue V1 = to->singleValue();
	WRValue& V2 = from->singleValue();
	wr_DivideBinary[(V1.type<<2)|V2.type](&V1, &V2, target);
}
void wr_DivideBinary_I_E( WRValue* to, WRValue* from, WRValue* target )
{
	WRValue& V = from->singleValue();
	wr_DivideBinary[(WR_INT<<2)|V.type](to, &V, target);
}
void wr_DivideBinary_F_E( WRValue* to, WRValue* from, WRValue* target )
{
	WRValue& V = from->singleValue();
	wr_DivideBinary[(WR_FLOAT<<2)|V.type](to, &V, target);
}
void wr_DivideBinary_R_E( WRValue* to, WRValue* from, WRValue* target ) { wr_DivideBinary[(to->r->type<<2)|WR_EX]( to->r, from, target); }
void wr_DivideBinary_E_R( WRValue* to, WRValue* from, WRValue* target ) { wr_DivideBinary[(WR_EX<<2)+from->r->type](to, from->r, target); }
void wr_DivideBinary_I_R( WRValue* to, WRValue* from, WRValue* target ) { wr_DivideBinary[(WR_INT<<2)+from->r->type](to, from->r, target); }
void wr_DivideBinary_R_F( WRValue* to, WRValue* from, WRValue* target ) { wr_DivideBinary[(to->r->type<<2)|WR_FLOAT](to->r, from, target); }
void wr_DivideBinary_R_R( WRValue* to, WRValue* from, WRValue* target ) { wr_DivideBinary[(to->r->type<<2)|from->r->type](to->r, from->r, target); }
void wr_DivideBinary_R_I( WRValue* to, WRValue* from, WRValue* target ) { wr_DivideBinary[(to->r->type<<2)|WR_INT](to->r, from, target); }
void wr_DivideBinary_F_R( WRValue* to, WRValue* from, WRValue* target ) { wr_DivideBinary[(WR_FLOAT<<2)+from->r->type](to, from->r, target); }
void wr_DivideBinary_I_I( WRValue* to, WRValue* from, WRValue* target )
{
	target->p2 = INIT_AS_INT;
	if ( from->i )
	{
		target->i = wr_idiv( to->i, from->i );
	}
	else
	{
		target->i = 0; // with WRENCH_TRAP_DIVISION_BY_ZERO the VM stops before
	}
}
void wr_DivideBinary_I_F( WRValue* to, WRValue* from, WRValue* target )
{
	target->p2 = INIT_AS_FLOAT;
	if ( from->f )
	{
		target->f = (float)to->i / from->f;
	}
	else
	{
		target->i = 0; // with WRENCH_TRAP_DIVISION_BY_ZERO the VM stops before
	}
}
void wr_DivideBinary_F_I( WRValue* to, WRValue* from, WRValue* target )
{
	target->p2 = INIT_AS_FLOAT;
	if ( from->i )
	{
		target->f = to->f / (float)from->i;
	}
	else
	{
		target->i = 0; // with WRENCH_TRAP_DIVISION_BY_ZERO the VM stops before
	}
}
void wr_DivideBinary_F_F( WRValue* to, WRValue* from, WRValue* target )
{
	target->p2 = INIT_AS_FLOAT;
	if ( from->f )
	{
		target->f = to->f / from->f;
	}
	else
	{
		target->i = 0; // with WRENCH_TRAP_DIVISION_BY_ZERO the VM stops before
	}
}

WRTargetFunc wr_DivideBinary[16] = 
{
	wr_DivideBinary_I_I,  wr_DivideBinary_I_F,  wr_DivideBinary_I_R,  wr_DivideBinary_I_E,
	wr_DivideBinary_F_I,  wr_DivideBinary_F_F,  wr_DivideBinary_F_R,  wr_DivideBinary_F_E,
	wr_DivideBinary_R_I,  wr_DivideBinary_R_F,  wr_DivideBinary_R_R,  wr_DivideBinary_R_E,
	wr_DivideBinary_E_I,  wr_DivideBinary_E_F,  wr_DivideBinary_E_R,  wr_DivideBinary_E_E,
};


void wr_AdditionBinary_E_I( WRValue* to, WRValue* from, WRValue* target )
{
	if ( !wr_concatStringCheck(to, from, target) )
	{
		WRValue& V = to->singleValue();
		wr_AdditionBinary[(V.type<<2)|WR_INT](&V, from, target);
	}
}

void wr_AdditionBinary_I_E(WRValue* to, WRValue* from, WRValue* target)
{
	if ( !wr_concatStringCheck(to, from, target) )
	{
		WRValue& V = from->singleValue();
		wr_AdditionBinary[(WR_INT << 2) | V.type](to, &V, target);
	}
}

void wr_AdditionBinary_E_F( WRValue* to, WRValue* from, WRValue* target )
{
	if ( !wr_concatStringCheck(to, from, target) )
	{
		WRValue& V = to->singleValue();
		wr_AdditionBinary[(V.type<<2)|WR_FLOAT](&V, from, target);
	}
}
void wr_AdditionBinary_E_E( WRValue* to, WRValue* from, WRValue* target )
{
	if ( IS_CONTAINER_MEMBER(to->xtype) || IS_CONTAINER_MEMBER(from->xtype) )
	{
		WRValue V1 = to->deref();
		WRValue& V2 = from->deref();
		wr_AdditionBinary[(V1.type<<2)|V2.type](&V1, &V2, target);
	}
	else if ( !wr_concatStringCheck(to, from, target) )
	{
		// table + table and the like: no result, but the target slot must
		// not keep whatever stale value it held
		target->init();
	}
}
void wr_AdditionBinary_F_E( WRValue* to, WRValue* from, WRValue* target )
{
	if ( !wr_concatStringCheck(to, from, target) )
	{
		WRValue& V = from->singleValue();
		wr_AdditionBinary[(WR_FLOAT<<2)|V.type](to, &V, target);
	}
}
void wr_AdditionBinary_R_E( WRValue* to, WRValue* from, WRValue* target ) { wr_AdditionBinary[(to->r->type<<2)|WR_EX]( to->r, from, target); }
void wr_AdditionBinary_E_R( WRValue* to, WRValue* from, WRValue* target ) { wr_AdditionBinary[(WR_EX<<2)+from->r->type](to, from->r, target); }
void wr_AdditionBinary_I_R( WRValue* to, WRValue* from, WRValue* target ) { wr_AdditionBinary[(WR_INT<<2)+from->r->type](to, from->r, target); }
void wr_AdditionBinary_R_F( WRValue* to, WRValue* from, WRValue* target ) { wr_AdditionBinary[(to->r->type<<2)|WR_FLOAT](to->r, from, target); }
void wr_AdditionBinary_R_R( WRValue* to, WRValue* from, WRValue* target ) { wr_AdditionBinary[(to->r->type<<2)|from->r->type](to->r, from->r, target); }
void wr_AdditionBinary_R_I( WRValue* to, WRValue* from, WRValue* target ) { wr_AdditionBinary[(to->r->type<<2)|WR_INT](to->r, from, target); }
void wr_AdditionBinary_F_R( WRValue* to, WRValue* from, WRValue* target ) { wr_AdditionBinary[(WR_FLOAT<<2)+from->r->type](to, from->r, target); }
void wr_AdditionBinary_I_I( WRValue* to, WRValue* from, WRValue* target ) { target->p2 = INIT_AS_INT; target->i = wr_iadd_wrap( to->i, from->i ); }
void wr_AdditionBinary_I_F( WRValue* to, WRValue* from, WRValue* target ) { target->p2 = INIT_AS_FLOAT; target->f = (float)to->i + from->f; }
void wr_AdditionBinary_F_I( WRValue* to, WRValue* from, WRValue* target ) { target->p2 = INIT_AS_FLOAT; target->f = to->f + (float)from->i; }
void wr_AdditionBinary_F_F( WRValue* to, WRValue* from, WRValue* target ) { target->p2 = INIT_AS_FLOAT; target->f = to->f + from->f; }
WRTargetFunc wr_AdditionBinary[16] = 
{
	wr_AdditionBinary_I_I,  wr_AdditionBinary_I_F,  wr_AdditionBinary_I_R,  wr_AdditionBinary_I_E,
	wr_AdditionBinary_F_I,  wr_AdditionBinary_F_F,  wr_AdditionBinary_F_R,  wr_AdditionBinary_F_E,
	wr_AdditionBinary_R_I,  wr_AdditionBinary_R_F,  wr_AdditionBinary_R_R,  wr_AdditionBinary_R_E,
	wr_AdditionBinary_E_I,  wr_AdditionBinary_E_F,  wr_AdditionBinary_E_R,  wr_AdditionBinary_E_E,
};


void doTargetFuncBlank( WRValue* to, WRValue* from, WRValue* target ) {}

//------------------------------------------------------------------------------
#define X_INT_BINARY( NAME, OPERATION ) \
void NAME##Binary_E_I( WRValue* to, WRValue* from, WRValue* target )\
{\
	WRValue& V = to->singleValue();\
	NAME##Binary[(V.type<<2)|WR_INT](&V, from, target);\
}\
void NAME##Binary_E_E( WRValue* to, WRValue* from, WRValue* target )\
{\
	WRValue V1 = to->singleValue();\
	WRValue& V2 = from->singleValue();\
	NAME##Binary[(V1.type<<2)|V2.type](&V1, &V2, target);\
}\
void NAME##Binary_I_E( WRValue* to, WRValue* from, WRValue* target )\
{\
	WRValue& V = from->singleValue();\
	NAME##Binary[(WR_INT<<2)+V.type](to, &V, target);\
}\
void NAME##Binary_E_R( WRValue* to, WRValue* from, WRValue* target ) { NAME##Binary[(WR_EX<<2)|from->r->type](to, from->r, target); }\
void NAME##Binary_R_E( WRValue* to, WRValue* from, WRValue* target ) { NAME##Binary[(to->r->type<<2)|WR_EX]( to->r, from, target); }\
void NAME##Binary_I_R( WRValue* to, WRValue* from, WRValue* target ) { WRValue* rhs = from->r; if ( rhs->type == WR_INT ) { target->p2 = INIT_AS_INT; target->i = OPERATION( to->i, rhs->i ); } else { NAME##Binary[(WR_INT<<2)+rhs->type](to, rhs, target); } }\
void NAME##Binary_R_R( WRValue* to, WRValue* from, WRValue* target ) { NAME##Binary[(to->r->type<<2)|from->r->type](to->r, from->r, target); }\
void NAME##Binary_R_I( WRValue* to, WRValue* from, WRValue* target ) { WRValue* lhs = to->r; if ( lhs->type == WR_INT ) { target->p2 = INIT_AS_INT; target->i = OPERATION( lhs->i, from->i ); } else { NAME##Binary[(lhs->type<<2)|WR_INT](lhs, from, target); } }\
void NAME##Binary_I_I( WRValue* to, WRValue* from, WRValue* target ) { target->p2 = INIT_AS_INT; target->i = OPERATION( to->i, from->i ); }\
void NAME##Binary_N_N( WRValue* to, WRValue* from, WRValue* target ) { int32_t x = to->asInt(); int32_t y = from->asInt(); target->p2 = INIT_AS_INT; target->i = OPERATION( x, y ); }\
WRTargetFunc NAME##Binary[16] = \
{\
	NAME##Binary_I_I, NAME##Binary_N_N, NAME##Binary_I_R, NAME##Binary_I_E,\
	NAME##Binary_N_N, NAME##Binary_N_N, NAME##Binary_N_N, NAME##Binary_N_N,\
	NAME##Binary_R_I, NAME##Binary_N_N, NAME##Binary_R_R, NAME##Binary_R_E,\
	NAME##Binary_E_I, NAME##Binary_N_N, NAME##Binary_E_R, NAME##Binary_E_E,\
};\

X_INT_BINARY( wr_LeftShift, wr_iop_shl );
X_INT_BINARY( wr_RightShift, wr_iop_shr );
X_INT_BINARY( wr_Mod, wr_iop_mod );
X_INT_BINARY( wr_AND, wr_iop_and );
X_INT_BINARY( wr_OR, wr_iop_or );
X_INT_BINARY( wr_XOR, wr_iop_xor );



// ORDERS: two strings compare by their bytes
#define X_COMPARE( NAME, OPERATION, ORDERS ) \
bool NAME##_E_E( WRValue* to, WRValue* from )\
{\
	int order;\
	if ( ORDERS && wr_stringOrder(to, from, &order) )\
	{\
		return order OPERATION 0;\
	}\
	WRValue V1 = to->singleValue();\
	WRValue& V2 = from->singleValue();\
	return NAME[(V1.type<<2)|V2.type](&V1, &V2);\
}\
bool NAME##_E_I( WRValue* to, WRValue* from )\
{\
	WRValue& V = to->singleValue();\
	return NAME[(V.type<<2)|WR_INT](&V, from);\
}\
bool NAME##_E_F( WRValue* to, WRValue* from )\
{\
	WRValue& V = to->singleValue();\
	return NAME[(V.type<<2)|WR_FLOAT](&V, from);\
}\
bool NAME##_I_E( WRValue* to, WRValue* from )\
{\
	WRValue& V = from->singleValue();\
	return NAME[(WR_INT<<2)|V.type](to, &V);\
}\
bool NAME##_F_E( WRValue* to, WRValue* from )\
{\
	WRValue& V = from->singleValue();\
	return NAME[(WR_FLOAT<<2)|V.type](to, &V);\
}\
bool NAME##_R_E( WRValue* to, WRValue* from ) { return NAME[(to->r->type<<2)|WR_EX](to->r, from); }\
bool NAME##_E_R( WRValue* to, WRValue* from ) { return NAME[(WR_EX<<2)|from->r->type](to, from->r); }\
bool NAME##_R_R( WRValue* to, WRValue* from ) { return NAME[(to->r->type<<2)|from->r->type](to->r, from->r); }\
bool NAME##_R_I( WRValue* to, WRValue* from ) { return NAME[(to->r->type<<2)|WR_INT](to->r, from); }\
bool NAME##_R_F( WRValue* to, WRValue* from ) { return NAME[(to->r->type<<2)|WR_FLOAT](to->r, from); }\
bool NAME##_I_R( WRValue* to, WRValue* from ) { return NAME[(WR_INT<<2)+from->r->type](to, from->r); }\
bool NAME##_F_R( WRValue* to, WRValue* from ) { return NAME[(WR_FLOAT<<2)+from->r->type](to, from->r); }\
bool NAME##_I_I( WRValue* to, WRValue* from ) { return to->i OPERATION from->i; }\
bool NAME##_I_F( WRValue* to, WRValue* from ) { return (float)to->i OPERATION from->f; }\
bool NAME##_F_I( WRValue* to, WRValue* from ) { return to->f OPERATION (float)from->i; }\
bool NAME##_F_F( WRValue* to, WRValue* from ) { return to->f OPERATION from->f; }\
WRReturnFunc NAME[16] = \
{\
	NAME##_I_I, NAME##_I_F, NAME##_I_R, NAME##_I_E,\
	NAME##_F_I, NAME##_F_F, NAME##_F_R, NAME##_F_E,\
	NAME##_R_I, NAME##_R_F, NAME##_R_R, NAME##_R_E,\
	NAME##_E_I, NAME##_E_F, NAME##_E_R, NAME##_E_E,\
};\

X_COMPARE( wr_CompareGT, >, true );
X_COMPARE( wr_LogicalAND, &&, false );
X_COMPARE( wr_LogicalOR, ||, false );











//X_COMPARE( wr_CompareLT, < );

bool wr_CompareLT_E_E( WRValue* to, WRValue* from )
{
	int order;
	if ( wr_stringOrder(to, from, &order) )
	{
		return order < 0;
	}
	WRValue V1 = to->singleValue();
	WRValue& V2 = from->singleValue();
	return wr_CompareLT[(V1.type<<2)|V2.type](&V1, &V2);
}
bool wr_CompareLT_E_I( WRValue* to, WRValue* from )
{
	WRValue& V = to->singleValue();
	return wr_CompareLT[(V.type<<2)|WR_INT](&V, from);
}
bool wr_CompareLT_E_F( WRValue* to, WRValue* from )
{
	WRValue& V = to->singleValue();
	return wr_CompareLT[(V.type<<2)|WR_FLOAT](&V, from);
}
bool wr_CompareLT_I_E( WRValue* to, WRValue* from )
{
	WRValue& V = from->singleValue();
	return wr_CompareLT[(WR_INT<<2)|V.type](to, &V);
}
bool wr_CompareLT_F_E( WRValue* to, WRValue* from )
{
	WRValue& V = from->singleValue();
	return wr_CompareLT[(WR_FLOAT<<2)|V.type](to, &V);
}
bool wr_CompareLT_R_E( WRValue* to, WRValue* from ) { return wr_CompareLT[(to->r->type<<2)|WR_EX](to->r, from); }
bool wr_CompareLT_E_R( WRValue* to, WRValue* from ) { return wr_CompareLT[(WR_EX<<2)|from->r->type](to, from->r); }
bool wr_CompareLT_R_R( WRValue* to, WRValue* from ) { return wr_CompareLT[(to->r->type<<2)|from->r->type](to->r, from->r); }
bool wr_CompareLT_R_I( WRValue* to, WRValue* from ) { return wr_CompareLT[(to->r->type<<2)|WR_INT](to->r, from); }
bool wr_CompareLT_R_F( WRValue* to, WRValue* from ) { return wr_CompareLT[(to->r->type<<2)|WR_FLOAT](to->r, from); }
bool wr_CompareLT_I_R( WRValue* to, WRValue* from ) { return wr_CompareLT[(WR_INT<<2)+from->r->type](to, from->r); }
bool wr_CompareLT_F_R( WRValue* to, WRValue* from ) { return wr_CompareLT[(WR_FLOAT<<2)+from->r->type](to, from->r); }
bool wr_CompareLT_I_I( WRValue* to, WRValue* from ) { return to->i < from->i; }
bool wr_CompareLT_I_F( WRValue* to, WRValue* from ) { return (float)to->i < from->f; }
bool wr_CompareLT_F_I( WRValue* to, WRValue* from ) { return to->f < (float)from->i; }
bool wr_CompareLT_F_F( WRValue* to, WRValue* from ) { return to->f < from->f; }
WRReturnFunc wr_CompareLT[16] = 
{
	wr_CompareLT_I_I, wr_CompareLT_I_F, wr_CompareLT_I_R, wr_CompareLT_I_E,
	wr_CompareLT_F_I, wr_CompareLT_F_F, wr_CompareLT_F_R, wr_CompareLT_F_E,
	wr_CompareLT_R_I, wr_CompareLT_R_F, wr_CompareLT_R_R, wr_CompareLT_R_E,
	wr_CompareLT_E_I, wr_CompareLT_E_F, wr_CompareLT_E_R, wr_CompareLT_E_E,
};

bool wr_CompareEQ_E_E( WRValue* to, WRValue* from )
{
	WRValue V1 = to->singleValue();
	WRValue& V2 = from->singleValue();
	return wr_CompareEQ[(V1.type<<2)|V2.type](&V1, &V2);
}
bool wr_CompareEQ_E_I( WRValue* to, WRValue* from )
{
	if ( !wr_holdsNumber(to) )
	{
		return false;
	}
	WRValue& V = to->singleValue();
	return wr_CompareEQ[(V.type<<2)|WR_INT](&V, from);
}
bool wr_CompareEQ_E_F( WRValue* to, WRValue* from )
{
	if ( !wr_holdsNumber(to) )
	{
		return false;
	}
	WRValue& V = to->singleValue();
	return wr_CompareEQ[(V.type<<2)|WR_FLOAT](&V, from);
}
bool wr_CompareEQ_I_E( WRValue* to, WRValue* from )
{
	if ( !wr_holdsNumber(from) )
	{
		return false;
	}
	WRValue& V = from->singleValue();
	return wr_CompareEQ[(WR_INT<<2)|V.type](to, &V);
}
bool wr_CompareEQ_F_E( WRValue* to, WRValue* from )
{
	if ( !wr_holdsNumber(from) )
	{
		return false;
	}
	WRValue& V = from->singleValue();
	return wr_CompareEQ[(WR_FLOAT<<2)|V.type](to, &V);
}
bool wr_CompareEQ_R_E( WRValue* to, WRValue* from ) { return wr_CompareEQ[(to->r->type<<2)|WR_EX](to->r, from); }
bool wr_CompareEQ_E_R( WRValue* to, WRValue* from ) { return wr_CompareEQ[(WR_EX<<2)|from->r->type](to, from->r); }
bool wr_CompareEQ_R_R( WRValue* to, WRValue* from ) { return wr_CompareEQ[(to->r->type<<2)|from->r->type](to->r, from->r); }
bool wr_CompareEQ_R_I( WRValue* to, WRValue* from ) { return wr_CompareEQ[(to->r->type<<2)|WR_INT](to->r, from); }
bool wr_CompareEQ_R_F( WRValue* to, WRValue* from ) { return wr_CompareEQ[(to->r->type<<2)|WR_FLOAT](to->r, from); }
bool wr_CompareEQ_I_R( WRValue* to, WRValue* from ) { return wr_CompareEQ[(WR_INT<<2)+from->r->type](to, from->r); }
bool wr_CompareEQ_F_R( WRValue* to, WRValue* from ) { return wr_CompareEQ[(WR_FLOAT<<2)+from->r->type](to, from->r); }
bool wr_CompareEQ_I_I( WRValue* to, WRValue* from ) { return to->i == from->i; }
bool wr_CompareEQ_I_F( WRValue* to, WRValue* from )
{
	return WR_FLOATS_EQUAL( (float)to->i, from->f );
}
bool wr_CompareEQ_F_I( WRValue* to, WRValue* from )
{
	return WR_FLOATS_EQUAL( to->f, (float)from->i );
}
bool wr_CompareEQ_F_F( WRValue* to, WRValue* from )
{
	return WR_FLOATS_EQUAL( to->f, from->f );
}
WRReturnFunc wr_CompareEQ[16] = 
{
	wr_CompareEQ_I_I, wr_CompareEQ_I_F, wr_CompareEQ_I_R, wr_CompareEQ_I_E,
	wr_CompareEQ_F_I, wr_CompareEQ_F_F, wr_CompareEQ_F_R, wr_CompareEQ_F_E,
	wr_CompareEQ_R_I, wr_CompareEQ_R_F, wr_CompareEQ_R_R, wr_CompareEQ_R_E,
	wr_CompareEQ_E_I, wr_CompareEQ_E_F, wr_CompareEQ_E_R, wr_CompareEQ_E_E,
};

//------------------------------------------------------------------------------
// integers step as unsigned so INT_MAX + 1 wraps instead of being undefined
#define X_UNARY_PRE( NAME, OPERATION, SIGN ) \
void NAME##_E( WRValue* value )\
{\
	WRValue& V = value->singleValue();\
	NAME [ V.type ]( &V );\
	wr_valueToEx( value, &V );\
}\
void NAME##_I( WRValue* value ) { value->ui = value->ui SIGN 1u; }\
void NAME##_F( WRValue* value ) { OPERATION value->f; }\
void NAME##_R( WRValue* value ) { NAME [ value->r->type ]( value->r ); *value = *value->r; }\
WRUnaryFunc NAME[4] = \
{\
	NAME##_I, NAME##_F, NAME##_R, NAME##_E\
};\

X_UNARY_PRE( wr_preinc, ++, + );
X_UNARY_PRE( wr_predec, --, - );

//------------------------------------------------------------------------------
#define X_UNARY_POST( NAME, OPERATION, SIGN ) \
void NAME##_E( WRValue* value, WRValue* stack )\
{\
	WRValue& V = value->singleValue();\
	WRValue temp; \
	NAME [ V.type ]( &V, &temp );\
	wr_valueToEx( value, &V );\
	*stack = temp; \
}\
void NAME##_I( WRValue* value, WRValue* stack ) { stack->p2 = INIT_AS_INT; stack->i = value->i; value->ui = value->ui SIGN 1u; }\
void NAME##_R( WRValue* value, WRValue* stack ) { NAME[ value->r->type ]( value->r, stack ); }\
void NAME##_F( WRValue* value, WRValue* stack ) { stack->p2 = INIT_AS_FLOAT; stack->f = value->f OPERATION; }\
WRVoidFunc NAME[4] = \
{\
	NAME##_I,  NAME##_F,  NAME##_R, NAME##_E\
};\

X_UNARY_POST( wr_postinc, ++, + );
X_UNARY_POST( wr_postdec, --, - );

#endif
