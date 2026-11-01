/*~
1
20
30
41
50
60
1
0
70
1
1
82
91
92
150
140
100
~*/

var hitsA = 0;
var hitsB = 0;

function hitA()
{
	hitsA += 1;
	return 60;
}

function hitB()
{
	hitsB += 1;
	return 70;
}

println( 1 ? 1 : 2 );
println( 0 ? 10 : 20 );
println( (0 ? 10 : 20) + 10 );
println( 1 ? (0 ? 40 : 41) : 42 );
println( 0 ? 40 : 1 ? 50 : 51 );

println( 1 ? hitA() : hitB() );
println( hitsA );
println( hitsB );
println( 0 ? hitA() : hitB() );
println( hitsA );
println( hitsB );

var arr[] = { 80, 81, 82 };
println( arr[0 ? 0 : 2] );

var hv = { 1 : 0 ? 90 : 91 };
println( hv[1] );

var hk = { (0 ? 2 : 3) : 92 };
println( hk[3] );

var next = -1;
next = 0 ? 111 : 150;
println( next );

println( 1 + 0 ? 140 : 141 );

goto done;
println( "bad" );
done:
println( 100 );

// every check below prints only when it fails

// true arms (the optimizer merged code across the jump target)
var c = 1;
var x = 0;
x = c ? 7 : 8;
if ( x != 7 ) { println( "tern1 ", x ); }
function tf( a ) { return a ? 5 : 0; }
if ( tf(1) != 5 || tf(0) != 0 ) { println( "tern2" ); }
if ( -(c ? 1 : 2) != -1 ) { println( "tern3" ); }
var ta[] = { 80, 81, 82 };
if ( ta[c ? 0 : 2] != 80 ) { println( "tern4" ); }
var t5 = 5;
var t6 = 6;
var tz = 6;
if ( ((c ? t5 : t6) == tz) != 0 ) { println( "tern5" ); }

// a ternary may follow a postfix operator
var tp = 1;
var tr = tp++ ? 10 : 20;
if ( tr != 10 || tp != 2 ) { println( "tern6 ", tr, " ", tp ); }
tr = tp-- ? (tp-- ? "a" : "b") : "c";
if ( tr != "a" || tp != 0 ) { println( "tern7 ", tr, " ", tp ); }
