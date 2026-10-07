#include "common.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <stdint.h>


#ifdef WIN32

#include <windows.h>

#else

#include <fcntl.h>
#include <sys/time.h>
#include <time.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <errno.h>

#endif

#include "parser.h"


char *uppercase(char *str)
{
	int i;
	for(i=0;;i++) {
		switch(str[i]) {
			case 'a'...'z':
				str[i] = str[i] - ('a'-'A');
				break;
			case 0:
				return str;
		}
	}
}

///////////////////////////////////////////////////////////////////////////////
// PARSER FUNCTIONS
///////////////////////////////////////////////////////////////////////////////

char *iparser; // Current Parser Index

//Skip Spaces
void parse_spaces()
{
	while ( (*iparser==' ')||(*iparser=='\t') ) iparser++;
}


int charpos( char c, char *str )
{
	int i;
	int l = strlen(str);
	for(i=0; i<l; i++) if (c==str[i]) return i+1;
	return 0;
}

int parse_value(char *str, char *delimiters)
{
	int len;
	char *end;
	parse_spaces();
	end=iparser;
	while ( !charpos(*end,delimiters) && (*end!=0) ) end++;
	if ( (len=end-iparser)>0 ) {
		/* TASK 3.7: the terminator must land INSIDE any caller's char[255].
		 * It was len=255, so str[255]=0 wrote one byte past a 255-byte buffer
		 * (read_config's scratch, parse_server_data's, twin_read_chninfo's and
		 * parse_boolean's own). 254 keeps every 254-char token complete and
		 * truncates longer ones one char earlier, still NUL-terminated. */
		if (len>=255) len=254;
		memcpy(str, iparser, len);
		iparser = end;
	}
	str[len] = 0;
	return len;
}

int parse_str(char *str)
{
	int len;
	char *end;
	parse_spaces();
	end=iparser;
	while ( (*end!=0)&&(*end!=' ')&&(*end!='\t')&&(*end!=13)&&(*end!=10) ) end++;
	if ( (len=end-iparser)>0 ) {
		/* TASK 3.7: the terminator must land INSIDE any caller's char[255].
		 * It was len=255, so str[255]=0 wrote one byte past a 255-byte buffer
		 * (read_config's scratch, parse_server_data's, twin_read_chninfo's and
		 * parse_boolean's own). 254 keeps every 254-char token complete and
		 * truncates longer ones one char earlier, still NUL-terminated. */
		if (len>=255) len=254;
		memcpy(str, iparser, len);
		iparser = end;
	}
	str[len] = 0;
	return len;
}

int parse_name(char *str)
{
	int len;
	char *end;
	parse_spaces();
	end=iparser;
	while ( (*end!=0)&&(*end!=' ')&&(*end!='\t')&&(*end!=13)&&(*end!=10)&&(*end!=']')&&(*end!=':') ) end++;
	if ( (len=end-iparser)>0 ) {
		/* TASK 3.7: the terminator must land INSIDE any caller's char[255].
		 * It was len=255, so str[255]=0 wrote one byte past a 255-byte buffer
		 * (read_config's scratch, parse_server_data's, twin_read_chninfo's and
		 * parse_boolean's own). 254 keeps every 254-char token complete and
		 * truncates longer ones one char earlier, still NUL-terminated. */
		if (len>=255) len=254;
		memcpy(str, iparser, len);
		iparser = end;
	}
	str[len] = 0;
	return len;
}

int parse_boolean()
{
	char str[255];
	parse_value(str,"\r\n\t;,:]= ");
	if (!strcmp(str,"1")) return 1;
	else if (!strcmp(str,"0")) return 0;
	else {
		uppercase(str);
		if (!strcmp(str,"NO")) return 0;
		else if (!strcmp(str,"YES")) return 1;
		else if (!strcmp(str,"OFF")) return 0;
		else if (!strcmp(str,"ON")) return 1;
	}
	return 0; // Error
}

int parse_int(char *str)
{
	int len;
	char *end;
	parse_spaces();
	end=iparser;
	while ( (*end>='0')&&(*end<='9') ) end++;
	if ( (len=end-iparser)>0 ) {
		/* TASK 3.7: the terminator must land INSIDE any caller's char[255].
		 * It was len=255, so str[255]=0 wrote one byte past a 255-byte buffer
		 * (read_config's scratch, parse_server_data's, twin_read_chninfo's and
		 * parse_boolean's own). 254 keeps every 254-char token complete and
		 * truncates longer ones one char earlier, still NUL-terminated. */
		if (len>=255) len=254;
		memcpy(str, iparser, len);
		iparser = end;
	}
	str[len] = 0;
	return len;
}

int parse_hex(char *str)
{
	int len;
	char *end;
	parse_spaces();
	end=iparser;
	while ( ((*end>='0')&&(*end<='9'))||((*end>='A')&&(*end<='F'))||((*end>='a')&&(*end<='f')) ) end++;
	if ( (len=end-iparser)>0 ) {
		/* TASK 3.7: the terminator must land INSIDE any caller's char[255].
		 * It was len=255, so str[255]=0 wrote one byte past a 255-byte buffer
		 * (read_config's scratch, parse_server_data's, twin_read_chninfo's and
		 * parse_boolean's own). 254 keeps every 254-char token complete and
		 * truncates longer ones one char earlier, still NUL-terminated. */
		if (len>=255) len=254;
		memcpy(str, iparser, len);
		iparser = end;
	}
	str[len] = 0;
	return len;
}

int parse_bin(char *str)
{
	int len;
	char *end;
	parse_spaces();
	end=iparser;
	while ( (*end=='0')||(*end=='1') ) end++;
	if ( (len=end-iparser)>0 ) {
		/* TASK 3.7: the terminator must land INSIDE any caller's char[255].
		 * It was len=255, so str[255]=0 wrote one byte past a 255-byte buffer
		 * (read_config's scratch, parse_server_data's, twin_read_chninfo's and
		 * parse_boolean's own). 254 keeps every 254-char token complete and
		 * truncates longer ones one char earlier, still NUL-terminated. */
		if (len>=255) len=254;
		memcpy(str, iparser, len);
		iparser = end;
	}
	str[len] = 0;
	return len;
}

int parse_expect( char c )
{
	parse_spaces();
	if (*iparser==c) {
		iparser++;
		return 1;
	}
	else return 0;
}

int parse_quotes( char quote, char *str, int size )
{
	str[0] = 0;
	parse_spaces();
	if (*iparser==quote) {
		iparser++;
		char *start = iparser;
		while ( (*iparser!=quote)&&(*iparser!='\n')&&(*iparser!='\r')&&(*iparser!=0) ) iparser++;
		if (*iparser==quote) {
			if (iparser-start) {
				/* TASK 3.8b: the copy is bounded by the CALLER's buffer now.
				 * It was strcpy(), so any quoted value longer than the
				 * destination -- read_config's char str[255] at nine call
				 * sites, the malloc'd http_file_data url/mime[512] at two,
				 * the twin fields at two more -- wrote past its end and
				 * killed the server at config read, silently. A probe on
				 * this exact line wrote 401 bytes into a 255-byte buffer
				 * (ASan stack-buffer-overflow) before the fix. size-1 keeps
				 * a value that fits complete and truncates longer ones one
				 * char before the buffer edge, still NUL-terminated: the
				 * same rule as the TASK 3.7 caps on parse_str/parse_hex/
				 * parse_bin above. The truncation is silent because this
				 * function has no line/field context to name -- the 3.7
				 * caps are silent for the same reason. iparser still stops
				 * after the closing quote, so the rest of the line parses
				 * exactly as before. */
				int len = (int)(iparser - start);
				if ( len > size-1 ) len = size-1;
				*iparser = 0;
				memcpy( str, start, len );
				str[len] = 0;
				*iparser = quote;
				iparser++;
			}
			return 1;
		}
	}
	return 0;
}


