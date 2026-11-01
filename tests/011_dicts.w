/*~ ~*/

var hashTest = { 1:10, 2:20, "three":30 };
if ( hashTest._exists(1) != 1 ) println("x1");
if ( hashTest._exists(3) != 0 ) println("x2");
if ( hashTest._exists("three") != 1 ) println("x3");
var somethingH._remove( 1 );
if ( somethingH._exists(1) != 0 ) println("x4");
somethingH._remove( "three" );
if ( somethingH._exists("three") != 0 ) println("x4");

var blue   = { 10:"one", 20:"two", "three":30 };
var green[]  = { 1, 2, blue };
var red[]    = { green, 3 };

var colors[] = { red, red };

if ( colors[0][0][1] != 2 ) println("sub 0");
if ( colors[0][0][1] != 2 ) println("sub 1");
if ( colors[0][1] != 3 ) println("sub 2");
if ( blue[10] != "one" ) println("sub 3");
if ( colors[0][0][2]["three"] != 30 ) println("sub 4");
if ( colors[0][1][2]["three"] != 0 ) println("sub 5");


var table =
{
	"1":1,
	"2":2,
	3:"3",
	4:"4"
};

if ( table["1"] != 1 ) println( "bad 1" );
if ( table["2"] != 2 ) println( "bad 2" );
if ( table[3] != "3" ) println( "bad 3" );
if ( table[4] != "4" ) println( "bad 4" );
if ( table[5] != 0 ) println( "bad 5" );


var table2 =
{
	"1":1,
	"2":2,
};

if ( table2["2"] != 2 ) println( "bad 22" );

table[6] = 6;
table[6] = "7";
table[6] = 8;
table[6] = "9";
table[6] = "9";
table[6] = "9";
table[6] = "9";
table[6] = "9";
table[6] = "9";
table[6] = "9";
table[6] = "9";
table[6] = "10";
if ( table[6] != "10" ) println( "bad 6" );


table[7] = "10";
table[8] = "10";
table[9] = "10";
table["a"] = "10";
table["b"] = "10";
table["c"] = "10";
table["d"] = "10";
if ( table[4] != "4" ) { println( "bad 4d" ); println(table[4]); }
goto j;
j:
table["e"] = "10";
if ( table[4] != "4" ) { println( "bad 4d" ); println(table[4]); }
table["f"] = "10";
table["g"] = "10";
table["h"] = "10";
table["i"] = "10";
table["j"] = "10";
table["k"] = "10";
table["7"] = "11";
table["m"] = "10";

if ( table.m != "10" ) println( "bad m" );

if ( table["1"] != 1 ) println( "bad 1a" );
if ( table["2"] != 2 ) println( "bad 2b" );
if ( table[3] != "3" ) println( "bad 3c" );
if ( table[4] != "4" ) { println( "bad 4d" ); println(table[4]); }
if ( table[5] != 0 ) println( "bad 5e" );

if ( table["7"] != "11" ) println( "bad 71" );

var aa = {:};

var a = { "a" : 2 }; // creating a blank hash table

a = "hello";
var b = "hello";
if ( a != "hello" ) println("string bad 1");
if ( "hello" != a ) println("string bad 2");
if ( b != a ) println("string bad 3");
if ( "why?" != "why?" ) println("string bad 4");



a = 0;
if ( a._hash != 0 ) println( "bad 1") ;

a = 20;
if ( a._hash != 20 ) println( "bad 2") ;

a = "hello";
if ( a._hash != 1335831723 ) println("bad 3");


var hash1 = { "one":100 };
var hash2 = { "two":10, 1:hash1, "member":var hash3 };
hash3 = { "three":30, 2:hash2 };

var array1 = { 1, 2, hash3 };
var array2 = { hash2, array1, 4 };
var array3 = { 50, 60, array2, hash3, hash2 };

if ( array3[2][0]["two"] != 10 ) println("hsh 0");
if ( array3[2][0][1]["one"] != 100 ) println("hsh 1");
if ( array1[0] != 1 ) println("hsh 2" );
if ( array1[1] != 2 ) println("hsh 3" );
if ( 2 != array1[1] ) println("hsh 4" );
if ( 1 != array1[0] ) println("hsh 5" );
if ( array1[2]["three"] != 30 ) println("hsh 6" );
if ( array1[2][2]["two"] != 10 ) println("hsh 7" );
if ( hash1.one != 100 ) println("hsh 8" );
if ( hash2.two != 10 ) println("hsh 9" );

//if ( hash2.member["three"] != 30 ) println("hsh 9" );  // TODO (yuck)


//a.f( 1 ) == TypeOf<a>::f( a, 1 );

// every check below prints only when it fails

// removed keys stay removed
var rh = { 1:100, 2:200 };
rh._remove( 1 );
if ( rh[1] != 0 ) { println( "remove1" ); }

// _remove gives the container back, so removals chain
var rm = { "a": 1, "b": 2, "c": 3 };
rm._remove( "a" )._remove( "b" );
if ( rm._count != 1 || !rm._exists( "c" ) ) { println( "remove2" ); }

// a hash table counts its entries through growth and removal
var hc = { "a":1 };
for( var hci = 0; hci < 100; ++hci )
{
	hc[hci] = hci;
}
if ( hc._count != 101 ) { println( "count1 ", hc._count ); }
for( var hcr = 0; hcr < 100; ++hcr )
{
	hc._remove( hcr );
}
hc._remove( "a" );
if ( hc._count != 0 ) { println( "count2 ", hc._count ); }

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
if ( gh["zz"] != 5 ) { println( "grow1" ); }

// a hash initializer key may be a variable
var hk = "key";
var hkt = { hk : 5, "other" : hk };
if ( hkt["key"] != 5 || hkt["other"] != "key" || hkt._count != 2 ) { println( "hashkey1" ); }

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

// float keys work in hash tables, keyed by the float
var fh = { 1: "one" };
fh[2.5] = "f";
fh[3] = "three";
if ( fh[2.5] != "f" || fh[1] != "one" || fh[3] != "three" || fh._count != 3 ) { println( "float1" ); }
var fn = { 2.5: 7, 1.25: 8 };
if ( fn[2.5] != 7 || fn[1.25] != 8 || fn._count != 2 ) { println( "float2" ); }
fn[2.5] += 3;
fn[1.25]++;
if ( fn[2.5] != 10 || fn[1.25] != 9 ) { println( "float3" ); }
var fk;
var fv;
var keySum = 0.0;
var valueSum = 0;
for( fk, fv : fn ) { keySum += fk; valueSum += fv; }
if ( keySum != 3.75 || valueSum != 19 ) { println( "float4 ", keySum, " ", valueSum ); }
if ( !fn._exists( 2.5 ) || fn._exists( 2.0 ) ) { println( "float5" ); }

// hashing a table that holds itself terminates
var self = { 1:2 };
self[5] = self;
var hs = { };
hs[self] = 7;
if ( hs[self] != 7 ) { println( "self1" ); }

// adding two tables yields 0, not a stale stack value
var ta = { 1:1 };
var tb = { 2:2 };
var stale = "stale";
var tc = ta + tb;
if ( tc != 0 ) { println( "add1 ", tc ); }

// _exists on a temporary table
function mkTempTable() { var t = { "k" : "value" }; return t; }
if ( mkTempTable()._exists("k") != 1 ) { println( "exists1" ); }

// "{ }" is an empty hash table, or an empty array after "var x[] ="; a
// copy of it is the same container
var e = { };
var e2 = e;
e2["k"] = 1;
if ( e._count != 1 || e.k != 1 ) { println( "empty1 ", e._count ); }

var x[] = { };
var x2 = x;
x2[0] = 3;
if ( x._count != 1 || x[0] != 3 ) { println( "empty2 ", x._count ); }

var none = { };
var n = 0;
var key;
var val;
for( key, val : none ) { ++n; }
if ( n != 0 || none._count != 0 ) { println( "empty3" ); }

// nested empty initializers are tables of their own
var nest[] = { { }, { } };
var first = nest[0];
first["a"] = 1;
if ( nest[0]._count != 1 || nest[1]._count != 0 ) { println( "empty4" ); }
var inner = { "t": { } };
var t = inner.t;
t[7] = 8;
if ( inner.t[7] != 8 ) { println( "empty5" ); }

function fresh() { return { }; }
var f1 = fresh();
var f2 = fresh();
f1[1] = 1;
if ( f2._count != 0 || f1._count != 1 ) { println( "empty6" ); }
