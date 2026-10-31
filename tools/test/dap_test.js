// End to end test of "wrench dap": drives the adapter the way VS Code does.
// usage: node dap_test.js [path to wrench executable] [test name]
'use strict';

const { spawn, execFileSync } = require('child_process');
const path = require('path');
const fs = require('fs');
const os = require('os');
const assert = require('assert');

const exe = process.argv[2] || path.join(__dirname, '..', '..', 'win32', 'Debug', 'wrench.exe');
const fixtures = path.join(__dirname, 'fixtures');

class DapClient {
	constructor(args) {
		this.proc = spawn(exe, args || ['dap'], { stdio: ['pipe', 'pipe', 'pipe'] });
		this.seq = 1;
		this.buffer = Buffer.alloc(0);
		this.pending = new Map();
		this.events = [];
		this.waiters = [];
		this.stderr = '';
		this.proc.stdout.on('data', (d) => this.onData(d));
		this.proc.stderr.on('data', (d) => { this.stderr += d.toString(); });
	}

	onData(data) {
		this.buffer = Buffer.concat([this.buffer, data]);
		for (;;) {
			const headerEnd = this.buffer.indexOf('\r\n\r\n');
			if (headerEnd < 0) {
				return;
			}
			const header = this.buffer.slice(0, headerEnd).toString();
			const match = /Content-Length: (\d+)/.exec(header);
			assert(match, 'bad header: ' + header);
			const length = parseInt(match[1], 10);
			if (this.buffer.length < headerEnd + 4 + length) {
				return;
			}
			const body = this.buffer.slice(headerEnd + 4, headerEnd + 4 + length).toString();
			this.buffer = this.buffer.slice(headerEnd + 4 + length);
			this.onMessage(JSON.parse(body));
		}
	}

	onMessage(msg) {
		if (msg.type === 'response') {
			const p = this.pending.get(msg.request_seq);
			if (p) {
				this.pending.delete(msg.request_seq);
				p(msg);
			}
		} else if (msg.type === 'event') {
			this.events.push(msg);
			this.waiters = this.waiters.filter((w) => !w());
		}
	}

	request(command, args) {
		const seq = this.seq++;
		const msg = JSON.stringify({ seq, type: 'request', command, arguments: args || {} });
		this.proc.stdin.write(`Content-Length: ${Buffer.byteLength(msg)}\r\n\r\n${msg}`);
		return new Promise((resolve, reject) => {
			const timer = setTimeout(() => reject(new Error('timeout waiting for ' + command)), 15000);
			this.pending.set(seq, (r) => { clearTimeout(timer); resolve(r); });
		});
	}

	async ok(command, args) {
		const r = await this.request(command, args);
		assert(r.success, `${command} failed: ${r.message}`);
		return r.body;
	}

	// wait for an event (consumed in order of arrival)
	waitEvent(name, predicate) {
		return new Promise((resolve, reject) => {
			const timer = setTimeout(() => reject(new Error('timeout waiting for event ' + name + '\nstderr: ' + this.stderr)), 15000);
			const check = () => {
				const i = this.events.findIndex((e) => e.event === name && (!predicate || predicate(e.body)));
				if (i >= 0) {
					const e = this.events.splice(i, 1)[0];
					clearTimeout(timer);
					resolve(e.body);
					return true;
				}
				return false;
			};
			if (!check()) {
				this.waiters.push(check);
			}
		});
	}

	output() {
		return this.events.filter((e) => e.event === 'output').map((e) => e.body.output).join('');
	}

	close() {
		this.proc.kill();
	}
}

async function variablesByName(dap, ref) {
	const vars = (await dap.ok('variables', { variablesReference: ref })).variables;
	const map = {};
	for (const v of vars) {
		map[v.name] = v;
	}
	return map;
}

async function testLaunchSession() {
	const dap = new DapClient();
	const program = path.join(fixtures, 'sample.w');

	const caps = await dap.ok('initialize', { adapterID: 'wrench', linesStartAt1: true, columnsStartAt1: true });
	assert(caps.supportsConditionalBreakpoints && caps.supportsLogPoints && caps.supportsSetVariable);

	await dap.ok('launch', { program, stopOnEntry: true });
	await dap.waitEvent('initialized');

	// line 12 has no code, the breakpoint moves to 13
	const bps = await dap.ok('setBreakpoints', {
		source: { path: program },
		breakpoints: [{ line: 12 }, { line: 19, condition: 'n == 3' }, { line: 26, logMessage: 'about to print {total}' }],
	});
	assert.strictEqual(bps.breakpoints[0].line, 13);
	assert(bps.breakpoints.every((b) => b.verified));
	await dap.ok('setExceptionBreakpoints', { filters: ['runtimeErrors'] });
	await dap.ok('configurationDone');

	let stopped = await dap.waitEvent('stopped');
	assert.strictEqual(stopped.reason, 'entry');
	let st = await dap.ok('stackTrace', { threadId: 1 });
	assert.strictEqual(st.stackFrames[0].line, 1);

	// remove the line 13 breakpoint but keep the others
	await dap.ok('setBreakpoints', {
		source: { path: program },
		breakpoints: [{ line: 19, condition: 'n == 3' }, { line: 26, logMessage: 'about to print {total}' }],
	});

	await dap.ok('continue', { threadId: 1 });
	stopped = await dap.waitEvent('stopped');
	assert.strictEqual(stopped.reason, 'breakpoint');
	st = await dap.ok('stackTrace', { threadId: 1 });
	assert.strictEqual(st.stackFrames[0].name, 'outer');
	assert.strictEqual(st.stackFrames[0].line, 19);
	assert.strictEqual(st.stackFrames.length, 2);

	const scopes = (await dap.ok('scopes', { frameId: st.stackFrames[0].id })).scopes;
	assert.deepStrictEqual(scopes.map((s) => s.name), ['Locals', 'Globals']);
	const locals = await variablesByName(dap, scopes[0].variablesReference);
	assert.strictEqual(locals.n.value, '3', 'condition n == 3 should have filtered earlier hits');
	assert.strictEqual(locals.r.value, '4');

	const globals = await variablesByName(dap, scopes[1].variablesReference);
	assert.strictEqual(globals.g.value, '10');
	assert.strictEqual(globals.s.value, '"hello"');
	assert.strictEqual(globals.arr.value, 'array[3]');
	assert(globals.arr.variablesReference > 0 && globals.arr.indexedVariables === 3);

	const arr = (await dap.ok('variables', { variablesReference: globals.arr.variablesReference })).variables;
	assert.deepStrictEqual(arr.map((v) => v.value), ['1', '2.5', '"x"']);
	const pt = await variablesByName(dap, globals.p.variablesReference);
	assert.strictEqual(pt.y.value, '7');
	const hash = await variablesByName(dap, globals.h.variablesReference);
	assert.strictEqual(hash['["a"]'].value, '1');

	// evaluate for watch, hover and the console
	let ev = await dap.ok('evaluate', { expression: 'r * 10 + n', frameId: st.stackFrames[0].id, context: 'watch' });
	assert.strictEqual(ev.result, '43');
	ev = await dap.ok('evaluate', { expression: 'h[2]', frameId: st.stackFrames[0].id, context: 'hover' });
	assert.strictEqual(ev.result, '"two"');
	ev = await dap.ok('evaluate', { expression: 'g = g + 5', frameId: st.stackFrames[0].id, context: 'repl' });
	assert.strictEqual(ev.result, '15');
	const bad = await dap.request('evaluate', { expression: 'nosuch + 1', frameId: st.stackFrames[0].id, context: 'watch' });
	assert(!bad.success && /no variable/.test(bad.message));

	// setVariable through the variables view
	const sv = await dap.ok('setVariable', { variablesReference: scopes[0].variablesReference, name: 'r', value: '100' });
	assert.strictEqual(sv.value, '100');

	const completions = await dap.ok('completions', { text: 'to', column: 3, frameId: st.stackFrames[0].id });
	assert(completions.targets.some((t) => t.label === 'total'));

	const locations = await dap.ok('breakpointLocations', { source: { path: program }, line: 11, endLine: 15 });
	assert.deepStrictEqual(locations.breakpoints.map((b) => b.line), [13, 14]);

	// step into add, then out again
	await dap.ok('stepIn', { threadId: 1 });
	stopped = await dap.waitEvent('stopped');
	st = await dap.ok('stackTrace', { threadId: 1 });
	assert.strictEqual(stopped.reason, 'step');

	// continue: the log point prints and does not stop, the program ends
	await dap.ok('continue', { threadId: 1 });
	const exited = await dap.waitEvent('exited');
	assert.strictEqual(exited.exitCode, 0);
	await dap.waitEvent('terminated');
	const out = dap.output();
	assert(/about to print \d+/.test(out), 'log point output missing: ' + out);
	assert(/total=\d+/.test(out), 'program output missing: ' + out);

	await dap.ok('disconnect', { terminateDebuggee: true });
	dap.close();
}

// watchpoints: a global, an element and a local (which goes away with its
// call), conditions evaluated by the adapter
async function testDataBreakpoints() {
	const dap = new DapClient();
	const program = path.join(fixtures, 'sample.w');
	const caps = await dap.ok('initialize', { adapterID: 'wrench' });
	assert(caps.supportsDataBreakpoints);
	await dap.ok('launch', { program });
	await dap.waitEvent('initialized');
	await dap.ok('setBreakpoints', { source: { path: program }, breakpoints: [{ line: 13 }] });
	await dap.ok('configurationDone');

	let stopped = await dap.waitEvent('stopped');
	assert.strictEqual(stopped.reason, 'breakpoint');
	let st = await dap.ok('stackTrace', { threadId: 1 });
	const scopes = (await dap.ok('scopes', { frameId: st.stackFrames[0].id })).scopes;
	const globals = await variablesByName(dap, scopes[1].variablesReference);

	const total = await dap.ok('dataBreakpointInfo', { variablesReference: scopes[1].variablesReference, name: 'total' });
	assert(total.dataId && total.canPersist && total.accessTypes[0] === 'write');
	assert.strictEqual(total.description, '::total');

	const arrVars = (await dap.ok('variables', { variablesReference: globals.arr.variablesReference })).variables;
	assert.strictEqual(arrVars[1].evaluateName, '::arr[1]');
	const element = await dap.ok('dataBreakpointInfo', { variablesReference: globals.arr.variablesReference, name: arrVars[1].name });
	assert.strictEqual(element.description, '::arr[1]');

	const local = await dap.ok('dataBreakpointInfo', { variablesReference: scopes[0].variablesReference, name: 'c' });
	assert(local.dataId && !local.canPersist);
	const none = await dap.ok('dataBreakpointInfo', { name: 'nosuch', frameId: st.stackFrames[0].id });
	assert(none.dataId === null && /no variable/.test(none.description));

	let set = await dap.ok('setDataBreakpoints', { breakpoints: [{ dataId: total.dataId }, { dataId: local.dataId }] });
	assert(set.breakpoints.every((b) => b.verified));
	const localId = set.breakpoints[1].id;
	const totalId = set.breakpoints[0].id;
	await dap.ok('setBreakpoints', { source: { path: program }, breakpoints: [] });

	// c = a + b writes the local
	await dap.ok('continue', { threadId: 1 });
	stopped = await dap.waitEvent('stopped');
	assert.strictEqual(stopped.reason, 'data breakpoint');
	assert.deepStrictEqual(stopped.hitBreakpointIds, [localId]);
	assert.strictEqual(stopped.description, 'c changed');
	st = await dap.ok('stackTrace', { threadId: 1 });
	assert.strictEqual(st.stackFrames[0].line, 14);

	// add returns: its local goes, then total changes in the loop
	await dap.ok('continue', { threadId: 1 });
	const gone = await dap.waitEvent('breakpoint', (b) => b.breakpoint.id === localId);
	assert.strictEqual(gone.breakpoint.verified, false);
	stopped = await dap.waitEvent('stopped');
	assert.deepStrictEqual(stopped.hitBreakpointIds, [totalId]);
	let ev = await dap.ok('evaluate', { expression: 'total', frameId: 1, context: 'watch' });
	assert.strictEqual(ev.result, '2');

	// a condition: only the last change of the loop passes
	set = await dap.ok('setDataBreakpoints', { breakpoints: [{ dataId: total.dataId, condition: 'total > 20' }] });
	assert(set.breakpoints[0].verified && set.breakpoints[0].id === totalId);
	await dap.ok('continue', { threadId: 1 });
	stopped = await dap.waitEvent('stopped');
	assert.strictEqual(stopped.reason, 'data breakpoint');
	ev = await dap.ok('evaluate', { expression: 'total', frameId: 1, context: 'watch' });
	assert.strictEqual(ev.result, '30');

	await dap.ok('continue', { threadId: 1 });
	await dap.waitEvent('exited');
	await dap.ok('disconnect', { terminateDebuggee: true });
	dap.close();
}

// a program built from several files: breakpoints and stack frames in the
// included file, its folder found through wrench.json
async function testIncludedFiles() {
	const dap = new DapClient();
	const dir = path.join(fixtures, 'includes');
	const program = path.join(dir, 'main.w');
	const util = path.join(dir, 'lib', 'util.w');
	await dap.ok('initialize', { adapterID: 'wrench' });
	await dap.ok('launch', { program });
	await dap.waitEvent('initialized');

	const bps = await dap.ok('setBreakpoints', { source: { path: util }, breakpoints: [{ line: 4 }] });
	assert(bps.breakpoints[0].verified, JSON.stringify(bps));
	assert.strictEqual(bps.breakpoints[0].line, 5); // the brace has no code
	const locations = await dap.ok('breakpointLocations', { source: { path: util }, line: 1, endLine: 8 });
	assert.deepStrictEqual(locations.breakpoints.map((b) => b.line), [1, 5, 6]); // line 1 is top level code
	const other = await dap.ok('setBreakpoints', { source: { path: path.join(dir, 'shared', 'nowhere.w') }, breakpoints: [{ line: 1 }] });
	assert(!other.breakpoints[0].verified);
	await dap.ok('configurationDone');

	const stopped = await dap.waitEvent('stopped');
	assert.strictEqual(stopped.reason, 'breakpoint');
	const st = await dap.ok('stackTrace', { threadId: 1 });
	assert.strictEqual(st.stackFrames[0].name, 'utilSquare');
	assert.strictEqual(st.stackFrames[0].line, 5);
	assert(path.resolve(st.stackFrames[0].source.path).toLowerCase() === path.resolve(util).toLowerCase(), st.stackFrames[0].source.path);
	assert.strictEqual(st.stackFrames[1].line, 6);
	assert(path.resolve(st.stackFrames[1].source.path).toLowerCase() === path.resolve(program).toLowerCase(), st.stackFrames[1].source.path);

	// a private global is "calls@util.w" in the Globals of any frame, and
	// that name works in expressions and watchpoints; plain "calls" only
	// means it in util.w
	const mainScopes = (await dap.ok('scopes', { frameId: st.stackFrames[1].id })).scopes;
	const mainGlobalsRef = mainScopes.find((s) => s.name === 'Globals').variablesReference;
	const mainGlobals = await variablesByName(dap, mainGlobalsRef);
	assert.strictEqual(mainGlobals['calls@util.w'].evaluateName, '::calls@util.w');
	const inMain = (expression) => dap.request('evaluate', { expression, frameId: st.stackFrames[1].id, context: 'watch' });
	assert.strictEqual((await inMain('::calls@util.w + 1')).body.result, '1');
	assert(/no variable named 'calls'/.test((await inMain('calls')).message));
	assert(/no variable named 'calls@nowhere\.w'/.test((await inMain('::calls@nowhere.w')).message));
	const inUtil = await dap.ok('evaluate', { expression: 'calls', frameId: st.stackFrames[0].id, context: 'watch' });
	assert.strictEqual(inUtil.result, '0');
	const calls = await dap.ok('dataBreakpointInfo', { variablesReference: mainGlobalsRef, name: 'calls@util.w' });
	assert(calls.dataId, calls.description);
	assert.strictEqual(calls.description, '::calls@util.w');
	const watch = await dap.ok('setDataBreakpoints', { breakpoints: [{ dataId: calls.dataId }] });
	assert(watch.breakpoints[0].verified, JSON.stringify(watch));

	// the main file's breakpoints stay apart from the included file's
	await dap.ok('setBreakpoints', { source: { path: program }, breakpoints: [{ line: 8 }] });
	await dap.ok('setBreakpoints', { source: { path: util }, breakpoints: [] });
	await dap.ok('continue', { threadId: 1 });
	const changed = await dap.waitEvent('stopped');
	assert.strictEqual(changed.reason, 'data breakpoint');
	assert.strictEqual(changed.description, '::calls@util.w changed');
	await dap.ok('setDataBreakpoints', { breakpoints: [] });
	await dap.ok('continue', { threadId: 1 });
	const second = await dap.waitEvent('stopped');
	assert.strictEqual(second.reason, 'breakpoint');
	const st2 = await dap.ok('stackTrace', { threadId: 1 });
	assert.strictEqual(st2.stackFrames[0].line, 8);

	await dap.ok('continue', { threadId: 1 });
	await dap.waitEvent('exited');
	assert(/total=105/.test(dap.output()), dap.output());
	await dap.ok('disconnect', { terminateDebuggee: true });
	dap.close();
}

// a module the script compiles: a program of its own on the importer's
// thread, its source served by reference, breakpoints set in it
async function testImportedModule() {
	const dap = new DapClient();
	const program = path.join(fixtures, 'import.w');
	await dap.ok('initialize', { adapterID: 'wrench' });
	await dap.ok('launch', { program });
	await dap.waitEvent('initialized');
	await dap.ok('setBreakpoints', { source: { path: program }, breakpoints: [{ line: 3 }] });
	await dap.ok('configurationDone');

	const module = await dap.waitEvent('module');
	assert.strictEqual(module.reason, 'new');
	assert.strictEqual(module.module.id, 1);
	assert.strictEqual(module.module.name, 'import 1');
	const stopped = await dap.waitEvent('stopped');
	assert.strictEqual(stopped.reason, 'breakpoint');
	assert.strictEqual(stopped.threadId, 1);
	const modules = await dap.ok('modules', {});
	assert.deepStrictEqual(modules.modules.map((m) => m.name), ['import.w', 'import 1']);
	const threads = await dap.ok('threads');
	assert.strictEqual(threads.threads.length, 1); // a module runs on its importer's stack

	await dap.ok('stepIn', { threadId: 1 });
	assert.strictEqual((await dap.waitEvent('stopped')).threadId, 1);
	const st = await dap.ok('stackTrace', { threadId: 1 });
	assert.strictEqual(st.stackFrames.length, 2);
	const inModule = st.stackFrames[0];
	assert.strictEqual(inModule.name, 'triple');
	assert.strictEqual(inModule.line, 3);
	assert.strictEqual(inModule.moduleId, 1);
	// the module's source is shown from a read-only copy named by its hash,
	// the same file in every session
	const modulePath = inModule.source.path;
	assert(/wrench-sources[\\/][0-9a-f]{8}[\\/]module\.w$/.test(modulePath || ''), JSON.stringify(inModule.source));
	assert(!inModule.source.sourceReference, JSON.stringify(inModule.source));
	assert(/import 1/.test(inModule.source.name), inModule.source.name);
	assert.strictEqual(st.stackFrames[1].line, 3);
	assert(path.resolve(st.stackFrames[1].source.path).toLowerCase() === path.resolve(program).toLowerCase());
	assert(/var t = x \* 3;/.test(fs.readFileSync(modulePath, 'utf8')));
	assert.strictEqual(fs.statSync(modulePath).mode & 0o222, 0, 'the copy is read-only');

	const scopes = await dap.ok('scopes', { frameId: 1 });
	assert.deepStrictEqual(scopes.scopes.map((s) => s.name), ['Locals', 'Globals of import 1']);
	const locals = await variablesByName(dap, scopes.scopes[0].variablesReference);
	assert.strictEqual(locals.x.value, '5');
	const outer = await dap.ok('scopes', { frameId: 2 });
	const globals = await variablesByName(dap, outer.scopes[0].variablesReference);
	assert(/function triple/.test(globals.src.value), JSON.stringify(globals.src));
	assert.strictEqual((await dap.ok('evaluate', { expression: 'x', frameId: 1, context: 'watch' })).result, '5');

	// a breakpoint in the module's embedded source
	const bps = await dap.ok('setBreakpoints', { source: inModule.source, breakpoints: [{ line: 4 }] });
	assert(bps.breakpoints[0].verified, JSON.stringify(bps));
	assert.strictEqual(bps.breakpoints[0].line, 4);
	// VS Code places a verified breakpoint by the path in the reply
	assert.strictEqual(bps.breakpoints[0].source.path, modulePath, JSON.stringify(bps));
	await dap.ok('continue', { threadId: 1 });
	const hit = await dap.waitEvent('stopped');
	assert.strictEqual(hit.reason, 'breakpoint');
	assert.deepStrictEqual(hit.hitBreakpointIds, [bps.breakpoints[0].id]);
	const st2 = await dap.ok('stackTrace', { threadId: 1 });
	assert.strictEqual(st2.stackFrames[0].line, 4);
	assert.strictEqual((await dap.ok('evaluate', { expression: 't', frameId: 1, context: 'watch' })).result, '15');

	await dap.ok('continue', { threadId: 1 });
	await dap.waitEvent('exited');
	assert(/15/.test(dap.output()), dap.output());
	await dap.ok('disconnect', { terminateDebuggee: true });
	dap.close();

	// the next session: VS Code sends the breakpoint kept in the copy before
	// the module exists. The image beside the copy places it, the target
	// holds it by the module's hash and sets it when the module is imported:
	// the call right after the import stops
	const next = new DapClient();
	await next.ok('initialize', { adapterID: 'wrench' });
	await next.ok('launch', { program });
	await next.waitEvent('initialized');
	const kept = await next.ok('setBreakpoints', { source: { name: 'module.w', path: modulePath }, breakpoints: [{ line: 4 }] });
	assert(kept.breakpoints[0].verified && kept.breakpoints[0].line === 4, JSON.stringify(kept));
	await next.ok('configurationDone');
	const bound = await next.waitEvent('breakpoint', (b) => b.breakpoint.id === kept.breakpoints[0].id && b.breakpoint.verified);
	assert.strictEqual(bound.breakpoint.source.path, modulePath, JSON.stringify(bound));
	const again = await next.waitEvent('stopped');
	assert.strictEqual(again.reason, 'breakpoint');
	const st3 = await next.ok('stackTrace', { threadId: 1 });
	assert.strictEqual(st3.stackFrames[0].name, 'triple');
	assert.strictEqual(st3.stackFrames[0].line, 4);
	assert.strictEqual(st3.stackFrames[0].source.path, modulePath);
	await next.ok('disconnect', { terminateDebuggee: true });
	next.close();
}

async function testRuntimeError() {
	const dap = new DapClient();
	const program = path.join(fixtures, 'failing.w');
	await dap.ok('initialize', { adapterID: 'wrench' });
	await dap.ok('launch', { program });
	await dap.waitEvent('initialized');
	await dap.ok('setExceptionBreakpoints', { filters: ['runtimeErrors'] });
	await dap.ok('configurationDone');

	const stopped = await dap.waitEvent('stopped');
	assert.strictEqual(stopped.reason, 'exception');
	assert(/function_not_found/.test(stopped.text));
	assert(/'missing' is not provided by the target/.test(stopped.description), stopped.description);
	const info = await dap.ok('exceptionInfo', { threadId: 1 });
	assert(/line 3/.test(info.description));
	assert(/'missing'/.test(info.description), info.description);
	const st = await dap.ok('stackTrace', { threadId: 1 });
	assert.strictEqual(st.stackFrames[0].name, 'inner');
	const ev = await dap.ok('evaluate', { expression: 'v', frameId: 1, context: 'watch' });
	assert.strictEqual(ev.result, '5');

	await dap.ok('continue', { threadId: 1 });
	const exited = await dap.waitEvent('exited');
	assert.notStrictEqual(exited.exitCode, 0);
	await dap.ok('disconnect', {});
	dap.close();
}

// a script for a host application: wrench.json next to it says what the
// application provides, the local target imitates that
async function testHostProject() {
	const program = path.join(fixtures, 'host_app', 'app.w');

	const dap = new DapClient();
	await dap.ok('initialize', { adapterID: 'wrench' });
	await dap.ok('launch', { program });
	await dap.waitEvent('initialized');
	await dap.ok('setExceptionBreakpoints', { filters: ['runtimeErrors'] });
	await dap.ok('configurationDone');

	const stopped = await dap.waitEvent('stopped');
	assert.strictEqual(stopped.reason, 'exception');
	assert(/'openWindow' is not provided by the target/.test(stopped.description), stopped.description);
	const st = await dap.ok('stackTrace', { threadId: 1 });
	assert.strictEqual(st.stackFrames[0].line, 6);
	const answer = await dap.ok('evaluate', { expression: 'answer', frameId: 1, context: 'watch' });
	assert.strictEqual(answer.result, '42');
	const confirmed = await dap.ok('evaluate', { expression: 'confirmed', frameId: 1, context: 'watch' });
	assert.strictEqual(confirmed.result, '1');
	const out = dap.output();
	assert(/Imitating the host application described by .*wrench\.json/.test(out), out);
	assert(/getAnswer is provided by the host application, here it returns 42/.test(out), out);
	assert(/ui::alert is provided by the host application, here it returns 1/.test(out), out);
	await dap.ok('disconnect', { terminateDebuggee: true });
	dap.close();

	// "project": "none" debugs with the plain wrench host, getAnswer is missing
	const plain = new DapClient();
	await plain.ok('initialize', { adapterID: 'wrench' });
	await plain.ok('launch', { program, project: 'none' });
	await plain.waitEvent('initialized');
	await plain.ok('setExceptionBreakpoints', { filters: ['runtimeErrors'] });
	await plain.ok('configurationDone');
	const stopped2 = await plain.waitEvent('stopped');
	assert(/'getAnswer' is not provided by the target/.test(stopped2.description), stopped2.description);
	await plain.ok('disconnect', { terminateDebuggee: true });
	plain.close();
}

// a compiled program as "program": symbols and source come from the bytecode
async function testBinaryProgram() {
	const sample = path.join(fixtures, 'sample.w');
	const withSource = path.join(os.tmpdir(), `wrench_dap_src_${process.pid}.bin`);
	const withoutSource = path.join(os.tmpdir(), `wrench_dap_nosrc_${process.pid}.bin`);
	execFileSync(exe, ['c', '-debugsource', sample, withSource]);
	execFileSync(exe, ['c', '-debug', sample, withoutSource]);

	try {
		const dap = new DapClient();
		await dap.ok('initialize', { adapterID: 'wrench' });
		await dap.ok('launch', { program: withSource, stopOnEntry: true });
		await dap.waitEvent('initialized');
		// breakpoints are set in the embedded source document
		const bps = await dap.ok('setBreakpoints', { source: { sourceReference: 1 }, breakpoints: [{ line: 19 }] });
		assert(bps.breakpoints[0].verified && bps.breakpoints[0].line === 19);
		await dap.ok('configurationDone');

		let stopped = await dap.waitEvent('stopped');
		assert.strictEqual(stopped.reason, 'entry');
		let st = await dap.ok('stackTrace', { threadId: 1 });
		// shown from a copy named by the program's hash; reference 1 still
		// names the main file (the breakpoint above, the source request)
		assert(/wrench-sources[\\/][0-9a-f]{8}[\\/]sample\.w$/.test(st.stackFrames[0].source.path || ''), JSON.stringify(st.stackFrames[0].source));
		assert(/embedded source/.test(st.stackFrames[0].source.name), st.stackFrames[0].source.name);
		assert(/function outer/.test(fs.readFileSync(st.stackFrames[0].source.path, 'utf8')));
		const src = await dap.ok('source', { sourceReference: 1 });
		assert(/function outer/.test(src.content));

		await dap.ok('continue', { threadId: 1 });
		stopped = await dap.waitEvent('stopped');
		assert.strictEqual(stopped.reason, 'breakpoint');
		st = await dap.ok('stackTrace', { threadId: 1 });
		assert.strictEqual(st.stackFrames[0].name, 'outer');
		assert.strictEqual(st.stackFrames[0].line, 19);
		const ev = await dap.ok('evaluate', { expression: 'r', frameId: 1, context: 'watch' });
		assert.strictEqual(ev.result, '1');
		await dap.ok('disconnect', { terminateDebuggee: true });
		dap.close();

		// without -debugsource it still runs, there is just no source to show
		const plain = new DapClient();
		await plain.ok('initialize', { adapterID: 'wrench' });
		await plain.ok('launch', { program: withoutSource });
		await plain.waitEvent('initialized');
		await plain.ok('configurationDone');
		const exited = await plain.waitEvent('exited');
		assert.strictEqual(exited.exitCode, 0);
		assert(/carries no source/.test(plain.output()), plain.output());
		assert(/total=\d+/.test(plain.output()), plain.output());
		await plain.ok('disconnect', {});
		plain.close();
	} finally {
		fs.rmSync(withSource, { force: true });
		fs.rmSync(withoutSource, { force: true });
	}
}

// firmware that called setAllowUpload(false), played by dbgserver -noupload
async function testUploadRefused() {
	const port = 19000 + Math.floor(Math.random() * 2000);
	const program = path.join(fixtures, 'sample.w');
	const server = spawn(exe, ['dbgserver', '-target', `tcp:${port}`, '-noupload', program], { stdio: ['ignore', 'pipe', 'pipe'] });
	await new Promise((resolve) => setTimeout(resolve, 500));

	try {
		// launch would upload: refused with a hint
		const dap = new DapClient();
		await dap.ok('initialize', { adapterID: 'wrench' });
		const r = await dap.request('launch', { program, transport: 'tcp', host: '127.0.0.1', port });
		assert(!r.success && /does not accept uploaded programs/.test(r.message), r.message);
		dap.close();

		// attach with the local source never uploads
		const att = new DapClient();
		await att.ok('initialize', { adapterID: 'wrench' });
		await att.ok('attach', { program, transport: 'tcp', host: '127.0.0.1', port });
		await att.waitEvent('initialized');
		await att.ok('configurationDone');
		assert(!/different build/.test(att.output()), att.output());
		await att.ok('disconnect', {});
		att.close();

		// launch without upload runs what the target holds
		const run = new DapClient();
		await run.ok('initialize', { adapterID: 'wrench' });
		await run.ok('launch', { program, transport: 'tcp', host: '127.0.0.1', port, upload: false });
		await run.waitEvent('initialized');
		await run.ok('setBreakpoints', { source: { path: program }, breakpoints: [{ line: 14 }] });
		await run.ok('configurationDone');
		await run.waitEvent('stopped');
		const st = await run.ok('stackTrace', { threadId: 1 });
		assert.strictEqual(st.stackFrames[0].name, 'add');
		await run.ok('disconnect', { terminateDebuggee: true });
		run.close();
	} finally {
		server.kill();
	}
}

// firmware that started its script with breakOnEntry(), played by
// dbgserver -hold: attaching finds it waiting on the first line
async function testAttachToWaitingScript() {
	const port = 23000 + Math.floor(Math.random() * 2000);
	const program = path.join(fixtures, 'sample.w');
	const server = spawn(exe, ['dbgserver', '-target', `tcp:${port}`, '-hold', program], { stdio: ['ignore', 'pipe', 'pipe'] });
	await new Promise((resolve) => setTimeout(resolve, 500));

	try {
		const dap = new DapClient();
		await dap.ok('initialize', { adapterID: 'wrench' });
		await dap.ok('attach', { program, transport: 'tcp', host: '127.0.0.1', port });
		await dap.waitEvent('initialized');
		await dap.ok('setBreakpoints', { source: { path: program }, breakpoints: [{ line: 14 }] });
		await dap.ok('configurationDone');

		let stopped = await dap.waitEvent('stopped');
		assert.strictEqual(stopped.reason, 'entry');
		let st = await dap.ok('stackTrace', { threadId: 1 });
		assert.strictEqual(st.stackFrames[0].line, 1);

		await dap.ok('continue', { threadId: 1 });
		stopped = await dap.waitEvent('stopped');
		assert.strictEqual(stopped.reason, 'breakpoint');
		st = await dap.ok('stackTrace', { threadId: 1 });
		assert.strictEqual(st.stackFrames[0].name, 'add');
		await dap.ok('disconnect', {});
		dap.close();
	} finally {
		server.kill();
	}
}

// requests with odd arguments fail on their own instead of ending the
// session; a false conditional breakpoint inside a stepped-over call does
// not cancel the step
async function testRobustness() {
	const dap = new DapClient();
	const program = path.join(fixtures, 'sample.w');
	await dap.ok('initialize', { adapterID: 'wrench', linesStartAt1: true, columnsStartAt1: false });
	await dap.ok('launch', { program });
	await dap.waitEvent('initialized');
	await dap.ok('setBreakpoints', { source: { path: program }, breakpoints: [{ line: 18 }] });
	await dap.ok('configurationDone');
	let stopped = await dap.waitEvent('stopped');
	assert.strictEqual(stopped.reason, 'breakpoint');

	const completions = await dap.request('completions', { text: 'to', column: 0, frameId: 1 });
	assert(completions.success, 'completions with column 0');
	const frames = await dap.request('stackTrace', { threadId: 1, startFrame: -1 });
	assert(frames.success && frames.body.stackFrames.length > 0, 'stackTrace with startFrame -1');
	const body = '{ this is not json';
	dap.proc.stdin.write(`Content-Length: ${Buffer.byteLength(body)}\r\n\r\n${body}`);
	await dap.ok('threads');

	// step over line 18: add() runs into a breakpoint whose condition is false
	await dap.ok('setBreakpoints', { source: { path: program }, breakpoints: [{ line: 18 }, { line: 13, condition: 'a > 100' }] });
	await dap.ok('next', { threadId: 1 });
	stopped = await dap.waitEvent('stopped');
	assert.strictEqual(stopped.reason, 'step');
	const st = await dap.ok('stackTrace', { threadId: 1 });
	assert.strictEqual(st.stackFrames[0].line, 19, 'the step must end on the next line of outer()');

	await dap.ok('disconnect', { terminateDebuggee: true });
	dap.close();
}

async function testErrors() {
	const dap = new DapClient();
	await dap.ok('initialize', { adapterID: 'wrench' });
	const r = await dap.request('launch', { program: path.join(fixtures, 'does_not_exist.w') });
	assert(!r.success && /cannot read/.test(r.message));
	const r2 = await dap.request('launch', { program: path.join(fixtures, 'sample.w'), transport: 'tcp', port: 1, connectTimeout: 500 });
	assert(!r2.success && /cannot connect/.test(r2.message));
	dap.close();
}

async function testRemoteAttach() {
	// a dbgserver target preloaded with the program, debugged through attach
	const port = 15000 + Math.floor(Math.random() * 2000);
	const program = path.join(fixtures, 'sample.w');
	const server = spawn(exe, ['dbgserver', '-target', `tcp:${port}`, program], { stdio: ['ignore', 'pipe', 'pipe'] });
	await new Promise((resolve) => setTimeout(resolve, 500));

	try {
		const dap = new DapClient();
		await dap.ok('initialize', { adapterID: 'wrench' });
		// launch remotely: upload, run, stop on a breakpoint
		await dap.ok('launch', { program, transport: 'tcp', host: '127.0.0.1', port });
		await dap.waitEvent('initialized');
		await dap.ok('setBreakpoints', { source: { path: program }, breakpoints: [{ line: 14, hitCondition: '3' }] });
		await dap.ok('configurationDone');
		await dap.waitEvent('stopped');
		const st = await dap.ok('stackTrace', { threadId: 1 });
		assert.strictEqual(st.stackFrames[0].name, 'add');
		const ev = await dap.ok('evaluate', { expression: 'a', frameId: 1, context: 'watch' });
		assert.strictEqual(ev.result, '2', 'hit condition 3 should stop on the third call');
		await dap.ok('disconnect', { terminateDebuggee: true });
		dap.close();

		// attach without a local file: symbols and source come from the target
		const dap2 = new DapClient();
		await dap2.ok('initialize', { adapterID: 'wrench' });
		await dap2.ok('attach', { transport: 'tcp', host: '127.0.0.1', port });
		await dap2.waitEvent('initialized');
		await dap2.ok('configurationDone');
		const src = await dap2.ok('source', { sourceReference: 1 });
		assert(/function outer/.test(src.content));
		await dap2.ok('disconnect', {});
		dap2.close();
	} finally {
		server.kill();
	}
}

// a target that runs a second script in a context of its own: a thread of
// its own, breakpoints in its file, its globals
async function testAttachedContext() {
	const port = 15000 + Math.floor(Math.random() * 2000);
	const program = path.join(fixtures, 'sample.w');
	const ticker = path.join(fixtures, 'ticker.w');
	const server = spawn(exe, ['dbgserver', '-target', `tcp:${port}`, '-context', ticker, program], { stdio: ['ignore', 'pipe', 'pipe'] });
	await new Promise((resolve) => setTimeout(resolve, 500));

	try {
		const dap = new DapClient();
		await dap.ok('initialize', { adapterID: 'wrench' });
		await dap.ok('attach', { program, transport: 'tcp', host: '127.0.0.1', port });
		await dap.waitEvent('initialized');
		const threads = await dap.ok('threads');
		assert.deepStrictEqual(threads.threads.map((t) => [t.id, t.name]), [[1, 'wrench'], [2, 'ticker.w']]);
		const modules = await dap.ok('modules', {});
		assert.deepStrictEqual(modules.modules.map((m) => m.name), ['sample.w', 'ticker.w']);

		const bps = await dap.ok('setBreakpoints', { source: { path: ticker }, breakpoints: [{ line: 6 }] });
		assert(bps.breakpoints[0].verified, JSON.stringify(bps));
		await dap.ok('configurationDone');

		const stopped = await dap.waitEvent('stopped');
		assert.strictEqual(stopped.reason, 'breakpoint');
		assert.strictEqual(stopped.threadId, 2);
		const st = await dap.ok('stackTrace', { threadId: 2 });
		assert.strictEqual(st.stackFrames[0].name, 'tick');
		assert.strictEqual(st.stackFrames[0].line, 6);
		assert(path.resolve(st.stackFrames[0].source.path).toLowerCase() === path.resolve(ticker).toLowerCase(), JSON.stringify(st.stackFrames[0].source));
		const idle = await dap.ok('stackTrace', { threadId: 1 });
		assert.strictEqual(idle.stackFrames.length, 0);

		const scopes = await dap.ok('scopes', { frameId: 1 });
		assert.deepStrictEqual(scopes.scopes.map((s) => s.name), ['Locals', 'Globals of ticker.w']);
		const globals = await variablesByName(dap, scopes.scopes[1].variablesReference);
		assert(parseInt(globals.ticks.value, 10) >= 1, JSON.stringify(globals));
		const ticks = await dap.ok('evaluate', { expression: 'ticks', frameId: 1, context: 'watch' });
		assert.strictEqual(ticks.result, globals.ticks.value);

		// the program's own breakpoints and globals stay apart
		const info = await dap.ok('dataBreakpointInfo', { name: 'ticks', variablesReference: scopes.scopes[1].variablesReference });
		assert(/@1\|/.test(info.dataId), info.dataId);

		await dap.ok('setBreakpoints', { source: { path: ticker }, breakpoints: [] });
		await dap.ok('continue', { threadId: 2 });
		await dap.ok('disconnect', { terminateDebuggee: false });
		dap.close();
	} finally {
		server.kill();
	}
}

// the uploaded program ending does not end the session while the target
// runs a context of its own
async function testProgramEndsBesideContext() {
	const port = 17000 + Math.floor(Math.random() * 2000);
	const program = path.join(fixtures, 'sample.w');
	const ticker = path.join(fixtures, 'ticker.w');
	const server = spawn(exe, ['dbgserver', '-target', `tcp:${port}`, '-context', ticker, program], { stdio: ['ignore', 'pipe', 'pipe'] });
	await new Promise((resolve) => setTimeout(resolve, 500));

	try {
		const dap = new DapClient();
		await dap.ok('initialize', { adapterID: 'wrench' });
		await dap.ok('launch', { program, transport: 'tcp', host: '127.0.0.1', port });
		await dap.waitEvent('initialized');
		await dap.ok('configurationDone');
		await dap.waitEvent('output', (b) => /keeps running its other programs/.test(b.output));

		await dap.ok('setBreakpoints', { source: { path: ticker }, breakpoints: [{ line: 6 }] });
		const stopped = await dap.waitEvent('stopped');
		assert.strictEqual(stopped.threadId, 2);
		assert(!dap.events.some((e) => e.event === 'terminated'), dap.output());
		assert(/total=/.test(dap.output()), dap.output());

		await dap.ok('setBreakpoints', { source: { path: ticker }, breakpoints: [] });
		await dap.ok('continue', { threadId: 2 });
		await dap.ok('disconnect', { terminateDebuggee: false });
		dap.close();
	} finally {
		server.kill();
	}
}

// attach goes over tcp unless told otherwise, and names the attribute
// that is missing
async function testAttachDefaults() {
	const dap = new DapClient();
	await dap.ok('initialize', { adapterID: 'wrench' });
	const r = await dap.request('attach', {});
	assert(!r.success && /needs the "port" attribute/.test(r.message), r.message);
	const r2 = await dap.request('attach', { transport: 'serial' });
	assert(!r2.success && /needs the "serialPort" attribute/.test(r2.message), r2.message);
	dap.close();

	const port = 37000 + Math.floor(Math.random() * 2000);
	const program = path.join(fixtures, 'sample.w');
	const server = spawn(exe, ['dbgserver', '-target', `tcp:${port}`, program], { stdio: ['ignore', 'pipe', 'pipe'] });
	await new Promise((resolve) => setTimeout(resolve, 500));
	try {
		const dap2 = new DapClient();
		await dap2.ok('initialize', { adapterID: 'wrench' });
		await dap2.ok('attach', { program, port });
		await dap2.waitEvent('initialized');
		await dap2.ok('configurationDone');
		assert(new RegExp(`Connected to tcp 127\\.0\\.0\\.1:${port}`).test(dap2.output()), dap2.output());
		await dap2.ok('disconnect', {});
		dap2.close();
	} finally {
		server.kill();
	}
}

// a local launch uploads the program itself: that is no news to report
async function testNoLoadedNoticeForOwnUpload() {
	const dap = new DapClient();
	await dap.ok('initialize', { adapterID: 'wrench' });
	await dap.ok('launch', { program: path.join(fixtures, 'sample.w') });
	await dap.waitEvent('initialized');
	await dap.ok('configurationDone');
	await dap.waitEvent('exited');
	assert(/total=\d+/.test(dap.output()), dap.output());
	assert(!/loaded a new program/.test(dap.output()), dap.output());
	await dap.ok('disconnect', {});
	dap.close();
}

// stopping a local session that runs code without debug information (it
// cannot answer requests) is quick
async function testQuickStopWithoutDebugCode() {
	const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'wrench_dap_loop_'));
	const source = path.join(dir, 'loop.w');
	const bin = path.join(dir, 'loop.bin');
	fs.writeFileSync(source, 'var i = 0;\nwhile( 1 )\n{\n\ti = i + 1;\n}\n');
	execFileSync(exe, ['c', source, bin]);
	try {
		for (const command of ['terminate', 'disconnect']) {
			const dap = new DapClient();
			await dap.ok('initialize', { adapterID: 'wrench' });
			await dap.ok('launch', { program: bin });
			await dap.waitEvent('initialized');
			await dap.ok('configurationDone');
			await new Promise((resolve) => setTimeout(resolve, 300));
			const start = Date.now();
			await dap.ok(command, command === 'disconnect' ? { terminateDebuggee: true } : {});
			if (command === 'terminate') {
				await dap.waitEvent('terminated');
				await dap.ok('disconnect', {});
			}
			const took = Date.now() - start;
			assert(took < 2500, `${command} took ${took} ms`);
			dap.close();
		}
	} finally {
		fs.rmSync(dir, { recursive: true, force: true });
	}
}

(async () => {
	const tests = [testLaunchSession, testDataBreakpoints, testIncludedFiles, testImportedModule, testRuntimeError, testHostProject, testBinaryProgram, testUploadRefused, testAttachToWaitingScript, testRobustness, testErrors, testRemoteAttach, testAttachedContext, testProgramEndsBesideContext,
		testAttachDefaults, testNoLoadedNoticeForOwnUpload, testQuickStopWithoutDebugCode];
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
