//CMD00 - ECM (request)
//CMD01 - ECM (response)
//CMD02 - EMM (in clientmode - set EMM, in server mode - EMM data) - obsolete
//CMD03 - ECM (cascading request)
//CMD04 - ECM (cascading response)
//CMD05 - EMM (emm request) send cardata/cardinfo to client
//CMD06 - EMM (incomming EMM in server mode)
//CMD19 - EMM (incomming EMM in server mode) only seen with caid 0x1830
//CMD08 - Stop sending requests to the server for current srvid,prvid,caid
//CMD44 - MPCS/OScam internal error notification
//CMD55 - connect_on_init/keepalive

//CMD0x3d - CACHEEX Cache-push id request
//CMD0x3e - CACHEEX Cache-push id answer
//CMD0x3f - CACHEEX cache-push


struct camd35_client_data *getcs378xclientbyid(uint32_t id)
{
	if (cfg.cs378x.server) {
		struct camd35_client_data *cli = cfg.cs378x.server->client;
		while (cli) {
			if (!(cli->flags&FLAG_DELETE))
				if (cli->id==id) return cli;
			cli = cli->next;
		}
	}
	return NULL;
}

///////////////////////////////////////////////////////////////////////////////
// CONNECT
///////////////////////////////////////////////////////////////////////////////

void cs378x_disconnect_cli(struct camd35_client_data *cli)
{
	cli->connection.status = 0;
	uint32_t ticks = GetTickCount();
	cli->connection.uptime += ticks - cli->connection.time;
	cli->connection.lastseen = ticks; // Last Seen
	close(cli->handle);
	cli->handle = -1;
	debugf(0," cs378x: client '%s' disconnected \n", cli->user);
}

void *cs378x_connect_cli(struct connect_cli_data *param)
{
	uint8_t buf[1024];
	// Store data from param
	struct camd35_server_data *cs378x = param->server;
	int sock = param->sock;
	uint32_t ip = param->ip;
	free(param);

	int received = recv_nonb( sock, buf, 32+4, 5000);
	if (received<=0 || received < 36 || received > (int)sizeof(buf)) {
		debugf(getdbgflag(DBG_CS378X,0,0), " cs378x: new connection aborted, rto\n");
		close(sock);
		return NULL;
	}
	uint32_t ucrc = (buf[0]<<24)|(buf[1]<<16)|(buf[2]<<8)|buf[3];
	//Check for clients
	struct camd35_client_data *cli = cs378x->client;
	while (cli) {
		if (cli->ucrc==ucrc) break;
		cli = cli->next;
	}
	if (!cli) {
		cli = cs378x->cacheexclient;
		while (cli) {
			if (cli->ucrc==ucrc) break;
			cli = cli->next;
		}
	}
	if (!cli) {
		debugf(getdbgflag(DBG_CS378X,cs378x->id,0)," cs378x%d: Unknown Client UCRC=%08x (%s)\n", cs378x->id, ucrc, ip2string(ip));
		close(sock);
		return NULL;
	}
	// Check reconnection
	if (cli->connection.status>0) cs378x_disconnect_cli(cli);
	//
	aes_decrypt( &cli->decryptkey, buf+4, received-4);
	int newlen = cs378x_frame_length(buf, received, (int)sizeof(buf), 0);
	if (newlen < 0 || newlen < received) {
		debugf(getdbgflag(DBG_CS378X,0,0), " cs378x: new connection has invalid or oversized frame\n");
		close(sock);
		return NULL;
	}
	if (received<newlen) {
		int need = newlen-received;
		int n = recv_nonb( sock, buf+received, need, 1000);
		if (n != need) {
			debugf(getdbgflag(DBG_CS378X,0,0), " cs378x: new connection aborted, failed receive data\n");
			close(sock);
			return NULL;
		}
		aes_decrypt( &cli->decryptkey, buf+received, n);
		received += n;
	}
	if (received != newlen) {
		close(sock);
		return NULL;
	}
	//
	if (buf[4]==CAMD_KEEPALIVE) {
		//debugf(0," camd35: Keepalive from client '%s'\n", cli->user);
		if ( !cs378x_send( sock, &cli->encryptkey, cli->ucrc, buf+4, 20+1) ) {
			close( sock );
			return NULL;
		}
		debugf(getdbgflag(DBG_CS378X,0,cli->id)," cs378x: client '%s' connected\n", cli->user);
		cli->connection.status = 1;
		cli->connection.time = GetTickCount();
		cli->handle = sock;
		cli->ip = ip;
		pipe_wakeup( prg.pipe.cs378x[1] );
	}
#ifdef CACHEEX
	else if (buf[4]==CAMD_CEX_IDREQUEST) {
		if (received < 32) { close(sock); return NULL; }
		//debugf(getdbgflag(DBG_CS378X,0,cli->id)," camd35: Nodeid from client '%s'\n", cli->user);
		memcpy( cli->nodeid, buf+24, 8);
		memset(buf, 0, 20+12);
		buf[0] = CAMD_CEX_IDREPLY;
		buf[1] = 12;
		memcpy( buf+20, cccam_nodeid, 8);
		if ( !cs378x_send( sock, &cli->encryptkey, cli->ucrc, buf, 20+12) ) {
			close( sock );
			return NULL;
		}
		char str[8*3+1];
		array2hex( cli->nodeid, str, 8);
		debugf(getdbgflag(DBG_CS378X,0,cli->id)," cs378x: CacheEX client '%s' connected, Nodeid = %s\n", cli->user, str);
		cli->connection.status = 1;
		cli->connection.time = GetTickCount();
		cli->handle = sock;
		cli->ip = ip;
		pipe_wakeup( prg.pipe.cs378x_cex[1] );
	}
#endif
	else close(sock);
	return NULL;
}


void cs378x_srv_accept(struct camd35_server_data *srv)
{
	struct sockaddr_in newaddr;
	socklen_t socklen = sizeof(struct sockaddr);
	int newfd = accept( srv->handle, (struct sockaddr*)&newaddr, /*(socklen_t*)*/&socklen);
	if ( newfd<=0 ) {
		if ( (errno!=EAGAIN) && (errno!=EINTR) ) debugf( getdbgflag(DBG_CS378X,0,0)," cs378x: Accept failed (errno=%d)\n", errno);
	}
	else {
		uint32_t newip = newaddr.sin_addr.s_addr;
		if ( isblockedip(newip) ) {
			debugf( getdbgflag(DBG_CS378X,0,0)," cs378x: New Connection (%s) closed, ip blocked\n", ip2string(newip) );
			close(newfd);
		}
		else {
			pthread_t srv_tid;
			if (cfg.cccam.keepalive) SetSocketKeepalive(newfd);
			SetSocketNoDelay(newfd);
			if (SetSoketNonBlocking(newfd) < 0) {
				debugf(DBG_ERROR, " cs378x: unable to set accepted socket nonblocking (errno=%d)\n", errno);
				close(newfd);
				return;
			}
			//debugf( getdbgflag(DBG_CS378X,0,0)," cs378x: new client Connection(%d)...%s\n", newfd, ip2string(newip) );
			struct connect_cli_data *newdata = malloc( sizeof(struct connect_cli_data) );
			newdata->server = srv; 
			newdata->sock = newfd; 
			newdata->ip = newaddr.sin_addr.s_addr;
			if ( !create_thread(&srv_tid, (threadfn)cs378x_connect_cli,newdata) ) {
				free( newdata );
				close( newfd );
			}
		}
	}
}

#ifndef MONOTHREAD_ACCEPT

void *cs378x_accept_thread(void *param)
{
	prctl(PR_SET_NAME,"cs378x Accept",0,0,0);

	while(!prg.restart) {
		uint64_t config_generation;
		struct pollfd pfd[MAX_PFD];
		int pfdcount = 0;
		newcamd_config_read_lock();
		config_generation = cfg.cache.generation;
		struct camd35_server_data *cs378x = cfg.cs378x.server;
		while (cs378x) {
			if ( !IS_DISABLED(cs378x->flags) && (cs378x->handle>0) ) {
				cs378x->ipoll = pfdcount;
				pfd[pfdcount].fd = cs378x->handle;
				pfd[pfdcount++].events = POLLIN | POLLPRI;
			} else cs378x->ipoll = -1;
			cs378x = cs378x->next;
		}
		newcamd_config_read_unlock();

		if (pfdcount) {
			int retval = poll(pfd, pfdcount, 3006);
			if (retval > 0) {
				newcamd_config_read_lock();
				if (config_generation != cfg.cache.generation) {
					newcamd_config_read_unlock();
					continue;
				}
				struct camd35_server_data *cs378x = cfg.cs378x.server;
				while (cs378x) {
					if ( !IS_DISABLED(cs378x->flags) && (cs378x->handle>0) &&
					     (cs378x->ipoll>=0) && (cs378x->handle==pfd[cs378x->ipoll].fd) &&
					     (pfd[cs378x->ipoll].revents & (POLLIN|POLLPRI)) )
						cs378x_srv_accept(cs378x);
					cs378x = cs378x->next;
				}
				newcamd_config_read_unlock();
			} else if (retval < 0) usleep(96000);
		} else sleep(1);
	}
	return NULL;
}
#endif


///////////////////////////////////////////////////////////////////////////////
// SEND DCW
///////////////////////////////////////////////////////////////////////////////

void cs378x_senddcw_cli(struct camd35_client_data *cli, const struct ledger_delivery_snapshot *delivery)
{
	uint8_t buf[CC_MAXMSGSIZE];
	uint32_t ticks = GetTickCount();
#ifdef CACHEEX
	if (!delivery || !delivery->lifetime_id ||
	    delivery->lifetime_id != __atomic_load_n(&cli->ecm.lifetime_id, __ATOMIC_ACQUIRE)) return;
#endif

	if (cli->ecm.status==STAT_DCW_SENT) {
		debugf(getdbgflag(DBG_CS378X,0,cli->id)," +> cw send failed to cs378x client '%s', cw already sent\n", cli->user); 
		return;
	}
	if (cli->connection.status<=0) {
		debugf(getdbgflag(DBG_CS378X,0,cli->id)," +> cw send failed to cs378x client '%s', client disconnected\n", cli->user); 
		return;
	}
	if (!cli->ecm.busy) {
		debugf(getdbgflag(DBG_CS378X,0,cli->id)," +> cw send failed to cs378x client '%s', no ecm request\n", cli->user); 
		return;
	}

#ifdef CACHEEX
	ECM_DATA *ecm = NULL;
	uint16_t response_caid = delivery->caid;
	uint32_t response_provid = delivery->provid;
	uint8_t response_provider_known = delivery->provider_known;
	uint16_t response_sid = delivery->sid;
	uint32_t response_hash = delivery->hash;
	uint8_t response_tag = delivery->tag;
	uint32_t response_profile = delivery->profile_id;
	uint32_t response_recvtime = delivery->recvtime;
#else
	ECM_DATA *ecm = cli->ecm.request;
	uint16_t response_caid = ecm->caid;
	uint32_t response_provid = ecm->provid;
	uint8_t response_provider_known = ecm->provider_known;
	uint16_t response_sid = ecm->sid;
	uint32_t response_hash = ecm->hash;
	uint8_t response_tag = ecm->ecm[0];
	uint32_t response_profile = ecm->cs ? (uint32_t)ecm->cs->id : 0;
	uint32_t response_recvtime = cli->ecm.recvtime;
#endif
	//FREEZE
	int samechannel = (cli->lastecm.caid==response_caid)&&(cli->lastecm.prov==response_provid)&&(cli->lastecm.sid==response_sid);
	int enablefreeze=0;
	if (samechannel) {
		if ( (cli->lastecm.hash!=response_hash)&&(cli->lastecm.tag!=response_tag) )
		if (cli->lastecm.status==1 && (uint32_t)(ticks-cli->lastdcwtime)>200u) enablefreeze = 1;
	} else cli->zap++;
	//
	cli->lastecm.caid = response_caid;
	cli->lastecm.prov = response_provid;
	cli->lastecm.provider_known = response_provider_known;
	cli->lastecm.sid = response_sid;
	cli->lastecm.hash = response_hash;
	cli->lastecm.tag = response_tag;
	cli->lastecm.decodetime = ticks-response_recvtime;
#ifdef CACHEEX
	cli->lastecm.request = delivery->request;
#else
	cli->lastecm.request = cli->ecm.request;
#endif

	// SID
	buf[8] = (response_sid>>8) & 0xFF;
	buf[9] = (response_sid) & 0xFF;
	// CAID
	buf[10] = (response_caid>>8) & 0xFF;
	buf[11] = (response_caid) & 0xFF;
	// PROVID
	buf[12] = (response_provid>>24) & 0xFF;
	buf[13] = (response_provid>>16) & 0xFF;
	buf[14] = (response_provid>>8) & 0xFF;
	buf[15] = (response_provid) & 0xFF;
	// PIN
	buf[16] = (cli->ecm.pin>>8);
	buf[17] = cli->ecm.pin & 0xFF;

#ifdef CACHEEX
	if (delivery->status == STAT_DCW_SUCCESS) {
#else
	if ( (ecm->dcwstatus==STAT_DCW_SUCCESS)&&(ecm->hash==cli->ecm.hash) ) {
#endif
		uint32_t sent_at;
		cli->lastecm.status=0;
		cli->lastecm.dcwsrctype = DCW_SOURCE_NONE;
		cli->lastecm.dcwsrcid = 0;
		memset(cli->lastecm.dcw, 0, sizeof(cli->lastecm.dcw));
#ifdef CACHEEX
		memset(cli->lastecm.ecmd5, 0, sizeof(cli->lastecm.ecmd5));
		cli->lastecm.ecmlen = 0;
#endif
		buf[0] = CAMD_ECM_REPLY;
		buf[1] = 16;
		// ECM
#ifdef CACHEEX
		memcpy(buf+20, delivery->cw, 16);
#else
		memcpy(buf+20, ecm->cw, 16);
#endif
		if ( !cs378x_send( cli->handle, &cli->encryptkey, cli->ucrc, buf, 20+16) ) {
			cs378x_disconnect_cli( cli );
			return;
		}
		sent_at = GetTickCount();
#ifdef CACHEEX
		LEDGER_DELIVERY_TO_LASTECM(cli->lastecm, *delivery);
#else
		cli->lastecm.dcwsrctype = ecm->dcwsrctype;
		cli->lastecm.dcwsrcid = ecm->dcwsrcid;
		memcpy(cli->lastecm.dcw, ecm->cw, sizeof(cli->lastecm.dcw));
#endif
		cli->lastecm.status=1;
		cli->ecmok++;
		cli->lastdcwtime = sent_at;
		cli->ecmoktime += sent_at-response_recvtime;
#ifdef CACHEEX
		ledger_note_delivered(delivery, sent_at);
#endif
		debugf(getdbgflagpro(DBG_CS378X,0,cli->id,response_profile)," => cw to cs378x client '%s' ch %04x:%06x:%04x (%dms)\n", cli->user, response_caid,response_provid,response_sid, sent_at-response_recvtime);
	}
	else { //if (ecm->data->dcwstatus==STAT_DCW_FAILED)
		if (enablefreeze) {
			cli->freeze++;
		}
		cli->lastecm.dcwsrctype = DCW_SOURCE_NONE;
		cli->lastecm.dcwsrcid = 0;
		cli->lastecm.status=0;
		memset(cli->lastecm.dcw, 0, sizeof(cli->lastecm.dcw));
#ifdef CACHEEX
		memset(cli->lastecm.ecmd5, 0, sizeof(cli->lastecm.ecmd5));
		cli->lastecm.ecmlen = 0;
#endif
		buf[0] = 0x44;
		buf[1] = 0;
		if ( !cs378x_send( cli->handle, &cli->encryptkey, cli->ucrc, buf, 20) ) {
			cs378x_disconnect_cli( cli );
			return;
		}
		debugf(getdbgflagpro(DBG_CS378X,0,cli->id,response_profile)," |> decode failed to cs378x client '%s' ch %04x:%06x:%04x (%dms)\n", cli->user, response_caid,response_provid,response_sid, GetTickCount()-response_recvtime);
	}
	cli->ecm.busy=0;
	cli->ecm.status = STAT_DCW_SENT;
}

///////////////////////////////////////////////////////////////////////////////

// Check sending cw to clients
void cs378x_check_sendcw(ECM_DATA *ecm, const struct ledger_delivery_snapshot *delivery)
{
#ifdef CACHEEX
	if (!delivery) return;
#endif
	struct camd35_server_data *cs378x = cfg.cs378x.server;
	while (cs378x) {
		if ( !IS_DISABLED(cs378x->flags) && (cs378x->handle>0) ) {
			struct camd35_client_data *cli = cs378x->client;
			while (cli) {
				#ifdef CACHEEX
				if (!IS_DISABLED(cli->flags) && cli->handle>0 && __atomic_load_n(&cli->ecm.lifetime_id, __ATOMIC_ACQUIRE)==delivery->lifetime_id) {
#else
				if (!IS_DISABLED(cli->flags) && cli->handle>0 && cli->ecm.busy && cli->ecm.request==ecm && cli->ecm.hash==ecm->hash) {
#endif
					cs378x_senddcw_cli( cli, delivery );
				}
				cli = cli->next;
			}
		}
		cs378x = cs378x->next;
	}
}



///////////////////////////////////////////////////////////////////////////////
// RECV MSG
///////////////////////////////////////////////////////////////////////////////

void cs378x_store_ecmclient(ECM_DATA *ecm, struct camd35_client_data *cli)
{
	uint32_t ticks = GetTickCount();
	cli->ecm.recvtime = ticks;
	cli->ecm.request = ecm;
#ifdef CACHEEX
	__atomic_store_n(&cli->ecm.lifetime_id, ecm_request_lifetime_id_load(ecm), __ATOMIC_RELEASE);
#endif
    cli->ecm.status = STAT_ECM_SENT;
	ecm_addip(ecm, cli->ip);

	/* IBS: Record ECM request from CS378x client */
	{
		uint32_t bad_source = 0;
		uint8_t bad_cw_hash[8] = {0};
		int bs_detected = ibs_on_ecm_request(
			cli->ip, ecm->caid, ecm->provid, ecm->sid,
			ticks, &bad_source, bad_cw_hash);
		if (bs_detected) {
			debugf(0, " !!! [IBS-DETECT] BLACK SCREEN (CS378x) %04X:%06X:%04X client %08X -- source %d was WRONG\n",
				ecm->caid, ecm->provid, ecm->sid, cli->ip, bad_source);
			cwpostval_trust_bad(bad_source);
		}
	}
}

void *cs378x_recvmsg( struct camd35_client_data *cli )
{
	struct cardserver_data *cs;
	uint8_t cw[16];
	uint8_t buf[2048];
	int received = recv_nonb( cli->handle, buf, 32+4, 1000);
	if (received<=0 || received < 36 || received > (int)sizeof(buf)) {
		debugf(getdbgflag(DBG_CS378X,0,cli->id)," cs378x: read failed from client '%s'\n", cli->user);
		cs378x_disconnect_cli(cli);
		return NULL;
	}
	uint32_t ucrc = (buf[0]<<24)|(buf[1]<<16)|(buf[2]<<8)|buf[3];
	if (ucrc!=cli->ucrc) {
		debugf(getdbgflag(DBG_CS378X,0,cli->id)," cs378x: wrong UCRC failed from client '%s'\n", cli->user);
		cs378x_disconnect_cli(cli);
		return NULL;
	}
	aes_decrypt( &cli->decryptkey, buf+4, received-4);
	int newlen = cs378x_frame_length(buf, received, (int)sizeof(buf), 1);
	if (newlen < 0 || newlen < received) {
		debugf(getdbgflag(DBG_CS378X,0,cli->id)," cs378x: invalid or oversized frame from client '%s'\n", cli->user);
		cs378x_disconnect_cli(cli);
		return NULL;
	}
	if (received<newlen) {
		int need = newlen-received;
		int n = recv_nonb( cli->handle, buf+received, need, 1000);
		if (n != need) {
			debugf(getdbgflag(DBG_CS378X,0,cli->id)," cs378x: read failed from client '%s'\n", cli->user);
			cs378x_disconnect_cli(cli);
			return NULL;
		}
		aes_decrypt( &cli->decryptkey, buf+received, n);
		received += n;
	}
	if (received != newlen) {
		cs378x_disconnect_cli(cli);
		return NULL;
	}
	//debugf(getdbgflag(DBG_CS378X,0,cli->id), " cs378x: msg from client '%s' \n", cli->user); //debughex(buf, newlen);

	uint32_t ticks = GetTickCount();

	switch (buf[4]) {
		// Keepalive
		case CAMD_KEEPALIVE:
			//debugf(0," camd35: Keepalive from client '%s'\n", cli->user);
			if ( !cs378x_send( cli->handle, &cli->encryptkey, cli->ucrc, buf+4, 20+1) ) cs378x_disconnect_cli( cli );
			else if (cli->connection.status<=0) {
				debugf(getdbgflag(DBG_CS378X,0,cli->id)," cs378x: client '%s' connected\n", cli->user);
				cli->connection.status = 1;
				cli->connection.time = GetTickCount();
			}
			break;

#ifdef CACHEEX
		// Request Nodeid
		case CAMD_CEX_IDREQUEST:
			if (received < 32) break; /* 8-byte node ID starts at offset 24. */
			//debugf(0," camd35: Nodeid from client '%s'\n", cli->user);
			memcpy( cli->nodeid, buf+24, 8);
			memset(buf, 0, 20+12);
			buf[0] = CAMD_CEX_IDREPLY;
			buf[1] = 12;
			memcpy( buf+20, cccam_nodeid, 8);
			if ( !cs378x_send( cli->handle, &cli->encryptkey, cli->ucrc, buf, 20+12) ) cs378x_disconnect_cli( cli );
			else if (cli->connection.status<=0) {
				char str[8*3+1];
				array2hex( cli->nodeid, str, 8);
				debugf(getdbgflag(DBG_CS378X,0,cli->id)," cs378x: client '%s' connected, Nodeid = %s\n", cli->user, str);
				cli->connection.status = 1;
				cli->connection.time = GetTickCount();
			}
			break;

		// push out
		case CAMD_CEX_PUSH:
			if (cli->cacheex_mode!=3) break;
			/* Includes the trailing 8-byte node ID at [61..68]. */
			if (received < 69) break;
			memcpy( cw, buf+44, 16);
			if (dcw_hard_reject(cw)) break; /* TASK 3.15: profile gate is cache_setdcw */
			struct cache_data cacheex;
			memset(&cacheex, 0, sizeof(cacheex));
			cacheex.provider_known = 1; /* fixed-width packet has provider, even when zero */
			cacheex.sid = (buf[12]<<8)|buf[13];
			cacheex.caid = (buf[14]<<8)|buf[15];
			cacheex.provid = (buf[16]<<24)|(buf[17]<<16)|(buf[18]<<8)|buf[19];
			// Look for cardserver
			cs = getcsbycaprovid(cacheex.caid, cacheex.provid);
			if ( !cs || !cs->option.fallowcacheex ) {
				pthread_mutex_lock(&prg.lockcacheex);
				cli->cacheex.badcw++;
				pthread_mutex_unlock(&prg.lockcacheex);
				break;
			}
			if ((buf[23]&0xFE)==0x80) cacheex.tag = buf[23]; else cacheex.tag = 0;
			memcpy( cacheex.ecmd5, buf+24, 16);
			//if ( !checkECMD5(cacheex.ecmd5) ) cli->cacheex.totalcsp++;
			cacheex.hash = (buf[43]<<24) | (buf[42]<<16) | (buf[41]<<8) | buf[40];
			if (!cacheex_check(&cacheex)) break;
			pthread_mutex_lock(&prg.lockcacheex);
			cli->cacheex.got[0]++;
			int uphop = buf[60];
			if (uphop<10) cli->cacheex.got[uphop]++;
			pthread_mutex_unlock(&prg.lockcacheex);
			//
			pthread_mutex_lock( &prg.lockcache );
			int res = cache_setdcw( &cacheex, cw, NO_CYCLE, PEER_CS378X_CLIENT | cli->id );
#ifdef CACHEEX
			/*
			 * TASK 1.3 — offer this CW to the reuse detector.
			 *
			 * Safe here and only here: on the cache-exchange paths `cacheex.ecmd5`
			 * is decoded from the message, so the 128-bit ECM identity is real.
			 * The CSP TYPE_REPLY path (clustredcache.c, `cache_setdcw(&req,...)`)
			 * carries a 32-bit hash and no ecmd5 at all, so `req.ecmd5` there is
			 * uninitialised stack and must never be offered -- feeding it in would
			 * manufacture proofs out of garbage.
			 *
			 * OBSERVE-ONLY: counts and logs, changes no score and disables nothing.
			 * GR3 allows hard action on a genuine reuse proof, but only once the
			 * detector has been checked against real traffic.
			 */
			if ( cwreuse_offer_scoped( &cwreuse_tab, cw, cacheex.ecmd5, 1,
					cacheex.caid, cacheex.sid, cacheex.provid, cacheex.provider_known,
					GetTickCount() ) == CWREUSE_PROOF ) {
				cwreuse_proofs++;
				/* TASK 2.6: the convicted key itself goes into negative memory,
				 * so no other source and no fresh entry can ever serve it. */
				cwn_note( DCW_SOURCE_CACHE, PEER_CS378X_CLIENT | cli->id, cw, cacheex.caid, cacheex.provid, cacheex.provider_known, cacheex.sid, GetTickCount() );
				/*
				 * TASK 1.4 — a definitive proof scores against the source that
				 * supplied the key, at GR4 granularity. The srcid keeps its PEER_*
				 * origin flags, because a CSP peer and a cache-exchange client with
				 * the same id are different sources and must not share a score.
				 */
				trust_record_lk_scoped( &trust_tab, TRUST_EV_PROOF, DCW_SOURCE_CACHE, PEER_CS378X_CLIENT | cli->id,
					cacheex.caid, cacheex.provid, cacheex.provider_known,
					cacheex.sid, GetTickCount() );
				/* TASK 2.8: the same proof, summed one level coarser. */
				trustagg_note_lk( TRUST_EV_PROOF, DCW_SOURCE_CACHE, PEER_CS378X_CLIENT | cli->id,
				                  cacheex.caid, GetTickCount() );
				debugf(DBG_ERROR," !!! CW REUSE PROOF: same cw for a different ecm on a different service, ch %04x:%06x:%04x from %s\n",
					cacheex.caid, cacheex.provid, cacheex.sid, cli->user);
				/*
				 * TASK 1.5 -- mark, then sweep. This is the first place in the
				 * project that ACTS rather than observes, and it acts on a proof
				 * only (GR3): cwreuse_offer() returned CWREUSE_PROOF, so this
				 * source has now sent the identical key for two different ECMs on
				 * two different services. Nothing here triggers on a score.
				 *
				 * Marking sets DCW_ERROR on the cached CW, which the serving path
				 * already refuses -- no unlinking, no free, no cache layout change.
				 * lockcache is held at this point, as the sweep requires.
				 *
				 * Scoped to this one channel, per GR4. A source that is bad here
				 * is not assumed bad everywhere; widening is the trust score's
				 * job, and a false positive that kills a good card is worse than
				 * the original black screen (GR3).
				 */
				if ( cache_purge_mark_scoped( &purge_tab, DCW_SOURCE_CACHE, PEER_CS378X_CLIENT | cli->id,
						cacheex.caid, cacheex.sid, cacheex.provid, cacheex.provider_known,
						GetTickCount() ) ) {
					int npurged = cache_purge_sweep_scoped( &purge_tab, DCW_SOURCE_CACHE, PEER_CS378X_CLIENT | cli->id,
						cacheex.caid, cacheex.sid, cacheex.provid, cacheex.provider_known, DCW_ERROR );
					/* npurged can be 0: the entry may already be gone, or it may have
					 * been supplied first by a different peer (cwdata->peerid records
					 * the FIRST reporter only). Logged either way -- GR8. */
					debugf(DBG_ERROR," !!! CACHE PURGE: ch %04x:%06x:%04x marked %d cached cw(s) from %s\n",
						cacheex.caid, cacheex.provid, cacheex.sid, npurged, cli->user);
				}
			}
#endif
			pthread_mutex_unlock( &prg.lockcache );
			cache_peerrep_save_after_unlock();
			if (res&DCW_ERROR) {
				if ( !(res&DCW_SKIP) ) {
					pthread_mutex_lock(&prg.lockcacheex);
					cli->cacheex.badcw++;
					pthread_mutex_unlock(&prg.lockcacheex);
				}
			}
			else if (res&DCW_CYCLE) {
				if ( cs->option.cacheex.maxhop>uphop ) {
					uint8_t nodeid[8];
					memcpy( nodeid, buf+61, 8);
					pipe_send_cacheex_push_cache(&cacheex, cw, nodeid); //cacheex_push(&cacheex, cw, nodeid);
				}
			}
			//debugf(getdbgflag(DBG_CS378X,0,cli->id)," cs378x: push from client '%s' %04x:%06x:%04x|%02x:%08x\n", cli->user, cacheex.caid,cacheex.provid,cacheex.sid, cacheex.tag,cacheex.hash);
			break;
#endif

		case CAMD_ECM_REQUEST:
#ifdef CACHEEX
			if (cli->cacheex_mode) {
				cli->ecmdenied++;
				break;
			}
#endif
			cli->ecmnb++;
			cli->lastecmtime = ticks;
			//Check for card availability
			if (received < 27) {
				debugf(getdbgflag(DBG_CS378X,0,cli->id)," cs378x: short ECM header from client '%s'\n", cli->user);
				cs378x_disconnect_cli(cli);
				return NULL;
			}
			int ecmlen = (((buf[25] & 0x0f) << 8) | buf[26]) + 3;
			uint8_t ecmdata[MAX_ECM_SIZE];
			if (ecmlen < 3 || ecmlen > (int)sizeof(ecmdata) || ecmlen > received-24) {
				debugf(getdbgflag(DBG_CS378X,0,cli->id)," cs378x: invalid ECM length %d from client '%s'\n", ecmlen, cli->user);
				cs378x_disconnect_cli(cli);
				return NULL;
			}
			memcpy( ecmdata, buf+24, (size_t)ecmlen );
			uint16_t pin = buf[4+16]<<8 | buf[4+17];
			uint16_t sid = buf[4+8]<<8 | buf[4+9];
			uint16_t caid = buf[4+10]<<8 | buf[4+11];
			uint32_t provid = ecm_getprovid( ecmdata, ecmlen, caid );
			if (provid==0) provid = buf[4+12]<<24 | buf[4+13]<<16 | buf[4+14]<<8 | buf[4+15];
			if ( !caid ) {
				cli->ecmdenied++;
				buf[4] = 0x44;
				buf[5] = 0;
				if ( !cs378x_send( cli->handle, &cli->encryptkey, cli->ucrc, buf+4, 20) ) cs378x_disconnect_cli( cli );
				else debugf(getdbgflag(DBG_CS378X,0,cli->id)," <!> decode failed to cs378x client '%s' ch %04x:%06x:%04x Invalid CAID\n", cli->user,caid,provid,sid);
				break;
			}
			// Look for cardserver
			cs = cfg.cardserver;
			while (cs) {
				if (caid==cs->card.caid) {
					int j;
					for (j=0; j<cs->card.nbprov;j++) if (provid==cs->card.prov[j].id) break;
					if (j<cs->card.nbprov) break;
				}
				cs = cs->next;
			}
			if (!cs) {
				cli->ecmdenied++;
				buf[4] = 0x44;
				buf[5] = 0;
				if ( !cs378x_send( cli->handle, &cli->encryptkey, cli->ucrc, buf+4, 20) ) cs378x_disconnect_cli( cli );
				else debugf(getdbgflag(DBG_CS378X,0,cli->id)," <!> decode failed to client '%s' ch %04x:%06x:%04x, Invalid CAID/PROVIDER\n", cli->user,caid,provid,sid);
				break;
			}
			// Check for Accepted sids
			uint8_t cw1cycle;
			if ( !accept_sid(cs, provid, sid, ecm_getchid(ecmdata,ecmlen,caid), ecmlen, &cw1cycle) ) {
				cli->ecmdenied++;
				cs->ecmdenied++;
				buf[4] = 0x44;
				buf[5] = 0;
				if ( !cs378x_send( cli->handle, &cli->encryptkey, cli->ucrc, buf+4, 20) ) cs378x_disconnect_cli( cli );
				else debugf(getdbgflagpro(DBG_CS378X,0,cli->id,cs->id)," <!> decode failed to cs378x client '%s' ch %04x:%06x:%04x SID not accepted\n", cli->user,caid,provid,sid);
				break;
			}

			// ACCEPTED
			ecm_lock(); //###
			// Search for ECM
			ECM_DATA *ecm = search_ecmdata_any(cs, ecmdata,  ecmlen, sid, caid, provid, 1); // dont get failed ecm request from cache
			/* TASK R31 (D82) -- receiver attestation tap, mirroring
			 * srv-newcamd's: this client's too-soon re-ask after a
			 * delivered key is one receiver's vote on the BYTES;
			 * the armed number of distinct voters files them
			 * (attest_note). Classification only -- nothing is
			 * scored or refused here, and nothing runs unless
			 * CACHE ATTEST is armed. */
			/* TASK R43 (D106) -- a retry is the SAME ECM asked again:
			 * same hash and same tag. A NEW ECM inside the window is
			 * the provider's normal rotation, not evidence that the
			 * key failed; counting it filed good keys as bad. */
			if ( (cli->lastecm.caid==caid) && (cli->lastecm.prov==provid) && (cli->lastecm.sid==sid) && cli->lastecm.status
				&& ecm && (ecm->hash==cli->lastecm.hash) && (ecm->ecm[0]==cli->lastecm.tag) ) {
				if ( retry_classify( ticks, cli->lastdcwtime, cli->lastecm.status, retry_window_ms, soft_fail_window_ms )==RETRY_HARD ) {
					attest_note( ATTEST_TAG_CS378X | (uint32_t)cli->id, cli->lastecm.dcw,
						cli->lastecm.caid, cli->lastecm.prov,
						cli->lastecm.provider_known, cli->lastecm.sid,
						cli->lastecm.dcwsrctype, cli->lastecm.dcwsrcid, ticks );
#ifdef CACHEEX
					LEDGER_NOTE_CLIENT_FAILURE(ecm, cli->lastecm, cli->lastdcwtime, ticks);
#endif
				}
			}
			int isnew =  ( ecm==NULL );
			if (ecm) {
				ecm->lastrecvtime = ticks;
				if (ecm->dcwstatus==STAT_DCW_FAILED) {
					if (ecm->period > cs->option.dcw.retry) {
						buf[4] = 0x44;
						buf[5] = 0;
						if ( !cs378x_send( cli->handle, &cli->encryptkey, cli->ucrc, buf+4, 20) ) cs378x_disconnect_cli( cli );
						else debugf(getdbgflagpro(DBG_CS378X,0,cli->id, cs->id)," <!> decode failed to cs378x client '%s' ch %04x:%06x:%04x:%08x, already failed\n",cli->user, caid, provid, sid, ecm->hash);
					}
					else {
						ecm->period++; // RETRY
						cs378x_store_ecmclient(ecm, cli);
						debugf(getdbgflagpro(DBG_CS378X,0,cli->id, cs->id)," <- ecm from cs378x client '%s' ch %04x:%06x:%04x:%08x**\n", cli->user, caid, provid, sid, ecm->hash);
						cli->ecm.busy=1;
						cli->ecm.pin = pin;
						cli->ecm.hash = ecm->hash;
						ecm_request_set_dcwstatus(ecm, STAT_DCW_WAIT);
						ecm->cachestatus = 0; //ECM_CACHE_NONE; // Resend Request
						ecm->checktime = 1; // Check NOW
						pipe_wakeup( prg.pipe.ecm[1] );
					}
				}
				else { // SUCCESS/WAIT
					cs378x_store_ecmclient(ecm, cli);
					cli->ecm.pin = pin;
					debugf(getdbgflagpro(DBG_CS378X,0,cli->id, cs->id)," <- ecm cs378x CCcam client '%s' ch %04x:%06x:%04x:%08x*\n", cli->user, caid, provid, sid, ecm->hash);
					cli->ecm.busy=1;
					cli->ecm.hash = ecm->hash;
					if (cli->dcwcheck) {
						if ( !ecm->lastdecode.ecm && (ecm->lastdecode.ecm!=ecm) ) {
							checkfreeze_checkECM( ecm, cli->lastecm.request);
							if (ecm->lastdecode.ecm) pipe_cache_find(ecm, cs);
						}
					}
					// Check for Success/Timeout
					if (!ecm->checktime) {
						if (cli->dcwcheck) {
							if ( (ecm->dcwstatus==STAT_DCW_SUCCESS) && !checkfreeze_setdcw(ecm,ecm->cw) ) { // ??? last ecm is wrong
								ecm_request_set_dcwstatus(ecm, STAT_DCW_WAIT);
								memset( ecm->cw, 0, 16 );
								ecm->checktime = 1; // Wakeup Now
								pipe_wakeup( prg.pipe.ecm[1] );
							}
							else {
								LEDGER_CAPTURE_CLIENT_DELIVERY(delivery, ecm, cli);
								ecm_unlock();
								cs378x_senddcw_cli(cli,delivery);
								break;
							}
						}
						else {
							LEDGER_CAPTURE_CLIENT_DELIVERY(delivery, ecm, cli);
							ecm_unlock();
							cs378x_senddcw_cli(cli,delivery);
							break;
						}
					}
				}
			}
			else {
				// Setup ECM Request for Server(s)
				ecm = store_ecmdata(cs, ecmdata, ecmlen, sid,caid,provid);
				if (!ecm) {
					cs->ecmdenied++;
					cli->ecmdenied++;
					buf[4] = 0x44;
					buf[5] = 0;
					if (!cs378x_send(cli->handle, &cli->encryptkey, cli->ucrc, buf+4, 20)) cs378x_disconnect_cli(cli);
					ecm_unlock();
					break;
				}
				cs->ecmaccepted++;
				cs378x_store_ecmclient(ecm, cli);
				cli->ecm.pin = pin;
				debugf(getdbgflagpro(DBG_CS378X,0,cli->id, cs->id)," <- ecm from cs378x client '%s' ch %04x:%06x:%04x:%08x\n",cli->user,caid,provid,sid, ecm->hash);
				cli->ecm.busy=1;
				cli->ecm.hash = ecm->hash;
				ecm->cw1cycle = cw1cycle;
				ecm_request_set_dcwstatus(ecm, STAT_DCW_WAIT);
#ifdef CHECK_NEXTDCW
				if (cli->dcwcheck) checkfreeze_checkECM( ecm, cli->lastecm.request);
#endif
				if (cs->option.fallowcache) {
					ecm->waitcache = 1;
					ecm_request_set_dcwstatus(ecm, STAT_DCW_WAITCACHE);
					ecm->checktime = ecm->recvtime + cs->option.cachetimeout;
					pipe_cache_find(ecm, cs);
				}
				else ecm->checktime = 1; // Check NOW
				pipe_wakeup( prg.pipe.ecm[1] );
			}
			ecm_unlock();
			if (isnew) wakeup_sendecm();
			break;
	}
	return NULL;
}

//xxx: host port user pass { cacheex_mode = 3; shares = 0500:42400&42800; }
void cs378x_recv_pipe()
{
	uint8_t buf[64];
	struct pollfd pfd;
	do {
		pipe_recv( prg.pipe.cs378x[0], buf, sizeof(buf));
		pfd.fd = prg.pipe.cs378x[0];
		pfd.events = POLLIN | POLLPRI;
	} while (poll(&pfd, 1, 0)>0);
}

///////////////////////////////////////////////////////////////////////////////
void *cs378x_recvmsg_thread(void *param)
{
	pthread_mutex_lock(&prg.locksrv);
	cfg.cs378x.pid_recvmsg = syscall(SYS_gettid);
	pthread_mutex_unlock(&prg.locksrv);
	prctl(PR_SET_NAME,"cs378x RecvMSG Thread",0,0,0);

	while (!prg.restart) {
		struct pollfd pfd[MAX_CSPORTS];
		int pfdcount = 0;
		uint64_t config_generation;

		newcamd_config_read_lock();
		config_generation = cfg.cache.generation;
		pfd[pfdcount].fd = prg.pipe.cs378x[0];
		pfd[pfdcount++].events = POLLIN | POLLPRI;
		
		struct camd35_server_data *cs378x = cfg.cs378x.server;
		while (cs378x) {
			if ( !IS_DISABLED(cs378x->flags)&&(cs378x->handle>0) ) {
				struct camd35_client_data *cli = cs378x->client;
				while (cli) {
					if (cli->connection.status>0) {
						cli->ipoll = pfdcount;
						pfd[pfdcount].fd = cli->handle;
						pfd[pfdcount++].events = POLLIN | POLLPRI;
					} else cli->ipoll = -1;
					cli = cli->next;
				}
			}
			cs378x = cs378x->next;
		}

		newcamd_config_read_unlock();
		int retval = poll(pfd, pfdcount, 3000);

		if (retval>0) {
			newcamd_config_read_lock();
			if (config_generation != cfg.cache.generation) {
				newcamd_config_read_unlock();
				continue;
			}

			struct camd35_server_data *cs378x = cfg.cs378x.server;
			while (cs378x) {
				if ( !IS_DISABLED(cs378x->flags)&&(cs378x->handle>0) ) {
					struct camd35_client_data *client = cs378x->client;
					while (client) {
						if ( (client->handle>0) && (client->ipoll>=0) && (client->handle==pfd[client->ipoll].fd) ) {
							if ( pfd[client->ipoll].revents & (POLLIN|POLLPRI) ) {
								cs378x_recvmsg( client );
							}
						}
						client = client->next;
					}
				}
				cs378x = cs378x->next;
			}

			if ( pfd[0].revents & (POLLIN|POLLPRI) ) cs378x_recv_pipe();
			newcamd_config_read_unlock();

		} else if (retval<0) usleep(99000);
	}
	return NULL;
}


///////////////////////////////////////////////////////////////////////////////
// CAMD35 CACHEEX RECVMSG
///////////////////////////////////////////////////////////////////////////////

void cs378x_cacheex_recv_pipe()
{
	uint8_t buf[64];
	struct pollfd pfd;
	do {
		pipe_recv( prg.pipe.cs378x_cex[0], buf, sizeof(buf));
		pfd.fd = prg.pipe.cs378x_cex[0];
		pfd.events = POLLIN | POLLPRI;
	} while (poll(&pfd, 1, 0)>0);
}

///////////////////////////////////////////////////////////////////////////////
void *cs378x_cacheex_recvmsg_thread(void *param)
{
	pthread_mutex_lock(&prg.locksrv);
	cfg.cs378x.pid_recvmsg = syscall(SYS_gettid);
	pthread_mutex_unlock(&prg.locksrv);
	prctl(PR_SET_NAME,"cs378x RecvMSG Thread",0,0,0);

	while (!prg.restart) {
		struct pollfd pfd[MAX_CSPORTS];
		int pfdcount = 0;
		uint64_t config_generation;

		newcamd_config_read_lock();
		config_generation = cfg.cache.generation;
		pfd[pfdcount].fd = prg.pipe.cs378x_cex[0];
		pfd[pfdcount++].events = POLLIN | POLLPRI;
		
		struct camd35_server_data *cs378x = cfg.cs378x.server;
		while (cs378x) {
			if ( !IS_DISABLED(cs378x->flags)&&(cs378x->handle>0) ) {
				struct camd35_client_data *cli = cs378x->cacheexclient;
				while (cli) {
					if (cli->connection.status>0) {
						cli->ipoll = pfdcount;
						pfd[pfdcount].fd = cli->handle;
						pfd[pfdcount++].events = POLLIN | POLLPRI;
					} else cli->ipoll = -1;
					cli = cli->next;
				}
			}
			cs378x = cs378x->next;
		}

		newcamd_config_read_unlock();
		int retval = poll(pfd, pfdcount, 3000);

		if (retval>0) {
			newcamd_config_read_lock();
			if (config_generation != cfg.cache.generation) {
				newcamd_config_read_unlock();
				continue;
			}

			struct camd35_server_data *cs378x = cfg.cs378x.server;
			while (cs378x) {
				if ( !IS_DISABLED(cs378x->flags)&&(cs378x->handle>0) ) {
					struct camd35_client_data *client = cs378x->cacheexclient;
					while (client) {
						if ( (client->handle>0) && (client->ipoll>=0) && (client->handle==pfd[client->ipoll].fd) ) {
							if ( pfd[client->ipoll].revents & (POLLIN|POLLPRI) ) {
								cs378x_recvmsg( client );
							}
						}
						client = client->next;
					}
				}
				cs378x = cs378x->next;
			}

			if ( pfd[0].revents & (POLLIN|POLLPRI) ) cs378x_cacheex_recv_pipe();
			newcamd_config_read_unlock();

		} else if (retval<0) usleep(99000);
	}
	return NULL;
}

///////////////////////////////////////////////////////////////////////////////
// CAMD35 SERVER: START/STOP
///////////////////////////////////////////////////////////////////////////////

int start_thread_cs378x()
{
	pthread_t tid;
#ifndef MONOTHREAD_ACCEPT
	create_thread(&tid, cs378x_accept_thread,NULL);
#endif

	create_thread(PACKED_MPTR(&cfg, pthread_t, cs378x.tid_recvmsg), cs378x_cacheex_recvmsg_thread,NULL);
	create_thread(PACKED_MPTR(&cfg, pthread_t, cs378x.tid_recvmsg), cs378x_recvmsg_thread,NULL);
	return 0;
}


