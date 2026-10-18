/*~ done
~*/

// VM open issues; every check prints only when it fails

// arrays grow with spare room: appending is linear, and room reused after
// a truncation is cleared
var a[] = { 0 };
for( var i = 1; i < 5000; ++i )
{
	a[a._count] = i;
}
if ( a._count != 5000 || a[4999] != 4999 || a[1234] != 1234 ) { println( "grow1 ", a._count ); }
array::truncate( a, 10 );
a[20] = 7;
if ( a._count != 21 || a[9] != 9 || a[10] != 0 || a[15] != 0 || a[20] != 7 ) { println( "grow2" ); }
var f[] = { 1.5 };
for( var fi = 1; fi < 300; ++fi ) { f[fi] = fi + 0.5; }
array::truncate( f, 2 );
f[5] = 9;
if ( f._count != 6 || f[1] != 1.5 || f[2] != 0 || f[4] != 0 || f[5] != 9 ) { println( "grow3" ); }

// the hash of an empty slot (0xABABABAB) works as a key like any other
var M = -1414812757;
var h = { "x": 0 };
h[M] = 5;
h[7] = 1;
if ( h[M] != 5 || h._count != 3 || !h._exists(M) ) { println( "nullkey1 ", h._count ); }
for( var j = 0; j < 200; ++j ) { h[j * 13] = j; }
if ( h[M] != 5 || h._count != 203 ) { println( "nullkey2 ", h._count ); }
var k;
var v;
var seen = 0;
for( k, v : h ) { if ( k == M ) { seen = v; } }
if ( seen != 5 ) { println( "nullkey3" ); }
var copy = std::deserialize( std::serialize( h ) );
if ( copy[M] != 5 || copy._count != 203 ) { println( "nullkey4" ); }
h._remove( M );
if ( h._exists(M) || h._count != 202 ) { println( "nullkey5" ); }

// deep nesting survives collections (marking does not recurse)
var head;
for( var n = 0; n < 5000; ++n )
{
	var node[] = { head, n, "p" + n };
	head = node;
}
var junk;
for( var g = 0; g < 20000; ++g ) { junk = "garbage-garbage" + g; }
var count = 0;
var p = head;
while( p )
{
	if ( p[2] != "p" + p[1] ) { println( "deep1 ", p[1] ); }
	++count;
	p = p[0];
}
if ( count != 5000 ) { println( "deep2 ", count ); }

println( "done" );
