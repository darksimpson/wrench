// End to end test of the console debugger "wrench dbg".
// usage: node cli_test.js [path to wrench executable]
'use strict';

const { spawn, spawnSync, execFileSync } = require('child_process');
const path = require('path');
const fs = require('fs');
const os = require('os');
const assert = require('assert');

const exe = process.argv[2] || path.join(__dirname, '..', '..', 'win32', 'Debug', 'wrench.exe');
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

async function testAttachNeedsRemoteTarget() {
	const out = dbg(['-attach'], '');
	assert(/without a file the program comes from the target/.test(out), out);
}

(async () => {
	const tests = [testBinaryFile, testBinaryWithoutSource, testAttachFetchesProgram, testUploadRefused, testAttachToWaitingScript, testCommandRobustness, testPingWhileStopped, testWatchCommands, testAttachNeedsRemoteTarget];
	let failed = 0;
	for (const t of tests) {
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
