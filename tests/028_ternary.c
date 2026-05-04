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
