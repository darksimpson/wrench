/*~ made t1
made t2
done
~*/

// every check prints only when it fails

// a struct with parameters but no member of its own: new gives 0 as for an
// empty struct, the arguments are dropped cleanly
struct NoMembers( a, b ) { }
struct Empty { }
var nm = 0;
for( var i = 0; i < 100; ++i )
{
	nm = new NoMembers( i, i + 1 );
}
var em = new Empty;
if ( nm != 0 || em != 0 ) { println( "nomembers1" ); }

// "var p" with the name of a parameter p is a member; in its initializer p
// is still the parameter, after it p is the member
struct Reading( sensor, value )
{
	var sensor = sensor;
	var value = value * 2;
	var label = "s" + sensor;
}
var r = new Reading( 7, 4 );
if ( r.sensor != 7 || r.value != 8 || r.label != "s7" ) { println( "shadow1 ", r.sensor, " ", r.value, " ", r.label ); }
var r0 = new Reading;
if ( r0.sensor != 0 || r0.value != 0 ) { println( "shadow2" ); }
var rp = new Reading { 1, 2, "x" }; // members in declaration order
if ( rp.sensor != 1 || rp.value != 2 || rp.label != "x" ) { println( "shadow3" ); }
var rn = new Reading( 3, 5 ) { label = "n" };
if ( rn.sensor != 3 || rn.value != 10 || rn.label != "n" ) { println( "shadow4" ); }

// the member is a new variable: "var p;" starts it at 0 like any var, and a
// later "p" means the member
struct Counter( start )
{
	var count = start;
	var start;
	start = count + 1;
}
var c = new Counter( 5 );
if ( c.count != 5 || c.start != 6 ) { println( "shadow5 ", c.count, " ", c.start ); }

// the book's example: a member copied from a parameter of another name
struct Sensor( name, limit )
{
	var label = "sensor " + name;
	var max = limit;
	if ( !max )
	{
		max = 100;
	}
	println( "made " + name );
}
var s1 = new Sensor( "t1", 50 );
var s2 = new Sensor( "t2" );
if ( s1.max != 50 || s2.max != 100 || s1.name != 0 ) { println( "sensor1" ); }

// new is an expression: an argument, a return value, a ternary branch
struct P( x, y )
{
	var px = x;
	var py = y;
}
function sum( p ) { return p.px + p.py; }
function make( n ) { return new P( n, n * 10 ); }
function makeEmpty() { return new P; }
function pick( c ) { return c ? new P( 1, 2 ) : 0; }
if ( sum( new P( 3, 4 ) ) != 7 ) { println( "new1" ); }
if ( sum( new P ) != 0 ) { println( "new2" ); }
if ( make( 2 ).py != 20 || makeEmpty().px != 0 ) { println( "new3" ); }
if ( pick( 1 ).py != 2 || pick( 0 ) != 0 ) { println( "new4" ); }
if ( sum( new P( 1, 1 ) { py = 10 } ) != 11 ) { println( "new5" ); }
if ( new P( 8, 9 ).py != 9 ) { println( "new6" ); }
var total = 0;
for( var k = 0; k < 200; ++k )
{
	total += sum( new P( k, 1 ) );
}
if ( total != 20100 ) { println( "new7 ", total ); }
var slots[3];
slots[1] = new P( 5, 6 );
if ( slots[1].py != 6 ) { println( "new8" ); }
var holder = { };
holder.k = new P( 2, 3 );
if ( holder.k.py != 3 ) { println( "new9" ); }

// an array of instances as a value, and still filled in place after "="
function count( list ) { return list._count; }
if ( count( new P[] { { 1, 2 }, { 3, 4 }, { 5 } } ) != 3 ) { println( "newarray1" ); }
if ( count( new P[] { } ) != 0 ) { println( "newarray2" ); }
var chosen = 0 ? 0 : new P[] { { 7 } };
if ( chosen._count != 1 || chosen[0].px != 7 ) { println( "newarray3" ); }
var direct = new P[] { { 1 }, { 2 } };
if ( direct._count != 2 || direct[1].px != 2 ) { println( "newarray4" ); }

// an array sized by a bare variable
var n = 3;
var sized[n];
if ( sized._count != 3 ) { println( "size1 ", sized._count ); }
function sizedLocal( m )
{
	var local[m];
	return local._count;
}
if ( sizedLocal( 4 ) != 4 ) { println( "size2" ); }

// nested initializers anywhere in a hash table or an array
var h = { "a": { 1, 2 }, "b": 2, "c": { "x": 5 }, "d": 4 };
if ( h["a"][1] != 2 || h.b != 2 || h.c.x != 5 || h.d != 4 ) { println( "nested1" ); }
var h2 = { "a": { 1 }, };
if ( h2.a[0] != 1 ) { println( "nested2" ); }
var arr = { { 1 }, 2, { 3, { 4 } } };
if ( arr[0][0] != 1 || arr[1] != 2 || arr[2][1][0] != 4 ) { println( "nested3" ); }

// enums with commas, a comma after the last entry is fine
enum { EA, EB, EC, }
if ( EC != 2 ) { println( "enum1" ); }

// for ( ;; ) is still a loop without a condition
var loops = 0;
for( ;; )
{
	if ( ++loops == 3 )
	{
		break;
	}
}
if ( loops != 3 ) { println( "for1" ); }

// the largest 32 bit literals still compile
if ( 4294967295 != -1 || 0xFFFFFFFF != -1 ) { println( "literal1" ); }

println( "done" );
