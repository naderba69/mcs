///////////////////////////////////////////////////////////////////////////////
// SETDCW
///////////////////////////////////////////////////////////////////////////////

inline void peer_hitprofile( struct cachepeer_data *peer, int csid )
{
	int i;
	for(i=0; i<MAX_CSPORTS; i++) {
		if (!peer->csporthit[i].csid) {
			peer->csporthit[i].csid = csid;
			peer->csporthit[i].hits = 1;
			break;
		}
		else if (peer->csporthit[i].csid==csid) {
			peer->csporthit[i].hits++;
			break;
		}
	}
}

#ifdef CACHEEX
inline void cacheex_cccam_hitprofile( struct cc_client_data *cli, int csid )
{
	int i;
	for(i=0; i<MAX_CSPORTS; i++) {
		if (!cli->csporthit[i].csid) {
			cli->csporthit[i].csid = csid;
			cli->csporthit[i].hits = 1;
			break;
		}
		else if (cli->csporthit[i].csid==csid) {
			cli->csporthit[i].hits++;
			break;
		}
	}
}

#ifdef CAMD35_SRV
inline void cacheex_camd35_hitprofile( struct camd35_client_data *cli, int csid )
{
	int i;
	for(i=0; i<MAX_CSPORTS; i++) {
		if (!cli->csporthit[i].csid) {
			cli->csporthit[i].csid = csid;
			cli->csporthit[i].hits = 1;
			break;
		}
		else if (cli->csporthit[i].csid==csid) {
			cli->csporthit[i].hits++;
			break;
		}
	}
}
#endif

#ifdef CS378X_SRV
inline void cacheex_cs378x_hitprofile( struct camd35_client_data *cli, int csid )
{
	int i;
	for(i=0; i<MAX_CSPORTS; i++) {
		if (!cli->csporthit[i].csid) {
			cli->csporthit[i].csid = csid;
			cli->csporthit[i].hits = 1;
			break;
		}
		else if (cli->csporthit[i].csid==csid) {
			cli->csporthit[i].hits++;
			break;
		}
	}
}
#endif

inline void cacheex_server_hitprofile( struct server_data *srv, int csid )
{
	int i;
	for(i=0; i<MAX_CSPORTS; i++) {
		if (!srv->csporthit[i].csid) {
			srv->csporthit[i].csid = csid;
			srv->csporthit[i].hits = 1;
			break;
		}
		else if (srv->csporthit[i].csid==csid) {
			srv->csporthit[i].hits++;
			break;
		}
	}
}

#endif


inline int dcwcheck_nds( ECM_DATA *ecm, uint8_t dcw[16], int swap )
{
	char nullcw[8] = "\0\0\0\0\0\0\0\0";
	//Must be halfnulled dcw
	//if ( memcmp(dcw,nullcw,3) && memcmp(dcw+8,nullcw,3) ) return 0;
	// get cwcycle
	int cwcycle = 0;
	if ( dcwcmp8(dcw,nullcw) ) cwcycle = 1;
	else if ( dcwcmp8(dcw+8,nullcw) ) cwcycle = 0;
	//
	//if (ecm->cw1cycle==0) return 1;
	if (0x81==ecm->ecm[0]) {
		if (cwcycle==1) return 1;
#ifdef DCWSWAP
		else if (swap) {
			char tmp[8];
			memcpy( tmp, dcw, 8);
			memcpy( dcw, dcw+8, 8);
			memcpy( dcw+8, tmp, 8);
			return 1;
		}
#endif
		else return 0;
	}
	else {
		if (cwcycle==0) return 1;
#ifdef DCWSWAP
		else if (swap) {
			char tmp[8];
			memcpy( tmp, dcw, 8);
			memcpy( dcw, dcw+8, 8);
			memcpy( dcw+8, tmp, 8);
			return 1;
		}
#endif
		else return 0;
	}
	
}


#ifndef THREAD_DCW

void ecm_setdcw( ECM_DATA *ecm, uint8_t dcw[16], int srctype, int srcid )
{
	char nullcw[16] = "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0";
	if ( dcwcmp16(dcw,nullcw) ) return;

	struct cardserver_data *cs = ecm->cs;
	if (!cs) return;

	if ( dcwcmp8(dcw,nullcw) && dcwcmp8(dcw+8,nullcw) ) return;

	/*
	 * TASK 1.8 — structural soft signal, evaluated before acceptDCW() so that
	 * a key which is about to be rejected is attributed as well. A rejected key
	 * otherwise leaves no trace anywhere: acceptDCW() returns a bare 0 and the
	 * client only ever sees a timeout.
	 *
	 * This does not change delivery. The scan's only output is evidence, and
	 * acceptDCW() below still makes the same decision it always made.
	 *
	 * The two filter gates are passed in rather than read inside the scan so
	 * that a filter the operator has disabled contributes nothing: if they
	 * turned DCWFILTER CHECKSUM off, their bouquet legitimately fails it and
	 * counting that would penalise a good source on every key.
	 */
	dcwstruct_note(&trust_tab,
		dcwstruct_scan(dcw, dcw_filter_checksum, dcw_filter_repeat),
		dcw, srctype, srcid, ecm->caid, ecm->provid, ecm->sid, GetTickCount());

	/* TASK 2.2 -- entropy and collision forensics (unreachable half; see the active copy). */
	cwentropy_note(&trust_tab,
#ifdef CACHEEX
		ecm->ecmd5,
#else
		0,
#endif
		dcw, srctype, srcid, ecm->caid, ecm->provid, ecm->sid, GetTickCount());

	/*
	 * TASK 2.4 -- plausibility: how much of the key's shape survives.
	 *
	 * The third of the three evidence-only hooks, in the same place and for the
	 * same reason as the two above: this runs before acceptDCW() decides
	 * anything, so a key that is about to be discarded is measured too. It
	 * reads no filter gate on purpose -- "exactly one of four group sums is
	 * wrong" means the same thing whether or not the operator left DCWFILTER
	 * CHECKSUM on, which is what makes this a statement about the key rather
	 * than about the configuration.
	 *
	 * Nothing here can reject, delay or alter a delivery: the return value goes
	 * to a counter and, at most, to one soft trust event.
	 */
	cwp_note(srctype, srcid, dcw, ecm->caid, ecm->provid, ecm->sid, GetTickCount());
	/*
	 * TASK 2.7 -- the mirror scan, reading no filter gate on purpose: "half 2
	 * is the complement of half 1" means the same thing whether or not the
	 * operator left the checksum filter on. The repaired variant passes the
	 * checksum layer and is DELIVERED -- this is the only place its story
	 * gets told.
	 */
	cm_note(srctype, srcid, dcw, ecm->caid, ecm->provid, ecm->sid, GetTickCount());


	/*
	 * TASK 2.1 -- offer the key to the agreement ledger.
	 *
	 * Above acceptDCW() on purpose, for the same reason dcwstruct_note() is: the
	 * key that loses the comparison is usually the one about to be discarded
	 * (the ECM already has a delivered key, or the filter refuses this one), and
	 * a disagreement that is never recorded is a disagreement that cannot
	 * corroborate anything later.
	 *
	 * `applied` is 1 because this function is only reached when a key is being
	 * applied to a client's request. Whether it is then *accepted* is decided
	 * below, and an unaccepted key leaves the client with a timeout, which GR2
	 * keeps out of the accusation path -- the ledger records the delivery, the
	 * client actually received (lastecm.dcw), and the retry detector decides
	 * whether a client ever suffered for it.
	 */
#ifdef CACHEEX
	ledger_note_delivery(ecm->ecmd5, ecm->hash, ecm->caid, ecm->provid, ecm->sid,
		dcw, srctype, srcid, GetTickCount());
#endif

	if (!acceptDCW_profile(dcw, cs)) return;

	int cwpart = 2;
	if (ecm->cw1cycle) {
		if (ecm->ecm[0]==ecm->cw1cycle) cwpart = 1; else cwpart = 0;
	}
	else {
		if ( dcwcmp8(dcw,nullcw) ) cwpart = 1;
		else if ( dcwcmp8(dcw+8,nullcw) ) cwpart = 0;
	}

	if (ecm->dcwstatus==STAT_DCW_SUCCESS) {
		return;
	}
	/* TASK 3.16: see the locked copy below. A cache push must not
	 * revive an ECM whose failure was already committed. */
	if (srctype==DCW_SOURCE_CACHE && ecm->dcwstatus==STAT_DCW_FAILED) {
		return;
	}
/*
	if ( !ecmdata_check_cw( ecm->ecm[0], ecm->hash, ecm->caid, ecm->provid, ecm->sid, dcw, cwpart) ) {
		return;
	}
*/

	if (srctype!=DCW_SOURCE_CACHE) {
		pthread_mutex_lock( &prg.lockcache );
		int f = cache_check_cw( ecm->recvtime, ecm->ecm[0], ecm->caid, ecm->hash, ecm->sid, dcw, cwpart);
		pthread_mutex_unlock( &prg.lockcache );
		if (!f) {
			return;
		}
	}

	// filter non-nds halfnulled cw
	if ( dcwcmp8(dcw,nullcw) || dcwcmp8(dcw+8,nullcw) ) {
		if ((ecm->caid>>8)!=9) {
			return;
		}
		int swap = 0;
#ifdef DCWSWAP
		if (cs)	if (cs->option.dcw.swap) swap = 1;
#endif
		if ( !dcwcheck_nds( ecm, dcw, swap) ) {
			return;
		}
	}

#ifdef CHECK_NEXTDCW
	if (cs->option.dcw.check && !cs->option.dcw.halfnulled) {
		int check = checkfreeze_setdcw(ecm,dcw);
		if (check==0) {
			ecm->lastdecode.error++;
			return;
		}
		else if (check==1) {
			ecm->lastdecode.counter = 0;
			ecm->lastdecode.cwcycle = 0;
		}
		else if (check&2) {
			ecm->lastdecode.counter++;
			if (check&4) ecm->lastdecode.cwcycle = '1';
			else ecm->lastdecode.cwcycle = '0';
		}
	}
#endif

#ifdef TESTCHANNEL
	int testchannel = ( (ecm->caid==cfg.testchn.caid) && (ecm->provid==cfg.testchn.provid) && (ecm->sid==cfg.testchn.sid) );
	if (testchannel) {
		char dump[64];
		array2hex( dcw, dump, 16);
		char temp[512];
		src2string(srctype, srcid, temp);
		fdebugf(" =(setdcw)= from %s ch %04x:%06x:%04x/%02x:%08x => %s\n", temp, ecm->caid, ecm->provid, ecm->sid, ecm->ecm[0], ecm->hash, dump);
	}
#endif

	ecm->statusmsg = "Decode Success";
	int instant = (ecm->dcwstatus==STAT_DCW_WAITCACHE);
	ecm->dcwsrctype = srctype;
	ecm->dcwsrcid = srcid;
	ecm->dcwstatus = STAT_DCW_SUCCESS;
	ecm->checktime = 0;
	ecm->waitserver = 0;
	sid_newecm(ecm);
	memcpy( ecm->cw, dcw, 16 );

	// Check timeout
	uint32_t ecmtime = GetTickCount()-ecm->recvtime;
	if ( ecmtime > cs->option.dcw.timeout*ecm->period ) return;
	// Send DCW to clients
	clients_check_sendcw(ecm);

	// Update Stat
	cs->ecmok++;
	cs->ecmoktime += ecmtime;
	int time = (ecmtime+50)/100;
	if (time<99) cs->ttime[time]++; else cs->ttime[99]++;

	if (srctype==DCW_SOURCE_CACHE) {
		if (srcid&PEER_CSP) { // Cache
			struct cachepeer_data *peer = getpeerbyid(srcid&0xffff);
			if (peer) {
				// setup peer last used cache
				peer->lastcaid = ecm->caid;
				peer->lastprov = ecm->provid;
				peer->lastsid = ecm->sid;
				peer->lastdecodetime = ecmtime;
				// add to profiles hits
				peer_hitprofile( peer, cs->id );
				peer->hitnb++;
				cs->hits.csp++;
				cfg.cache.hits++;
				if (instant) {
					peer->ihitnb++;
					cs->hits.instant.csp++;
					cfg.cache.ihits++;
				}
			}
			if (time<99) cs->ttimecache[time]++; else cs->ttimecache[99]++;
		}

#ifdef CACHEEX
		else if (srcid&PEER_CCCAM_CLIENT) { // Cacheex
			struct cc_client_data *cli = getcecccamclientbyid(srcid&0xffff);
			if (cli) {
				// setup client last used cache
				cli->cacheex.lastcaid = ecm->caid;
				cli->cacheex.lastprov = ecm->provid;
				cli->cacheex.lastsid = ecm->sid;
				cli->cacheex.lastdecodetime = ecmtime;
				// add to profiles hits
				cacheex_cccam_hitprofile( cli, cs->id );
				cli->cacheex.hits++;
				cs->hits.cacheex++;
				cfg.cacheex.hits++;
				if (instant) {
					cfg.cacheex.ihits++;
					cs->hits.instant.cacheex++;
					cli->cacheex.ihits++;
				}
			}
			if (time<99) cs->ttimecacheex[time]++; else cs->ttimecacheex[99]++;
		}

#ifdef CAMD35_SRV
		//PEERID_CAMD35
		else if (srcid&PEER_CAMD35_CLIENT) {
			struct camd35_client_data *cli = getcamd35clientbyid(srcid&0xffff);
			if (cli) {
				// setup client last used cache
				cli->cacheex.lastcaid = ecm->caid;
				cli->cacheex.lastprov = ecm->provid;
				cli->cacheex.lastsid = ecm->sid;
				cli->cacheex.lastdecodetime = ecmtime;
				// add to profiles hits
				cacheex_camd35_hitprofile( cli, cs->id );
				cli->cacheex.hits++;
				cs->hits.cacheex++;
				cfg.cacheex.hits++;
				if (instant) {
					cfg.cacheex.ihits++;
					cs->hits.instant.cacheex++;
					cli->cacheex.ihits++;
				}
			}
			if (time<99) cs->ttimecacheex[time]++; else cs->ttimecacheex[99]++;
		}
#endif

#ifdef CS378X_SRV
		//PEERID_CS378X
		else if (srcid&PEER_CS378X_CLIENT) {
			struct camd35_client_data *cli = getcs378xclientbyid(srcid&0xffff);
			if (cli) {
				// setup client last used cache
				cli->cacheex.lastcaid = ecm->caid;
				cli->cacheex.lastprov = ecm->provid;
				cli->cacheex.lastsid = ecm->sid;
				cli->cacheex.lastdecodetime = ecmtime;
				// add to profiles hits
				cacheex_cs378x_hitprofile( cli, cs->id );
				cli->cacheex.hits++;
				cs->hits.cacheex++;
				cfg.cacheex.hits++;
				if (instant) {
					cfg.cacheex.ihits++;
					cs->hits.instant.cacheex++;
					cli->cacheex.ihits++;
				}
			}
			if (time<99) cs->ttimecacheex[time]++; else cs->ttimecacheex[99]++;
		}
#endif

		else if (srcid&PEER_CACHEEX_SERVER) {
			struct server_data *srv = getcesrvbyid( srcid&0xffff );
			if (srv) {
				// setup client last used cache
				srv->cacheex.lastcaid = ecm->caid;
				srv->cacheex.lastprov = ecm->provid;
				srv->cacheex.lastsid = ecm->sid;
				srv->cacheex.lastdecodetime = ecmtime;
				// add to profiles hits
				cacheex_server_hitprofile( srv, cs->id );
				srv->cacheex.hits++;
				cs->hits.cacheex++;
				cfg.cacheex.hits++;
				if (instant) {
					cfg.cacheex.ihits++;
					cs->hits.instant.cacheex++;
					srv->cacheex.ihits++;
				}
			}
			if (time<99) cs->ttimecacheex[time]++; else cs->ttimecacheex[99]++;
		}
#endif
	}
	else if (srctype==DCW_SOURCE_SERVER) {
		struct server_data *srv = getsrvbyid( srcid&0xffff );
		if (srv) srv->hits++;
		if (time<99) cs->ttimecards[time]++; else cs->ttimecards[99]++;
	}
#ifdef SRV_CSCACHE
	else if (srctype==DCW_SOURCE_CSCLIENT) {
		struct cs_client_data *cli = getnewcamdclientbyid( srcid&0xffff );
		if (cli) cli->cachedcw++;
		if (time<99) cs->ttimeclients[time]++; else cs->ttimeclients[99]++;
	}
	else if (srctype==DCW_SOURCE_MGCLIENT) {
		struct mg_client_data *cli = getmgcamdclientbyid( srcid&0xffff );
		if (cli) cli->cachedcw++;
		if (time<99) cs->ttimeclients[time]++; else cs->ttimeclients[99]++;
	}
#endif

#ifdef CACHEEX
	// Send DCW to CACHE-EX servers
	if ( cs->option.fallowcacheex )
	if (ecmtime<cs->option.cacheexvalidtime) { // only for ecm with low time
		pipe_send_cacheex_push_out(ecm);
	}
#endif

	// Send DCW to Cache if not sent
	if ( cs->option.fallowcache && cs->option.cachesendrep && !(ecm->cachestatus&ECM_CACHE_REP) ) {
		//if (ecm->from!=ECM_FROM_CACHEEX)
		pipe_cache_reply(ecm,cs); //Send Good Cache Reply
		ecm->cachestatus |= ECM_CACHE_REP;
	}

#ifdef CLI_CSCACHE
	// Send to Newcamd Cached Servers
	int i;
	for( i=0; i<20; i++ ) {
		if (!ecm->server[i].srvid) break;
		if (ecm->server[i].flag==ECM_SRV_REQUEST) {
			struct server_data *srv = getsrvbyid(ecm->server[i].srvid);
			if (!srv) continue;
			if (!srv->busy) continue;
			if ( (srv->type==TYPE_NEWCAMD)&&(srv->cscached) ) { // Send DCW to server
				struct cs_custom_data srvcd;
				unsigned char buf[32];
				srvcd.msgid = srv->ecm.msgid;
				srvcd.caid = ecm->caid;
				srvcd.sid = ecm->sid;
				srvcd.provid = ecm->provid;
				buf[0] = ecm->ecm[0] | 0x40; // 0xC0 | 0xC1
				buf[2] = 0x10;
				memcpy(&buf[3], &ecm->cw,16);
				if ( !cs_message_send( srv->handle, &srvcd, buf, 19, srv->sessionkey) ) disconnect_srv( srv );
			}
		}
	}
#endif
}


#endif



#ifdef THREAD_DCW

void ecm_setdcwdata( ECM_DATA *ecm, uint8_t dcw[16], int srctype, int srcid, int cwmark )
{
	char nullcw[16] = "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0";
	if ( dcwcmp16(dcw,nullcw) ) return;

	struct cardserver_data *cs = ecm->cs;
	if (!cs) return;

	if ( dcwcmp8(dcw,nullcw) && dcwcmp8(dcw+8,nullcw) ) return;

	/*
	 * TASK 1.8 — structural soft signal, evaluated before acceptDCW() so that
	 * a key which is about to be rejected is attributed as well. A rejected key
	 * otherwise leaves no trace anywhere: acceptDCW() returns a bare 0 and the
	 * client only ever sees a timeout.
	 *
	 * This does not change delivery. The scan's only output is evidence, and
	 * acceptDCW() below still makes the same decision it always made.
	 *
	 * The two filter gates are passed in rather than read inside the scan so
	 * that a filter the operator has disabled contributes nothing: if they
	 * turned DCWFILTER CHECKSUM off, their bouquet legitimately fails it and
	 * counting that would penalise a good source on every key.
	 */
	dcwstruct_note(&trust_tab,
		dcwstruct_scan(dcw, dcw_filter_checksum, dcw_filter_repeat),
		dcw, srctype, srcid, ecm->caid, ecm->provid, ecm->sid, GetTickCount());

	/*
	 * TASK 2.2 -- entropy and collision forensics.
	 *
	 * Same place as dcwstruct_note() and for the same reason: above acceptDCW(),
	 * so a key that is about to be thrown away is examined too. That is not
	 * bookkeeping -- a forged key is exactly the kind that gets discarded, and a
	 * forgery nobody looks at is a forgery nobody can prove.
	 *
	 * The identity passed is the 128-bit ECM MD5 (its 64-bit prefix is what the
	 * ring compares against); with no CACHEEX there is no ecmd5 and the collision
	 * half goes quiet, while the entropy half keeps working, because it needs no
	 * identity at all.
	 */
	cwentropy_note(&trust_tab,
#ifdef CACHEEX
		ecm->ecmd5,
#else
		0,
#endif
		dcw, srctype, srcid, ecm->caid, ecm->provid, ecm->sid, GetTickCount());

	/*
	 * TASK 2.4 -- plausibility: how much of the key's shape survives.
	 *
	 * The third of the three evidence-only hooks, in the same place and for the
	 * same reason as the two above: this runs before acceptDCW() decides
	 * anything, so a key that is about to be discarded is measured too. It
	 * reads no filter gate on purpose -- "exactly one of four group sums is
	 * wrong" means the same thing whether or not the operator left DCWFILTER
	 * CHECKSUM on, which is what makes this a statement about the key rather
	 * than about the configuration.
	 *
	 * Nothing here can reject, delay or alter a delivery: the return value goes
	 * to a counter and, at most, to one soft trust event.
	 */
	cwp_note(srctype, srcid, dcw, ecm->caid, ecm->provid, ecm->sid, GetTickCount());

	/* TASK 2.7 -- the mirror scan, reading no filter gate on purpose. The
	 * repaired mirror variant passes the checksum layer and is DELIVERED;
	 * this is the only place its story gets told. */
	cm_note(srctype, srcid, dcw, ecm->caid, ecm->provid, ecm->sid, GetTickCount());


	/*
	 * TASK 2.1 -- offer the key to the agreement ledger.
	 *
	 * Above acceptDCW() on purpose, for the same reason dcwstruct_note() is: the
	 * key that loses the comparison is usually the one about to be discarded
	 * (the ECM already has a delivered key, or the filter refuses this one), and
	 * a disagreement that is never recorded is a disagreement that cannot
	 * corroborate anything later.
	 *
	 * `applied` is 1 because this function is only reached when a key is being
	 * applied to a client's request. Whether it is then *accepted* is decided
	 * below, and an unaccepted key leaves the client with a timeout, which GR2
	 * keeps out of the accusation path -- the ledger records the delivery, the
	 * client actually received (lastecm.dcw), and the retry detector decides
	 * whether a client ever suffered for it.
	 */
#ifdef CACHEEX
	ledger_note_delivery(ecm->ecmd5, ecm->hash, ecm->caid, ecm->provid, ecm->sid,
		dcw, srctype, srcid, GetTickCount());
#endif

	if (!acceptDCW_profile(dcw, cs)) return;

	int cwpart = 2;
	if (ecm->cw1cycle) {
		if (ecm->ecm[0]==ecm->cw1cycle) cwpart = 1; else cwpart = 0;
	}
	else {
		if ( dcwcmp8(dcw,nullcw) ) cwpart = 1;
		else if ( dcwcmp8(dcw+8,nullcw) ) cwpart = 0;
	}

	pthread_mutex_lock(&prg.lockecm);

	if (ecm->dcwstatus==STAT_DCW_SUCCESS) {
		pthread_mutex_unlock(&prg.lockecm);
		return;
	}
	/*
	 * TASK 3.16: the pipe handler may have queued this push while the
	 * ECM was still waiting, and the failure committed before this
	 * thread took the lock. Do not flip FAILED to SUCCESS, and do not
	 * send the key. A card-server reply is not this path.
	 */
	if (srctype==DCW_SOURCE_CACHE && ecm->dcwstatus==STAT_DCW_FAILED) {
		pthread_mutex_unlock(&prg.lockecm);
		return;
	}
/*
	if ( !ecmdata_check_cw( ecm->ecm[0], ecm->hash, ecm->caid, ecm->provid, ecm->sid, dcw, cwpart) ) {
		pthread_mutex_unlock(&prg.lockecm);
		return;
	}
*/

	if (srctype!=DCW_SOURCE_CACHE) {
		pthread_mutex_lock( &prg.lockcache );
		int f = cache_check_cw( ecm->recvtime, ecm->ecm[0], ecm->caid, ecm->hash, ecm->sid, dcw, cwpart);
		pthread_mutex_unlock( &prg.lockcache );
		if (!f) {
			pthread_mutex_unlock(&prg.lockecm);
			return;
		}
	}

	// filter non-nds halfnulled cw
	if ( dcwcmp8(dcw,nullcw) || dcwcmp8(dcw+8,nullcw) ) {
		if ((ecm->caid>>8)!=9) {
			pthread_mutex_unlock(&prg.lockecm);
			return;
		}
		int swap = 0;
#ifdef DCWSWAP
		if (cs)	if (cs->option.dcw.swap) swap = 1;
#endif
		if ( !dcwcheck_nds( ecm, dcw, swap) ) {
			pthread_mutex_unlock(&prg.lockecm);
			return;
		}
	}

#ifdef CHECK_NEXTDCW
	if (cs->option.dcw.check && !cs->option.dcw.halfnulled) {
		int check = checkfreeze_setdcw(ecm,dcw);
		if (check==0) {
			ecm->lastdecode.error++;
			pthread_mutex_unlock(&prg.lockecm);
			return;
		}
		else if (check==1) {
			ecm->lastdecode.counter = 0;
			ecm->lastdecode.cwcycle = 0;
		}
		else if (check&2) {
			ecm->lastdecode.counter++;
			if (check&4) ecm->lastdecode.cwcycle = '1';
			else ecm->lastdecode.cwcycle = '0';
		}
	}
#endif

#ifdef TESTCHANNEL
	int testchannel = ( (ecm->caid==cfg.testchn.caid) && (ecm->provid==cfg.testchn.provid) && (ecm->sid==cfg.testchn.sid) );
	if (testchannel) {
		char dump[64];
		array2hex( dcw, dump, 16);
		char temp[512];
		src2string(srctype, srcid, temp);
		fdebugf(" =(setdcw)= from %s ch %04x:%06x:%04x/%02x:%08x => %s\n", temp, ecm->caid, ecm->provid, ecm->sid, ecm->ecm[0], ecm->hash, dump);
	}
#endif

	ecm->statusmsg = "Decode Success";
	int instant = (ecm->dcwstatus==STAT_DCW_WAITCACHE);
	ecm->dcwsrctype = srctype;
	ecm->dcwsrcid = srcid;
	ecm->dcwstatus = STAT_DCW_SUCCESS;
	ecm->checktime = 0;
	ecm->waitserver = 0;
	sid_newecm(ecm);
	memcpy( ecm->cw, dcw, 16 );

	/*
	 * TASK 3.6: the waiter is answered -- release the cache entry's push arm
	 * now, while the success is already committed (dcwstatus above) and the
	 * back-pointer still unambiguous. The entry's arm must not outlive its
	 * waiter: an armed entry with a dead waiter force-marks every later peer
	 * key DCW_SENT -- burying it for every future ECM of that hash -- and
	 * stays silent towards peer requests. lockecm -> lockcache is the tree's
	 * sanctioned order; the srctype!=CACHE cache_check_cw walk above took it
	 * first. The helper's guard keeps a newer waiter's arm intact.
	 */
	cache_clear_sendpipe( ecm );

	pthread_mutex_unlock(&prg.lockecm);

	// Check timeout
	uint32_t ecmtime = GetTickCount()-ecm->recvtime;
	if ( ecmtime > cs->option.dcw.timeout*ecm->period ) return;
	// Send DCW to clients
	clients_check_sendcw(ecm);

	/*
	 * TASK 2.5 -- the cache hand-off, observed at the moment it happens (see
	 * cwcycle.h). Only cache-sourced keys are watched, and srcid keeps every
	 * cache flavour in its own slot: CSP peers (id|PEER_CSP), cache-ex CCCam,
	 * Camd35 and Cs378x clients each carry a different flag (GR1/GR4). The
	 * expectation is the half THIS ECM declared through its own tag byte; the
	 * observed marker is what the key's sender declared on the wire, carried
	 * here through the two pipes -- 0 when nobody declared anything, which
	 * makes the delivery unverifiable, not contradictory. The call cannot
	 * reject, delay, alter or score anything: the delivery above is already
	 * done.
	 */
	if (srctype==DCW_SOURCE_CACHE)
		ccy_note(DCW_SOURCE_CACHE, srcid, ecm->caid, ecm->provid, ecm->sid,
		         GetTickCount(), CWCY_AT_SEND,
		         cwcy_expect(ecm->ecm[0], ecm->cw1cycle),
		         cwcy_observed(cwmark));

	// Update Stat
	cs->ecmok++;
	cs->ecmoktime += ecmtime;
	int time = (ecmtime+50)/100;
	if (time<99) cs->ttime[time]++; else cs->ttime[99]++;

	if (srctype==DCW_SOURCE_CACHE) {
		if (srcid&PEER_CSP) { // Cache
			struct cachepeer_data *peer = getpeerbyid(srcid&0xffff);
			if (peer) {
				// setup peer last used cache
				peer->lastcaid = ecm->caid;
				peer->lastprov = ecm->provid;
				peer->lastsid = ecm->sid;
				peer->lastdecodetime = ecmtime;
				// add to profiles hits
				peer_hitprofile( peer, cs->id );
				peer->hitnb++;
				cs->hits.csp++;
				cfg.cache.hits++;
				if (instant) {
					peer->ihitnb++;
					cs->hits.instant.csp++;
					cfg.cache.ihits++;
				}
			}
			if (time<99) cs->ttimecache[time]++; else cs->ttimecache[99]++;
		}

#ifdef CACHEEX
		else if (srcid&PEER_CCCAM_CLIENT) { // Cacheex
			struct cc_client_data *cli = getcecccamclientbyid(srcid&0xffff);
			if (cli) {
				// setup client last used cache
				cli->cacheex.lastcaid = ecm->caid;
				cli->cacheex.lastprov = ecm->provid;
				cli->cacheex.lastsid = ecm->sid;
				cli->cacheex.lastdecodetime = ecmtime;
				// add to profiles hits
				cacheex_cccam_hitprofile( cli, cs->id );
				cli->cacheex.hits++;
				cs->hits.cacheex++;
				cfg.cacheex.hits++;
				if (instant) {
					cfg.cacheex.ihits++;
					cs->hits.instant.cacheex++;
					cli->cacheex.ihits++;
				}
			}
			if (time<99) cs->ttimecacheex[time]++; else cs->ttimecacheex[99]++;
		}

#ifdef CAMD35_SRV
		//PEERID_CAMD35
		else if (srcid&PEER_CAMD35_CLIENT) {
			struct camd35_client_data *cli = getcamd35clientbyid(srcid&0xffff);
			if (cli) {
				// setup client last used cache
				cli->cacheex.lastcaid = ecm->caid;
				cli->cacheex.lastprov = ecm->provid;
				cli->cacheex.lastsid = ecm->sid;
				cli->cacheex.lastdecodetime = ecmtime;
				// add to profiles hits
				cacheex_camd35_hitprofile( cli, cs->id );
				cli->cacheex.hits++;
				cs->hits.cacheex++;
				cfg.cacheex.hits++;
				if (instant) {
					cfg.cacheex.ihits++;
					cs->hits.instant.cacheex++;
					cli->cacheex.ihits++;
				}
			}
			if (time<99) cs->ttimecacheex[time]++; else cs->ttimecacheex[99]++;
		}
#endif

#ifdef CS378X_SRV
		//PEERID_CS378X
		else if (srcid&PEER_CS378X_CLIENT) {
			struct camd35_client_data *cli = getcs378xclientbyid(srcid&0xffff);
			if (cli) {
				// setup client last used cache
				cli->cacheex.lastcaid = ecm->caid;
				cli->cacheex.lastprov = ecm->provid;
				cli->cacheex.lastsid = ecm->sid;
				cli->cacheex.lastdecodetime = ecmtime;
				// add to profiles hits
				cacheex_cs378x_hitprofile( cli, cs->id );
				cli->cacheex.hits++;
				cs->hits.cacheex++;
				cfg.cacheex.hits++;
				if (instant) {
					cfg.cacheex.ihits++;
					cs->hits.instant.cacheex++;
					cli->cacheex.ihits++;
				}
			}
			if (time<99) cs->ttimecacheex[time]++; else cs->ttimecacheex[99]++;
		}
#endif

		else if (srcid&PEER_CACHEEX_SERVER) {
			struct server_data *srv = getcesrvbyid( srcid&0xffff );
			if (srv) {
				// setup client last used cache
				srv->cacheex.lastcaid = ecm->caid;
				srv->cacheex.lastprov = ecm->provid;
				srv->cacheex.lastsid = ecm->sid;
				srv->cacheex.lastdecodetime = ecmtime;
				// add to profiles hits
				cacheex_server_hitprofile( srv, cs->id );
				srv->cacheex.hits++;
				cs->hits.cacheex++;
				cfg.cacheex.hits++;
				if (instant) {
					cfg.cacheex.ihits++;
					cs->hits.instant.cacheex++;
					srv->cacheex.ihits++;
				}
			}
			if (time<99) cs->ttimecacheex[time]++; else cs->ttimecacheex[99]++;
		}
#endif
	}
	else if (srctype==DCW_SOURCE_SERVER) {
		struct server_data *srv = getsrvbyid( srcid&0xffff );
		if (srv) srv->hits++;
		if (time<99) cs->ttimecards[time]++; else cs->ttimecards[99]++;
	}
#ifdef SRV_CSCACHE
	else if (srctype==DCW_SOURCE_CSCLIENT) {
		struct cs_client_data *cli = getnewcamdclientbyid( srcid&0xffff );
		if (cli) cli->cachedcw++;
		if (time<99) cs->ttimeclients[time]++; else cs->ttimeclients[99]++;
	}
	else if (srctype==DCW_SOURCE_MGCLIENT) {
		struct mg_client_data *cli = getmgcamdclientbyid( srcid&0xffff );
		if (cli) cli->cachedcw++;
		if (time<99) cs->ttimeclients[time]++; else cs->ttimeclients[99]++;
	}
#endif

#ifdef CACHEEX
	// Send DCW to CACHE-EX servers
	if ( cs->option.fallowcacheex )
	if (ecmtime<cs->option.cacheexvalidtime) { // only for ecm with low time
		pipe_send_cacheex_push_out(ecm);
	}
#endif

	// Send DCW to Cache if not sent
	if ( cs->option.fallowcache && cs->option.cachesendrep && !(ecm->cachestatus&ECM_CACHE_REP) ) {
		//if (ecm->from!=ECM_FROM_CACHEEX)
		pipe_cache_reply(ecm,cs); //Send Good Cache Reply
		ecm->cachestatus |= ECM_CACHE_REP;
	}

#ifdef CLI_CSCACHE
	// Send to Newcamd Cached Servers
	int i;
	for( i=0; i<20; i++ ) {
		if (!ecm->server[i].srvid) break;
		if (ecm->server[i].flag==ECM_SRV_REQUEST) {
			struct server_data *srv = getsrvbyid(ecm->server[i].srvid);
			if (!srv) continue;
			if (!srv->busy) continue;
			if ( (srv->type==TYPE_NEWCAMD)&&(srv->cscached) ) { // Send DCW to server
				struct cs_custom_data srvcd;
				unsigned char buf[32];
				srvcd.msgid = srv->ecm.msgid;
				srvcd.caid = ecm->caid;
				srvcd.sid = ecm->sid;
				srvcd.provid = ecm->provid;
				buf[0] = ecm->ecm[0] | 0x40; // 0xC0 | 0xC1
				buf[2] = 0x10;
				memcpy(&buf[3], &ecm->cw,16);
				if ( !cs_message_send( srv->handle, &srvcd, buf, 19, srv->sessionkey) ) disconnect_srv( srv );
			}
		}
	}
#endif
}





/*
 * TASK 2.5 -- one extra byte rides at the end of the dcw pipe message: the
 * cycle marker the key's sender declared on the wire (0 = nothing declared).
 * Only the cache pipe consumer has it; every other producer passes 0.
 */
inline int get_setdcwdata(uint8_t *buf, void *ecm, uint8_t *dcw, int *srctype, int *srcid, int *cwmark)
{
	int index = 1;
	memcpy(srctype, buf+index, sizeof(int) );
	index += sizeof(int);
	memcpy(srcid, buf+index, sizeof(int) );
	index += sizeof(int);
	memcpy( ecm, buf+index, sizeof(void*) );
	index += sizeof(void*);
	memcpy( dcw, buf+index, 16 );
	index+=16;
	if (cwmark) *cwmark = buf[index];
	index+=1;
	return index;
}

int put_setdcwdata(uint8_t *buf, ECM_DATA *ecm, uint8_t *dcw, int srctype, int srcid, int cwmark )
{
	buf[0] = 55;
	int index = 1;
	memcpy(buf+index, &srctype, sizeof(int) );
	index += sizeof(int);
	memcpy(buf+index, &srcid, sizeof(int) );
	index += sizeof(int);
	memcpy( buf+index, &ecm, sizeof(void*) );
	index += sizeof(void*);
	memcpy( buf+index, dcw, 16 );
	index+=16;
	buf[index] = (uint8_t)cwmark;
	index+=1;
	return index;
}


/*
 * TASK 2.5 -- the marked form of the marshal. Every existing caller keeps
 * ecm_setdcw(): card servers and cache-ex clients never declare a cycle, so
 * their marker is 0. Only the cache pipe consumer (th-ecm.c,
 * PIPE_CACHE_FIND_SUCCESS) knows what the sending peer declared, and calls
 * ecm_setdcw_marked() so the declaration survives the trip to the delivery
 * hook inside ecm_setdcwdata().
 */
void ecm_setdcw_marked( ECM_DATA *ecm, uint8_t dcw[16], int srctype, int srcid, int cwmark )
{
	uint8_t buf[64];
	int len = put_setdcwdata(buf, ecm, dcw, srctype, srcid, cwmark );
	pipe_send( dcwpipe[1], buf, len);
}

void ecm_setdcw( ECM_DATA *ecm, uint8_t dcw[16], int srctype, int srcid )
{
	ecm_setdcw_marked( ecm, dcw, srctype, srcid, 0 );
}

void *setdcw_thread(void *param)
{

	prg.pid_setdcw = syscall(SYS_gettid);
	prg.tid_setdcw = pthread_self();
	prctl(PR_SET_NAME,"Set DCW",0,0,0);

	struct pollfd pfd;
	while (1) {
		pfd.fd = dcwpipe[0];
		pfd.events = POLLIN | POLLPRI;
		int retval = poll(&pfd, 1, 3000);
		if ( retval>0 ) {
			uint8_t buf[64];
			int len = pipe_recv( dcwpipe[0], buf);
			if (len>0) {
				ECM_DATA *ecm;
				uint8_t dcw[16];
				int srctype;
				int srcid;
				int cwmark = 0;
				get_setdcwdata(buf, &ecm, dcw, &srctype, &srcid, &cwmark);
				ecm_setdcwdata( ecm, dcw, srctype, srcid, cwmark );
			}
		}
	}
}

#endif

