/*~
one
two
3
6
6
7
seven
~*/

var s;
s["key"] = "value";
if ( s["key"] != "value" ) { println("s0"); }
var t = s["key"];
s["2"] = 2;
s["3"] = 3;
s["4"] = 4;
if ( t != "value" ) { println("s1"); }

var hashTable = { 1:"one", 2:"two", 3:3, "str":6 };

if ( hashTable[1] != "one" ) println("h0");
println( hashTable[1] ); // "one"
if ( hashTable[2] != "two" ) println("h1");
println( hashTable[2] ); // "two"
if ( hashTable[3] != 3 ) println("h2");
println( hashTable[3] ); // 3

println( hashTable["str"] ); // 6

println( hashTable.str ); // 6

hashTable.str = 7;
println( hashTable.str ); // 6
hashTable.str = "seven";
println( hashTable.str ); // 6

// every check below prints only when it fails

// member syntax keeps a stored string key
var named = { "name": 5 };
named.name = 6;
var nk;
var nv;
for( nk, nv : named ) { if ( nk != "name" || nk._count != 4 || nv != 6 ) { println( "member1 ", nk ); } }

// reading a member or element of a value that is no container gives 0 and
// leaves the value alone; assigning to one still makes it a container
var ms = "abc";
var r = ms.length;
if ( r != 0 || ms != "abc" ) { println( "read1 ", r, " ", ms ); }

var n = 5;
r = n.x;
if ( r != 0 || n != 5 ) { println( "read2 ", r, " ", n ); }

var f = 1.5;
r = f[3];
if ( r != 0 || f != 1.5 ) { println( "read3 ", r, " ", f ); }

var e;
r = e.k;
r = e[2];
if ( r != 0 || e != 0 ) { println( "read4 ", r, " ", e ); }

var deep = 7;
r = deep.a.b;
if ( r != 0 || deep != 7 ) { println( "read5 ", r, " ", deep ); }

// a guard reads without side effects
var p = 0;
if ( p.ready ) { println( "read6" ); }
if ( p != 0 ) { println( "read7 ", p ); }

// reads of a container never create: a missing key or member, an element
// past the end of an array or a string
var rh = { "x": 1 };
var missing = rh["missing"];
var missingMember = rh.zzz;
if ( missing != 0 || missingMember != 0 || rh._count != 1 || rh._exists( "missing" ) ) { println( "read8 ", rh._count ); }
var seen = 0;
for( nk, nv : rh ) { ++seen; }
if ( seen != 1 ) { println( "read9" ); }
var ra[] = { 1, 2, 3 };
if ( ra[10] != 0 || ra._count != 3 ) { println( "read10 ", ra._count ); }
var rs = "abc";
if ( rs[10] != 0 || rs._count != 3 ) { println( "read11 ", rs._count ); }

// writes create containers and grow them
rh["y"] = 2;
rh.z += 3;
rh["w"]++;
ra[4] = 5;
if ( rh._count != 4 || rh.z != 3 || rh["w"] != 1 || ra._count != 5 || ra[3] != 0 ) { println( "write1" ); }

var h;
h.k = 5;
if ( h.k != 5 ) { println( "write2" ); }

var a;
a[2] = 9;
if ( a._count != 3 || a[2] != 9 ) { println( "write3" ); }

var m;
m.a.b = 1;
if ( m.a.b != 1 ) { println( "write4" ); }

var c;
c.n += 4;
c.n++;
if ( c.n != 5 ) { println( "write5 ", c.n ); }

var g;
function setG() { g.k = 9; }
setG();
if ( g.k != 9 ) { println( "write6" ); }

// existing containers work as before
var arr[] = { 1, 2 };
arr[1] += 5;
arr[0]++;
if ( arr[0] != 2 || arr[1] != 7 ) { println( "elem1 ", arr[0], " ", arr[1] ); }

var mt = { "x": { "y": 3 } };
mt.x.y += 1;
if ( mt.x.y != 4 ) { println( "member2" ); }

struct S { var v; }
var st = new S;
st.v = 4;
st.v++;
if ( st.v != 5 ) { println( "struct1" ); }

var text = "text";
if ( text[1] != 'e' || text != "text" ) { println( "string1" ); }
