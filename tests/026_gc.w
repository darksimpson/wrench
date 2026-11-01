/*~ done
~*/

// values the garbage collector must keep, with collections forced all the
// time (the test state has a GC hint of 0); every check prints only when it
// fails

// a container that holds itself survives garbage collection
var cyc[2];
cyc[0] = cyc;
cyc[1] = 42;
for( var ci=0; ci<200; ++ci )
{
	var trash = "garbage " + ci;
}
if ( cyc[0][1] != 42 ) { println( "cycle1" ); }

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
