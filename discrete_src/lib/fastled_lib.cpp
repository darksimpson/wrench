/*******************************************************************************
Copyright (c) 2026 Curt Hartung -- curt.hartung@gmail.com

MIT Licence

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*******************************************************************************/

// wrench.h first: the switch below may be set in its configuration section
#include "wrench.h"

#ifdef WRENCH_ARDUINO_FASTLED

#include <Arduino.h>
#include <FastLED.h>

// Script API, for one WS2812B strip in GRB order on the data pin
// WR_FASTLED_DATA_PIN (wrench.h). FastLED takes the pin as a template
// argument, so it is fixed at compile time and can not be picked by the
// script.
//
// fastled::setup2812( numLeds )
// fastled::setup2812( dataPin, numLeds )
//   allocates the pixel buffer (all pixels off) and registers the strip
//   with FastLED. dataPin is optional and, when given, must equal
//   WR_FASTLED_DATA_PIN. Calling it again resizes the buffer. Returns 1 on
//   success, 0 on failure (bad count, wrong pin, out of memory).
// fastled::brightness( level )
//   global brightness 0 to 255, used by the next fastled::show().
// fastled::set( led, r, g, b )
//   sets one pixel, components 0 to 255 (clamped); an index outside the
//   strip is ignored. The strip only changes on fastled::show().
// fastled::show()
//   sends the pixel buffer to the strip.

static CRGB* wr_leds = 0;
static int wr_numLeds = 0;
static CLEDController* wr_ledController = 0;

// largest strip setup2812 accepts
#ifndef WR_FASTLED_MAX_LEDS
#define WR_FASTLED_MAX_LEDS 0xFFFF
#endif

// a color or brightness argument, clamped to a byte
static uint8_t wr_fastledComponent( const WRValue& value )
{
	const int i = value.asInt();
	if ( i < 0 )
	{
		return 0;
	}
	return (i > 255) ? 255 : (uint8_t)i;
}

//------------------------------------------------------------------------------
void wr_fastled_setup2812( WRValue* stackTop, const int argn, WRContext* c )
{
	stackTop->init();

	if ( argn < 1 )
	{
		return;
	}

	WRValue* args = stackTop - argn;

	int count;
	if ( argn > 1 )
	{
		if ( args[0].asInt() != WR_FASTLED_DATA_PIN )
		{
			return; // the pin is compiled in, another one can not work
		}
		count = args[1].asInt();
	}
	else
	{
		count = args[0].asInt();
	}

	if ( count <= 0 || count > WR_FASTLED_MAX_LEDS )
	{
		return;
	}

	CRGB* leds = (CRGB*)g_malloc( count * sizeof(CRGB) );
	if ( !leds )
	{
#ifdef WRENCH_HANDLE_MALLOC_FAIL
		g_mallocFailed = true;
#endif
		return;
	}

	for( int i=0; i<count; ++i )
	{
		leds[i].r = 0;
		leds[i].g = 0;
		leds[i].b = 0;
	}

	if ( wr_ledController )
	{
		// FastLED keeps every controller it is given for good, so the strip
		// is registered only once and later just pointed at the new buffer
		wr_ledController->setLeds( leds, count );
	}
	else
	{
		wr_ledController = &FastLED.addLeds<WS2812B, WR_FASTLED_DATA_PIN, GRB>( leds, count );
	}

	// nothing refers to the old buffer any more
	if ( wr_leds )
	{
		g_free( wr_leds );
	}

	wr_leds = leds;
	wr_numLeds = count;
	stackTop->i = 1;
}

//------------------------------------------------------------------------------
void wr_fastled_brightness( WRValue* stackTop, const int argn, WRContext* c )
{
	if ( argn > 0 )
	{
		FastLED.setBrightness( wr_fastledComponent(*(stackTop - argn)) );
	}
}

//------------------------------------------------------------------------------
void wr_fastled_set( WRValue* stackTop, const int argn, WRContext* c )
{
	if ( argn < 4 || !wr_leds )
	{
		return;
	}

	WRValue* args = stackTop - argn;

	const int led = args[0].asInt();
	if ( led >= 0 && led < wr_numLeds )
	{
		wr_leds[led].r = wr_fastledComponent( args[1] );
		wr_leds[led].g = wr_fastledComponent( args[2] );
		wr_leds[led].b = wr_fastledComponent( args[3] );
	}
}

//------------------------------------------------------------------------------
void wr_fastled_show( WRValue* stackTop, const int argn, WRContext* c )
{
	if ( wr_ledController )
	{
		FastLED.show();
	}
}

//------------------------------------------------------------------------------
void wr_loadFastLEDLib( WRState* w )
{
	wr_registerLibraryFunction( w, "fastled::setup2812", wr_fastled_setup2812 ); // ( [data_pin,] num_leds ) ret: 1 ok, 0 failed
	wr_registerLibraryFunction( w, "fastled::brightness", wr_fastled_brightness ); // ( brightness 0-255 )
	wr_registerLibraryFunction( w, "fastled::set", wr_fastled_set ); // ( led, r, g, b )
	wr_registerLibraryFunction( w, "fastled::show", wr_fastled_show ); // ()
}

#endif
