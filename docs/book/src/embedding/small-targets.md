# Small targets: memory and build flags

This page is for firmware with little flash and RAM. It gives measured
figures for code size and memory, the build flags that trade speed or
features for size, the stack and the allocator, and what to leave out. The
complete list of flags is in [Build flags](../reference/build-flags.md).

## Where the memory goes

**Flash (code).** The VM is the core; the compiler is several times its
size, and the standard library, the debugger and the scheduler are added
only if compiled in and used. Bytecode images go to flash too, as `const`
arrays.

**RAM.** A running program needs:

| Item | Size on a 32-bit target |
|---|---|
| `WRState` | 72 bytes, plus its registry of natives (144 bytes for a fresh state in total) |
| `WRContext` | 96 bytes, plus 8 bytes per global and a function table entry per function |
| value stack, per context | 8 bytes per entry (`stackSize`, 64 by default, so 512 bytes) |
| strings, arrays, hash tables | what the script creates, until the garbage collector frees it |
| bytecode | nothing if it runs from flash; its size if loaded into RAM |
| debug server | about 2 x `WRENCH_DEBUG_MAX_PAYLOAD` plus a few hundred bytes, if used |

Measured with a counting allocator for `examples/debug_targets/script/blink.w`
(a 243 byte image, three globals, a struct, a four element array), a stack
of 32 entries and two natives, built for 32-bit x86 with MSVC:

| | Heap in use |
|---|---|
| after `wr_newState( 32 )` and two `wr_registerFunction()` | 144 bytes |
| plus `wr_newContext()` for the image in flash | 675 bytes |
| after the global code and 100 calls of `loop()` | 1069 bytes |

On a 64-bit PC the same program uses about twice that, since values and
pointers are twice as large.

**Code size.** Measured on x86-64 with GCC 14 (`-Os`, unused sections
removed), for a program that runs a bytecode array and registers one
native, without the standard library. Absolute numbers differ on a
microcontroller (ARM Thumb code is smaller, AVR larger), the ratios hold:

| Build | Code added |
|---|---|
| default (VM, compiler linked because it is called) | 143 KB |
| default, compiler never called | 57 KB |
| `WRENCH_WITHOUT_COMPILER` | 57 KB |
| `WRENCH_WITHOUT_COMPILER` + `WRENCH_COMPACT` | 32 KB |
| `WRENCH_WITHOUT_COMPILER` + `WRENCH_REALLY_COMPACT` | 25 KB |
| `WRENCH_WITHOUT_COMPILER` + `WRENCH_COMPACT` + `WRENCH_INCLUDE_DEBUG_CODE` | 44 KB |
| `WRENCH_WITHOUT_COMPILER` + `WRENCH_COMPACT` + `WRENCH_TIME_SLICES` + `WRENCH_HANDLE_MALLOC_FAIL` | 34 KB |

The project quotes about 28 KB of flash and under 700 bytes of RAM for the
VM on an AVR Arduino (README), enough to run on an Uno Mini.

## Size related flags

| Flag | Saves | Costs |
|---|---|---|
| `WRENCH_WITHOUT_COMPILER` | the compiler, if a linker would not drop it anyway | compiling on the target |
| `WRENCH_COMPACT` | about 45% of the VM: no unrolled loops or inlined paths | some speed |
| `WRENCH_REALLY_COMPACT` | a few KB more: a `switch` instead of the computed goto dispatch | more speed; implies `WRENCH_COMPACT` |
| `WRENCH_DEFAULT_STACK_SIZE` | 8 bytes of RAM per entry and context | scripts with deep recursion fail sooner |
| `WRENCH_UNPROTECTED_STACK` | a compare on every push | an overflow overwrites memory |
| `WRENCH_GC_MARK_STACK_SIZE` | native stack of the collector, one pointer per entry (32) | extra passes over very deep data |
| `WRENCH_MAX_PARSE_DEPTH` | native stack of the compiler | deeply nested source fails to compile |
| `WRENCH_DEBUG_MAX_PAYLOAD` and the other `WRENCH_DEBUG_MAX_*` | RAM of the debug server | smaller frames, fewer breakpoints and handles |

Flags that add code, to leave undefined on a small target unless needed:
`WRENCH_INCLUDE_DEBUG_CODE`, `WRENCH_TIME_SLICES`,
`WRENCH_HANDLE_MALLOC_FAIL`, `WRENCH_FLOAT_SPRINTF` (hands float
formatting to the C library's `snprintf`; without it wrench formats floats
itself, as C `printf` does), `WRENCH_TRAP_DIVISION_BY_ZERO`, and
the file and TCP flags.

On Arduino and other build systems where command line defines are awkward,
put them into a `wrench_config.h` next to `wrench.h`; it is included
automatically ([Quick start: Arduino](quickstart-arduino.md#wrench_configh)).

## Stack size

The value stack is allocated with each context: `wr_newState( stackSize )`
sets its size for every context of the state. It holds one frame per active
script call (return address, arguments, locals) and temporaries of
expressions, not the contents of containers. The examples for small boards
use 20 to 48 entries; 64 is the default. A script that needs more fails
with `WR_ERR_stack_overflow`, so try the scripts you ship with the stack you
pick. Imported modules share their parent's stack.

## Stack protection

By default every push is checked, and calls check that their frame fits
(`WRENCH_PROTECT_STACK_FROM_OVERFLOW`, defined unless
`WRENCH_UNPROTECTED_STACK` is). The check is one compare per push. Leave it
on unless you know every script fits and you need the last few percent of
speed; without it an overflow silently overwrites the memory after the
stack.

## The allocator

```cpp
typedef void* (*WR_ALLOC)( size_t size );
typedef void (*WR_FREE)( void* ptr );

void wr_setGlobalAllocator( WR_ALLOC wralloc, WR_FREE wrfree );
void* wr_malloc( size_t size );
void wr_free( void* ptr );
```

wrench allocates everything through one global pair of functions,
`malloc()` and `free()` by default: states, contexts, objects of scripts,
compiled bytecode, strings from `asMallocString()`, buffers from
`wr_serialize()`, the debug server's state. `wr_setGlobalAllocator()`
replaces them, for a memory pool, PSRAM, or accounting.

* Set it once, before the first wrench call. Memory is freed with whatever
  allocator is set at that moment.
* It applies to all states and threads; make it thread safe if states run
  on several threads.
* Memory wrench hands to you (bytecode from `wr_compile()`, text from
  `asMallocString()`, the debugger's `takeUpload()`) is freed with
  `wr_free()`, and memory you hand to wrench with `takeOwnership` must come
  from `wr_malloc()`.

The counting allocator used for the table above:

```cpp
#include "wrench.h"

#include <stdlib.h>

// counts the bytes wrench holds, with a small header per block
static long g_current = 0;
static long g_peak = 0;

static void* countingAlloc( size_t size )
{
	size_t* block = (size_t*)malloc( size + sizeof(size_t) );
	if ( !block )
	{
		return 0;
	}
	*block = size;
	g_current += (long)size;
	if ( g_current > g_peak )
	{
		g_peak = g_current;
	}
	return block + 1;
}

static void countingFree( void* p )
{
	if ( p )
	{
		size_t* block = (size_t*)p - 1;
		g_current -= (long)*block;
		free( block );
	}
}

int main()
{
	wr_setGlobalAllocator( countingAlloc, countingFree );
	// ... use wrench ...
	return 0;
}
```

## Garbage collection

The collector runs when a context has allocated more than a threshold
since its last run, 4000 bytes by default (`WRENCH_DEFAULT_ALLOCATED_MEMORY_GC_HINT`):

```cpp
void wr_setAllocatedMemoryGCHint( WRState* w, const uint32_t bytes );
```

A lower threshold keeps the peak memory lower at the cost of more
collections; scripts that allocate nothing in their steady state (only
numbers, containers built once) never trigger it.

## Out of memory

Without `WRENCH_HANDLE_MALLOC_FAIL`, wrench assumes allocations succeed; a
null from the allocator is dereferenced. With it, every allocation is
checked, a failure stops the script with `WR_ERR_malloc_failed`, and the VM
tests the failure flag after each instruction that may have allocated. Use
it where the scripts can exhaust the heap and the device must survive that.
The failure flag is cleared at the start of the next call into the VM
([Errors and robustness](errors.md#recovering-after-an-error)).

## Running from flash

Bytecode runs where it is, without a copy
([Running from flash or ROM](running.md#running-from-flash-or-rom)). On
little endian targets that allow unaligned loads (x86, Cortex-M3 and
later), `WRENCH_UNALIGNED_READS` lets the VM read 16 and 32 bit operands
with one access instead of byte by byte. Do not define it where unaligned
loads fault, such as Cortex-M0 and the ESP32's Xtensa cores, or where the
image sits in memory that only allows aligned access.

## What to leave out

A minimal firmware that runs fixed scripts:

* compile on the PC (`wrench ch`) and define `WRENCH_WITHOUT_COMPILER`;
* define `WRENCH_COMPACT` (or `WRENCH_REALLY_COMPACT` if flash is tight
  and speed is not);
* load no standard library, or only the loaders you need
  ([The standard library](natives.md#the-standard-library));
* compile the images without `-debug` and with `-noglobals` if the host
  never looks up globals by name (4 bytes per global);
* pick the smallest stack the scripts run with;
* add `WRENCH_INCLUDE_DEBUG_CODE` with small `WRENCH_DEBUG_MAX_*` values
  only in development builds, if it does not fit in production.
