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

#if defined(WRENCH_LITTLEFS_FILE_IO) || defined(WRENCH_SPIFFS_FILE_IO)
#include <new>
#include <Arduino.h>
#include <FS.h>
#ifdef WRENCH_LITTLEFS_FILE_IO
#include <LittleFS.h>
#define FILE_OBJ LittleFS
#endif

#ifdef WRENCH_SPIFFS_FILE_IO
#include <SPIFFS.h>
#define FILE_OBJ SPIFFS
#endif

// where str::printf output goes: any Arduino Print object (Serial1, a
// display, ...) can be configured instead of the default Serial
#ifndef WRENCH_FS_STDOUT
#define WRENCH_FS_STDOUT Serial
#endif

//------------------------------------------------------------------------------
// io::open flags, the values Linux uses so a script means the same thing as
// on the desktop backends; the access mode is the low two bits
enum WRFSOpenFlags
{
	WRFS_O_RDONLY = 0x0,
	WRFS_O_WRONLY = 0x1,
	WRFS_O_RDWR = 0x2,
	WRFS_O_ACCMODE = 0x3,
	WRFS_O_CREAT = 0x40,
	WRFS_O_EXCL = 0x80,
	WRFS_O_TRUNC = 0x200,
	WRFS_O_APPEND = 0x400,
};

//------------------------------------------------------------------------------
enum WRFSSeek
{
	LFS_SET = 0,
	LFS_CUR,
	LFS_END,
};

//------------------------------------------------------------------------------
// holds a C++ object, so it is always constructed with placement new and
// destroyed with wr_freeFile(), never just g_malloc'ed / g_free'd. Each
// open file belongs to the state that opened it: scripts only see their
// own descriptors and a state's cleanup only closes its own files
struct WRFSFile
{
	File file;
	WRState* owner;
	int fd;
	WRFSFile* next;
};

static WRFSFile* g_OpenFiles = 0;

static void wr_freeFile( WRFSFile* entry )
{
	entry->file.close();
	entry->~WRFSFile();
	g_free( entry );
}

// lowest descriptor not in use; they start at 3 like on the desktop
// (0 to 2 being the standard streams) so a valid one is never 0 or -1
static int wr_fsNewFd()
{
	for( int fd = 3; ; ++fd )
	{
		WRFSFile* f = g_OpenFiles;
		while( f && f->fd != fd )
		{
			f = f->next;
		}

		if ( !f )
		{
			return fd;
		}
	}
}

//------------------------------------------------------------------------------
void wr_stdout( const char* data, const int size )
{
	if ( size > 0 )
	{
		WRENCH_FS_STDOUT.write( (const uint8_t*)data, (size_t)size );
	}
}

//------------------------------------------------------------------------------
static WRFSFile* wr_fsFind( WRState* w, const WRValue& handle )
{
	const int fd = handle.asInt();
	for( WRFSFile* f = g_OpenFiles; f; f = f->next )
	{
		if ( f->fd == fd && f->owner == w )
		{
			return f;
		}
	}

	return 0;
}

//------------------------------------------------------------------------------
void wr_read_file( WRValue* stackTop, const int argn, WRContext* c )
{
	stackTop->init(); // 0 when the file can not be read

	if ( argn != 1 )
	{
		return;
	}

	WRValue* arg = stackTop - 1;
	WRValue::MallocStrScoped fileName( *arg );
	if ( !fileName || !FILE_OBJ.exists(fileName) )
	{
		return;
	}

	File file = FILE_OBJ.open( fileName, "r" );
	if ( !file )
	{
		return;
	}

	if ( !file.isDirectory() )
	{
		const size_t size = file.size();
		stackTop->va = c->getSVA( (int)size, SV_CHAR, false );
#ifdef WRENCH_HANDLE_MALLOC_FAIL
		if ( !stackTop->va )
		{
			stackTop->init();
			file.close();
			return;
		}
#endif
		stackTop->p2 = INIT_AS_ARRAY;

		if ( size && file.read( (uint8_t*)stackTop->va->m_Cdata, size ) != size )
		{
			stackTop->init();
		}
	}

	file.close();
}

//------------------------------------------------------------------------------
void wr_write_file( WRValue* stackTop, const int argn, WRContext* c )
{
	stackTop->init(); // 1 on success, 0 on failure

	if ( argn != 2 )
	{
		return;
	}

	WRValue::MallocStrScoped fileName( *(stackTop - 2) );
	if ( !fileName )
	{
		return;
	}

	unsigned int len;
	const char* data = (char*)((stackTop - 1)->array(&len));
	if ( !data )
	{
		return;
	}

	File file = FILE_OBJ.open( fileName, "w" );
	if ( !file )
	{
		return;
	}

	const size_t written = len ? file.write( (const uint8_t *)data, len ) : 0;
	stackTop->i = (written == len) ? 1 : 0;

	file.close();
}

//------------------------------------------------------------------------------
void wr_delete_file( WRValue* stackTop, const int argn, WRContext* c )
{
	if ( argn == 1 )
	{
		WRValue::MallocStrScoped fileName( *(stackTop - 1) );
		if ( fileName )
		{
			FILE_OBJ.remove( fileName );
		}
	}
}

//------------------------------------------------------------------------------
void wr_getline( WRValue* stackTop, const int argn, WRContext* c )
{
	// there is no standard input with a file system backend
}

// maps io::open flags to an Arduino FS (fopen style) mode, or 0 when the
// open must fail. "r", "r+", "w", "w+", "a" and "a+" are what the ESP32 and
// ESP8266 file systems accept
static const char* wr_fsOpenMode( const char* fileName, const int flags )
{
	const int access = flags & WRFS_O_ACCMODE;
	if ( access == WRFS_O_ACCMODE )
	{
		return 0; // not a valid access mode
	}

	const bool exists = FILE_OBJ.exists( fileName );
	if ( exists )
	{
		if ( (flags & WRFS_O_CREAT) && (flags & WRFS_O_EXCL) )
		{
			return 0;
		}
	}
	else if ( !(flags & WRFS_O_CREAT) )
	{
		return 0;
	}

	if ( access == WRFS_O_RDONLY )
	{
		if ( !exists )
		{
			// read only but O_CREAT: create it empty, then read it
			File created = FILE_OBJ.open( fileName, "w" );
			if ( !created )
			{
				return 0;
			}
			created.close();
		}

		return "r";
	}

	const bool readToo = (access == WRFS_O_RDWR);

	if ( flags & WRFS_O_APPEND )
	{
		return readToo ? "a+" : "a";
	}

	if ( (flags & WRFS_O_TRUNC) || !exists )
	{
		return readToo ? "w+" : "w"; // creates, or empties the file
	}

	// keep the contents: "r+" is the only mode that writes without
	// truncating or appending, so write only access uses it too
	return "r+";
}

//------------------------------------------------------------------------------
void wr_ioOpen( WRValue* stackTop, const int argn, WRContext* c )
{
	stackTop->init();
	stackTop->i = -1;

	if ( !argn )
	{
		return;
	}

	WRValue* args = stackTop - argn;

	const int flags = (argn > 1) ? args[1].asInt() : (WRFS_O_RDWR | WRFS_O_CREAT);

	// the script string is not null terminated, make a proper copy
	WRValue::MallocStrScoped fileName( *args );
	if ( !fileName )
	{
		return;
	}

	const char* openMode = wr_fsOpenMode( fileName, flags );
	if ( !openMode )
	{
		return;
	}

	void* mem = g_malloc( sizeof(WRFSFile) );
	if ( !mem )
	{
#ifdef WRENCH_HANDLE_MALLOC_FAIL
		g_mallocFailed = true;
#endif
		return;
	}

	WRFSFile* entry = new (mem) WRFSFile();
	entry->next = 0;
	entry->owner = c->w;
	entry->file = FILE_OBJ.open( fileName, openMode );

	if ( !entry->file || entry->file.isDirectory() )
	{
		wr_freeFile( entry );
		return;
	}

	entry->fd = wr_fsNewFd();
	entry->next = g_OpenFiles;
	g_OpenFiles = entry;
	stackTop->i = entry->fd;
}

//------------------------------------------------------------------------------
void wr_ioClose( WRValue* stackTop, const int argn, WRContext* c )
{
	stackTop->init();
	stackTop->i = -1;

	if ( !argn )
	{
		return;
	}

	const int fd = (stackTop - argn)->asInt();

	WRFSFile* prev = 0;
	for( WRFSFile* cur = g_OpenFiles; cur; cur = cur->next )
	{
		if ( cur->fd == fd && cur->owner == c->w )
		{
			if ( !prev )
			{
				g_OpenFiles = cur->next;
			}
			else
			{
				prev->next = cur->next;
			}

			wr_freeFile( cur );
			stackTop->i = 0;
			return;
		}

		prev = cur;
	}
}

//------------------------------------------------------------------------------
void wr_ioRead( WRValue* stackTop, const int argn, WRContext* c )
{
	if ( argn != 2 )
	{
		return;
	}

	WRValue* args = stackTop - argn;

	int toRead = args[1].asInt();
	if ( toRead <= 0 )
	{
		return;
	}

	// an unknown descriptor reads nothing, like a failed read() on the
	// desktop
	WRFSFile* fd = wr_fsFind( c->w, args[0] );

	// never allocate more than the file could still deliver
	const int remaining = fd ? (int)(fd->file.size() - fd->file.position()) : 0;
	if ( toRead > remaining )
	{
		toRead = (remaining > 0) ? remaining : 0;
	}

	stackTop->va = c->getSVA( toRead, SV_CHAR, false );
#ifdef WRENCH_HANDLE_MALLOC_FAIL
	if ( !stackTop->va )
	{
		stackTop->init();
		return;
	}
#endif
	stackTop->p2 = INIT_AS_ARRAY;

	int result = toRead ? (int)fd->file.read( (uint8_t*)stackTop->va->m_Cdata, toRead ) : 0;

	stackTop->va->m_size = (result > 0) ? result : 0;
}

//------------------------------------------------------------------------------
void wr_ioWrite( WRValue* stackTop, const int argn, WRContext* c )
{
	stackTop->init();
	stackTop->i = -1;

	if ( argn < 2 )
	{
		return;
	}

	WRValue* args = stackTop - argn;

	WRFSFile* fd = wr_fsFind( c->w, args[0] );
	if ( !fd )
	{
		return;
	}

	WRValue& data = args[1].deref();
	if ( IS_ARRAY(data.xtype) )
	{
		uint32_t size = data.va->m_size;
		if ( argn > 2 )
		{
			size = args[2].asInt();
		}

		if ( data.va->m_type == SV_CHAR )
		{
			stackTop->i = (int)fd->file.write( data.va->m_Cdata, (size > data.va->m_size) ? data.va->m_size : size );
		}
		else if ( data.va->m_type == SV_VALUE )
		{
			// .. does this even make sense?
		}
	}
	else if ( IS_RAW_ARRAY(data.xtype) )
	{
		// a raw array value holds the data pointer and its size itself
		const uint32_t rawSize = EX_RAW_ARRAY_SIZE_FROM_P2(data.p2);
		uint32_t size = rawSize;
		if ( argn > 2 )
		{
			size = args[2].asInt();
		}

		stackTop->i = (int)fd->file.write( (const uint8_t *)data.c, (size > rawSize) ? rawSize : size );
	}
}

//------------------------------------------------------------------------------
// returns the new offset from the start, or -1
void wr_ioSeek( WRValue* stackTop, const int argn, WRContext* c )
{
	stackTop->init();
	stackTop->i = -1;

	if ( argn < 2 )
	{
		return;
	}

	WRValue* args = stackTop - argn;

	WRFSFile* fd = wr_fsFind( c->w, args[0] );
	if ( !fd )
	{
		return;
	}

	int offset = args[1].asInt();
	const int whence = (argn > 2) ? args[2].asInt() : LFS_SET;

	if ( whence == LFS_END )
	{
		offset += (int)fd->file.size();
	}
	else if ( whence == LFS_CUR )
	{
		offset += (int)fd->file.position();
	}
	else if ( whence != LFS_SET )
	{
		return;
	}

	if ( offset < 0 || !fd->file.seek((uint32_t)offset, SeekSet) )
	{
		return;
	}

	stackTop->i = (int)fd->file.position();
}

//------------------------------------------------------------------------------
void wr_ioFSync(WRValue* stackTop, const int argn, WRContext* c)
{
	stackTop->init();
	stackTop->i = -1;

	if ( argn == 1 )
	{
		WRFSFile* fd = wr_fsFind( c->w, *(stackTop - 1) );
		if ( fd )
		{
			fd->file.flush();
			stackTop->i = 0;
		}
	}
}

//------------------------------------------------------------------------------
// closes only the files the state being destroyed opened
void wr_ioCleanupFunction( WRState* w, void* param )
{
	WRFSFile* prev = 0;
	WRFSFile* cur = g_OpenFiles;
	while( cur )
	{
		WRFSFile* next = cur->next;
		if ( cur->owner == w )
		{
			if ( !prev )
			{
				g_OpenFiles = next;
			}
			else
			{
				prev->next = next;
			}
			wr_freeFile( cur );
		}
		else
		{
			prev = cur;
		}
		cur = next;
	}
}

//------------------------------------------------------------------------------
void wr_ioPushConstants( WRState* w )
{
	wr_registerLibraryConstant( w, "io::O_RDONLY", (int32_t)WRFS_O_RDONLY );
	wr_registerLibraryConstant( w, "io::O_RDWR", (int32_t)WRFS_O_RDWR );
	wr_registerLibraryConstant( w, "io::O_APPEND", (int32_t)WRFS_O_APPEND );
	wr_registerLibraryConstant( w, "io::O_CREAT", (int32_t)WRFS_O_CREAT );
	wr_registerLibraryConstant( w, "io::O_TRUNC", (int32_t)WRFS_O_TRUNC );
	wr_registerLibraryConstant( w, "io::O_EXCL", (int32_t)WRFS_O_EXCL );

	wr_registerLibraryConstant( w, "io::SEEK_SET", (int32_t)LFS_SET );
	wr_registerLibraryConstant( w, "io::SEEK_CUR", (int32_t)LFS_CUR );
	wr_registerLibraryConstant( w, "io::SEEK_END", (int32_t)LFS_END );

	FILE_OBJ.begin();

	wr_addLibraryCleanupFunction( w, wr_ioCleanupFunction, 0 );

}

#endif
