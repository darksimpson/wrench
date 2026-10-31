// Copy wrench executables into the extension so the packaged .vsix works
// without any settings. Each binary lands in bin/<platform>-<arch>/, the
// platform and architecture are read from its PE or ELF header; the folders
// of other platforms and architectures already in bin/ are kept.
// usage: node scripts/bundle-exe.js [paths to wrench executables...]
// Without arguments it takes the Windows build from win32/Release and the
// Linux build from the repository root (on Linux or WSL:
// "make LDFLAGS='-static -s'" so it runs on any distribution).
'use strict';

const fs = require('fs');
const path = require('path');

const root = path.join(__dirname, '..');
const repo = path.join(root, '..', '..');

const PE_MACHINES = { 0x14c: 'ia32', 0x8664: 'x64', 0xaa64: 'arm64' };
const ELF_MACHINES = { 0x03: 'ia32', 0x28: 'arm', 0x3e: 'x64', 0xb7: 'arm64' };

// returns { platform, arch } or null when the file is not an executable we know
function identify(file) {
	const fd = fs.openSync(file, 'r');
	try {
		const head = Buffer.alloc(64);
		fs.readSync(fd, head, 0, 64, 0);
		if (head[0] === 0x4d && head[1] === 0x5a) {
			const peOffset = head.readUInt32LE(0x3c);
			const pe = Buffer.alloc(6);
			fs.readSync(fd, pe, 0, 6, peOffset);
			if (pe.toString('latin1', 0, 4) !== 'PE\0\0') {
				return null;
			}
			const arch = PE_MACHINES[pe.readUInt16LE(4)];
			return arch ? { platform: 'win32', arch } : null;
		}
		if (head.toString('latin1', 0, 4) === '\x7fELF') {
			const machine = head[5] === 2 ? head.readUInt16BE(18) : head.readUInt16LE(18);
			const arch = ELF_MACHINES[machine];
			return arch ? { platform: 'linux', arch } : null;
		}
		return null;
	} finally {
		fs.closeSync(fd);
	}
}

const explicit = process.argv.length > 2;
const candidates = explicit ? process.argv.slice(2) : [
	path.join(repo, 'win32', 'Release', 'wrench.exe'),
	path.join(repo, 'wrench'),
];

// every argument is checked before bin/ is touched, so a failed run leaves
// it as it was
const copies = [];
for (const source of candidates) {
	if (!fs.existsSync(source)) {
		if (explicit) {
			console.error(`not found: ${source}`);
			process.exit(1);
		}
		continue;
	}
	const kind = identify(source);
	if (!kind) {
		console.error(`not a Windows or Linux executable: ${source}`);
		process.exit(1);
	}
	copies.push({ source, kind, target: `${kind.platform}-${kind.arch}` });
}
if (!copies.length) {
	console.error('nothing to bundle (build win32/wrench.vcxproj Release, or run make LDFLAGS=\'-static -s\' in the repository root)');
	process.exit(1);
}

// only the platform-architecture folders bundled now are replaced, the
// others in bin/ stay
const destRoot = path.join(root, 'bin');
for (const copy of copies) {
	const destDir = path.join(destRoot, copy.target);
	fs.rmSync(destDir, { recursive: true, force: true });
	fs.mkdirSync(destDir, { recursive: true });
	const dest = path.join(destDir, copy.kind.platform === 'win32' ? 'wrench.exe' : 'wrench');
	fs.copyFileSync(copy.source, dest);
	if (process.platform !== 'win32') {
		fs.chmodSync(dest, 0o755);
	}
	console.log(`bundled ${copy.source} -> bin/${copy.target}`);
}

const present = fs.readdirSync(destRoot);
if (!present.some((d) => d.startsWith('win32-'))) {
	console.warn('warning: bin/ has no Windows build (build win32/wrench.vcxproj Release)');
}
if (!present.some((d) => d.startsWith('linux-'))) {
	console.warn('warning: bin/ has no Linux build (run make LDFLAGS=\'-static -s\' in the repository root)');
}
