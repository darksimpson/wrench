/*~
5
6
5
5
10
30
1
7
4
0,0,0,0
0
3
~*/

var push[];
if ( list::count(push) != 0 ) println("list1");

list::push( push, 33 );
if ( list::count(push) != 1 ) println("list2");
if ( push[0] != 33 )println("list3");

list::push( push, 44 );
if ( list::count(push) != 2 ) println("list4");
if ( push[0] != 44 ) println("list5");
if ( push[1] != 33 ) println("list6");

var popb[] = { 10, 20, 30 };
list::pop_back( popb );
if ( list::count(popb) != 2 ) println("list7");
if ( popb[1] != 20 ) println("list8");
if ( popb[0] != 10 ) println("list9");

var pop[] = { 10, 20, 30 };
list::pop( pop );

if ( list::count(pop) != 2 ) println( "list10 " + list::count(pop) );
if ( pop[0] != 20 ) println("list11 " + pop[0] );
if ( pop[1] != 30 ) println("list12 " + pop[1] );


var i[] = { 10, 20, 30, 40, 50, 60, 70, 80, 90, 100 };
array::insert( i, 6 );
if ( i[5] != 60 ) println("array0 " + i[5] );
if ( i[6] != 0 ) println("array1 " + i[6] );
if ( i[7] != 70 ) println("array2 " + i[7] );
if ( i[8] != 80 ) println("array3 " + i[8] );

array::insert( i, 2, 2 );
if ( i[1] != 20 ) println("array4 " + i[1] );
if ( i[2] != 0 ) println("array5 " + i[2] );
if ( i[3] != 0 ) println("array6 " + i[3] );
if ( i[4] != 30 ) println("array7 " + i[4] );
if ( i[5] != 40 ) println("array8 " + i[5] );
if ( i[6] != 50 ) println("array9 " + i[6] );

var t[] = { 10, 20, 30, 40, 50, 60, 70, 80, 90, 100 };
array::truncate( t, 4 );
if ( t[3] != 40 ) println("array10" + t[3] );
if ( t._count != 4 ) println("array11" + t._count  );

var r[] = { 10, 20, 30, 40, 50, 60, 70, 80, 90, 100 };
array::remove( r, 1 ); // remove the '20'
if ( r[0] != 10 ) println("array12" + r[0] );
if ( r[1] != 30 ) println("array13" + r[1] );
if ( r[2] != 40 ) println("array14" + r[2] );

array::remove( r, 0 ); // remove the '10'
if ( r[0] != 30 ) println("array12" + r[0] );
if ( r[1] != 40 ) println("array13" + r[1] );
if ( r[2] != 50 ) println("array14" + r[2] );
array::remove( r, 0, 100 ); // remove it all
if ( array::count(r) != 0 ) println("array15" + array::count(r) );

var append[] = { 10, 20 };
array::insert( append, 100 );
if ( append._count != 3 ) println("array16 " + append._count );
if ( append[0] != 10 ) println("array17 " + append[0] );
if ( append[1] != 20 ) println("array18 " + append[1] );
if ( append[2] != 0 ) println("array19 " + append[2] );

array::insert( append, 1, 0 );
array::insert( append, 1, -1 );
if ( append._count != 3 ) println("array20 " + append._count );

array::remove( append, 1, 0 );
array::remove( append, 1, -1 );
if ( append._count != 3 ) println("array21 " + append._count );

append._remove( 100 );
if ( append._count != 3 ) println("array22 " + append._count );

var empty[];
empty._remove( 0 );
if ( empty._count != 0 ) println("array23 " + empty._count );

// push stores the value, not a reference to the caller's variable
var L[];
var x = 5;
list::push_back( L, x );
x = 6;
println( L[0] );
list::push( L, x );
x = 7;
println( L[0] );
function pushLocal( l )
{
	var y = 5;
	list::push_front( l, y );
	y = 99;
}
var L2[];
pushLocal( L2 );
println( L2[0] );
var Q[];
var z = 5;
queue::push( Q, z );
z = 8;
println( Q[0] );

// peek returns a copy
var p[] = { 10, 20, 30 };
println( list::peek(p) );
println( queue::peek(p) );
var s[] = { 1, 2, 3 };
println( stack::peek(s) );
println( list::peek({7, 8}) );

// array::clear zero-fills and bounds its count, array::truncate checks its
// arguments
var b[] = { 1 };
array::clear( b, 4 );
println( array::count(b) );
println( b[0] + "," + b[1] + "," + b[2] + "," + b[3] );
array::clear( b, 268435457 );
println( array::count(b) );
var tr[] = { 1, 2, 3 };
array::truncate( tr );
println( array::count(tr) );

// library functions still replace the variable passed first (prints only
// when it fails)
var cleared[] = { 1, 2 };
array::clear( cleared, 4 );
if ( cleared._count != 4 ) { println( "arg10 ", cleared._count ); }
