# Breakpoints, stepping and watchpoints

This page describes exactly how the debugger's tools behave, whichever
front end you use: where breakpoints bind, how conditions and logpoints
are evaluated, what each step does (also across calls the firmware makes),
pausing, runtime errors, watchpoints, the expression evaluator and setting
values. [Debugging in VS Code](vscode.md) and
[The console debugger](console.md) show how to use them.

## Breakpoints

### Lines with code

The compiler puts a line marker in front of every statement of a program
compiled with debug information; the target stops at markers, nothing
else. A breakpoint therefore sits on a line that has a marker. One asked
for on a line without code (a comment, a blank line, a lone brace, a
declaration that compiles to nothing) moves to the next line of the same
file that has code. In `wrench dbg`, with the program of the
[console session](console.md#a-session) (line 23 is the `{` of
`lineTotal`):

```text
(wdb:idle) b 23
line 23 has no code, using line 24
breakpoint at line 24
(wdb:idle) b 23
breakpoint removed from line 24
(wdb:idle) b 37
no code at or after line 37
```

VS Code moves the breakpoint in the editor. When nothing after the line
has code, the breakpoint is not set ("no code at or after this line").

A few lines are worth knowing:

* A `for` loop has a marker on its condition, so a breakpoint on the `for`
  line stops at every pass, and stepping visits the loop header. An
  increment on a line of its own has its own marker.
* An endless loop such as `while ( 1 ) {}` or `for ( ;; ) {}` gets a
  marker for its line on every pass, so it can be paused and stopped at.
* A line with several statements is one line for the debugger; a call in
  the middle of a line (`println( f() )`) stops there once.
* Included files are compiled in place of their `include`, so the first
  line a program runs (where "stop on entry" halts) can be in an included
  file ([Several files](programs.md#programs-made-of-several-files)).

### How breakpoints bind

The target knows programs and lines, not files. A program made of several
files (`include`) is compiled as one text, and its lines are counted
through all of it; the debug symbols carry a table that maps those lines
back to files. The debugger does the mapping: you set a breakpoint in
`stats.w`, line 8, and the target gets program 0, line 21 (say). The
mapping comes from the program as it was built, so:

* Breakpoints match the build the target runs. Edit a file after the
  build, and lines shift: a launch rebuilds and uploads, so it matches
  again; an attach warns that the target runs a different build.
* A file used by several programs (an included library in two contexts)
  gets the breakpoint in each of them.
* A file that belongs to no program the target debugs cannot hold a
  breakpoint until such a program appears.

The target holds `WRENCH_DEBUG_MAX_BREAKPOINTS` (32 by default) over all
programs. Past that, VS Code marks the extra ones "the target has no room
for more breakpoints", and the target answers "too many breakpoints for
the target".

### When programs change

Breakpoints follow the programs:

* An upload replaces program 0. The target drops program 0's breakpoints
  the moment the upload completes, and the debugger sets them again for
  the new program right after, before it runs.
* When a context the firmware attached is destroyed, its breakpoints go
  with it.
* A module (imported with `sys::importCompile()` or
  `sys::importByteCode()`) goes away when its importer is reset or
  destroyed. Its breakpoints are kept, parked by the module's bytecode
  hash, and are back the moment the same module is imported again, from
  its first line. Parked breakpoints make room when new ones need it.
* A module imported for the first time cannot have breakpoints set before
  its global code runs: its source is only known once it is there. Break
  in the importer and step in, or set them and run again.

## Conditions, hit counts and logpoints

These are features of VS Code; the target only knows plain breakpoints.
When the program reaches a breakpoint line, the target halts and the
adapter decides, in this order:

1. **Condition**: the expression is evaluated in the stopped frame. False:
   the program goes on at once. An expression that cannot be evaluated
   stops, and the Debug Console says why: "Breakpoint condition 'count >'
   failed: unexpected end of expression".
2. **Hit count**: the hit is counted (only hits that passed the
   condition count). If the hit count does not match, the program goes on.
   Hit counts survive editing other breakpoints; changing the condition or
   the hit count of a breakpoint starts its count over.
3. **Logpoint**: the message is printed, with each `{expression}`
   replaced by its value, and the program goes on.
4. Otherwise the program stops.

| Hit count | Stops on hit |
|---|---|
| `5` | 5 and every one after it |
| `>= 5`, `> 4` | 5 and later |
| `== 5` | 5 only |
| `<= 2`, `< 3` | 1 and 2 |
| `% 3` | 3, 6, 9 ... |

Several breakpoints on the same line (in different files of one program
that map there, or a logpoint and a breakpoint) are all evaluated; the
program stops if any of them says so.

A breakpoint that does not stop does not disturb a step: when a step over
or step out runs into such a breakpoint (in a called function, say), the
step carries on as if the breakpoint were not there. A step that ends on a
breakpoint line counts the hit and prints a logpoint, but stays a step.

Every hit of a conditional breakpoint or logpoint halts the target for a
round trip. In a busy loop over a slow link this slows the program down
considerably; a plain breakpoint costs nothing until it is hit.

## Stepping

Steps work by line and by call depth:

| Step | Ends at the next line marker that is |
|---|---|
| Step into (`s`, F11) | anywhere: the next line that runs, also inside a called function |
| Step over (`n`, F10) | in the same function or a caller of it |
| Step out (`o`, Shift+F11) | in a caller of the current function |

Depth comes from the VM's frames, so steps work the same in code the
firmware entered through `wr_callFunction()` as in calls inside the script.
Some consequences:

* A step that returns from a function lands on the next line marker of the
  caller. If the call was the last thing on its line, that is the next
  line, not the rest of the calling line: stepping over `return sum;` in
  the [console session](console.md#a-session) ends on line 32, the `for`
  condition, not on line 34 that made the call.
* A breakpoint or watchpoint inside a function you step over stops there,
  with its own reason.
* Stepping into a function of an imported module enters the module; the
  call stack shows both programs.

### A step only ends on its own stack

The target is single threaded, but it can run several programs: program 0
and contexts the firmware attached run on stacks of their own. A step
belongs to the stack it started on (a module shares the stack of its
importer). While you step through program 0, the firmware calling into
another attached context does not end the step there; that code runs past
it, and only stops at its own breakpoints.

### Steps across calls from the firmware

Firmware usually calls a script's functions one at a time: `loop()`,
`onEvent()`. A step still pending when such a call returns to the firmware
is not cancelled; it carries into the next call:

* Step over or step into on the last line of `onEvent()`: the call
  returns, the firmware goes on, and the step ends on the first line of the
  next call into the script on that stack (the next `onEvent()`).
* Step out of `onEvent()`: there is no script caller to land in, and a
  step out never ends in a later call at the same depth. It stays pending
  until a breakpoint, a pause or a run of the global code ends it; in
  `wrench dbg` the prompt does not come back until then (Ctrl+C pauses).

This session on a firmware that calls `onEvent()` every 300 ms shows the
first case; line 15 is the `return` of `onEvent()`:

```text
(wdb) n

[step] line 15
in onEvent, line 15
    13  		println( "alarm ", value );
    14  	}
 >  15  	return last;
    16  }
    17  
(wdb) n

[step] line 8
in onEvent, line 8
     6  function onEvent( type, value )
     7  {
 >   8  	count += 1;
*    9  	last = value * 2;
    10  	log[count % 4] = last;
```

## Pause

Pause stops the program at the next line marker it executes. While script
code runs, the VM polls the link every `WRENCH_DEBUG_POLL_MS` (20 ms), so a
pause lands within that time plus the time of the line that is running.
Two things delay or prevent it:

* A native function that blocks: nothing polls inside it, unless it calls
  `poll()`. The pause then lands on the script's next line after the
  native returns.
* Code compiled without debug information has no line markers; it cannot
  be paused at all.

Pausing a target that is halted already answers with its current stop
again. Pausing a target where no script code runs (between calls of host
driven firmware) stops at the next line the firmware runs.
[Blocking code, yield and polling](blocking.md) goes through every case.

## Runtime errors

When a runtime error happens in a debugged program, the VM tells the
server before it unwinds anything. The target halts on the failing line
with reason "error": the call stack, the locals and the globals are those
of the moment of the error, so you can see why `x[i]` failed or which
function was missing. Continuing lets the VM fail as it would without a
debugger.

This needs a debugger attached; without one the error goes straight to the
firmware. In VS Code the **Runtime errors** entry in the Breakpoints view
(on by default) decides whether the program stops; `wrench dbg` always
stops:

```text
*** runtime error WR_ERR_function_not_found at line 2
*** 'led' is not provided by the target
in <global>, line 2
     1  var on = 1;
 >   2  led( on );
     3  println( "led is ", on );
```

For a missing function the VM records the hash of the name that was
called; the debugger looks the name up in the program's source and in
`wrench.json` and names it. Host code can do the same with
`wr_getMissingFunctionHash()`.

## Watchpoints

A watchpoint stops the program when a value changes. It names the value by
where it lives, a path:

| Path | Watches |
|---|---|
| `x` | a local of the selected frame, else a global |
| `::g` | a global |
| `count@stats.w` | a private global of an included file |
| `a[2]` | an array element |
| `p.member` | a struct member |
| `h["key"]`, `h[5]` | a hash table entry |
| `s[0]` | a character of a string |
| `a[1].k` | any combination, up to `WRENCH_DEBUG_MAX_WATCH_STEPS` (4) steps |

The target follows the path again from its root at every line marker and
compares the value with what it was. When it differs, the program stops on
the line after the one that changed it, with reason "watch". What counts
as a change:

* a number: its value (writing the value it already has is no change);
* a string: a different string, length or contents;
* an array, hash table or struct: a different container, or a different
  number of elements; a changed element is caught by watching that
  element;
* the path itself: an element appearing or going away (a key removed, an
  array cut short).

Values the debugger writes while the program is stopped do not count.

A watch on a local belongs to the call it was set in. It goes away when
that call returns: VS Code greys it out ("its function returned"),
`wrench dbg` says `[watch 1: n went out of scope]`. A watch on a global
of a program goes away with that program.

Watchpoints are set while the program is stopped, because locals and
paths are resolved in the selected frame. In VS Code use **Break on Value
Change** on a variable, element or member; it takes a condition and a hit
count like a breakpoint, evaluated the same way. In `wrench dbg` use
`watch <path>` and `unwatch [n]`.

The target holds `WRENCH_DEBUG_MAX_WATCHES` (4 by default); one more is
refused with "the target has no room for more watchpoints". Each watch
costs a little time at every line marker while it is set, nothing when
there are none.

## Evaluating expressions

Watches, hovers, conditions, logpoints, the debug console, `p` and `set`
share one evaluator, which runs in the debugger on the PC and reads values
from the target as it needs them. It understands:

| Kind | Examples |
|---|---|
| integers, decimal and hexadecimal | `42`, `-7`, `0x1F` |
| floats | `2.5`, `1e3` |
| strings, in double or single quotes, with `\n` `\r` `\t` `\0` escapes | `"bolt"`, `'x'` |
| variables | `total`, `::total`, `count@stats.w`, `::count@stats.w` |
| elements and members | `a[2]`, `a[i + 1]`, `p.member`, `h["key"]`, `h.key`, `s[0]` |
| `._count`, and `._hash` of a number or a string | `orders._count`, `name._hash`; read only, they cannot be watched or assigned |
| arithmetic | `+ - * / %`, `+` also joins strings: `name + "s"` |
| comparisons | `== != < <= > >=`; numbers with numbers, strings with strings; containers compare by identity with `==` and `!=` |
| logic | `&& \|\| !`, the result is 1 or 0 |
| grouping | `( )` |

Integer division and `%` follow integer rules (`7 / 2` is 3, `7 / 2.0` is
3.5). There are no function calls and no `++` or `+=`: an expression
never runs script code or changes anything, except an assignment in the
debug console.

Names are looked up in the selected frame: a local of its function first,
then a global of its program. A private global (one declared `private` in
an included file) is found by its plain name only in a frame of its own
file; from anywhere else write `::count@stats.w`, the name the variables
view shows. See [Several files, programs and modules](programs.md).

The errors say what went wrong:

```text
missing: no variable named 'missing'
orders[9]: index 9 out of range (size 3)
o.nosuch: no member nosuch
(1 + 2: expected ')'
```

## Setting values

A variable, an element or a member can be changed while the program is
stopped: `set x = 5` in `wrench dbg`, `x = 5` in the VS Code debug console,
or Set Value in the variables view. The right side is an expression,
evaluated first: `set total = total * 2`.

The new value must be a number or a string ("only numbers and strings can
be assigned"). It replaces whatever the variable held, a container
included, but no container can be assigned. A string is created in the
context of the variable it goes to.

Values handed out while the program is stopped stay valid until it
resumes. An element of an array, a hash table entry or a struct member is
changed in place; the change is no change for watchpoints.
