/////
// File: srv-mgcamd.c
// Modification date: 2011.11.11
/////


///////////////////////////////////////////////////////////////////////////////
// PROTO
///////////////////////////////////////////////////////////////////////////////

void mg_senddcw_cli(struct mg_client_data *cli, const struct ledger_delivery_snapshot *delivery);


struct mgcamdserver_data *getmgcamdserverbyid(uint32_t id)
{
	struct mgcamdserver_data *mgcamd = cfg.mgcamd.server;
	while (mgcamd) {
		if (!(mgcamd->flags&FLAG_DELETE))
			if (mgcamd->id==id) return mgcamd;
		mgcamd = mgcamd->next;
	}
	return NULL;
}

struct mg_client_data *getmgcamdclientbyid(uint32_t id)
{
	struct mgcamdserver_data *mgcamd = cfg.mgcamd.server;
	while (mgcamd) {
		if (!(mgcamd->flags&FLAG_DELETE)) {
			struct mg_client_data *cli = mgcamd->client;
			while (cli) {
				if (!(cli->flags&FLAG_DELETE))
					if (cli->id==id) return cli;
				cli = cli->next;
			}
		}
		mgcamd = mgcamd->next;
	}
	return NULL;
}

struct mg_client_data *getmgcamdclientbyname(struct mgcamdserver_data *mgcamd, char *name)
{
	if (!(mgcamd->flags&FLAG_DELETE)) {
		uint32_t hash = hashCode( (unsigned char *)name, strlen(name) );
		struct mg_client_data *cli = mgcamd->client;
		while (cli) {
			if (!(cli->flags&FLAG_DELETE))
				if (cli->userhash==hash)
					if ( !strcmp(cli->user,name) ) return cli;
			cli = cli->next;
		}
	}
	return NULL;
}

///////////////////////////////////////////////////////////////////////////////
// DISCONNECT
///////////////////////////////////////////////////////////////////////////////

void mg_disconnect_cli(struct mg_client_data *cli)
{
	cli->connection.status = 0;
	uint32_t ticks = GetTickCount();
	cli->connection.uptime += ticks - cli->connection.time;
	cli->connection.lastseen = ticks; // Last Seen
	close(cli->handle);
	cli->handle = -1;
	cli->parent->clipfd.update = 1;
	debugf(getdbgflag(DBG_MGCAMD,0,cli->id)," mgcamd: client '%s' disconnected\n", cli->user);
}


void mg_sendcard_add(struct cardserver_data *cs, struct mgcamdserver_data *mgcamd, struct mg_client_data *cli)
{
	uint8_t buf[4];
	struct cs_custom_data clicd; // Custom data
	if ( cs && cs->option.fsharemgcamd ) {
		int len;
		for ( len=0; len<cs->card.nbprov; len++) {
			clicd.sid = mgcamd->port;
			clicd.caid = cs->card.caid; 
			clicd.provid = cs->card.prov[len].id;
			buf[0] = EXT_ADD_CARD; buf[1]=0; buf[2]=0;
			if ( card_sharelimits(cli->sharelimits, clicd.caid, clicd.provid) )
				if ( !cs_message_send(cli->handle, &clicd, buf, 3, cli->sessionkey) ) { mg_disconnect_cli( cli ); return; }
		}
	}
}

void mg_sendcard_del(struct cardserver_data *cs, struct mgcamdserver_data *mgcamd, struct mg_client_data *cli)
{
	uint8_t buf[4];
	struct cs_custom_data clicd; // Custom data
	if ( cs && cs->option.fsharemgcamd ) {
		int len;
		for ( len=0; len<cs->card.nbprov; len++) {
			clicd.sid = mgcamd->port;
			clicd.caid = cs->card.caid; 
			clicd.provid = cs->card.prov[len].id;
			buf[0] = EXT_REMOVE_CARD; buf[1]=0; buf[2]=0;
			if ( card_sharelimits(cli->sharelimits, clicd.caid, clicd.provid) )
				cs_message_send(cli->handle, &clicd, buf, 3, cli->sessionkey);
		}
	}
}

///////////////////////////////////////////////////////////////////////////////
// CONNECT
///////////////////////////////////////////////////////////////////////////////

void *mg_connect_cli(struct connect_cli_data *param)
{
    char passwdcrypt[120];
	unsigned char keymod[14];
	int i,index;
	unsigned char sessionkey[16];
	struct cs_custom_data clicd;
	unsigned char buf[CWS_NETMSGSIZE];
	uint32_t connected_client_id = 0;

	struct mgcamdserver_data *mgcamd = param->server;
	int sock = param->sock;
	uint32_t ip = param->ip;
	free(param);

#ifdef IPLIST
	struct ip_hacker_data *ipdata = iplist_find( mgcamd->iplist, ip );
	if (ipdata) iplist_newlogin( ipdata );
#endif

	// Create random deskey
	for (i=0; i<14; i++) keymod[i] = 0xff & rand();
	// Create Multics ID
	keymod[3] = (keymod[0]^'M') + keymod[1] + keymod[2];
	keymod[7] = keymod[4] + (keymod[5]^'C') + keymod[6];
	keymod[11] = keymod[8] + keymod[9] + (keymod[10]^'S');
	// send random des key
	if ( !send_nonb(sock, keymod, 14, 500) ) {
		debugf(getdbgflag(DBG_MGCAMD,0,0)," mgcamd: error sending init sequence\n");
		close(sock);
		return NULL;
	}

	// Calc SessionKey
	des_login_key_get(keymod, mgcamd->key, 14, sessionkey);

//STAT: LOGIN INFO
	// 3. login info
	i = cs_message_receive(sock, &clicd, buf, sessionkey,5000);
	if (i<=0) {
		if (i==-2) debugf(getdbgflag(DBG_MGCAMD,0,0)," mgcamd: (%s) new connection closed, wrong des key\n", ip2string(ip));
		else debugf(getdbgflag(DBG_MGCAMD,0,0)," mgcamd: (%s) new connection closed, receive timeout\n", ip2string(ip));
		close(sock);
		return NULL;
	}

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
		debugf(getdbgflag(DBG_MGCAMD,0,0)," newcamd: wrong username length (%s)\n", ip2string(ip));
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

	pthread_mutex_lock(&prg.lockclimg);
	index = 3;
	struct mg_client_data *cli = mgcamd->client;
	int found = 0;
	char *name = (char*)(buf+index);
	uint32_t hash = hashCode( (unsigned char *)name, strlen(name) );
	while (cli) {
		if (cli->userhash==hash)
		if (!strcmp(cli->user, name)) {
			if (IS_DISABLED(cli->flags)) { // Connect only enabled clients
				debugf(getdbgflag(DBG_MGCAMD,0,cli->id)," mgcamd: connection refused for client '%s' (%s), client disabled\n", cli->user, ip2string(ip));
				pthread_mutex_unlock(&prg.lockclimg);
				close(sock);
				return NULL;
			}
			found=1;
			break;
		}
		cli = cli->next;
	}
	if (!found) {
		buf[0] = MSG_CLIENT_2_SERVER_LOGIN_NAK;
		buf[1] = 0;
		buf[2] = 0;
		//cs_message_send(sock, NULL, buf, 3, sessionkey);
		debugf(getdbgflag(DBG_MGCAMD,0,0)," mgcamd: unknown user '%s' (%s)\n", &buf[3], ip2string(ip));
		pthread_mutex_unlock(&prg.lockclimg);
		close(sock);
		return NULL;
	}

	// Check for Host
	if (cli->host) {
		struct host_data *host = cli->host;
		if (host_record_client_ip(host, ip, getseconds() + 60)) {
			debugf(getdbgflag(DBG_MGCAMD,0,cli->id)," mgcamd: Client '%s' (%s), ip refused\n",cli->user, ip2string(ip)); 
			cli->nbloginerror++;
			pthread_mutex_unlock(&prg.lockclimg);
			close(sock);
			return NULL;
		}
	}

	// Check password
	index += strlen(cli->user) +1;
	__md5_crypt(cli->pass, "$1$abcdefgh$",passwdcrypt);
	if (!strcmp(passwdcrypt,(char*)&buf[index])) {
		if (cli->ip!=ip) cli->nbdiffip++;
		//Check Reconnection
		if (cli->connection.status>0) {
			if ( tick32_elapsed(GetTickCount(), cli->connection.time) > 60000u ) {
				mg_disconnect_cli(cli);
				if (cli->ip==ip) debugf(getdbgflag(DBG_MGCAMD,0,cli->id)," mgcamd: Client '%s' (%s) already connected\n",cli->user, ip2string(ip));
				else {
					debugf(getdbgflag(DBG_MGCAMD,0,cli->id)," mgcamd: Client '%s' (%s) already connected with different ip, Connections closed (%s)\n", cli->user, ip2string(cli->ip), ip2string(ip));
					cli->nbloginerror++;
					pthread_mutex_unlock(&prg.lockclimg);
					close(sock);
					return NULL;
				}
			}
			else {
				debugf(getdbgflag(DBG_MGCAMD,0,cli->id)," mgcamd: Client '%s' just connected, new connection closed (%s)\n", cli->user, ip2string(ip));
				cli->nbloginerror++;
				pthread_mutex_unlock(&prg.lockclimg);
				close(sock);
				return NULL;
			}
		}

		cli->nblogin++;

#ifdef IPLIST
		if (ipdata) iplist_goodlogin(ipdata);
#endif
		// Store program id
		cli->progid = clicd.sid;

		buf[0] = MSG_CLIENT_2_SERVER_LOGIN_ACK;
		buf[1] = 0;
		buf[2] = 0;

		//clicd.msgid = 0;
		clicd.sid = 0x6E73;
		clicd.caid = 0;
		clicd.provid = 0x14000000; // mgcamd protocol version?
		cs_message_send(sock, &clicd, buf, 3, sessionkey);
		//
		des_login_key_get( mgcamd->key, (unsigned char*)passwdcrypt, strlen(passwdcrypt),sessionkey);
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
			debugf(getdbgflag(DBG_MGCAMD,0,cli->id)," mgcamd: client '%s' connected (%s)\n", cli->user, ip2string(ip));
			connected_client_id = cli->id;
			pthread_mutex_unlock(&prg.lockclimg);
#ifdef EPOLL_MGCAMD
			pipe_send_u32(prg.pipe.mgcamd[1], PIPE_CLI_CONNECTED, connected_client_id);
#else
		pipe_wakeup( prg.pipe.mgcamd[1] );
#endif
		// update pfd data
		cli->parent->clipfd.update = 1;
	}
	else {
		// send NAK
		buf[0] = MSG_CLIENT_2_SERVER_LOGIN_NAK;
		buf[1] = 0;
		buf[2] = 0;
		//cs_message_send(sock, NULL, buf, 3, sessionkey);
		debugf(getdbgflag(DBG_MGCAMD,0,cli->id)," mgcamd: client '%s' wrong password (%s)\n", cli->user, ip2string(ip));
		cli->nbloginerror++;
		pthread_mutex_unlock(&prg.lockclimg);
		close(sock);
	}
	return NULL;
}



void mgcamd_srv_accept(struct mgcamdserver_data *srv)
{
	struct sockaddr_in newaddr;
	socklen_t socklen = sizeof(struct sockaddr);
	int newfd = accept( srv->handle, (struct sockaddr*)&newaddr, /*(socklen_t*)*/&socklen);
	if ( newfd<=0 ) {
		if ( (errno!=EAGAIN) && (errno!=EINTR) ) debugf( getdbgflag(DBG_MGCAMD,0,0)," Mgcamd%d: Accept failed (errno=%d)\n", srv->id,errno);
	}
	else {
		SetSocketReuseAddr(newfd);
		uint32_t newip = newaddr.sin_addr.s_addr;
#ifdef IPLIST
		struct ip_hacker_data *ipdata = iplist_find( srv->iplist, newip );
		if (!ipdata) {
			ipdata = iplist_add( newip );
			ipdata->next = srv->iplist;
			srv->iplist = ipdata;
		}
#endif
		if ( isblockedip(newip) ) {
			debugf(getdbgflag(DBG_MGCAMD,0,0)," Mgcamd%d: New Connection (%s) closed, ip blocked\n", srv->id, ip2string(newip) );
			close(newfd);
		}
#ifdef IPLIST
		else if ( !iplist_accept( ipdata ) ) {
			debugf(getdbgflag(DBG_CCCAM,srv->id,0)," Mgcamd%d: New Connection (%s) closed, ip temporary blocked\n", srv->id, ip2string(newip) );
			close(newfd);
		}
#endif
		else {
			pthread_t srv_tid;
			if (cfg.cccam.keepalive) SetSocketKeepalive(newfd);
			SetSocketNoDelay(newfd);
			if (SetSoketNonBlocking(newfd) < 0) {
				debugf(DBG_ERROR, " Mgcamd%d: unable to set accepted socket nonblocking (errno=%d)\n", srv->id, errno);
				close(newfd);
				return;
			}
			//debugf(getdbgflag(DBG_MGCAMD,0,0)," Mgcamd%d: new connection (%s)\n", srv->id, ip2string(newip) );
			struct connect_cli_data *newdata = malloc( sizeof(struct connect_cli_data) );
			newdata->server = srv; 
			newdata->sock = newfd; 
			newdata->ip = newaddr.sin_addr.s_addr;
			if ( !create_thread(&srv_tid, (threadfn)mg_connect_cli,newdata) ) {
				free( newdata );
				close( newfd );
			}
		}
	}
}

#ifndef MONOTHREAD_ACCEPT

void *mgcamd_accept_thread(void *param)
{
	prctl(PR_SET_NAME,"MGcamd Accept Thread",0,0,0);
	sleep(5);

	while(!prg.restart) {
		uint64_t config_generation;
		struct pollfd pfd[18];
		int pfdcount = 0;
		newcamd_config_read_lock();
		config_generation = cfg.cache.generation;
		struct mgcamdserver_data *mgcamd = cfg.mgcamd.server;
		while (mgcamd) {
			if ( !IS_DISABLED(mgcamd->flags) && (mgcamd->handle>0) ) {
				mgcamd->ipoll = pfdcount;
				pfd[pfdcount].fd = mgcamd->handle;
				pfd[pfdcount++].events = POLLIN | POLLPRI;
			} else mgcamd->ipoll = -1;
			mgcamd = mgcamd->next;
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
				struct mgcamdserver_data *mgcamd = cfg.mgcamd.server;
				while (mgcamd) {
					if ( !IS_DISABLED(mgcamd->flags) && (mgcamd->handle>0) &&
					     (mgcamd->ipoll>=0) && (mgcamd->handle==pfd[mgcamd->ipoll].fd) &&
					     (pfd[mgcamd->ipoll].revents & (POLLIN|POLLPRI)) )
						mgcamd_srv_accept(mgcamd);
					mgcamd = mgcamd->next;
				}
				newcamd_config_read_unlock();
			} else if (retval < 0) usleep(96000);
		} else sleep(1);
	}
	return NULL;
}
#endif

///////////////////////////////////////////////////////////////////////////////
// RECEIVE MESSAGES
///////////////////////////////////////////////////////////////////////////////


void mg_store_ecmclient( ECM_DATA *ecm, struct mg_client_data *cli, int climsgid)
{
	cli->ecm.recvtime = GetTickCount();
	cli->ecm.request = ecm;
#ifdef CACHEEX
	__atomic_store_n(&cli->ecm.lifetime_id, ecm_request_lifetime_id_load(ecm), __ATOMIC_RELEASE);
#endif
    cli->ecm.climsgid = climsgid;
    cli->ecm.status = STAT_ECM_SENT;
	ecm_addip(ecm, cli->ip);
}



///////////////////////////////////////////////////////////////////////////////

int mg_cli_getmsg(struct mg_client_data *cli, uint8_t *buf, struct cs_custom_data *clicd)
{
	int len;
	if (cli->handle<=0) return 0; // disconnected

#ifdef RECVMSG_PEEK

	len = cs_msg_chkrecv(cli->handle);
	if (len==0) {
		debugf(getdbgflag(DBG_MGCAMD,0,cli->id)," mgcamd: client '%s' read failed %d\n", cli->user,len);
		mg_disconnect_cli(cli);
	}
	else if (len==-1) {
		if (!cli->chkrecvtime) cli->chkrecvtime = GetTickCount();
		else if (tick32_elapsed(GetTickCount(), cli->chkrecvtime) > 300u) {
			debugf(getdbgflag(DBG_MGCAMD,0,cli->id)," mgcamd: client '%s' read failed %d\n", cli->user,len);
			mg_disconnect_cli(cli);
		}
	}
	else if (len>0) {
		cli->chkrecvtime = 0;
		len = cs_message_receive(cli->handle, clicd, buf, cli->sessionkey,3);
		if (len==0) {
			debugf(getdbgflag(DBG_MGCAMD,0,cli->id)," mgcamd: client '%s' read failed %d\n", cli->user,len);
			mg_disconnect_cli(cli);
		}
		else if (len<0) {
			debugf(getdbgflag(DBG_MGCAMD,0,cli->id)," mgcamd: client '%s' read failed %d(%d)\n", cli->user,len,errno);
			mg_disconnect_cli(cli);
		}
		else return len;
	}

#else 
	len = cs_peekmsg( cli->handle, &cli->msg, cli->sessionkey, clicd, buf );
	uint32_t ticks = GetTickCount();
	if (len==0) {
		debugf(getdbgflag(DBG_MGCAMD,0,cli->id)," mgcamd: client '%s' read failed %d\n", cli->user,len);
		mg_disconnect_cli(cli);
	}
	else if (len==-1) {
		if (!cli->chkrecvtime) cli->chkrecvtime = ticks;
		else if ((uint32_t)(ticks - cli->chkrecvtime) > 1000u) {
			debugf(getdbgflag(DBG_MGCAMD,0,cli->id)," mgcamd: client '%s' read failed %d\n", cli->user,len);
			mg_disconnect_cli(cli);
		}
	}
	else return len;
#endif

	return 0;
}


void mg_cli_recvmsg(struct mg_client_data *cli)
{
	struct cs_custom_data clicd; // Custom data
	struct cardserver_data *cs=NULL; 
	int len;
	unsigned char buf[CWS_NETMSGSIZE];

	unsigned char ecmdata[CWS_NETMSGSIZE]; // for other use
	int ecmlen;

	len = mg_cli_getmsg(cli, buf, &clicd);
	if (len<=0) return;

	uint32_t ticks = GetTickCount();
	cli->chkrecvtime = 0;
	cli->lastactivity = ticks;
	struct mgcamdserver_data *mgcamd = cli->parent;

	switch ( buf[0] ) {

		case EXT_GET_VERSION:
			memset( buf, 0, sizeof(buf) );
			buf[0] = EXT_GET_VERSION; buf[1]=0; buf[2]=0;
			buf[3] = 0x31;
			buf[4] = 0x2e;
			buf[5] = 0x36;
			buf[6] = 0x37;// 1.67
			if ( !cs_message_send(cli->handle, 0, buf, 7, cli->sessionkey) ) { mg_disconnect_cli( cli ); return; }
			break;

		case MSG_CARD_DATA_REQ:
			memset( buf, 0, sizeof(buf) );
			buf[0] = MSG_CARD_DATA; buf[1]=0; buf[2]=0;
			buf[3] = 2;
			buf[4] = 0;
			buf[5] = 0;
			buf[14] = 1;
			if ( !cs_message_send(cli->handle, &clicd, buf, 15+11, cli->sessionkey) ) { mg_disconnect_cli( cli ); return; }
			if (cli->csport[0]) {
				int i;
				for (i=0;i<MAX_CSPORTS;i++) {
					if (!cli->csport[i]) break;
					cs = getcsbyport(cli->csport[i]);
					if ( cs && cs->option.fsharemgcamd && !(cli->flags&FLAG_EXPIRED) )
					for (len=0; len<cs->card.nbprov; len++) {
						clicd.sid = mgcamd->port;
						clicd.caid = cs->card.caid; 
						clicd.provid = cs->card.prov[len].id;
						buf[0] = EXT_ADD_CARD; buf[1]=0; buf[2]=0;
						if ( card_sharelimits(cli->sharelimits, clicd.caid, clicd.provid) )
							if ( !cs_message_send(cli->handle, &clicd, buf, 3, cli->sessionkey) ) { mg_disconnect_cli( cli ); return; }
					}
				}
			}
			else if (mgcamd->csport[0]) {
				int i;
				for(i=0;i<MAX_CSPORTS;i++) {
					if (!mgcamd->csport[i]) break;
					cs = getcsbyport(mgcamd->csport[i]);
					if ( cs && cs->option.fsharemgcamd && !(cli->flags&FLAG_EXPIRED) )
					for (len=0; len<cs->card.nbprov; len++) {
						clicd.sid = mgcamd->port;
						clicd.caid = cs->card.caid; 
						clicd.provid = cs->card.prov[len].id;
						buf[0]=EXT_ADD_CARD; buf[1]=0; buf[2]=0;
						if ( card_sharelimits(cli->sharelimits, clicd.caid, clicd.provid) )
							if ( !cs_message_send(cli->handle, &clicd, buf, 3, cli->sessionkey) ) { mg_disconnect_cli( cli ); return; }
					}
				}
			}
			else {
				cs = cfg.cardserver;
				while (cs) {
					if ( cs->option.fsharemgcamd && !(cli->flags&FLAG_EXPIRED) )
					for (len=0; len<cs->card.nbprov; len++) {
						clicd.sid = mgcamd->port;
						clicd.caid = cs->card.caid; 
						clicd.provid = cs->card.prov[len].id;
						buf[0]=EXT_ADD_CARD; buf[1]=0; buf[2]=0;
						if ( card_sharelimits(cli->sharelimits, clicd.caid, clicd.provid) )
							if ( !cs_message_send(cli->handle, &clicd, buf, 3, cli->sessionkey) ) {	mg_disconnect_cli( cli ); return; }
					}
					cs = cs->next;
				}
			}

			debugf(getdbgflag(DBG_MGCAMD,0,cli->id)," mgcamd: send card data to client '%s'\n", cli->user);
			cli->cardsent = 1;
			break;

		case 0x80:
		case 0x81:
					//debugdump(buf, len, "ECM: ");
					cli->lastecmtime = ticks;
					//cli->ecmnb++; new var for not accepted ecm's
					cli->ecmnb++;
					ecmlen = len;
					memcpy( ecmdata, buf, len);
					uint32_t provid = ecm_getprovid( ecmdata, ecmlen, clicd.caid );
					if (provid!=0) clicd.provid = provid;

					if (cli->ecm.busy) {
						cli->ecmdenied++;
						// send decode failed
						buf[1] = 0; buf[2] = 0;
						if ( !cs_message_send( cli->handle, &clicd, buf, 3, cli->sessionkey) )  { mg_disconnect_cli( cli ); return; }
						debugf(getdbgflag(DBG_MGCAMD,0,cli->id)," <|> decode failed to mgcamd client '%s' ch %04x:%06x:%04x, too many ecm requests\n", cli->user,clicd.caid,clicd.provid,clicd.sid);
						break;
					}
					// Check for CAID&SID
					if ( !clicd.caid ) {
						cli->ecmdenied++;
						// send decode failed
						buf[1] = 0; buf[2] = 0;
						if ( !cs_message_send( cli->handle, &clicd, buf, 3, cli->sessionkey) ) { mg_disconnect_cli( cli ); return; }
						debugf(getdbgflag(DBG_MGCAMD,0,cli->id)," <|> decode failed to mgcamd client '%s' ch %04x:%06x:%04x Invalid CAID\n", cli->user,clicd.caid,clicd.provid,clicd.sid);
						break;
					}
					int i,j,port;
					if (cli->csport[0]) {
						for(i=0; i<MAX_CSPORTS; i++) {
							port = cli->csport[i];
							if (!port) break;
							cs = getcsbyport(port);
							if (cs)
							if (cs->option.fsharemgcamd)
							if (clicd.caid==cs->card.caid) {
								for (j=0; j<cs->card.nbprov;j++) if (clicd.provid==cs->card.prov[j].id) break;
								if (j<cs->card.nbprov) break;
							}
						}
						if (!port || !cs) {
							cli->ecmdenied++;
							// send decode failed
							buf[1] = 0; buf[2] = 0;
							if ( !cs_message_send( cli->handle, &clicd, buf, 3, cli->sessionkey) ) { mg_disconnect_cli( cli ); return; }
							debugf(getdbgflag(DBG_MGCAMD,0,cli->id)," <|> decode failed to client '%s' ch %04x:%06x:%04x, Invalid CAID/PROVIDER\n", cli->user,clicd.caid,clicd.provid,clicd.sid);
							break;
						}
					}
					else {
						cs = cfg.cardserver;
						while (cs) {
							if (cs->option.fsharemgcamd)
							if (clicd.caid==cs->card.caid) {
								for (j=0; j<cs->card.nbprov;j++) if (clicd.provid==cs->card.prov[j].id) break;
								if (j<cs->card.nbprov) break;
							}
							cs = cs->next;
						}
						if (!cs) {
							cli->ecmdenied++;
							// send decode failed
							buf[1] = 0; buf[2] = 0;
							if ( !cs_message_send( cli->handle, &clicd, buf, 3, cli->sessionkey) ) { mg_disconnect_cli( cli ); return; }
							debugf(getdbgflag(DBG_MGCAMD,0,cli->id)," <|> decode failed to client '%s' ch %04x:%06x:%04x, Invalid CAID/PROVIDER\n", cli->user,clicd.caid,clicd.provid,clicd.sid);
							break;
						}
					}
					// Check for Accepted sids
					uint8_t cw1cycle;
					if ( !accept_sid(cs, clicd.provid, clicd.sid, ecm_getchid(ecmdata,ecmlen,clicd.caid), ecmlen, &cw1cycle) ) {
						cli->ecmdenied++;
						cs->ecmdenied++;
						// send decode failed
						buf[1] = 0; buf[2] = 0;
						if ( !cs_message_send( cli->handle, &clicd, buf, 3, cli->sessionkey) ) { mg_disconnect_cli( cli ); return; }
						debugf(getdbgflagpro(DBG_MGCAMD,0,cli->id,cs->id)," <|> decode failed to mgcamd client '%s' ch %04x:%06x:%04x SID not accepted\n", cli->user,clicd.caid,clicd.provid,clicd.sid);
						break;
					}

					if ( !accept_ecmlen(len) ) {
						cli->ecmdenied++;
						cs->ecmdenied++;
						// send decode failed
						buf[1] = 0; buf[2] = 0;
						if ( !cs_message_send( cli->handle, &clicd, buf, 3, cli->sessionkey) ) { mg_disconnect_cli( cli ); return; }
						debugf(getdbgflagpro(DBG_MGCAMD,0,cli->id,cs->id)," <|> decode failed to mgcamd client '%s' ch %04x:%06x:%04x, ecm length error(%d)\n", cli->user,clicd.caid,clicd.provid,clicd.sid,len);
						break;
					}

					// ACCEPTED
					ecm_lock(); //###

					// Search for ECM
					ECM_DATA *ecm = search_ecmdata_any(cs, ecmdata, ecmlen, clicd.sid, clicd.caid, clicd.provid, 1); // dont get failed ecm request from cache
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
					if ( (cli->lastecm.caid==clicd.caid) && (cli->lastecm.prov==clicd.provid) && (cli->lastecm.sid==clicd.sid) && cli->lastecm.status
						&& ecm && (ecm->hash==cli->lastecm.hash) && (ecm->ecm[0]==cli->lastecm.tag) ) {
						if ( retry_classify( ticks, cli->lastdcwtime, cli->lastecm.status, retry_window_ms, soft_fail_window_ms )==RETRY_HARD ) {
							attest_note( ATTEST_TAG_MGCAMD | (uint32_t)cli->id, cli->lastecm.dcw,
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
								// send decode failed
								buf[1] = 0; buf[2] = 0;
								cs_message_send(cli->handle, &clicd, buf, 3, cli->sessionkey);
								debugf(getdbgflagpro(DBG_MGCAMD,0,cli->id,cs->id)," <|> decode failed to mgcamd client '%s' ch %04x:%06x:%04x, already failed\n", cli->user,clicd.caid,clicd.provid,clicd.sid);
							}
							else {
								ecm->period++; // RETRY
								mg_store_ecmclient(ecm, cli, clicd.msgid);
								debugf(getdbgflagpro(DBG_MGCAMD,0,cli->id, cs->id)," <- ecm from mgcamd client '%s' ch %04x:%06x:%04x:%08x**\n",cli->user,clicd.caid,clicd.provid,clicd.sid,ecm->hash);
								cli->ecm.busy=1;
								cli->ecm.hash = ecm->hash;
								ecm_request_set_dcwstatus(ecm, STAT_DCW_WAIT);
								ecm->cachestatus = 0; //ECM_CACHE_NONE; // Resend Request
								ecm->checktime = 1; // Check NOW
								pipe_wakeup( prg.pipe.ecm[1] );
							}
						}
						else {
							//TODO: Add another card for sending ecm
							mg_store_ecmclient(ecm, cli, clicd.msgid);
							debugf(getdbgflagpro(DBG_MGCAMD,0,cli->id, cs->id)," <- ecm from mgcamd client '%s' ch %04x:%06x:%04x:%08x*\n",cli->user,clicd.caid,clicd.provid,clicd.sid,ecm->hash);
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
										ecm_unlock(); //###
										mg_senddcw_cli(cli,delivery);
										break;
									}
								}
								else {
									LEDGER_CAPTURE_CLIENT_DELIVERY(delivery, ecm, cli);
									ecm_unlock(); //###
									mg_senddcw_cli(cli,delivery);
									break;
								}
							}
						}
					}
					else {
						// Setup ECM Request for Server(s)
						ecm = store_ecmdata(cs, ecmdata, ecmlen, clicd.sid,clicd.caid,clicd.provid);
						if (!ecm) {
							cs->ecmdenied++;
							cli->ecmdenied++;
							uint8_t failure[3] = { ecmdata[0], 0, 0 };
							if (!cs_message_send(cli->handle, &clicd, failure, sizeof(failure), cli->sessionkey)) mg_disconnect_cli(cli);
							ecm_unlock();
							break;
						}
						cs->ecmaccepted++;
						mg_store_ecmclient(ecm, cli, clicd.msgid);
						debugf(getdbgflagpro(DBG_MGCAMD,0,cli->id, cs->id)," <- ecm from mgcamd client '%s' ch %04x:%06x:%04x:%08x\n",cli->user,clicd.caid,clicd.provid,clicd.sid,ecm->hash);
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

#ifdef TESTCHANNEL
						int testchannel = ( (ecm->caid==cfg.testchn.caid)&&(ecm->provid==cfg.testchn.provid)&&(!cfg.testchn.sid||(ecm->sid==cfg.testchn.sid)) );
						if (testchannel) {
							fdebugf(" <- ecm from Mgcamd client '%s' ch %04x:%06x:%04x %02x:%08x\n", cli->user, ecm->caid, ecm->provid, ecm->sid, ecm->ecm[0], ecm->hash);
						}
#endif
					}

					ecm_unlock(); //###
					if (isnew) wakeup_sendecm();
					break;

#ifdef SRV_CSCACHE
				// Incoming DCW from client
		case 0xC0:
		case 0xC1:
					cli->lastecmtime = ticks;
					if (!cli->ecm.busy) break;

					ecm_lock(); //###

					ecm = cli->ecm.request;
					if (!ecm) {
						debugf(getdbgflag(DBG_MGCAMD,0,cli->id)," <!= wrong cw from Client '%s': pending ECM no longer exists\n", cli->user);
						ecm_unlock();
						break;
					}
					if (ecm->hash!=cli->ecm.hash) {
						debugf(getdbgflagpro(DBG_MGCAMD,0,cli->id,(ecm->cs ? ecm->cs->id : 0))," <!= wrong cw from Client '%s' ch %04x:%06x:%04x\n", cli->user, ecm->caid,ecm->provid,ecm->sid, ticks-cli->lastecmtime);
						ecm_unlock();
						break;
					}
					if ( (ecm->caid!=clicd.caid) || (ecm->sid!=clicd.sid) || (ecm->provid!=clicd.provid) || (cli->ecm.climsgid!=clicd.msgid) ) {
						debugf(getdbgflagpro(DBG_MGCAMD,0,cli->id,(ecm->cs ? ecm->cs->id : 0))," <!= wrong cw from Client '%s' ch %04x:%06x:%04x\n", cli->user, ecm->caid,ecm->provid,ecm->sid, ticks-cli->lastecmtime);
						ecm_unlock();
						break;
					}
					if ( (buf[0]&0x81)!=ecm->ecm[0] ) {
						debugf(getdbgflagpro(DBG_MGCAMD,0,cli->id,(ecm->cs ? ecm->cs->id : 0))," <!= wrong cw from Client '%s' ch %04x:%06x:%04x\n", cli->user, ecm->caid,ecm->provid,ecm->sid, ticks-cli->lastecmtime);
						ecm_unlock();
						break;
					}
					if (buf[2]!=0x10) {
						debugf(getdbgflagpro(DBG_MGCAMD,0,cli->id,(ecm->cs ? ecm->cs->id : 0))," <!= wrong cw from Client '%s' ch %04x:%06x:%04x\n", cli->user, ecm->caid,ecm->provid,ecm->sid, ticks-cli->lastecmtime);
						ecm_unlock();
						break;
					}
					// Check for DCW
					if (!acceptDCW_profile_ecm(&buf[3], ecm->cs, ecm->caid, ecm->provid, ecm->sid)) {
						debugf(getdbgflagpro(DBG_MGCAMD,0,cli->id,(ecm->cs ? ecm->cs->id : 0))," <!= wrong cw from Client '%s' ch %04x:%06x:%04x\n", cli->user, ecm->caid,ecm->provid,ecm->sid, ticks-cli->lastecmtime);
						ecm_unlock();
						break;
					}
					debugf(getdbgflagpro(DBG_MGCAMD,0,cli->id,(ecm->cs ? ecm->cs->id : 0))," <= cw from mgcamd client '%s' ch %04x:%06x:%04x (%dms)\n", cli->user, ecm->caid,ecm->provid,ecm->sid, ticks-cli->lastecmtime);
					if (ecm->dcwstatus!=STAT_DCW_SUCCESS) {
						static char msg[] = "Good dcw from mgcamd Client";
						ecm->statusmsg = msg;
						// Store ECM Answer
						ecm_setdcw( ecm, &buf[3], DCW_SOURCE_MGCLIENT, cli->id );
					}
					else {	//TODO: check same dcw between cards
						if ( memcmp(&ecm->cw, &buf[3],16) ) debugf(getdbgflagpro(DBG_MGCAMD,0,cli->id,(ecm->cs ? ecm->cs->id : 0))," !!! different dcw from newcamd client '%s'\n", cli->user);
					}

					ecm_unlock(); //###
					break;
#endif

		default:
					if (buf[0]==MSG_KEEPALIVE) {
#ifdef SRV_CSCACHE
						// Check for Cache client??
						if (clicd.sid==(('C'<<8)|'H')) { clicd.caid = ('O'<<8)|'K'; cli->cachedcw++; }
#endif
						if ( !cs_message_send(cli->handle, &clicd, buf, 3, cli->sessionkey) ) mg_disconnect_cli( cli );
					}
					else {
						debugf(getdbgflag(DBG_MGCAMD,0,cli->id)," mgcamd: unknown message type '%02x' from client '%s'\n",buf[0],cli->user);
						buf[1]=0; buf[2]=0;
						if ( !cs_message_send( cli->handle, &clicd, buf, 3, cli->sessionkey) ) mg_disconnect_cli( cli );
					}

	} // Switch
}

///////////////////////////////////////////////////////////////////////////////
// MGCAMD SERVER:  SEND DCW TO CLIENTS
///////////////////////////////////////////////////////////////////////////////

void mg_senddcw_cli(struct mg_client_data *cli, const struct ledger_delivery_snapshot *delivery)
{
	unsigned char buf[CWS_NETMSGSIZE];
	struct cs_custom_data clicd; // Custom data
	uint32_t ticks = GetTickCount();
#ifdef CACHEEX
	if (!delivery || !delivery->lifetime_id ||
	    delivery->lifetime_id != __atomic_load_n(&cli->ecm.lifetime_id, __ATOMIC_ACQUIRE)) return;
#endif

	if (cli->ecm.status==STAT_DCW_SENT) {
		debugf(getdbgflag(DBG_MGCAMD,0,cli->id)," +> cw send failed to mgcamd client '%s', cw already sent\n", cli->user); 
		return;
	}
	if (cli->handle==INVALID_SOCKET) {
		debugf(getdbgflag(DBG_MGCAMD,0,cli->id)," +> cw send failed to mgcamd client '%s', client disconnected\n", cli->user); 
		return;
	}
	if (!cli->ecm.busy) {
		debugf(getdbgflag(DBG_MGCAMD,0,cli->id)," +> cw send failed to mgcamd client '%s', no ecm request\n", cli->user); 
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
	int enablefreeze = 0;	/* TASK 1.10a: was uninitialised, read as freeze noise */
	if (cli->lastecm.caid==response_caid && cli->lastecm.prov==response_provid && cli->lastecm.sid==response_sid) {
		if (cli->lastecm.hash!=response_hash && cli->lastecm.status==1 &&
		    (uint32_t)(ticks-cli->lastdcwtime)>200u) enablefreeze = 1;
	} else cli->zap++;
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

	clicd.msgid = cli->ecm.climsgid;
	clicd.sid = response_sid;
	clicd.caid = response_caid;
	clicd.provid = response_provid;

#ifdef CACHEEX
	if (delivery->status == STAT_DCW_SUCCESS) {
#else
	if (ecm->hash==cli->ecm.hash && ecm->dcwstatus==STAT_DCW_SUCCESS) {
#endif
		uint32_t sent_at;
#ifdef CACHEEX
		buf[0] = delivery->tag;
#else
		buf[0] = ecm->ecm[0];
#endif
		buf[1] = 0;
		buf[2] = 0x10;
#ifdef CACHEEX
		memcpy(&buf[3], delivery->cw, 16);
#else
		memcpy(&buf[3], ecm->cw, 16);
#endif
		if ( !cs_message_send( cli->handle, &clicd, buf, 19, cli->sessionkey) ) {
			mg_disconnect_cli( cli );
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
		cli->ecmoktime += sent_at-response_recvtime;
		cli->lastdcwtime = sent_at;
#ifdef CACHEEX
		ledger_note_delivered(delivery, sent_at);
#endif
		debugf(getdbgflagpro(DBG_MGCAMD,0,cli->id,response_profile)," => cw to mgcamd client '%s' ch %04x:%06x:%04x (%dms)\n", cli->user, response_caid,response_provid,response_sid, sent_at-response_recvtime);
	}
	else { //if (ecm->data->dcwstatus==STAT_DCW_FAILED)
		if (enablefreeze) cli->freeze++;
		cli->lastecm.status=0;
		cli->lastecm.dcwsrctype = DCW_SOURCE_NONE;
		cli->lastecm.dcwsrcid = 0;
		memset(cli->lastecm.dcw, 0, sizeof(cli->lastecm.dcw));
#ifdef CACHEEX
		memset(cli->lastecm.ecmd5, 0, sizeof(cli->lastecm.ecmd5));
		cli->lastecm.ecmlen = 0;
#endif
		buf[0] = response_tag;
		buf[1] = 0;
		buf[2] = 0;
		if ( !cs_message_send(  cli->handle, &clicd, buf, 3, cli->sessionkey) ) {
			mg_disconnect_cli( cli );
			return;
		}
		debugf(getdbgflagpro(DBG_MGCAMD,0,cli->id,response_profile)," |> decode failed to mgcamd client '%s' ch %04x:%06x:%04x (%dms)\n", cli->user, response_caid,response_provid,response_sid, GetTickCount()-response_recvtime);
	}
	cli->ecm.busy=0;
	cli->ecm.status = STAT_DCW_SENT;
}

///////////////////////////////////////////////////////////////////////////////

// Check sending cw to clients
void mg_check_sendcw(ECM_DATA *ecm, const struct ledger_delivery_snapshot *delivery)
{
#ifdef CACHEEX
	if (!delivery) return;
#endif
	struct mgcamdserver_data *mgcamd = cfg.mgcamd.server;
	while (mgcamd) {
		if ( !IS_DISABLED(mgcamd->flags) && (mgcamd->handle>0) ) {
			struct mg_client_data *cli = mgcamd->client;
			while (cli) {
				#ifdef CACHEEX
				if (!IS_DISABLED(cli->flags) && cli->handle!=INVALID_SOCKET && __atomic_load_n(&cli->ecm.lifetime_id, __ATOMIC_ACQUIRE)==delivery->lifetime_id) {
#else
				if (!IS_DISABLED(cli->flags) && cli->handle!=INVALID_SOCKET && cli->ecm.busy && cli->ecm.request==ecm && cli->ecm.status==STAT_ECM_SENT) {
#endif
					mg_senddcw_cli( cli, delivery );
				}
				cli = cli->next;
			}
		}
		mgcamd = mgcamd->next;
	}
}


///////////////////////////////////////////////////////////////////////////////

void mg_recv_pipe()
{
	struct mgcamdserver_data *mgcamd;
	struct cardserver_data *cs;
	struct cardserver_data card_event;

	struct mg_client_data *cli;

	uint8_t buf[1024];
	struct pollfd pfd[2];
	do {
		int len = pipe_recv( prg.pipe.mgcamd[0], buf, sizeof(buf));
		if (len>0) {
			switch(buf[0]) {
				case PIPE_WAKEUP:  // ADD NEW CLIENT
					if (len != 2) break;
					//debugf(" wakeup csmsg\n");
					break;
#ifdef EPOLL_MGCAMD
					case PIPE_CLI_CONNECTED:  // ADD NEW FD by stable client id
						{
							uint32_t client_id;
							struct epoll_event ev;
							if (!pipe_decode_u32_message(buf, (size_t)len, PIPE_CLI_CONNECTED, &client_id) || !client_id) break;
							cli = getmgcamdclientbyid(client_id);
							if (!cli || cli->handle <= 0 || IS_DISABLED(cli->flags)) break;
							memset(&ev, 0, sizeof(ev));
							ev.events = EPOLLIN | EPOLLPRI | EPOLLRDHUP;
							ev.data.u64 = client_id;
							if (epoll_ctl(prg.epoll.mgcamd, EPOLL_CTL_ADD, cli->handle, &ev) == -1)
								debugf(DBG_ERROR,"Err! EPOLL_CTL_ADD %s (%d)\n", cli->user, cli->handle);
						}
						break;
#endif
					case PIPE_CARD_DEL:
					case PIPE_CARD_ADD:
						{
							struct pipe_card_snapshot snapshot;
							struct mg_client_data *cardcli;
							int i;
							if (!pipe_decode_card_message(buf, (size_t)len, buf[0], &snapshot) ||
							    snapshot.provider_count > CARD_MAXPROV) break;
							memset(&card_event, 0, sizeof(card_event));
							card_event.id = snapshot.profile_id;
							card_event.card.caid = snapshot.caid;
							card_event.card.nbprov = snapshot.provider_count;
							card_event.option.fsharecccam = !!(snapshot.share_flags & PIPE_CARD_SHARE_CCCAM);
							card_event.option.fsharemgcamd = !!(snapshot.share_flags & PIPE_CARD_SHARE_MGCAMD);
							for (i = 0; i < snapshot.provider_count; i++)
								card_event.card.prov[i].id = snapshot.provider_ids[i];
							cs = &card_event;
							mgcamd = cfg.mgcamd.server;
							while (mgcamd) {
								if (!IS_DISABLED(mgcamd->flags) && mgcamd->handle > 0) {
									cardcli = mgcamd->client;
									while (cardcli) {
										if (!IS_DISABLED(cardcli->flags) && cardcli->handle > 0) {
											if (buf[0] == PIPE_CARD_DEL) mg_sendcard_del(cs, mgcamd, cardcli);
											else mg_sendcard_add(cs, mgcamd, cardcli);
										}
										cardcli = cardcli->next;
									}
								}
								mgcamd = mgcamd->next;
							}
						}
						break;

			}
		}
		pfd[0].fd = prg.pipe.mgcamd[0];
		pfd[0].events = POLLIN | POLLPRI;
	} while (poll(pfd, 1, 3)>0);
}

///////////////////////////////////////////////////////////////////////////////

#ifdef EPOLL_MGCAMD

void *mg_recvmsg_thread(void *param)
{
	int i;

	cfg.mgcamd.pid_recvmsg = syscall(SYS_gettid);
	prg.pid_mg_msg = syscall(SYS_gettid);
	prctl(PR_SET_NAME,"Mgcamd RecvMSG",0,0,0);

	struct epoll_event evlist[MAX_EPOLL_EVENTS]; // epoll recv events

	prg.epoll.mgcamd = epoll_create( MAX_EPOLL_EVENTS );
	// Add PIPE
	struct epoll_event ev; // epoll event
	memset(&ev, 0, sizeof(ev));
	ev.events = EPOLLIN | EPOLLPRI | EPOLLRDHUP;
	ev.data.u64 = 0;
	if ( epoll_ctl(prg.epoll.mgcamd, EPOLL_CTL_ADD, prg.pipe.mgcamd[0], &ev) == -1 ) printf("epoll_ctl");

	while (!prg.restart) {
		newcamd_config_read_lock();
		uint64_t config_generation = cfg.cache.generation;
		uint32_t thread_delay = cfg.delay.thread;
		newcamd_config_read_unlock();
		int ready = epoll_wait( prg.epoll.mgcamd, evlist, MAX_EPOLL_EVENTS, 1003);
		if (ready < 0) {
			if ( (errno==EINTR)||(errno==EAGAIN) ) usleep(1000);
			else {
				usleep(99000);
				debugf(DBG_ERROR,"Err! epoll_wait (%d)", errno);
			}
			continue;
		}
		if (ready == 0) continue; // timeout
		if (thread_delay) usleep(thread_delay);

		newcamd_config_read_lock();
		if (config_generation != cfg.cache.generation) {
			newcamd_config_read_unlock();
			continue;
		}
		for (i=0; i < ready; i++) {
			uint32_t client_id = (uint32_t)evlist[i].data.u64;
			if ( evlist[i].events & (EPOLLIN|EPOLLPRI) ) {
				if (!client_id) mg_recv_pipe();
				else {
					struct mg_client_data *cli = getmgcamdclientbyid(client_id);
					if (cli && cli->handle > 0) mg_cli_recvmsg(cli);
				}
			}
			else if ( evlist[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR) ) {
				if (!client_id) debugf(DBG_ERROR,"Err! epoll_wait() pipe\n");
				else {
					struct mg_client_data *cli = getmgcamdclientbyid(client_id);
					if (cli && cli->handle > 0) mg_disconnect_cli(cli);
				}
			}
		}
		newcamd_config_read_unlock();
	}
	return NULL;
}

#else

void *mg_recvmsg_thread(void *param)
{
	struct pollfd pfd[MAX_PFD];
	int pfdcount;
	uint32_t thread_delay;

	cfg.mgcamd.pid_recvmsg = syscall(SYS_gettid);
	prg.pid_mg_msg = syscall(SYS_gettid);
	prctl(PR_SET_NAME,"MGcamd RecvMSG",0,0,0);

	while (!prg.restart) {
		uint64_t config_generation;
		newcamd_config_read_lock();
		config_generation = cfg.cache.generation;
		thread_delay = cfg.delay.thread;
		pfdcount = 0;
		// PIPE
		pfd[pfdcount].fd = prg.pipe.mgcamd[0];
		pfd[pfdcount++].events = POLLIN | POLLPRI;

		struct mgcamdserver_data *mgcamd = cfg.mgcamd.server;
		while (mgcamd) {
			if ( !IS_DISABLED(mgcamd->flags)&&(mgcamd->handle>0) ) {
				if (mgcamd->clipfd.update||mgcamd->clipfd.count<0) {
					mgcamd->clipfd.count = 0;
					struct mg_client_data *cli = mgcamd->client;
					while (cli && (mgcamd->clipfd.count<MGCAMD_MAX_PFD)) {
						if ( !IS_DISABLED(cli->flags) && (cli->handle>0) && !(cli->flags&FLAG_WORKTHREAD) ) {
							cli->ipoll = mgcamd->clipfd.count;
							mgcamd->clipfd.pfd[mgcamd->clipfd.count].fd = cli->handle;
							mgcamd->clipfd.pfd[mgcamd->clipfd.count++].events = POLLIN | POLLPRI;
						} else cli->ipoll = -1;
						cli = cli->next;
					}
					mgcamd->clipfd.update = 0;
					//debugf(getdbgflag(DBG_ERROR,0,0), " mgcamd clients poll updated %d\n", mgcamd->clipfd.count);
				}
				mgcamd->clipfd.ipoll = pfdcount;
				if (mgcamd->clipfd.count>0) {
					memcpy( &pfd[pfdcount], mgcamd->clipfd.pfd, mgcamd->clipfd.count * sizeof(struct pollfd) );
					pfdcount += mgcamd->clipfd.count;
				}
			}
			mgcamd = mgcamd->next;
		}

		newcamd_config_read_unlock();
		int retval = poll(pfd, pfdcount, 3010); // for 3seconds

		if ( retval>0 ) {
			if (thread_delay) usleep(thread_delay);
			newcamd_config_read_lock();
			if (config_generation != cfg.cache.generation) {
				newcamd_config_read_unlock();
				continue;
			}

			struct mgcamdserver_data *mgcamd = cfg.mgcamd.server;
			while (mgcamd) {
				if ( !IS_DISABLED(mgcamd->flags)&&(mgcamd->handle>0)&&(mgcamd->clipfd.count>0) ) {
					pthread_mutex_lock(&prg.lockclimg);
					struct mg_client_data *mgcli = mgcamd->client;
					while (mgcli) {
						if ( !IS_DISABLED(mgcli->flags)&&(mgcli->handle>0)&&(mgcli->ipoll>=0)&&(mgcli->handle==pfd[mgcamd->clipfd.ipoll+mgcli->ipoll].fd) ) {
							if ( pfd[mgcamd->clipfd.ipoll+mgcli->ipoll].revents & (POLLHUP|POLLNVAL) ) mg_disconnect_cli(mgcli);
							else if ( pfd[mgcamd->clipfd.ipoll+mgcli->ipoll].revents & (POLLIN|POLLPRI) ) {
								mg_cli_recvmsg(mgcli);
							}
							///else if ( (GetTickCount()-mgcli->lastactivity) > 600000 ) mg_disconnect_cli(mgcli);
						}
						mgcli = mgcli->next;
					}
					pthread_mutex_unlock(&prg.lockclimg);
				}
				mgcamd = mgcamd->next;
			}
			//
			if ( pfd[0].revents & (POLLIN|POLLPRI) ) mg_recv_pipe();
			newcamd_config_read_unlock();
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
// MGCAMD SERVER: START/STOP
///////////////////////////////////////////////////////////////////////////////

int start_thread_mgcamd()
{
	pthread_t tid;
#ifndef MONOTHREAD_ACCEPT
	create_thread(&tid, mgcamd_accept_thread,NULL);
#endif

	create_thread(PACKED_MPTR(&cfg, pthread_t, mgcamd.tid_recvmsg), mg_recvmsg_thread,NULL);
	return 0;
}


