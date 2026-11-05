///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

void camd35_send_keepalive(struct server_data *srv)
{
	uint8_t buf[64];
#ifdef CACHEEX
	if (srv->cacheex_mode) {
		// Request Nodeid
		memset(buf, 0, 32);
		buf[0] = 0x3D;
		buf[1] = 12;
		memcpy( buf+20, cccam_nodeid, 8);
		camd35_sendto( srv->handle, host_ip_snapshot(srv->host), srv->port, &srv->encryptkey, srv->ucrc, buf, 20+12);
	}
	else
#endif
	{
		// keepalive
		uint8_t buf[64];
		memset(buf,0, 21);
		buf[0] = 0x37;
		buf[1] = 1;
		camd35_sendto( srv->handle, host_ip_snapshot(srv->host), srv->port, &srv->encryptkey, srv->ucrc, buf, 20+1);
	}
}

///////////////////////////////////////////////////////////////////////////////

int camd35_sendecm_srv(struct server_data *srv, ECM_DATA *ecm)
{
	srv->ecm.msgid++;
	if (srv->ecm.msgid>0xfff) srv->ecm.msgid = 1;

	unsigned char buf[1024];
	memset(buf, 0, 20);
	buf[0] = 0; // Command
	buf[1] = 0; // Length
	buf[8] = ecm->sid>>8;
	buf[9] = ecm->sid&0xff;
	buf[10] = ecm->caid>>8;
	buf[11] = ecm->caid&0xff;
	buf[12] = ecm->provid>>24;
	buf[13] = ecm->provid>>16;
	buf[14] = ecm->provid>>8;
	buf[15] = ecm->provid&0xff;
	buf[16] = srv->ecm.msgid>>8;
	buf[17] = srv->ecm.msgid;
	memcpy( buf+20, ecm->ecm, ecm->ecmlen);
	camd35_sendto( srv->handle, host_ip_snapshot(srv->host), srv->port, &srv->encryptkey, srv->ucrc, buf, 20+ecm->ecmlen);
	return 1;
}


///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

void camd35_srv_recvmsg(struct server_data *srv)
{
	uint8_t cw[16];
	struct cardserver_data *cs;
	ECM_DATA *ecm;

	struct sockaddr_in si_other;
	socklen_t slen = sizeof(si_other);
	unsigned char buf[1024];

	int received = recvfrom( srv->handle, buf, sizeof(buf), 0, (struct sockaddr*)&si_other, &slen);
	if ( (received<20)||(received>1020) ) return;

	uint32_t ucrc = (buf[0]<<24)|(buf[1]<<16)|(buf[2]<<8)|buf[3];
	//Check for client
	if (srv->ucrc!=ucrc) return;
	//
	aes_decrypt( &srv->decryptkey, buf+4, received-4);
#ifdef DEBUG_NETWORK
	if (flag_debugnet) {
		unsigned int recv_ip;
		unsigned short recv_port;
		memcpy( &recv_ip, &si_other.sin_addr, 4);
		recv_port = ntohs(si_other.sin_port);
		debugf(0," camd35: Recv data (length=%d) from address (%s:%d)\n", received, ip2string(recv_ip), recv_port );
		debughex(buf,received);
	}
#endif

	switch (buf[4]) {

		case CAMD_ECM_REPLY:
			srv->lastdcwtime = GetTickCount();
			if (!srv->busy) {
				debugf(getdbgflag(DBG_SERVER,0,srv->id)," [!] dcw error from camd35 server (%s:%d), unknown ecm request\n",srv->host->name,srv->port);
				break;
			}
			//
			if (buf[5]!=0x10) {
				debugf(getdbgflag(DBG_SERVER,0,srv->id)," [!] dcw error from camd35 server (%s:%d), wrong length!!!\n",srv->host->name,srv->port);
				break;
			}
			if (received < 40) {
				debugf(getdbgflag(DBG_SERVER,0,srv->id)," [!] short camd35 CW response (%d bytes)\n", received);
				break;
			}
			ecm_lock(); //### Keep request/profile pointers stable through validation.
			ecm = srv->ecm.request;
			if (!ecm) {
				debugf(getdbgflag(DBG_SERVER,0,srv->id)," [!] dcw error from camd35 server (%s:%d), ecm not found!!!\n",srv->host->name,srv->port);
				ecm_unlock();
				break;
			}
			if ((((uint16_t)buf[20] << 8) | buf[21]) != (uint16_t)srv->ecm.msgid) {
				debugf(getdbgflag(DBG_SERVER,0,srv->id)," [!] stale camd35 response, wrong message-id\n");
				ecm_unlock();
				break;
			}
			if (!ecm_request_lifetime_matches(ecm, srv->ecm.request, srv->ecm.lifetime_id) ||
			    ecm->hash != srv->ecm.hash) {
				debugf(getdbgflag(DBG_SERVER,0,srv->id)," [!] stale camd35 response, request lifetime changed\n");
				ecm_unlock();
				break;
			}
			if (!acceptDCW_profile_ecm(buf+24, ecm->cs, ecm->caid, ecm->provid, ecm->sid)) {
				srv->ecmerrdcw++;
				debugf(getdbgflag(DBG_SERVER,0,srv->id)," [!] dcw error from camd35 server (%s:%d), bad dcw!!!\n",srv->host->name,srv->port);
				ecm_unlock();
				break;
			}
			srv->busy = 0;
			pipe_cmd( prg.pipe.ecm[1], PIPE_SRV_AVAILABLE );
			srv->ecmok++;
			srv->lastecmoktime = GetTickCount()-srv->lastecmtime;
			srv->ecmoktime += srv->lastecmoktime;
			ecm_setsrvflagdcw( ecm, srv->id, ECM_SRV_REPLY_GOOD, buf+24 );
			debugf(getdbgflagpro(DBG_SERVER,0,srv->id,(ecm->cs ? ecm->cs->id : 0))," <= cw from camd35 server (%s:%d) ch %04x:%06x:%04x (%dms)\n", srv->host->name,srv->port, ecm->caid,ecm->provid,ecm->sid, GetTickCount()-srv->lastecmtime);

			if (ecm->dcwstatus!=STAT_DCW_SUCCESS) {
				static char msg[] = "Good dcw from camd35 server";
				ecm->statusmsg = msg;
				// Store ECM Answer
				ecm_setdcw( ecm, buf+24, DCW_SOURCE_SERVER, srv->id );
			}
			else {	//TODO: check same dcw between cards
				srv->ecmerrdcw ++;
				if ( memcmp( ecm->cw, buf+24, 16) ) debugf(getdbgflagpro(DBG_SERVER,0,srv->id,(ecm->cs ? ecm->cs->id : 0))," !!! different dcw from camd35 server (%s:%d)\n",srv->host->name,srv->port);
			}
#ifdef SID_FILTER
			// ADD IN SID LIST
			cs= ecm->cs;
			if (cs) {
				cardsids_update( srv->busycard, ecm->provid, ecm->sid, 1);
				srv_cstatadd( srv, cs->id, 1 , srv->lastecmoktime);
			}
#endif
			ecm_unlock(); //###
			break;

		case 0x44:
			srv->lastdcwtime = GetTickCount();
			if (!srv->busy) {
				debugf(getdbgflag(DBG_SERVER,0,srv->id)," [!] dcw error from camd35 server (%s:%d), unknown ecm request\n",srv->host->name,srv->port);
				break;
			}
			if (received < 24) {
				debugf(getdbgflag(DBG_SERVER,0,srv->id)," [!] short camd35 negative response (%d bytes)\n", received);
				break;
			}
			ecm_lock(); //###
			ecm = srv->ecm.request;
			if (!ecm || ((((uint16_t)buf[20] << 8) | buf[21]) != (uint16_t)srv->ecm.msgid) ||
			    !ecm_request_lifetime_matches(ecm, srv->ecm.request, srv->ecm.lifetime_id) ||
			    ecm->hash != srv->ecm.hash) {
				debugf(getdbgflag(DBG_SERVER,0,srv->id)," [!] stale camd35 negative response ignored\n");
				ecm_unlock();
				break;
			}
			srv->busy = 0;
			pipe_cmd( prg.pipe.ecm[1], PIPE_SRV_AVAILABLE );
			cs = ecm->cs;
			ecm_setsrvflag(ecm, srv->id, ECM_SRV_REPLY_FAIL);
			debugf(getdbgflagpro(DBG_SERVER,0,srv->id,(ecm->cs ? ecm->cs->id : 0))," <| decode failed from camd35 server (%s:%d) ch %04x:%06x:%04x (%dms)\n", srv->host->name,srv->port, ecm->caid,ecm->provid,ecm->sid, GetTickCount()-srv->lastecmtime);
#ifdef SID_FILTER
			if (cs) {
				cardsids_update( srv->busycard, ecm->provid, ecm->sid, -1);
				srv_cstatadd( srv, cs->id, 0 , 0);
			}
#endif
			ecm_unlock(); //###
			wakeup_sendecm(); // Wakeup ecm waiting for available servers
			break;

		// Keepalive
		case CAMD_KEEPALIVE:
			srv->keepalive.status = 0;
			//debugf(0," server(camd35): Keepalive from (%s:%d)\n", srv->host->name, srv->port);
			if (srv->connection.status<=0) {
				debugf(0," connect to camd35 server (%s:%d)\n", srv->host->name, srv->port);
				srv->connection.status = 1;
				srv->connection.time = GetTickCount();
			}
			break;

		// Request Nodeid
		case CAMD_CEX_IDREPLY:
			srv->keepalive.status = 0;
			//debugf(0," server(camd35): Got Nodeid from (%s:%d)\n", srv->host->name, srv->port);
			memcpy( srv->nodeid, buf+24, 8);
			if (srv->connection.status<=0) {
				char str[8*3+1];
				array2hex( srv->nodeid, str, 8);
				debugf(0," connected to camd35 server (%s:%d), Nodeid = %s\n", srv->host->name, srv->port, str);
				srv->connection.status = 1;
				srv->connection.time = GetTickCount();
			}
			break;

#ifdef CACHEEX
		// push out
		case CAMD_CEX_PUSH:
			memcpy( cw, buf+44, 16);
			if (dcw_hard_reject(cw)) break; /* TASK 3.15: profile gate is cache_setdcw */
			//srv->cacheex.totalrep++;
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
				srv->cacheex.badcw++;
				pthread_mutex_unlock(&prg.lockcacheex);
				break;
			}
			if ((buf[23]&0xFE)==0x80) cacheex.tag = buf[23]; else cacheex.tag = 0;
			memcpy( cacheex.ecmd5, buf+24, 16);
			//if ( !checkECMD5(cacheex.ecmd5) ) cli->cacheex.totalcsp++;
			cacheex.hash = (buf[43]<<24) | (buf[42]<<16) | (buf[41]<<8) | buf[40];
			if (!cacheex_check(&cacheex)) break;
			//debugf( getdbgflag(DBG_CACHEEX, 0, 0)," CACHEEX PUSH from client(%d) %04x:%06x:%04x (%08x)\n",cli->id,cacheex.caid,cacheex.provid,cacheex.sid,cacheex.hash);
			pthread_mutex_lock(&prg.lockcacheex);
			srv->cacheex.got[0]++;
			int uphop = buf[60];
			if (uphop<10) srv->cacheex.got[uphop]++;
			pthread_mutex_unlock(&prg.lockcacheex);
			//
			pthread_mutex_lock( &prg.lockcache );
			int res = cache_setdcw( &cacheex, cw, NO_CYCLE, PEER_CACHEEX_SERVER | srv->id );
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
				cwn_note( DCW_SOURCE_CACHE, PEER_CACHEEX_SERVER | srv->id, cw, cacheex.caid, cacheex.provid, cacheex.provider_known, cacheex.sid, GetTickCount() );
				/*
				 * TASK 1.4 — a definitive proof scores against the source that
				 * supplied the key, at GR4 granularity. The srcid keeps its PEER_*
				 * origin flags, because a CSP peer and a cache-exchange client with
				 * the same id are different sources and must not share a score.
				 */
				trust_record_lk_scoped( &trust_tab, TRUST_EV_PROOF, DCW_SOURCE_CACHE, PEER_CACHEEX_SERVER | srv->id,
					cacheex.caid, cacheex.provid, cacheex.provider_known,
					cacheex.sid, GetTickCount() );
				/* TASK 2.8: the same proof, summed one level coarser. */
				trustagg_note_lk( TRUST_EV_PROOF, DCW_SOURCE_CACHE, PEER_CACHEEX_SERVER | srv->id,
				                  cacheex.caid, GetTickCount() );
				debugf(DBG_ERROR," !!! CW REUSE PROOF: same cw for a different ecm on a different service, ch %04x:%06x:%04x from %s\n",
					cacheex.caid, cacheex.provid, cacheex.sid, srv->user);
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
				if ( cache_purge_mark_scoped( &purge_tab, DCW_SOURCE_CACHE, PEER_CACHEEX_SERVER | srv->id,
						cacheex.caid, cacheex.sid, cacheex.provid, cacheex.provider_known,
						GetTickCount() ) ) {
					int npurged = cache_purge_sweep_scoped( &purge_tab, DCW_SOURCE_CACHE, PEER_CACHEEX_SERVER | srv->id,
						cacheex.caid, cacheex.sid, cacheex.provid, cacheex.provider_known, DCW_ERROR );
					/* npurged can be 0: the entry may already be gone, or it may have
					 * been supplied first by a different peer (cwdata->peerid records
					 * the FIRST reporter only). Logged either way -- GR8. */
					debugf(DBG_ERROR," !!! CACHE PURGE: ch %04x:%06x:%04x marked %d cached cw(s) from %s\n",
						cacheex.caid, cacheex.provid, cacheex.sid, npurged, srv->user);
				}
			}
#endif
			pthread_mutex_unlock( &prg.lockcache );
			cache_peerrep_save_after_unlock();
			if (res&DCW_ERROR) {
				if ( !(res&DCW_SKIP)) {
					pthread_mutex_lock(&prg.lockcacheex);
					srv->cacheex.badcw++;
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
			//debugf(0," camd35: push out from server %04x:%06x:%04x|%02x:%08x\n", cacheex.caid,cacheex.provid,cacheex.sid, cacheex.tag,cacheex.hash);
			break;
#endif

	}

}

// CACHEEX MODE 2
void *camd35_srv_recvmsg_thread(struct server_data *srv)
{
	srv->pid = syscall(SYS_gettid);

	while (!prg.restart) {
		struct pollfd pfd;
		int fd;
		pthread_mutex_lock(&prg.locksrv);
		fd = srv->handle;
		pthread_mutex_unlock(&prg.locksrv);
		if (fd<=0) {
			srv->pid = 0;
			return NULL;
		}
		pfd.fd = fd;
		pfd.events = POLLIN | POLLPRI;
		int retval = poll(&pfd, 1, 3009);
		if (retval==0) continue; // timeout
		if (retval<0) {
			pthread_mutex_lock(&prg.locksrv);
			if (srv->handle == fd) disconnect_srv(srv);
			pthread_mutex_unlock(&prg.locksrv);
			srv->pid = 0;
			return NULL;
		}
		pthread_mutex_lock(&prg.locksrv);
		if (srv->handle == fd) camd35_srv_recvmsg(srv);
		pthread_mutex_unlock(&prg.locksrv);
	}
	return NULL;
}


///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

int camd35_connect_srv(struct server_data *srv, int fd)
{
	uint8_t buf[64];
#ifdef CACHEEX
	if (srv->cacheex_mode) {
		// Request Nodeid
		memset(buf, 0, 32);
		buf[0] = CAMD_CEX_IDREQUEST;
		buf[1] = 12;
		memcpy( buf+20, cccam_nodeid, 8);
		camd35_sendto( fd, host_ip_snapshot(srv->host), srv->port, &srv->encryptkey, srv->ucrc, buf, 20+12);
		struct pollfd pfd;
		pfd.fd = fd;
		pfd.events = POLLIN | POLLPRI;
		int ret = poll(&pfd, 1, 3000);
		while (ret>0) {
			struct sockaddr_in si_other;
			socklen_t slen = sizeof(si_other);
			unsigned char buf[1024];
			int received = recvfrom( fd, buf, sizeof(buf), 0, (struct sockaddr*)&si_other, &slen);
			if ( (received<20)||(received>1020) ) break;
			uint32_t ucrc = (buf[0]<<24)|(buf[1]<<16)|(buf[2]<<8)|buf[3];
			if (srv->ucrc!=ucrc) break;
			aes_decrypt( &srv->decryptkey, buf+4, received-4);
			if (buf[4]==CAMD_CEX_IDREPLY) { // good
				memcpy( srv->nodeid, buf+24, 8);
				array2hex( srv->nodeid, buf, 8);
				debugf(0," Connected to camd35 server (%s:%d), Nodeid = %s\n", srv->host->name, srv->port, buf);
				pthread_mutex_lock(&prg.locksrv);
				srv->connection.status = 1;
				srv->connection.time = GetTickCount();
				srv->keepalive.status = 0;
				srv->keepalive.time = GetTickCount();
				srv->handle = fd;
				pthread_mutex_unlock(&prg.locksrv);
				if (!server_connection_is_enabled(srv, fd)) return 0;
				if (srv->cacheex_mode==2) {
					if (!create_thread(PACKED_MPTR(srv, pthread_t, tid), (threadfn)camd35_srv_recvmsg_thread, srv)) {
						pthread_mutex_lock(&prg.locksrv);
						if (srv->handle == fd) disconnect_srv(srv);
						pthread_mutex_unlock(&prg.locksrv);
					}
				}
				else pipe_wakeup( prg.pipe.cacheex[1] );
				return 0;
			}
			break;
		}
		close(fd);			
	}
	else
#endif

	{
		// keepalive
		uint8_t buf[64];
		memset(buf,0, 21);
		buf[0] = CAMD_KEEPALIVE;
		buf[1] = 1;
		camd35_sendto( fd, host_ip_snapshot(srv->host), srv->port, &srv->encryptkey, srv->ucrc, buf, 20+1);
		struct pollfd pfd;
		pfd.fd = fd;
		pfd.events = POLLIN | POLLPRI;
		int ret = poll(&pfd, 1, 3000);
		while (ret>0) {
			struct sockaddr_in si_other;
			socklen_t slen = sizeof(si_other);
			unsigned char buf[1024];
			int received = recvfrom( fd, buf, sizeof(buf), 0, (struct sockaddr*)&si_other, &slen);
			if ( (received<20)||(received>1020) ) break;
			uint32_t ucrc = (buf[0]<<24)|(buf[1]<<16)|(buf[2]<<8)|buf[3];
			if (srv->ucrc!=ucrc) break;
			aes_decrypt( &srv->decryptkey, buf+4, received-4);
			if (buf[4]==CAMD_KEEPALIVE) { // good
				//SetUP Cards
				int count = 0;
				while (srv->sharelimits[count].caid!=0xFFFF) {
					struct cs_card_data *pcard = malloc( sizeof(struct cs_card_data) );
					if (!pcard) {
						debugf(DBG_ERROR, " !!! unable to allocate camd35 server card record for server %u\n", (unsigned)srv->id);
						break;
					}
					memset(pcard, 0, sizeof(struct cs_card_data) );
					pcard->caid = srv->sharelimits[count].caid;
					pcard->nbprov = 1;
					pcard->prov[0] = srv->sharelimits[count].provid;
					pcard->uphops = srv->sharelimits[count].uphops;
					pthread_mutex_lock(&prg.locksrv);
					pcard->next = srv->card;
					srv->card = pcard;
					pthread_mutex_unlock(&prg.locksrv);
					count++;
				}
				//
				debugf(0," Connected to camd35 server (%s:%d)\n", srv->host->name, srv->port);
				pthread_mutex_lock(&prg.locksrv);
				srv->connection.status = 1;
				srv->connection.time = GetTickCount();
				srv->keepalive.status = 0;
				srv->keepalive.time = GetTickCount();
				srv->handle = fd;
				pthread_mutex_unlock(&prg.locksrv);
				server_publish_connection_ready(srv, fd);
				return 0;
			}
			break;
		}
		close(fd);			
	}
	return 1;
}



