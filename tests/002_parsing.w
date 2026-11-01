/*~
)
;
}
0
1
10
100
1.01
1.00001
0.00004
hello
hello
~*/

println(")");
println(";");
println("}");

var a = {};

// test floating/integer point string converter
println( 0.0 );
println( 1.0 );
println( 10.0 );
println( 100.0 );
println( 1.01 );
println( 1.00001 );
println( .00004 );

if ( "\097" != "a" ) println("a ar");
if ( '\0' != 0 ) println("zero char");
if ( 'a' != 0x61 ) println("err a");
if ( '\r' != 0x0d ) println("err a3");
if ( '\n' != 0x0a ) println("err a4");
if ( '' != 0 ) println("err a5");
if ( 1 != 1 ) println("err one");
if ( 0x13 != 19 ) println("err hex");
if ( 023 != 19 ) println("err octal");

// test crazy comment combinations

/***//*
 */
// comment

println( "hello" ); // eol comment
println( /**/"hello"  /*  */ ); /*  *//*
*/

blank blank0;
blank var blank1;
var blank blank2;
if ( blank0 != 0 ) println("err blank0");
if ( blank1 != 0 ) println("err blank1");
if ( blank2 != 0 ) println("err blank2");

a = 10 / 2;
if ( a != 5 ) println("err 1");
a = 10/	/*h*/2;
if ( a != 5 ) println("err 1");
a = 10/**//	/*h*/2;
if ( a != 5 ) println("err 1");

// empty statements push nothing, so must pop nothing; every check below
// prints only when it fails
function es( a, b ) { var t = a + b;; return t; }
if ( es(3, 4) != 7 ) { println( "empty1" ); }
var ek = 10;
for( var ei=0; ei<300; ++ei ) { ; }
if ( ek + 1 != 11 ) { println( "empty2" ); }

// quoted punctuation is data
var qa[] = { "x", ",", "y" };
if ( qa._count != 3 || qa[1] != "," ) { println( "quote1" ); }
var qb[] = { "{", "}" };
if ( qb[0] != "{" || qb[1] != "}" ) { println( "quote2" ); }
var qh = { "k" : "{" };
if ( qh["k"] != "{" ) { println( "quote3" ); }
function qf() { "}"; return 5; }
if ( qf() != 5 ) { println( "quote4" ); }
var qx = 1;
if ( qx ) ";"; else println( "quote5" );

// literals
if ( 1e3 != 1000 || 2.5e-1 != 0.25 || 09.5 != 9.5 || 010 != 8 || 07e1 != 70 ) { println( "num1" ); }
if ( '\xFF' != 255 || '\x41' != 65 ) { println( "char1" ); }
var es1 = "a\x41\tb";
if ( es1._count != 4 || es1[1] != 'A' ) { println( "escape1" ); }

// the largest 32 bit literals still compile
if ( 4294967295 != -1 || 0xFFFFFFFF != -1 ) { println( "literal1" ); }
