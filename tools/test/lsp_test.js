// End to end test of "wrench lsp".
// usage: node lsp_test.js [path to wrench executable]
'use strict';

const { spawn } = require('child_process');
const path = require('path');
const fs = require('fs');
const assert = require('assert');

const exe = process.argv[2] || path.join(__dirname, '..', '..', 'win32', 'Debug', 'wrench.exe');

class LspClient {
	constructor() {
		this.proc = spawn(exe, ['lsp'], { stdio: ['pipe', 'pipe', 'pipe'] });
		this.id = 1;
		this.buffer = Buffer.alloc(0);
		this.pending = new Map();
		this.notifications = [];
		this.waiters = [];
		this.proc.stdout.on('data', (d) => this.onData(d));
		this.proc.stderr.on('data', () => {});
	}

	onData(data) {
		this.buffer = Buffer.concat([this.buffer, data]);
		for (;;) {
			const headerEnd = this.buffer.indexOf('\r\n\r\n');
			if (headerEnd < 0) {
				return;
			}
			const length = parseInt(/Content-Length: (\d+)/.exec(this.buffer.slice(0, headerEnd).toString())[1], 10);
			if (this.buffer.length < headerEnd + 4 + length) {
				return;
			}
			const msg = JSON.parse(this.buffer.slice(headerEnd + 4, headerEnd + 4 + length).toString());
			this.buffer = this.buffer.slice(headerEnd + 4 + length);
			if (msg.id !== undefined && this.pending.has(msg.id)) {
				this.pending.get(msg.id)(msg);
				this.pending.delete(msg.id);
			} else if (msg.id === null && msg.error) {
				// errors that belong to no request (unparsable input)
				this.notifications.push({ method: '$error', params: msg.error });
				this.waiters = this.waiters.filter((w) => !w());
			} else if (msg.method) {
				this.notifications.push(msg);
				this.waiters = this.waiters.filter((w) => !w());
			}
		}
	}

	send(obj) {
		this.sendRaw(JSON.stringify(Object.assign({ jsonrpc: '2.0' }, obj)));
	}

	sendRaw(text) {
		this.proc.stdin.write(`Content-Length: ${Buffer.byteLength(text)}\r\n\r\n${text}`);
	}

	request(method, params) {
		const id = this.id++;
		this.send({ id, method, params });
		return new Promise((resolve, reject) => {
			const timer = setTimeout(() => reject(new Error('timeout ' + method)), 10000);
			this.pending.set(id, (m) => { clearTimeout(timer); m.error ? reject(new Error(m.error.message)) : resolve(m.result); });
		});
	}

	notify(method, params) {
		this.send({ method, params });
	}

	waitNotification(method, predicate) {
		return new Promise((resolve, reject) => {
			const timer = setTimeout(() => reject(new Error('timeout waiting for ' + method)), 10000);
			const check = () => {
				const i = this.notifications.findIndex((n) => n.method === method && (!predicate || predicate(n.params)));
				if (i >= 0) {
					clearTimeout(timer);
					resolve(this.notifications.splice(i, 1)[0].params);
					return true;
				}
				return false;
			};
			if (!check()) {
				this.waiters.push(check);
			}
		});
	}
}

const uri = 'file:///c%3A/work/demo.w';
const source = [
	'// running total', // 0
	'var total = 0;', // 1
	'enum Color { RED, GREEN = 5 };', // 2
	'', // 3
	'// adds two numbers', // 4
	'function add( a, b )', // 5
	'{', // 6
	'  var sum = a + b;', // 7
	'  ::total = ::total + sum;', // 8
	'  return sum;', // 9
	'}', // 10
	'struct Point', // 11
	'{', // 12
	'  var x;', // 13
	'  var y = 2;', // 14
	'};', // 15
	'var p = new Point;', // 16
	'p.x = add( 1, Color::GREEN );', // 17
	'println( math::sqrt( p.x ) );', // 18
].join('\n');

// file URI the way VS Code writes it (file:///d%3A/dir/file.w)
function vscodeUri(file) {
	let p = file.replace(/\\/g, '/');
	if (/^[A-Za-z]:/.test(p)) {
		p = '/' + p[0].toLowerCase() + '%3A' + p.slice(2);
	}
	return 'file://' + p;
}

// a document next to fixtures/host_app/wrench.json, which declares the
// host application's functions and loads only the math library
async function testHostProject(lsp) {
	const hostUri = vscodeUri(path.join(__dirname, 'fixtures', 'host_app', 'editor.w'));
	const text = [
		'var a = getAnswer();', // 0
		'var b = ui::alert( "x", "t", 3 );', // 1
		'var c = str::concat( "a", "b" );', // 2
		'var d = math::sqrt( 4.0 );', // 3
		'openWindow( a );', // 4
		'print( a );', // 5
		'log( a, b, c, d );', // 6
		'function helper( x ) { return x; }', // 7
		'helper( 1 );', // 8
		'unit other( x ) { return x; }', // 9
		'other( 1 );', // 10
	].join('\n');
	lsp.notify('textDocument/didOpen', { textDocument: { uri: hostUri, languageId: 'wrench', version: 1, text } });
	const diags = await lsp.waitNotification('textDocument/publishDiagnostics', (p) => p.uri === hostUri);
	const found = diags.diagnostics.map((d) => `${d.range.start.line}:${d.code}`).sort();
	assert.deepStrictEqual(found, ['1:argument-count', '2:library-not-loaded', '4:undeclared-call', '5:undeclared-call'], JSON.stringify(diags.diagnostics));
	assert(diags.diagnostics.every((d) => d.severity === 2));
	const argCount = diags.diagnostics.find((d) => d.code === 'argument-count');
	assert(/ui::alert takes 1 to 2 arguments, this call passes 3/.test(argCount.message), argCount.message);

	const td = { uri: hostUri };
	let hover = await lsp.request('textDocument/hover', { textDocument: td, position: { line: 0, character: 10 } });
	assert(/function getAnswer\(\)/.test(hover.contents.value) && /The answer the application computed/.test(hover.contents.value), hover.contents.value);
	hover = await lsp.request('textDocument/hover', { textDocument: td, position: { line: 2, character: 12 } });
	assert(/Not available.*'string' library/.test(hover.contents.value), hover.contents.value);

	let items = await lsp.request('textDocument/completion', { textDocument: td, position: { line: 1, character: 12 } });
	assert(items.some((i) => i.label === 'alert'), 'ui:: completion');
	items = await lsp.request('textDocument/completion', { textDocument: td, position: { line: 8, character: 0 } });
	const labels = items.map((i) => i.label);
	for (const expected of ['getAnswer', 'log', 'ui', 'math', 'helper']) {
		assert(labels.includes(expected), expected + ' missing from completion');
	}
	for (const absent of ['str', 'print', 'tcp']) {
		assert(!labels.includes(absent), absent + ' should not be offered');
	}

	const sig = await lsp.request('textDocument/signatureHelp', { textDocument: td, position: { line: 1, character: 24 } });
	assert.strictEqual(sig.signatures[0].label, 'ui::alert( text, title? )');
	assert.strictEqual(sig.activeParameter, 1);

	const def = await lsp.request('textDocument/definition', { textDocument: td, position: { line: 0, character: 10 } });
	assert(/host_app\/wrench\.json$/.test(def.uri), def.uri);
	assert.strictEqual(def.range.start.line, 3);

	// file://localhost/... names a file on this machine, the project is found
	// (through the WSL test shim, a Linux path given on Windows, the file
	// lives under /mnt)
	let local = vscodeUri(path.join(__dirname, 'fixtures', 'host_app', 'local.w')).slice('file://'.length);
	if (process.platform === 'win32' && exe.startsWith('/')) {
		local = local.replace(/^\/([a-z])%3A\//, '/mnt/$1/');
	}
	const localUri = 'file://localhost' + local;
	lsp.notify('textDocument/didOpen', { textDocument: { uri: localUri, languageId: 'wrench', version: 1, text: 'openWindow( 1 );' } });
	const localDiags = await lsp.waitNotification('textDocument/publishDiagnostics', (p) => /local\.w$/.test(p.uri));
	assert.deepStrictEqual(localDiags.diagnostics.map((d) => d.code), ['undeclared-call'], JSON.stringify(localDiags));
}

// character offset of the n-th occurrence of needle in a line
function at(line, needle, n = 0) {
	let i = -1;
	for (let k = 0; k <= n; k++) {
		i = line.indexOf(needle, i + 1);
	}
	assert(i >= 0, needle + ' not in ' + line);
	return i;
}

async function openDoc(lsp, docUri, text, version = 1) {
	lsp.notify('textDocument/didOpen', { textDocument: { uri: docUri, languageId: 'wrench', version, text } });
	return lsp.waitNotification('textDocument/publishDiagnostics', (p) => p.uri === docUri && p.version === version);
}

// a script that includes others: what they define is defined, an error in
// one (also in its unsaved editor text) shows on the include that brings it
async function testIncludes(lsp) {
	const dir = path.join(__dirname, 'fixtures', 'includes');
	const mainUri = vscodeUri(path.join(dir, 'main.w'));
	const utilUri = vscodeUri(path.join(dir, 'lib', 'util.w'));
	const mainText = fs.readFileSync(path.join(dir, 'main.w'), 'utf8');

	let diags = await openDoc(lsp, mainUri, mainText);
	assert.deepStrictEqual(diags.diagnostics, [], JSON.stringify(diags.diagnostics));

	// what the included files declare: hover, definition, completion and
	// signature help reach it, a private name stays theirs
	const mainLines = mainText.split(/\r?\n/);
	const td = { uri: mainUri };
	const square = { line: 5, character: mainLines[5].indexOf('utilSquare') + 2 };
	const hover = await lsp.request('textDocument/hover', { textDocument: td, position: square });
	assert(/function utilSquare\( x \)/.test(hover.contents.value) && /declared in util\.w/.test(hover.contents.value), hover.contents.value);
	const def = await lsp.request('textDocument/definition', { textDocument: td, position: square });
	assert(/lib\/util\.w$/i.test(decodeURIComponent(def.uri)), def.uri);
	assert.deepStrictEqual(def.range.start, { line: 2, character: 9 });
	const items = await lsp.request('textDocument/completion', { textDocument: td, position: { line: 7, character: 0 } });
	assert(items.some((i) => i.label === 'utilSquare') && items.some((i) => i.label === 'extraBonus'), 'included functions in completion');
	assert(!items.some((i) => i.label === 'calls'), 'a private global of an included file is not offered');
	const sig = await lsp.request('textDocument/signatureHelp', { textDocument: td, position: { line: 5, character: mainLines[5].indexOf('( i') + 2 } });
	assert.strictEqual(sig.signatures[0].label, 'utilSquare( x )');

	await openDoc(lsp, utilUri, 'function utilSquare( x )\n{\n\treturn x * ;\n}\n');
	lsp.notify('textDocument/didChange', { textDocument: { uri: mainUri, version: 2 }, contentChanges: [{ text: mainText }] });
	diags = await lsp.waitNotification('textDocument/publishDiagnostics', (p) => p.uri === mainUri && p.version === 2);
	assert.strictEqual(diags.diagnostics.length, 1, JSON.stringify(diags.diagnostics));
	assert.strictEqual(diags.diagnostics[0].range.start.line, 0);
	// the compiler notices the missing operand at the '}' on line 4
	assert(/^util\.w:4: bad expression/.test(diags.diagnostics[0].message), diags.diagnostics[0].message);
}

// regression checks for the audit findings
async function testAuditFixes(lsp) {
	const docUri = 'file:///c%3A/work/audit.w';
	const td = { uri: docUri };
	const lines = [
		'var count = 5;', // 0
		'function bump() { count = count + 1; return ::count; }', // 1
		'function shadow() { var count = 1; return count; }', // 2
		'struct A { var size; var only; }', // 3
		'struct B { var size; }', // 4
		'var a = new A { size = 2, only = 3 };', // 5
		'a.size = 1;', // 6
		'a.only = 2;', // 7
		'var size = 3;', // 8
		'size = size + 1;', // 9
		'unit helper( x ) { return x; }', // 10
		'helper( 1 );', // 11
		'function init() { var ::g = 1; }', // 12
		'function use() { return ::g + count; }', // 13
		'var h = { "k":1 };', // 14
		'// note: a.', // 15
		'var str = "a.b";', // 16
		'var f = 1.5;', // 17
		'function sw( v ) { switch ( v ) { case 1: return 2; default: return 3; } }', // 18
		'var mixed = new B;', // 19
		'mixed = 0;', // 20
		'mixed.size = 1;', // 21
		'function make() { var q = new B; q.size = 2; return q; }', // 22
	];
	const diags = await openDoc(lsp, docUri, lines.join('\n'));
	assert.strictEqual(diags.diagnostics.length, 0, JSON.stringify(diags.diagnostics));
	const pos = (line, needle, n = 0, offset = 0) => ({ line, character: at(lines[line], needle, n) + offset });

	// 1: a global used by its plain name inside a function
	let edit = await lsp.request('textDocument/rename', { textDocument: td, position: pos(1, 'count', 1), newName: 'total' });
	const countEdits = edit.changes[docUri].map((e) => `${e.range.start.line}:${e.range.start.character}`).sort();
	const countExpected = [pos(0, 'count'), pos(1, 'count', 0), pos(1, 'count', 1), pos(1, '::count', 0, 2), pos(13, 'count')];
	assert.deepStrictEqual(countEdits, countExpected.map((p) => `${p.line}:${p.character}`).sort(), JSON.stringify(countEdits));
	let def = await lsp.request('textDocument/definition', { textDocument: td, position: pos(1, 'count', 0) });
	assert.deepStrictEqual(def.range.start, { line: 0, character: 4 });
	let hover = await lsp.request('textDocument/hover', { textDocument: td, position: pos(1, 'count', 1) });
	assert(/global/.test(hover.contents.value), hover.contents.value);
	let refs = await lsp.request('textDocument/references', { textDocument: td, position: pos(1, 'count', 0), context: { includeDeclaration: true } });
	assert.strictEqual(refs.length, 5);
	// the local of the same name stays apart
	edit = await lsp.request('textDocument/rename', { textDocument: td, position: pos(2, 'count', 1), newName: 'n' });
	assert.strictEqual(edit.changes[docUri].length, 2);
	// completion inside a function offers a global both ways
	let items = await lsp.request('textDocument/completion', { textDocument: td, position: pos(13, '::g') });
	assert(items.some((i) => i.label === 'count') && items.some((i) => i.label === '::count'), 'global completion in a function');

	// 2: .size belongs to A or B. a is only ever given new A, q new B: their
	// members are known; mixed is given other values too: no link, no rename
	const prep = await lsp.request('textDocument/prepareRename', { textDocument: td, position: pos(21, 'size') });
	assert.strictEqual(prep, null);
	edit = await lsp.request('textDocument/rename', { textDocument: td, position: pos(4, 'size'), newName: 'length' });
	assert.deepStrictEqual(edit.changes[docUri].map((e) => e.range.start.line).sort((a, b) => a - b), [4, 22]);
	def = await lsp.request('textDocument/definition', { textDocument: td, position: pos(6, 'size') });
	assert.deepStrictEqual(def.range.start, { line: 3, character: at(lines[3], 'size') });
	items = await lsp.request('textDocument/completion', { textDocument: td, position: pos(22, 'q.', 0, 2), context: { triggerKind: 2, triggerCharacter: '.' } });
	assert(items.some((i) => i.label === 'size') && !items.some((i) => i.label === 'only'), 'members of the variable\'s struct');
	items = await lsp.request('textDocument/completion', { textDocument: td, position: pos(21, '.', 0, 1), context: { triggerKind: 2, triggerCharacter: '.' } });
	assert(items.some((i) => i.label === 'size') && items.some((i) => i.label === 'only'), 'every struct\'s members without a type');
	// .only exists in one struct
	edit = await lsp.request('textDocument/rename', { textDocument: td, position: pos(7, 'only'), newName: 'single' });
	assert.deepStrictEqual(edit.changes[docUri].map((e) => e.range.start.line).sort(), [3, 5, 7]);

	// 9: member initializers of new A { ... } are members, not the global size
	edit = await lsp.request('textDocument/rename', { textDocument: td, position: pos(8, 'size'), newName: 'big' });
	assert.deepStrictEqual(edit.changes[docUri].map((e) => e.range.start.line).sort(), [8, 9, 9]);
	edit = await lsp.request('textDocument/rename', { textDocument: td, position: pos(3, 'size'), newName: 'length' });
	assert.deepStrictEqual(edit.changes[docUri].map((e) => e.range.start.line).sort(), [3, 5, 6]);

	// 3: unit is a synonym for function
	const symbols = await lsp.request('textDocument/documentSymbol', { textDocument: td });
	const helper = symbols.find((s) => s.name === 'helper');
	assert(helper && helper.kind === 12, 'unit in the outline');
	def = await lsp.request('textDocument/definition', { textDocument: td, position: pos(11, 'helper') });
	assert.deepStrictEqual(def.range.start, { line: 10, character: 5 });
	const sig = await lsp.request('textDocument/signatureHelp', { textDocument: td, position: pos(11, '1') });
	assert.strictEqual(sig.signatures[0].label, 'helper( x )');

	// 15: var ::g inside a function declares a global
	assert(symbols.some((s) => s.name === 'g' && s.kind === 13), 'var ::g in the outline');
	def = await lsp.request('textDocument/definition', { textDocument: td, position: pos(13, 'g', 0, 0) });
	assert.deepStrictEqual(def.range.start, { line: 12, character: at(lines[12], '::g') + 2 });

	// 8: reserved words are not names
	await assert.rejects(lsp.request('textDocument/rename', { textDocument: td, position: pos(8, 'size'), newName: 'while' }), /reserved/);

	// 10: no completion after a single ':', inside comments and strings, or
	// after a number
	const none = [pos(14, ':', 0, 1), pos(15, '.', 0, 1), pos(16, '.', 0, 1), pos(17, '.', 0, 1), pos(18, 'case 1:', 0, 7)];
	for (const p of none) {
		items = await lsp.request('textDocument/completion', { textDocument: td, position: p, context: { triggerKind: 2, triggerCharacter: lines[p.line][p.character - 1] } });
		assert.strictEqual(items.length, 0, `completion at ${JSON.stringify(p)}: ${items.slice(0, 5).map((i) => i.label)}`);
	}
	items = await lsp.request('textDocument/completion', { textDocument: td, position: pos(7, '.', 0, 1), context: { triggerKind: 2, triggerCharacter: '.' } });
	assert(items.some((i) => i.label === 'only'), 'member completion after a name');

	// 7: the error column of a line with non-ASCII text
	const errorRange = async (u, text) => (await openDoc(lsp, u, text)).diagnostics[0].range;
	const ascii = await errorRange('file:///c%3A/work/ascii.w', 'var s = "privet mir privet"; var t = s +; var u = 3;');
	const utf8 = await errorRange('file:///c%3A/work/utf8.w', 'var s = "\u043f\u0440\u0438\u0432\u0435\u0442 \u043c\u0438\u0440 \u043f\u0440\u0438\u0432\u0435\u0442"; var t = s +; var u = 3;');
	assert.deepStrictEqual(utf8, ascii);
	assert(ascii.start.character >= 37 && ascii.start.character <= 40, JSON.stringify(ascii));

	// 5: nesting the compiler cannot take is reported instead of compiled
	for (const text of ['var x = ' + '('.repeat(3000) + '1' + ')'.repeat(3000) + ';',
		'function f() ' + '{'.repeat(5000) + '}'.repeat(5000),
		'var a = 1; function f() { ' + 'if ( a ) '.repeat(1000) + 'a = 2; }',
		'var a = 1; var x = ' + 'a ? 1 : '.repeat(1000) + '1;']) {
		const deep = await openDoc(lsp, 'file:///c%3A/work/deep.w', text);
		assert.deepStrictEqual(deep.diagnostics.map((d) => d.code), ['nesting'], JSON.stringify(deep.diagnostics).slice(0, 300));
		lsp.notify('textDocument/didClose', { textDocument: { uri: 'file:///c%3A/work/deep.w' } });
		await lsp.waitNotification('textDocument/publishDiagnostics', (p) => p.uri === 'file:///c%3A/work/deep.w');
	}
	const fine = await openDoc(lsp, 'file:///c%3A/work/fine.w', 'var x = ' + '('.repeat(50) + '1' + ')'.repeat(50) + ';');
	assert.strictEqual(fine.diagnostics.length, 0, JSON.stringify(fine.diagnostics));

	// an unparsable body is answered with a parse error, the server goes on
	lsp.sendRaw('{"jsonrpc": "2.0", "id": 99, "method": ');
	const parseError = await lsp.waitNotification('$error');
	assert.strictEqual(parseError.code, -32700, JSON.stringify(parseError));
	assert(/^Parse error: /.test(parseError.message), parseError.message);
	hover = await lsp.request('textDocument/hover', { textDocument: td, position: pos(1, 'count', 1) });
	assert(/global/.test(hover.contents.value));
}

// strict mode scopes names by block: two loops in a row have an i each, a
// name of a block that ended is not offered, var ::g belongs to the program
async function testBlockScope(lsp) {
	const docUri = 'file:///c%3A/work/blocks.w';
	const td = { uri: docUri };
	const lines = [
		'function f( n )', // 0
		'{', // 1
		'\tvar total = 0;', // 2
		'\tfor ( var i = 0; i < n; ++i ) { total += i; }', // 3
		'\tfor ( var i = 0; i < n; ++i ) { total += i * 2; }', // 4
		'\tif ( n ) { var inner = 1; total += inner; } else { var inner = 2; total -= inner; }', // 5
		'\tfor ( var j = 0; j < n; ++j ) total += j;', // 6
		'\tvar j = total;', // 7
		'\t', // 8
		'\treturn j;', // 9
		'}', // 10
		'if ( 1 ) { var hidden = 2; var ::shared = 3; }', // 11
		'var v = shared + f( 2 );', // 12
		'var p = { "k": { 1 } };', // 13
		'', // 14
	];
	const diags = await openDoc(lsp, docUri, lines.join('\n'));
	assert.strictEqual(diags.diagnostics.length, 0, JSON.stringify(diags.diagnostics));
	const pos = (line, needle, n = 0, offset = 0) => ({ line, character: at(lines[line], needle, n) + offset });
	const editLines = (edit) => edit.changes[docUri].map((e) => `${e.range.start.line}:${e.range.start.character}`).sort();

	// the i of each loop is its own
	let edit = await lsp.request('textDocument/rename', { textDocument: td, position: pos(4, 'i <'), newName: 'k' });
	assert.deepStrictEqual(editLines(edit), [pos(4, 'i ='), pos(4, 'i <'), pos(4, '++i', 0, 2), pos(4, 'i *')].map((p) => `${p.line}:${p.character}`).sort());
	let def = await lsp.request('textDocument/definition', { textDocument: td, position: pos(4, '++i', 0, 2) });
	assert.deepStrictEqual(def.range.start, pos(4, 'i ='));
	let refs = await lsp.request('textDocument/references', { textDocument: td, position: pos(3, 'i ='), context: { includeDeclaration: true } });
	assert.deepStrictEqual(refs.map((r) => r.range.start.line), [3, 3, 3, 3]);

	// the two branches of an if, a loop without braces and the var after it
	edit = await lsp.request('textDocument/rename', { textDocument: td, position: pos(5, 'inner', 1), newName: 'x' });
	assert.deepStrictEqual(editLines(edit), [pos(5, 'inner', 0), pos(5, 'inner', 1)].map((p) => `${p.line}:${p.character}`).sort());
	def = await lsp.request('textDocument/definition', { textDocument: td, position: pos(9, 'j') });
	assert.deepStrictEqual(def.range.start, pos(7, 'j'));
	def = await lsp.request('textDocument/definition', { textDocument: td, position: pos(6, '++j', 0, 2) });
	assert.deepStrictEqual(def.range.start, pos(6, 'j ='));

	// completion offers what is in view
	let items = await lsp.request('textDocument/completion', { textDocument: td, position: { line: 8, character: 1 } });
	let labels = items.map((i) => i.label);
	assert(labels.includes('total') && labels.includes('n') && labels.includes('j'), labels.join(','));
	assert(!labels.includes('inner') && !labels.includes('i') && !labels.includes('hidden'), labels.join(','));
	items = await lsp.request('textDocument/completion', { textDocument: td, position: { line: 14, character: 0 } });
	labels = items.map((i) => i.label);
	assert(labels.includes('shared') && labels.includes('v') && labels.includes('p') && !labels.includes('hidden'), labels.join(','));

	// var ::shared in a block is a global of the program
	def = await lsp.request('textDocument/definition', { textDocument: td, position: pos(12, 'shared') });
	assert.deepStrictEqual(def.range.start, pos(11, '::shared', 0, 2));

	// non-strict code has no block scope: one i for both loops
	lsp.notify('workspace/didChangeConfiguration', { settings: { wrench: { nonStrict: true } } });
	await lsp.waitNotification('textDocument/publishDiagnostics', (p) => p.uri === docUri);
	edit = await lsp.request('textDocument/rename', { textDocument: td, position: pos(4, 'i <'), newName: 'k' });
	assert.deepStrictEqual([...new Set(edit.changes[docUri].map((e) => e.range.start.line))].sort(), [3, 4]);
	lsp.notify('workspace/didChangeConfiguration', { settings: { wrench: { nonStrict: false } } });
	await lsp.waitNotification('textDocument/publishDiagnostics', (p) => p.uri === docUri);
	edit = await lsp.request('textDocument/rename', { textDocument: td, position: pos(4, 'i <'), newName: 'k' });
	assert.deepStrictEqual([...new Set(edit.changes[docUri].map((e) => e.range.start.line))], [4]);

	lsp.notify('textDocument/didClose', { textDocument: { uri: docUri } });
	await lsp.waitNotification('textDocument/publishDiagnostics', (p) => p.uri === docUri && p.diagnostics.length === 0);
}

(async () => {
	const lsp = new LspClient();
	try {
		const init = await lsp.request('initialize', { processId: null, rootUri: null, capabilities: {} });
		assert(init.capabilities.hoverProvider && init.capabilities.renameProvider.prepareProvider);
		lsp.notify('initialized', {});

		lsp.notify('textDocument/didOpen', { textDocument: { uri, languageId: 'wrench', version: 1, text: source } });
		let diags = await lsp.waitNotification('textDocument/publishDiagnostics', (p) => p.version === 1);
		assert.strictEqual(diags.diagnostics.length, 0, JSON.stringify(diags.diagnostics));

		const td = { uri };
		// hover on a user function shows its signature and doc comment
		let hover = await lsp.request('textDocument/hover', { textDocument: td, position: { line: 17, character: 7 } });
		assert(/function add\( a, b \)/.test(hover.contents.value) && /adds two numbers/.test(hover.contents.value), hover.contents.value);
		// hover on a library function
		hover = await lsp.request('textDocument/hover', { textDocument: td, position: { line: 18, character: 13 } });
		assert(/math::sqrt/.test(hover.contents.value), JSON.stringify(hover));
		// hover on a global referenced as ::total
		hover = await lsp.request('textDocument/hover', { textDocument: td, position: { line: 8, character: 5 } });
		assert(/global/.test(hover.contents.value));

		// definition of the parameter 'b' used on line 7
		let def = await lsp.request('textDocument/definition', { textDocument: td, position: { line: 7, character: 16 } });
		assert.deepStrictEqual(def.range.start, { line: 5, character: 17 });
		// definition of an enum member through Color::GREEN
		def = await lsp.request('textDocument/definition', { textDocument: td, position: { line: 17, character: 23 } });
		assert.strictEqual(def.range.start.line, 2);

		// references to the global total: declaration plus two uses
		const refs = await lsp.request('textDocument/references', { textDocument: td, position: { line: 1, character: 5 }, context: { includeDeclaration: true } });
		assert.strictEqual(refs.length, 3);

		// rename sum touches only the function's three occurrences
		const prep = await lsp.request('textDocument/prepareRename', { textDocument: td, position: { line: 9, character: 10 } });
		assert.strictEqual(prep.start.line, 9);
		const edit = await lsp.request('textDocument/rename', { textDocument: td, position: { line: 9, character: 10 }, newName: 'result' });
		assert.strictEqual(edit.changes[uri].length, 3);

		// document symbols
		const symbols = await lsp.request('textDocument/documentSymbol', { textDocument: td });
		const names = symbols.map((s) => s.name);
		for (const n of ['total', 'Color', 'add', 'Point', 'p']) {
			assert(names.includes(n), n + ' missing from ' + names);
		}
		const point = symbols.find((s) => s.name === 'Point');
		assert.deepStrictEqual(point.children.map((c) => c.name), ['x', 'y']);

		// completion: namespace members, members after '.', locals inside a function
		let items = await lsp.request('textDocument/completion', { textDocument: td, position: { line: 18, character: 15 } });
		assert(items.some((i) => i.label === 'sqrt') && items.some((i) => i.label === 'pow'), 'math:: completion');
		items = await lsp.request('textDocument/completion', { textDocument: td, position: { line: 17, character: 2 } });
		assert(items.some((i) => i.label === 'x') && items.some((i) => i.label === '_count'), 'member completion');
		items = await lsp.request('textDocument/completion', { textDocument: td, position: { line: 9, character: 9 } });
		assert(items.some((i) => i.label === 'sum') && items.some((i) => i.label === '::total') && items.some((i) => i.label === 'while'));

		// signature help inside add( 1, | )
		const sig = await lsp.request('textDocument/signatureHelp', { textDocument: td, position: { line: 17, character: 15 } });
		assert.strictEqual(sig.signatures[0].label, 'add( a, b )');
		assert.strictEqual(sig.activeParameter, 1);

		const folds = await lsp.request('textDocument/foldingRange', { textDocument: td });
		assert(folds.some((f) => f.startLine === 6 && f.endLine === 10));

		// a broken edit produces a diagnostic on the right line
		const broken = source.replace('return sum;', 'return sum +;');
		lsp.notify('textDocument/didChange', { textDocument: { uri, version: 2 }, contentChanges: [{ text: broken }] });
		diags = await lsp.waitNotification('textDocument/publishDiagnostics', (p) => p.version === 2);
		assert.strictEqual(diags.diagnostics.length, 1);
		assert.strictEqual(diags.diagnostics[0].range.start.line, 9, JSON.stringify(diags.diagnostics[0]));

		// undeclared variables are errors in strict mode
		lsp.notify('textDocument/didChange', { textDocument: { uri, version: 3 }, contentChanges: [{ text: 'var a = 1;\nb = a;\n' }] });
		diags = await lsp.waitNotification('textDocument/publishDiagnostics', (p) => p.version === 3);
		assert.strictEqual(diags.diagnostics.length, 1);
		assert.strictEqual(diags.diagnostics[0].range.start.line, 1);

		await testHostProject(lsp);
		await testAuditFixes(lsp);
		await testIncludes(lsp);
		await testBlockScope(lsp);

		await lsp.request('shutdown', null);
		lsp.notify('exit', null);
		console.log('PASS lsp');
		process.exit(0);
	} catch (e) {
		console.log('FAIL lsp: ' + e.stack);
		lsp.proc.kill();
		process.exit(1);
	}
})();
