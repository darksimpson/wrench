// The debugger demo: one program made of three files, plus a module it
// compiles at run time. README.md says what to try.

include "shapes.w"; // next to this file
include "stats.w"; // in lib/, found through "include" in wrench.json

var sizes[] = { 2, 3, 4, 5, 6, 7 };
var info = { "unit":"cm2", "boxes":3 };
var total = 0;

for( var i = 0; i < 3; ++i )
{
	var b = new Box( sizes[i * 2], sizes[i * 2 + 1] );
	var area = boxArea( b );
	total = statsAdd( area );
}

// a module compiled from a string while the program runs; the debugger
// shows it as "import 1 (embedded source)"
var module = "function label( name, value )\n{\n\tvar text = name + \" = \" + value;\n\treturn text;\n}\n";
sys::importCompile( module );

println( label("total", total), " ", info["unit"] );
println( label("average", statsAverage()) );
