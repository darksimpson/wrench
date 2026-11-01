/*~
zero
one
two
~*/

var somethingH = { 1:10, 2:20 };
if ( somethingH._exists(1) != 1 ) println("x1");
somethingH._remove( 1 );
if ( somethingH._exists(1) != 0 ) println("x2");

var something = { 1, 2, 3 };

var on = 1;
for( var value : something )
{
	if ( value != on++ )
	{
		println("f on 1");
	}
}
if ( on != 4 ) println( "f2");

something = { 2 };
on = 2;
for( var value : something )
{
	if ( value != on++ )
	{
		println("f on 2");
	}
	break;
}
if ( on != 3 ) println( "f3");

something = { 1, 2, 3 };
somethingH = { 1:10, 2:20, "bill?":30, 500:"bob", 12:13 };
on = 0;
for( var key, var value2 : somethingH )
{
	on++;
	if ( somethingH[key] != value2 ) println("key err 1");
}
if ( on != 5 ) println( "wrong on" );

somethingH = { -1:10, 0:20, 1:30 };
var on2 = 0;
for( var key, var value2 : somethingH )
{
	on2++;
}
if ( on2 != 3 ) println( "wrong on2" );


for( var value2 : something )
{
	for( value2 : something )
	{
		for( value2 : something )
		{
			if ( value2 != 1 )
			{
				println("f4");
			}
			break;
		}
		if ( value2 != 1 )
		{
			println("f4");
		}
		break;
	}
	break;
}


var somArray[] = {"zero", "one", "two" };

for( var v : somArray ) {
	println(v);
}

// every check below prints only when it fails

// continue inside foreach goes to the next element
var fa[] = { 1, 2, 3 };
var fv;
var fs = 0;
for( fv : fa )
{
	if ( fv == 2 )
	{
		continue;
	}
	fs += fv;
}
if ( fs != 4 ) { println( "fe1 ", fs ); }

var fk;
var fh = { 1:10, 2:20, 3:30 };
var fks = 0;
for( fk, fv : fh )
{
	if ( fk == 2 )
	{
		continue;
	}
	fks += fv;
}
if ( fks != 40 ) { println( "fe2 ", fks ); }

// the container on a line of its own
var ft = 0;
for( var fev :
	fa )
{
	ft = ft + fev;
}
if ( ft != 6 ) { println( "foreach1" ); }
