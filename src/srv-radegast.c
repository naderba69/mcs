///////////////////////////////////////////////////////////////////////////////
// PROTO
///////////////////////////////////////////////////////////////////////////////

#include "tick_math.h"

void *rdgd_connect_cli_thread(void *param);
void *rdgd_getclimsg(void *param);
uint rdgd_check_sendcw();
void rdgd_disconnect_cli(struct cardserver_data *cs, struct rdgd_client_data *cli);


///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

static ECM_DATA *rdgd_get_ecm_request(struct rdgd_client_data *cli)
{
	ECM_DATA *ecm = NULL;
	memcpy(&ecm, cli->ecm.request_storage, sizeof(ecm));
	return ecm;
}

static void rdgd_set_ecm_request(struct rdgd_client_data *cli, ECM_DATA *ecm)
{
	memcpy(cli->ecm.request_storage, &ecm, sizeof(ecm));
}

static void rdgd_send_failure(struct rdgd_client_data *cli)
{
	unsigned char failure[4] = { 0x02, 0x02, 0x04, 0x00 };
	if (cli && cli->handle > 0) rdgd_message_send(cli->handle, failure, sizeof(failure));
}

void rdgd_senddcw_cli(struct cardserver_data *cs, struct rdgd_client_data *cli)
{
	unsigned char buf[CWS_NETMSGSIZE];

	if (cli->ecm.status==STAT_DCW_SENT) {
		debugf(0, " +> cw send failed to radegast client (%s), cw already sent\n", ip2string(cli->ip));
		return;
	}
	if (cli->handle==INVALID_SOCKET) {
		debugf(0, " +> cw send failed to radegast client (%s), client disconnected\n", ip2string(cli->ip));
		return;
	}
	if (!cli->ecm.busy) {
		debugf(0, " +> cw send failed to radegast client (%s), no ecm request\n", ip2string(cli->ip));
		return;
	}

	ECM_DATA *ecm = rdgd_get_ecm_request(cli);
#ifdef CACHEEX
	struct ledger_delivery_snapshot delivered;
	int delivered_valid = ecm &&
		ledger_delivery_snapshot_capture_locked(&delivered, ecm) &&
		delivered.request == ecm && delivered.lifetime_id == __atomic_load_n(&cli->ecm.lifetime_id, __ATOMIC_ACQUIRE);
	if (ecm && !delivered_valid) {
		/* The stored pointer no longer names this client's exact ECM lifetime. */
		rdgd_send_failure(cli);
		cli->ecm.busy = 0;
		cli->ecm.status = STAT_DCW_SENT;
		return;
	}
	uint32_t response_recvtime = delivered_valid ? delivered.recvtime : cli->ecm.recvtime;
	if (delivered_valid) {
		cli->ecm.lastcaid = delivered.caid;
		cli->ecm.lastprov = delivered.provid;
		cli->ecm.lastprovider_known = delivered.provider_known;
		cli->ecm.lastsid = delivered.sid;
		cli->ecm.lasthash = delivered.hash;
		cli->ecm.lasttag = delivered.tag;
		cli->ecm.lastdecodetime = GetTickCount()-response_recvtime;
	}
#else
	uint32_t response_recvtime = cli->ecm.recvtime;
	if (ecm) {
		cli->ecm.lastcaid = ecm->caid;
		cli->ecm.lastprov = ecm->provid;
		cli->ecm.lastprovider_known = ecm->provider_known;
		cli->ecm.lastsid = ecm->sid;
		cli->ecm.lasthash = ecm->hash;
		cli->ecm.lasttag = ecm->ecm[0];
		cli->ecm.lastdecodetime = GetTickCount()-response_recvtime;
	}
#endif

#ifdef CACHEEX
	if (delivered_valid && delivered.status==STAT_DCW_SUCCESS) {
#else
	if ( ecm && (ecm->dcwstatus==STAT_DCW_SUCCESS) ) {
#endif
		uint32_t sent_at;
		cli->ecm.laststatus=0;
		cli->ecm.lastdcwsrctype = DCW_SOURCE_NONE;
		cli->ecm.lastdcwsrcid = 0;
		memset(cli->ecm.lastcw, 0, sizeof(cli->ecm.lastcw));
#ifdef CACHEEX
		memset(cli->ecm.lastecmd5, 0, sizeof(cli->ecm.lastecmd5));
		cli->ecm.lastecm_len = 0;
#endif
		// Send DCW
		buf[0] = 0x02;
		buf[1] = 0x12;
		buf[2] = 0x05;
		buf[3] = 0x10;
#ifdef CACHEEX
		memcpy(&buf[4], delivered.cw, 16);
#else
		memcpy(&buf[4], ecm->cw, 16);
#endif
		if (rdgd_message_send(cli->handle, buf, 0x14) <= 0) {
			rdgd_disconnect_cli(cs, cli);
			return;
		}
		sent_at = GetTickCount();
#ifdef CACHEEX
		cli->ecm.lastcaid = delivered.caid;
		cli->ecm.lastprov = delivered.provid;
		cli->ecm.lastprovider_known = delivered.provider_known;
		cli->ecm.lastsid = delivered.sid;
		cli->ecm.lasthash = delivered.hash;
		cli->ecm.lasttag = delivered.tag;
		cli->ecm.lastdcwsrctype = delivered.srctype;
		cli->ecm.lastdcwsrcid = delivered.srcid;
		memcpy(cli->ecm.lastcw, delivered.cw, sizeof(cli->ecm.lastcw));
		memcpy(cli->ecm.lastecmd5, delivered.ecmd5,
		       sizeof(cli->ecm.lastecmd5));
		cli->ecm.lastecm_len = delivered.ecmlen;
		ledger_note_delivered(&delivered, sent_at);
#else
		cli->ecm.lastdcwsrctype = ecm->dcwsrctype;
		cli->ecm.lastdcwsrcid = ecm->dcwsrcid;
		memcpy(cli->ecm.lastcw, ecm->cw, sizeof(cli->ecm.lastcw));
#endif
		cli->ecm.laststatus=1;
		cli->ecmok++;
		cli->ecmoktime += sent_at-response_recvtime;
		cli->lastdcwtime = sent_at;
#ifdef CACHEEX
		debugf(0, " => cw to client (%s) ch %04x:%06x:%04x (%dms)\n", ip2string(cli->ip), delivered.caid,delivered.provid,delivered.sid, sent_at-response_recvtime);
#else
		debugf(0, " => cw to client (%s) ch %04x:%06x:%04x (%dms)\n", ip2string(cli->ip), ecm->caid,ecm->provid,ecm->sid, sent_at-response_recvtime);
#endif
	}
	else { //if (ecm->data->dcwstatus==STAT_DCW_FAILED)
		cli->ecm.laststatus=0;
		cli->ecm.lastdcwsrctype = DCW_SOURCE_NONE;
		cli->ecm.lastdcwsrcid = 0;
		memset(cli->ecm.lastcw, 0, sizeof(cli->ecm.lastcw));
#ifdef CACHEEX
		memset(cli->ecm.lastecmd5, 0, sizeof(cli->ecm.lastecmd5));
		cli->ecm.lastecm_len = 0;
#endif
		rdgd_send_failure(cli);
#ifdef CACHEEX
		if (delivered_valid) debugf(0, " |> decode failed to client (%s) ch %04x:%06x:%04x (%dms)\n", ip2string(cli->ip), delivered.caid,delivered.provid,delivered.sid, GetTickCount()-response_recvtime);
		else debugf(0, " |> decode failed to radegast client (%s), ECM record unavailable\n", ip2string(cli->ip));
#else
		if (ecm) debugf(0, " |> decode failed to client (%s) ch %04x:%06x:%04x (%dms)\n", ip2string(cli->ip), ecm->caid,ecm->provid,ecm->sid, GetTickCount()-response_recvtime);
		else debugf(0, " |> decode failed to radegast client (%s), ECM record unavailable\n", ip2string(cli->ip));
#endif
	}
	cli->ecm.busy=0;
	cli->ecm.status = STAT_DCW_SENT;
}



///////////////////////////////////////////////////////////////////////////////

void rdgd_disconnect_cli(struct cardserver_data *cs,struct rdgd_client_data *cli)
{
	if (cli)
	if (cli->handle>0) {
		//pthread_mutex_lock(&prg.lockrdgdcli);
		debugf(0, " radegast: client (%s) disconnected\n", ip2string(cli->ip));
		close(cli->handle);
		// Remove
		struct rdgd_client_data *n = cs->radegast.client;
		if (n) {
			if (n==cli) {
				cs->radegast.client = n->next;
				free(cli);
			}
			else {
				while (n->next) {
					if (cli==n->next) {
						n->next = n->next->next;
						free(cli);
						break;
					}
					n = n->next;
				}
			}
		}
		//pthread_mutex_unlock(&prg.lockrdgdcli);
	}
}


void *rdgd_connect_cli_thread(void *param)
{
	int clientsock;
	struct sockaddr_in clientaddr;
	(void)param;

	while (1) {
		struct pollfd pfd[MAX_CSPORTS];
		int pfdcount = 0;
		struct cardserver_data *cs;
		int i;

		pthread_mutex_lock(&prg.lockrdgdsrv);
		cs = cfg.cardserver;
		while (cs) {
			cs->radegast.ipoll = -1;
			if (cs->radegast.handle > 0 && pfdcount < MAX_CSPORTS) {
				pfd[pfdcount].fd = cs->radegast.handle;
				pfd[pfdcount].events = POLLIN | POLLPRI;
				pfd[pfdcount].revents = 0;
				cs->radegast.ipoll = pfdcount++;
			}
			cs = cs->next;
		}
		pthread_mutex_unlock(&prg.lockrdgdsrv);

		/* Never hold the server-list lock across a blocking poll. */
		int retval = poll(pfd, pfdcount, 100);
		if (retval > 0) {
			pthread_mutex_lock(&prg.lockrdgdsrv);
			for (i = 0; i < pfdcount; i++) {
				if (!(pfd[i].revents & (POLLIN | POLLPRI))) continue;
				cs = cfg.cardserver;
				while (cs && cs->radegast.handle != pfd[i].fd) cs = cs->next;
				if (!cs) continue; /* listener changed during poll */

				socklen_t socklen = sizeof(clientaddr);
				clientsock = accept(cs->radegast.handle, (struct sockaddr *)&clientaddr, &socklen);
				if (clientsock < 0) {
					if (errno != EAGAIN && errno != EINTR)
						debugf(0, " Radegast: Accept failed (errno=%d)\n", errno);
					continue;
				}
				SetSocketKeepalive(clientsock);
				SetSocketNoDelay(clientsock);
				if (SetSoketNonBlocking(clientsock) < 0) {
					debugf(DBG_ERROR, " Radegast: unable to set accepted socket nonblocking (errno=%d)\n", errno);
					close(clientsock);
					continue;
				}
				struct rdgd_client_data *cli = malloc(sizeof(struct rdgd_client_data));
				if (!cli) {
					close(clientsock);
					debugf(DBG_ERROR, " Radegast: unable to allocate client record\n");
					continue;
				}
				memset(cli, 0, sizeof(struct rdgd_client_data));
				cli->handle = clientsock;
				cli->ip = clientaddr.sin_addr.s_addr;
				pthread_mutex_lock(&prg.lockrdgdcli);
				cli->next = cs->radegast.client;
				cs->radegast.client = cli;
				pthread_mutex_unlock(&prg.lockrdgdcli);
				debugf(0, " radegast: client (%s) connected\n", ip2string(cli->ip));
				pipe_wakeup(prg.pipe.ecm[1]);
			}
			pthread_mutex_unlock(&prg.lockrdgdsrv);
		} else if (retval < 0 && errno != EINTR) {
			usleep(3000);
		}
	}
	return NULL;
}

void rdgd_store_ecmclient(struct cardserver_data *cs, ECM_DATA *ecm, struct rdgd_client_data *cli)
{
	cli->ecm.recvtime = GetTickCount();
	rdgd_set_ecm_request(cli, ecm);
#ifdef CACHEEX
	__atomic_store_n(&cli->ecm.lifetime_id, ecm_request_lifetime_id_load(ecm), __ATOMIC_RELEASE);
#endif
	cli->ecm.status = STAT_ECM_SENT;
	ecm_addip(ecm, cli->ip);
}

/* Radegast carries a stream-framed packet containing tag/length/value TLVs.
 * Validate the remaining bytes before reading the length byte or value. */
static int rdgd_tlv_is_complete(const uint8_t *buf, int received, int index)
{
	if (!buf || received < 0 || index < 0 || index > received) return 0;
	if (received - index < 2) return 0;
	return (int)buf[index + 1] <= received - index - 2;
}

static int rdgd_cache_has_peers(void)
{
	struct cacheserver_data *cache;
	int found = 0;
	pthread_mutex_lock(&prg.lockcache);
	for (cache = cfg.cache.server; cache; cache = cache->next) {
		if (cache->peer) { found = 1; break; }
	}
	pthread_mutex_unlock(&prg.lockcache);
	return found;
}

int accept_ecm(struct cardserver_data *cs, uint16_t caid, uint32_t provid,
               uint16_t sid, uint16_t chid, uint16_t ecmlen, uint8_t *cw1cycle)
{
	if (!cw1cycle) return 0;
	*cw1cycle = 0;
	// Check for caid, accept caid=0
	if (!accept_caid(cs, caid)) return 0;

	// Check for provid, accept provid==0
	if (!accept_prov(cs, provid)) return 0;

	// Check for Accepted SIDs, CHID and ECM length
	if (!accept_sid(cs, provid, sid, chid, ecmlen, cw1cycle)) return 0;

	return 1;
}


void rdgd_cli_recvmsg(struct rdgd_client_data *cli, struct cardserver_data *cs)
{
	int len;
	unsigned char buf[300];
	int i;

	if (cli->handle>0) {
		len = rdgd_check_message(cli->handle);
		if (len==0) {
			debugf(0, " radegast: client (%s) read failed %d\n", ip2string(cli->ip),len);
			rdgd_disconnect_cli(cs,cli);
		}
		else if (len==-1) {
			if (!cli->chkrecvtime) cli->chkrecvtime = GetTickCount();
			else if (tick32_elapsed(GetTickCount(), cli->chkrecvtime) > 300u) {
				debugf(0, " radegast: client (%s) read failed %d\n", ip2string(cli->ip),len);
				rdgd_disconnect_cli(cs,cli);
			}
		}
		else if (len>0) {
			cli->chkrecvtime = 0;
			len = rdgd_message_receive(cli->handle, buf, 0);
			if (len==0) {
				debugf(0, " radegast: client (%s) read failed %d\n", ip2string(cli->ip),len);
				rdgd_disconnect_cli(cs,cli);
			}
			else if (len<0) {
				debugf(0, " radegast: client '%s' read failed %d(%d)\n", ip2string(cli->ip),len,errno);
				rdgd_disconnect_cli(cs,cli);
			}
			else if (len>0) switch ( buf[0] ) {
				case 0x01: // ECM
					cli->lastecmtime = GetTickCount();
					cli->ecmnb++;
					if (cli->ecm.busy) {
						cs->ecmdenied++;
						cli->ecmdenied++;
						rdgd_send_failure(cli); // reject this new ECM; preserve the active request
						break;
					}
					uint16_t caid = 0;
					uint32_t provid = 0;
					int provider_known = 0;
					unsigned char ecmdata[300];
					memset(ecmdata, 0, sizeof(ecmdata));
					int ecmlen = 0;
					int index = 2, malformed = 0;
					while (index < len) {
						if (!rdgd_tlv_is_complete(buf, len, index)) { malformed = 1; break; }
						uint8_t tlv_type = buf[index];
						uint8_t tlv_len = buf[index + 1];
						switch (tlv_type) {
						case 2: // CAID (upper byte only, oldstyle)
							if (tlv_len < 1) malformed = 1;
							else caid = (uint16_t)buf[index + 2] << 8;
							break;
						case 10: // CAID
							if (tlv_len < 2) malformed = 1;
							else caid = ((uint16_t)buf[index + 2] << 8) | buf[index + 3];
							break;
						case 3: // ECM DATA
							if (!tlv_len || tlv_len > sizeof(ecmdata)) malformed = 1;
							else {
								ecmlen = tlv_len;
								memcpy(ecmdata, buf + index + 2, ecmlen);
							}
							break;
						case 6: // PROVID (ASCII hex)
							if (tlv_len > 8) { malformed = 1; break; }
							for (i = 0; i < tlv_len; i++) {
								int digit = hexvalue(buf[index + 2 + i]);
								if (digit < 0) { malformed = 1; break; }
								provid = (provid << 4) | (uint32_t)digit;
							}
							if (tlv_len > 0) provider_known = 1;
							break;
						case 7: // KEYNR (ASCII), not needed
						case 8: // ECM PROCESS PID, not needed
						default:
							break;
						}
						if (malformed) break;
						index += 2 + tlv_len;
					}
					if (!ecmlen) malformed = 1;
					if (malformed) {
						cs->ecmdenied++;
						cli->ecmdenied++;
						rdgd_send_failure(cli);
						break;
					}
					if (!provider_known) {
						uint32_t extracted_provider = 0;
						if (ecm_getprovid_known(ecmdata, ecmlen, caid, &extracted_provider)) {
							provid = extracted_provider;
							provider_known = 1;
						}
					}

					uint8_t cw1cycle = 0;
					if (!accept_ecm(cs, caid, provid, 0, ecm_getchid(ecmdata, ecmlen, caid), ecmlen, &cw1cycle)) {
						cs->ecmdenied++;
						cli->ecmdenied++;
						rdgd_send_failure(cli);
						break;
					}

					// ACCEPTED
					ecm_lock(); //###

					// Search for ECM
					ECM_DATA *ecm = search_ecmdata_dcw(ecmdata, ecmlen, 0, caid, provid, provider_known); // key includes parsed provider presence
					if (ecm) {
						ecm->lastrecvtime = GetTickCount();
						//TODO: Add another card for sending ecm
						rdgd_store_ecmclient(cs, ecm, cli);
						debugf(0, " <- ecm from client (%s) ch %04x:%06x:%04x*\n", ip2string(cli->ip), caid, provid, 0);
						cli->ecm.busy=1;
					}
					else {
						// Setup ECM Request for Server(s)
						ecm = store_ecmdata_known(cs, ecmdata, ecmlen, 0, caid, provid, provider_known);
						if (!ecm) {
							cs->ecmdenied++;
							cli->ecmdenied++;
							rdgd_send_failure(cli);
							ecm_unlock();
							break;
						}
						cs->ecmaccepted++;
						rdgd_store_ecmclient(cs, ecm, cli);
						debugf(0, " <- ecm from client (%s) ch %04x:%06x:%04x\n", ip2string(cli->ip), caid, provid, 0);
						cli->ecm.busy=1;
						ecm->cw1cycle = cw1cycle;
						if (cs->option.fallowcache && rdgd_cache_has_peers()) {
							ecm->waitcache = 1;
							ecm_request_set_dcwstatus(ecm, STAT_DCW_WAITCACHE);
							ecm->checktime = ecm->recvtime +
							    ecm_get_cache_initial_delay_ms(ecm, cs, GetTickCount());
							pipe_cache_find(ecm, cs);
						} else ecm_request_set_dcwstatus(ecm, STAT_DCW_WAIT);
					}
					ecm_unlock(); //###
					wakeup_sendecm();
					break;

				default:
					buf[0] = 0x81;
					buf[1] = 0;
					rdgd_message_send(cli->handle,buf,2);
					//radegast_send(answer);
					//cs_log("unknown request %02X, len=%d", buf[0], buf[1]);
			}
		}
	}
}


// Check sending cw to clients
struct rdgd_poll_target {
	struct cardserver_data *cs;
	struct rdgd_client_data *cli;
};

void *rdgd_getclimsg(void *param)
{
	(void)param;
	while (1) {
		size_t count = 0, index = 0;
		struct pollfd *pfds = NULL;
		struct rdgd_poll_target *targets = NULL;
		struct cardserver_data *cs;
		struct rdgd_client_data *cli;
		int ready, i;

		/* Match the acceptor's lock order; keep the Radegast client list
		 * stable while poll returns and a readable client can be removed. */
		pthread_mutex_lock(&prg.lockrdgdsrv);
		pthread_mutex_lock(&prg.lockrdgdcli);
		for (cs = cfg.cardserver; cs; cs = cs->next)
			for (cli = cs->radegast.client; cli; cli = cli->next)
				if (cli->handle > 0) count++;

		if (count) {
			pfds = calloc(count, sizeof(*pfds));
			targets = malloc(count * sizeof(*targets));
			if (!pfds || !targets) {
				free(pfds);
				free(targets);
				pthread_mutex_unlock(&prg.lockrdgdcli);
				pthread_mutex_unlock(&prg.lockrdgdsrv);
				usleep(50000);
				continue;
			}
			for (cs = cfg.cardserver; cs; cs = cs->next) {
				for (cli = cs->radegast.client; cli; cli = cli->next) {
					if (cli->handle <= 0) continue;
					pfds[index].fd = cli->handle;
					pfds[index].events = POLLIN | POLLPRI;
					pfds[index].revents = 0;
					targets[index].cs = cs;
					targets[index].cli = cli;
					index++;
				}
			}
		}

		ready = poll(pfds, (nfds_t)count, 50);
		if (ready > 0) {
			for (i = 0; i < (int)count; i++) {
				if (pfds[i].revents & (POLLIN | POLLPRI | POLLHUP | POLLERR | POLLNVAL))
					rdgd_cli_recvmsg(targets[i].cli, targets[i].cs);
			}
		}
		ecm_lock();
		rdgd_check_sendcw();
		ecm_unlock();
		free(pfds);
		free(targets);
		pthread_mutex_unlock(&prg.lockrdgdcli);
		pthread_mutex_unlock(&prg.lockrdgdsrv);
		if (ready < 0 && errno != EINTR) usleep(3000);
	}
	return NULL;
}

uint rdgd_check_sendcw()
{
	struct cardserver_data *cs = cfg.cardserver;
	uint restime = (uint32_t)GetTickCount() + 10000u;
	uint clitime = 0;

	while (cs) {
		if (cs->radegast.handle>0) {
			struct rdgd_client_data *cli = cs->radegast.client;
			uint32_t ticks = GetTickCount();
			while (cli) {
				struct rdgd_client_data *next = cli->next;
				if ( (cli->handle>0)&&(cli->ecm.busy) ) {
					clitime = ticks+11000;
					// Check for DCW ANSWER
					ECM_DATA *ecm = rdgd_get_ecm_request(cli);
					if (ecm) {
						// Check for FAILED
						if (ecm->dcwstatus==STAT_DCW_FAILED) rdgd_senddcw_cli(cs, cli);
						// Check for SUCCESS
						else if (ecm->dcwstatus==STAT_DCW_SUCCESS) rdgd_senddcw_cli(cs, cli);
						// check for timeout
						else if (tick32_elapsed(ticks, cli->ecm.recvtime) > (uint32_t)cs->option.dcw.timeout) rdgd_senddcw_cli(cs, cli); else clitime = cli->ecm.recvtime+cs->option.dcw.timeout;
					}
					else rdgd_senddcw_cli(cs, cli); // failed
					if (tick32_deadline_before(clitime, restime)) restime = clitime;
				}
				cli = next;
			}
		}
		cs = cs->next;
	}
	return (restime+1);
}


