// Wrench for VS Code: starts "wrench lsp" for editing support and registers
// "wrench dap" as the debug adapter. All the logic lives in the wrench
// executable so the editor, the command line and the targets agree.

import * as vscode from 'vscode';
import * as path from 'path';
import * as fs from 'fs';
import * as cp from 'child_process';
import { LanguageClient, LanguageClientOptions, ServerOptions } from 'vscode-languageclient/node';

let client: LanguageClient | undefined;
let output: vscode.OutputChannel;

const DISASSEMBLY_SCHEME = 'wrench-disassembly';

function executableName(): string {
	return process.platform === 'win32' ? 'wrench.exe' : 'wrench';
}

// configured path, then the copy shipped in the extension, then the PATH
function findExecutable(context: vscode.ExtensionContext): string {
	const configured = vscode.workspace.getConfiguration('wrench').get<string>('executablePath', '').trim();
	if (configured) {
		const resolved = configured.replace('${workspaceFolder}', vscode.workspace.workspaceFolders?.[0]?.uri.fsPath ?? '');
		return resolved;
	}

	for (const target of bundledTargets()) {
		const candidate = path.join(context.extensionPath, 'bin', target, executableName());
		if (fs.existsSync(candidate)) {
			ensureExecutable(candidate);
			return candidate;
		}
	}
	return executableName();
}

// bin/ folders that can run here, best first: 64 bit Windows also runs the
// 32 bit build, Windows on ARM emulates both
function bundledTargets(): string[] {
	const native = `${process.platform}-${process.arch}`;
	if (process.platform === 'win32') {
		return [native, 'win32-x64', 'win32-ia32'].filter((t, i, all) => all.indexOf(t) === i);
	}
	return [native];
}

// a .vsix is a zip and loses the unix execute bit
function ensureExecutable(file: string): void {
	if (process.platform === 'win32') {
		return;
	}
	try {
		fs.accessSync(file, fs.constants.X_OK);
	} catch {
		try {
			fs.chmodSync(file, 0o755);
		} catch (e) {
			output.appendLine(`cannot make ${file} executable: ${e}`);
		}
	}
}

function checkExecutable(exe: string): boolean {
	try {
		const result = cp.spawnSync(exe, ['v'], { timeout: 5000 });
		return !result.error;
	} catch {
		return false;
	}
}

async function startLanguageClient(context: vscode.ExtensionContext): Promise<void> {
	const exe = findExecutable(context);
	if (!checkExecutable(exe)) {
		const choice = await vscode.window.showWarningMessage(
			`Wrench: cannot run '${exe}'. Set "wrench.executablePath" to your wrench executable to enable checking and debugging.`,
			'Open Settings');
		if (choice === 'Open Settings') {
			vscode.commands.executeCommand('workbench.action.openSettings', 'wrench.executablePath');
		}
		return;
	}

	const serverOptions: ServerOptions = {
		run: { command: exe, args: ['lsp'] },
		debug: { command: exe, args: ['lsp'] },
	};

	const clientOptions: LanguageClientOptions = {
		documentSelector: [{ language: 'wrench' }],
		initializationOptions: {
			nonStrict: vscode.workspace.getConfiguration('wrench').get<boolean>('nonStrict', false),
		},
		synchronize: {
			configurationSection: 'wrench',
			// host application descriptions, scripts are rechecked when one changes
			fileEvents: vscode.workspace.createFileSystemWatcher('**/wrench.json'),
		},
		outputChannel: output,
	};

	client = new LanguageClient('wrench', 'Wrench Language Server', serverOptions, clientOptions);
	await client.start();
}

class WrenchDebugAdapterFactory implements vscode.DebugAdapterDescriptorFactory {
	constructor(private readonly context: vscode.ExtensionContext) {}

	createDebugAdapterDescriptor(): vscode.ProviderResult<vscode.DebugAdapterDescriptor> {
		return new vscode.DebugAdapterExecutable(findExecutable(this.context), ['dap']);
	}
}

class WrenchConfigurationProvider implements vscode.DebugConfigurationProvider {
	resolveDebugConfiguration(folder: vscode.WorkspaceFolder | undefined, config: vscode.DebugConfiguration): vscode.ProviderResult<vscode.DebugConfiguration> {
		// F5 without a launch.json: debug the active wrench file locally
		if (!config.type && !config.request && !config.name) {
			const editor = vscode.window.activeTextEditor;
			if (editor && editor.document.languageId === 'wrench') {
				config.type = 'wrench';
				config.name = 'Debug wrench file';
				config.request = 'launch';
				config.program = '${file}';
			}
		}

		if (config.request === 'launch' && !config.program) {
			return vscode.window.showErrorMessage('Wrench: "program" must name the .w file to debug, or a .bin compiled with -debugsource.').then(() => undefined);
		}
		return config;
	}

	provideDebugConfigurations(): vscode.ProviderResult<vscode.DebugConfiguration[]> {
		return [
			{ type: 'wrench', request: 'launch', name: 'Debug wrench file', program: '${file}' },
			{ type: 'wrench', request: 'launch', name: 'Debug on TCP target', program: '${file}', transport: 'tcp', host: '127.0.0.1', port: 5050 },
			{ type: 'wrench', request: 'launch', name: 'Debug on serial target', program: '${file}', transport: 'serial', serialPort: 'COM3', baudRate: 115200 },
			{ type: 'wrench', request: 'attach', name: 'Attach to target', program: '${file}', transport: 'tcp', host: '127.0.0.1', port: 5050 },
		];
	}
}

// read-only documents holding "wrench p" output
class DisassemblyProvider implements vscode.TextDocumentContentProvider, vscode.Disposable {
	private readonly changed = new vscode.EventEmitter<vscode.Uri>();
	readonly onDidChange = this.changed.event;

	constructor(private readonly context: vscode.ExtensionContext) {}

	provideTextDocumentContent(uri: vscode.Uri): string {
		const source = uri.query ? decodeURIComponent(uri.query) : '';
		const result = cp.spawnSync(findExecutable(this.context), ['p', '-debug', source], { encoding: 'utf8', timeout: 20000 });
		if (result.error) {
			return `could not run wrench: ${result.error.message}`;
		}
		return (result.stdout || '') + (result.stderr || '');
	}

	// VS Code keeps an open document's content until told it changed
	refresh(uri: vscode.Uri): void {
		if (vscode.workspace.textDocuments.some((d) => d.uri.toString() === uri.toString())) {
			this.changed.fire(uri);
		}
	}

	dispose(): void {
		this.changed.dispose();
	}
}

function disassemblyUri(file: string): vscode.Uri {
	return vscode.Uri.from({ scheme: DISASSEMBLY_SCHEME, path: path.basename(file) + '.disasm', query: encodeURIComponent(file) });
}

async function activeWrenchFile(): Promise<string | undefined> {
	const editor = vscode.window.activeTextEditor;
	if (!editor || editor.document.languageId !== 'wrench') {
		vscode.window.showWarningMessage('Wrench: open a .w file first.');
		return undefined;
	}
	if (editor.document.isDirty) {
		await editor.document.save();
	}
	return editor.document.uri.fsPath;
}

export async function activate(context: vscode.ExtensionContext): Promise<void> {
	output = vscode.window.createOutputChannel('Wrench');
	context.subscriptions.push(output);

	const disassembly = new DisassemblyProvider(context);
	context.subscriptions.push(
		vscode.debug.registerDebugAdapterDescriptorFactory('wrench', new WrenchDebugAdapterFactory(context)),
		vscode.debug.registerDebugConfigurationProvider('wrench', new WrenchConfigurationProvider()),
		vscode.debug.registerDebugConfigurationProvider('wrench', new WrenchConfigurationProvider(), vscode.DebugConfigurationProviderTriggerKind.Dynamic),
		disassembly,
		vscode.workspace.registerTextDocumentContentProvider(DISASSEMBLY_SCHEME, disassembly),
	);

	context.subscriptions.push(vscode.commands.registerCommand('wrench.showDisassembly', async () => {
		const file = await activeWrenchFile();
		if (!file) {
			return;
		}
		const uri = disassemblyUri(file);
		// a tab still open from an earlier run shows the old output otherwise
		disassembly.refresh(uri);
		const doc = await vscode.workspace.openTextDocument(uri);
		await vscode.window.showTextDocument(doc, { preview: true, viewColumn: vscode.ViewColumn.Beside });
	}));

	// keep an open disassembly in step with its saved source
	context.subscriptions.push(vscode.workspace.onDidSaveTextDocument((saved) => {
		if (saved.languageId === 'wrench' && saved.uri.scheme === 'file') {
			disassembly.refresh(disassemblyUri(saved.uri.fsPath));
		}
	}));

	context.subscriptions.push(vscode.commands.registerCommand('wrench.compile', async () => {
		const file = await activeWrenchFile();
		if (!file) {
			return;
		}
		const outFile = file.replace(/\.[^./\\]+$/, '') + '.bin';
		const args = ['c'];
		if (vscode.workspace.getConfiguration('wrench').get<boolean>('compileDebugInfo', true)) {
			args.push('-debugsource');
		}
		if (vscode.workspace.getConfiguration('wrench').get<boolean>('nonStrict', false)) {
			args.push('-nostrict');
		}
		args.push(file, outFile);
		const result = cp.spawnSync(findExecutable(context), args, { encoding: 'utf8', timeout: 20000 });
		output.appendLine(`wrench ${args.join(' ')}`);
		output.append(result.stdout || '');
		if (result.error || result.status !== 0) {
			output.show(true);
			vscode.window.showErrorMessage('Wrench: compile failed, see the Wrench output.');
		} else {
			vscode.window.showInformationMessage(`Wrench: wrote ${path.basename(outFile)}`);
		}
	}));

	context.subscriptions.push(vscode.commands.registerCommand('wrench.createProjectFile', async () => {
		// next to the active script, else in the first workspace folder
		const editor = vscode.window.activeTextEditor;
		let folder: string | undefined;
		if (editor && editor.document.uri.scheme === 'file') {
			folder = path.dirname(editor.document.uri.fsPath);
		} else {
			folder = vscode.workspace.workspaceFolders?.[0]?.uri.fsPath;
		}
		if (!folder) {
			vscode.window.showWarningMessage('Wrench: open a .w file or a folder first.');
			return;
		}
		const file = path.join(folder, 'wrench.json');
		if (!fs.existsSync(file)) {
			const template = {
				libraries: ['math', 'string'],
				functions: [
					{ name: 'myFunction', params: ['value', 'option?'], doc: 'Registered by the application with wr_registerFunction', returns: 0 },
					{ name: 'app::log', params: ['values...'], doc: 'Registered with wr_registerLibraryFunction' },
				],
				undeclaredCalls: 'warning',
			};
			fs.writeFileSync(file, JSON.stringify(template, null, '\t') + '\n');
		}
		await vscode.window.showTextDocument(vscode.Uri.file(file));
	}));

	context.subscriptions.push(vscode.commands.registerCommand('wrench.restartLanguageServer', async () => {
		if (client) {
			await client.stop();
			client = undefined;
		}
		await startLanguageClient(context);
	}));

	context.subscriptions.push(vscode.workspace.onDidChangeConfiguration(async (e) => {
		if (e.affectsConfiguration('wrench.executablePath')) {
			await vscode.commands.executeCommand('wrench.restartLanguageServer');
		}
	}));

	await startLanguageClient(context);
}

export async function deactivate(): Promise<void> {
	if (client) {
		await client.stop();
	}
}
