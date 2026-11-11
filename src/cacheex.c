#include "cache-pipe-codec.h"


///////////////////////////////////////////////////////////////////////////////
// TOOL
///////////////////////////////////////////////////////////////////////////////


int acceptshare( struct sharelimit_data sharelimits[100], uint16_t caid, uint32_t provid)
{
	int i;
	if (sharelimits[0].caid==0xffff) return 1;
	for (i=0; i<100; i++) {
		if (sharelimits[i].caid==0xffff) break;
		if (sharelimits[i].caid==caid) {
			if (sharelimits[i].provid==provid) return 1;
			else if (sharelimits[i].provid==0xFFFFFF) return 1;
		}
	}
	return 0;
}


///////////////////////////////////////////////////////////////////////////////
// TOOL
///////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
// pipe --> cacheex
///////////////////////////////////////////////////////////////////////////////

int pipe_send_cacheex_push_out(ECM_DATA *ecm)
{
	uint8_t buf[128];
	if (!ecm) return 0;
	buf[0] = PIPE_CACHEEX_PUSH_LOCAL;
	buf[1] = (ecm->caid)>>8; buf[2] = (ecm->caid)&0xff;
	buf[3] = ecm->ecm[0];
	buf[4] = ecm->provid>>16; buf[5] = ecm->provid>>8; buf[6] = ecm->provid & 0xff;
	buf[7] = (ecm->sid)>>8; buf[8] = (ecm->sid)&0xff;
	buf[9] = ecm->hash>>24; buf[10] = ecm->hash>>16; buf[11] = ecm->hash>>8; buf[12] = ecm->hash & 0xff;
	memcpy(buf+13, ecm->ecmd5, 16);
	memcpy(buf+29, ecm->cw, 16);
	pipe_send( prg.pipe.cacheex[1], buf, 45);
	return 1;
}

/* Send the identical internal pipe record from the frozen successful
 * delivery. The ECM pool slot may already have been reused after setdcw
 * releases lockecm; the private pipe format is unchanged. */
int pipe_send_cacheex_push_out_snapshot(const struct ledger_delivery_snapshot *delivery)
{
	uint8_t buf[128];
	if (!delivery || !delivery->lifetime_id) return 0;
	buf[0] = PIPE_CACHEEX_PUSH_LOCAL;
	buf[1] = (uint8_t)(delivery->caid >> 8); buf[2] = (uint8_t)delivery->caid;
	buf[3] = delivery->tag;
	buf[4] = (uint8_t)(delivery->provid >> 16);
	buf[5] = (uint8_t)(delivery->provid >> 8);
	buf[6] = (uint8_t)delivery->provid;
	buf[7] = (uint8_t)(delivery->sid >> 8); buf[8] = (uint8_t)delivery->sid;
	buf[9] = (uint8_t)(delivery->hash >> 24);
	buf[10] = (uint8_t)(delivery->hash >> 16);
	buf[11] = (uint8_t)(delivery->hash >> 8); buf[12] = (uint8_t)delivery->hash;
	memcpy(buf+13, delivery->ecmd5, sizeof(delivery->ecmd5));
	memcpy(buf+29, delivery->cw, sizeof(delivery->cw));
	pipe_send(prg.pipe.cacheex[1], buf, 45);
	return 1;
}

int pipe_send_cacheex_push_cache(struct cache_data *pcache, uint8_t *cw, uint8_t *nodeid)
{
	uint8_t buf[128];
	buf[0] = PIPE_CACHEEX_PUSH_REMOTE;
	buf[1] = (pcache->caid)>>8; buf[2] = (pcache->caid)&0xff;
	buf[3] = pcache->tag;
	buf[4] = pcache->provid>>16; buf[5] = pcache->provid>>8; buf[6] = pcache->provid & 0xff;
	buf[7] = (pcache->sid)>>8; buf[8] = (pcache->sid)&0xff;
	buf[9] = pcache->hash>>24; buf[10] = pcache->hash>>16; buf[11] = pcache->hash>>8; buf[12] = pcache->hash & 0xff;
	memcpy(buf+13, pcache->ecmd5, 16);
	memcpy(buf+29, cw, 16);
	memcpy( buf+29+16, nodeid, 8);
	pipe_send( prg.pipe.cacheex[1], buf, 29+16+8);
	return 1;
}

int pipe_send_cacheex_push_in(struct cache_data *pcache, uint8_t cw[16], int clid, int hop)
{
	uint8_t buf[128];
	buf[0] = PIPE_CACHEEX_PUSH_IN;
	buf[1] = (pcache->caid)>>8; buf[2] = (pcache->caid)&0xff;
	buf[3] = pcache->tag;
	buf[4] = pcache->provid>>16; buf[5] = pcache->provid>>8; buf[6] = pcache->provid & 0xff;
	buf[7] = (pcache->sid)>>8; buf[8] = (pcache->sid)&0xff;
	buf[9] = pcache->hash>>24; buf[10] = pcache->hash>>16; buf[11] = pcache->hash>>8; buf[12] = pcache->hash & 0xff;
	memcpy(buf+13, pcache->ecmd5, 16);
	memcpy(buf+29, cw, 16);
	buf[45] = clid>>8;
	buf[46] = clid;
	buf[47] = hop;
	pipe_send( prg.pipe.cacheex[1], buf, 48);
	return 1;
}

///////////////////////////////////////////////////////////////////////////////
// PUSH OUT 
///////////////////////////////////////////////////////////////////////////////

int get_cccam_cacheex_push(struct cache_data *pcache, uint8_t cw[16], uint8_t *buf, uint8_t *nodeid )
{
	memset(buf, 0, 57+8 );

	buf[0] = pcache->caid>>8;
	buf[1] = pcache->caid;

	buf[2] = pcache->provid>>24;
	buf[3] = pcache->provid>>16;
	buf[4] = pcache->provid>>8;
	buf[5] = pcache->provid;

	buf[10] = pcache->sid>>8;
	buf[11] = pcache->sid;

	buf[12] = 0x24; // ( 16 + 4 + 16 )
	buf[13] = 0;

	buf[14] = 0;

	buf[19] = pcache->tag;

	memcpy(buf+20, pcache->ecmd5, 16);
	buf[36] = pcache->hash;
	buf[37] = pcache->hash >> 8;
	buf[38] = pcache->hash >> 16;
	buf[39] = pcache->hash >> 24; 
	memcpy(buf+40, cw, 16);
	if (nodeid) {
		buf[56] = 2;
		memcpy(buf+57, cccam_nodeid, 8 );
		memcpy(buf+57+8, nodeid, 8 );
		return 57+8+8;
	}
	buf[56] = 1; // Uphops = 1 (local)
	memcpy(buf+57, cccam_nodeid, 8 );
	return 57+8;
}

int get_camd35_cacheex_push(struct cache_data *pcache, uint8_t cw[16], uint8_t *buf, uint8_t *nodeid )
{
	memset(buf, 0, 57+8 );
	buf[0] = 0x3F;
	buf[1] = 57+8-20;

	buf[8] = pcache->sid>>8;
	buf[9] = pcache->sid;

	buf[10] = pcache->caid>>8;
	buf[11] = pcache->caid;

	buf[12] = pcache->provid>>24;
	buf[13] = pcache->provid>>16;
	buf[14] = pcache->provid>>8;
	buf[15] = pcache->provid;

	buf[19] = pcache->tag;

	memcpy(buf+20, pcache->ecmd5, 16);
	buf[36] = pcache->hash;
	buf[37] = pcache->hash >> 8;
	buf[38] = pcache->hash >> 16;
	buf[39] = pcache->hash >> 24; 
	memcpy(buf+40, cw, 16);
	if (nodeid) {
		buf[1] += 8;
		buf[56] = 2;
		memcpy(buf+57, cccam_nodeid, 8 );
		memcpy(buf+57+8, nodeid, 8 );
		return 57+8+8;
	}
	buf[56] = 1; // Uphops = 1 (local)
	memcpy(buf+57, cccam_nodeid, 8 );
	return 57+8;
}

void cacheex_push(struct cache_data *pcache, uint8_t cw[16], uint8_t *nodeid )
{
	uint8_t camd35buf[128];
	int camd35len = get_camd35_cacheex_push( pcache, cw, camd35buf, nodeid );
	uint8_t cccambuf[128];
	int cccamlen = get_cccam_cacheex_push( pcache, cw, cccambuf, nodeid );

	// PUSH TO SERVERS cacheex=3
	struct server_data *srv = cfg.cacheexserver;
	while (srv) {
		//debugf( getdbgflag(DBG_CACHEEX, 0, 0)," CACHEEX PUSH to server (%s:%d) %04x:%06x:%04x:%08x\n",srv->host->name, srv->port,pcache->caid,pcache->provid,pcache->sid,pcache->hash); debughex(cw,16);
		if ( (srv->cacheex_mode==3) && (srv->connection.status>0) )
		if ( acceptshare(srv->sharelimits, pcache->caid, pcache->provid) ) {
			if (srv->type==TYPE_CCCAM) {
				if ( !cc_msg_send( srv->handle, &srv->sendblock, CC_MSG_CACHE_PUSH, cccamlen, cccambuf) ) disconnect_srv(srv);
			}
#ifdef CAMD35_CLI
			else if (srv->type==TYPE_CAMD35) camd35_sendto( srv->handle, host_ip_snapshot(srv->host), srv->port, &srv->encryptkey, srv->ucrc, camd35buf, camd35len);
#endif
#ifdef CS378X_CLI
			else if (srv->type==TYPE_CS378X) {
				if ( !cs378x_send( srv->handle, &srv->encryptkey, srv->ucrc, camd35buf, camd35len) )  disconnect_srv(srv);
			}
#endif
			srv->cacheex.push[0]++;
			if (nodeid) srv->cacheex.push[2]++; else srv->cacheex.push[1]++;
		}
		srv = srv->next;
	}

#ifdef CCCAM_SRV
	// PUSH TO CCCAM CLIENTS cacheex=2
	struct cccam_server_data *cccam = cfg.cccam.server;
	while (cccam) {
		struct cc_client_data *cli = cccam->cacheexclient;
		while (cli) {
			if ( (cli->cacheex_mode==2) && (cli->connection.status>0) )
			if ( acceptshare(cli->sharelimits, pcache->caid, pcache->provid) ) {
				if (cc_send_cacheex_push_cli(cli, cccambuf, cccamlen)) {
					pthread_mutex_lock(&prg.lockcacheex);
					cli->cacheex.push[0]++;
					if (nodeid) cli->cacheex.push[2]++; else cli->cacheex.push[1]++;
					pthread_mutex_unlock(&prg.lockcacheex);
				}
				//debugf( getdbgflag(DBG_CACHEEX, 0, 0)," CACHEEX PUSH to client %04x:%06x:%04x:%08x\n",pcache->caid,pcache->provid,pcache->sid,pcache->hash);// debughex(req.cw,16);
			}
			cli = cli->next;
		}
		cccam = cccam->next;
	}
#endif

#ifdef CAMD35_SRV
	// PUSH TO CAMD35 CLIENTS cacheex=2
	struct camd35_server_data *camd35 = cfg.camd35.server;
	while (camd35) {
		struct camd35_client_data *cli = camd35->cacheexclient;
		while (cli) {
			if ( (cli->cacheex_mode==2) && (cli->connection.status>0) )
			if ( !nodeid || memcmp(nodeid, cli->nodeid, 8) )
			if ( acceptshare(cli->sharelimits, pcache->caid, pcache->provid) ) {
				camd35_sendto( camd35->handle, cli->ip, cli->port, &cli->encryptkey, cli->ucrc, camd35buf, camd35len);
				pthread_mutex_lock(&prg.lockcacheex);
				cli->cacheex.push[0]++;
				if (nodeid) cli->cacheex.push[2]++; else cli->cacheex.push[1]++;
				pthread_mutex_unlock(&prg.lockcacheex);
				//debugf( getdbgflag(DBG_CACHEEX, 0, 0)," CACHEEX PUSH to client %04x:%06x:%04x:%08x\n",pcache->caid,pcache->provid,pcache->sid,pcache->hash);// debughex(req.cw,16);
			}
			cli = cli->next;
		}
		camd35 = camd35->next;
	}
#endif

#ifdef CS378X_SRV
	// PUSH TO CS378X CLIENTS cacheex=2
	struct camd35_server_data *cs378x = cfg.cs378x.server;
	while (cs378x) {
		struct camd35_client_data *cli = cs378x->cacheexclient;
		while (cli) {
			if ( (cli->cacheex_mode==2) && (cli->connection.status>0) )
			if ( !nodeid || memcmp(nodeid, cli->nodeid, 8) )
			if ( acceptshare(cli->sharelimits, pcache->caid, pcache->provid) ) {
				if ( !cs378x_send( cli->handle, &cli->encryptkey, cli->ucrc, camd35buf, camd35len) ) cs378x_disconnect_cli(cli);
				pthread_mutex_lock(&prg.lockcacheex);
				cli->cacheex.push[0]++;
				if (nodeid) cli->cacheex.push[2]++; else cli->cacheex.push[1]++;
				pthread_mutex_unlock(&prg.lockcacheex);
				//debugf( getdbgflag(DBG_CACHEEX, 0, 0)," CACHEEX PUSH to client %04x:%06x:%04x:%08x\n",pcache->caid,pcache->provid,pcache->sid,pcache->hash);// debughex(req.cw,16);
			}
			cli = cli->next;
		}
		cs378x = cs378x->next;
	}
#endif

}



void cacheex_pipe_recvmsg()
{
	uint8_t buf[512];
	uint8_t cw[16];
	struct cache_data req;
	struct pollfd pfd;

	do {
		int len = pipe_recv(prg.pipe.cacheex[0], buf, sizeof(buf));
		if (len > 0) {
			if (!cacheex_pipe_frame_length_valid(buf[0], (size_t)len)) {
				debugf(getdbgflag(DBG_ERROR,0,0),
				       " cacheex pipe: invalid command/length (%u/%d)\n", buf[0], len);
			} else {
				memset(&req, 0, sizeof(req));
				//debugf(getdbgflag(DBG_CACHEEX, 0, 0), " Recv from CacheEX Pipe\n"); debughex(buf, len);
				switch (buf[0]) {
				case PIPE_WAKEUP:  // ADD NEW CLIENT
					break;

				case PIPE_CACHEEX_PUSH_LOCAL: // from local
					// Setup Cache Request
					req.caid = (buf[1]<<8) | buf[2];
					req.tag = buf[3];
					req.provid = (buf[4]<<16) | (buf[5]<<8) | buf[6];
					req.sid = (buf[7]<<8) | buf[8];
					req.hash = (buf[9]<<24) | (buf[10]<<16) | (buf[11]<<8) | buf[12];
					memcpy(req.ecmd5, buf+13, 16);
					memcpy(cw, buf+29, 16);
					// Send Reply
					pthread_mutex_lock(&prg.lockcacheex);
					cfg.cacheex.rep++;
					pthread_mutex_unlock(&prg.lockcacheex);
					cacheex_push(&req, cw, NULL);
					break;

				case PIPE_CACHEEX_PUSH_REMOTE: // from cacheex peers
					// Setup Cache Request
					req.caid = (buf[1]<<8) | buf[2];
					req.tag = buf[3];
					req.provid = (buf[4]<<16) | (buf[5]<<8) | buf[6];
					req.sid = (buf[7]<<8) | buf[8];
					req.hash = (buf[9]<<24) | (buf[10]<<16) | (buf[11]<<8) | buf[12];
					memcpy(req.ecmd5, buf+13, 16);
					memcpy(cw, buf+29, 16);
					// Send Reply
					pthread_mutex_lock(&prg.lockcacheex);
					cfg.cacheex.rep++;
					pthread_mutex_unlock(&prg.lockcacheex);
					cacheex_push(&req, cw, buf+29+16);
					break;

				case PIPE_CACHEEX_PUSH_IN:
					/* This frame is currently produced but has no consumer action. */
					break;
				}
			}
		}

		pfd.fd = prg.pipe.cacheex[0];
		pfd.events = POLLIN | POLLPRI;
	} while (poll(&pfd, 1, 1) > 0);
}

/*
1 for server>clients
1 for client>servers
*/
///////////////////////////////////////////////////////////////////////////////

int cacheex_check( struct cache_data *req )
{
	if ( !req->caid || !req->hash || !req->sid ) return 0;
	if (cfg.cache.caids[0]) {
		int i;
		for(i=0; i<32; i++) {
			if (!cfg.cache.caids[i]) break;
			if (cfg.cache.caids[i]==req->caid) return 1;
		}
		return 0;
	}
	return 1;
}

///////////////////////////////////////////////////////////////////////////////

void *cacheex_recvmsg_thread(void *param)
{
	__atomic_store_n(&prg.pid_ccex_msg, (pid_t)syscall(SYS_gettid), __ATOMIC_RELEASE);

	struct pollfd pfd[CACHEEX_MAX_PFD];
	uint64_t pfd_server_generation[CACHEEX_MAX_PFD];
	uint32_t pfd_cc_session_generation[CACHEEX_MAX_PFD];
	while (!prg.restart) {
		int pfdcount = 0;
		uint64_t config_generation;
		uint32_t thread_delay;

		newcamd_config_read_lock();
		config_generation = cfg.cache.generation;
		thread_delay = cfg.delay.thread;

		// Clients mode 2; the session snapshot keeps poll events tied to the
		// exact connection, without holding locksrv while polling or dispatching.
		struct cccam_server_data *cccam = cfg.cccam.server;
		while (cccam) {
			struct cc_client_data *cccli = cccam->cacheexclient;
			while (cccli) {
				int sock;
				uint32_t generation;
				cccli->ipoll = -1;
				if (!IS_DISABLED(cccam->flags) && cccli->cacheex_mode == 2 &&
				    !IS_DISABLED(cccli->flags) && pfdcount < CACHEEX_MAX_PFD &&
				    cc_client_session_snapshot(cccli, &sock, &generation)) {
					cccli->ipoll = pfdcount;
					pfd[pfdcount].fd = sock;
					pfd_cc_session_generation[pfdcount] = generation;
					pfd[pfdcount++].events = POLLIN | POLLPRI;
				}
				cccli = cccli->next;
			}
			cccam = cccam->next;
		}

		newcamd_config_read_unlock();

		/* Server connection fields and ipoll are owned by locksrv. Reacquire
		 * the config/runtime lock pair without waiting on locksrv while pinning
		 * the graph, then discard this snapshot if a reload crossed the gap. */
		newcamd_config_read_lock_mutex(&prg.locksrv);
		if (config_generation != cfg.cache.generation) {
			newcamd_config_read_unlock_mutex(&prg.locksrv);
			continue;
		}
		struct server_data *srv = cfg.cacheexserver;
		while (srv) {
			srv->ipoll = -1;
			if (srv->cacheex_mode == 3 && !IS_DISABLED(srv->flags) &&
			    srv->handle > 0 && pfdcount < CACHEEX_MAX_PFD) {
				srv->ipoll = pfdcount;
				pfd[pfdcount].fd = srv->handle;
				pfd_server_generation[pfdcount] = srv->connect_generation;
				pfd[pfdcount++].events = POLLIN | POLLPRI;
			}
			srv = srv->next;
		}
		newcamd_config_read_unlock_mutex(&prg.locksrv);

		int retval = poll(pfd, pfdcount, 3005); // approximately 3 seconds
		if (retval > 0) {
			if (thread_delay) usleep(thread_delay);

			/* Re-pin and reject descriptor snapshots made obsolete by reload. */
			newcamd_config_read_lock();
			if (config_generation == cfg.cache.generation) {
				/* Client receive/disconnect paths serialize through each client's
				 * I/O guard. Do not hold locksrv across socket work or parser calls. */
				cccam = cfg.cccam.server;
				while (cccam) {
					if (!IS_DISABLED(cccam->flags)) {
						struct cc_client_data *cccli = cccam->cacheexclient;
						while (cccli) {
							int ipoll = cccli->ipoll;
							if (cccli->cacheex_mode == 2 &&
							    !IS_DISABLED(cccli->flags) && ipoll >= 0 &&
							    ipoll < pfdcount &&
							    pfd_cc_session_generation[ipoll]) {
								int sock = pfd[ipoll].fd;
								uint32_t generation =
									pfd_cc_session_generation[ipoll];
								if (pfd[ipoll].revents & (POLLHUP | POLLNVAL))
									cc_disconnect_cli_session(cccli, sock,
									                           generation);
								else if (pfd[ipoll].revents & (POLLIN | POLLPRI))
									cc_cli_recvmsg_session(cccli, sock,
									                        generation);
							}
							cccli = cccli->next;
						}
					}
					cccam = cccam->next;
				}

				pthread_mutex_lock(&prg.locksrv);
				srv = cfg.cacheexserver;
				while (srv) {
					int ipoll = srv->ipoll;
					if (srv->cacheex_mode == 3 && !IS_DISABLED(srv->flags) &&
					    srv->handle > 0 && ipoll >= 0 && ipoll < pfdcount &&
					    srv->handle == pfd[ipoll].fd &&
					    srv->connect_generation == pfd_server_generation[ipoll]) {
						if (pfd[ipoll].revents & (POLLHUP | POLLNVAL))
							disconnect_srv(srv);
						else if (pfd[ipoll].revents & (POLLIN | POLLPRI)) {
							if (srv->type == TYPE_CCCAM) cc_srv_recvmsg(srv);
#ifdef CAMD35_CLI
							else if (srv->type == TYPE_CAMD35)
								camd35_srv_recvmsg(srv);
#endif
#ifdef CS378X_CLI
							else if (srv->type == TYPE_CS378X)
								cs378x_srv_recvmsg(srv);
#endif
						}
					}
					srv = srv->next;
				}
				pthread_mutex_unlock(&prg.locksrv);
			}
			newcamd_config_read_unlock();
		} else if (retval < 0 && errno != EINTR) {
			debugf(0, " thread receive messages: poll error %d(errno=%d)\n", retval, errno);
		}
	}
	return NULL;
}

void *cacheex_pipe_thread(void *param)
{
	struct pollfd pfd;
	while(1) {
		pfd.fd = prg.pipe.cacheex[0];
		pfd.events = POLLIN | POLLPRI;

		int retval = poll(&pfd, 1, 3015); // for 3seconds

		if ( retval>0 ) {
			newcamd_config_read_lock();
			pthread_mutex_lock(&prg.locksrv);
			cacheex_pipe_recvmsg();
			pthread_mutex_unlock(&prg.locksrv);
			newcamd_config_read_unlock();
		}
		else if ( retval<0 ) {
			debugf(0, " thread receive messages: poll error %d(errno=%d)\n", retval, errno);
		}

	}
	return NULL;
}

int start_thread_cacheex()
{
	//memset( icacheextab, 0, sizeof(icacheextab));
	//create_thread(PACKED_MPTR(&prg, pthread_t, tid_cacheex), (threadfn)cacheex_send_thread,NULL);
	create_thread(PACKED_MPTR(&prg, pthread_t, tid_cacheex), cacheex_recvmsg_thread,NULL);
	create_thread(PACKED_MPTR(&prg, pthread_t, tid_cacheex), cacheex_pipe_thread,NULL);
	return 0;
}


