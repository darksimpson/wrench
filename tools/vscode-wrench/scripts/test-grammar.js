// Tokenizes sample wrench code with the same engine VS Code uses and checks
// the scopes of interesting tokens.
// usage: node scripts/test-grammar.js
'use strict';

const fs = require('fs');
const path = require('path');
const assert = require('assert');
const vsctm = require('vscode-textmate');
const oniguruma = require('vscode-oniguruma');

const root = path.join(__dirname, '..');
const wasm = fs.readFileSync(path.join(root, 'node_modules', 'vscode-oniguruma', 'release', 'onig.wasm')).buffer;

const onigLib = oniguruma.loadWASM(wasm).then(() => ({
	createOnigScanner: (patterns) => new oniguruma.OnigScanner(patterns),
	createOnigString: (s) => new oniguruma.OnigString(s),
}));

const registry = new vsctm.Registry({
	onigLib,
	loadGrammar: async (scopeName) => {
		if (scopeName !== 'source.wrench') {
			return null;
		}
		const file = path.join(root, 'syntaxes', 'wrench.tmLanguage.json');
		return vsctm.parseRawGrammar(fs.readFileSync(file, 'utf8'), file);
	},
});

const lines = [
	'function add( a, b ) // sum',
	'var s = "x=%d\\n";',
	'var h = { "k":1, 2:0x1F, 3:1.5f, 4:\'c\' };',
	'struct Point { var x; };',
	'var p = new Point;',
	'::total += math::sqrt( p.x ) + Color::RED + arr._count;',
	'if ( a != b && !c ) { return yield( 1 ); } /* done */',
	'unit helper( x ) { return x; }',
	'switch ( a ) {',
	'\tcase 1: break;',
	'    default: break;',
	'default:',
	'  retry: goto retry;',
	'include "lib/util.w";',
	'private var count = 0;',
	'var include = private + 1;',
];

function scopeOf(tokens, line, text) {
	const start = line.indexOf(text);
	const tok = tokens.find((t) => t.startIndex <= start && start < t.endIndex);
	return tok ? tok.scopes.join(' ') : '';
}

registry.loadGrammar('source.wrench').then((grammar) => {
	let state = vsctm.INITIAL;
	const results = [];
	for (const line of lines) {
		const r = grammar.tokenizeLine(line, state);
		results.push({ line, tokens: r.tokens });
		state = r.ruleStack;
	}

	const expect = [
		[0, 'function', 'storage.type.function'],
		[0, 'add', 'entity.name.function'],
		[0, 'b', 'variable.parameter'],
		[0, '// sum', 'comment.line'],
		[1, '%d', 'constant.other.placeholder'],
		[1, '\\n', 'constant.character.escape'],
		[2, '0x1F', 'constant.numeric.hex'],
		[2, '1.5f', 'constant.numeric.float'],
		[2, '\'c\'', 'string.quoted.single'],
		[3, 'Point', 'entity.name.type.struct'],
		[4, 'new', 'keyword.operator.new'],
		[5, 'total', 'variable.other.global'],
		[5, 'sqrt', 'support.function.library'],
		[5, 'RED', 'variable.other.enummember'],
		[5, '_count', 'support.function.builtin'],
		[6, '&&', 'keyword.operator.logical'],
		[6, 'return', 'keyword.control'],
		[6, 'yield', 'keyword.control'],
		[6, '/* done */', 'comment.block'],
		[7, 'unit', 'storage.type.function'],
		[7, 'helper', 'entity.name.function'],
		[7, 'x', 'variable.parameter'],
		[9, 'case', 'keyword.control'],
		[10, 'default', 'keyword.control'],
		[11, 'default', 'keyword.control'],
		[12, 'retry', 'entity.name.label'],
		[13, 'include', 'keyword.control.import'],
		[13, 'lib/util.w', 'string.quoted.double'],
		[14, 'private', 'storage.modifier'],
		[14, 'var', 'storage.type'],
	];
	// scopes a token must not have
	const reject = [
		[9, 'case', 'entity.name.label'],
		[10, 'default', 'entity.name.label'],
		[11, 'default', 'entity.name.label'],
		[15, 'include', 'keyword.control.import'], // a plain name elsewhere
		[15, 'private', 'storage.modifier'],
	];

	let failed = 0;
	for (const [i, text, scope] of expect) {
		const got = scopeOf(results[i].tokens, results[i].line, text);
		if (!got.includes(scope)) {
			failed++;
			console.log(`FAIL '${text}' on line ${i}: expected ${scope}, got ${got}`);
		}
	}
	for (const [i, text, scope] of reject) {
		const got = scopeOf(results[i].tokens, results[i].line, text);
		if (got.includes(scope)) {
			failed++;
			console.log(`FAIL '${text}' on line ${i}: must not be ${scope}, got ${got}`);
		}
	}
	assert.strictEqual(failed, 0);
	console.log(`PASS grammar (${expect.length + reject.length} checks)`);
}).catch((e) => {
	console.log('FAIL grammar: ' + e.stack);
	process.exit(1);
});
