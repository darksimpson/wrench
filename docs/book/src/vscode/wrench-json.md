# Describing your host application: wrench.json

Scripts for an application call the natives it registers and can use only
the libraries it loads. A `wrench.json` file tells the wrench tools about
that host, so the editor can check and complete the calls and the debugger
can imitate the host on a PC. This page describes every field, where the
file is looked for, and what each tool does with it.

## An example

An application that registers two natives of its own, a library function
`ui::alert()`, `println()`, and loads only the math and string libraries:

```cpp
wr_registerFunction( w, "openDoc", openDoc );
wr_registerFunction( w, "log", logValues );
wr_registerFunction( w, "println", printLine );
wr_registerLibraryFunction( w, "ui::alert", uiAlert );
wr_loadMathLib( w );
wr_loadStringLib( w );
```

Its `wrench.json`, next to the scripts or in a folder above them:

```json
{
	"libraries": [ "math", "string" ],
	"functions": [
		{ "name": "openDoc", "params": [ "path", "mode?" ], "doc": "Opens a document, returns its handle", "returns": 1 },
		{ "name": "ui::alert", "params": [ "text" ], "doc": "Shows a message box" },
		"log( values... )",
		"println( values... )"
	],
	"undeclaredCalls": "warning",
	"include": [ "lib", "../shared" ]
}
```

How natives and library functions are written on the host side is in
[Native functions and libraries](../embedding/natives.md).

## Where it is found

For a script, the tools take the nearest `wrench.json` in the script's
folder or in a folder above it. A whole project usually has one, in its
root. A tool can be pointed at a file instead (`-project` for `wrench dbg`
and `wrench dbgserver`, `"project"` in a
[launch configuration](launch.md#project)), and `none` turns the search
off.

## The fields

All fields are optional. The tools ignore fields they do not know; the
JSON schema VS Code checks the file against allows only these and
`$schema`. A file that is not valid JSON, or whose fields have the wrong
form, is reported and ignored as a whole.

### libraries

The standard libraries the application loads: a list of names, or the
string `"all"`. Missing, or a list containing `"all"`, means every library,
as with `wr_loadAllLibs()`.

| Name | Loader | Provides |
|---|---|---|
| `math` | `wr_loadMathLib()` | `math::` |
| `string` | `wr_loadStringLib()` | `str::` |
| `std` | `wr_loadStdLib()` | `std::` (except serialization) and `time::` |
| `io` | `wr_loadIOLib()` | `io::` |
| `msg` | `wr_loadMessageLib()` | `msg::` |
| `sys` | `wr_loadSysLib()` | `sys::` |
| `serialize` | `wr_loadSerializeLib()` | `std::serialize`, `std::deserialize` |
| `debug` | `wr_loadDebugLib()` | `debug::` |
| `tcp` | `wr_loadTCPLib()` | `tcp::` |
| `container` | `wr_loadContainerLib()` | `array::`, `hash::`, `list::`, `queue::`, `stack::` |

The library functions themselves are in the
[Standard library](../library/index.md) part.

### functions

The natives (`wr_registerFunction()`) and library functions
(`wr_registerLibraryFunction()`) of the application, each either a
signature string or an object.

A signature string is the name, optionally followed by the parameters in
parentheses:

```json
"functions": [ "led( on )", "app::log( values... )", "reset()", "beep" ]
```

An object has these fields:

| Field | Type | Meaning |
|---|---|---|
| `name` | string | required: `name` or `lib::name` |
| `params` | list of strings | the parameter names; leave the field out to skip the argument count check |
| `doc` | string | shown on hover and in completion |
| `returns` | number, string, boolean or null | what the local debug target answers in place of the real function, default 0 |

In both forms a parameter ending in `?` is optional and one ending in
`...` takes any number of arguments, so `"open( path, mode? )"` takes one
or two arguments and `"log( values... )"` any number. A string without
parentheses (`"beep"`), like an object without `params`, declares the
function without checking its arguments; `"reset()"` takes none.

A name may appear only once. List here also:

* `print` and `println`, if your application registers them. The wrench
  tools have them, a host does not unless it registers them, so the editor
  warns about calls to them while they are not declared;
* the functions of modules your scripts import at run time
  (`sys::importCompile`), which the editor cannot see.

`returns` is used by the local debug target only (see below); a number
without a fraction is answered as an int, other numbers as a float, `true`
and `false` as 1 and 0.

### undeclaredCalls

How the editor reports calls to functions that are neither declared here,
nor defined in the script or the files it includes, nor part of a loaded
library: `"warning"` (the default), `"error"` or `"ignore"`. It applies to
calls of libraries the host does not load as well. Argument counts that do
not fit `params` are always warnings.

### include

Folders where `include "file.w"` also looks, after the folder of the file
that has the `include`. Relative folders are relative to the folder of
`wrench.json`.

```json
"include": [ "lib", "../shared" ]
```

The command line tools search `-I` folders before these. The debuggers
also look for the included files of a program in these folders when they
open them; see [Several files, programs and modules](../debugging/programs.md).
The language side of `include` is in
[Several files: include and private](../language/include.md).

## What uses it

### The editor

The language server (see [Editing features](editing.md)) reads the
`wrench.json` of every open script and reads it again when it changes:

* completion offers the declared functions, `lib::` namespaces of the host
  included, and leaves out libraries the host does not load;
* hover shows a declared function's signature and `doc`, and on a library
  function that is not loaded, a note saying so;
* signature help shows the declared parameters;
* go to definition on a declared function opens `wrench.json` at its
  declaration;
* every call is checked: undeclared functions, unloaded libraries and
  argument counts, see
  [Calls checked against wrench.json](editing.md#calls-checked-against-wrenchjson);
* `include` resolves through its folders.

VS Code also validates the file against the extension's JSON schema while
you edit it, with completion of the field names and library names.

### The local debug target

A launch with the local transport (VS Code) or `wrench dbg` without
`-target` imitates the host:

* only the listed libraries are loaded;
* `print` and `println` are always there, they write to the debugger;
* a declared function answers its `returns` value and says so once in the
  output, for example
  `[wrench] led is provided by the host application, here it returns 7`;
* any other function stops the program with a runtime error that names
  it: `'str::len' is not provided by the target (its 'string' library is
  not loaded there)`.

Without a `wrench.json` the local target loads every library and fails on
any native other than `print` and `println`.

### wrench dbgserver

`wrench dbgserver` with a script imitates the host of that script's
`wrench.json` the same way, and reports it when it starts:

```text
imitating the host described by .../wrench.json
```

Without a `wrench.json` it stands in for any host: every native a script
calls answers 0, with a message the first time, so scripts written for a
device run on the PC.

### Error messages on any target

When a script on any target, also real firmware, calls a function the
target does not have, the debugger names the function from the declared
names and the program's source: `'openDoc' is not provided by the
target`.

## Creating one

**Wrench: Describe the Host Application** in the Command Palette opens the
`wrench.json` next to the active script, and creates it from a template
first if there is none (see [Editing features](editing.md#commands)).
