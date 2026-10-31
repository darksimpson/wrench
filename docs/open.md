# Open issues

Known limits and defects that are not fixed yet, and ideas still under
discussion, by scope. Changes already decided on but put off to a later
release are in `deferred.md`; finished work is in `done.md`, and CHANGELIST
lists it for users.

## Language and compiler

* A program is limited to 64KB of bytecode (`WR_ERR_code_too_large`):
  function offsets and other fixups in the image are 16 bit. Lifting it
  would change the bytecode format; jumps and switches inside that limit
  are not limited any more.
* Assigning to a member or element of a variable that holds a string or a
  number still replaces it with a container (`var n = 5; n.k = 1;`); only
  reads were changed. A runtime error there was considered and left out.
* Reading an element past the end of an array grows the array.
* Member syntax on a key that does not exist yet stores the key's hash, the
  name itself is not in the bytecode.
* `._remove()` returns the container, so removals chain; a 1 / 0 result
  would need a new opcode, the compiler builds `{}` with this one.
* Namespaces were deferred. A design that fits: `namespace x { ... }` at
  the top level prefixes the names declared in it (`x::name`), unqualified
  names inside look in the namespace first; `x::f()` calls compile as
  library calls today, the linker would retarget them in place (both 5
  bytes) and a library call that finds nothing would look in imported
  modules.
* A private function cannot be reached through its name as a value
  (`var f = helper;` holds the shared name's hash); `private struct` is not
  supported.
* Names and keys are known only by their hash, and a collision goes
  unnoticed in many places; the fixes are planned in `deferred.md` ("Check
  hash hits against the text").

## VM

* Calling a function by its name as a value (`var f = name; f();`) is not
  supported (see 7.0.1 notes).
* A module cannot call functions of its importer
  (`WR_ERR_function_not_found`): the importer's global code is still
  running when the module's runs. Modules can call each other and the host.
* Opcodes are validated only as they execute: a corrupt image whose CRC
  matches can still read operands out of range before reaching an unknown
  opcode. A validation pass at load time would make images from untrusted
  sources safe.

## Embedding API

* `WR_ERR_tried_to_load_non_resolvable`, `WR_ERR_array_must_be_indexed`,
  `WR_ERR_scontext_not_found` and `WR_ERR_switch_with_no_cases` are never
  set; `c_errStrings` is not there with `WRENCH_WITHOUT_COMPILER`.

## Debugger

* Stepping granularity is the source line; `stopOnEntry` for an attached
  host-driven target means "the next line the firmware executes". Left as
  it is: finer markers would cost code size in every debug build.
* A module compiled for the first time (a source text no session has seen,
  so a program hash with no copy in `wrench-sources` yet) cannot have
  breakpoints before its global code runs: its source is only known once
  it is there. Break in the importer and step in, or set them and run
  again; from then on they are parked by the program's hash and hit from
  the module's first call.
* A `wrench dbg` without -attach on a target that refuses uploads detaches
  now, but its Hello has already consumed a `-hold` halt; avoiding that
  needs a protocol change.

## Libraries

* The SPIFFS / LittleFS file backend and the FastLED library were fixed
  from reading only; they have not been compiled for ESP32 since.
* glibc 2.41 prints `%#g` wrong when rounding carries into a new digit;
  wrench without `WRENCH_FLOAT_SPRINTF` prints the C standard result.

## Tools

* The language server's find references and rename stay within one file,
  and types are only the evident ones (`new S`): a variable given the
  result of a call, a parameter or a member gets no type.

## Testing

* A fuzzer for `wr_compile` and `wr_deserialize` (both take untrusted
  input) would be worth adding.

## Documentation

* The website (`www/index.html`) is behind the code in many places; the
  book is the reference.
