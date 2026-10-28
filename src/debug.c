#include "common.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <stdint.h>


#ifdef WIN32

#include <windows.h>
#include <sys/types.h>

#else

#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>
#include <errno.h>
#include <sys/time.h>
#include <time.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <errno.h>

#endif

#include "debug.h"
#include "debugcore.h"	/* TASK 3.3 -- no line enters the ring unbounded */

int flag_debugtrace=0;
struct trace_data trace;

char dbgline[MAX_DBGLINES][512];
int idbgline = 0;

void add_dbgline(char *line)
{
	/* TASK 3.3 -- was strcpy(slot, line): a caller line longer than the
	 * slot wrote past dbgline[..][512] in .bss. Clamped-copy instead. */
	dbg_addline( dbgline[idbgline], (int)sizeof(dbgline[0]), line );
	idbgline++;
	if (idbgline>=MAX_DBGLINES) idbgline = 0;
}

void encryptstr( unsigned char *src, unsigned char *dest)
{
	unsigned char last = 0;
	*dest= 0;
	dest++;
	while(*src) {
		*dest = *src^0xA5^last;
		last = *src;
		src++;
		dest++;
	}
	*dest = 0;
}

void decryptstr( char *src, char *dest)
{
	unsigned char last = 0;
	if (*src!=0) return;
	src++;
	while (*src) {
		*dest = *src^0xA5^last;
		last = *dest;
		src++;
		dest++;
	}
	*dest = 0;
}

void debug(char *str)
{
	add_dbgline(str);
	if (flag_debugscr) printf( "%s", str );

	if (flag_debugfile) {
		FILE *fhandle;
		fhandle=fopen(debug_file, "at");
		if (fhandle!=0) {
			fprintf(fhandle,"%s", str );
			fclose(fhandle);
		}
	}

	if ( flag_debugtrace ) sendto(trace.sock, str, strlen(str), 0, (struct sockaddr *)&trace.addr, sizeof(trace.addr) );
}

char* debugtime(char *str)
{
    struct timeval tv;
    gettimeofday( &tv, NULL );
    int ms = tv.tv_usec / 1000;
    int hr = (1+(tv.tv_sec/3600)) % 24;
    int mn = (tv.tv_sec % 3600) /60;
    int sd = (tv.tv_sec % 60);
    sprintf( str, "[%02d:%02d:%02d.%03d]", hr,mn,sd,ms);
    return str;
}


/*
	struct {
		int type; // ALL, CCcam, Servers, Profiles/Newcamd, Mgcamd
		union {
			struct {
				int srv; // 0 all
				int cli; 
			} cccam;
			struct {
				int cli;	
			} mgcamd;
			struct {
				int peer;
			} cache;
			struct {
				int profiles;
				int newcamdcli;
			} profiles;
			struct {
				int server;
			} servers;
	} fdebug;

0xff ff ff ff

TYPE, SRV/PROFILEID/
*/


uint32_t flagdebug = 0; // ALL

uint32_t getdbgflag( int i, int j, int k)
{
	return (i<<24) | (j<<16) | k;
}

// to see servers/clients when profile is for debug
uint32_t getdbgflagpro( int i, int j, int k, int csid )
{
	if ( (flagdebug>>24)==DBG_NEWCAMD ) return ( (DBG_NEWCAMD<<24) | (csid<<16) );
	else return (i<<24) | (j<<16) | k;
}

int chkdbgflag( uint32_t f )
{
	uint32_t i,j;
	//
	i = flagdebug>>24;
	if (!i) return 1; // ok
	j = f>>24;
	if (i!=j) return 0;
	//
	i = 0xff&(flagdebug>>16);
	j = 0xff&(f>>16);
	if (i!=j) {
		if (i!=0) return 0; // nok
	}
	//
	i = 0xffff&flagdebug;
	if (!i) return 1; // ok
	j = 0xffff&f;
	if (i!=j) return 0;
	return 1;	
}


// Formatted Debug
void debugf(uint32_t flag, char *format, ...)
{
	if (!chkdbgflag(flag)) return;
	int index;
	char debugline[1024];
	char fstr[1024];
	if (format[0]==0) { // DECRYPT
		decryptstr(format, fstr);
	} else {
		/* TASK 3.3 -- the format string comes from the caller too */
		strncpy(fstr, format, sizeof(fstr)-1);
		fstr[sizeof(fstr)-1] = 0;
	}

	if (fstr[0]==' ') { // ADD TIME
		struct timeval tv;
		gettimeofday( &tv, NULL );
		int ms = tv.tv_usec / 1000;
		int hr = (1+(tv.tv_sec/3600)) % 24;
		int mn = (tv.tv_sec % 3600) /60;
		int sd = (tv.tv_sec % 60);
		sprintf( debugline, "[%02d:%02d:%02d.%03d]", hr,mn,sd,ms);
		index = strlen(debugline);
	} else index=0;
	
	va_list args;
	va_start (args, fstr);
	/* TASK 3.3 -- was vsprintf: the formatted line overflowed
	 * debugline[1024] on long names and long evidence strings. */
	vsnprintf( debugline+index, (int)sizeof(debugline)-index, fstr, args);
	va_end( args );
	debugline[sizeof(debugline)-1] = 0;

	debug(debugline);
}

#define DUMP_LENGTH 0x10
void debughex(uint8_t *buffer, int len)
{
	int i;
	for ( i = 0; i < len; ++i ) {
		if (!(i%DUMP_LENGTH)) debugf(0," \t  %04x: ",i);
		debugf(0,"%02X ", buffer[i]);
		if (!((i+1)%DUMP_LENGTH)) debug("\n");
	}
	if (i%DUMP_LENGTH) debug("\n");
}

#define DUMP_LENGTH 0x10
void bin2hex(uint8_t *src, uint8_t *buf, int len)
{
	int pos = 0;
	int i;
	for ( i = 0; i < len; ++i ) {
		if (!(i%DUMP_LENGTH)) {
			sprintf( buf+pos, " \t  %04x: ",i);
			pos += 10;
		}
		sprintf(buf+pos, "%02X ", src[i]);
		pos += 3;
		if (!((i+1)%DUMP_LENGTH)) {
			buf[pos] = '\n';
			pos++;
		}
	}
	if (i%DUMP_LENGTH) {
		buf[pos] = '\n';
		pos++;
	}
	buf[pos] = 0;
}

void fdebug(char *str)
{
	FILE *fhandle;
//	sprintf( fdebug_file,"%s.log", config_file);
	fhandle=fopen(debug_file, "at");
	if (fhandle!=0) {
		fprintf(fhandle,"%s", str );
		fclose(fhandle);
	}
}

// Formatted fdebug
void fdebugf(char *format, ...)
{
	int index;
	char fdebugline[4096];
	if (format[0]==' ') { // ADD TIME
		struct timeval tv;
		gettimeofday( &tv, NULL );
		int ms = tv.tv_usec / 1000;
		int hr = (1+(tv.tv_sec/3600)) % 24;
		int mn = (tv.tv_sec % 3600) /60;
		int sd = (tv.tv_sec % 60);
		sprintf( fdebugline, "[%02d:%02d:%02d.%03d]", hr,mn,sd,ms);
		index = strlen(fdebugline);
	} else index=0;
	
	va_list args;
	va_start (args, format);
	vsnprintf( fdebugline+index, (int)sizeof(fdebugline)-index, format, args);
	va_end( args );
	fdebugline[sizeof(fdebugline)-1] = 0;

	fdebug(fdebugline);
}


