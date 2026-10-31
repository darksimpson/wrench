// private: seen only in this file, like a static variable in C
private var count = 0;
private var sum = 0;

function statsAdd( value )
{
	count += 1;
	sum += value;
	return sum;
}

function statsAverage()
{
	if ( count == 0 )
	{
		return 0;
	}
	return sum / count;
}
