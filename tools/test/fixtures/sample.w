var g = 10;
var s = "hello";
var arr[] = { 1, 2.5, "x" };
struct Pt
{
	var x;
	var y = 7;
};
var p = new Pt;
var h = { "a":1, 2:"two" };
function add( a, b )
{
	var c = a + b;
	return c;
}
function outer( n )
{
	var r = add( n, 1 );
	return r * 2;
}
var total = 0;
for( var i=0; i<5; ++i )
{
	total += outer( i );
}
println( "total=", total );
g = 99;
