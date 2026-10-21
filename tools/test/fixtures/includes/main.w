include "lib/util.w";
include "extra.w";
var total = 0;
for( var i = 0; i < 3; ++i )
{
	total += utilSquare( i );
}
println( "total=", total + extraBonus() );
