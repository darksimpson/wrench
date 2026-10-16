/*~ done
~*/

// regressions for the audit findings; every check prints only when it fails

// continue inside foreach goes to the next element
var fa[] = { 1, 2, 3 };
var fv;
var fs = 0;
for( fv : fa )
{
	if ( fv == 2 )
	{
		continue;
	}
	fs += fv;
}
if ( fs != 4 ) { println( "fe1 ", fs ); }

var fk;
var fh = { 1:10, 2:20, 3:30 };
var fks = 0;
for( fk, fv : fh )
{
	if ( fk == 2 )
	{
		continue;
	}
	fks += fv;
}
if ( fks != 40 ) { println( "fe2 ", fks ); }

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

// a member of a temporary table or struct survives garbage collection
var gcs;
var gci;
function churn()
{
	gci = 0;
	while( gci < 2000 )
	{
		gcs = "GARBAGE-GARBAGE-GARBAGE-GARBAGE";
		++gci;
	}
	return "!";
}
function mkTable() { var t = { "k" : "VALUE-" + "abcdefghijklmnop" }; return t; }
var tm = mkTable()["k"] + churn();
if ( tm != "VALUE-abcdefghijklmnop!" ) { println( "temp1 ", tm ); }
struct TS
{
	var name;
}
function mkStruct() { var s = new TS; s.name = "STRUCT-" + "0123456789"; return s; }
var sm = mkStruct()["name"] + churn();
if ( sm != "STRUCT-0123456789!" ) { println( "temp2 ", sm ); }

// adding two tables yields 0, not a stale stack value
var ta = { 1:1 };
var tb = { 2:2 };
var stale = "stale";
var tc = ta + tb;
if ( tc != 0 ) { println( "add1 ", tc ); }

// hashing a table that holds itself terminates
var self = { 1:2 };
self[5] = self;
var hs = { };
hs[self] = 7;
if ( hs[self] != 7 ) { println( "hash1" ); }

// library constants are no functions
if ( sys::isFunction("io::O_CREAT") == 1 ) { println( "const1" ); }

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

// integer compound assignments with floats work like the binary operators
var cf = 5.5;
cf %= 2;
if ( cf != 1 ) { println( "fassign1 ", cf ); }
var ci = 7;
ci %= 2.0;
if ( ci != 1 ) { println( "fassign2 ", ci ); }
var cg = 6.0;
cg |= 1;
if ( cg != 7 ) { println( "fassign3 ", cg ); }
var ch = 1.0;
ch <<= 3;
if ( ch != 8 ) { println( "fassign4 ", ch ); }

// _exists on a temporary table
if ( mkTable()._exists("k") != 1 ) { println( "exists1" ); }

// integers wrap instead of overflowing
var big = 2147483647;
++big;
if ( big != -2147483648 ) { println( "wrap1 ", big ); }
var small = -2147483648;
small--;
if ( small != 2147483647 ) { println( "wrap2 ", small ); }
var mul = 65536;
mul *= 65536;
if ( mul != 0 ) { println( "wrap3 ", mul ); }

// a ternary may follow a postfix operator
var tp = 1;
var tr = tp++ ? 10 : 20;
if ( tr != 10 || tp != 2 ) { println( "tern1 ", tr, " ", tp ); }
tr = tp-- ? (tp-- ? "a" : "b") : "c";
if ( tr != "a" || tp != 0 ) { println( "tern2 ", tr, " ", tp ); }

// a member of a computed value
var mg = new TS;
mg.name = "member";
function getMg() { return mg; }
if ( getMg().name != "member" ) { println( "member1" ); }
if ( (mg).name != "member" ) { println( "member2" ); }
if ( getMg().name + "!" != "member!" ) { println( "member3" ); }
var mh = { "in": { "deep": 3 } };
function getMh() { return mh; }
if ( getMh().in.deep != 3 ) { println( "member4" ); }

// a hash initializer key may be a variable
var hk = "key";
var hkt = { hk : 5, "other" : hk };
if ( hkt["key"] != 5 || hkt["other"] != "key" || hkt._count != 2 ) { println( "hashkey1" ); }

// parentheses around a case value
var cv = 5;
var cr = 0;
switch( cv )
{
	case (4): cr = 4; break;
	case ((5)): cr = 5; break;
	case (-1): cr = -1; break;
}
if ( cr != 5 ) { println( "case1 ", cr ); }

// folding a negation keeps the value
if ( -(-128) != 128 ) { println( "neg1 ", -(-128) ); }
if ( -(-32768) != 32768 ) { println( "neg2 ", -(-32768) ); }
if ( -(-127) != 127 ) { println( "neg3" ); }

println( "done" );
