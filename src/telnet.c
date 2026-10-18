#include "common.h"
#ifdef MCS_DCWSTATS
#include "chanhealth.h"	/* TASK R47 (D110): dcwstats reset clears the channel table */
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
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

#include "telnet.h"
#include "main.h"
#include "parser.h"
#include "httpserver.h"
#include "peerrep.h"		/* TASK R4 (D58): unconditional -- PEERREP answers in both flavours */
#ifdef MCS_DCWSTATS
#include "dcwstats.h"	/* TASK 3.11: the readout and reset the header promised */
#endif

int writes( int fd, char *str )
{
	return write(fd, str, strlen(str) );
}

char *getcountrycodebyip(uint32_t ip);

struct telnet_conn_data { /* TASK R3 (D57) */
	int fd;
	uint32_t ip;
};

#ifdef MCS_DCWSTATS
/* TASK R48 (D111): one line back for a `diagscope add|del` edit. The codes
 * are those of diagscope.h. */
static void diagscope_say(int fd, int add, const char *what, const char *name, int rc)
{
	const char *m;

	if (add) {
		if (rc == 0) m = "added";
		else if (rc == 1) m = "already in scope";
		else if (rc == -1) m = "no room: at most 4 channels and 4 profiles; del one first";
		else m = "empty, or longer than 63 characters";
	}
	else m = (rc == 0) ? "removed" : "not in scope";
	char line[160];

	sprintf(line, "diagscope: %s %.63s %s\r\n", what, name, m);
	writes(fd, line);
}
#endif

void *telnetprocess(int *param )
{
	/* TASK R3 (D57): the thread's own connection data, now with the
	 * source address. Every wait below burns THIS thread only. */
	struct telnet_conn_data *tcd = (struct telnet_conn_data *)param;
	int fd = tcd->fd;
	uint32_t myip = tcd->ip;
	free(param);

	char buf[4096];
	char str[256];
	char wbuf[4096];
	int len;
	/* TASK R3 (D57): the optional allowlist closes the door before any
	 * credential is read -- one line, said once, with the address. */
	if ( !login_allowed(&cfg.telnet.allow, myip) ) {
		debugf(getdbgflag(DBG_HTTP,0,0)," [LOGIN ALLOW] telnet: %s is not on the allow list, connection closed pre-auth\n", ip2string(myip));
		close(fd);
		return NULL;
	}
	//
	writes(fd, "Welcome to Telnet Server\r\n\r\nLogin: ");

	len = recv( fd, buf, sizeof(buf), MSG_NOSIGNAL);
	if (len<=0) { close(fd); return NULL; }
	//printf(" Received(%d): '%s'\r\n", len, buf);
	if ( (buf[len-2]!='\r')||(buf[len-1]!='\n') )  { close(fd); return NULL; }
	buf[len-2] = 0;
	if ( strcmp(buf, cfg.telnet.user) ) {
		/* TASK R3 (D57): the progressive delay. The n-th consecutive
		 * failure from this address waits base<<(n-1), capped; a good
		 * login resets the count. The answer itself is stock. */
		uint32_t lt_fails = login_note_failure(myip);
		uint32_t lt_wait = login_delay_ms(cfg.telnet.logindelay, lt_fails);
		if (lt_wait) debugf(getdbgflag(DBG_HTTP,0,0)," [LOGIN THROTTLE] telnet: failure #%u from %s, delaying %ums\n", lt_fails, ip2string(myip), lt_wait);
		if (lt_wait) usleep(lt_wait*1000);
		writes(fd, "wrong username, bye.\r\n");
		close(fd);
		return NULL;
	}
	//
	writes(fd, "Password: ");
	len = recv( fd, buf, sizeof(buf), MSG_NOSIGNAL);
	if (len<=0) { close(fd); return NULL; }
	//printf(" Received(%d): '%s'\r\n", len, buf);
	if ( (buf[len-2]!='\r')||(buf[len-1]!='\n') )  { close(fd); return NULL; }
	buf[len-2] = 0;
	if ( strcmp(buf, cfg.telnet.pass) ) {
		uint32_t lt_fails = login_note_failure(myip); /* TASK R3 (D57) */
		uint32_t lt_wait = login_delay_ms(cfg.telnet.logindelay, lt_fails);
		if (lt_wait) debugf(getdbgflag(DBG_HTTP,0,0)," [LOGIN THROTTLE] telnet: failure #%u from %s, delaying %ums\n", lt_fails, ip2string(myip), lt_wait);
		if (lt_wait) usleep(lt_wait*1000);
		writes(fd, "wrong password, bye.\r\n");
		close(fd);
		return NULL;
	}
	login_note_success(myip); /* TASK R3 (D57): a good login forgives the typos */
	strcpy(buf, "\r\n\r\ntype 'help' for command list\r\n");write(fd, buf, strlen(buf) );

	while ( 1 ) {
		writes(fd, "\r\n[command]: ");
		len = recv( fd, buf, sizeof(buf), MSG_NOSIGNAL);
		if (len<=0) { close(fd); return NULL; }
		//printf(" Received(%d): %s", len, buf);
		if ( (buf[len-2]!='\r')||(buf[len-1]!='\n') )  { close(fd); return NULL; }
		buf[len-2] = 0;

		iparser = buf;
		parse_spaces();
		if (*iparser==0) continue;
		if (!parse_name(str)) continue;
		uppercase(str);

		if ( !strcmp(str, "EXIT") || !strcmp(str, "QUIT") ) {
			writes(fd, "bye.\r\n");
			break;
		}
		else if ( !strcmp(str, "UPTIME") ) {
			unsigned int d= GetTickCount()/1000;
			sprintf( wbuf,"Uptime: %02dd %02d:%02d:%02d\r\n", d/(3600*24), (d/3600)%24, (d/60)%60, d%60);
			writes(fd, wbuf);
		}
		else if ( !strcmp(str, "STAT") ) {
			sprintf( wbuf,"Total Profiles: %d\r\nTotal Servers: %d\r\nTotal Cache Servers: %d\r\nTotal CCcam Servers: %d\r\n", cfg.totalprofiles, cfg.totalservers, cfg.cache.totalservers, cfg.cccam.totalservers);
			writes(fd, wbuf);
		}
		else if ( !strcmp(str, "DEBUG") ) {
			int i=idbgline;
			do {
				sprintf( wbuf, "%s", dbgline[i] );
				writes(fd, wbuf);
				i++;
				if (i>=MAX_DBGLINES) i=0;
			} while (i!=idbgline);
		}
		else if ( !strcmp(str, "LOADAVG") ) {
			FILE *fp = fopen ("/proc/loadavg", "r");
			if ( fgets(wbuf, sizeof(wbuf), fp) ) writes(fd, wbuf);
			fclose(fp);
		}
		else if ( !strcmp(str, "MEMINFO") ) {
			FILE *fp = fopen ("/proc/meminfo", "r");
			if ( fgets(wbuf, sizeof(wbuf), fp) ) writes(fd, wbuf);
			if ( fgets(wbuf, sizeof(wbuf), fp) ) writes(fd, wbuf);
			fclose(fp);
		}
		else if ( !strcmp(str, "CPUINFO") ) {
			FILE *fp = fopen ("/proc/cpuinfo", "r");
			while ( fgets(wbuf, sizeof(wbuf), fp) ) writes(fd, wbuf);
			fclose(fp);
		}
		else if ( !strcmp(str, "SCHED") ) {
			if (parse_name(str)) {
				uppercase(str);
				if ( !strcmp(str, "CCCAM") ) {
					sprintf( str, "/proc/%d/sched", prg.pid_cc_msg);
					FILE *fp = fopen ( str, "r");
					while ( fgets(wbuf, sizeof(wbuf), fp) ) writes(fd, wbuf);
					fclose(fp);
				}
				else if ( !strcmp(str, "NEWCAMD") ) {
					sprintf( str, "/proc/%d/sched", prg.pid_cs_msg);
					FILE *fp = fopen ( str, "r");
					while ( fgets(wbuf, sizeof(wbuf), fp) ) writes(fd, wbuf);
					fclose(fp);
				}
				else if ( !strcmp(str, "ECM") ) {
					sprintf( str, "/proc/%d/sched", prg.pid_msg);
					FILE *fp = fopen ( str, "r");
					while ( fgets(wbuf, sizeof(wbuf), fp) ) writes(fd, wbuf);
					fclose(fp);
				}
				else if ( !strcmp(str, "MGCAMD") ) {
					sprintf( str, "/proc/%d/sched", prg.pid_mg_msg);
					FILE *fp = fopen ( str, "r");
					while ( fgets(wbuf, sizeof(wbuf), fp) ) writes(fd, wbuf);
					fclose(fp);
				}
#ifdef CACHEEX
				else if ( !strcmp(str, "CACHEEX") ) {
					sprintf( str, "/proc/%d/sched", prg.pid_ccex_msg);
					FILE *fp = fopen ( str, "r");
					while ( fgets(wbuf, sizeof(wbuf), fp) ) writes(fd, wbuf);
					fclose(fp);
				}
#endif
				else if ( !strcmp(str, "CACHE") ) {
					sprintf( str, "/proc/%d/sched", prg.pid_cache);
					FILE *fp = fopen ( str, "r");
					while ( fgets(wbuf, sizeof(wbuf), fp) ) writes(fd, wbuf);
					fclose(fp);
				}
			}
			else {
				sprintf( str, "/proc/%d/sched", prg.pid_main);
				FILE *fp = fopen ( str, "r");
				while ( fgets(wbuf, sizeof(wbuf), fp) ) writes(fd, wbuf);
				fclose(fp);
			}
		}
		/*
		 * TASK 3.11 -- dcwstats / dcwstats reset.
		 *
		 * The counters and dcwstats_reset() have existed since the Phase-4
		 * instrumentation; this is the operator's hand on them. The line
		 * format is `<reason> <count>` using dcwstats_reason_name(), so the
		 * web page (3.12) can print the same names and the two readouts
		 * cannot drift. A second word other than RESET is a usage line, not
		 * a silent no-op and not a reset.
		 *
		 * A stock build has no counters and must not reference the symbol.
		 * It still answers, once, and says which build to use -- the same
		 * config and the same command on both releases.
		 */
		else if ( !strcmp(str, "PEERREP") ) { /* TASK R4 (D58) */
			if (!peerrep_on) {
				writes(fd, "peerrep: OFF\r\n");
			}
			else {
				int i, n = peerrep_count();
				uint32_t ip; uint16_t port; int stage, events; char reason[32];
				static const char *const sn[] = { "monitor", "distrust", "isolate", "ban" };
				sprintf(wbuf, "peerrep: ON\r\n");
				writes(fd, wbuf);
				for (i=0; i<n; i++) {
					if (!peerrep_get(i, &ip, &port, &stage, &events, reason, sizeof(reason))) break;
					sprintf(wbuf, "%u.%u.%u.%u:%u\tstage=%s\tevents=%d\tlast='%s'\r\n",
						0xFF&(ip), 0xFF&(ip>>8), 0xFF&(ip>>16), 0xFF&(ip>>24), port,
						sn[stage & 3], events, reason);
					writes(fd, wbuf);
				}
			}
		}
#ifdef MCS_DCWSTATS
		else if ( !strcmp(str, "DIAGSCOPE") ) { /* TASK R48 (D111): stats flavour only, the stock binary has no such command */
			struct diag_scope sc;
			unsigned cu, pu, su;
			char verb[16], what[16], line[160], nm[40];
			int i, rc;

			if (!parse_name(verb)) {
				diagscope_snapshot(&sc);
				sprintf(wbuf, "diagscope: detail is %s, counters are always on\r\n",
					diag_scope_empty(&sc) ? "OFF" : "ON");
				writes(fd, wbuf);
				for (i = 0; i < sc.nch; i++) {
					sprintf(wbuf, "  channel %04X:%06X:%04X\r\n", (unsigned)sc.caid[i],
						(unsigned)sc.provid[i], (unsigned)sc.sid[i]);
					writes(fd, wbuf);
				}
				for (i = 0; i < sc.nprof; i++) {
					sprintf(wbuf, "  profile %s\r\n", sc.prof[i]);
					writes(fd, wbuf);
				}
				writes(fd, "usage: diagscope add channel CAID:PROV:SID | add profile NAME | del channel CAID:PROV:SID | del profile NAME | clear\r\n");
			}
			else {
				uppercase(verb);
				if (!strcmp(verb, "CLEAR")) {
					diagscope_clear();
					writes(fd, "diagscope: cleared, detail is OFF\r\n");
				}
				else if ((!strcmp(verb, "ADD") || !strcmp(verb, "DEL")) && parse_name(what)) {
					int add = !strcmp(verb, "ADD");

					uppercase(what);
					if (!strcmp(what, "CHANNEL")) {
						parse_spaces();
						if (sscanf(iparser, "%x:%x:%x", &cu, &pu, &su) != 3 || cu > 0xFFFF || su > 0xFFFF) {
							writes(fd, "diagscope: a channel is CAID:PROV:SID in hex, e.g. 1884:000000:1136\r\n");
						}
						else {
							rc = add ? diagscope_add_channel(cu, pu, (uint16_t)su)
								  : diagscope_del_channel(cu, pu, (uint16_t)su);
							sprintf(nm, "%04X:%06X:%04X", (unsigned)cu, (unsigned)pu, (unsigned)su);
							diagscope_say(fd, add, "channel", nm, rc);
						}
					}
					else if (!strcmp(what, "PROFILE")) {
						int n;

						parse_spaces();
						strncpy(line, iparser, sizeof(line) - 1);
						line[sizeof(line) - 1] = 0;
						n = strlen(line);
						while (n > 0 && (line[n - 1] == ' ' || line[n - 1] == '\t')) line[--n] = 0;
						rc = add ? diagscope_add_profile(line) : diagscope_del_profile(line);
						diagscope_say(fd, add, "profile", line, rc);
					}
					else writes(fd, "diagscope: usage: add|del channel CAID:PROV:SID, add|del profile NAME\r\n");
				}
				else writes(fd, "diagscope: usage: diagscope | add|del channel|profile ... | clear\r\n");
			}
		}
#endif /* MCS_DCWSTATS: DIAGSCOPE */
		else if ( !strcmp(str, "DCWSTATS") ) {
#ifdef MCS_DCWSTATS
			if (parse_name(str)) {
				uppercase(str);
				if ( !strcmp(str, "RESET") ) {
					dcwstats_reset();
					chanhealth_reset();	/* TASK R47 (D110): the /diag totals reset too */
					writes(fd, "dcwstats: counters reset\r\n");
				}
				else writes(fd, "dcwstats: usage: dcwstats [reset]\r\n");
			}
			else {
				int i;
				sprintf(wbuf, "dcwstats: %s\r\n", dcwstats_on ? "ON" : "OFF");
				writes(fd, wbuf);
				for (i = 0; i < DCW_REJ_COUNT; i++) {
					sprintf(wbuf, "%s %lu\r\n", dcwstats_reason_name(i), dcwstats[i]);
					writes(fd, wbuf);
				}
			}
#else
			parse_name(str);
			writes(fd, "dcwstats: this build has no counters; use the stats release\r\n");
#endif
		}
		else if ( !strcmp(str, "HELP") ) {
			writes(fd, " Commands: help - uptime - stat - cccam - mgcamd - debug - loadavg - cpuinfo - meminfo - dcwstats - exit/quit\r\n");
		}
		else if (!strcmp(str,"CCCAM")) {
			if (!parse_int(str)) {
				int index = 0;
				struct cccam_server_data *srv = cfg.cccam.server;
				while (srv) {
					index++;
					sprintf( wbuf, "id:%d  Port:%d", srv->id, srv->port);
					if (srv->handle>0) strcat( wbuf, " Status:ON "); else strcat( wbuf, " Status:OFF");
					int total, connected, active;
					cccam_clients( srv, &total, &connected, &active );
					sprintf( str, "Clients Total:%d, Connected:%d, Active:%d", total, connected, active );
					strcat( wbuf, str );
					strcat( wbuf, "\r\n");
					writes(fd, wbuf);
					srv = srv->next;
				}
				sprintf(wbuf, "Total CCcam servers = %d\r\n", index);
				writes(fd, wbuf);
			}
			else { // cccam <cientname>
				struct cccam_server_data *cccam = getcccamserverbyid( atoi(str) );
				if (!cccam) continue;
				if (!parse_name(str)) continue;
				struct cc_client_data *cli = getcccamclientbyname( cccam, str );
				if (!cli) continue;
				if (cli->connection.status>0) {
					char *p = getcountrycodebyip(cli->ip);
					if (p) 
						sprintf(wbuf, "<tr><td>%s</td><td>%s</td><td class=online>Connected</td><td>[%s] %s</td>", cfg.http.title, cli->user, p, ip2string(cli->ip) );
					else
						sprintf(wbuf, "<tr><td>%s</td><td>%s</td><td class=online>Connected</td><td>%s</td>", cfg.http.title, cli->user, ip2string(cli->ip) );
					//Last Used Share
					if ( cli->lastecm.caid ) {
						char tmp[512];
						if (cli->lastecm.status)
							sprintf( tmp,"<td class=success>channel %s (%dms) OK</td>", getchname(cli->lastecm.caid, cli->lastecm.prov, cli->lastecm.sid) , cli->lastecm.decodetime );
						else
							sprintf( tmp,"<td class=failed>channel %s (%dms) NOK</td>", getchname(cli->lastecm.caid, cli->lastecm.prov, cli->lastecm.sid) , cli->lastecm.decodetime );
						strcat( wbuf, tmp);
						// last time
						unsigned int d= (GetTickCount()-cli->ecm.recvtime)/1000;
						sprintf( tmp,"<td>%02dd %02d:%02d:%02d</td></tr>\r\n", d/(3600*24), (d/3600)%24, (d/60)%60, d%60);
						strcat( wbuf, tmp);
					} else strcat( wbuf, "<td> </td><td> </td></tr>\r\n");
				}
				else {
					if (cli->connection.lastseen) {
						uint32_t d = (GetTickCount()-cli->connection.lastseen)/1000;
						sprintf( wbuf,"<tr><td>%s</td><td>%s</td><td class=offline>Disconnected</td><td>Last Seen</td><td>%02dd %02d:%02d:%02d</td></tr>\r\n", cfg.http.title, cli->user, d/(3600*24),(d/3600)%24,(d/60)%60,d%60);
					}
					else sprintf(wbuf, "<tr><td>%s</td><td>%s</td><td class=offline>Disconnected</td><td> </td><td> </td></tr>\r\n", cfg.http.title, cli->user );
				}
				writes(fd, wbuf);
			}
		}
		else if (!strcmp(str,"MGCAMD")) {
			if (!parse_int(str)) {
				int index = 0;
				struct mgcamdserver_data *srv = cfg.mgcamd.server;
				while (srv) {
					index++;
					sprintf( wbuf, "id:%d  Port:%d", srv->id, srv->port);
					if (srv->handle>0) strcat( wbuf, " Status:ON "); else strcat( wbuf, " Status:OFF");
					int total, connected, active;
					mgcamd_clients( srv, &total, &connected, &active );
					sprintf( str, "Clients Total:%d, Connected:%d, Active:%d", total, connected, active );
					strcat( wbuf, str );
					strcat( wbuf, "\r\n");
					writes(fd, wbuf);
					srv = srv->next;
				}
				sprintf(wbuf, "Total Mgcamd servers = %d\r\n", index);
				writes(fd, wbuf);
			}
			else { // mgcamd <cientname>
				struct mgcamdserver_data *srv = getmgcamdserverbyid( atoi(str) );
				if (!srv) continue;
				if (!parse_name(str)) continue;
				struct mg_client_data *cli = getmgcamdclientbyname( srv, str );
				if (!cli) continue;
				if (cli->handle>0) {
					char *p = getcountrycodebyip(cli->ip);
					if (p) 
						sprintf(wbuf, "<tr><td>%s</td><td>%s</td><td class=online>Connected</td><td>[%s] %s</td>", cfg.http.title, cli->user, p, ip2string(cli->ip) );
					else
						sprintf(wbuf, "<tr><td>%s</td><td>%s</td><td class=online>Connected</td><td>%s</td>", cfg.http.title, cli->user, ip2string(cli->ip) );
					//Last Used Share
					if ( cli->lastecm.caid ) {
						char tmp[512];
						if (cli->lastecm.status)
							sprintf( tmp,"<td class=success>channel %s (%dms) OK</td>", getchname(cli->lastecm.caid, cli->lastecm.prov, cli->lastecm.sid) , cli->lastecm.decodetime );
						else
							sprintf( tmp,"<td class=failed>channel %s (%dms) NOK</td>", getchname(cli->lastecm.caid, cli->lastecm.prov, cli->lastecm.sid) , cli->lastecm.decodetime );
						strcat( wbuf, tmp);
						// last time
						unsigned int d= (GetTickCount()-cli->ecm.recvtime)/1000;
						sprintf( tmp,"<td>%02dd %02d:%02d:%02d</td></tr>\r\n", d/(3600*24), (d/3600)%24, (d/60)%60, d%60);
						strcat( wbuf, tmp);
					} else strcat( wbuf, "<td> </td><td> </td></tr>\r\n");
				}
				else sprintf(wbuf, "<tr><td>%s</td><td>%s</td><td class=offline>Disconnected</td><td> </td><td> </td></tr>\r\n", cfg.http.title, cli->user );
				writes(fd, wbuf);
			}
		}
		else if (!strcmp(str,"UPDATE")) {
			if (parse_name(str)) {
				uppercase(str);
				if ( !strcmp(str, "CCCAM") ) {
					if (parse_int(str)) {
						struct cccam_server_data *cccam = getcccamserverbyid( atoi(str) );
						if (!cccam) continue;
						// user
						if (!parse_str(str)) continue;
						struct cc_client_data *cli = getcccamclientbyname( cccam, str );
						if (!cli) continue;
						// pass
						if (!parse_str(str)) continue;
						if ( strcmp(str, "*") ) {
							if ( strcmp(cli->pass, str) ) {
								strcpy(cli->pass, str);
								// disconnect
								if (cli->connection.status>0) cc_disconnect_cli(cli);
							}
						}
						// date
						if (!parse_str(str)) continue;
						if ( strcmp(str, "*") ) {
							if ( (str[4]=='-')&&(str[7]=='-') ) strptime(  str, "%Y-%m-%d %H", &cli->enddate);
							else if ( (str[2]=='-')&&(str[5]=='-') ) strptime(  str, "%d-%m-%Y %H", &cli->enddate);
						}
						//
						if (parse_int(str)) {
							if (str[0]=='1') cli->flags &= ~FLAG_DISABLE; 
							else {
								cli->flags |= FLAG_DISABLE;
								if (cli->connection.status>0) cc_disconnect_cli(cli);
							}
						}
						sprintf(wbuf, "CCcam Client '%s' Updated\r\n", cli->user);
						writes(fd, wbuf);
					} else writes(fd, "update cccam <server id> <client name> <password> <expire-date> <1:enable/0:disable>\r\n");
				}
				else
				if ( !strcmp(str, "MGCAMD") ) {
					if (parse_int(str)) {
						struct mgcamdserver_data *mgcamd = getmgcamdserverbyid( atoi(str) );
						if (!mgcamd) continue;
						// user
						if (!parse_str(str)) continue;
						struct mg_client_data *cli = getmgcamdclientbyname( mgcamd, str );
						if (!cli) continue;
						// pass
						if (!parse_str(str)) continue;
						if ( strcmp(str, "*") ) {
							if ( strcmp(cli->pass, str) ) {
								strcpy(cli->pass, str);
								// disconnect
								if (cli->connection.status>0) mg_disconnect_cli(cli);
							}
						}
						// date
						if (!parse_name(str)) continue;
						if ( strcmp(str, "*") ) {
							if ( (str[4]=='-')&&(str[7]=='-') ) strptime(  str, "%Y-%m-%d %H", &cli->enddate);
							else if ( (str[2]=='-')&&(str[5]=='-') ) strptime(  str, "%d-%m-%Y %H", &cli->enddate);
						}
						//
						if (parse_int(str)) {
							if (str[0]=='1') cli->flags &= ~FLAG_DISABLE;
							else {
								cli->flags |= FLAG_DISABLE;
								if (cli->connection.status>0) mg_disconnect_cli(cli);
							}
						}
						sprintf(wbuf, "mgcamd Client '%s' Updated\r\n", cli->user);
						writes(fd, wbuf);
					} else writes(fd, "update mgcamd <server id> <client name> <password> <expire-date> <1:enable/0:disable>\r\n");
				}

			}
		}
		else {
			writes(fd, "unknown command.\r\n");
		}

	}

	close(fd);
	return NULL;
}




void *telnet_thread(void *param)
{
	int clientsock;
	struct sockaddr_in client_addr;
	socklen_t socklen = sizeof(client_addr);
	prctl(PR_SET_NAME,"Telnet",0,0,0);
	while(1) {
		if (cfg.telnet.handle>0) {
			struct pollfd pfd;
			pfd.fd = cfg.telnet.handle;
			pfd.events = POLLIN | POLLPRI;
			int retval = poll(&pfd, 1, 3002);
			if ( retval>0 ) {
				if ( pfd.revents & (POLLIN|POLLPRI) ) {
					clientsock = accept(cfg.telnet.handle, (struct sockaddr*)&client_addr, /*(socklen_t*)*/&socklen);
					if ( clientsock<0 ) {
						debugf(getdbgflag(DBG_HTTP,0,0)," telnet Server: Accept Error\n");
						break;
					}
					else {
						//SetSocketNoDelay(clientsock);
						pthread_t cli_tid;
						/* TASK R3 (D57): the door now knows who is knocking --
						 * the per-IP allowlist and the progressive delay need
						 * the source address. Same wire behavior when both
						 * are off. */
						struct telnet_conn_data *param = malloc( sizeof(struct telnet_conn_data) );
						param->fd = clientsock;
						param->ip = client_addr.sin_addr.s_addr;
						if ( !create_thread(&cli_tid, (threadfn)telnetprocess,param) ) {
							free( param );
							close( clientsock );
						}
					}
				}
			}
			else if (retval<0) {
				debugf(getdbgflag(DBG_HTTP,0,0)," THREAD telnet: poll error %d(errno=%d)\n", retval, errno);
				usleep(50000);
			}
		} else usleep(100000);
	}// While

	debugf(getdbgflag(DBG_HTTP,0,0),"Exiting telnet Thread\n");
	return NULL;
}

pthread_t telnet_tid;
int start_thread_telnet()
{
	create_thread(&telnet_tid, telnet_thread, NULL); // Priority
	return 0;
}


