# Devices: Arduino, ESP32 and peripherals

These libraries give scripts on a microcontroller access to its hardware:
pins, delays, the I2C bus, character displays and WS2812 LED strips.
Read this page if you put wrench on an Arduino or ESP32 board and want
scripts to drive the hardware without writing natives of your own.

All of them only compile for their platform; on a PC their source files
compile to nothing. The examples on this page can therefore not run with
`wrench r` on a PC (they compile there, but stop with
`WR_ERR_lib_function_not_found`); they are written from the library
code. For setting up wrench on a board, see
[Quick start: Arduino](../embedding/quickstart-arduino.md) and
[Quick start: ESP32 with ESP-IDF](../embedding/quickstart-esp32.md).

| Library | Source | Compiled when | Loader |
|---|---|---|---|
| `std::delay`, `io::` pins, `wire::`, `lcd::` | `src/lib/arduino_lib.cpp` | `ARDUINO` is defined (Arduino IDE, PlatformIO with the Arduino framework) | `wr_loadAllArduinoLibs` or one of the loaders below |
| `fastled::` | `src/lib/fastled_lib.cpp` | `WRENCH_ARDUINO_FASTLED` is defined, and the FastLED library is installed | `wr_loadFastLEDLib` |

None of them is part of `wr_loadAllLibs`, and `wrench.json` has no names
for them: describe them under `functions` if you want the language server
to know them (see [Describing your host application: wrench.json](../vscode/wrench-json.md)).
Both files are part of the single file build (`wrench.cpp`), so a sketch
that uses it has them already.

## Loading them

| Loader | Registers |
|---|---|
| `wr_loadAllArduinoLibs( w )` | all of the four below |
| `wr_loadArduinoSTDLib( w )` | `std::delay` |
| `wr_loadArduinoIOLib( w )` | `io::pinMode`, `io::digitalWrite`, `io::analogRead` |
| `wr_loadArduinoLCDLib( w )` | `lcd::begin`, `lcd::setCursor`, `lcd::print`, only when `WR_ARDUINO_LCD` names a display (see [lcd](#lcd)); otherwise nothing |
| `wr_loadArduinoWireLib( w )` | the `wire::` functions |
| `wr_loadArduinoLib( w )` | the old name of `wr_loadAllArduinoLibs` |
| `wr_loadFastLEDLib( w )` | the `fastled::` functions |

`wrench.h` declares the Arduino loaders when `ARDUINO` is defined, and
`wr_loadFastLEDLib` when `WRENCH_ARDUINO_FASTLED` is defined.

```cpp
#include "wrench.h"

WRState* w;

void setup()
{
	w = wr_newState();
	wr_loadMathLib( w );
	wr_loadStringLib( w );
	wr_loadArduinoSTDLib( w );
	wr_loadArduinoIOLib( w );
	wr_loadArduinoWireLib( w );
}
```

The pin functions share the `io::` namespace with the
[file functions](io.md) but have different names, so both can be loaded
together (the file functions need `WRENCH_SPIFFS_FILE_IO` or
`WRENCH_LITTLEFS_FILE_IO` on a board).

## Standard libraries on a board

The portable libraries work on boards as on a PC: [math](math.md),
[str](str.md), [sys](sys.md), [containers](containers.md),
[msg](msg-time.md), [serialization](serialize.md) and [debug](debug.md),
and `std::rand` / `std::srand`. Some do not:

* [`time::ms`](msg-time.md#timems) counts milliseconds with `millis()`
  on every Arduino board (with `esp_timer` on ESP-IDF, `HAL_GetTick()`
  with the STM32 HAL).
* [`std::time`](std.md#stdtime) is the Unix time on ESP32 once SNTP or an
  RTC set the clock; on AVR boards it is the seconds since the start.
* The [file functions](io.md) need the SPIFFS or LittleFS backend.
* [`tcp::`](tcp.md) has no backend for boards.
* [`str::printf`](str.md#strprintf) writes to `Serial` with a file system
  backend, otherwise to C `stdout`.

## Function overview

| Function | Arguments | Returns | Summary |
|---|---|---|---|
| `std::delay` | `ms` | nothing | wait `ms` milliseconds (Arduino `delay`) |
| `io::pinMode` | `pin, mode` | nothing | 0 input, anything else output |
| `io::digitalWrite` | `pin, value` | nothing | set a digital output |
| `io::analogRead` | `pin` | int | read an analog input |
| `wire::begin` | none | nothing | join the I2C bus as controller |
| `wire::beginTransmission` | `address` | nothing | start a write to `address` |
| `wire::write` | `value [, count]` | int | queue an int as one byte, or a string's bytes; the number queued |
| `wire::endTransmission` | `[stop]` | int | send the queued bytes; 0 on success |
| `wire::requestFrom` | `address, count` | int | read `count` bytes; the number received |
| `wire::available` | none | int | bytes waiting to be read |
| `wire::read` | none | int | the next received byte |
| `lcd::begin` | `columns, rows` | int | start the display; the library's status, or 0 |
| `lcd::setCursor` | `column, row` | nothing | move the cursor |
| `lcd::print` | `value` | int | print a value as text; the characters printed |
| `fastled::setup2812` | `[pin,] count` | 1 or 0 | set up a WS2812B strip |
| `fastled::brightness` | `level` | nothing | global brightness 0 to 255 |
| `fastled::set` | `led, r, g, b` | nothing | set one pixel |
| `fastled::show` | none | nothing | send the pixels to the strip |

Like the other libraries, a function called with the wrong number of
arguments does nothing and returns 0; arguments are converted to int.

## std::delay

`std::delay( ms )` calls Arduino `delay()`: the script and everything
else on that core wait `ms` milliseconds. It needs exactly one argument.

> [!WARNING]
> While `std::delay` waits, the VM runs no instructions, so a debugger
> cannot pause or stop the script until it returns. For long waits return
> from the script and let the firmware call it again later, as the
> `loop()` example in the [introduction](../introduction.md) does, or see
> [Blocking code, yield and polling](../debugging/blocking.md).

## Arduino pins: io

* `io::pinMode( pin, mode )` sets the pin to `INPUT` when `mode` is 0 and
  to `OUTPUT` for any other value. Pull-ups (`INPUT_PULLUP`) cannot be
  selected.
* `io::digitalWrite( pin, value )` calls `digitalWrite( pin, value )`.
* `io::analogRead( pin )` returns `analogRead( pin )`, its range depends
  on the board (0 to 1023 on AVR, 0 to 4095 on ESP32).

There is no `io::digitalRead` or `io::analogWrite`; register natives for
them if you need them.

```wrench
io::pinMode( 13, 1 ); // output
for( var i = 0; i < 10; ++i )
{
	io::digitalWrite( 13, i % 2 );
	std::delay( 250 );
}
var level = io::analogRead( 0 );
println( "A0 = ", level );
```

## wire: I2C

The `wire::` functions call the Arduino `Wire` library (the default bus,
`Wire`) one to one.

* `wire::begin()` joins the bus as controller.
* `wire::beginTransmission( address )` starts a write to the device at the
  7-bit `address`.
* `wire::write( value [, count] )` queues bytes for the transmission: an
  int as one byte (its low 8 bits), a string as its bytes, at most `count`
  of them when `count` is given. It returns the number of bytes queued,
  which is less when the `Wire` buffer is full. A `count` of 0 or less
  queues nothing and returns 0.
* `wire::endTransmission()` sends the queued bytes and a stop condition;
  `wire::endTransmission( 0 )` sends them without the stop, for a
  following read. It returns the `Wire` status, 0 on success.
* `wire::requestFrom( address, count )` reads `count` bytes from the
  device into the receive buffer and returns how many arrived.
* `wire::available()` returns the number of bytes left in the buffer,
  `wire::read()` the next one (-1 when there is none).

```wrench
var address = 0x68;
wire::begin();
wire::beginTransmission( address );
wire::write( 0x75 ); // the register to read
var status = wire::endTransmission( 0 ); // 0: keep the bus for the read
if ( status == 0 && wire::requestFrom( address, 1 ) == 1 )
{
	println( "register 0x75 = ", wire::read() );
}
else
{
	println( "no answer, status ", status );
}
```

## lcd

The `lcd::` functions drive one character display through any library
with the LiquidCrystal interface (`begin`, `setCursor`, `print`):
LiquidCrystal, LiquidCrystal_I2C, hd44780 and others. Tell wrench which
display object to use by defining `WR_ARDUINO_LCD` as its name, for
example in `wrench_config.h` next to `wrench.h`:

```cpp
#include <LiquidCrystal.h>
extern LiquidCrystal lcd; // defined in the sketch
#define WR_ARDUINO_LCD lcd
```

Without `WR_ARDUINO_LCD`, `wr_loadArduinoLCDLib` registers nothing and a
script that calls `lcd::` stops with `WR_ERR_lib_function_not_found`.

* `lcd::begin( columns, rows )` calls `begin` on the display and returns
  the status the library returns, or 0 when its `begin` returns nothing
  (as LiquidCrystal's does).
* `lcd::setCursor( column, row )` moves the cursor.
* `lcd::print( value )` prints `value` converted to text, at most 60
  characters, and returns the number of characters the library printed.

```wrench
lcd::begin( 16, 2 );
lcd::setCursor( 0, 0 );
lcd::print( "temp" );
lcd::setCursor( 0, 1 );
lcd::print( 21.5 );
```

## fastled: WS2812 LED strips

The `fastled::` functions drive one strip of WS2812B LEDs (GRB order)
through the [FastLED](https://github.com/FastLED/FastLED) library.
Compile with `WRENCH_ARDUINO_FASTLED` defined (for example in
`wrench_config.h`) and FastLED installed. FastLED needs the data pin at
compile time, so it is a build setting:

| Setting | Default | Meaning |
|---|---|---|
| `WR_FASTLED_DATA_PIN` | 6 | the strip's data pin |
| `WR_FASTLED_MAX_LEDS` | 65535 | the largest strip `fastled::setup2812` accepts |

### fastled::setup2812

`fastled::setup2812( count )` or `fastled::setup2812( pin, count )`
allocates the pixel buffer for `count` LEDs, all off, and registers the
strip with FastLED. If `pin` is given it must equal `WR_FASTLED_DATA_PIN`;
another pin cannot work, and the call fails. Calling it again resizes the
buffer (the strip stays registered once). It returns 1 on success and 0
when `count` is 0, negative or above `WR_FASTLED_MAX_LEDS`, the pin is
wrong, or memory runs out.

### fastled::brightness

`fastled::brightness( level )` sets FastLED's global brightness, 0 to 255
(values outside are clamped), used from the next `fastled::show()`.

### fastled::set

`fastled::set( led, r, g, b )` sets pixel `led` (0 to count - 1) to the
color; each component is clamped to 0 to 255. An index outside the strip,
or a call before `fastled::setup2812`, is ignored. The LEDs change only on
`fastled::show()`.

### fastled::show

`fastled::show()` sends the buffer to the strip. Before a successful
`fastled::setup2812` it does nothing.

```wrench
var count = 30;
if ( !fastled::setup2812( count ) )
{
	println( "strip setup failed" );
}
fastled::brightness( 64 );
for( var i = 0; i < count; ++i )
{
	fastled::set( i, i * 8, 0, 255 - i * 8 );
}
fastled::show();
```

The pixel buffer is shared by every state in the firmware: there is one
strip, whichever script drives it.

## ESP32

There is no library of ESP32 functions. On an ESP32 with the Arduino
framework, use `wr_loadAllArduinoLibs` for pins, I2C and delays, the
SPIFFS or LittleFS backend for [files](io.md), and natives of your own
for Wi-Fi and the other peripherals. Firmware built with ESP-IDF (no
`ARDUINO`) gets none of the libraries on this page.

The FastLED library was last fixed from reading the code; the review in
`docs/review.md` notes that it has not been compiled for ESP32 since.
