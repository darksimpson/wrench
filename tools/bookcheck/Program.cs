// Checks the wrench examples of the book (docs/book/src):
//   dotnet run --project tools/bookcheck -- <wrench executable> <work folder> <book src folder or .md files...>
//
// A ```wrench block followed by "Output:" and a ```text block is run with
// "wrench r" and its output compared; any other wrench block is compiled
// with "wrench c". Markers in an HTML comment on the line before a block:
//   <!-- check:skip -->          not checked (a fragment, or code for a host with natives)
//   <!-- check:error -->         must fail to compile
//   <!-- check:runtime -->       compiled and run, output not compared
//   <!-- check:file=NAME -->     written to NAME in the work folder (for include examples), not run
//   <!-- check:bin=NAME -->      compiled with "wrench c" into NAME in the work folder (for importByteCode examples)
//   <!-- check:as=NAME -->       checked as NAME in the work folder, next to files written before
// Exit status 0 when every example passes.

using System.Diagnostics;
using System.Text;

if ( args.Length < 3 )
{
	Console.WriteLine( "usage: bookcheck <wrench executable> <work folder> <book src folder or .md files...>" );
	return 2;
}

string wrench = Path.GetFullPath( args[0] );
string work = Path.GetFullPath( args[1] );
Directory.CreateDirectory( work );

var pages = new List<string>();
for ( int a = 2; a < args.Length; ++a )
{
	if ( Directory.Exists(args[a]) )
	{
		var found = Directory.GetFiles( args[a], "*.md", SearchOption.AllDirectories );
		Array.Sort( found, StringComparer.Ordinal );
		pages.AddRange( found );
	}
	else
	{
		pages.Add( args[a] );
	}
}

int failures = 0;
int ran = 0;
int compiled = 0;
int skipped = 0;

foreach ( string md in pages )
{
	string[] lines = File.ReadAllLines( md );
	string pageName = Path.GetFileNameWithoutExtension( md );
	string pageDir = Path.GetFileName( Path.GetDirectoryName(md) );
	int blockNo = 0;
	for ( int i = 0; i < lines.Length; ++i )
	{
		if ( lines[i].Trim() != "```wrench" )
		{
			continue;
		}

		string marker = "";
		for ( int m = i - 1; m >= 0; --m )
		{
			if ( lines[m].Trim().Length == 0 )
			{
				continue;
			}
			if ( lines[m].Trim().StartsWith("<!-- check:") )
			{
				marker = lines[m].Trim();
			}
			break;
		}

		int start = i + 1;
		int end = start;
		while ( end < lines.Length && lines[end].Trim() != "```" )
		{
			++end;
		}
		string code = string.Join( "\n", lines, start, end - start ) + "\n";
		int blockLine = i + 1;
		i = end;
		++blockNo;

		string expected = ExpectedOutput( lines, end + 1 );

		if ( marker.Contains("check:skip") )
		{
			++skipped;
			continue;
		}
		if ( marker.Contains("check:file=") )
		{
			string path = Path.Combine( work, MarkerValue(marker, "check:file=") );
			Directory.CreateDirectory( Path.GetDirectoryName(path) );
			File.WriteAllText( path, code );
			continue;
		}
		if ( marker.Contains("check:bin=") )
		{
			string bin = Path.Combine( work, MarkerValue(marker, "check:bin=") );
			string source = Path.ChangeExtension( bin, ".w" );
			File.WriteAllText( source, code );
			++compiled;
			string output = Run( wrench, "c \"" + Path.GetFileName(source) + "\" \"" + Path.GetFileName(bin) + "\"", work );
			if ( output.Contains("compile error") || output.Contains("err:") || !File.Exists(bin) )
			{
				++failures;
				Console.WriteLine( $"{md}:{blockLine}: does not compile to {bin}\n{output}" );
			}
			continue;
		}

		string file = Path.Combine( work, pageDir + "_" + pageName + "_" + blockNo + ".w" );
		if ( marker.Contains("check:as=") )
		{
			file = Path.Combine( work, MarkerValue(marker, "check:as=") );
			Directory.CreateDirectory( Path.GetDirectoryName(file) );
		}
		File.WriteAllText( file, code );

		if ( expected != null || marker.Contains("check:runtime") )
		{
			++ran;
			string output = Run( wrench, "r \"" + Path.GetFileName(file) + "\"", Path.GetDirectoryName(file) );
			if ( expected != null && Normalize(output) != Normalize(expected) )
			{
				++failures;
				Console.WriteLine( $"{md}:{blockLine}: output differs\n--- expected\n{expected}--- got\n{output}---" );
			}
		}
		else
		{
			++compiled;
			string bin = Path.Combine( work, "check.bin" );
			string output = Run( wrench, "c \"" + Path.GetFileName(file) + "\" \"" + bin + "\"", Path.GetDirectoryName(file) );
			bool failed = output.Contains( "compile error" ) || output.Contains( "err:" );
			bool wantError = marker.Contains( "check:error" );
			if ( failed != wantError )
			{
				++failures;
				Console.WriteLine( $"{md}:{blockLine}: compile {(failed ? "failed" : "succeeded")}, expected {(wantError ? "an error" : "success")}\n{output}" );
			}
		}
	}
}

Console.WriteLine( $"{pages.Count} pages: ran {ran}, compiled {compiled}, skipped {skipped}, failures {failures}" );
return failures == 0 ? 0 : 1;

// the ```text block after an "Output:" line that follows the block, if any
static string ExpectedOutput( string[] lines, int from )
{
	int j = from;
	while ( j < lines.Length && lines[j].Trim().Length == 0 )
	{
		++j;
	}
	if ( j >= lines.Length || lines[j].Trim() != "Output:" )
	{
		return null;
	}
	++j;
	while ( j < lines.Length && lines[j].Trim().Length == 0 )
	{
		++j;
	}
	if ( j >= lines.Length || lines[j].Trim() != "```text" )
	{
		return null;
	}
	var text = new StringBuilder();
	for ( int k = j + 1; k < lines.Length && lines[k].Trim() != "```"; ++k )
	{
		text.Append( lines[k] ).Append( '\n' );
	}
	return text.ToString();
}

static string MarkerValue( string marker, string key )
{
	int p = marker.IndexOf( key ) + key.Length;
	return marker.Substring( p, marker.IndexOf(" -->") - p ).Trim();
}

static string Run( string exe, string arguments, string dir )
{
	var psi = new ProcessStartInfo( exe, arguments )
	{
		RedirectStandardOutput = true,
		RedirectStandardError = true,
		UseShellExecute = false,
		WorkingDirectory = dir,
	};
	using var p = Process.Start( psi );
	string o = p.StandardOutput.ReadToEnd();
	string e = p.StandardError.ReadToEnd();
	p.WaitForExit();
	return o + e;
}

static string Normalize( string s )
{
	return s.Replace( "\r\n", "\n" ).TrimEnd( '\n', ' ' );
}
