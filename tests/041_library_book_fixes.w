/*~ done
~*/

// regressions for the library bugs the book found; every check prints only
// when it fails

// sys::isFunction finds the functions of imported modules, before and after
// their first call, and an empty registry entry left by an earlier call (the
// second run of this file) is not a function
if ( sys::isFunction( "lbfImported" ) != 0 ) { println( "isf1 ", sys::isFunction( "lbfImported" ) ); }
sys::importCompile( "function lbfImported() { return 41; }" );
if ( sys::isFunction( "lbfImported" ) != 2 ) { println( "isf2 ", sys::isFunction( "lbfImported" ) ); }
if ( lbfImported() != 41 ) { println( "isf3" ); }
if ( sys::isFunction( "lbfImported" ) != 2 ) { println( "isf4 ", sys::isFunction( "lbfImported" ) ); }
function lbfLocal() { return 1; }
if ( sys::isFunction( "lbfLocal" ) != 2 || sys::isFunction( "println" ) != 1 || sys::isFunction( "str::mid" ) != 1 ) { println( "isf5" ); }
if ( sys::isFunction( "io::O_CREAT" ) != 0 || sys::isFunction( "lbfNothing" ) != 0 ) { println( "isf6" ); }

// str::mid / str::substr of an empty string are the empty string
var lbfEmpty = "";
if ( str::mid( "", 0 ) != "" || str::strlen( str::mid( "", 0 ) ) != 0 ) { println( "mid1" ); }
if ( str::substr( "", 1, 2 ) != "" ) { println( "mid2" ); }
if ( str::mid( lbfEmpty, 0, 5 ) != "" ) { println( "mid3" ); }
if ( str::mid( "abc", 1 ) != "bc" || str::mid( "abc", 3 ) != "" ) { println( "mid4" ); }

// str::isalnum is registered
if ( !str::isalnum( 'a' ) || !str::isalnum( '7' ) || str::isalnum( '-' ) || str::isalnum( ' ' ) ) { println( "aln1" ); }

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

// msg:: stores ints, floats and strings as they are, refuses containers
msg::write( "lbfGain", 2.5 );
if ( msg::read( "lbfGain" ) != 2.5 ) { println( "msg1 ", msg::read( "lbfGain" ) ); }
if ( msg::write( "lbfName", "probe" ) != 0 ) { println( "msg2" ); }
var lbfName = msg::read( "lbfName" );
if ( str::strlen( lbfName ) != 5 || lbfName != "probe" ) { println( "msg3 ", lbfName ); }
var lbfSource = "abc";
msg::write( "lbfCopy", lbfSource );
lbfSource += "d";
if ( msg::read( "lbfCopy" ) != "abc" ) { println( "msg4 ", msg::read( "lbfCopy" ) ); }
var lbfArray[] = { 1, 2 };
if ( msg::write( "lbfArray", lbfArray ) != -1 || msg::peek( "lbfArray" ) ) { println( "msg5" ); }
var lbfTable = { "a": 1 };
if ( msg::write( "lbfTable", lbfTable ) != -1 || msg::peek( "lbfTable" ) ) { println( "msg6" ); }
if ( msg::write( "lbfMissing" ) != -1 || msg::peek( "lbfMissing" ) ) { println( "msg7" ); }
msg::write( "lbfName", 7 );
if ( msg::read( "lbfName" ) != 7 ) { println( "msg8" ); }
msg::write( "lbfName", "again" );
if ( msg::read( "lbfName", true ) != "again" || msg::peek( "lbfName" ) || msg::read( "lbfName" ) != 0 ) { println( "msg9" ); }
msg::clear( "lbfCopy" );
if ( msg::peek( "lbfCopy" ) ) { println( "msg10" ); }
msg::write( 7, -3 );
if ( msg::read( 7 ) != -3 ) { println( "msg11" ); }
msg::clear( 7 );
msg::write( "lbfGain", "" );
if ( msg::read( "lbfGain", true ) != "" ) { println( "msg12" ); }

// std::srand restarts the sequence of std::rand
std::srand( 1234 );
var lbfR1 = std::rand( 1000000 );
var lbfR2 = std::rand( 1000000 );
std::srand( 1234 );
if ( std::rand( 1000000 ) != lbfR1 || std::rand( 1000000 ) != lbfR2 ) { println( "rand1" ); }

// the clocks run on the desktop
var lbfStart = time::ms();
if ( std::time() <= 0 || time::ms() - lbfStart < 0 ) { println( "time1" ); }

// io:: has O_WRONLY, io::deleteFile reports its result, io::write of data
// that is not a string is an error
if ( sys::isFunction( "io::open" ) )
{
	if ( io::O_WRONLY == io::O_RDONLY || io::O_WRONLY == io::O_RDWR ) { println( "io1" ); }
	var lbfFd = io::open( "_041_wronly.txt", io::O_WRONLY | io::O_CREAT | io::O_TRUNC );
	if ( lbfFd < 0 ) { println( "io2" ); }
	if ( io::write( lbfFd, "abc" ) != 3 ) { println( "io3" ); }
	if ( io::write( lbfFd, 5 ) != -1 || io::write( lbfFd, lbfArray ) != -1 || io::write( lbfFd ) != -1 ) { println( "io4" ); }
	if ( io::write( lbfFd, "" ) != 0 ) { println( "io5" ); }
	io::close( lbfFd );
	if ( io::readFile( "_041_wronly.txt" ) != "abc" ) { println( "io6" ); }
	if ( io::deleteFile( "_041_wronly.txt" ) != 0 ) { println( "io7" ); }
	if ( io::deleteFile( "_041_wronly.txt" ) != -1 || io::deleteFile() != -1 ) { println( "io8" ); }
}

// tcp:: functions that make a socket fail with -1 without their arguments
if ( sys::isFunction( "tcp::bind" ) )
{
	if ( tcp::bind() != -1 ) { println( "tcp1" ); }
	if ( tcp::connect() != -1 || tcp::connect( "" ) != -1 || tcp::connect( "", 80 ) != -1 ) { println( "tcp2" ); }
	if ( tcp::accept() != -1 || tcp::peek() != -1 ) { println( "tcp3" ); }
}

println( "done" );
