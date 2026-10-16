/*~ done
~*/

// regressions for VM bugs; every check prints only when it fails

// compound assignment to an element must not write the right-hand variable
var a[] = { 10, 20 };
var y = 3;
a[0] -= y;
if ( a[0] != 7 || y != 3 ) { println( "ca1 ", a[0], " ", y ); }
var b[] = { 7, 4 };
a[0] += b[1];
if ( a[0] != 11 || b[1] != 4 ) { println( "ca2 ", a[0], " ", b[1] ); }
a[0] *= b[1];
if ( a[0] != 44 || b[1] != 4 ) { println( "ca3" ); }
a[0] /= b[1];
if ( a[0] != 11 || b[1] != 4 ) { println( "ca4" ); }
a[0] |= b[1];
if ( a[0] != 15 || b[1] != 4 ) { println( "ca5" ); }
var h = { "x" : 1 };
var k = 7;
h["x"] += k;
if ( h["x"] != 8 || k != 7 ) { println( "ca6" ); }
var fl = 2.5;
var iv = 2;
fl += iv;
if ( fl != 4.5 || iv != 2 ) { println( "ca7" ); }

// comparing an int with a float leaves the int alone
var i = 3;
if ( i < 2.5 ) {}
if ( i != 3 ) { println( "cmp1 ", i ); }
var j = 3;
if ( j > 2.5 ) {}
if ( j != 3 ) { println( "cmp2" ); }
function lc() { var q = 4; if ( q < 4.5 ) {} return q; }
if ( lc() != 4 ) { println( "cmp3" ); }

// integer division and modulo never trap
var zero = 0;
var minusOne = -1;
var imin = -2147483647 - 1;
if ( 7 / zero != 0 || 7 % zero != 0 ) { println( "div1" ); }
var m = 9;
m %= zero;
if ( m != 0 ) { println( "div2" ); }
if ( imin / minusOne != imin || imin % minusOne != 0 ) { println( "div3" ); }
var sh = 1;
if ( (sh << 33) != 2 ) { println( "shift1" ); }

// integer operators on floats use the integer values
var f55 = 5.5;
var two = 2;
if ( f55 % two != 1 || (f55 & two) != 0 ) { println( "fint1" ); }
var mx = 4;
var ma[] = { 2, 9 };
if ( ma[1] % mx != 1 ) { println( "fint2" ); }

// locals start at zero
function junk() { var p = 777; var r = 888; return p + r; }
function fresh() { var u; var v; return u; }
junk();
if ( fresh() != 0 ) { println( "local1" ); }

// removed hash keys stay removed
var rh = { 1:100, 2:200 };
rh._remove( 1 );
if ( rh[1] != 0 ) { println( "hash1" ); }

// writing through a member of a table that grows meanwhile
var gh = { "a" : 1 };
function grow()
{
	for( var gi=0; gi<2000; ++gi )
	{
		gh[gi] = "str" + gi;
	}
	return 5;
}
gh["zz"] = grow();
if ( gh["zz"] != 5 ) { println( "hash2" ); }

// a container that holds itself survives garbage collection
var cyc[2];
cyc[0] = cyc;
cyc[1] = 42;
for( var ci=0; ci<200; ++ci )
{
	var churn = "garbage " + ci;
}
if ( cyc[0][1] != 42 ) { println( "cycle1" ); }

// logical operators with floats
if ( !(1.5 && 2.5) || (0.0 || 0.0) ) { println( "logic1" ); }

println( "done" );
