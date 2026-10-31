// Checks of the VS Code extension that need no VS Code: its manifest, the
// wrench.json schema, the command lines it runs and scripts/bundle-exe.js.
// Run after compiling the extension (npm run compile in tools/vscode-wrench).
// usage: node extension_test.js [path to wrench executable] [test name]
'use strict';

const { spawnSync } = require('child_process');
const path = require('path');
const fs = require('fs');
const os = require('os');
const assert = require('assert');

const exe = path.resolve(process.argv[2] || path.join(__dirname, '..', '..', 'win32', 'Debug', 'wrench.exe'));
const extension = path.join(__dirname, '..', 'vscode-wrench');

function readJson(file) {
	return JSON.parse(fs.readFileSync(file, 'utf8'));
}

// what package.json offers for launch.json agrees with the adapter
async function testDebuggerAttributes() {
	const pkg = readJson(path.join(extension, 'package.json'));
	const attrs = pkg.contributes.debuggers.find((d) => d.type === 'wrench').configurationAttributes;
	for (const request of ['launch', 'attach']) {
		const p = attrs[request].properties;
		assert(p.connectTimeout && p.connectTimeout.default === 5000, `${request}: connectTimeout`);
		assert(/2000/.test(p.timeout.description) && /serial/.test(p.timeout.description), `${request}: timeout ${p.timeout.description}`);
		assert(/needed/.test(p.port.description), `${request}: port ${p.port.description}`);
		for (const [name, prop] of Object.entries(p)) {
			assert(prop.description, `${request}.${name} has no description`);
		}
	}
	assert.strictEqual(attrs.attach.properties.transport.default, 'tcp');
	assert.strictEqual(attrs.launch.properties.transport.default, 'local');
}

// wrench.json files with "include" are valid, and the descriptions say what
// the libraries hold
async function testProjectSchema() {
	const schema = readJson(path.join(extension, 'schemas', 'wrench.schema.json'));
	assert.strictEqual(schema.additionalProperties, false);
	const include = schema.properties.include;
	assert(include && include.type === 'array' && include.items.type === 'string' && include.description, 'no "include" in the schema');
	const libs = schema.properties.libraries.oneOf[1].items;
	const io = libs.enumDescriptions[libs.enum.indexOf('io')];
	assert(/^io::/.test(io) && !/time/.test(io), io);
	// every top level key of the example projects is in the schema
	for (const file of [path.join(__dirname, '..', '..', 'examples', 'debug_demo', 'wrench.json')]) {
		for (const key of Object.keys(readJson(file))) {
			assert(schema.properties[key], `${file}: "${key}" is not in the schema`);
		}
	}
}

// Show Disassembly compiles like the editor checks: non-strict when
// wrench.nonStrict is set
async function testCommandLines() {
	const commands = require(path.join(extension, 'out', 'commands.js'));
	assert.deepStrictEqual(commands.disassemblyArgs('a.w', false), ['p', '-debug', 'a.w']);
	assert.deepStrictEqual(commands.disassemblyArgs('a.w', true), ['p', '-debug', '-nostrict', 'a.w']);
	assert.deepStrictEqual(commands.compileArgs('a.w', 'a.bin', true, true), ['c', '-debugsource', '-nostrict', 'a.w', 'a.bin']);

	// and the executable takes it: a script without 'var' disassembles
	const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'wrench_ext_'));
	try {
		const script = path.join(dir, 'loose.w');
		fs.writeFileSync(script, 'x = 5;\nprintln( x );\n');
		const strict = spawnSync(exe, commands.disassemblyArgs(script, false), { encoding: 'utf8' });
		assert.notStrictEqual(strict.status, 0, strict.stdout);
		const loose = spawnSync(exe, commands.disassemblyArgs(script, true), { encoding: 'utf8' });
		assert.strictEqual(loose.status, 0, loose.stdout);
		assert(/Wrench Bytecode Disassembly/.test(loose.stdout), loose.stdout);
	} finally {
		fs.rmSync(dir, { recursive: true, force: true });
	}
}

// bundle-exe checks its arguments before it touches bin/, and replaces only
// the platform-architecture folders it bundles
async function testBundleExe() {
	const root = fs.mkdtempSync(path.join(os.tmpdir(), 'wrench_bundle_'));
	try {
		fs.mkdirSync(path.join(root, 'scripts'));
		const script = path.join(root, 'scripts', 'bundle-exe.js');
		fs.copyFileSync(path.join(extension, 'scripts', 'bundle-exe.js'), script);
		const bin = path.join(root, 'bin');
		fs.mkdirSync(path.join(bin, 'win32-ia32'), { recursive: true });
		fs.mkdirSync(path.join(bin, 'linux-x64'), { recursive: true });
		fs.writeFileSync(path.join(bin, 'win32-ia32', 'wrench.exe'), 'old windows');
		fs.writeFileSync(path.join(bin, 'linux-x64', 'wrench'), 'old linux');

		const run = (...args) => spawnSync(process.execPath, [script, ...args], { encoding: 'utf8' });
		let r = run(path.join(root, 'missing'));
		assert.notStrictEqual(r.status, 0, r.stdout + r.stderr);
		assert.strictEqual(fs.readFileSync(path.join(bin, 'win32-ia32', 'wrench.exe'), 'utf8'), 'old windows');
		assert.strictEqual(fs.readFileSync(path.join(bin, 'linux-x64', 'wrench'), 'utf8'), 'old linux');

		// a 64 bit little endian ELF header for arm64
		const elf = Buffer.alloc(64);
		elf.write('\x7fELF', 0, 'latin1');
		elf[4] = 2;
		elf[5] = 1;
		elf.writeUInt16LE(0xb7, 18);
		const arm = path.join(root, 'wrench-arm64');
		fs.writeFileSync(arm, elf);
		r = run(arm);
		assert.strictEqual(r.status, 0, r.stdout + r.stderr);
		assert(fs.existsSync(path.join(bin, 'linux-arm64', 'wrench')), r.stdout);
		assert.strictEqual(fs.readFileSync(path.join(bin, 'win32-ia32', 'wrench.exe'), 'utf8'), 'old windows');
		assert.strictEqual(fs.readFileSync(path.join(bin, 'linux-x64', 'wrench'), 'utf8'), 'old linux');

		if (fs.existsSync(exe) && process.platform === 'win32') {
			r = run(exe);
			assert.strictEqual(r.status, 0, r.stdout + r.stderr);
			assert(fs.readFileSync(path.join(bin, 'win32-ia32', 'wrench.exe')).equals(fs.readFileSync(exe)));
			assert.strictEqual(fs.readFileSync(path.join(bin, 'linux-x64', 'wrench'), 'utf8'), 'old linux');
		}
	} finally {
		fs.rmSync(root, { recursive: true, force: true });
	}
}

(async () => {
	const tests = [testDebuggerAttributes, testProjectSchema, testCommandLines, testBundleExe];
	let failed = 0;
	for (const t of tests.filter((t) => !process.argv[3] || t.name === process.argv[3])) {
		try {
			await t();
			console.log(`PASS ${t.name}`);
		} catch (e) {
			failed++;
			console.log(`FAIL ${t.name}: ${e.stack}`);
		}
	}
	process.exit(failed ? 1 : 0);
})();
