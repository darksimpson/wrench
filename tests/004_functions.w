/*~
argtest
1
2
3
4
5
6
6
4000000
20
0
1.2
10
7
4000000
20
0
1.2
10
8
0
20
8
1
20
8
2
20
8
0
0
30
1
0
30
2
3
30
4
5
30
5
3
8
0
1
1
2
3
5
8
13
21
34
55
10
10
20
20
3
3
4 4
~*/


var x = -2;
var y;
var a = 10;
var glob;
if ( x != -2 ) println( "xfail1");
incGlob(y)-1;


a = println("argtest");
if ( a != 20 ) { println("afail1"); }
a = 10;
a = println();
if ( a != 20 ) { println("afail2"); }
a = 10;
a = println(1,2,3,4,5,6);
if ( a != 20 ) { println("afail3"); }

glob = 10;
incGlob();
function incGlob()
{
	glob++;
}
if ( glob != 11 ) println("g1");


tle(tle());
a = tle();
function tle
{
}


a = 5;

++::a;
println( a );

test();

function test()
{
	var b = 4000000;
	println( b );
	b = 20;
	println( b );
	b = 0;
	println( b );
	b = 1.2;
	println( b );

	b = 10;
	println( b );
	++::a;
	println( ::a );
	
}

test();


function test2(a)
{
	println( a );
	a = 20;
	println( a );
	
	println( ::a );
}

test2();
test2(1);
test2(2,3);

function test3(a,b)
{
	println( a );
	println( b );
	b = 30;
	println( b );
}

test3();
test3(1);
test3(2,3);
test3(4,5,6 );


function test4()
{
}

test4();
test4(1);
test4(2,3);
test4(4,5,6 );


function test5()
{
	return 5;
}

println( test5() );

function test6()
{
	var b = 2;
	var c = 3;
	var d = 4;
	return c;
}
println( test6() );

function test7()
{
	return ::a;
}
println( test7() );


function fibonacci( n )
{
	if (n <= 1) return n;
	return fibonacci(n - 2) + fibonacci(n - 1);
}

var i;
for ( i = 0; i <= 10; ++i)
{
	println( fibonacci(i) );
}


a = 10;
function fa()
{
	a = 20;
}
function g_fa()
{
	::a = 20;
}
println(a);
println(::a);
fa();
println(a);
g_fa();
println(a);


function farray( a )
{
	a[0] = 21;
}

function farg( a )
{
	a = 25;
}

a[1] = {0};
farray( a );
if ( a[0] != 21 ) println("ferr 0");

//work = 40;
//println( work );
//farg( work );
//println( work );

// every check below prints only when it fails

// locals start at zero
function junk() { var p = 777; var r = 888; return p + r; }
function fresh() { var u; var v; return u; }
junk();
if ( fresh() != 0 ) { println( "local1" ); }

// functions defined inside a block (the unit table grows meanwhile)
function u1() { return 1; }
function u2() { return 2; }
function u3() { return 3; }
var ux = 0;
if ( 1 )
{
	function u4() { return 4; }
	ux = 5;
}
if ( ux != 5 || u4() != 4 ) { println( "unit1" ); }

// a nested block does not end the enclosing one
function nb()
{
	var r = 0;
	{
		r = 1;
	}
	r = r + 1;
	return r;
}
if ( nb() != 2 ) { println( "block1" ); }

// arguments are values: a change to a parameter never reaches the caller
function bump( n ) { n += 1; ++n; n++; --n; n -= 0; n *= 2; }
function setTo( n, o ) { n = o; }
function bumpMember( m ) { m += 10; m++; }
var av = 1;
bump( av );
if ( av != 1 ) { println( "arg1 ", av ); }
var bv = 7;
setTo( av, bv );
if ( av != 1 || bv != 7 ) { println( "arg2 ", av, " ", bv ); }
var ah = { "k": 1 };
bumpMember( ah.k );
var aarr[] = { 5 };
bumpMember( aarr[0] );
if ( ah.k != 1 || aarr[0] != 5 ) { println( "arg3 ", ah.k, " ", aarr[0] ); }
function localCaller() { var l = 3; bump( l ); setTo( l, 9 ); return l; }
if ( localCaller() != 3 ) { println( "arg4" ); }

// containers are still shared
function fill( list, table ) { list[0] = 42; table.added = 1; }
var shared[] = { 0 };
var sharedTable = { "x": 1 };
fill( shared, sharedTable );
if ( shared[0] != 42 || sharedTable.added != 1 ) { println( "arg5" ); }

// every argument is the value it had when it was evaluated (the test
// println writes each argument on a line of its own)
function two( x, y ) { return x * 10 + y; }
var aj = 1;
if ( two( aj, aj++ ) != 11 || aj != 2 ) { println( "arg6 ", aj ); }
var ag = 4;
function setAg() { ag = 100; return 0; }
if ( two( ag, setAg() ) != 40 ) { println( "arg7" ); }
var hm = { "k": 1 };
if ( two( hm.k, hm.k = 5 ) != 15 ) { println( "arg7a" ); }
var ap = 3;
if ( two( ap, ap += 4 ) != 37 ) { println( "arg7b" ); }
var ae = 1;
if ( two( ae, ae = 9 ) != 19 ) { println( "arg7c" ); }
function local3( q ) { var r = q; return two( r, r++ ) * 10 + two( q, ++q ); }
if ( local3( 2 ) != 22 * 10 + 23 ) { println( "arg7d ", local3( 2 ) ); }
var ai = 3;
println( ai, ai++ );
println( str::format( "%d %d", ai, ai++ ) );
if ( ai != 5 ) { println( "arg8 ", ai ); }

// a struct constructor keeps the values it got
struct Holder( p ) { var q = p; }
var hv = 2;
var holder = new Holder( hv );
hv = 99;
if ( holder.q != 2 ) { println( "arg9 ", holder.q ); }

// so does a function of a module
sys::importCompile( "function modBump( n ) { n += 1; return n; }" );
var mv = 1;
if ( modBump( mv ) != 2 || mv != 1 ) { println( "arg11 ", mv ); }
