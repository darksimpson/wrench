# std

The `std::` namespace holds general purpose functions: random numbers and
the wall clock time. Other libraries add more names to it, so this page
also tells you where those are documented.

| Function | Loaded by | Documented in |
|---|---|---|
| `std::rand`, `std::srand`, `std::time` | `wr_loadStdLib` | this page |
| `std::serialize`, `std::deserialize` | `wr_loadSerializeLib` | [Serialization](serialize.md) |
| `std::delay` | `wr_loadArduinoSTDLib` (Arduino only) | [Devices](devices.md#stddelay) |

`wr_loadStdLib( w )` also registers `time::ms`, described in
[Messages and time](msg-time.md#timems). In `wrench.json` this library is
`std`. It needs no build flags; `std::time` depends on the platform (see
below).

## Functions

| Function | Arguments | Returns | Summary |
|---|---|---|---|
| `std::rand` | `to` or `from, to` | int | pseudo-random int, both ends included |
| `std::srand` | `seed` | nothing | seed the generator |
| `std::time` | none | int | seconds since 1970 (Unix time) |

## std::rand

`std::rand( to )` returns a pseudo-random int from 0 to `to`;
`std::rand( from, to )` returns one from `from` to `to`. Both ends are
included, so `std::rand( 1, 6 )` is a die roll.

* When the lower bound is not below the upper one, the result is the
  lower bound: `std::rand( 6, 2 )` is always 6, `std::rand( -3 )` is
  always 0.
* Arguments are converted to int; a float is truncated.
* Without arguments the result is 0 and the generator does not advance.
* The full int range, `std::rand( -2147483648, 2147483647 )`, works.

The generator is the Park-Miller "minimal standard" generator. It is fast
and small, not suitable for anything that must be unpredictable (keys,
tokens). The same seed gives the same sequence on every platform:

```wrench
std::srand( 1234 );
var rolls = "";
for( var i = 0; i < 8; ++i )
{
	rolls += " " + std::rand( 1, 6 );
}
println( "rolls:", rolls );
```

Output:

```text
rolls: 5 5 1 2 2 3 3 6
```

```wrench
std::srand( 99 );
println( std::rand( 10 ), " ", std::rand( -5, -1 ), " ", std::rand( 6, 2 ), " ", std::rand( 3, 3 ) );
println( std::rand( 0 ), " ", std::rand( -3 ), " ", std::rand() );
```

Output:

```text
0 -5 6 3
0 0 0
```

## std::srand

`std::srand( seed )` sets the generator's state to `seed` (converted to
int). It needs exactly one argument; otherwise it does nothing. A seed of
0 is replaced by the built in default seed, because 0 would make the
generator return 0 forever.

Without `std::srand`, every run of the program starts from the same
seed and produces the same numbers. Seed from the clock to get a
different sequence each time:

```wrench
std::srand( std::time() ); // a different sequence on every run
var roll = std::rand( 1, 6 );
println( roll >= 1 && roll <= 6 );
```

Output:

```text
1
```

> [!NOTE]
> Each state has its own generator (`WRState::randSeed`), shared by the
> contexts of that state: seeding in one script changes the numbers the
> other scripts of the same state get, but not those of other states.
> `wr_loadStdLib()` starts it from the global `wr_Seed`, so a host that
> sets `wr_Seed` before loading the library picks the starting seed. The
> host draws from a state's generator with `wr_randEx( w, from, to )`;
> `wr_randEx( from, to )` without a state uses `wr_Seed` itself, a
> generator of the host that no script touches.

## std::time

`std::time()` returns the current time as seconds since 1970-01-01 UTC
(C `time( 0 )`), as an int. It ignores its arguments.

```wrench
var start = std::time();
println( "seconds since 1970: ", start > 1700000000 );
```

Output:

```text
seconds since 1970: 1
```

Where it comes from depends on the target:

| Target | Source | Result |
|---|---|---|
| Windows, Linux, macOS, ARM boards, ESP32 (ESP-IDF or the Arduino core), any `__clang__` build | C `time( 0 )` | Unix time; on a board only once the firmware set the clock (SNTP, an RTC), before that whatever the C library starts at, usually seconds since the start |
| other Arduino boards (AVR) | `millis() / 1000` | seconds since the board started |
| anything else | none | 0 |

For measuring intervals in milliseconds use
[`time::ms`](msg-time.md#timems).
