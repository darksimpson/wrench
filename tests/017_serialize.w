/*~
2 200 100
9
8
x
131072 0
self 0
deep ok
0
0
0
0
0
0
~*/

// check HASH
var d = { 1:100, "2":200, "3":"300", 40:400, 50:500 };
var d1 = std::serialize( d );
var d2 = std::deserialize( d1 );
if ( d2[1] != 100 ) println("d0");
if ( d2["2"] != 200 ) println("d1");
if ( d2[50] != 500 ) println("d2");
if ( d2["3"] != "300" ) println("d3");

// check ARRAY
var c[] = { 1, 2, 3 };
var c1 = std::serialize( c );
var c2 = std::deserialize( c1 );
if ( c2[1] != 2 ) println( "c0" );
if ( c2._count != 3 ) println( "c1" );

// check int and float
var a = 10;
var a1 = std::serialize( a );
var a2 = std::deserialize( a1 );
if ( a2 != 10 ) println("a0");
a = 10.5;
a1 = std::serialize( a );
a2 = std::deserialize( a1 );
if ( a2 != 10.5 ) println("a1");

// String (char array)
var b = "a test string";
var b1 = std::serialize( b );
var b2 = std::deserialize( b1 );
if ( b2 != "a test string" ) println( "b0" );

// and now ALL OF THEM! fire zee missiles!
var e = { b, a, c, d };
var f = { 1:e, 2:e, 3:d };

var f1 = std::serialize( f );
var f2 = std::deserialize( f1 );

if( f2[1][1] != 10.5 ) println( "f0" );
if( f2[2][0] != "a test string" ) println( "f1" );
if( f2[3]["3"] != "300" ) println( "f2" );

// BTW encode/decode the most complicated one to make sure it plays well
// with the file system
io::writeFile( "_serialize_test.bin", std::serialize(f) );

var g = std::deserialize( io::readFile( "_serialize_test.bin" ) );

if( g[1][1] != 10.5 ) println( "g0" );
if( g[2][0] != "a test string" ) println( "g1" );
if( g[3]["3"] != "300" ) println( "g2" );

// hash tables keep their counts, nested containers round trip
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

// a string too large for the format is refused (0), not truncated
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

// malformed or truncated input gives 0
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

// every check below prints only when it fails

// round trips, also through the growing output buffer
var big[3000];
for( var bi = 0; bi < 3000; ++bi ) { big[bi] = bi * 3; }
var bs = std::serialize( big );
if ( str::strlen(bs) != 5 + 3000 * 5 ) { println( "ser1 ", str::strlen(bs) ); }
var bb = std::deserialize( bs );
if ( bb._count != 3000 || bb[0] != 0 || bb[2999] != 8997 ) { println( "ser2" ); }

var nested = { 1:"abc", 2:7, "k":2.5 };
nested[3] = big;
var nested2 = std::deserialize( std::serialize(nested) );
if ( nested2[1] != "abc" || nested2[2] != 7 || nested2["k"] != 2.5 || nested2[3][1500] != 4500 ) { println( "ser3" ); }

// serializing in a loop collects garbage but keeps live values
for( var gi = 0; gi < 50; ++gi )
{
	var gs = std::serialize( big );
	if ( str::strlen(gs) != 15005 ) { println( "ser4" ); }
}
if ( big[2999] != 8997 || bs != std::serialize(big) ) { println( "ser5" ); }

// hostile input is refused before anything large is allocated
var hdr = str::format( "%c%c%c%c%c", 3, 160, 4, 255, 255 ); // array of 65535 values, no elements
if ( std::deserialize(hdr) != 0 ) { println( "des1" ); }
var nestHdr = "";
for( var nj = 0; nj < 64; ++nj ) { nestHdr += hdr; }
for( var nk = 0; nk < 20; ++nk )
{
	if ( std::deserialize(nestHdr) != 0 ) { println( "des2" ); }
}
var chdr = str::format( "%c%c%c%c%cabc", 3, 160, 3, 255, 255 ); // 65535 chars, three present
if ( std::deserialize(chdr) != 0 ) { println( "des3" ); }
var two = str::format( "%c%c%c%c%c", 3, 160, 4, 2, 0 ) + std::serialize( 5 ) + std::serialize( "xy" );
var tw = std::deserialize( two );
if ( tw._count != 2 || tw[0] != 5 || tw[1] != "xy" ) { println( "des4" ); }
var twoShort = str::format( "%c%c%c%c%c", 3, 160, 4, 3, 0 ) + std::serialize( 5 ) + std::serialize( "xy" );
if ( std::deserialize(twoShort) != 0 ) { println( "des5" ); }
