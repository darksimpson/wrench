/*~ done
~*/

// every check prints only when it fails

// included twice, and inc_math.w and inc_util.w include each other: each
// file is compiled once
include "include/inc_math.w";
include "include/inc_util.w"
include "include/inc_math.w";

// both files keep a private count, this file has its own shared one
var count = 7;

if ( mathNext() != 2 || mathNext() != 4 ) { println( "private1" ); }
if ( utilCount() != 101 || utilCount() != 102 ) { println( "private2" ); }
if ( count != 7 ) { println( "private3 ", count ); }
if ( mathTwice(21) != 42 ) { println( "nested1" ); }

// a private function of another file is not this file's: a call goes by
// name at run time and finds nothing, the function name stays free here
function step() { return 9; }
if ( step() != 9 ) { println( "private4" ); }

// private is a plain name anywhere but at the start of a top level statement
function localPrivate()
{
	var private = 3;
	return private + 1;
}
if ( localPrivate() != 4 ) { println( "name1" ); }

println( "done" );
