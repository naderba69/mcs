///////////////////////////////////////////////////////////////////////////////
// File: srv-freecccam.c
///////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
// PROTO
///////////////////////////////////////////////////////////////////////////////

void freecc_cli_recvmsg(struct cc_client_data *cli);



///////////////////////////////////////////////////////////////////////////////
// FREECCCAM SERVER: CONNECT CLIENTS
///////////////////////////////////////////////////////////////////////////////

void frcc_sendcards_cli(struct cc_client_data *cli)
{
	int nbcard=0;
	struct cardserver_data *cs = cfg.cardserver;

	int i;
	if (cfg.freecccam.csport[0]) {
		for(i=0;i<MAX_CSPORTS;i++) {
			if(cfg.freecccam.csport[i]) {
				cs = getcsbyport(cfg.freecccam.csport[i]);
				if (cs)
					if (cc_sendcard_cli(cs, cli,0)) nbcard++;
			} else break;
		}
	}
	else {
		while (cs) {
			if (cc_sendcard_cli(cs, cli,0)) nbcard++;
			cs = cs->next;
		}
	}

	debugf(0," FreeCCcam: %d cards --> client(%s)\n",  nbcard, ip2string(cli->ip) );
}


///////////////////////////////////////////////////////////////////////////////
// CCCAM SERVER: DISCONNECT CLIENTS
///////////////////////////////////////////////////////////////////////////////

void frcc_disconnect_cli(struct cc_client_data *cli)
{
	if ( (cli->connection.status>0) && (cli->handle>0) ) {
		cli->connection.status = 0;
		uint32_t ticks = GetTickCount();
		cli->connection.uptime += ticks - cli->connection.time;
		cli->connection.lastseen = ticks; // Last Seen
		close(cli->handle);
		cli->handle = -1;
		debugf(0," FreeCCcam: client '%s' disconnected \n", cli->user);
		//////cli->parent->clipfd.update = 1;
	}
}

///////////////////////////////////////////////////////////////////////////////

void *freecc_connect_cli(struct connect_cli_data *param)
{
	uint8_t buf[CC_MAXMSGSIZE];
	uint8_t data[64];
	int i;
	struct cc_crypt_block sendblock;	// crypto state block
	struct cc_crypt_block recvblock;	// crypto state block
	char usr[64];
	char pwd[255];

	int sock = param->sock;
	uint32_t ip = param->ip;
	free(param);

	memset(usr, 0, sizeof(usr));
	memset(pwd, 0, sizeof(pwd));
	// create & send random seed
	for(i=0; i<12; i++ ) data[i]=fast_rnd();
	// Create Multics ID
	data[3] = (data[0]^'M') + data[1] + data[2];
	data[7] = data[4] + (data[5]^'C') + data[6];
	data[11] = data[8] + data[9] + (data[10]^'S');
	//Create checksum for "O" cccam:
	for (i = 0; i < 4; i++) {
		data[12 + i] = (data[i] + data[4 + i] + data[8 + i]) & 0xff;
	}
	if ( !send_nonb(sock, data, 16, 100) ) {
		close(sock);
		return NULL;
	}
	//XOR init bytes with 'CCcam'
	cc_crypt_xor(data);
	//SHA1
	SHA_CTX ctx;
	SHA1_Init(&ctx);
	SHA1_Update(&ctx, data, 16);
	SHA1_Final(buf, &ctx);
	//initialisate crypto states
	cc_crypt_init(&sendblock, buf, 20);
	cc_decrypt(&sendblock, data, 16);
	cc_crypt_init(&recvblock, data, 16);
	cc_decrypt(&recvblock, buf, 20);
	//debugdump(buf, 20, "SHA1 hash:");
	memcpy(usr,buf,20);
	if ((i=recv_nonb(sock, buf, 20,3000)) == 20) {
		cc_decrypt(&recvblock, buf, 20);
		//debugdump(buf, 20, "Recv SHA1 hash:");
		if ( memcmp(buf,usr,20)!=0 ) {
			//debugf(0," cc_connect_cli(): wrong sha1 hash from client! (%s)\n",ip2string(ip));
			close(sock);
			return NULL;
		}
	} else {
		//debugf(0," cc_connect_cli(): recv sha1 timeout\n");
		close(sock);
		return NULL;
	}

  // receive username
	if ((i=recv_nonb(sock, buf, 20,3000)) == 20) {
		cc_decrypt(&recvblock, buf, i);
		memcpy(usr,buf,20);
		//debugf(0," cc_connect_cli(): username '%s'\n", usr);
	}
	else {
		//debugf(0," cc_connect_cli(): recv user timeout\n");
		close(sock);
		return NULL;
	}


  // Check for username
	if ( strcmp(cfg.freecccam.user,usr) ) {
		debugf(0," FreeCCcam: Failed to connect client (%s), wrong username\n",ip2string(ip));
		close(sock);
		return NULL;
	}

	pthread_mutex_lock(&prg.lockfreecccli);

	int found = 0;
	struct cc_client_data *cli = cfg.freecccam.server.client;
	while (cli) {
		if (cli->handle<=0) {
			found = 1;
			break;
		}
		else {
			if (cli->ip == ip) { // dont connect
				frcc_disconnect_cli(cli);
				found = 1;
				break;
			}
		}
		cli = cli->next;
	}
	// check for inactive clients
	if (!found) {
		uint32_t ticks = GetTickCount();
		while (cli) {
			if (cli->handle>0) {
				// Check if we can disconnect idle state clients
				if  ( (ticks-cli->lastecmtime) > 90000 ) frcc_disconnect_cli(cli);
			}
			if (cli->handle<=0) {
				found = 1;
				break;
			}
			cli = cli->next;
		}
	}

	pthread_mutex_unlock(&prg.lockfreecccli);

	if (!found) {
		debugf(0," FreeCCcam: Failed to connect client (%s), no available connection\n",ip2string(ip));
		close(sock);
		return NULL;
	}

  // receive passwd / 'CCcam'
	if ((i=recv_nonb(sock, buf, 6,3000)) == 6) {
		memset(pwd, 0, sizeof(pwd));
		strcpy( pwd, cfg.freecccam.pass);
		cc_encrypt(&recvblock, (uint8_t *)pwd, strlen(pwd));
		cc_decrypt(&recvblock, buf, 6);
		if (memcmp( buf, "CCcam\0",6)) {
			debugf(0," FreeCCcam: login failed from client(%s)\n",ip2string(ip));
			close(sock);
			return NULL;
		}
	} 
	else {
		close(sock);
		return NULL;
	}

  // send passwd ack
	memset(buf, 0, 20);
	memcpy(buf, "CCcam\0", 6);
	//debugf(0,"Server: send ack '%s'\n",buf);
	cc_encrypt(&sendblock, buf, 20);
	if ( !send_nonb(sock, buf, 20, 100) ) {
		close(sock);
		return NULL;
	}
	sprintf(cli->user,"%s", ip2string(ip));
	//cli->ecmnb=0;
	//cli->ecmok=0;
	memcpy(&cli->sendblock,&sendblock,sizeof(sendblock));
	memcpy(&cli->recvblock,&recvblock,sizeof(recvblock));
	debugf(0," FreeCCcam: client(%s) connected\n",ip2string(ip));

  // recv cli data
	memset(buf, 0, sizeof(buf));
	i = cc_msg_recv( sock, &cli->recvblock, buf, 3000);
	if (i!=97) {
		debugf(0," error recv cli data\n");
		close(sock);
		return NULL;
	}

  // Setup Client Data
//	pthread_mutex_lock(&prg.lockfreecccli);
	memcpy( cli->nodeid, buf+24, 8);
	memcpy( cli->version, buf+33, 32);
	memcpy( cli->build, buf+65, 32 );
	debugf(0," FreeCCcam: client(%s) running version %s build %s\n",ip2string(ip), cli->version, cli->build);  // cli->nodeid,8,
	cli->cardsent = 0;

	cli->connection.status = 1;
	cli->connection.time = GetTickCount();
	cli->lastactivity = GetTickCount();
	cli->lastecmtime = GetTickCount();
	cli->chkrecvtime = 0;
	cli->ip = ip;
	cli->msg.len = 0;
	cli->handle = sock;
	cli->ecm.busy = 0;

//	pthread_mutex_unlock(&prg.lockfreecccli);
  // send cli data ack
	cc_msg_send( sock, &cli->sendblock, CC_MSG_CLI_INFO, 0, NULL);
	//cc_msg_send( sock, &cli->sendblock, CC_MSG_BAD_ECM, 0, NULL);
	int sendversion = ( (cli->version[28]=='W')&&(cli->version[29]='H')&&(cli->version[30]='O') );
	cc_sendinfo_cli(cli, sendversion);
	//cc_msg_send( sock, &cli->sendblock, CC_MSG_BAD_ECM, 0, NULL);
	cli->cardsent = 1;
	usleep(99000);
	//if (!sendversion)
	frcc_sendcards_cli(cli);

	pipe_wakeup( frcc_pipe[1] );
	return cli;
}



void freecccam_srv_accept(struct cccam_server_data *srv)
{
	struct sockaddr_in newaddr;
	socklen_t socklen = sizeof(struct sockaddr);
	int newfd = accept( srv->handle, (struct sockaddr*)&newaddr, /*(socklen_t*)*/&socklen);
	if ( newfd<=0 ) {
		if ( (errno!=EAGAIN) && (errno!=EINTR) ) debugf( getdbgflag(DBG_CCCAM,0,0)," FreeCCcam%d: Accept failed (errno=%d)\n", errno);
	}
	else {
		uint32_t newip = newaddr.sin_addr.s_addr;
		if ( isblockedip(newip) ) {
			debugf(getdbgflag(DBG_CCCAM,0,0)," FreeCCcam%d: New Connection (%s) closed, ip blocked\n", ip2string(newip) );
			close(newfd);
		}
		else {
			pthread_t srv_tid;
			SetSocketKeepalive(newfd);
			if (SetSoketNonBlocking(newfd) < 0) {
				debugf(DBG_ERROR, " FreeCCcam: unable to set accepted socket nonblocking (errno=%d)\n", errno);
				close(newfd);
				return;
			}
			//debugf(getdbgflag(DBG_CCCAM,0,0)," FreeCCcam%d: new connection (%s)\n", ip2string(newip) );
			struct connect_cli_data *newdata = malloc( sizeof(struct connect_cli_data) );
			newdata->server = srv; 
			newdata->sock = newfd; 
			newdata->ip = newaddr.sin_addr.s_addr;
			if ( !create_thread(&srv_tid, (threadfn)freecc_connect_cli,newdata) ) {
				free( newdata );
				close( newfd );
			}
		}
	}
}


void *freecccam_accept_thread(void *param)
{
	prctl(PR_SET_NAME,"FreeCCcam Accept",0,0,0);

	while(!prg.restart) {
		uint64_t config_generation;
		struct pollfd pfd[3];
		int pfdcount = 0;
		newcamd_config_read_lock();
		config_generation = cfg.cache.generation;
		if ( !IS_DISABLED(cfg.freecccam.server.flags) && (cfg.freecccam.server.handle>0) ) {
			cfg.freecccam.server.ipoll = pfdcount;
			pfd[pfdcount].fd = cfg.freecccam.server.handle;
			pfd[pfdcount++].events = POLLIN | POLLPRI;
		} else cfg.freecccam.server.ipoll = -1;
		newcamd_config_read_unlock();

		if (pfdcount) {
			int retval = poll(pfd, pfdcount, 3006);
			if (retval > 0) {
				newcamd_config_read_lock();
				if (config_generation != cfg.cache.generation) {
					newcamd_config_read_unlock();
					continue;
				}
				if ( !IS_DISABLED(cfg.freecccam.server.flags) &&
				     (cfg.freecccam.server.handle>0) &&
				     (cfg.freecccam.server.ipoll>=0) &&
				     (cfg.freecccam.server.handle==pfd[cfg.freecccam.server.ipoll].fd) &&
				     (pfd[cfg.freecccam.server.ipoll].revents & (POLLIN|POLLPRI)) )
					freecccam_srv_accept(&cfg.freecccam.server);
				newcamd_config_read_unlock();
			} else if (retval < 0) usleep(96000);
		} else sleep(1);
	}
	return NULL;
}
////////////////////////////////////////////////////////////////////////////////
// CCCAM SERVER: SEND DCW TO CLIENTS
////////////////////////////////////////////////////////////////////////////////

void frcc_senddcw_cli(struct cc_client_data *cli, const struct ledger_delivery_snapshot *delivery)
{
	uint8_t buf[CC_MAXMSGSIZE];
	uint32_t ticks = GetTickCount();
#ifdef CACHEEX
	if (!delivery || !delivery->lifetime_id ||
	    delivery->lifetime_id != __atomic_load_n(&cli->ecm.lifetime_id, __ATOMIC_ACQUIRE)) return;
#endif

	if (cli->ecm.status==STAT_DCW_SENT) {
		debugf(getdbgflag(DBG_CCCAM,cli->parent->id,cli->id)," +> cw send failed to CCcam client '%s', cw already sent\n", cli->user); 
		return;
	}
	if (cli->handle<=0) {
		debugf(getdbgflag(DBG_CCCAM,cli->parent->id,cli->id)," +> cw send failed to CCcam client '%s', client disconnected\n", cli->user); 
		return;
	}
	if (!cli->ecm.busy) {
		debugf(getdbgflag(DBG_CCCAM,cli->parent->id,cli->id)," +> cw send failed to CCcam client '%s', no ecm request\n", cli->user); 
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
	if (delivery->status == STAT_DCW_SUCCESS) {
#else
	cli->lastecm.request = cli->ecm.request;
	if (ecm->dcwstatus==STAT_DCW_SUCCESS && ecm->hash==cli->ecm.hash) {
#endif
		uint32_t sent_at;
#ifdef CACHEEX
		memcpy(buf, delivery->cw, 16);
#else
		memcpy(buf, ecm->cw, 16);
#endif
		cc_crypt_cw( cli->nodeid, cli->ecm.cardid , buf);
		if ( !cc_msg_send_and_encrypt(cli->handle, &cli->sendblock,
							      CC_MSG_ECM_REQUEST, 16, buf, buf, 16) ) {
			frcc_disconnect_cli( cli );
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
		debugf(0," => cw to CCcam client '%s' ch %04x:%06x:%04x (%dms)\n", cli->user, response_caid,response_provid,response_sid, sent_at-response_recvtime);
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
		cc_msg_send( cli->handle, &cli->sendblock, CC_MSG_ECM_NOK1, 0, NULL);
		debugf(0," |> decode failed to CCcam client '%s' ch %04x:%06x:%04x (%dms)\n", cli->user, response_caid,response_provid,response_sid, GetTickCount()-response_recvtime);
	}
	cli->ecm.busy=0;
	cli->ecm.status = STAT_DCW_SENT;
}


// Receive messages from clients
void freecc_cli_recvmsg(struct cc_client_data *cli)
{
	unsigned char buf[CC_MAXMSGSIZE];
	unsigned char data[CC_MAXMSGSIZE]; // for other use
	unsigned int cardid;
	int len;

	if (cli->handle>0) {
		len = cc_peekmsg( cli->handle, &cli->recvblock, &cli->msg, buf );
		uint32_t ticks = GetTickCount();
		if (len==0) {
			debugf(0," FreeCCcam: client(%s) read failed %d\n", cli->user,len);
			frcc_disconnect_cli(cli);
		}
		else if (len==-1) {
			if (!cli->chkrecvtime) cli->chkrecvtime = ticks;
			else if ((uint32_t)(ticks - cli->chkrecvtime) > 500u) {
				debugf(0," FreeCCcam: client(%s) read failed %d\n", cli->user,len);
				frcc_disconnect_cli(cli);
			}
		}
		else if (len>0) {
			cli->chkrecvtime = 0;
				cli->lastactivity = ticks;
				switch (buf[1]) {
					 case CC_MSG_ECM_REQUEST:
						cli->ecmnb++;
						cli->lastecmtime = ticks;
						if (len<20) return; // Avoid malicious peers

						if (cli->ecm.busy) {
							// send decode failed
							debugf(0," <|> decode failed to FreeCCcam client(%s), too many ecm requests\n", cli->user);
							break;
						}
						cli->ecm.busy = 0;

						//Check for card availability
						memcpy( data, buf+17, len-17);
						cardid = buf[10]<<24 | buf[11]<<16 | buf[12]<<8 | buf[13];
						uint16_t caid = buf[4]<<8 | buf[5];
						uint16_t sid = buf[14]<<8 | buf[15];
						uint32_t provid = ecm_getprovid( data, len-17, caid );
						if (provid==0) provid = buf[6]<<24 | buf[7]<<16 | buf[8]<<8 | buf[9];

						// Check for Profile
						struct cardserver_data *cs=getcsbyid( cardid );
						if (!cs) {
							cli->ecmdenied++;
							cc_msg_send( cli->handle, &cli->sendblock, CC_MSG_ECM_NOK2, 0, NULL);
							debugf(0," <|> decode failed to FreeCCcam client(%s), card-id %x not found\n", cli->user, cardid);
							break;
						}
						// Check ECM
						uint8_t cw1cycle;
						char *error = cs_accept_ecm(cs,caid,provid,sid,ecm_getchid(data,len-17,caid), len-17, data, &cw1cycle);
						if (error) {
							cli->ecmdenied++;
							cs->ecmdenied++;
							cc_msg_send( cli->handle, &cli->sendblock, CC_MSG_ECM_NOK2, 0, NULL);
							debugf(0," <|> decode failed to FreeCCcam client(%s) ch %04x:%06x:%04x, %s\n", cli->user, caid,provid,sid, error);
							break;
						}
						// ACCEPTED
						ecm_lock();
	
						// Search for ECM
						ECM_DATA *ecm = search_ecmdata_any(cs, data,  len-17, sid, caid, provid, 1); // dont get failed ecm request from cache
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
								attest_note( ATTEST_TAG_FREECCCAM | (uint32_t)cli->id, cli->lastecm.dcw,
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
								cc_store_ecmclient(ecm, cli);
								debugf( getdbgflagpro(DBG_CCCAM,0,cli->id,cs->id)," <- ecm from FreeCCcam client '%s' ch %04x:%06x:%04x**\n", cli->user, caid, provid, sid);
								cli->ecm.busy=1;
								cli->ecm.hash = ecm->hash;
								cli->ecm.cardid = cardid;
								ecm->recvtime = ticks;
								ecm_request_set_dcwstatus(ecm, STAT_DCW_WAIT);
								ecm->cachestatus = 0; //ECM_CACHE_NONE; // Resend Request
								ecm->checktime = 1; // Check NOW
							}
							else { // SUCCESS/WAIT
								cc_store_ecmclient(ecm, cli);
								debugf(getdbgflagpro(DBG_CCCAM,0,cli->id,cs->id)," <- ecm from FreeCCcam client '%s' ch %04x:%06x:%04x*\n", cli->user, caid, provid, sid);
								cli->ecm.busy=1;
								cli->ecm.hash = ecm->hash;
								cli->ecm.cardid = cardid;
								// Check for Success/Timeout
								if (!ecm->checktime) {
									LEDGER_CAPTURE_CLIENT_DELIVERY(delivery, ecm, cli);
									frcc_senddcw_cli(cli, delivery);
								}
							}
						}
						else {
							// Setup ECM Request for Server(s)
							ecm = store_ecmdata(cs, data, len-17, sid, caid, provid);
							if (!ecm) {
								cs->ecmdenied++;
								cli->ecmdenied++;
								if (!cc_msg_send(cli->handle, &cli->sendblock, CC_MSG_ECM_NOK1, 0, NULL)) frcc_disconnect_cli(cli);
								ecm_unlock();
								break;
							}
							cs->ecmaccepted++;
							cc_store_ecmclient(ecm, cli);
							debugf( getdbgflagpro(DBG_CCCAM,0,cli->id,cs->id), " <- ecm from FreeCCcam client(%s) ch %04x:%06x:%04x\n", cli->user,caid,provid,sid);
							cli->ecm.busy=1;
							cli->ecm.hash = ecm->hash;
							cli->ecm.cardid = cardid;
							ecm_request_set_dcwstatus(ecm, STAT_DCW_WAIT);
							ecm->checktime = 1; // Check NOW
							if (cs->option.fallowcache) {
								ecm->waitcache = 1;
								ecm_request_set_dcwstatus(ecm, STAT_DCW_WAITCACHE);
								ecm->checktime = ecm->recvtime + cs->option.cachetimeout;
								pipe_cache_find(ecm, cs);
							}
						}

						ecm_unlock();
						if (isnew) wakeup_sendecm();
						break;

					 case CC_MSG_KEEPALIVE:
						//printf(" Keepalive from client '%s'\n",cli->user);
						cc_msg_send( cli->handle, &cli->sendblock, CC_MSG_KEEPALIVE, 0, NULL);
						break;
				}
			}
	}
}

// Check sending cw to clients
void freecc_check_sendcw(ECM_DATA *ecm, const struct ledger_delivery_snapshot *delivery)
{
#ifdef CACHEEX
	if (!delivery) return;
#endif
	struct cccam_server_data *cccam = &cfg.freecccam.server;
	if (cccam) {
		if ( !IS_DISABLED(cccam->flags) && (cccam->handle>0) ) {
			struct cc_client_data *cli = cccam->client;
			while (cli) {
				#ifdef CACHEEX
				if (!IS_DISABLED(cli->flags) && cli->handle>0 && __atomic_load_n(&cli->ecm.lifetime_id, __ATOMIC_ACQUIRE)==delivery->lifetime_id) {
#else
				if (!IS_DISABLED(cli->flags) && cli->handle>0 && cli->ecm.busy && cli->ecm.request==ecm && cli->ecm.hash==ecm->hash) {
#endif
					frcc_senddcw_cli( cli, delivery );
				}
				cli = cli->next;
			}
		}
	}
}


///////////////////////////////////////////////////////////////////////////////

void frcc_recv_pipe()
{
	uint8_t buf[1024];
	struct pollfd pfd[2];
	do {
		int len = pipe_recv( frcc_pipe[0], buf, sizeof(buf));
		if (len>0) {
			switch(buf[0]) {
				case PIPE_WAKEUP:  // ADD NEW CLIENT
					if (len != 2) break;
					//debugf(" wakeup csmsg\n");
					break;
			}
		}
		pfd[0].fd = frcc_pipe[0];
		pfd[0].events = POLLIN | POLLPRI;
	} while (poll(pfd, 1, 1)>0);
}

///////////////////////////////////////////////////////////////////////////////

void *frcc_recvmsg_thread(void *param)
{
	struct pollfd pfd[MAX_PFD];
	int pfdcount;
	prctl(PR_SET_NAME,"FreeCCcam RecvMSG Thread",0,0,0);
	while(!prg.restart) {
		uint64_t config_generation;
		newcamd_config_read_lock();
		config_generation = cfg.cache.generation;
		pfdcount = 0;
		// PIPE
		pfd[pfdcount].fd = frcc_pipe[0];
		pfd[pfdcount++].events = POLLIN | POLLPRI;
		struct cccam_server_data *freecccam = &cfg.freecccam.server;
		if ( !IS_DISABLED(freecccam->flags)&&(freecccam->handle>0) ) {
			struct cc_client_data *cli = freecccam->client;
			while (cli && (pfdcount<MAX_PFD)) {
				if ( !IS_DISABLED(cli->flags)&&(cli->handle>0) ) {
					cli->ipoll = pfdcount;
					pfd[pfdcount].fd = cli->handle;
					pfd[pfdcount++].events = POLLIN | POLLPRI;
				} else cli->ipoll = -1;
				cli = cli->next;
			}
		}

		newcamd_config_read_unlock();
		int retval = poll(pfd, pfdcount, 3008); // for 3seconds

		if ( retval>0 ) {
			newcamd_config_read_lock();
			if (config_generation != cfg.cache.generation) {
				newcamd_config_read_unlock();
				continue;
			}

			// CCcam Clients
			struct cccam_server_data *freecccam = &cfg.freecccam.server;
			if ( !IS_DISABLED(freecccam->flags)&&(freecccam->handle>0) ) {
				//pthread_mutex_lock(&prg.lockcccli);
				struct cc_client_data *cli = freecccam->client;
				while (cli) {
					if ( !IS_DISABLED(cli->flags)&&(cli->handle>0)&&(cli->ipoll>=0)&&(cli->handle==pfd[cli->ipoll].fd) ) {
						if ( pfd[cli->ipoll].revents & (POLLHUP|POLLNVAL) ) frcc_disconnect_cli(cli);
						else if ( pfd[cli->ipoll].revents & (POLLIN|POLLPRI) ) {
							freecc_cli_recvmsg(cli);
						}
						///else if ( (GetTickCount()-cccli->lastactivity) > 600000 ) frcc_disconnect_cli(cccli);
					}
					cli = cli->next;
				}
				//pthread_mutex_unlock(&prg.lockcccli);
			}

			//
			if ( pfd[0].revents & (POLLIN|POLLPRI) ) frcc_recv_pipe();
			newcamd_config_read_unlock();
		}
		else if ( retval<0 ) {
			debugf(0, " thread receive messages: poll error %d(errno=%d)\n", retval, errno);
		}

	}
	return NULL;
}



///////////////////////////////////////////////////////////////////////////////
// FREECCCAM SERVER: START/STOP
///////////////////////////////////////////////////////////////////////////////

int start_thread_freecccam()
{
	pthread_t tid;
#ifndef MONOTHREAD_ACCEPT
	create_thread(&tid, freecccam_accept_thread,NULL);
#endif

	create_thread(&tid, frcc_recvmsg_thread,NULL);
	return 0;
}


