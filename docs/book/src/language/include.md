# Several files: include and private

A program can be split into several source files with `include`, and a
file can keep globals and functions to itself with `private`. This page
explains both, where included files are found, and how errors and the
debugger show them. Loading code while the program runs is a different
mechanism, described in [Modules: importing code at run time](import.md).

## include

`include "file.w"` at the top level compiles that file in place: its
functions, structs, enums and globals become part of the program, exactly
as if its text stood where the `include` is.

<!-- check:file=lib/counter.w -->
```wrench
// lib/counter.w
private var count = 0;

private function step()
{
	return 1;
}

function next()
{
	count += step();
	return count;
}
```

<!-- check:file=lib/shapes.w -->
```wrench
// lib/shapes.w
include "counter.w"; // next to this file: lib/counter.w

function newId()
{
	return "shape" + next();
}
```

<!-- check:as=main.w -->
```wrench
// main.w
include "lib/counter.w";
include "lib/shapes.w"; // includes counter.w again: not compiled twice

var count = 100; // main.w's own count, not the one of counter.w

println( next(), " ", newId(), " ", count );
```

Output:

```text
1 shape2 100
```

The rules:

* `include` is only allowed at the top level, outside any braces. Inside
  a function or a block it is `WR_ERR_include_not_at_top_level`.
* The `;` after the file name is optional.
* Each file is compiled once per program, however often it is included,
  so files may include each other (and the main file may be included
  back) without harm. The first `include` of a file decides where its text
  goes.
* An `include` inside a comment or a string is not an include.
* Includes nest at most 16 deep (`WRENCH_MAX_INCLUDE_DEPTH`,
  `WR_ERR_include_too_deep`).
* Inclusion happens before compiling, so the usual order rules hold for
  the combined text: a function sees the globals and enum constants
  declared above it, including those of files included above it (see
  [Inside a function](variables.md#inside-a-function)). Include the files
  that declare globals first.

Everything that is not `private` is shared by all files of the program:
functions, structs, named and anonymous enum constants and globals. Two
functions or structs of one name in different files are
`WR_ERR_function_redefined`. Two top level `var x` in different files are
`WR_ERR_variable_redeclared` (see
[Declaring a name again](variables.md#declaring-a-name-again)), which is
the main reason to make globals of a library file `private`. A file that
uses a global of another file writes it without `var`.

## Where included files are found

A relative name is looked for:

1. next to the file that contains the `include`;
2. in the folders given with `-I` to `wrench c`, `ch`, `p` and `r` (in
   the order given):

   ```bash
   wrench r -I lib -I ../common main.w
   ```

3. in the `"include"` folders of the project's `wrench.json`, which the
   command line tool, the debugger and the VS Code extension read (see
   [Describing your host application: wrench.json](../vscode/wrench-json.md)).

An absolute name is used as it is. The same file reached under two
spellings (`lib/../lib/a.w`) counts as one file.

### Includes in your own application

The compiler opens no files itself, since it may run on a device without
a file system. A host that compiles scripts with includes passes a
callback to `wr_compileWithOptions()` that finds a file and returns its
text; see [Compiling scripts](../embedding/compiling.md). Without a
callback, as with `wr_compile()` and `sys::importCompile()`, an `include`
is the compile error `WR_ERR_include_not_supported`. Precompiled bytecode
needs nothing: the included files are already part of it.

## Errors in included files

A compile error names the file it is in and the line within that file:

<!-- check:file=lib/broken.w -->
```wrench
function ok()
{
	return 1;
}

var x = ;
```

<!-- check:as=uses_broken.w -->
```wrench
include "lib/broken.w";
println( ok() );
```

Output:

```text
lib/broken.w:6
err: WR_ERR_bad_expression[9]
6     var x = ;
              ^
compile error [WR_ERR_bad_expression]
```

A file that cannot be found is `WR_ERR_include_not_found`, reported on
the line of the `include`. The VS Code extension shows an error of an
included file on the `include` that brings it in.

## private

`private` in front of a top level `var` or `function` makes the name
belong to its file, like `static` in C:

* code in the same file finds its own private name first, before a shared
  name of the same spelling;
* code in other files does not see it, and other files may declare their
  own (private or shared) of the same name, as `count` in the example
  above;
* a private global is not found by `wr_getGlobalRef()`, and a private
  function cannot be called by its name from the host (`wr_callFunction()`)
  or from another file. A call from another file looks for a shared
  function of that name, and fails with `WR_ERR_function_not_found` at run
  time if there is none.

`private` is only special at the start of a top level statement, and only
in front of `var` and `function`. `private struct`, `private x = 1;`
(without `var`) and `private` inside a block are compile errors; inside a
function, `private` is an ordinary name (`var private = 3;` works).

> [!NOTE]
> A private function used as a value (`var f = step;`) gives the hash of
> the shared name, not of the private function, so a host cannot call it
> back through that hash.

## Debugging programs of several files

Programs made of several files are debugged as one program: breakpoints
and stepping work in every file, and call stacks show each frame in its
own file. A program compiled with `-debug` names each included file
relative to the main file's folder (`lib/stats.w`; an absolute name only
for a file on another drive), and the debugger looks for it next to the
program and in the project's include folders. Compile errors still show
the path the file was read from. `-debugsource` embeds every file's text
in the bytecode. In the
variables view a private global shows with its file, as `count@counter.w`;
an expression typed while stopped in that file means its own `count`, and
`::count@counter.w` names it from anywhere. See
[Several files, programs and modules](../debugging/programs.md).
