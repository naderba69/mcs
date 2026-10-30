#include "safe-time.h"
#include "htmlesc.h"	/* TASK 3.2 -- client names are data, not markup */
#ifdef MCS_DCWSTATS
#include "dcwstats.h"	/* TASK 3.12: home-page readout, same names as telnet */
#endif
#include "peerrep.h"	/* TASK R5 (D59): the ladder section of /json */
#include "monjson.h"	/* TASK R5 (D59): the pure /json document builder */
#ifdef MCS_DCWSTATS
#include "cwlog.h"	/* TASK R11 (D65): the /cwlog page + /json section */
#include "peer_origin.h"/* TASK R11 (D65): peer id masks on the page */
#endif
#include "biss.h"	/* BISS key management */
#include "diagnostics.h"	/* Advanced diagnostics and analytics */
#include "debug-ecm.h"	/* Detailed ECM tracking */
#include "server-stats.h"	/* Server statistics */
#include "log-viewer.h"	/* Live log viewer */
#include "user-trace.h"
#include "cache-analytics.h"	/* Cache analytics */
#include "auto-tuning.h"	/* Auto-tuning suggestions */
#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>

#ifdef WIN32

#include <windows.h>
#include <sys/types.h>
#include <sys/_default_fcntl.h>
#include <sys/poll.h>
#include <cygwin/types.h>
#include <cygwin/socket.h>
#include <sys/errno.h>
#include <cygwin/in.h>
#include <sched.h>
#include <netdb.h>
#include <netinet/tcp.h>

#else

#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>
#include <errno.h>
#include <pthread.h>
#include <poll.h>
#include <sys/prctl.h>
#include <poll.h>

#endif

#include "debug.h"
#include "convert.h"
#include "tools.h"
#include "threads.h"
#include "ecmdata.h"

#ifdef CCCAM
#include "msg-cccam.h"
#endif

#include "config.h"
#include "sockets.h"

#include "httpserver.h"
#include "httpbuffer.c"
#include "dyn_buffer.c"

#include "main.h"

#include "images.c"
#include "country.c"
#include "httpstyle.c"



#define HTTP_GET  0
#define HTTP_POST 1

struct cs_client_data *getnewcamdclientbyid(uint32_t id);
struct cccam_server_data *getcccamserverbyid(uint32_t id);
struct cc_client_data *getcccamclientbyid(uint32_t id);
struct cc_client_data *getcccamclientbyname(struct cccam_server_data *cccam, char *name);
#ifdef MGCAMD_SRV
struct mgcamdserver_data *getmgcamdserverbyid(uint32_t id);
struct mg_client_data *getmgcamdclientbyid(uint32_t id);
struct mg_client_data *getmgcamdclientbyname(struct mgcamdserver_data *mgcamd, char *name);
void mg_disconnect_cli(struct mg_client_data *cli);
#endif

#ifdef CS378X_SRV
struct camd35_client_data *getcs378xclientbyid(uint32_t id);
#endif


#define LIST_ACTIVE       0
#define LIST_CONNECTED    1
#define LIST_DISCONNECTED 2
#define LIST_ALL          3


#define ACTION_PAGE     0
#define ACTION_DIV      1
#define ACTION_ROW      2
#define ACTION_XML      3
#define ACTION_DISABLE  4
#define ACTION_ENABLE   5
#define ACTION_STATUS   6
#define ACTION_DEBUG    7



char HTTP_UPDATE_DIV[] = "\nvar autorefresh=%d;\nvar tautorefresh;\nfunction setautorefresh(t)\n{\n	clearTimeout(tautorefresh);\n	autorefresh = t;\n	if (t>0) tautorefresh = setTimeout('updateDiv()',autorefresh);\n}\nfunction updateDiv()\n{\n	var httpRequest;\n	try {\n		httpRequest = new XMLHttpRequest();  // Mozilla, Safari, etc\n	}\n	catch(trymicrosoft) {\n		try {\n			httpRequest = new ActiveXObject('Msxml2.XMLHTTP');\n		}\n		catch(oldermicrosoft) {\n			try {\n				httpRequest = new ActiveXObject('Microsoft.XMLHTTP');\n			}\n			catch(failed) {\n				httpRequest = false;\n			}\n		}\n	}\n	if (!httpRequest) {\n		alert('Your browser does not support Ajax.');\n		return false;\n	}\n	// Action http_request\n	httpRequest.onreadystatechange = function()\n	{\n		if (httpRequest.readyState == 4) {\n			if(httpRequest.status == 200) {\n				requestError=0;\n				document.getElementById('mainDiv').innerHTML = httpRequest.responseText;\n			}\n			tautorefresh = setTimeout('updateDiv()',autorefresh);\n		}\n	}\n	httpRequest.open('GET', '%s',true);\n	httpRequest.send(null);\n}\n";
char HTTP_UPDATE_ROW[] = "\nvar idx = 0;\nvar tupdateRow;\n\nfunction setupdateRow(id)\n{\n	clearTimeout(tupdateRow);\n	idx = id;\n	if (id>0) tupdateRow = setTimeout('updateRow()',1000);\n}\n\nvar lastidx = 0;\nvar requestError = 0;\nfunction updateRow()\n{\n	if (lastidx!=idx) {\n		requestError = 0;\n		lastidx = idx;\n	}\n	if ( !requestError && (idx>0) ) {\n		var httpRequest;\n		try {\n			httpRequest = new XMLHttpRequest();  // Mozilla, Safari, etc\n		}\n		catch(trymicrosoft) {\n			try {\n				httpRequest = new ActiveXObject('Msxml2.XMLHTTP');\n			}\n			catch(oldermicrosoft) {\n				try {\n					httpRequest = new ActiveXObject('Microsoft.XMLHTTP');\n				}\n				catch(failed) {\n					httpRequest = false;\n				}\n			}\n		}\n		if (!httpRequest) {\n			alert('Your browser does not support Ajax.');\n			return false;\n		}\n		var savedidx = idx;\n		// Action http_request\n		httpRequest.onreadystatechange = function()\n		{\n			if (httpRequest.readyState == 4) {\n				if (httpRequest.status == 200) {\n					requestError=0;\n					xmlupdateRow( httpRequest.responseXML, 'Row'+savedidx );\n				}\n				else {\n					requestError++;\n				}\n				tupdateRow = setTimeout('updateRow()',1000);\n			}\n		}\n		httpRequest.open('GET', %s, true);\n		httpRequest.send(null);\n		requestError++;\n	}\n}\n";


int getcountryimage( char *code )
{
	int i;
	for(i=0; i<MAX_COUNTRY_IMAGES; i++) {
		if ( !strcmp(country_images[i].code, code) ) return i;
	}
	return -1;
}

void getcountryhtml( char *code, char *html )
{
	int i;
	for(i=0; i<MAX_COUNTRY_IMAGES; i++) {
		if ( !strcmp(country_images[i].code, code) ) {
			sprintf(html,"<img src='/flag%s.gif'>", code);
			return;
		}
	}
	sprintf(html, "[%s]", code);
}


char *getcountrycodebyip(uint32_t ip)
{
	struct ip2country_data *data= cfg.ip2country;
	ip = (ip>>24&0xFF)|(ip>>8&0xFF00)|(ip<<8&0xFF0000)|(ip<<24&0xFF000000);  // from little endian -> big endian
	while (data) {
		if ( (ip>=data->ipstart)&&(ip<=data->ipend) ) return data->code;
		data = data->next;
	}
	return NULL;
}

int isblockedip(uint32_t ip)
{
	if (!cfg.blockcountry[0][0]) return 0; // accept
	char *p = getcountrycodebyip(ip);
	if (p) {
		int i;
		for(i=0; i<512; i++) {
			if (!cfg.blockcountry[i][0]) break;
			if ( !strcmp(cfg.blockcountry[i], p) ) return 1; // block
		}
	}
	return 0; // accept
}

char *getcountryname(char *code)
{
	int i;
	for(i=0; i<MAX_COUNTRY_IMAGES; i++) {
		if ( !strcmp(country_images[i].code, code) ) return country_images[i].name;
	}
	return NULL;
}

struct cachepeer_data *getpeerbyid(int id);
struct server_data *getsrvbyid(uint32_t id);
void cc_disconnect_cli(struct cc_client_data *cli);
char *src2string(int srctype, int srcid, char *ret);

typedef struct
{
	char name[256];
	char value[512];
} http_get;

typedef struct http_request
{
	int sock;
	uint32_t ip;
	struct dyn_buffer dbf;
	int type;//= (HTTP_GET/HTTP_POST)
	char path[512];
	char file[512];
	int http_version;//(0:1.0,1:1.1)
	char Host[100];//(localhost:9999)
	int Connection;//(1:keep-alive, 0:close);
	http_get getlist[20];
	int getcount;
	http_get postlist[20];
	int postcount;
	http_get headers[20];
	int hdrcount;

	/* TASK R7 (D61): per-request skin/session state. rtl drives
	 * <html dir=rtl lang=ar>; setcookie accumulates Set-Cookie pairs
	 * that http_replyok_write() puts on the page's own 200 header. */
	int rtl;
	char setcookie[240];

} http_request;
void http_send_biss(int sock, http_request *req);
void http_send_auto_tuning(int sock, http_request *req);

/*
 * TASK 3.1 -- bounded. The wire decides how long a header value or a request
 * line is; `max` is what the destination actually holds. Stock copied until
 * `end` with no bound at all: a long request line or a long header line
 * wrote past the struct on the connection thread's stack.
 */
void buf2str( char *dest, char *start, char *end, int max)
{
  while (*start==' ') start++;
  while (start<=end && max>1)
  {
	*dest=*start;
	start++;
	dest++;
	max--;
  }
  *dest='\0';
}

///////////////////////////////////////////////////////////////////////////////
char *isset_get(http_request *req, char *name)
{
  int i;
  char *n,*v;
  for(i=0; i<req->getcount; i++) {
    n = req->getlist[i].name;
    v = req->getlist[i].value;
    if (!strcmp(name, n)) {
		//printf("[$_GET] Name: '%s'    Value :'%s'\n", n,v);
		return v;
	}
  }
  return NULL;
}


///////////////////////////////////////////////////////////////////////////////
char *isset_header(http_request *req, char *name)
{
  int i;
  char *n,*v;
  //printf("Searching '%s'\n", name);
  for(i=0; i<req->hdrcount; i++) {
	//printf("[HEADER] Name: '%s'    Value :'%s'\n", req->headers[i].name, req->headers[i].value);
    n = req->headers[i].name;
    v = req->headers[i].value;
    if (!strcmp(name, n)) return v;
  }
  return NULL;
}

///////////////////////////////////////////////////////////////////////////////
void explode_get(http_request *req, char *get) // Get Variables
{
  char *end,*a;
  int i;
  i=0;
  //debugf(getdbgflag(DBG_HTTP,0,0),"explode_get()\n");
  while ( (end=strchr(get, '&')) ) 
  {
	*end = '\0';
        if (i>8) break; //@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
	if ( (a=strchr(get, '=')) ) {
	  *a='\0';
	  strncpy(req->getlist[i].name,get,255);
	  req->getlist[i].name[255]=0;
	  strncpy(req->getlist[i].value,a+1,255);
	  req->getlist[i].value[255]=0;
	  //printf("$_GET['%s'] = '%s'\n",req->getlist[i].name,req->getlist[i].value);
	  i++;
	}
	get=end+1;
  }
  if ( (a=strchr(get, '=')) ) {
    *a='\0';
    strncpy(req->getlist[i].name,get,255);
    strncpy(req->getlist[i].value,a+1,255);
    req->getlist[i].name[255]=0;
    req->getlist[i].value[255]=0;
    //printf("$_GET['%s'] = '%s'\n",req->getlist[i].name,req->getlist[i].value);
    i++;
  }
  req->getcount=i;
}

///////////////////////////////////////////////////////////////////////////////
void explode_post(http_request *req, char *post)
{
  char *end,*a;
  int i;
  i=0;
  while ( (end=strchr(post, '&')) ) 
  {
	*end = '\0';
	/* TASK 3.1 -- stock had no bound here at all: a POST body with enough
	 * &name=value pairs wrote straight past postlist[20] on the stack. */
	if (i>=19) break;
	if ( (a=strchr(post, '=')) ) {
	  *a='\0';
	  strncpy(req->postlist[i].name,post,255);
	  strncpy(req->postlist[i].value,a+1,255);
	  req->postlist[i].name[255]=0;
	  req->postlist[i].value[255]=0;
	  //printf("$_POST['%s'] = '%s'\n",req->postlist[i].name,req->postlist[i].value);
	  i++;
	}
	post=end+1;
  }
  if (i<19 && (a=strchr(post, '=')) ) {
    *a='\0';
    strncpy(req->postlist[i].name,post,255);
    strncpy(req->postlist[i].value,a+1,255);
    req->postlist[i].name[255]=0;
    req->postlist[i].value[255]=0;
    i++;
  }
  req->postcount=i;
}

///////////////////////////////////////////////////////////////////////////////


int extractreq(http_request *req, char *buffer, int len )
{
	char *path_start, *path_end;
	char *rnrn, *slash;

	//printf("buffer size %d\n",len);
	//#Check Header
	if (!(rnrn=strstr( buffer, "\r\n\r\n"))) return -1;
	int reqsize = (rnrn-buffer)+4;
	//#Get Path
	path_start = buffer+4;
	while (*path_start==' ') path_start++;
	path_end=path_start;
	while (*path_end!=' ') path_end++;
	buf2str( req->path, path_start, path_end-1, (int)sizeof(req->path));
//debugf(0, " HTTP PATH = '%s'\n", req->path);
	//#extract filename, and path
	slash = path_start = (char*)&req->path;
	while (*path_start) {
		if (*path_start=='/') slash=path_start;
		else if (*path_start=='?') {
			explode_get(req,path_start+1);
			*path_start='\0';
			break;
		}
		path_start++;
	}
	slash++;
	strncpy( req->file, slash, 100);
	//#Extract headers
	path_start = buffer+4;
	while ( (*path_start!='\r')&&(*path_start!='\n') ) path_start++;
	if (*path_start=='\r') path_start++;
	if (*path_start=='\n') path_start++;
	while ( path_start<rnrn ) {
		// start = path_start
		//get end of line
		path_end = path_start;
		slash = NULL;
		while ( (*path_end!='\r')&&(*path_end!='\n')&&(*path_end!=0) ) {
			if (*path_end==':') if (!slash) slash = path_end;
			path_end++;
		}
		if (path_end==path_start) break; // end
		char tmp = *path_end;
		*path_end = 0;
		if (slash) {
			/* TASK 3.1 -- the array holds 20; stock incremented hdrcount for
			 * every header line on the wire and wrote straight past the end. */
			if ( req->hdrcount >= (int)(sizeof(req->headers)/sizeof(req->headers[0])) ) break;
			// Extract header name: value
			buf2str( req->headers[req->hdrcount].name , path_start, slash-1,
			         (int)sizeof(req->headers[0].name));
			buf2str( req->headers[req->hdrcount].value, slash+1, path_end-1,
			         (int)sizeof(req->headers[0].value));
			//printf(">> %s\n", path_start);
			//printf("[HEADER] Name: '%s'    Value :'%s'\n", req->headers[req->hdrcount].name, req->headers[req->hdrcount].value);
			//if ( !strcmp(req->headers[req->hdrcount].name,"Authorization") ) 
			req->hdrcount++;
		}
		*path_end = tmp;
		path_start = path_end;
		while ( (*path_start=='\r')||(*path_start=='\n') ) path_start++;
	}

	if ( !memcmp(buffer,"GET ",4) ) {
		//printf("requesttype = GET\n");
		req->type = HTTP_GET;
	}
	else if ( !memcmp(buffer,"POST",4) ) {
		//printf("requesttype = POST\n");
		req->type = HTTP_POST;
		int i;
		for(i=0; i<req->hdrcount; i++) {
			if ( !strcmp(req->headers[i].name,"Content-Length") ) {
				reqsize += atoi(req->headers[i].value);
				//printf("req size %d\n", reqsize);
				return reqsize;
			}
		}
	}
	return 0;
}




int parse_http_request(int sock, http_request *req )
{
	unsigned char buffer[2048]; // HTTP Header cant be greater than 1k
	int size;
	int totalsize = 0;
	struct dyn_buffer saved_dbf = req->dbf;
	memset(buffer,0,sizeof(buffer));
	memset(req,0, sizeof(http_request));
	req->dbf = saved_dbf;
	size = recv( sock, buffer, sizeof(buffer) - 1, MSG_NOSIGNAL);
	if (size<10) return 0;
	totalsize += size;
	//printf("** Receiving %d bytes\n%s\n",size,buffer );
	if ( !memcmp(buffer,"GET ",4) || !memcmp(buffer,"POST",4) ) {
		buffer[size] = '\0';

		// Get Header
		while ( !strstr((char*)buffer, "\r\n\r\n") ) {
			struct pollfd pfd;
			pfd.fd = sock;
			pfd.events = POLLIN | POLLPRI;
			int retval = poll(&pfd, 1, 5000);
			if ( retval>0 )	{
				if ( pfd.revents & (POLLHUP|POLLNVAL) ) return 0; // Disconnect
				else if ( pfd.revents & (POLLIN|POLLPRI) ) {
					int len;
					if (size >= (int)sizeof(buffer) - 1) return 0;
					len = recv(sock, (buffer+size), sizeof(buffer)-1-size, MSG_NOSIGNAL);
					//printf("** Receiving %d bytes\n",len );
					if (len<=0) return 0;
					size+=len;
					buffer[size]=0;
					totalsize += len;
				}
			}
			else if (retval==0) break;
			else return 0;
		}
		// Received Header
		//debugf(getdbgflag(DBG_HTTP,0,0)," Received Header >>>\n%s\n<<<\n", buffer);
		int ret = extractreq(req,(char*)buffer,size);
		if (ret==-1) return 0;
		//Get Data
		if (req->type==HTTP_POST) {
			while (ret>totalsize) {
				//printf("Waiting....\n");
				struct pollfd pfd;
				pfd.fd = sock;
				pfd.events = POLLIN | POLLPRI;
				int retval = poll(&pfd, 1, 5000);
				if ( retval>0 )	{
					if ( pfd.revents & (POLLHUP|POLLNVAL) ) return 0; // Disconnect
					else if ( pfd.revents & (POLLIN|POLLPRI) ) {
						if (size>=sizeof(buffer)) {
							dynbuf_write( &req->dbf, buffer, size);
							size = 0;
						}
						int len = recv(sock, (buffer+size), sizeof(buffer)-size, MSG_NOSIGNAL);
						//printf("** Receiving %d bytes\n",len );
						if (len<=0) return 0;
						size+=len;
						totalsize += len;
					}
				}
				else return 0;
			}
			if (size) dynbuf_write( &req->dbf, buffer, size);
		}
	}
	else return 0;
	return 1;
}



/// XXX: not thread safe
char channelname[256];
char *getchname(uint16_t caid, uint32_t prov, uint16_t sid )
{
	struct chninfo_data *chn= cfg.chninfo;
	while (chn) {
		if ( (chn->caid==caid)&&(chn->prov==prov)&&(chn->sid==sid) ) return chn->name;
		chn = chn->next;
	}
	sprintf(channelname, "%04X:%06X:%04X", caid, prov, sid );
	return channelname;
}

struct chninfo_data *getchninfo(uint16_t caid, uint32_t prov, uint16_t sid )
{
	struct chninfo_data *chn= cfg.chninfo;
	while (chn) {
		if ( (chn->caid==caid)&&(chn->prov==prov)&&(chn->sid==sid) ) return chn;
		chn = chn->next;
	}
	return NULL;;
}

int total_profiles()
{
	int count=0;
	struct cardserver_data *cs = cfg.cardserver;
	while(cs) {
		count++;
		cs = cs->next;
	}
	return count;
}	



int total_servers()
{
	int nb=0;
	struct server_data *srv=cfg.server;
	while (srv) {
		nb++;
		srv=srv->next;
	}
	return nb;
}

int connected_servers()
{
	int nb=0;
	struct server_data *srv=cfg.server;
	while (srv) {
		if ( !IS_DISABLED(srv->flags)&&(srv->handle>0) ) nb++;
		srv=srv->next;
	}
	return nb;
}

/*
int totalcachepeers()
{
	struct cachepeer_data *peer;
	int count=0;

	peer = cfg.cache.peer;
	while (peer) {
		count++;
		peer = peer->next;
	}
	return count;
}
*/

void cache_peers( struct cacheserver_data *cache, int *total, int *active )
{
	*total = 0;
	*active = 0;
	struct cachepeer_data *peer = cache->peer;
	while (peer) {
		(*total)++;
		if ( peer->ping>0 ) (*active)++;
		peer=peer->next;
	}
}

void total_cache_peers( int *total, int *active )
{
	*total = 0;
	*active = 0;
	struct cacheserver_data *cache = cfg.cache.server;
	while (cache) {
		struct cachepeer_data *peer = cache->peer;
		while (peer) {
			(*total)++;
			if ( peer->ping>0 ) (*active)++;
			peer=peer->next;
		}
		cache = cache->next;
	}
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

//color: #000000; background-color: #FFFFFF;
char http_replyok[] = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n";

char http_html[] = "<HTML>\n";
char http_html_[] = "</HTML>\n";

char http_head[] = "<HEAD>\n";
char http_head_[] = "</HEAD>\n";

char http_body[] = "<BODY>\n";
char http_body_[] = "</BODY>\n";

char html_title[] = "<title>%s - %s</title>\n";

char http_link[] = "<meta http-equiv=\"Content-type\" content=\"text/html; charset=utf-8\"/> <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"> <link rel=\"icon\" type=\"image/png\" href=\"/favicon.png\">\n";

char http_style[] = "<link rel=\"stylesheet\" href=\"style.css\" type=\"text/css\" />\n";

/*
 * TASK R7 (D61) -- the new skin's shared writers. Three tiny helpers
 * replace the stock static writes so every HTML page: opens with an
 * optional Set-Cookie block (token sessions + the language toggle),
 * emits <html dir=rtl lang=ar> when the request resolved to Arabic,
 * and carries the stylesheet INLINE (no second request, nothing
 * external) unless the operator's STYLESHEET FILE overrides the skin.
 * Any new page must use these -- the old static http_replyok /
 * http_html / http_style writes cannot carry cookies or direction.
 */
static __thread int tls_http_rtl; /* request-local: the menu writer reads it */
static char *mlabel(char *en, char *ar) { return tls_http_rtl ? ar : en; }

static void http_replyok_write(http_request *req, struct tcp_buffer_data *tcpbuf, int sock)
{
	tcp_writestr(tcpbuf, sock, "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n");
	if (req->setcookie[0]) {
		tcp_writestr(tcpbuf, sock, "Set-Cookie: ");
		tcp_writestr(tcpbuf, sock, req->setcookie);
		tcp_writestr(tcpbuf, sock, "\r\n");
	}
	tcp_writestr(tcpbuf, sock, "\r\n");
}

static void http_html_write(http_request *req, struct tcp_buffer_data *tcpbuf, int sock)
{
	if (req->rtl) tcp_writestr(tcpbuf, sock, "<HTML dir=rtl lang=ar>\n");
	else tcp_write(tcpbuf, sock, http_html, strlen(http_html));
}

static void http_style_write(struct tcp_buffer_data *tcpbuf, int sock)
{
	if (strlen(cfg.stylesheet_file))
		tcp_write(tcpbuf, sock, http_style, strlen(http_style)); /* operator override keeps the link */
	else {
		tcp_writestr(tcpbuf, sock, "<style type=\"text/css\">\n");
		tcp_write(tcpbuf, sock, style_css, strlen(style_css));
		tcp_writestr(tcpbuf, sock, "\n</style>\n");
	}
}


#define PAGE_HOME      1
#define PAGE_SERVERS   2
#define PAGE_CACHE     3
#define PAGE_PROFILES  4
#define PAGE_NEWCAMD   5
#define PAGE_CCCAM     6
#define PAGE_FREECCCAM 7
#define PAGE_MGCAMD    8
#define PAGE_EDITOR    9
#define PAGE_RESTART   10
#define PAGE_CACHEEX   11

#define PAGE_CAMD35    12
#define PAGE_CS378X    13
#define PAGE_DEBUG     14
#define PAGE_CWLOG     15   /* TASK R11 (D65), stats flavour only */
#define PAGE_BISS      16   /* BISS key management */


char *yesno( int a )
{
	static char yes[] ="YES";
	static char no[] ="NO";
	if (a) return yes; else return no;
}

char *onoff( int a )
{
	static char yes[] ="ON";
	static char no[] ="OFF";
	if (a) return yes; else return no;
}

int unreadsms()
{
	int nb = 0;
	struct cacheserver_data *cache = cfg.cache.server;
	while (cache) {
		struct cachepeer_data *peer = cache->peer;
		while (peer) {
			if ( (peer->sms)&&(peer->sms->status==0) ) nb++;
			peer = peer->next;
		}
		cache = cache->next;
	}
	return nb;
}		

void tcp_write_menu(struct tcp_buffer_data *tcpbuf, int sock, int selected)
{
	char *cNormal = "<li><a href='%s'>%s</a></li>";
	char *cSelected = "<li><a class=selected href='%s'>%s</a></li>";
	char *cDisabled = "<li><a class=disabled href='%s'>%s</a></li>";
	char *class;
	char buf[512];

	tcp_writestr(tcpbuf, sock, "<div class=menu><ul>" );
	// Home
	if (selected==PAGE_HOME) class = cSelected; else class = cNormal;
	sprintf( buf, class, "/", mlabel("Home","الرئيسية")); tcp_writestr(tcpbuf, sock, buf);

	// Servers
	if ( !cfg.http.show.nodebug ) {
		if (selected==PAGE_DEBUG) class = cSelected; else class = cNormal;
		sprintf( buf, class, "/debug", mlabel("Debug","التنقيح")); tcp_writestr(tcpbuf, sock, buf);
	}

	// Threads
	sprintf( buf, cNormal, "/threads", mlabel("Threads","الخيوط")); tcp_writestr(tcpbuf, sock, buf);

	// Servers
	if ( !cfg.http.show.noservers ) {
		if (cfg.server!=NULL) { 
			if (selected==PAGE_SERVERS) class = cSelected; else class = cNormal;
		} else class = cDisabled;
		sprintf( buf, class, "/servers", mlabel("Servers","الخوادم")); tcp_writestr(tcpbuf, sock, buf);
	}
	// Cache
	if ( !cfg.http.show.nocache ) {
		if (cfg.cache.server) {
			if (selected==PAGE_CACHE) class = cSelected; else class = cNormal;
		} else class = cDisabled;
		int unread = unreadsms();
		char cache[32]; sprintf(cache, "%s", mlabel("Cache","الكاش"));
		if (unread) sprintf( cache, "%s (%d)", mlabel("Cache","الكاش"), unread);
		sprintf( buf, class, "/cache", cache); tcp_writestr(tcpbuf, sock, buf);
	}
	// BISS Keys
	{
		if (selected==PAGE_BISS) class = cSelected; else class = cNormal;
		sprintf( buf, class, "/biss", mlabel("BISS","BISS")); tcp_writestr(tcpbuf, sock, buf);
	}
	// Profiles
	if (!cfg.http.show.noprofiles) {
		if (cfg.cardserver!=NULL) {
			if (selected==PAGE_PROFILES) class = cSelected; else class = cNormal;
		} else class = cDisabled;
		sprintf( buf, class, "/profiles", mlabel("Profiles","الباقات")); tcp_writestr(tcpbuf, sock, buf);
	}
	// Newcamd
	if ( !cfg.http.show.nonewcamd ) {
		if (cfg.cardserver!=NULL) {
			if (selected==PAGE_NEWCAMD) class = cSelected; else class = cNormal;
		} else class = cDisabled;
		sprintf( buf, class, "/newcamd", mlabel("Newcamd","نيوكامد")); tcp_writestr(tcpbuf, sock, buf);
	}
#ifdef MGCAMD_SRV
	if ( !cfg.http.show.noservers && (cfg.mgcamd.server!=NULL) ) {
		if (selected==PAGE_MGCAMD) class = cSelected; else class = cNormal;
		sprintf( buf, class, "/mgcamd", mlabel("Mgcamd","مجكامد")); tcp_writestr(tcpbuf, sock, buf);
	}
#endif

#ifdef CCCAM_SRV
	// CCcam
	if ( !cfg.http.show.nocccam && (cfg.cccam.server!=NULL) ) {
		if (selected==PAGE_CCCAM) class = cSelected; else class = cNormal;
		sprintf( buf, class, "/cccam", "CCcam"); tcp_writestr(tcpbuf, sock, buf);
	}
#endif

#ifdef CS378X_SRV
	// cs378x
	if (cfg.cs378x.server!=NULL) {
		if (selected==PAGE_CS378X) class = cSelected; else class = cNormal;
		sprintf( buf, class, "/cs378x", "Cs378x"); tcp_writestr(tcpbuf, sock, buf);
	}
#endif

#ifdef CAMD35_SRV
	// camd35
	if (cfg.camd35.server!=NULL) {
		if (selected==PAGE_CAMD35) class = cSelected; else class = cNormal;
		sprintf( buf, class, "/camd35", "Camd35"); tcp_writestr(tcpbuf, sock, buf);
	}
#endif

#ifdef CACHEEX
	// CacheEX
	if ( !cfg.http.show.nocacheex ) {
		if (selected==PAGE_CACHEEX) class = cSelected; else class = cNormal;
		sprintf( buf, class, "/cacheex", "CacheEX"); tcp_writestr(tcpbuf, sock, buf);
	}
#endif

#ifdef MCS_DCWSTATS
	/* TASK R11 (D65): the per-CW verdict page. Visible only where the
	 * counters exist -- the stock menu (and binary) is untouched. */
	if (selected==PAGE_CWLOG) class = cSelected; else class = cNormal;
	sprintf( buf, class, "/cwlog", mlabel("CW Log","سجل الأحكام")); tcp_writestr(tcpbuf, sock, buf);
#endif

#ifdef FREECCCAM_SRV
	// FreeCCcam
	if (cfg.freecccam.server.handle>0) {
		if (selected==PAGE_FREECCCAM) class = cSelected; else class = cNormal;
		sprintf( buf, class, "/freecccam", "FreeCCcam"); tcp_writestr(tcpbuf, sock, buf);
	}
#endif
	if (!cfg.http.show.noeditor) {
		if (selected==PAGE_EDITOR) class = cSelected; else class = cNormal;
		sprintf( buf, class, "/editor", mlabel("Editor","المحرر")); tcp_writestr(tcpbuf, sock, buf);
	}
	if (!cfg.http.show.norestart) {
		if (selected==PAGE_RESTART) class = cSelected; else class = cNormal;
		sprintf( buf, class, "/restart", mlabel("Restart","إعادة تشغيل")); tcp_writestr(tcpbuf, sock, buf);
		sprintf( buf, class, "/diagnostics", mlabel("Diagnostics","التشخيص")); tcp_writestr(tcpbuf, sock, buf);
		sprintf( buf, class, "/debug-ecm", mlabel("Debug ECM","تتبع ECM")); tcp_writestr(tcpbuf, sock, buf);
		sprintf( buf, class, "/server-stats", mlabel("Server Stats","إحصائيات الخوادم")); tcp_writestr(tcpbuf, sock, buf);
		sprintf( buf, class, "/log-viewer", mlabel("Log Viewer","عارض السجلات")); tcp_writestr(tcpbuf, sock, buf);
#ifdef CCCAM_SRV
		if (!cfg.http.show.nocccam) { sprintf(buf, class, "/user-trace", mlabel("CCcam Trace", "متابعة CCcam")); tcp_writestr(tcpbuf, sock, buf); }
#endif
		sprintf( buf, class, "/cache-analytics", mlabel("Cache Analytics","تحليل الكاش")); tcp_writestr(tcpbuf, sock, buf);
		sprintf( buf, class, "/auto-tuning", mlabel("Auto-Tuning","الضبط التلقائي")); tcp_writestr(tcpbuf, sock, buf);
	}

	/* TASK R7 (D61): the language toggle and logout close the bar */
	if (tls_http_rtl) tcp_writestr(tcpbuf, sock, "<li><a href='?lang=en'>English</a></li>");
	else tcp_writestr(tcpbuf, sock, "<li><a href='?lang=ar'>عربي</a></li>");
	sprintf( buf, "<li><a href='/logout'>%s</a></li>", mlabel("Logout","خروج"));
	tcp_writestr(tcpbuf, sock, buf);
	tcp_writestr(tcpbuf, sock, "</ul><span style='float:right;'>Multi Card Server r"REVISION_STR"</span></div>\n");

	//
	if ( (selected!=PAGE_RESTART)&&(selected!=PAGE_EDITOR) ) {
		tcp_writestr(tcpbuf, sock, "<div align=right>Autorefresh <select onchange='setautorefresh(this.value);'>");
		if (cfg.http.autorefresh==0) tcp_writestr(tcpbuf, sock, "<option value=0 selected>OFF</option>");
		else tcp_writestr(tcpbuf, sock, "<option value=0>OFF</option>");
		int i;
		for (i=1; i<=10; i++) {
			if ( cfg.http.autorefresh==i ) sprintf( buf, "<option value=%d selected>%ds</option>", i*1000, i); else sprintf( buf, "<option value=%d>%ds</option>", i*1000, i);
			tcp_writestr(tcpbuf, sock, buf);
		}

		if ( cfg.http.autorefresh>10 ) {
			sprintf( buf, "<option value=%d selected>%ds</option>", cfg.http.autorefresh*1000, cfg.http.autorefresh); 
			tcp_writestr(tcpbuf, sock, buf);
		}
		tcp_writestr(tcpbuf, sock, "</select></div>\n");
	}
}




void tcp_writeecmdata(struct tcp_buffer_data *tcpbuf, int sock, int ecmok, int ecmnb)
{
	char http_buf[2048];
	if (ecmnb) {
		int n;
		if (ecmnb>9999999) n = (ecmok*10)/(ecmnb/10); else n = (ecmok*100)/ecmnb;
		sprintf( http_buf, "<td>%d<span style=\"float: right;\">%d%%</span></td>", ecmok, n );
	}
	else
		sprintf( http_buf, "<td><span style=\"float: right;\">0%%</span></td>" );
	tcp_write(tcpbuf, sock, http_buf, strlen(http_buf) );
}

void tcp_writeecmdata2(struct tcp_buffer_data *tcpbuf, int sock, int ecmok, int ecmnb)
{
	char http_buf[2048];
	if (ecmnb) {
		int n;
		if (ecmnb>9999999) n = (ecmok*10)/(ecmnb/10); else n = (ecmok*100)/ecmnb;
		sprintf( http_buf, "<td>%d / %d<span style=\"float: right;\">%d%%</span></td>", ecmok, ecmnb, n );
	}
	else
		sprintf( http_buf, "<td><span style=\"float: right;\">0%%</span></td>" );
	tcp_write(tcpbuf, sock, http_buf, strlen(http_buf) );
}

void getstatcell(int ecmok, int ecmnb, char *dest)
{
	if (ecmnb) {
		int n;
		if (ecmnb>9999999) n = (ecmok*10)/(ecmnb/10); else n = (ecmok*100)/ecmnb;
		sprintf( dest, "%d<span style=\"float: right;\">%d%%</span>", ecmok, n );
	}
	else sprintf( dest, "<span style=\"float: right;\">0%%</span>" );
}

void getstatcell2(int ecmok, int ecmnb, char *dest)
{
	if (ecmnb) {
		int n;
		if (ecmnb>9999999) n = (ecmok*10)/(ecmnb/10); else n = (ecmok*100)/ecmnb;
		sprintf( dest, "%d / %d<span style=\"float: right;\">%d%%</span>", ecmok, ecmnb, n );
	}
	else sprintf( dest, "<span style=\"float: right;\">0%%</span>" );
}

#include <sys/stat.h>

void http_send_file(int sock, http_request *req, char *type, char *fname)
{
	struct tcp_buffer_data tcpbuf;
	tcp_init(&tcpbuf);

	struct stat fstat;
	if ( stat( fname, &fstat)<0 ) {
		// Not found
	}
	else {
		FILE *fd = fopen( fname, "r");
		if (fd==NULL) {
			// ERROR
		}
		else {
			char buf[1024];
			sprintf( buf, "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %d\r\nConnection: close\r\n\r\n", type, (int)fstat.st_size);
			tcp_write(&tcpbuf, sock, buf, strlen(buf) );
			while (!feof(fd)) {
				int result = fread (buf,1, sizeof(buf),fd);
				if (result>0) tcp_write(&tcpbuf, sock, (char*)buf, result );
			}
			fclose(fd);
		}
	}
	tcp_flush(&tcpbuf, sock);
}


void http_send_answer(int sock, http_request *req, char *type, char *buf, int size)
{
	char http_buf[2048];
	struct tcp_buffer_data tcpbuf;
	tcp_init(&tcpbuf);
	sprintf( http_buf, "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %d\r\nConnection: close\r\n\r\n", type, size);
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	tcp_write(&tcpbuf, sock, (char*)buf, size );
	tcp_flush(&tcpbuf, sock);
}

void http_send_image(int sock, http_request *req, unsigned char *buf, int size, char *type)
{
	char http_buf[2048];
	struct tcp_buffer_data tcpbuf;
	tcp_init(&tcpbuf);
	sprintf( http_buf, "HTTP/1.1 200 OK\r\nAccept-Ranges: bytes\r\nCache-Control: private, max-age=86400\r\nContent-Length: %d\r\nConnection: close\r\nContent-Type: image/%s\r\n\r\n", size, type);
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	tcp_write(&tcpbuf, sock, (char*)buf, size );
	tcp_flush(&tcpbuf, sock);
}

void http_send_xml(int sock, http_request *req, char *buf, int size)
{
	char http_buf[2048];
	struct tcp_buffer_data tcpbuf;
	tcp_init(&tcpbuf);
	sprintf( http_buf, "HTTP/1.1 200 OK\r\nContent-Length: %d\r\nAccept-Ranges: bytes\r\nConnection: close\r\nContent-Type: application/xml\r\n\r\n", size);
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	tcp_write(&tcpbuf, sock, (char*)buf, size );
	tcp_flush(&tcpbuf, sock);
}


void http_send_ok(int sock)
{
	char http_buf[100];
	struct tcp_buffer_data tcpbuf;
	tcp_init(&tcpbuf);
	sprintf( http_buf, "HTTP/1.1 200 OK\r\n\r\n");
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	tcp_flush(&tcpbuf, sock);
}


void http_send_text(int sock, char *buf)
{
	int size = strlen(buf);
	struct tcp_buffer_data tcpbuf;
	tcp_init(&tcpbuf);
	//char http_buf[100];
	//sprintf( http_buf, "HTTP/1.1 200 OK\r\nContent-Length: %d\r\n\r\n",size);
	//tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	tcp_write(&tcpbuf, sock, (char*)buf, size );
	tcp_flush(&tcpbuf, sock);
}




void http_send_ecmstatus(struct tcp_buffer_data *tcpbuf, int sock, ECM_DATA *ecm)
{
	char http_buf[2048];
	tcp_writestr(tcpbuf, sock, "<table class=\"infotable\"><tbody>\n" );
	sprintf( http_buf,"<tr><th>Current Ecm Request</th></tr>\n");
	tcp_write(tcpbuf, sock, http_buf, strlen(http_buf) );
	// Status Msg
	if (ecm->statusmsg) {
		sprintf( http_buf,"<tr><td>%s</td></tr>", ecm->statusmsg);
		tcp_write(tcpbuf, sock, http_buf, strlen(http_buf) );
	}
	// Channel name
	sprintf( http_buf,"<tr><td>Channel  %s</td></tr>\n", getchname(ecm->caid, ecm->provid, ecm->sid) );
	tcp_write(tcpbuf, sock, http_buf, strlen(http_buf) );
	// ECM
	sprintf( http_buf,"<tr><td>ECM(%d): ", ecm->ecmlen); tcp_write(tcpbuf, sock, http_buf, strlen(http_buf) );
	array2hex( ecm->ecm, http_buf, ecm->ecmlen );	tcp_write(tcpbuf, sock, http_buf, strlen(http_buf) );
	sprintf( http_buf,"</td></tr>\n");
	// Last DCW
#ifdef CHECK_NEXTDCW
	if ( ecm->lastdecode.ecm && (ecm->lastdecode.counter>0) ) {
		sprintf( http_buf,"<tr><td>Previous CW: "); tcp_write(tcpbuf, sock, http_buf, strlen(http_buf) );
		array2hex( ecm->lastdecode.dcw, http_buf, 16 ); tcp_write(tcpbuf, sock, http_buf, strlen(http_buf) );
		tcp_writestr(tcpbuf, sock, "</td></tr>\n");
		if ((ecm->lastdecode.cwcycle&0xFE)=='0') sprintf( http_buf,"<tr><td>Next Cycle = CW%c</td></tr>\n", ecm->lastdecode.cwcycle);
		tcp_write(tcpbuf, sock, http_buf, strlen(http_buf) );
		if (ecm->lastdecode.error) {
			sprintf( http_buf,"<tr><td>Errors = %d</td></tr>\n", ecm->lastdecode.error);
			tcp_write(tcpbuf, sock, http_buf, strlen(http_buf) );
		}
		sprintf( http_buf,"<tr><td>Total Cycles = %d</td></tr>\n<tr><td>ECM Interval = %ds</td></tr>\n", ecm->lastdecode.counter, ecm->lastdecode.dcwchangetime/1000);
		tcp_write(tcpbuf, sock, http_buf, strlen(http_buf) );
	}
#endif
	// Servers
	if (ecm->server[0].srvid) {
		sprintf( http_buf, "<tr><td><table class='infotable'><tbody><tr><th width='30px'>ID</th><th width='250px'>Server</th><th width='50px'>Status</th><th width='70px'>Start time</th><th width='70px'>End time</th><th width='90px'>Elapsed time</th><th>CW</th></tr></tbody>");
		tcp_write(tcpbuf, sock, http_buf, strlen(http_buf) );
		int i;
		for(i=0; i<20; i++) {
			if (!ecm->server[i].srvid) break;
			char* str_srvstatus[] = { "WAIT", "OK", "NOK", "BUSY" };
			struct server_data *srv = getsrvbyid(ecm->server[i].srvid);
			if (srv) {
				sprintf( http_buf,"<tr><td>%d</td><td>%s:%d</td><td>%s</td><td>%dms</td>", i+1, srv->host->name, srv->port, str_srvstatus[ecm->server[i].flag], ecm->server[i].sendtime - ecm->recvtime );
				tcp_write(tcpbuf, sock, http_buf, strlen(http_buf) );
				// Recv Time
				if (ecm->server[i].statustime>ecm->server[i].sendtime)
					sprintf( http_buf,"<td>%dms</td><td>%dms</td>", ecm->server[i].statustime - ecm->recvtime, ecm->server[i].statustime-ecm->server[i].sendtime );
				else
					sprintf( http_buf,"<td>--</td><td>--</td>");
				tcp_write(tcpbuf, sock, http_buf, strlen(http_buf) );
				// DCW
				if (ecm->server[i].flag==ECM_SRV_REPLY_GOOD) {
					sprintf( http_buf,"<td>"); tcp_write(tcpbuf, sock, http_buf, strlen(http_buf) );
					array2hex( ecm->server[i].dcw, http_buf, 16 );	tcp_write(tcpbuf, sock, http_buf, strlen(http_buf) );
					sprintf( http_buf,"</td>"); tcp_write(tcpbuf, sock, http_buf, strlen(http_buf) );
				}
				else {
					sprintf( http_buf,"<td>--</td>");
					tcp_write(tcpbuf, sock, http_buf, strlen(http_buf) );
				}
				sprintf( http_buf,"</tr>");
				tcp_write(tcpbuf, sock, http_buf, strlen(http_buf) );
			}
		}
		tcp_writestr(tcpbuf, sock, "</tbody></table></td></tr>\n" );
	}
	// End of table
	tcp_writestr(tcpbuf, sock, "</tbody></table><br>\n");
}



void flagdebugvalue( char *str )
{
	uint32_t i,j,k;
	i = (flagdebug>>24);
	j = (flagdebug>>16)&0xff;
	k = flagdebug&0xffff;
	strcpy( str, "UNKNOWN");
	switch (i) {
		case DBG_ALL:
			strcpy( str, "ALL");
			break;
		case DBG_NEWCAMD:
			if (!j) strcpy( str, "PROFILES");
			else {
				struct cardserver_data *cs = getcsbyid(j);
				if (cs) {
					if (!k) sprintf( str, "[%s]", cs->name);
					else {
						struct cs_client_data *cli = getnewcamdclientbyid(k);
						if (cli) { char hesc[512]; html_esc(hesc,(int)sizeof(hesc),cli->user); sprintf( str, "[%s] Newcamd Client '%s'", cs->name, hesc); }
						else sprintf( str, "[%s] Unknown Newcamd Client ID=%d", cs->name, k);
					}
				} else sprintf( str, "Unknown Profile ID=%d", j);
			}
			break;
		case DBG_CCCAM:
			if (!j) strcpy( str, "CCCAM");
			else {
				struct cccam_server_data *cc = getcccamserverbyid(j);
				if (cc) {
					if (!k) sprintf( str, "CCcam%d [%d]", cc->id, cc->port);
					else {
						struct cc_client_data *cli = getcccamclientbyid(k);
						if (cli) { char hesc[512]; html_esc(hesc,(int)sizeof(hesc),cli->user); sprintf( str, "CCcam%d - Client '%s'", cc->id, hesc); }
						else sprintf( str, "CCcam%d - Unknown Client ID=%d", cc->id, k);
					}
				} else sprintf( str, "Unknown CCcam Server ID=%d", j);
			}
			break;
		case DBG_SERVER:
			if (!k) strcpy( str, "SERVERS");
			else {
				struct server_data *srv = getsrvbyid(k);
				if (srv) 
					sprintf( str, "Server (%s:%d)", srv->host->name, srv->port);
				else sprintf( str, "Unknown Server ID=%d", k);
			}
			break;
		case DBG_MGCAMD:
			if (!k) strcpy( str, "MGCAMD");
			else {
				struct mg_client_data *cli = getmgcamdclientbyid(k);
				if (cli) {
					char hesc[512]; html_esc(hesc,(int)sizeof(hesc),cli->user);
					sprintf( str, "Mgcamd Client (%s)", hesc);
				}
				else sprintf( str, "Unknown Mgcamd Client ID=%d", k);
			}
			break;
		case DBG_ERROR:
			strcpy( str, "ERROR");
			break;

	}
}


void http_send_index(int sock, http_request *req)
{
	char http_buf[2048];
	struct tcp_buffer_data tcpbuf;

	// Action
	char *str_action = isset_get( req, "action");
	int get_action = 0;
	if (str_action) {
		if (!strcmp(str_action,"div")) get_action = 1;
		else if (!strcmp(str_action,"row")) get_action = 2;
		else str_action = NULL;
	}
	if (!str_action) str_action = "page";
	//
	tcp_init(&tcpbuf);
	if (get_action==0) {
		http_replyok_write(req, &tcpbuf, sock);
		http_html_write(req, &tcpbuf, sock);
		tcp_write(&tcpbuf, sock, http_head, strlen(http_head) );
		sprintf( http_buf, html_title, cfg.http.title, "Home"); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		tcp_write(&tcpbuf, sock, http_link, strlen(http_link) );
		http_style_write(&tcpbuf, sock);
		// JS
		tcp_writestr(&tcpbuf, sock, "\n<script type='text/javascript'>");
		// ACTIONS REQUEST
		tcp_writestr(&tcpbuf, sock, "\nfunction imgrequest( url, el )\n{\n	var httpRequest;\n	try { httpRequest = new XMLHttpRequest(); }\n	catch (trymicrosoft) { try { httpRequest = new ActiveXObject('Msxml2.XMLHTTP'); } catch (oldermicrosoft) { try { httpRequest = new ActiveXObject('Microsoft.XMLHTTP'); } catch(failed) { httpRequest = false; } } }\n	if (!httpRequest) { alert('Your browser does not support Ajax.'); return false; }\n	if ( typeof(el)!='undefined' ) {\n		el.onclick = null;\n		el.style.opacity = '0.7';\n		httpRequest.onreadystatechange = function()\n		{\n			if (httpRequest.readyState == 4) if (httpRequest.status == 200) el.style.opacity = '0.3';\n		}\n	}\n	httpRequest.open('GET', url, true);\n	httpRequest.send(null);\n}\n");
		// UPD DIV
		char url[256];
		sprintf( url, "/?action=div");
		sprintf( http_buf, HTTP_UPDATE_DIV, cfg.http.autorefresh*1000, url);
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		//
		tcp_writestr(&tcpbuf, sock, "\nfunction start()\n{\n	 setautorefresh(autorefresh);\n}");
		tcp_writestr(&tcpbuf, sock, "\n</script>\n");
		tcp_write(&tcpbuf, sock, http_head_, strlen(http_head_) );
		tcp_writestr(&tcpbuf, sock, "<body onload=\"start();\">");
		tcp_write_menu(&tcpbuf, sock,PAGE_HOME);
		// DIV
		tcp_writestr(&tcpbuf, sock, "<div id='mainDiv'>");
	}

	tcp_writestr(&tcpbuf, sock, "<div style=\"margin-left:10px;\">");

	unsigned int d= GetTickCount()/1000;
	sprintf( http_buf,"Uptime: %02dd %02d:%02d:%02d", d/(3600*24), (d/3600)%24, (d/60)%60, d%60); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );

	FILE *fp = fopen ("/proc/loadavg", "r");
	if (fp) {
		float avg;
		int i = fscanf(fp, "%f", &avg);
		sprintf( http_buf,"<br>Load Average: %01.2f", avg); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		fclose(fp);
	}

	fp = fopen ("/proc/meminfo", "r");
	if (fp) {
		int memfree;
		int i = fscanf (fp, "MemTotal: %*d kB MemFree: %d kB", &memfree);
		sprintf( http_buf,"<br>Free Memory: %d kB", memfree); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		fclose(fp);
	}
	sprintf( http_buf, "<br>Total Profiles: %d", cfg.totalprofiles ); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	sprintf( http_buf, "<br>Total Servers: %d", cfg.totalservers ); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	sprintf( http_buf, "<br>Total Cache Servers: %d", cfg.cache.totalservers ); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	sprintf( http_buf, "<br>Total CCcam Servers: %d", cfg.cccam.totalservers ); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	sprintf( http_buf, "<br>Total Mgcamd Servers: %d", cfg.mgcamd.totalservers ); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	sprintf( http_buf, "<br>Total Camd35 Servers: %d", cfg.camd35.totalservers ); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	sprintf( http_buf, "<br>Total cs378x Servers: %d", cfg.cs378x.totalservers ); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	/*
	 * TASK 3.12 -- DCW stats on the home page, inside the refreshed div.
	 *
	 * Same names as telnet `dcwstats` (dcwstats_reason_name), so the two
	 * readouts cannot drift. Every string is html_esc'd before it is
	 * interpolated (3.2): the names are ours today, and a later name with
	 * '&' must not break the page. Counts are printed with %lu, never as
	 * text from outside. action=div includes this block, so the page's own
	 * refresh moves the numbers. A stock build has no counters and must
	 * not reference the symbol; it says so, once, like telnet.
	 */
#ifdef MCS_DCWSTATS
	{
		char hesc[64];
		int i;
		html_esc(hesc, (int)sizeof(hesc), dcwstats_on ? "ON" : "OFF");
		sprintf(http_buf, "<br><br><b>DCW stats</b> %s<table class=option border=1px cellspacing=0><tr><th>Reason</th><th>Count</th></tr>", hesc);
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf));
		for (i = 0; i < DCW_REJ_COUNT; i++) {
			html_esc(hesc, (int)sizeof(hesc), dcwstats_reason_name(i));
			sprintf(http_buf, "<tr><td>%s</td><td>%lu</td></tr>", hesc, dcwstats[i]);
			tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf));
		}
		tcp_writestr(&tcpbuf, sock, "</table>");
	}
#else
	{
		char hesc[96];
		html_esc(hesc, (int)sizeof(hesc), "this build has no counters; use the stats release");
		sprintf(http_buf, "<br><br><b>DCW stats</b> %s", hesc);
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf));
	}
#endif

	/* TASK R7 (D61): the stock forum link is gone -- the skin keeps the
	 * pages free of any external reference, so an air-gapped box or the
	 * sandboxed preview renders exactly the same. */
	tcp_writestr(&tcpbuf, sock, "</div>");

	if (get_action==0) tcp_writestr(&tcpbuf, sock, "</body></html>");
	tcp_flush(&tcpbuf, sock);
}


void http_send_debug(int sock, http_request *req)
{
	char http_buf[2048];
	struct tcp_buffer_data tcpbuf;

	// Action
	char *str_action = isset_get( req, "action");
	int get_action = 0;
	if (str_action) {
		if (!strcmp(str_action,"div")) get_action = 1;
		else if (!strcmp(str_action,"row")) get_action = 2;
		else if (!strcmp(str_action,"debug")) {
			get_action = 3;
			char *str_value = isset_get( req, "value");
			if (str_value) {
				if (!strcmp(str_value,"ALL")) flagdebug = getdbgflag( DBG_ALL, 0, 0);
				//else if (!strcmp(str_value,"CONFIG")) flagdebug = getdbgflag( DBG_CONFIG, 0, 0);
				else if (!strcmp(str_value,"SERVER")) flagdebug = getdbgflag( DBG_SERVER, 0, 0);
				else if (!strcmp(str_value,"CACHE")) flagdebug = getdbgflag( DBG_CACHE, 0, 0);
				else if (!strcmp(str_value,"NEWCAMD")) flagdebug = getdbgflag( DBG_NEWCAMD, 0, 0);
				else if (!strcmp(str_value,"MGCAMD")) flagdebug = getdbgflag( DBG_MGCAMD, 0, 0);
				else if (!strcmp(str_value,"CCCAM")) flagdebug = getdbgflag( DBG_CCCAM, 0, 0);
#ifdef CS378X_SRV
				else if (!strcmp(str_value,"CS378X")) flagdebug = getdbgflag( DBG_CS378X, 0, 0);
#endif
#ifdef CACHEEX
				else if (!strcmp(str_value,"CACHEEX")) flagdebug = getdbgflag( DBG_CACHEEX, 0, 0);
#endif
				//else if (!strcmp(str_value,"HTTP")) flagdebug = getdbgflag( DBG_HTTP, 0, 0);
				else if (!strcmp(str_value,"ERROR")) flagdebug = getdbgflag( DBG_ERROR, 0, 0);
				//else return;
				http_send_ok(sock);
			}
			return;
		}
		else str_action = NULL;
	}
	if (!str_action) str_action = "page";
	//
	tcp_init(&tcpbuf);
	if (get_action==0) {
		http_replyok_write(req, &tcpbuf, sock);
		http_html_write(req, &tcpbuf, sock);
		tcp_write(&tcpbuf, sock, http_head, strlen(http_head) );
		sprintf( http_buf, html_title, cfg.http.title, "Home"); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		tcp_write(&tcpbuf, sock, http_link, strlen(http_link) );
		http_style_write(&tcpbuf, sock);
		// JS
		tcp_writestr(&tcpbuf, sock, "\n<script type='text/javascript'>");
		// ACTIONS REQUEST
		tcp_writestr(&tcpbuf, sock, "\nfunction imgrequest( url, el )\n{\n	var httpRequest;\n	try { httpRequest = new XMLHttpRequest(); }\n	catch (trymicrosoft) { try { httpRequest = new ActiveXObject('Msxml2.XMLHTTP'); } catch (oldermicrosoft) { try { httpRequest = new ActiveXObject('Microsoft.XMLHTTP'); } catch(failed) { httpRequest = false; } } }\n	if (!httpRequest) { alert('Your browser does not support Ajax.'); return false; }\n	if ( typeof(el)!='undefined' ) {\n		el.onclick = null;\n		el.style.opacity = '0.7';\n		httpRequest.onreadystatechange = function()\n		{\n			if (httpRequest.readyState == 4) if (httpRequest.status == 200) el.style.opacity = '0.3';\n		}\n	}\n	httpRequest.open('GET', url, true);\n	httpRequest.send(null);\n}\n");
		// UPD DIV
		char url[256];
		sprintf( url, "/debug?action=div");
		sprintf( http_buf, HTTP_UPDATE_DIV, cfg.http.autorefresh*1000, url);
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		//
		tcp_writestr(&tcpbuf, sock, "\nfunction start()\n{\n	 setautorefresh(autorefresh);\n}");
		tcp_writestr(&tcpbuf, sock, "\n</script>\n");
		tcp_write(&tcpbuf, sock, http_head_, strlen(http_head_) );
		tcp_writestr(&tcpbuf, sock, "<body onload=\"start();\">");
		tcp_write_menu(&tcpbuf, sock,PAGE_DEBUG);
		// DIV
		tcp_writestr(&tcpbuf, sock, "<div id='mainDiv'>");
	}

	if (!cfg.http.show.nodebug) {
		tcp_writestr(&tcpbuf, sock, "<br>\n<fieldset><legend> Debug: <select onchange=\"imgrequest('/debug?action=debug&amp;value='+this.value);\" style='width:250px;'>");
		int sel;
		if ( (flagdebug&0xffffff)!=0 ) {
			char str[255];
			flagdebugvalue( str );
			sprintf( http_buf, "<option>%s</option>",str);
			tcp_writestr(&tcpbuf, sock, http_buf);
			sel = 0x10;
		} else sel = (flagdebug>>24);
		if (sel==DBG_ALL) tcp_writestr(&tcpbuf, sock, "<option value='ALL' selected>ALL</option>"); else tcp_writestr(&tcpbuf, sock, "<option value='ALL'>ALL</option>");
		if (sel==DBG_SERVER) tcp_writestr(&tcpbuf, sock, "<option value='SERVER' selected>SERVERS</option>"); else tcp_writestr(&tcpbuf, sock, "<option value='SERVER'>SERVERS</option>");
		if (sel==DBG_CACHE) tcp_writestr(&tcpbuf, sock, "<option value='CACHE' selected>CACHE</option>"); else tcp_writestr(&tcpbuf, sock, "<option value='CACHE'>CACHE</option>"); 
		if (sel==DBG_NEWCAMD) tcp_writestr(&tcpbuf, sock, "<option value='NEWCAMD' selected>PROFILES</option>"); else tcp_writestr(&tcpbuf, sock, "<option value='NEWCAMD'>PROFILES</option>");
		if (sel==DBG_MGCAMD) tcp_writestr(&tcpbuf, sock, "<option value='MGCAMD' selected>MGCAMD</option>"); else tcp_writestr(&tcpbuf, sock, "<option value='MGCAMD'>MGCAMD</option>"); 
		if (sel==DBG_CCCAM) tcp_writestr(&tcpbuf, sock, "<option value='CCCAM' selected>CCCAM</option>"); else tcp_writestr(&tcpbuf, sock, "<option value='CCCAM'>CCCAM</option>");
#ifdef CS378X_SRV
		if (sel==DBG_CS378X) tcp_writestr(&tcpbuf, sock, "<option value='CS378X' selected>CS378X</option>"); else tcp_writestr(&tcpbuf, sock, "<option value='CS378X'>CS378X</option>"); 
#endif
#ifdef CACHEEX
		if (sel==DBG_CACHEEX) tcp_writestr(&tcpbuf, sock, "<option value='CACHEEX' selected>CACHEEX</option>"); else tcp_writestr(&tcpbuf, sock, "<option value='CACHEEX'>CACHEEX</option>"); 
#endif
		if (sel==DBG_ERROR) tcp_writestr(&tcpbuf, sock, "<option value='ERROR' selected>ERROR</option>"); else tcp_writestr(&tcpbuf, sock, "<option value='ERROR'>ERROR</option>");
		tcp_writestr(&tcpbuf, sock, "</select></legend>\n");
		sprintf( http_buf, "<pre style=\"font-size:10; color:#004455;\">"); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		int current = idbgline;
		int i = current - 35;
		if (i<0) i += 70;
		do {
			{ char hesc[3200]; html_esc(hesc,(int)sizeof(hesc),dbgline[i]);
			  tcp_write(&tcpbuf, sock, hesc, strlen(hesc)); }	/* TASK 3.2 -- log lines are text, not markup */
			i++;
			if (i>=MAX_DBGLINES) i=0;
		} while (i!=current);
		sprintf( http_buf, "</pre></fieldset>"); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	}

	if (get_action==0) {
		tcp_writestr(&tcpbuf, sock, "</div></body></html>");
	}
	tcp_flush(&tcpbuf, sock);
}

void http_send_restart(int sock, http_request *req)
{
	char http_buf[2048];
	struct tcp_buffer_data tcpbuf;
	tcp_init(&tcpbuf);
	http_replyok_write(req, &tcpbuf, sock);
	http_html_write(req, &tcpbuf, sock);
	tcp_write(&tcpbuf, sock, http_head, strlen(http_head) );
	sprintf( http_buf, html_title, cfg.http.title, "Restarting"); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	//tcp_write(&tcpbuf, sock, http_link, strlen(http_link) );
	//tcp_write(&tcpbuf, sock, http_style, strlen(http_style) );
	tcp_write(&tcpbuf, sock, http_head_, strlen(http_head_) );
	tcp_write(&tcpbuf, sock, http_body, strlen(http_body) );
	//tcp_write_menu(&tcpbuf, sock,PAGE_RESTART);
	sprintf( http_buf, "<script type=\"text/JavaScript\"><!--\nsetTimeout(\"location.href = '/';\",5000);\n--></script>\n<h3>Restarting %s<br>Plesase Wait...</h3>", cfg.http.title);
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	tcp_flush(&tcpbuf, sock);
	flag_debugfile = 1;
	debugf( 0 , " Restart: from http server\n");
	prg.restart = 1;
}


///////////////////////////////////////////////////////////////////////////////
// SERVERS
///////////////////////////////////////////////////////////////////////////////

char *srvtypename(struct server_data *srv)
{
	static char newcamd[] = "Newcamd";
	static char cccam[] = "CCcam";
	static char radegast[] = "Radegast";
#ifdef CACHEEX
	static char cacheex[] = "CacheEX";
	if (srv->cacheex_mode && (srv->type==TYPE_CCCAM)) return cacheex;
#endif
	if (srv->type==TYPE_NEWCAMD) return newcamd;
	if (srv->type==TYPE_CCCAM) return cccam;
	if (srv->type==TYPE_RADEGAST) return radegast;
	return NULL;
}
	

int srv_cardcount(struct server_data *srv, int uphops)
{
	int count=0;
	struct cs_card_data *card = srv->card;
	while (card) {
		if ( (uphops==-1) 
#ifdef CCCAM_CLI
			|| (card->uphops==uphops)
#endif
		) count++;
		card = card->next;
	}
	return count;
}


char *xmlescape( char *str )
{
// "   &quot;
// '   &apos;
// <   &lt;
// >   &gt;
// &   &amp;
	/* TASK 3.9 -- was char exml[5000] with an unbounded dest walk, then an
	 * unchecked strcpy() of the result back into the caller's buffer.
	 * Every call site in this file passes a char[N][2048] cell row
	 * (grep-verified: 8, 10, 11 and 12 rows, all 2048 wide), so the
	 * frame is the cell edge: escaped text caps at the frame minus room
	 * for one 6-char entity, the copy-back can never write past the
	 * caller's cell, and the web thread's stack loses 3KB of latent
	 * overflow. Honest cells (a few hundred chars) keep upstream's
	 * bytes and entities; only text that would escape longer is
	 * truncated, with one warning, and the page stays valid. */
	char exml[2048];
	char *src = str;
	char *dest = exml;
	char * const xend = exml + sizeof(exml) - 7;	/* one 6-char entity + NUL */
	while (*src) {
		if (dest >= xend) {	/* TASK 3.9 -- the frame edge */
			debugf( getdbgflag(DBG_HTTP,0,0), "xmlescape: cell text too long, truncated at the cell edge; the page stays valid");
			break;
		}
		switch (*src) {
			case '&':
				memcpy(dest,"&amp;", 5);
				dest +=5;
				break;				
			case '<':
				memcpy(dest,"&lt;", 4);
				dest +=4;
				break;				
			case '>':
				memcpy(dest,"&gt;", 4);
				dest +=4;
				break;				
			case '"':
				memcpy(dest,"&quot;", 6);
				dest +=6;
				break;				
			case '\'':
				memcpy(dest,"&apos;", 6);
				dest +=6;
				break;				
			default:
				*dest = *src;
				dest++;
		}
		src++;
	}
	*dest = 0;
	strcpy( str, exml);
	return str;
}

/* R35 -- escape a RAW TEXT field (peer/client-supplied strings: user,
 * realname, program, version, statmsg, host names) for embedding in a
 * composed cell, via the TASK 3.2 house helper html_esc(). The page's
 * own markup is built AROUND the escaped text, never through here; the
 * XML branches still run xmlescape() over the finished cell as
 * transport encoding (their JS injects via innerHTML after the XML
 * parser un-escapes). Blanket-escaping composed cells on the HTML
 * branches (TASK 3.2/3.9) made browsers print the page's own tags as
 * text -- the mangled pages. buf must hold 1024 bytes (html_esc bound). */
static char *escf( char *buf, const char *src )
{
	html_esc( buf, 1024, src );
	return buf;
}

char *providerID( unsigned short caid, unsigned int provid )
{
	unsigned int caprovid = (caid<<16) | provid;
	struct providers_data *prov = cfg.providers;
	while (prov) {
		if (prov->caprovid==caprovid) return prov->name;
		prov = prov->next;
	}
	return NULL;
}

void getservercells(struct server_data *srv, char cell[8][2048] )
{
	char temp[2048];
	char et1[1024], et2[1024];	/* R35 text-field scratch */
	unsigned int ticks = GetTickCount();
	uint d;
	int i;
	memset(cell, 0, 8*2048);
	// CELL0
	uint32_t uptime;
	if (srv->connection.status>0) uptime = (ticks-srv->connection.time) + srv->connection.uptime; else uptime = srv->connection.uptime;
	d = uptime / (ticks/100);
	if (d>100) d = 100;	/* R35: reconnect-accumulated uptime can exceed 100% of process life (the "266%" cell) */
	uptime /= 1000;
	sprintf( cell[0],"<span title='%02dd %02d:%02d:%02d'>%d%%</span>",uptime/(3600*24),(uptime/3600)%24,(uptime/60)%60,uptime%60 ,d);

	// CELL1
	sprintf( cell[1],"<a href=\"/server?id=%d\">%s:%d</a><br>", srv->id,escf(et1,srv->host->name),srv->port);
	if (!srv->host->ip && srv->host->clip)
		sprintf( temp,"0.0.0.0 (%s)",(char*)ip2string(srv->host->ip) );
	else {
		char *p = getcountrycodebyip(srv->host->ip);
		if (p) sprintf( temp,"<img src='/flag_%s.gif' title='%s'> %s", p, getcountryname(p), (char*)ip2string(srv->host->ip) ); else sprintf( temp,"%s",(char*)ip2string(srv->host->ip) );
	}
	strcat( cell[1], temp );
	// CELL2
	if (srv->type==TYPE_NEWCAMD) {
		if (srv->progname) {
			if (srv->version) sprintf( cell[2],"%s %s", escf(et1,srv->progname), escf(et2,srv->version));
			else strcpy( cell[2], escf(et1,srv->progname));
		}
		else sprintf( cell[2],"Newcamd");
	}
#ifdef CCCAM_CLI
	else if (srv->type==TYPE_CCCAM) {
		if (srv->handle>0)
			sprintf( cell[2],"%s %s<br>%02x%02x%02x%02x%02x%02x%02x%02x", escf(et1,srv->progname), escf(et2,srv->version), srv->nodeid[0],srv->nodeid[1],srv->nodeid[2],srv->nodeid[3],srv->nodeid[4],srv->nodeid[5],srv->nodeid[6],srv->nodeid[7]);
		else sprintf( cell[2],"CCcam");
#ifdef CACHEEX
		if (srv->cacheex_mode) strcat( cell[2], "<br>CacheEX");
#endif
		//if (srv->progname) sprintf( cell[2],"<td>CCcam(%s) %s", srv->progname, srv->version); else sprintf( cell[2],"<td>CCcam %s", srv->version);
	}
#endif
#ifdef RADEGAST_CLI
	else if (srv->type==TYPE_RADEGAST) sprintf( cell[2],"Radegast");
#endif
#ifdef CAMD35_CLI
	else if (srv->type==TYPE_CAMD35) sprintf( cell[2],"Camd35");
#endif
#ifdef CS378X_CLI
	else if (srv->type==TYPE_CS378X) sprintf( cell[2],"cs378x");
#endif
	else sprintf( cell[2],"Unknown");

	// CELL3
	if (srv->connection.status>0) {
		d = (ticks-srv->connection.time)/1000;
		sprintf( cell[3],"%02dd %02d:%02d:%02d", d/(3600*24),(d/3600)%24,(d/60)%60,d%60);
		if (srv->busy) sprintf( cell[7],"busy"); else sprintf( cell[7],"online");
	}
	else {
		sprintf( cell[7],"offline");
		if (srv->flags&FLAG_DELETE) sprintf( cell[3],"Removed");
		else if (srv->flags&FLAG_EXPIRED) sprintf( cell[3],"Expired");
		else if (srv->flags&FLAG_DISABLE) sprintf( cell[3],"Disabled");
		else sprintf( cell[3],"offline");
	}

#ifdef CCCAM_CLI
#ifdef CACHEEX
	if (srv->cacheex_mode) {
		sprintf( cell[4],"%d",srv->ecmnb);
		strcpy( cell[5], " "); // default
	}
	else
#endif
#endif
	{
		// CELL4
		if (srv->ecmnb)
			sprintf( cell[4],"%d / %d<span style=\"float: right;\">%d%%</span><br>Hits = %d",srv->ecmok ,srv->ecmnb, (srv->ecmok*100)/srv->ecmnb, srv->hits);
		else
			sprintf( cell[4],"<span style=\"float: right;\">0%%</span>");
		// CELL5
		if (srv->ecmok)
			sprintf( cell[5],"%d ms",(srv->ecmoktime/srv->ecmok) ); //, srv->hits );
		else
			sprintf( cell[5],"-- ms");
	}

	// CELL6
	strcpy( cell[6], " "); // default
	if (srv->connection.status>0) {
		if (srv->type==TYPE_CCCAM)
			sprintf( temp,"<b>Total Cards = %d</b> ( Hop1 = %d, Hop2 = %d )<font style=\"font-size: 9;\">", srv_cardcount(srv,-1), srv_cardcount(srv,1), srv_cardcount(srv,2) );
		else
			sprintf( temp,"<b>Total Cards = %d</b><font style=\"font-size: 9;\">", srv_cardcount(srv,-1) );
		strcpy( cell[6], temp );
		int icard = 0;
		struct cs_card_data *card = srv->card;
		while (card) {
			if (card->uphops<=1) {
				if (icard>3) {
					strcat( cell[6], "<br> ..." );
					break;
				}
				char *provname = providerID(card->caid,card->prov[0]);
				if (provname) sprintf( temp,"<br><b>%04x:</b> %x <font color=#CC3300>%s</font>",card->caid,card->prov[0], provname); else sprintf( temp,"<br><b>%04x:</b> %x",card->caid,card->prov[0]);
				strcat( cell[6], temp );
				for(i=1; i<card->nbprov; i++) {
					char *provname = providerID(card->caid,card->prov[i]);
					if (provname) sprintf( temp,", %x <font color=#CC3300>%s</font>", card->prov[i], provname); else sprintf( temp,", %x", card->prov[i]);
					if ( (strlen(cell[6])+strlen(temp))<sizeof(cell[6]) )  strcat( cell[6], temp );
				}
				icard++;
			}
			card = card->next;
		}
		strcat( cell[6],"</font>\0");
	}
	else {
		if (srv->statmsg) {
			if (srv->connection.lastseen) {
				d = (ticks-srv->connection.lastseen)/1000;
				sprintf( temp,"%s<br>Last Seen %02dd %02d:%02d:%02d", escf(et1,srv->statmsg), d/(3600*24),(d/3600)%24,(d/60)%60,d%60);
			}
			else sprintf( temp,"%s",escf(et1,srv->statmsg));
			strcpy( cell[6], temp );
		}
	}

	strcat( cell[6], "<span style='float:right;'>");
	if ( !(srv->flags&(FLAG_DELETE|FLAG_EXPIRED)) ) {
		if (srv->flags&FLAG_DISABLE) {
			sprintf( temp," <img title='Enable' src='enable.png' OnClick=\"imgrequest('/server?id=%d&action=enable',this);\">",srv->id);
			strcat( cell[6], temp );
		}
		else {
			sprintf( temp," <img title='disable' src='disable.png' OnClick=\"imgrequest('/server?id=%d&action=disable',this);\">",srv->id);
			strcat( cell[6], temp );
		}
	}
	sprintf( temp," <img title='Debug' src='debug.png' OnClick=\"imgrequest('/server?id=%d&action=debug',this);\">",srv->id);
	strcat( cell[6], temp );
	strcat( cell[6], "</span>");
}

void alltotal_servers( int *all, int *cccam, int *newcamd, int *radegast )
{
	*all = 0;
	*cccam = 0;
	*newcamd = 0;
	*radegast = 0;

	struct server_data *srv=cfg.server;
	while (srv) {
		(*all)++;
		if (srv->type==TYPE_CCCAM) (*cccam)++;
		else if (srv->type==TYPE_NEWCAMD) (*newcamd)++;
		else if (srv->type==TYPE_RADEGAST) (*radegast)++;
		srv=srv->next;
	}
}

void allconnected_servers( int *all, int *cccam, int *newcamd, int *radegast )
{
	*all = 0;
	*cccam = 0;
	*newcamd = 0;
	*radegast = 0;

	struct server_data *srv=cfg.server;
	while (srv) {
		if ( !IS_DISABLED(srv->flags)&&(srv->handle>0) ) {
			(*all)++;
			if (srv->type==TYPE_CCCAM) (*cccam)++;
			else if (srv->type==TYPE_NEWCAMD) (*newcamd)++;
			else if (srv->type==TYPE_RADEGAST) (*radegast)++;
		}
		srv=srv->next;
	}
}

void http_send_servers(int sock, http_request *req)
{
	char http_buf[5000];
	struct tcp_buffer_data tcpbuf;

	char cell[8][2048];
	struct server_data *srv;
	int i;

	// Action
	char *str_action = isset_get( req, "action");
	int get_action = 0;
	if (str_action) {
		if (!strcmp(str_action,"div")) get_action = 1;
		else if (!strcmp(str_action,"row")) get_action = 2;
		else str_action = NULL;
	}
	if (!str_action) str_action = "page";
	//
	char *str_type = isset_get( req, "type");
	int get_type = 0;
	if (str_type) {
		if (!strcmp(str_type,"cccam"))  get_type = 1;
		else if (!strcmp(str_type,"newcamd")) get_type = 2;
		else if (!strcmp(str_type,"radegast")) get_type = 3;
		else str_type = NULL;
	}
	if (!str_type) str_type = "all";
	//
	char *str_list = isset_get( req, "list");
	int get_list = LIST_ALL;
	if (str_list) {
		if (!strcmp(str_list,"connected")) get_list = LIST_CONNECTED;
		else if (!strcmp(str_list,"disconnected")) get_list = LIST_DISCONNECTED;
		else str_list = NULL;
	}
	if (!str_list) str_list = "all";

	//
	char *id = isset_get( req, "id");
	// Get Server ID
	if (id)	{
		i = atoi(id);
		//look for server
		srv = cfg.server;
		while (srv) {
			if (!(srv->flags&FLAG_DELETE)) {
				if (srv->id==(uint32_t)i) break;
			}
			srv = srv->next;
		}
		if (!srv) return;
		char *action = isset_get( req, "action");
		if (action) {
			if (!strcmp(action,"disable")) {
				srv->flags |= FLAG_DISABLE;
				if (srv->connection.status>0) disconnect_srv(srv);
			}
			else if (!strcmp(action,"enable")) {
				srv->flags &= ~FLAG_DISABLE;
				srv->host->checkiptime = 0;
			}
		}			
		// Send XML CELLS
		getservercells(srv,cell);
		char buf[5000] = "";
		sprintf( buf, "<server>\n<c0>%s</c0>\n<c1>%s</c1>\n<c2>%s</c2>\n<c3_c>%s</c3_c>\n<c3>%s</c3>\n<c4>%s</c4>\n<c5>%s</c5>\n<c6>%s</c6>\n</server>\n",cell[0],cell[1],cell[2],cell[7],cell[3],cell[4],cell[5],cell[6] );
		http_send_xml( sock, req, buf, strlen(buf));
		return;
	}

	tcp_init(&tcpbuf);
	if (get_action==0) {
		http_replyok_write(req, &tcpbuf, sock);
		http_html_write(req, &tcpbuf, sock);
		tcp_write(&tcpbuf, sock, http_head, strlen(http_head) );
		sprintf( http_buf, html_title, cfg.http.title, "Servers"); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		tcp_write(&tcpbuf, sock, http_link, strlen(http_link) );
		http_style_write(&tcpbuf, sock);
		// JS
		tcp_writestr(&tcpbuf, sock, "\n<script type='text/javascript'>");
		// ACTIONS REQUEST
		tcp_writestr(&tcpbuf, sock, "\nfunction imgrequest( url, el )\n{\n	var httpRequest;\n	try { httpRequest = new XMLHttpRequest(); }\n	catch (trymicrosoft) { try { httpRequest = new ActiveXObject('Msxml2.XMLHTTP'); } catch (oldermicrosoft) { try { httpRequest = new ActiveXObject('Microsoft.XMLHTTP'); } catch(failed) { httpRequest = false; } } }\n	if (!httpRequest) { alert('Your browser does not support Ajax.'); return false; }\n	if ( typeof(el)!='undefined' ) {\n		el.onclick = null;\n		el.style.opacity = '0.7';\n		httpRequest.onreadystatechange = function()\n		{\n			if (httpRequest.readyState == 4) if (httpRequest.status == 200) el.style.opacity = '0.3';\n		}\n	}\n	httpRequest.open('GET', url, true);\n	httpRequest.send(null);\n}\n");
		// UPD ROW
		tcp_writestr(&tcpbuf, sock, "\nfunction xmlupdateRow( xmlDoc, id )\n{\n	var row = document.getElementById(id);\n	row.cells.item(0).innerHTML = xmlDoc.getElementsByTagName('c0')[0].childNodes[0].nodeValue;\n	row.cells.item(1).innerHTML = xmlDoc.getElementsByTagName('c1')[0].childNodes[0].nodeValue;\n	row.cells.item(2).innerHTML = xmlDoc.getElementsByTagName('c2')[0].childNodes[0].nodeValue;\n	row.cells.item(3).className = xmlDoc.getElementsByTagName('c3_c')[0].childNodes[0].nodeValue;\n	row.cells.item(3).innerHTML = xmlDoc.getElementsByTagName('c3')[0].childNodes[0].nodeValue;\n	row.cells.item(4).innerHTML = xmlDoc.getElementsByTagName('c4')[0].childNodes[0].nodeValue;\n	row.cells.item(5).innerHTML = xmlDoc.getElementsByTagName('c5')[0].childNodes[0].nodeValue;\n	row.cells.item(6).innerHTML = xmlDoc.getElementsByTagName('c6')[0].childNodes[0].nodeValue;\n}\n" );
		char url[256];
		sprintf( url, "'/servers?id='+idx");
		sprintf( http_buf, HTTP_UPDATE_ROW, url);
/////"\nvar idx = 0;\nvar lastidx = 0;\nvar requestError = 0;\nfunction updateRow()\n{\n	if (lastidx!=idx) {\n		requestError = 0;\n		lastidx = idx;\n	}\n	if ( !requestError && (idx>0) ) {\n		var httpRequest;\n		try {\n			httpRequest = new XMLHttpRequest();  // Mozilla, Safari, etc\n		}\n		catch(trymicrosoft) {\n			try {\n				httpRequest = new ActiveXObject('Msxml2.XMLHTTP');\n			}\n			catch(oldermicrosoft) {\n				try {\n					httpRequest = new ActiveXObject('Microsoft.XMLHTTP');\n				}\n				catch(failed) {\n					httpRequest = false;\n				}\n			}\n		}\n		if (!httpRequest) {\n			alert('Your browser does not support Ajax.');\n			return false;\n		}\n		var savedidx = idx;\n		// Action http_request\n		httpRequest.onreadystatechange = function()\n		{\n			if (httpRequest.readyState == 4) {\n				if (httpRequest.status == 200) {\n					requestError=0;\n					xmlupdateRow( httpRequest.responseXML, 'Row'+savedidx );\n				}\n				else {\n					requestError++;\n				}\n				t = setTimeout('updateRow()',1000);\n			}\n		}\n		httpRequest.open('GET', %s, true);\n		httpRequest.send(null);\n		requestError++;\n	} else t = setTimeout('updateRow()',1000);\n}\n"
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		// UPD DIV
		sprintf( url, "/servers?action=div&type=%s&list=%s", str_type, str_list);
		sprintf( http_buf, HTTP_UPDATE_DIV, cfg.http.autorefresh*1000, url);
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		//
		tcp_writestr(&tcpbuf, sock, "\nfunction start()\n{\n	setautorefresh(autorefresh);\n}");
		tcp_writestr(&tcpbuf, sock, "\n</script>\n");
		tcp_write(&tcpbuf, sock, http_head_, strlen(http_head_) );
		tcp_writestr(&tcpbuf, sock, "<body onload=\"start();\">");
		tcp_write_menu(&tcpbuf, sock,PAGE_SERVERS);
		// DIV
		tcp_writestr(&tcpbuf, sock, "<div id='mainDiv'>");
	}
	//
	int iall, icccam, inewcamd, iradegast; // Total
	alltotal_servers( &iall, &icccam, &inewcamd, &iradegast );
	int jall, jcccam, jnewcamd, jradegast; // Connected
	allconnected_servers( &jall, &jcccam, &jnewcamd, &jradegast );
	//
	int connected = jall;
	int total = iall;
	if (get_type==1) { connected=jcccam; total=icccam; }
	else if (get_type==2) { connected=jnewcamd; total=inewcamd; }
	else if (get_type==3) { connected=jradegast; total=iradegast; }
	//
	tcp_writestr(&tcpbuf, sock, "<select style=\"width:200px;\" onchange=\"parent.location.href='/servers?type='+this.value\">");
	sprintf( http_buf, "<option value=all>All Servers (%d/%d)</option>",jall, iall );
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	if (inewcamd) {
		if (get_type==2) sprintf( http_buf, "<option value=newcamd selected>Newcamd Servers (%d/%d)</option>",jnewcamd,inewcamd );
		else sprintf( http_buf, "<option value=newcamd>Newcamd Servers (%d/%d)</option>",jnewcamd,inewcamd );
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	}
	if (icccam) {
		if (get_type==1) sprintf( http_buf, "<option value=cccam selected>CCcam Servers (%d/%d)</option>",jcccam,icccam );
		else sprintf( http_buf, "<option value=cccam>CCcam Servers (%d/%d)</option>",jcccam,icccam );
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	}
	if (iradegast) {
		if (get_type==3) sprintf( http_buf, "<option value=radegast>Radegast Servers (%d/%d)</option>",jradegast,iradegast );
		else sprintf( http_buf, "<option value=radegast selected>Radegast Servers (%d/%d)</option>",jradegast,iradegast );
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	}
	tcp_writestr(&tcpbuf, sock, "</select>");
	//
	char *class1 = "button"; char *class2 = "sbutton";
	char *class;
	if (get_list==LIST_ALL) class = class2; else class = class1;
	sprintf( http_buf," <input type=button class=%s onclick=\"parent.location='/servers?type=%s&amp;list=all'\" value='All (%d)'>",class,str_type,total);
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	if (get_list==LIST_CONNECTED) class = class2; else class = class1;
	sprintf( http_buf," <input type=button class=%s onclick=\"parent.location='/servers?type=%s&amp;list=connected'\" value='Connected (%d)'>",class,str_type,connected);
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	if (get_list==LIST_DISCONNECTED) class = class2; else class = class1;
	sprintf( http_buf," <input type=button class=%s onclick=\"parent.location='/servers?type=%s&amp;list=disconnected'\" value='Disconnected (%d)'>",class,str_type,total-connected);
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	// Table
	sprintf( http_buf, "<br><table class=maintable width=100%%>\n<tr><th width=20px>Uptime</th><th width=200px>Host</th><th width=100px>Server</th><th width=100px>Connected</th><th width=150px>Ecm OK</th><th width=50px>EcmTime</th><th>Cards</th></tr>\n");
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	srv = cfg.server;
	int alt = 0;

	if (get_type==0) {
		while (srv) {
			if (!(srv->flags&FLAG_DELETE))
			if ( ((get_list&LIST_CONNECTED)&&(srv->handle>0))||((get_list&LIST_DISCONNECTED)&&(srv->handle<=0)) ) {
				if (alt==1) alt=2; else alt=1;
				getservercells(srv,cell);
				sprintf( http_buf,"<tr id=\"Row%d\" class=alt%d onMouseOver='setupdateRow(%d)' onMouseOut='setupdateRow(0)'><td align=\"center\">%s</td><td>%s</td><td>%s</td><td class=\"%s\">%s</td><td>%s</td><td align=\"center\">%s</td><td>%s</td></tr>\n",srv->id,alt,srv->id,cell[0],cell[1],cell[2],cell[7],cell[3],cell[4],cell[5],cell[6]);
				tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			}
			srv = srv->next;
		}
	}
	else if (get_type==1) {
		while (srv) {
			if (!(srv->flags&FLAG_DELETE))
			if (srv->type==TYPE_CCCAM)
			if ( ((get_list&LIST_CONNECTED)&&(srv->handle>0))||((get_list&LIST_DISCONNECTED)&&(srv->handle<=0)) ) {
				if (alt==1) alt=2; else alt=1;
				getservercells(srv,cell);
				sprintf( http_buf,"<tr id=\"Row%d\" class=alt%d onMouseOver='setupdateRow(%d)' onMouseOut='setupdateRow(0)'><td align=\"center\">%s</td><td>%s</td><td>%s</td><td class=\"%s\">%s</td><td>%s</td><td align=\"center\">%s</td><td>%s</td></tr>\n",srv->id,alt,srv->id,cell[0],cell[1],cell[2],cell[7],cell[3],cell[4],cell[5],cell[6]);
				tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			}
			srv = srv->next;
		}
	}
	else if (get_type==2) {
		while (srv) {
			if (!(srv->flags&FLAG_DELETE))
			if (srv->type==TYPE_NEWCAMD)
			if ( ((get_list&LIST_CONNECTED)&&(srv->handle>0))||((get_list&LIST_DISCONNECTED)&&(srv->handle<=0)) ) {
				if (alt==1) alt=2; else alt=1;
				getservercells(srv,cell);
				sprintf( http_buf,"<tr id=\"Row%d\" class=alt%d onMouseOver='setupdateRow(%d)' onMouseOut='setupdateRow(0)'><td align=\"center\">%s</td><td>%s</td><td>%s</td><td class=\"%s\">%s</td><td>%s</td><td align=\"center\">%s</td><td>%s</td></tr>\n",srv->id,alt,srv->id,cell[0],cell[1],cell[2],cell[7],cell[3],cell[4],cell[5],cell[6]);
				tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			}
			srv = srv->next;
		}
	}
	else if (get_type==3) {
		while (srv) {
			if (!(srv->flags&FLAG_DELETE))
			if (srv->type==TYPE_RADEGAST)
			if ( ((get_list&LIST_CONNECTED)&&(srv->handle>0))||((get_list&LIST_DISCONNECTED)&&(srv->handle<=0)) ) {
				if (alt==1) alt=2; else alt=1;
				getservercells(srv,cell);
				sprintf( http_buf,"<tr id=\"Row%d\" class=alt%d onMouseOver='setupdateRow(%d)' onMouseOut='setupdateRow(0)'><td align=\"center\">%s</td><td>%s</td><td>%s</td><td class=\"%s\">%s</td><td>%s</td><td align=\"center\">%s</td><td>%s</td></tr>\n",srv->id,alt,srv->id,cell[0],cell[1],cell[2],cell[7],cell[3],cell[4],cell[5],cell[6]);
				tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			}
			srv = srv->next;
		}
	}
	tcp_writestr(&tcpbuf, sock, "</table>");

	if (get_action==0) {
		tcp_writestr(&tcpbuf, sock, "</div></body></html>");
	}
	tcp_flush(&tcpbuf, sock);
}



void http_send_server(int sock, http_request *req)
{
	char http_buf[2048];
	struct tcp_buffer_data tcpbuf;
	char *provname;

	//
	int get_id;
	char *str_id = isset_get( req, "id");
	if (str_id)	get_id = atoi(str_id); else return;

	//look for server
	struct server_data *srv = getsrvbyid( get_id );
	if (!srv) {
		tcp_init(&tcpbuf);
		http_replyok_write(req, &tcpbuf, sock);
		sprintf( http_buf, "<br>Server not found (id=%d)<br>", get_id);
		tcp_flush(&tcpbuf, sock);
		return;
	}
	//
	// Action
	char *str_action = isset_get( req, "action");
	int get_action = 0;
	if (str_action) {
		if (!strcmp(str_action,"div")) get_action = 1;
		else if (!strcmp(str_action,"row")) get_action = 2;
		else if (!strcmp(str_action,"disable")) get_action = 3;
		else if (!strcmp(str_action,"enable")) get_action = 4;
		else if (!strcmp(str_action,"status")) get_action = 5;
		else if (!strcmp(str_action,"info")) get_action = 6; // XML info
		else if (!strcmp(str_action,"debug")) get_action = 7;
		else str_action = NULL;
	}
	if (!str_action) str_action = "page";
	//
	if (get_action==3) {
		srv->flags |= FLAG_DISABLE;
		if (srv->connection.status>0) disconnect_srv(srv);
		http_send_ok(sock);
		return;
	}
	else if (get_action==4) {
		srv->flags &= ~FLAG_DISABLE;
		srv->host->checkiptime = 0;
		http_send_ok(sock);
		return;
	}
	else if (get_action==5) {
		if (srv->handle>0) http_send_text(sock,"connected"); else http_send_text(sock,"disconnected");
		tcp_flush(&tcpbuf, sock);
		return;
	}
	else if (get_action==7) {
		flagdebug = getdbgflag( DBG_SERVER, 0, srv->id);
		http_send_ok(sock);
		return;
	}
	//

	// Send Server infoPage
	tcp_init(&tcpbuf);
	http_replyok_write(req, &tcpbuf, sock);
	http_html_write(req, &tcpbuf, sock);
	tcp_write(&tcpbuf, sock, http_head, strlen(http_head) );
	sprintf( http_buf, html_title, cfg.http.title, "Server"); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	tcp_write(&tcpbuf, sock, http_link, strlen(http_link) );
	http_style_write(&tcpbuf, sock);
	tcp_write(&tcpbuf, sock, http_head_, strlen(http_head_) );
	tcp_write(&tcpbuf, sock, http_body, strlen(http_body) );
	tcp_write_menu(&tcpbuf, sock,0);

	tcp_writestr(&tcpbuf, sock, "<table width=100%><tr><td style=\"vertical-align:top; width:40%\">");
	//
	tcp_writestr(&tcpbuf, sock, "<table class=infotable><tbody>\n<tr><th colspan=2>Server Informations</th></tr>\n" );
	// Host:Port
	sprintf( http_buf,"<tr><td class=left>Host</td><td class=right>%s : %d</td></tr>\n", srv->host->name, srv->port);
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	// Server Type
	tcp_writestr(&tcpbuf, sock, "<tr><td class=left>Type</td><td class=right>");
	if (srv->type==TYPE_CCCAM) tcp_writestr(&tcpbuf, sock, "CCcam</td></tr>\n");
	else if (srv->type==TYPE_NEWCAMD) tcp_writestr(&tcpbuf, sock, "Newcamd</td></tr>\n");
	else if (srv->type==TYPE_RADEGAST) tcp_writestr(&tcpbuf, sock, "Radegast</td></tr>\n");
	// USER
	sprintf( http_buf,"<tr><td class=left>User</td><td class=right>%s</td></tr>\n",srv->user );
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	// Connection Time
	if (srv->connection.status>0) {
		tcp_writestr(&tcpbuf, sock, "<tr><td class=left>Status</td><td class=right>Connected</td></tr>\n");
		uint32_t d = (GetTickCount()-srv->connection.time)/1000;
		sprintf( http_buf,"<tr><td class=left>Connection time</td><td class=right>%02dd %02d:%02d:%02d</td></tr>\n", d/(3600*24), (d/3600)%24, (d/60)%60, d%60);
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		// IP
		sprintf( http_buf,"<tr><td class=left>IP Address</td><td class=right>%s</td></tr>\n",(char*)ip2string(srv->host->ip) );
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		if (srv->type==TYPE_CCCAM) {
			// Version
			sprintf( http_buf,"<tr><td class=left>Version</td><td class=right>%s</td></tr>\n", srv->version);
			tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			// Nodeid
			sprintf( http_buf,"<tr><td class=left>NodeID</td><td class=right>%02x%02x%02x%02x%02x%02x%02x%02x</td></tr>\n", srv->nodeid[0],srv->nodeid[1],srv->nodeid[2],srv->nodeid[3],srv->nodeid[4],srv->nodeid[5],srv->nodeid[6],srv->nodeid[7]);
			tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		}
	}
	else {
		tcp_writestr(&tcpbuf, sock, "<tr><td class=left>Status</td><td class=right>Disconnected</td></tr>\n");
		if (srv->connection.lastseen) {
			uint32_t d = (GetTickCount()-srv->connection.lastseen)/1000;
			sprintf( http_buf,"<tr><td class=left>Last Seen</td><td class=right>%02dd %02d:%02d:%02d</td></tr>\n", d/(3600*24),(d/3600)%24,(d/60)%60,d%60);
			tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		}
	}
	// UPTIME
	if ( srv->connection.uptime || (srv->connection.status>0) ) {
		uint32_t uptime;
		if (srv->connection.status>0) uptime = (GetTickCount()-srv->connection.time)+srv->connection.uptime; else uptime = srv->connection.uptime;
		uptime /= 1000;
		sprintf( http_buf,"<tr><td class=left>Uptime</td><td class=right>%02dd %02d:%02d:%02d</td></tr>",uptime/(3600*24),(uptime/3600)%24,(uptime/60)%60,uptime%60);
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	}
	// Priority
	sprintf( http_buf,"<tr><td class=left>Priority</td><td class=right>%d</td></tr>", srv->priority);
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	// EOT
	tcp_writestr(&tcpbuf, sock, "</tbody></table><br>\n" );
	if (srv->ecmnb) {
		// Ecm Stat
		tcp_writestr(&tcpbuf, sock, "<table class=\"infotable\"><tbody>\n" );
		tcp_writestr(&tcpbuf, sock, "<tr><th colspan=2>ECM Statistics</th></tr>\n" );
		sprintf( http_buf, "<tr><td class=left>Total ECM requests</td><td class=right>%d</td></tr>\n", srv->ecmnb);
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		sprintf( http_buf, "<tr><td class=left>Good ECM answer</td><td class=right>%d</td></tr>\n", srv->ecmok);
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		//Ecm Time
		if (srv->ecmok) {
			sprintf( http_buf,"<tr><td class=left>Average Time</td><td class=right>%d ms</td></tr>\n",(srv->ecmoktime/srv->ecmok) );
			tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		}
		// EOT
		tcp_writestr(&tcpbuf, sock, "</tbody></table><br>\n" );
	}

	tcp_writestr(&tcpbuf, sock, "</td><td style=\"vertical-align:top;\">");

	if (srv->cstat[0].csid) { //Print used profiles
		sprintf( http_buf, "<br>Used Profiles<br><table class=option><tr><th width=200px>Profile name</th><th width=90px>Total ECM</th><th width=90px>Ecm OK</th><th width=90px>Ecm Time</th></tr>");
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		int alt=0;
		int i;
		for ( i=0; i<MAX_CSPORTS; i++ ) {
			if (!srv->cstat[i].csid) break;
			struct cardserver_data *cs = getcsbyid(srv->cstat[i].csid);
			if (!cs) continue;
			if (alt==1) alt=2; else alt=1;
			//Profile name
			sprintf( http_buf,"<tr><td class=alt%d><a href=\"/profile?id=%d\">%s</a></td>",alt, cs->id, cs->name); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			//TotalECM
			sprintf( http_buf, "<td class=alt%d align=center>%d</td>",alt, srv->cstat[i].ecmnb ); //,cs->ecmdenied);
			tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			//ECM OK
			tcp_writeecmdata(&tcpbuf, sock, srv->cstat[i].ecmok, srv->cstat[i].ecmnb );
			//ECM TIME
			int temp;
			if (srv->cstat[i].ecmok) temp =  srv->cstat[i].ecmoktime/srv->cstat[i].ecmok; else temp=0;
			if (temp)
				sprintf( http_buf, "<td class=alt%d align=center>%dms</td>",alt, temp);
			else
				sprintf( http_buf, "<td class=alt%d align=center>-- ms</td>",alt);
			tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			//Close Row
			sprintf( http_buf,"</tr>");
			tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		}
		sprintf( http_buf,"</table>");
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	}
	// EOT
	tcp_writestr(&tcpbuf, sock, "</td></tr></table>");


		if (srv->handle>0) {
			// Print CardList
			if (srv->type==TYPE_CCCAM) {
				sprintf( http_buf, "<br>Total Cards = %d ( Hop1 = %d, Hop2 = %d )<br><table class=maintable width=100%%><tr><th width=120px>NodeID_CardID</th><th width=150px>EcmOK</th><th width=70px>EcmTime</th><th>Caid/Providers</th></tr>",srv_cardcount(srv,-1), srv_cardcount(srv,1), srv_cardcount(srv,2));
				tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				struct cs_card_data *card = srv->card;
				int alt=0;
				while(card) {
					if (alt==1) alt=2; else alt=1;
#ifdef CCCAM_CLI
					sprintf( http_buf,"<tr><td class=alt%d>%02x%02x%02x%02x%02x%02x%02x%02x_%x</td>",alt, card->nodeid[0], card->nodeid[1], card->nodeid[2], card->nodeid[3], card->nodeid[4], card->nodeid[5], card->nodeid[6], card->nodeid[7], card->shareid);
#else
					sprintf( http_buf,"<tr><td class=alt%d>%x</td>",alt, card->id);
#endif
					tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );

#ifdef CCCAM_CLI
					sprintf( http_buf,"<td class=alt%d>%d / %d<span style=\"float:right\">",alt,card->ecmok,card->ecmnb);
					tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );

					if (card->ecmnb)					
						sprintf( http_buf,"%d%%</span></td>", card->ecmok*100/card->ecmnb);
					else
						sprintf( http_buf,"0%%</span></td>");
					tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );

					if (card->ecmok)
						sprintf( http_buf,"<td class=alt%d align=center>%d ms</td>",alt, card->ecmoktime/card->ecmok );
					else
						sprintf( http_buf,"<td class=alt%d align=center>-- ms</td>",alt);
					tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );

					provname = providerID(card->caid,card->prov[0]);
					if (provname) sprintf( http_buf,"<td class=alt%d>[%d] <b>%04x:</b> %x <font color=#CC3300>%s</font>",alt,card->uphops,card->caid,card->prov[0], provname);
					else sprintf( http_buf,"<td class=alt%d>[%d] <b>%04x:</b> %x",alt,card->uphops,card->caid,card->prov[0]);
					tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
#else
					provname = providerID(card->caid,card->prov[0]);
					if (provname) sprintf( http_buf,"<td class=alt%d><b>%04x:</b> %x <font color=#CC3300>%s</font>",alt,card->caid,card->prov[0], provname);
					else sprintf( http_buf,"<td class=alt%d><b>%04x:</b> %x",alt,card->caid,card->prov[0]);
					tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
#endif
					int i;
					for(i=1; i<card->nbprov; i++) {
						provname = providerID(card->caid,card->prov[i]);
						if (provname) sprintf( http_buf,", %x <font color=#CC3300>%s</font>", card->prov[i], provname); else sprintf( http_buf,", %x", card->prov[i]);
						tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
					}
					sprintf( http_buf,"</td></tr>");
					tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );

					card = card->next;
				}
				sprintf( http_buf,"</table>");
				tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			}
			else {
				sprintf( http_buf, "<br>Cards:<br><table class=maintable width=100%%><tr><th>Caid/Providers</th></tr>");
				tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );

				struct cs_card_data *card = srv->card;
				int alt=0;
				while(card) {
					if (alt==1) alt=2; else alt=1;
					provname = providerID(card->caid,card->prov[0]);
					if (provname) sprintf( http_buf,"<tr><td class=alt%d><b>%04x:</b> %x <font color=#CC3300>%s</font>",alt,card->caid,card->prov[0], provname);
					else sprintf( http_buf,"<tr><td class=alt%d><b>%04x:</b> %x",alt,card->caid,card->prov[0]);
					tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
					int i;
					for(i=1; i<card->nbprov; i++) {
						provname = providerID(card->caid,card->prov[i]);
						if (provname) sprintf( http_buf,", %x <font color=#CC3300>%s</font>", card->prov[i], provname);
						else sprintf( http_buf,", %x", card->prov[i]);
						tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
					}
					sprintf( http_buf,"</td></tr>");
					tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );

					card = card->next;
				}
				sprintf( http_buf,"</table>");
				tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			}
		}

	tcp_flush(&tcpbuf, sock);
}


///////////////////////////////////////////////////////////////////////////////
// [CACHE]
///////////////////////////////////////////////////////////////////////////////

inline int isactivepeer(struct cachepeer_data *peer)
{
	if ( peer->ping>0 ) return 1;
	return 0;
}

void getcachecells(struct cachepeer_data *peer, char cell[12][2048] )
{
	char temp[2048];
	char et1[1024], et2[1024];	/* R35 text-field scratch */

	memset(cell, 0, 12*2048);
	// CELL0#Host/port
#ifdef NEWCACHE
	if ( (peer->protocol)&&(peer->ping>0) ) {
		if (peer->sms) {
			if (peer->sms->status==0) sprintf( cell[0],"<span style='float:right'><img src=sms_new.gif></span><a href='/cachepeer?id=%d'>%s:%d</a>", peer->id, escf(et1,peer->host->name),peer->port);
			else  sprintf( cell[0],"<span style='float:right'><img src=sms_old.gif></span><a href='/cachepeer?id=%d'>%s:%d</a>", peer->id, escf(et1,peer->host->name),peer->port);
		}
		else sprintf( cell[0],"<a href='/cachepeer?id=%d'>%s:%d</a>", peer->id, escf(et1,peer->host->name),peer->port);
	}
	else
#endif
	sprintf( cell[0],"%s:%d", escf(et1,peer->host->name),peer->port);
	// CELL1#IP
	char *p = getcountrycodebyip(peer->host->ip);
	if (p) sprintf( cell[1],"<img src='/flag_%s.gif' title='%s'> %s", p, getcountryname(p), (char*)ip2string(peer->host->ip) ); else sprintf( cell[1],"%s",(char*)ip2string(peer->host->ip) );
	// CELL2#Program
	sprintf( cell[2],"%s %s", escf(et1,peer->program), escf(et2,peer->version));
	// CELL3 # Ping
	if (IS_DISABLED(peer->flags)) {
		sprintf( cell[3],"offline");
		sprintf( cell[4],"Dis.");
	}
	else {
		if ( peer->ping>0 ) {
			sprintf( cell[3],"online");
			sprintf( cell[4],"%d", peer->ping);
		}
		else {
			sprintf( cell[3],"offline");
			sprintf( cell[4],"?");
		}
		if (peer->csporthit[0].csid) {
			strcat( cell[4], "<table class=\"connect_data\">" );
			if (peer->ismultics) sprintf( temp,"<tr><td>Protocol</td><td>*%d</td></tr>", peer->protocol);
			else sprintf( temp,"<tr><td>Protocol</td><td>%d</td></tr>", peer->protocol);
			strcat( cell[4], temp );
			strcat( cell[4], "<tr><td width=150px>Profile</td><td>Hits</td></tr>" );
			int i;
			for(i=0; i<10; i++) {
				if (!peer->csporthit[i].csid) break;
				struct cardserver_data *cs = getcsbyid(peer->csporthit[i].csid);
				if (!cs) continue;
				sprintf( temp,"<tr><td>%s</td><td>%d</td></tr>", cs->name,peer->csporthit[i].hits);
				strcat( cell[4], temp );
			}
			strcat( cell[4], "</table>");
		}
	}



	// CELL4 # Request
	sprintf( cell[5],"%d",peer->reqnb);
	// CELL5 #
	sprintf( cell[6],"%d",peer->repok);

	sprintf( cell[7],"%d",peer->sentreq);
	sprintf( cell[8],"%d",peer->sentrep);

	// CELL8 # Cache Hits/Total
	getstatcell( peer->hitnb, cfg.cache.hits, cell[9] );
	// CELL9 # Instant Cache
	getstatcell( peer->ihitnb, peer->hitnb, cell[10] );
	// CELL10 # Last Used Cache
	if (peer->lastcaid) {
		sprintf( cell[11],"ch %s (%dms)", getchname(peer->lastcaid, peer->lastprov, peer->lastsid) , peer->lastdecodetime );
	}
	else strcpy( cell[11], " ");

	strcat( cell[11], "<span style='float:right;'>");
	if ( !(peer->flags&(FLAG_DELETE|FLAG_EXPIRED)) ) {
		if (peer->flags&FLAG_DISABLE) {
			sprintf( temp," <img title='Enable' src='enable.png' OnClick=\"imgrequest('/cachepeer?id=%d&action=enable',this);\">",peer->id);
			strcat( cell[11], temp );
		}
		else {
			sprintf( temp," <img title='disable' src='disable.png' OnClick=\"imgrequest('/cachepeer?id=%d&action=disable',this);\">",peer->id);
			strcat( cell[11], temp );
		}
	}
	//sprintf( temp," <img title='Debug' src='debug.png' OnClick=\"imgrequest('/cachepeer?id=%d&action=debug',this);\">",peer->id); strcat( cell[9], temp );
	strcat( cell[11], "</span>");

}

struct cacheserver_data *getcacheserverbyid(uint32_t id)
{
	struct cacheserver_data *cache = cfg.cache.server;
	while (cache) {
		if (!(cache->flags&FLAG_DELETE))
			if (cache->id==id) return cache;
		cache = cache->next;
	}
	return NULL;
}


void http_send_cache(int sock, http_request *req)
{
	char http_buf[2048];
	struct tcp_buffer_data tcpbuf;
	struct cachepeer_data *peer;
	char cell[12][2048];

	char *peerid = isset_get( req, "peerid");
	// Get Peer ID
	if (peerid)	{
		int i = atoi(peerid);
		//look for server
		struct cacheserver_data *cache = cfg.cache.server;
		while (cache) {
			peer = cache->peer;
			while (peer) {
				if (peer->id==(uint32_t)i) break;
				peer = peer->next;
			}
			cache = cache->next;
		}
		if (!peer) return;
		// Send XML CELLS
		getcachecells(peer,cell);
		for(i=0; i<12; i++) xmlescape( cell[i] );
		char buf[5000] = "";
		sprintf( buf, "<peer>\n<c0>%s</c0>\n<c1>%s</c1>\n<c2>%s</c2>\n<c3_c>%s</c3_c>\n<c3>%s</c3>\n<c4>%s</c4>\n<c5>%s</c5>\n<c6>%s</c6><c7>%s</c7><c8>%s</c8><c9>%s</c9><c10>%s</c10>\n</peer>\n",cell[0],cell[1],cell[2],cell[3],cell[4],cell[5],cell[6],cell[7],cell[8],cell[9],cell[10],cell[11] );
		http_send_xml( sock, req, buf, strlen(buf));
		return;
	}

	// Param Action
	char *str_action = isset_get( req, "action");
	int get_action = 0;
	if (str_action) {
		if (!strcmp(str_action,"div")) get_action = 1;
		else if (!strcmp(str_action,"row")) get_action = 2;
		else if (!strcmp(str_action,"disable")) {
			str_action = NULL;
			char *str_z = isset_get( req, "z"); // server ID
			if (str_z) {
				int get_z = atoi(str_z);
				struct cacheserver_data *cache = cfg.cache.server;
				while (cache) {
					peer = cache->peer;
					while (peer) {
						if ( (peer->ping>0)&&(peer->hitnb<get_z) ) {
							peer->flags |= FLAG_DISABLE;
							peer->nbcards = 0;
							peer->ping = 0;
						}
						peer = peer->next;
					}
					cache = cache->next;
				}
			}
		}
		else if (!strcmp(str_action,"config")) {
			tcp_init(&tcpbuf);
			struct cacheserver_data *cache = cfg.cache.server;
			while (cache) {
				sprintf( http_buf, "\n\nCACHE PORT: %d\n", cache->port);
				tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				peer = cache->peer;
				while (peer) {
					if (peer->ping>0) {
						sprintf( http_buf, "CACHE PEER: %s %d\n", peer->host->name, peer->port);
						tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
					}
					peer = peer->next;
				}
				cache = cache->next;
			}
			tcp_flush(&tcpbuf, sock);
			return;
		}
		else str_action = NULL;
	}
	if (!str_action) str_action = "page";
	//
	char *str_id = isset_get( req, "id"); // server ID
	int get_id = 0;
	if (str_id)	get_id = atoi(str_id);
	// Param List
	char *str_list = isset_get( req, "list");
	int get_list = LIST_CONNECTED;
	if (str_list) {
		if (!strcmp(str_list,"active")) get_list = LIST_CONNECTED;
		else if (!strcmp(str_list,"inactive")) get_list = LIST_DISCONNECTED;
		else if (!strcmp(str_list,"all")) get_list = LIST_ALL;
		else str_list=NULL;
	}
	if (!str_list) str_list = "active";
	//
	//
	struct cacheserver_data *cache = NULL;
	if (get_id) {
		cache = getcacheserverbyid(get_id);
		if (!cache) return;
	}

	tcp_init(&tcpbuf);
	if (get_action==0) {
		http_replyok_write(req, &tcpbuf, sock);
		http_html_write(req, &tcpbuf, sock);
		tcp_write(&tcpbuf, sock, http_head, strlen(http_head) );
		sprintf( http_buf, html_title, cfg.http.title, "Cache"); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		tcp_write(&tcpbuf, sock, http_link, strlen(http_link) );
		http_style_write(&tcpbuf, sock);
		// JS
		tcp_writestr(&tcpbuf, sock, "\n<script type='text/javascript'>");
		// ACTIONS REQUEST
		tcp_writestr(&tcpbuf, sock, "\nfunction imgrequest( url, el )\n{\n	var httpRequest;\n	try { httpRequest = new XMLHttpRequest(); }\n	catch (trymicrosoft) { try { httpRequest = new ActiveXObject('Msxml2.XMLHTTP'); } catch (oldermicrosoft) { try { httpRequest = new ActiveXObject('Microsoft.XMLHTTP'); } catch(failed) { httpRequest = false; } } }\n	if (!httpRequest) { alert('Your browser does not support Ajax.'); return false; }\n	if ( typeof(el)!='undefined' ) {\n		el.onclick = null;\n		el.style.opacity = '0.7';\n		httpRequest.onreadystatechange = function()\n		{\n			if (httpRequest.readyState == 4) if (httpRequest.status == 200) el.style.opacity = '0.3';\n		}\n	}\n	httpRequest.open('GET', url, true);\n	httpRequest.send(null);\n}\n");
		// UPD ROW
		tcp_writestr(&tcpbuf, sock, "\nfunction xmlupdateRow( xmlDoc, id )\n{\n	var row = document.getElementById(id);\n	row.cells.item(0).innerHTML = xmlDoc.getElementsByTagName('c0')[0].childNodes[0].nodeValue;\n	row.cells.item(1).innerHTML = xmlDoc.getElementsByTagName('c1')[0].childNodes[0].nodeValue;\n	row.cells.item(2).innerHTML = xmlDoc.getElementsByTagName('c2')[0].childNodes[0].nodeValue;\n	row.cells.item(3).className = xmlDoc.getElementsByTagName('c3_c')[0].childNodes[0].nodeValue;\n	row.cells.item(3).innerHTML = xmlDoc.getElementsByTagName('c3')[0].childNodes[0].nodeValue;\n	row.cells.item(4).innerHTML = xmlDoc.getElementsByTagName('c4')[0].childNodes[0].nodeValue;\n	row.cells.item(5).innerHTML = xmlDoc.getElementsByTagName('c5')[0].childNodes[0].nodeValue;\n	row.cells.item(6).innerHTML = xmlDoc.getElementsByTagName('c6')[0].childNodes[0].nodeValue;\n	row.cells.item(7).innerHTML = xmlDoc.getElementsByTagName('c7')[0].childNodes[0].nodeValue;\n	row.cells.item(8).innerHTML = xmlDoc.getElementsByTagName('c8')[0].childNodes[0].nodeValue;\n	row.cells.item(9).innerHTML = xmlDoc.getElementsByTagName('c9')[0].childNodes[0].nodeValue;\n	row.cells.item(10).innerHTML = xmlDoc.getElementsByTagName('c10')[0].childNodes[0].nodeValue;\n}\n");
		char url[256];
		sprintf( url, "'/cache?peerid='+idx");
		sprintf( http_buf, HTTP_UPDATE_ROW, url);
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		// UPD DIV
		sprintf( url, "/cache?action=div&list=%s", str_list);
		sprintf( http_buf, HTTP_UPDATE_DIV, cfg.http.autorefresh*1000, url);
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		//
		tcp_writestr(&tcpbuf, sock, "\nfunction start()\n{\n	setautorefresh(autorefresh);\n}");
		tcp_writestr(&tcpbuf, sock, "\n</script>\n");
		tcp_write(&tcpbuf, sock, http_head_, strlen(http_head_) );
		tcp_writestr(&tcpbuf, sock, "<body onload=\"start();\">");
		tcp_write_menu(&tcpbuf, sock,PAGE_CACHE);
		// DIV
		tcp_writestr(&tcpbuf, sock, "<div id='mainDiv'>");
	}

	tcp_writestr(&tcpbuf, sock, "<table border=1 width=100%><tr>");
	sprintf( http_buf,"<td align=center>AliveTime = %ds</td>", cfg.cache.alivetime/1000 ); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	sprintf( http_buf,"<td align=center>Auto-Add = %s</td>", yesno(cfg.cache.autoadd) ); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	if (cfg.cache.autoadd) {
		sprintf( http_buf,"<td align=center>Auto-Enable = %s</td>", yesno(cfg.cache.autoenable) ); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	}
	sprintf( http_buf,"<td align=center>Filter = %s</td>", onoff( cfg.cache.filter) ); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	sprintf( http_buf,"<td align=center>Total Requests = %d</td>",cfg.cache.req); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	if (cfg.cache.req>0) {
		sprintf( http_buf,"<td align=center>Total Replies = %d (%d%%)</td>", cfg.cache.rep, (cfg.cache.rep*100)/cfg.cache.req );
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	} else tcp_writestr(&tcpbuf, sock, "<td align=center>Total Replies = 0</td>" );
	tcp_writestr(&tcpbuf, sock, "</tr></table>");


	//
	tcp_writestr(&tcpbuf, sock, "<table style=\"margin:0px; padding:0px; border-width:0px; border-spacing: 1px;\"><tr>");
	tcp_writestr(&tcpbuf, sock, "<td style=\"margin:0px; padding:0px;\"><a href='/cache'><table border=1 width=200px>");
	// Total Servers
	sprintf( http_buf, "<tr><td>Total Cache Servers: %d</td></tr>", cfg.cache.totalservers ); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	int total, active;
	total_cache_peers( &total, &active );
	sprintf( http_buf, "<tr><td>Active Peers: %d / %d</td></tr>", active, total ); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	tcp_writestr(&tcpbuf, sock, "</table></a></td>");
	// Servers
	if (cfg.cache.server) {
		int counter = 0;
		struct cacheserver_data *cache = cfg.cache.server;
		while ( cache && (counter<5) ) {
			sprintf( http_buf, "<td style=\"margin:0px; padding: 0px;\"><a href='/cache?id=%d'><table border=1 width=150px>", cache->id);
			tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			if (cache->handle>0) sprintf( http_buf, "<tr><td>cache %d [<span class=success>%d</span>]</td></tr>", cache->id, cache->port);
			else sprintf( http_buf, "<tr><td>cache %d [<span class=failed>%d</span>]</td></tr>", cache->id, cache->port);
			tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			int total, active;
			cache_peers( cache, &total, &active );
			sprintf( http_buf, "<tr><td>Active: %d / %d</td></tr>", active, total);
			tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			tcp_writestr(&tcpbuf, sock, "</table></a></td>");
			counter++;
			cache = cache->next;
		}
	}
	// End of table
	tcp_writestr(&tcpbuf, sock, "</tr></table><br>");

	// Buttons
	tcp_writestr(&tcpbuf, sock, "<select style=\"width:200px;\" onchange=\"parent.location.href='/cache?id='+this.value\">");
	sprintf( http_buf, "<option value=0>ALL (%d)</option>", cfg.cache.totalservers);
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	struct cacheserver_data *tmp = cfg.cache.server;
	while (tmp) {
		if (get_id==tmp->id) sprintf( http_buf, "<option value=%d selected>[%d] cache %d</option>",tmp->id,tmp->port, tmp->id );
		else sprintf( http_buf, "<option value=%d>[%d] cache %d</option>",tmp->id,tmp->port, tmp->id );
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		tmp = tmp->next;
	}
	tcp_writestr(&tcpbuf, sock, "</select> ");
	//
	if (cache) cache_peers( cache, &total, &active ); else total_cache_peers( &total, &active );
	char *class1 = "button"; char *class2 = "sbutton";
	char *class;
	if (get_list==LIST_CONNECTED) class = class2; else class = class1;
	sprintf( http_buf, "<input type=button class=%s onclick=\"parent.location='/cache?id=%d&amp;list=active'\" value='Active Peers (%d)'>", class, get_id, active);
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	if (get_list==LIST_DISCONNECTED) class = class2; else class = class1;
	sprintf( http_buf, "<input type=button class=%s onclick=\"parent.location='/cache?id=%d&amp;list=inactive'\" value='Inactive Peers (%d)'>", class, get_id, total-active);
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	if (get_list==LIST_ALL) class = class2; else class = class1;
	sprintf( http_buf, " <input type=button class=%s onclick=\"parent.location='/cache?id=%d&amp;list=all'\" value='All Peers (%d)'>", class, get_id, total);
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );

	// Peers MainTable
	tcp_writestr(&tcpbuf,sock, "\n<table class=maintable width=100%>");
	tcp_writestr(&tcpbuf,sock, "\n<tr><th width=200px>Host</th><th width=110px>IP Address</th><th width=80px>Program</th><th width=30px>Ping</th><th width=70px>Requests</th><th width=70px>Replies</th><th width=70px>Sent REQ</th><th width=70px>Sent REP</th><th width=90px>Cache Hits</th><th width=80px>Instant Hits</th><th>Last Cache Hit</th></tr>\n");
	int alt=0;
	if (cache) {
		int total, active;
		cache_peers( cache, &total, &active );
		sprintf( http_buf,"\n<tr><td class=alt3 colspan=12> cache %d (%d)</td></tr>", cache->id, active); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		peer = cache->peer;
		while (peer) {
			int isactive = isactivepeer(peer);
			if ( (isactive&&(get_list&LIST_CONNECTED)) || (!isactive&&(get_list&LIST_DISCONNECTED)) ) {
				if (alt==1) alt=2; else alt=1;
				getcachecells(peer, cell);
				/* R35: HTML rows render composed cells as-is; raw text fields are pre-escaped in the builders (escf/html_esc). Blanket escape here printed the page's own tags as text. */
				if (peer->runtime) alt=3;
				sprintf( http_buf,"<tr id=\"Row%d\" class=alt%d onMouseOver='setupdateRow(%d)' onMouseOut='setupdateRow(0)'><td>%s</td><td>%s</td><td>%s</td><td class=\"%s\">%s</td><td>%s</td><td>%s</td><td>%s</td><td>%s</td><td>%s</td><td>%s</td><td>%s</td></tr>\n",peer->id,alt,peer->id,cell[0],cell[1],cell[2],cell[3],cell[4],cell[5],cell[6],cell[7],cell[8],cell[9],cell[10],cell[11]);
				tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			}
			peer = peer->next;
		}
		// Total
		cache_peers( cache, &total, &active );
		sprintf( http_buf,"<tr class=alt3><td align=right>Total</td><td colspan=3>%d</td>",total);
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		int totreq = 0;
		int totrepok = 0;
		int tothits = 0;
		int totihits = 0;

		peer = cache->peer;
		while (peer) {
			totreq += peer->reqnb;
			totrepok += peer->repok;
			tothits += peer->hitnb;
			totihits += peer->ihitnb;
			peer = peer->next;
		}
		sprintf( http_buf,"<td>%d</td>",totreq); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		sprintf( http_buf,"<td>%d</td>",totrepok); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		sprintf( http_buf,"<td colspan=2> </td>"); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		tcp_writeecmdata(&tcpbuf, sock, tothits, cfg.cache.hits);
		tcp_writeecmdata(&tcpbuf, sock, totihits, tothits);
		sprintf( http_buf,"<td> </td></tr>"); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	}
	else {
		cache = cfg.cache.server;
		while (cache) {
			int total, active;
			cache_peers( cache, &total, &active );
			sprintf( http_buf,"\n<tr><td class=alt3 colspan=12> cache %d (%d)</td></tr>", cache->id, active); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			peer = cache->peer;
			while (peer) {
				int isactive = isactivepeer(peer);
				if ( (isactive&&(get_list&LIST_CONNECTED)) || (!isactive&&(get_list&LIST_DISCONNECTED)) ) {
					if (alt==1) alt=2; else alt=1;
					getcachecells(peer, cell);
					/* R35: HTML rows render composed cells as-is; raw text fields are pre-escaped in the builders (escf/html_esc). Blanket escape here printed the page's own tags as text. */
					if (peer->runtime) alt=3;
					sprintf( http_buf,"<tr id=\"Row%d\" class=alt%d onMouseOver='setupdateRow(%d)' onMouseOut='setupdateRow(0)'><td>%s</td><td>%s</td><td>%s</td><td class=\"%s\">%s</td><td>%s</td><td>%s</td><td>%s</td><td>%s</td><td>%s</td><td>%s</td><td>%s</td></tr>\n",peer->id,alt,peer->id,cell[0],cell[1],cell[2],cell[3],cell[4],cell[5],cell[6],cell[7],cell[8],cell[9],cell[10],cell[11]);
					tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				}
				peer = peer->next;
			}
			cache = cache->next;
		}

		// Total
		total_cache_peers( &total, &active );
		sprintf( http_buf,"<tr class=alt3><td align=right>Total</td><td colspan=3>%d</td>",total);
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		int totreq = 0;
		int totrepok = 0;
		int tothits = 0;
		int totihits = 0;

		cache = cfg.cache.server;
		while (cache) {
			peer = cache->peer;
			while (peer) {
				totreq += peer->reqnb;
				totrepok += peer->repok;
				tothits += peer->hitnb;
				totihits += peer->ihitnb;
				peer = peer->next;
			}
			cache = cache->next;
		}

		sprintf( http_buf,"<td>%d</td>",totreq); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		sprintf( http_buf,"<td>%d</td>",totrepok); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		sprintf( http_buf,"<td colspan=2> </td>"); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		tcp_writeecmdata(&tcpbuf, sock, tothits, cfg.cache.hits);
		tcp_writeecmdata(&tcpbuf, sock, totihits, tothits);
		sprintf( http_buf,"<td> </td></tr>"); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	}


	tcp_writestr(&tcpbuf, sock, "</table>");

	if (get_action==0) {
		tcp_writestr(&tcpbuf, sock, "</div></body></html>");
	}
	tcp_flush(&tcpbuf, sock);
}

struct sms_data *cache_new_sms(char *msg);
void cache_send_sms(struct cachepeer_data *peer, struct sms_data *sms);

///////////////////////////////////////////////////////////////////////////////
void http_send_cache_peer(int sock, http_request *req)
{
	char *str_id = isset_get( req, "id");
	if (!str_id) return; //error
	int get_id = atoi(str_id);
	//
	struct cachepeer_data *peer = getpeerbyid( get_id );
	if (!peer) return;
	// Action
	char *str_action = isset_get( req, "action");
	int get_action = 0;
	if (str_action) {
		if (!strcmp(str_action,"div")) get_action = 1;
		else if (!strcmp(str_action,"row")) get_action = 2;
		else if (!strcmp(str_action,"disable")) get_action = 3;
		else if (!strcmp(str_action,"enable")) get_action = 4;
		else if (!strcmp(str_action,"status")) get_action = 5;
		else if (!strcmp(str_action,"sms")) get_action = 10;
		else str_action = NULL;
	}
	if (!str_action) str_action = "page";
	//
	if (get_action==3) {
		peer->flags |= FLAG_DISABLE;
		peer->ping = 0;
 		http_send_ok(sock);
		return;
	}
	else if (get_action==4) {
		peer->flags &= ~FLAG_DISABLE;
		peer->ping = 0;
		peer->lastpingsent = 0;
		peer->program[0] = 0;
		peer->version[0] = 0;
		http_send_ok(sock);
		return;
	}
	else if (get_action==5) {
		if ( peer->ping>0 ) http_send_text(sock,"active"); else http_send_text(sock,"inactive");
		return;
	}
	else if (get_action==10) {
		// Terminate the string
		req->dbf.data[req->dbf.datasize] = 0;
		char *msg = strstr( (char*)req->dbf.data, "\r\n\r\n" );
		if (msg) {
			// Check Length
			int len = strlen(msg);
			if (len<2) return;
			if (len>1000) msg[1000] = 0;
			// Create MSG
			struct sms_data *sms = cache_new_sms(msg+4);
			cache_send_sms( peer, sms);
			// Wait ACK
			sleep(1);
			if (sms->status&2) {
				http_send_ok(sock);
				return;
			}
		}
		return;
	}


	char http_buf[2048];
	struct tcp_buffer_data tcpbuf;

	tcp_init(&tcpbuf);
	if (!get_action) {
		http_replyok_write(req, &tcpbuf, sock);
		http_html_write(req, &tcpbuf, sock);
		tcp_write(&tcpbuf, sock, http_head, strlen(http_head) );
		sprintf( http_buf, html_title, cfg.http.title, " Cache Peer"); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		tcp_write(&tcpbuf, sock, http_link, strlen(http_link) );
		http_style_write(&tcpbuf, sock);

		// JS
		tcp_writestr(&tcpbuf, sock, "\n<script type='text/javascript'>");
		// ACTIONS REQUEST
		tcp_writestr(&tcpbuf, sock, "\nfunction smsrequest( peerid , button )\n{\n	msg = document.getElementById('message').value;\n	if (msg=='') return;\n	var httpRequest;\n	try { httpRequest = new XMLHttpRequest(); }\n	catch (trymicrosoft) { try { httpRequest = new ActiveXObject('Msxml2.XMLHTTP'); } catch (oldermicrosoft) { try { httpRequest = new ActiveXObject('Microsoft.XMLHTTP'); } catch(failed) { httpRequest = false; } } }\n	if (!httpRequest) { alert('Your browser does not support Ajax.'); return false; }\n	button.disabled = true;\n	mydiv = document.getElementById('smsdiv');\n	mydiv.innerHTML = 'Sending message to peer...';\n	clearTimeout(tautorefresh);\n	httpRequest.onreadystatechange = function()\n	{\n		if (httpRequest.readyState == 4) {\n			if (httpRequest.status == 200) {\n				mydiv.innerHTML = 'Message Sent Successfully';\n				document.getElementById('message').value = '';\n			}\n			else mydiv.innerHTML = 'Failed to send message';\n			button.disabled = false;\n			if (!autorefresh) autorefresh = 3000;\n			updateDiv();\n		}\n	}\n	httpRequest.open('POST', '/cachepeer?action=sms&id='+peerid, true);\n	httpRequest.send( msg );\n}\n");
		// UPD DIV
		char url[255];
		sprintf( url, "/cachepeer?id=%d&action=div", peer->id);
		sprintf( http_buf, HTTP_UPDATE_DIV, cfg.http.autorefresh*1000, url);
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		//
		tcp_writestr(&tcpbuf, sock, "\n</script>\n");
		tcp_write(&tcpbuf, sock, http_head_, strlen(http_head_) );
		tcp_writestr(&tcpbuf, sock, "<body>");
		tcp_write_menu(&tcpbuf, sock,0);
		//
		tcp_writestr(&tcpbuf, sock, "<table style='padding:0px; margin:0px;' width='100%'><tbody>");
		tcp_writestr(&tcpbuf, sock, "<tr><td style='vertical-align:top; width:400px;'>");
		// Peer Infos
		tcp_writestr(&tcpbuf, sock, "<table class='infotable'><tbody><tr><th colspan=2>Cache Peer Informations</th></tr>");
		sprintf( http_buf,"<tr><td class=left>Host:Port</td><td class=right>%s:%d</td></tr>", peer->host->name, peer->port); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		if (peer->cards[0]) {
			tcp_writestr(&tcpbuf, sock, "<tr><td class=left>Card list</td><td class=right><select>");
			int i;
			for (i=0; i<1024; i++) {
				if (!peer->cards[i]) break;
				if ( (peer->cards[i]>>24)==5 )
					sprintf( http_buf,"<option>0500:%06x</option>", peer->cards[i]&0xffffff);
				else
					sprintf( http_buf,"<option>%04x:%06x</option>", peer->cards[i]>>16, peer->cards[i]&0xffff);
				tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			}
			tcp_writestr(&tcpbuf, sock, "</select></td></tr>");
		}
		tcp_writestr(&tcpbuf, sock, "</tbody></table>");
		// Stat
		tcp_writestr(&tcpbuf, sock, "<br><table class='infotable'><tbody><tr><th colspan=2>Peer Statistics</th></tr>");
		sprintf( http_buf,"<tr><td class=left>Total Sent Requests</td><td class=right>%d</td></tr>", peer->sentreq);
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		sprintf( http_buf,"<tr><td class=left>Total Sent Replies</td><td class=right>%d</td></tr>", peer->sentrep);
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		sprintf( http_buf,"<tr><td class=left>Total Received Requests</td><td class=right>%d</td></tr>", peer->reqnb);
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		sprintf( http_buf,"<tr><td class=left>Total Received Replies</td><td class=right>%d</td></tr>", peer->repok);
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		sprintf( http_buf,"<tr><td class=left>Total Cache Hits</td><td class=right>%d</td></tr>", peer->hitnb);
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		tcp_writestr(&tcpbuf, sock, "</tbody></table>");
		// Profiles Hits
		tcp_writestr(&tcpbuf, sock, "<br><table class='infotable'><tbody><tr><th colspan=2>Profiles Hits</th></tr>");
		int i;
		for(i=0; i<MAX_CSPORTS; i++) {
			if (!peer->csporthit[i].csid) break;
			struct cardserver_data *cs = getcsbyid(peer->csporthit[i].csid);
			if (!cs) continue;
			sprintf( http_buf,"<tr><td class=left>%s</td><td class=right>%d</td></tr>", cs->name,peer->csporthit[i].hits);
			tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		}
		tcp_writestr(&tcpbuf, sock, "</tbody></table>");
		tcp_writestr(&tcpbuf, sock, "</td><td style='vertical-align:top;'>");
		// Messages
		tcp_writestr(&tcpbuf, sock, "<table class='infotable' width=100%><tr><th colspan=2>Send Message</td></tr><tr>");
		tcp_writestr(&tcpbuf, sock, "<td><textarea id='message' name='message' style='width:100%; height:50px;'></textarea></td>");
		sprintf( http_buf,"<td width=150px align=center><input type=button style='width=120px' value='Send Message' onclick='smsrequest(%d, this)'><br><div id=smsdiv></div></td>", peer->id);
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		tcp_writestr(&tcpbuf, sock, "</tr><tr><td colspan=2>");
		tcp_writestr(&tcpbuf, sock, "<div id=mainDiv>");
	}
	if (peer->sms) {
		struct sms_data *sms = peer->sms;
		while (sms) {
			// Get Time
			char timebuf [80];
			struct tm * timeinfo = localtime (&(time_t){sms->rawtime});
			strftime (timebuf,80,"%x %X",timeinfo);
			//
			char *color;
			if (sms->status&1) {
				if (sms->status&2) color = "blue"; else color = "grey";
			}
			else {
				if (sms->status&2) color = "green"; else color = "red";
				sms->status = 2;
			}
			//
			sprintf( http_buf,"<font color=%s><pre>-----[%s]-------\n%s</pre></font>", color, timebuf, sms->msg);
			tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			sms = sms->next;
		}
	}

	if (!get_action) {
		tcp_writestr(&tcpbuf, sock, "</div></td></tr></table> </td></tr></table></body></html>");
	}
	tcp_flush(&tcpbuf, sock);
}


///////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
void getprofilecells(struct cardserver_data *cs, char cell[11][2048])
{
	char temp[2048];
	char et1[1024], et2[1024];	/* R35 text-field scratch */
	// CELL0 # Profile name
	sprintf( cell[0],"<a href=\"/profile?id=%d\">%s</a>", cs->id, escf(et1,cs->name));
	// CELL1 # Port
	sprintf( cell[1],"<a href=\"/newcamd?pid=%d\">%d</a>", cs->id, cs->newcamd.port); 
	if (cs->newcamd.handle>0) sprintf( cell[10],"online"); else sprintf( cell[10],"offline"); 
	// CELL2 # Ecm Time
	if (cs->ecmok) sprintf( cell[2],"%d ms",(cs->ecmoktime/cs->ecmok) ); else sprintf( cell[2],"-- ms");

	// CELL3 # TotalECM
	int ecmnb = cs->ecmaccepted+cs->ecmdenied;
	sprintf( cell[3], "%d", ecmnb );
	// CELL4 # AcceptedECM
	getstatcell( cs->ecmaccepted, ecmnb, cell[4] );
	// CELL5 # ECM OK
	getstatcell( cs->ecmok, cs->ecmaccepted, cell[5] );
	// CELL6 # CacheHits
	getstatcell( cs->hits.csp, cs->ecmok, cell[6] );
#ifdef CACHEEX
	if (cs->option.fallowcacheex) {
		getstatcell( cs->hits.cacheex, cs->ecmok, temp );
		strcat( cell[6], "<br>" );
		strcat( cell[6], temp );
	}
#endif 
	// CELL7 # Cache iHits
	getstatcell( cs->hits.instant.csp, cs->hits.csp, cell[7] );
#ifdef CACHEEX
	if (cs->option.fallowcacheex) {
		getstatcell( cs->hits.instant.cacheex, cs->hits.cacheex, temp );
		strcat( cell[7], "<br>" );
		strcat( cell[7], temp );
	}
#endif 
	// CELL8 # Clients
	int i=0;
	int j=0;
	struct cs_client_data *usr = cs->newcamd.client;
	while (usr) {
		i++;
		if (usr->handle>0) j++;
		usr = usr->next;
	}
	getstatcell2(j,i,cell[8]);
	// CELL9 # Caid:Providers
	sprintf( cell[9],"<b>%04X:</b> %x ",cs->card.caid,cs->card.prov[0].id);
	for(i=1; i<cs->card.nbprov; i++) {
		sprintf( temp,",%x ",cs->card.prov[i].id);
		strcat( cell[9], temp );
	}

	strcat( cell[9], "<span style='float:right;'>");
	sprintf( temp," <img title='Debug' src='debug.png' OnClick=\"imgrequest('/profile?id=%d&action=debug',this);\">",cs->id);
	strcat( cell[9], temp );
	strcat( cell[9], "</span>");
}


void http_send_profiles(int sock, http_request *req)
{
	char http_buf[2048];
	struct tcp_buffer_data tcpbuf;

	char cell[11][2048];

	//  Get Params
	char *str_action = isset_get( req, "action");
	int get_action = 0;
	if (str_action) {
		if (!strcmp(str_action,"div")) get_action = ACTION_DIV;
		else if (!strcmp(str_action,"row")) get_action = ACTION_ROW;
		else if (!strcmp(str_action,"xml")) get_action = ACTION_XML; // Get Clients info in xml
		else str_action = NULL;
	}
	if (!str_action) { str_action = "page"; get_action = ACTION_PAGE; }
	//
	if (get_action==ACTION_XML) {
		char *str_id = isset_get( req, "id"); // CCcam server ID
		int get_id = 0;
		if (str_id) get_id = atoi( str_id );
		struct cardserver_data *cs = getcsbyid( get_id );

		tcp_init(&tcpbuf);
		tcp_writestr(&tcpbuf, sock, "HTTP/1.1 200 OK\r\nAccept-Ranges: bytes\r\nConnection: close\r\nContent-Type: application/xml\r\n\r\n");
		tcp_writestr(&tcpbuf, sock, "<multics>");
		struct cardserver_data *srv;
		if (cs) srv = cs; else srv = cfg.cardserver;
		while (srv) {
			tcp_writestr(&tcpbuf, sock, "\n<profile>");
			sprintf(http_buf, "<id>%d</id>", srv->id); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			sprintf(http_buf, "<name>%s</name>", srv->name); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			sprintf(http_buf, "<caid>%04x</caid>", srv->card.caid); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			int i;
			for (i=0; i<srv->card.nbprov; i++) {
				sprintf(http_buf, "<provider>%06x</provider>", srv->card.prov[i].id);
				tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			}
			sprintf(http_buf, "<totalecm>%d</totalecm>", srv->ecmaccepted+srv->ecmdenied); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			sprintf(http_buf, "<acceptedecm>%d</acceptedecm>", srv->ecmaccepted); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			sprintf(http_buf, "<ecmok>%d</ecmok>", srv->ecmok); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			tcp_writestr(&tcpbuf, sock, "\n</profile>");
			if (cs) break; else srv = srv->next;
		}
		tcp_writestr(&tcpbuf, sock, "\n</multics>");
		tcp_flush(&tcpbuf, sock);
		return;
	}
	//
	int i;
	char *id = isset_get( req, "id");
	// Get Peer ID
	if (id)	{
		i = atoi(id);
		//look for server
		struct cardserver_data *cs = cfg.cardserver;
		while (cs) {
			if (cs->id==(uint32_t)i) break;
			cs = cs->next;
		}
		if (!cs) return;
		// Send XML CELLS
		getprofilecells(cs,cell);
		for(i=0; i<11; i++) xmlescape( cell[i] );
		char buf[5000] = "";
		sprintf( buf, "<profile>\n<c0>%s</c0>\n<c1_c>%s</c1_c>\n<c1>%s</c1>\n<c2>%s</c2>\n<c3>%s</c3>\n<c4>%s</c4>\n<c5>%s</c5>\n<c6>%s</c6>\n<c7>%s</c7>\n<c8>%s</c8>\n<c9>%s</c9>\n</profile>\n",cell[0],cell[10],cell[1],cell[2],cell[3],cell[4],cell[5],cell[6],cell[7],cell[8],cell[9] );
		http_send_xml( sock, req, buf, strlen(buf));
		return;
	}


	tcp_init(&tcpbuf);

	if (get_action==ACTION_PAGE) {
		http_replyok_write(req, &tcpbuf, sock);
		http_html_write(req, &tcpbuf, sock);
		tcp_write(&tcpbuf, sock, http_head, strlen(http_head) );
		sprintf( http_buf, html_title, cfg.http.title, "Profiles"); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		tcp_write(&tcpbuf, sock, http_link, strlen(http_link) );
		http_style_write(&tcpbuf, sock);
		// JS
		tcp_writestr(&tcpbuf, sock, "\n<script type='text/javascript'>");
		// ACTIONS REQUEST
		tcp_writestr(&tcpbuf, sock, "\nfunction imgrequest( url, el )\n{\n	var httpRequest;\n	try { httpRequest = new XMLHttpRequest(); }\n	catch (trymicrosoft) { try { httpRequest = new ActiveXObject('Msxml2.XMLHTTP'); } catch (oldermicrosoft) { try { httpRequest = new ActiveXObject('Microsoft.XMLHTTP'); } catch(failed) { httpRequest = false; } } }\n	if (!httpRequest) { alert('Your browser does not support Ajax.'); return false; }\n	if ( typeof(el)!='undefined' ) {\n		el.onclick = null;\n		el.style.opacity = '0.7';\n		httpRequest.onreadystatechange = function()\n		{\n			if (httpRequest.readyState == 4) if (httpRequest.status == 200) el.style.opacity = '0.3';\n		}\n	}\n	httpRequest.open('GET', url, true);\n	httpRequest.send(null);\n}\n");
		// UPD ROW
		tcp_writestr(&tcpbuf, sock, "\nfunction xmlupdateRow( xmlDoc, id )\n{\n	var row = document.getElementById(id);\n	row.cells.item(0).innerHTML = xmlDoc.getElementsByTagName('c0')[0].childNodes[0].nodeValue;\n	row.cells.item(1).className = xmlDoc.getElementsByTagName('c1_c')[0].childNodes[0].nodeValue;\n	row.cells.item(1).innerHTML = xmlDoc.getElementsByTagName('c1')[0].childNodes[0].nodeValue;\n	row.cells.item(2).innerHTML = xmlDoc.getElementsByTagName('c2')[0].childNodes[0].nodeValue;\n	row.cells.item(3).innerHTML = xmlDoc.getElementsByTagName('c3')[0].childNodes[0].nodeValue;\n	row.cells.item(4).innerHTML = xmlDoc.getElementsByTagName('c4')[0].childNodes[0].nodeValue;\n	row.cells.item(5).innerHTML = xmlDoc.getElementsByTagName('c5')[0].childNodes[0].nodeValue;\n	row.cells.item(6).innerHTML = xmlDoc.getElementsByTagName('c6')[0].childNodes[0].nodeValue;\n	row.cells.item(7).innerHTML = xmlDoc.getElementsByTagName('c7')[0].childNodes[0].nodeValue;\n	row.cells.item(8).innerHTML = xmlDoc.getElementsByTagName('c8')[0].childNodes[0].nodeValue;\n	row.cells.item(9).innerHTML = xmlDoc.getElementsByTagName('c9')[0].childNodes[0].nodeValue;\n}\n");
		char url[256];
		sprintf( url, "'/profiles?id='+idx");
		sprintf( http_buf, HTTP_UPDATE_ROW, url);
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		// UPD DIV
		sprintf( url, "/profiles?action=div");
		sprintf( http_buf, HTTP_UPDATE_DIV, cfg.http.autorefresh*1000, url);
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		//
		tcp_writestr(&tcpbuf, sock, "\nfunction start()\n{\n	setautorefresh(autorefresh);\n}");
		tcp_writestr(&tcpbuf, sock, "\n</script>\n");
		//
		tcp_write(&tcpbuf, sock, http_head_, strlen(http_head_) );
		tcp_writestr(&tcpbuf, sock, "\n<body onload=\"start();\">");
		tcp_write_menu(&tcpbuf, sock,PAGE_PROFILES);
		// DIV
		tcp_writestr(&tcpbuf, sock, "<div id='mainDiv'>");
	}

	sprintf( http_buf, "Total Profiles: %d", total_profiles() ); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	tcp_writestr(&tcpbuf, sock, "<br>\n<table class=maintable width=100%%><tr><th width=150px>Profile name</th><th width=50px>Port</th><th width=60px>EcmTime</th><th width=60px>TotalECM</th><th width=90px>AcceptedECM</th><th width=80px>Ecm OK</th><th width=85px>Cache/Ex Hits</th><th width=80px>Instant Hits</th><th width=80px>Clients</th><th>Caid:Providers</th></tr>");

	struct cardserver_data *cs = cfg.cardserver;
	int alt=0;
	while(cs) {
		if (alt==1) alt=2; else alt=1;
		getprofilecells( cs, cell );
		/* R35: page/div tail is HTML -- no blanket escape (builders pre-escape text) */
		sprintf( http_buf,"\n<tr id=\"Row%d\" class=alt%d onMouseOver='setupdateRow(%d)' onMouseOut='setupdateRow(0)'><td>%s</td><td class=%s>%s</td><td align=center>%s</td><td align=center>%s</td><td>%s</td><td>%s</td><td>%s</td><td>%s</td><td>%s</td><td>%s</td></tr>",cs->id,alt,cs->id,cell[0],cell[10],cell[1],cell[2],cell[3],cell[4],cell[5],cell[6],cell[7],cell[8],cell[9]);
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		cs = cs->next;
	}
	// Total
	if (alt==1) alt=2; else alt=1;
	sprintf( http_buf,"\n<tr class=alt3><td align=right>Total</td><td align=center>%d</td><td align=center>--</td>",total_profiles()); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	int totecm = 0;
	int totecmaccepted = 0;
	int totecmok = 0;
	int totcachehits = 0;
	int totcacheihits = 0;
	cs = cfg.cardserver;
	while(cs) {
		totecm += cs->ecmaccepted+cs->ecmdenied;
		totecmaccepted += cs->ecmaccepted;
		totecmok += cs->ecmok;
		totcachehits += cs->hits.csp;
#ifdef CACHEEX
		totcachehits += cs->hits.cacheex;
#endif
		totcacheihits += cs->hits.instant.csp;
#ifdef CACHEEX
		totcacheihits += cs->hits.instant.cacheex;
#endif
		cs = cs->next;
	}
	sprintf( http_buf,"<td align=center>%d</td>",totecm); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	tcp_writeecmdata(&tcpbuf, sock, totecmaccepted, totecm);
	tcp_writeecmdata(&tcpbuf, sock, totecmok, totecmaccepted);
	tcp_writeecmdata(&tcpbuf, sock, totcachehits, totecmok);
	tcp_writeecmdata(&tcpbuf, sock, totcacheihits, totcachehits);
	sprintf( http_buf, "<td colspan=2> </td></tr>"); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	// Speed
	uint ticks = GetTickCount()/1000;
	sprintf( http_buf,"<tr class=alt2><td align=right>Average speed</td><td colspan=2 align=center>(per minute)</td>"); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	sprintf( http_buf,"<td align=center>%d</td>", totecm*60/ticks); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	sprintf( http_buf,"<td>%d</td>", totecmaccepted*60/ticks); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	sprintf( http_buf,"<td>%d</td>", totecmok*60/ticks); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	sprintf( http_buf,"<td>%d</td>", totcachehits*60/ticks); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	sprintf( http_buf,"<td>%d</td>", totcacheihits*60/ticks); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	sprintf( http_buf, "<td colspan=2> </td></tr>"); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );

	sprintf( http_buf, "\n</table>"); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );

	if (get_action==ACTION_PAGE) {
		tcp_writestr(&tcpbuf, sock, "\n</div></body></html>");
	}
	tcp_flush(&tcpbuf, sock);
}

///////////////////////////////////////////////////////////////////////////////
// PROFILE
///////////////////////////////////////////////////////////////////////////////

void cs_clients( struct cardserver_data *cs, int *total, int *connected, int *active )
{
	*total = 0;
	*connected = 0;
	*active = 0;
	struct cs_client_data *cli=cs->newcamd.client;
	while (cli) {
		(*total)++;
		if (cli->connection.status>0) {
			(*connected)++;
			if ( (GetTickCount()-cli->lastecmtime) < 20000 ) (*active)++;
		}
		cli=cli->next;
	}
}

void cs_allclients( int *total, int *connected, int *active )
{
	*total = 0;
	*connected = 0;
	*active = 0;

	struct cardserver_data *cs = cfg.cardserver;
	while (cs) {
		struct cs_client_data *cli = cs->newcamd.client;
		while (cli) {
			(*total)++;
			if (cli->connection.status>0) {
				(*connected)++;
				if ( (GetTickCount()-cli->lastecmtime) < 20000 ) (*active)++;
			}
			cli=cli->next;
		}
		cs = cs->next;
	}
}


#ifdef RADEGAST_SRV

int connected_radegast_clients(struct cardserver_data *cs)
{
	int nb=0;
	struct rdgd_client_data *rdgdcli=cs->radegast.client;
	if (cs->radegast.handle)
	while (rdgdcli) {
		if (rdgdcli->handle>0) nb++;
		rdgdcli=rdgdcli->next;
	}
	return nb;
}

#endif

char *programid(unsigned int id)
{
	typedef struct {
		char name[13];
		unsigned int id;
	} tnewcamdprog; 

	static tnewcamdprog camdp[] = { 
		{ "Generic", 0x0000 },
		{ "VDRSC",   0x5644 },
		{ "LCE", 0x4C43 },
		{ "Camd3", 0x4333 },
		{ "Radegast", 0x7264 },
		{ "Gbox2CS", 0x6762 },
		{ "Mgcamd", 0x6D67 },
		{ "WinCSC", 0x7763 },
		{ "newcs", 0x6E73 },
		{ "cx", 0x6378 },
		{ "Kaffeine", 0x6B61 },
		{ "Evocamd", 0x6576 },
		{ "CCcam", 0x4343 },
		{ "Tecview", 0x5456 },
		{ "AlexCS", 0x414C },
		{ "Rqcamd", 0x0666 },
		{ "Rq-echo", 0x0667 },
		{ "Acamd", 0x9911 },
		{ "Cardlink", 0x434C },
		{ "Octagon", 0x4765 },
		{ "sbcl", 0x5342 },
		{ "NextYE2k", 0x6E65 },
		{ "NextYE2k", 0x4E58 },
		{ "DiabloCam/UW", 0x4453 },
		{ "OScam", 0x8888 },
		{ "Scam", 0x7363 },
		{ "Rq-sssp/CW", 0x0669 },
		{ "Rq-sssp/CS", 0x0665 },
		{ "JlsRq", 0x0769 },
		{ "eyetvCamd", 0x4543 }
	};
	static char unknown[] = "Unknown";
	unsigned int i;
	id = id & 0xffff;
	for( i=0; i<sizeof(camdp)/sizeof(tnewcamdprog); i++ )
		if (camdp[i].id==id) return camdp[i].name;
	return unknown;
}

char* str_laststatus[] = { "NOK", "OK" };


///////////////////////////////////////////////////////////////////////////////

void getnewcamdclientcells(struct cs_client_data *cli, char cell[10][2048])
{
	char temp[2048];
	char et1[1024], et2[1024];	/* R35 text-field scratch */
	unsigned int ticks = GetTickCount();
	unsigned int d;
	// CELL0 # User name
	sprintf( cell[0],"<a href='/newcamdclient?id=%d'>%s</a>", cli->id, escf(et1,cli->user));
	// CELL1 # PROGRAM ID
	if (cli->connection.status>0)
		sprintf( cell[1],"<span title='%04x'>%s</span>", cli->progid, programid(cli->progid));
	else
		strcpy( cell[1], " ");
	// CELL2 # IP
	if (cli->connection.status>0) {
		char *p = getcountrycodebyip(cli->ip);
		if (p) sprintf( cell[2],"<img src='/flag_%s.gif' title='%s'> %s", p, getcountryname(p), (char*)ip2string(cli->ip) ); else sprintf( cell[2], "%s", (char*)ip2string(cli->ip) );
	}
	else
		strcpy( cell[2], " ");
	// CELL3 # CONNECTION TIME
	if (cli->connection.status>0) {
		d = (ticks-cli->connection.time)/1000;
		if (cli->ecm.busy) sprintf( cell[9], "busy"); else sprintf( cell[9], "online");
		sprintf( cell[3],"%02dd %02d:%02d:%02d", d/(3600*24), (d/3600)%24, (d/60)%60, d%60);
	}
	else {
		sprintf( cell[9], "offline");
		if (cli->flags&FLAG_DELETE) sprintf( cell[3],"Removed");
		else if (cli->flags&FLAG_EXPIRED) sprintf( cell[3],"Expired");
		else if (cli->flags&FLAG_DISABLE) sprintf( cell[3],"Disabled");
		else sprintf( cell[3],"offline");
	}
#ifdef EXPIREDATE
	if (cli->enddate.tm_year) {
		sprintf( temp,"<br>Expire: %d-%02d-%02d", 1900+cli->enddate.tm_year, cli->enddate.tm_mon+1, cli->enddate.tm_mday);
		strcat( cell[3], temp );
	}
#endif
	sprintf( temp, "<table class=\"connect_data\"><tr><td>Successful Login: %d</td><td>Aborted Connections: %d</td><td>Total Zap: %d</td><td>Channel Freeze: %d</td></tr></table>", cli->nblogin, cli->nbloginerror, cli->zap, cli->freeze );
	strcat( cell[3], temp );

	// ECM STAT
#ifdef SRV_CSCACHE
	if (cli->cachedcw) sprintf( cell[4], "%d [%d]", cli->ecmnb, cli->cachedcw); else sprintf( cell[4], "%d", cli->ecmnb );
#else
	sprintf( cell[4], "%d", cli->ecmnb );
#endif
	//
	int ecmaccepted = cli->ecmnb-cli->ecmdenied;
	getstatcell( ecmaccepted, cli->ecmnb, cell[5]);
	getstatcell( cli->ecmok, ecmaccepted, cell[6]);
	// Ecm Time
	if (cli->ecmok)
		sprintf( cell[7],"%d ms",(cli->ecmoktime/cli->ecmok) );
	else
		sprintf( cell[7],"-- ms");

	//Last Used Share
	if (cli->connection.status<=0 && cli->connection.lastseen) {
		d = (ticks-cli->connection.lastseen)/1000;
		sprintf( cell[8],"Last Seen %02dd %02d:%02d:%02d", d/(3600*24),(d/3600)%24,(d/60)%60,d%60);
	}
	else if ( cli->lastecm.caid ) {
		if (cli->lastecm.status) sprintf( cell[8],"<span class=success"); else sprintf( cell[8],"<span class=failed");
		sprintf( temp," title='%04x:%06x:%04x'>ch %s (%dms) %s ",cli->lastecm.caid, cli->lastecm.prov, cli->lastecm.sid, getchname(cli->lastecm.caid, cli->lastecm.prov, cli->lastecm.sid) , cli->lastecm.decodetime, str_laststatus[cli->lastecm.status] );
		strcat( cell[8], temp);
		if ( (GetTickCount()-cli->ecm.recvtime) < 20000 ) {
			// From ???
			if (cli->lastecm.status) {
				strcat( cell[8], " / from ");
				src2string(cli->lastecm.dcwsrctype, cli->lastecm.dcwsrcid, temp);
				strcat( cell[8], temp);
			}
		}
		strcat( cell[8], "</span>");
	}
	else strcpy( cell[8], " ");

	strcat( cell[8], "<span style='float:right;'>");
	if ( !(cli->flags&(FLAG_DELETE|FLAG_EXPIRED)) ) {
		if (cli->flags&FLAG_DISABLE) {
			sprintf( temp," <img title='Enable' src='enable.png' OnClick=\"imgrequest('/newcamdclient?id=%d&action=enable',this);\">",cli->id);
			strcat( cell[8], temp );
		}
		else {
			sprintf( temp," <img title='disable' src='disable.png' OnClick=\"imgrequest('/newcamdclient?id=%d&action=disable',this);\">",cli->id);
			strcat( cell[8], temp );
		}
	}
	sprintf( temp," <img title='Debug' src='debug.png' OnClick=\"imgrequest('/newcamdclient?id=%d&action=debug',this);\">",cli->id);
	strcat( cell[8], temp );
	strcat( cell[8], "</span>");

}

void http_send_newcamd(int sock, http_request *req) // page, div, row
{
	char http_buf[2048];
	char cell[10][2048];
	struct tcp_buffer_data tcpbuf;
	struct cs_client_data *cli;

	//  Get Params
	char *str_list = isset_get( req, "list");
	int get_list = LIST_ACTIVE;
	if (str_list) {
		if (!strcmp(str_list,"connected")) get_list = LIST_CONNECTED;
		else if (!strcmp(str_list,"all")) get_list = LIST_ALL;
		else str_list=NULL;
	}
	if (!str_list) str_list = "active";
	// Param 'action'
	char *str_action = isset_get( req, "action");
	int get_action;
	if (str_action) {
		if (!strcmp(str_action,"div")) get_action = ACTION_DIV;
		else if (!strcmp(str_action,"row")) get_action = ACTION_ROW;
		else if (!strcmp(str_action,"xml")) get_action = ACTION_XML; // Get Clients info in xml
		else if (!strcmp(str_action,"disable")) get_action = ACTION_DISABLE;
		else if (!strcmp(str_action,"enable")) get_action = ACTION_ENABLE;
		else if (!strcmp(str_action,"status")) get_action = ACTION_STATUS;
		else if (!strcmp(str_action,"debug")) get_action = ACTION_DEBUG;
		else str_action = NULL;
	}
	if (!str_action) { str_action = "page"; get_action = ACTION_PAGE; }

	// Profile ID
	char *str_pid = isset_get( req, "pid");
	int get_pid;
	if (str_pid) get_pid = atoi(str_pid); else get_pid = 0;
	struct cardserver_data *cs = getcsbyid(get_pid);
////
	if (get_action==ACTION_XML) {
		tcp_init(&tcpbuf);
		tcp_writestr(&tcpbuf, sock, "HTTP/1.1 200 OK\r\nAccept-Ranges: bytes\r\nConnection: close\r\nContent-Type: application/xml\r\n\r\n");

		tcp_writestr(&tcpbuf, sock, "<multics>");

		struct cardserver_data *srv;
		if (cs) srv = cs; else srv = cfg.cardserver;
		while (srv) {
			tcp_writestr(&tcpbuf, sock, "\n<newcamd>");
			sprintf(http_buf, "<id>%d</id>", srv->id); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			sprintf(http_buf, "<port>%d</port>", srv->newcamd.port); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			sprintf(http_buf, "<status>%d</status>", (srv->newcamd.handle>0) ); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			uint32_t ticks = GetTickCount();
			struct cs_client_data *cli = srv->newcamd.client;
			while (cli) {
				tcp_writestr(&tcpbuf, sock, "<user>");
				{ char hesc[512]; html_esc(hesc,(int)sizeof(hesc),cli->user);
				sprintf(http_buf, "<name>%s</name>", hesc); }
				tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				if (cli->connection.status>0) {
					tcp_writestr(&tcpbuf, sock, "<status>1</status>");
					sprintf( http_buf,"<ip>%s</ip>", (char*)ip2string(cli->ip) ); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
					char *p = getcountrycodebyip(cli->ip);
					if (p) sprintf(http_buf, "<country>%s</country>", p); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
					uint32_t d = (ticks - cli->connection.time)/1000;
					sprintf(http_buf, "<connected>%02dd %02d:%02d:%02d</connected>", d/(3600*24), (d/3600)%24, (d/60)%60, d%60); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				}
				else {
					sprintf(http_buf, "<status>%d</status>",cli->flags&0x0E);
					tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				}
				tcp_writestr(&tcpbuf, sock, "</user>");
				cli = cli->next;
			}
			tcp_writestr(&tcpbuf, sock, "\n</newcamd>");

			if (cs) break; else srv = srv->next;
		}
		tcp_writestr(&tcpbuf, sock, "\n</multics>");
		tcp_flush(&tcpbuf, sock);
		return;
	}

	char *id = isset_get( req, "id");
	if (id)	{ // XML
		int i = atoi(id);
		struct cs_client_data *cli = getnewcamdclientbyid(i);
		if (!cli) return;
		// Send XML CELLS
		getnewcamdclientcells(cli,cell);
		for(i=0; i<10; i++) xmlescape( cell[i] );
		char buf[5000] = "";
		sprintf( buf, "<newcamd>\n<c0>%s</c0>\n<c1>%s</c1>\n<c2>%s</c2>\n<c3_c>%s</c3_c>\n<c3>%s</c3>\n<c4>%s</c4>\n<c5>%s</c5>\n<c6>%s</c6>\n<c7>%s</c7>\n<c8>%s</c8>\n</newcamd>\n",cell[0],cell[1],cell[2],cell[9],cell[3],cell[4],cell[5],cell[6],cell[7],cell[8] );
		http_send_xml( sock, req, buf, strlen(buf));
		return;
	}

	tcp_init(&tcpbuf);
	if (get_action==ACTION_PAGE) {
		http_replyok_write(req, &tcpbuf, sock);
		http_html_write(req, &tcpbuf, sock);
		tcp_write(&tcpbuf, sock, http_head, strlen(http_head) );
		sprintf( http_buf, html_title, cfg.http.title, "Newcamd"); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		tcp_write(&tcpbuf, sock, http_link, strlen(http_link) );
		http_style_write(&tcpbuf, sock);
		// JS
		tcp_writestr(&tcpbuf, sock, "\n<script type='text/javascript'>");
		// ACTIONS REQUEST
		tcp_writestr(&tcpbuf, sock, "\nfunction imgrequest( url, el )\n{\n	var httpRequest;\n	try { httpRequest = new XMLHttpRequest(); }\n	catch (trymicrosoft) { try { httpRequest = new ActiveXObject('Msxml2.XMLHTTP'); } catch (oldermicrosoft) { try { httpRequest = new ActiveXObject('Microsoft.XMLHTTP'); } catch(failed) { httpRequest = false; } } }\n	if (!httpRequest) { alert('Your browser does not support Ajax.'); return false; }\n	if ( typeof(el)!='undefined' ) {\n		el.onclick = null;\n		el.style.opacity = '0.7';\n		httpRequest.onreadystatechange = function()\n		{\n			if (httpRequest.readyState == 4) if (httpRequest.status == 200) el.style.opacity = '0.3';\n		}\n	}\n	httpRequest.open('GET', url, true);\n	httpRequest.send(null);\n}\n");
		// UPD ROW
		tcp_writestr(&tcpbuf, sock, "\nfunction xmlupdateRow( xmlDoc, id )\n{\n	var row = document.getElementById(id);\n	row.cells.item(0).innerHTML = xmlDoc.getElementsByTagName('c0')[0].childNodes[0].nodeValue;\n	row.cells.item(1).innerHTML = xmlDoc.getElementsByTagName('c1')[0].childNodes[0].nodeValue;\n	row.cells.item(2).innerHTML = xmlDoc.getElementsByTagName('c2')[0].childNodes[0].nodeValue;\n	row.cells.item(3).className = xmlDoc.getElementsByTagName('c3_c')[0].childNodes[0].nodeValue;\n	row.cells.item(3).innerHTML = xmlDoc.getElementsByTagName('c3')[0].childNodes[0].nodeValue;\n	row.cells.item(4).innerHTML = xmlDoc.getElementsByTagName('c4')[0].childNodes[0].nodeValue;\n	row.cells.item(5).innerHTML = xmlDoc.getElementsByTagName('c5')[0].childNodes[0].nodeValue;\n	row.cells.item(6).innerHTML = xmlDoc.getElementsByTagName('c6')[0].childNodes[0].nodeValue;\n	row.cells.item(7).innerHTML = xmlDoc.getElementsByTagName('c7')[0].childNodes[0].nodeValue;\n	row.cells.item(8).innerHTML = xmlDoc.getElementsByTagName('c8')[0].childNodes[0].nodeValue;\n}\n");
		char url[256];
		sprintf( url, "'/newcamd?id='+idx");
		sprintf( http_buf, HTTP_UPDATE_ROW, url);
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		// UPD DIV
		sprintf( url, "/newcamd?pid=%d&list=%s&action=div", get_pid, str_list);
		sprintf( http_buf, HTTP_UPDATE_DIV, cfg.http.autorefresh*1000, url);
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		//
		tcp_writestr(&tcpbuf, sock, "\nfunction start()\n{\n	setautorefresh(autorefresh);\n}");
		tcp_writestr(&tcpbuf, sock, "\n</script>\n");
		//
		tcp_write(&tcpbuf, sock, http_head_, strlen(http_head_) );
		tcp_writestr(&tcpbuf, sock, "\n<body onload=\"start();\">");
		tcp_write_menu(&tcpbuf, sock,PAGE_NEWCAMD);
		// DIV
		tcp_writestr(&tcpbuf, sock, "<div id='mainDiv'>");
	}

	sprintf( http_buf, "<select style=\"width:200px;\" onchange=\"parent.location.href='/newcamd?pid='+this.value\"> ><option value=0>All Profiles(%d)</option>", total_profiles());
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	struct cardserver_data *tmp = cfg.cardserver;
	while (tmp) {
		if (tmp->id==get_pid) sprintf( http_buf, "<option value=%d selected> [%d] %s </option>", tmp->id, tmp->newcamd.port, tmp->name);
		else sprintf( http_buf, "<option value=%d> [%d] %s </option>", tmp->id, tmp->newcamd.port, tmp->name);
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		tmp = tmp->next;
	}
	sprintf( http_buf, "</select> ");
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );

	//
	int total, connected, active;
	if (cs) cs_clients( cs, &total, &connected, &active ); else cs_allclients( &total, &connected, &active );
	char *class1 = "button"; char *class2 = "sbutton";
	char *class;
	if (get_list==LIST_ACTIVE) class = class2; else class = class1;
	sprintf( http_buf, " <input type=button class=%s onclick=\"parent.location='/newcamd?pid=%d&amp;list=active'\" value='Active Clients(%d)'>", class, get_pid,active);
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	if (get_list==LIST_CONNECTED) class = class2; else class = class1;
	sprintf( http_buf, " <input type=button class=%s onclick=\"parent.location='/newcamd?pid=%d&amp;list=connected'\" value='Connected Clients(%d)'>", class, get_pid,connected);
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	if (get_list==LIST_ALL) class = class2; else class = class1;
	sprintf( http_buf, " <input type=button class=%s onclick=\"parent.location='/newcamd?pid=%d&amp;list=all'\" value='All Clients(%d)'>", class, get_pid,total);
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );


	//NEWCAMD CLIENTS
	if (cs) {
		//
		sprintf( http_buf, "<br><table class=maintable width=100%%><tr><th width=100px>Client</th><th width=70px>Program</th><th width=120px>IP Address</th><th width=100px>Connected</th><th width=60px>TotalEcm</th><th width=90px>AcceptedEcm</th><th width=90px>EcmOK</th><th width=50px>EcmTime</th><th>Last used share</th></tr>\n");
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		cli = cs->newcamd.client;
		int alt=0;

		if (get_list==LIST_ACTIVE) {
			while (cli) {
				if ( (cli->connection.status>0)&&((GetTickCount()-cli->lastecmtime) < 20000) ) {
					if (alt==1) alt=2; else alt=1;
					getnewcamdclientcells(cli, cell);
					/* R35: HTML rows render composed cells as-is; raw text fields are pre-escaped in the builders (escf/html_esc). Blanket escape here printed the page's own tags as text. */
					sprintf( http_buf,"<tr id=\"Row%d\" class=alt%d onMouseOver='setupdateRow(%d)' onMouseOut='setupdateRow(0)'> <td>%s</td><td>%s</td><td>%s</td><td class=%s>%s</td><td>%s</td><td>%s</td><td>%s</td><td>%s</td><td>%s</td></tr>\n",cli->id,alt,cli->id,cell[0],cell[1],cell[2],cell[9],cell[3],cell[4],cell[5],cell[6],cell[7],cell[8]);
					tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				}
				cli = cli->next;
			}
		}
		else if (get_list==LIST_ALL) {
			while (cli) {
				if (alt==1) alt=2; else alt=1;
				getnewcamdclientcells(cli, cell);
				/* R35: HTML rows render composed cells as-is; raw text fields are pre-escaped in the builders (escf/html_esc). Blanket escape here printed the page's own tags as text. */
				sprintf( http_buf,"<tr id=\"Row%d\" class=alt%d onMouseOver='setupdateRow(%d)' onMouseOut='setupdateRow(0)'> <td>%s</td><td>%s</td><td>%s</td><td class=%s>%s</td><td>%s</td><td>%s</td><td>%s</td><td>%s</td><td>%s</td></tr>\n",cli->id,alt,cli->id,cell[0],cell[1],cell[2],cell[9],cell[3],cell[4],cell[5],cell[6],cell[7],cell[8]);
				tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				cli = cli->next;
			}
		}
		else if (get_list==LIST_CONNECTED) {
			while (cli) {
				if (cli->connection.status>0) {
					if (alt==1) alt=2; else alt=1;
					getnewcamdclientcells(cli, cell);
					/* R35: HTML rows render composed cells as-is; raw text fields are pre-escaped in the builders (escf/html_esc). Blanket escape here printed the page's own tags as text. */
					sprintf( http_buf,"<tr id=\"Row%d\" class=alt%d onMouseOver='setupdateRow(%d)' onMouseOut='setupdateRow(0)'> <td>%s</td><td>%s</td><td>%s</td><td class=%s>%s</td><td>%s</td><td>%s</td><td>%s</td><td>%s</td><td>%s</td></tr>\n",cli->id,alt,cli->id,cell[0],cell[1],cell[2],cell[9],cell[3],cell[4],cell[5],cell[6],cell[7],cell[8]);
					tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				}
				cli = cli->next;
			}
		}
	
		sprintf( http_buf, "</table>");
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	}
	else {
		//
		sprintf( http_buf, "<br><table class=maintable width=100%%><tr><th width=100px>Client</th><th width=70px>Program</th><th width=120px>IP Address</th><th width=100px>Connected</th><th width=60px>TotalEcm</th><th width=90px>AcceptedEcm</th><th width=90px>EcmOK</th><th width=50px>EcmTime</th><th>Last used share</th></tr>\n");
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );

		int alt=0;
		struct cardserver_data *cs = cfg.cardserver;
		while (cs) {
			int total, connected, active;
			cs_clients( cs, &total, &connected, &active );
			if ( (get_list==LIST_ACTIVE) && active ) {
				sprintf( http_buf, "<tr><td class=alt3 colspan=9>%s (%d)</td></tr>\n", cs->name, active); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				cli = cs->newcamd.client;
				while (cli) {
					if ( (cli->connection.status>0)&&((GetTickCount()-cli->lastecmtime) < 20000) ) {
						if (alt==1) alt=2; else alt=1;
						getnewcamdclientcells(cli, cell);
						/* R35: HTML rows render composed cells as-is; raw text fields are pre-escaped in the builders (escf/html_esc). Blanket escape here printed the page's own tags as text. */
						sprintf( http_buf,"<tr id=\"Row%d\" class=alt%d onMouseOver='setupdateRow(%d)' onMouseOut='setupdateRow(0)'> <td>%s</td><td>%s</td><td>%s</td><td class=%s>%s</td><td>%s</td><td>%s</td><td>%s</td><td>%s</td><td>%s</td></tr>\n",cli->id,alt,cli->id,cell[0],cell[1],cell[2],cell[9],cell[3],cell[4],cell[5],cell[6],cell[7],cell[8]);
						tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
					}
					cli = cli->next;
				}
			}
			else if ( (get_list==LIST_ALL) && total ) {
				sprintf( http_buf, "<tr><td class=alt3 colspan=9>%s (%d)</td></tr>\n", cs->name, total); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				cli = cs->newcamd.client;
				while (cli) {
					if (alt==1) alt=2; else alt=1;
					getnewcamdclientcells(cli, cell);
					/* R35: HTML rows render composed cells as-is; raw text fields are pre-escaped in the builders (escf/html_esc). Blanket escape here printed the page's own tags as text. */
					sprintf( http_buf,"<tr id=\"Row%d\" class=alt%d onMouseOver='setupdateRow(%d)' onMouseOut='setupdateRow(0)'> <td>%s</td><td>%s</td><td>%s</td><td class=%s>%s</td><td>%s</td><td>%s</td><td>%s</td><td>%s</td><td>%s</td></tr>\n",cli->id,alt,cli->id,cell[0],cell[1],cell[2],cell[9],cell[3],cell[4],cell[5],cell[6],cell[7],cell[8]);
					tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
					cli = cli->next;
				}
			}
			else if ( (get_list==LIST_CONNECTED) && connected ) {
				sprintf( http_buf, "<tr><td class=alt3 colspan=9>%s (%d)</td></tr>\n", cs->name, connected); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				cli = cs->newcamd.client;
				while (cli) {
					if (cli->connection.status>0) {
						if (alt==1) alt=2; else alt=1;
						getnewcamdclientcells(cli, cell);
						/* R35: HTML rows render composed cells as-is; raw text fields are pre-escaped in the builders (escf/html_esc). Blanket escape here printed the page's own tags as text. */
						sprintf( http_buf,"<tr id=\"Row%d\" class=alt%d onMouseOver='setupdateRow(%d)' onMouseOut='setupdateRow(0)'> <td>%s</td><td>%s</td><td>%s</td><td class=%s>%s</td><td>%s</td><td>%s</td><td>%s</td><td>%s</td><td>%s</td></tr>\n",cli->id,alt,cli->id,cell[0],cell[1],cell[2],cell[9],cell[3],cell[4],cell[5],cell[6],cell[7],cell[8]);
						tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
					}
					cli = cli->next;
				}
			}
			cs = cs->next;
		}
		sprintf( http_buf, "</table>");
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	}

	if (get_action==ACTION_PAGE) {
		tcp_writestr(&tcpbuf, sock, "</div></body></html>");
	}

	tcp_flush(&tcpbuf, sock);
}


///////////////////////////////////////////////////////////////////////////////
void http_send_newcamd_client(int sock, http_request *req)
{
	char http_buf[2048];
	struct tcp_buffer_data tcpbuf;
	char *str_id = isset_get( req, "id");
	if (!str_id) return; //error
	int get_id = atoi(str_id);
	//
	struct cs_client_data *cli = getnewcamdclientbyid( get_id );
	if (!cli) return;
	// Action
	char *str_action = isset_get( req, "action");
	int get_action = 0;
	if (str_action) {
		if (!strcmp(str_action,"div")) get_action = 1;
		else if (!strcmp(str_action,"row")) get_action = 2;
		else if (!strcmp(str_action,"disable")) get_action = 3;
		else if (!strcmp(str_action,"enable")) get_action = 4;
		else if (!strcmp(str_action,"status")) get_action = 5;
		else if (!strcmp(str_action,"info")) get_action = 6; // XML info
		else if (!strcmp(str_action,"debug")) get_action = 7;
		else str_action = NULL;
	}
	if (!str_action) str_action = "page";
	//
	if (get_action==3) {
		cli->flags |= FLAG_DISABLE;
		if (cli->connection.status>0) cs_disconnect_cli(cli);
		http_send_ok(sock);
		return;
	}
	else if (get_action==4) {
		cli->flags &= ~FLAG_DISABLE;
		http_send_ok(sock);
		return;
	}
	else if (get_action==5) {
		if (cli->connection.status>0) http_send_text(sock,"connected"); else http_send_text(sock,"disconnected");
		return;
	}
	else if (get_action==7) {
		flagdebug = getdbgflag( DBG_NEWCAMD, cli->pid, cli->id);
		http_send_ok(sock);
		return;
	}
	//
	tcp_init(&tcpbuf);
	if (get_action==0) {
		http_replyok_write(req, &tcpbuf, sock);
		http_html_write(req, &tcpbuf, sock);
		tcp_write(&tcpbuf, sock, http_head, strlen(http_head) );
		sprintf( http_buf, html_title, cfg.http.title, "Newcamd Client"); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		tcp_write(&tcpbuf, sock, http_link, strlen(http_link) );
		http_style_write(&tcpbuf, sock);
		// JS
		tcp_writestr(&tcpbuf, sock, "\n<script type='text/javascript'>");
		// UPD DIV
		char url[256];
		sprintf( url, "/newcamdclient?id=%d&action=div", get_id);
		sprintf( http_buf, HTTP_UPDATE_DIV, cfg.http.autorefresh*1000, url);
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		//
		tcp_writestr(&tcpbuf, sock, "\nfunction start()\n{\n	setautorefresh(autorefresh);\n}");
		tcp_writestr(&tcpbuf, sock, "\n</script>\n");
		tcp_write(&tcpbuf, sock, http_head_, strlen(http_head_) );
		tcp_writestr(&tcpbuf, sock, "<body onload=\"start();\">");
		tcp_write_menu(&tcpbuf, sock,0);
		// DIV
		tcp_writestr(&tcpbuf, sock, "<div id='mainDiv'>");
	}
	//
	tcp_writestr(&tcpbuf, sock, "<table style=\"padding:0px; margin:0px;\" width=\"100%%\"><tbody>\n" );
	tcp_writestr(&tcpbuf, sock, "<tr><td style=\"vertical-align:top; width:400px;\">\n" );
	//
	tcp_writestr(&tcpbuf, sock, "<table class=infotable><tbody>\n<tr><th colspan=2>Newcamd Client Informations</th></tr>\n" );
	// Profile
	struct cardserver_data *cs = getcsbyid( cli->pid );
	if (cs) {
		sprintf( http_buf,"<tr><td class=left>Profile</td><td class=right><a href='/profile?id=%d'>%s</a></td></tr>\n", cs->id, cs->name);
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	}
	// NAME
	{ char hesc[512]; html_esc(hesc,(int)sizeof(hesc),cli->user);
sprintf( http_buf,"<tr><td class=left>User name</td><td class=right>%s</td></tr>\n",hesc); }
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	// Connection Time
	if (cli->connection.status>0) {
		tcp_writestr(&tcpbuf, sock, "<tr><td class=left>Status</td><td class=right>Connected</td></tr>\n");
		uint32_t d = (GetTickCount()-cli->connection.time)/1000;
		sprintf( http_buf,"<tr><td class=left>Connection time</td><td class=right>%02dd %02d:%02d:%02d</td></tr>\n", d/(3600*24), (d/3600)%24, (d/60)%60, d%60);
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		// IP
		sprintf( http_buf,"<tr><td class=left>IP Address</td><td class=right>%s</td></tr>\n",(char*)ip2string(cli->ip) );
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		// Program ID
		sprintf( http_buf,"<tr><td class=left>Client Program</td><td class=right>%s(%04x)</td></tr>",programid(cli->progid), cli->progid );
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	}
	else {
		tcp_writestr(&tcpbuf, sock, "<tr><td class=left>Status</td><td class=right>Disconnected</td></tr>\n");
		if ( cli->connection.status<=0 && cli->connection.lastseen) {
			uint32_t d = (GetTickCount()-cli->connection.lastseen)/1000;
			sprintf( http_buf,"<tr><td class=left>Last Seen</td><td class=right>%02dd %02d:%02d:%02d</td></tr>\n", d/(3600*24),(d/3600)%24,(d/60)%60,d%60);
			tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		}
	}
	// UPTIME
	if ( cli->connection.uptime || (cli->connection.status>0) ) {
		uint32_t uptime;
		if (cli->connection.status>0) uptime = (GetTickCount()-cli->connection.time)+cli->connection.uptime; else uptime = cli->connection.uptime;
		uptime /= 1000;
		sprintf( http_buf,"<tr><td class=left>Uptime</td><td class=right>%02dd %02d:%02d:%02d</td></tr>",uptime/(3600*24),(uptime/3600)%24,(uptime/60)%60,uptime%60);
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	}
#ifdef CHECK_NEXTDCW
	sprintf( http_buf,"<tr><td class=left>DCW CHECK</td><td class=right>%s</td></tr>", yesno(cli->dcwcheck) );
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
#endif
	tcp_writestr(&tcpbuf, sock, "</tbody></table><br>\n" );

	// INFO
	struct client_info_data *info = cli->info;
	if (info) {
		tcp_writestr(&tcpbuf, sock, "<table class=\"infotable\"><tbody>\n" );
		tcp_writestr(&tcpbuf, sock, "<tr><th colspan=2>Additional Informations</th></tr>\n" );
		while (info) {
			sprintf( http_buf,"<tr><td class=left>%s</td><td class=right>%s</td></tr>\n",info->name,info->value);
			tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			info = info->next;
		}
		tcp_writestr(&tcpbuf, sock, "</tbody></table><br>\n" );
	}

	// Ecm Stat
	tcp_writestr(&tcpbuf, sock, "<table class=\"infotable\"><tbody>\n" );
	tcp_writestr(&tcpbuf, sock, "<tr><th colspan=2>ECM Statistics</th></tr>\n" );
	int ecmaccepted = cli->ecmnb-cli->ecmdenied;
	sprintf( http_buf, "<tr><td class=left>Total ECM requests</td><td class=right>%d</td></tr>\n", cli->ecmnb);
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	sprintf( http_buf, "<tr><td class=left>Accepted ECM requests</td><td class=right>%d</td></tr>\n", ecmaccepted);
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	sprintf( http_buf, "<tr><td class=left>Good ECM answer</td><td class=right>%d</td></tr>\n", cli->ecmok);
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	//Ecm Time
	if (cli->ecmok) {
		sprintf( http_buf,"<tr><td class=left>Average Time</td><td class=right>%d ms</td></tr>\n",(cli->ecmoktime/cli->ecmok) );
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	}
#ifdef SRV_CSCACHE
	sprintf( http_buf, "<tr><td class=left>Cached DCW</td><td class=right>%d</td></tr>\n", cli->cachedcw);
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
#endif
	// Freeze
	sprintf( http_buf,"<tr><td class=left>Total Freeze</td><td class=right>%d</td></tr>\n", cli->freeze);
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	tcp_writestr(&tcpbuf, sock, "</tbody></table><br>\n" );


	tcp_writestr(&tcpbuf, sock, "</td><td style=\"vertical-align:top;\">\n" );

	//Last Used Share
	if ( cli->lastecm.caid ) {
		tcp_writestr(&tcpbuf, sock, "<table class=\"infotable\"><tbody>\n" );
		tcp_writestr(&tcpbuf, sock, "<tr><th>Last Used share</th></tr>\n");
		// Decode Status
		if (cli->lastecm.status)
			sprintf( http_buf,"<tr><td>Decode success</td></tr>\n");
		else
			sprintf( http_buf,"<tr><td>Decode failed</td></tr>\n");
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		// Channel
		sprintf( http_buf,"<tr><td>Channel %s (%dms) %s</td></tr>\n", getchname(cli->lastecm.caid, cli->lastecm.prov, cli->lastecm.sid) , cli->lastecm.decodetime, str_laststatus[cli->lastecm.status] );
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );

		// Server
		if ( (GetTickCount()-cli->ecm.recvtime) < 20000 ) {
			// From ???
			if (cli->lastecm.status) {
				tcp_writestr(&tcpbuf, sock, "<tr><td>From ");
				src2string(cli->lastecm.dcwsrctype, cli->lastecm.dcwsrcid, http_buf );
				tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				tcp_writestr(&tcpbuf, sock, "</td></tr>");
			}
			// Last ECM
			ECM_DATA *ecm = cli->lastecm.request;
			// ECM
			sprintf( http_buf,"<tr><td>ECM(%d): ", ecm->ecmlen); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			array2hex( ecm->ecm, http_buf, ecm->ecmlen );	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			sprintf( http_buf,"</td></tr>\n"); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			// DCW
			if (cli->lastecm.status) {
				sprintf( http_buf,"<tr><td>CW: ");	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				array2hex( ecm->cw, http_buf, 16 );	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				sprintf( http_buf,"</td></tr>\n"); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			}
#ifdef CHECK_NEXTDCW
			if ( ecm->lastdecode.ecm && (ecm->lastdecode.counter>0) ) {
				sprintf( http_buf,"<tr><td>Previous CW: "); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				array2hex( ecm->lastdecode.dcw, http_buf, 16 ); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				tcp_writestr(&tcpbuf, sock, "</td></tr>\n");
				if (ecm->lastdecode.error) {
					sprintf( http_buf,"<tr><td>Errors = %d</td></tr>\n", ecm->lastdecode.error);
					tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				}
				sprintf( http_buf,"<tr><td>Total Cycles = %d</td></tr>\n<tr><td>ECM Interval = %ds</td></tr>\n", ecm->lastdecode.counter, ecm->lastdecode.dcwchangetime/1000);
				tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			}
#endif
			//
			if (ecm->server[0].srvid) {
				sprintf( http_buf, "<tr><td><table class='infotable'><tbody><tr><th width='30px'>ID</th><th width='250px'>Server</th><th width='50px'>Status</th><th width='70px'>Start time</th><th width='70px'>End time</th><th width='90px'>Elapsed time</th><th>CW</th></tr></tbody>");
				tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				int i;
				for(i=0; i<20; i++) {
					if (!ecm->server[i].srvid) break;
					char* str_srvstatus[] = { "WAIT", "OK", "NOK", "BUSY" };
					struct server_data *srv = getsrvbyid(ecm->server[i].srvid);
					if (srv) {
						sprintf( http_buf,"<tr><td>%d</td><td>%s:%d</td><td>%s</td><td>%dms</td>", i+1, srv->host->name, srv->port, str_srvstatus[ecm->server[i].flag], ecm->server[i].sendtime - ecm->recvtime );
						tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
						// Recv Time
						if (ecm->server[i].statustime>ecm->server[i].sendtime)
							sprintf( http_buf,"<td>%dms</td><td>%dms</td>", ecm->server[i].statustime - ecm->recvtime, ecm->server[i].statustime-ecm->server[i].sendtime );
						else
							sprintf( http_buf,"<td>--</td><td>--</td>");
						tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
						// DCW
						if (ecm->server[i].flag==ECM_SRV_REPLY_GOOD) {
							sprintf( http_buf,"<td>"); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
							array2hex( ecm->server[i].dcw, http_buf, 16 );	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
							sprintf( http_buf,"</td>"); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
						}
						else {
							sprintf( http_buf,"<td>--</td>");
							tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
						}
						sprintf( http_buf,"</tr>");
						tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
					}
				}
				tcp_writestr(&tcpbuf, sock, "</tbody></table></td></tr>\n" );
			}
		}
		tcp_writestr(&tcpbuf, sock, "</tbody></table><br>\n" );
	}

	// Current Busy Ecm
	if (cli->ecm.busy) {
		ECM_DATA *ecm = cli->ecm.request;
		if (ecm) http_send_ecmstatus(&tcpbuf, sock, ecm);
	}

	tcp_writestr(&tcpbuf, sock, "</td></tr></tbody></table>" );

	if (get_action==0) {
		tcp_writestr(&tcpbuf, sock, "</div>");
		tcp_writestr(&tcpbuf, sock, "</body></html>");
	}
	tcp_flush(&tcpbuf, sock);
}


void http_send_profile(int sock, http_request *req)
{
	char http_buf[1024];
	struct tcp_buffer_data tcpbuf;
	// Get Profile
	int get_id = 0;
	char *str_id = isset_get( req, "id");
	if (str_id)	get_id = atoi(str_id);
	struct cardserver_data *cs = getcsbyid(get_id);
	if (!cs) return;
	// Action
	char *str_action = isset_get( req, "action");
	int get_action = 0;
	if (str_action) {
		if (!strcmp(str_action,"div")) get_action = 1;
		else if (!strcmp(str_action,"row")) get_action = 2;
		else if (!strcmp(str_action,"disable")) get_action = 3;
		else if (!strcmp(str_action,"enable")) get_action = 4;
		else if (!strcmp(str_action,"status")) get_action = 5;
		else if (!strcmp(str_action,"xml")) get_action = 6; // XML info
		else if (!strcmp(str_action,"debug")) get_action = 7;
		else str_action = NULL;
	}
	if (!str_action) str_action = "page";
	//
	if (get_action==3) {
		cs->flags |= FLAG_DISABLE;
		////////// cc_disconnect_cli(cli);
		http_send_ok(sock);
		return;
	}
	else if (get_action==4) {
		cs->flags &= ~FLAG_DISABLE;
		http_send_ok(sock);
		return;
	}
	else if (get_action==5) {
		if (IS_DISABLED(cs->flags)) http_send_text(sock,"active"); else http_send_text(sock,"inactive");
		return;
	}
	else if (get_action==7) {
		flagdebug = getdbgflag( DBG_NEWCAMD, cs->id, 0);
		http_send_ok(sock);
		return;
	}

	//
	tcp_init(&tcpbuf);
	http_replyok_write(req, &tcpbuf, sock);
	http_html_write(req, &tcpbuf, sock);
	tcp_write(&tcpbuf, sock, http_head, strlen(http_head) );
	sprintf( http_buf, html_title, cfg.http.title, "Profile"); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	tcp_write(&tcpbuf, sock, http_link, strlen(http_link) );
	http_style_write(&tcpbuf, sock);
	tcp_write(&tcpbuf, sock, http_head_, strlen(http_head_) );
	tcp_write(&tcpbuf, sock, http_body, strlen(http_body) );
	tcp_write_menu(&tcpbuf, sock,0);

	sprintf( http_buf, "<input type=button onclick=\"parent.location='/newcamd?pid=%d'\" value='Newcamd Clients'>", cs->id);
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );

	sprintf( http_buf, "<br><br><div class=\"outer\"> <div class=\"top\"><b>Profile: %s</b><ul><li>Newcamd Port = %d</li>",cs->name, cs->newcamd.port);
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
#ifdef RADEGAST_SRV
	sprintf( http_buf, "<li>Radegast Port = %d</li>", cs->radegast.port);
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
#endif
	sprintf( http_buf, "<li>Network ID = %04X</li>", cs->option.onid);
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	sprintf( http_buf, "<li>Caid = %04X</li><li>Providers =", cs->card.caid);
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	int i;
	for (i=0;i<cs->card.nbprov;i++) {
		sprintf( http_buf, " %06x",cs->card.prov[i].id);
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	}
	sprintf( http_buf, "</li><li>Total ECM = %d</li> <li>Accepted ECM = %d</li><li>Ecm OK = %d</li><li>Ecm Time = %dms</li><li>Total Cache Hits = %d</li><li>Instant Cache Hits = %d</li></ul>", cs->ecmaccepted+cs->ecmdenied, cs->ecmaccepted, cs->ecmok, cs->ecmoktime/(cs->ecmok+1), cs->hits.csp, cs->hits.instant.csp);
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );

	sprintf( http_buf, "</div><span style=\"float:right\"><table class=option border=1px cellspacing=0><tr><th width=150px>Option</th><th width=50px>Value</th></tr>");
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );

	sprintf( http_buf,"<tr><td>ECM CHECK</td><td>%s</td></tr>", yesno(cs->option.checkecm)); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	sprintf( http_buf,"<tr><td>ECM CHECK LENGTH</td><td>%s</td></tr>", yesno(cs->option.checkecmlength)); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	sprintf( http_buf,"<tr><td>DCW TIMEOUT</td><td>%dms</td></tr>", cs->option.dcw.timeout); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	sprintf( http_buf,"<tr><td>DCW RETRY</td><td>%d</td></tr>", cs->option.dcw.retry ); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	sprintf( http_buf,"<tr><td>DCW CHECK</td><td>%s</td></tr>", yesno(cs->option.dcw.check) ); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	sprintf( http_buf,"<tr><td>DCW HALFNULLED</td><td>%s</td></tr>", yesno(cs->option.dcw.halfnulled) ); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
#ifdef DCWSWAP
	sprintf( http_buf,"<tr><td>DCW SWAP</td><td>%s</td></tr>", yesno(cs->option.dcw.swap) ); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
#endif
	sprintf( http_buf,"<tr><td>DCW MAXFAILED</td><td>%d</td></tr>", cs->option.maxfailedecm); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	sprintf( http_buf,"<tr><td>SERVER MAX</td><td>%d</td></tr>", cs->option.server.max); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	sprintf( http_buf,"<tr><td>SERVER INTERVAL</td><td>%dms</td></tr>", cs->option.server.interval); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	sprintf( http_buf,"<tr><td>SERVER TIMEOUT</td><td>%dms</td></tr>", cs->option.server.timeout); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
//	sprintf( http_buf,"<tr><td>SERVER TIMEPERECM:</td><td>%d</td></tr>", cs->option.server.timeperecm); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	sprintf( http_buf,"<tr><td>SERVER VALIDECMTIME</td><td>%dms</td></tr>", cs->option.server.validecmtime); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	sprintf( http_buf,"<tr><td>SERVER FIRST</td><td>%d</td></tr>", cs->option.server.first); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	//
	sprintf( http_buf,"<tr><td>ACCEPT NULL CAID</td><td>%s</td></tr>", yesno(cs->option.faccept0caid) ); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	sprintf( http_buf,"<tr><td>ACCEPT NULL PROVIDER</td><td>%s</td></tr>", yesno(cs->option.faccept0provider) ); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	sprintf( http_buf,"<tr><td>ACCEPT NULL SID</td><td>%s</td></tr>", yesno(cs->option.faccept0sid) ); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	//
	sprintf( http_buf,"<tr><td>ENABLE CCCAM</td><td>%s</td></tr>", yesno(cs->option.fallowcccam) ); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	sprintf( http_buf,"<tr><td>ENABLE NEWCAMD</td><td>%s</td></tr>", yesno(cs->option.fallownewcamd) ); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	sprintf( http_buf,"<tr><td>ENABLE RADEGAST</td><td>%s</td></tr>", yesno(cs->option.fallowradegast) ); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	sprintf( http_buf,"<tr><td>ENABLE CAMD35</td><td>%s</td></tr>", yesno(cs->option.fallowcamd35) ); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	sprintf( http_buf,"<tr><td>ENABLE CS378X</td><td>%s</td></tr>", yesno(cs->option.fallowcs378x) ); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	sprintf( http_buf,"<tr><td>ENABLE CACHE</td><td>%s</td></tr>", yesno(cs->option.fallowcache) ); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
#ifdef CACHEEX
	sprintf( http_buf,"<tr><td>ENABLE CACHEEX</td><td>%s</td></tr>", yesno(cs->option.fallowcacheex) ); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	sprintf( http_buf,"<tr><td>CACHEEX MAXHOP</td><td>%d</td></tr>", cs->option.cacheex.maxhop ); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	//sprintf( http_buf,"<tr><td>CACHEEX VALIDECMTIME</td><td>%dms</td></tr>", cs->option.cacheexvalidtime ); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
#endif
	//
	sprintf( http_buf,"<tr><td>RETRY NEWCAMD</td><td>%d</td></tr>", cs->option.retry.newcamd); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	sprintf( http_buf,"<tr><td>RETRY CCCAM</td><td>%d</td></tr>", cs->option.retry.cccam); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	sprintf( http_buf,"<tr><td>CACHE TIMEOUT</td><td>%dms</td></tr>", cs->option.cachetimeout); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	sprintf( http_buf,"<tr><td>CACHE SENDREQ</td><td>%s</td></tr>", yesno(cs->option.cachesendreq) ); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	//sprintf( http_buf,"<tr><td>CACHE RESENDREQ</td><td>%s</td></tr>", yesno(cs->option.cacheresendreq) ); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	sprintf( http_buf,"<tr><td>CACHE SENDREP</td><td>%s</td></tr>", yesno(cs->option.cachesendrep) ); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	sprintf( http_buf,"<tr><td>CACHE STATIC</td><td>%s</td></tr>", yesno(cs->option.cachestatic) ); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	sprintf( http_buf, "</table></span></div><br><br>"); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	tcp_writestr(&tcpbuf, sock, "<div style=\"clear:both\"></div>" );

/*
#ifdef RADEGAST_SRV
	struct rdgd_client_data *rdgdcli;
	if (cs->radegast.handle && cs->radegast.client) {
		//READEGAST CLIENTS
		sprintf( http_buf, "<br>Connected Radegast Clients: %d<br><table class=maintable width=100%%><tr><th width=110px>IP Address</th><th width=100px>Connected</th><th width=60px>TotalEcm</th><th width=90px>AcceptedEcm</th><th width=90px>EcmOK</th><th width=50px>EcmTime</th><th>Last used share</th></tr>", connected_radegast_clients(cs));
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		rdgdcli = cs->radegast.client;
		int alt=0;
		while (rdgdcli) {
			if (rdgdcli->handle>0) {
				if (alt==1) alt=2; else alt=1;
				d = (GetTickCount()-rdgdcli->connected)/1000;
				if (rdgdcli->ecm.busy)
					sprintf( http_buf,"<tr class=alt%d><td>%s</td><td class=\"busy\">%02dd %02d:%02d:%02d</td>",alt,(char*)ip2string(rdgdcli->ip), d/(3600*24), (d/3600)%24, (d/60)%60, d%60);
				else
					sprintf( http_buf,"<tr class=alt%d><td>%s</td><td class=\"online\">%02dd %02d:%02d:%02d</td>",alt,(char*)ip2string(rdgdcli->ip), d/(3600*24), (d/3600)%24, (d/60)%60, d%60);
				tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );

				sprintf( http_buf, "<td align=center>%d</td>", rdgdcli->ecmnb );
				tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				int ecmaccepted = rdgdcli->ecmnb-rdgdcli->ecmdenied;
				tcp_writeecmdata(&tcpbuf, sock, ecmaccepted, rdgdcli->ecmnb);
				tcp_writeecmdata(&tcpbuf, sock, rdgdcli->ecmok, ecmaccepted);
				//Ecm Time
				if (rdgdcli->ecmok)
					sprintf( http_buf,"<td align=center>%d ms</td>",(rdgdcli->ecmoktime/rdgdcli->ecmok) );
				else
					sprintf( http_buf,"<td align=center>-- ms</td>");
				tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				//Last Used Share
				if ( rdgdcli->ecm.lastcaid ) {
					if (rdgdcli->ecm.laststatus) sprintf( http_buf,"<td class=success>"); else sprintf( http_buf,"<td class=failed>");
					tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
					sprintf( http_buf,"ch %s (%dms) %s ", getchname(rdgdcli->ecm.lastcaid, rdgdcli->ecm.lastprov, rdgdcli->ecm.lastsid) , rdgdcli->ecm.lastdecodetime, str_laststatus[rdgdcli->ecm.laststatus] );
					tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
					if ( (GetTickCount()-rdgdcli->ecm.recvtime) < 20000 ) {
						if (rdgdcli->ecm.lastdcwsrctype==DCW_SOURCE_SERVER) {
							struct server_data *srv = getsrvbyid(rdgdcli->ecm.lastdcwsrcid);
							if (srv) {
								sprintf( http_buf," / from server (%s:%d)", srv->host->name, srv->port);
								tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
							}
						}
						else if (rdgdcli->ecm.lastdcwsrctype==DCW_SOURCE_CACHE) {
							struct cachepeer_data *peer = getpeerbyid(rdgdcli->ecm.lastdcwsrcid);
							if (peer) {
								sprintf( http_buf," / from cache peer (%s:%d)", peer->host->name, peer->port);
								tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
							}
						}
					}
					sprintf( http_buf,"</td>");
					tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				}
				else {
					sprintf( http_buf,"<td> </td>");
					tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				}
				sprintf( http_buf,"</tr>");
				tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			}
			rdgdcli = rdgdcli->next;
		}
		sprintf( http_buf, "</table>");
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	}
#endif

*/

	// Send Stat
	sprintf( http_buf, "<style type=\"text/css\">\n.mainborder\n{ background: #d2d2d2; border: 1px solid #0B198C; border-spacing: 0px; font: 10px verdana, geneva, lucida, 'lucida grande', arial, helvetica, sans-serif; padding: 1 2; }\n.redborder { background: #d25555; border-left: 1px solid #eee; border-right: 1px solid #eee; border-bottom: 1px solid #eee; border-spacing: 0px; font: 9px verdana, geneva, lucida, 'lucida grande', arial, helvetica, sans-serif; }\n.greenborder { background: #55d255; border-left: 1px solid #eee; border-right: 1px solid #eee; border-bottom: 1px solid #eee; border-spacing: 0px; font: 9px verdana, geneva, lucida, 'lucida grande', arial, helvetica, sans-serif; }\n.cacheborder { background: #5555e2; border-left: 1px solid #eee; border-right: 1px solid #eee; border-bottom: 1px solid #eee; border-spacing: 0px; font: 9px verdana, geneva, lucida, 'lucida grande', arial, helvetica, sans-serif; }\n</style>\n");
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
#ifdef SRV_CSCACHE
	sprintf( http_buf, "<style type=\"text/css\">\n.clientsborder { background: goldenrod; border-left: 1px solid #eee; border-right: 1px solid #eee; border-bottom: 1px solid #eee; border-spacing: 0px; font: 9px verdana, geneva, lucida, 'lucida grande', arial, helvetica, sans-serif; }\n</style>\n");
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
#endif
#ifdef CACHEEX
	sprintf( http_buf, "<style type=\"text/css\">\n.cacheexborder { background: darkblue; border-left: 1px solid #eee; border-right: 1px solid #eee; border-bottom: 1px solid #eee; border-spacing: 0px; font: 9px verdana, geneva, lucida, 'lucida grande', arial, helvetica, sans-serif; }\n</style>\n");
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
#endif
	if (cs->ecmok) {
		sprintf( http_buf, "\n<br><br><table class=\"mainborder\" width=100%%>"); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		sprintf( http_buf, "<tr><td><div class=\"redborder\" style=\"height: 10px; width: 10px;\"></div> </td><td width=100%%>Total DCW number</td></tr><tr><td><div class=greenborder style=\"height: 10px; width: 10px;\"></div> </td><td width=100%%>Number of DCW from servers</td></tr><tr><td><div class=cacheborder style=\"height: 10px; width: 10px;\"></div> </td><td width=100%%>Number of DCW from Cache</td></tr><tr><td><div class=cacheexborder style=\"height: 10px; width: 10px;\"></div> </td><td width=100%%>Number of DCW from CacheEX</td></tr><tr><td><div class=clientsborder style=\"height: 10px; width: 10px;\"></div> </td><td width=100%%>Number of DCW from Newcamd/Mgcamd Clients</td></tr>\n"); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		//Get Max of ttime
		int max=1;
		int timeout = cs->option.dcw.timeout * (cs->option.dcw.retry+1);
		if (timeout>10000) timeout = 10000;
		for(i=0; i<(timeout/100); i++) if (max<cs->ttime[i]) max=cs->ttime[i];
		for(i=0; i<(timeout/100); i++) {
			sprintf( http_buf, "<tr><td>%d.%ds</td><td>", i/10,i%10 );
			tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			// RED
			int width = cs->ttime[i]*100/max;
			if (width>10)
				sprintf( http_buf, "<div class=redborder style='height:3px; width:%d%%'><span style=\"float: right;\">%d</span></div>", width, cs->ttime[i] );
			else {
				if (!width && cs->ttime[i])
					sprintf( http_buf, "<div class=redborder style='height:2px; width:1px;'></div>");
				else
					sprintf( http_buf, "<div class=redborder style='height:2px; width:%d%%'></div>", width );
			}
			tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			// GREEN
			width = cs->ttimecards[i]*100/max;
			if ( !width && cs->ttimecards[i] )
				sprintf( http_buf, "<div class=greenborder style='height:2px; width:1px;'></div>");
			else
				sprintf( http_buf, "<div class=greenborder style='height:2px; width:%d%%'></div>", width );
			tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			// CACHE - BLUE
			width = cs->ttimecache[i]*100/max;
			if ( !width && cs->ttimecache[i] )
				sprintf( http_buf, "<div class=cacheborder style='height:2px; width:1px;'></div>");
			else
				sprintf( http_buf, "<div class=cacheborder style='height:2px; width:%d%%'></div>", width );
			tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			// CACHEEX
			width = cs->ttimecacheex[i]*100/max;
			if ( !width && cs->ttimecacheex[i] )
				sprintf( http_buf, "<div class=cacheexborder style='height:2px; width:1px;'></div>");
			else
				sprintf( http_buf, "<div class=cacheexborder style='height:2px; width:%d%%'></div>", width );
			tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
#ifdef SRV_CSCACHE
			// YELLOW
			width = cs->ttimeclients[i]*100/max;
			if ( !width && cs->ttimeclients[i] )
				sprintf( http_buf, "<div class=clientsborder style='height:2px; width:1px;'></div>");
			else
				sprintf( http_buf, "<div class=clientsborder style='height:2px; width:%d%%'></div>", width );
			tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
#endif
			//
			sprintf( http_buf, "</td></tr>");
			tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		}
		sprintf( http_buf, "</table><br>\n"); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	}

	// Runtime SIDS
	if (cs->deniedsids[0].sid) {
		sprintf( http_buf, "<br><b>Available Servers</b>");
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		int maxcards = 0;
		for(i=0; i<1024; i++) {
			if (cs->deniedsids[i].sid) {
				if (cs->deniedsids[i].nbsrv>maxcards) maxcards = cs->deniedsids[i].nbsrv;
			}
			else break;
		}

		int icard = 0;
		while(icard<=maxcards) {
			sprintf( http_buf, "<br>%d cards: ",icard);
			tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			for(i=0; i<1024; i++)
				if (cs->deniedsids[i].sid) {
					if (cs->deniedsids[i].nbsrv==icard) {
						sprintf( http_buf, "%04x ", cs->deniedsids[i].sid);
						tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
					}
				}
				else break;
			icard++;
		}
	}

	if (cs->sidlist.data) {
		sprintf( http_buf, "<br><table> <tr><th>SID:CHID:ECMLEN.CW1CYCLE</th> <th>ECMNB</td> <th>ECMOK</th> </tr>");
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );

		int i;
		struct sid_chid_ecmlen_data *sids = cs->sidlist.data;
		for(i=0;i<MAX_SIDS;i++,sids++) {
			if (!sids->sid) break;
			sprintf( http_buf, "<tr> <td>%04x:%04x:%04x.%02x</td> <td>%d</td> <td>%d</td> </tr>", sids->sid, sids->chid, sids->ecmlen, sids->cw1cycle, sids->ecmnb, sids->ecmok );
			tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		}

		sprintf( http_buf, "</table>");
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );

		// show sid file
		if (cs->card.nbprov==1) {
			struct sid_chid_ecmlen_data *sids = cs->sidlist.data;
			for(i=0;i<MAX_SIDS;i++,sids++) {
				if (!sids->sid) break;
				struct chninfo_data *chn = getchninfo(cs->card.caid, cs->card.prov[0].id, sids->sid);
				if (!chn) continue;
				if (sids->cw1cycle)
					sprintf( http_buf, "%04x:%06x:%04x.%02x \"%s\"<br>", cs->card.caid, cs->card.prov[0].id, sids->sid, sids->cw1cycle, chn->name );
				else
					sprintf( http_buf, "%04x:%06x:%04x \"%s\"<br>", cs->card.caid, cs->card.prov[0].id, sids->sid, chn->name );
				tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			}
		}
	}

	for(i=0; i<cs->card.nbprov; i++) {
		if (cs->card.prov[i].sidlist.data) {
			sprintf( http_buf, "<br><br>SIDLIST FOR PROVIDER: %06x<br><table> <tr><th>SID:CHID:ECMLEN.CW1CYCLE</th> <th>ECMNB</td> <th>ECMOK</th> </tr>",cs->card.prov[i].id);
			tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			struct sid_chid_ecmlen_data *sids = cs->card.prov[i].sidlist.data;
			int j;
			for(j=0;j<MAX_SIDS;j++,sids++) {
				if (j>=cs->card.prov[i].sidlist.total) break;
				if (!sids->sid) break;
				sprintf( http_buf, "<tr> <td>%04x:%04x:%04x.%02x</td> <td>%d</td> <td>%d</td> </tr>", sids->sid, sids->chid, sids->ecmlen, sids->cw1cycle, sids->ecmnb, sids->ecmok );
				tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			}
			sprintf( http_buf, "</table>");
			tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );

			tcp_writestr(&tcpbuf, sock, "<pre style='width:80%;height:50%; border:1 solid #777'>");
			sids = cs->card.prov[i].sidlist.data;
			for(i=0;i<MAX_SIDS;i++,sids++) {
				if (!sids->sid) break;
				struct chninfo_data *chn = getchninfo(cs->card.caid, cs->card.prov[0].id, sids->sid);
				if (!chn) continue;
				if (sids->cw1cycle)
					sprintf( http_buf, "%04x:%06x:%04x.%02x \"%s\"\n", cs->card.caid, cs->card.prov[0].id, sids->sid, sids->cw1cycle, chn->name );
				else
					sprintf( http_buf, "%04x:%06x:%04x \"%s\"\n", cs->card.caid, cs->card.prov[0].id, sids->sid, chn->name );
				tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			}
			tcp_writestr(&tcpbuf, sock, "</pre>");
		}
	}

	tcp_flush(&tcpbuf, sock);
}


////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

#ifdef CCCAM_SRV

void cccam_clients( struct cccam_server_data *cccam, int *total, int *connected, int *active )
{
	*total = 0;
	*connected = 0;
	*active = 0;
	struct cc_client_data *cli = cccam->client;
	while (cli) {
		(*total)++;
		if (cli->connection.status>0) {
			(*connected)++;
			if ( (GetTickCount()-cli->lastecmtime) < 20000 ) (*active)++;
		}
		cli=cli->next;
	}
}

void total_cccam_clients( struct config_data *cfg, int *total, int *connected, int *active )
{
	*total = 0;
	*connected = 0;
	*active = 0;
	struct cccam_server_data *cccam = cfg->cccam.server;
	while (cccam) {
		struct cc_client_data *cli = cccam->client;
		while (cli) {
			(*total)++;
			if (cli->connection.status>0) {
				(*connected)++;
				if ( (GetTickCount()-cli->lastecmtime) < 20000 ) (*active)++;
			}
			cli=cli->next;
		}
		cccam = cccam->next;
	}
}

void getcccamcells(struct cc_client_data *cli, char cell[10][2048])
{
	char temp[2048];
	char et1[1024], et2[1024];	/* R35 text-field scratch */
	unsigned int ticks = GetTickCount();
	unsigned int d;
	// CELL0 # NAME
	if (cli->realname)
		sprintf( cell[0],"<a href='/cccamclient?id=%d'>%s<br>%s</a>",cli->id,escf(et1,cli->user),escf(et2,cli->realname));
	else
		sprintf( cell[0],"<a href='/cccamclient?id=%d'>%s</a>",cli->id,escf(et1,cli->user));
	// CELL1 # VERSION
	if (strlen(cli->version)) sprintf( cell[1],"CCcam %s<br>%02x%02x%02x%02x%02x%02x%02x%02x", escf(et1,cli->version), cli->nodeid[0],cli->nodeid[1],cli->nodeid[2],cli->nodeid[3],cli->nodeid[4],cli->nodeid[5],cli->nodeid[6],cli->nodeid[7]);
	else strcpy( cell[1]," " ); 
	// CELL2 # IP
	char *p = getcountrycodebyip(cli->ip);
	if (cli->host)
		if (p) sprintf( cell[2],"<img src='/flag_%s.gif' title='%s'> %s<br>%s", p, getcountryname(p), (char*)ip2string(cli->ip), escf(et1,cli->host->name) ); else sprintf( cell[2],"%s<br>%s",(char*)ip2string(cli->ip), escf(et1,cli->host->name) );
	else
		if (p) sprintf( cell[2],"<img src='/flag_%s.gif' title='%s'> %s", p, getcountryname(p), (char*)ip2string(cli->ip) ); else sprintf( cell[2],"%s",(char*)ip2string(cli->ip) );
	// CELL3 # Connection Time
	if (cli->connection.status>0) {
		if (cli->ecm.busy) sprintf( cell[9],"busy"); else sprintf( cell[9],"online");
		d = (ticks-cli->connection.time)/1000;
		sprintf( cell[3], "%02dd %02d:%02d:%02d", d/(3600*24), (d/3600)%24, (d/60)%60, d%60);
	}
	else {
		strcpy( cell[9], "offline" );
		if (cli->flags&FLAG_DELETE) sprintf( cell[3],"Removed");
		else if (cli->flags&FLAG_EXPIRED) sprintf( cell[3],"Expired");
		else if (cli->flags&FLAG_DISABLE) sprintf( cell[3],"Disabled");
		else sprintf( cell[3],"offline");
	}
#ifdef EXPIREDATE
	if (cli->enddate.tm_year) {
		sprintf( temp,"<br>Expire: %d-%02d-%02d", 1900+cli->enddate.tm_year, cli->enddate.tm_mon+1, cli->enddate.tm_mday);
		strcat( cell[3], temp );
	}
#endif
	sprintf( temp, "<table class=\"connect_data\"><tr><td>Successful Login: %d</td><td>Aborted Connections: %d</td><td>Total Zapping: %d</td><td>Channel Freeze: %d</td></tr></table>", cli->nblogin, cli->nbloginerror, cli->zap, cli->freeze );
	strcat( cell[3], temp );

	// CELL4+5+6 # ECM STAT: TOTAL/ACCEPTED/OK
	sprintf( cell[4], "%d", cli->ecmnb);
	int ecmaccepted = cli->ecmnb-cli->ecmdenied;
	getstatcell( ecmaccepted, cli->ecmnb, cell[5]);
	getstatcell( cli->ecmok, ecmaccepted, cell[6]);
	// CELL7 # Ecm Time
	if (cli->ecmok) sprintf( cell[7],"%d ms",(cli->ecmoktime/cli->ecmok) ); else sprintf( cell[7],"-- ms");
	// CELL8 # Last Used Share
	if ( cli->connection.status<=0 && cli->connection.lastseen) {
		d = (ticks-cli->connection.lastseen)/1000;
		sprintf( cell[8],"Last Seen %02dd %02d:%02d:%02d", d/(3600*24),(d/3600)%24,(d/60)%60,d%60);
	}
	else if ( cli->lastecm.caid ) {
		if (cli->lastecm.status)  strcpy( cell[8],"<span class=success"); else strcpy( cell[8],"<span class=failed");
		sprintf( temp," title='%04x:%06x:%04x'>ch %s (%dms) %s ",cli->lastecm.caid, cli->lastecm.prov, cli->lastecm.sid, getchname(cli->lastecm.caid, cli->lastecm.prov, cli->lastecm.sid) , cli->lastecm.decodetime, str_laststatus[cli->lastecm.status] );
		strcat( cell[8], temp );
		if ( (GetTickCount()-cli->ecm.recvtime) < 20000 ) {
			// From ???
			if (cli->lastecm.status) {
				strcat( cell[8], " / from ");
				src2string(cli->lastecm.dcwsrctype, cli->lastecm.dcwsrcid, temp);
				strcat( cell[8], temp);
			}
		}
		strcat( cell[8], "</span>" );
	}
	else strcpy( cell[8], " ");
	strcat( cell[8], "<span style='float:right;'>");
	if ( !(cli->flags&(FLAG_DELETE|FLAG_EXPIRED)) ) {
		if (cli->flags&FLAG_DISABLE) {
			sprintf( temp," <img title='Enable' src='enable.png' OnClick=\"imgrequest('/cccamclient?action=enable&id=%d',this);\">",cli->id);
			strcat( cell[8], temp );
		}
		else {
			sprintf( temp," <img title='disable' src='disable.png' OnClick=\"imgrequest('/cccamclient?action=disable&id=%d',this);\">",cli->id);
			strcat( cell[8], temp );
		}
	}
	sprintf( temp," <img title='Debug' src='debug.png' OnClick=\"imgrequest('/cccamclient?action=debug&id=%d',this);\">",cli->id);
	strcat( cell[8], temp );
	strcat( cell[8], "</span>");
}

int total_cccam_servers()
{
	int count=0;
	struct cccam_server_data *srv = cfg.cccam.server;
	while (srv) {
		count++;
		srv = srv->next;
	}
	return count;
}	


void http_send_cccam(int sock, http_request *req)
{
	char http_buf[2048];
	struct tcp_buffer_data tcpbuf;
	char cell[10][2048];

	// Get Params
	char *str_action = isset_get( req, "action");
	char *str_list = isset_get( req, "list");
	char *str_id = isset_get( req, "id"); // CCcam server ID
	char *str_clid = isset_get( req, "clid"); // Client ID
	char *str_clname = isset_get( req, "clname"); // Client NAME
	// Param 'action'
	int get_action;
	if (str_action) {
		if (!strcmp(str_action,"div")) get_action = ACTION_DIV;
		else if (!strcmp(str_action,"row")) get_action = ACTION_ROW;
		else if (!strcmp(str_action,"xml")) get_action = ACTION_XML; // Get Clients info in xml
		else if (!strcmp(str_action,"disable")) get_action = ACTION_DISABLE;
		else if (!strcmp(str_action,"enable")) get_action = ACTION_ENABLE;
		else if (!strcmp(str_action,"status")) get_action = ACTION_STATUS;
		else if (!strcmp(str_action,"debug")) get_action = ACTION_DEBUG;
		else str_action = NULL;
	}
	if (!str_action) { str_action = "page"; get_action = ACTION_PAGE; }

	/////////////////////////////////////////////

	if (get_action==ACTION_ROW) {
		// Check for XML ROW
		struct cc_client_data *cli = NULL;
		if (str_clid) {
			cli = getcccamclientbyid( atoi(str_clid) );
			if (!cli) return;
		}
		else {
			if (str_id && str_clname) {
				struct cccam_server_data *cccam = getcccamserverbyid( atoi(str_id) );
				if (!cccam) return;
				cli = getcccamclientbyname( cccam, str_clname );
				if (!cli) return;
			}
			else return;
		}
		// Send XML CELLS
		getcccamcells(cli,cell);
		int i; for(i=0; i<10; i++) xmlescape( cell[i] );
		char buf[5000] = "";
		sprintf( buf, "<cccam>\n<c0>%s</c0>\n<c1>%s</c1>\n<c2>%s</c2>\n<c3_c>%s</c3_c>\n<c3>%s</c3>\n<c4>%s</c4>\n<c5>%s</c5>\n<c6>%s</c6>\n<c7>%s</c7>\n<c8>%s</c8>\n</cccam>\n",cell[0],cell[1],cell[2],cell[9],cell[3],cell[4],cell[5],cell[6],cell[7],cell[8] );
		http_send_xml( sock, req, buf, strlen(buf));
		return;
	}			
	else if (get_action==ACTION_XML) {
		struct cccam_server_data *cccam = NULL;
		if (str_id) cccam = getcccamserverbyid( atoi(str_id) );
		tcp_init(&tcpbuf);
		tcp_writestr(&tcpbuf, sock, "HTTP/1.1 200 OK\r\nAccept-Ranges: bytes\r\nConnection: close\r\nContent-Type: application/xml\r\n\r\n");

		tcp_writestr(&tcpbuf, sock, "<multics>");

		struct cccam_server_data *srv;
		if (cccam) srv = cccam; else srv = cfg.cccam.server;
		while (srv) {
			tcp_writestr(&tcpbuf, sock, "\n<cccam>");
			sprintf(http_buf, "<id>%d</id>", srv->id); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			sprintf(http_buf, "<port>%d</port>", srv->port); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			sprintf(http_buf, "<status>%d</status>", (srv->handle>0) ); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			uint32_t ticks = GetTickCount();
			struct cc_client_data *cli = srv->client;
			while (cli) {
				tcp_writestr(&tcpbuf, sock, "<user>");
				{ char hesc[512]; html_esc(hesc,(int)sizeof(hesc),cli->user);
				sprintf(http_buf, "<name>%s</name>", hesc); }
				tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				if (cli->connection.status>0) {
					tcp_writestr(&tcpbuf, sock, "<status>1</status>");
					sprintf( http_buf,"<ip>%s</ip>", (char*)ip2string(cli->ip) ); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
					char *p = getcountrycodebyip(cli->ip);
					if (p) sprintf(http_buf, "<country>%s</country>", p); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
					uint32_t d = (ticks - cli->connection.time)/1000;
					sprintf(http_buf, "<connected>%02dd %02d:%02d:%02d</connected>", d/(3600*24), (d/3600)%24, (d/60)%60, d%60); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				}
				else {
					sprintf(http_buf, "<status>%d</status>",cli->flags&0x0E);
					tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				}
				tcp_writestr(&tcpbuf, sock, "</user>");
				cli = cli->next;
			}
			tcp_writestr(&tcpbuf, sock, "\n</cccam>");

			if (cccam) break; else srv = srv->next;
		}
		tcp_writestr(&tcpbuf, sock, "\n</multics>");
		tcp_flush(&tcpbuf, sock);
		return;
	}

	// Param 'id'
	int get_id = 0;
	if (str_id)	get_id = atoi(str_id);
	// Param 'list'
	int get_list = LIST_ACTIVE;
	if (str_list) {
		if (!strcmp(str_list,"connected")) get_list = LIST_CONNECTED;
		else if (!strcmp(str_list,"all")) get_list = LIST_ALL;
		else str_list = NULL;
	}
	if (!str_list) str_list = "active";
	//
	struct cccam_server_data *cccam = NULL;
	if (get_id) {
		cccam = getcccamserverbyid(get_id);
		if (!cccam) return;
	}

	tcp_init(&tcpbuf);
	http_replyok_write(req, &tcpbuf, sock);
	if (get_action==ACTION_PAGE) {
		http_html_write(req, &tcpbuf, sock);
		tcp_write(&tcpbuf, sock, http_head, strlen(http_head) );
		sprintf( http_buf, html_title, cfg.http.title, "CCcam"); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		tcp_write(&tcpbuf, sock, http_link, strlen(http_link) );
		http_style_write(&tcpbuf, sock);
		// JS
		tcp_writestr(&tcpbuf, sock, "\n<script type='text/javascript'>");
		// ACTIONS REQUEST
		tcp_writestr(&tcpbuf, sock, "\nfunction imgrequest( url, el )\n{\n	var httpRequest;\n	try { httpRequest = new XMLHttpRequest(); }\n	catch (trymicrosoft) { try { httpRequest = new ActiveXObject('Msxml2.XMLHTTP'); } catch (oldermicrosoft) { try { httpRequest = new ActiveXObject('Microsoft.XMLHTTP'); } catch(failed) { httpRequest = false; } } }\n	if (!httpRequest) { alert('Your browser does not support Ajax.'); return false; }\n	if ( typeof(el)!='undefined' ) {\n		el.onclick = null;\n		el.style.opacity = '0.7';\n		httpRequest.onreadystatechange = function()\n		{\n			if (httpRequest.readyState == 4) if (httpRequest.status == 200) el.style.opacity = '0.3';\n		}\n	}\n	httpRequest.open('GET', url, true);\n	httpRequest.send(null);\n}\n");
		// UPD ROW
		tcp_writestr(&tcpbuf, sock, "\nfunction xmlupdateRow( xmlDoc, id )\n{\n	var row = document.getElementById(id);\n	row.cells.item(0).innerHTML = xmlDoc.getElementsByTagName('c0')[0].childNodes[0].nodeValue;\n	row.cells.item(1).innerHTML = xmlDoc.getElementsByTagName('c1')[0].childNodes[0].nodeValue;\n	row.cells.item(2).innerHTML = xmlDoc.getElementsByTagName('c2')[0].childNodes[0].nodeValue;\n	row.cells.item(3).className = xmlDoc.getElementsByTagName('c3_c')[0].childNodes[0].nodeValue;\n	row.cells.item(3).innerHTML = xmlDoc.getElementsByTagName('c3')[0].childNodes[0].nodeValue;\n	row.cells.item(4).innerHTML = xmlDoc.getElementsByTagName('c4')[0].childNodes[0].nodeValue;\n	row.cells.item(5).innerHTML = xmlDoc.getElementsByTagName('c5')[0].childNodes[0].nodeValue;\n	row.cells.item(6).innerHTML = xmlDoc.getElementsByTagName('c6')[0].childNodes[0].nodeValue;\n	row.cells.item(7).innerHTML = xmlDoc.getElementsByTagName('c7')[0].childNodes[0].nodeValue;\n	row.cells.item(8).innerHTML = xmlDoc.getElementsByTagName('c8')[0].childNodes[0].nodeValue;\n}\n");
		char url[256];
		sprintf( url, "'/cccam?action=row&clid='+idx");
		sprintf( http_buf, HTTP_UPDATE_ROW, url);
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		// UPD DIV
		sprintf( url, "/cccam?action=div&id=%d&list=%s", get_id, str_list);
		sprintf( http_buf, HTTP_UPDATE_DIV, cfg.http.autorefresh*1000, url);
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		//
		tcp_writestr(&tcpbuf, sock, "\nfunction start()\n{\n	setautorefresh(autorefresh);\n}");
		tcp_writestr(&tcpbuf, sock, "\n</script>\n");
		tcp_write(&tcpbuf, sock, http_head_, strlen(http_head_) );
		tcp_writestr(&tcpbuf, sock, "<body onload=\"start();\">");
		tcp_write_menu(&tcpbuf, sock,PAGE_CCCAM);
		tcp_writestr(&tcpbuf, sock, "<table style=\"margin:0px; padding:0px; border-width:0px; border-spacing: 1px;\"><tr>");
		tcp_writestr(&tcpbuf, sock, "<td style=\"margin:0px; padding:0px;\"><a href='/cccam'><table border=1 width=200px>");
		// Total Servers
		sprintf( http_buf, "<tr><td>Total CCcam Servers: %d</td></tr>", total_cccam_servers() ); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		// Nodeid
		sprintf( http_buf,"<tr><td>NodeID = %02x%02x%02x%02x%02x%02x%02x%02x</td></tr>", cfg.cccam.nodeid[0], cfg.cccam.nodeid[1], cfg.cccam.nodeid[2], cfg.cccam.nodeid[3], cfg.cccam.nodeid[4], cfg.cccam.nodeid[5], cfg.cccam.nodeid[6], cfg.cccam.nodeid[7]);
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		// Version
		sprintf( http_buf, "<tr><td>Version = %s</td></tr>", cfg.cccam.version);
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		tcp_writestr(&tcpbuf, sock, "</table></a></td>");
		// Servers
		if (cfg.cccam.server) {
			int counter = 0;
			struct cccam_server_data *cccam = cfg.cccam.server;
			while ( cccam && (counter<5) ) {
				sprintf( http_buf, "<td style=\"margin:0px; padding: 0px;\"><a href='/cccam?id=%d'><table border=1 width=150px>", cccam->id);
				tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				if (cccam->handle>0) sprintf( http_buf, "<tr><td>CCcam %d [<span class=success>ENABLED</span>]</td></tr>", cccam->id);
				else sprintf( http_buf, "<tr><td>CCcam %d [<span class=failed>DISABLED</span>]</td></tr>", cccam->id);
				tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				sprintf( http_buf, "<tr><td>Port = %d</td></tr>", cccam->port);
				tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				int total, connected, active;
				cccam_clients( cccam, &total, &connected, &active );
				sprintf( http_buf, "<tr><td>Connected: %d / %d</td></tr>", connected, total);
				tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				tcp_writestr(&tcpbuf, sock, "</table></a></td>");
				counter++;
				cccam = cccam->next;
			}
		}
		// End of table
		tcp_writestr(&tcpbuf, sock, "</tr></table><br>");
		// DIV
		tcp_writestr(&tcpbuf, sock, "\n<div id='mainDiv'>");
	}

	tcp_writestr(&tcpbuf, sock, "<select style=\"width:200px;\" onchange=\"parent.location.href='/cccam?id='+this.value\">");
	sprintf( http_buf, "<option value=0>ALL (%d)</option>", total_cccam_servers());
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	struct cccam_server_data *tmp = cfg.cccam.server;
	while (tmp) {
		if (get_id==tmp->id) sprintf( http_buf, "<option value=%d selected>[%d] CCcam %d</option>",tmp->id,tmp->port, tmp->id );
		else sprintf( http_buf, "<option value=%d>[%d] CCcam %d</option>",tmp->id,tmp->port, tmp->id );
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		tmp = tmp->next;
	}
	tcp_writestr(&tcpbuf, sock, "</select> ");
	//
	int total, connected, active;
	if (cccam) cccam_clients( cccam, &total, &connected, &active ); else total_cccam_clients( &cfg, &total, &connected, &active );
	char *class1 = "button"; char *class2 = "sbutton";
	char *class;
	if (get_list==LIST_ACTIVE) class = class2; else class = class1;
	sprintf( http_buf, "<input type=button class=%s onclick=\"parent.location='/cccam?id=%d&amp;list=active'\" value='Active Clients (%d)'>", class, get_id, active);
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	if (get_list==LIST_CONNECTED) class = class2; else class = class1;
	sprintf( http_buf, " <input type=button class=%s onclick=\"parent.location='/cccam?id=%d&amp;list=connected'\" value='Connected Clients (%d)'>", class, get_id, connected);
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	if (get_list==LIST_ALL) class = class2; else class = class1;
	sprintf( http_buf, " <input type=button class=%s onclick=\"parent.location='/cccam?id=%d&amp;list=all'\" value='All Clients (%d)'>", class, get_id, total);
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	//
	if (get_id) { // One Server Selected
		// Table
		sprintf( http_buf, "\n<table class=maintable width=100%%><tr><th width=100px>Client</th><th width=70px>version</th><th width=120px>ip</th><th width=110px>Connected</th><th width=60px>TotalEcm</th><th width=90px>AcceptedEcm</th><th width=90px>EcmOK</th><th width=50px>EcmTime</th><th>Last used share</th></tr>");
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		struct cc_client_data *cli = cccam->client;
		int alt=0;
		if (get_list==LIST_ACTIVE) {
			while (cli) {
				if ( (cli->connection.status>0)&&((GetTickCount()-cli->lastecmtime) < 20000) ) {
					if (alt==1) alt=2; else alt=1;
					getcccamcells(cli,cell);
					/* R35: HTML rows render composed cells as-is; raw text fields are pre-escaped in the builders (escf/html_esc). Blanket escape here printed the page's own tags as text. */
					sprintf( http_buf,"\n<tr id=\"Row%d\" class=alt%d onMouseOver='setupdateRow(%d)' onMouseOut='setupdateRow(0)'> <td>%s</td><td>%s</td><td>%s</td><td class=\"%s\">%s</td><td align=center>%s</td><td>%s</td><td>%s</td><td align=center>%s</td><td>%s</td></tr>\n",cli->id,alt,cli->id,cell[0],cell[1],cell[2],cell[9],cell[3],cell[4],cell[5],cell[6],cell[7],cell[8]);
					tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				}
				cli = cli->next;
			}
		}
		else if (get_list==LIST_CONNECTED) {
			while (cli) {
				if (cli->connection.status>0) {
					if (alt==1) alt=2; else alt=1;
					getcccamcells(cli,cell);
					/* R35: HTML rows render composed cells as-is; raw text fields are pre-escaped in the builders (escf/html_esc). Blanket escape here printed the page's own tags as text. */
					sprintf( http_buf,"\n<tr id=\"Row%d\" class=alt%d onMouseOver='setupdateRow(%d)' onMouseOut='setupdateRow(0)'> <td>%s</td><td>%s</td><td>%s</td><td class=\"%s\">%s</td><td align=center>%s</td><td>%s</td><td>%s</td><td align=center>%s</td><td>%s</td></tr>\n",cli->id,alt,cli->id,cell[0],cell[1],cell[2],cell[9],cell[3],cell[4],cell[5],cell[6],cell[7],cell[8]);
					tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				}
				cli = cli->next;
			}
		}
		else { // ALL
			while (cli) {
				if (alt==1) alt=2; else alt=1;
				getcccamcells(cli,cell);
				/* R35: HTML rows render composed cells as-is; raw text fields are pre-escaped in the builders (escf/html_esc). Blanket escape here printed the page's own tags as text. */
				sprintf( http_buf,"\n<tr id=\"Row%d\" class=alt%d onMouseOver='setupdateRow(%d)' onMouseOut='setupdateRow(0)'> <td>%s</td><td>%s</td><td>%s</td><td class=\"%s\">%s</td><td align=center>%s</td><td>%s</td><td>%s</td><td align=center>%s</td><td>%s</td></tr>\n",cli->id,alt,cli->id,cell[0],cell[1],cell[2],cell[9],cell[3],cell[4],cell[5],cell[6],cell[7],cell[8]);
				tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				cli = cli->next;
			}
		}
		sprintf( http_buf, "\n</table>");
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	}

	else {
		// Table
		tcp_writestr(&tcpbuf,sock, "\n<table class=maintable width=100%>");
		tcp_writestr(&tcpbuf,sock, "\n<tr><th width=100px>Client</th><th width=70px>version</th><th width=120px>ip</th><th width=110px>Connected</th><th width=60px>TotalEcm</th><th width=90px>AcceptedEcm</th><th width=90px>EcmOK</th><th width=50px>EcmTime</th><th>Last used share</th></tr>");
		int alt=0;
		cccam = cfg.cccam.server;
		while (cccam) {
			int total, connected, active;
			cccam_clients( cccam, &total, &connected, &active );
			if ( (get_list==LIST_ACTIVE) && active ) {
				sprintf( http_buf,"\n<tr><td class=alt3 colspan=9> CCcam %d (%d)</td></tr>", cccam->id, active); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				struct cc_client_data *cli = cccam->client;
				while (cli) {
					if ( (cli->connection.status>0)&&((GetTickCount()-cli->lastecmtime) < 20000) ) {
						if (alt==1) alt=2; else alt=1;
						getcccamcells(cli,cell);
						/* R35: HTML rows render composed cells as-is; raw text fields are pre-escaped in the builders (escf/html_esc). Blanket escape here printed the page's own tags as text. */
						sprintf( http_buf,"\n<tr id=\"Row%d\" class=alt%d onMouseOver='setupdateRow(%d)' onMouseOut='setupdateRow(0)'> <td>%s</td><td>%s</td><td>%s</td><td class=\"%s\">%s</td><td align=center>%s</td><td>%s</td><td>%s</td><td align=center>%s</td><td>%s</td></tr>\n",cli->id,alt,cli->id,cell[0],cell[1],cell[2],cell[9],cell[3],cell[4],cell[5],cell[6],cell[7],cell[8]);
						tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
					}
					cli = cli->next;
				}
			}
			else if ( (get_list==LIST_ALL) && total ) {
				sprintf( http_buf,"\n<tr><td class=alt3 colspan=9> CCcam %d (%d)</td></tr>", cccam->id, total); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				struct cc_client_data *cli = cccam->client;
				while (cli) {
					if (alt==1) alt=2; else alt=1;
					getcccamcells(cli,cell);
					/* R35: HTML rows render composed cells as-is; raw text fields are pre-escaped in the builders (escf/html_esc). Blanket escape here printed the page's own tags as text. */
					sprintf( http_buf,"\n<tr id=\"Row%d\" class=alt%d onMouseOver='setupdateRow(%d)' onMouseOut='setupdateRow(0)'> <td>%s</td><td>%s</td><td>%s</td><td class=\"%s\">%s</td><td align=center>%s</td><td>%s</td><td>%s</td><td align=center>%s</td><td>%s</td></tr>\n",cli->id,alt,cli->id,cell[0],cell[1],cell[2],cell[9],cell[3],cell[4],cell[5],cell[6],cell[7],cell[8]);
					tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
					cli = cli->next;
				}
			}
			else if ( (get_list==LIST_CONNECTED) && connected ) {
				sprintf( http_buf,"\n<tr><td class=alt3 colspan=9> CCcam %d (%d)</td></tr>", cccam->id, connected); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				struct cc_client_data *cli = cccam->client;
				while (cli) {
					if (cli->connection.status>0) {
						if (alt==1) alt=2; else alt=1;
						getcccamcells(cli,cell);
						/* R35: HTML rows render composed cells as-is; raw text fields are pre-escaped in the builders (escf/html_esc). Blanket escape here printed the page's own tags as text. */
						sprintf( http_buf,"\n<tr id=\"Row%d\" class=alt%d onMouseOver='setupdateRow(%d)' onMouseOut='setupdateRow(0)'> <td>%s</td><td>%s</td><td>%s</td><td class=\"%s\">%s</td><td align=center>%s</td><td>%s</td><td>%s</td><td align=center>%s</td><td>%s</td></tr>\n",cli->id,alt,cli->id,cell[0],cell[1],cell[2],cell[9],cell[3],cell[4],cell[5],cell[6],cell[7],cell[8]);
						tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
					}
					cli = cli->next;
				}
			}
			cccam = cccam->next;
		}
		sprintf( http_buf, "</table>");
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	}
	if (get_action==ACTION_PAGE) {
		tcp_writestr(&tcpbuf, sock, "</div>");
		tcp_writestr(&tcpbuf, sock, "</body></html>");
	}

	tcp_flush(&tcpbuf, sock);
}



#ifdef CS378X_SRV

void getcs378xcells(struct camd35_client_data *cli, char cell[10][2048])
{
	char temp[2048];
	char et1[1024], et2[1024];	/* R35 text-field scratch */

	// CELL0 # NAME
	sprintf( cell[0],"<a href='/mgcamdclient?id=%d'>%s</a>",cli->id,escf(et1,cli->user));


	// CELL1 # UCRC
	sprintf( cell[1],"%08x", cli->ucrc);

	// CELL2 # IP
	if ( cli->ip ) { // Get Last IP
		char *p = getcountrycodebyip(cli->ip);
		if (p) sprintf( cell[2],"<img src='/flag_%s.gif' title='%s'> %s", p, getcountryname(p), (char*)ip2string(cli->ip) ); else sprintf( cell[2],"%s",(char*)ip2string(cli->ip) );
	}
	else strcpy( cell[2], " ");

	// CELL3 # Connection Time
	if (cli->connection.status>0) {
		if (cli->ecm.busy) sprintf( cell[9],"busy"); else sprintf( cell[9],"online");
		uint d = (GetTickCount()-cli->connection.time)/1000;
		sprintf( cell[3], "%02dd %02d:%02d:%02d", d/(3600*24), (d/3600)%24, (d/60)%60, d%60);
	}
	else {
		sprintf( cell[9],"offline");
		if (cli->flags&FLAG_DELETE) sprintf( cell[3],"Removed");
		else if (cli->flags&FLAG_EXPIRED) sprintf( cell[3],"Expired");
		else if (cli->flags&FLAG_DISABLE) sprintf( cell[3],"Disabled");
		else sprintf( cell[3],"offline");
	}
	// CELL4+5+6 # ECM STAT: TOTAL/ACCEPTED/OK
	// ECM STAT
	sprintf( cell[4], "%d", cli->ecmnb );

	int ecmaccepted = cli->ecmnb-cli->ecmdenied;
	getstatcell( ecmaccepted, cli->ecmnb, cell[5]);
	getstatcell( cli->ecmok, ecmaccepted, cell[6]);

	// CELL7 # Ecm Time
	if (cli->ecmok) sprintf( cell[7],"%d ms",(cli->ecmoktime/cli->ecmok) ); else sprintf( cell[7],"-- ms");

	// CELL8 # Last Used Share
/*
	if ( srv->connection.status<=0 && srv->connection.lastseen) {
		int d = (GetTickCount()-cli->connection.lastseen)/1000;
		sprintf( cell[8],"Last Seen %02dd %02d:%02d:%02d", d/(3600*24),(d/3600)%24,(d/60)%60,d%60);
	}
	else
*/
	if ( cli->lastecm.caid ) {
		if (cli->lastecm.status)  strcpy( cell[8],"<span class=success"); else strcpy( cell[8],"<span class=failed");
		sprintf( temp," title='%04x:%06x:%04x'>ch %s (%dms) %s ",cli->lastecm.caid, cli->lastecm.prov, cli->lastecm.sid, getchname(cli->lastecm.caid, cli->lastecm.prov, cli->lastecm.sid) , cli->lastecm.decodetime, str_laststatus[cli->lastecm.status] );
		strcat( cell[8], temp );
		if ( (GetTickCount()-cli->ecm.recvtime) < 20000 ) {
			// From ???
			if (cli->lastecm.status) {
				strcat( cell[8], " / from ");
				src2string(cli->lastecm.dcwsrctype, cli->lastecm.dcwsrcid, temp);
				strcat( cell[8], temp);
			}
		}
		strcat( cell[8], "</span>" );
	}
	else strcpy( cell[8], " ");

	strcat( cell[8], "<span style='float:right;'>");
	if ( !(cli->flags&(FLAG_DELETE|FLAG_EXPIRED)) ) {
		if (cli->flags&FLAG_DISABLE) {
			sprintf( temp," <img title='Enable' src='enable.png' OnClick=\"imgrequest('/mgcamdclient?id=%d&action=enable',this);\">",cli->id);
			strcat( cell[8], temp );
		}
		else {
			sprintf( temp," <img title='disable' src='disable.png' OnClick=\"imgrequest('/mgcamdclient?id=%d&action=disable',this);\">",cli->id);
			strcat( cell[8], temp );
		}
	}
	sprintf( temp," <img title='Debug' src='debug.png' OnClick=\"imgrequest('/mgcamdclient?id=%d&action=debug',this);\">",cli->id);
	strcat( cell[8], temp );
	strcat( cell[8], "</span>");
}

void total_cs378x_clients( int *total, int *connected, int *active )
{
	*total = 0;
	*connected = 0;
	*active = 0;
	struct camd35_server_data *cs378x = cfg.cs378x.server;
	while (cs378x) {
		struct camd35_client_data *cli = cs378x->client;
		while (cli) {
			(*total)++;
			if (cli->connection.status>0) {
				(*connected)++;
				if ( (GetTickCount()-cli->lastecmtime) < 20000 ) (*active)++;
			}
			cli=cli->next;
		}
		cs378x = cs378x->next;
	}
}

void cs378x_clients( struct camd35_server_data *cs378x, int *total, int *connected, int *active )
{
	*total = 0;
	*connected = 0;
	*active = 0;
	struct camd35_client_data *cli = cs378x->client;
	while (cli) {
		(*total)++;
		if (cli->connection.status>0) {
			(*connected)++;
			if ( (GetTickCount()-cli->lastecmtime) < 20000 ) (*active)++;
		}
		cli=cli->next;
	}
}

void http_send_cs378x(int sock, http_request *req)
{
	char http_buf[4096];
	struct tcp_buffer_data tcpbuf;
	char cell[10][2048];

	// Get Params
	char *str_action = isset_get( req, "action");
	char *str_list = isset_get( req, "list");
	char *str_id = isset_get( req, "id"); // server ID
	char *str_clid = isset_get( req, "clid"); // Client ID
	// Param 'action'
	int get_action;
	if (str_action) {
		if (!strcmp(str_action,"div")) get_action = ACTION_DIV;
		else if (!strcmp(str_action,"row")) get_action = ACTION_ROW;
		else if (!strcmp(str_action,"xml")) get_action = ACTION_XML; // Get Clients info in xml
		else if (!strcmp(str_action,"disable")) get_action = ACTION_DISABLE;
		else if (!strcmp(str_action,"enable")) get_action = ACTION_ENABLE;
		else if (!strcmp(str_action,"status")) get_action = ACTION_STATUS;
		else if (!strcmp(str_action,"debug")) get_action = ACTION_DEBUG;
		else str_action = NULL;
	}
	if (!str_action) { str_action = "page"; get_action = ACTION_PAGE; }
	/////////////////////////////////////////////
	if (get_action==ACTION_ROW) {
		// Check for XML ROW
		if (str_clid) {
			int id = atoi(str_clid);
			struct camd35_server_data *cs378x = cfg.cs378x.server;
			while (cs378x) {
				if (!(cs378x->flags&FLAG_DELETE)) {
					struct camd35_client_data *cli = cs378x->client;
					while (cli) {
						if ( !(cli->flags&FLAG_DELETE) && (cli->id==id) ) {
							// Send XML CELLS
							getcs378xcells(cli,cell);
							int i; for(i=0; i<10; i++) xmlescape( cell[i] );
							sprintf( http_buf, "<cs378x>\n<c0>%s</c0>\n<c1>%s</c1>\n<c2>%s</c2>\n<c3_c>%s</c3_c>\n<c3>%s</c3>\n<c4>%s</c4>\n<c5>%s</c5>\n<c6>%s</c6>\n<c7>%s</c7>\n<c8>%s</c8>\n</cs378x>\n",cell[0],cell[1],cell[2],cell[9],cell[3],cell[4],cell[5],cell[6],cell[7],cell[8] );
							http_send_xml( sock, req, http_buf, strlen(http_buf));
						}
						cli = cli->next;
					}
				}
				cs378x = cs378x->next;
			}
		}
		return;
	}			

	// Param 'list'
	int get_list = LIST_ACTIVE;
	if (str_list) {
		if (!strcmp(str_list,"connected")) get_list = LIST_CONNECTED;
		else if (!strcmp(str_list,"all")) get_list = LIST_ALL;
		else str_list = NULL;
	}
	if (!str_list) str_list = "active";
	// Param 'id'
	int get_id = 0;
	struct camd35_server_data *cs378x = NULL;
	if (str_id)	{
		get_id = atoi(str_id);
		cs378x = cfg.cs378x.server;
		while (cs378x) {
			if (cs378x->id == get_id) break;
			cs378x = cs378x->next;
		}
		if (!cs378x) get_id = 0;
	}
	//
	tcp_init(&tcpbuf);
	if (get_action==ACTION_PAGE) {
		http_replyok_write(req, &tcpbuf, sock);
		http_html_write(req, &tcpbuf, sock);
		tcp_write(&tcpbuf, sock, http_head, strlen(http_head) );
		sprintf( http_buf, html_title, cfg.http.title, "cs378x"); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		tcp_write(&tcpbuf, sock, http_link, strlen(http_link) );
		http_style_write(&tcpbuf, sock);
		// JS
		tcp_writestr(&tcpbuf, sock, "\n<script type='text/javascript'>");
		// ACTIONS REQUEST
		tcp_writestr(&tcpbuf, sock, "\nfunction imgrequest( url, el )\n{\n	var httpRequest;\n	try { httpRequest = new XMLHttpRequest(); }\n	catch (trymicrosoft) { try { httpRequest = new ActiveXObject('Msxml2.XMLHTTP'); } catch (oldermicrosoft) { try { httpRequest = new ActiveXObject('Microsoft.XMLHTTP'); } catch(failed) { httpRequest = false; } } }\n	if (!httpRequest) { alert('Your browser does not support Ajax.'); return false; }\n	if ( typeof(el)!='undefined' ) {\n		el.onclick = null;\n		el.style.opacity = '0.7';\n		httpRequest.onreadystatechange = function()\n		{\n			if (httpRequest.readyState == 4) if (httpRequest.status == 200) el.style.opacity = '0.3';\n		}\n	}\n	httpRequest.open('GET', url, true);\n	httpRequest.send(null);\n}\n");
		// UPD ROW
		tcp_writestr(&tcpbuf, sock, "\nfunction xmlupdateRow( xmlDoc, id )\n{\n	var row = document.getElementById(id);\n	row.cells.item(0).innerHTML = xmlDoc.getElementsByTagName('c0')[0].childNodes[0].nodeValue;\n	row.cells.item(1).innerHTML = xmlDoc.getElementsByTagName('c1')[0].childNodes[0].nodeValue;\n	row.cells.item(2).innerHTML = xmlDoc.getElementsByTagName('c2')[0].childNodes[0].nodeValue;\n	row.cells.item(3).className = xmlDoc.getElementsByTagName('c3_c')[0].childNodes[0].nodeValue;\n	row.cells.item(3).innerHTML = xmlDoc.getElementsByTagName('c3')[0].childNodes[0].nodeValue;\n	row.cells.item(4).innerHTML = xmlDoc.getElementsByTagName('c4')[0].childNodes[0].nodeValue;\n	row.cells.item(5).innerHTML = xmlDoc.getElementsByTagName('c5')[0].childNodes[0].nodeValue;\n	row.cells.item(6).innerHTML = xmlDoc.getElementsByTagName('c6')[0].childNodes[0].nodeValue;\n	row.cells.item(7).innerHTML = xmlDoc.getElementsByTagName('c7')[0].childNodes[0].nodeValue;\n	row.cells.item(8).innerHTML = xmlDoc.getElementsByTagName('c8')[0].childNodes[0].nodeValue;\n}\n");
		char url[256];
		sprintf( url, "'/cs378x?action=row&clid='+idx");
		sprintf( http_buf, HTTP_UPDATE_ROW, url);
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		// UPD DIV
		sprintf( url, "/cs378x?action=div&id=%d&list=%s", get_id, str_list);
		sprintf( http_buf, HTTP_UPDATE_DIV, cfg.http.autorefresh*1000, url);
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		//
		tcp_writestr(&tcpbuf, sock, "\nfunction start()\n{\n	setautorefresh(autorefresh);\n}");
		tcp_writestr(&tcpbuf, sock, "\n</script>\n");
		tcp_write(&tcpbuf, sock, http_head_, strlen(http_head_) );
		tcp_writestr(&tcpbuf, sock, "<body onload=\"start();\">");
		tcp_write_menu(&tcpbuf, sock,PAGE_CS378X);
		// DIV
		tcp_writestr(&tcpbuf, sock, "<div id='mainDiv'>");
	}

	tcp_writestr(&tcpbuf, sock, "<table style=\"margin:0px; padding:0px; border-width:0px; border-spacing: 1px;\"><tr>");
	tcp_writestr(&tcpbuf, sock, "<td style=\"margin:0px; padding:0px;\"><a href='/cs378x'><table border=1 width=200px>");
	// Total Servers
	sprintf( http_buf, "<tr><td>Total cs378x Servers: %d</td></tr>", cfg.cs378x.totalservers); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	int total, connected, active;
	total_cs378x_clients( &total, &connected, &active );
	sprintf( http_buf, "<tr><td>Connected Clients: %d / %d</td></tr>", connected, total ); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	tcp_writestr(&tcpbuf, sock, "</table></a></td>");
	// Servers
	if (cfg.cs378x.server) {
		int counter = 0;
		struct camd35_server_data *cs378x = cfg.cs378x.server;
		while ( cs378x && (counter<5) ) {
			sprintf( http_buf, "<td style=\"margin:0px; padding: 0px;\"><a href='/cs378x?id=%d'><table border=1 width=150px>", cs378x->id);
			tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			if (cs378x->handle>0) sprintf( http_buf, "<tr><td>cs378x %d [<span class=success>%d</span>]</td></tr>", cs378x->id, cs378x->port);
			else sprintf( http_buf, "<tr><td>cs378x %d [<span class=failed>%d</span>]</td></tr>", cs378x->id, cs378x->port);
			tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			int total, connected, active;
			cs378x_clients( cs378x, &total, &connected, &active );
			sprintf( http_buf, "<tr><td>Connected: %d / %d</td></tr>", connected, total);
			tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			tcp_writestr(&tcpbuf, sock, "</table></a></td>");
			counter++;
			cs378x = cs378x->next;
		}
	}
	// End of table
	tcp_writestr(&tcpbuf, sock, "</tr></table><br>");

	tcp_writestr(&tcpbuf, sock, "<select style=\"width:200px;\" onchange=\"parent.location.href='/cs378x?id='+this.value\">");
	sprintf( http_buf, "<option value=0>ALL (%d)</option>", cfg.cs378x.totalservers);
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	struct camd35_server_data *tmp = cfg.cs378x.server;
	while (tmp) {
		if (get_id==tmp->id) sprintf( http_buf, "<option value=%d selected>[%d] cs378x %d</option>",tmp->id,tmp->port, tmp->id );
		else sprintf( http_buf, "<option value=%d>[%d] cs378x %d</option>",tmp->id,tmp->port, tmp->id );
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		tmp = tmp->next;
	}
	tcp_writestr(&tcpbuf, sock, "</select> ");
	//
	if (cs378x) cs378x_clients( cs378x, &total, &connected, &active ); else total_cs378x_clients( &total, &connected, &active );
	char *class1 = "button"; char *class2 = "sbutton";
	char *class;
	if (get_list==LIST_ACTIVE) class = class2; else class = class1;
	sprintf( http_buf, "<input type=button class=%s onclick=\"parent.location='/cs378x?id=%d&amp;list=active'\" value='Active Clients (%d)'>", class, get_id, active);
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	if (get_list==LIST_CONNECTED) class = class2; else class = class1;
	sprintf( http_buf, " <input type=button class=%s onclick=\"parent.location='/cs378x?id=%d&amp;list=connected'\" value='Connected Clients (%d)'>", class, get_id, connected);
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	if (get_list==LIST_ALL) class = class2; else class = class1;
	sprintf( http_buf, " <input type=button class=%s onclick=\"parent.location='/cs378x?id=%d&amp;list=all'\" value='All Clients (%d)'>", class, get_id, total);
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	//
	if (get_id) { // One Server Selected
		// Table
		sprintf( http_buf, "\n<table class=maintable width=100%%><tr><th width=100px>Client</th><th width=70px>ucrc</th><th width=120px>ip</th><th width=110px>Connected</th><th width=60px>TotalEcm</th><th width=90px>AcceptedEcm</th><th width=90px>EcmOK</th><th width=50px>EcmTime</th><th>Last used share</th></tr>");
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		struct camd35_client_data *cli = cs378x->client;
		int alt=0;
		if (get_list==LIST_ACTIVE) {
			while (cli) {
				if ( (cli->connection.status>0)&&((GetTickCount()-cli->lastecmtime) < 20000) ) {
					if (alt==1) alt=2; else alt=1;
					getcs378xcells(cli,cell);
					/* R35: HTML rows render composed cells as-is; raw text fields are pre-escaped in the builders (escf/html_esc). Blanket escape here printed the page's own tags as text. */
					sprintf( http_buf,"\n<tr id=\"Row%d\" class=alt%d onMouseOver='setupdateRow(%d)' onMouseOut='setupdateRow(0)'> <td>%s</td><td>%s</td><td>%s</td><td class=\"%s\">%s</td><td align=center>%s</td><td>%s</td><td>%s</td><td align=center>%s</td><td>%s</td></tr>\n",cli->id,alt,cli->id,cell[0],cell[1],cell[2],cell[9],cell[3],cell[4],cell[5],cell[6],cell[7],cell[8]);
					tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				}
				cli = cli->next;
			}
		}
		else if (get_list==LIST_CONNECTED) {
			while (cli) {
				if (cli->connection.status>0) {
					if (alt==1) alt=2; else alt=1;
					getcs378xcells(cli,cell);
					/* R35: HTML rows render composed cells as-is; raw text fields are pre-escaped in the builders (escf/html_esc). Blanket escape here printed the page's own tags as text. */
					sprintf( http_buf,"\n<tr id=\"Row%d\" class=alt%d onMouseOver='setupdateRow(%d)' onMouseOut='setupdateRow(0)'> <td>%s</td><td>%s</td><td>%s</td><td class=\"%s\">%s</td><td align=center>%s</td><td>%s</td><td>%s</td><td align=center>%s</td><td>%s</td></tr>\n",cli->id,alt,cli->id,cell[0],cell[1],cell[2],cell[9],cell[3],cell[4],cell[5],cell[6],cell[7],cell[8]);
					tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				}
				cli = cli->next;
			}
		}
		else { // ALL
			while (cli) {
				if (alt==1) alt=2; else alt=1;
				getcs378xcells(cli,cell);
				/* R35: HTML rows render composed cells as-is; raw text fields are pre-escaped in the builders (escf/html_esc). Blanket escape here printed the page's own tags as text. */
				sprintf( http_buf,"\n<tr id=\"Row%d\" class=alt%d onMouseOver='setupdateRow(%d)' onMouseOut='setupdateRow(0)'> <td>%s</td><td>%s</td><td>%s</td><td class=\"%s\">%s</td><td align=center>%s</td><td>%s</td><td>%s</td><td align=center>%s</td><td>%s</td></tr>\n",cli->id,alt,cli->id,cell[0],cell[1],cell[2],cell[9],cell[3],cell[4],cell[5],cell[6],cell[7],cell[8]);
				tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				cli = cli->next;
			}
		}
		sprintf( http_buf, "\n</table>");
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	}

	else {
		// Table
		tcp_writestr(&tcpbuf,sock, "\n<table class=maintable width=100%>");
		tcp_writestr(&tcpbuf,sock, "\n<tr><th width=100px>Client</th><th width=70px>version</th><th width=120px>ip</th><th width=110px>Connected</th><th width=60px>TotalEcm</th><th width=90px>AcceptedEcm</th><th width=90px>EcmOK</th><th width=50px>EcmTime</th><th>Last used share</th></tr>");
		int alt=0;
		cs378x = cfg.cs378x.server;
		while (cs378x) {
			int total, connected, active;
			cs378x_clients( cs378x, &total, &connected, &active );
			if ( (get_list==LIST_ACTIVE) && active ) {
				sprintf( http_buf,"\n<tr><td class=alt3 colspan=9> cs378x %d (%d)</td></tr>", cs378x->id, active); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				struct camd35_client_data *cli = cs378x->client;
				while (cli) {
					if ( (cli->connection.status>0)&&((GetTickCount()-cli->lastecmtime) < 20000) ) {
						if (alt==1) alt=2; else alt=1;
						getcs378xcells(cli,cell);
						/* R35: HTML rows render composed cells as-is; raw text fields are pre-escaped in the builders (escf/html_esc). Blanket escape here printed the page's own tags as text. */
						sprintf( http_buf,"\n<tr id=\"Row%d\" class=alt%d onMouseOver='setupdateRow(%d)' onMouseOut='setupdateRow(0)'> <td>%s</td><td>%s</td><td>%s</td><td class=\"%s\">%s</td><td align=center>%s</td><td>%s</td><td>%s</td><td align=center>%s</td><td>%s</td></tr>\n",cli->id,alt,cli->id,cell[0],cell[1],cell[2],cell[9],cell[3],cell[4],cell[5],cell[6],cell[7],cell[8]);
						tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
					}
					cli = cli->next;
				}
			}
			else if ( (get_list==LIST_ALL) && total ) {
				sprintf( http_buf,"\n<tr><td class=alt3 colspan=9> cs378x %d (%d)</td></tr>", cs378x->id, total); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				struct camd35_client_data *cli = cs378x->client;
				while (cli) {
					if (alt==1) alt=2; else alt=1;
					getcs378xcells(cli,cell);
					/* R35: HTML rows render composed cells as-is; raw text fields are pre-escaped in the builders (escf/html_esc). Blanket escape here printed the page's own tags as text. */
					sprintf( http_buf,"\n<tr id=\"Row%d\" class=alt%d onMouseOver='setupdateRow(%d)' onMouseOut='setupdateRow(0)'> <td>%s</td><td>%s</td><td>%s</td><td class=\"%s\">%s</td><td align=center>%s</td><td>%s</td><td>%s</td><td align=center>%s</td><td>%s</td></tr>\n",cli->id,alt,cli->id,cell[0],cell[1],cell[2],cell[9],cell[3],cell[4],cell[5],cell[6],cell[7],cell[8]);
					tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
					cli = cli->next;
				}
			}
			else if ( (get_list==LIST_CONNECTED) && connected ) {
				sprintf( http_buf,"\n<tr><td class=alt3 colspan=9> cs378x %d (%d)</td></tr>", cs378x->id, connected); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				struct camd35_client_data *cli = cs378x->client;
				while (cli) {
					if (cli->connection.status>0) {
						if (alt==1) alt=2; else alt=1;
						getcs378xcells(cli,cell);
						/* R35: HTML rows render composed cells as-is; raw text fields are pre-escaped in the builders (escf/html_esc). Blanket escape here printed the page's own tags as text. */
						sprintf( http_buf,"\n<tr id=\"Row%d\" class=alt%d onMouseOver='setupdateRow(%d)' onMouseOut='setupdateRow(0)'> <td>%s</td><td>%s</td><td>%s</td><td class=\"%s\">%s</td><td align=center>%s</td><td>%s</td><td>%s</td><td align=center>%s</td><td>%s</td></tr>\n",cli->id,alt,cli->id,cell[0],cell[1],cell[2],cell[9],cell[3],cell[4],cell[5],cell[6],cell[7],cell[8]);
						tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
					}
					cli = cli->next;
				}
			}
			cs378x = cs378x->next;
		}
		sprintf( http_buf, "</table>");
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	}
	if (get_action==ACTION_PAGE) {
		tcp_writestr(&tcpbuf, sock, "</div>");
		tcp_writestr(&tcpbuf, sock, "</body></html>");
	}

	tcp_flush(&tcpbuf, sock);
}

#endif

















#ifdef CAMD35_SRV

void getcamd35cells(struct camd35_client_data *cli, char cell[10][2048])
{
	char temp[2048];
	char et1[1024], et2[1024];	/* R35 text-field scratch */

	// CELL0 # NAME
	sprintf( cell[0],"<a href='/mgcamdclient?id=%d'>%s</a>",cli->id,escf(et1,cli->user));


	// CELL1 # UCRC
	sprintf( cell[1],"%08x", cli->ucrc);

	// CELL2 # IP
	if ( cli->ip ) { // Get Last IP
		char *p = getcountrycodebyip(cli->ip);
		if (p) sprintf( cell[2],"<img src='/flag_%s.gif' title='%s'> %s", p, getcountryname(p), (char*)ip2string(cli->ip) ); else sprintf( cell[2],"%s",(char*)ip2string(cli->ip) );
	}
	else strcpy( cell[2], " ");

	// CELL3 # Connection Time
	if (cli->connection.status>0) {
		if (cli->ecm.busy) sprintf( cell[9],"busy"); else sprintf( cell[9],"online");
		uint d = (GetTickCount()-cli->connection.time)/1000;
		sprintf( cell[3], "%02dd %02d:%02d:%02d", d/(3600*24), (d/3600)%24, (d/60)%60, d%60);
	}
	else {
		sprintf( cell[9],"offline");
		if (cli->flags&FLAG_DELETE) sprintf( cell[3],"Removed");
		else if (cli->flags&FLAG_EXPIRED) sprintf( cell[3],"Expired");
		else if (cli->flags&FLAG_DISABLE) sprintf( cell[3],"Disabled");
		else sprintf( cell[3],"offline");
	}
	// CELL4+5+6 # ECM STAT: TOTAL/ACCEPTED/OK
	// ECM STAT
	sprintf( cell[4], "%d", cli->ecmnb );

	int ecmaccepted = cli->ecmnb-cli->ecmdenied;
	getstatcell( ecmaccepted, cli->ecmnb, cell[5]);
	getstatcell( cli->ecmok, ecmaccepted, cell[6]);

	// CELL7 # Ecm Time
	if (cli->ecmok) sprintf( cell[7],"%d ms",(cli->ecmoktime/cli->ecmok) ); else sprintf( cell[7],"-- ms");

	// CELL8 # Last Used Share
/*
	if ( srv->connection.status<=0 && srv->connection.lastseen) {
		int d = (GetTickCount()-cli->connection.lastseen)/1000;
		sprintf( cell[8],"Last Seen %02dd %02d:%02d:%02d", d/(3600*24),(d/3600)%24,(d/60)%60,d%60);
	}
	else
*/
	if ( cli->lastecm.caid ) {
		if (cli->lastecm.status)  strcpy( cell[8],"<span class=success"); else strcpy( cell[8],"<span class=failed");
		sprintf( temp," title='%04x:%06x:%04x'>ch %s (%dms) %s ",cli->lastecm.caid, cli->lastecm.prov, cli->lastecm.sid, getchname(cli->lastecm.caid, cli->lastecm.prov, cli->lastecm.sid) , cli->lastecm.decodetime, str_laststatus[cli->lastecm.status] );
		strcat( cell[8], temp );
		if ( (GetTickCount()-cli->ecm.recvtime) < 20000 ) {
			// From ???
			if (cli->lastecm.status) {
				strcat( cell[8], " / from ");
				src2string(cli->lastecm.dcwsrctype, cli->lastecm.dcwsrcid, temp);
				strcat( cell[8], temp);
			}
		}
		strcat( cell[8], "</span>" );
	}
	else strcpy( cell[8], " ");

	strcat( cell[8], "<span style='float:right;'>");
	if ( !(cli->flags&(FLAG_DELETE|FLAG_EXPIRED)) ) {
		if (cli->flags&FLAG_DISABLE) {
			sprintf( temp," <img title='Enable' src='enable.png' OnClick=\"imgrequest('/mgcamdclient?id=%d&action=enable',this);\">",cli->id);
			strcat( cell[8], temp );
		}
		else {
			sprintf( temp," <img title='disable' src='disable.png' OnClick=\"imgrequest('/mgcamdclient?id=%d&action=disable',this);\">",cli->id);
			strcat( cell[8], temp );
		}
	}
	sprintf( temp," <img title='Debug' src='debug.png' OnClick=\"imgrequest('/mgcamdclient?id=%d&action=debug',this);\">",cli->id);
	strcat( cell[8], temp );
	strcat( cell[8], "</span>");
}

void total_camd35_clients( int *total, int *connected, int *active )
{
	*total = 0;
	*connected = 0;
	*active = 0;
	struct camd35_server_data *camd35 = cfg.camd35.server;
	while (camd35) {
		struct camd35_client_data *cli = camd35->client;
		while (cli) {
			(*total)++;
			if (cli->connection.status>0) {
				(*connected)++;
				if ( (GetTickCount()-cli->lastecmtime) < 20000 ) (*active)++;
			}
			cli=cli->next;
		}
		camd35 = camd35->next;
	}
}

void camd35_clients( struct camd35_server_data *camd35, int *total, int *connected, int *active )
{
	*total = 0;
	*connected = 0;
	*active = 0;
	struct camd35_client_data *cli = camd35->client;
	while (cli) {
		(*total)++;
		if (cli->connection.status>0) {
			(*connected)++;
			if ( (GetTickCount()-cli->lastecmtime) < 20000 ) (*active)++;
		}
		cli=cli->next;
	}
}

void http_send_camd35(int sock, http_request *req)
{
	char http_buf[4096];
	struct tcp_buffer_data tcpbuf;
	char cell[10][2048];

	// Get Params
	char *str_action = isset_get( req, "action");
	char *str_list = isset_get( req, "list");
	char *str_id = isset_get( req, "id"); // server ID
	char *str_clid = isset_get( req, "clid"); // Client ID
	// Param 'action'
	int get_action;
	if (str_action) {
		if (!strcmp(str_action,"div")) get_action = ACTION_DIV;
		else if (!strcmp(str_action,"row")) get_action = ACTION_ROW;
		else if (!strcmp(str_action,"xml")) get_action = ACTION_XML; // Get Clients info in xml
		else if (!strcmp(str_action,"disable")) get_action = ACTION_DISABLE;
		else if (!strcmp(str_action,"enable")) get_action = ACTION_ENABLE;
		else if (!strcmp(str_action,"status")) get_action = ACTION_STATUS;
		else if (!strcmp(str_action,"debug")) get_action = ACTION_DEBUG;
		else str_action = NULL;
	}
	if (!str_action) { str_action = "page"; get_action = ACTION_PAGE; }
	/////////////////////////////////////////////

	if (get_action==ACTION_ROW) {
		// Check for XML ROW
		struct camd35_client_data *cli = NULL;
		if (str_clid) {
			int id = atoi(str_clid);
			struct camd35_server_data *camd35 = cfg.camd35.server;
			while (camd35) {
				if (!(camd35->flags&FLAG_DELETE)) {
					struct camd35_client_data *cli = camd35->client;
					while (cli) {
						if ( !(cli->flags&FLAG_DELETE) && (cli->id==id) ) {
							// Send XML CELLS
							getcamd35cells(cli,cell);
							int i; for(i=0; i<10; i++) xmlescape( cell[i] );
							sprintf( http_buf, "<camd35>\n<c0>%s</c0>\n<c1>%s</c1>\n<c2>%s</c2>\n<c3_c>%s</c3_c>\n<c3>%s</c3>\n<c4>%s</c4>\n<c5>%s</c5>\n<c6>%s</c6>\n<c7>%s</c7>\n<c8>%s</c8>\n</camd35>\n",cell[0],cell[1],cell[2],cell[9],cell[3],cell[4],cell[5],cell[6],cell[7],cell[8] );
							http_send_xml( sock, req, http_buf, strlen(http_buf));
						}
						cli = cli->next;
					}
				}
				camd35 = camd35->next;
			}
		}
		return;
	}			

	// Param 'list'
	int get_list = LIST_ACTIVE;
	if (str_list) {
		if (!strcmp(str_list,"connected")) get_list = LIST_CONNECTED;
		else if (!strcmp(str_list,"all")) get_list = LIST_ALL;
		else str_list = NULL;
	}
	if (!str_list) str_list = "active";
	// Param 'id'
	int get_id = 0;
	struct camd35_server_data *camd35 = NULL;
	if (str_id)	{
		get_id = atoi(str_id);
		camd35 = cfg.camd35.server;
		while (camd35) {
			if (camd35->id == get_id) break;
			camd35 = camd35->next;
		}
		if (!camd35) get_id = 0;
	}
	//
	tcp_init(&tcpbuf);
	if (get_action==ACTION_PAGE) {
		http_replyok_write(req, &tcpbuf, sock);
		http_html_write(req, &tcpbuf, sock);
		tcp_write(&tcpbuf, sock, http_head, strlen(http_head) );
		sprintf( http_buf, html_title, cfg.http.title, "camd35"); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		tcp_write(&tcpbuf, sock, http_link, strlen(http_link) );
		http_style_write(&tcpbuf, sock);
		// JS
		tcp_writestr(&tcpbuf, sock, "\n<script type='text/javascript'>");
		// ACTIONS REQUEST
		tcp_writestr(&tcpbuf, sock, "\nfunction imgrequest( url, el )\n{\n	var httpRequest;\n	try { httpRequest = new XMLHttpRequest(); }\n	catch (trymicrosoft) { try { httpRequest = new ActiveXObject('Msxml2.XMLHTTP'); } catch (oldermicrosoft) { try { httpRequest = new ActiveXObject('Microsoft.XMLHTTP'); } catch(failed) { httpRequest = false; } } }\n	if (!httpRequest) { alert('Your browser does not support Ajax.'); return false; }\n	if ( typeof(el)!='undefined' ) {\n		el.onclick = null;\n		el.style.opacity = '0.7';\n		httpRequest.onreadystatechange = function()\n		{\n			if (httpRequest.readyState == 4) if (httpRequest.status == 200) el.style.opacity = '0.3';\n		}\n	}\n	httpRequest.open('GET', url, true);\n	httpRequest.send(null);\n}\n");
		// UPD ROW
		tcp_writestr(&tcpbuf, sock, "\nfunction xmlupdateRow( xmlDoc, id )\n{\n	var row = document.getElementById(id);\n	row.cells.item(0).innerHTML = xmlDoc.getElementsByTagName('c0')[0].childNodes[0].nodeValue;\n	row.cells.item(1).innerHTML = xmlDoc.getElementsByTagName('c1')[0].childNodes[0].nodeValue;\n	row.cells.item(2).innerHTML = xmlDoc.getElementsByTagName('c2')[0].childNodes[0].nodeValue;\n	row.cells.item(3).className = xmlDoc.getElementsByTagName('c3_c')[0].childNodes[0].nodeValue;\n	row.cells.item(3).innerHTML = xmlDoc.getElementsByTagName('c3')[0].childNodes[0].nodeValue;\n	row.cells.item(4).innerHTML = xmlDoc.getElementsByTagName('c4')[0].childNodes[0].nodeValue;\n	row.cells.item(5).innerHTML = xmlDoc.getElementsByTagName('c5')[0].childNodes[0].nodeValue;\n	row.cells.item(6).innerHTML = xmlDoc.getElementsByTagName('c6')[0].childNodes[0].nodeValue;\n	row.cells.item(7).innerHTML = xmlDoc.getElementsByTagName('c7')[0].childNodes[0].nodeValue;\n	row.cells.item(8).innerHTML = xmlDoc.getElementsByTagName('c8')[0].childNodes[0].nodeValue;\n}\n");
		char url[256];
		sprintf( url, "'/camd35?action=row&clid='+idx");
		sprintf( http_buf, HTTP_UPDATE_ROW, url);
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		// UPD DIV
		sprintf( url, "/camd35?action=div&id=%d&list=%s", get_id, str_list);
		sprintf( http_buf, HTTP_UPDATE_DIV, cfg.http.autorefresh*1000, url);
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		//
		tcp_writestr(&tcpbuf, sock, "\nfunction start()\n{\n	setautorefresh(autorefresh);\n}");
		tcp_writestr(&tcpbuf, sock, "\n</script>\n");
		tcp_write(&tcpbuf, sock, http_head_, strlen(http_head_) );
		tcp_writestr(&tcpbuf, sock, "<body onload=\"start();\">");
		tcp_write_menu(&tcpbuf, sock,PAGE_CAMD35);
		// DIV
		tcp_writestr(&tcpbuf, sock, "<div id='mainDiv'>");
	}

	tcp_writestr(&tcpbuf, sock, "<table style=\"margin:0px; padding:0px; border-width:0px; border-spacing: 1px;\"><tr>");
	tcp_writestr(&tcpbuf, sock, "<td style=\"margin:0px; padding:0px;\"><a href='/camd35'><table border=1 width=200px>");
	// Total Servers
	sprintf( http_buf, "<tr><td>Total Camd35 Servers: %d</td></tr>", cfg.camd35.totalservers); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	int total, connected, active;
	total_camd35_clients( &total, &connected, &active );
	sprintf( http_buf, "<tr><td>Connected Clients: %d / %d</td></tr>", connected, total ); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	tcp_writestr(&tcpbuf, sock, "</table></a></td>");
	// Servers
	if (cfg.camd35.server) {
		int counter = 0;
		struct camd35_server_data *camd35 = cfg.camd35.server;
		while ( camd35 && (counter<5) ) {
			sprintf( http_buf, "<td style=\"margin:0px; padding: 0px;\"><a href='/camd35?id=%d'><table border=1 width=150px>", camd35->id);
			tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			if (camd35->handle>0) sprintf( http_buf, "<tr><td>camd35 %d [<span class=success>%d</span>]</td></tr>", camd35->id, camd35->port);
			else sprintf( http_buf, "<tr><td>camd35 %d [<span class=failed>%d</span>]</td></tr>", camd35->id, camd35->port);
			tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			int total, connected, active;
			camd35_clients( camd35, &total, &connected, &active );
			sprintf( http_buf, "<tr><td>Connected: %d / %d</td></tr>", connected, total);
			tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			tcp_writestr(&tcpbuf, sock, "</table></a></td>");
			counter++;
			camd35 = camd35->next;
		}
	}
	// End of table
	tcp_writestr(&tcpbuf, sock, "</tr></table><br>");

	tcp_writestr(&tcpbuf, sock, "<select style=\"width:200px;\" onchange=\"parent.location.href='/camd35?id='+this.value\">");
	sprintf( http_buf, "<option value=0>ALL (%d)</option>", cfg.camd35.totalservers); //total_camd35_servers());
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	struct camd35_server_data *tmp = cfg.camd35.server;
	while (tmp) {
		if (get_id==tmp->id) sprintf( http_buf, "<option value=%d selected>[%d] camd35 %d</option>",tmp->id,tmp->port, tmp->id );
		else sprintf( http_buf, "<option value=%d>[%d] camd35 %d</option>",tmp->id,tmp->port, tmp->id );
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		tmp = tmp->next;
	}
	tcp_writestr(&tcpbuf, sock, "</select> ");
	//
	if (camd35) camd35_clients( camd35, &total, &connected, &active ); else total_camd35_clients( &total, &connected, &active );
	char *class1 = "button"; char *class2 = "sbutton";
	char *class;
	if (get_list==LIST_ACTIVE) class = class2; else class = class1;
	sprintf( http_buf, "<input type=button class=%s onclick=\"parent.location='/camd35?id=%d&amp;list=active'\" value='Active Clients (%d)'>", class, get_id, active);
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	if (get_list==LIST_CONNECTED) class = class2; else class = class1;
	sprintf( http_buf, " <input type=button class=%s onclick=\"parent.location='/camd35?id=%d&amp;list=connected'\" value='Connected Clients (%d)'>", class, get_id, connected);
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	if (get_list==LIST_ALL) class = class2; else class = class1;
	sprintf( http_buf, " <input type=button class=%s onclick=\"parent.location='/camd35?id=%d&amp;list=all'\" value='All Clients (%d)'>", class, get_id, total);
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	//
	if (camd35) { // One Server Selected
		// Table
		sprintf( http_buf, "\n<table class=maintable width=100%%><tr><th width=100px>Client</th><th width=70px>ucrc</th><th width=120px>ip</th><th width=110px>Connected</th><th width=60px>TotalEcm</th><th width=90px>AcceptedEcm</th><th width=90px>EcmOK</th><th width=50px>EcmTime</th><th>Last used share</th></tr>");
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		struct camd35_client_data *cli = camd35->client;
		int alt=0;
		if (get_list==LIST_ACTIVE) {
			while (cli) {
				if ( (cli->connection.status>0)&&((GetTickCount()-cli->lastecmtime) < 20000) ) {
					if (alt==1) alt=2; else alt=1;
					getcamd35cells(cli,cell);
					/* R35: HTML rows render composed cells as-is; raw text fields are pre-escaped in the builders (escf/html_esc). Blanket escape here printed the page's own tags as text. */
					sprintf( http_buf,"\n<tr id=\"Row%d\" class=alt%d onMouseOver='setupdateRow(%d)' onMouseOut='setupdateRow(0)'> <td>%s</td><td>%s</td><td>%s</td><td class=\"%s\">%s</td><td align=center>%s</td><td>%s</td><td>%s</td><td align=center>%s</td><td>%s</td></tr>\n",cli->id,alt,cli->id,cell[0],cell[1],cell[2],cell[9],cell[3],cell[4],cell[5],cell[6],cell[7],cell[8]);
					tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				}
				cli = cli->next;
			}
		}
		else if (get_list==LIST_CONNECTED) {
			while (cli) {
				if (cli->connection.status>0) {
					if (alt==1) alt=2; else alt=1;
					getcamd35cells(cli,cell);
					/* R35: HTML rows render composed cells as-is; raw text fields are pre-escaped in the builders (escf/html_esc). Blanket escape here printed the page's own tags as text. */
					sprintf( http_buf,"\n<tr id=\"Row%d\" class=alt%d onMouseOver='setupdateRow(%d)' onMouseOut='setupdateRow(0)'> <td>%s</td><td>%s</td><td>%s</td><td class=\"%s\">%s</td><td align=center>%s</td><td>%s</td><td>%s</td><td align=center>%s</td><td>%s</td></tr>\n",cli->id,alt,cli->id,cell[0],cell[1],cell[2],cell[9],cell[3],cell[4],cell[5],cell[6],cell[7],cell[8]);
					tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				}
				cli = cli->next;
			}
		}
		else { // ALL
			while (cli) {
				if (alt==1) alt=2; else alt=1;
				getcamd35cells(cli,cell);
				/* R35: HTML rows render composed cells as-is; raw text fields are pre-escaped in the builders (escf/html_esc). Blanket escape here printed the page's own tags as text. */
				sprintf( http_buf,"\n<tr id=\"Row%d\" class=alt%d onMouseOver='setupdateRow(%d)' onMouseOut='setupdateRow(0)'> <td>%s</td><td>%s</td><td>%s</td><td class=\"%s\">%s</td><td align=center>%s</td><td>%s</td><td>%s</td><td align=center>%s</td><td>%s</td></tr>\n",cli->id,alt,cli->id,cell[0],cell[1],cell[2],cell[9],cell[3],cell[4],cell[5],cell[6],cell[7],cell[8]);
				tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				cli = cli->next;
			}
		}
		sprintf( http_buf, "\n</table>");
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	}

	else {
		// Table
		tcp_writestr(&tcpbuf,sock, "\n<table class=maintable width=100%>");
		tcp_writestr(&tcpbuf,sock, "\n<tr><th width=100px>Client</th><th width=70px>version</th><th width=120px>ip</th><th width=110px>Connected</th><th width=60px>TotalEcm</th><th width=90px>AcceptedEcm</th><th width=90px>EcmOK</th><th width=50px>EcmTime</th><th>Last used share</th></tr>");
		int alt=0;
		camd35 = cfg.camd35.server;
		while (camd35) {
			int total, connected, active;
			camd35_clients( camd35, &total, &connected, &active );
			if ( (get_list==LIST_ACTIVE) && active ) {
				sprintf( http_buf,"\n<tr><td class=alt3 colspan=9> camd35 %d (%d)</td></tr>", camd35->id, active); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				struct camd35_client_data *cli = camd35->client;
				while (cli) {
					if ( (cli->connection.status>0)&&((GetTickCount()-cli->lastecmtime) < 20000) ) {
						if (alt==1) alt=2; else alt=1;
						getcamd35cells(cli,cell);
						/* R35: HTML rows render composed cells as-is; raw text fields are pre-escaped in the builders (escf/html_esc). Blanket escape here printed the page's own tags as text. */
						sprintf( http_buf,"\n<tr id=\"Row%d\" class=alt%d onMouseOver='setupdateRow(%d)' onMouseOut='setupdateRow(0)'> <td>%s</td><td>%s</td><td>%s</td><td class=\"%s\">%s</td><td align=center>%s</td><td>%s</td><td>%s</td><td align=center>%s</td><td>%s</td></tr>\n",cli->id,alt,cli->id,cell[0],cell[1],cell[2],cell[9],cell[3],cell[4],cell[5],cell[6],cell[7],cell[8]);
						tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
					}
					cli = cli->next;
				}
			}
			else if ( (get_list==LIST_ALL) && total ) {
				sprintf( http_buf,"\n<tr><td class=alt3 colspan=9> camd35 %d (%d)</td></tr>", camd35->id, total); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				struct camd35_client_data *cli = camd35->client;
				while (cli) {
					if (alt==1) alt=2; else alt=1;
					getcamd35cells(cli,cell);
					/* R35: HTML rows render composed cells as-is; raw text fields are pre-escaped in the builders (escf/html_esc). Blanket escape here printed the page's own tags as text. */
					sprintf( http_buf,"\n<tr id=\"Row%d\" class=alt%d onMouseOver='setupdateRow(%d)' onMouseOut='setupdateRow(0)'> <td>%s</td><td>%s</td><td>%s</td><td class=\"%s\">%s</td><td align=center>%s</td><td>%s</td><td>%s</td><td align=center>%s</td><td>%s</td></tr>\n",cli->id,alt,cli->id,cell[0],cell[1],cell[2],cell[9],cell[3],cell[4],cell[5],cell[6],cell[7],cell[8]);
					tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
					cli = cli->next;
				}
			}
			else if ( (get_list==LIST_CONNECTED) && connected ) {
				sprintf( http_buf,"\n<tr><td class=alt3 colspan=9> camd35 %d (%d)</td></tr>", camd35->id, connected); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				struct camd35_client_data *cli = camd35->client;
				while (cli) {
					if (cli->connection.status>0) {
						if (alt==1) alt=2; else alt=1;
						getcamd35cells(cli,cell);
						/* R35: HTML rows render composed cells as-is; raw text fields are pre-escaped in the builders (escf/html_esc). Blanket escape here printed the page's own tags as text. */
						sprintf( http_buf,"\n<tr id=\"Row%d\" class=alt%d onMouseOver='setupdateRow(%d)' onMouseOut='setupdateRow(0)'> <td>%s</td><td>%s</td><td>%s</td><td class=\"%s\">%s</td><td align=center>%s</td><td>%s</td><td>%s</td><td align=center>%s</td><td>%s</td></tr>\n",cli->id,alt,cli->id,cell[0],cell[1],cell[2],cell[9],cell[3],cell[4],cell[5],cell[6],cell[7],cell[8]);
						tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
					}
					cli = cli->next;
				}
			}
			camd35 = camd35->next;
		}
		sprintf( http_buf, "</table>");
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	}
	if (get_action==ACTION_PAGE) {
		tcp_writestr(&tcpbuf, sock, "</div>");
		tcp_writestr(&tcpbuf, sock, "</body></html>");
	}

	tcp_flush(&tcpbuf, sock);
}

#endif


















#ifdef CACHEEX

void cacheex_server_cells(struct server_data *srv, char cell[10][2048] )
{
	char temp[2048];
	char et1[1024], et2[1024];	/* R35 text-field scratch */
	unsigned int ticks = GetTickCount();
	uint32_t d;

	memset(cell, 0, 10*2048);
	if (
		(srv->type!=TYPE_CCCAM)
#ifdef CAMD35_CLI
		&&(srv->type!=TYPE_CAMD35)
#endif
#ifdef CS378X_CLI
		&&(srv->type!=TYPE_CS378X) 
#endif
	) return;
	if (!srv->cacheex_mode) return;
	// CELL0
	sprintf( cell[0],"%s:%d",escf(et1,srv->host->name),srv->port);
	// CELL1*IP
	if (!srv->host->ip && srv->host->clip)
		sprintf( temp,"0.0.0.0 (%s)",(char*)ip2string(srv->host->ip) );
	else {
		char *p = getcountrycodebyip(srv->host->ip);
		if (p) sprintf( temp,"<img src='/flag_%s.gif' title='%s'> %s", p, getcountryname(p), (char*)ip2string(srv->host->ip) );
		else sprintf( temp,"%s",(char*)ip2string(srv->host->ip) );
	}
	strcat( cell[1], temp );
	// CELL2
	if (srv->connection.status>0) {
		if (srv->type==TYPE_CCCAM) sprintf( cell[2],"mode%d (CCcam)", srv->cacheex_mode);
#ifdef CAMD35_CLI
		else if (srv->type==TYPE_CAMD35) sprintf( cell[2],"mode%d (camd35)", srv->cacheex_mode);
#endif
#ifdef CS378X_CLI
		else if (srv->type==TYPE_CS378X) sprintf( cell[2],"mode%d (cs378x)", srv->cacheex_mode);
#endif
		else sprintf( cell[2],"Unknown"); 
	}
	else sprintf( cell[2]," ");
	// CELL3
	if (srv->connection.status>0)
		sprintf( cell[3],"%02x%02x%02x%02x%02x%02x%02x%02x", srv->nodeid[0],srv->nodeid[1],srv->nodeid[2],srv->nodeid[3],srv->nodeid[4],srv->nodeid[5],srv->nodeid[6],srv->nodeid[7]);
	else sprintf( cell[3]," ");
	// CELL4
	if (srv->connection.status>0) {
		d = (ticks-srv->connection.time)/1000;
		sprintf( cell[4],"%02dd %02d:%02d:%02d", d/(3600*24),(d/3600)%24,(d/60)%60,d%60);
		sprintf( cell[9],"online");
	}
	else {
		sprintf( cell[9],"offline");
		if (srv->flags&FLAG_DELETE) sprintf( cell[4],"Removed");
		else if (srv->flags&FLAG_EXPIRED) sprintf( cell[4],"Expired");
		else if (srv->flags&FLAG_DISABLE) sprintf( cell[4],"Disabled");
		else sprintf( cell[4],"offline");
	}
	if ( (srv->cacheex_mode==2) && srv->csporthit[0].csid ) {
		strcat( cell[4], "<table class=\"connect_data\">" );
		strcat( cell[4], "<tr><td width=150px>Profile</td><td>Hits</td></tr>" );
		int i; char temp[512];
		for(i=0; i<10; i++) {
			if (!srv->csporthit[i].csid) break;
			struct cardserver_data *cs = getcsbyid(srv->csporthit[i].csid);
			if (!cs) continue;
			sprintf( temp,"<tr><td>%s</td><td>%d</td></tr>", cs->name,srv->csporthit[i].hits);
			strcat( cell[4], temp );
		}
		strcat( cell[4], "</table>");
	}

	// CELL5
	sprintf( cell[5], "%d",srv->cacheex.push[0]); // PUSH
	int i;
	for (i=1; i<10; i++ ) {
		if (srv->cacheex.push[i]>0) {
			sprintf( temp,"<br>[%d] %d", i, srv->cacheex.push[i]);
			strcat( cell[5], temp );
		}
	}

	// CELL6
	sprintf( cell[6], "%d",srv->cacheex.got[0]); // GOT
	for (i=1; i<10; i++ ) {
		if (srv->cacheex.got[i]>0) {
			sprintf( temp,"<br>[%d] %d", i, srv->cacheex.got[i]);
			strcat( cell[6], temp );
		}
	}

	// CELL7
	sprintf( cell[7], "%d",srv->cacheex.hits); // HIT
	if ( (srv->cacheex_mode==2) && srv->cacheex.badcw ) {
		sprintf( temp,"<br>bad=%d", srv->cacheex.badcw);
		strcat( cell[7], temp );
	}

	// CELL8
	if (srv->cacheex_mode==3) {
		if (srv->sharelimits[0].caid==0xffff) strcpy( cell[8], " ");
		else {
			sprintf( cell[8]," Shares = %04x:%x", srv->sharelimits[0].caid, srv->sharelimits[0].provid);
			int i;
			for (i=1; i<100; i++) {
				if (srv->sharelimits[i].caid==0xffff) break;
				sprintf( temp,", %04x:%x", srv->sharelimits[i].caid, srv->sharelimits[i].provid);
				strcat( cell[8], temp );
			}
		}
	}
	else if (srv->cacheex_mode==2) {
		if (srv->cacheex.lastcaid) {
			sprintf( cell[8],"ch %s (%dms)", getchname(srv->cacheex.lastcaid, srv->cacheex.lastprov, srv->cacheex.lastsid) , srv->cacheex.lastdecodetime );
		}
		else strcpy( cell[8], " ");
	}
}

void cacheex_cccamclient_cells(struct cc_client_data *cli, char cell[10][2048])
{
	char temp[2048];
	char et1[1024], et2[1024];	/* R35 text-field scratch */
	unsigned int ticks = GetTickCount();
	unsigned int d;
	memset(cell, 0, 10*2048);
	if (!cli->cacheex_mode) return;
	// CELL0 # NAME
	sprintf( cell[0],"%s",escf(et1,cli->user));
	// CELL1 # IP
	char *p = getcountrycodebyip(cli->ip);
	if (cli->host)
		if (p) sprintf( cell[1],"<img src='/flag_%s.gif' title='%s'> %s<br>%s", p, getcountryname(p), (char*)ip2string(cli->ip), escf(et1,cli->host->name) ); else sprintf( cell[1],"%s<br>%s",(char*)ip2string(cli->ip), escf(et1,cli->host->name) );
	else
		if (p) sprintf( cell[1],"<img src='/flag_%s.gif' title='%s'> %s", p, getcountryname(p), (char*)ip2string(cli->ip) ); else sprintf( cell[1],"%s",(char*)ip2string(cli->ip) );

	// CELL2 # VERSION
	sprintf( cell[2],"mode%d", cli->cacheex_mode);
	// CELL3 # nodeid
	if (strlen(cli->version)) sprintf( cell[3],"%02x%02x%02x%02x%02x%02x%02x%02x", cli->nodeid[0],cli->nodeid[1],cli->nodeid[2],cli->nodeid[3],cli->nodeid[4],cli->nodeid[5],cli->nodeid[6],cli->nodeid[7]);
	// CELL4 # Connection Time
	if (cli->connection.status>0) {
		sprintf( cell[9],"online");
		d = (ticks-cli->connection.time)/1000;
		sprintf( cell[4], "%02dd %02d:%02d:%02d", d/(3600*24), (d/3600)%24, (d/60)%60, d%60);
	}
	else {
		strcpy( cell[9], "offline" );
		if (cli->flags&FLAG_DELETE) sprintf( cell[4],"Removed");
		else if (cli->flags&FLAG_EXPIRED) sprintf( cell[4],"Expired");
		else if (cli->flags&FLAG_DISABLE) sprintf( cell[4],"Disabled");
		else sprintf( cell[4],"offline");
	}
	if (cli->csporthit[0].csid) {
		strcat( cell[4], "<table class=\"connect_data\">" );
		strcat( cell[4], "<tr><td width=150px>Profile</td><td>Hits</td></tr>" );
		int i; char temp[512];
		for(i=0; i<10; i++) {
			if (!cli->csporthit[i].csid) break;
			struct cardserver_data *cs = getcsbyid(cli->csporthit[i].csid);
			if (!cs) continue;
			sprintf( temp,"<tr><td>%s</td><td>%d</td></tr>", cs->name,cli->csporthit[i].hits);
			strcat( cell[4], temp );
		}
		strcat( cell[4], "</table>");
	}
	// CELL5
	sprintf( cell[5], "%d", cli->cacheex.push[0]);
	int i;
	for (i=1; i<10; i++ ) {
		if (cli->cacheex.push[i]>0) {
			sprintf( temp,"<br>[%d] %d", i, cli->cacheex.push[i]);
			strcat( cell[5], temp );
		}
	}

	// CELL6
	sprintf( cell[6], "%d", cli->cacheex.got[0]);
	for (i=1; i<10; i++ ) {
		if (cli->cacheex.got[i]>0) {
			sprintf( temp,"<br>[%d] %d", i, cli->cacheex.got[i]);
			strcat( cell[6], temp );
		}
	}

	// CELL7
	sprintf( cell[7], "%d", cli->cacheex.hits);
	if ( (cli->cacheex_mode==3) && cli->cacheex.badcw ) {
		sprintf( temp,"<br>bad=%d", cli->cacheex.badcw);
		strcat( cell[7], temp );
	}

	// CELL8
	if (cli->cacheex_mode==2) {
		if (cli->sharelimits[0].caid==0xffff) strcpy( cell[8], " ");
		else {
			sprintf( cell[8]," Shares = %04x:%x", cli->sharelimits[0].caid, cli->sharelimits[0].provid);
			int i;
			for (i=1; i<100; i++) {
				if (cli->sharelimits[i].caid==0xffff) break;
				sprintf( temp,", %04x:%x", cli->sharelimits[i].caid, cli->sharelimits[i].provid);
				strcat( cell[8], temp );
			}
		}
	}
	else if (cli->cacheex_mode==3) {
		if (cli->cacheex.lastcaid) {
			sprintf( cell[8],"ch %s (%dms)", getchname(cli->cacheex.lastcaid, cli->cacheex.lastprov, cli->cacheex.lastsid) , cli->cacheex.lastdecodetime );
		}
		else strcpy( cell[8], " ");
	}
}

#if defined(CAMD35_SRV) || defined(CS378X_SRV)

void cacheex_camd35client_cells(struct camd35_client_data *cli, char cell[10][2048])
{
	char temp[2048];
	char et1[1024], et2[1024];	/* R35 text-field scratch */
	unsigned int ticks = GetTickCount();
	unsigned int d;
	memset(cell, 0, 10*2048);
	if (!cli->cacheex_mode) return;
	// CELL0 # NAME
	sprintf( cell[0],"%s",escf(et1,cli->user));
	// CELL1 # IP
	char *p = getcountrycodebyip(cli->ip);
	if (p) sprintf( cell[1],"<img src='/flag_%s.gif' title='%s'> %s", p, getcountryname(p), (char*)ip2string(cli->ip) );
	else sprintf( cell[1],"%s",(char*)ip2string(cli->ip) );

	// CELL2 # VERSION
	sprintf( cell[2],"mode%d", cli->cacheex_mode);
	// CELL3 # nodeid
	sprintf( cell[3],"%02x%02x%02x%02x%02x%02x%02x%02x", cli->nodeid[0],cli->nodeid[1],cli->nodeid[2],cli->nodeid[3],cli->nodeid[4],cli->nodeid[5],cli->nodeid[6],cli->nodeid[7]);
	// CELL4 # Connection Time
	if (cli->connection.status>0) {
		sprintf( cell[9],"online");
		d = (ticks-cli->connection.time)/1000;
		sprintf( cell[4], "%02dd %02d:%02d:%02d", d/(3600*24), (d/3600)%24, (d/60)%60, d%60);
	}
	else {
		strcpy( cell[9], "offline" );
		if (cli->flags&FLAG_DELETE) sprintf( cell[4],"Removed");
		else if (cli->flags&FLAG_EXPIRED) sprintf( cell[4],"Expired");
		else if (cli->flags&FLAG_DISABLE) sprintf( cell[4],"Disabled");
		else sprintf( cell[4],"offline");
	}
	if (cli->csporthit[0].csid) {
		strcat( cell[4], "<table class=\"connect_data\">" );
		strcat( cell[4], "<tr><td width=150px>Profile</td><td>Hits</td></tr>" );
		int i; char temp[512];
		for(i=0; i<10; i++) {
			if (!cli->csporthit[i].csid) break;
			struct cardserver_data *cs = getcsbyid(cli->csporthit[i].csid);
			if (!cs) continue;
			sprintf( temp,"<tr><td>%s</td><td>%d</td></tr>", cs->name,cli->csporthit[i].hits);
			strcat( cell[4], temp );
		}
		strcat( cell[4], "</table>");
	}

	// CELL5
	sprintf( cell[5], "%d", cli->cacheex.push[0]); // sent
	int i;
	for (i=1; i<10; i++ ) {
		if (cli->cacheex.push[i]>0) {
			sprintf( temp,"<br>[%d] %d", i, cli->cacheex.push[i]);
			strcat( cell[5], temp );
		}
	}

	// CELL6
	sprintf( cell[6], "%d", cli->cacheex.got[0]); // received
	for (i=1; i<10; i++ ) {
		if (cli->cacheex.got[i]>0) {
			sprintf( temp,"<br>[%d] %d", i, cli->cacheex.got[i]);
			strcat( cell[6], temp );
		}
	}

	// CELL7
	sprintf( cell[7], "%d", cli->cacheex.hits);
	if ( (cli->cacheex_mode==3) && cli->cacheex.badcw ) {
		sprintf( temp,"<br>bad=%d", cli->cacheex.badcw);
		strcat( cell[7], temp );
	}

	// CELL8
	if (cli->cacheex_mode==2) {
		if (cli->sharelimits[0].caid==0xffff) strcpy( cell[8], " ");
		else {
			sprintf( cell[8]," Shares = %04x:%x", cli->sharelimits[0].caid, cli->sharelimits[0].provid);
			int i;
			for (i=1; i<100; i++) {
				if (cli->sharelimits[i].caid==0xffff) break;
				sprintf( temp,", %04x:%x", cli->sharelimits[i].caid, cli->sharelimits[i].provid);
				strcat( cell[8], temp );
			}
		}
	}
	else if (cli->cacheex_mode==3) {
		if (cli->cacheex.lastcaid) {
			sprintf( cell[8],"ch %s (%dms)", getchname(cli->cacheex.lastcaid, cli->cacheex.lastprov, cli->cacheex.lastsid) , cli->cacheex.lastdecodetime );
		}
		else strcpy( cell[8], " ");
	}
}

#endif

void http_send_cacheex(int sock, http_request *req)
{
	char http_buf[4096];
	struct tcp_buffer_data tcpbuf;
	char cell[10][2048];

	// Get Params
	char *str_action = isset_get( req, "action");
	// Param 'action'
	int get_action;
	if (str_action) {
		if (!strcmp(str_action,"div")) get_action = ACTION_DIV;
		else str_action = NULL;
	}
	if (!str_action) { str_action = "page"; get_action = ACTION_PAGE; }

	char *id = isset_get( req, "id");
	if (id) {
		int i = atoi(id);
		if ( (i>>16)==1 ) { // CCcam Clients
			struct cccam_server_data *cccam = cfg.cccam.server;
			while (cccam) {
				if (!(cccam->flags&FLAG_DELETE)) {
					struct cc_client_data *cli = cccam->cacheexclient;
					while (cli) {
						if ( !(cli->flags&FLAG_DELETE) && cli->id==(i&0xffff) ) {
			                cacheex_cccamclient_cells(cli,cell);
			                for(i=0; i<10; i++) xmlescape( cell[i] );
			                sprintf( http_buf, "<cacheex>\n<c0>%s</c0>\n<c1>%s</c1>\n<c2>%s</c2>\n<c3>%s</c3>\n<c4_c>%s</c4_c>\n<c4>%s</c4>\n<c5>%s</c5>\n<c6>%s</c6><c7>%s</c7><c8>%s</c8>\n</cacheex>\n",cell[0],cell[1],cell[2],cell[3],cell[9],cell[4],cell[5],cell[6],cell[7],cell[8] );
			                http_send_xml( sock, req, http_buf, strlen(http_buf));
							return;
						}
						cli = cli->next;
					}
				}
				cccam = cccam->next;
			}
		}
		else if ( (i>>16)==2 ) { // camd35 Clients
			struct camd35_server_data *camd35 = cfg.camd35.server;
			while (camd35) {
				if (!(camd35->flags&FLAG_DELETE)) {
					struct camd35_client_data *cli = camd35->cacheexclient;
					while (cli) {
						if ( !(cli->flags&FLAG_DELETE) && cli->id==(i&0xffff) ) {
			                cacheex_camd35client_cells(cli,cell);
			                for(i=0; i<10; i++) xmlescape( cell[i] );
			                sprintf( http_buf, "<cacheex>\n<c0>%s</c0>\n<c1>%s</c1>\n<c2>%s</c2>\n<c3>%s</c3>\n<c4_c>%s</c4_c>\n<c4>%s</c4>\n<c5>%s</c5>\n<c6>%s</c6><c7>%s</c7><c8>%s</c8>\n</cacheex>\n",cell[0],cell[1],cell[2],cell[3],cell[9],cell[4],cell[5],cell[6],cell[7],cell[8] );
			                http_send_xml( sock, req, http_buf, strlen(http_buf));
							return;
						}
						cli = cli->next;
					}
				}
				camd35 = camd35->next;
			}
		}
		else if ( (i>>16)==3 ) { // cs378x Clients
			struct camd35_server_data *cs378x = cfg.cs378x.server;
			while (cs378x) {
				if (!(cs378x->flags&FLAG_DELETE)) {
					struct camd35_client_data *cli = cs378x->cacheexclient;
					while (cli) {
						if ( !(cli->flags&FLAG_DELETE) && cli->id==(i&0xffff) ) {
			                cacheex_camd35client_cells(cli,cell);
			                for(i=0; i<10; i++) xmlescape( cell[i] );
			                sprintf( http_buf, "<cacheex>\n<c0>%s</c0>\n<c1>%s</c1>\n<c2>%s</c2>\n<c3>%s</c3>\n<c4_c>%s</c4_c>\n<c4>%s</c4>\n<c5>%s</c5>\n<c6>%s</c6><c7>%s</c7><c8>%s</c8>\n</cacheex>\n",cell[0],cell[1],cell[2],cell[3],cell[9],cell[4],cell[5],cell[6],cell[7],cell[8] );
			                http_send_xml( sock, req, http_buf, strlen(http_buf));
							return;
						}
						cli = cli->next;
					}
				}
				cs378x = cs378x->next;
			}
		}
		else if ( (i>>16)==4 ) { // Server
			struct server_data *srv = cfg.cacheexserver;
			while (srv) {
				if ( !(srv->flags&FLAG_DELETE) && srv->id==(i&0xffff) ) {
					cacheex_server_cells(srv,cell);
					for(i=0; i<10; i++) xmlescape( cell[i] );
					sprintf( http_buf, "<cacheex>\n<c0>%s</c0>\n<c1>%s</c1>\n<c2>%s</c2>\n<c3>%s</c3>\n<c4_c>%s</c4_c>\n<c4>%s</c4>\n<c5>%s</c5>\n<c6>%s</c6><c7>%s</c7><c8>%s</c8>\n</cacheex>\n",cell[0],cell[1],cell[2],cell[3],cell[9],cell[4],cell[5],cell[6],cell[7],cell[8] );
					http_send_xml( sock, req, http_buf, strlen(http_buf));
					return;
				}
				srv = srv->next;
			}
		}
		return;
	}

	/////////////////////////////////////////////
	struct cccam_server_data *cccam = NULL;
	tcp_init(&tcpbuf);
	http_replyok_write(req, &tcpbuf, sock);
	if (get_action==ACTION_PAGE) {
		http_html_write(req, &tcpbuf, sock);
		tcp_write(&tcpbuf, sock, http_head, strlen(http_head) );
		sprintf( http_buf, html_title, cfg.http.title, "CacheEX"); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		tcp_write(&tcpbuf, sock, http_link, strlen(http_link) );
		http_style_write(&tcpbuf, sock);
		// JS
		tcp_writestr(&tcpbuf, sock, "\n<script type='text/javascript'>");
		// ACTIONS REQUEST
		tcp_writestr(&tcpbuf, sock, "\nfunction imgrequest( url, el )\n{\n	var httpRequest;\n	try { httpRequest = new XMLHttpRequest(); }\n	catch (trymicrosoft) { try { httpRequest = new ActiveXObject('Msxml2.XMLHTTP'); } catch (oldermicrosoft) { try { httpRequest = new ActiveXObject('Microsoft.XMLHTTP'); } catch(failed) { httpRequest = false; } } }\n	if (!httpRequest) { alert('Your browser does not support Ajax.'); return false; }\n	if ( typeof(el)!='undefined' ) {\n		el.onclick = null;\n		el.style.opacity = '0.7';\n		httpRequest.onreadystatechange = function()\n		{\n			if (httpRequest.readyState == 4) if (httpRequest.status == 200) el.style.opacity = '0.3';\n		}\n	}\n	httpRequest.open('GET', url, true);\n	httpRequest.send(null);\n}\n");
		// UPD ROW
		tcp_writestr(&tcpbuf, sock, "\nfunction xmlupdateRow( xmlDoc, id )\n{\n	var row = document.getElementById(id);\n	row.cells.item(0).innerHTML = xmlDoc.getElementsByTagName('c0')[0].childNodes[0].nodeValue;\n	row.cells.item(1).innerHTML = xmlDoc.getElementsByTagName('c1')[0].childNodes[0].nodeValue;\n	row.cells.item(2).innerHTML = xmlDoc.getElementsByTagName('c2')[0].childNodes[0].nodeValue;\n	row.cells.item(3).innerHTML = xmlDoc.getElementsByTagName('c3')[0].childNodes[0].nodeValue;\n	row.cells.item(4).className = xmlDoc.getElementsByTagName('c4_c')[0].childNodes[0].nodeValue;\n	row.cells.item(4).innerHTML = xmlDoc.getElementsByTagName('c4')[0].childNodes[0].nodeValue;\n	row.cells.item(5).innerHTML = xmlDoc.getElementsByTagName('c5')[0].childNodes[0].nodeValue;\n	row.cells.item(6).innerHTML = xmlDoc.getElementsByTagName('c6')[0].childNodes[0].nodeValue;\n	row.cells.item(7).innerHTML = xmlDoc.getElementsByTagName('c7')[0].childNodes[0].nodeValue;\n	row.cells.item(8).innerHTML = xmlDoc.getElementsByTagName('c8')[0].childNodes[0].nodeValue;\n}\n");
		char url[256];
		sprintf( url, "'/cacheex?action=row&id='+idx");
		sprintf( http_buf, HTTP_UPDATE_ROW, url);
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		// UPD DIV
		sprintf( url, "/cacheex?action=div");
		sprintf( http_buf, HTTP_UPDATE_DIV, cfg.http.autorefresh*1000, url);
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		//
		tcp_writestr(&tcpbuf, sock, "\nfunction start()\n{\n	setautorefresh(autorefresh);\n}");
		tcp_writestr(&tcpbuf, sock, "\n</script>\n");
		tcp_write(&tcpbuf, sock, http_head_, strlen(http_head_) );
		tcp_writestr(&tcpbuf, sock, "<body onload=\"start();\">");
		tcp_write_menu(&tcpbuf, sock,PAGE_CACHEEX);
		// DIV
		tcp_writestr(&tcpbuf, sock, "\n<div id='mainDiv'>");
	}

	sprintf( http_buf,"<br> * Total Replies = %d</td>",cfg.cacheex.rep); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	sprintf( http_buf,"<br> * Total Hits = %d</td>",cfg.cacheex.hits); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );

	// TABLE
	tcp_writestr(&tcpbuf,sock, "\n<br><table class=maintable width=100%>");
	tcp_writestr(&tcpbuf,sock, "\n<tr><th width=170px>Name / Host</th><th width=120px>ip</th><th width=80px>Mode</th><th width=100px>NodeID</th><th width=110px>Connected</th><th width=80px>Push</th><th width=80px>Got</th><th width=80px>Hits</th><th>Last CacheEX Hit</th></tr>");
	int alt=0;

	// CCcam Clients
	cccam = cfg.cccam.server;
	while (cccam) {
		// Count Clients
		int counter = 0;
		struct cc_client_data *cli = cccam->cacheexclient;
		while (cli) {
			if (cli->cacheex_mode) counter++;
			cli = cli->next;
		}
		// test
		if (counter) {
			sprintf( http_buf,"\n<tr><td class=alt3 colspan=9> CCcam %d (%d)</td></tr>", cccam->id, counter); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			struct cc_client_data *cli = cccam->cacheexclient;
			while (cli) {
				if (cli->cacheex_mode) {
					if (alt==1) alt=2; else alt=1;
					cacheex_cccamclient_cells(cli,cell);
					/* R35: HTML rows render composed cells as-is; raw text fields are pre-escaped in the builders (escf/html_esc). Blanket escape here printed the page's own tags as text. */
					sprintf( http_buf,"\n<tr class=alt%d id=\"Row%d\" onMouseOver='setupdateRow(%d)' onMouseOut='setupdateRow(0)'> <td>%s</td><td>%s</td><td>%s</td><td>%s</td><td class=\"%s\">%s</td><td>%s</td><td>%s</td><td>%s</td><td>%s</td></tr>\n",alt,(cli->id+0x10000),(cli->id+0x10000),cell[0],cell[1],cell[2],cell[3],cell[9],cell[4],cell[5],cell[6],cell[7],cell[8]);
					tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				}
				cli = cli->next;
			}
		}
		cccam = cccam->next;
	}

#ifdef CAMD35_SRV
	// CAMD35 CLIENTS
	struct camd35_server_data *camd35 = cfg.camd35.server;
	while (camd35) {
		struct camd35_client_data *cli = camd35->cacheexclient;
		// Count Clients
		int counter = 0;
		while (cli) {
			if (cli->cacheex_mode) counter++;
			cli = cli->next;
		}
		if (counter) {
			sprintf( http_buf,"\n<tr><td class=alt3 colspan=9> Camd35 %d (%d)</td></tr>", camd35->id, counter); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			cli = camd35->cacheexclient;
			while (cli) {
				if (cli->cacheex_mode) {
					if (alt==1) alt=2; else alt=1;
					cacheex_camd35client_cells(cli,cell);
					/* R35: HTML rows render composed cells as-is; raw text fields are pre-escaped in the builders (escf/html_esc). Blanket escape here printed the page's own tags as text. */
					sprintf( http_buf,"\n<tr class=alt%d id=\"Row%d\" onMouseOver='setupdateRow(%d)' onMouseOut='setupdateRow(0)'> <td>%s</td><td>%s</td><td>%s</td><td>%s</td><td class=\"%s\">%s</td><td>%s</td><td>%s</td><td>%s</td><td>%s</td></tr>\n",alt,(cli->id+0x20000),(cli->id+0x20000),cell[0],cell[1],cell[2],cell[3],cell[9],cell[4],cell[5],cell[6],cell[7],cell[8]);
					tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				}
				cli = cli->next;
			}
		}
		camd35 = camd35->next;
	}
#endif

#ifdef CS378X_SRV
	// cs378x CLIENTS
	struct camd35_server_data *cs378x = cfg.cs378x.server;
	while (cs378x) {
		struct camd35_client_data *cli = cs378x->cacheexclient;
		// Count Clients
		int counter = 0;
		while (cli) {
			if (cli->cacheex_mode) counter++;
			cli = cli->next;
		}
		if (counter) {
			sprintf( http_buf,"\n<tr><td class=alt3 colspan=9> cs378x %d (%d)</td></tr>", cs378x->id, counter); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			cli = cs378x->cacheexclient;
			while (cli) {
				if (cli->cacheex_mode) {
					if (alt==1) alt=2; else alt=1;
					cacheex_camd35client_cells(cli,cell);
					/* R35: HTML rows render composed cells as-is; raw text fields are pre-escaped in the builders (escf/html_esc). Blanket escape here printed the page's own tags as text. */
					sprintf( http_buf,"\n<tr class=alt%d id=\"Row%d\" onMouseOver='setupdateRow(%d)' onMouseOut='setupdateRow(0)'> <td>%s</td><td>%s</td><td>%s</td><td>%s</td><td class=\"%s\">%s</td><td>%s</td><td>%s</td><td>%s</td><td>%s</td></tr>\n",alt,(cli->id+0x30000),(cli->id+0x30000),cell[0],cell[1],cell[2],cell[3],cell[9],cell[4],cell[5],cell[6],cell[7],cell[8]);
					tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				}
				cli = cli->next;
			}
		}
		cs378x = cs378x->next;
	}
#endif

	// CACHEEX Servers
	int counter = 0;
	struct server_data *srv = cfg.cacheexserver;
	while (srv) {
		if (!(srv->flags&FLAG_DELETE))
		if ( (srv->type==TYPE_CCCAM)
#ifdef CAMD35_CLI
			||(srv->type==TYPE_CAMD35)
#endif
#ifdef CS378X_CLI
			||(srv->type==TYPE_CS378X)
#endif
		)
		if (srv->cacheex_mode) counter++;
		srv = srv->next;
	}
	// test
	if (counter) {
		sprintf( http_buf,"\n<tr><td class=alt3 colspan=9> Servers (%d)</td></tr>", counter); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		struct server_data *srv = cfg.cacheexserver;
		while (srv) {
			if (!(srv->flags&FLAG_DELETE))
			if ( (srv->type==TYPE_CCCAM)
#ifdef CAMD35_CLI
				||(srv->type==TYPE_CAMD35)
#endif
#ifdef CS378X_CLI
				||(srv->type==TYPE_CS378X)
#endif
			)
			if (srv->cacheex_mode) {
				if (alt==1) alt=2; else alt=1;
				cacheex_server_cells(srv,cell);
				/* R35: HTML rows render composed cells as-is; raw text fields are pre-escaped in the builders (escf/html_esc). Blanket escape here printed the page's own tags as text. */
				sprintf( http_buf,"<tr class=alt%d id=\"Row%d\" onMouseOver='setupdateRow(%d)' onMouseOut='setupdateRow(0)'> <td>%s</td><td>%s</td><td>%s</td><td>%s</td><td class=\"%s\">%s</td><td>%s</td><td>%s</td><td>%s</td><td>%s</td></tr>\n",alt,(srv->id+0x40000),(srv->id+0x40000),cell[0],cell[1],cell[2],cell[3],cell[9],cell[4],cell[5],cell[6],cell[7],cell[8]);
				tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			}
			srv = srv->next;
		}
	}

	//
	sprintf( http_buf, "</table>");
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );

	//
	if (get_action==ACTION_PAGE) {
		tcp_writestr(&tcpbuf, sock, "</div>");
		tcp_writestr(&tcpbuf, sock, "</body></html>");
	}
	tcp_flush(&tcpbuf, sock);
}

#endif

///////////////////////////////////////////////////////////////////////////////

void http_send_cccam_client(int sock, http_request *req)
{
	char http_buf[2048];
	struct tcp_buffer_data tcpbuf;


	// Get Params
	char *str_action = isset_get( req, "action");
	char *str_id = isset_get( req, "id"); // Client ID
	char *str_name = isset_get( req, "name"); // Client NAME
	char *str_srvid = isset_get( req, "srvid"); // CCcam Server ID

	// Param 'action'
	int get_action;
	if (str_action) {
		if (!strcmp(str_action,"div")) get_action = ACTION_DIV;
		else if (!strcmp(str_action,"xml")) get_action = ACTION_XML; // Get Clients info in xml
		else if (!strcmp(str_action,"disable")) get_action = ACTION_DISABLE;
		else if (!strcmp(str_action,"enable")) get_action = ACTION_ENABLE;
		else if (!strcmp(str_action,"status")) get_action = ACTION_STATUS;
		else if (!strcmp(str_action,"debug")) get_action = ACTION_DEBUG;
		else str_action = NULL;
	}
	if (!str_action) { str_action = "page"; get_action = ACTION_PAGE; }

	/////////////////////////////////////////////

	// GET CLIENT
	struct cc_client_data *cli = NULL;
	if (str_id) cli = getcccamclientbyid( atoi(str_id) );
	else if (str_srvid && str_name) {
		struct cccam_server_data *cccam = getcccamserverbyid( atoi(str_srvid) );
		if (cccam) cli = getcccamclientbyname( cccam, str_name );
	}
	if (!cli) return;
	//

	if (get_action==ACTION_XML) {
		tcp_init(&tcpbuf);
		tcp_writestr(&tcpbuf, sock, "HTTP/1.1 200 OK\r\nAccept-Ranges: bytes\r\nConnection: close\r\nContent-Type: application/xml\r\n\r\n");
		tcp_writestr(&tcpbuf, sock, "<client>");
		uint32_t ticks = GetTickCount();
		sprintf(http_buf, "<id>%d</id>", cli->id); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		{ char hesc[512]; html_esc(hesc,(int)sizeof(hesc),cli->user);
		sprintf(http_buf, "<name>%s</name>", hesc); }
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		if (cli->connection.status>0) {
			tcp_writestr(&tcpbuf, sock, "<status>1</status>");
			sprintf( http_buf,"<ip>%s</ip>", (char*)ip2string(cli->ip) ); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			char *p = getcountrycodebyip(cli->ip);
			if (p) sprintf(http_buf, "<country>%s</country>", p); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			uint32_t d = (ticks - cli->connection.time)/1000;
			sprintf(http_buf, "<connected>%02dd %02d:%02d:%02d</connected>", d/(3600*24), (d/3600)%24, (d/60)%60, d%60); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			sprintf( http_buf,"<version>%s</version>", cli->version ); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			sprintf( http_buf,"<busy>%d</busy>", cli->ecm.busy ); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			d = (ticks - cli->lastactivity)/1000;
			sprintf(http_buf, "<lastactivity>%02dd %02d:%02d:%02d</lastactivity>", d/(3600*24), (d/3600)%24, (d/60)%60, d%60); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			if ( cli->lastecm.caid ) {
				sprintf(http_buf, "<lastshare>%04x:%06x:%04x</lastshare>",cli->lastecm.caid,cli->lastecm.prov,cli->lastecm.sid); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				sprintf(http_buf, "<lastsharestatus>%d</lastsharestatus>",cli->lastecm.status); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			}
		}
		else {
			sprintf(http_buf, "<status>%d</status>",cli->flags&0x0E);
			tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		}
		sprintf(http_buf, "<ecmnb>%d</ecmnb>", cli->ecmnb); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		sprintf(http_buf, "<ecmaccepted>%d</ecmaccepted>", cli->ecmnb-cli->ecmdenied); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		sprintf(http_buf, "<ecmok>%d</ecmok>", cli->ecmok); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		tcp_writestr(&tcpbuf, sock, "\n</client>");
		tcp_flush(&tcpbuf, sock);
		return;
	}
	else if (get_action==ACTION_DISABLE) {
		cli->flags |= FLAG_DISABLE;
		if (cli->connection.status>0) cc_disconnect_cli(cli);
		http_send_ok(sock);
		return;
	}
	else if (get_action==ACTION_ENABLE) {
		cli->flags &= ~FLAG_DISABLE;
		http_send_ok(sock);
		return;
	}
	else if (get_action==ACTION_DEBUG) {
		flagdebug = getdbgflag( DBG_CCCAM, cli->parent->id, cli->id);
		http_send_ok(sock);
		return;
	}
	else if (get_action==ACTION_STATUS) {
		if (cli->connection.status>0) http_send_text(sock,"connected"); else http_send_text(sock,"disconnected");
		return;
	}

	tcp_init(&tcpbuf);
	if (get_action==0) {
		http_replyok_write(req, &tcpbuf, sock);
		http_html_write(req, &tcpbuf, sock);
		tcp_write(&tcpbuf, sock, http_head, strlen(http_head) );
		sprintf( http_buf, html_title, cfg.http.title, "CCcam Client"); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		tcp_write(&tcpbuf, sock, http_link, strlen(http_link) );
		http_style_write(&tcpbuf, sock);
		// JS
		tcp_writestr(&tcpbuf, sock, "\n<script type='text/javascript'>");
		// UPD DIV
		char url[256];
		sprintf( url, "/cccamclient?action=div&id=%d", cli->id);
		sprintf( http_buf, HTTP_UPDATE_DIV, cfg.http.autorefresh*1000, url);
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		//
		tcp_writestr(&tcpbuf, sock, "\nfunction start()\n{\n	setautorefresh(autorefresh);\n}");
		tcp_writestr(&tcpbuf, sock, "\n</script>\n");
		tcp_write(&tcpbuf, sock, http_head_, strlen(http_head_) );
		tcp_writestr(&tcpbuf, sock, "<body onload=\"start();\">");
		tcp_write_menu(&tcpbuf, sock,0);
		// DIV
		tcp_writestr(&tcpbuf, sock, "<div id='mainDiv'>");
	}

	tcp_writestr(&tcpbuf, sock, "<br><table style=\"padding:0px; margin:0px;\" width=\"100%%\"><tbody>\n" );
	tcp_writestr(&tcpbuf, sock, "<tr><td style=\"vertical-align:top; width:400px;\">\n" );


	tcp_writestr(&tcpbuf, sock, "<table class=infotable><tbody>\n<tr><th colspan=2>Client Informations</th></tr>\n" );
	// NAME
	{ char hesc[512]; html_esc(hesc,(int)sizeof(hesc),cli->user);
sprintf( http_buf,"<tr><td class=left>User name</td><td class=right>%s</td></tr>\n",hesc); }
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	// Connection Time
	if (cli->connection.status>0) {
		tcp_writestr(&tcpbuf, sock, "<tr><td class=left>Status</td><td class=right>Connected</td></tr>\n");
		uint32_t d = (GetTickCount()-cli->connection.time)/1000;
		sprintf( http_buf,"<tr><td class=left>Connection time</td><td class=right>%02dd %02d:%02d:%02d</td></tr>\n", d/(3600*24), (d/3600)%24, (d/60)%60, d%60);
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		// IP
		sprintf( http_buf,"<tr><td class=left>IP Address</td><td class=right>%s</td></tr>\n",(char*)ip2string(cli->ip) );
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		// fd
		sprintf( http_buf,"<tr><td class=left>File Descriptor</td><td class=right>%d</td></tr>\n", cli->handle );
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		// CCcam Version
		sprintf( http_buf,"<tr><td class=left>CCcam Version</td><td class=right>%s</td></tr>",cli->version );
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	}
	else {
		tcp_writestr(&tcpbuf, sock, "<tr><td class=left>Status</td><td class=right>Disconnected</td></tr>\n");
		if (cli->connection.lastseen) {
			uint32_t d = (GetTickCount()-cli->connection.lastseen)/1000;
			sprintf( http_buf,"<tr><td class=left>Last Seen</td><td class=right>%02dd %02d:%02d:%02d</td></tr>\n", d/(3600*24),(d/3600)%24,(d/60)%60,d%60);
			tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		}
	}
	// UPTIME
	if ( cli->connection.uptime || (cli->connection.status>0) ) {
		uint32_t uptime;
		if (cli->connection.status>0) uptime = (GetTickCount()-cli->connection.time)+cli->connection.uptime; else uptime = cli->connection.uptime;
		uptime /= 1000;
		sprintf( http_buf,"<tr><td class=left>Uptime</td><td class=right>%02dd %02d:%02d:%02d</td></tr>",uptime/(3600*24),(uptime/3600)%24,(uptime/60)%60,uptime%60);
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	}
#ifdef CHECK_NEXTDCW
	sprintf( http_buf,"<tr><td class=left>DCW CHECK</td><td class=right>%s</td></tr>", yesno(cli->dcwcheck) );
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
#endif
/*
	if (cli->option.nodeid[0]) {
		sprintf( http_buf,"<tr><td class=left>NodeID</td><td class=right>%lx</td></tr>", cli->option.nodeid);
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	}
*/
	tcp_writestr(&tcpbuf, sock, "</tbody></table><br>\n" );


	// INFO
	struct client_info_data *info = cli->info;
	if (info) {
		tcp_writestr(&tcpbuf, sock, "<table class=\"infotable\"><tbody>\n" );
		tcp_writestr(&tcpbuf, sock, "<tr><th colspan=2>Additional Informations</th></tr>\n" );
		while (info) {
			sprintf( http_buf,"<tr><td class=left>%s</td><td class=right>%s</td></tr>\n",info->name,info->value);
			tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			info = info->next;
		}
		tcp_writestr(&tcpbuf, sock, "</tbody></table><br>\n" );
	}

	// Ecm Stat
	tcp_writestr(&tcpbuf, sock, "<table class=\"infotable\"><tbody>\n" );
	tcp_writestr(&tcpbuf, sock, "<tr><th colspan=2>ECM Statistics</th></tr>\n" );
	int ecmaccepted = cli->ecmnb-cli->ecmdenied;
	sprintf( http_buf, "<tr><td class=left>Total ECM requests</td><td class=right>%d</td></tr>\n", cli->ecmnb);
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	sprintf( http_buf, "<tr><td class=left>Accepted ECM requests</td><td class=right>%d</td></tr>\n", ecmaccepted);
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	sprintf( http_buf, "<tr><td class=left>Good ECM answer</td><td class=right>%d</td></tr>\n", cli->ecmok);
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	//Ecm Time
	if (cli->ecmok) {
		sprintf( http_buf,"<tr><td class=left>Average Time</td><td class=right>%d ms</td></tr>\n",(cli->ecmoktime/cli->ecmok) );
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	}
	// Freeze
	sprintf( http_buf,"<tr><td class=left>Total Freeze</td><td class=right>%d</td></tr>\n", cli->freeze);
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );

	sprintf( http_buf,"<tr><td class=left>Total Zap</td><td class=right>%d</td></tr>\n", cli->zap);
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );

	sprintf( http_buf,"<tr><td class=left>Total Logins</td><td class=right>%d</td></tr>\n", cli->nblogin);
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );

	sprintf( http_buf,"<tr><td class=left>Total Bad Logins</td><td class=right>%d</td></tr>\n", cli->nbloginerror);
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );

	sprintf( http_buf,"<tr><td class=left>Total differents ip logins</td><td class=right>%d</td></tr>\n", cli->nbdiffip);
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );

	sprintf( http_buf,"<tr><td class=left>Total DCW client Errors</td><td class=right>%d</td></tr>\n", cli->nbdcwerr);
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );

	tcp_writestr(&tcpbuf, sock, "</tbody></table><br>\n" );


	tcp_writestr(&tcpbuf, sock, "</td><td style=\"vertical-align:top;\">\n" );

	//Last Used Share
	if ( cli->lastecm.caid ) {
		tcp_writestr(&tcpbuf, sock, "<table class=\"infotable\"><tbody>\n" );
		tcp_writestr(&tcpbuf, sock, "<tr><th>Last Used share</th></tr>\n");
		// Decode Status
		if (cli->lastecm.status)
			sprintf( http_buf,"<tr><td>Decode success</td></tr>\n");
		else
			sprintf( http_buf,"<tr><td>Decode failed</td></tr>\n");
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		// Channel
		sprintf( http_buf,"<tr><td>Channel %s (%dms) %s</td></tr>\n", getchname(cli->lastecm.caid, cli->lastecm.prov, cli->lastecm.sid) , cli->lastecm.decodetime, str_laststatus[cli->lastecm.status] );
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );

		// Server
		if ( (GetTickCount()-cli->ecm.recvtime) < 20000 ) {
			// From ???
			if (cli->lastecm.status) {
				tcp_writestr(&tcpbuf, sock, "<tr><td>From ");
				src2string(cli->lastecm.dcwsrctype, cli->lastecm.dcwsrcid, http_buf );
				tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				tcp_writestr(&tcpbuf, sock, "</td></tr>");
			}
			// Last ECM
			ECM_DATA *ecm = cli->lastecm.request;
			// ECM
			sprintf( http_buf,"<tr><td>ECM(%d): ", ecm->ecmlen); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			array2hex( ecm->ecm, http_buf, ecm->ecmlen );	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			sprintf( http_buf,"</td></tr>\n"); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			// DCW
			if (cli->lastecm.status) {
				sprintf( http_buf,"<tr><td>CW: ");	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				array2hex( ecm->cw, http_buf, 16 );	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				sprintf( http_buf,"</td></tr>\n"); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			}
#ifdef CHECK_NEXTDCW
			if ( ecm->lastdecode.ecm && (ecm->lastdecode.counter>0) ) {
				sprintf( http_buf,"<tr><td>Previous CW: "); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				array2hex( ecm->lastdecode.dcw, http_buf, 16 ); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				tcp_writestr(&tcpbuf, sock, "</td></tr>\n");
				if (ecm->lastdecode.error) {
					sprintf( http_buf,"<tr><td>Errors = %d</td></tr>\n", ecm->lastdecode.error);
					tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				}
				sprintf( http_buf,"<tr><td>Total Cycles = %d</td></tr>\n<tr><td>ECM Interval = %ds</td></tr>\n", ecm->lastdecode.counter, ecm->lastdecode.dcwchangetime/1000);
				tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			}
#endif

			//
			if (ecm->server[0].srvid) {
				sprintf( http_buf, "<tr><td><table class='infotable'><tbody><tr><th width='30px'>ID</th><th width='250px'>Server</th><th width='50px'>Status</th><th width='70px'>Start time</th><th width='70px'>End time</th><th width='90px'>Elapsed time</th><th>CW</th></tr></tbody>");
				tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				int i;
				for(i=0; i<20; i++) {
					if (!ecm->server[i].srvid) break;
					char* str_srvstatus[] = { "WAIT", "OK", "NOK", "BUSY" };
					struct server_data *srv = getsrvbyid(ecm->server[i].srvid);
					if (srv) {
						sprintf( http_buf,"<tr><td>%d</td><td>%s:%d</td><td>%s</td><td>%dms</td>", i+1, srv->host->name, srv->port, str_srvstatus[ecm->server[i].flag], ecm->server[i].sendtime - ecm->recvtime );
						tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
						// Recv Time
						if (ecm->server[i].statustime>ecm->server[i].sendtime)
							sprintf( http_buf,"<td>%dms</td><td>%dms</td>", ecm->server[i].statustime - ecm->recvtime, ecm->server[i].statustime-ecm->server[i].sendtime );
						else
							sprintf( http_buf,"<td>--</td><td>--</td>");
						tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
						// DCW
						if (ecm->server[i].flag==ECM_SRV_REPLY_GOOD) {
							sprintf( http_buf,"<td>"); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
							array2hex( ecm->server[i].dcw, http_buf, 16 );	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
							sprintf( http_buf,"</td>"); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
						}
						else {
							sprintf( http_buf,"<td>--</td>");
							tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
						}
						sprintf( http_buf,"</tr>");
						tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
					}
				}
				tcp_writestr(&tcpbuf, sock, "</tbody></table></td></tr>\n" );
			}
		}
		tcp_writestr(&tcpbuf, sock, "</tbody></table><br>\n" );
	}

	// Current Busy Ecm
	if (cli->ecm.busy) {
		ECM_DATA *ecm = cli->ecm.request;
		if (ecm) http_send_ecmstatus(&tcpbuf, sock, ecm);
	}

	tcp_writestr(&tcpbuf, sock, "</td></tr></tbody></table>" );

	if (get_action==0) {
		tcp_writestr(&tcpbuf, sock, "</div>");
		tcp_writestr(&tcpbuf, sock, "</body></html>");
	}
	tcp_flush(&tcpbuf, sock);
}


#ifdef FREECCCAM_SRV

int freecccam_connectedclients()
{
	int nb=0;
	struct cc_client_data *cli=cfg.freecccam.server.client;
	while (cli) {
		if (cli->connection.status>0) nb++;
		cli=cli->next;
	}
	return nb;
}


void http_send_freecccam(int sock, http_request *req)
{
	char http_buf[4096];
	struct tcp_buffer_data tcpbuf;
	char cell[10][2048];

	char *str_clid = isset_get( req, "clid"); // Client ID
	if (str_clid) {
		int id = atoi(str_clid);
		struct cc_client_data *cli = cfg.freecccam.server.client;
		while (cli) {
			if ( cli->id==id ) {
				// Send XML CELLS
				getcccamcells(cli,cell);
				int i; for(i=0; i<10; i++) xmlescape( cell[i] );
				sprintf( http_buf, "<cccam>\n<c0>%s</c0>\n<c1>%s</c1>\n<c2_c>%s</c2_c>\n<c2>%s</c2>\n<c3>%s</c3>\n<c4>%s</c4>\n<c5>%s</c5>\n<c6>%s</c6>\n<c7>%s</c7>\n</cccam>\n",cell[2],cell[1],cell[9],cell[3],cell[4],cell[5],cell[6],cell[7],cell[8] );
				http_send_xml( sock, req, http_buf, strlen(http_buf));
				return;
			}			
			cli = cli->next;
		}
		return;
	}

	tcp_init(&tcpbuf);
	http_replyok_write(req, &tcpbuf, sock);
	http_html_write(req, &tcpbuf, sock);
	tcp_write(&tcpbuf, sock, http_head, strlen(http_head) );
	sprintf( http_buf, html_title, cfg.http.title, "FreeCCcam"); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	tcp_write(&tcpbuf, sock, http_link, strlen(http_link) );
	http_style_write(&tcpbuf, sock);
	// JS
	tcp_writestr(&tcpbuf, sock, "\n<script type='text/javascript'>");
	// UPD ROW
	tcp_writestr(&tcpbuf, sock, "\nfunction xmlupdateRow( xmlDoc, id )\n{\n	var row = document.getElementById(id);\n	row.cells.item(0).innerHTML = xmlDoc.getElementsByTagName('c0')[0].childNodes[0].nodeValue;\n	row.cells.item(1).innerHTML = xmlDoc.getElementsByTagName('c1')[0].childNodes[0].nodeValue;\n	row.cells.item(2).className = xmlDoc.getElementsByTagName('c2_c')[0].childNodes[0].nodeValue;\n	row.cells.item(2).innerHTML = xmlDoc.getElementsByTagName('c2')[0].childNodes[0].nodeValue;\n	row.cells.item(3).innerHTML = xmlDoc.getElementsByTagName('c3')[0].childNodes[0].nodeValue;\n	row.cells.item(4).innerHTML = xmlDoc.getElementsByTagName('c4')[0].childNodes[0].nodeValue;\n	row.cells.item(5).innerHTML = xmlDoc.getElementsByTagName('c5')[0].childNodes[0].nodeValue;\n	row.cells.item(6).innerHTML = xmlDoc.getElementsByTagName('c6')[0].childNodes[0].nodeValue;\n	row.cells.item(7).innerHTML = xmlDoc.getElementsByTagName('c7')[0].childNodes[0].nodeValue;\n}\n");
	char url[256];
	sprintf( url, "'/freecccam?action=row&clid='+idx");
	sprintf( http_buf, HTTP_UPDATE_ROW, url);
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	//
	tcp_writestr(&tcpbuf, sock, "\n</script>\n");

	tcp_write(&tcpbuf, sock, http_head_, strlen(http_head_) );
	tcp_write(&tcpbuf, sock, http_body, strlen(http_body) );
	tcp_write_menu(&tcpbuf, sock,PAGE_FREECCCAM);

	if (cfg.freecccam.server.handle>0) { sprintf( http_buf, "FreeCCcam Server [<font color=#00ff00>ENABLED</font>]");tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) ); }
	else {
		sprintf( http_buf, "FreeCCcam Server [<font color=#ff0000>DISABLED</font>]");
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		tcp_flush(&tcpbuf, sock);
		return;
	}

	sprintf( http_buf, "<br>Port = %d<br>Connected Clients: %d<br><center><table class=maintable width=100%%><tr><th width=200px>Client ip</th><th width=70px>version</th><th width=110px>connected</th><th width=60px>TotalEcm</th><th width=90px>AcceptedEcm</th><th width=90px>EcmOK</th><th width=50px>EcmTime</th><th>Last used share</th></tr>", cfg.freecccam.server.port, freecccam_connectedclients());
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );

	int alt=0;
	struct cc_client_data *cli = cfg.freecccam.server.client;

	while (cli) {
		if (cli->connection.status>0) {
			if (alt==1) alt=2; else alt=1;
			getcccamcells( cli,cell);
			sprintf( http_buf,"\n<tr class=alt%d id=\"Row%d\" onMouseOver='setupdateRow(%d)' onMouseOut='setupdateRow(0)'> <td>%s</td><td>%s</td><td class=\"%s\">%s</td><td align=center>%s</td><td>%s</td><td>%s</td><td align=center>%s</td><td>%s</td></tr>\n",alt,cli->id,cli->id,cell[2],cell[1],cell[9],cell[3],cell[4],cell[5],cell[6],cell[7],cell[8]);
			tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		}
		cli = cli->next;
	}

	sprintf( http_buf, "</table></center>");
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );

	tcp_flush(&tcpbuf, sock);
}

#endif

#endif




#ifdef MGCAMD_SRV

void getmgcamdcells(struct mg_client_data *cli, char cell[10][2048])
{
	char temp[2048];
	char et1[1024], et2[1024];	/* R35 text-field scratch */

	// CELL0 # NAME
	if (cli->realname)
		{ char hesc[512], resc[512];
		html_esc(hesc,(int)sizeof(hesc),cli->user);
		html_esc(resc,(int)sizeof(resc),cli->realname);
		sprintf( cell[0],"<a href='/mgcamdclient?id=%d'>%s<br>%s</a>",cli->id,hesc,resc); }
	else
		{ char hesc[512]; html_esc(hesc,(int)sizeof(hesc),cli->user);
	sprintf( cell[0],"<a href='/mgcamdclient?id=%d'>%s</a>",cli->id,hesc); }
	// CELL1 # PROGRAM ID
	if (cli->connection.status>0)
		sprintf( cell[1],"<span title='%04x'>%s</span>", cli->progid, programid(cli->progid));
	else strcpy( cell[1], " ");
	// CELL2 # IP
	if ( cli->ip ) { // Get Last IP
		char *p = getcountrycodebyip(cli->ip);
		if (cli->host)
			if (p) sprintf( cell[2],"<img src='/flag_%s.gif' title='%s'> %s<br>%s", p, getcountryname(p), (char*)ip2string(cli->ip), escf(et1,cli->host->name) ); else sprintf( cell[2],"%s<br>%s",(char*)ip2string(cli->ip), escf(et1,cli->host->name) );
		else
			if (p) sprintf( cell[2],"<img src='/flag_%s.gif' title='%s'> %s", p, getcountryname(p), (char*)ip2string(cli->ip) ); else sprintf( cell[2],"%s",(char*)ip2string(cli->ip) );
	}
	else strcpy( cell[2], " ");
	// CELL3 # Connection Time
	if (cli->connection.status>0) {
		if (cli->ecm.busy) sprintf( cell[9],"busy"); else sprintf( cell[9],"online");
		uint d = (GetTickCount()-cli->connection.time)/1000;
		sprintf( cell[3], "%02dd %02d:%02d:%02d", d/(3600*24), (d/3600)%24, (d/60)%60, d%60);
	}
	else {
		sprintf( cell[9],"offline");
		if (cli->flags&FLAG_DELETE) sprintf( cell[3],"Removed");
		else if (cli->flags&FLAG_EXPIRED) sprintf( cell[3],"Expired");
		else if (cli->flags&FLAG_DISABLE) sprintf( cell[3],"Disabled");
		else sprintf( cell[3],"offline");
	}
#ifdef EXPIREDATE
	if (cli->enddate.tm_year) {
		sprintf( temp,"<br>Expire: %d-%02d-%02d", 1900+cli->enddate.tm_year, cli->enddate.tm_mon+1, cli->enddate.tm_mday);
		strcat( cell[3], temp );
	}
#endif
	sprintf( temp, "<table class=\"connect_data\"><tr><td>Successful Login: %d</td><td>Aborted Connections: %d</td><td>Total Zapping: %d</td><td>Channel Freeze: %d</td></tr></table>", cli->nblogin, cli->nbloginerror, cli->zap, cli->freeze );
	strcat( cell[3], temp );
	// CELL4+5+6 # ECM STAT: TOTAL/ACCEPTED/OK
	// ECM STAT

#ifdef SRV_CSCACHE
	if (cli->cachedcw) sprintf( cell[4], "%d [%d]", cli->ecmnb, cli->cachedcw); else sprintf( cell[4], "%d", cli->ecmnb );
#else
	sprintf( cell[4], "%d", cli->ecmnb );
#endif

	int ecmaccepted = cli->ecmnb-cli->ecmdenied;
	getstatcell( ecmaccepted, cli->ecmnb, cell[5]);
	getstatcell( cli->ecmok, ecmaccepted, cell[6]);

	// CELL7 # Ecm Time
	if (cli->ecmok) sprintf( cell[7],"%d ms",(cli->ecmoktime/cli->ecmok) ); else sprintf( cell[7],"-- ms");

	// CELL8 # Last Used Share
	if ( cli->lastecm.caid ) {
		if (cli->lastecm.status)  strcpy( cell[8],"<span class=success"); else strcpy( cell[8],"<span class=failed");
		sprintf( temp," title='%04x:%06x:%04x'>ch %s (%dms) %s ",cli->lastecm.caid, cli->lastecm.prov, cli->lastecm.sid, getchname(cli->lastecm.caid, cli->lastecm.prov, cli->lastecm.sid) , cli->lastecm.decodetime, str_laststatus[cli->lastecm.status] );
		strcat( cell[8], temp );
		if ( (GetTickCount()-cli->ecm.recvtime) < 20000 ) {
			// From ???
			if (cli->lastecm.status) {
				strcat( cell[8], " / from ");
				src2string(cli->lastecm.dcwsrctype, cli->lastecm.dcwsrcid, temp);
				strcat( cell[8], temp);
			}
		}
		strcat( cell[8], "</span>" );
	}
	else strcpy( cell[8], " ");

	strcat( cell[8], "<span style='float:right;'>");
	if ( !(cli->flags&(FLAG_DELETE|FLAG_EXPIRED)) ) {
		if (cli->flags&FLAG_DISABLE) {
			sprintf( temp," <img title='Enable' src='enable.png' OnClick=\"imgrequest('/mgcamdclient?id=%d&action=enable',this);\">",cli->id);
			strcat( cell[8], temp );
		}
		else {
			sprintf( temp," <img title='disable' src='disable.png' OnClick=\"imgrequest('/mgcamdclient?id=%d&action=disable',this);\">",cli->id);
			strcat( cell[8], temp );
		}
	}
	sprintf( temp," <img title='Debug' src='debug.png' OnClick=\"imgrequest('/mgcamdclient?id=%d&action=debug',this);\">",cli->id);
	strcat( cell[8], temp );
	strcat( cell[8], "</span>");

}


int total_mgcamd_servers()
{
	int count=0;
	struct mgcamdserver_data *srv = cfg.mgcamd.server;
	while (srv) {
		count++;
		srv = srv->next;
	}
	return count;
}	

void mgcamd_clients( struct mgcamdserver_data *mgcamd, int *total, int *connected, int *active )
{
	*total = 0;
	*connected = 0;
	*active = 0;
	struct mg_client_data *cli = mgcamd->client;
	while (cli) {
		(*total)++;
		if (cli->connection.status>0) {
			(*connected)++;
			if ( (GetTickCount()-cli->lastecmtime) < 20000 ) (*active)++;
		}
		cli=cli->next;
	}
}

void total_mgcamd_clients( int *total, int *connected, int *active )
{
	*total = 0;
	*connected = 0;
	*active = 0;
	struct mgcamdserver_data *mgcamd = cfg.mgcamd.server;
	while (mgcamd) {
		struct mg_client_data *cli = mgcamd->client;
		while (cli) {
			(*total)++;
			if (cli->connection.status>0) {
				(*connected)++;
				if ( (GetTickCount()-cli->lastecmtime) < 20000 ) (*active)++;
			}
			cli=cli->next;
		}
		mgcamd = mgcamd->next;
	}
}


void http_send_mgcamd(int sock, http_request *req)
{
	char http_buf[2048];
	struct tcp_buffer_data tcpbuf;
	char cell[10][2048];

	// Get Params
	char *str_action = isset_get( req, "action");
	char *str_list = isset_get( req, "list");
	char *str_id = isset_get( req, "id"); // server ID
	char *str_clid = isset_get( req, "clid"); // Client ID
	char *str_clname = isset_get( req, "clname"); // Client NAME
	// Param 'action'
	int get_action;
	if (str_action) {
		if (!strcmp(str_action,"div")) get_action = ACTION_DIV;
		else if (!strcmp(str_action,"row")) get_action = ACTION_ROW;
		else if (!strcmp(str_action,"xml")) get_action = ACTION_XML; // Get Clients info in xml
		else if (!strcmp(str_action,"disable")) get_action = ACTION_DISABLE;
		else if (!strcmp(str_action,"enable")) get_action = ACTION_ENABLE;
		else if (!strcmp(str_action,"status")) get_action = ACTION_STATUS;
		else if (!strcmp(str_action,"debug")) get_action = ACTION_DEBUG;
		else str_action = NULL;
	}
	if (!str_action) { str_action = "page"; get_action = ACTION_PAGE; }

	/////////////////////////////////////////////

	if (get_action==ACTION_ROW) {
		// Check for XML ROW
		struct mg_client_data *cli = NULL;
		if (str_clid) {
			cli = getmgcamdclientbyid( atoi(str_clid) );
			if (!cli) return;
		}
		else {
			if (str_id && str_clname) {
				struct mgcamdserver_data *mgcamd = getmgcamdserverbyid( atoi(str_id) );
				if (!mgcamd) return;
				cli = getmgcamdclientbyname( mgcamd, str_clname );
				if (!cli) return;
			}
			else return;
		}
		// Send XML CELLS
		getmgcamdcells(cli,cell);
		int i; for(i=0; i<10; i++) xmlescape( cell[i] );
		char buf[5000] = "";
		sprintf( buf, "<mgcamd>\n<c0>%s</c0>\n<c1>%s</c1>\n<c2>%s</c2>\n<c3_c>%s</c3_c>\n<c3>%s</c3>\n<c4>%s</c4>\n<c5>%s</c5>\n<c6>%s</c6>\n<c7>%s</c7>\n<c8>%s</c8>\n</mgcamd>\n",cell[0],cell[1],cell[2],cell[9],cell[3],cell[4],cell[5],cell[6],cell[7],cell[8] );
		http_send_xml( sock, req, buf, strlen(buf));
		return;
	}			
	else if (get_action==ACTION_XML) {
		struct mgcamdserver_data *mgcamd = NULL;
		if (str_id) mgcamd = getmgcamdserverbyid( atoi(str_id) );
		tcp_init(&tcpbuf);
		tcp_writestr(&tcpbuf, sock, "HTTP/1.1 200 OK\r\nAccept-Ranges: bytes\r\nConnection: close\r\nContent-Type: application/xml\r\n\r\n");

		tcp_writestr(&tcpbuf, sock, "<multics>");

		struct mgcamdserver_data *srv;
		if (mgcamd) srv = mgcamd; else srv = cfg.mgcamd.server;
		while (srv) {
			tcp_writestr(&tcpbuf, sock, "\n<mgcamd>");
			sprintf(http_buf, "<id>%d</id>", srv->id); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			sprintf(http_buf, "<port>%d</port>", srv->port); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			sprintf(http_buf, "<status>%d</status>", (srv->handle>0) ); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			uint32_t ticks = GetTickCount();
			struct mg_client_data *cli = srv->client;
			while (cli) {
				tcp_writestr(&tcpbuf, sock, "<user>");
				{ char hesc[512]; html_esc(hesc,(int)sizeof(hesc),cli->user);
				sprintf(http_buf, "<name>%s</name>", hesc); }
				tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				if (cli->connection.status>0) {
					tcp_writestr(&tcpbuf, sock, "<status>1</status>");
					sprintf( http_buf,"<ip>%s</ip>", (char*)ip2string(cli->ip) ); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
					char *p = getcountrycodebyip(cli->ip);
					if (p) sprintf(http_buf, "<country>%s</country>", p); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
					uint32_t d = (ticks - cli->connection.time)/1000;
					sprintf(http_buf, "<connected>%02dd %02d:%02d:%02d</connected>", d/(3600*24), (d/3600)%24, (d/60)%60, d%60); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				}
				else {
					sprintf(http_buf, "<status>%d</status>",cli->flags&0x0E);
					tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				}
				tcp_writestr(&tcpbuf, sock, "</user>");
				cli = cli->next;
			}
			tcp_writestr(&tcpbuf, sock, "\n</mgcamd>");

			if (mgcamd) break; else srv = srv->next;
		}
		tcp_writestr(&tcpbuf, sock, "\n</multics>");
		tcp_flush(&tcpbuf, sock);
		return;
	}


	// Param 'id'
	int get_id = 0;
	if (str_id)	get_id = atoi(str_id);
	// Param 'list'
	int get_list = LIST_ACTIVE;
	if (str_list) {
		if (!strcmp(str_list,"connected")) get_list = LIST_CONNECTED;
		else if (!strcmp(str_list,"all")) get_list = LIST_ALL;
		else str_list = NULL;
	}
	if (!str_list) str_list = "active";
	//
	struct mgcamdserver_data *mgcamd = NULL;
	if (get_id) {
		mgcamd = getmgcamdserverbyid(get_id);
		if (!mgcamd) return;
	}

	tcp_init(&tcpbuf);
	if (get_action==ACTION_PAGE) {
		http_replyok_write(req, &tcpbuf, sock);
		http_html_write(req, &tcpbuf, sock);
		tcp_write(&tcpbuf, sock, http_head, strlen(http_head) );
		sprintf( http_buf, html_title, cfg.http.title, "MGcamd"); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		tcp_write(&tcpbuf, sock, http_link, strlen(http_link) );
		http_style_write(&tcpbuf, sock);
		// JS
		tcp_writestr(&tcpbuf, sock, "\n<script type='text/javascript'>");
		// ACTIONS REQUEST
		tcp_writestr(&tcpbuf, sock, "\nfunction imgrequest( url, el )\n{\n	var httpRequest;\n	try { httpRequest = new XMLHttpRequest(); }\n	catch (trymicrosoft) { try { httpRequest = new ActiveXObject('Msxml2.XMLHTTP'); } catch (oldermicrosoft) { try { httpRequest = new ActiveXObject('Microsoft.XMLHTTP'); } catch(failed) { httpRequest = false; } } }\n	if (!httpRequest) { alert('Your browser does not support Ajax.'); return false; }\n	if ( typeof(el)!='undefined' ) {\n		el.onclick = null;\n		el.style.opacity = '0.7';\n		httpRequest.onreadystatechange = function()\n		{\n			if (httpRequest.readyState == 4) if (httpRequest.status == 200) el.style.opacity = '0.3';\n		}\n	}\n	httpRequest.open('GET', url, true);\n	httpRequest.send(null);\n}\n");
		// UPD ROW
		tcp_writestr(&tcpbuf, sock, "\nfunction xmlupdateRow( xmlDoc, id )\n{\n	var row = document.getElementById(id);\n	row.cells.item(0).innerHTML = xmlDoc.getElementsByTagName('c0')[0].childNodes[0].nodeValue;\n	row.cells.item(1).innerHTML = xmlDoc.getElementsByTagName('c1')[0].childNodes[0].nodeValue;\n	row.cells.item(2).innerHTML = xmlDoc.getElementsByTagName('c2')[0].childNodes[0].nodeValue;\n	row.cells.item(3).className = xmlDoc.getElementsByTagName('c3_c')[0].childNodes[0].nodeValue;\n	row.cells.item(3).innerHTML = xmlDoc.getElementsByTagName('c3')[0].childNodes[0].nodeValue;\n	row.cells.item(4).innerHTML = xmlDoc.getElementsByTagName('c4')[0].childNodes[0].nodeValue;\n	row.cells.item(5).innerHTML = xmlDoc.getElementsByTagName('c5')[0].childNodes[0].nodeValue;\n	row.cells.item(6).innerHTML = xmlDoc.getElementsByTagName('c6')[0].childNodes[0].nodeValue;\n	row.cells.item(7).innerHTML = xmlDoc.getElementsByTagName('c7')[0].childNodes[0].nodeValue;\n	row.cells.item(8).innerHTML = xmlDoc.getElementsByTagName('c8')[0].childNodes[0].nodeValue;\n}\n");
		char url[256];
		sprintf( url, "'/mgcamd?action=row&clid='+idx");
		sprintf( http_buf, HTTP_UPDATE_ROW, url);
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		// UPD DIV
		sprintf( url, "/mgcamd?action=div&id=%d&list=%s", get_id, str_list);
		sprintf( http_buf, HTTP_UPDATE_DIV, cfg.http.autorefresh*1000, url);
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		//
		tcp_writestr(&tcpbuf, sock, "\nfunction start()\n{\n	setautorefresh(autorefresh);\n}");
		tcp_writestr(&tcpbuf, sock, "\n</script>\n");
		tcp_write(&tcpbuf, sock, http_head_, strlen(http_head_) );
		tcp_writestr(&tcpbuf, sock, "<body onload=\"start();\">");
		tcp_write_menu(&tcpbuf, sock,PAGE_MGCAMD);
		// DIV
		tcp_writestr(&tcpbuf, sock, "<div id='mainDiv'>");
	}

	tcp_writestr(&tcpbuf, sock, "<table style=\"margin:0px; padding:0px; border-width:0px; border-spacing: 1px;\"><tr>");
	tcp_writestr(&tcpbuf, sock, "<td style=\"margin:0px; padding:0px;\"><a href='/mgcamd'><table border=1 width=200px>");
	// Total Servers
	sprintf( http_buf, "<tr><td>Total Mgcamd Servers: %d</td></tr>", total_mgcamd_servers() ); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	int total, connected, active;
	total_mgcamd_clients( &total, &connected, &active );
	sprintf( http_buf, "<tr><td>Connected Clients: %d / %d</td></tr>", connected, total ); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	tcp_writestr(&tcpbuf, sock, "</table></a></td>");
	// Servers
	if (cfg.mgcamd.server) {
		int counter = 0;
		struct mgcamdserver_data *mgcamd = cfg.mgcamd.server;
		while ( mgcamd && (counter<5) ) {
			sprintf( http_buf, "<td style=\"margin:0px; padding: 0px;\"><a href='/mgcamd?id=%d'><table border=1 width=150px>", mgcamd->id);
			tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			if (mgcamd->handle>0) sprintf( http_buf, "<tr><td>mgcamd %d [<span class=success>%d</span>]</td></tr>", mgcamd->id, mgcamd->port);
			else sprintf( http_buf, "<tr><td>mgcamd %d [<span class=failed>%d</span>]</td></tr>", mgcamd->id, mgcamd->port);
			tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			int total, connected, active;
			mgcamd_clients( mgcamd, &total, &connected, &active );
			sprintf( http_buf, "<tr><td>Connected: %d / %d</td></tr>", connected, total);
			tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			tcp_writestr(&tcpbuf, sock, "</table></a></td>");
			counter++;
			mgcamd = mgcamd->next;
		}
	}
	// End of table
	tcp_writestr(&tcpbuf, sock, "</tr></table><br>");

	tcp_writestr(&tcpbuf, sock, "<select style=\"width:200px;\" onchange=\"parent.location.href='/mgcamd?id='+this.value\">");
	sprintf( http_buf, "<option value=0>ALL (%d)</option>", total_mgcamd_servers());
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	struct mgcamdserver_data *tmp = cfg.mgcamd.server;
	while (tmp) {
		if (get_id==tmp->id) sprintf( http_buf, "<option value=%d selected>[%d] mgcamd %d</option>",tmp->id,tmp->port, tmp->id );
		else sprintf( http_buf, "<option value=%d>[%d] mgcamd %d</option>",tmp->id,tmp->port, tmp->id );
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		tmp = tmp->next;
	}
	tcp_writestr(&tcpbuf, sock, "</select> ");
	//
	if (mgcamd) mgcamd_clients( mgcamd, &total, &connected, &active ); else total_mgcamd_clients( &total, &connected, &active );
	char *class1 = "button"; char *class2 = "sbutton";
	char *class;
	if (get_list==LIST_ACTIVE) class = class2; else class = class1;
	sprintf( http_buf, "<input type=button class=%s onclick=\"parent.location='/mgcamd?id=%d&amp;list=active'\" value='Active Clients (%d)'>", class, get_id, active);
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	if (get_list==LIST_CONNECTED) class = class2; else class = class1;
	sprintf( http_buf, " <input type=button class=%s onclick=\"parent.location='/mgcamd?id=%d&amp;list=connected'\" value='Connected Clients (%d)'>", class, get_id, connected);
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	if (get_list==LIST_ALL) class = class2; else class = class1;
	sprintf( http_buf, " <input type=button class=%s onclick=\"parent.location='/mgcamd?id=%d&amp;list=all'\" value='All Clients (%d)'>", class, get_id, total);
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	//
	if (get_id) { // One Server Selected
		// Table
		sprintf( http_buf, "\n<table class=maintable width=100%%><tr><th width=100px>Client</th><th width=70px>version</th><th width=120px>ip</th><th width=110px>Connected</th><th width=60px>TotalEcm</th><th width=90px>AcceptedEcm</th><th width=90px>EcmOK</th><th width=50px>EcmTime</th><th>Last used share</th></tr>");
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		struct mg_client_data *cli = mgcamd->client;
		int alt=0;
		if (get_list==LIST_ACTIVE) {
			while (cli) {
				if ( (cli->connection.status>0)&&((GetTickCount()-cli->lastecmtime) < 20000) ) {
					if (alt==1) alt=2; else alt=1;
					getmgcamdcells(cli,cell);
					/* R35: HTML rows render composed cells as-is; raw text fields are pre-escaped in the builders (escf/html_esc). Blanket escape here printed the page's own tags as text. */
					sprintf( http_buf,"\n<tr id=\"Row%d\" class=alt%d onMouseOver='setupdateRow(%d)' onMouseOut='setupdateRow(0)'> <td>%s</td><td>%s</td><td>%s</td><td class=\"%s\">%s</td><td align=center>%s</td><td>%s</td><td>%s</td><td align=center>%s</td><td>%s</td></tr>\n",cli->id,alt,cli->id,cell[0],cell[1],cell[2],cell[9],cell[3],cell[4],cell[5],cell[6],cell[7],cell[8]);
					tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				}
				cli = cli->next;
			}
		}
		else if (get_list==LIST_CONNECTED) {
			while (cli) {
				if (cli->connection.status>0) {
					if (alt==1) alt=2; else alt=1;
					getmgcamdcells(cli,cell);
					/* R35: HTML rows render composed cells as-is; raw text fields are pre-escaped in the builders (escf/html_esc). Blanket escape here printed the page's own tags as text. */
					sprintf( http_buf,"\n<tr id=\"Row%d\" class=alt%d onMouseOver='setupdateRow(%d)' onMouseOut='setupdateRow(0)'> <td>%s</td><td>%s</td><td>%s</td><td class=\"%s\">%s</td><td align=center>%s</td><td>%s</td><td>%s</td><td align=center>%s</td><td>%s</td></tr>\n",cli->id,alt,cli->id,cell[0],cell[1],cell[2],cell[9],cell[3],cell[4],cell[5],cell[6],cell[7],cell[8]);
					tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				}
				cli = cli->next;
			}
		}
		else { // ALL
			while (cli) {
				if (alt==1) alt=2; else alt=1;
				getmgcamdcells(cli,cell);
				/* R35: HTML rows render composed cells as-is; raw text fields are pre-escaped in the builders (escf/html_esc). Blanket escape here printed the page's own tags as text. */
				sprintf( http_buf,"\n<tr id=\"Row%d\" class=alt%d onMouseOver='setupdateRow(%d)' onMouseOut='setupdateRow(0)'> <td>%s</td><td>%s</td><td>%s</td><td class=\"%s\">%s</td><td align=center>%s</td><td>%s</td><td>%s</td><td align=center>%s</td><td>%s</td></tr>\n",cli->id,alt,cli->id,cell[0],cell[1],cell[2],cell[9],cell[3],cell[4],cell[5],cell[6],cell[7],cell[8]);
				tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				cli = cli->next;
			}
		}
		sprintf( http_buf, "\n</table>");
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	}

	else {
		// Table
		tcp_writestr(&tcpbuf,sock, "\n<table class=maintable width=100%>");
		tcp_writestr(&tcpbuf,sock, "\n<tr><th width=100px>Client</th><th width=70px>version</th><th width=120px>ip</th><th width=110px>Connected</th><th width=60px>TotalEcm</th><th width=90px>AcceptedEcm</th><th width=90px>EcmOK</th><th width=50px>EcmTime</th><th>Last used share</th></tr>");
		int alt=0;
		mgcamd = cfg.mgcamd.server;
		while (mgcamd) {
			int total, connected, active;
			mgcamd_clients( mgcamd, &total, &connected, &active );
			if ( (get_list==LIST_ACTIVE) && active ) {
				sprintf( http_buf,"\n<tr><td class=alt3 colspan=9> mgcamd %d (%d)</td></tr>", mgcamd->id, active); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				struct mg_client_data *cli = mgcamd->client;
				while (cli) {
					if ( (cli->connection.status>0)&&((GetTickCount()-cli->lastecmtime) < 20000) ) {
						if (alt==1) alt=2; else alt=1;
						getmgcamdcells(cli,cell);
						/* R35: HTML rows render composed cells as-is; raw text fields are pre-escaped in the builders (escf/html_esc). Blanket escape here printed the page's own tags as text. */
						sprintf( http_buf,"\n<tr id=\"Row%d\" class=alt%d onMouseOver='setupdateRow(%d)' onMouseOut='setupdateRow(0)'> <td>%s</td><td>%s</td><td>%s</td><td class=\"%s\">%s</td><td align=center>%s</td><td>%s</td><td>%s</td><td align=center>%s</td><td>%s</td></tr>\n",cli->id,alt,cli->id,cell[0],cell[1],cell[2],cell[9],cell[3],cell[4],cell[5],cell[6],cell[7],cell[8]);
						tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
					}
					cli = cli->next;
				}
			}
			else if ( (get_list==LIST_ALL) && total ) {
				sprintf( http_buf,"\n<tr><td class=alt3 colspan=9> mgcamd %d (%d)</td></tr>", mgcamd->id, total); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				struct mg_client_data *cli = mgcamd->client;
				while (cli) {
					if (alt==1) alt=2; else alt=1;
					getmgcamdcells(cli,cell);
					/* R35: HTML rows render composed cells as-is; raw text fields are pre-escaped in the builders (escf/html_esc). Blanket escape here printed the page's own tags as text. */
					sprintf( http_buf,"\n<tr id=\"Row%d\" class=alt%d onMouseOver='setupdateRow(%d)' onMouseOut='setupdateRow(0)'> <td>%s</td><td>%s</td><td>%s</td><td class=\"%s\">%s</td><td align=center>%s</td><td>%s</td><td>%s</td><td align=center>%s</td><td>%s</td></tr>\n",cli->id,alt,cli->id,cell[0],cell[1],cell[2],cell[9],cell[3],cell[4],cell[5],cell[6],cell[7],cell[8]);
					tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
					cli = cli->next;
				}
			}
			else if ( (get_list==LIST_CONNECTED) && connected ) {
				sprintf( http_buf,"\n<tr><td class=alt3 colspan=9> mgcamd %d (%d)</td></tr>", mgcamd->id, connected); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				struct mg_client_data *cli = mgcamd->client;
				while (cli) {
					if (cli->connection.status>0) {
						if (alt==1) alt=2; else alt=1;
						getmgcamdcells(cli,cell);
						/* R35: HTML rows render composed cells as-is; raw text fields are pre-escaped in the builders (escf/html_esc). Blanket escape here printed the page's own tags as text. */
						sprintf( http_buf,"\n<tr id=\"Row%d\" class=alt%d onMouseOver='setupdateRow(%d)' onMouseOut='setupdateRow(0)'> <td>%s</td><td>%s</td><td>%s</td><td class=\"%s\">%s</td><td align=center>%s</td><td>%s</td><td>%s</td><td align=center>%s</td><td>%s</td></tr>\n",cli->id,alt,cli->id,cell[0],cell[1],cell[2],cell[9],cell[3],cell[4],cell[5],cell[6],cell[7],cell[8]);
						tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
					}
					cli = cli->next;
				}
			}
			mgcamd = mgcamd->next;
		}
		sprintf( http_buf, "</table>");
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	}
	if (get_action==ACTION_PAGE) {
		tcp_writestr(&tcpbuf, sock, "</div>");
		tcp_writestr(&tcpbuf, sock, "</body></html>");
	}

	tcp_flush(&tcpbuf, sock);
}


///////////////////////////////////////////////////////////////////////////////

void http_send_mgcamd_client(int sock, http_request *req)
{
	char http_buf[2048];
	struct tcp_buffer_data tcpbuf;
	// Get Client ID
	char *str_id = isset_get( req, "id");
	if (!str_id) return; //error
	int get_id = atoi(str_id);
	struct mg_client_data *cli = getmgcamdclientbyid(get_id);
	if (!cli) return;
	// Action
	char *str_action = isset_get( req, "action");
	int get_action = 0;
	if (str_action) {
		if (!strcmp(str_action,"div")) get_action = 1;
		else if (!strcmp(str_action,"row")) get_action = 2;
		else if (!strcmp(str_action,"disable")) get_action = 3;
		else if (!strcmp(str_action,"enable")) get_action = 4;
		else if (!strcmp(str_action,"status")) get_action = 5;
		else if (!strcmp(str_action,"info")) get_action = 6; // XML info
		else if (!strcmp(str_action,"debug")) get_action = 7;
		else str_action = NULL;
	}
	if (!str_action) str_action = "page";
	//
	if (get_action==3) {
		cli->flags |= FLAG_DISABLE;
		if (cli->connection.status>0) mg_disconnect_cli(cli);
		http_send_ok(sock);
		return;
	}
	else if (get_action==4) {
		cli->flags &= ~FLAG_DISABLE;
		http_send_ok(sock);
		return;
	}
	else if (get_action==5) {
		if (cli->connection.status>0) http_send_text(sock,"connected"); else http_send_text(sock,"disconnected");
		return;
	}
	else if (get_action==7) {
		flagdebug = getdbgflag( DBG_MGCAMD, 0, cli->id);
		http_send_ok(sock);
		return;
	}
	//
	tcp_init(&tcpbuf);
	if (get_action==0) {
		http_replyok_write(req, &tcpbuf, sock);
		http_html_write(req, &tcpbuf, sock);
		tcp_write(&tcpbuf, sock, http_head, strlen(http_head) );
		sprintf( http_buf, html_title, cfg.http.title, "Mgcamd Client"); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		tcp_write(&tcpbuf, sock, http_link, strlen(http_link) );
		http_style_write(&tcpbuf, sock);
		// JS
		tcp_writestr(&tcpbuf, sock, "\n<script type='text/javascript'>");
		// UPD DIV
		char url[256];
		sprintf( url, "/mgcamdclient?id=%d&action=div", get_id);
		sprintf( http_buf, HTTP_UPDATE_DIV, cfg.http.autorefresh*1000, url);
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		//
		tcp_writestr(&tcpbuf, sock, "\nfunction start()\n{\n	setautorefresh(autorefresh);\n}");
		tcp_writestr(&tcpbuf, sock, "\n</script>\n");
		tcp_write(&tcpbuf, sock, http_head_, strlen(http_head_) );
		tcp_writestr(&tcpbuf, sock, "<body onload=\"start();\">");
		tcp_write_menu(&tcpbuf, sock,0);
		// DIV
		tcp_writestr(&tcpbuf, sock, "<div id='mainDiv'>");
	}

	tcp_writestr(&tcpbuf, sock, "<table style=\"padding:0px; margin:0px;\" width=\"100%%\"><tbody>\n" );
	tcp_writestr(&tcpbuf, sock, "<tr><td style=\"vertical-align:top; width:400px;\">\n" );

	tcp_writestr(&tcpbuf, sock, "<table class=infotable><tbody>\n<tr><th colspan=2>Client Informations</th></tr>\n" );
	// NAME
	{ char hesc[512]; html_esc(hesc,(int)sizeof(hesc),cli->user);
sprintf( http_buf,"<tr><td class=left>User name</td><td class=right>%s</td></tr>\n",hesc); }
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	// Connection Time
	if (cli->connection.status>0) {
		tcp_writestr(&tcpbuf, sock, "<tr><td class=left>Status</td><td class=right>Connected</td></tr>\n");
		uint32_t d = (GetTickCount()-cli->connection.time)/1000;
		sprintf( http_buf,"<tr><td class=left>Connection time</td><td class=right>%02dd %02d:%02d:%02d</td></tr>\n", d/(3600*24), (d/3600)%24, (d/60)%60, d%60);
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		// IP
		sprintf( http_buf,"<tr><td class=left>IP Address</td><td class=right>%s</td></tr>\n",(char*)ip2string(cli->ip) );
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		// Program ID
		sprintf( http_buf,"<tr><td class=left>Client Program</td><td class=right>%s(%04x)</td></tr>",programid(cli->progid), cli->progid );
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	}
	else {
		tcp_writestr(&tcpbuf, sock, "<tr><td class=left>Status</td><td class=right>Disconnected</td></tr>\n");
		if ( cli->connection.lastseen ) {
			uint32_t d = (GetTickCount()-cli->connection.lastseen)/1000;
			sprintf( http_buf,"<tr><td class=left>Last Seen</td><td class=right>%02dd %02d:%02d:%02d</td></tr>\n", d/(3600*24),(d/3600)%24,(d/60)%60,d%60);
			tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		}
	}
	// UPTIME
	if ( cli->connection.uptime || (cli->connection.status>0) ) {
		uint32_t uptime;
		if (cli->connection.status>0) uptime = (GetTickCount()-cli->connection.time)+cli->connection.uptime; else uptime = cli->connection.uptime;
		uptime /= 1000;
		sprintf( http_buf,"<tr><td class=left>Uptime</td><td class=right>%02dd %02d:%02d:%02d</td></tr>",uptime/(3600*24),(uptime/3600)%24,(uptime/60)%60,uptime%60);
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	}
#ifdef CHECK_NEXTDCW
	sprintf( http_buf,"<tr><td class=left>DCW CHECK</td><td class=right>%s</td></tr>", yesno(cli->dcwcheck) );
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
#endif
	tcp_writestr(&tcpbuf, sock, "</tbody></table><br>\n" );


	// INFO
	struct client_info_data *info = cli->info;
	if (info) {
		tcp_writestr(&tcpbuf, sock, "<table class=\"infotable\"><tbody>\n" );
		tcp_writestr(&tcpbuf, sock, "<tr><th colspan=2>Additional Informations</th></tr>\n" );
		while (info) {
			sprintf( http_buf,"<tr><td class=left>%s</td><td class=right>%s</td></tr>\n",info->name,info->value);
			tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			info = info->next;
		}
		tcp_writestr(&tcpbuf, sock, "</tbody></table><br>\n" );
	}

	// Ecm Stat
	tcp_writestr(&tcpbuf, sock, "<table class=\"infotable\"><tbody>\n" );
	tcp_writestr(&tcpbuf, sock, "<tr><th colspan=2>ECM Statistics</th></tr>\n" );
	int ecmaccepted = cli->ecmnb-cli->ecmdenied;
	sprintf( http_buf, "<tr><td class=left>Total ECM requests</td><td class=right>%d</td></tr>\n", cli->ecmnb);
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	sprintf( http_buf, "<tr><td class=left>Accepted ECM requests</td><td class=right>%d</td></tr>\n", ecmaccepted);
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	sprintf( http_buf, "<tr><td class=left>Good ECM answer</td><td class=right>%d</td></tr>\n", cli->ecmok);
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	//Ecm Time
	if (cli->ecmok) {
		sprintf( http_buf,"<tr><td class=left>Average Time</td><td class=right>%d ms</td></tr>\n",(cli->ecmoktime/cli->ecmok) );
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	}
#ifdef SRV_CSCACHE
	sprintf( http_buf, "<tr><td class=left>Cached CW</td><td class=right>%d</td></tr>\n", cli->cachedcw);
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
#endif
	// Freeze
	sprintf( http_buf,"<tr><td class=left>Total Freeze</td><td class=right>%d</td></tr>\n", cli->freeze);
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	tcp_writestr(&tcpbuf, sock, "</tbody></table><br>\n" );


	tcp_writestr(&tcpbuf, sock, "</td><td style=\"vertical-align:top;\">\n" );

	//Last Used Share
	if ( cli->lastecm.caid ) {
		tcp_writestr(&tcpbuf, sock, "<table class=\"infotable\"><tbody>\n" );
		tcp_writestr(&tcpbuf, sock, "<tr><th>Last Used share</th></tr>\n");
		// Decode Status
		if (cli->lastecm.status)
			sprintf( http_buf,"<tr><td>Decode success</td></tr>\n");
		else
			sprintf( http_buf,"<tr><td>Decode failed</td></tr>\n");
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		// Channel
		sprintf( http_buf,"<tr><td>Channel %s (%dms) %s</td></tr>\n", getchname(cli->lastecm.caid, cli->lastecm.prov, cli->lastecm.sid) , cli->lastecm.decodetime, str_laststatus[cli->lastecm.status] );
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );

		// Server
		if ( (GetTickCount()-cli->ecm.recvtime) < 20000 ) {
			// From ???
			if (cli->lastecm.status) {
				tcp_writestr(&tcpbuf, sock, "<tr><td>From ");
				src2string(cli->lastecm.dcwsrctype, cli->lastecm.dcwsrcid, http_buf );
				tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				tcp_writestr(&tcpbuf, sock, "</td></tr>");
			}
			// Last ECM
			ECM_DATA *ecm = cli->lastecm.request;
			// ECM
			sprintf( http_buf,"<tr><td>ECM(%d): ", ecm->ecmlen); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			array2hex( ecm->ecm, http_buf, ecm->ecmlen );	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			sprintf( http_buf,"</td></tr>\n"); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			// DCW
			if (cli->lastecm.status) {
				sprintf( http_buf,"<tr><td>CW: ");	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				array2hex( ecm->cw, http_buf, 16 );	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				sprintf( http_buf,"</td></tr>\n"); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			}
#ifdef CHECK_NEXTDCW
			if ( ecm->lastdecode.ecm && (ecm->lastdecode.counter>0) ) {
				sprintf( http_buf,"<tr><td>Previous CW: "); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				array2hex( ecm->lastdecode.dcw, http_buf, 16 ); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				tcp_writestr(&tcpbuf, sock, "</td></tr>\n");
				if (ecm->lastdecode.error) {
					sprintf( http_buf,"<tr><td>Errors = %d</td></tr>\n", ecm->lastdecode.error);
					tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				}
				sprintf( http_buf,"<tr><td>Total Cycles = %d</td></tr>\n<tr><td>ECM Interval = %ds</td></tr>\n", ecm->lastdecode.counter, ecm->lastdecode.dcwchangetime/1000);
				tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			}
#endif
			//
			if (ecm->server[0].srvid) {
				sprintf( http_buf, "<tr><td><table class='infotable'><tbody><tr><th width='30px'>ID</th><th width='250px'>Server</th><th width='50px'>Status</th><th width='70px'>Start time</th><th width='70px'>End time</th><th width='90px'>Elapsed time</th><th>CW</th></tr></tbody>");
				tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
				int i;
				for(i=0; i<20; i++) {
					if (!ecm->server[i].srvid) break;
					char* str_srvstatus[] = { "WAIT", "OK", "NOK", "BUSY" };
					struct server_data *srv = getsrvbyid(ecm->server[i].srvid);
					if (srv) {
						sprintf( http_buf,"<tr><td>%d</td><td>%s:%d</td><td>%s</td><td>%dms</td>", i+1, srv->host->name, srv->port, str_srvstatus[ecm->server[i].flag], ecm->server[i].sendtime - ecm->recvtime );
						tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
						// Recv Time
						if (ecm->server[i].statustime>ecm->server[i].sendtime)
							sprintf( http_buf,"<td>%dms</td><td>%dms</td>", ecm->server[i].statustime - ecm->recvtime, ecm->server[i].statustime-ecm->server[i].sendtime );
						else
							sprintf( http_buf,"<td>--</td><td>--</td>");
						tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
						// DCW
						if (ecm->server[i].flag==ECM_SRV_REPLY_GOOD) {
							sprintf( http_buf,"<td>"); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
							array2hex( ecm->server[i].dcw, http_buf, 16 );	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
							sprintf( http_buf,"</td>"); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
						}
						else {
							sprintf( http_buf,"<td>--</td>");
							tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
						}
						sprintf( http_buf,"</tr>");
						tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
					}
				}
				tcp_writestr(&tcpbuf, sock, "</tbody></table></td></tr>\n" );
			}
		}
		tcp_writestr(&tcpbuf, sock, "</tbody></table><br>\n" );
	}

	// Current Busy Ecm
	if (cli->ecm.busy) {
		ECM_DATA *ecm = cli->ecm.request;
		if (ecm) http_send_ecmstatus(&tcpbuf, sock, ecm);
	}

	tcp_writestr(&tcpbuf, sock, "</td></tr></tbody></table>" );

	if (get_action==0) {
		tcp_writestr(&tcpbuf, sock, "</div>");
		tcp_writestr(&tcpbuf, sock, "</body></html>");
	}
	tcp_flush(&tcpbuf, sock);
}


#endif



void http_send_host(int sock, http_request *req)
{
	char http_buf[2048];
	struct tcp_buffer_data tcpbuf;
	tcp_init(&tcpbuf);
	http_replyok_write(req, &tcpbuf, sock);
	http_html_write(req, &tcpbuf, sock);
	tcp_write(&tcpbuf, sock, http_head, strlen(http_head) );
	sprintf( http_buf, html_title, cfg.http.title, "Host"); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	tcp_write(&tcpbuf, sock, http_link, strlen(http_link) );
	http_style_write(&tcpbuf, sock);
	tcp_write(&tcpbuf, sock, http_head_, strlen(http_head_) );
	tcp_write(&tcpbuf, sock, http_body, strlen(http_body) );
	tcp_write_menu(&tcpbuf, sock,0);

	sprintf( http_buf, "<table class=maintable width=100%%><tr><th width=200px>HostName</th><th width=70px>IP</th><th width=100px>Check Time (sec)</th></tr>");
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );

	struct host_data *host = cfg.host;
	int alt=0;
	while (host) {
		if (alt==1) alt=2; else alt=1;
		sprintf( http_buf,"<tr class=alt%d><td>%s</td><td>%s<br>%s</td><td>%d</td>",alt, host->name, (char*)ip2string(host->ip),(char*)ip2string(host->clip), host->checkiptime-getseconds() );
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		host = host->next;
	}

	sprintf( http_buf, "</table>");
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );

	tcp_flush(&tcpbuf, sock);
}


void http_send_threads(int sock, http_request *req)
{
        char http_buf[2048];
        struct tcp_buffer_data tcpbuf;
        tcp_init(&tcpbuf);
        http_replyok_write(req, &tcpbuf, sock);
        http_html_write(req, &tcpbuf, sock);
        tcp_write(&tcpbuf, sock, http_head, strlen(http_head) );
        sprintf( http_buf, html_title, cfg.http.title, "threads"); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
        tcp_write(&tcpbuf, sock, http_link, strlen(http_link) );
        http_style_write(&tcpbuf, sock);
        tcp_write(&tcpbuf, sock, http_head_, strlen(http_head_) );
        tcp_write(&tcpbuf, sock, http_body, strlen(http_body) );
		tcp_write_menu(&tcpbuf, sock,0);


		tcp_writestr(&tcpbuf, sock, "<br>Load Average = ");
		FILE *fp = fopen ("/proc/loadavg", "r");
		fgets(http_buf, sizeof(http_buf), fp);
		fclose(fp);
		tcp_writestr(&tcpbuf, sock, http_buf);

		// /proc/meminfo 
		fp = fopen ("/proc/meminfo", "r");
		fgets(http_buf, sizeof(http_buf), fp); tcp_writestr(&tcpbuf, sock, http_buf);
		fgets(http_buf, sizeof(http_buf), fp); tcp_writestr(&tcpbuf, sock, http_buf);
		fclose(fp);

		sprintf( http_buf, "<br>THREADID Main = %d",prg.pid_main ); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		sprintf( http_buf, "<br>THREADID Config = %d",prg.pid_cfg ); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		sprintf( http_buf, "<br> THREADID DNS = %d",prg.pid_dns ); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		sprintf( http_buf, "<br> THREADID Servers Connections = %d",prg.pid_srv ); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		sprintf( http_buf, "<br> THREADID Recv Messages = %d",prg.pid_msg ); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		sprintf( http_buf, "<br> THREADID SET DCW = %d",prg.pid_setdcw ); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		sprintf( http_buf, "<br> THREADID Cache = %d",prg.pid_cache ); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		sprintf( http_buf, "<br> THREADID Date = %d",prg.pid_date ); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );

		sprintf( http_buf, "<br> THREADID Connect Clients = %d",prg.pid_connect ); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );

		sprintf( http_buf, "<br> THREADID Newcamd messages = %d",prg.pid_cs_msg ); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		sprintf( http_buf, "<br> THREADID Mgcamd messages = %d",cfg.mgcamd.pid_recvmsg ); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		sprintf( http_buf, "<br> THREADID CCcam messages = %d",cfg.cccam.pid_recvmsg ); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
#ifdef CS378X_SRV
		sprintf( http_buf, "<br> THREADID CS378X messages = %d",cfg.cs378x.pid_recvmsg ); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
#endif
#ifdef CACHEEX
        sprintf( http_buf, "<br> THREADID Ccacheex messages = %d",prg.pid_ccex_msg ); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
#endif

		// CACHEEX Servers
#ifdef CACHEEX
		struct server_data *srv = cfg.server;
		while (srv) {
			if ( !(srv->flags&FLAG_DELETE) && (srv->cacheex_mode==2) ) {
		        sprintf( http_buf, "<br> THREADID Cacheex Server (%s:%d) =  %d", srv->host->name, srv->port, srv->pid );
				tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			}
			srv = srv->next;
		}
#endif
		sprintf( http_buf, "<br><br> TOTAL ECM REQUESTS = %d",totalecm ); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
		tcp_flush(&tcpbuf, sock);
}


#include "bmsearch.c"

void http_send_editor(int sock, http_request *req, int index)
{
	char http_buf[2048];
	struct tcp_buffer_data tcpbuf;
	tcp_init(&tcpbuf);
	http_replyok_write(req, &tcpbuf, sock);
	http_html_write(req, &tcpbuf, sock);
	tcp_write(&tcpbuf, sock, http_head, strlen(http_head) );
	sprintf( http_buf, html_title, cfg.http.title, "Editor"); tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	tcp_write(&tcpbuf, sock, http_link, strlen(http_link) );
	http_style_write(&tcpbuf, sock);
	tcp_write(&tcpbuf, sock, http_head_, strlen(http_head_) );
	tcp_writestr(&tcpbuf, sock, "<body onload=\"document.getElementById('submitbutton').disabled=false;\">");
	tcp_write_menu(&tcpbuf, sock,PAGE_EDITOR);
	//
	int i;
	struct filename_data *fs = cfg.files;
	for( i =0; i<index; i++) {
		if (!fs) break;
		fs = fs->next;
	}
	if ( (i!=index)||(!fs) ) return;
	char *fname = fs->name;
	int noeditor = fs->noeditor;
	//
	if ( (req->type==HTTP_POST) && !noeditor ) {
		// Check Content-Type
		char *content = isset_header(req, "Content-Type");
		if (!content) {
			debugf(getdbgflag(DBG_HTTP,0,0)," Invalid form\n");
			return;
		}
		// Parse Content-type
		if ( memcmp(content,"multipart/form-data",19) ) {
			debugf(getdbgflag(DBG_HTTP,0,0)," Invalid Content-type\n");
			return;
		}
		// Get ';'
		while (*content!=';') {
			if (*content==0)  {
				debugf(getdbgflag(DBG_HTTP,0,0)," Invalid header data\n");
				return;
			}
			content++;
		}
		content++;
		// Skip Spaces
		while (*content==' ') content++;
		// Get Boundry
		if ( memcmp(content,"boundary",8) ) {
			debugf(getdbgflag(DBG_HTTP,0,0)," Invalid Content-type\n");
			return;
		}
		// Get '='
		while (*content!='=') {
			if (*content==0)  {
				debugf(getdbgflag(DBG_HTTP,0,0)," Invalid header data\n");
				return;
			}
			content++;
		}
		content++;
		// Skip Spaces
		while (*content==' ') content++;
		// Get Boundary Value
		char boundary[255];
		sprintf( boundary, "\r\n--%s", content);
		//printf(" boundary: '%s'\n", boundary);

		// search for boundary in file
		content = req->dbf.data;

		while (content) {
			content = (char*) boyermoore_horspool_memmem( (uint8_t*)content, req->dbf.datasize-(content-(char*)req->dbf.data), (uint8_t*)boundary, strlen(boundary) );
			if (content) {
				content += strlen(boundary);
				if ( *content=='\r' && *(content+1)=='\n' ) {
					content+=2;
					// Get Content-Disposition
					// Content-Disposition: form-data; name="textedit"
					char *p = content;
					while (*p!='\r') p++;
					if ( *p=='\r' && *(p+1)=='\n' && *(p+2)=='\r' && *(p+3)=='\n' ) { // Good
						*p=0;
						//printf(" Content: '%s'\n", content);
						char *pdata = p+4;
						// search for newt boundary
						content = (char*)boyermoore_horspool_memmem( (uint8_t*)content, req->dbf.datasize-(content-(char*)req->dbf.data), (uint8_t*)boundary, strlen(boundary) );
						*content = 0;
						//printf(" the file is:\n-------------\n%s\n-------------\n", pdata); 
						// save
						FILE *cfgfd = fopen( fname, "w");
						if (!cfgfd) {
							sprintf( http_buf, "<h2>Error opening file '%s'</h2>", fname);
						}
						else {
							fwrite( pdata, 1, content-pdata, cfgfd);
							fclose(cfgfd);
							sprintf( http_buf, "<script type=\"text/JavaScript\"><!--\nsetTimeout(\"location.href = '/editor%d';\",5000);\n--></script>\n<h3><center>file '%s' is Successfully Saved</center></h3>", index, fname);
						}
						tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
					}
				}
			}
		}
		tcp_flush(&tcpbuf, sock);
	}
	else {

		tcp_writestr(&tcpbuf, sock, "<form enctype=\"multipart/form-data\" method=\"post\">");

		tcp_writestr(&tcpbuf, sock, "<span style='float:right'><select onchange=\"window.location=this.value\" style='width:250px;'>");
		struct filename_data *fs = cfg.files;
		int i =0;
		while (fs) {
			if (!fs->noeditor) {
				if (i==index) sprintf( http_buf, "<option value=\"/editor%d\" selected>%s</option>",i, fs->name);
				else sprintf( http_buf, "<option value=\"/editor%d\">%s</option>",i, fs->name);
				tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			}
			i++;
			fs = fs->next;
		}
		tcp_writestr(&tcpbuf, sock, "</select></span>");

		sprintf( http_buf, "<input type=submit id='submitbutton' value=\"Save '%s'\" disabled><br>",fname);
		tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );

		if (!noeditor) {
			FILE *fd = fopen(fname, "r");
			if (fd) {
				tcp_writestr(&tcpbuf, sock, "<center><textarea cols=\"40\" wrap=\"off\" rows=\"9\" spellcheck=\"false\" name=\"textedit\">");
				while( !feof(fd) ) {
					int len = fread(http_buf, 1, sizeof(http_buf), fd);
					if (len<=0) break;
					tcp_write(&tcpbuf, sock, http_buf, len );
				}
				fclose(fd);
				sprintf( http_buf, "</textarea></center></form>");
				tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			}
			else {
				sprintf( http_buf, "<br>Cant open file '%s'", fname);
				tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
			}
		}
		tcp_flush(&tcpbuf, sock);
	}
}


#ifdef MCS_DCWSTATS
/*
 * TASK R11 (D65) -- /cwlog: the per-CW verdict page. The last keys the
 * cache judged, newest first, straight from the cwlog ring (fed next to
 * the dcwstats counters, under the SAME `DCW STATS` switch -- no new
 * config key, and the stock flavour never compiles any of this). The
 * R7 page rules all apply: the three mandatory writers, the embedded
 * style, mlabel for the Arabic/English labels, and the same
 * authenticated chain in front as /json (the route below sits in it).
 */
static void http_send_cwlog(int sock, struct http_request *req)
{
	struct cwlog_entry rows[CWLOG_CAP];
	char hex[33];
	char http_buf[2048];
	struct tcp_buffer_data tcpbuf;
	int n, i, on;

	(void)req; /* the shell writers read the request themselves */
	tcp_init(&tcpbuf);
	http_replyok_write(req, &tcpbuf, sock);
	http_html_write(req, &tcpbuf, sock);
	tcp_write(&tcpbuf, sock, http_head, strlen(http_head) );
	sprintf( http_buf, html_title, cfg.http.title, mlabel("CW Log","سجل الأحكام"));
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf) );
	tcp_write(&tcpbuf, sock, http_link, strlen(http_link) );
	http_style_write(&tcpbuf, sock);
	tcp_write(&tcpbuf, sock, http_head_, strlen(http_head_) );
	tcp_write(&tcpbuf, sock, http_body, strlen(http_body) );
	tcp_write_menu(&tcpbuf, sock, PAGE_CWLOG);

	on = dcwstats_on;
	n = on ? cwlog_snapshot(rows, CWLOG_CAP) : 0;

	tcp_writestr(&tcpbuf, sock, "<h2>");
	tcp_writestr(&tcpbuf, sock, mlabel("Cache CW verdicts", "أحكام كاش على مفاتيح CW"));
	tcp_writestr(&tcpbuf, sock, "</h2>\n");
	sprintf( http_buf, "<p>%s <b>%s</b> &middot; %s %d / %d</p>\n",
		mlabel("recording", "التسجيل"),
		on ? mlabel("ON (DCW STATS)", "ON (مع DCW STATS)") : mlabel("OFF (arm DCW STATS)", "OFF (فعّل DCW STATS)"),
		mlabel("rows", "الصفوف"), n, cwlog_capacity());
	tcp_writestr(&tcpbuf, sock, http_buf);

	tcp_writestr(&tcpbuf, sock, "<table class=module style='font-size:0.85em; white-space:nowrap;'><tr>");
	tcp_writestr(&tcpbuf, sock, "<th>#</th>");
	sprintf( http_buf, "<th>%s</th>", mlabel("age", "العمر")); tcp_writestr(&tcpbuf, sock, http_buf);
	sprintf( http_buf, "<th>%s</th>", mlabel("channel", "القناة")); tcp_writestr(&tcpbuf, sock, http_buf);
	sprintf( http_buf, "<th>%s</th>", mlabel("peer", "النظير")); tcp_writestr(&tcpbuf, sock, http_buf);
	sprintf( http_buf, "<th>%s</th>", mlabel("verdict", "الحكم")); tcp_writestr(&tcpbuf, sock, http_buf);
	sprintf( http_buf, "<th>%s</th>", mlabel("reason", "السبب")); tcp_writestr(&tcpbuf, sock, http_buf);
	tcp_writestr(&tcpbuf, sock, "<th>CW</th></tr>\n");

	for (i = 0; i < n; i++) {
		const struct cwlog_entry *r = &rows[i];
		int j;
		static const char *vcol[] = { "#1e8449", "#c0392b", "#b9770e", "#5d6d7e" };
		for (j = 0; j < 16; j++) sprintf( hex + j*2, "%02x", r->cw[j] );
		hex[32] = 0;
		sprintf( http_buf, "<tr><td>%d</td><td>+%lumin %lus</td><td>%04x:%06x:%04x</td><td>%s%d</td>"
			"<td style='color:%s;'>%s</td><td>%s</td><td style='font-family:monospace;'>%s</td></tr>\n",
			i + 1,
			(long)(r->tick / 60000), (long)((r->tick / 1000) % 60),
			r->caid, r->provid, r->sid,
			(r->peerid & PEER_ORIGIN_FLAGS) ? "csp:" : "peer:", (r->peerid & PEER_ID_MASK),
			vcol[r->verdict <= CWLOG_STORED ? r->verdict : CWLOG_STORED],
			cwlog_verdict_name(r->verdict),
			cwlog_reason_name(r->reason),
			hex);
		tcp_writestr(&tcpbuf, sock, http_buf);
	}
	if (n == 0) {
		sprintf( http_buf, "<tr><td colspan=7>%s</td></tr>\n",
			on ? mlabel("no verdicts recorded yet", "لا أحكام مسجّلة بعد")
			   : mlabel("recording is off -- turn on DCW STATS", "التسجيل مطفأ -- فعّل DCW STATS"));
		tcp_writestr(&tcpbuf, sock, http_buf);
	}
	tcp_writestr(&tcpbuf, sock, "</table>\n<p style='font-size:0.8em;'>");
	tcp_writestr(&tcpbuf, sock, mlabel(
		"newest first &middot; the ring keeps the last 100 verdicts &middot; machine readout: /json (section cwlog)",
		"الأحدث أولاً &middot; الحلقة تحفظ آخر 100 حكم &middot; القراءة الآلية: /json (قسم cwlog)"));
	tcp_writestr(&tcpbuf, sock, "</p>\n</body></html>\n");
	tcp_flush(&tcpbuf, sock);
}
#endif /* MCS_DCWSTATS */


/* TASK R5 (D59): the machine readout. One JSON document behind the SAME
 * Basic auth as every page (the gate runs before this router, and the R3
 * doors sit in front of the gate): dcwstats counters, the peer-reputation
 * ladder, the cache-peer rows. The snapshot is taken the same unlocked way
 * the stock HTML pages read these fields; the document itself is built by
 * the pure monjson module (unit-tested, hand-rolled, no libraries). */
static void http_send_json(int sock, struct http_request *req)
{
	char buf[MONJSON_BUF_MAX];
	struct monjson_peer peers[MONJSON_MAX_PEERS];
	struct monjson_rep reps[MONJSON_MAX_REPS];
	/* TASK R11 (D65): the verdict-ring rows live at FUNCTION scope on
	 * purpose -- in.logs keeps pointing at them after the fill block
	 * below ends; a block-scope array here segfaulted the -O2 build. */
	struct monjson_cwl cwlrows[MONJSON_MAX_LOGS];
	struct monjson_in in;
	int npeers = 0, nreps = 0;
	memset(&in, 0, sizeof(in));

	struct cacheserver_data *cache = cfg.cache.server;
	while (cache && npeers < MONJSON_MAX_PEERS) {
		struct cachepeer_data *peer = cache->peer;
		while (peer && npeers < MONJSON_MAX_PEERS) {
			struct monjson_peer *row = &peers[npeers++];
			memset(row, 0, sizeof(*row));
			snprintf(row->name, sizeof(row->name), "%s", peer->host->name);
			row->ip = peer->host->ip;
			row->port = peer->port;
			row->ping = peer->ping;
			row->cards = peer->nbcards;
			row->ecmnb = (unsigned long)peer->reqnb;
			row->ecmok = (unsigned long)peer->repok;
			row->hitnb = (unsigned long)peer->hitnb;
			row->disabled = IS_DISABLED(peer->flags) ? 1 : 0;
			row->rep_stage = -1;
			peer = peer->next;
		}
		cache = cache->next;
	}

	if (peerrep_on) {
		int i, n = peerrep_count();
		if (n > MONJSON_MAX_REPS) n = MONJSON_MAX_REPS;
		for (i = 0; i < n; i++) {
			uint32_t ip; uint16_t port; int stage, events;
			char reason[MONJSON_REASON_MAX];
			if (!peerrep_get(i, &ip, &port, &stage, &events, reason, sizeof(reason))) break;
			struct monjson_rep *r = &reps[nreps++];
			r->ip = ip; r->port = port; r->stage = stage; r->events = events;
			snprintf(r->reason, sizeof(r->reason), "%s", reason);
			int j;
			for (j = 0; j < npeers; j++) {
				if (peers[j].ip == ip && peers[j].port == (int)port) {
					peers[j].rep_stage = stage;
					peers[j].rep_events = events;
					snprintf(peers[j].rep_reason, sizeof(peers[j].rep_reason), "%s", reason);
					break;
				}
			}
		}
	}

#ifdef MCS_DCWSTATS
	in.dcwstats_on = dcwstats_on;
	in.dcwstats = dcwstats;
#else
	/* the stock flavour has no counters: the builder emits the same
	 * section with on=0 and zeros, so the document shape is identical */
	in.dcwstats_on = 0;
	in.dcwstats = NULL;
#endif
	/* TASK R11 (D65): the verdict ring. Same shape in every flavour;
	 * stock carries on=0 and no rows. Sized by MONJSON_MAX_LOGS (= the
	 * ring's CWLOG_CAP), which the stock flavour has too. */
	{
		int nlog = 0;
		int cwlogon = 0;
		memset(cwlrows, 0, sizeof(cwlrows));
#ifdef MCS_DCWSTATS
		cwlogon = dcwstats_on ? 1 : 0;
		if (cwlogon) {
			struct cwlog_entry raw[CWLOG_CAP];
			nlog = cwlog_snapshot(raw, CWLOG_CAP);
			int li, j;
			for (li = 0; li < nlog; li++) {
				cwlrows[li].tick = raw[li].tick;
				cwlrows[li].caid = raw[li].caid;
				cwlrows[li].provid = (int)raw[li].provid;
				cwlrows[li].sid = raw[li].sid;
				cwlrows[li].verdict = raw[li].verdict;
				cwlrows[li].reason = raw[li].reason;
				cwlrows[li].peerid = raw[li].peerid;
				for (j = 0; j < 16; j++) sprintf( &cwlrows[li].cw[j*2], "%02x", raw[li].cw[j] );
				cwlrows[li].cw[32] = 0;
			}
		}
#endif
		in.cwlog_on = cwlogon;
#ifdef MCS_DCWSTATS
		in.cwlog_cap = cwlogon ? cwlog_capacity() : 0;
#else
		in.cwlog_cap = 0;
#endif
		in.nlogs = nlog;
		in.logs = cwlrows;
	}
	in.peerrep_on = peerrep_on;
	/* through locals: the tree builds with -fpack-struct, and taking the
	 * address of a packed member is (rightly) fatal here */
	{ int td = 0, ti = 0, tb = 0;
	  peerrep_thresholds(&td, &ti, &tb);
	  in.th_distrust = td; in.th_isolate = ti; in.th_ban = tb; }
	in.uptime = GetTickCount()/1000;
	in.now = (long)time(NULL);
	in.nreps = nreps; in.reps = reps;
	in.npeers = npeers; in.peers = peers;

	int len = monjson_build(buf, sizeof(buf), &in);
	if (len < 0) {
		/* unreachable in practice (256 KB against ~60 KB worst case), and
		 * even then the body stays a valid one-key document */
		static const char mjerr[] = "{\"error\":\"document overflow\"}";
		http_send_answer(sock, req, "application/json", (char*)mjerr, sizeof(mjerr)-1);
		return;
	}
	http_send_answer(sock, req, "application/json", buf, len);
}

int atoint(char *index)
{
  int n=0;
  while (*index)
  { 
    if ( (*index<'0')||(*index>'9') ) return n;
    else n = n*10 + (*index - '0');
    index++;
  }
  return n;
}

#include "base64.c"

struct connect_data {
	int sock;
	uint32_t ip;
};

/*
 * TASK R7 (D61) -- token sessions. After ONE successful Basic login the
 * browser holds a 256-bit random token (HttpOnly, SameSite=Strict) and
 * later requests may present it instead of credentials. The DOORS DO NOT
 * MOVE: the R3 allowlist still closes pre-auth before anything here runs,
 * and a request with no valid cookie and no credentials still walks the
 * R3 progressive delay into the same 401. Sessions live in RAM only
 * (a restart logs everyone out -- deliberate), are bound to the source
 * address, idle-expire after 30 minutes, die absolutely after 12 hours,
 * and the table holds 16 (the oldest is evicted). /logout invalidates.
 */
#define HTTP_SESSIONS      16
#define HTTP_SESSION_IDLE  1800     /* 30 min without a request */
#define HTTP_SESSION_LIFE  43200    /* 12 h absolute */
struct http_session { int used; uint8_t token[32]; uint32_t ip; time_t born; time_t last; };
static struct http_session http_sessions[HTTP_SESSIONS];
static pthread_mutex_t http_session_lock = PTHREAD_MUTEX_INITIALIZER;

/* 64 hex chars from the OS entropy pool; a weak time-based mix only if
 * the pool is unavailable (never seen on the deployed systems). */
static void http_hex_token(char *out)
{
	uint8_t raw[32];
	int fd = open("/dev/urandom", 0); /* O_RDONLY */
	int got = 0;
	if (fd >= 0) { got = (read(fd, raw, 32) == 32); close(fd); }
	if (!got) {
		uint32_t t = (uint32_t)time(NULL) ^ (uint32_t)GetTickCount();
		int i;
		for (i = 0; i < 8; i++) {
			raw[i*4] = (uint8_t)(t >> 24); raw[i*4+1] = (uint8_t)(t >> 16);
			raw[i*4+2] = (uint8_t)(t >> 8); raw[i*4+3] = (uint8_t)t;
			t = t * 1664525u + 1013904223u;
		}
	}
	raw[0] ^= (uint8_t)(uintptr_t)out; /* tiny extra stir */
	{
		int i;
		for (i = 0; i < 32; i++) sprintf(out + 2*i, "%02x", raw[i]);
	}
	out[64] = 0;
}

static int http_hex_byte(const char *p, uint8_t *out)
{
	int h, l;
	if (p[0] >= '0' && p[0] <= '9') h = p[0] - '0';
	else if (p[0] >= 'a' && p[0] <= 'f') h = p[0] - 'a' + 10;
	else return 0;
	if (p[1] >= '0' && p[1] <= '9') l = p[1] - '0';
	else if (p[1] >= 'a' && p[1] <= 'f') l = p[1] - 'a' + 10;
	else return 0;
	*out = (uint8_t)((h << 4) | l);
	return 1;
}

/* parse the MCSSESSION cookie into tok; returns 1 on a well-formed value */
static int http_session_parse(http_request *req, uint8_t *tok)
{
	char *ck = isset_header(req, "Cookie");
	char *p;
	int i;
	if (!ck) return 0;
	p = strstr(ck, "MCSSESSION=");
	if (!p) return 0;
	p += 11;
	for (i = 0; i < 32; i++) if (!http_hex_byte(p + 2*i, tok + i)) return 0;
	if (p[64] != 0 && p[64] != ';' && p[64] != ' ') return 0;
	return 1;
}

static int http_session_check(http_request *req, uint32_t ip)
{
	uint8_t tok[32];
	time_t now = time(NULL);
	int i, found = 0;
	if (!http_session_parse(req, tok)) return 0;
	pthread_mutex_lock(&http_session_lock);
	for (i = 0; i < HTTP_SESSIONS; i++) {
		struct http_session *s = &http_sessions[i];
		if (s->used && s->ip == ip && !memcmp(s->token, tok, 32)) {
			if (now - s->last <= HTTP_SESSION_IDLE && now - s->born <= HTTP_SESSION_LIFE) {
				s->last = now;
				found = 1;
			}
			break;
		}
	}
	pthread_mutex_unlock(&http_session_lock);
	return found;
}

/* append one Set-Cookie pair to the request's pending header block */
static void http_add_cookie(http_request *req, const char *pair)
{
	size_t left = sizeof(req->setcookie) - strlen(req->setcookie) - 1;
	if (req->setcookie[0]) { strncat(req->setcookie, "\r\nSet-Cookie: ", left); left = sizeof(req->setcookie) - strlen(req->setcookie) - 1; }
	strncat(req->setcookie, pair, left);
}

static void http_session_mint(http_request *req, uint32_t ip)
{
	char hex[65];
	uint8_t tok[32];
	time_t now = time(NULL);
	int i, slot = -1, oldest = -1;
	time_t oldest_t = 0;
	http_hex_token(hex);
	for (i = 0; i < 32; i++) http_hex_byte(hex + 2*i, tok + i);
	pthread_mutex_lock(&http_session_lock);
	for (i = 0; i < HTTP_SESSIONS; i++) {
		struct http_session *s = &http_sessions[i];
		if (!s->used) { slot = i; break; }
		if (now - s->last > HTTP_SESSION_IDLE || now - s->born > HTTP_SESSION_LIFE) { slot = i; break; }
		if (oldest < 0 || s->last < oldest_t) { oldest = i; oldest_t = s->last; }
	}
	if (slot < 0) slot = oldest;
	{
		struct http_session *s = &http_sessions[slot];
		memset(s, 0, sizeof(*s));
		s->used = 1; s->ip = ip; s->born = now; s->last = now;
		memcpy(s->token, tok, 32);
	}
	pthread_mutex_unlock(&http_session_lock);
	{
		char pair[128];
		sprintf(pair, "MCSSESSION=%s; Path=/; HttpOnly; SameSite=Strict; Max-Age=%d", hex, HTTP_SESSION_IDLE);
		http_add_cookie(req, pair);
	}
}

static void http_session_drop(http_request *req, uint32_t ip)
{
	uint8_t tok[32];
	int i;
	if (http_session_parse(req, tok)) {
		pthread_mutex_lock(&http_session_lock);
		for (i = 0; i < HTTP_SESSIONS; i++) {
			struct http_session *s = &http_sessions[i];
			if (s->used && s->ip == ip && !memcmp(s->token, tok, 32)) { s->used = 0; break; }
		}
		pthread_mutex_unlock(&http_session_lock);
	}
	http_add_cookie(req, "MCSSESSION=; Path=/; HttpOnly; Max-Age=0");
}

#ifdef CCCAM_SRV
/* One process-wide CSRF secret; fail closed if secure randomness is absent. */
static pthread_once_t trace_csrf_once = PTHREAD_ONCE_INIT;
static char trace_csrf[65];
static void trace_csrf_init(void)
{
    unsigned char raw[32];
    int fd = open("/dev/urandom", O_RDONLY), n = 0;
    if (fd < 0) return;
    while (n < (int)sizeof(raw)) {
        int r = read(fd, raw + n, sizeof(raw) - n);
        if (r <= 0) { close(fd); return; }
        n += r;
    }
    close(fd);
    for (n = 0; n < 32; ++n) sprintf(trace_csrf + n * 2, "%02x", raw[n]);
}
static char *trace_post_value(http_request *req, const char *name)
{
    int i;
    for (i = 0; i < req->postcount; ++i)
        if (!strcmp(req->postlist[i].name, name)) return req->postlist[i].value;
    return NULL;
}
static int trace_parse_number(const char *text, int base, unsigned long max, unsigned long *out)
{
    char *end;
    unsigned long v;
    const char *p;
    if (!text || !*text) return 0;
    for (p = text; *p; ++p)
        if (!((*p >= '0' && *p <= '9') || (base == 16 && ((*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F'))))) return 0;
    errno = 0;
    v = strtoul(text, &end, base);
    if (errno || *end || v > max) return 0;
    *out = v;
    return 1;
}
static const char *trace_type_name(enum user_trace_event_type type)
{
    switch (type) {
    case USER_TRACE_RECEIVED: return "استلام ECM";
    case USER_TRACE_DENIED: return "رفض الطلب";
    case USER_TRACE_JOINED: return "انضم إلى ECM موجود";
    case USER_TRACE_NEW: return "ECM جديد";
    case USER_TRACE_SENT: return "إرسال لمصدر";
    case USER_TRACE_SOURCE_REPLY: return "رد المصدر";
    case USER_TRACE_VALIDATED: return "اجتاز مرحلة تحقق";
    case USER_TRACE_CACHE: return "حركة كاش";
    case USER_TRACE_DISPATCH_SKIPPED: return "تجاوز إرسال جماعي";
    case USER_TRACE_REJECTED: return "رفض استجابة";
    case USER_TRACE_FAILED: return "فشل";
    case USER_TRACE_DELIVERED: return "أرسلت استجابة CW";
    case USER_TRACE_SEND_FAILED: return "تعذر إرسال الاستجابة";
    default: return "حدث غير معروف";
    }
}
static const char *trace_detail_ar(const char *detail)
{
    if (!strncmp(detail, "CW rejected by DCW MINTIME;", 27)) return "رُفضت CW بحد DCW MINTIME عند المعالجة؛ المدة منذ الإرسال والحد في التفاصيل";
    if (!strcmp(detail, "CW rejected: zero half not allowed for CAID family")) return "رُفضت CW: عائلة CAID لا تسمح بنصف صفري";
    if (!strcmp(detail, "CW committed; client dispatch skipped after DCW timeout")) return "ثُبّت نجاح CW لكن استدعاء إرسال العملاء الجماعي تُجاوز بعد مهلة DCW";
    if (!strncmp(detail, "CCcam CW response;", 18)) return "وصل رد CW من مصدر CCcam؛ لم تُحسم صلاحيته بعد";
    if (!strncmp(detail, "CCcam NOK1 response;", 20)) return "رد مصدر CCcam بـNOK1؛ قد تُعاد المحاولة";
    if (!strncmp(detail, "CCcam NOK2 response;", 20)) return "رد مصدر CCcam بـNOK2؛ قد تُعاد المحاولة";
    if (!strcmp(detail, "CCcam CW passed initial validation; further gates remain")) return "اجتازت CW تحقق CCcam الأولي؛ لا تزال فحوص أخرى";
    if (!strcmp(detail, "CCcam malformed CW response: fewer than 16 bytes; source disconnected")) return "رُفض رد CCcam قصير وأُغلق اتصال المصدر";
    if (!strcmp(detail, "CCcam CW rejected: checksum validation failed")) return "رُفضت CW من CCcam: فشل تحقق checksum";
    if (!strcmp(detail, "CCcam CW rejected: null-word validation failed")) return "رُفضت CW من CCcam: لم تجتز قاعدة الأجزاء الصفرية";
    if (!strcmp(detail, "CCcam CW rejected: repeated-byte validation failed")) return "رُفضت CW من CCcam: لم تجتز قاعدة البايتات المتكررة";
    if (!strcmp(detail, "CCcam CW rejected: operator BAD-DCW list")) return "رُفضت CW من CCcam: موجودة في قائمة BAD-DCW";
    if (!strcmp(detail, "CCcam CW rejected: enabled pattern validation")) return "رُفضت CW من CCcam: لم تجتز فلتر النمط";
    if (!strcmp(detail, "CCcam response rejected: ECM length not whitelisted")) return "رُفض رد CCcam: طول ECM غير موجود في القائمة المسموحة";
    if (!strcmp(detail, "CCcam retry send reported success")) return "أفادت دالة الإرسال بنجاح إعادة الطلب إلى CCcam";
    if (!strcmp(detail, "Newcamd CW rejected: checksum validation failed")) return "رُفضت: فشل تحقق checksum";
    if (!strcmp(detail, "Newcamd CW rejected: null-word validation failed")) return "رُفضت: لم تجتز قاعدة أجزاء CW الصفرية";
    if (!strcmp(detail, "Newcamd CW rejected: repeated-byte validation failed")) return "رُفضت: لم تجتز قاعدة البايتات المتكررة";
    if (!strcmp(detail, "Newcamd CW rejected: operator BAD-DCW list")) return "رُفضت: موجودة في قائمة BAD-DCW";
    if (!strcmp(detail, "Newcamd CW rejected: enabled pattern validation")) return "رُفضت: لم تجتز فلتر النمط المفعّل";
    if (!strcmp(detail, "Newcamd CW passed initial validation; further gates remain")) return "اجتازت التحقق الأولي؛ لا تزال فحوص أخرى لاحقة";
    if (!strncmp(detail, "CSP reply dropped: incomplete integrity header", 46)) return "أُسقط رد CSP قبل التحقق: ترويسة السلامة ناقصة";
    if (!strncmp(detail, "CSP reply dropped: incomplete CW payload", 40)) return "أُسقط رد CSP: لا يحتوي CW كاملة";
    if (!strncmp(detail, "CSP reply dropped: unknown sender", 33)) return "أُسقط رد CSP من عنوان غير معروف";
    if (!strncmp(detail, "CSP reply dropped: configured peer disabled", 42)) return "أُسقط رد CSP لأن النظير معطّل";
    if (!strncmp(detail, "CSP reply dropped: peer reputation isolation", 44)) return "أُسقط رد CSP لعزل النظير في السمعة";
    if (!strncmp(detail, "CSP reply dropped: integrity tag mismatch", 41)) return "أُسقط رد CSP لاختلاف بايت سلامة الترويسة";
    if (!strncmp(detail, "CSP reply dropped: invalid ECM tag", 34)) return "أُسقط رد CSP: نوع ECM غير صالح";
    if (!strncmp(detail, "CSP reply dropped: missing channel or hash", 42)) return "أُسقط رد CSP: قناة أو بصمة ناقصة";
    if (!strncmp(detail, "CSP reply dropped: CAID not allowed by cache", 44)) return "أُسقط رد CSP: CAID غير مسموح في الكاش";
    if (!strncmp(detail, "CSP reply dropped: oversized datagram", 37)) return "أُسقط رد CSP لتجاوز حجم الرسالة المسموح";
    if (!strcmp(detail, "cache rejected: negative memory gate")) return "الكاش رفض CW بسبب الذاكرة السلبية";
    if (!strcmp(detail, "cache rejected: attestation gate")) return "الكاش رفض CW بسبب بوابة attestation";
    if (!strcmp(detail, "cache held: threshold not reached")) return "الكاش ينتظر بلوغ حد التأكيدات";
    if (!strcmp(detail, "cache held: CacheEX confirmation threshold")) return "CacheEX ينتظر تأكيدات إضافية";
    if (!strcmp(detail, "cache rejected: half-null not allowed for CAID family")) return "الكاش رفض نصف CW الصفري لعائلة CAID";
    if (!strncmp(detail, "cache wait policy: initial schedule=", 36)) return "وقت الجدولة الأولي من البروفايل؛ قد يُعاد تحديده أو ينتهي مبكراً";
    if (!strncmp(detail, "cache wait policy: ECM thread timeout=", 38)) return "مهلة CAID التي استعملها خيط ECM عند هذه المعالجة؛ ليست مدة انتظار مؤكدة";
    if (!strcmp(detail, "cache CW entered validation; decision pending")) return "وصلت CW لمسار التحقق في الكاش؛ القرار لم يصدر";
    if (!strcmp(detail, "cache consensus window expired; cycle checks pending")) return "انتهت نافذة consensus؛ لم يحسم فحص الدورة بعد";
    if (!strncmp(detail, "cache consensus capacity:", 25)) return "سعة جدول Consensus العامة عند عدم توفر خانة؛ لا تنسب الخانات للعميل";
    if (!strncmp(detail, "cache cycle context:", 20)) return "سياق فلتر الدورة: الوارد والمتوقع وتوفر CW سابقة";
    if (!strncmp(detail, "cache cycle mismatch:", 21)) return "لم تُمرر CW لاختلاف الدورة المتوقعة عن الدورة التي قيّمها الكاش";
    if (!strcmp(detail, "cache CW validation policy: attached ECM profile")) return "طبق فحص CW إعدادات بروفايل ECM المرتبط بسجل الكاش؛ لا يثبت ارتباطه بمحاولة العميل";
    if (!strcmp(detail, "cache CW validation policy: global; no attached ECM profile")) return "طبق فحص CW القواعد العامة لعدم وجود بروفايل ECM مرتبط بسجل الكاش";
    if (!strncmp(detail, "cache consensus vote:", 21)) return "قرار consensus وعدد معرفات النظراء وأوزانها؛ تعددها لا يثبت استقلال المصادر";
    if (!strcmp(detail, "cache held: consensus awaiting corroboration")) return "الكاش احتجز CW انتظاراً لتأكيد consensus";
    if (!strcmp(detail, "cache rejected: consensus decision; weighted loss not established for this arrival")) return "رفض Consensus الرد؛ لم يُثبت فقدان الوزن لهذا الرد، فلا يعاقَب النظير بسبب هذا القرار";
    if (!strcmp(detail, "cache rejected: consensus weighted decision")) return "الكاش رفض CW بقرار consensus";
    if (!strcmp(detail, "cache not forwarded: filter did not establish cycle")) return "الفلتر لم يثبت دورة CW فلم يمررها في هذا المسار";
    if (!strcmp(detail, "cache forwarded CW to ECM pipe; delivery not confirmed")) return "حاول الكاش تمرير CW إلى مسار ECM؛ لا يثبت تسليمها";
    if (!strcmp(detail, "cache lookup: no existing entry; peer response may still arrive")) return "لم يوجد سجل كاش عند البحث؛ قد يصل رد النظير لاحقاً";
    if (!strcmp(detail, "cache rejected: profile CW checksum validation failed")) return "رفض الكاش CW: فشل تحقق checksum";
    if (!strcmp(detail, "cache rejected: profile CW null-word validation failed")) return "رفض الكاش CW: لم تجتز قاعدة الأجزاء الصفرية";
    if (!strcmp(detail, "cache rejected: profile CW repeated-byte validation failed")) return "رفض الكاش CW: لم تجتز قاعدة البايتات المتكررة";
    if (!strcmp(detail, "cache rejected: profile CW operator BAD-DCW list")) return "رفض الكاش CW: موجودة في قائمة BAD-DCW";
    if (!strcmp(detail, "cache rejected: profile CW enabled pattern validation")) return "رفض الكاش CW: لم تجتز فلتر النمط المفعّل";
    if (!strcmp(detail, "cache rejected: profile CW validation; specific check unknown")) return "رفض الكاش CW في فحص البروفايل؛ الفحص المحدد غير معروف";
    if (!strcmp(detail, "cache rejected: CacheEX local-only gate")) return "رفض الكاش CW بسبب CacheEX local-only";
    if (!strcmp(detail, "cache lookup invoked; pipe delivery and result not confirmed")) return "طُلب بحث الكاش؛ وصول الطلب والنتيجة غير مؤكدين";
    if (!strcmp(detail, "cache peer request invoked; pipe delivery not confirmed")) return "طُلب إرسال بحث إلى نظراء الكاش؛ وصول الطلب غير مؤكد";
    if (!strcmp(detail, "cache peer retry invoked; pipe delivery not confirmed")) return "طُلبت إعادة البحث لدى نظراء الكاش؛ وصول الطلب غير مؤكد";
    if (!strcmp(detail, "Newcamd retry send reported success")) return "أفادت دالة الإرسال بنجاح إعادة الطلب إلى Newcamd";
    return "";
}
/* Render only evidence bound to the selected attempt. A channel-only cache
 * event must never be inferred to have caused an individual client's result. */
static void trace_summary_evidence(struct tcp_buffer_data *tcpbuf, int sock,
                                   const char *label, const struct user_trace_event *e)
{
    char out[1400], safe[768];
    const char *ar = trace_detail_ar(e->detail);
    html_esc(safe, sizeof(safe), e->detail);
    snprintf(out, sizeof(out), "<li>%s: %s%s%s؛ مصدر %d / %d؛ المدة المسجلة %u ms</li>",
             label, ar, *ar ? " — " : "", safe, e->source_type, e->source_id, e->elapsed_ms);
    tcp_writestr(tcpbuf, sock, out);
}
static void trace_summary_write(struct tcp_buffer_data *tcpbuf, int sock,
                                const struct user_trace_snapshot *snap)
{
    struct user_trace_summary sum;
    char out[1024];
    user_trace_summarize(snap, &sum);
    tcp_writestr(tcpbuf, sock, "<h2>خلاصة آخر محاولة مسجلة</h2>");
    if (sum.incomplete)
        tcp_writestr(tcpbuf, sock, "<p><b>التغطية ناقصة:</b> فقدت أو استبدلت أحداث أو روابط في الجلسة؛ قد تكون أدلة هذه المحاولة ناقصة.</p>");
    if (sum.received < 0) {
        tcp_writestr(tcpbuf, sock, "<p>لا يوجد طلب مستلم محفوظ في الجلسة؛ النتيجة والسبب غير معروفين.</p>");
        return;
    }
    {
        const struct user_trace_event *e = &snap->events[sum.received];
        snprintf(out, sizeof(out), "<p>المحاولة %llu — <code>%04X:%06X:%04X</code>. لا يفترض تطابق البصمة هوية الطلب.</p><ul>",
                 (unsigned long long)sum.attempt, e->caid, e->provider, e->sid);
        tcp_writestr(tcpbuf, sock, out);
    }
    if (sum.client_result >= 0) {
        const struct user_trace_event *e = &snap->events[sum.client_result];
        if (e->type == USER_TRACE_DELIVERED)
            tcp_writestr(tcpbuf, sock, "<li><b>نتيجة العميل:</b> سجل النظام إرسال CW؛ لا يثبت ظهور الصورة.</li>");
        else if (e->type == USER_TRACE_SEND_FAILED)
            tcp_writestr(tcpbuf, sock, "<li><b>نتيجة العميل:</b> سجل النظام تعذر إرسال الاستجابة.</li>");
        else if (e->type == USER_TRACE_DENIED)
            tcp_writestr(tcpbuf, sock, "<li><b>نتيجة العميل:</b> سجل النظام رفض الطلب.</li>");
        else tcp_writestr(tcpbuf, sock, "<li><b>نتيجة العميل:</b> سجل النظام فشل الطلب.</li>");
        trace_summary_evidence(tcpbuf, sock, "دليل نتيجة العميل", e);
    } else tcp_writestr(tcpbuf, sock, "<li><b>نتيجة العميل:</b> غير معروفة من الأحداث المحتفظ بها؛ غياب الحدث ليس دليل عدم الإرسال.</li>");
    if (sum.last_ecm_failure >= 0)
        trace_summary_evidence(tcpbuf, sock, "حدث فشل ECM مشترك", &snap->events[sum.last_ecm_failure]);
    if (sum.last_source_reply >= 0)
        trace_summary_evidence(tcpbuf, sock, "آخر رد مصدر مسجل", &snap->events[sum.last_source_reply]);
    else tcp_writestr(tcpbuf, sock, "<li>رد المصدر: لم يُسجَّل في الأحداث المحتفظ بها؛ لا يعني أنه لم يصل.</li>");
    if (sum.last_source_rejection >= 0)
        trace_summary_evidence(tcpbuf, sock, "رفض مسجل خلال المحاولة (ليس بالضرورة سبب النتيجة النهائية)", &snap->events[sum.last_source_rejection]);
    if (sum.last_cache_policy >= 0)
        trace_summary_evidence(tcpbuf, sock, "سياسة انتظار الكاش عند البحث", &snap->events[sum.last_cache_policy]);
    if (sum.last_cache_lookup >= 0)
        trace_summary_evidence(tcpbuf, sock, "طلب بحث الكاش", &snap->events[sum.last_cache_lookup]);
    else tcp_writestr(tcpbuf, sock, "<li>بحث الكاش: غير معروف لهذه المحاولة من الأحداث المحتفظ بها.</li>");
    tcp_writestr(tcpbuf, sock, "</ul><p>أحداث الكاش المصنفة «قناة فقط» لا تثبت أنها تخص هذه المحاولة؛ لذلك لا تُنسب إليها كسبب. القيم المسجلة لقطة عند البحث؛ قد تنتهي المهلة قبل وقتها بسبب مسارات أخرى.</p>");
}
static void http_send_user_trace(int sock, http_request *req)
{
    struct tcp_buffer_data tcpbuf;
    struct user_trace_snapshot *snap;
    char out[2048];
    size_t i;
    pthread_once(&trace_csrf_once, trace_csrf_init);
    tcp_init(&tcpbuf);
    if (!cfg.http.user[0] || !cfg.http.pass[0] || !trace_csrf[0]) {
        tcp_writestr(&tcpbuf, sock, "HTTP/1.1 403 Forbidden\r\nConnection: close\r\nContent-Type: text/plain; charset=utf-8\r\n\r\nTrace requires configured web credentials and secure randomness.");
        tcp_flush(&tcpbuf, sock); return;
    }
    snap = malloc(sizeof(*snap));
    if (!snap) {
        tcp_writestr(&tcpbuf, sock, "HTTP/1.1 503 Service Unavailable\r\nConnection: close\r\n\r\n");
        tcp_flush(&tcpbuf, sock); return;
    }
    if (req->type == HTTP_POST) {
        char body[1024], *start, *csrf, *action;
        int len, ok = 0;
        char *content_type = isset_header(req, "Content-Type");
        start = NULL;
        if (req->dbf.data) {
            int pos;
            for (pos = 0; pos + 4 <= req->dbf.datasize; ++pos) {
                if (!memcmp(req->dbf.data + pos, "\r\n\r\n", 4)) {
                    start = req->dbf.data + pos;
                    break;
                }
            }
        }
        if (start && content_type && !strncmp(content_type, "application/x-www-form-urlencoded", 33)) {
            start += 4;
            len = req->dbf.datasize - (int)(start - req->dbf.data);
            if (len >= 0 && len < (int)sizeof(body)) {
                memcpy(body, start, len); body[len] = 0;
                explode_post(req, body);
                csrf = trace_post_value(req, "csrf"); action = trace_post_value(req, "action");
                if (csrf && strlen(csrf) == 64 && !strcmp(csrf, trace_csrf) && action) {
                    if (!strcmp(action, "stop")) {
                        user_trace_get_snapshot(snap); user_trace_stop(snap->session); ok = 1;
                    } else if (!strcmp(action, "start")) {
                        unsigned long client, caid = 0, prov = 0, sid = 0;
                        char *mode = trace_post_value(req, "mode");
                        int all = mode && !strcmp(mode, "all");
                        if ((!mode || !strcmp(mode, "single") || all) &&
                            trace_parse_number(trace_post_value(req, "client"), 10, 0xffffffffUL, &client) && client &&
                            (all || (trace_parse_number(trace_post_value(req, "caid"), 16, 0xffff, &caid) && caid &&
                                     trace_parse_number(trace_post_value(req, "provid"), 16, 0xffffff, &prov) &&
                                     trace_parse_number(trace_post_value(req, "sid"), 16, 0xffff, &sid) && sid))) {
                            struct user_trace_target t = {0};
                            user_trace_config_lock();
                            if (getcccamclientbyid((uint32_t)client)) {
                                t.client_id = client; t.caid = caid; t.provider = prov; t.sid = sid;
                                t.all_channels = all;
                                ok = user_trace_start(&t) != 0;
                            }
                            user_trace_config_unlock();
                        }
                    }
                }
            }
        }
        free(snap);
        if (ok) tcp_writestr(&tcpbuf, sock, "HTTP/1.1 303 See Other\r\nLocation: /user-trace\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n");
        else tcp_writestr(&tcpbuf, sock, "HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Type: text/plain; charset=utf-8\r\n\r\nInvalid trace request or CSRF token.");
        tcp_flush(&tcpbuf, sock); return;
    }
    user_trace_get_snapshot(snap);
    tcp_writestr(&tcpbuf, sock, "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\nConnection: close\r\n");
    if (req->setcookie[0]) { tcp_writestr(&tcpbuf, sock, "Set-Cookie: "); tcp_writestr(&tcpbuf, sock, req->setcookie); tcp_writestr(&tcpbuf, sock, "\r\n"); }
    tcp_writestr(&tcpbuf, sock, "\r\n");
    tcp_writestr(&tcpbuf, sock, "<!doctype html><html lang='ar' dir='rtl'><head><meta charset='utf-8'><title>متابعة عميل</title><style>body{background:#151b22;color:#ddd;font:16px sans-serif;margin:2em}input,select,button{padding:.5em;margin:.3em}table{border-collapse:collapse;width:100%}td,th{border:1px solid #555;padding:.5em;text-align:right}code{direction:ltr;display:inline-block}</style></head><body><h1>متابعة CCcam</h1><p>جلسة واحدة مشتركة للمشرفين؛ البدء يستبدل الجلسة السابقة، وإعادة تحميل الإعدادات توقفها. تبدأ المتابعة للطلبات الجديدة فقط. اختيار العميل يملأ آخر قناة مسجلة إن توفرت، وقد لا تكون القناة الحالية؛ يمكن تعديلها يدوياً. بعض أسباب الرفض غير مسجلة في هذه النسخة. أحداث الكاش على مستوى القناة فقط، وقد تخص طلبات عملاء آخرين؛ تطابق CAID وSID لا يثبت تطابق الطلب أو Provider، و000000 قد تعني أن المصدر لم يرسله. الأحداث المشتركة تخص ECM وقد يشارك فيه عملاء آخرون؛ إرسال CW لا يثبت ظهور الصورة.</p>");
    snprintf(out, sizeof(out), "<form method='post' action='/user-trace'><input type='hidden' name='csrf' value='%s'><input type='hidden' name='action' value='start'><label>العميل <select name='client' required onchange='traceChoose(this)'><option value='' disabled%s>اختر عميلاً</option>", trace_csrf, snap->active ? "" : " selected");
    tcp_writestr(&tcpbuf, sock, out);
    {
        struct trace_option { uint32_t id; int connected; uint16_t caid, sid; uint32_t provider; char name[64]; };
        struct trace_option *options = calloc(4096, sizeof(*options));
        size_t n = 0, k;
        int truncated = 0;
        if (options) {
            struct cccam_server_data *server;
            user_trace_config_lock();
            server = cfg.cccam.server;
            while (server) {
                struct cc_client_data *client = server->client;
                while (client) {
                    if (!(server->flags & FLAG_DELETE) && !(client->flags & FLAG_DELETE)) {
                        if (n == 4096) { truncated = 1; break; }
                        options[n].id = client->id;
                        options[n].connected = client->connection.status > 0;
                        options[n].caid = client->lastecm.caid;
                        options[n].provider = client->lastecm.prov;
                        options[n].sid = client->lastecm.sid;
                        memcpy(options[n].name, client->user, sizeof(options[n].name));
                        options[n].name[sizeof(options[n].name)-1] = 0;
                        ++n;
                    }
                    client = client->next;
                }
                if (truncated) break;
                server = server->next;
            }
            user_trace_config_unlock();
            for (k = 0; k < n; ++k) {
                char name[384]; html_esc(name, sizeof(name), options[k].name);
                snprintf(out, sizeof(out), "<option value='%u'%s data-caid='%04X' data-provid='%06X' data-sid='%04X'>%s (ID %u، %s، آخر قناة %04X:%06X:%04X)</option>", options[k].id,
                    snap->active && snap->target.client_id == options[k].id ? " selected" : "",
                    options[k].caid, options[k].provider, options[k].sid, name, options[k].id,
                    options[k].connected ? "متصل" : "غير متصل", options[k].caid, options[k].provider, options[k].sid);
                tcp_writestr(&tcpbuf, sock, out);
            }
            free(options);
        }
        if (truncated) tcp_writestr(&tcpbuf, sock, "<option disabled>القائمة محدودة بأول 4096 عميل</option>");
        if (!options) tcp_writestr(&tcpbuf, sock, "<option disabled>تعذر تحميل العملاء</option>");
    }
    snprintf(out, sizeof(out), "</select></label><label>نطاق المراقبة <select name='mode' onchange='traceMode(this)'><option value='single'%s>قناة واحدة</option><option value='all'%s>كل قنوات العميل</option></select></label><label>CAID <input name='caid' maxlength='4' value='%04X'></label><label>Provider <input name='provid' maxlength='6' value='%06X'></label><label>SID <input name='sid' maxlength='4' value='%04X'></label><button type='submit'>بدء متابعة جديدة</button></form><form method='post' action='/user-trace'><input type='hidden' name='csrf' value='%s'><input type='hidden' name='action' value='stop'><button type='submit'>إيقاف</button></form>", snap->target.all_channels ? "" : " selected", snap->target.all_channels ? " selected" : "", snap->active ? snap->target.caid : 0, snap->active ? snap->target.provider : 0, snap->active ? snap->target.sid : 0, trace_csrf);
    tcp_writestr(&tcpbuf, sock, out);
    snprintf(out, sizeof(out), "<section id='trace-data'><p>الحالة: %s | العميل ID: %u | النطاق: %s <code>%04X:%06X:%04X</code> | أحداث مستبدلة: %llu | روابط مفقودة: %llu | أحداث مفقودة: %llu</p><p>تنبيه: تظهر CW للمشرفين المخوّلين فقط وتبقى في ذاكرة الجلسة حتى بدء جلسة جديدة أو إعادة التشغيل؛ استخدم شبكة إدارة آمنة.</p>", snap->active ? "تعمل" : "متوقفة", snap->target.client_id, snap->target.all_channels ? "كل القنوات" : "قناة واحدة", snap->target.caid, snap->target.provider, snap->target.sid, (unsigned long long)snap->overwritten, (unsigned long long)snap->dropped_links, (unsigned long long)snap->dropped_events);
    tcp_writestr(&tcpbuf, sock, out);
    tcp_writestr(&tcpbuf, sock, "<details><summary>كيف أقرأ أدلة الكاش؟</summary><p>عند امتلاء Consensus: used عدد الخانات المستعملة من الإجمالي، held خانات تنتظر انتهاء النافذة، overdue خانات محتجزة تجاوزت النافذة، reclaimable خانات غير محتجزة يمكن إعادة استخدامها، oldest hold عمر أقدم حجز وwindow مدة النافذة. الخانات التي ليست held هي سجلات غير محتجزة محفوظة مؤقتاً. هذه حالة الجدول العام عند الحدث، لا هوية أصحاب الخانات ولا سببًا مثبتًا لفشل هذا العميل.</p><p>في أحداث الدورة: incoming الدورة التي وصلت إلى مسار الفحص، وقد تكون محفوظة من أول رد عند انتهاء Consensus؛ expected المتوقعة في سجل الكاش وassessed الدورة التي قيّمها الفلتر. no marker تعني عدم وجود علامة دورة معروفة في هذا المسار؛ previous CW يبين توفر CW سابقة للمقارنة، وليس صحتها. اختلاف الدورة لا يثبت وحده أن المصدر خبيث أو أن الصورة لن تظهر.</p></details>");
    snprintf(out, sizeof(out), "<p>ردود CSP قصيرة جدًا لا تكفي لقراءة القناة: %llu خلال جلسة المتابعة، على مستوى السيرفر كله؛ لا يمكن نسبتها لهذا العميل أو قناته. أحداث CSP المرفوضة ذات الترويسة غير المتحقق منها تعرض قناة يدّعيها المرسل فقط، دون CW أو إثبات طلب العميل.</p>", (unsigned long long)snap->csp_unattributed_short);
    tcp_writestr(&tcpbuf, sock, out);
    trace_summary_write(&tcpbuf, sock, snap);
    tcp_writestr(&tcpbuf, sock, "<table><tr><th>الوقت</th><th>المحاولة</th><th>القناة</th><th>بصمة ECM غير فريدة</th><th>الحدث</th><th>النطاق</th><th>المصدر (نوع / ID داخلي)</th><th>المدة ms</th><th>التفاصيل</th><th>CW</th></tr>");
    for (i = 0; i < snap->count; ++i) {
        struct user_trace_event *e = &snap->events[i];
        char safe[768], timestr[32], attempt[32], elapsed[32], cw_html[160] = "—";
        time_t timestamp = e->timestamp;
        if (e->shared == 2) { strcpy(attempt, "—"); strcpy(elapsed, "—"); }
        else { snprintf(attempt, sizeof(attempt), "%llu", (unsigned long long)e->attempt); snprintf(elapsed, sizeof(elapsed), "%u", e->elapsed_ms); }
        html_esc(safe, sizeof(safe), e->detail);
        mcs_format_local_time(timestr, sizeof(timestr), timestamp, "%H:%M:%S");
        if (e->has_cw) {
            char hex[33]; size_t j;
            for (j = 0; j < 16; ++j) snprintf(hex + j * 2, 3, "%02X", e->cw[j]);
            snprintf(cw_html, sizeof(cw_html), "<details data-event='%llu'><summary>إظهار CW</summary><code>%s</code></details>", (unsigned long long)e->sequence, hex);
        }
        snprintf(out, sizeof(out), "<tr><td>%s</td><td>%s</td><td><code>%04X:%06X:%04X</code></td><td><code>%08X</code></td><td>%s</td><td>%s</td><td>%d / %d</td><td>%s</td><td>%s%s%s</td><td>%s</td></tr>", timestr, attempt, e->caid, e->provider, e->sid, e->ecm_hash, trace_type_name(e->type), e->shared == 2 ? "قناة فقط، ليس ربط عميل" : (e->shared ? "ECM مشترك" : "العميل"), e->source_type, e->source_id, elapsed, trace_detail_ar(e->detail), *trace_detail_ar(e->detail) ? " — " : "", safe, cw_html);
        tcp_writestr(&tcpbuf, sock, out);
    }
    tcp_writestr(&tcpbuf, sock, "</table>");
    tcp_writestr(&tcpbuf, sock, "</section><p><a href='/user-trace'>تحديث يدوي</a> | <a href='/'>الرئيسية</a></p><script>function traceMode(s){for(const n of ['caid','provid','sid'])document.querySelector('[name='+n+']').disabled=s.value==='all';}traceMode(document.querySelector('[name=mode]'));function traceChoose(s){const o=s.options[s.selectedIndex];const c=o.dataset.caid,p=o.dataset.provid,i=o.dataset.sid;document.querySelector('[name=caid]').value=c&&c!='0000'?c:'';document.querySelector('[name=provid]').value=p||'000000';document.querySelector('[name=sid]').value=i&&i!='0000'?i:'';}setInterval(async function(){try{const r=await fetch('/user-trace',{cache:'no-store'});if(!r.ok)return;const doc=new DOMParser().parseFromString(await r.text(),'text/html');const n=doc.getElementById('trace-data');if(n){const open=Array.from(document.querySelectorAll('#trace-data details[open]')).map(x=>x.dataset.event);const box=document.getElementById('trace-data');box.innerHTML=n.innerHTML;box.querySelectorAll('details').forEach(x=>{if(open.includes(x.dataset.event))x.open=true;});}}catch(e){}},5000);</script></body></html>");
    tcp_flush(&tcpbuf, sock);
    free(snap);
}
#endif

void *gererClient(struct connect_data *param)
{
	int sock = param->sock;
	uint32_t ip = param->ip;
	free(param);

	/* TASK R3 (D57): the optional allowlist closes the door before the
	 * request is even parsed -- one line, said once, with the address. */
	if ( !login_allowed(&cfg.http.allow, ip) ) {
		debugf(getdbgflag(DBG_HTTP,0,0)," [LOGIN ALLOW] http: %s is not on the allow list, connection closed pre-auth\n", ip2string(ip));
		close(sock);
		return NULL;
	}

	http_request req;

	struct pollfd pfd;
	pfd.fd = sock;
	pfd.events = POLLIN | POLLPRI;
	int retval = poll(&pfd, 1, 2000);

	//printf("\n*Connexion de %s(%d)\n", tt, sock); // print pid
	if (!dynbuf_init(&req.dbf, 1024)) {
		close(sock);
		return NULL;
	}

	if ( retval>0 )
	if ( pfd.revents & (POLLIN|POLLPRI) ) 
	if ( parse_http_request(sock, &req) ) {

		/* TASK R7 (D61): per-request skin state. ?lang= wins over the
		 * MCSLANG cookie; RTL then drives <html dir> and the menu labels.
		 * Cookies accumulate in req.setcookie and ride on the page's own
		 * 200 header (http_replyok_write). */
		req.rtl = 0; req.setcookie[0] = 0;
		{
			char *lg = isset_get(&req, "lang");
			char *ck = isset_header(&req, "Cookie");
			if (lg && !strcmp(lg, "ar")) { req.rtl = 1; http_add_cookie(&req, "MCSLANG=ar; Path=/; Max-Age=31536000"); }
			else if (lg && !strcmp(lg, "en")) { req.rtl = 0; http_add_cookie(&req, "MCSLANG=en; Path=/; Max-Age=31536000"); }
			else if (ck && strstr(ck, "MCSLANG=ar")) req.rtl = 1;
		}
		tls_http_rtl = req.rtl;

		/* TASK R7 (D61): /logout needs no credentials -- it can only
		 * invalidate the session token the caller already holds. Same
		 * door semantics: no data, one page, the cookie expires. */
		if ( (req.type==HTTP_GET) && !strcmp(req.path, "/logout") ) {
			http_session_drop(&req, ip);
			{
				struct tcp_buffer_data tcpbuf;
				tcp_init(&tcpbuf);
				http_replyok_write(&req, &tcpbuf, sock);
				tcp_writestr(&tcpbuf, sock, "<HTML><HEAD><title>Logged out</title></HEAD><BODY><p>Logged out.</p><p><a href='/'>Home</a></p></BODY></HTML>\n");
				tcp_flush(&tcpbuf, sock);
			}
		}
		else {
		int auth=0;
		if ( (req.type==HTTP_GET)||(req.type==HTTP_POST) ) {
			/* TASK 3.1 -- the open door. Stock checked cfg.http.user[0] TWICE
			 * and never looked at the password: a file with HTTP PASS set but
			 * no HTTP USER line served every page to everyone. Open only when
			 * NEITHER is configured (what an unconfigured stock server did);
			 * half-configured now requires Basic like a full one. */
			if (!cfg.http.user[0] && !cfg.http.pass[0]) auth = 1;
			else {
				int i;
				/* TASK R7 (D61): a valid session token authenticates by
				 * itself -- same door, no second password. Minting happens
				 * only where Basic actually matches below. */
				if ( http_session_check(&req, ip) ) auth = 1;
				for(i=0; i<req.hdrcount; i++) {
					if( !strcmp(req.headers[i].name,"Authorization") ) {
						//printf("Authorization: %s\n", req.headers[i].value);
						//get auth type
						if (!memcmp(req.headers[i].value, "Basic ",6)) {
							// get encrypted login
							char pass[256];
							char realpass[256];
							/* TASK 3.1 -- base64_pdecode() has no output bound:
							 * 340 base64 chars already decode past pass[256].
							 * Longer credentials cannot be real (user+pass are
							 * 64+64 in config.h); refuse them without decoding. */
							if (strlen(&req.headers[i].value[6]) > 340) break;
							base64_pdecode( &req.headers[i].value[6], pass);
							//printf("PASS: %s\n",pass);
/*
							time_t nowtime = time(NULL);
							struct tm *nowtm = localtime(&nowtime);
							sprintf(realpass,"%s:%s%d", cfg.http.user, cfg.http.pass,nowtm->tm_hour);
*/
							sprintf(realpass,"%s:%s", cfg.http.user, cfg.http.pass);
							if (!strcmp(pass,realpass)) {
							auth=1;
							/* TASK R7 (D61): one token per real login; a
							 * request that already presented a valid token
							 * keeps the one it has (no churn). */
							if ( !http_session_check(&req, ip) ) http_session_mint(&req, ip);
						}
						}
						break;
					}
				}
			}
			if ( auth ) {
				login_note_success(ip); /* TASK R3 (D57): a good login resets the failure count */
				req.sock = sock;
				req.ip = ip;
				if (strcmp(req.path,"/")==0) http_send_index(sock,&req);
				else if (strcmp(req.path,"/debug")==0) {
					if (!cfg.http.show.nodebug) http_send_debug(sock,&req);
				}
				else if (strcmp(req.path,"/profiles")==0) {
					if (!cfg.http.show.noprofiles) http_send_profiles(sock,&req);
				}
				else if (strcmp(req.path,"/profile")==0) {
					if (!cfg.http.show.noprofiles) http_send_profile(sock,&req);
				}
				else if (strcmp(req.path,"/newcamd")==0) {
					if (!cfg.http.show.nonewcamd) http_send_newcamd(sock,&req);
				}
				else if (strcmp(req.path,"/newcamdclient")==0) {
					if (!cfg.http.show.nonewcamd) http_send_newcamd_client(sock,&req);
				}
				else if (strcmp(req.path,"/servers")==0) {
					if (!cfg.http.show.noservers) http_send_servers(sock,&req);
				}
				else if (strcmp(req.path,"/server")==0) {
					if (!cfg.http.show.noservers) http_send_server(sock,&req);
				}
				else if (strcmp(req.path,"/cache")==0) {
					if (!cfg.http.show.nocache) http_send_cache(sock,&req);
				}
				else if (strcmp(req.path,"/cachepeer")==0) {
					if (!cfg.http.show.nocache) http_send_cache_peer(sock,&req);
				}
				else if (strcmp(req.path,"/biss")==0) {
					http_send_biss(sock,&req);
				}
#ifdef CCCAM_SRV
				else if (strcmp(req.path,"/cccam")==0) {
					if (!cfg.http.show.nocccam) http_send_cccam(sock,&req);
				}
				else if (strcmp(req.path,"/cccamclient")==0) {
					if (!cfg.http.show.nocccam) http_send_cccam_client(sock,&req);
				}
#endif
#ifdef CS378X_SRV
				else if (strcmp(req.path,"/cs378x")==0) {
					http_send_cs378x(sock,&req);
				}
#endif
#ifdef CAMD35_SRV
				else if (strcmp(req.path,"/camd35")==0) {
					http_send_camd35(sock,&req);
				}
#endif
#ifdef CACHEEX
				else if (strcmp(req.path,"/cacheex")==0) {
					http_send_cacheex(sock,&req);
				}
#endif
#ifdef FREECCCAM_SRV
				else if (strcmp(req.path,"/freecccam")==0) {
					http_send_freecccam(sock,&req);
				}
#endif
#ifdef MGCAMD_SRV
				else if (strcmp(req.path,"/mgcamd")==0) {
					if (!cfg.http.show.nomgcamd) http_send_mgcamd(sock,&req);
				}
				else if (strcmp(req.path,"/mgcamdclient")==0) {
					if (!cfg.http.show.nomgcamd) http_send_mgcamd_client(sock,&req);
				}
#endif
				else if (strcmp(req.path,"/restart")==0) {
					if (!cfg.http.show.norestart) http_send_restart(sock,&req);
				}
				else if (strcmp(req.path,"/diagnostics")==0) {
					http_send_diagnostics(sock,&req);
				}
				else if (strcmp(req.path,"/debug-ecm")==0) {
					http_send_debug_ecm(sock,&req);
				}
				else if (strcmp(req.path,"/server-stats")==0) {
					http_send_server_stats(sock,&req);
				}
#ifdef CCCAM_SRV
				else if (strcmp(req.path,"/user-trace")==0) {
					if (!cfg.http.show.nocccam) http_send_user_trace(sock,&req);
				}
#endif
				else if (strcmp(req.path,"/log-viewer")==0) {
					debugf(0, " [HTTP] /log-viewer requested\n");
					http_send_log_viewer(sock,&req);
				}
				else if (strcmp(req.path,"/cache-analytics")==0) {
					http_send_cache_analytics(sock,&req);
				}
				else if (strcmp(req.path,"/auto-tuning")==0) {
					http_send_auto_tuning(sock,&req);
				}
				else if ( !memcmp(req.path,"/editor",7) )  {
					if (!cfg.http.show.noeditor) {
						int index=0;
						if ( (req.path[7]>='0')&&(req.path[7]<='9') ) index = req.path[7] - '0';
						if ( (req.path[8]>='0')&&(req.path[8]<='9') ) index = (index*10) + (req.path[8]-'0');
						http_send_editor(sock,&req, index);
					}
				}
				else if (strcmp(req.path,"/style.css")==0) {
					if (strlen(cfg.stylesheet_file)) {
						http_send_file(sock, &req, "text/css", cfg.stylesheet_file);
					}
					else http_send_answer(sock, &req, "text/css", style_css, strlen(style_css));
				}
				else if (strcmp(req.path,"/connect.png")==0) {
					http_send_image(sock, &req, connect_png, sizeof(connect_png), "png");
				}
				else if (strcmp(req.path,"/disconnect.png")==0) {
					http_send_image(sock, &req, disconnect_png, sizeof(disconnect_png), "png");
				}
				else if (strcmp(req.path,"/enable.png")==0) {
					http_send_image(sock, &req, enable_png, sizeof(enable_png), "png");
				}
				else if (strcmp(req.path,"/disable.png")==0) {
					http_send_image(sock, &req, disable_png, sizeof(disable_png), "png");
				}
				else if (strcmp(req.path,"/debug.png")==0) {
					http_send_image(sock, &req, debug_png, sizeof(debug_png), "png");
				}
				else if (strcmp(req.path,"/refresh.png")==0) {
					http_send_image(sock, &req, refresh_png, sizeof(refresh_png), "png");
				}
				else if (strcmp(req.path,"/favicon.png")==0) {
					http_send_image(sock, &req, favicon_png, sizeof(favicon_png), "png");
				}
				else if (strcmp(req.path,"/sms_new.gif")==0) {
					http_send_image(sock, &req, sms_new_gif, sizeof(sms_new_gif), "gif");
				}
				else if (strcmp(req.path,"/sms_old.gif")==0) {
					http_send_image(sock, &req, sms_old_gif, sizeof(sms_old_gif), "gif");
				}
				else if (strcmp(req.path,"/host")==0) {
					http_send_host(sock,&req);
				}
				else if (strcmp(req.path,"/threads")==0) {
					http_send_threads(sock,&req);
				}
				else if (strcmp(req.path,"/json")==0) { /* TASK R5 (D59) */
					http_send_json(sock,&req);
				}
#ifdef MCS_DCWSTATS
				else if (strcmp(req.path,"/cwlog")==0) { /* TASK R11 (D65): the verdict page */
					http_send_cwlog(sock,&req);
				}
#endif
				else if ( !memcmp(req.path,"/flag_",6) && !memcmp(req.path+8,".gif",4) ) {
					// check for code
					char code[3];
					code[0] = req.path[6];
					code[1] = req.path[7];
					code[2] = 0;
					int i;
					for(i=0; i<MAX_COUNTRY_IMAGES; i++) {
						if ( !strcmp(country_images[i].code, code) ) {
							http_send_image(sock, &req, country_images[i].data, country_images[i].len, "gif");
							break;
						}
					}
					if (i>=MAX_COUNTRY_IMAGES) http_send_image(sock, &req, country_images[0].data, country_images[0].len, "gif");
				}

				else {
					struct http_file_data *file = cfg.http.files;
					while (file) {
						if ( !strcmp(req.path,file->url) ) {
							http_send_file( sock, &req, file->mime, file->path);
							break;
						}
						file = file->next;
					}
				}
			}
			else { // send( client_sock, (char*)data, strlen(data),0);
				//printf("%s\n", http_buf);
				/* TASK R3 (D57): the progressive delay, counted per source
				 * address across both doors. The n-th consecutive failure
				 * waits base<<(n-1) ms, capped, in THIS connection's own
				 * thread, before the stock 401 goes out. Off = instant 401
				 * exactly as stock. */
				{
					uint32_t lt_fails = login_note_failure(ip);
					uint32_t lt_wait = login_delay_ms(cfg.http.logindelay, lt_fails);
					if (lt_wait) debugf(getdbgflag(DBG_HTTP,0,0)," [LOGIN THROTTLE] http: failure #%u from %s, delaying %ums\n", lt_fails, ip2string(ip), lt_wait);
					if (lt_wait) usleep(lt_wait*1000);
				}
				struct tcp_buffer_data tcpbuf;
				char auth[1024];
				tcp_init(&tcpbuf);
				sprintf( auth, "HTTP/1.1 401 Unauthorized\r\nWWW-Authenticate: Basic realm=\"%s\"\r\nVary: Accept-Encoding\r\nConnection: close\r\nContent-Type: text/html\r\n\r\n<HTML><HEAD><TITLE>%s</TITLE></HEAD><BODY><H2>Access forbidden, authorization required</H2></BODY></HTML>", cfg.http.title, cfg.http.title);
				tcp_write(&tcpbuf, sock, auth, strlen(auth) );
				tcp_flush(&tcpbuf, sock);
				//return;
			}
		}
		} /* TASK R7: the else of /logout */
	}

	dynbuf_free(&req.dbf);

	//printf("*Deconnexion de %s(%d)\n", tt, sock);

	if ( close(sock) ) debugf(getdbgflag(DBG_HTTP,0,0)," HTTP Server: socket close failed(%d)\n",sock);

	return NULL;
}

/* ==========================================================================
 * BISS Key Management Page
 * ========================================================================== */
void http_send_biss(int sock, struct http_request *req)
{
	struct tcp_buffer_data tcpbuf;
	char *str_action = isset_get(req, "action");
	int action = 0; // 0=page, 1=add, 2=delete

	if (str_action) {
		if (!strcmp(str_action, "add")) action = 1;
		else if (!strcmp(str_action, "delete")) action = 2;
	}

	tcp_init(&tcpbuf);

	// Handle add action
	if (action == 1) {
		char *str_caid = isset_get(req, "caid");
		char *str_sid = isset_get(req, "sid");
		char *str_provid = isset_get(req, "provid");
		char *str_key = isset_get(req, "key");

		if (str_caid && str_sid && str_provid && str_key) {
			uint16_t caid = strtoul(str_caid, NULL, 16);
			uint16_t sid = strtoul(str_sid, NULL, 16);
			uint32_t provid = strtoul(str_provid, NULL, 16);
			/* TASK R26: a 32-hex key is a BISS2 session word
			 * (16 bytes, CAID 2610); 16 hex stays BISS1. */
			if (strlen(str_key) >= 32) {
				uint8_t key2[16];
				int i;
				for (i = 0; i < 16; i++) {
					char hex[3] = {str_key[i*2], str_key[i*2+1], 0};
					key2[i] = strtoul(hex, NULL, 16);
				}
				if (biss2_add(caid, sid, provid, key2)) {
					debugf(0, "BISS2: Added session word for %04X:%06X:%04X\n", caid, provid, sid);
				}
			} else if (strlen(str_key) >= 16) {
				uint8_t key[8];
				int i;
				for (i = 0; i < 8; i++) {
					char hex[3] = {str_key[i*2], str_key[i*2+1], 0};
					key[i] = strtoul(hex, NULL, 16);
				}

				if (biss_add(caid, sid, provid, key)) {
					debugf(0, "BISS: Added key for %04X:%06X:%04X\n", caid, provid, sid);
				}
			}
		}
	}

	// Handle delete action
	if (action == 2) {
		char *str_caid = isset_get(req, "caid");
		char *str_sid = isset_get(req, "sid");
		char *str_provid = isset_get(req, "provid");

		if (str_caid && str_sid && str_provid) {
			uint16_t caid = strtoul(str_caid, NULL, 16);
			uint16_t sid = strtoul(str_sid, NULL, 16);
			uint32_t provid = strtoul(str_provid, NULL, 16);

			if (biss_remove(caid, sid, provid)) {
				debugf(0, "BISS: Removed key for %04X:%06X:%04X\n", caid, provid, sid);
			}
			if (biss2_remove(caid, sid, provid)) {
				debugf(0, "BISS2: Removed session word for %04X:%06X:%04X\n", caid, provid, sid);
			}
		}
	}

	// Send HTML header
	char http_buf[8192];
	sprintf(http_buf,
		"HTTP/1.1 200 OK\r\n"
		"Content-Type: text/html\r\n"
		"Connection: close\r\n\r\n"
		"<html><head><title>BISS Keys</title>"
		"<style>"
		"body { font-family: Arial; margin: 20px; }"
		"h1 { color: #333; }"
		"table { border-collapse: collapse; width: 100%%; margin-top: 20px; }"
		"th, td { border: 1px solid #ddd; padding: 8px; text-align: left; }"
		"th { background-color: #4CAF50; color: white; }"
		"tr:nth-child(even) { background-color: #f2f2f2; }"
		".form-box { background: #f9f9f9; padding: 20px; margin: 20px 0; border-radius: 5px; }"
		"input[type=text] { padding: 8px; margin: 5px; width: 150px; }"
		"input[type=submit] { padding: 10px 20px; background: #4CAF50; color: white; border: none; cursor: pointer; }"
		"input[type=submit]:hover { background: #45a049; }"
		".delete-btn { background: #f44336; color: white; padding: 5px 10px; text-decoration: none; border-radius: 3px; }"
		".delete-btn:hover { background: #da190b; }"
		"</style></head><body>"
		"<h1>🔐 BISS Key Management</h1>"
		"<p><a href='/'>← Back to Home</a></p>"
	);
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf));

	// Add key form
	sprintf(http_buf,
		"<div class='form-box'>"
		"<h2>Add New BISS Key</h2>"
		"<form method='GET' action='/biss'>"
		"<input type='hidden' name='action' value='add'>"
		"CAID: <input type='text' name='caid' placeholder='2600' required>"
		"SID: <input type='text' name='sid' placeholder='1234' required>"
		"PROVID: <input type='text' name='provid' placeholder='000000' required>"
		"Key: <input type='text' name='key' placeholder='0123456789ABCDEF' maxlength='32' required>"
		"<input type='submit' value='Add Key'>"
		"</form>"
		"<small>BISS1 (CAID 2600): 16 hex (8 bytes) &nbsp;|&nbsp; BISS2 (CAID 2610): 32 hex (16-byte session word)</small>"
		"</div>"
	);
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf));

	// Display keys table
	sprintf(http_buf,
		"<h2>Stored BISS Keys (%d total)</h2>"
		"<table>"
		"<tr><th>CAID</th><th>SID</th><th>PROVID</th><th>Key</th><th>Added</th><th>Used</th><th>Actions</th></tr>",
		biss_count()
	);
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf));

	// List all keys
	int i;
	for (i = 0; i < biss_keys.count; i++) {
		if (biss_keys.keys[i].active) {
			char key_str[17];
			int j;
			for (j = 0; j < 8; j++) {
				sprintf(key_str + j*2, "%02X", biss_keys.keys[i].key[j]);
			}
			key_str[16] = 0;

			char added_time[32] = "Never";
			char last_used[32] = "Never";
			if (biss_keys.keys[i].added_time > 0) {
				time_t t = biss_keys.keys[i].added_time;
				strftime(added_time, sizeof(added_time), "%Y-%m-%d %H:%M", localtime(&t));
			}
			if (biss_keys.keys[i].last_used > 0) {
				time_t t = biss_keys.keys[i].last_used;
				strftime(last_used, sizeof(last_used), "%Y-%m-%d %H:%M", localtime(&t));
			}

			sprintf(http_buf,
				"<tr>"
				"<td>%04X</td>"
				"<td>%04X</td>"
				"<td>%06X</td>"
				"<td><code>%s</code></td>"
				"<td>%s</td>"
				"<td>%s (%d times)</td>"
				"<td><a class='delete-btn' href='/biss?action=delete&caid=%04X&sid=%04X&provid=%06X' onclick='return confirm(\"Delete this key?\")'>Delete</a></td>"
				"</tr>",
				biss_keys.keys[i].caid,
				biss_keys.keys[i].sid,
				biss_keys.keys[i].provid,
				key_str,
				added_time,
				last_used,
				biss_keys.keys[i].use_count,
				biss_keys.keys[i].caid,
				biss_keys.keys[i].sid,
				biss_keys.keys[i].provid
			);
			tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf));
		}
	}

	// TASK R26: BISS2 session words (separate table, 16-byte keys)
	sprintf(http_buf,
		"</table>"
		"<h2>Stored BISS2 Session Words (%d total)</h2>"
		"<table>"
		"<tr><th>CAID</th><th>SID</th><th>PROVID</th><th>Session Word</th><th>Used</th><th>Actions</th></tr>",
		biss2_count()
	);
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf));
	for (i = 0; i < biss2_keys.count; i++) {
		if (biss2_keys.keys[i].active) {
			char key_str[33];
			int j;
			for (j = 0; j < 16; j++) {
				sprintf(key_str + j*2, "%02X", biss2_keys.keys[i].key[j]);
			}
			key_str[32] = 0;
			sprintf(http_buf,
				"<tr>"
				"<td>%04X</td>"
				"<td>%04X</td>"
				"<td>%06X</td>"
				"<td><code>%s</code></td>"
				"<td>%d times</td>"
				"<td><a class='delete-btn' href='/biss?action=delete&caid=%04X&sid=%04X&provid=%06X' onclick='return confirm(\"Delete this key?\")'>Delete</a></td>"
				"</tr>",
				biss2_keys.keys[i].caid,
				biss2_keys.keys[i].sid,
				biss2_keys.keys[i].provid,
				key_str,
				biss2_keys.keys[i].use_count,
				biss2_keys.keys[i].caid,
				biss2_keys.keys[i].sid,
				biss2_keys.keys[i].provid
			);
			tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf));
		}
	}

	// Close table and page
	sprintf(http_buf,
		"</table>"
		"<p style='margin-top: 20px; color: #666;'>"
		"💡 <strong>Tip:</strong> BISS keys are used automatically when ECM requests match the CAID/SID/PROVID."
		"</p>"
		"</body></html>"
	);
	tcp_write(&tcpbuf, sock, http_buf, strlen(http_buf));

	tcp_flush(&tcpbuf, sock);
}


void *http_thread(void *param)
{
	int clientsock;
	struct sockaddr_in client_addr;
	socklen_t socklen = sizeof(client_addr);

	prctl(PR_SET_NAME,"HTTP Server",0,0,0);

	while (!prg.restart) {
		if (cfg.http.handle>0) {
			//pthread_mutex_lock(&prg.lockhttp);

			struct pollfd pfd;
			pfd.fd = cfg.http.handle;
			pfd.events = POLLIN | POLLPRI;
			int retval = poll(&pfd, 1, 3002);
			if ( retval>0 ) {
				if ( pfd.revents & (POLLIN|POLLPRI) ) {
					clientsock = accept(cfg.http.handle, (struct sockaddr*)&client_addr, /*(socklen_t*)*/&socklen);
					if ( clientsock<0 ) {
						debugf(getdbgflag(DBG_HTTP,0,0)," HTTP Server: Accept Error\n");
						break;
					}
					else {
						//SetSocketNoDelay(clientsock);
						pthread_t cli_tid;
						struct connect_data *newdata = malloc( sizeof(struct connect_data) );
						newdata->sock = clientsock; 
						newdata->ip = client_addr.sin_addr.s_addr;
						if (!create_thread(&cli_tid, (threadfn)gererClient, newdata)) {
							close(clientsock);
							free( newdata );
						}
					}
				}
			}
			else if (retval<0) {
				debugf(getdbgflag(DBG_HTTP,0,0)," THREAD HTTP: poll error %d(errno=%d)\n", retval, errno);
				usleep(99000);
			}
			//pthread_mutex_unlock(&prg.lockhttp);
			//usleep(10);
		} else usleep(100000);
	}// While

	debugf(getdbgflag(DBG_HTTP,0,0),"Exiting HTTP Thread\n");
	return NULL;
}

pthread_t http_tid;
int start_thread_http()
{
	create_thread(&http_tid, http_thread, NULL);
	return 0;
}

