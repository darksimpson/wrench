# Releases and the single-file build

This page explains how the two files most users add to their projects,
`wrench.h` and `wrench.cpp`, are made from the sources, how versions are
numbered, and how a release is published. It is for maintainers and for
anyone who wants a single file build of their own checkout.

## The single file build

The sources in `src/` are the real code. The single file build puts the
part of them a host or firmware needs into two files, so a project adds
wrench by adding `wrench.h` and `wrench.cpp`. It is not kept in git: every
release has it attached (see below), and you make it yourself with either
of:

```bash
make dist                    # on Linux, writes dist/wrench.h and dist/wrench.cpp
wrench release src dist      # anywhere, with a wrench executable; creates dist
```

`wrench release <fromdir> <todir>` creates `todir` when it does not
exist, reads the sources from `fromdir` and:

* writes `todir/wrench.h`: `src/wrench.h` with `#define WRENCH_COMBINED`
  in front, which tells the header that the rest is in one file;
* writes `todir/wrench.cpp`: the internal headers and then the `.cpp`
  files of the compiler, the VM, the utilities, the debug server and its
  transports, the disassembler, the standard library and the device
  libraries, in a fixed order (the list `sourceOrder` in
  `src/utils/wrench_cli.cpp`), wrapped in `#pragma warning` lines for MSVC;
* writes `version.txt`, the version of `fromdir/wrench.h`, into the folder
  that holds `fromdir`: the repository root for `release src dist` (see
  [Versions](#versions)).

A source it cannot read or a file it cannot write stops it with a message
that names the file, and the tool exits with -1; `make dist` and CI fail
then.

Nothing of the host tools goes in: the debug client, the console debugger,
the debug adapter, the language server and the command line itself are
only built from `src/`. A file added to `src/` that the library needs must
also be added to `sourceOrder`, or the single file build will not link.

### Checking it

```bash
make dist-check
```

builds `dist`, then compiles `dist/wrench.cpp` on its own as C++98 with
`-Wall -Werror` in six configurations, and finally links
`examples/basic.cpp` against it and runs it:

```text
single file build: []
single file build: [-DWRENCH_INCLUDE_DEBUG_CODE -DWRENCH_LINUX_TCP -DWRENCH_LINUX_SERIAL -DWRENCH_LINUX_FILE_IO]
single file build: [-DWRENCH_COMPACT]
single file build: [-DWRENCH_COMPACT -DWRENCH_REALLY_COMPACT -DWRENCH_INCLUDE_DEBUG_CODE]
single file build: [-DWRENCH_WITHOUT_COMPILER -DWRENCH_COMPACT -DWRENCH_REALLY_COMPACT]
single file build: [-DWRENCH_UNPROTECTED_STACK -DWRENCH_HANDLE_MALLOC_FAIL -DWRENCH_TIME_SLICES]
g++ -std=c++98 -Wall -Werror -O1 -Idist -DWRENCH_INCLUDE_DEBUG_CODE -DWRENCH_LINUX_TCP dist/wrench.cpp examples/basic.cpp -o dist/basic
cd examples && ../dist/basic
Hello World!
0123456789
```

CI does the same, and also compiles the single file build with MSVC on
Windows (default, with the debug server, Win32 TCP, serial and file I/O,
and compact).

`examples/make_examples.sh` builds `examples/basic.cpp` and
`examples/multi_context.cpp` against `../dist`, and
`src/utils/wrench_min.cpp` measures the smallest VM (without compiler,
really compact) built from it.

## Versions

The version is three numbers, defined in `src/wrench.h`:

```cpp
#define WRENCH_VERSION_MAJOR 8
#define WRENCH_VERSION_MINOR 0
#define WRENCH_VERSION_BUILD 0
```

| Where | Shows |
|---|---|
| `wrench v` and the help text | `version: 8.0` |
| `wrench dbg`, VS Code's Debug Console | `wrench 8.0.0`, the version the target reports when a debugger connects |
| `wrench lsp` | its server information to the editor |
| `version.txt` | `8.0.0`, written by `wrench release` (and so by `make dist`) |

The major version salts every bytecode image's checksum. A VM loads
images of its own major version and, through
`WRENCH_PREVIOUS_IMAGE_VERSION`, those of the version before, which it
runs with their old behaviour; an older VM refuses a newer image (see
[The bytecode image](bytecode.md#compatibility)). Raise the major version
when images change in a way older VMs cannot run or that changes what
existing programs do; raise the minor or build number for everything
else.

## Publishing a release

1. Update `WRENCH_VERSION_*` in `src/wrench.h`.
2. Make the heading of the top section of the `CHANGELIST` the new
   version, without `(unreleased)`.
3. Run `make dist` (or `wrench release src dist`) so `version.txt`
   matches, and commit.
4. Push a tag `v` plus the version, for example `v8.1.0`.

For a tag starting with `v`, CI runs all its jobs. The single file build
job checks that the tag is exactly `v` followed by the contents of
`version.txt` and fails otherwise:

```text
tag v8.1.0 but wrench.h says 8.0.0
```

When every job passed, the job "Publish release" attaches these files to
the GitHub release of that tag:

| File | What it is |
|---|---|
| `wrench.h`, `wrench.cpp` | the single file build |
| `wrench-windows-x86.exe` | the command line tool for Windows, the tested Release build |
| `wrench-linux-x64.tar.gz` | the command line tool for Linux, a static build that runs on any distribution |
| `wrench-<version>.vsix` | the VS Code extension with both executables bundled |
| `wrench-book.zip` | this book, built, to read offline |

It creates the release (titled with the version, without the `v`, and
with generated notes) or, when it exists already, replaces its files.
Every CI run, tagged or not, also keeps the same files as build artifacts:
`wrench-single-file`, `wrench-windows-x86`, `wrench-linux-x64`,
`wrench-vscode` and the built book as `wrench-book`.

The VS Code extension is versioned separately (`version` in
`tools/vscode-wrench/package.json`, now 0.1.0), so the name of the
`.vsix` carries its own version, not the one of the tag; it is attached to
the release but not published to the Visual Studio Marketplace. See
[Building the extension](../vscode/building.md).

## The CHANGELIST

`CHANGELIST` in the repository root lists what changed, newest first. Each
version has a heading line, the version followed by dashes, and one line
per change below it, starting with `- `. Changes not released yet collect
under the next version marked `(unreleased)`, now `8.0.0 (unreleased)`.
Changes in behaviour start with
"behaviour change:", so users can find what may break their scripts.
Larger work has sections of its own under the version (for example
"Debugger, rebuilt"). Add your line in the same commit as the change.
