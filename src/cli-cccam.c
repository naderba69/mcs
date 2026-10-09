struct cs_card_data *cc_getcardbyid( struct server_data *srv, uint32_t id );
int cc_sendecm_srv(struct server_data *srv, ECM_DATA *ecm);

///////////////////////////////////////////////////////////////////////////////
// CLIENT CONNECT
///////////////////////////////////////////////////////////////////////////////

struct cs_card_data *cc_getcardbyid( struct server_data *srv, uint32_t id )
{
	struct cs_card_data *card = srv->card;
	while (card) {
		if (card->shareid==id) return card;
		card = card->next;
	}
	return NULL;
}


// Send Client info to server.
int cc_sendinfo_srv(struct server_data *srv, int ismultics)
{
	uint8_t buf[CC_MAXMSGSIZE];
	memset(buf, 0, CC_MAXMSGSIZE);
	memcpy(buf, srv->user, 20);
	memcpy(buf + 20, cfg.cccam.nodeid, 8 );
	buf[28] = 0; //srv->wantemus;
	memcpy(buf + 29, cfg.cccam.version, 32);	// cccam version (ascii)
	if (ismultics) {
		buf[57]='W'; buf[58]='H'; buf[59]='O';
	}
	memcpy(buf + 61, cfg.cccam.build, 32);	// build number (ascii)
	debugf( getdbgflag(DBG_SERVER, 0, srv->id), " Server: send client info User: '%s', Version: '%s', Build: '%s'.\n", srv->user, cfg.cccam.version, cfg.cccam.build);
	return cc_msg_send( srv->handle, &srv->sendblock, CC_MSG_CLI_INFO, 20 + 8 + 1 + 64, buf);
}


///////////////////////////////////////////////////////////////////////////////
// Connect to a server.
// Return
// 0: no error

void *cacheex_cc_srv_recvmsg(struct server_data *srv);

static char CCcam[] = "CCcam";
static char CCcam_OScam[] = "CCcam/OScam";
static char CCcam_MultiCS[] = "CCcam/MCS";

int cc_connect_srv(struct server_data *srv, int fd)
{
	int n;
	uint8_t data[20];
	uint8_t hash[SHA_DIGEST_LENGTH];
	uint8_t buf[CC_MAXMSGSIZE];
	char pwd[64];
	//
	if (fd < 0) return -1;
	// INIT
	srv->progname = CCcam;
	memset( srv->version, 0, sizeof(srv->version) );
	// get init seed(random) from server
	if((n = recv_nonb(fd, data, 16,5000)) != 16) {
		static char msg[]= "Server does not return init sequence";
		srv->statmsg = msg;
		debugf( getdbgflag(DBG_SERVER, 0, srv->id), " Server (%s:%d) does not return 16 bytes\n", srv->host->name,srv->port);
		return -2;
	}

#ifdef DEBUG_NETWORK
	if (flag_debugnet) {
		debugf( getdbgflag(DBG_SERVER, 0, srv->id), " CCcam: receive server init seed (%d)\n",n);
		debughex(data,n);
	}
#endif

	// Check Multics
	int ismultics = 0;
	uint8_t a = (data[0]^'M') + data[1] + data[2];
	uint8_t b = data[4] + (data[5]^'C') + data[6];
	uint8_t c = data[8] + data[9] + (data[10]^'S');
	if ( (a==data[3])&&(b==data[7])&&(c==data[11]) ) {
		srv->progname = CCcam_MultiCS;
		ismultics = 1;
	}

	//Check oscam-cccam
	uint32_t sum = 0x1234;
	uint32_t recv_sum = (data[14] << 8) | data[15];
	int i;
	for (i=0; i<14; i++) sum+= data[i];
	if (sum==recv_sum) srv->progname = CCcam_OScam;

	cc_crypt_xor(data);  // XOR init bytes with 'CCcam'

	SHA_CTX ctx;
	SHA1_Init(&ctx);
	SHA1_Update(&ctx, data, 16);
	SHA1_Final(hash, &ctx);

	//debugdump(hash, sizeof(hash), "CCcam: sha1 hash:");

	//init crypto states
	cc_crypt_init(&srv->recvblock, hash, 20);
	cc_decrypt(&srv->recvblock, data, 16); 
	cc_crypt_init(&srv->sendblock, data, 16);
	cc_decrypt(&srv->sendblock, hash, 20);

	if ( !cc_msg_send( fd, &srv->sendblock, CC_MSG_NO_HEADER, 20,hash) ) return -1;   // send crypted hash to server
	memset(buf, 0, sizeof(buf));
	memcpy(buf, srv->user, 20);
	//debugf( getdbgflag(DBG_SERVER, 0, srv->id), " CCcam: username '%s'\n",srv->username);
	if ( !cc_msg_send( fd, &srv->sendblock, CC_MSG_NO_HEADER, 20, buf) ) return -1;    // send usr '0' padded -> 20 bytes

	memset(buf, 0, sizeof(buf));
	memset(pwd, 0, sizeof(pwd));

	//debugf("CCcam: 'CCcam' xor\n");
	memcpy(buf, "CCcam\0", 6);
	strncpy(pwd, srv->pass, 63);
	cc_encrypt(&srv->sendblock, (uint8_t *)pwd, strlen(pwd));
	if ( !cc_msg_send( fd, &srv->sendblock, CC_MSG_NO_HEADER, 6, buf) ) return -1; // send 'CCcam' xor w/ pwd
	if ((n = recv_nonb(fd, data, 20,5000)) != 20) {
		static char msg[]= "Password ACK not received";
		srv->statmsg = msg;
		debugf( getdbgflag(DBG_SERVER, 0, srv->id), " CCcam: login failed to Server (%s:%d), pwd ack not received (n = %d)\n",srv->host->name,srv->port, n);
		return -2;
	}
	cc_decrypt(&srv->recvblock, data, 20);
	//hexdump(data, 20, "CCcam: pwd ack received:");

	if (memcmp(data, buf, 5)) {  // check server response
		static char msg[]= "Invalid user/pass";
		srv->statmsg = msg;
		debugf( getdbgflag(DBG_SERVER, 0, srv->id), " CCcam: login failed to Server (%s:%d), usr/pwd invalid\n",srv->host->name,srv->port);
		return -2;
	}// else debugf( getdbgflag(DBG_SERVER, 0, srv->id), " CCcam: login succeeded to Server (%s:%d)\n",srv->host->name,srv->port);

	srv->handle = fd;
	if (!cc_sendinfo_srv(srv,ismultics)) {
		srv->handle = -1;
		static char msg[]= "Error sending client data";
		srv->statmsg = msg;
		debugf( getdbgflag(DBG_SERVER, 0, srv->id), " CCcam: login failed to Server (%s:%d), could not send client data\n",srv->host->name,srv->port);
		return -3;
	}
	// Update Server data
	static char msg[]= "Connected";
	srv->statmsg = msg;
	srv->connection.status = 1;
	srv->connection.time = GetTickCount();
	srv->keepalive.status = 0;
	srv->keepalive.time = GetTickCount();
	srv->busy = 0;
	srv->lastecmoktime = 0;
	srv->lastecmtime = 0;
	srv->lastdcwtime = 0;
	srv->chkrecvtime = 0;
	srv->msg.len = 0;
	//srv->handle = fd;
	memset(srv->version,0,32);

#ifdef CACHEEX
	if (srv->cacheex_mode==2) {
		if (!create_thread( PACKED_MPTR(srv, pthread_t, tid), (threadfn)cacheex_cc_srv_recvmsg, srv )) {
			disconnect_srv(srv);
		}
	}
	else
#endif

#ifdef EPOLL_ECM
	pipe_pointer( prg.pipe.ecm[1], PIPE_SRV_CONNECTED, srv );
#else
	pipe_cmd( prg.pipe.ecm[1], PIPE_SRV_CONNECTED );
#endif
	return 0;
}

///////////////////////////////////////////////////////////////////////////////

void cc_srv_recvmsg(struct server_data *srv)
{     
	unsigned char buf[CC_MAXMSGSIZE];
	struct cs_card_data *card;
	struct cardserver_data *cs;
	int i, len;
	ECM_DATA *ecm;

	if ( (srv->type==TYPE_CCCAM)&&(srv->handle>0) ) {
		len = cc_peekmsg( srv->handle, &srv->recvblock, &srv->msg, buf );
		if (len==0) {
			debugf( getdbgflag(DBG_SERVER, 0, srv->id), " CCcam: server (%s:%d) read failed %d\n", srv->host->name, srv->port, len);
			disconnect_srv(srv);
		}
		else if (len==-1) {
			if (!srv->chkrecvtime) srv->chkrecvtime = GetTickCount();
			else if ( (srv->chkrecvtime+500)<GetTickCount() ) {
				debugf( getdbgflag(DBG_SERVER, 0, srv->id), " CCcam: server (%s:%d) read failed %d\n", srv->host->name, srv->port, len);
				disconnect_srv(srv);
			}
		}
		else if (len>0) {
			srv->chkrecvtime = 0;
			switch (buf[1]) {
				case CC_MSG_CLI_INFO:
					debugf( getdbgflag(DBG_SERVER, 0, srv->id), " CCcam: Client data ACK from Server (%s:%d)\n", srv->host->name,srv->port);
					break;

				case CC_MSG_ECM_REQUEST: // Get CW
					if (!srv->busy) {
						debugf( getdbgflag(DBG_SERVER, 0, srv->id), " [!] dcw error from server (%s:%d), unknown ecm request\n",srv->host->name,srv->port);
						break;
					}
#ifdef CACHEEX
					if (srv->cacheex_mode) {
						debugf( getdbgflag(DBG_SERVER, 0, srv->id), " [!] dcw error from cacheex server (%s:%d)\n",srv->host->name,srv->port);
						break;
					}
#endif
					uint8_t dcw[16];
					cc_crypt_cw( cfg.cccam.nodeid, srv->busycardid, &buf[4]);
					memcpy(dcw, &buf[4], 16);
					cc_decrypt(&srv->recvblock, buf+4, len-4); // additional crypto step				

					srv->busy = 0;
					pipe_cmd( prg.pipe.ecm[1], PIPE_SRV_AVAILABLE );

					srv->lastdcwtime = GetTickCount();

					pthread_mutex_lock(&prg.lockecm); //###

					ecm = srv->ecm.request;
					if (!ecm) {
						debugf( getdbgflag(DBG_SERVER, 0, srv->id), " [!] error cw from server (%s:%d), ecm not found!!!\n",srv->host->name,srv->port);
						pthread_mutex_unlock(&prg.lockecm); //###
						srv->busy = 0;
						break;
					}
					// check for ECM???
					if (ecm->hash!=srv->ecm.hash) {
						debugf( getdbgflag(DBG_SERVER, 0, srv->id), " [!] error cw from server (%s:%d), ecm deleted!!!\n",srv->host->name,srv->port);
						pthread_mutex_unlock(&prg.lockecm); //###
						srv->busy = 0;
						break;
					}

					cs = ecm->cs;
					card = cc_getcardbyid( srv, srv->busycardid );
					// Check for DCW
					if (!acceptDCW_profile(dcw, cs)) {
						srv->ecmerrdcw ++;
#ifdef SID_FILTER
						if (cs && card) {
							cardsids_update( card, ecm->provid, ecm->sid, -1);
							srv_cstatadd( srv, cs->id, 0 , 0);
						}
#endif
						ecm_setsrvflag(srv->ecm.request, srv->id, ECM_SRV_REPLY_FAIL);
						pthread_mutex_unlock(&prg.lockecm); //###
						break;
					}
//					else {

					srv->lastecmoktime = GetTickCount()-srv->lastecmtime;
					srv->ecmoktime += srv->lastecmoktime;
					srv->ecmok++;

					ecm_setsrvflagdcw(srv->ecm.request, srv->id, ECM_SRV_REPLY_GOOD,dcw);
#ifdef SID_FILTER
					if (cs && card) {
						cardsids_update( card, ecm->provid, ecm->sid, 1); /// + Card nodeID
						srv_cstatadd( srv, cs->id, 1 , srv->lastecmoktime);
					}
#endif
					if (card) {
						card->ecmoktime += GetTickCount()-srv->lastecmtime;
						card->ecmok++;
					}

					if (ecm->dcwstatus!=STAT_DCW_SUCCESS) {
						static char msg[] = "Good dcw from CCcam server";
						ecm->statusmsg = msg;
						debugf( getdbgflagpro(DBG_SERVER, 0, srv->id,cs->id), " <= cw from CCcam server (%s:%d) ch %04x:%06x:%04x (%dms)\n", srv->host->name,srv->port, ecm->caid,ecm->provid,ecm->sid, GetTickCount()-srv->lastecmtime);
						ecm_setdcw( ecm, dcw, DCW_SOURCE_SERVER, srv->id );
					}

					pthread_mutex_unlock(&prg.lockecm); //###
					wakeup_sendecm(); // Wakeup ecm waiting for availabe servers
					break;

				case CC_MSG_ECM_NOK1: // EAGAIN, Retry
/*
					if (!srv->busy) break;
					ecm = srv->busyecm;
					debugf( getdbgflag(DBG_SERVER, 0, srv->id), " <| decode1 failed from CCcam server (%s:%d) ch %04x:%06x:%04x (%dms)\n", srv->host->name,srv->port, ecm->caid,ecm->provid,ecm->sid, GetTickCount()-srv->lastecmtime);

					if ( (GetTickCount()-srv->lastecmtime)<CC_ECMRETRY_TIMEOUT ) {
						if (srv->retry<CC_ECMRETRY_MAX) {
							srv->busy = 0;
							if (cc_sendecm_srv(srv, ecm)) {
								srv->lastecmtime = GetTickCount();
								srv->busy = 1;
								srv->retry++;
								break;
							}
						}
					}
					if (srv->retry>=CC_ECMRETRY_MAX) {
						ecm_setsrvflag(ecm, srv->id, ECM_SRV_EXCLUDE); 
					}
					srv->busy = 0;
					break;
*/
				case CC_MSG_ECM_NOK2: // ecm decode failed
					if (!srv->busy) {
						debugf( getdbgflag(DBG_SERVER, 0, srv->id), " [!] dcw error from server (%s:%d), unknown ecm request\n",srv->host->name,srv->port);
						break;
					}
#ifdef CACHEEX
					if (srv->cacheex_mode) {
						debugf( getdbgflag(DBG_SERVER, 0, srv->id), " [!] dcw error from cacheex server (%s:%d)\n",srv->host->name,srv->port);
						break;
					}
#endif

					pthread_mutex_lock(&prg.lockecm); //###

					ecm = srv->ecm.request;
					if (!ecm) {
						debugf( getdbgflag(DBG_SERVER, 0, srv->id), " [!] dcw error from server (%s:%d), ecm not found!!!\n",srv->host->name,srv->port);
						pthread_mutex_unlock(&prg.lockecm); //###
						srv->busy = 0;
						break;
					}
					// check for ECM???
					if (ecm->hash!=srv->ecm.hash) {
						debugf( getdbgflag(DBG_SERVER, 0, srv->id), " [!] dcw error from server (%s:%d), ecm deleted!!!\n",srv->host->name,srv->port);
						pthread_mutex_unlock(&prg.lockecm); //###
						srv->busy = 0;
						break;
					}

					debugf( getdbgflagpro(DBG_SERVER, 0, srv->id,ecm->cs->id), " <| decode failed from CCcam server (%s:%d) ch %04x:%06x:%04x (%dms)\n", srv->host->name,srv->port, ecm->caid,ecm->provid,ecm->sid, GetTickCount()-srv->lastecmtime);
					cs = ecm->cs;

					if ( cs && (ecm->dcwstatus!=STAT_DCW_SUCCESS) && (srv->retry<cs->option.retry.cccam) ) {
						if ((GetTickCount()-ecm->recvtime) < (cs->option.server.timeout*ecm->period)) {
							srv->busy = 0;
							if (cc_sendecm_srv(srv, ecm)) {
								srv->lastecmtime = GetTickCount();
								srv->busy = 1;
								srv->retry++;
								debugf( getdbgflagpro(DBG_SERVER, 0, srv->id,ecm->cs->id), " (RE%d) -> ecm to CCcam server (%s:%d) ch %04x:%06x:%04x\n",srv->retry,srv->host->name,srv->port,ecm->caid,ecm->provid,ecm->sid);
								pthread_mutex_unlock(&prg.lockecm); //###
								break;
							}
						}
					}

#ifdef SID_FILTER
					if (cs) {
						card = cc_getcardbyid( srv, srv->busycardid );
						if (card) cardsids_update(card, ecm->provid, ecm->sid, -1);
						srv_cstatadd( srv, cs->id, 0 , 0);
					}
#endif
					ecm_setsrvflag(srv->ecm.request, srv->id, ECM_SRV_REPLY_FAIL);

					srv->busy = 0;
					pthread_mutex_unlock(&prg.lockecm); //###
					pipe_cmd( prg.pipe.ecm[1], PIPE_SRV_AVAILABLE );
					break;


				case CC_MSG_BAD_ECM:
					if ( !cc_msg_send( srv->handle, &srv->sendblock, CC_MSG_BAD_ECM, 0, NULL) ) disconnect_srv(srv);
					//debugf( getdbgflag(DBG_SERVER, 0, srv->id), " CCcam: cmd 0x05 from Server (%s:%d)\n",srv->host->name,srv->port);
					//currentecm.state = ECM_STATUS_FAILED;
					break;

				case CC_MSG_KEEPALIVE:
					srv->keepalive.status = 0;
					//debugf( getdbgflag(DBG_SERVER, 0, srv->id), " CCcam: Keepalive ACK from Server (%s:%d)\n",srv->host->name,srv->port);
					break;

				case CC_MSG_CARD_DEL: // Delete Card
					pthread_mutex_lock(&prg.lockecm); //###
					card = srv->card;
					uint32_t k = buf[4]<<24 | buf[5]<<16 | buf[6]<<8 | buf[7];
					struct cs_card_data *prevcard = NULL;
					while (card) {
						if (card->shareid==k) {
							debugf( getdbgflag(DBG_SERVER, 0, srv->id), " CCcam: server (%s:%d), remove share-id %d\n",srv->host->name,srv->port,k);
							if (prevcard) prevcard->next = card->next; else srv->card = card->next;
							//Free SIDs
							int s;
							for(s=0; s<256; s++) {
								struct sid_data *sid1 = card->sids[s];
								while (sid1) {
									struct sid_data *sid = sid1;
									sid1 = sid1->next;
									free(sid);
								}
							}
							free(card);
							// check for current ecm
							if (srv->busy && (srv->busycardid==k) ) ecm_setsrvflag(srv->ecm.request, srv->id, ECM_SRV_EXCLUDE);
							break;
						}
						prevcard = card;
						card = card->next;
					}
					pthread_mutex_unlock(&prg.lockecm); //###
			  		break;

				case CC_MSG_CARD_ADD:
					// remove own cards -> same nodeid "cfg.cccam.nodeid"
					if ( (buf[24]<=16) ) { // && memcmp(buf+26+buf[24]*7,cfg.cccam.nodeid,8) ) { // check Only the first 4 bytes
						// nodeid index = 26 + 7 * buf[24]
						struct cs_card_data tcard;
						memset(&tcard, 0, sizeof(struct cs_card_data) );
						tcard.shareid = buf[4]<<24 | buf[5]<<16 | buf[6]<<8 | buf[7];
						tcard.uphops = buf[14]+1;
						memcpy( tcard.nodeid, buf+26+buf[24]*7, 8);
						tcard.caid = (buf[12]<<8)+(buf[13]);
						tcard.nbprov = buf[24];
						i = 26+buf[24]*7;
						if (tcard.nbprov>CARD_MAXPROV) tcard.nbprov = CARD_MAXPROV;

						debugf( getdbgflag(DBG_SERVER, 0, srv->id), " CCcam: new card (%s:%d) %02x%02x%02x%02x%02x%02x%02x%02x_%x uphops %d caid %04x providers %d\n",srv->host->name,srv->port, buf[i],buf[i+1],buf[i+2],buf[i+3],buf[i+4],buf[i+5],buf[i+6],buf[i+7],tcard.shareid ,tcard.uphops, tcard.caid, tcard.nbprov);

						int nbprov = 0; // Accepted Providers
						for (i=0;i<tcard.nbprov; i++) {
							uint32_t provid = (buf[25+i*7]<<16) | (buf[26+i*7]<<8) | (buf[27+i*7]);
							if ( tcard.uphops <= srv_sharelimits( srv, tcard.caid, provid) ) {
								tcard.prov[nbprov] = provid;
								nbprov++;
								//debugf( getdbgflag(DBG_SERVER, 0, srv->id), " Accepted %04x:%06x\n", tcard.caid, provid);
							}
							//else debugf( getdbgflag(DBG_SERVER, 0, srv->id), " Ignored %04x:%06x\n", tcard.caid, provid);
						}
						if (nbprov) {
							tcard.nbprov = nbprov;
							//debugf( getdbgflag(DBG_SERVER, 0, srv->id), " CCcam: new card (%s:%d) %02x%02x%02x%02x%02x%02x%02x%02x_%x uphops %d caid %04x providers %d\n",srv->host->name,srv->port, buf[i],buf[i+1],buf[i+2],buf[i+3],buf[i+4],buf[i+5],buf[i+6],buf[i+7],tcard.shareid ,tcard.uphops, tcard.caid, tcard.nbprov);
							card = malloc( sizeof(struct cs_card_data) );
							memcpy( card, &tcard, sizeof(struct cs_card_data) );
							pthread_mutex_lock(&prg.lockecm); //###
							card->next = srv->card;
							srv->card = card;
							pthread_mutex_unlock(&prg.lockecm); //###
						}
					}
					break;

				case CC_MSG_SRV_INFO:
					memcpy(srv->nodeid, buf+4, 8);
					memcpy(srv->version, buf+12, 31);
					if ( (srv->version[25]=='M')&&(srv->version[26]=='C')&&(srv->version[27]=='S')&&(srv->version[28]==0) ) {
						sprintf( srv->version, "r%d",  srv->version[29] | (srv->version[30]<<8));
						srv->progname = CCcam_MultiCS;
						// printf(" Multics %d\n", srv->version[29] | (srv->version[30]<<8) );
					}
					for (i=12; i<53; i++) {
						if (!buf[i]) break;
						if ( (buf[i]<32)||(buf[i]>'z') ) {
							memset(srv->version, 0, 31);
							break;
						}
					}
					memcpy(srv->build, buf+44, 31);
					debugf( getdbgflag(DBG_SERVER, 0, srv->id), " CCcam: server (%s:%d), info: version %s build %s\n",srv->host->name,srv->port,buf+12, buf+44);
					break;

#ifdef CACHEEX
				 case CC_MSG_CACHE_PUSH:
					if (srv->cacheex_mode!=2) break;
					if (buf[18]==0) { // Got CW
						if (!dcw_hard_reject(buf+44)) { /* TASK 3.15: profile gate is cache_setdcw */
							uint8_t cw[16];
							srv->cacheex.got[0]++;
							int uphop = buf[60];
							if (uphop<10) srv->cacheex.got[uphop]++;
							struct cache_data cacheex;
							cacheex.caid = (buf[4]<<8) | buf[5];
							cacheex.provid = (buf[6]<<24) | (buf[7]<<16) | (buf[8]<<8) | buf[9];
							cacheex.sid = (buf[14]<<8) | buf[15];
							// Look for cardserver
							cs = getcsbycaprovid(cacheex.caid, cacheex.provid);
							if ( !cs || !cs->option.fallowcacheex ) {
								srv->cacheex.badcw++;
								break;
							}
							if ((buf[19+4]&0xFE)==0x80) cacheex.tag = buf[19+4]; else cacheex.tag = 0;
							memcpy( cacheex.ecmd5, buf+24, 16);
							if ( !checkECMD5(cacheex.ecmd5) ) srv->cacheex.csp++;
							cacheex.hash = (buf[43]<<24) | (buf[42]<<16) | (buf[41]<<8) | buf[40];
							memcpy( cw, buf+44, 16);
							if (!cacheex_check(&cacheex)) break;
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
									pipe_send_cacheex_push_cache(&cacheex, cw, srv->nodeid); //cacheex_push(&cacheex, cw, nodeid);
								}
							}
							//debugf( getdbgflag(DBG_CACHEEX, 0, 0)," CACHEEX PUSH from CCcam server (%s:%d) %04x:%06x:%04x:%08x\n", srv->host->name, srv->port,cacheex.caid,cacheex.provid,cacheex.sid,cacheex.hash);
						}
					}
/*
					else {
						cli->cacheex.totalreq++;
					}
*/
					break;
#endif

				//default: debugdump(buf,len," CCcam: unknown packet from server (%s:%d): ",srv->host->name,srv->port);
				} // switch
			srv->keepalive.time = GetTickCount();
		}
	}
}


// Receive messages from server
// CACHEEX MODE 2
void *cacheex_cc_srv_recvmsg(struct server_data *srv)
{
	srv->pid = syscall(SYS_gettid);

	while (srv->handle>0) {
		struct pollfd pfd;
		pfd.fd = srv->handle;
		pfd.events = POLLIN | POLLPRI;
		int retval = poll(&pfd, 1, 3005); // for 3seconds
		if (retval==0) continue;
		else if (retval<0) { // error
			disconnect_srv(srv);
			break;
		}
		else if ( pfd.revents & (POLLIN|POLLPRI) ) cc_srv_recvmsg(srv);
		else {
			disconnect_srv(srv);
			break;
		}
	}

	srv->pid = 0;
	return NULL;
}

///////////////////////////////////////////////////////////////////////////////

int cc_sendecm_srv(struct server_data *srv, ECM_DATA *ecm)
{
	unsigned char buf[CC_MAXMSGSIZE];

	//if ( (srv->handle>0)&&(!srv->busy) ) {
	//	if (!cc_getcardbyid(srv, srv->busycardid)) return 0;
		//debugf( getdbgflag(DBG_SERVER, 0, srv->id), " -> ecm to CCcam server (%s:%d) ch %04x:%06x:%04x shareid %x\n", srv->host->name, srv->port,ecm->caid,ecm->provid,ecm->sid,srv->busycardid);
		buf[0] = ecm->caid>>8;
		buf[1] = ecm->caid&0xff;
		buf[2] = ecm->provid>>24;
		buf[3] = ecm->provid>>16;
		buf[4] = ecm->provid>>8;
		buf[5] = ecm->provid&0xff;
		// srv->busycardid is saved from srvtab_arrange()
		buf[6] = srv->busycardid>>24;
		buf[7] = srv->busycardid>>16;
		buf[8] = srv->busycardid>>8;
		buf[9] = srv->busycardid&0xff;
		buf[10] = ecm->sid>>8;
		buf[11] = ecm->sid&0xff;
		buf[12] = ecm->ecmlen;
		memcpy( &buf[13],&ecm->ecm[0], ecm->ecmlen);
		srv->lastecmtime = GetTickCount();
		srv->busy = 1;
		////srv->ecm.msgid = ecm->id;
		return cc_msg_send( srv->handle, &srv->sendblock, CC_MSG_ECM_REQUEST, 13+ecm->ecmlen, buf );
	//}
	//return 0;
}


