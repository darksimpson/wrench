# Files: io

The `io::` file library reads and writes files: whole files in one call
(`io::readFile`, `io::writeFile`), or through descriptors like POSIX
`open`, `read`, `write` and `lseek`. It also reads lines from standard
input. Read this page when a script stores data, logs, or reads input.

```wrench
if ( !io::writeFile( "notes.txt", "first line\nsecond line\n" ) )
{
	println( "cannot write" );
}
var text = io::readFile( "notes.txt" );
println( text._count, " bytes" );
print( text );
io::deleteFile( "notes.txt" );
println( io::readFile( "notes.txt" ) );
```

Output:

```text
23 bytes
first line
second line
0
```

> [!NOTE]
> On Arduino boards the `io::` namespace also holds pin functions
> (`io::pinMode`, `io::digitalWrite`, `io::analogRead`). They come from a
> different library, `wr_loadArduinoIOLib`, and are described in
> [Devices](devices.md#arduino-pins-io).

## Loading and platforms

The host loads the library with `wr_loadIOLib( w )` (or
`wr_loadAllLibs( w )`); in `wrench.json` its name is `io`. What it
contains depends on one build flag, which selects the file system backend
(see [Build flags](../reference/build-flags.md)):

| Flag | Backend | Source file |
|---|---|---|
| `WRENCH_WIN32_FILE_IO` | Windows C runtime (`_open`, `_read`, ...) | `src/lib/std_io.cpp` |
| `WRENCH_LINUX_FILE_IO` | POSIX (`open`, `read`, ...), Linux and similar | `src/lib/std_io.cpp` |
| `WRENCH_SPIFFS_FILE_IO` | Arduino `SPIFFS` file system (ESP32, ESP8266) | `src/lib/std_io_little_spiffs.cpp` |
| `WRENCH_LITTLEFS_FILE_IO` | Arduino `LittleFS` file system (ESP32, ESP8266) | `src/lib/std_io_little_spiffs.cpp` |
| none | none: `wr_loadIOLib` registers nothing | |

Define exactly one. Without any, a script that calls an `io::` file
function stops with `WR_ERR_lib_function_not_found`. `wrench.exe` on
Windows is built with `WRENCH_WIN32_FILE_IO`, the Linux build with
`WRENCH_LINUX_FILE_IO`.

The SPIFFS and LittleFS backend calls `begin()` on the file system when
the library is loaded, keeps its own table of open files per state, and
closes a state's files when the state is destroyed. It has no standard
input. See [Differences on SPIFFS and LittleFS](#differences-on-spiffs-and-littlefs).

> [!NOTE]
> `src/lib/std_io_defs.h` lists the functions a backend provides
> (`wr_read_file`, `wr_ioOpen` and so on, plus `wr_stdout`, which
> [`str::printf`](str.md#strprintf) writes through). `wr_loadIOLib`
> registers them only when one of the four flags is defined, so a backend
> of your own either takes the place of one of the shipped files or
> registers its functions itself with `wr_registerLibraryFunction`.

## Functions and constants

| Function | Arguments | Returns | Summary |
|---|---|---|---|
| `io::readFile` | `name` | string or 0 | the whole file |
| `io::writeFile` | `name, data` | 1 or 0 | create or replace the file with `data` |
| `io::deleteFile` | `name` | int | delete the file; 0 or -1 |
| `io::open` | `name [, flags]` | int | open a file, return a descriptor or -1 |
| `io::close` | `fd` | int | close a descriptor; 0 or -1 |
| `io::read` | `fd, count` | string | read up to `count` bytes |
| `io::write` | `fd, data [, count]` | int | write a string; bytes written or -1 |
| `io::seek` | `fd, offset [, whence]` | int | move the file position; the new position or -1 |
| `io::fsync` | `fd` | int | flush the file to storage; 0 or -1 |
| `io::getline` | none | string or -1 | one line from standard input, -1 at the end of the input |

| Constant | Use |
|---|---|
| `io::O_RDONLY` | `io::open`: read only |
| `io::O_WRONLY` | `io::open`: write only |
| `io::O_RDWR` | `io::open`: read and write |
| `io::O_CREAT` | `io::open`: create the file if it does not exist |
| `io::O_TRUNC` | `io::open`: empty an existing file |
| `io::O_APPEND` | `io::open`: every write goes to the end |
| `io::O_EXCL` | `io::open`: with `io::O_CREAT`, fail if the file exists |
| `io::SEEK_SET` | `io::seek`: offset from the start |
| `io::SEEK_CUR` | `io::seek`: offset from the current position |
| `io::SEEK_END` | `io::seek`: offset from the end |

The constants have the values of the platform's C library, which differ
between Windows and Linux; always use the names. On Windows:

```wrench
println( io::O_RDONLY, " ", io::O_WRONLY, " ", io::O_RDWR, " ", io::O_APPEND, " ", io::O_CREAT, " ", io::O_TRUNC, " ", io::O_EXCL );
println( io::SEEK_SET, " ", io::SEEK_CUR, " ", io::SEEK_END );
```

Output:

```text
0 1 2 8 256 512 1024
0 1 2
```

On Linux the first line is `0 1 2 1024 64 512 128`; the SPIFFS and
LittleFS backend uses the Linux values.

File names are strings, passed to the platform as they are: relative names
are relative to the host's current directory, and on SPIFFS and LittleFS
names start with `/`.

## Whole files

### io::readFile

`io::readFile( name )` returns the whole file as a string. The file is
read in binary: line endings are kept as they are and zero bytes are
fine. An empty file gives an empty string. When the file does not exist,
cannot be read, or the call does not have exactly one argument, the
result is the int 0, so test it before using it as a string.

### io::writeFile

`io::writeFile( name, data )` creates the file, or replaces its contents,
with the bytes of the string `data`, and returns 1 on success. It returns
0 when the file cannot be opened for writing, when `data` is not a string
(an array of values or a number is not written; the file is not touched),
or when it is not called with exactly two arguments. An empty string
writes an empty file.

To store other values in a file, turn them into a string with
[`std::serialize`](serialize.md) first.

### io::deleteFile

`io::deleteFile( name )` deletes the file and returns 0, or -1 when it
could not be deleted (it does not exist, it is a directory, it is in use
or read only) or the call does not have exactly one argument.

```wrench
println( io::readFile( "missing.txt" ), " ", io::writeFile( "empty.txt", "" ), " ", io::readFile( "empty.txt" )._count );
var values[] = { 1, 2, 3 };
println( io::writeFile( "values.bin", values ), " ", io::writeFile( "values.bin", 5 ) );
println( io::deleteFile( "empty.txt" ), " ", io::deleteFile( "empty.txt" ) );
```

Output:

```text
0 1 0
0 0
0 -1
```

## Descriptors

The descriptor functions are thin wrappers around the C runtime (on
Windows and Linux) and keep its behaviour: positions, partial reads and
writes, and -1 for errors.

### io::open

`io::open( name [, flags] )` opens the file and returns a descriptor, an
int of 0 or more (in practice 3 or more), or -1 when it fails or `name` is
missing. `flags` is an OR of the `io::O_` constants; without it the file
is opened with `io::O_RDWR | io::O_CREAT`, read and write, created if
missing. Files are always opened in binary mode.

A file that `io::open` creates gets read and write permission for
everyone the platform allows (0666 minus the process umask on Linux,
read and write on Windows). A third argument is accepted and ignored, the
permissions cannot be chosen.

### io::read

`io::read( fd, count )` reads up to `count` bytes at the current position
and returns them as a string; its `._count` is the number of bytes
actually read. At the end of the file, or on an error (a bad descriptor),
the string is empty. A `count` of 0 or less, or a call without exactly
two arguments, returns the int 0. One call reads at most
`WRENCH_MAX_IO_READ` bytes (1 MB unless the host defines it otherwise);
read in a loop for more.

### io::write

`io::write( fd, data [, count] )` writes the bytes of the string `data`,
or only its first `count` bytes, at the current position, and returns the
number of bytes written or -1 on an error. A `count` larger than the
string is cut to its length. A `data` that is not a string (an array of
values, a number), or a call without `data`, writes nothing and returns
-1. Host code can also pass a raw array (a block of host
memory, see [Passing data](../embedding/data.md)), which is written the
same way.

### io::seek

`io::seek( fd, offset [, whence] )` moves the position to `offset` bytes
from the start (`io::SEEK_SET`, the default), from the current position
(`io::SEEK_CUR`) or from the end (`io::SEEK_END`), and returns the new
position from the start, or -1 on an error. `io::seek( fd, 0,
io::SEEK_END )` returns the file size.

### io::fsync

`io::fsync( fd )` makes sure everything written to the descriptor has
reached the storage device (`_commit` on Windows, `fsync` on Linux) and
returns 0, or -1 on an error.

### io::close

`io::close( fd )` closes the descriptor and returns 0, or -1 if it was
not open. Close every descriptor you open: the Windows and Linux backends
do not close them when the script or state ends.

```wrench
var fd = io::open( "log.txt", io::O_RDWR | io::O_CREAT | io::O_TRUNC );
if ( fd < 0 )
{
	println( "cannot open" );
}
println( io::write( fd, "one two three\n" ) );
println( io::write( fd, "abcdef", 3 ) );
println( io::seek( fd, 4 ) );
var word = io::read( fd, 3 );
println( "[", word, "] ", word._count );
println( io::seek( fd, -3, io::SEEK_END ) );
println( "[", io::read( fd, 100 ), "]" );
println( io::read( fd, 100 )._count );
println( io::fsync( fd ), " ", io::close( fd ), " ", io::close( fd ) );
io::deleteFile( "log.txt" );
```

Output:

```text
14
3
4
[two] 3
14
[abc]
0
0 0 -1
```

With `io::O_APPEND` every write goes to the end, wherever the position
is; with `io::O_CREAT | io::O_EXCL` the open fails if the file exists:

```wrench
var fd = io::open( "app.log", io::O_RDWR | io::O_CREAT | io::O_APPEND );
io::write( fd, "start\n" );
io::seek( fd, 0 );
io::write( fd, "stop\n" );
io::close( fd );
print( io::readFile( "app.log" ) );
var once = io::open( "app.log", io::O_RDWR | io::O_CREAT | io::O_EXCL );
println( once );
io::deleteFile( "app.log" );
```

Output:

```text
start
stop
-1
```

A descriptor that is not open:

```wrench
println( io::open( "missing.txt", io::O_RDONLY ), " ", io::open() );
println( "[", io::read( 99, 5 ), "] ", io::read( 99, 0 ), " ", io::write( 99, "x" ), " ", io::seek( 99, 0 ), " ", io::close( 99 ) );
println( io::write( 99, 5 ), " ", io::write( 99 ) );
```

Output:

```text
-1 -1
[] 0 -1 -1 -1
-1 -1
```

On Windows a bad descriptor is reported as -1 like on Linux; the C
runtime's invalid parameter handler, which would end the process, is
switched off around each call.

## io::getline

`io::getline()` reads one line from standard input and returns it as a
string, without the line ending. A line ends at `\n`, at `\r\n` or at a
lone `\r`, so input with Windows and Linux line endings reads the same.
A line longer than 256 characters is returned in pieces of 256.

An empty line is the empty string `""`. When the input has ended and no
character is left, `io::getline` returns the int -1 instead, so a loop
can tell the two apart. A last line without a line ending is still
returned as a line.

```wrench
print( "name? " );
var name = io::getline();
println( "hello, ", name, " (", name._count, " characters)" );
var line = io::getline();
while( line != -1 )
{
	println( "[", line, "] ", line._count );
	line = io::getline();
}
println( "end of input: ", line );
```

Run with three lines of input, the second one empty:

```bash
printf 'Ada\r\n\r\nlast' | wrench r ask.w
```

Output:

```text
name? hello, Ada (3 characters)
[] 0
[last] 4
end of input: -1
```

The SPIFFS and LittleFS backend has no standard input: `io::getline`
returns -1 there.

## Differences on SPIFFS and LittleFS

The ESP32 / ESP8266 backend (`WRENCH_SPIFFS_FILE_IO` or
`WRENCH_LITTLEFS_FILE_IO`) gives the functions the same meaning, with
these differences:

* Descriptors are numbers from 3 up, handed out by wrench, not by an
  operating system. Each belongs to the state that opened it: another
  state cannot use it, and destroying the state closes it.
* `io::open` maps the flags to an Arduino file mode (`"r"`, `"r+"`,
  `"w"`, `"w+"`, `"a"`, `"a+"`). Opening a directory fails.
* `io::read` never returns more than the rest of the file.
* `io::write` returns -1 when the descriptor is unknown, as for a
  `data` that is not a string.
* `io::seek` with an unknown `whence`, or to a position before the start,
  returns -1.
* `io::fsync` calls `flush()` on the file.
* `io::getline` returns -1, the end of the input.
* `io::readFile` of a directory returns 0.
