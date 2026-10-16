/*~ case0
after
done
~*/

// regressions for compiler bugs; every check prints only when it fails

// ternary true arms (the optimizer merged code across the jump target)
var c = 1;
var x = 0;
x = c ? 7 : 8;
if ( x != 7 ) { println( "tern1 ", x ); }
function tf( a ) { return a ? 5 : 0; }
if ( tf(1) != 5 || tf(0) != 0 ) { println( "tern2" ); }
if ( -(c ? 1 : 2) != -1 ) { println( "tern3" ); }
var ta[] = { 80, 81, 82 };
if ( ta[c ? 0 : 2] != 80 ) { println( "tern4" ); }
var t5 = 5;
var t6 = 6;
var tz = 6;
if ( ((c ? t5 : t6) == tz) != 0 ) { println( "tern5" ); }

// stale optimizer cache when appending code
var g1 = -13;
if ( (-(g1 + g1) | -32768) != -32742 ) { println( "append1" ); }

// operand order with both operands on the stack
var sa[] = { 10 };
var sc[] = { 0, 100 };
var sb = 0;
if ( sa[0] - ~sb + sc[1] != 111 ) { println( "swap1" ); }
var a2 = 7;
var c2 = -13;
var b2 = 100000;
if ( ~a2 >= -9 && c2 - 7 < 700000 - 2 && -9 + b2 < 0 ) { println( "swap2" ); }
var sx = 3;
if ( (-1 | 2 * sx & sx == -5) != -1 ) { println( "swap3" ); }

// precedence and associativity
if ( 1 + 5 % 3 != 3 ) { println( "prec1" ); }
if ( 10 - 7 % 4 != 7 ) { println( "prec2" ); }
if ( (6 ^ 3 & 5) != 7 ) { println( "prec3" ); }
var pa = 10;
var pb = 0;
pa += pb = 1;
if ( pa != 11 || pb != 1 ) { println( "assoc1 ", pa, " ", pb ); }
var pc = 10;
var pd = 5;
pc += pd += 1;
if ( pc != 16 || pd != 6 ) { println( "assoc2 ", pc, " ", pd ); }

// switch: code after a loop or a nested break must not fall back into the dispatch
var si = 0;
function sw( v )
{
	switch( v )
	{
		case 0:
			println( "case0" );
			do { si = si + 1; } while( si < 2 );
	}
	println( "after" );
}
sw( 0 );
var sn = 0;
var sv = 0;
var scond = 0;
switch( sv )
{
	case 0:
		sn = sn + 1;
		if ( scond ) { break; }
}
if ( sn != 1 ) { println( "switch2" ); }

// a store right before the next case label is reachable
var dx = 0;
var ds = 1;
switch( ds ) { case 0: dx = 1; case 1: dx = 2; }
if ( dx != 2 ) { println( "switch3" ); }
function sdef( s ) { var y = 0; switch( s ) { case 0: y = 100; default: y = 200; } return y; }
if ( sdef(5) != 200 ) { println( "switch4" ); }

// negative case labels
var neg = -1;
var negHit = 0;
switch( neg ) { case -1: negHit = 1; break; case 1: negHit = 2; break; }
if ( negHit != 1 ) { println( "case1" ); }
var negf = -2.5;
switch( negf ) { case -2.5: negHit = 3; break; case 7: negHit = 4; break; }
if ( negHit != 3 ) { println( "case2" ); }

// empty statements push nothing, so must pop nothing
function es( a, b ) { var t = a + b;; return t; }
if ( es(3, 4) != 7 ) { println( "empty1" ); }
var ek = 10;
for( var ei=0; ei<300; ++ei ) { ; }
if ( ek + 1 != 11 ) { println( "empty2" ); }

// quoted punctuation is data
var qa[] = { "x", ",", "y" };
if ( qa._count != 3 || qa[1] != "," ) { println( "quote1" ); }
var qb[] = { "{", "}" };
if ( qb[0] != "{" || qb[1] != "}" ) { println( "quote2" ); }
var qh = { "k" : "{" };
if ( qh["k"] != "{" ) { println( "quote3" ); }
function qf() { "}"; return 5; }
if ( qf() != 5 ) { println( "quote4" ); }
var qx = 1;
if ( qx ) ";"; else println( "quote5" );

// prefix operators back to back
var px = 3;
if ( !!px != 1 || - -px != 3 || ~-px != 2 || -~px != 4 ) { println( "prefix1" ); }

// enum constants do not replace member names
enum { ey, ez };
struct EP { var ey = 20; var ez = 30; };
var ep = new EP;
if ( ep.ey != 20 || ep.ez != 30 || ey != 0 || ez != 1 ) { println( "enum1" ); }

// literals
if ( 1e3 != 1000 || 2.5e-1 != 0.25 || 09.5 != 9.5 || 010 != 8 || 07e1 != 70 ) { println( "num1" ); }
if ( '\xFF' != 255 || '\x41' != 65 ) { println( "char1" ); }
var es1 = "a\x41\tb";
if ( es1._count != 4 || es1[1] != 'A' ) { println( "escape1" ); }

// foreach with the container on its own line
var fa[] = { 1, 2, 3 };
var ft = 0;
for( var fv :
	fa )
{
	ft = ft + fv;
}
if ( ft != 6 ) { println( "foreach1" ); }

println( "done" );
