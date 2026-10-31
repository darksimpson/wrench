// source of v7_image.bin, an image as wrench 7.2.2 makes it: compiled by the
// 7.2.2 tree before 8.0 (wrench c v7_image.w), which already set the image
// flag WR_READS_KEEP_VALUES, so that flag is cleared again and the CRC
// recomputed with salt 7 (the code uses only opcodes 7.2.2 has). Not a test
// by itself: testVmBookFixes in src/utils/wrench_cli.cpp runs the image and
// checks that an 8.x VM keeps its old behaviour (reads create)
var h = { "a": 1 };
var arr[] = { 1, 2 };
var s = "ab";
function sum( n )
{
	var t = 0;
	for( var i = 1; i <= n; ++i )
	{
		t += i;
	}
	return t;
}
var missing = h["missing"];
var past = arr[5];
var hashCount = h._count;
var arrayCount = arr._count;
var total = sum( 10 );
