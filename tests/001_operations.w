/*~
10
-10
26
27
28
8
2
33
120
19
31
32
33
16.3818
90.1
8
1
4
15
15
27
27
12
3
3
13
3
13
10
3
3
3
3
10
1.3
~*/

var z1 = -.0;
if ( z1 != -.0 ) { println( "z1 ",  z1 ); }
var z = -0;
if ( z != 0 ) println("z0 " + z );


if ( 10 / 0 ) {}
if ( 10.1 / 0 ) {}
var zero = 0;
if( 10 / zero ) {}
if( 10.1 / zero ) {}
zero = 0.5;
zero = 0;
if( 10 / zero ) {}
if( 10.1 / zero ) {}

// dividing by zero through /= must not crash either (a literal cannot be
// assigned to, so each case divides a fresh variable)
var dz = 10;
if ( dz /= 0 ){}
var dzf = 10.1;
if ( dzf /= 0 ){}
zero = 0;
dz = 10;
if( dz /= zero ) {}
dzf = 10.1;
if( dzf /= zero ) {}
zero = 0.5;
zero = 0;
dz = 10;
if( dz /= zero ) {}
dzf = 10.1;
if( dzf /= zero ) {}



var y = "BB";
z = "CC";
var y1 = y + z;
y = y + z;
if ( y1 != "BBCC" ) print("fail bin1");
if ( y != "BBCC" ) print("fail bin1");



var a = "BB";
var b = "CC";
var d = a + b;
a = a + b;
b = a + b;
if ( d != "BBCC" ) println("op 0");
if ( a != "BBCC" ) println("op 1");
if ( b != "BBCCCC" ) println("op 2");


var two = 0;
var s = 0xAABBCCDD;
var r = s + two;
s = 0xA;
r = s + two;
s = 1.0;
r = s + two;

two = 0;
s = 0xAABBCCDD;
r = s + two;
if ( r != 0xAABBCCDD ) { println("o1"); }
s = 0xA;
r = s + two;
if ( r != 0xA ) println("o2");

s = 1.0;
r = s + two;
if ( r != 1.0) println(r);

loc();

function loc()
{
	var ltwo = 0;
	var ls = 0xAABBCCDD;
	var lr = ls + ltwo;
	ls = 0xA;
	lr = ls + ltwo;
	ls = 1.0;
	lr = ls + ltwo;

	ltwo = 0;
	ls = 0xAABBCCDD;
	lr = ls + ltwo;
	if ( lr != 0xAABBCCDD ) { println("l1"); }
	ls = 0xA;
	lr = ls + ltwo;
	if ( lr != 0xA ) println("l2");
	ls = 1.0;
	lr = ls + ltwo;
	if ( lr != 1.0) println("l3");
}



// test casting
var ii = 10.5;
var j = (int)ii;
if ( ii != 10.5 ) { println("j0"); }
if ( j != 10 ) { println("j1"); }
ii = 10 / 1000;
if ( ii != 0 ) { println("ii 1"); }
ii = (float)10 / 1000;
if ( ii != .01 ) { println("ii 2"); }
ii = 10 / (float)1000;
if ( ii != .01 ) { println("ii 3"); }
ii = 67.2;
var iiCast = (int)ii;
ii = iiCast;
if ( ii != 67 ) { println("ii 4"); }


var a10[10] = { 10 };
if ( a10[0] / 1000 != 0 ) { println("ii 4"); }
if ( (float)a10[0] / 1000 != .01 ) { println("ii 5"); }
if ( a10[0] / (float)1000 != .01 ) { println("ii 5"); }


var not = 1;
if ( !not ) println("n1");
if ( not != 1 ) println("n2");
var flip = 0x0000FFFF;
if ( ~flip != 0xFFFF0000 ) println("flip1");
if ( flip != 0x0000FFFF ) println("flip2");

var a1;
var b1;
var c1;
a1 = b1 = c1 = 10;
if ( a1 != 10 || b1 != 10 || c1 != 10 ) println("err 0");

a = 1;
b = -1;
var c = --a + ++b;
if ( c != 0 ) { println("err00"); }

a = 90;
a++;
++a;
b = a++;
c = ++a;
if ( a != 94 ) println("err 1");

a--;
--a;
b = a--;
c = --a;
if ( a != 90 ) println("err 2");

if ( .1 != 0.1 ) println( "val 0" );
if ( -.1 != -0.1 ) println( "val 1" );
if ( -.2 != -0.2 ) println( "val 2" );
if ( 0xF != 15 ) println( "val 3" );

b = 4000000;
if ( b != 4000000 ) println( "assign 0" );
if ( 4000000 != b ) println( "assign 1" );
b = 20;
if ( b != 20 ) println( "assign 2" );
if ( 20 != b ) println( "assign 3" );

b = 0;
if ( b != 0 ) println( "assign 4" );
if ( 0 != b ) println( "assign 5" );

b = 1.2;
if ( b != 1.2 ) println( "assign 6" );
if ( 1.2000 != b ) println( "assign 7" );


b = 10;
b += 5;
b /= 3;
b -= 2;
b *= 6;
if ( b != 18) println( "oper 0" );

a = 10;
a -= 3;
if ( a != 7 ) println( "oper 1" );
b = 10.2;
b -= .5 + (a += 1);

if ( b != 1.7 ) println( "oper 2" );
if ( a != 8 ) println( "oper 3" );

var n = -(-(-(-(-(-10))))); // 10 
println(n);
n = -(-(-(-(-10)))); // -10 
println(n);

a = 25;
println( 1 + ++a - 1 );  // 26
println( ++a + 1 - 1 ); // 27
println( 1 - 1 + ++a ); // 28

println( 10 - 2 );
println( 10 / 5 );

a = 28;
println( 1 + a++ + 10 - 2 * 3 ); // 33

println( (40 + 50) * 4 / (((2 + 1))) ); // 120
println( 3 * 5 - 10 + 10 + 10 - 2 * 3 ); // 19
println( 1 + 1 + a++ ); // 31
println( a++ + 1 + 1 ); // 32 
println( 1 + a++ + 1 ); // 33

var e = 0x04;
var f;
var g;

d = 90.1;
println( d /= 5.5 );
println( d *= 5.5 );

f = e << 1;
g = e >> 2;
println( f );
println( g );
println( e );

a = 10;
b = 12;
println( a += 5 ); // 15
println( a ); // 15
println( a += b ); // 27
println( a ); // 27
println( b ); // 12

a = 13;
b = 10;
println( 13 % 10 ); // 3
println( a % 10 ); // 3
println( a ); // 13
println( a % b ); // 3
println( a ); // 13
println( b ); // 10

println( a %= 10 ); // 3
println( a ); // 3
a = 13; 
println( a %= b ); // 3
println( a ); // 3
println( b ); // 10

a = 1.3;
println( a );

// // excercise keyhole optimizer loads

var ag = 8;
var bg = 3;
var ab;
local();

function local()
{
	a = 8;
	b = 3;
	c = a % b;if ( c != 2 ) println("F1");
	c = a << b;if ( c != 64 ) println("F2");
	c = a >> b;if ( c != 1 ) println("F3");
	
	a = 9;
	c = a & b;if ( c != 1 ) println("F4");
	c = a ^ b;if ( c != 10 ) println("F5");
	a = 8;
	c = a | b;if ( c != 11 ) println("F6");

	::ag = 8;
	b = 3;
	c = ::ag % b;if ( c != 2 ) println("F1");
	c = ::ag << b;if ( c != 64 ) println("F2");
	c = ::ag >> b;if ( c != 1 ) println("F3");

	::ag = 9;
	c = ::ag & b;if ( c != 1 ) println("F4");
	c = ::ag ^ b;if ( c != 10 ) println("F5");
	::ag = 8;
	c = ::ag | b;if ( c != 11 ) println("F6");


	a = 8;
	::ab = 3;
	c = a % ::ab;if ( c != 2 ) println("F1");
	c = a << ::ab;if ( c != 64 ) println("F2");
	c = a >> ::ab;if ( c != 1 ) println("F3");

	a = 9;
	c = a & ::ab;if ( c != 1 ) println("F4");
	c = a ^ ::ab;if ( c != 10 ) println("F5");
	a = 8;
	c = a | ::ab;if ( c != 11 ) println("F6");

	::ag = 8;
	::bg = 3;
	c = ::ag % ::bg;if ( c != 2 ) println("F1");
	c = ::ag << ::bg;if ( c != 64 ) println("F2");
	c = ::ag >> ::bg;if ( c != 1 ) println("F3");

	::ag = 9;
	c = ::ag & ::bg;if ( c != 1 ) println("F4");
	c = ::ag ^ ::bg;if ( c != 10 ) println("F5");
	::ag = 8;
	c = ::ag | ::bg;if ( c != 11 ) println("F6");
	
}

var gn = 30;
function foo()
{
	var gn = 2;  // local 'n' is 2
	if ( ::gn != 30 ) println("gn0");
	::gn = 40;   // global 'n' was 30, will now be 40
	if ( gn != 2 ) println("gn1");
	if ( ::gn != 40 ) println("gn2");
}

if ( gn != 30 ) println("gn3");
foo();
if ( gn != 40 ) println("gn4");
if ( ::gn != 40 ) println("gn5");

// globals and parameters on either side of + and -, every check prints only
// when it fails
var ga = 10;
var gb = 20;
var gc = 30;
var gd = 40;
var ge = 50;
var gf = 600;

function globalsAndParams( p1, p2, p3, p4, p5 )
{
	if ( p2 / ge != 4 ) { println( "gp0" ); }
	if ( gf / p3 != 2 ) { println( "gp1" ); }

	if ( ga + p1 != 110 ) { println( "gp2" ); }
	if ( gb + p1 != 120 ) { println( "gp3" ); }
	if ( gc + p1 != 130 ) { println( "gp4" ); }
	if ( gd + p1 != 140 ) { println( "gp5" ); }
	if ( ge + p1 != 150 ) { println( "gp6" ); }

	if ( p1 + ga != 110 ) { println( "gp7" ); }
	if ( p1 + gb != 120 ) { println( "gp8" ); }
	if ( p1 + gc != 130 ) { println( "gp9" ); }
	if ( p1 + gd != 140 ) { println( "gp10" ); }
	if ( p1 + ge != 150 ) { println( "gp11" ); }

	if ( ga + p3 != 310 ) { println( "gp12" ); }
	if ( gb + p3 != 320 ) { println( "gp13" ); }
	if ( gc + p3 != 330 ) { println( "gp14" ); }
	if ( gd + p3 != 340 ) { println( "gp15" ); }
	if ( ge + p3 != 350 ) { println( "gp16" ); }

	if ( p3 + ga != 310 ) { println( "gp17" ); }
	if ( p3 + gb != 320 ) { println( "gp18" ); }
	if ( p3 + gc != 330 ) { println( "gp19" ); }
	if ( p3 + gd != 340 ) { println( "gp20" ); }
	if ( p3 + ge != 350 ) { println( "gp21" ); }

	if ( ga - p1 != -90 ) { println( "gm1" ); }
	if ( gb - p1 != -80 ) { println( "gm2" ); }
	if ( gc - p1 != -70 ) { println( "gm3" ); }
	if ( gd - p1 != -60 ) { println( "gm4" ); }
	if ( ge - p1 != -50 ) { println( "gm5" ); }

	if ( p1 - ga != 90 ) { println( "gm6" ); }
	if ( p1 - gb != 80 ) { println( "gm7" ); }
	if ( p1 - gc != 70 ) { println( "gm8" ); }
	if ( p1 - gd != 60 ) { println( "gm9" ); }
	if ( p1 - ge != 50 ) { println( "gm10" ); }

	if ( ga - p3 != -290 ) { println( "gm11" ); }
	if ( gb - p3 != -280 ) { println( "gm12" ); }
	if ( gc - p3 != -270 ) { println( "gm13" ); }
	if ( gd - p3 != -260 ) { println( "gm14" ); }
	if ( ge - p3 != -250 ) { println( "gm15" ); }

	if ( p3 - ga != 290 ) { println( "gm16" ); }
	if ( p3 - gb != 280 ) { println( "gm17" ); }
	if ( p3 - gc != 270 ) { println( "gm18" ); }
	if ( p3 - gd != 260 ) { println( "gm19" ); }
	if ( p3 - ge != 250 ) { println( "gm20" ); }
}
globalsAndParams( 100, 200, 300, 400, 500 );

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

// prefix operators back to back
var px = 3;
if ( !!px != 1 || - -px != 3 || ~-px != 2 || -~px != 4 ) { println( "prefix1" ); }

// folding a negation keeps the value
if ( -(-128) != 128 ) { println( "neg1 ", -(-128) ); }
if ( -(-32768) != 32768 ) { println( "neg2 ", -(-32768) ); }
if ( -(-127) != 127 ) { println( "neg3" ); }

// compound assignment to an element must not write the right-hand variable
var ca[] = { 10, 20 };
var cy = 3;
ca[0] -= cy;
if ( ca[0] != 7 || cy != 3 ) { println( "ca1 ", ca[0], " ", cy ); }
var cb[] = { 7, 4 };
ca[0] += cb[1];
if ( ca[0] != 11 || cb[1] != 4 ) { println( "ca2 ", ca[0], " ", cb[1] ); }
ca[0] *= cb[1];
if ( ca[0] != 44 || cb[1] != 4 ) { println( "ca3" ); }
ca[0] /= cb[1];
if ( ca[0] != 11 || cb[1] != 4 ) { println( "ca4" ); }
ca[0] |= cb[1];
if ( ca[0] != 15 || cb[1] != 4 ) { println( "ca5" ); }
var ch = { "x" : 1 };
var ck = 7;
ch["x"] += ck;
if ( ch["x"] != 8 || ck != 7 ) { println( "ca6" ); }
var cfl = 2.5;
var civ = 2;
cfl += civ;
if ( cfl != 4.5 || civ != 2 ) { println( "ca7" ); }
