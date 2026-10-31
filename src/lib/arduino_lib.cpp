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

#include "wrench.h"

#ifdef ARDUINO

#include <Arduino.h>
#include <Wire.h>

//------------------------------------------------------------------------------
void wr_std_delay( WRValue* stackTop, const int argn, WRContext* c)
{
	// std::delay( milliseconds )
	if( argn == 1)
	{
		delay( (unsigned long)stackTop[-1].asInt() ); // a signed int will work up to 25 days
	}
}

//------------------------------------------------------------------------------
void wr_io_pinMode( WRValue* stackTop, const int argn, WRContext* c)
{
	// io::pinMode( pin, <0 input, 1 output> )
	if( argn == 2)
	{
		pinMode( stackTop[-2].asInt(), (stackTop[-1].asInt() == 0) ? INPUT : OUTPUT );
	}
}

//------------------------------------------------------------------------------
void wr_io_digitalWrite( WRValue* stackTop, const int argn, WRContext* c)
{
	// io::digitalWrite( pin, value )
	if( argn == 2)
	{
		digitalWrite(stackTop[-2].asInt(), stackTop[-1].asInt());
	}
}

//------------------------------------------------------------------------------
void wr_io_analogRead( WRValue* stackTop, const int argn, WRContext* c)
{
	// io::analogRead( pin )
	if ( argn == 1)
	{
		stackTop->i = analogRead( stackTop[-1].asInt() );
	}
}

#ifdef WR_ARDUINO_LCD

// the display is the object WR_ARDUINO_LCD names (see wrench.h), any
// display with the LiquidCrystal interface: LiquidCrystal, LiquidCrystal_I2C,
// hd44780 and others

// begin() returns nothing in LiquidCrystal but a status in other libraries:
// "(begin(), status)" stores a returned int through the operator below and
// leaves 0 when begin() is void (a void operand takes the built-in comma)
struct WRLcdStatus
{
	int value;
};

inline WRLcdStatus& operator,( const int value, WRLcdStatus& status )
{
	status.value = value;
	return status;
}

//------------------------------------------------------------------------------
void wr_lcd_begin( WRValue* stackTop, const int argn, WRContext* c)
{
	// lcd::begin( columns, rows )
	// returns status (0 when the library reports none)
	if( argn == 2)
	{
		WRLcdStatus status;
		status.value = 0;
		stackTop->i = (WR_ARDUINO_LCD.begin( stackTop[-2].asInt(), stackTop[-1].asInt() ), status).value;
	}
}

//------------------------------------------------------------------------------
void wr_lcd_setCursor( WRValue* stackTop, const int argn, WRContext* c)
{
	// lcd::setCursor( column, row )
	if( argn == 2 )
	{
		WR_ARDUINO_LCD.setCursor( stackTop[-2].asInt(), stackTop[-1].asInt() );
	}
}

//------------------------------------------------------------------------------
void wr_lcd_print( WRValue* stackTop, const int argn, WRContext* c)
{
	// lcd::print( arg )
	// returns number of chars printed
	if( argn == 1 )
	{
		char buf[61];
		stackTop->i = (int)WR_ARDUINO_LCD.print( stackTop[-1].asString(buf, sizeof(buf)) );
	}
}

#endif

//------------------------------------------------------------------------------
void wr_wire_begin( WRValue* stackTop, const int argn, WRContext* c)
{
	Wire.begin();
}

//------------------------------------------------------------------------------
void wr_wire_beginTransmission( WRValue* stackTop, const int argn, WRContext* c)
{
	if( argn == 1)
	{
		Wire.beginTransmission( stackTop[-1].asInt() );
	}
}

//------------------------------------------------------------------------------
// wire::write( value [, count] )
// queues bytes of the transmission: an int is one byte, a string its
// characters, at most 'count' of them when 'count' is given. Returns the
// number of bytes queued
void wr_wire_write( WRValue* stackTop, const int argn, WRContext* c)
{
	if( argn != 1 && argn != 2 )
	{
		return;
	}

	WRValue* args = stackTop - argn;
	unsigned int len = 1;
	const uint8_t* data = (const uint8_t*)args[0].array( &len, SV_CHAR );
	if ( argn == 2 )
	{
		const int count = args[1].asInt();
		if ( count <= 0 )
		{
			return;
		}
		if ( (unsigned int)count < len )
		{
			len = (unsigned int)count;
		}
	}

	if ( data )
	{
		stackTop->i = len ? (int)Wire.write( data, len ) : 0;
	}
	else
	{
		stackTop->i = (int)Wire.write( (uint8_t)args[0].asInt() );
	}
}

//------------------------------------------------------------------------------
void wr_wire_endTransmission( WRValue* stackTop, const int argn, WRContext* c)
{
	if( argn == 0)
	{
		stackTop->i = Wire.endTransmission();
	}
	else if( argn == 1)
	{
		stackTop->i = Wire.endTransmission( stackTop[-1].asInt() );
	}
}

//------------------------------------------------------------------------------
void wr_wire_requestFrom( WRValue* stackTop, const int argn, WRContext* c)
{
	// wire::requestFrom( address, bytes )
	if( argn == 2)
	{
		stackTop->i = Wire.requestFrom( stackTop[-2].asInt(), stackTop[-1].asInt() );
	}
}

//------------------------------------------------------------------------------
void wr_wire_available( WRValue* stackTop, const int argn, WRContext* c)
{
	stackTop->i = Wire.available();
}

//------------------------------------------------------------------------------
void wr_wire_read( WRValue* stackTop, const int argn, WRContext* c)
{
	stackTop->i = Wire.read();
}

//------------------------------------------------------------------------------
void wr_loadArduinoWireLib( WRState* w )
{
	wr_registerLibraryFunction( w, "wire::begin", wr_wire_begin);
	wr_registerLibraryFunction( w, "wire::beginTransmission", wr_wire_beginTransmission);
	wr_registerLibraryFunction( w, "wire::write", wr_wire_write);
	wr_registerLibraryFunction( w, "wire::endTransmission", wr_wire_endTransmission);
	wr_registerLibraryFunction( w, "wire::requestFrom", wr_wire_requestFrom);
	wr_registerLibraryFunction( w, "wire::available", wr_wire_available);
	wr_registerLibraryFunction( w, "wire::read", wr_wire_read);
}

//------------------------------------------------------------------------------
void wr_loadArduinoSTDLib( WRState* w )
{
	wr_registerLibraryFunction( w, "std::delay", wr_std_delay );
}

//------------------------------------------------------------------------------
void wr_loadArduinoIOLib( WRState* w )
{
	wr_registerLibraryFunction( w, "io::pinMode", wr_io_pinMode );
	wr_registerLibraryFunction( w, "io::digitalWrite", wr_io_digitalWrite );
	wr_registerLibraryFunction( w, "io::analogRead", wr_io_analogRead );
}

//------------------------------------------------------------------------------
// registers nothing without a display (WR_ARDUINO_LCD), so a script calling
// lcd:: fails with WR_ERR_lib_function_not_found instead of doing nothing
void wr_loadArduinoLCDLib( WRState* w )
{
#ifdef WR_ARDUINO_LCD
	wr_registerLibraryFunction( w, "lcd::begin", wr_lcd_begin );
	wr_registerLibraryFunction( w, "lcd::setCursor", wr_lcd_setCursor );
	wr_registerLibraryFunction( w, "lcd::print", wr_lcd_print );
#endif
}

//------------------------------------------------------------------------------
// declared in wrench.h
void wr_loadAllArduinoLibs( WRState* w )
{
	wr_loadArduinoLCDLib( w );
	wr_loadArduinoIOLib( w );
	wr_loadArduinoSTDLib( w );
	wr_loadArduinoWireLib( w );
}

// the old name, kept for sketches that call it directly
void wr_loadArduinoLib( WRState* w )
{
	wr_loadAllArduinoLibs( w );
}

#endif
