#ifndef _DEBUGGER_H
#define _DEBUGGER_H
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

#ifdef WRENCH_INCLUDE_DEBUG_CLIENT

// "wrench dbg": interactive console debugger.
// sourcePath: a .w file, or a .bin compiled with -debugsource (its source is
// embedded). Null with attach reads program and source from the target
// target: "local", "tcp:<host>:<port>", "serial:<port>[:<baud>]"
// attach: do not upload or launch, debug what the target already runs
// commands come from 'in' (stdin normally, a script for tests)
// projectPath: wrench.json describing the host application, null to use
// the nearest one, "none" for no project
int wr_debugCliMain( const char* sourcePath, const char* target, bool attach, FILE* in = 0, const char* projectPath = 0 );

#endif

#endif
