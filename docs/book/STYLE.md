# Writing the wrench book

The book is built with [mdBook](https://rust-lang.github.io/mdBook/) 0.5:

```
mdbook build docs/book      # writes docs/book/book, open index.html from disk
mdbook serve docs/book      # live preview on http://localhost:3000
```

The outline is `src/SUMMARY.md`; every page listed there is a file under
`src/`. This file is not part of the book.

## Readers

Two kinds of readers, often the same person:

* people who write wrench scripts (the language, the library, VS Code)
* people who put wrench into an application or firmware (embedding,
  debugging, small targets)

Write for someone who knows C or JavaScript but has never seen wrench.

## Voice

* Second person, present tense, active voice: "call `wr_run()`", "the
  compiler emits".
* Plain and exact. No marketing words, no "simply", "just", "easy".
* Short paragraphs, one idea each. Lists for steps and options, tables for
  reference material (functions, flags, options).
* Every page starts with one or two sentences that say what it covers and
  who needs it.
* Explain why as well as how where the reason is not obvious (a limit, a
  rule, a trade-off).
* Link instead of repeating: a fact lives on one page, others link to it.

## Accuracy

Everything in the book must be true of the code in this repository.

* Read the source before describing behaviour: `src/wrench.h` for the API,
  `src/lib/*.cpp` for the library, `src/utils/wrench_cli.cpp` for the
  command line, `tests/*.w` for language behaviour.
* Every complete wrench program in the book runs with
  `wrench r file.w`; show its real output. Fragments must compile when put
  into a program.
* C++ examples use the real signatures from `wrench.h` and compile against
  the single file build.
* When the code and an older text (the website in `www/`, `README`)
  disagree, the code wins.

`tools/bookcheck` checks the wrench examples, and CI runs it on every
push:

```
dotnet run --project tools/bookcheck -- win32/Release/wrench.exe <work folder> docs/book/src
```

A `wrench` block followed by "Output:" and a `text` block is run and its
output compared; any other `wrench` block must compile. An HTML comment on
the line before a block changes that:

| Marker | Meaning |
|---|---|
| `<!-- check:skip -->` | not checked: a fragment, or a script for a host with its own natives |
| `<!-- check:error -->` | must fail to compile |
| `<!-- check:runtime -->` | run, output not compared |
| `<!-- check:file=NAME -->` | written to NAME, for an include example to find |
| `<!-- check:as=NAME -->` | checked as NAME, next to the files written before |
| `<!-- check:bin=NAME -->` | compiled into the bytecode file NAME, for an import example |

## Markdown conventions

* Code blocks name their language: `wrench`, `cpp`, `json`, `text` for
  program output and consoles, `bash` for commands.
* C++ and wrench examples follow the project style: tabs for indentation,
  braces on every block, braces on their own line.
* Output under an example goes in its own `text` block, introduced by a
  line "Output:".
* Callouts use GitHub syntax:

  ```
  > [!NOTE]
  > Something worth knowing.
  ```

  `NOTE` for background, `TIP` for a better way, `IMPORTANT` for rules that
  must be followed, `WARNING` for things that break or lose data.
* Links between pages are relative (`../embedding/natives.md`). Links to
  files of the repository use
  `https://github.com/darksimpson/wrench/blob/main/<path>`.
* Plain ASCII text: no arrows, emoji, typographic quotes or dashes, no
  pictures drawn with characters. Use tables, lists, or an SVG image in
  `src/images/` (class `diagram`) for diagrams.
* Headings in sentence case. One `#` heading per page, the page title.
