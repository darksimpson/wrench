# Compiling, disassembling and assembling

This page is for people who want to see what the compiler makes of a
script: the commands that compile and run, the disassembler and how to read
its listings. Every option of the commands is in the
[command line reference](../reference/cli.md).

## The compiler in brief

The compiler lives in `src/cc` and is part of the library, so a host
application can compile scripts itself (see
[Compiling scripts](../embedding/compiling.md)); the command line tool is
one such host.

| File | Part |
|---|---|
| `token.cpp` | the tokenizer: numbers, strings and their escapes, operators, comments |
| `cc.cpp` | the parser and code generator: statements, functions, structs, `switch`, `goto`, line markers |
| `expression.cpp` | expressions, operator precedence, and the choice of the fused opcodes |
| `optimizer.cpp` | a peephole optimizer over the emitted opcodes |
| `include.cpp` | `include "file"`: the file table and the mapping of lines |
| `link.cpp` | puts the units together into a [bytecode image](bytecode.md): header, function table, symbols, code, checksum |

The compiler reads the source once and emits code into units, the global
code and one unit per function, which the linker then puts together. Only
when a jump turns out too far for 16 bits does it compile again, with room
for a long jump. It needs no file system: included files come through a
callback the host supplies. It prints nothing: it returns an error code and
a message.

## Compiling and running

| Command | Use |
|---|---|
| `wrench c file.w file.bin` | compile to a bytecode file, for `wr_run()`, a device's flash or `rb` |
| `wrench ch file.w file.h name` | compile to a C header with `name_bytecode[]` and `name_bytecodeSize` |
| `wrench p file.w` | compile and print the disassembly, nothing written |
| `wrench r file.w` | compile and run, with all libraries and `print`/`println` |
| `wrench rb file.bin` | run a compiled file |
| `wrench d file.bin` | print the disassembly of a compiled file |

`c`, `ch`, `p` and `r` take the compile options `-debug`, `-debugsource`,
`-noglobals`, `-nostrict` and `-I <folder>`. A program compiled once runs on
any target with the same major version of wrench, whatever its byte order
or word size: the image is checked and read byte by byte (see
[The bytecode image](bytecode.md)).

In VS Code, **Wrench: Compile to Bytecode** runs `wrench c` on the open
file and **Wrench: Show Disassembly** shows `wrench p -debug` beside it
(see [Editing features](../vscode/editing.md#commands)).

## Is there an assembler?

No. The tool has no command that turns a text listing back into bytecode,
and the listing format is not meant to be read back in. There is no `ca`
command either: `wrench ca` prints the help text like any unknown command.

To put an image into a project that is not written in C, convert the
output of `wrench c` with the tools of that project, or use `wrench ch`
and keep the array.

## The disassembler

`wrench d` and `wrench p` call `wr_disassemble()`, which every build with
the compiler has:

```cpp
void wr_disassemble( const uint8_t* bytecode, const unsigned int len, WRstr& out, const bool includeComments =true );
void wr_disassemble( const uint8_t* bytecode, const unsigned int len, char** out, unsigned int* outLen =0 );
```

The second form returns a buffer you free with `wr_free()`. In a build with
`WRENCH_WITHOUT_COMPILER` the disassembler is left out and returns a one
line message.

## Reading a listing

The listing below is `hello.w` compiled with `-debug`:

```wrench
var count = 3;

function add( a, b )
{
	return a + b;
}

for ( var i = 0; i < count; ++i )
{
	println( add( i, 10 ) );
}
```

```bash
wrench p -debug hello.w
```

Output:

```text
Wrench Bytecode Disassembly
===========================
globals : 0x02
units   : 0x01
flags   : 0x13
code    : [0x0059..0x0095)

Globals Table
-------------
  g[0x00] hash=0xC47A7A04 ::count
  g[0x01] hash=0xEDBE8594 ::i

Function Table
--------------
  idx  hash         args frame baseAdj offset
  0x00 0x3B391274 0x02 0x00  0x03   0x008E add

Debug Symbols @0x001C (0x003D bytes)
-------------
  source hash=0x1E25124D
  unit 0 ::global args=0 locals: ::count ::i
  unit 1 add args=2 locals: a b
  file 0 hello.w source=0x00000000 bytes
  lines from 1: file 0 line 1

unit global @0x0059
  0059: F5 01 40                            DebugInfo                      line 1
  005C: CF 00 03                            LiteralInt8ToGlobal            g[0x00] imm=0x03
  005F: F5 08 40                            DebugInfo                      line 8
  0062: CF 01 00                            LiteralInt8ToGlobal            g[0x01] imm=0x00
  0065: F5 08 40                            DebugInfo                      line 8
  0068: 84 00 01 20 09                      GGCompareLTBZ8                 g[0x00] g[0x01] rel8=0x20 pad=0x09 ->0x008B
  006D: F5 0A 40                            DebugInfo                      line 10
  0070: 20 01                               LoadFromGlobal                 g[0x01]
  0072: AF 0A                               LiteralInt8                    0x0A
  0074: F5 01 00                            DebugInfo                      call fnIdx=0x00                              ; add
  0077: 07 02 00 02 58                      CallFunctionByIndex            argc=0x02 fnIdx=0x00 pcSkip=0x02
  007C: 08                                  PushIndexFunctionReturnValue
  007D: F5 00 0E                            DebugInfo                      call external
  0080: 06 01 A6 F8 BF 18                   CallFunctionByHashAndPop       argc=0x01 hash=0x18BFF8A6
  0086: 90 01                               IncGlobal                      g[0x01]
  0088: 37 DC 09                            RelativeJump8                  rel8=0xDC pad=0x09 ->0x0065
  008B: 02                                  LiteralZero
  008C: EF                                  GlobalStop
  008D: 14                                  Stop

unit 0 add hash=0x3B391274 args=2 frame=0 baseAdj=3 @0x008E
  008E: F5 05 40                            DebugInfo                      line 5
  0091: DC 01 00                            LLBinaryAddition               l[0x01] l[0x00]
  0094: 13                                  Return
```

### The header

| Line | Meaning |
|---|---|
| `globals : 0x02` | two globals: `count` and `i` (a `for` variable at the top level is a global) |
| `units   : 0x01` | one function besides the global code |
| `flags   : 0x13` | the compiler flags stored in the image: `WR_INCLUDE_GLOBALS` (0x01), `WR_EMBED_DEBUG_CODE` (0x02) and `WR_READS_KEEP_VALUES` (0x10), see [The bytecode image](bytecode.md#the-header) |
| `code    : [0x0059..0x0095)` | where the code starts and ends; before it lie the tables and the debug symbols, after it the 4 byte checksum |

### The globals table

Present when the image has `WR_INCLUDE_GLOBALS` (unless `-noglobals`). Each
entry is the hash of the global's name with `::` in front: `0xC47A7A04` is
the hash of `::count`. `wr_getGlobalRef()` searches this table; names
themselves are not stored here. When the image has debug symbols, the
listing adds the name from them (`::count`); without them it shows the
hash alone.

### The function table

One row per function: its index, the hash of its name (`0x3B391274` is the
hash of `add`), the number of parameters, `frame` (locals beyond the
parameters), `baseAdj` (how far the frame base lies below the stack top
after the call: 1 + args + frame) and the offset of its code in the image.
The name at the end of the row comes from the debug symbols, as for the
globals.

### Debug symbols and embedded source

With `-debug` or `-debugsource` the listing decodes the
[debug symbols](debug-info.md#the-symbols-section): the hash of the
source, one line per unit with its parameters and locals (unit 0 is the
global code, its locals are the globals), the file table, and the
segments that map the lines of the compiled text to files and lines
(`lines from 1: file 0 line 1`). With `-debugsource` a line
`Embedded Source @0x005D (0x00000079 bytes, not listed)` follows, which
gives the place and size of the source text without printing it.

### The code

Every unit starts with a heading: `unit global` for the global code, then
`unit <index>` for each function, with its name when the image has debug
symbols and the same values as its table row.
Each line of code has four columns:

1. the offset of the instruction in the image, in hex;
2. its bytes;
3. the opcode name;
4. the operands, decoded.

The operand notation:

| Notation | Meaning |
|---|---|
| `g[0x01]` | global number 1 (here `i`) |
| `l[0x00]` | slot 0 of the current frame: parameters first (`a`, `b`), then locals |
| `imm=0x03`, `0x0A` | a literal value in the instruction |
| `rel8=0x20 ->0x008B` | a relative jump, and the offset it lands on |
| `pad=0x09` | an unused byte: the optimizer turned a 16 bit jump into its faster 8 bit form in place, the second byte of the old offset stays |
| `argc=0x01 hash=0x18BFF8A6` | a call by name: the argument count and the hash of the name (`println`) |
| `argc=0x02 fnIdx=0x00 pcSkip=0x02` | a call of a function of this program by its index; `pcSkip` steps over the rest of the call by hash the linker rewrote (the `58` is left of that hash) |

Walking through the global code:

* `LiteralInt8ToGlobal g[0x00] imm=0x03` is `var count = 3;`. The VM has
  fused opcodes for common pairs: storing a literal into a global is one
  instruction.
* `GGCompareLTBZ8 g[0x00] g[0x01] ... ->0x008B` is the loop condition
  `i < count`: compare two globals and branch out of the loop when the
  comparison is false (`BZ`, branch if zero).
* `LoadFromGlobal`, `LiteralInt8` push the arguments `i` and `10`.
* `CallFunctionByIndex` calls `add`; `PushIndexFunctionReturnValue` pushes
  what it returned.
* `CallFunctionByHashAndPop` calls `println` by the hash of its name and
  drops its result. The VM finds natives and functions of imported modules
  by this hash at run time.
* `IncGlobal g[0x01]` is `++i`, and `RelativeJump8 ->0x0065` jumps back to
  the loop's line marker and condition.
* `LiteralZero` and `GlobalStop` end the global code with the return value
  0. `GlobalStop` has no operand; the `Stop` after it is the place the
  context remembers as its end, where later calls from the host return to
  (see [The code](bytecode.md#the-code)).

The function `add` is one fused instruction, `LLBinaryAddition` of two
frame slots, and `Return`.

### Struct member tables

A function used as a struct has its member table right before its code
(see [The code](bytecode.md#the-code)). The listing shows it as data, not
as instructions: a heading with the number of members, parameters and
slots, the 4 byte head, then one row per slot with the hash of a member
name, the member's index and its name. For `struct P { var x; var y = 2; }`:

```text
unit 1 P member table @0x00B3: members=2 args=0 slots=2 (data)
  00B3: 02 00 02 00
  00B7: F4 4E 0C FC 01                      hash=0xFC0C4EF4 -> member 0x01 y
  00BC: 87 50 0C FD 00                      hash=0xFD0C5087 -> member 0x00 x

unit 1 P hash=0xD50C118F args=0 frame=2 baseAdj=3 @0x00C1
```

### Line markers and call markers

A `DebugInfo` instruction (`F5`) carries a 16 bit codeword, stored low
byte first: the bytes `01 40` are the codeword `0x4001`. The listing
decodes it:

| Codeword | Listing | Meaning |
|---|---|---|
| `0x4000` + line | `line 8` | a line marker: the code that follows belongs to this line of the compiled text; in an included file a comment adds the file and its own line (`; lib/stats.w:3`) |
| `0xC000` + high bits | `line high bits=0x0001` | the upper bits of the next line marker, for lines above 16383 |
| `0x0000` + n | `call fnIdx=0x00 ; add` | the call that follows calls function unit n, the function with index n - 1 (`0x0001` is `add`) |
| `0x0E00` | `call external` | the call that follows goes to a native, a library function or a module, by hash |
| `0x8000` | `return` | a function has returned; defined, but the compiler does not emit it |

Lines count through the compiled text, the main file with its included
files in place (see [Debug information](debug-info.md)); for a line of the
main file the listing shows that number alone.

The line marker at `0x0065` is on the loop's own line: the jump back at
`0x0088` lands on it, so every pass of the loop meets a line marker and a
debugger can pause there. How the debugger uses the markers and what they
cost is in [Debug information](debug-info.md).

### Opcode names

The names follow the families described in
[The virtual machine](vm.md#opcode-families): a prefix `GG`, `GL`, `LG` or
`LL` says that both operands are globals (`G`) or locals (`L`), `GS` and
`LS` that one is a global or local and the other is on the stack, a suffix
`BZ` that the instruction also branches when the result is zero, `8` that
the jump offset is 8 bits, `AndPop` that the result is not kept.
`Extended` is a prefix: the opcode is in the second byte, for example
`RelativeJump32` for a jump that does not fit in 16 bits.
