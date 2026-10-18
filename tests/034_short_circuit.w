/*~ done
~*/

// && and || evaluate their right side only when the left side does not
// decide the result, and yield 1 or 0; every check prints only on failure

var log = "";
function t(n) { log += n; return 1; }
function f(n) { log += n; return 0; }
function chk( got, want, wantLog, name ) { if ( got != want || log != wantLog ) { println( "FAIL ", name, " got ", got, " log ", log ); } log = ""; }
chk( t("a") && t("b"), 1, "ab", "tt&" );
chk( t("a") && f("b"), 0, "ab", "tf&" );
chk( f("a") && t("b"), 0, "a", "ft&" );
chk( f("a") || t("b"), 1, "ab", "ft|" );
chk( t("a") || t("b"), 1, "a", "tt|" );
chk( f("a") || f("b"), 0, "ab", "ff|" );
chk( t("a") || f("b") && t("c"), 1, "a", "prec1" );
chk( f("a") && t("b") || t("c"), 1, "ac", "prec2" );
chk( f("a") || t("b") && f("c"), 0, "abc", "prec3" );
chk( t("a") && t("b") && t("c"), 1, "abc", "chain&" );
chk( f("a") || f("b") || t("c"), 1, "abc", "chain|" );
chk( (f("a") || t("b")) && t("c"), 1, "abc", "paren" );
chk( t("a") && f("b") ? 5 : 6, 6, "ab", "tern1" );
chk( f("a") ? 1 : t("b") || t("c"), 1, "ab", "tern2" );
chk( t("a") ? f("b") && t("c") : 9, 0, "ab", "tern3" );
chk( !f("a") && t("b"), 1, "ab", "not" );
chk( 2 && 3, 1, "", "values&" );
chk( 0 || 7, 1, "", "values|" );
chk( 2.5 && "x", 1, "", "mixed" );
var x = f("a") || t("b");
chk( x, 1, "ab", "assign" );
var i = 0; var n = 0;
while ( i < 5 && t("w") ) { ++i; }
chk( i, 5, "wwwww", "while" );
for( i = 0; i < 3 || f("q"); ++i ) { n++; }
chk( n, 3, "q", "for" );
var a[] = { 1, 2, 3 };
var k = 10;
if ( k < a._count && a[k] == 1 ) { println( "FAIL guard" ); }
if ( a._count != 3 ) { println( "FAIL grew" ); }
function g( p, q ) { return p && q; }
chk( g(1, 0), 0, "", "fn" );
chk( g(1, 2) + 1, 2, "", "arith" );
println( "done" );
