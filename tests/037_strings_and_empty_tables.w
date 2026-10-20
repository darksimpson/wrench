/*~ done
~*/

// every check prints only when it fails

// writes to the characters of a string land in the string
var s = "ab";
s[1] = 'z';
if ( s != "az" ) { println( "char1 ", s ); }
s[0] += 1;
s[1]--;
if ( s != "by" ) { println( "char2 ", s ); }
++s[0];
if ( s[0] != 'c' ) { println( "char3" ); }

// a write past the end grows the string and stores the character
s[4] = 'e';
if ( s._count != 5 || s[2] != 0 || s[3] != 0 || s[4] != 'e' ) { println( "char4 ", s._count ); }

function local()
{
	var t = "cd";
	t[0] = 'x';
	var u = t + "!";
	u[2] = '?';
	return t + u;
}
if ( local() != "xdxd?" ) { println( "char5 ", local() ); }

// strings inside containers
var h = { "s": "ab" };
h.s[0] = 'q';
if ( h.s != "qb" ) { println( "char6 ", h.s ); }
var a[] = { "ab", "cd" };
a[1][1] = 'k';
if ( a[1] != "ck" || a[0] != "ab" ) { println( "char7" ); }

// a string is shared like an array: assigning it does not copy it, and
// a character written through either name shows in both
var c = s;
c[0] = 'Z';
if ( s[0] != 'Z' || c[0] != 'Z' ) { println( "char8" ); }
c += "!";
if ( s._count != 5 || c._count != 6 ) { println( "char9" ); }

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

println( "done" );
