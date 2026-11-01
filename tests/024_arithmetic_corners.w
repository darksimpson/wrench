/*~
-2147483648
-2147483648
2147483647
0.12346
2
90.1
1e+10
-1e+10
inf
nan
0.2
5
0.1
-8
8
-18
4
~*/

// 32-bit signed boundary wrap checks
var imax = 2147483647;
if ( imax + 1 != -2147483648 ) println("ac1");
if ( imax + 2 != -2147483647 ) println("ac2");

var imin = -2147483648;
if ( imin - 1 != 2147483647 ) println("ac3");
if ( -imin != -2147483648 ) println("ac4");

// Signed division/modulo corner behavior
if ( -7 / 3 != -2 ) println("ac5");
if ( 7 / -3 != -2 ) println("ac6");
if ( -7 / -3 != 2 ) println("ac7");

if ( -13 % 5 != -3 ) println("ac8");
if ( 13 % -5 != 3 ) println("ac9");
if ( -13 % -5 != -3 ) println("ac10");

// Operator precedence/associativity corners
if ( 1 + 2 << 3 != 24 ) println("ac11");
if ( (1 | 2 & 4) != 1 ) println("ac12");
if ( (1 ^ 2 & 3) != 3 ) println("ac13");
if ( 16 >> 1 + 1 != 4 ) println("ac14");
if ( 3 + 4 * 5 - 6 / 2 != 20 ) println("ac15");

// Compound assignment around boundaries
var a = 0x7FFFFFFE;
a += 1;
if ( a != 2147483647 ) println("ac16");
a += 1;
if ( a != -2147483648 ) println("ac17");
a -= 2;
if ( a != 2147483646 ) println("ac18");

// Mixed int/float arithmetic around truncation and sign
var f = -5;
f /= 2;
if ( f != -2 ) println("ac19");

f = -5.0;
f /= 2;
if ( f != -2.5 ) println("ac20");

var m = (int)(-1.9) * 3 + (int)(2.9);
if ( m != -1 ) println("ac21");

// every check below prints only when it fails

// integer division and modulo never trap
var zero = 0;
var minusOne = -1;
var dmin = -2147483647 - 1;
if ( 7 / zero != 0 || 7 % zero != 0 ) { println( "div1" ); }
var dm = 9;
dm %= zero;
if ( dm != 0 ) { println( "div2" ); }
if ( dmin / minusOne != dmin || dmin % minusOne != 0 ) { println( "div3" ); }
var sh = 1;
if ( (sh << 33) != 2 ) { println( "shift1" ); }

// integer operators on floats use the integer values
var f55 = 5.5;
var two = 2;
if ( f55 % two != 1 || (f55 & two) != 0 ) { println( "fint1" ); }
var mx = 4;
var ma[] = { 2, 9 };
if ( ma[1] % mx != 1 ) { println( "fint2" ); }

// ~ works on the int a float truncates to
if ( ~2.5 != -3 || ~(-1.5) != 0 || ~3 != -4 ) { println( "not1" ); }

// integer compound assignments with floats work like the binary operators
var cf = 5.5;
cf %= 2;
if ( cf != 1 ) { println( "fassign1 ", cf ); }
var ci = 7;
ci %= 2.0;
if ( ci != 1 ) { println( "fassign2 ", ci ); }
var cg = 6.0;
cg |= 1;
if ( cg != 7 ) { println( "fassign3 ", cg ); }
var ch = 1.0;
ch <<= 3;
if ( ch != 8 ) { println( "fassign4 ", ch ); }

// ++, -- and *= wrap instead of overflowing
var big = 2147483647;
++big;
if ( big != -2147483648 ) { println( "wrap1 ", big ); }
var small = -2147483648;
small--;
if ( small != 2147483647 ) { println( "wrap2 ", small ); }
var mul = 65536;
mul *= 65536;
if ( mul != 0 ) { println( "wrap3 ", mul ); }

// printing: INT_MIN, floats rounded to 6 significant digits, exponent
// forms, inf and nan
var pmin = -2147483647 - 1;
println( pmin );
println( str::format("%d", pmin) );
println( 2147483647 );
println( 0.123456 );
println( 1.999996 );
var pd = 90.1;
pd /= 5.5;
pd *= 5.5;
println( pd );
var pbig = 10000000000.0;
println( pbig );
println( -pbig );
var pinf = pbig * pbig * pbig * pbig;
println( pinf );
println( pinf - pinf );

// floats divided and subtracted with a global float on either side, and an
// operation on a literal followed by one on two variables
var fb = 0.0;
var fa = 10.0;

function initFromMillis()
{
	fb = millis()/200.0;
}

function floatOps( i, j )
{
	println( i / fa );
	println( fa / i );
	println( i / j );

	println( i - fa );
	println( fa - i );
	println( i - j );
}

floatOps( 2.0, 20.0 );

function literalThenVariables()
{
	var t = 1.0;
	var x = t * 4.0;
	var z = x * t;
	return z;
}

println( literalThenVariables() );
