// shared helpers for 038_include.w, not a test of its own
include "inc_util.w";

private var count = 0;

private function step()
{
	return 2;
}

function mathNext()
{
	count += step();
	return count;
}

function mathTwice( x )
{
	return utilDouble( x );
}
