var src = "function triple( x )\n{\n  var t = x * 3;\n  return t;\n}\n";
sys::importCompile( src );
var r = triple( 5 );
println( r );
