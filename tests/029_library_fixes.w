/*~
math
8
1.5708
1
12
numbers
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
format
[100000]
[10000000000000000]
[37777777777]
[11111111111111111111111111111111]
[   -3]
[-0003]
[-3   |]
[5]
[1.5]
[ab]
[5]
[5]
[000000ff]
[0xff]
[0x00ff]
[-2147483648]
[4294967295]
4096
[%]
containers
5
6
5
5
10
30
1
7
4
0,0,0,0
0
3
rand
ok
5
ok
serialize
2 200 100
9
8
x
131072 0
self 0
deep ok
malformed
0
0
0
0
0
0
done
~*/

println( "math" );
println( math::pow(2, 3) );
println( math::atan2(1, 0) );
println( math::fmod(7, 3) );
println( math::ldexp(3, 2) );

println( "numbers" );
var m = -2147483647 - 1;
println( m );
println( str::format("%d", m) );
println( 2147483647 );
println( 0.123456 );
println( 1.999996 );
var d = 90.1;
d /= 5.5;
d *= 5.5;
println( d );
var big = 10000000000.0;
println( big );
println( -big );
var inf = big * big * big * big;
println( inf );
println( inf - inf );

println( "format" );
println( "[" + str::format("%o", 32768) + "]" );
println( "[" + str::format("%b", 65536) + "]" );
println( "[" + str::format("%o", -1) + "]" );
println( "[" + str::format("%b", -1) + "]" );
println( "[" + str::format("%5d", -3) + "]" );
println( "[" + str::format("%05d", -3) + "]" );
println( "[" + str::format("%-5d|", -3) + "]" );
println( "[" + str::format("%s", 5) + "]" );
println( "[" + str::format("%s", 1.5) + "]" );
println( "[" + str::format("%s%s", "a", "b") + "]" );
println( "[" + str::format("%ld", 5) + "]" );
println( "[" + str::format("%hd", 5) + "]" );
println( "[" + str::format("%08x", 255) + "]" );
println( "[" + str::format("%#x", 255) + "]" );
println( "[" + str::format("%#06x", 255) + "]" );
println( "[" + str::format("%d", m) + "]" );
println( "[" + str::format("%u", -1) + "]" );
println( str::strlen(str::format("%99999999d", 1)) );
println( "[" + str::format("%%") + "]" );

println( "containers" );
var L[];
var x = 5;
list::push_back( L, x );
x = 6;
println( L[0] );
list::push( L, x );
x = 7;
println( L[0] );
function pushLocal( l )
{
	var y = 5;
	list::push_front( l, y );
	y = 99;
}
var L2[];
pushLocal( L2 );
println( L2[0] );
var Q[];
var z = 5;
queue::push( Q, z );
z = 8;
println( Q[0] );

var p[] = { 10, 20, 30 };
println( list::peek(p) );
println( queue::peek(p) );
var s[] = { 1, 2, 3 };
println( stack::peek(s) );
println( list::peek({7, 8}) );

var b[] = { 1 };
array::clear( b, 4 );
println( array::count(b) );
println( b[0] + "," + b[1] + "," + b[2] + "," + b[3] );
array::clear( b, 268435457 );
println( array::count(b) );
var t[] = { 1, 2, 3 };
array::truncate( t );
println( array::count(t) );

println( "rand" );
var r = std::rand( -2147483648, 2147483647 );
println( "ok" );
println( std::rand(5, 5) );
var inRange = 1;
for( var i = 0; i < 200; ++i )
{
	var v = std::rand( 1, 6 );
	if ( v < 1 || v > 6 )
	{
		inRange = 0;
	}
}
if ( inRange )
{
	println( "ok" );
}

println( "serialize" );
var h = { 1:100, 2:200 };
var h2 = std::deserialize( std::serialize(h) );
println( hash::count(h2) + " " + h2[2] + " " + h2[1] );

var na[] = { 7, 8 };
var nb[] = { 5, 6, na };
var nh = { "z":9 };
var n = { 1:nb, "k":nh, 3:"x" };
var n2 = std::deserialize( std::serialize(n) );
println( n2["k"]["z"] );
println( n2[1][2][1] );
println( n2[3] );

var bigs = "0123456789abcdef";
while( str::strlen(bigs) < 70000 )
{
	bigs += bigs;
}
println( str::strlen(bigs) + " " + std::serialize(bigs) );

// a self-referencing array must be refused, not recursed into forever;
// the cycle is broken right away since the gc can not mark it yet
var self[2];
self[0] = self;
var selfSer = std::serialize( self );
self[0] = 0;
println( "self " + selfSer );

var deep[] = { 1 };
for( var k = 0; k < 10; ++k )
{
	var wrap[1];
	wrap[0] = deep;
	deep = wrap;
}
var deep2 = std::deserialize( std::serialize(deep) );
if ( deep2[0][0][0][0][0][0][0][0][0][0][0] == 1 )
{
	println( "deep ok" );
}

println( "malformed" );
println( std::deserialize(str::format("%c%c%c%c", 3, 224, 0, 0)) );
println( std::deserialize(str::format("%c%c%c%c", 3, 224, 255, 255)) );
println( std::deserialize(str::format("%c%c%c%c%c%c", 3, 160, 5, 10, 0, 65)) );
println( std::deserialize(str::format("%c%c%c", 0, 1, 2)) );
println( std::deserialize(str::format("%c%c%c%c%c%c%c", 3, 224, 5, 0, 1, 0, 9)) );
var nest = "";
for( var j = 0; j < 100; ++j )
{
	nest += str::format( "%c%c%c%c%c", 3, 160, 4, 1, 0 );
}
nest += str::format( "%c%c%c%c%c", 0, 1, 0, 0, 0 );
println( std::deserialize(nest) );

println( "done" );
