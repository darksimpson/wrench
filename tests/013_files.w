/*~ ~*/

var j = "_test file_";
if ( !io::writeFile( "_std_file_test.txt", j) ) println("bad file write");
var k = io::readFile( "_std_file_test.txt" );
if ( k != j ) println( "bad readback" );

// io:: has O_WRONLY, io::deleteFile reports its result, io::write of data
// that is not a string is an error (every check prints only when it fails)
if ( sys::isFunction( "io::open" ) )
{
	var ioArray[] = { 1, 2 };
	if ( io::O_WRONLY == io::O_RDONLY || io::O_WRONLY == io::O_RDWR ) { println( "io1" ); }
	var fd = io::open( "_io_wronly_test.txt", io::O_WRONLY | io::O_CREAT | io::O_TRUNC );
	if ( fd < 0 ) { println( "io2" ); }
	if ( io::write( fd, "abc" ) != 3 ) { println( "io3" ); }
	if ( io::write( fd, 5 ) != -1 || io::write( fd, ioArray ) != -1 || io::write( fd ) != -1 ) { println( "io4" ); }
	if ( io::write( fd, "" ) != 0 ) { println( "io5" ); }
	io::close( fd );
	if ( io::readFile( "_io_wronly_test.txt" ) != "abc" ) { println( "io6" ); }
	if ( io::deleteFile( "_io_wronly_test.txt" ) != 0 ) { println( "io7" ); }
	if ( io::deleteFile( "_io_wronly_test.txt" ) != -1 || io::deleteFile() != -1 ) { println( "io8" ); }
}
