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

#if (defined(WRENCH_INCLUDE_DEBUG_CODE) || defined(WRENCH_INCLUDE_DEBUG_CLIENT)) \
		&& (defined(WRENCH_DEBUG_HAS_TCP) || defined(WRENCH_DEBUG_HAS_SERIAL))

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment( lib, "Ws2_32.lib" )
#else
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>
#endif

static uint32_t wr_debugHostMillis()
{
#if defined(_WIN32)
	return (uint32_t)GetTickCount64();
#else
	struct timespec ts;
	clock_gettime( CLOCK_MONOTONIC, &ts );
	return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
#endif
}

static void wr_debugHostSleep( int ms )
{
#if defined(_WIN32)
	Sleep( (DWORD)ms );
#else
	usleep( (useconds_t)ms * 1000u );
#endif
}

#ifdef WRENCH_DEBUG_HAS_TCP

#if defined(_WIN32)
typedef SOCKET wr_socket_t;
#define WR_SOCKET_INVALID ((intptr_t)INVALID_SOCKET)
static void wr_closeSocket( wr_socket_t s ) { closesocket( s ); }
static void wr_socketStartup()
{
	static bool started = false;
	if ( !started )
	{
		WSADATA data;
		WSAStartup( MAKEWORD(2, 2), &data );
		started = true;
	}
}
static bool wr_socketWouldBlock()
{
	int e = WSAGetLastError();
	return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS;
}
static void wr_socketNonBlocking( wr_socket_t s )
{
	u_long on = 1;
	ioctlsocket( s, FIONBIO, &on );
}
#else
typedef int wr_socket_t;
#define WR_SOCKET_INVALID ((intptr_t)-1)
static void wr_closeSocket( wr_socket_t s ) { ::close( s ); }
static void wr_socketStartup()
{
	static bool started = false;
	if ( !started )
	{
		// a peer closing mid-write must not kill the process
		signal( SIGPIPE, SIG_IGN );
		started = true;
	}
}
static bool wr_socketWouldBlock()
{
	return errno == EWOULDBLOCK || errno == EAGAIN || errno == EINPROGRESS || errno == EINTR;
}
static void wr_socketNonBlocking( wr_socket_t s )
{
	int flags = fcntl( s, F_GETFL, 0 );
	fcntl( s, F_SETFL, flags | O_NONBLOCK );
}
#endif

static void wr_socketNoDelay( wr_socket_t s )
{
	int on = 1;
	setsockopt( s, IPPROTO_TCP, TCP_NODELAY, (const char*)&on, sizeof(on) );
}

// wait until the socket can be read (or written), -1 on error. Waiting for
// write also watches errors: Windows reports a failed connect there only
static int wr_socketWait( wr_socket_t s, bool forWrite, int timeoutMs )
{
	fd_set set;
	FD_ZERO( &set );
	FD_SET( s, &set );
	fd_set errors;
	FD_ZERO( &errors );
	FD_SET( s, &errors );
	struct timeval tv;
	tv.tv_sec = timeoutMs / 1000;
	tv.tv_usec = (timeoutMs % 1000) * 1000;
	int r = select( (int)s + 1, forWrite ? 0 : &set, forWrite ? &set : 0, forWrite ? &errors : 0, &tv );
	return r;
}

static bool wr_socketWriteAll( intptr_t& sock, const uint8_t* data, uint32_t len )
{
	if ( sock == WR_SOCKET_INVALID )
	{
		return false;
	}

	wr_socket_t s = (wr_socket_t)sock;
	uint32_t start = wr_debugHostMillis();
	while ( len )
	{
#if defined(_WIN32)
		int sent = send( s, (const char*)data, (int)len, 0 );
#else
		int sent = (int)send( s, data, len, MSG_NOSIGNAL );
#endif
		if ( sent > 0 )
		{
			data += sent;
			len -= (uint32_t)sent;
			continue;
		}

		if ( sent < 0 && wr_socketWouldBlock() && (wr_debugHostMillis() - start) < 10000 )
		{
			wr_socketWait( s, true, 50 );
			continue;
		}

		wr_closeSocket( s );
		sock = WR_SOCKET_INVALID;
		return false;
	}
	return true;
}

static int wr_socketReadSome( intptr_t& sock, uint8_t* data, uint32_t maxLen )
{
	if ( sock == WR_SOCKET_INVALID )
	{
		return -1;
	}

	wr_socket_t s = (wr_socket_t)sock;
#if defined(_WIN32)
	int got = recv( s, (char*)data, (int)maxLen, 0 );
#else
	int got = (int)recv( s, data, maxLen, 0 );
#endif
	if ( got > 0 )
	{
		return got;
	}

	if ( got < 0 && wr_socketWouldBlock() )
	{
		return 0;
	}

	// orderly shutdown or hard error
	wr_closeSocket( s );
	sock = WR_SOCKET_INVALID;
	return -1;
}

WrenchDebugTcpServerTransport::WrenchDebugTcpServerTransport()
	: m_listen( WR_SOCKET_INVALID ), m_client( WR_SOCKET_INVALID ), m_lost( false )
{
	wr_socketStartup();
}

WrenchDebugTcpServerTransport::~WrenchDebugTcpServerTransport()
{
	if ( m_client != WR_SOCKET_INVALID )
	{
		wr_closeSocket( (wr_socket_t)m_client );
	}
	if ( m_listen != WR_SOCKET_INVALID )
	{
		wr_closeSocket( (wr_socket_t)m_listen );
	}
}

bool WrenchDebugTcpServerTransport::listen( int port, bool loopbackOnly )
{
	wr_socket_t s = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );
	if ( (intptr_t)s == WR_SOCKET_INVALID )
	{
		return false;
	}

	int on = 1;
#if defined(_WIN32)
	// SO_REUSEADDR would let a second server bind the same port on Windows
	setsockopt( s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char*)&on, sizeof(on) );
#else
	setsockopt( s, SOL_SOCKET, SO_REUSEADDR, (const char*)&on, sizeof(on) );
#endif

	struct sockaddr_in addr;
	memset( &addr, 0, sizeof(addr) );
	addr.sin_family = AF_INET;
	addr.sin_port = htons( (unsigned short)port );
	addr.sin_addr.s_addr = htonl( loopbackOnly ? INADDR_LOOPBACK : INADDR_ANY );

	if ( bind(s, (struct sockaddr*)&addr, sizeof(addr)) != 0 || ::listen(s, 1) != 0 )
	{
		wr_closeSocket( s );
		return false;
	}

	wr_socketNonBlocking( s );
	m_listen = (intptr_t)s;
	return true;
}

bool WrenchDebugTcpServerTransport::waitForClient( int timeoutMs )
{
	if ( m_client != WR_SOCKET_INVALID )
	{
		return true;
	}
	if ( m_listen == WR_SOCKET_INVALID )
	{
		return false;
	}

	if ( timeoutMs > 0 && wr_socketWait((wr_socket_t)m_listen, false, timeoutMs) <= 0 )
	{
		return false;
	}

	wr_socket_t c = accept( (wr_socket_t)m_listen, 0, 0 );
	if ( (intptr_t)c == WR_SOCKET_INVALID )
	{
		return false;
	}

	wr_socketNonBlocking( c );
	wr_socketNoDelay( c );
	m_client = (intptr_t)c;
	return true;
}

bool WrenchDebugTcpServerTransport::write( const uint8_t* data, uint32_t len )
{
	bool hadClient = m_client != WR_SOCKET_INVALID;
	bool ok = wr_socketWriteAll( m_client, data, len );
	if ( hadClient && m_client == WR_SOCKET_INVALID )
	{
		m_lost = true; // read() reports it before taking the next client
	}
	return ok;
}

int WrenchDebugTcpServerTransport::read( uint8_t* data, uint32_t maxLen )
{
	if ( m_lost )
	{
		m_lost = false;
		return -1;
	}
	if ( m_client == WR_SOCKET_INVALID )
	{
		// between clients: pick up the next one, report "no data" meanwhile
		waitForClient( 0 );
		return 0;
	}
	return wr_socketReadSome( m_client, data, maxLen );
}

uint32_t WrenchDebugTcpServerTransport::millis()
{
	return wr_debugHostMillis();
}

void WrenchDebugTcpServerTransport::idle()
{
	if ( m_client != WR_SOCKET_INVALID )
	{
		wr_socketWait( (wr_socket_t)m_client, false, 10 );
	}
	else
	{
		wr_debugHostSleep( 10 );
	}
}

bool WrenchDebugTcpServerTransport::connected()
{
	return m_client != WR_SOCKET_INVALID;
}

WrenchDebugTcpClientTransport::WrenchDebugTcpClientTransport()
	: m_socket( WR_SOCKET_INVALID )
{
	wr_socketStartup();
}

WrenchDebugTcpClientTransport::~WrenchDebugTcpClientTransport()
{
	close();
}

void WrenchDebugTcpClientTransport::close()
{
	if ( m_socket != WR_SOCKET_INVALID )
	{
		wr_closeSocket( (wr_socket_t)m_socket );
		m_socket = WR_SOCKET_INVALID;
	}
}

bool WrenchDebugTcpClientTransport::connect( const char* host, int port, int timeoutMs )
{
	close();

	struct addrinfo hints;
	memset( &hints, 0, sizeof(hints) );
	hints.ai_family = AF_UNSPEC; // IPv4 or IPv6, whatever the name resolves to
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	char portText[16];
	snprintf( portText, sizeof(portText), "%d", port );

	struct addrinfo* result = 0;
	if ( getaddrinfo(host, portText, &hints, &result) != 0 || !result )
	{
		return false;
	}

	bool ok = false;
	for ( struct addrinfo* a = result; a && !ok; a = a->ai_next )
	{
		wr_socket_t s = socket( a->ai_family, a->ai_socktype, a->ai_protocol );
		if ( (intptr_t)s == WR_SOCKET_INVALID )
		{
			continue;
		}

		wr_socketNonBlocking( s );
		int r = ::connect( s, a->ai_addr, (int)a->ai_addrlen );
		if ( r != 0 && wr_socketWouldBlock() )
		{
			if ( wr_socketWait(s, true, timeoutMs) > 0 )
			{
				int err = 0;
				socklen_t errLen = sizeof(err);
				getsockopt( s, SOL_SOCKET, SO_ERROR, (char*)&err, &errLen );
				r = err ? -1 : 0;
			}
		}

		if ( r == 0 )
		{
			wr_socketNoDelay( s );
			m_socket = (intptr_t)s;
			ok = true;
		}
		else
		{
			wr_closeSocket( s );
		}
	}

	freeaddrinfo( result );
	return ok;
}

bool WrenchDebugTcpClientTransport::write( const uint8_t* data, uint32_t len )
{
	return wr_socketWriteAll( m_socket, data, len );
}

int WrenchDebugTcpClientTransport::read( uint8_t* data, uint32_t maxLen )
{
	return wr_socketReadSome( m_socket, data, maxLen );
}

uint32_t WrenchDebugTcpClientTransport::millis()
{
	return wr_debugHostMillis();
}

void WrenchDebugTcpClientTransport::idle()
{
	if ( m_socket != WR_SOCKET_INVALID )
	{
		wr_socketWait( (wr_socket_t)m_socket, false, 10 );
	}
	else
	{
		wr_debugHostSleep( 10 );
	}
}

bool WrenchDebugTcpClientTransport::connected()
{
	return m_socket != WR_SOCKET_INVALID;
}

#endif

#ifdef WRENCH_DEBUG_HAS_SERIAL

#if defined(_WIN32)
#define WR_SERIAL_INVALID ((intptr_t)INVALID_HANDLE_VALUE)
#else
#define WR_SERIAL_INVALID ((intptr_t)-1)
#endif

WrenchDebugSerialTransport::WrenchDebugSerialTransport()
	: m_handle( WR_SERIAL_INVALID )
{
}

WrenchDebugSerialTransport::~WrenchDebugSerialTransport()
{
	close();
}

void WrenchDebugSerialTransport::close()
{
	if ( m_handle == WR_SERIAL_INVALID )
	{
		return;
	}
#if defined(_WIN32)
	CloseHandle( (HANDLE)m_handle );
#else
	::close( (int)m_handle );
#endif
	m_handle = WR_SERIAL_INVALID;
}

#if !defined(_WIN32)
// termios only knows fixed rates; an unknown one fails the open instead of
// silently running at another speed
static bool wr_baudConstant( int baud, speed_t& speed )
{
	struct Rate
	{
		int baud;
		speed_t speed;
	};
	static const Rate rates[] =
	{
		{ 1200, B1200 }, { 2400, B2400 }, { 4800, B4800 }, { 9600, B9600 }, { 19200, B19200 },
		{ 38400, B38400 }, { 57600, B57600 }, { 115200, B115200 },
#ifdef B230400
		{ 230400, B230400 },
#endif
#ifdef B460800
		{ 460800, B460800 },
#endif
#ifdef B500000
		{ 500000, B500000 },
#endif
#ifdef B576000
		{ 576000, B576000 },
#endif
#ifdef B921600
		{ 921600, B921600 },
#endif
#ifdef B1000000
		{ 1000000, B1000000 },
#endif
#ifdef B1152000
		{ 1152000, B1152000 },
#endif
#ifdef B1500000
		{ 1500000, B1500000 },
#endif
#ifdef B2000000
		{ 2000000, B2000000 },
#endif
#ifdef B2500000
		{ 2500000, B2500000 },
#endif
#ifdef B3000000
		{ 3000000, B3000000 },
#endif
#ifdef B3500000
		{ 3500000, B3500000 },
#endif
#ifdef B4000000
		{ 4000000, B4000000 },
#endif
	};
	for ( unsigned int i = 0; i < sizeof(rates) / sizeof(rates[0]); ++i )
	{
		if ( rates[i].baud == baud )
		{
			speed = rates[i].speed;
			return true;
		}
	}
	return false;
}
#endif

bool WrenchDebugSerialTransport::open( const char* port, int baud )
{
	close();
	if ( !port || !port[0] )
	{
		return false;
	}

#if defined(_WIN32)
	// COM10 and above only open through the device namespace
	char name[64];
	if ( strncmp(port, "\\\\.\\", 4) == 0 )
	{
		snprintf( name, sizeof(name), "%s", port );
	}
	else
	{
		snprintf( name, sizeof(name), "\\\\.\\%s", port );
	}

	HANDLE h = CreateFileA( name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL );
	if ( h == INVALID_HANDLE_VALUE )
	{
		return false;
	}

	SetupComm( h, 8192, 8192 );
	PurgeComm( h, PURGE_RXABORT | PURGE_TXABORT | PURGE_RXCLEAR | PURGE_TXCLEAR );

	DCB dcb;
	memset( &dcb, 0, sizeof(dcb) );
	dcb.DCBlength = sizeof(dcb);
	if ( !GetCommState(h, &dcb) )
	{
		CloseHandle( h );
		return false;
	}

	dcb.BaudRate = (DWORD)baud;
	dcb.ByteSize = 8;
	dcb.Parity = NOPARITY;
	dcb.StopBits = ONESTOPBIT;
	dcb.fBinary = TRUE;
	dcb.fParity = FALSE;
	dcb.fOutxCtsFlow = FALSE;
	dcb.fOutxDsrFlow = FALSE;
	// DTR held high: many USB-UART bridges and native USB CDC devices only
	// send data while DTR is asserted
	dcb.fDtrControl = DTR_CONTROL_ENABLE;
	dcb.fDsrSensitivity = FALSE;
	dcb.fOutX = FALSE;
	dcb.fInX = FALSE;
	dcb.fRtsControl = RTS_CONTROL_ENABLE;
	dcb.fAbortOnError = FALSE;
	if ( !SetCommState(h, &dcb) )
	{
		CloseHandle( h );
		return false;
	}

	// reads return immediately with whatever is buffered
	COMMTIMEOUTS timeouts;
	memset( &timeouts, 0, sizeof(timeouts) );
	timeouts.ReadIntervalTimeout = MAXDWORD;
	timeouts.ReadTotalTimeoutConstant = 0;
	timeouts.ReadTotalTimeoutMultiplier = 0;
	timeouts.WriteTotalTimeoutConstant = 2000;
	timeouts.WriteTotalTimeoutMultiplier = 10;
	SetCommTimeouts( h, &timeouts );

	m_handle = (intptr_t)h;
	return true;
#else
	speed_t speed;
	if ( !wr_baudConstant(baud, speed) )
	{
		return false;
	}

	int fd = ::open( port, O_RDWR | O_NOCTTY | O_NONBLOCK );
	if ( fd < 0 )
	{
		return false;
	}

	struct termios tio;
	if ( tcgetattr(fd, &tio) != 0 )
	{
		::close( fd );
		return false;
	}

	cfmakeraw( &tio );
	tio.c_cflag |= CLOCAL | CREAD;
	tio.c_cflag &= ~CRTSCTS;
	tio.c_cc[VMIN] = 0;
	tio.c_cc[VTIME] = 0;
	cfsetispeed( &tio, speed );
	cfsetospeed( &tio, speed );
	if ( tcsetattr(fd, TCSANOW, &tio) != 0 )
	{
		::close( fd );
		return false;
	}
	tcflush( fd, TCIOFLUSH );

	m_handle = (intptr_t)fd;
	return true;
#endif
}

bool WrenchDebugSerialTransport::write( const uint8_t* data, uint32_t len )
{
	if ( m_handle == WR_SERIAL_INVALID )
	{
		return false;
	}

#if defined(_WIN32)
	while ( len )
	{
		DWORD written = 0;
		if ( !WriteFile((HANDLE)m_handle, data, len, &written, NULL) || written == 0 )
		{
			return false;
		}
		data += written;
		len -= written;
	}
	return true;
#else
	uint32_t start = wr_debugHostMillis();
	while ( len )
	{
		ssize_t n = ::write( (int)m_handle, data, len );
		if ( n > 0 )
		{
			data += n;
			len -= (uint32_t)n;
		}
		else if ( n < 0 && (errno == EAGAIN || errno == EINTR) && (wr_debugHostMillis() - start) < 5000 )
		{
			wr_debugHostSleep( 1 );
		}
		else
		{
			return false;
		}
	}
	return true;
#endif
}

int WrenchDebugSerialTransport::read( uint8_t* data, uint32_t maxLen )
{
	if ( m_handle == WR_SERIAL_INVALID )
	{
		return -1;
	}

#if defined(_WIN32)
	DWORD got = 0;
	if ( !ReadFile((HANDLE)m_handle, data, maxLen, &got, NULL) )
	{
		return -1;
	}
	return (int)got;
#else
	ssize_t n = ::read( (int)m_handle, data, maxLen );
	if ( n < 0 )
	{
		return (errno == EAGAIN || errno == EINTR) ? 0 : -1;
	}
	return (int)n;
#endif
}

uint32_t WrenchDebugSerialTransport::millis()
{
	return wr_debugHostMillis();
}

void WrenchDebugSerialTransport::idle()
{
	wr_debugHostSleep( 2 );
}

bool WrenchDebugSerialTransport::connected()
{
	return m_handle != WR_SERIAL_INVALID;
}

#endif

#endif
