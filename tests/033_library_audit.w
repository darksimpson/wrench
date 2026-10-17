/*~ done
~*/

// regressions for the standard library audit; every check prints only when it fails

// str::sprintf makes the destination a new string: variables that shared
// the old one keep it
var sa = "hello";
var sb = sa;
str::sprintf( sb, "x%d", 1 );
if ( sa != "hello" || sb != "x1" ) { println( "sp1 ", sa, " ", sb ); }

var sc = "longer string here";
var sd = sc;
str::sprintf( sd, "%s", "a much much longer string than before, way longer" );
if ( sc != "longer string here" ) { println( "sp2 ", sc ); }
if ( sd != "a much much longer string than before, way longer" ) { println( "sp3 ", sd ); }

var sarr[] = { "one", "two" };
var se = sarr[0];
str::sprintf( se, "Z" );
if ( sarr[0] != "one" || se != "Z" ) { println( "sp4 ", sarr[0] ); }

function spf()
{
	var s = "literal!";
	var r = s;
	str::sprintf( s, "%d", 7 );
	return r;
}
if ( spf() != "literal!" || spf() != "literal!" ) { println( "sp5" ); }

// the destination may also be an argument (or the format)
var ss = "";
for( var si = 0; si < 100; ++si ) { ss += "a"; }
var sexp = ss + "!";
str::sprintf( ss, "%s!", ss );
if ( str::strlen(ss) != 101 || ss != sexp ) { println( "sp6 ", str::strlen(ss) ); }

var st = "abcdefgh";
str::sprintf( st, "%s%s", "XY", st );
if ( st != "XYabcdefgh" ) { println( "sp7 ", st ); }

var sf = "%d-%d";
str::sprintf( sf, sf, 1, 2 );
if ( sf != "1-2" ) { println( "sp8 ", sf ); }

var sl = "abc";
var sret = str::sprintf( sl, "%s%s%s%s", "0123456789012345678901234567890123456789", "0123456789012345678901234567890123456789", sl, sl );
if ( sret != 86 || str::strlen(sl) != 86 || str::right(sl, 6) != "abcabc" ) { println( "sp9 ", sret ); }

// float conversions always consume their argument
var ff = str::format( "%f|%d", 1.5, 7 );
if ( str::right(ff, 2) != "|7" || str::left(ff, 3) != "1.5" ) { println( "ff1 [", ff, "]" ); }
ff = str::format( "%.2f|%s", 2.25, "x" );
if ( str::right(ff, 2) != "|x" ) { println( "ff2 [", ff, "]" ); }
ff = str::format( "%g/%e/%d", 1.0, 2.0, 3 );
if ( str::right(ff, 2) != "/3" ) { println( "ff3 [", ff, "]" ); }
ff = str::format( "%lf%c", -0.5, 'q' );
if ( str::right(ff, 1) != "q" || str::left(ff, 1) != "-" ) { println( "ff4 [", ff, "]" ); }
ff = str::format( "%f%d" );
if ( str::right(ff, 1) != "0" ) { println( "ff5 [", ff, "]" ); }

// a precision is accepted (and not taken as part of the field width)
ff = str::format( "%5.3d|%d", 7, 8 );
if ( str::right(ff, 2) != "|8" || str::strlen(ff) != 7 ) { println( "ff6 [", ff, "]" ); }

// str::tol reads strings of any length and clamps to 32 bits
if ( str::tol("000000000000000000000042") != 42 ) { println( "tol1" ); }
if ( str::tol("                     7") != 7 ) { println( "tol2" ); }
if ( str::tol("99999999999") != 2147483647 ) { println( "tol3 ", str::tol("99999999999") ); }
if ( str::tol("-99999999999") != -2147483647 - 1 ) { println( "tol4 ", str::tol("-99999999999") ); }
if ( str::tol("2147483647") != 2147483647 ) { println( "tol5" ); }
if ( str::tol("-2147483648") != -2147483647 - 1 ) { println( "tol6" ); }
if ( str::tol("2147483648") != 2147483647 ) { println( "tol7" ); }
if ( str::tol("ff", 16) != 255 || str::tol("0x1F", 16) != 31 ) { println( "tol8" ); }
if ( str::tol("0x10", 0) != 16 || str::tol("010", 0) != 8 || str::tol("12", 0) != 12 ) { println( "tol9" ); }
if ( str::tol("12abc") != 12 || str::tol("") != 0 || str::tol("abc") != 0 ) { println( "tol10" ); }
if ( str::tol("  -15") != -15 || str::tol("+9") != 9 ) { println( "tol11" ); }
if ( str::tol("101", 2) != 5 || str::tol("z", 36) != 35 ) { println( "tol12" ); }
if ( str::tol(42) != 42 || str::tol("42", 1) != 0 ) { println( "tol13" ); }

// serialize round trips, also through the growing output buffer
var big[3000];
for( var bi = 0; bi < 3000; ++bi ) { big[bi] = bi * 3; }
var bs = std::serialize( big );
if ( str::strlen(bs) != 5 + 3000 * 5 ) { println( "ser1 ", str::strlen(bs) ); }
var bb = std::deserialize( bs );
if ( bb._count != 3000 || bb[0] != 0 || bb[2999] != 8997 ) { println( "ser2" ); }

var nested = { 1:"abc", 2:7, "k":2.5 };
nested[3] = big;
var nb = std::deserialize( std::serialize(nested) );
if ( nb[1] != "abc" || nb[2] != 7 || nb["k"] != 2.5 || nb[3][1500] != 4500 ) { println( "ser3" ); }

// serializing in a loop collects garbage but keeps live values
for( var gi = 0; gi < 50; ++gi )
{
	var gs = std::serialize( big );
	if ( str::strlen(gs) != 15005 ) { println( "ser4" ); }
}
if ( big[2999] != 8997 || bs != std::serialize(big) ) { println( "ser5" ); }

// hostile input is refused before anything large is allocated
var hdr = str::format( "%c%c%c%c%c", 3, 160, 4, 255, 255 ); // array of 65535 values, no elements
if ( std::deserialize(hdr) != 0 ) { println( "des1" ); }
var nest = "";
for( var nj = 0; nj < 64; ++nj ) { nest += hdr; }
for( var nk = 0; nk < 20; ++nk )
{
	if ( std::deserialize(nest) != 0 ) { println( "des2" ); }
}
var chdr = str::format( "%c%c%c%c%cabc", 3, 160, 3, 255, 255 ); // 65535 chars, three present
if ( std::deserialize(chdr) != 0 ) { println( "des3" ); }
var two = str::format( "%c%c%c%c%c", 3, 160, 4, 2, 0 ) + std::serialize( 5 ) + std::serialize( "xy" );
var tw = std::deserialize( two );
if ( tw._count != 2 || tw[0] != 5 || tw[1] != "xy" ) { println( "des4" ); }
var twoShort = str::format( "%c%c%c%c%c", 3, 160, 4, 3, 0 ) + std::serialize( 5 ) + std::serialize( "xy" );
if ( std::deserialize(twoShort) != 0 ) { println( "des5" ); }

// importing invalid bytecode reports -1 and the script keeps running
if ( sys::importByteCode("this is not bytecode at all") != -1 ) { println( "imp1" ); }
if ( sys::importByteCode(5) != -1 ) { println( "imp2" ); }
if ( sys::importByteCode("") != -1 ) { println( "imp3" ); }

// debug printing is always available, it does nothing without a debugger
debug::print( "x%d", 1 );
debug::println( "y" );

// an invalid socket is an error, not "no data"
if ( sys::isFunction("tcp::peek") )
{
	if ( tcp::peek(-1) != -1 ) { println( "tcp1" ); }
	if ( tcp::peek(-1, 10) != -1 ) { println( "tcp2" ); }
}

println( "done" );
