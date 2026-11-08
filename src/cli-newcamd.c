#include "newcamd-keepalive.h"

static char string_newcamd[] = "Newcamd";
static char string_mgcamd[] = "Mgcamd";

static char string_newcamd_mcs[] = "Newcamd-MCS";
static char string_mgcamd_mcs[] = "Mgcamd-MCS";


///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

int cs_connect_srv(struct server_data *srv, int fd)
{
	char passwdcrypt[120];
	unsigned char keymod[14];
	int i,index,len;
	unsigned char buf[CWS_NETMSGSIZE];
	unsigned char sessionkey[16];
	// INIT
	pthread_mutex_lock(&prg.locksrv);
	srv->progname = NULL;
	memset( srv->version, 0, sizeof(srv->version) );
	memset( srv->build, 0, 32);
	pthread_mutex_unlock(&prg.locksrv);
	//
	if( recv_nonb(fd, keymod, 14,5000) != 14 ) {
		static char msg[]= "Server does not return init sequence";
		server_set_statmsg(srv, msg);
		debugf(getdbgflag(DBG_SERVER,0,srv->id)," newcamd: server does not return init sequence\n");
		return -1;
	}
	// Check Multics
	int ismultics = 0;

	uint8_t a = (keymod[0]^'M') + keymod[1] + keymod[2];
	uint8_t b = keymod[4] + (keymod[5]^'C') + keymod[6];
	uint8_t c = keymod[8] + keymod[9] + (keymod[10]^'S');
	if ( (a==keymod[3])&&(b==keymod[7])&&(c==keymod[11]) ) {
		ismultics = 1;
	}

	//debugdump(keymod,14,"Recv DES Key: ");
	des_login_key_get(keymod, srv->key, 14, sessionkey);  
	//debugdump(sessionkey,16,"Login Key: ");

	// 3. Send login info
	struct cs_custom_data clicd; // Custom data
	memset( &clicd, 0, sizeof(clicd));
	//clicd.sid =  0x4343; // CCcam
	clicd.sid =  cfg.newcamd.clientid; // Mgcamd

	index = 3;
	buf[0] = MSG_CLIENT_2_SERVER_LOGIN;
	buf[1] = 0;
	strcpy( (char*)&buf[3], srv->user);
	index += strlen(srv->user)+1;

	__md5_crypt(srv->pass, "$1$abcdefgh$",passwdcrypt);
	//debugf(getdbgflag(DBG_SERVER,0,srv->id)," passwdcrypt = %s\n",passwdcrypt);
	strcpy((char*)buf+index, (char*)passwdcrypt);
	index+=strlen(passwdcrypt)+1;
	if (ismultics) clicd.provid=0x0057484F;
	if ( !cs_message_send(fd, &clicd, buf, index, sessionkey) ) return -1;
	srv->ping = GetTickCount();
	// 3.1 Get login answer

	len = cs_message_receive(fd, &clicd, buf, sessionkey,15000);
	if (len<3) {
		static char msg[]= "Login error";
		server_set_statmsg(srv, msg);
		debugf(getdbgflag(DBG_SERVER,0,srv->id)," newcamd: login answer length error (%d) from server (%s:%d)\n",len, srv->host->name,srv->port);
		return INVALID_SOCKET;
	}
	if ( buf[0] == MSG_CLIENT_2_SERVER_LOGIN_NAK ) {
		static char msg[]= "Invalid user/pass";
		server_set_statmsg(srv, msg);
		debugf(getdbgflag(DBG_SERVER,0,srv->id)," newcamd: login failed to server (%s:%d)\n", srv->host->name,srv->port);
		return -1;
	}
	else if( buf[0] != MSG_CLIENT_2_SERVER_LOGIN_ACK ) {
		static char msg[]= "Login error";
		server_set_statmsg(srv, msg);
		debugf(getdbgflag(DBG_SERVER,0,srv->id)," newcamd: Error, expected MSG_CLIENT_2_SERVER_LOGIN_ACK\n");
		return -1;
	}
	else debugf(getdbgflag(DBG_SERVER,0,srv->id)," newcamd: connect to server (%s:%d) user '%s'\n", srv->host->name, srv->port, srv->user);
	// Ping
	srv->ping = GetTickCount()-srv->ping;
	// mgcamd protocol version?
	pthread_mutex_lock(&prg.locksrv);
	if (clicd.sid==0x6E73) { //&&( (clicd.provid>>24)==0x14) ) {
		if (ismultics) srv->progname = string_mgcamd_mcs;
		else srv->progname = string_mgcamd;
	}
	else if (clicd.provid==0x004D4353) {
		sprintf( srv->version, "r%d", clicd.sid);
		srv->progname = string_newcamd_mcs;
	}
	else srv->progname = string_newcamd;
	pthread_mutex_unlock(&prg.locksrv);
	//
	des_login_key_get( srv->key, (uint8_t*)passwdcrypt, strlen(passwdcrypt), sessionkey);
//debugdump(sessionkey,16,"sessionkey: ");
	memcpy( srv->sessionkey, sessionkey,16);

	// 4. Send MSG_CARD_DATA_REQ
	memset( &clicd, 0, sizeof(clicd) );
	clicd.msgid = 1;
	buf[0]=MSG_CARD_DATA_REQ;
	buf[1]=0; buf[2]=0;
	if ( !cs_message_send(fd, &clicd, buf, 3, srv->sessionkey) ) return -1;
	len = cs_message_receive( fd, NULL, buf, srv->sessionkey,5000);
	if (len==0) {
		static char msg[]= "Disconnected";
		server_set_statmsg(srv, msg);
		debugf(getdbgflag(DBG_SERVER,0,srv->id)," newcamd: client disconnected\n");
		return INVALID_SOCKET;
	}
	else if (len<0) {
		static char msg[]= "failed to receive card data";
		server_set_statmsg(srv, msg);
		debugf(getdbgflag(DBG_SERVER,0,srv->id)," newcamd: Error Recv MSG_CARD_DATA (%d)\n",len);
		return INVALID_SOCKET;
	}
	if (buf[0]!=MSG_CARD_DATA) {
		static char msg[]= "failed to receive card data";
		server_set_statmsg(srv, msg);
		debugf(getdbgflag(DBG_SERVER,0,srv->id)," newcamd: expected MSG_CARD_DATA\n");
		return INVALID_SOCKET;
	}

	else if (len<15) {
		debugf(getdbgflag(DBG_SERVER,0,srv->id)," newcamd: MSG_CARD_DATA, length error (%d)\n",len);
		//return INVALID_SOCKET;
	}
	else {
		// 5. Parse CAID and PROVID(s)
		if ( (buf[14]<CARD_MAXPROV)&&(len>=(6+11*buf[14]))&&(buf[4]||buf[5]) ) { // CAID != 0x0000
			struct cs_card_data *pcard = malloc( sizeof(struct cs_card_data) );
			if (pcard) {
				memset(pcard, 0, sizeof(struct cs_card_data) );
				pcard->caid = ((buf[4]<<8) | buf[5]);
				pcard->nbprov = buf[14];
				pcard->uphops = 1;
				//pcard->sids = NULL;
				if (pcard->nbprov>CARD_MAXPROV) pcard->nbprov = CARD_MAXPROV;
				debugf(getdbgflag(DBG_SERVER,0,srv->id)," newcamd: caid %04x providers %d\n", pcard->caid,buf[14]);
				for( i=0; i<pcard->nbprov; i++ ) {
					pcard->prov[i] = (buf[15+11*i]<<16)|(buf[16+11*i]<<8)|buf[17+11*i];
					//debugf(getdbgflag(DBG_SERVER,0,srv->id)," newcamd: provider %02d = %06X\n", i+1, pcard->prov[i]);
				}
				pthread_mutex_lock(&prg.locksrv);
				pcard->next = srv->card;
				srv->card = pcard;
				pthread_mutex_unlock(&prg.locksrv);
			}
			else printf(" newcamd: failed to allocate memory\n");
		} else debugf(getdbgflag(DBG_SERVER,0,srv->id)," newcamd: caid 0\n");
	}

#ifdef CLI_CSCACHE
/*	// Send keepalive Caching Check???
	clicd.msgid = 0;
	clicd.sid = ('C'<<8) | 'H';
	clicd.caid = 0;
	clicd.provid = 0;
	buf[0] = MSG_KEEPALIVE;
	buf[1] = 0;
	buf[2] = 0;
	cs_message_send(fd, &clicd, buf, 3, srv->sessionkey);
*/
#endif
	// Update Server data atomically with respect to HTTP and the event loop.
	static char msg[]= "Connected";
	pthread_mutex_lock(&prg.locksrv);
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
#ifdef CLI_CSCACHE
	srv->cscached = 0;
#endif
	srv->handle = fd;
	pthread_mutex_unlock(&prg.locksrv);
	server_publish_connection_ready(srv, fd);
	return 0;
}


///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

int cs_sendecm_srv(struct cardserver_data *cs, struct server_data *srv, ECM_DATA *ecm)
{
	unsigned char buf[CWS_NETMSGSIZE];
	struct cs_custom_data srvcd; // Custom data

	srv->ecm.msgid++;
	if (srv->ecm.msgid>0xfff) srv->ecm.msgid = 1;
	srvcd.msgid = srv->ecm.msgid;
	srvcd.sid = ecm->sid;
	srvcd.caid = ecm->caid;
	srvcd.provid = ecm->provid;

	memcpy( &buf[0], &ecm->ecm[0], ecm->ecmlen );
	return cs_message_send(  srv->handle, &srvcd, buf, ecm->ecmlen, srv->sessionkey);
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

void cs_srv_recvmsg(struct server_data *srv)
{
	struct cardserver_data *cs;
	struct cs_custom_data srvcd; // Custom data
	int len;
	ECM_DATA *ecm;
	unsigned char buf[CWS_NETMSGSIZE];

	if ( (srv->handle<=0)||(srv->type!=TYPE_NEWCAMD) ) return;

	len = cs_peekmsg( srv->handle, &srv->msg, srv->sessionkey, &srvcd, buf );
	if (len==0) {
		debugf(getdbgflag(DBG_SERVER,0,srv->id)," newcamd: server (%s:%d) read failed %d\n", srv->host->name, srv->port, len);
		disconnect_srv(srv);
	}
	else if (len==-1) {
		if (!srv->chkrecvtime) srv->chkrecvtime = GetTickCount();
		else if (tick32_elapsed(GetTickCount(), srv->chkrecvtime) > 300u) {
			debugf(getdbgflag(DBG_SERVER,0,srv->id)," newcamd: server (%s:%d) wrong data\n", srv->host->name, srv->port);
			disconnect_srv(srv);
		}
	}
	else if (len>0) {
		srv->chkrecvtime = 0;
			switch ( buf[0] ) {
				case 0x80:
				case 0x81:
					srv->lastdcwtime = GetTickCount();
					if (!srv->busy) {
						debugf(getdbgflag(DBG_SERVER,0,srv->id)," [!] dcw error from server (%s:%d), unknown ecm request\n",srv->host->name,srv->port);
						break;
					}
					ecm_lock(); //### Lock before checking the request pointer/profile.
					if (srvcd.msgid!=srv->ecm.msgid) {
						debugf(getdbgflag(DBG_SERVER,0,srv->id)," [!] dcw error from server (%s:%d), wrong message-id!!!\n",srv->host->name,srv->port);
						ecm_unlock();
						break;
					}
					ecm = srv->ecm.request;
					if (!ecm) {
						debugf(getdbgflag(DBG_SERVER,0,srv->id)," [!] dcw error from server (%s:%d), ecm not found!!!\n",srv->host->name,srv->port);
						ecm_unlock(); //###
						break;
					}
					if (!ecm_request_lifetime_matches(ecm, srv->ecm.request, srv->ecm.lifetime_id) ||
					    ecm->hash != srv->ecm.hash) {
						debugf(getdbgflag(DBG_SERVER,0,srv->id)," [!] stale Newcamd server response, request lifetime changed\n");
						ecm_unlock();
						break;
					}
					srv->busy = 0;
					pipe_cmd( prg.pipe.ecm[1], PIPE_SRV_AVAILABLE );
					if (user_trace_same_instance(srv->ecm.trace_instance_at_send, ecm->trace_instance)) {
						const char *detail;
						if (buf[2] != 0x10) detail = "Newcamd negative response; retry may follow";
						else if (ishalfnulledcw(buf + 3)) detail = "Newcamd CW response; zero half present; validity not determined";
						else detail = "Newcamd CW response; validity not determined";
						char timed_detail[128];
						if (srv->lastecmtime)
							snprintf(timed_detail, sizeof(timed_detail), "%s; source response %u ms", detail,
							         (unsigned)(GetTickCount() - srv->lastecmtime));
						else
							snprintf(timed_detail, sizeof(timed_detail), "%s; source time unavailable", detail);
						if (buf[2] == 0x10)
							user_trace_shared_cw(ecm->trace_instance, TYPE_NEWCAMD, srv->id,
							                     GetTickCount()-ecm->recvtime, timed_detail, buf+3);
						else
							user_trace_shared_event(ecm->trace_instance, USER_TRACE_SOURCE_REPLY,
							                        TYPE_NEWCAMD, srv->id, GetTickCount()-ecm->recvtime, timed_detail);
					}
					if (buf[2]==0x10) {
						// Check for DCW
						int cw_reason = DCW_VALID, checksum_exemption_used = 0;
						if (!acceptDCW_profile_ecm_trace_reason(buf+3, ecm->cs, ecm->caid, ecm->provid, ecm->sid, &cw_reason, &checksum_exemption_used)) {
							const char *why = "Newcamd CW rejected by initial validation; reason unknown";
							switch (cw_reason) {
							case DCW_INVALID_CHECKSUM: why = "Newcamd CW rejected: checksum validation failed"; break;
							case DCW_INVALID_NULL: why = "Newcamd CW rejected: null-word validation failed"; break;
							case DCW_INVALID_REPEAT: why = "Newcamd CW rejected: repeated-byte validation failed"; break;
							case DCW_INVALID_BADLIST: why = "Newcamd CW rejected: operator BAD-DCW list"; break;
							case DCW_INVALID_PATTERN: why = "Newcamd CW rejected: enabled pattern validation"; break;
							}
							if (user_trace_same_instance(srv->ecm.trace_instance_at_send, ecm->trace_instance)) user_trace_shared_event(ecm->trace_instance, USER_TRACE_REJECTED,
							    TYPE_NEWCAMD, srv->id, GetTickCount()-ecm->recvtime, why);
							srv->ecmerrdcw ++;
							ecm_unlock(); //###
							break;
						}
						if (user_trace_same_instance(srv->ecm.trace_instance_at_send, ecm->trace_instance)) user_trace_shared_event(ecm->trace_instance, USER_TRACE_VALIDATED,
						    TYPE_NEWCAMD, srv->id, GetTickCount()-ecm->recvtime,
						    checksum_exemption_used
						        ? "Newcamd CW passed initial checks under scoped checksum exemption; validity and picture unknown"
						        : "Newcamd CW passed initial validation; further gates remain");
						srv->ecmok++;
						srv->lastecmoktime = GetTickCount()-srv->lastecmtime;
						srv->ecmoktime += srv->lastecmoktime;

						/*
						 * TASK 2.3 -- the latency this source just took, offered to the
						 * forensics layer. This is the ONLY call site, and that is the
						 * point: a cache hit is instant by design (GR5) and a
						 * client-pushed key was never produced by a card at all, so the
						 * measurement is meaningful for card servers and nowhere else.
						 * Keeping the hook here makes "Newcamd only" structural rather
						 * than a rule someone has to remember when they add the next
						 * protocol -- the same reasoning as TASK 1.3's, in reverse.
						 *
						 * `lastecmtime == 0` means no request was ever sent on this
						 * slot (it is cleared in the connect path, cli-newcamd.c:175),
						 * so there is no measured interval to judge. A reply with no
						 * request behind it is not a latency, it is a bug, and giving
						 * the window an invented number for it would corrupt every
						 * later verdict about that source.
						 */
						if (srv->lastecmtime)
							cwtime_note(srv->id, srv->lastecmoktime,
							            ecm->caid, ecm->provid, ecm->provider_known, ecm->sid);

						ecm_setsrvflagdcw( ecm, srv->id, ECM_SRV_REPLY_GOOD,buf+3 );
						uint32_t response_time = GetTickCount()-srv->lastecmtime;
						debugf(getdbgflagpro(DBG_SERVER,0,srv->id,(ecm->cs ? ecm->cs->id : 0))," <= cw from server (%s:%d) ch %04x:%06x:%04x (%dms)\n", srv->host->name,srv->port, ecm->caid,ecm->provid,ecm->sid, response_time);

						if (ecm->dcwstatus!=STAT_DCW_SUCCESS) {
							static char msg[] = "Good dcw from Newcamd server";
							ecm->statusmsg = msg;
							/* CAID-specific performance tracking: record success */
							caid_integration_track_performance(ecm->caid, response_time, 1);
							// Store ECM Answer
							ecm_setdcw( ecm, buf+3, DCW_SOURCE_SERVER, srv->id );
						}
						else {	//TODO: check same dcw between cards
							srv->ecmerrdcw ++;
							if ( memcmp(ecm->cw, buf+3, 16) ) debugf(getdbgflagpro(DBG_SERVER,0,srv->id,(ecm->cs ? ecm->cs->id : 0))," !!! different dcw from server (%s:%d)\n",srv->host->name,srv->port);
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
					}
					else {
					cs= ecm->cs;
					if ( cs && (ecm->dcwstatus!=STAT_DCW_SUCCESS) && (srv->retry<cs->option.retry.newcamd) ) {
						uint32_t caid_timeout = caid_integration_get_server_timeout(ecm->caid);
						if ( tick32_elapsed(GetTickCount(), ecm->recvtime) < (uint32_t)(caid_timeout*ecm->period) ) {
								srv->busy = 0;
								if (cs_sendecm_srv(cs, srv, ecm)>0) {
									srv->retry++;
									ecm->lastsendtime = GetTickCount();
									debugf(getdbgflagpro(DBG_SERVER,0,srv->id,(ecm->cs ? ecm->cs->id : 0))," (RE%d) -> ecm to server (%s:%d) ch %04x:%06x:%04x\n",srv->retry,srv->host->name,srv->port,ecm->caid,ecm->provid,ecm->sid);
									srv->lastecmtime = GetTickCount();
									srv->ecmnb++;
									srv->busy = 1;
									if (user_trace_same_instance(srv->ecm.trace_instance_at_send, ecm->trace_instance)) user_trace_shared_event(ecm->trace_instance, USER_TRACE_SENT,
									    TYPE_NEWCAMD, srv->id, GetTickCount()-ecm->recvtime,
									    "Newcamd retry send reported success");
									srv->ecm.request = ecm;
									srv->ecm.lifetime_id = ecm_request_lifetime_id_load(ecm);
									srv->ecm.hash = ecm->hash;
									srv->ecm.trace_instance_at_send = ecm->trace_instance;
									ecm_unlock(); //###
									break;
								}
							}
						}

						ecm_setsrvflag(ecm, srv->id, ECM_SRV_REPLY_FAIL);
						debugf(getdbgflagpro(DBG_SERVER,0,srv->id,(ecm->cs ? ecm->cs->id : 0))," <| decode failed from server (%s:%d) ch %04x:%06x:%04x (%dms)\n", srv->host->name,srv->port, ecm->caid,ecm->provid,ecm->sid, GetTickCount()-srv->lastecmtime);
#ifdef SID_FILTER
						// ADD IN SID LIST
						if (cs) {
							cardsids_update( srv->busycard, ecm->provid, ecm->sid, -1);
							srv_cstatadd( srv, cs->id, 0 , 0);
						}
#endif
						ecm_unlock(); //###
						wakeup_sendecm(); // Wakeup ecm waiting for availabe servers
					}
					break;

				// ADD CARD
//	MSG_SERVER_2_CLIENT_REMOVECARD = 0xd4,
//	MSG_SERVER_2_CLIENT_ADDCARD = 0xd3,
				case 0xD3:
					if (srvcd.caid) { // CAID != 0x0000
						struct cs_card_data tcard;
						memset(&tcard, 0, sizeof(struct cs_card_data) );
						tcard.caid = srvcd.caid;
						tcard.nbprov = 1;
						tcard.prov[0] = srvcd.provid;
						tcard.uphops = 1;
						if ( tcard.uphops <= srv_sharelimits( srv, tcard.caid, srvcd.provid) ) {
							debugf(getdbgflag(DBG_SERVER,0,srv->id)," Mgcamd: new card (%s:%d) caid %04x provider %06X\n", srv->host->name,srv->port, tcard.caid, tcard.prov[0]);
							//debugf(getdbgflag(DBG_SERVER,0,srv->id)," Accepted %04x:%06x\n", tcard.caid, srvcd.provid);
							struct cs_card_data *card = malloc( sizeof(struct cs_card_data) );
							memcpy( card, &tcard, sizeof(struct cs_card_data) );
							pthread_mutex_lock(&srv->lock); //###
							card->next = srv->card;
							srv->card = card;
							pthread_mutex_unlock(&srv->lock); //###
						}
						//else debugf(getdbgflag(DBG_SERVER,0,srv->id)," Ignored %04x:%06x\n", tcard.caid, srvcd.provid);
					}
					break;

				case 0xD4: // Delete Card
					ecm_lock(); //###
					struct cs_card_data *card = srv->card;
					struct cs_card_data *prevcard = NULL;
					while (card) {
						if ( (card->caid==srvcd.caid)&&(card->prov[0]==srvcd.provid) ) {
							debugf( getdbgflag(DBG_SERVER, 0, srv->id), " Mgcamd: server (%s:%d), remove card %04x:%06x\n",srv->host->name,srv->port, srvcd.caid, srvcd.provid);
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
							/////XXX if (srv->busy && (srv->busycardid==k) ) ecm_setsrvflag(srv->ecm.request, srv->id, ECM_SRV_EXCLUDE);
							break;
						}
						prevcard = card;
						card = card->next;
					}
					ecm_unlock(); //###
			  		break;

				default:
					if (buf[0]==MSG_KEEPALIVE) {
						if (srv->keepalive.status) {
#ifdef CLI_CSCACHE
							if ( ( srvcd.sid==(('C'<<8)|'H') ) && ( srvcd.caid==(('O'<<8)|'K') ) ) srv->cscached = 1;
#endif
							srv->ping = GetTickCount() - srv->keepalive.time;
							//debugf(getdbgflag(DBG_SERVER,0,srv->id)," <- keepalive from server (%s:%d) ping %dms\n",srv->host->name,srv->port,srv->ping);
							srv->keepalive.status = 0;
						}
						//else debugf(getdbgflag(DBG_SERVER,0,srv->id)," <- Error keepalive from server (%s:%d)\n",srv->host->name,srv->port);
					} else {
						debugf(getdbgflag(DBG_SERVER,0,srv->id)," unknown message type '%02x' CAID:%04X PROVID:%06X from server (%s:%d)\n",buf[0],srvcd.caid,srvcd.provid,srv->host->name,srv->port);
					}
			}
			srv->keepalive.time = GetTickCount();
	}
}


void cs_check_keepalive(struct server_data *srv)
{
	struct cs_custom_data clicd; // Custom data
	unsigned char buf[CWS_NETMSGSIZE];
	uint32_t ticks;

	if (!srv || srv->handle <= 0 || srv->type != TYPE_NEWCAMD) return;
	ticks = GetTickCount();

	// Check for sending keep alive
	if (!srv->keepalive.status) {
		if (tick32_elapsed(ticks, srv->keepalive.time) >
		    (uint32_t)(KEEPALIVE_NEWCAMD*1000u)) {
			buf[0] = MSG_KEEPALIVE;
			buf[1] = 0;
			buf[2] = 0;
			//debugf(getdbgflag(DBG_SERVER,0,srv->id)," -> keepalive to server (%s:%d)\n",srv->host->name,srv->port);
			if (!cs_message_send(srv->handle, NULL, buf, 3, srv->sessionkey)) {
				disconnect_srv(srv);
			} else {
				srv->keepalive.time = ticks;
				srv->keepalive.status = 1;
			}
		}
	} else if (newcamd_keepalive_reply_timed_out(ticks, srv->keepalive.time)) {
#ifdef CLI_CSCACHE
		// Retry the keepalive using the cache-check marker supported by peers.
		clicd.msgid = 0;
		clicd.sid = ('C'<<8) | 'H';
		clicd.caid = 0;
		clicd.provid = 0;
		buf[0] = MSG_KEEPALIVE;
		buf[1] = 0;
		buf[2] = 0;
		if (!cs_message_send(srv->handle, &clicd, buf, 3, srv->sessionkey)) {
			disconnect_srv(srv);
		} else {
			/* status remains a pending-response flag; time is the deadline base. */
			srv->keepalive.time = ticks;
			srv->keepalive.status = 1;
		}
#else
		debugf(getdbgflag(DBG_SERVER,0,srv->id)," ??? no keepalive response from server (%s:%d)\n",srv->host->name,srv->port);
		srv->keepalive.status = 0;
#endif
	}
}


