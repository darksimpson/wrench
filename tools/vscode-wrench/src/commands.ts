// Command lines of the wrench executable that the extension runs. Kept
// apart from extension.ts, without the vscode module, so that
// tools/test/extension_test.js can check them.

// "wrench p": the disassembly of a source file, compiled with debug code
// (as the debugger runs it) and in the mode the editor checks it in
export function disassemblyArgs(file: string, nonStrict: boolean): string[] {
	const args = ['p', '-debug'];
	if (nonStrict) {
		args.push('-nostrict');
	}
	args.push(file);
	return args;
}

// "wrench c": a source file to bytecode next to it
export function compileArgs(file: string, outFile: string, debugInfo: boolean, nonStrict: boolean): string[] {
	const args = ['c'];
	if (debugInfo) {
		args.push('-debugsource');
	}
	if (nonStrict) {
		args.push('-nostrict');
	}
	args.push(file, outFile);
	return args;
}
