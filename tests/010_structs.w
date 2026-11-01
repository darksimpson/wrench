/*~
made t1
made t2
~*/

// these should both be legal, null list length but legal

struct args( a, b )
{
	var c = b;
	var d = a;
}

struct Frame
{
	var Channel;
	var Id;
	var Length;
}
var frame = new Frame ();

frame.Channel = 10;
frame.Id = 20;
frame.Length = 30;

checkStruct( frame );

var z = 1;

struct EMPTY
{
};
var e = new EMPTY;
e = new EMPTY[]{};
e = new EMPTY[]{{}};


struct KWT
{
	var s;
	var tok;
};

var kwds2 = new KWT[]{};
var kwds3 = new KWT[]{{}};

var abs = "and";

var really = { 10, 20 };

var kwds = new KWT[]
{
	{"abs", 0},
	{abs, z},
	{"asc", 2,},
	{"bye", really},
};


if ( kwds[0].s != "abs" || kwds[0].tok != 0 ) println("kwd 0");

if ( kwds[1].s != "and" || kwds[1].tok != 1 ) println("kwd 1");
if ( kwds[2].s != "asc" || kwds[2].tok != 2 ) println("kwd 2");
if ( kwds[3].s != "bye" || kwds[3].tok[1] != 20 ) println("kwd 3");




var instance2 = new a
{
	123,
	42,
	0,
	"bob",
	4 + 5,
};

if ( instance2.a != "bob" ) println("err 2");
if ( instance2.p != 123 ) println("err 1");

var instance3 = new a()
{
};

if ( instance3.p != 23 ) println( "err 3" );

var instance4 = new a
{
	321
};

if ( instance4.p != 321 ) println ("err 4" );
if ( instance4.h != 2 ) println( "err 5" );


var instance0 = new a();
instance0 = new a();
instance0 = new a();
instance0 = new a();
instance0 = new a();


if ( instance0.b != "hello" ) println( "err 6" );

var g = "hi2";
instance0.p = g;
if ( instance0.p != "hi2" ) println("err 7");
instance0.p = "1";
instance0.p = "2";
if ( (instance0.p = "<<>>") != "<<>>" )  println("err 8");
instance0.p = "4";
if ( instance0.p != "4" )  println("err 9");


instance0.p = 20;
if ( instance0.p != 20 )  println("err 10");
instance0.p = "ok2";
if ( instance0.p != "ok2" )  println("err 11");
instance0.p = 1.5;

if ( instance0.p != 1.5 ) println("err 12");
if ( instance0.b != "hello" ) println("err 13");

if ((instance0.b = instance0.p) != 1.5) println("err 14");
if ( instance0.b != 1.5)  println("err 15");

instance0.p = "long string";
if ( (instance0.b = instance0.p) != "long string" ) println("err 15");
if ( instance0.b != "long string") println("err 16");



var j[30] = { 8, 9 };
if ( j[0] != 8 ) println("err 17");
if ( j[1] != 9 ) println("err 18");


var aa = 10;
var bb = 20;
j[10] = { aa, bb + aa, 45, 50, 77, 60 };
if ( j[0] != 10 )println("err 19");
if ( j[1] != 30 )println("err 20");
if ( j[2] != 45 )println("err 21");
if ( j[5] != 60 )println("err 22");

var in = new a;

in.a = 8;
in.b = 7;
if ( in.a == in.b && 1 ) println("G1");
if ( in.a != in.a && 1 ) println("G2");
if ( in.a < in.b && 1 )  println("G3");
if ( in.b > in.a && 1 )  println("G4");
if ( in.b >= aa && 1 ) println("G5");
if ( in.a <= in.b && 1 ) println("G6");

localIf();
function localIf()
{
	::in.a = 8;
	::in.b = 7;
	if ( ::in.a == ::in.b && 1 ) println("L1");
	if ( ::in.a != ::in.a && 1 ) println("L2");

}

var n;
if ( n != n )
{
	println( "null NOT okay" );
}

in.a = 2;

if ( in.a + in.a + in.a + in.a != 8 ) println("err 23");

// play with the parser:
struct Bob
{//{
/*{*/};

function/**/a()//{
{
	var p = 23;
	var h = 2;
	var b = "hello";
	var a;
	b;
}

instance2 = new a
{
	321,
	42,
	"bob",
	in.a,
	4 + 5,
};

if ( instance2.p != 321 ) println("err 24");
if ( instance2.a != 2 ) println("err 25");

instance3 = new a
{
};

if ( instance3.p != 23 ) println("err 26");


instance4 = new a
{
	321
};

if ( instance4.p != 321) println("err 27");
if ( instance4.h != 2 ) println("err 28");

struct/* */particle
{// 
	var x;
	var y;
	var z; // specifically does NOT get the 'global' z because this is a struct
	var vx;
	var vy;
	var vz;
};

function update_particle(p)
{
	p.x+=p.vx;
	p.y+=p.vy;
	p.z+=p.vz;
}

function update(particles)
{
	var p;
	for( p=0; p<500; ++p )
	{
		update_particle(particles[p]);
	}
}

function update_several_times(particles, count)
{
	var i;
	for ( i = 0; i < count; ++i)
	{
		update(particles);
	}
}

function update_several_timesI( particles, count )
{
	var i;
	var p;
	for ( i = 0; i < count; ++i )
	{
		for( p=0; p<500; ++p )
		{
			particles[p].x += particles[p].vx;
			particles[p].y += particles[p].vy;
			particles[p].z += particles[p].vz;
		}
	}
}

var particles[500];
var i;
for ( i = 0; i < 500; ++i )
{
	particles[i] = new particle
	{
		x = i + 0.1,
		y = i + 0.2,
		z = i + 0.3,
		vx = 1.1,
		vy = 2.1,
		vz = 3.1
	};
}


update_several_timesI( particles, 50 );
update_several_times( particles, 50 );

//if ( particles[10].x != 122.299881 ) { println(particles[10].x); println("err 29"); }
//if ( particles[499].y != 713.397705 ) { println(particles[499].y); println("err 30"); }



var t1 = new T1()
{
	55,66
};
if ( t1.a != 55 ) println("t1 1");
if ( t1.b != 66 ) println("t1 2");
if ( t1.c != 0 ) println("t1 3");

var t2 = new T1()
{
	0,555,666
};
if ( t2.a != 0 ) println("t2 1");
if ( t2.b != 555 ) println("t2 2");
if ( t2.c != 666) println("t2 3");

var a = 10;
var t3 = new T1(a);
if ( t3.a != 11 ) println("t3 1");
if ( t3.b != 15 ) println("t3 2");
if ( t3.c != 10 ) println("t3 3");

a = 16;
var t4 = new T1(a) { b = 201, a = 101 };
if ( t4.a != 101 ) println("t4 1");
if ( t4.b != 201 ) println("t4 2");
if ( t4.c != 16 ) println("t4 3");

t4 = new T1(a) { c = 601, b = 501, a = 401  };
if ( t4.a != 401 ) println("t4 1b");
if ( t4.b != 501 ) println("t4 2b");
if ( t4.c != 601 ) println("t4 3b");

struct T1( n ) { var a = 11; var b = 15; var c = n; };

// every check below prints only when it fails (the two lines of output are
// what the Sensor constructor prints)

// a struct with parameters but no member of its own: new gives 0 as for an
// empty struct, the arguments are dropped cleanly
struct NoMembers( a, b ) { }
struct Empty { }
var nm = 0;
for( var nmi = 0; nmi < 100; ++nmi )
{
	nm = new NoMembers( nmi, nmi + 1 );
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
var counter = new Counter( 5 );
if ( counter.count != 5 || counter.start != 6 ) { println( "shadow5 ", counter.count, " ", counter.start ); }

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

// a member of a computed value
struct TS
{
	var name;
}
var mg = new TS;
mg.name = "member";
function getMg() { return mg; }
if ( getMg().name != "member" ) { println( "member1" ); }
if ( (mg).name != "member" ) { println( "member2" ); }
if ( getMg().name + "!" != "member!" ) { println( "member3" ); }
var mh = { "in": { "deep": 3 } };
function getMh() { return mh; }
if ( getMh().in.deep != 3 ) { println( "member4" ); }
