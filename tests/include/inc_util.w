// included by inc_math.w and by 027_include.w: compiled once
include "inc_math.w";

private var count = 100;

function utilDouble( x )
{
	return x * 2;
}

function utilCount()
{
	count += 1;
	return count;
}
