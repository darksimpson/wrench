// End to end test of the console debugger "wrench dbg" and the other
// commands of the command line tool.
// usage: node cli_test.js [path to wrench executable] [test name]
'use strict';

const { spawn, spawnSync, execFileSync } = require('child_process');
const net = require('net');
const path = require('path');
const fs = require('fs');
const os = require('os');
const assert = require('assert');

const exe = path.resolve(process.argv[2] || path.join(__dirname, '..', '..', 'win32', 'Debug', 'wrench.exe'));
const sample = path.join(__dirname, 'fixtures', 'sample.w');

// run "wrench dbg" with commands on stdin, returns everything it printed
function dbg(args, commands) {
	const r = spawnSync(exe, ['dbg', ...args], { input: commands, encoding: 'utf8', timeout: 20000 });
	assert(!r.error, String(r.error));
	return r.stdout + r.stderr;
}

function sleep(ms) {
	return new Promise((resolve) => setTimeout(resolve, ms));
}

// a compiled program with embedded source debugs like its .w file
async function testBinaryFile() {
	const bin = path.join(os.tmpdir(), `wrench_cli_src_${process.pid}.bin`);
	execFileSync(exe, ['c', '-debugsource', sample, bin]);
	try {
		const out = dbg([bin], 'b 19\nr\np n\nq\n');
		assert(/\[breakpoint\] line 19/.test(out), out);
		assert(/return r \* 2/.test(out), out);
		assert(/n = 0/.test(out), out);
	} finally {
		fs.rmSync(bin, { force: true });
	}
}

// without -debugsource the program runs but there is no source to list
async function testBinaryWithoutSource() {
	const bin = path.join(os.tmpdir(), `wrench_cli_nosrc_${process.pid}.bin`);
	execFileSync(exe, ['c', '-debug', sample, bin]);
	try {
		const out = dbg([bin], 'l\nr\nq\n');
		assert(/carries no source/.test(out), out);
		assert(/no source, the program was compiled without -debugsource/.test(out), out);
		assert(/total=\d+/.test(out), out);
	} finally {
		fs.rmSync(bin, { force: true });
	}
}

// -attach without a file reads program and source from the target
async function testAttachFetchesProgram() {
	const port = 17000 + Math.floor(Math.random() * 2000);
	const server = spawn(exe, ['dbgserver', '-target', `tcp:${port}`, sample], { stdio: ['ignore', 'pipe', 'pipe'] });
	await sleep(500);
	try {
		const out = dbg(['-attach', '-target', `tcp:127.0.0.1:${port}`], 'l 13\nb 14\nr\nbt\np a\nq\n');
		assert(/read the program from the target \(\d+ bytes, source embedded\)/.test(out), out);
		assert(/13\s+var c = a \+ b;/.test(out), out);
		assert(/\[breakpoint\] line 14/.test(out), out);
		assert(/#1\s+outer\s+line 18/.test(out), out);
		assert(/a = 0/.test(out), out);
	} finally {
		server.kill();
	}
}

// a target that refuses uploads: dbg says so, -attach still works
async function testUploadRefused() {
	const port = 21000 + Math.floor(Math.random() * 2000);
	const server = spawn(exe, ['dbgserver', '-target', `tcp:${port}`, '-noupload', sample], { stdio: ['ignore', 'pipe', 'pipe'] });
	await sleep(500);
	try {
		let out = dbg(['-target', `tcp:127.0.0.1:${port}`, sample], 'q\n');
		assert(/does not accept uploaded programs/.test(out), out);
		out = dbg(['-attach', '-target', `tcp:127.0.0.1:${port}`, sample], 'b 14\nr\nbt\nq\n');
		assert(/\[breakpoint\] line 14/.test(out), out);
	} finally {
		server.kill();
	}
}

// a script started with breakOnEntry() waits on its first line for us
async function testAttachToWaitingScript() {
	const port = 25000 + Math.floor(Math.random() * 2000);
	const server = spawn(exe, ['dbgserver', '-target', `tcp:${port}`, '-hold', sample], { stdio: ['ignore', 'pipe', 'pipe'] });
	await sleep(500);
	try {
		const out = dbg(['-attach', '-target', `tcp:127.0.0.1:${port}`], 'b 14\nc\nbt\nq\n');
		assert(/\[entry\] line 1/.test(out), out);
		assert(/\[breakpoint\] line 14/.test(out), out);
		assert(/#1\s+outer\s+line 18/.test(out), out);
	} finally {
		server.kill();
	}
}

// commands that used to hang, options in any order, moved breakpoints
async function testCommandRobustness() {
	// the file before the options works too
	let out = dbg([sample, '-target', 'local'], 'c\nb 12\nb 12\nb\nq\n');
	assert(/not stopped, 'r' runs the program/.test(out), out);
	assert(/line 12 has no code, using line 13/.test(out), out);
	assert(/breakpoint removed from line 13/.test(out), out);
	out = dbg(['-target', 'local', sample, '-bogus'], 'q\n');
	assert(/unknown option -bogus/.test(out), out);
}

// a target with a link timeout stays halted while the user thinks: dbg
// pings it from the prompt
async function testPingWhileStopped() {
	const port = 29000 + Math.floor(Math.random() * 2000);
	const server = spawn(exe, ['dbgserver', '-target', `tcp:${port}`, '-hold', '-linktimeout', '1500', sample], { stdio: ['ignore', 'pipe', 'pipe'] });
	await sleep(500);
	const client = spawn(exe, ['dbg', '-attach', '-target', `tcp:127.0.0.1:${port}`], { stdio: ['pipe', 'pipe', 'pipe'] });
	let out = '';
	client.stdout.on('data', (d) => { out += d.toString(); });
	client.stderr.on('data', (d) => { out += d.toString(); });
	const exited = new Promise((resolve) => client.on('exit', resolve));
	try {
		client.stdin.write('b 14\nc\n');
		for (let i = 0; i < 100 && !/\[breakpoint\] line 14/.test(out); ++i) {
			await sleep(50);
		}
		assert(/\[breakpoint\] line 14/.test(out), out);
		await sleep(3000); // twice the target's link timeout
		client.stdin.write('p a\nbt\nq\n');
		await exited;
		assert(/a = 0/.test(out), out);
		assert(/#1\s+outer\s+line 18/.test(out), out);
	} finally {
		client.kill();
		server.kill();
	}
}

// watch: a local goes away with its call, a global stops the loop
async function testWatchCommands() {
	const out = dbg([sample], 'b 13\nr\nwatch c\nwatch ::total\nwatch\nb 13\nc\nc\nunwatch\nq\n');
	assert(/watch 1: c = 0/.test(out), out);
	assert(/watch 2: ::total = 0/.test(out), out);
	assert(/ {2}1: c\r?\n {2}2: ::total/.test(out), out);
	assert(/\[watch 1: c changed\] line 14/.test(out), out);
	assert(/c = 1/.test(out), out);
	assert(/\[watch 1: c went out of scope\]/.test(out), out);
	assert(/\[watch 2: ::total changed\] line 22/.test(out), out);
	assert(/::total = 2/.test(out), out);
	assert(/all watches removed/.test(out), out);
}

// a module the script compiles is a program of its own: stepping into it,
// the stack through it, its breakpoints kept for the next run
async function testImportedModule() {
	const script = path.join(__dirname, 'fixtures', 'import.w');
	const out = dbg([script], 'b 3\nr\nprograms\ns\nbt\np x\nb 4\nc\nc\nr\nb\nc\np t\nc\nq\n');
	assert(/\[program 1: import 1 imported by program 0\]/.test(out), out);
	assert(/>0 {2}import\.w\r?\n 1 {2}import 1 {2}\(imported by program 0\)/.test(out), out);
	assert(/\[step\] line 3 in import 1/.test(out), out);
	assert(/#0 {2}triple {2}line 3 in import 1\r?\n #1 {2}<global> {2}line 3/.test(out), out);
	assert(/x = 5/.test(out), out);
	assert(/breakpoint at line 4 in import 1/.test(out), out);
	assert(/\[program 1 \(import 1\) is gone\]/.test(out), out);
	assert(/ {2}line 4 in import 1: return t;/.test(out), out);
	assert((out.match(/\[breakpoint\] line 4 in import 1/g) || []).length === 2, out);
	assert(/t = 15/.test(out), out);
}

// a target that runs a script in a context of its own beside the program:
// 'c' waits for it to stop, its file takes breakpoints
async function testAttachedContext() {
	const port = 27000 + Math.floor(Math.random() * 2000);
	const ticker = path.join(__dirname, 'fixtures', 'ticker.w');
	const server = spawn(exe, ['dbgserver', '-target', `tcp:${port}`, '-context', ticker, sample], { stdio: ['ignore', 'pipe', 'pipe'] });
	await sleep(500);
	try {
		const out = dbg(['-attach', '-target', `tcp:127.0.0.1:${port}`, sample], 'b ticker.w:6\nc\nbt\np ticks\nq\n');
		assert(/the target debugs 2 programs/.test(out), out);
		assert(/>0 {2}sample\.w\r?\n 1 {2}ticker\.w/.test(out), out);
		assert(/breakpoint at line 6 in ticker\.w/.test(out), out);
		assert(/waiting for a program of the target to stop/.test(out), out);
		assert(/\[breakpoint\] line 6 in ticker\.w/.test(out), out);
		assert(/#0 {2}tick {2}line 6 in ticker\.w/.test(out), out);
		assert(/ticks = \d+/.test(out), out);
	} finally {
		server.kill();
	}
}

async function testAttachNeedsRemoteTarget() {
	const out = dbg(['-attach'], '');
	assert(/without a file the program comes from the target/.test(out), out);
}

// run a wrench command, returns { status, out }
function wrench(args, options) {
	const r = spawnSync(exe, args, { encoding: 'utf8', timeout: 120000, ...options });
	assert(!r.error, String(r.error));
	return { status: r.status, out: r.stdout + r.stderr };
}

function tempDir(name) {
	const dir = path.join(os.tmpdir(), `wrench_cli_${name}_${process.pid}`);
	fs.rmSync(dir, { recursive: true, force: true });
	fs.mkdirSync(dir, { recursive: true });
	return dir;
}

// r and rb fail with a non-zero exit code, a program's own error code
// (sys::halt) is shown as a number, not as a name of the table
async function testRunExitCodes() {
	const dir = tempDir('exit');
	try {
		const halt = path.join(dir, 'halt.w');
		fs.writeFileSync(halt, 'println("before");\nsys::halt(150);\n');
		let r = wrench(['r', halt]);
		assert.strictEqual(r.status, 1, r.out);
		assert(/before/.test(r.out), r.out);
		assert(/err: user error 150/.test(r.out), r.out);
		const bin = path.join(dir, 'halt.bin');
		r = wrench(['c', halt, bin]);
		assert.strictEqual(r.status, 0, r.out);
		r = wrench(['rb', bin]);
		assert.strictEqual(r.status, 1, r.out);
		assert(/err: user error 150/.test(r.out), r.out);
		const bad = path.join(dir, 'bad.w');
		fs.writeFileSync(bad, 'var x = ;\n');
		r = wrench(['r', bad]);
		assert.strictEqual(r.status, 2, r.out);
		assert(/compile error/.test(r.out), r.out);
		r = wrench(['c', bad, bin]);
		assert.strictEqual(r.status, 2, r.out);
		r = wrench(['r', sample]);
		assert.strictEqual(r.status, 0, r.out);
		r = wrench(['v']);
		assert.strictEqual(r.status, 0, r.out);
		assert(/usage: wrench <command>/.test(r.out), r.out);
		assert(!/\bca \[/.test(r.out), r.out);
	} finally {
		fs.rmSync(dir, { recursive: true, force: true });
	}
}

// only the first argument is the command: files may be named like one
async function testCommandWordIsFirst() {
	const dir = tempDir('word');
	try {
		let r = wrench(['c', sample, 'p'], { cwd: dir });
		assert.strictEqual(r.status, 0, r.out);
		assert(fs.existsSync(path.join(dir, 'p')), r.out);
		assert(!/GlobalStop/.test(r.out), r.out);
		r = wrench(['ch', sample, 'out.h', 'd'], { cwd: dir });
		assert.strictEqual(r.status, 0, r.out);
		assert(/const unsigned char d_bytecode\[\]/.test(fs.readFileSync(path.join(dir, 'out.h'), 'utf8')), r.out);
		// options of dbgserver are not taken by dbg, and the other way round
		for (const option of ['-hold', '-noupload', '-v', '-linktimeout', '-context']) {
			r = wrench(['dbg', option, '100', sample]);
			assert(new RegExp(`unknown option ${option} for dbg`).test(r.out), r.out);
			assert.notStrictEqual(r.status, 0, r.out);
		}
		r = wrench(['dbgserver', '-attach', '-target', 'tcp:1', sample]);
		assert(/unknown option -attach for dbgserver/.test(r.out), r.out);
	} finally {
		fs.rmSync(dir, { recursive: true, force: true });
	}
}

// release creates a missing target folder and writes version.txt beside the
// source folder, not into the current one
async function testRelease() {
	const dir = tempDir('release');
	try {
		const src = path.join(dir, 'src');
		fs.cpSync(path.join(__dirname, '..', '..', 'src'), src, { recursive: true });
		const work = path.join(dir, 'work');
		fs.mkdirSync(work);
		let r = wrench(['release', src, path.join(dir, 'dist')], { cwd: work });
		assert.strictEqual(r.status, 0, r.out);
		assert(fs.existsSync(path.join(dir, 'dist', 'wrench.cpp')), r.out);
		assert(/^#define WRENCH_COMBINED/.test(fs.readFileSync(path.join(dir, 'dist', 'wrench.h'), 'utf8')), r.out);
		const header = fs.readFileSync(path.join(src, 'wrench.h'), 'utf8');
		const v = ['MAJOR', 'MINOR', 'BUILD'].map((n) => new RegExp(`#define WRENCH_VERSION_${n} (\\d+)`).exec(header)[1]).join('.');
		assert.strictEqual(fs.readFileSync(path.join(dir, 'version.txt'), 'utf8'), v);
		assert(!fs.existsSync(path.join(work, 'version.txt')), 'version.txt written into the current folder');
		const tcpHeader = fs.readFileSync(path.join(src, 'lib', 'std_tcp.h'), 'utf8');
		const wrenchCpp = fs.readFileSync(path.join(dir, 'dist', 'wrench.cpp'), 'utf8');
		assert.strictEqual(wrenchCpp.split(tcpHeader).length, 2, 'std_tcp.h is in wrench.cpp more than once');
		r = wrench(['release', src, path.join(dir, 'missing', 'dist')], { cwd: work });
		assert.notStrictEqual(r.status, 0, r.out);
		assert(/cannot create the folder/.test(r.out), r.out);
	} finally {
		fs.rmSync(dir, { recursive: true, force: true });
	}
}

// a test script that does not compile is a failure, the following ones
// still run
async function testTestsContinueAfterCompileError() {
	const dir = tempDir('tests');
	try {
		fs.writeFileSync(path.join(dir, 'test_files.txt'), 'bad.w\ngood.w\n');
		fs.writeFileSync(path.join(dir, 'bad.w'), 'var x = ;\n');
		fs.writeFileSync(path.join(dir, 'good.w'), '/*~\nok\n~*/\nprintln("ok");\n');
		const r = wrench(['t', dir], { cwd: path.join(__dirname, '..', '..') });
		assert.notStrictEqual(r.status, 0, r.out);
		assert(/test \[1\]\[bad\.w\]: [^]*compile error/.test(r.out), r.out);
		assert(/test \[2\]\[good\.w\]: PASS/.test(r.out), r.out);
	} finally {
		fs.rmSync(dir, { recursive: true, force: true });
	}
}

// a TCP proxy to a debug server that records the message types of the
// frames the debugger sends
function recordingProxy(targetPort) {
	return new Promise((resolve) => {
		const types = [];
		const sockets = [];
		const server = net.createServer((client) => {
			const target = net.connect(targetPort, '127.0.0.1');
			sockets.push(client, target);
			let buf = Buffer.alloc(0);
			client.on('data', (d) => {
				target.write(d);
				buf = Buffer.concat([buf, d]);
				// sync 0xA5 0x57, type, seq, u16 length, payload, u16 crc
				while (buf.length >= 6) {
					if (buf[0] !== 0xa5 || buf[1] !== 0x57) {
						buf = buf.subarray(1);
						continue;
					}
					const len = buf[4] | (buf[5] << 8);
					if (buf.length < 8 + len) {
						break;
					}
					types.push(buf[2]);
					buf = buf.subarray(8 + len);
				}
			});
			target.on('data', (d) => client.write(d));
			client.on('close', () => target.end());
			target.on('close', () => client.end());
			client.on('error', () => {});
			target.on('error', () => {});
		});
		server.listen(0, '127.0.0.1', () => resolve({
			port: server.address().port,
			types,
			close: () => { sockets.forEach((s) => s.destroy()); server.close(); },
		}));
	});
}

// dbg that cannot use a target (it refuses uploads) leaves it with a
// Detach, not just a dropped link
async function testRefusedTargetDetached() {
	const port = 31000 + Math.floor(Math.random() * 2000);
	const server = spawn(exe, ['dbgserver', '-target', `tcp:${port}`, '-noupload', '-hold', sample], { stdio: ['ignore', 'pipe', 'pipe'] });
	await sleep(500);
	const proxy = await recordingProxy(port);
	try {
		const out = await new Promise((resolve) => {
			const c = spawn(exe, ['dbg', '-target', `tcp:127.0.0.1:${proxy.port}`, sample], { stdio: ['pipe', 'pipe', 'pipe'] });
			let text = '';
			c.stdout.on('data', (d) => { text += d.toString(); });
			c.on('exit', () => resolve(text));
			c.stdin.end('q\n');
		});
		assert(/does not accept uploaded programs/.test(out), out);
		await sleep(300);
		assert(proxy.types.includes(0x01), `hello not sent: ${proxy.types}`);
		assert(proxy.types.includes(0x18), `no detach among ${proxy.types}`);
	} finally {
		proxy.close();
		server.kill();
	}
}

// an interactive dbg session: write commands, wait for output
function session(args) {
	const client = spawn(exe, ['dbg', ...args], { stdio: ['pipe', 'pipe', 'pipe'] });
	const s = { out: '', client };
	client.stdout.on('data', (d) => { s.out += d.toString(); });
	client.stderr.on('data', (d) => { s.out += d.toString(); });
	s.exited = new Promise((resolve) => client.on('exit', resolve));
	s.send = (text) => client.stdin.write(text);
	s.waitFor = async (re, ms = 5000) => {
		for (let i = 0; i < ms / 50 && !re.test(s.out); ++i) {
			await sleep(50);
		}
		assert(re.test(s.out), s.out);
	};
	return s;
}

// a stop of a function the firmware calls shows at the idle prompt without
// another command; 'r' while halted there ends that call, runs the program,
// and the breakpoint is hit again by the firmware's next call
async function testStopsWhileIdleAndRestart() {
	const port = 33000 + Math.floor(Math.random() * 2000);
	const ticker = path.join(__dirname, 'fixtures', 'ticker.w');
	const server = spawn(exe, ['dbgserver', '-target', `tcp:${port}`, '-context', ticker, sample], { stdio: ['ignore', 'pipe', 'pipe'] });
	await sleep(500);
	const s = session(['-attach', '-target', `tcp:127.0.0.1:${port}`, sample]);
	try {
		s.send('b ticker.w:6\n');
		await s.waitFor(/\[breakpoint\] line 6 in ticker\.w/);
		s.send('r\n');
		await s.waitFor(/total=30[^]*\[ended, returned 0\][^]*\[breakpoint\] line 6 in ticker\.w/);
		assert(!/launch failed/.test(s.out), s.out);
		s.send('bt\nq\n');
		await s.exited;
		assert(/#0 {2}tick {2}line 6 in ticker\.w/.test(s.out.slice(s.out.lastIndexOf('[breakpoint]'))), s.out);
		assert.strictEqual((s.out.match(/\[breakpoint\] line 6 in ticker\.w/g) || []).length, 2, s.out);
	} finally {
		s.client.kill();
		server.kill();
	}
}

// tcp:<port> listens on 127.0.0.1 only, tcp:0.0.0.0:<port> on every
// interface
async function testServerListensOnLoopback() {
	const lan = Object.values(os.networkInterfaces()).flat().find((a) => a && a.family === 'IPv4' && !a.internal);
	const canConnect = (host, port) => new Promise((resolve) => {
		const c = net.connect(port, host);
		c.on('connect', () => { c.destroy(); resolve(true); });
		c.on('error', () => resolve(false));
	});
	let port = 35000 + Math.floor(Math.random() * 2000);
	let server = spawn(exe, ['dbgserver', '-target', `tcp:${port}`, sample], { stdio: ['ignore', 'pipe', 'pipe'] });
	let out = '';
	server.stdout.on('data', (d) => { out += d.toString(); });
	await sleep(500);
	try {
		assert(new RegExp(`listening on tcp 127\\.0\\.0\\.1:${port} \\(this machine only\\)`).test(out), out);
		assert(await canConnect('127.0.0.1', port), 'no connection over loopback');
		if (lan) {
			assert(!(await canConnect(lan.address, port)), `tcp:${port} reachable on ${lan.address}`);
		}
	} finally {
		server.kill();
	}
	port += 1;
	server = spawn(exe, ['dbgserver', '-target', `tcp:0.0.0.0:${port}`, sample], { stdio: ['ignore', 'pipe', 'pipe'] });
	out = '';
	server.stdout.on('data', (d) => { out += d.toString(); });
	await sleep(500);
	try {
		assert(new RegExp(`listening on tcp 0\\.0\\.0\\.0:${port} \\(every interface\\)`).test(out), out);
		if (lan) {
			assert(await canConnect(lan.address, port), `tcp:0.0.0.0:${port} not reachable on ${lan.address}`);
		}
	} finally {
		server.kill();
	}
}

(async () => {
	const tests = [testBinaryFile, testBinaryWithoutSource, testAttachFetchesProgram, testUploadRefused, testAttachToWaitingScript, testCommandRobustness, testPingWhileStopped, testWatchCommands, testImportedModule, testAttachedContext, testAttachNeedsRemoteTarget,
		testRunExitCodes, testCommandWordIsFirst, testRelease, testTestsContinueAfterCompileError, testRefusedTargetDetached, testStopsWhileIdleAndRestart, testServerListensOnLoopback];
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
