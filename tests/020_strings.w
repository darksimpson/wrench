/*~
[100000]
[10000000000000000]
[37777777777]
[11111111111111111111111111111111]
[   -3]
[-0003]
[-3   |]
[5]
[1.5]
[ab]
[5]
[5]
[000000ff]
[0xff]
[0x00ff]
[-2147483648]
[4294967295]
4096
[%]
~*/

var s = "string";
if ( s + 123 != "string123" ) println( "st_err0" );
if ( "string" + 123 != "string123" ) println( "st_err1" );
if ( 123 + "string" != "123string" ) println( "st_err2" );
if ( 123 + s != "123string" ) println( "st_err3" );
if ( s != "string" ) println( "st_err4" );
if ( (s += 123) != "string123" ) println ( "st_err5" );
var i = 123;
if ( (i += "string") != "123string" ) println ( "st_err6" );
if ( (123 + "string" + 34 + "hi") != "123string34hi" ) println ( "st_err7" );
var f = 12.5;
var fs = "string";
if ( fs + f != "string12.5" ) println( "st_err8" );
if ( "string" + f != "string12.5" ) println( "st_err9" );
if ( f + "string" != "12.5string" ) println( "st_err10" );
if ( f + fs != "12.5string" ) println( "st_err11" );

	
var h = "Hello " + "World";
if ( h != "Hello World" ) println( "hw_err0" );
var h1 = "Hello ";
var w1 = "World";
if ( h1 + w1 != "Hello World" ) println( "hw_err1" );
if ( h1 + "World" != "Hello World" ) println( "hw_err2" );
if ( "Hello " + w1 != "Hello World" ) println( "hw_err3" );
if ( "Hello " + "World" != "Hello World" ) println( "hw_err4" );
if ( h1 != "Hello " ) println( "hw_err5" );
if ( w1 != "World" ) println( "hw_err5" );


var h2 = "Hello ";
var w2 = "World";
if ( (h2 += w2) != "Hello World" ) println( "hw_err6" );
h2 = "Hello ";
if ( (h2 += "World") != "Hello World" ) println( "hw_err7" );

h2 = "Hello ";
var h3 = "foo";
if ( (h3 = h2 + w2) != "Hello World" ) println( "hw_err8" );
h3 = "foo";
if ( (h3 = h2 + "World") != "Hello World" ) println( "hw_err9" );
h3 = "foo";
if ( h3 = "Hello " + "World" != "Hello World" ) println( "hw_err10" );

var a1 = { "Hello " };
var a2 = { "w":"World" };
if ( a1[0] + "World" != "Hello World" ) println( "hw_err11" );
if ( a1[0] + a2["w"] != "Hello World" ) println( "hw_err12" );
if ( "Hello " + a2["w"] != "Hello World" ) println( "hw_err13" );


var y = "hello";
y += "hi";
if ( y != "hellohi" ) print("hw_err14");

// str::format: octal and binary widths, signs and padding, %s of a value
// that is no string, length modifiers, a capped width
var fmtMin = -2147483647 - 1;
println( "[" + str::format("%o", 32768) + "]" );
println( "[" + str::format("%b", 65536) + "]" );
println( "[" + str::format("%o", -1) + "]" );
println( "[" + str::format("%b", -1) + "]" );
println( "[" + str::format("%5d", -3) + "]" );
println( "[" + str::format("%05d", -3) + "]" );
println( "[" + str::format("%-5d|", -3) + "]" );
println( "[" + str::format("%s", 5) + "]" );
println( "[" + str::format("%s", 1.5) + "]" );
println( "[" + str::format("%s%s", "a", "b") + "]" );
println( "[" + str::format("%ld", 5) + "]" );
println( "[" + str::format("%hd", 5) + "]" );
println( "[" + str::format("%08x", 255) + "]" );
println( "[" + str::format("%#x", 255) + "]" );
println( "[" + str::format("%#06x", 255) + "]" );
println( "[" + str::format("%d", fmtMin) + "]" );
println( "[" + str::format("%u", -1) + "]" );
println( str::strlen(str::format("%99999999d", 1)) );
println( "[" + str::format("%%") + "]" );

// every check below prints only when it fails

// str::format follows C printf: flags, width, precision and *
function lbfCheck( name, got, want )
{
	if ( got != want )
	{
		println( name, " [", got, "] want [", want, "]" );
	}
}
lbfCheck( "f1", str::format( "%+d|% d|%+d|% d", 5, 5, -5, -5 ), "+5| 5|-5|-5" );
lbfCheck( "f2", str::format( "%+5d|%-+5d|% 05d|%+i", 7, 7, 7, 0 ), "   +7|+7   | 0007|+0" );
lbfCheck( "f3", str::format( "%5c|%-3c|%c", 'x', 'y', 'z' ), "    x|y  |z" );
lbfCheck( "f4", str::format( "%.2s|%5.1s|%-4.3s|%.0s|", "abc", "xyz", "hello", "gone" ), "ab|    x|hel ||" );
lbfCheck( "f5", str::format( "%.3d|%5.3d|%.0d|%-6.4d|", 7, -7, 0, 42 ), "007| -007||0042  |" );
lbfCheck( "f6", str::format( "%#x|%#X|%#o|%#x|%#.0o", 255, 255, 8, 0, 0 ), "0xff|0XFF|010|0|0" );
lbfCheck( "f7", str::format( "%08.3d|%08d|%-08d|", 5, -5, 5 ), "     005|-0000005|5       |" );
lbfCheck( "f8", str::format( "%*d|%-*d|%.*d", 4, 1, 4, 2, 3, 3 ), "   1|2   |003" );
lbfCheck( "f9", str::format( "%*d|%.*d|", -4, 1, -1, 5 ), "1   |5|" );
lbfCheck( "f10", str::format( "%u|%x|%o", -1, -1, 8 ), "4294967295|ffffffff|10" );
lbfCheck( "f11", str::format( "%ld|%hd|%lld|%%", 1, 2, 3 ), "1|2|3|%" );

lbfCheck( "g1", str::format( "%.2f|%f|%08.3f", 3.14159, 1.5, -3.5 ), "3.14|1.500000|-003.500" );
lbfCheck( "g2", str::format( "%+.1f|% .0f|%#.0f|%.0f", 2.26, 7.0, 3.0, 0.4 ), "+2.3| 7|3.|0" );
lbfCheck( "g3", str::format( "%e|%.3e|%E|%.0e", 12345.678, 1536.0, 0.15625, 5000.0 ), "1.234568e+04|1.536e+03|1.562500E-01|5e+03" );
lbfCheck( "g4", str::format( "%g|%g|%g|%g|%#g", 0.0001, 100000.0, 1000000.0, 1.5, 1.5 ), "0.0001|100000|1e+06|1.5|1.50000" );
lbfCheck( "g5", str::format( "%.3g|%G|%10.3f|%-8.1f|", 3.14159, 0.0000000001, 2.5, 0.7 ), "3.14|1E-10|     2.500|0.7     |" );
lbfCheck( "g6", str::format( "%f|%.1f|%g|%e", 0.0, -0.04, 0.0, 0.0 ), "0.000000|-0.0|0|0.000000e+00" );
lbfCheck( "g7", str::format( "%.10f|%.12e", 0.1, 0.1 ), "0.1000000015|1.000000014901e-01" );
lbfCheck( "g8", str::format( "%f", 16777216.0 * 1024.0 ), "17179869184.000000" );
lbfCheck( "g9", str::format( "%+010.2f|%-+9.2e|%*.*f", 1.25, 1.0, 7, 1, 2.75 ), "+000001.25|+1.00e+00|    2.8" );

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

// str::mid / str::substr of an empty string are the empty string
var lbfEmpty = "";
if ( str::mid( "", 0 ) != "" || str::strlen( str::mid( "", 0 ) ) != 0 ) { println( "mid1" ); }
if ( str::substr( "", 1, 2 ) != "" ) { println( "mid2" ); }
if ( str::mid( lbfEmpty, 0, 5 ) != "" ) { println( "mid3" ); }
if ( str::mid( "abc", 1 ) != "bc" || str::mid( "abc", 3 ) != "" ) { println( "mid4" ); }

// str::isalnum is registered
if ( !str::isalnum( 'a' ) || !str::isalnum( '7' ) || str::isalnum( '-' ) || str::isalnum( ' ' ) ) { println( "aln1" ); }

// strings order by their bytes
if ( !("aa" < "b") || !("b" > "aa") || !("abc" <= "abc") || !("ab" < "abc") || ("abc" >= "abd") ) { println( "order1" ); }
var s1 = "apple";
var s2 = "banana";
if ( s1 > s2 || !(s1 < s2) || s2 <= s1 ) { println( "order2" ); }

// a string is never equal to a number, not even to its hash
var str = "abc";
var strHash = str._hash;
if ( str == strHash || !(str != strHash) || strHash == str ) { println( "eq1" ); }
var ne[] = { 1 };
if ( ne == ne._hash ) { println( "eq2" ); }

// writes to the characters of a string land in the string
var cs = "ab";
cs[1] = 'z';
if ( cs != "az" ) { println( "char1 ", cs ); }
cs[0] += 1;
cs[1]--;
if ( cs != "by" ) { println( "char2 ", cs ); }
++cs[0];
if ( cs[0] != 'c' ) { println( "char3" ); }

// a write past the end grows the string and stores the character
cs[4] = 'e';
if ( cs._count != 5 || cs[2] != 0 || cs[3] != 0 || cs[4] != 'e' ) { println( "char4 ", cs._count ); }

function charLocal()
{
	var t = "cd";
	t[0] = 'x';
	var u = t + "!";
	u[2] = '?';
	return t + u;
}
if ( charLocal() != "xdxd?" ) { println( "char5 ", charLocal() ); }

// strings inside containers
var charH = { "s": "ab" };
charH.s[0] = 'q';
if ( charH.s != "qb" ) { println( "char6 ", charH.s ); }
var charA[] = { "ab", "cd" };
charA[1][1] = 'k';
if ( charA[1] != "ck" || charA[0] != "ab" ) { println( "char7" ); }

// a string is shared like an array: assigning it does not copy it, and
// a character written through either name shows in both
var charC = cs;
charC[0] = 'Z';
if ( cs[0] != 'Z' || charC[0] != 'Z' ) { println( "char8" ); }
charC += "!";
if ( cs._count != 5 || charC._count != 6 ) { println( "char9" ); }

// += with a string on an element of a container stores the sum in it
var catH = { "k": "a", "i": 5 };
catH["k"] += "b";
if ( catH["k"] != "ab" ) { println( "cat1 ", catH["k"] ); }
catH["k"] += 7;
if ( catH["k"] != "ab7" ) { println( "cat2 ", catH["k"] ); }
catH["i"] += "x";
if ( catH["i"] != "5x" ) { println( "cat3 ", catH["i"] ); }
var catA[] = { "a" };
catA[0] += "b";
var catR = ( catA[0] += "c" );
if ( catA[0] != "abc" || catR != "abc" ) { println( "cat4 ", catA[0], " ", catR ); }
struct SC { var name = "n"; };
var catS = new SC;
catS.name += "x";
if ( catS.name != "nx" ) { println( "cat5 ", catS.name ); }
var catN = { "in": { "k": "a" } };
catN["in"]["k"] += "b";
if ( catN["in"]["k"] != "ab" ) { println( "cat6 ", catN["in"]["k"] ); }
function catLocal() { var l = { "k": "q" }; l["k"] += "r"; return l["k"]; }
if ( catLocal() != "qr" ) { println( "cat7" ); }
catH["f"] = 1.5;
catH["f"] += 1;
if ( catH["f"] != 2.5 ) { println( "cat8 ", catH["f"] ); }
