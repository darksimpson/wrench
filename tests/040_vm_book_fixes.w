/*~ 3
3
4 4
done
~*/

// VM bugs found while writing the book; every check prints only when it
// fails (the lines before "done" are what the natives below print, the test
// println writes each argument on a line of its own)

// arguments are values: a change to a parameter never reaches the caller
function bump( n ) { n += 1; ++n; n++; --n; n -= 0; n *= 2; }
function setTo( n, o ) { n = o; }
function bumpMember( m ) { m += 10; m++; }
var a = 1;
bump( a );
if ( a != 1 ) { println( "arg1 ", a ); }
var b = 7;
setTo( a, b );
if ( a != 1 || b != 7 ) { println( "arg2 ", a, " ", b ); }
var h = { "k": 1 };
bumpMember( h.k );
var arr[] = { 5 };
bumpMember( arr[0] );
if ( h.k != 1 || arr[0] != 5 ) { println( "arg3 ", h.k, " ", arr[0] ); }
function localCaller() { var l = 3; bump( l ); setTo( l, 9 ); return l; }
if ( localCaller() != 3 ) { println( "arg4" ); }

// containers are still shared
function fill( list, table ) { list[0] = 42; table.added = 1; }
var shared[] = { 0 };
var sharedTable = { "x": 1 };
fill( shared, sharedTable );
if ( shared[0] != 42 || sharedTable.added != 1 ) { println( "arg5" ); }

// every argument is the value it had when it was evaluated
function two( x, y ) { return x * 10 + y; }
var j = 1;
if ( two( j, j++ ) != 11 || j != 2 ) { println( "arg6 ", j ); }
var g = 4;
function setG() { g = 100; return 0; }
if ( two( g, setG() ) != 40 ) { println( "arg7" ); }
var hm = { "k": 1 };
if ( two( hm.k, hm.k = 5 ) != 15 ) { println( "arg7a" ); }
var p = 3;
if ( two( p, p += 4 ) != 37 ) { println( "arg7b" ); }
var e = 1;
if ( two( e, e = 9 ) != 19 ) { println( "arg7c" ); }
function local3( q ) { var r = q; return two( r, r++ ) * 10 + two( q, ++q ); }
if ( local3( 2 ) != 22 * 10 + 23 ) { println( "arg7d ", local3( 2 ) ); }
var i = 3;
println( i, i++ );
println( str::format( "%d %d", i, i++ ) );
if ( i != 5 ) { println( "arg8 ", i ); }

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

// library functions still replace the variable passed first
var cleared[] = { 1, 2 };
array::clear( cleared, 4 );
if ( cleared._count != 4 ) { println( "arg10 ", cleared._count ); }

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

// a float index on an array or a string is the int it truncates to
var fa[] = { 10, 20, 30 };
if ( fa[1.7] != 20 || fa[2.2] != 30 ) { println( "float6" ); }
fa[1.9] = 21;
fa[0.5] += 1;
if ( fa[0] != 11 || fa[1] != 21 || fa._count != 3 ) { println( "float7" ); }
var fs = "abc";
if ( fs[1.5] != 'b' ) { println( "float8" ); }
var fx;
fx[1.5] = 3;
if ( fx[1.5] != 3 || fx._count != 1 ) { println( "float9" ); }

// member syntax keeps a stored string key
var named = { "name": 5 };
named.name = 6;
var nk;
var nv;
for( nk, nv : named ) { if ( nk != "name" || nk._count != 4 || nv != 6 ) { println( "member1 ", nk ); } }

// reads never create
var rh = { "x": 1 };
var missing = rh["missing"];
var missingMember = rh.zzz;
if ( missing != 0 || missingMember != 0 || rh._count != 1 || rh._exists( "missing" ) ) { println( "read1 ", rh._count ); }
var seen = 0;
for( nk, nv : rh ) { ++seen; }
if ( seen != 1 ) { println( "read2" ); }
var ra[] = { 1, 2, 3 };
if ( ra[10] != 0 || ra._count != 3 ) { println( "read3 ", ra._count ); }
var rs = "abc";
if ( rs[10] != 0 || rs._count != 3 ) { println( "read4 ", rs._count ); }
// writes still create and grow
rh["y"] = 2;
rh.z += 3;
rh["w"]++;
ra[4] = 5;
if ( rh._count != 4 || rh.z != 3 || rh["w"] != 1 || ra._count != 5 || ra[3] != 0 ) { println( "read5" ); }

// ~ works on the int a float truncates to
if ( ~2.5 != -3 || ~(-1.5) != 0 || ~3 != -4 ) { println( "not1" ); }

// strings order by their bytes
if ( !("aa" < "b") || !("b" > "aa") || !("abc" <= "abc") || !("ab" < "abc") || ("abc" >= "abd") ) { println( "order1" ); }
var s1 = "apple";
var s2 = "banana";
if ( s1 > s2 || !(s1 < s2) || s2 <= s1 ) { println( "order2" ); }

// a string is never equal to a number, not even to its hash
var str = "abc";
var strHash = str._hash;
if ( str == strHash || !(str != strHash) || strHash == str ) { println( "eq1" ); }
var ne[] = { 1 };
if ( ne == ne._hash ) { println( "eq2" ); }

// _exists on an array or a string: whether the index is in range
if ( !ra._exists( 4 ) || ra._exists( 5 ) || ra._exists( -1 ) || !rs._exists( 0 ) || rs._exists( 3 ) ) { println( "exists1" ); }

// _remove gives the container back, so removals chain
var rm = { "a": 1, "b": 2, "c": 3 };
rm._remove( "a" )._remove( "b" );
if ( rm._count != 1 || !rm._exists( "c" ) ) { println( "remove1" ); }

// += with a string on an element of a container stores the sum in it
var sh = { "k": "a", "i": 5 };
sh["k"] += "b";
if ( sh["k"] != "ab" ) { println( "cat1 ", sh["k"] ); }
sh["k"] += 7;
if ( sh["k"] != "ab7" ) { println( "cat2 ", sh["k"] ); }
sh["i"] += "x";
if ( sh["i"] != "5x" ) { println( "cat3 ", sh["i"] ); }
var sa[] = { "a" };
sa[0] += "b";
var sr = ( sa[0] += "c" );
if ( sa[0] != "abc" || sr != "abc" ) { println( "cat4 ", sa[0], " ", sr ); }
struct SC { var name = "n"; };
var sc = new SC;
sc.name += "x";
if ( sc.name != "nx" ) { println( "cat5 ", sc.name ); }
var sn = { "in": { "k": "a" } };
sn["in"]["k"] += "b";
if ( sn["in"]["k"] != "ab" ) { println( "cat6 ", sn["in"]["k"] ); }
function catLocal() { var l = { "k": "q" }; l["k"] += "r"; return l["k"]; }
if ( catLocal() != "qr" ) { println( "cat7" ); }
sh["f"] = 1.5;
sh["f"] += 1;
if ( sh["f"] != 2.5 ) { println( "cat8 ", sh["f"] ); }

println( "done" );
