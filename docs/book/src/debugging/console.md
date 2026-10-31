# The console debugger

`wrench dbg` debugs a script from a terminal: in a VM of its own, on a
device, or on a PC target. It does everything the VS Code debugger does
except conditional breakpoints, hit counts and logpoints, and it is the
quickest way to check a target over a serial port or to script a session.

## Starting it

```text
wrench dbg [-target T] [-attach] [-project P] [file]
```

| Option | Meaning |
|---|---|
| `file` | a `.w` source file, or a `.bin` compiled with `-debugsource`; optional with `-attach` |
| `-target T` | `local` (the default): a VM inside the debugger; `tcp:<host>:<port>`; `serial:<port>[:<baud>]` (115200 by default) |
| `-attach` | debug what the target already runs: nothing is uploaded or restarted |
| `-project P` | the `wrench.json` that describes the host application; by default the nearest one in the file's folder or above, `none` ignores it |

The options of [`wrench dbgserver`](targets.md#the-pc-as-a-target)
(`-context`, `-linktimeout`, `-noupload`, `-hold`, `-v`) are refused here,
"unknown option -hold for dbg", followed by the usage text.

Typical uses:

```bash
wrench dbg file.w                                         # local VM
wrench dbg -target tcp:192.168.1.50:5050 file.w           # upload to a board and run it
wrench dbg -target serial:COM5:115200 -attach file.w      # what the board runs, local source
wrench dbg -target tcp:192.168.1.50:5050 -attach          # what the board runs, its embedded source
wrench dbg file.bin                                       # a compiled program, its embedded source
```

A `.w` file is compiled with debug information and its source. A `.bin`
is used as it is: a launch uploads it unchanged and the source shown is the
one embedded in it. A `.bin` without embedded source gives stacks and
variables but no listing:

```text
file.bin carries no source (compile it with -debugsource), only stacks and variables are available
```

Without `-attach` the debugger uploads the program right after connecting
and waits for `r` to run it. That needs a launchable target that accepts
uploads; others are refused with a hint:

```text
the target runs its own program and takes no launches (its firmware did not call setLaunchable(true)), use -attach
the target does not accept uploaded programs (its firmware called setAllowUpload(false)), use -attach
```

With `-attach` and a file, the debugger compiles the file the way the
target's program was compiled and compares them:

```text
warning: the target runs a different build of blink.w than the local one
```

With `-attach` and no file, it reads the program, its symbols and its
source from the target (a TCP or serial one; a local VM has nothing to
attach to):

```text
read the program from the target (1037 bytes, source embedded)
```

## The prompt

The prompt says whether the program is stopped:

| Prompt | State |
|---|---|
| `(wdb)` | the program is halted: stacks, variables and steps work |
| `(wdb:idle)` | nothing is halted: the program has not run yet, has ended, or runs on the target |

While a command like `c` or `n` runs, the debugger waits for the program to
stop, printing its output as it comes. Ctrl+C pauses the program; a second
Ctrl+C stops waiting and gives the prompt back, for a target that does not
stop (code without debug information, or no answer).

At either prompt the debugger keeps listening to the target: a breakpoint
hit by code the firmware runs on its own, output and other events are
shown as they arrive, followed by a new prompt.

## Commands

| Command | Does |
|---|---|
| `r`, `run` | launch program 0 from the start; when it is running or stopped, terminate it first |
| `c`, `continue` | continue until the next stop; while nothing is stopped and the target runs other programs, wait for one of them to stop (otherwise it says "not stopped, 'r' runs the program") |
| `n`, `next` | step over |
| `s`, `step` | step into |
| `o`, `finish` | step out of the current function |
| `b <line>`, `break <line>` | toggle a breakpoint; `b` alone lists them; `file.w:line` names a file of any program |
| `watch <expr>` | stop when a variable, element or member changes: `x`, `a[2]`, `s.member`; `watch` alone lists them |
| `unwatch [n]` | remove watch `n`, or all of them |
| `bt`, `where`, `w` | show the call stack |
| `f <n>`, `frame <n>` | select stack frame `n`; `p`, `set`, `locals`, `l` and `b` then work in its program and file |
| `l [line]`, `list [line]` | list the source around the current line, or around `line` (`file.w:line` for another file) |
| `programs` | the programs the target debugs: attached contexts and imported modules |
| `p <expr>`, `print <expr>` | print an expression: `x`, `a[2]`, `s.member`, `h["key"]`, `x + 1 > 3`, `a._count` |
| `set <var> = <value>` | change a variable, element or member to a number or a string |
| `locals` | the locals of the selected frame |
| `globals`, `g` | the globals of the selected frame's program |
| `d <expr>`, `display <expr>` | print an expression at every stop; the same command again removes it |
| `x` | toggle hexadecimal display of integers in `p` |
| `kill` | terminate the program |
| `q`, `quit` | leave: detach from a remote target (the program runs on), terminate a local one; the end of the input does the same |
| `h`, `help`, `?` | the command list |

A few details:

* `b 23` toggles. A line without code takes the next line that has some
  ("line 23 has no code, using line 24"), and `b 23` again removes it from
  there. The target holds up to `WRENCH_DEBUG_MAX_BREAKPOINTS` (32 by
  default).
* `b` and `l` with a bare line number use the file of the selected frame,
  or the main file while nothing is stopped. Stopped in an included file,
  `b 23` means line 23 of that file; select a frame of the main file with
  `f` first, or name the file by the end of its path: `b main.w:23`,
  `l lib/stats.w:8`.
* `r` while nothing ran yet launches. While the program runs or is
  stopped, it takes its breakpoints off the target, terminates the run,
  waits up to two seconds for its end, and launches again with fresh
  globals; the breakpoints are set again once the new run halts on its
  first line. Taking them off first means a function the firmware calls
  cannot hit one in between and keep the target too busy to launch: a
  target that is still busy is asked again for up to three seconds. A
  launch is a request the firmware carries out; see
  [Launch and attach](overview.md#launch-and-attach).
* `kill` ends the run. Stopped in a function the firmware called, it
  returns at once with "[terminated the call of the firmware]": the call
  ends, and the firmware may call the script again.
* `watch` needs a stopped program: names are looked up in the selected
  frame. A watch on a local is removed when its function returns:
  `[watch 2: n went out of scope]`.
* `set` evaluates the right side, so `set total = total * 2` works.
* While the debugger waits at the `(wdb)` prompt for a remote target, it
  pings the target every second, so a target with a link timeout (see
  [setLinkTimeout](firmware.md#when-the-debugger-goes-away)) does not let
  go of it while you read variables.

## A session

The program below keeps a list of orders and totals them:

```wrench
// a small order book: add orders, total them up
struct Order
{
	var item;
	var qty;
	var price;
};

var orders[] = { };
var total = 0;

function add( item, qty, price )
{
	var o = new Order;
	o.item = item;
	o.qty = qty;
	o.price = price;
	orders[orders._count] = o;
	return o;
}

function lineTotal( o )
{
	var sum = o.qty * o.price;
	return sum;
}

add( "bolt", 10, 3 );
add( "nut", 25, 1 );
add( "gear", 2, 40 );

for ( var i = 0; i < orders._count; ++i )
{
	total += lineTotal( orders[i] );
}
println( "total ", total );
```

Output:

```text
total 135
```

A session with it, `wrench dbg orders.w`; what you type follows the
prompts:

```text
connected to local, wrench 8.0.0, max packet 1024 bytes
orders.w: 36 lines, 3 functions. 'h' for help.
(wdb:idle) b 24
breakpoint at line 24
(wdb:idle) r

[breakpoint] line 24
in lineTotal, line 24
    22  function lineTotal( o )
    23  {
*>  24  	var sum = o.qty * o.price;
    25  	return sum;
    26  }
(wdb) bt
>#0  lineTotal  line 24
 #1  <global>  line 34
(wdb) locals
locals:
  o = {item: bolt, qty: 10, price: 3}
  sum = 0
(wdb) p o.item
o.item = "bolt"
(wdb) n

[step] line 25
in lineTotal, line 25
    23  {
*   24  	var sum = o.qty * o.price;
 >  25  	return sum;
    26  }
    27  
(wdb) p sum
sum = 30
(wdb) set sum = 100
sum = 100
(wdb) f 1
in <global>, line 34
    32  for ( var i = 0; i < orders._count; ++i )
    33  {
 >  34  	total += lineTotal( orders[i] );
    35  }
    36  println( "total ", total );
(wdb) p i
i = 0
(wdb) d total
total = 0
(wdb) b 24
breakpoint removed from line 24
(wdb) watch total
watch 1: total = 0
(wdb) c

[watch 1: total changed] line 32
in <global>, line 32
    30  add( "gear", 2, 40 );
    31  
 >  32  for ( var i = 0; i < orders._count; ++i )
    33  {
    34  	total += lineTotal( orders[i] );
total = 100
total = 100
(wdb) watch
  1: total
(wdb) c

[watch 1: total changed] line 32
in <global>, line 32
    30  add( "gear", 2, 40 );
    31  
 >  32  for ( var i = 0; i < orders._count; ++i )
    33  {
    34  	total += lineTotal( orders[i] );
total = 125
total = 125
(wdb) unwatch
all watches removed
(wdb) c
total 205

[ended, returned 0]
(wdb:idle) q
```

What happened:

* In the listing `*` marks a breakpoint and `>` the current line.
* `set sum = 100` changed the local before `return sum`, so the first
  order counts 100 and the program prints 205 instead of 135.
* `f 1` selected the caller's frame; `p i` then read its loop variable.
* The watch stopped on line 32, the line after the one that changed
  `total`. At a watch stop the debugger prints the watched expression, and
  `d total` printed it once more.

## Debugging a remote target

The same commands work on a target. Here
[`wrench dbgserver`](targets.md#the-pc-as-a-target) acts as a board that
runs `sensor.w` itself, and `wrench dbg` uploads `main.w` to it (both from
`examples/debug_demo`; the server was started with
`wrench dbgserver -target tcp:5171 -context sensor.w -v main.w`):

```text
> wrench dbg -target tcp:127.0.0.1:5171 main.w
host application described by D:\Temp\wrench\examples\debug_demo\wrench.json
connected to tcp 127.0.0.1:5171, wrench 8.0.0, max packet 1024 bytes
main.w: 24 lines, 4 functions. 'h' for help.
includes 2 more files, 'b file.w:line' and 'l file.w:line' reach them
the target debugs 2 programs ('programs' lists them):
>0  main.w  (3 files)
 1  sensor.w
(wdb:idle) b sensor.w:11
breakpoint at line 11 in sensor.w
(wdb:idle) r

[program 2: import 2 imported by program 0]
total = 68 cm2
average = 22

[ended, returned 0]
(wdb:idle)
[breakpoint] line 11 in sensor.w
in tick, line 11 in sensor.w
     9  {
    10  	readings += 1;
*>  11  	var value = (readings * 7) % 23;
    12  	history[readings % 4] = value;
    13  	return value;
(wdb) globals
globals of sensor.w:
  readings = 11
  history = [10, 17, 1, 3]
(wdb) programs
 0  main.w  (3 files)
>1  sensor.w
 2  import 2  (imported by program 0)
(wdb) q
```

`main.w` ran to its end; the target went on calling `tick()` of
`sensor.w`, and its breakpoint hit while the debugger waited at the idle
prompt, without a command. (To wait for such a stop inside a command, `c`
at the idle prompt waits until a program of the target stops.) Locations
in another program than program 0 read "line 11 in sensor.w". `q`
detached: the target runs on, `sensor.w` included.

## Attaching without the source

A target that runs a program compiled with `-debugsource` can be debugged
without any file. Here `wrench dbgserver -target tcp:5178 -hold orders.bin`
(`orders.bin` from `wrench c -debugsource orders.w orders.bin`) holds the
program on its first line until a debugger attaches:

```text
> wrench dbg -target tcp:127.0.0.1:5178 -attach
connected to tcp 127.0.0.1:5178, wrench 8.0.0, max packet 1024 bytes
read the program from the target (1037 bytes, source embedded)
target program: 36 lines, 3 functions. 'h' for help.

[entry] line 9
in <global>, line 9
     7  };
     8  
 >   9  var orders[] = { };
    10  var total = 0;
    11  
(wdb) b 25
breakpoint at line 25
(wdb) c

[breakpoint] line 25
in lineTotal, line 25
    23  {
    24  	var sum = o.qty * o.price;
*>  25  	return sum;
    26  }
    27  
(wdb) p o
o = {item: "bolt", qty: 10, price: 3}
(wdb) p o.item + "s"
o.item + "s" = "bolts"
(wdb) x
hex on
(wdb) p sum
sum = 0x0000001E
(wdb) p o._count
o._count = 0x00000003
(wdb) watch o._count
cannot watch o._count: ._count is computed, watch the value it is taken of
(wdb) q
```

`._count` and `._hash` read as they do in a script, but they are computed,
not stored: they cannot be watched or assigned.

## Scripting a session

`wrench dbg` reads its commands from standard input, so a session can run
from a file or a pipe, for example in a test:

```bash
printf 'b 24\nr\nbt\nlocals\nc\nq\n' | wrench dbg orders.w
```

The commands are not echoed then, the output follows each prompt directly.
A command that waits for the program (`r`, `c`, `n`) returns when the
program stops or ends, so the next command sees the stop. On a target that
runs code by itself, events show up as they arrive, also between commands;
give the target time (a `sleep` in the pipe) where a command must see a
stop that comes later. The end of the input leaves the session like `q`.
