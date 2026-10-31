# Editing features

This page describes what the extension does while you edit `.w` files:
the checks, hover, completion and navigation the language server
(`wrench lsp`) provides, the commands the extension adds, and its
settings. Installing it is covered in [Trying it out](quick-try.md).

The extension handles files ending in `.w` and `.wrench` as the language
`wrench`.

## Syntax highlighting and editing basics

Highlighting comes from a TextMate grammar in the extension
(`syntaxes/wrench.tmLanguage.json`) and works without the `wrench`
executable. Keywords, strings with their escapes and format placeholders,
decimal, hex, binary and float numbers, comments, struct and enum names,
function declarations and calls, library calls and `::` names each get a
scope of their own, which your color theme colors.

The language configuration gives the usual editor behaviour: Ctrl+/
toggles `//` comments, Shift+Alt+A block comments; brackets and quotes
close themselves; Enter inside a `/*` comment continues it with ` * `; the
indentation follows braces. Lines `// region` and `// endregion` (also
`// #region`) mark a region that folds.

## Errors as you type

The language server compiles each open file with the real compiler, about
a quarter of a second after you stop typing, and when you save. The first
compile error shows where the compiler found it, with its code and a short
message:

```text
bad expression (near ';')
```

When the compiler only notices the problem at the first token of the next
line (a missing operand or `;` at the end of a line), the error is put on
the end of the line before, where the mistake is.

* Files are checked as `wrench c` compiles them: a variable used without
  `var` is an error. Set `wrench.nonStrict` for older scripts (the
  compiler's `-nostrict`, see
  [Non-strict mode](../language/variables.md#non-strict-mode)); every
  open file is checked again.
* An error in an included file shows on the `include` line that brings the
  file in, with the file and line in the message, for example
  `shapes.w:7: bad expression`.
* Included files are read from disk, or from the editor when they are open
  there, unsaved changes included.
* Code nested deeper than 200 levels is not compiled (the compiler could
  run out of stack); you get a warning instead.

When a [`wrench.json`](wrench-json.md) describes the host application, the
server also checks every call against it (see
[Calls checked against wrench.json](#calls-checked-against-wrenchjson)).

## Hover

Hovering over a name shows what it is:

* a function or struct of your script: its signature, and the comment
  directly above its declaration (`//` lines or a `/* */` block; lines of
  only dashes, equals signs or stars are left out);
* a variable: whether it is a global, a local of which function, a
  parameter or a struct member, and its comment;
* an enum or enum member, with its value;
* something an included file declares: the same, and the file it is in;
* a library function (`math::sin`, `str::format`, `._count`): its
  signature and documentation, and a note when `wrench.json` does not load
  its library;
* a function of the host application from `wrench.json`: its signature and
  `doc`.

```wrench
// Area of a box in square centimetres.
function boxArea( b )
{
	return b.width * b.height;
}
```

Hovering over a call of `boxArea` shows `function boxArea( b )` and the
comment.

## Completion

Completion offers, depending on where you type:

* locals and parameters of the function you are in, the globals declared
  above it, functions, structs, enums and enum members of the file and of
  the files it includes (their `private` names excepted);
* after `::`, the globals (inside a function, `::name` reaches a global
  that a local of the same name hides);
* after `name::`, the members of that namespace: the functions of a
  library (`math::`, `str::`, ...), the functions of the host application
  declared as `app::name` in `wrench.json`, the members of an enum;
* after `x.`, the members of a struct, and the built-in members such as
  `._count` and `._exists`. When `x` is only ever given `new S` (also in a
  `var` declaration), only the members of `S` are offered; otherwise the
  members of every struct;
* the keywords and the library namespaces, and the functions of the host
  application.

Libraries that `wrench.json` does not load are left out. Nothing is
offered inside comments and strings, after a single `:` (a `case` label,
a ternary or a hash table key) or after a number.

## Signature help

Inside the parentheses of a call, the signature of the function shows with
the current parameter highlighted, for your own functions, those of
included files, library functions and the host functions from
`wrench.json`. It appears when you type `(` or `,`.

## Navigation

| Feature | What it does |
|---|---|
| Go to Definition (F12) | jumps to the declaration of a variable, function, struct, enum or member; into an included file for what it declares; into `wrench.json` for a host function |
| Find All References (Shift+F12) | lists the uses of a symbol in the file |
| highlights | putting the cursor on a name highlights its uses in the file, the declaration marked as such |
| Rename Symbol (F2) | renames a variable, function, struct, parameter or member everywhere in the file; refuses reserved words and names that are not identifiers |
| Outline and breadcrumbs | functions with their locals, structs and enums with their members, globals |
| folding | every block in braces, multi-line comments, and `// region` markers |

References, highlights and rename follow scopes: a global and a local of
the same name are different symbols, and so are members of different
structs, `p.size` and `q.size`, when `p` and `q` have evident types (only
ever given `new A` or `new B`). They also follow blocks (see
[Block scope](../language/variables.md#block-scope)): the `i` of two
`for ( var i ... )` loops in a row are two symbols, and completion offers
only names in view at the cursor. With `wrench.nonStrict` there is no
block scope, as for the compiler.

> [!NOTE]
> Find All References and Rename work within one file. Uses of a global
> or function in the files that include it, or are included by it, are not
> found or renamed.

## Calls checked against wrench.json

Scripts for an application call the natives that application registers.
When the folder of the script, or one above it, has a `wrench.json`, the
server knows which functions exist and checks every call:

| Warning | When |
|---|---|
| `'f' is neither a function of this script nor declared in wrench.json` | a call to a name nothing provides |
| `'math::sin' needs the 'math' library, which wrench.json does not list` | a library the application does not load |
| `'str::nope' is not a function of the 'string' library` | a misspelled library function |
| `'println' comes from the wrench tools; declare it in wrench.json if the host application provides it` | `print` and `println` exist in the wrench tools, not necessarily in your application |
| `openDoc takes 1 to 2 arguments, this call passes 3` | an argument count that does not fit the declared parameters |

They are warnings unless `"undeclaredCalls"` in `wrench.json` says
`"error"` (or `"ignore"` for none, except the argument counts). A
`wrench.json` that is not valid is reported on the first line of every
script under it, and ignored. Changes to `wrench.json` are picked up at
once: every open script is checked again. The file is described in
[Describing your host application: wrench.json](wrench-json.md).

Without a `wrench.json` no calls are checked: the server cannot know what
the host provides.

## Commands

The commands are in the Command Palette (Ctrl+Shift+P), under "Wrench".

| Command | What it does |
|---|---|
| **Wrench: Compile to Bytecode** | saves the file and runs `wrench c` on it, writing `<name>.bin` next to it; with `wrench.compileDebugInfo` on (the default) it adds `-debugsource`, with `wrench.nonStrict` `-nostrict`. The command line and the compiler's output go to the "Wrench" output channel |
| **Wrench: Show Disassembly** | saves the file and opens `wrench p -debug` of it beside the editor (with `-nostrict` when `wrench.nonStrict` is on), as a read-only document `<name>.w.disasm`; it follows the file each time you save |
| **Wrench: Describe the Host Application (wrench.json)** | opens the `wrench.json` next to the active script, or in the first workspace folder, creating it from a template if there is none |
| **Wrench: Restart Language Server** | stops and starts `wrench lsp`, for example after replacing the executable |

The first two are offered only while a wrench file is active. How to read
a disassembly is in
[Compiling, disassembling and assembling](../internals/tools.md#reading-a-listing).

The template **Describe the Host Application** writes:

```json
{
	"libraries": [
		"math",
		"string"
	],
	"functions": [
		{
			"name": "myFunction",
			"params": [
				"value",
				"option?"
			],
			"doc": "Registered by the application with wr_registerFunction",
			"returns": 0
		},
		{
			"name": "app::log",
			"params": [
				"values..."
			],
			"doc": "Registered with wr_registerLibraryFunction"
		}
	],
	"undeclaredCalls": "warning"
}
```

## Settings

| Setting | Default | What it does |
|---|---|---|
| `wrench.executablePath` | empty | the `wrench` executable to use; empty means the copy bundled in the extension, then the `PATH` (see [Where the extension finds wrench](quick-try.md#where-the-extension-finds-wrench)). `${workspaceFolder}` is replaced by the first workspace folder. A change restarts the language server |
| `wrench.nonStrict` | `false` | check files in non-strict mode, for older scripts (`-nostrict`, see [Non-strict mode](../language/variables.md#non-strict-mode)); also used by **Compile to Bytecode** and **Show Disassembly** |
| `wrench.compileDebugInfo` | `true` | **Compile to Bytecode** embeds debug information and the source (`-debugsource`), so the file can be debugged on a target |
| `wrench.trace.server` | `off` | trace the messages between VS Code and the language server in the output channel "Wrench Language Server": `off`, `messages` or `verbose` |

The launch configurations have settings of their own, see
[Launch configurations](launch.md).
