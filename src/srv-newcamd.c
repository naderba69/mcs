/////
// File: srv-newcamd.c
// Modification date: 2011.11.11
/////

///////////////////////////////////////////////////////////////////////////////
// PROTO
///////////////////////////////////////////////////////////////////////////////

void cs_disconnect_cli(struct cs_client_data *cli);
void cs_senddcw_cli(struct cs_client_data *cli);


struct cs_client_data *getnewcamdclientbyid(uint32_t id)
{
	struct cardserver_data *cs = cfg.cardserver;
	while (cs) {
		if (!(cs->flags&FLAG_DELETE)) {
			struct cs_client_data *cli = cs->newcamd.client;
			while (cli) {
				if (!(cli->flags&FLAG_DELETE))
					if (cli->id==id) return cli;
				cli = cli->next;
			}
		}
		cs = cs->next;
	}
	return NULL;
}


///////////////////////////////////////////////////////////////////////////////

void cs_disconnect_cli(struct cs_client_data *cli)
{
	cli->connection.status = 0;
	uint32_t ticks = GetTickCount();
	cli->connection.uptime += ticks - cli->connection.time;
	cli->connection.lastseen = ticks; // Last Seen
	close(cli->handle);
	cli->handle = -1;
	debugf(getdbgflag(DBG_NEWCAMD,cli->pid,cli->id)," newcamd: client '%s' disconnected\n", cli->user);
}


void *cs_connect_cli(struct connect_cli_data *param)
{
    char passwdcrypt[120];
	unsigned char keymod[14];
	int i,index;
	unsigned char sessionkey[16];
	struct cs_custom_data clicd;
	unsigned char buf[CWS_NETMSGSIZE];

	struct cardserver_data *cs = param->server;
	int sock = param->sock;
	uint32_t ip = param->ip;
	free(param);
	// Create random deskey
	for (i=0; i<14; i++) keymod[i] = 0xff & rand();
	// Create Multics ID
	keymod[3] = (keymod[0]^'M') + keymod[1] + keymod[2];
	keymod[7] = keymod[4] + (keymod[5]^'C') + keymod[6];
	keymod[11] = keymod[8] + keymod[9] + (keymod[10]^'S');

//STAT: SEND RND KEY
	// send random des key

	if ( !send_nonb(sock, keymod, 14, 500) ) {
		debugf(getdbgflag(DBG_NEWCAMD,cs->id,0)," newcamd: error sending init sequence\n");
		close(sock);
		return NULL;
	}

	uint32_t ticks = GetTickCount();
	// Calc SessionKey
	//debugf(getdbgflag(DBG_NEWCAMD,cs->id,0)," DES Key: "); debughex(keymod,14);
	des_login_key_get(keymod, cs->newcamd.key, 14, sessionkey);
	//debugf(getdbgflag(DBG_NEWCAMD,cs->id,0)," Login Key: "); debughex(sessionkey,16);

//STAT: LOGIN INFO
	// 3. login info
	i = cs_message_receive(sock, &clicd, buf, sessionkey,3000);
	if (i<=0) {
		if (i==-2) debugf(getdbgflag(DBG_NEWCAMD,cs->id,0)," %s: (%s) new connection closed, wrong des key\n", cs->name, ip2string(ip));
		else debugf(getdbgflag(DBG_NEWCAMD,cs->id,0)," %s: (%s) new connection closed, receive timeout\n", cs->name, ip2string(ip));
		close(sock);
		return NULL;
	}

	ticks = GetTickCount()-ticks;
	if (buf[0]!=MSG_CLIENT_2_SERVER_LOGIN) {
		close(sock);
		return NULL;
	}

	// Check username length
	if ( strlen( (char*)buf+3 )>63 ) {
		/*
		buf[0] = MSG_CLIENT_2_SERVER_LOGIN_NAK;
		buf[1] = 0;
		buf[2] = 0;
		cs_message_send(sock, NULL, buf, 3, sessionkey);
		*/
		debugf(getdbgflag(DBG_NEWCAMD,cs->id,0)," %s: (%s) new connection closed, wrong username length\n", cs->name, ip2string(ip));
		close(sock);
		return NULL;
	}

	// test username
	for(i=3; i<(3+64); i++) {
		if (buf[i]==0) break;
		if (buf[i]<=32) { // bad username
			close(sock);
			return NULL;
		}
	}

	pthread_mutex_lock(&prg.lockcli);
	index = 3;
	struct cs_client_data *cli = cs->newcamd.client;
	int found = 0;
	char *name = (char*)(buf+index);
	uint32_t hash = hashCode( (unsigned char *)name, strlen(name) );
	while (cli) {
		if (cli->userhash==hash)
		if (!strcmp(cli->user,name)) {
			if (IS_DISABLED(cli->flags)) { // Connect only enabled clients
				pthread_mutex_unlock(&prg.lockcli);
				debugf(getdbgflag(DBG_NEWCAMD,cli->pid,cli->id)," %s: connection refused for client '%s' (%s), client disabled\n", cs->name, cli->user, ip2string(ip));
				close(sock);
				return NULL;
			}
			found=1;
			break;
		}
		cli = cli->next;
	}
	if (!found) {
		pthread_mutex_unlock(&prg.lockcli);
		/*
		buf[0] = MSG_CLIENT_2_SERVER_LOGIN_NAK;
		buf[1] = 0;
		buf[2] = 0;
		cs_message_send(sock, NULL, buf, 3, sessionkey); */
			/* TASK 3.3 -- the login frame names the user, so echo it clamped:
		 * the wire guard above keeps it <=63; this is belt-and-braces for
		 * any path that reaches the unknown-user line with a longer name. */
		{ char uname[80]; int ul = (int)strlen((char*)&buf[3]);
		  if (ul > 63) ul = 63;
		  memcpy(uname, &buf[3], ul); uname[ul] = 0;
		  debugf(getdbgflag(DBG_NEWCAMD,cs->id,0)," %s: unknown user '%s' (%s)\n", cs->name, uname, ip2string(ip)); }
		close(sock);
		return NULL;
	}

	// Check password
	index += strlen(cli->user) +1;
	__md5_crypt(cli->pass, "$1$abcdefgh$",passwdcrypt);
	if (!strcmp(passwdcrypt,(char*)&buf[index])) {
		if (cli->ip==ip) cli->nbdiffip++;
		//Check Reconnection
		if (cli->connection.status>0) {
			if ( (GetTickCount()-cli->connection.time) > 60000 ) {
				cs_disconnect_cli(cli);
				if (cli->ip==ip) debugf(getdbgflag(DBG_NEWCAMD,cli->pid,cli->id)," %s: Client '%s' (%s) already connected\n", cs->name,cli->user, ip2string(ip));
				else {
					pthread_mutex_unlock(&prg.lockcli);
					debugf(getdbgflag(DBG_NEWCAMD,cli->pid,cli->id)," %s: Client '%s' (%s) already connected with different ip, Connection closed (%s)\n", cs->name, cli->user, ip2string(cli->ip), ip2string(ip));
					cli->nbloginerror++;
					close(sock);
					return NULL;
				}
			}
			else {
				debugf(getdbgflag(DBG_NEWCAMD,cli->pid,cli->id)," %s: Client '%s' just connected, new connection closed (%s)\n", cs->name, cli->user, ip2string(ip));
				pthread_mutex_unlock(&prg.lockcli);
				cli->nbloginerror++;
				close(sock);
				return NULL;
			}
		}
		cli->nblogin++;
		// Store program id
		cli->progid = clicd.sid;
		//
		buf[0] = MSG_CLIENT_2_SERVER_LOGIN_ACK;
		buf[1] = 0;
		buf[2] = 0;
		// Send Multics Id&Version
		if (clicd.provid==0x0057484F) { // WHO 
			clicd.provid = 0x004D4353; // MCS
			clicd.sid = REVISION; // Revision
			cs_message_send(sock, &clicd, buf, 3, sessionkey);
		} else cs_message_send(sock, NULL, buf, 3, sessionkey);
		des_login_key_get( cs->newcamd.key, (unsigned char*)passwdcrypt, strlen(passwdcrypt),sessionkey);
		memcpy( &cli->sessionkey, &sessionkey, 16);

		// Setup User data
		cli->msg.len = 0;
		cli->handle = sock;
		cli->ip = ip;
		memset( &cli->ecm, 0, sizeof(cli->ecm) );
		cli->connection.status = 1;
		cli->connection.time = GetTickCount();
		cli->lastactivity = GetTickCount();
		cli->lastecmtime = 0;
		cli->chkrecvtime = 0;
		debugf(getdbgflag(DBG_NEWCAMD,cli->pid,cli->id)," %s: client '%s' connected (%s)\n", cs->name, cli->user, ip2string(ip));
		pthread_mutex_unlock(&prg.lockcli);
		cli->cs->newcamd.clipfd.update = 1;
#ifdef EPOLL_NEWCAMD
		pipe_pointer( prg.pipe.newcamd[1], PIPE_CLI_CONNECTED, cli );
#else
		pipe_wakeup( prg.pipe.newcamd[1] );
#endif
	}
	else {
		pthread_mutex_unlock(&prg.lockcli);
		// send NAK
		buf[0] = MSG_CLIENT_2_SERVER_LOGIN_NAK;
		buf[1] = 0;
		buf[2] = 0;
		//cs_message_send(sock, NULL, buf, 3, sessionkey);
		debugf(getdbgflag(DBG_NEWCAMD,cli->pid,cli->id)," %s: client '%s' wrong password (%s)\n", cs->name, cli->user, ip2string(ip));
		cli->nbloginerror++;
		close(sock);
	}
	return NULL;
}


void newcamd_srv_accept(struct cardserver_data *srv)
{
	struct sockaddr_in newaddr;
	socklen_t socklen = sizeof(struct sockaddr);
	int newfd = accept( srv->newcamd.handle, (struct sockaddr*)&newaddr, /*(socklen_t*)*/&socklen);
	if ( newfd<=0 ) {
		if ( (errno!=EAGAIN) && (errno!=EINTR) ) debugf( getdbgflag(DBG_NEWCAMD,srv->id,0)," [%s] Accept failed (errno=%d)\n", srv->name,errno);
	}
	else {
		SetSocketReuseAddr(newfd);
		uint32_t newip = newaddr.sin_addr.s_addr;
		if ( isblockedip(newip) ) {
			debugf(getdbgflag(DBG_NEWCAMD,srv->id,0)," [%s] New Connection (%s) closed, ip blocked\n", srv->name, ip2string(newip) );
			close(newfd);
		}
		else {
			pthread_t srv_tid;
			if (cfg.newcamd.keepalive) SetSocketKeepalive(newfd);
			SetSocketNoDelay(newfd);
			SetSoketNonBlocking(newfd);
			//debugf(getdbgflag(DBG_NEWCAMD,srv->id,0)," [%s] new connection (%s)\n", srv->name, ip2string(newip) );
			struct connect_cli_data *newdata = malloc( sizeof(struct connect_cli_data) );
			newdata->server = srv; 
			newdata->sock = newfd; 
			newdata->ip = newaddr.sin_addr.s_addr;
			if ( !create_thread(&srv_tid, (threadfn)cs_connect_cli,newdata) ) {
				free( newdata );
				close( newfd );
			}
		}
	}
}

#ifndef MONOTHREAD_ACCEPT

void *newcamd_accept_thread(void *param)
{
	sleep(5);

	while(!prg.restart) {

		struct pollfd pfd[256];
		int pfdcount = 0;

		struct cardserver_data *cs = cfg.cardserver;
		while(cs) {
			if ( cs->option.fsharenewcamd && !IS_DISABLED(cs->newcamd.flags) && (cs->newcamd.handle>0) ) {
				cs->newcamd.ipoll = pfdcount;
				pfd[pfdcount].fd = cs->newcamd.handle;
				pfd[pfdcount++].events = POLLIN | POLLPRI;
			} else cs->newcamd.ipoll = -1;
			cs = cs->next;
		}

		if (pfdcount) {
			int retval = poll(pfd, pfdcount, 3006);
			if ( retval>0 ) {
				struct cardserver_data *cs = cfg.cardserver;
				while (cs) {
					if ( cs->option.fsharenewcamd )
					if ( !IS_DISABLED(cs->newcamd.flags)&&(cs->newcamd.handle>0) && (cs->newcamd.ipoll>=0) && (cs->newcamd.handle==pfd[cs->newcamd.ipoll].fd) ) {
						if ( pfd[cs->newcamd.ipoll].revents & (POLLIN|POLLPRI) ) newcamd_srv_accept( cs );
					}
					cs = cs->next;
				}
			}
			else if (retval<0) usleep(96000);
		} else sleep(1);
	}
	return NULL;
}

#endif

///////////////////////////////////////////////////////////////////////////////
//
///////////////////////////////////////////////////////////////////////////////

void cs_store_ecmclient(struct cardserver_data *cs, ECM_DATA *ecm, struct cs_client_data *cli, int climsgid)
{
	cli->ecm.recvtime = GetTickCount();
	cli->ecm.request = ecm;
    cli->ecm.climsgid = climsgid;
    cli->ecm.status = STAT_ECM_SENT;
	ecm_addip(ecm, cli->ip);
}

int cs_cli_getmsg(struct cs_client_data *cli,struct cs_custom_data *clicd, uint8_t *buf)
{
	int len;
	if (cli->handle<=0) return 0; // disconnected

#ifdef RECVMSG_PEEK
		len = cs_msg_chkrecv(cli->handle);
		if (len==0) {
			debugf(getdbgflag(DBG_NEWCAMD,cli->pid,cli->id)," newcamd: client '%s' read failed %d\n", cli->user,len);
			cs_disconnect_cli(cli);
		}
		else if (len==-1) {
			if (!cli->chkrecvtime) cli->chkrecvtime = GetTickCount();
			else if ( (cli->chkrecvtime+300)<GetTickCount() ) {
				debugf(getdbgflag(DBG_NEWCAMD,cli->pid,cli->id)," newcamd: client '%s' read failed %d\n", cli->user,len);
				cs_disconnect_cli(cli);
			}
		}
		else if (len>0) {
			cli->chkrecvtime = 0;
			len = cs_message_receive(cli->handle, clicd, buf, cli->sessionkey,3);
			if (len==0) {
				debugf(getdbgflag(DBG_NEWCAMD,cli->pid,cli->id)," newcamd: client '%s' read failed %d\n", cli->user,len);
				cs_disconnect_cli(cli);
			}
			else if (len<0) {
				debugf(getdbgflag(DBG_NEWCAMD,cli->pid,cli->id)," newcamd: client '%s' read failed %d(%d)\n", cli->user,len,errno);
				cs_disconnect_cli(cli);
			}
			else return len;
		}
#else 
	len = cs_peekmsg( cli->handle, &cli->msg, cli->sessionkey, clicd, buf );
	if (len==0) {
		debugf(getdbgflag(DBG_NEWCAMD,cli->pid,cli->id)," newcamd: client '%s' read failed %d\n", cli->user,len);
		cs_disconnect_cli(cli);
	}
	else if (len==-1) {
		uint32_t ticks = GetTickCount();
		if (!cli->chkrecvtime) cli->chkrecvtime = ticks;
		else if ( (cli->chkrecvtime+300)<ticks ) {
			debugf(getdbgflag(DBG_NEWCAMD,cli->pid,cli->id)," newcamd: client '%s' read timeout %d\n", cli->user,len);
			cs_disconnect_cli(cli);
		}
	} else return len;
#endif

	return 0;
}

void *cs_cli_recvmsg(struct cs_client_data *cli)
{
	struct cardserver_data *cs = cli->cs;
	struct cs_custom_data clicd; // Custom data
	int len;
	unsigned char buf[CWS_NETMSGSIZE];
	unsigned char data[CWS_NETMSGSIZE]; // for other use

	if (cli->handle<=0) return NULL;
	len = cs_cli_getmsg(cli,&clicd,buf);
	if (len<=0) return NULL;

	uint32_t ticks = GetTickCount();
	cli->chkrecvtime = 0;
	cli->lastactivity = ticks;

	switch ( buf[0] ) {
		case MSG_CARD_DATA_REQ:
					memset( buf, 0, sizeof(buf) );
					buf[0] = MSG_CARD_DATA;
					if (!cli->card.caid) {
						buf[4] = cs->card.caid>>8;
						buf[5] = cs->card.caid&0xff;
						//buf[14] = cs->card.nbprov;
						int nbprov=0;
						for(len=0;len<cs->card.nbprov;len++) {
							buf[15+11*nbprov] = cs->card.prov[len].id>>16;
							buf[16+11*nbprov] = cs->card.prov[len].id>>8;
							buf[17+11*nbprov] = cs->card.prov[len].id&0xff;
							nbprov++;
						}
						buf[14] = nbprov;
						cs_message_send(cli->handle, &clicd, buf, 15+11*nbprov, cli->sessionkey);
					}
					else {
						buf[4] = cli->card.caid>>8;
						buf[5] = cli->card.caid&0xff;
						int nbprov=0;
						for(len=0;len<cli->card.nbprov;len++) {
							buf[15+11*nbprov] = cli->card.prov[len]>>16;
							buf[16+11*nbprov] = cli->card.prov[len]>>8;
							buf[17+11*nbprov] = cli->card.prov[len]&0xff;
							nbprov++;
						}
						buf[14] = nbprov;
						cs_message_send(cli->handle, &clicd, buf, 15+11*nbprov, cli->sessionkey);
					}
					//debugf(getdbgflag(DBG_NEWCAMD,cli->pid,cli->id)," newcamd: send card data to client '%s'\n", cli->user);
					break;
		case 0x80:
		case 0x81:
					//debugdump(buf, len, "ECM: ");
					cli->lastecmtime = ticks;
					cli->ecmnb++;
					memcpy( data, buf, len);
					uint32_t provid = ecm_getprovid( data, clicd.caid );
					if (provid!=0) clicd.provid = provid;

					if (cli->ecm.busy) {
						cli->ecmdenied++;
						// send decode failed
						buf[1] = 0; buf[2] = 0;
						if ( !cs_message_send( cli->handle, &clicd, buf, 3, cli->sessionkey) ) cs_disconnect_cli( cli );
						else debugf(getdbgflag(DBG_NEWCAMD,cli->pid,cli->id)," <|> decode failed to client '%s' ch %04x:%06x:%04x, too many ecm requests\n", cli->user,clicd.caid,clicd.provid,clicd.sid);
						break;
					}
					// CHECK FOR ECM
					uint8_t cw1cycle;
					char *error = cs_accept_ecm(cs,clicd.caid,clicd.provid,clicd.sid,ecm_getchid(data,clicd.caid), len, data, &cw1cycle);
					if (error) {
						cs->ecmdenied++;
						cli->ecmdenied++;
						buf[1] = 0; buf[2] = 0;
						if ( !cs_message_send( cli->handle, &clicd, buf, 3, cli->sessionkey) ) cs_disconnect_cli( cli );
						else debugf(getdbgflag(DBG_NEWCAMD,cli->pid,cli->id)," <|> decode failed to client '%s' ch %04x:%06x:%04x, %s\n",cli->user, clicd.caid,clicd.provid,clicd.sid, error);
						break;
					}

					clicd.caid = cs->card.caid; // if caid == 0

					// ACCEPTED
					pthread_mutex_lock(&prg.lockecm); //###

					// Search for ECM
					ECM_DATA *ecm = search_ecmdata_any(cs, data,  len, clicd.sid, clicd.caid); // dont get failed ecm request from cache
					int isnew =  ( ecm==NULL );

					/*
					 * TASK 1.2 — post-delivery retry classification.
					 *
					 * This is the one point all three ingress branches pass
					 * through, and it runs before `lastecm` is overwritten for
					 * the new request, so it sees the *previous* delivery. That
					 * is the whole observation: none of these protocols has a
					 * negative ACK, so the only evidence that a delivered CW did
					 * not decrypt is that the client came back for the same
					 * service far sooner than its crypto period.
					 *
					 * It uses only fields that already exist: `lastecm.hash` for
					 * the GR6 key, `lastecm.status` for the GR2 timeout test,
					 * `lastdcwtime` for the interval, and `lastecm.dcwsrctype/
					 * dcwsrcid` for the GR1 origin. No allocation and no layout
					 * change (GR9, D6).
					 *
					 * OBSERVE-ONLY in this task. It increments two counters and
					 * writes one debug line. It changes no score and disables
					 * nothing, so the detection can be checked against real
					 * traffic before TASK 1.4 is allowed to act on it.
					 */
					/*
					 * Two things gate this block, and both were learned the hard
					 * way:
					 *
					 * 1. `ecm` is NULL here on the first request of a profile,
					 *    because search_ecmdata_any() found nothing. Upstream does
					 *    not touch `ecm` until its own `if (ecm)` guard further
					 *    down; nothing inserted above that guard may touch it
					 *    either. The original condition dereferenced `ecm->hash`
					 *    here and segfaulted on the very first ECM.
					 *
					 * 2. A channel change is not a retry. A viewer who zaps 2 s
					 *    after a perfectly good key would otherwise be read as a
					 *    client re-asking a failed one, and the source that supplied
					 *    the good key would be scored down for it. That is the false
					 *    positive GR3 forbids, so the block only runs when this
					 *    request is provably the same channel as the last delivery.
					 *    The caid/provid/sid test is upstream's own convention for
					 *    "same channel" (see the freeze check below); GR6 is still
					 *    met because the evidence stores are keyed on the hash and
					 *    on caid/provid/sid, never on sid alone.
					 *
					 * Cost of the gate: a zap produces no GOOD event for the
					 * channel that was left. Under-reporting GOOD in that one case
					 * is accepted; mis-scoring a working source is not.
					 *
					 * A zero `lastecm` means "nothing ever delivered" -- client
					 * nodes are memset at creation (config.c) -- and then
					 * retry_classify() returns NONE on GR2 and the GOOD branch is
					 * closed by its own `lastecm.status` test.
					 */
					int samechan = ( cli->lastecm.caid==clicd.caid )
								&& ( cli->lastecm.prov==clicd.provid )
								&& ( cli->lastecm.sid ==clicd.sid );
					if ( samechan ) {
						int rcls = retry_classify( ticks, cli->lastdcwtime,
							cli->lastecm.status, retry_window_ms, soft_fail_window_ms );
						if (rcls==RETRY_HARD) {
							cli->retryhard++;
							debugf(getdbgflag(DBG_NEWCAMD,cli->pid,cli->id)," !~> client '%s' re-asked ch %04x:%06x:%04x:%08x %d ms after a cw from source %d/%d (retry)\n", cli->user, clicd.caid,clicd.provid,clicd.sid, cli->lastecm.hash, (int)(ticks-cli->lastdcwtime), cli->lastecm.dcwsrctype, cli->lastecm.dcwsrcid);
						}
						else if (rcls==RETRY_SOFT) {
							cli->retrysoft++;
							debugf(getdbgflag(DBG_NEWCAMD,cli->pid,cli->id)," !?> client '%s' re-asked ch %04x:%06x:%04x:%08x %d ms after a cw from source %d/%d (soft)\n", cli->user, clicd.caid,clicd.provid,clicd.sid, cli->lastecm.hash, (int)(ticks-cli->lastdcwtime), cli->lastecm.dcwsrctype, cli->lastecm.dcwsrcid);
						}
						/*
						 * TASK 1.4 — feed the trust engine from the same
						 * classification, so the score and the counter cannot
						 * drift apart.
						 *
						 * The source scored is the one recorded at delivery
						 * (`lastecm.dcwsrctype/dcwsrcid`), never whichever source
						 * is current, which is the whole of GR1. A retry is
						 * TRUST_EV_SOFT and never TRUST_EV_PROOF: it is inferred
						 * from client behaviour, so GR3 forbids treating it as
						 * evidence that proves anything.
						 *
						 * The GOOD event is the other half. Without it every score
						 * could only fall and the table would fill with sources
						 * drifting to the floor regardless of how well they
						 * performed. It is emitted only when a CW really was
						 * delivered (RETRY_NONE with a nonzero status), so a timeout
						 * is not credited to anybody -- which is GR2.
						 */
						/*
						 * TASK 2.1 -- ask the agreement ledger first.
						 *
						 * A retry says a client came back; it does not say the key was
						 * wrong. The ledger answers the second question: if an
						 * independent source produced a DIFFERENT key for this same
						 * ECM, then one of the two keys was wrong, and the one the
						 * client was holding is the one that failed. That is GR3's
						 * third definitive proof, and it is the only thing in this
						 * project allowed to act on a single event.
						 *
						 * It is called with `ecm ?` because search_ecmdata_any() can
						 * legitimately return NULL here and the first version of this
						 * block segfaulted on exactly that dereference (see the note
						 * above). No ecmd5 means no identity, and no identity means
						 * no verdict -- the ledger refuses rather than guesses.
						 *
						 * Only RETRY_HARD is offered. A soft retry is inference on
						 * top of inference and GR3 does not allow a hard action on it.
						 */
						int led_proof = 0;
#ifdef CACHEEX
						if (rcls==RETRY_HARD)
							led_proof = ledger_note_failure(
								ecm?ecm->ecmd5:0, cli->lastecm.hash,
								cli->lastecm.caid, cli->lastecm.prov, cli->lastecm.sid,
								cli->lastecm.dcw,
								cli->lastecm.dcwsrctype, cli->lastecm.dcwsrcid, ticks);
#endif
						if (led_proof) {
							/*
							 * The same observation, graded as what the ledger made
							 * of it. Emitting SOFT and PROOF for one event would
							 * count it twice and decay the score as if the source
							 * had failed twice.
							 */
							trust_record_lk( &trust_tab, TRUST_EV_PROOF,
								cli->lastecm.dcwsrctype, cli->lastecm.dcwsrcid,
								cli->lastecm.caid, cli->lastecm.prov, cli->lastecm.sid, ticks );
							/* TASK 2.8: the same event, one level coarser. */
							trustagg_note_lk( TRUST_EV_PROOF,
								cli->lastecm.dcwsrctype, cli->lastecm.dcwsrcid,
								cli->lastecm.caid, ticks );
						}
						else if (rcls==RETRY_HARD || rcls==RETRY_SOFT) {
							trust_record_lk( &trust_tab, TRUST_EV_SOFT,
								cli->lastecm.dcwsrctype, cli->lastecm.dcwsrcid,
								cli->lastecm.caid, cli->lastecm.prov, cli->lastecm.sid, ticks );
							trustagg_note_lk( TRUST_EV_SOFT,
								cli->lastecm.dcwsrctype, cli->lastecm.dcwsrcid,
								cli->lastecm.caid, ticks );
						}
						else if (cli->lastecm.status && cli->lastecm.dcwsrctype!=DCW_SOURCE_NONE) {
							trust_record_lk( &trust_tab, TRUST_EV_GOOD,
								cli->lastecm.dcwsrctype, cli->lastecm.dcwsrcid,
								cli->lastecm.caid, cli->lastecm.prov, cli->lastecm.sid, ticks );
							/* TASK 2.8: GOOD is how a suppressed peer wins its
							 * requests back -- this tap is the recovery path. */
							trustagg_note_lk( TRUST_EV_GOOD,
								cli->lastecm.dcwsrctype, cli->lastecm.dcwsrcid,
								cli->lastecm.caid, ticks );
						}

						/*
						 * TASK 1.6 — the retry IS the recovery opportunity.
						 *
						 * A client that got a bad key re-asks 1-3 s later instead of at the
						 * ~10 s crypto period. Recording the source that supplied the key
						 * means srvtab_arrange() will pick a different one for this very
						 * request, so the retry lands somewhere else instead of returning
						 * the same bad key. That is MTTR <= 1 crypto period.
						 *
						 * A SOFT retry is inference, not proof (GR3), so this only
						 * re-routes -- it disables nothing, scores nothing and cannot
						 * black-screen a channel: rotation_should_avoid() caps itself at
						 * ROTATION_MAX_AVOID sources and always leaves one reachable.
						 *
						 * GR1: the source recorded is the one from `lastecm`, i.e. the one
						 * that actually delivered the key, never whichever is current.
						 */
						if (rcls==RETRY_HARD || rcls==RETRY_SOFT) {
							/*
							 * TASK 1.9 -- BAD-CW-LIMIT.
							 *
							 * Return 2 means THIS event is the one that reached
							 * the limit; 1 means the source was over it already;
							 * 0 means it was counted and is still tolerated.
							 * Only the crossing is logged, so the operator gets
							 * exactly one line per confirmed event no matter what
							 * the tolerance is set to -- and with the default of
							 * 1 the first event is always the crossing, which is
							 * why the line below is the same one TASK 1.6 wrote.
							 *
							 * Below the limit nothing is avoided, so the retry
							 * goes back to the same source. That is the point of
							 * the option: an operator who does not trust a single
							 * retry as evidence for *their* network can require
							 * more of it (GR3 -- one soft event is not a verdict).
							 */
							/*
							 * TASK 2.1 -- the tolerance does not apply to a
							 * proven event. BAD-CW-LIMIT exists to make an
							 * operator wait for more evidence before a source
							 * is re-routed; a proof *is* the evidence, and making
							 * it wait would only extend the black screen it was
							 * detected to end (GR3). So limit 1 here, whatever
							 * the option says, and the log line says why.
							 */
							int limit_now = led_proof ? 1 : rotation_badcw_limit;
							int rc19 = rotation_note_bad_limited( &rotation_tab,
									cli->lastecm.dcwsrctype, cli->lastecm.dcwsrcid,
									cli->lastecm.caid, cli->lastecm.sid, cli->lastecm.prov,
									ticks, limit_now );
							if (rc19==2) {
								debugf(DBG_ERROR," !!! ROTATION: ch %04x:%06x:%04x will avoid source %d/%d on the next request (client '%s' re-asked%s)\n",
									cli->lastecm.caid, cli->lastecm.prov, cli->lastecm.sid,
									(int)cli->lastecm.dcwsrctype, (int)cli->lastecm.dcwsrcid, cli->user,
									led_proof?", proof: the independent source disagreed":
									(limit_now>1?", limit reached":""));
							}
							else if (rc19==0 && limit_now>1) {
								debugf(getdbgflag(DBG_NEWCAMD,cli->pid,cli->id)," !?> BAD-CW LIMIT: ch %04x:%06x:%04x source %d/%d counted, below the limit of %d -- still trusted with the retry\n",
									cli->lastecm.caid, cli->lastecm.prov, cli->lastecm.sid,
									(int)cli->lastecm.dcwsrctype, (int)cli->lastecm.dcwsrcid,
									rotation_badcw_limit);
							}
						}
						else if (cli->lastecm.status && cli->lastecm.dcwsrctype!=DCW_SOURCE_NONE) {
							/*
							 * The channel works again. Clearing is tied to this evidence
							 * rather than to a timer: any fixed expiry is either long
							 * enough to keep serving bad keys or short enough to re-try the
							 * bad source before the client has proved the new one works.
							 */
							int ncleared = rotation_note_good( &rotation_tab,
									cli->lastecm.caid, cli->lastecm.sid, cli->lastecm.prov );
							if (ncleared>0) {
								debugf(DBG_ERROR," !!! ROTATION: ch %04x:%06x:%04x recovered, %d avoided source(s) released\n",
									cli->lastecm.caid, cli->lastecm.prov, cli->lastecm.sid, ncleared);
							}
							/*
							 * TASK 1.9 -- the same recovery, applied to the purge
							 * marks (GR8: every hard action reversible).
							 *
							 * The mark lifted is the one on the source that just
							 * delivered a key this client accepted -- it is that
							 * source's own proof of recovery, and nobody else's
							 * mark is touched: another source's reuse proof is
							 * still standing evidence about that other source
							 * (GR1, and the same scoping GR4 requires). A source
							 * quiet until its mark expires is the other way out,
							 * via SERVICE-BLACKLIST-TIME.
							 *
							 * lockcache is NOT taken here: this runs on the ECM
							 * thread with lockecm held, and the purge table is
							 * reachable from `cache_purge_sweep()` under
							 * lockcache. Taking it here would invert the existing
							 * lockecm -> lockcache order. The table is 64 fixed
							 * slots and this touches exactly one of them; the
							 * same reasoning TASK 1.5 used for the mark side,
							 * where the sweep is the only cache-touching part.
							 */
							if ( cache_purge_unmark( &purge_tab,
									cli->lastecm.dcwsrctype, cli->lastecm.dcwsrcid,
									cli->lastecm.caid, cli->lastecm.sid, cli->lastecm.prov ) ) {
								debugf(DBG_ERROR," !!! CACHE PURGE RELEASE: ch %04x:%06x:%04x mark on source %d/%d lifted, it just delivered a key this client accepted\n",
									cli->lastecm.caid, cli->lastecm.prov, cli->lastecm.sid,
									(int)cli->lastecm.dcwsrctype, (int)cli->lastecm.dcwsrcid);
							}
						}
					}

					if (ecm) {
						ecm->lastrecvtime = ticks;
						if (ecm->dcwstatus==STAT_DCW_FAILED) {
							if (ecm->period > cs->option.dcw.retry) {
								buf[1] = 0; buf[2] = 0;
								cs_message_send( cli->handle, &clicd, buf, 3, cli->sessionkey);
								debugf(getdbgflag(DBG_NEWCAMD,cli->pid,cli->id)," <|> decode failed to client '%s' ch %04x:%06x:%04x, already failed\n",cli->user, clicd.caid,clicd.provid,clicd.sid);
							}
							else {
								ecm->period++; // RETRY
								cs_store_ecmclient(cs, ecm, cli, clicd.msgid);
								debugf(getdbgflag(DBG_NEWCAMD,cli->pid,cli->id)," <- ecm from client '%s' ch %04x:%06x:%04x:%08x**\n",cli->user,clicd.caid,clicd.provid,clicd.sid,ecm->hash);
								cli->ecm.busy=1;
								cli->ecm.hash = ecm->hash;
								ecm->dcwstatus = STAT_DCW_WAIT;
								ecm->cachestatus = 0; //ECM_CACHE_NONE; // Resend Request
								ecm->checktime = 1; // Check NOW
								pipe_wakeup( prg.pipe.ecm[1] );
							}
						}
						else {
							//TODO: Add another card for sending ecm
							cs_store_ecmclient(cs, ecm, cli, clicd.msgid);
							debugf(getdbgflag(DBG_NEWCAMD,cli->pid,cli->id)," <- ecm from client '%s' ch %04x:%06x:%04x:%08x*\n",cli->user,clicd.caid,clicd.provid,clicd.sid,ecm->hash);
							cli->ecm.busy=1;
							cli->ecm.hash = ecm->hash;
							// Check for Success/Timeout
							if (!ecm->checktime) {
#ifdef CHECK_NEXTDCW
								if (cli->dcwcheck) {
									checkfreeze_checkECM( ecm, cli->lastecm.request);
									if ( (ecm->dcwstatus==STAT_DCW_SUCCESS) && !checkfreeze_setdcw(ecm,ecm->cw) ) { // ??? last ecm is wrong
										ecm->dcwstatus = STAT_DCW_WAIT;
										memset( ecm->cw, 0, 16 );
										ecm->checktime = 1; // Wakeup Now
										pipe_wakeup( prg.pipe.ecm[1] );
									}
									else cs_senddcw_cli(cli);
								}
								else
#endif
								cs_senddcw_cli(cli);
							}
#ifdef TESTCHANNEL
							int testchannel = ( (ecm->caid==cfg.testchn.caid)&&(ecm->provid==cfg.testchn.provid)&&(!cfg.testchn.sid||(ecm->sid==cfg.testchn.sid)) );
							if (testchannel) {
								fdebugf(" ECM from Newcamd client '%s' (%08x)\n", cli->user, ecm->hash); 
							}
#endif
						}
					}
					else {
						cs->ecmaccepted++;
						// Setup ECM Request for Server(s)
						ecm = store_ecmdata(cs, data, len, clicd.sid, clicd.caid, clicd.provid);
						cs_store_ecmclient(cs, ecm, cli, clicd.msgid);
						debugf(getdbgflag(DBG_NEWCAMD,cli->pid,cli->id)," <- ecm from client '%s' ch %04x:%06x:%04x:%08x\n",cli->user,clicd.caid,clicd.provid,clicd.sid,ecm->hash);
						cli->ecm.busy=1;
						cli->ecm.hash = ecm->hash;
						ecm->cw1cycle = cw1cycle;
						ecm->dcwstatus = STAT_DCW_WAIT;
#ifdef CHECK_NEXTDCW
						if (cli->dcwcheck) checkfreeze_checkECM( ecm, cli->lastecm.request);
#endif
						if (cs->option.fallowcache) {
							ecm->waitcache = 1;
							ecm->dcwstatus = STAT_DCW_WAITCACHE;
							ecm->checktime = ecm->recvtime + cs->option.cachetimeout;
							pipe_cache_find(ecm, cs);
						}
						else ecm->checktime = 1; // Check NOW
						pipe_wakeup( prg.pipe.ecm[1] );

#ifdef TESTCHANNEL
						int testchannel = ( (ecm->caid==cfg.testchn.caid)&&(ecm->provid==cfg.testchn.provid)&&(!cfg.testchn.sid||(ecm->sid==cfg.testchn.sid)) );
						if (testchannel) {
							fdebugf(" <- ecm from Newcamd client '%s' ch %04x:%06x:%04x %02x:%08x\n", cli->user, ecm->caid, ecm->provid, ecm->sid, ecm->ecm[0], ecm->hash);
						}
#endif
					}

					pthread_mutex_unlock(&prg.lockecm); //###
					if (isnew) wakeup_sendecm();
					break;


#ifdef SRV_CSCACHE
				// Incoming DCW from client
		case 0xC0:
		case 0xC1:
					cli->lastecmtime = ticks;
					if (!cli->ecm.busy) break;

					pthread_mutex_lock(&prg.lockecm); //###

					ecm = cli->ecm.request;
					if (!ecm) {
						debugf(getdbgflag(DBG_NEWCAMD,cli->pid,cli->id)," <!= wrong cw from Client '%s' ch %04x:%06x:%04x\n", cli->user, ecm->caid,ecm->provid,ecm->sid, ticks-cli->lastecmtime);
						pthread_mutex_unlock(&prg.lockecm);
						break;
					}
					if (ecm->hash!=cli->ecm.hash) {
						debugf(getdbgflag(DBG_NEWCAMD,cli->pid,cli->id)," <!= wrong cw from Client '%s' ch %04x:%06x:%04x\n", cli->user, ecm->caid,ecm->provid,ecm->sid, ticks-cli->lastecmtime);
						pthread_mutex_unlock(&prg.lockecm);
						break;
					}
					if ( (ecm->caid!=clicd.caid) || (ecm->sid!=clicd.sid) || (ecm->provid!=clicd.provid) || (cli->ecm.climsgid!=clicd.msgid) ) {
						debugf(getdbgflag(DBG_NEWCAMD,cli->pid,cli->id)," <!= wrong cw from Client '%s' ch %04x:%06x:%04x\n", cli->user, ecm->caid,ecm->provid,ecm->sid, ticks-cli->lastecmtime);
						pthread_mutex_unlock(&prg.lockecm);
						break;
					}
					if ( (buf[0]&0x81)!=ecm->ecm[0] ) {
						debugf(getdbgflag(DBG_NEWCAMD,cli->pid,cli->id)," <!= wrong cw from Client '%s' ch %04x:%06x:%04x\n", cli->user, ecm->caid,ecm->provid,ecm->sid, ticks-cli->lastecmtime);
						pthread_mutex_unlock(&prg.lockecm);
						break;
					}
					if (buf[2]!=0x10) {
						debugf(getdbgflag(DBG_NEWCAMD,cli->pid,cli->id)," <!= wrong cw from Client '%s' ch %04x:%06x:%04x\n", cli->user, ecm->caid,ecm->provid,ecm->sid, ticks-cli->lastecmtime);
						pthread_mutex_unlock(&prg.lockecm);
						break;
					}
					// Check for DCW
					if (!acceptDCW_profile(&buf[3], ecm->cs)) {
						debugf(getdbgflag(DBG_NEWCAMD,cli->pid,cli->id)," <!= wrong cw from Client '%s' ch %04x:%06x:%04x\n", cli->user, ecm->caid,ecm->provid,ecm->sid, ticks-cli->lastecmtime);
						pthread_mutex_unlock(&prg.lockecm);
						break;
					}
					debugf(getdbgflag(DBG_NEWCAMD,cli->pid,cli->id)," <= cw from Client '%s' ch %04x:%06x:%04x (%dms)\n", cli->user, ecm->caid,ecm->provid,ecm->sid, ticks-cli->lastecmtime);
					if (ecm->dcwstatus!=STAT_DCW_SUCCESS) {
						static char msg[] = "Good dcw from Newcamd Client";
						ecm->statusmsg = msg;
						// Store ECM Answer
						ecm_setdcw( ecm, &buf[3], DCW_SOURCE_CSCLIENT, (ecm->cs->id<<16)|cli->id );
					}
					else {	//TODO: check same dcw between cards
						if ( memcmp(&ecm->cw, &buf[3],16) ) debugf(getdbgflag(DBG_NEWCAMD,cli->pid,cli->id)," !!! different dcw from newcamd client '%s'\n", cli->user);
					}

					pthread_mutex_unlock(&prg.lockecm); //###
					break;
#endif

		default:
					if (buf[0]==MSG_KEEPALIVE) {
#ifdef SRV_CSCACHE
						// Check for Cache client??
						if (clicd.sid==(('C'<<8)|'H')) clicd.caid = ('O'<<8)|'K';
#endif
						//debugf(getdbgflag(DBG_NEWCAMD,cli->pid,cli->id)," <-> keepalive from/to client '%s'\n",cli->user);
						if ( !cs_message_send(cli->handle, &clicd, buf, 3, cli->sessionkey) ) cs_disconnect_cli( cli );
					}
					else {
						debugf(getdbgflag(DBG_NEWCAMD,cli->pid,cli->id)," newcamd: unknown message type '%02x' from client '%s'\n",buf[0],cli->user);
						buf[1]=0; buf[2]=0;
						if ( !cs_message_send( cli->handle, &clicd, buf, 3, cli->sessionkey) ) cs_disconnect_cli( cli );
					}
				} // Switch

	return NULL;
}

///////////////////////////////////////////////////////////////////////////////
// DCW
///////////////////////////////////////////////////////////////////////////////

void cs_senddcw_cli(struct cs_client_data *cli)
{
	unsigned char buf[CWS_NETMSGSIZE];
	struct cs_custom_data clicd; // Custom data

	if (cli->ecm.status==STAT_DCW_SENT) {
		debugf(getdbgflag(DBG_NEWCAMD,cli->pid,cli->id)," +> cw send failed to client '%s', cw already sent\n", cli->user); 
		return;
	}
	if (cli->handle==INVALID_SOCKET) {
		debugf(getdbgflag(DBG_NEWCAMD,cli->pid,cli->id)," +> cw send failed to client '%s', client disconnected\n", cli->user); 
		return;
	}
	if (!cli->ecm.busy) {
		debugf(getdbgflag(DBG_NEWCAMD,cli->pid,cli->id)," +> cw send failed to client '%s', no ecm request\n", cli->user); 
		return;
	}

	ECM_DATA *ecm = cli->ecm.request;
	//FREEZE
	/*
	 * TASK 1.10a — two r82a defects fixed here, both of which corrupt the
	 * per-client delivery record that Phase 1 attribution is built on:
	 *
	 *   1. `cli->lastecm.status=1` was an ASSIGNMENT inside the condition,
	 *      not a comparison. It unconditionally marked the previous delivery
	 *      as successful whenever the channel had not changed, so a client
	 *      that had actually been fed a bad CW was recorded as having
	 *      succeeded. That is a false negative on exactly the event the
	 *      trust engine must see, and it is a GR1 attribution error.
	 *
	 *   2. `enablefreeze` was left uninitialised when the outer branch was
	 *      not taken (a zap to a different channel), and was then read in
	 *      the decode-failed path as `if (enablefreeze) cli->freeze++`.
	 *      So `freeze` could be incremented with stack garbage — which is
	 *      what the /newcamd page shows and what `cardsids_update` uses to
	 *      decide whether to freeze a card.
	 *
	 * Both fixes are behaviour-preserving for the correct case: the freeze
	 * window is still 200 ms and only applies after a genuinely successful
	 * delivery on the same channel.
	 */
	int enablefreeze = 0;
	if ( (cli->lastecm.caid==ecm->caid)&&(cli->lastecm.prov==ecm->provid)&&(cli->lastecm.sid==ecm->sid) ) {
		if ( (cli->lastecm.status==1)&&(cli->lastdcwtime+200<GetTickCount()) ) enablefreeze = 1;
	} else cli->zap++;

	cli->lastecm.caid = ecm->caid;
	cli->lastecm.prov = ecm->provid;
	cli->lastecm.sid = ecm->sid;
	/*
	 * TASK 1.2 — `lastecm.hash` existed in the struct (config.h:346) but was
	 * never assigned anywhere in the tree, so every per-client record of "what
	 * was last delivered" was keyed on SID alone. GR6 forbids that: on Irdeto
	 * multi-CHID a single SID carries several independent services, and folding
	 * them together would attribute one service's failure to another.
	 *
	 * Set unconditionally here, alongside caid/prov/sid, so it is correct on
	 * both the success and the decode-failed path.
	 */
	cli->lastecm.hash = ecm->hash;
	cli->lastecm.decodetime = GetTickCount()-cli->ecm.recvtime;
	cli->lastecm.request = cli->ecm.request;

	clicd.msgid = cli->ecm.climsgid;
	clicd.sid = ecm->sid;
	clicd.caid = ecm->caid;
	clicd.provid = ecm->provid;

	if ( (ecm->hash==cli->ecm.hash)&&(ecm->dcwstatus==STAT_DCW_SUCCESS) ) {
		cli->lastecm.dcwsrctype = ecm->dcwsrctype;
		cli->lastecm.dcwsrcid = ecm->dcwsrcid;
		/*
		 * TASK 2.1 -- record WHICH key was delivered, not just who sent it.
		 *
		 * `lastecm.dcw` has existed all along and no protocol ever wrote it:
		 * every sibling (srv-camd35.c, srv-cccam.c, srv-cs378x.c, ...) fills
		 * in the source and the status here and leaves this field zero. That
		 * was invisible until something needed to name the key a client was
		 * actually holding -- which is exactly what the agreement ledger must
		 * match a failure against. Without it the delivered key reads as 16
		 * zero bytes and every proof silently fails to form.
		 *
		 * It is a copy of the field the message is built from two lines below,
		 * so it cannot disagree with what the client received.
		 */
		memcpy( cli->lastecm.dcw, ecm->cw, 16 );
		cli->lastecm.status=1;
		cli->ecmok++;
		cli->ecmoktime += GetTickCount()-cli->ecm.recvtime;
		buf[0] = ecm->ecm[0];
		buf[1] = 0;
		buf[2] = 0x10;
		memcpy( &buf[3], &ecm->cw, 16 );
		if ( !cs_message_send( cli->handle, &clicd, buf, 19, cli->sessionkey) ) cs_disconnect_cli( cli );
		else debugf(getdbgflag(DBG_NEWCAMD,cli->pid,cli->id)," => cw to client '%s' ch %04x:%06x:%04x (%dms)\n", cli->user, ecm->caid,ecm->provid,ecm->sid, GetTickCount()-cli->ecm.recvtime);
		cli->lastdcwtime = GetTickCount();
	}
	else { //if (ecm->data->dcwstatus==STAT_DCW_FAILED)
		if (enablefreeze) cli->freeze++;
		cli->lastecm.status=0;
		cli->lastecm.dcwsrctype = DCW_SOURCE_NONE;
		cli->lastecm.dcwsrcid = 0;
		/*
		 * TASK 2.1 -- and clear the key with the rest of the record. GR2: no
		 * key was delivered here, so nothing may be blamed for one later. The
		 * ledger only consults a failure when `status` says a delivery
		 * happened, but leaving a stale key next to a zeroed status is the
		 * kind of half-state that becomes a false accusation after the next
		 * refactor.
		 */
		memset( cli->lastecm.dcw, 0, 16 );
		buf[0] = ecm->ecm[0];
		buf[1] = 0;
		buf[2] = 0;
		if ( !cs_message_send(  cli->handle, &clicd, buf, 3, cli->sessionkey) ) cs_disconnect_cli( cli );
		else debugf(getdbgflag(DBG_NEWCAMD,cli->pid,cli->id)," |> decode failed to client '%s' ch %04x:%06x:%04x (%dms)\n", cli->user, ecm->caid,ecm->provid,ecm->sid, GetTickCount()-cli->ecm.recvtime);
	}
	cli->ecm.busy=0;
	cli->ecm.status = STAT_DCW_SENT;
}

// Check sending cw to clients
void cs_check_sendcw(ECM_DATA *ecm)
{
	struct cardserver_data *cs = cfg.cardserver;
	while (cs) {
		if (  !IS_DISABLED(cs->flags)&&(cs->newcamd.handle>0) ) {
			struct cs_client_data *cli = cs->newcamd.client;
			while (cli) {
				if (  !IS_DISABLED(cli->flags)&&(cli->handle!=INVALID_SOCKET)&&(cli->ecm.busy)&&(cli->ecm.request==ecm)&&(cli->ecm.status==STAT_ECM_SENT) ) {
					cs_senddcw_cli( cli );
				}
				cli = cli->next;
			}
		}
		cs = cs->next;
	}
}


///////////////////////////////////////////////////////////////////////////////

void cs_recv_pipe()
{
	uint8_t buf[1024];
	struct pollfd pfd[2];
	struct cs_client_data *cli;

	do {
		int len = pipe_recv( prg.pipe.newcamd[0], buf);
		if (len>0) {
			switch(buf[0]) {
				case PIPE_WAKEUP:  // ADD NEW CLIENT
					//debugf(" wakeup csmsg\n");
					break;
#ifdef EPOLL_NEWCAMD
				case PIPE_CLI_CONNECTED:  // ADD NEW FD
					memcpy( &cli, buf+1, sizeof(void*) );
					// Add to events
					struct epoll_event ev; // epoll event
					ev.events = EPOLLIN;
					ev.data.fd = cli->handle;
					ev.data.ptr = cli;
					if ( epoll_ctl(prg.epoll.newcamd, EPOLL_CTL_ADD, cli->handle, &ev) == -1 ) debugf(DBG_ERROR,"Err! EPOLL_CTL_ADD %s (%d)\n", cli->user, cli->handle);
					//else debugf(0,"EPOLL_CTL_ADD %s (%d)\n", cli->user, cli->handle);
					break;
#endif
			}
		}
		pfd[0].fd = prg.pipe.newcamd[0];
		pfd[0].events = POLLIN | POLLPRI;
	} while (poll(pfd, 1, 3)>0);
}

///////////////////////////////////////////////////////////////////////////////

#ifdef EPOLL_NEWCAMD


void *cs_recvmsg_thread(void *param)
{
	prg.pid_cs_msg = syscall(SYS_gettid);
	prctl(PR_SET_NAME,"Newcamd RecvMSG",0,0,0);

	prg.epoll.newcamd = epoll_create( MAX_EPOLL_EVENTS );
	// Add PIPE
	struct epoll_event ev; // epoll event
	ev.events = EPOLLIN | EPOLLPRI | EPOLLRDHUP;
	ev.data.ptr = NULL;
	if ( epoll_ctl(prg.epoll.newcamd, EPOLL_CTL_ADD, prg.pipe.newcamd[0], &ev) == -1 ) printf("epoll_ctl");
	// Main Loop
	struct epoll_event evlist[MAX_EPOLL_EVENTS]; // epoll recv events
	while (!prg.restart) {
		int ready = epoll_wait( prg.epoll.newcamd, evlist, MAX_EPOLL_EVENTS, 1004);
		if (ready == -1) {
			if ( (errno==EINTR)||(errno==EAGAIN) ) {
				usleep(1000);
				continue;
			}
			else {
				usleep(99000);
				debugf(DBG_ERROR,"Err! epoll_wait (%d)", errno);
			}
		}
		else if (ready==0) continue; // timeout

		//usleep(cfg.delay.thread);

		int i;
		for (i=0; i < ready; i++) {
			if ( evlist[i].events & (EPOLLIN|EPOLLPRI) ) {
				if (evlist[i].data.ptr == NULL) cs_recv_pipe();
				else cs_cli_recvmsg(evlist[i].data.ptr);
			}
			else if ( evlist[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR) ) { // EPOLLRDHUP
				if (evlist[i].data.ptr == NULL) debugf(DBG_ERROR,"Err! epoll_wait() pipe\n"); // error !!!
				else cs_disconnect_cli(evlist[i].data.ptr);
			}
		}
	}
	return NULL;
}

#else

void *cs_recvmsg_thread(void *param)
{
	struct pollfd pfd[MAX_PFD];
	int pfdcount;

	prg.pid_cs_msg = syscall(SYS_gettid);
	prctl(PR_SET_NAME,"Newcamd RecvMSG",0,0,0);

	while (!prg.restart) {
		pfdcount = 0;
		// PIPE
		pfd[pfdcount].fd = prg.pipe.newcamd[0];
		pfd[pfdcount++].events = POLLIN | POLLPRI;

		struct cardserver_data *cs = cfg.cardserver;
		while (cs) {
			if ( cs->option.fsharenewcamd )
			if ( !IS_DISABLED(cs->newcamd.flags)&&(cs->newcamd.handle>0) ) {
				if (cs->newcamd.clipfd.update||cs->newcamd.clipfd.count<0) {
					cs->newcamd.clipfd.count = 0;
					struct cs_client_data *cli = cs->newcamd.client;
					while (cli && (cs->newcamd.clipfd.count<NEWCAMD_MAX_PFD)) {
						if ( !IS_DISABLED(cli->flags) && (cli->handle>0) && !(cli->flags&FLAG_WORKTHREAD) ) {
							cli->ipoll = cs->newcamd.clipfd.count;
							cs->newcamd.clipfd.pfd[cs->newcamd.clipfd.count].fd = cli->handle;
							cs->newcamd.clipfd.pfd[cs->newcamd.clipfd.count++].events = POLLIN | POLLPRI;
						} else cli->ipoll = -1;
						cli = cli->next;
					}
					cs->newcamd.clipfd.update = 0;
					//debugf(getdbgflag(DBG_ERROR,0,0), " [%s] clients poll updated %d\n", cs->name, cs->newcamd.clipfd.count);
				}
				cs->newcamd.clipfd.ipoll = pfdcount;
				if (cs->newcamd.clipfd.count>0) {
					memcpy( &pfd[pfdcount], cs->newcamd.clipfd.pfd, cs->newcamd.clipfd.count * sizeof(struct pollfd) );
					pfdcount += cs->newcamd.clipfd.count;
				}
			} else cs->newcamd.clipfd.count = 0;
			cs = cs->next;
		}

		int retval = poll(pfd, pfdcount, 3012); // for 3seconds

		if ( retval>0 ) {
			usleep(cfg.delay.thread);

			// Newcamd Clients
			cs = cfg.cardserver;
			while (cs) {
				if ( cs->option.fsharenewcamd )
				if ( !IS_DISABLED(cs->newcamd.flags)&&(cs->newcamd.handle>0)&&(cs->newcamd.clipfd.count>0) ) {
					//pthread_mutex_lock(&prg.lockcli);
					struct cs_client_data *cscli = cs->newcamd.client;
					while (cscli) {
						if ( !IS_DISABLED(cscli->flags)&&(cscli->handle>0) && (cscli->ipoll>=0) && (cscli->handle==pfd[cs->newcamd.clipfd.ipoll+cscli->ipoll].fd) ) {
							if ( pfd[cs->newcamd.clipfd.ipoll+cscli->ipoll].revents & (POLLHUP|POLLNVAL) ) cs_disconnect_cli(cscli);
							else if ( pfd[cs->newcamd.clipfd.ipoll+cscli->ipoll].revents & (POLLIN|POLLPRI) ) {
								cs_cli_recvmsg(cscli);
							}
						}
						cscli = cscli->next;
					}
					//pthread_mutex_unlock(&prg.lockcli);
				}
				cs = cs->next;
			}
			//
			if ( pfd[0].revents & (POLLIN|POLLPRI) ) cs_recv_pipe();
		}
		else if ( retval<0 ) {
			debugf(0, " thread receive messages: poll error %d(errno=%d)\n", retval, errno);
			usleep(99000);
		}

	}
	return NULL;
}

#endif

///////////////////////////////////////////////////////////////////////////////
// NEWCAMD SERVER: START/STOP
///////////////////////////////////////////////////////////////////////////////

int start_thread_newcamd()
{
	pthread_t tid;
#ifndef MONOTHREAD_ACCEPT
	create_thread(&tid, newcamd_accept_thread,NULL);
#endif
	create_thread(&tid, cs_recvmsg_thread,NULL);
	return 0;
}


