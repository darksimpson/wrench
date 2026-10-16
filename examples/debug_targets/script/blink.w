// Demo script for the debug target examples. The firmware runs the global
// code once, then calls loop() over and over.

var count = 0;
var interval = 500;
var history[] = { 0, 0, 0, 0 };

struct Stats
{
	var calls;
	var last;
};
var stats = new Stats;

function remember( value )
{
	history[count % 4] = value;
	stats.last = value;
}

function loop()
{
	count = count + 1;
	stats.calls = count;

	var on = count % 2;
	led( on );
	remember( on * 100 + count );

	if ( count % 10 == 0 )
	{
		println( "tick ", count );
	}

	return interval;
}

println( "blink script started" );
