/*~ done
~*/

// reading a member or element of a value that is no container gives 0 and
// leaves the value alone; assigning to one still makes it a container.
// Every check prints only when it fails

var s = "abc";
var r = s.length;
if ( r != 0 || s != "abc" ) { println( "read1 ", r, " ", s ); }

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

// a guard reads without side effects now
var p = 0;
if ( p.ready ) { println( "read6" ); }
if ( p != 0 ) { println( "read7 ", p ); }

// writes create containers as before
var h;
h.k = 5;
if ( h.k != 5 ) { println( "write1" ); }

var a;
a[2] = 9;
if ( a._count != 3 || a[2] != 9 ) { println( "write2" ); }

var m;
m.a.b = 1;
if ( m.a.b != 1 ) { println( "write3" ); }

var c;
c.n += 4;
c.n++;
if ( c.n != 5 ) { println( "write4 ", c.n ); }

var g;
function setG() { g.k = 9; }
setG();
if ( g.k != 9 ) { println( "write5" ); }

// existing containers work as before
var arr[] = { 1, 2 };
arr[1] += 5;
arr[0]++;
if ( arr[0] != 2 || arr[1] != 7 ) { println( "elem1 ", arr[0], " ", arr[1] ); }

var t = { "x": { "y": 3 } };
t.x.y += 1;
if ( t.x.y != 4 ) { println( "member1" ); }

struct S { var v; }
var st = new S;
st.v = 4;
st.v++;
if ( st.v != 5 ) { println( "struct1" ); }

var text = "text";
if ( text[1] != 'e' || text != "text" ) { println( "string1" ); }

println( "done" );
