///////////////////////////////////////////////////////////////////////////////
// TCP
///////////////////////////////////////////////////////////////////////////////
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


void cs378x_send_keepalive(struct server_data *srv)
{
	uint8_t buf[64];

#ifdef CACHEEX
	if (srv->cacheex_mode) {
		// Request Nodeid
		memset(buf, 0, 32);
		buf[0] = CAMD_CEX_IDREQUEST;
		buf[1] = 12;
		memcpy( buf+20, cccam_nodeid, 8);
		cs378x_send( srv->handle, &srv->encryptkey, srv->ucrc, buf, 20+12);
	}
	else
#endif
	{
		// keepalive
		uint8_t buf[64];
		memset(buf,0, 21);
		buf[0] = CAMD_KEEPALIVE;
		buf[1] = 1;
		buf[2] = 0;
		cs378x_send( srv->handle, &srv->encryptkey, srv->ucrc, buf, 20+1);
	}
}

///////////////////////////////////////////////////////////////////////////////
// SEND ECM
///////////////////////////////////////////////////////////////////////////////

int cs378x_sendecm_srv(struct server_data *srv, ECM_DATA *ecm)
{
	srv->lastecmtime = GetTickCount();
	srv->ecmnb++;
	srv->busy=1;
	srv->ecm.msgid++;
	if (srv->ecm.msgid>0xfff) srv->ecm.msgid = 1;
	srv->ecm.request = ecm;

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
	cs378x_send( srv->handle, &srv->encryptkey, srv->ucrc, buf, 20+ecm->ecmlen);
	return 1;
}

///////////////////////////////////////////////////////////////////////////////
// RECV MSG
///////////////////////////////////////////////////////////////////////////////



///////////////////////////////////////////////////////////////////////////////
// RECV MSG
///////////////////////////////////////////////////////////////////////////////

// CACHEEX MODE 2
void *cs378x_srv_recvmsg(struct server_data *srv)
{
	uint8_t cw[16];
	struct cardserver_data *cs;
/*
		// TEST KEEPALIVE
		uint32_t ticks = GetTickCount();
		if ( !srv->keepalive.status && ((srv->keepalive.time+30000)<ticks) ) {
			cs378x_send_keepalive(srv);
			srv->keepalive.status = 1; // Sent and waiting for reply
			srv->keepalive.time = ticks;
		}
		else if ( (srv->keepalive.status==1) && ((srv->keepalive.time+5000)<ticks) ) {
			cs378x_send_keepalive(srv);
			srv->keepalive.status = 2;
		}
		else if ( (srv->keepalive.status>1) && ((srv->keepalive.time+10000)<ticks) ) {
			debugf(getdbgflag(DBG_SERVER,0,srv->id),, " cs378x server disconnected, keepalive timeout\n");
			close(srv->handle);
			srv->handle = -1;
			srv->pid = 0;
			srv->connected = 0;
			srv->keepalive.status = 0;
			srv->keepalive.time = 0;
			return NULL;
		}
*/

		unsigned char buf[2048];
		int received = recv_nonb( srv->handle, buf, 32+4, 1000);
		if (received<=0) {
			disconnect_srv(srv);
			//debugf(getdbgflag(DBG_SERVER,0,srv->id), " cs378x server disconnected, receive timeout\n");
			return NULL;
		}

		uint32_t ucrc = (buf[0]<<24)|(buf[1]<<16)|(buf[2]<<8)|buf[3];
		if (srv->ucrc!=ucrc) {
			disconnect_srv(srv);
			//debugf(getdbgflag(DBG_SERVER,0,srv->id), " cs378x server disconnected, wrong ucrc\n");
			return NULL;
		}
		aes_decrypt( &srv->decryptkey, buf+4, received-4);
		//Fix for ECM request size > 255 (use ecm length field)
		int datalen = buf[5];
		if (buf[4] == 0)
			{ datalen = (((buf[25] & 0x0f) << 8) | buf[26]) + 3; }
		else if ( (buf[4]&0xFC)==0x3C ) datalen = buf[5] | (buf[6] << 8); // cacheex
		int newlen = 4+camd35_padding(20+datalen);
		if (received<newlen) {
			int n = recv_nonb( srv->handle, buf+received, newlen-received, 500);
			if ( n != (newlen-received) ) {
				disconnect_srv(srv);
				//debugf(getdbgflag(DBG_SERVER,0,srv->id), " cs378x server disconnected, receive timeout\n");
				return NULL;
			}
			aes_decrypt( &srv->decryptkey, buf+received, n);
		}

		//debugf(getdbgflag(DBG_SERVER,0,srv->id)," msg from cs378x server (%s:%d)\n", srv->host->name, srv->port); debughex(buf, newlen);

		switch (buf[4]) {

			case CAMD_ECM_REPLY:
				srv->lastdcwtime = GetTickCount();
				if (!srv->busy) {
					debugf(getdbgflag(DBG_SERVER,0,srv->id)," [!] dcw error from cs378x server (%s:%d), unknown ecm request\n",srv->host->name,srv->port);
					break;
				}
				//
				if (buf[5]!=0x10) {
					debugf(getdbgflag(DBG_SERVER,0,srv->id)," [!] dcw error from cs378x server (%s:%d), wrong length!!!\n",srv->host->name,srv->port);
					break;
				}
				// Check for DCW
				if (!acceptDCW_profile(buf+24, srv->ecm.request ? srv->ecm.request->cs : NULL)) {
					srv->ecmerrdcw++;
					debugf(getdbgflag(DBG_SERVER,0,srv->id)," [!] dcw error from cs378x server (%s:%d), bad dcw!!!\n",srv->host->name,srv->port);
					break;
				}
				// Checl Stored ECM
				ECM_DATA *ecm = srv->ecm.request;
				if (!ecm) {
					debugf(getdbgflag(DBG_SERVER,0,srv->id)," [!] dcw error from cs378x server (%s:%d), ecm not found!!!\n",srv->host->name,srv->port);
					break;
				}
				//
				srv->busy = 0;
				pipe_cmd( prg.pipe.ecm[1], PIPE_SRV_AVAILABLE );
				pthread_mutex_lock(&prg.lockecm); //###

				// check for ECM validity
				if (ecm->hash!=srv->ecm.hash) {
					debugf(getdbgflag(DBG_SERVER,0,srv->id)," [!] dcw error from cs378x server (%s:%d), ecm deleted!!!\n",srv->host->name,srv->port);
					pthread_mutex_unlock(&prg.lockecm);
					break;
				}

				srv->ecmok++;
				srv->lastecmoktime = GetTickCount()-srv->lastecmtime;
				srv->ecmoktime += srv->lastecmoktime;
				ecm_setsrvflagdcw( ecm, srv->id, ECM_SRV_REPLY_GOOD, buf+24 );
				debugf(getdbgflagpro(DBG_SERVER,0,srv->id,ecm->cs->id)," <= cw from cs378x server (%s:%d) ch %04x:%06x:%04x (%dms)\n", srv->host->name,srv->port, ecm->caid,ecm->provid,ecm->sid, GetTickCount()-srv->lastecmtime);

				if (ecm->dcwstatus!=STAT_DCW_SUCCESS) {
					static char msg[] = "Good dcw from camd35 server";
					ecm->statusmsg = msg;
					// Store ECM Answer
					ecm_setdcw( ecm, buf+24, DCW_SOURCE_SERVER, srv->id );
				}
				else {	//TODO: check same dcw between cards
					srv->ecmerrdcw ++;
					if ( memcmp( ecm->cw, buf+24, 16) ) debugf(getdbgflagpro(DBG_SERVER,0,srv->id,ecm->cs->id)," !!! different dcw from cs378x server (%s:%d)\n",srv->host->name,srv->port);
				}
#ifdef SID_FILTER
				// ADD IN SID LIST
				cs= ecm->cs;
				if (cs) {
					cardsids_update( srv->busycard, ecm->provid, ecm->sid, 1);
					srv_cstatadd( srv, cs->id, 1 , srv->lastecmoktime);
				}
#endif
				pthread_mutex_unlock(&prg.lockecm); //###
				break;


			case 0x44:
				srv->lastdcwtime = GetTickCount();
				if (!srv->busy) {
					debugf(getdbgflag(DBG_SERVER,0,srv->id)," [!] dcw error from cs378x server (%s:%d), unknown ecm request\n",srv->host->name,srv->port);
					break;
				}
				// Checl Stored ECM
				ecm = srv->ecm.request;
				if (!ecm) {
					debugf(getdbgflag(DBG_SERVER,0,srv->id)," [!] dcw error from cs378x server (%s:%d), ecm not found!!!\n",srv->host->name,srv->port);
					break;
				}
				//
				srv->busy = 0;
				pipe_cmd( prg.pipe.ecm[1], PIPE_SRV_AVAILABLE );
				pthread_mutex_lock(&prg.lockecm); //###

				// check for ECM validity
				if (ecm->hash!=srv->ecm.hash) {
					debugf(getdbgflag(DBG_SERVER,0,srv->id)," [!] dcw error from cs378x server (%s:%d), ecm deleted!!!\n",srv->host->name,srv->port);
					pthread_mutex_unlock(&prg.lockecm);
					break;
				}

				cs= ecm->cs;
				ecm_setsrvflag(ecm, srv->id, ECM_SRV_REPLY_FAIL);
				debugf(getdbgflagpro(DBG_SERVER,0,srv->id,ecm->cs->id)," <| decode failed from cs378x server (%s:%d) ch %04x:%06x:%04x (%dms)\n", srv->host->name,srv->port, ecm->caid,ecm->provid,ecm->sid, GetTickCount()-srv->lastecmtime);
#ifdef SID_FILTER
				// ADD IN SID LIST
				if (cs) {
					cardsids_update( srv->busycard, ecm->provid, ecm->sid, -1);
					srv_cstatadd( srv, cs->id, 0 , 0);
				}
#endif
				pthread_mutex_unlock(&prg.lockecm); //###
				wakeup_sendecm(); // Wakeup ecm waiting for availabe servers
				break;

			// Keepalive
			case CAMD_KEEPALIVE:
				srv->keepalive.status = 0;
				//debugf(getdbgflag(DBG_SERVER,0,srv->id)," server(cs378x): Keepalive from (%s:%d)\n", srv->host->name, srv->port);
				if (srv->connection.status<=0) {
					debugf(getdbgflag(DBG_SERVER,0,srv->id)," Connected to cs378x server (%s:%d)\n", srv->host->name, srv->port);
					srv->connection.status = 1;
					srv->connection.time = GetTickCount();
				}
				break;

#ifdef CACHEEX
			// Request Nodeid
			case CAMD_CEX_IDREPLY:
				srv->keepalive.status = 0;
				//debugf(getdbgflag(DBG_SERVER,0,srv->id)," server(cs378x): Got Nodeid from (%s:%d)\n", srv->host->name, srv->port);
				memcpy( srv->nodeid, buf+24, 8);
				if (srv->connection.status<=0) {
					char str[8*3+1];
					array2hex( srv->nodeid, str, 8);
					debugf(getdbgflag(DBG_SERVER,0,srv->id)," Connected to cs378x server (%s:%d), Nodeid = %s\n", srv->host->name, srv->port, str);
					srv->connection.status = 1;
					srv->connection.time = GetTickCount();
				}
				break;

			// push out
			case CAMD_CEX_PUSH:
				if (newlen<60) break;
				memcpy( cw, buf+44, 16);
				if (dcw_hard_reject(cw)) break; /* TASK 3.15: profile gate is cache_setdcw */
				//srv->cacheex.totalrep++;
				struct cache_data cacheex;
				cacheex.sid = (buf[12]<<8)|buf[13];
				cacheex.caid = (buf[14]<<8)|buf[15];
				cacheex.provid = (buf[16]<<24)|(buf[17]<<16)|(buf[18]<<8)|buf[19];
				// Look for cardserver
				cs = getcsbycaprovid(cacheex.caid, cacheex.provid);
				if ( !cs || !cs->option.fallowcacheex ) {
					srv->cacheex.badcw++;
					break;
				}
				if ((buf[23]&0xFE)==0x80) cacheex.tag = buf[23]; else cacheex.tag = 0;
				memcpy( cacheex.ecmd5, buf+24, 16);
				//if ( !checkECMD5(cacheex.ecmd5) ) cli->cacheex.totalcsp++;
				cacheex.hash = (buf[43]<<24) | (buf[42]<<16) | (buf[41]<<8) | buf[40];
				if (!cacheex_check(&cacheex)) break;
				//debugf(getdbgflag(DBG_SERVER,0,srv->id), " CACHEEX PUSH from client(%d) %04x:%06x:%04x (%08x)\n",cli->id,cacheex.caid,cacheex.provid,cacheex.sid,cacheex.hash);
				srv->cacheex.got[0]++;
				int uphop = buf[60];
				if (uphop<10) srv->cacheex.got[uphop]++;
				//
				pthread_mutex_lock( &prg.lockcache );
				int res = cache_setdcw( &cacheex, cw, NO_CYCLE, PEER_CACHEEX_SERVER | srv->id );
#ifdef CACHEEX
			/*
			 * TASK 1.3 — offer this CW to the reuse detector.
			 *
			 * Safe here and only here: on the cache-exchange paths `cacheex.ecmd5`
			 * is decoded from the message, so the 128-bit ECM identity is real.
			 * The CSP TYPE_REPLY path (clustredcache.c) carries only a 32-bit
			 * hash; its cache entry may vote only when CACHE_FLAG_ECMD5_VALID
			 * records a digest supplied by a local ECM-pipe message. Fresh or
			 * peer-only entries are not eligible to manufacture a proof.
			 *
			 * R13 (D67) note: the OBSERVE-ONLY wording that stood here was stale.
			 * The proof path below is the TASK 1.5 action -- it scores
			 * (TRUST_EV_PROOF) and disables (cache_purge_mark + sweep) on the proof
			 * itself, unconditionally. What R13 changed is only WHO may testify:
			 * a push the cache refused no longer files the sighting.
			 */
			/* TASK R13 (D67): only a key that ENTERED the cache may testify. The
			 * three pre-storage drops (LOCAL_ONLY, the profile gate, BLOCK_FAKE_CW)
			 * return -1: a refused push must not become the first sighting of a
			 * reuse proof -- otherwise a peer could pre-poison a key it knows by
			 * pushing it for an unwanted service, having it refused, and the honest
			 * delivery of the same bytes would be convicted (and purged) globally. */
			if ( res >= 0 && cwreuse_offer( &cwreuse_tab, cw, cacheex.ecmd5, 1,
					cacheex.caid, cacheex.sid, cacheex.provid, GetTickCount() ) == CWREUSE_PROOF ) {
				cwreuse_proofs++;
				/* TASK 2.6: the convicted key itself goes into negative memory,
				 * so no other source and no fresh entry can ever serve it. */
				cwn_note( DCW_SOURCE_CACHE, PEER_CACHEEX_SERVER | srv->id, cw, cacheex.caid, cacheex.provid, cacheex.sid, GetTickCount() );
				/*
				 * TASK 1.4 — a definitive proof scores against the source that
				 * supplied the key, at GR4 granularity. The srcid keeps its PEER_*
				 * origin flags, because a CSP peer and a cache-exchange client with
				 * the same id are different sources and must not share a score.
				 */
				trust_record_lk( &trust_tab, TRUST_EV_PROOF, DCW_SOURCE_CACHE, PEER_CACHEEX_SERVER | srv->id,
					cacheex.caid, cacheex.provid, cacheex.sid, GetTickCount() );
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
				if ( cache_purge_mark( &purge_tab, DCW_SOURCE_CACHE, PEER_CACHEEX_SERVER | srv->id,
						cacheex.caid, cacheex.sid, cacheex.provid, GetTickCount() ) ) {
					int npurged = cache_purge_sweep( &purge_tab, DCW_SOURCE_CACHE, PEER_CACHEEX_SERVER | srv->id,
						cacheex.caid, cacheex.sid, cacheex.provid, DCW_ERROR );
					/* npurged can be 0: the entry may already be gone, or it may have
					 * been supplied first by a different peer (cwdata->peerid records
					 * the FIRST reporter only). Logged either way -- GR8. */
					debugf(DBG_ERROR," !!! CACHE PURGE: ch %04x:%06x:%04x marked %d cached cw(s) from %s\n",
						cacheex.caid, cacheex.provid, cacheex.sid, npurged, srv->user);
				}
			}
#endif
				pthread_mutex_unlock( &prg.lockcache );
				if (res&DCW_ERROR) {
					if ( !(res&DCW_SKIP) ) srv->cacheex.badcw++;
				}
				else if (res&DCW_CYCLE) {
					if ( cs->option.cacheex.maxhop>uphop ) {
						uint8_t nodeid[8];
						memcpy( nodeid, buf+61, 8);
						pipe_send_cacheex_push_cache(&cacheex, cw, nodeid); //cacheex_push(&cacheex, cw, nodeid);
					}
				}
				//debugf(getdbgflag(DBG_SERVER,0,srv->id), " cs378x: push out from server %04x:%06x:%04x|%02x:%08x\n", cacheex.caid,cacheex.provid,cacheex.sid, cacheex.tag,cacheex.hash);
				break;
#endif
		}

	srv->keepalive.time = GetTickCount();
	return NULL;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
// CACHEEX MODE 2
void *cs378x_srv_recvmsg_thread(struct server_data *srv)
{
	srv->pid = syscall(SYS_gettid);

	while (!prg.restart) {
		if (srv->handle<=0) {
			srv->pid = 0;
			disconnect_srv(srv);
			return NULL;
		}
		//
		struct pollfd pfd;
		pfd.fd = srv->handle;
		pfd.events = POLLIN | POLLPRI;
		int retval = poll(&pfd, 1, 3009);
		if (retval==0) continue;
		if (retval<0) {
			disconnect_srv(srv);
			return NULL;
		}
		//
		cs378x_srv_recvmsg( srv );
	}
	return NULL;
}


///////////////////////////////////////////////////////////////////////////////
//
///////////////////////////////////////////////////////////////////////////////

int cs378x_connect_srv(struct server_data *srv, int fd)
{

	srv->handle = fd;
	SetSocketTimeout(fd, 5000);
	cs378x_send_keepalive(srv);
	srv->handle = 0;
	// Poll
	struct pollfd pfd;
	pfd.fd = fd;
	pfd.events = POLLIN | POLLPRI;
	if ( poll( &pfd, 1, 3000) <=0 ) return 1;
	//
	unsigned char buf[1024];
	int n = cs378x_recv( fd, srv->ucrc, &srv->decryptkey, buf);
	if (n>0) {
#ifdef CACHEEX
		if ( buf[4] == CAMD_CEX_IDREPLY ) {
			memcpy( srv->nodeid, buf+24, 8);
			//debugf(getdbgflag(DBG_SERVER,0,srv->id)," server(cs378x): Got Nodeid from (%s:%d)\n", srv->host->name, srv->port);
			srv->connection.status = 1;
			srv->connection.time = GetTickCount();
			srv->keepalive.status = 0;
			srv->keepalive.time = GetTickCount();
			srv->handle = fd;
			//
			char str[8*3+1];
			array2hex( srv->nodeid, str, 8);
			debugf(getdbgflag(DBG_SERVER,0,srv->id)," Connected to cs378x server (%s:%d), Nodeid = %s\n", srv->host->name, srv->port, str);
			// Cache EX 2 -> thread
			if (srv->cacheex_mode==2) {
				if (!create_thread(PACKED_MPTR(srv, pthread_t, tid), (threadfn)cs378x_srv_recvmsg_thread, srv)) {
					disconnect_srv(srv);
					return 1;
				}
			}
			return 0;
		}
		else
#endif
		if ( buf[4] == CAMD_KEEPALIVE ) {
			//
			debugf(getdbgflag(DBG_SERVER,0,srv->id)," Connected to cs378x server (%s:%d)\n", srv->host->name, srv->port);
			srv->connection.status = 1;
			srv->connection.time = GetTickCount();
			srv->keepalive.status = 0;
			srv->keepalive.time = GetTickCount();
			srv->handle = fd;
			//SetUP Cards
			int count = 0;
			while (srv->sharelimits[count].caid!=0xFFFF) {
				struct cs_card_data *pcard = malloc( sizeof(struct cs_card_data) );
				memset(pcard, 0, sizeof(struct cs_card_data) );
				pcard->caid = srv->sharelimits[count].caid;
				pcard->nbprov = 1;
				pcard->prov[0] = srv->sharelimits[count].provid;
				pcard->uphops = srv->sharelimits[count].uphops;
				pcard->next = srv->card;
				srv->card = pcard;
				count++;
			}
#ifdef EPOLL_ECM
			pipe_pointer( prg.pipe.ecm[1], PIPE_SRV_CONNECTED, srv );
#else
			pipe_cmd( prg.pipe.ecm[1], PIPE_SRV_CONNECTED );
#endif
			return 0;
		}
	}
	return 1;
}



