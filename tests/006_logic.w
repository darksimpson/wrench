/*~
13
13
3
10.5
10.5
0.5
10.7
10.7
0.5
T
F
T1
T2
T3
T4
T5
T6
T7
T8
T9
ba
ba
a||b
a||b
ba
ba
a||b
a||b
ba
ba
ab
ab
ab
ab
a||b
a||b
~*/

abc();

var a = 10;
var b = 5;
var c = 0;

if ( 10 <= a ) { c += 0x1000; }
if ( 10 >= a ) { c += 0x1000; }
if ( 10 == b ) { println("CB1"); }
if ( 10 != a ) { println("CB2"); }
if ( 10 < b )  { println("CB3"); }
if ( b > 10 )  { println("CB4"); }
if ( 10 <= b ) { println("CB8"); }
if ( b >= 10 ) { println("CB8"); }

if ( 10 <= a && 10 <= a  ) { c += 0x10; }
if ( 10 >= a && 10 >= a ) { c += 0x10; }
if ( 10 == b && 10 == b ) { println("BB1"); }
if ( 10 != a && 10 != a ) { println("BB2"); }
if ( 10 < b && 10 < b ) { println("BB3"); }
if ( b > 10 && b > 10 ) { println("BB4"); }
if ( 10 <= b && 10 <= b ) { println("BB8"); }
if ( b >= 10 && b >= 10 ) { println("BB8"); }

if ( a <= a && a <= a  ) { c += 0x1; }
if ( a >= a && a >= a ) { c += 0x1; }
if ( a == b && a == b ) { println("AB1"); }
if ( a != a && a != a ) { println("AB2"); }
if ( a < b && a < b ) { println("AB3"); }
if ( b > a && b > a ) { println("AB4"); }
if ( a <= b && a <= b ) { println("AB8"); }
if ( b >= a && b >= a ) { println("AB8"); }


if ( a <= a ) { c += 0x100; }
if ( a >= a ) { c += 0x100; }
if ( a == b ) { println("CB1"); }
if ( a != a ) { println("CB2"); }
if ( a < b ) { println("CB3"); }
if ( b > a ) { println("CB4"); }
if ( a <= b ) { println("CB8"); }
if ( b >= a ) { println("CB8"); }


if ( c != 0x2222 ) { println("err C:"); println(c); }

function abc()
{
	var aa = 10;
	var ba = 5;
	var ca = 0;

	if ( 10 <= aa ) { ca += 0x1000; }
	if ( 10 >= aa ) { ca += 0x1000; }
	if ( 10 == ba ) { println("L CB1"); }
	if ( 10 != aa ) { println("L CB2"); }
	if ( 10 < ba )  { println("L CB3"); }
	if ( ba > 10 )  { println("L CB4"); }
	if ( 10 <= ba ) { println("L CB8"); }
	if ( ba >= 10 ) { println("L CB8"); }
	
	if ( 10 <= aa && 10 <= aa  ) { ca += 0x10; }
	if ( 10 >= aa && 10 >= aa ) { ca += 0x10; }
	if ( 10 == ba && 10 == ba ) { println("L BB1"); }
	if ( 10 != aa && 10 != aa ) { println("L BB2"); }
	if ( 10 < ba && 10 < ba ) { println("L BB3"); }
	if ( ba > 10 && ba > 10 ) { println("L BB4"); }
	if ( 10 <= ba && 10 <= ba ) { println("L BB8"); }
	if ( ba >= 10 && ba >= 10 ) { println("L BB8"); }

	if ( aa >= aa && aa >= aa ) { ca += 1; }
	if ( aa <= aa && aa <= aa ) { ca += 1; }
	if ( aa == ba && aa == ba ) { println("L AB1"); }
	if ( aa != aa && aa != aa ) { println("L AB2"); }
	if ( aa < ba && aa < ba ) { println("L AB3"); }
	if ( ba > aa && ba > aa ) { println("L AB4"); }
	if ( aa <= ba && aa <= ba ) { println("L AB8"); }
	if ( ba >= aa && ba >= aa ) { println("L AB8"); }

	if ( aa >= aa ) { ca += 0x100; }
	if ( aa <= aa ) { ca += 0x100; }
	if ( aa == ba ) { println("L AB1"); }
	if ( aa != aa ) { println("L AB2"); }
	if ( aa < ba ) { println("L AB3"); }
	if ( ba > aa ) { println("L AB4"); }
	if ( aa <= ba ) { println("L AB8"); }
	if ( ba >= aa ) { println("L AB8"); }

	if ( ca != 0x2222 ) { println("L err C:"); println(ca); }
}

a = 10.5;
if ( 10.5 < 10 || 10.5 < 10 ) println("E T1");
if ( 10.5 <= 10 || 10.5 <= 10 ) println("E T11");
if ( 10 > 10.5 || 10 > 10.5 ) println("E T2");
if ( 10 >= 10.5 || 10 >= 10.5 ) println("E T21");
if ( 10.5 == 10 || 10.5 == 10 ) println("E 3");
if ( 10 != 10 || 10 != 10 ) println("E 4");
if ( !(10 <= 10 || 10 <= 10) ) println("E 5");
if ( !(10 >= 10 || 10 >= 10) ) println("E 6");



// float <- int
var f = .5;
var i = 3;
if ( (f += i) != 3.5 ) println("35.1");
if ( f != 3.5 ) println("35.2");
if ( i != 3 ) println("35.3");

// int <- int
i = 10;
var j = 3;
println( i += j ); // 13
println( i );
println( j );

// int <- float
i = 10;
f = .5;
println( i += f ); // 10.5
println( i );
println( f );

// float <- float
var g = 10.2;
f = .5;
println( g += f ); // 10.7
println( g );
println( f );

if ( !1 )
{
	println("1");
}

a = 1;
if ( a )
{
	println("T");
}
else
{
	println("F");
}

if ( !a )
{
	println("T");
}
else
{
	println("F");
}



a = 10.5;
if ( a > 10 ) println("T1");
if ( a < 11 ) println("T2");
if ( a >= 10.5 ) println("T3");
if ( a >= 10 ) println("T4");
if ( a < 10.6 ) println("T5");
if ( a <= 10.5 ) println("T6");
if ( a <= 11 ) println("T7");
if ( a == 10.5 ) println("T8");
if ( a != 1) println("T9");

if ( a <= 10 ) println("B1");
if ( a >= 11 ) println("B2");
if ( a < 10.5 ) println("B3");
if ( a < 10 ) println("B4");
if ( a >= 10.6 ) println("B5");
if ( a > 10.5 ) println("B6");
if ( a > 11 ) println("B7");
if ( a != 10.5 ) println("B8");
if ( a == 1) println("B9");


a = 1000;
if ( a > 9000 ) println("B10");
if ( a >= 9000 ) println("B11");
if ( a < 900 ) println("B12");
if ( a <= 900 ) println("B13");
if ( a == 900 ) println("B14");
if ( a != 1000 ) println("B15");
if ( 9000 < a ) println("B101");
if ( 9000 <= a ) println("B111");
if ( 900 > a ) println("B121");
if ( 900 >= a ) println("B131");
if ( 900 == a ) println("B141");
if ( 1000 != a ) println("B151");


a = 100000;
if ( a > 900000 ) println("B14");
if ( a >= 900000 ) println("B15");
if ( a < 90000 ) println("B16");
if ( a <= 90000 ) println("B17");
if ( a == 90000 ) println("B18");
if ( a != 100000 ) println("B19");
if ( 900000 < a ) println("C10");
if ( 900000 <= a ) println("C11");
if ( 90000 > a ) println("C12");
if ( 90000 >= a ) println("C13");
if ( 90000 == a ) println("C14");
if ( 100000 != a ) println("C15");


b = 0;
if ( a && b ) println("ab");
if ( b && a ) println("ab");
if ( a && b ) println("ab"); else println("ba");
if ( b && a ) println("ab"); else println("ba");
if ( a || b ) println("a||b");
if ( b || a ) println("a||b");

a = 0;
b = 1;
if ( a && b ) println("ab");
if ( b && a ) println("ab");
if ( a && b ) println("ab"); else println("ba");
if ( b && a ) println("ab"); else println("ba");
if ( a || b ) println("a||b");
if ( b || a ) println("a||b");

b = 0;
if ( a && b ) println("ab");
if ( b && a ) println("ab");
if ( a && b ) println("ab"); else println("ba");
if ( b && a ) println("ab"); else println("ba");
if ( a || b ) println("a||b");
if ( b || a ) println("a||b");

a = 1;
b = 1;
if ( a && b ) println("ab");
if ( b && a ) println("ab");
if ( a && b ) println("ab"); else println("ba");
if ( b && a ) println("ab"); else println("ba");

a = 0;
if ( a && b ) println("ab");
if ( b && a ) println("ab");

a = 1;
b = 0;
if ( a && b ) println("ab");
if ( b && a ) println("ab");

a = 0;
if ( a || b ) println("a||b");
if ( b || a ) println("a||b");

a = 1;
b = 0;
if ( a || b ) println("a||b");
if ( b || a ) println("a||b");

// && and || evaluate their right side only when the left side does not
// decide the result, and yield 1 or 0; every check below prints only when
// it fails
var scLog = "";
function sct( n ) { scLog += n; return 1; }
function scf( n ) { scLog += n; return 0; }
function scChk( got, want, wantLog, name ) { if ( got != want || scLog != wantLog ) { println( "FAIL ", name, " got ", got, " log ", scLog ); } scLog = ""; }
scChk( sct("a") && sct("b"), 1, "ab", "tt&" );
scChk( sct("a") && scf("b"), 0, "ab", "tf&" );
scChk( scf("a") && sct("b"), 0, "a", "ft&" );
scChk( scf("a") || sct("b"), 1, "ab", "ft|" );
scChk( sct("a") || sct("b"), 1, "a", "tt|" );
scChk( scf("a") || scf("b"), 0, "ab", "ff|" );
scChk( sct("a") || scf("b") && sct("c"), 1, "a", "prec1" );
scChk( scf("a") && sct("b") || sct("c"), 1, "ac", "prec2" );
scChk( scf("a") || sct("b") && scf("c"), 0, "abc", "prec3" );
scChk( sct("a") && sct("b") && sct("c"), 1, "abc", "chain&" );
scChk( scf("a") || scf("b") || sct("c"), 1, "abc", "chain|" );
scChk( (scf("a") || sct("b")) && sct("c"), 1, "abc", "paren" );
scChk( sct("a") && scf("b") ? 5 : 6, 6, "ab", "tern1" );
scChk( scf("a") ? 1 : sct("b") || sct("c"), 1, "ab", "tern2" );
scChk( sct("a") ? scf("b") && sct("c") : 9, 0, "ab", "tern3" );
scChk( !scf("a") && sct("b"), 1, "ab", "not" );
scChk( 2 && 3, 1, "", "values&" );
scChk( 0 || 7, 1, "", "values|" );
scChk( 2.5 && "x", 1, "", "mixed" );
var scx = scf("a") || sct("b");
scChk( scx, 1, "ab", "assign" );
var sci = 0; var scn = 0;
while ( sci < 5 && sct("w") ) { ++sci; }
scChk( sci, 5, "wwwww", "while" );
for( sci = 0; sci < 3 || scf("q"); ++sci ) { scn++; }
scChk( scn, 3, "q", "for" );
var sca[] = { 1, 2, 3 };
var sck = 10;
if ( sck < sca._count && sca[sck] == 1 ) { println( "FAIL guard" ); }
if ( sca._count != 3 ) { println( "FAIL grew" ); }
function scAnd( p, q ) { return p && q; }
scChk( scAnd(1, 0), 0, "", "fn" );
scChk( scAnd(1, 2) + 1, 2, "", "arith" );

// logical operators with floats
if ( !(1.5 && 2.5) || (0.0 || 0.0) ) { println( "logic1" ); }

// comparing an int with a float leaves the int alone
var ci = 3;
if ( ci < 2.5 ) {}
if ( ci != 3 ) { println( "cmp1 ", ci ); }
var cj = 3;
if ( cj > 2.5 ) {}
if ( cj != 3 ) { println( "cmp2" ); }
function lc() { var q = 4; if ( q < 4.5 ) {} return q; }
if ( lc() != 4 ) { println( "cmp3" ); }
