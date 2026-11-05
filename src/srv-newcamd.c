/////
// File: srv-newcamd.c
// Modification date: 2011.11.11
/////

#include "newcamd-request.h"

///////////////////////////////////////////////////////////////////////////////
// PROTO
///////////////////////////////////////////////////////////////////////////////

void cs_disconnect_cli(struct cs_client_data *cli);
static void cs_disconnect_cli_unlocked(struct cs_client_data *cli);
static void cs_senddcw_cli_locked(struct cs_client_data *cli,
                                  const struct ledger_delivery_snapshot *delivery);

struct newcamd_connect_cli_data {
	uint32_t profile_id;
	int sock;
	uint32_t ip;
};

static void cs_lastecm_save_request(struct cs_client_data *cli,
                                    const struct ledger_delivery_snapshot *delivery,
                                    uint32_t sent_at)
{
	if (!cli || !delivery) return;
	cli->lastecm.caid = delivery->caid;
	cli->lastecm.prov = delivery->provid;
	cli->lastecm.provider_known = delivery->provider_known;
	cli->lastecm.sid = delivery->sid;
	cli->lastecm.hash = delivery->hash;
	cli->lastecm.tag = delivery->tag;
	cli->lastecm.request = delivery->request;
	cli->lastecm.decodetime = sent_at - delivery->recvtime;
#ifdef CACHEEX
	memcpy(cli->lastecm.ecmd5, delivery->ecmd5, sizeof(cli->lastecm.ecmd5));
	cli->lastecm.ecmlen = delivery->ecmlen;
#endif
}

void cs_senddcw_cli(struct cs_client_data *cli, const struct ledger_delivery_snapshot *delivery);


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

static void cs_disconnect_cli_unlocked(struct cs_client_data *cli)
{
	uint32_t ticks;
	if (!cli) return;
	cli->connection.status = 0;
	ticks = GetTickCount();
	cli->connection.uptime += ticks - cli->connection.time;
	cli->connection.lastseen = ticks; // Last Seen
	if (cli->handle > 0) {
#ifdef EPOLL_NEWCAMD
		if (prg.epoll.newcamd > 0)
			epoll_ctl(prg.epoll.newcamd, EPOLL_CTL_DEL, cli->handle, NULL);
#endif
		close(cli->handle);
	}
	cli->handle = -1;
	debugf(getdbgflag(DBG_NEWCAMD,cli->pid,cli->id)," newcamd: client '%s' disconnected\n", cli->user);
}

void cs_disconnect_cli(struct cs_client_data *cli)
{
	if (!cli || newcamd_client_state_lock(cli) != 0) return;
	cs_disconnect_cli_unlocked(cli);
	newcamd_client_state_unlock(cli);
}


static void *cs_connect_cli_inner(struct newcamd_connect_cli_data *param)
{
    char passwdcrypt[120];
	unsigned char keymod[14];
	int i,index;
	unsigned char sessionkey[16];
	struct cs_custom_data clicd;
	unsigned char buf[CWS_NETMSGSIZE];
	uint32_t connected_client_id = 0;

	uint32_t profile_id = param->profile_id;
	int sock = param->sock;
	uint32_t ip = param->ip;
	struct cardserver_data *cs;
	free(param);
	cs = getcsbyid(profile_id);
	if (!cs) {
		close(sock);
		return NULL;
	}
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
			if ( tick32_elapsed(GetTickCount(), cli->connection.time) > 60000u ) {
				cs_disconnect_cli_unlocked(cli);
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
			connected_client_id = cli->id;
			pthread_mutex_unlock(&prg.lockcli);
			cli->cs->newcamd.clipfd.update = 1;
#ifdef EPOLL_NEWCAMD
		/* Queue a stable client ID rather than a pointer that a later reload
		 * could free before the receiver drains this pipe. */
		pipe_send_u32(prg.pipe.newcamd[1], PIPE_CLI_CONNECTED, connected_client_id);
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


void *cs_connect_cli(void *opaque)
{
	void *result;
	newcamd_config_read_lock();
	result = cs_connect_cli_inner((struct newcamd_connect_cli_data *)opaque);
	newcamd_config_read_unlock();
	return result;
}

static void newcamd_srv_accept_inner(struct cardserver_data *srv)
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
			if (SetSoketNonBlocking(newfd) < 0) {
				debugf(DBG_ERROR, " [%s] unable to set accepted socket nonblocking (errno=%d)\n", srv->name, errno);
				close(newfd);
				return;
			}
			//debugf(getdbgflag(DBG_NEWCAMD,srv->id,0)," [%s] new connection (%s)\n", srv->name, ip2string(newip) );
			struct newcamd_connect_cli_data *newdata = malloc( sizeof(struct newcamd_connect_cli_data) );
			if (!newdata) { close(newfd); return; }
			newdata->profile_id = srv->id;
			newdata->sock = newfd;
			newdata->ip = newaddr.sin_addr.s_addr;
			if ( !create_thread(&srv_tid, (threadfn)cs_connect_cli,newdata) ) {
				free( newdata );
				close( newfd );
			}
		}
	}
}

void newcamd_srv_accept(struct cardserver_data *srv)
{
	newcamd_config_read_lock();
	newcamd_srv_accept_inner(srv);
	newcamd_config_read_unlock();
}

#ifndef MONOTHREAD_ACCEPT

void *newcamd_accept_thread(void *param)
{
	sleep(5);

	while(!prg.restart) {
			uint64_t config_generation;
			struct pollfd pfd[256];
			int pfdcount = 0;
			newcamd_config_read_lock();
			config_generation = cfg.cache.generation;

		struct cardserver_data *cs = cfg.cardserver;
		while(cs) {
			if ( cs->option.fsharenewcamd && !IS_DISABLED(cs->newcamd.flags) && (cs->newcamd.handle>0) ) {
				cs->newcamd.ipoll = pfdcount;
				pfd[pfdcount].fd = cs->newcamd.handle;
				pfd[pfdcount++].events = POLLIN | POLLPRI;
			} else cs->newcamd.ipoll = -1;
			cs = cs->next;
		}

			newcamd_config_read_unlock();
			if (pfdcount) {
				int retval = poll(pfd, pfdcount, 3006);
				if ( retval>0 ) {
					newcamd_config_read_lock();
					if (config_generation != cfg.cache.generation) {
						newcamd_config_read_unlock();
						continue;
					}
					struct cardserver_data *cs = cfg.cardserver;
					while (cs) {
						if ( cs->option.fsharenewcamd )
						if ( !IS_DISABLED(cs->newcamd.flags)&&(cs->newcamd.handle>0) && (cs->newcamd.ipoll>=0) && (cs->newcamd.handle==pfd[cs->newcamd.ipoll].fd) ) {
							if ( pfd[cs->newcamd.ipoll].revents & (POLLIN|POLLPRI) ) newcamd_srv_accept( cs );
						}
						cs = cs->next;
					}
					newcamd_config_read_unlock();
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
	__atomic_store_n(&cli->ecm.lifetime_id, ecm_request_lifetime_id_load(ecm), __ATOMIC_RELEASE);
    cli->ecm.climsgid = climsgid;
    cli->ecm.status = STAT_ECM_SENT;
	ecm_addip(ecm, cli->ip);

	/* IBS: Record ECM request from Newcamd client */
	{
		uint32_t bad_source = 0;
		uint8_t bad_cw_hash[8] = {0};
		int bs_detected = ibs_on_ecm_request(
			cli->ip, ecm->caid, ecm->provid, ecm->sid,
			cli->ecm.recvtime, &bad_source, bad_cw_hash);
		if (bs_detected) {
			debugf(0, " !!! [IBS-DETECT] BLACK SCREEN (Newcamd) %04X:%06X:%04X client %08X -- source %d was WRONG\n",
				ecm->caid, ecm->provid, ecm->sid, cli->ip, bad_source);
			cwpostval_trust_bad(bad_source);
		}
	}
}

static int cs_capture_client_delivery_locked(
	struct ledger_delivery_snapshot *out, ECM_DATA *ecm,
	const struct cs_client_data *cli)
{
	uint64_t lifetime_id;
	if (!out || !ecm || !cli || !cli->ecm.busy || cli->ecm.request != ecm)
		return 0;
	lifetime_id = __atomic_load_n(&cli->ecm.lifetime_id, __ATOMIC_ACQUIRE);
	if (!ecm_request_lifetime_matches(ecm, cli->ecm.request, lifetime_id) ||
	    !ledger_delivery_snapshot_capture_locked(out, ecm))
		return 0;
	return out->lifetime_id == lifetime_id;
}

#define CS_CAPTURE_CLIENT_DELIVERY(name, request, client) \
	struct ledger_delivery_snapshot name##_storage; \
	const struct ledger_delivery_snapshot *name = \
		cs_capture_client_delivery_locked(&name##_storage, (request), (client)) ? \
		&name##_storage : NULL

int cs_cli_getmsg(struct cs_client_data *cli,struct cs_custom_data *clicd, uint8_t *buf)
{
	int len;
	if (cli->handle<=0) return 0; // disconnected

#ifdef RECVMSG_PEEK
		len = cs_msg_chkrecv(cli->handle);
		if (len==0) {
			debugf(getdbgflag(DBG_NEWCAMD,cli->pid,cli->id)," newcamd: client '%s' read failed %d\n", cli->user,len);
			cs_disconnect_cli_unlocked(cli);
		}
		else if (len==-1) {
			if (!cli->chkrecvtime) cli->chkrecvtime = GetTickCount();
			else if (tick32_elapsed(GetTickCount(), cli->chkrecvtime) > 300u) {
				debugf(getdbgflag(DBG_NEWCAMD,cli->pid,cli->id)," newcamd: client '%s' read failed %d\n", cli->user,len);
				cs_disconnect_cli_unlocked(cli);
			}
		}
		else if (len>0) {
			cli->chkrecvtime = 0;
			len = cs_message_receive(cli->handle, clicd, buf, cli->sessionkey,3);
			if (len==0) {
				debugf(getdbgflag(DBG_NEWCAMD,cli->pid,cli->id)," newcamd: client '%s' read failed %d\n", cli->user,len);
				cs_disconnect_cli_unlocked(cli);
			}
			else if (len<0) {
				debugf(getdbgflag(DBG_NEWCAMD,cli->pid,cli->id)," newcamd: client '%s' read failed %d(%d)\n", cli->user,len,errno);
				cs_disconnect_cli_unlocked(cli);
			}
			else return len;
		}
#else
	len = cs_peekmsg( cli->handle, &cli->msg, cli->sessionkey, clicd, buf );
	if (len==0) {
		debugf(getdbgflag(DBG_NEWCAMD,cli->pid,cli->id)," newcamd: client '%s' read failed %d\n", cli->user,len);
		cs_disconnect_cli_unlocked(cli);
	}
	else if (len==-1) {
		uint32_t ticks = GetTickCount();
		if (!cli->chkrecvtime) cli->chkrecvtime = ticks;
		else if ((uint32_t)(ticks - cli->chkrecvtime) > 300u) {
			debugf(getdbgflag(DBG_NEWCAMD,cli->pid,cli->id)," newcamd: client '%s' read timeout %d\n", cli->user,len);
			cs_disconnect_cli_unlocked(cli);
		}
	} else return len;
#endif

	return 0;
}

void *cs_cli_recvmsg(struct cs_client_data *cli)
{
	struct cardserver_data *cs;
	int client_state_locked = 1;
	if (!cli || newcamd_client_state_lock(cli) != 0) return NULL;
	cs = cli->cs;
	struct cs_custom_data clicd; // Custom data
	int len;
	unsigned char buf[CWS_NETMSGSIZE];
	unsigned char data[CWS_NETMSGSIZE]; // for other use

	if (cli->handle<=0) {
		newcamd_client_state_unlock(cli);
		return NULL;
	}
	len = cs_cli_getmsg(cli,&clicd,buf);
	if (len<=0) {
		newcamd_client_state_unlock(cli);
		return NULL;
	}

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
					uint32_t provid = ecm_getprovid( data, len, clicd.caid );
					if (provid!=0) clicd.provid = provid;

					if (cli->ecm.busy) {
						cli->ecmdenied++;
						// send decode failed
						buf[1] = 0; buf[2] = 0;
						if ( !cs_message_send( cli->handle, &clicd, buf, 3, cli->sessionkey) ) cs_disconnect_cli_unlocked( cli );
						else debugf(getdbgflag(DBG_NEWCAMD,cli->pid,cli->id)," <|> decode failed to client '%s' ch %04x:%06x:%04x, too many ecm requests\n", cli->user,clicd.caid,clicd.provid,clicd.sid);
						break;
					}
					// CHECK FOR ECM
					uint8_t cw1cycle;
					char *error = cs_accept_ecm(cs,clicd.caid,clicd.provid,clicd.sid,ecm_getchid(data,len,clicd.caid), len, data, &cw1cycle);
					if (error) {
						cs->ecmdenied++;
						cli->ecmdenied++;
						buf[1] = 0; buf[2] = 0;
						if ( !cs_message_send( cli->handle, &clicd, buf, 3, cli->sessionkey) ) cs_disconnect_cli_unlocked( cli );
						else debugf(getdbgflag(DBG_NEWCAMD,cli->pid,cli->id)," <|> decode failed to client '%s' ch %04x:%06x:%04x, %s\n",cli->user, clicd.caid,clicd.provid,clicd.sid, error);
						break;
					}

					clicd.caid = cs->card.caid; // if caid == 0

					// ACCEPTED
					ecm_lock(); //###

					// Search for ECM
					ECM_DATA *ecm = search_ecmdata_any(cs, data,  len, clicd.sid, clicd.caid, clicd.provid, 1); // dont get failed ecm request from cache
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
					 *    "same channel" (see the freeze check below). R43 (D106)
					 *    tightened this to what GR6 always demanded: the gate
					 *    now requires the SAME ECM (hash and tag), because a
					 *    new ECM on the channel is normal rotation, not a
					 *    verdict on the key that was delivered.
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
#ifdef CACHEEX
					/* Require the saved full request identity; neither the reusable
					 * ECM pointer nor the 32-bit hash alone identifies a retry. */
					if (ecm) samechan = samechan &&
						(cli->lastecm.provider_known == ecm->provider_known) &&
						(cli->lastecm.ecmlen == ecm->ecmlen) &&
						(memcmp(cli->lastecm.ecmd5, ecm->ecmd5, 16) == 0);
#endif
					/* TASK R43 (D106) -- a retry is the SAME ECM asked again:
					 * same hash and same tag. A NEW ECM inside the window is
					 * the provider's normal rotation, not evidence that the
					 * key failed; counting it filed good keys as bad. */
					if ( samechan && ecm && (ecm->hash==cli->lastecm.hash)
					     && (ecm->ecm[0]==cli->lastecm.tag) ) {
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
						/* Only a hard retry of the saved, successfully delivered ECM is
						 * a candidate ledger failure. The ledger re-matches all saved
						 * identity/key/source fields before it can complete proof. */
						int led_proof = 0;
#ifdef CACHEEX
						if (rcls==RETRY_HARD)
							led_proof = ledger_note_failure(cli, ecm, ticks);
#endif
						if (led_proof) {
							/* ledger_note_failure already recorded the definitive
							 * proof and its aggregate event; do not count it twice. */
						}
						else if (rcls==RETRY_HARD || rcls==RETRY_SOFT) {
							trust_record_lk_scoped( &trust_tab, TRUST_EV_SOFT,
								cli->lastecm.dcwsrctype, cli->lastecm.dcwsrcid,
								cli->lastecm.caid, cli->lastecm.prov,
								cli->lastecm.provider_known, cli->lastecm.sid, ticks );
							trustagg_note_lk( TRUST_EV_SOFT,
								cli->lastecm.dcwsrctype, cli->lastecm.dcwsrcid,
								cli->lastecm.caid, ticks );
						}
						else if (cli->lastecm.status && cli->lastecm.dcwsrctype!=DCW_SOURCE_NONE) {
							trust_record_lk_scoped( &trust_tab, TRUST_EV_GOOD,
								cli->lastecm.dcwsrctype, cli->lastecm.dcwsrcid,
								cli->lastecm.caid, cli->lastecm.prov,
								cli->lastecm.provider_known, cli->lastecm.sid, ticks );
							/* TASK 2.8: GOOD is how a suppressed peer wins its
							 * requests back -- this tap is the recovery path. */
							trustagg_note_lk( TRUST_EV_GOOD,
								cli->lastecm.dcwsrctype, cli->lastecm.dcwsrcid,
								cli->lastecm.caid, ticks );
						}

						/*
						 * TASK R30 (D81) -- receiver attestation.
						 *
						 * The same hard retry, read as a receiver's vote on the
						 * BYTES: this client was handed `lastecm.dcw` and came
						 * back too soon. One client is inference (GR3); the
						 * armed number of DISTINCT clients re-asking after the
						 * same bytes is independent receivers attesting one
						 * fact, and files the bytes for a TTL (cwattest.h).
						 * Skipped when the ledger already PROVED this key --
						 * proof memory (cwneg) is permanent and outranks a
						 * filing. No-op unless CACHE ATTEST is armed; only
						 * cache-delivered keys can be filed (attest_note
						 * enforces it), since the gate sits at cache ingest.
						 */
						if (rcls==RETRY_HARD && !led_proof)
							attest_note( ATTEST_TAG_NEWCAMD | (uint32_t)cli->id, cli->lastecm.dcw,
								cli->lastecm.caid, cli->lastecm.prov,
								cli->lastecm.provider_known, cli->lastecm.sid,
								cli->lastecm.dcwsrctype, cli->lastecm.dcwsrcid, ticks );

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
							 * The ECM thread holds lockecm here. Follow the established
							 * lockecm -> lockcache order used by the success path and all
							 * cache-purge mark/sweep operations; this fixed-size table is
							 * shared with cache workers and must not be touched unlocked.
							 */
							int purge_released;
							pthread_mutex_lock(&prg.lockcache);
							purge_released = cache_purge_unmark_scoped(&purge_tab,
									cli->lastecm.dcwsrctype, cli->lastecm.dcwsrcid,
									cli->lastecm.caid, cli->lastecm.sid,
									cli->lastecm.prov, cli->lastecm.provider_known);
							pthread_mutex_unlock(&prg.lockcache);
							if (purge_released) {
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
								ecm_request_set_dcwstatus(ecm, STAT_DCW_WAIT);
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
										ecm_request_set_dcwstatus(ecm, STAT_DCW_WAIT);
										memset( ecm->cw, 0, 16 );
										ecm->checktime = 1; // Wakeup Now
										pipe_wakeup( prg.pipe.ecm[1] );
									}
									else {
										CS_CAPTURE_CLIENT_DELIVERY(delivery, ecm, cli);
										cs_senddcw_cli_locked(cli, delivery);
									}
								}
								else
#endif
								{
									CS_CAPTURE_CLIENT_DELIVERY(delivery, ecm, cli);
									cs_senddcw_cli_locked(cli, delivery);
								}
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
						// Setup ECM Request for Server(s)
						ecm = store_ecmdata(cs, data, len, clicd.sid, clicd.caid, clicd.provid);
						if (!ecm) {
							cs->ecmdenied++;
							cli->ecmdenied++;
							uint8_t failure[3] = { data[0], 0, 0 };
							if (!cs_message_send(cli->handle, &clicd, failure, sizeof(failure), cli->sessionkey)) cs_disconnect_cli_unlocked(cli);
							ecm_unlock();
							break;
						}
						cs->ecmaccepted++;
						cs_store_ecmclient(cs, ecm, cli, clicd.msgid);
						debugf(getdbgflag(DBG_NEWCAMD,cli->pid,cli->id)," <- ecm from client '%s' ch %04x:%06x:%04x:%08x\n",cli->user,clicd.caid,clicd.provid,clicd.sid,ecm->hash);
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
							fdebugf(" <- ecm from Newcamd client '%s' ch %04x:%06x:%04x %02x:%08x\n", cli->user, ecm->caid, ecm->provid, ecm->sid, ecm->ecm[0], ecm->hash);
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
							debugf(getdbgflag(DBG_NEWCAMD,cli->pid,cli->id)," <!= unexpected cw from Client '%s' with no outstanding ECM request\n", cli->user);
							ecm_unlock();
							break;
						}
					if (ecm->hash!=cli->ecm.hash) {
						debugf(getdbgflag(DBG_NEWCAMD,cli->pid,cli->id)," <!= wrong cw from Client '%s' ch %04x:%06x:%04x\n", cli->user, ecm->caid,ecm->provid,ecm->sid, ticks-cli->lastecmtime);
						ecm_unlock();
						break;
					}
					if ( (ecm->caid!=clicd.caid) || (ecm->sid!=clicd.sid) || (ecm->provid!=clicd.provid) || (cli->ecm.climsgid!=clicd.msgid) ) {
						debugf(getdbgflag(DBG_NEWCAMD,cli->pid,cli->id)," <!= wrong cw from Client '%s' ch %04x:%06x:%04x\n", cli->user, ecm->caid,ecm->provid,ecm->sid, ticks-cli->lastecmtime);
						ecm_unlock();
						break;
					}
					if ( (buf[0]&0x81)!=ecm->ecm[0] ) {
						debugf(getdbgflag(DBG_NEWCAMD,cli->pid,cli->id)," <!= wrong cw from Client '%s' ch %04x:%06x:%04x\n", cli->user, ecm->caid,ecm->provid,ecm->sid, ticks-cli->lastecmtime);
						ecm_unlock();
						break;
					}
					if (buf[2]!=0x10) {
						debugf(getdbgflag(DBG_NEWCAMD,cli->pid,cli->id)," <!= wrong cw from Client '%s' ch %04x:%06x:%04x\n", cli->user, ecm->caid,ecm->provid,ecm->sid, ticks-cli->lastecmtime);
						ecm_unlock();
						break;
					}
					// Check for DCW
					if (!acceptDCW_profile_ecm(&buf[3], ecm->cs, ecm->caid, ecm->provid, ecm->sid)) {
						debugf(getdbgflag(DBG_NEWCAMD,cli->pid,cli->id)," <!= wrong cw from Client '%s' ch %04x:%06x:%04x\n", cli->user, ecm->caid,ecm->provid,ecm->sid, ticks-cli->lastecmtime);
						ecm_unlock();
						break;
					}
					debugf(getdbgflag(DBG_NEWCAMD,cli->pid,cli->id)," <= cw from Client '%s' ch %04x:%06x:%04x (%dms)\n", cli->user, ecm->caid,ecm->provid,ecm->sid, ticks-cli->lastecmtime);
					if (ecm->dcwstatus!=STAT_DCW_SUCCESS) {
						static char msg[] = "Good dcw from Newcamd Client";
						int client_srcid = ((ecm->cs ? ecm->cs->id : 0)<<16)|cli->id;
						ecm->statusmsg = msg;
						/* The dispatch path locks client state. Do not hold this
						 * client's state mutex while ecm_setdcw() fans out. */
						newcamd_client_state_unlock(cli);
						client_state_locked = 0;
						ecm_setdcw(ecm, &buf[3], DCW_SOURCE_CSCLIENT, client_srcid);
					}
					else {	//TODO: check same dcw between cards
						if (memcmp(&ecm->cw, &buf[3],16))
							debugf(getdbgflag(DBG_NEWCAMD,cli->pid,cli->id)," !!! different dcw from newcamd client '%s'\n", cli->user);
					}

					ecm_unlock(); //###
					break;
#endif

		default:
					if (buf[0]==MSG_KEEPALIVE) {
#ifdef SRV_CSCACHE
						// Check for Cache client??
						if (clicd.sid==(('C'<<8)|'H')) clicd.caid = ('O'<<8)|'K';
#endif
						//debugf(getdbgflag(DBG_NEWCAMD,cli->pid,cli->id)," <-> keepalive from/to client '%s'\n",cli->user);
						if ( !cs_message_send(cli->handle, &clicd, buf, 3, cli->sessionkey) ) cs_disconnect_cli_unlocked( cli );
					}
					else {
						debugf(getdbgflag(DBG_NEWCAMD,cli->pid,cli->id)," newcamd: unknown message type '%02x' from client '%s'\n",buf[0],cli->user);
						buf[1]=0; buf[2]=0;
						if ( !cs_message_send( cli->handle, &clicd, buf, 3, cli->sessionkey) ) cs_disconnect_cli_unlocked( cli );
					}
				} // Switch

		if (client_state_locked) newcamd_client_state_unlock(cli);
		return NULL;
}

///////////////////////////////////////////////////////////////////////////////
// DCW
///////////////////////////////////////////////////////////////////////////////

static void cs_senddcw_cli_locked(struct cs_client_data *cli, const struct ledger_delivery_snapshot *delivery)
{
	unsigned char buf[CWS_NETMSGSIZE];
	struct cs_custom_data clicd; // Custom data
	ECM_DATA *request;
	uint64_t current_lifetime_id;
	uint32_t response_recvtime;
	uint16_t response_caid;
	uint32_t response_provid;
	uint16_t response_sid;
	uint8_t response_tag;
	uint32_t ticks;
	int enablefreeze = 0;

	if (!cli || !delivery || !delivery->lifetime_id) return;
	request = cli->ecm.request;
	current_lifetime_id = __atomic_load_n(&cli->ecm.lifetime_id, __ATOMIC_ACQUIRE);
	/* The per-client state lock is held by both dispatch paths. A pointer/hash
	 * may be recycled, so compare the client's current generation directly to
	 * the delivery generation as well as checking the request object's token. */
	if (current_lifetime_id != delivery->lifetime_id ||
	    !newcamd_delivery_identity_matches(request, current_lifetime_id,
	                                       delivery->request, delivery->lifetime_id) ||
	    ecm_request_lifetime_id_load(request) != delivery->lifetime_id)
		return;
	if (cli->ecm.status == STAT_DCW_SENT) {
		debugf(getdbgflag(DBG_NEWCAMD,cli->pid,cli->id)," +> cw send failed to client '%s', cw already sent\n", cli->user);
		return;
	}
	if (cli->handle == INVALID_SOCKET) {
		debugf(getdbgflag(DBG_NEWCAMD,cli->pid,cli->id)," +> cw send failed to client '%s', client disconnected\n", cli->user);
		return;
	}
	if (!cli->ecm.busy) {
		debugf(getdbgflag(DBG_NEWCAMD,cli->pid,cli->id)," +> cw send failed to client '%s', no ecm request\n", cli->user);
		return;
	}

	response_caid = delivery->caid;
	response_provid = delivery->provid;
	response_sid = delivery->sid;
	response_tag = delivery->tag;
	response_recvtime = delivery->recvtime;
	ticks = GetTickCount();
	if (cli->lastecm.caid == response_caid && cli->lastecm.prov == response_provid &&
	    cli->lastecm.sid == response_sid) {
		if (cli->lastecm.status == 1 && (uint32_t)(ticks-cli->lastdcwtime) > 200u)
			enablefreeze = 1;
	} else cli->zap++;

	clicd.msgid = cli->ecm.climsgid;
	clicd.sid = response_sid;
	clicd.caid = response_caid;
	clicd.provid = response_provid;

	if (delivery->status == STAT_DCW_SUCCESS) {
		uint32_t sent_at;
		buf[0] = response_tag;
		buf[1] = 0;
		buf[2] = 0x10;
		memcpy(&buf[3], delivery->cw, 16);
		if (!cs_message_send(cli->handle, &clicd, buf, 19, cli->sessionkey)) {
			cs_disconnect_cli_unlocked(cli);
			return;
		}
		sent_at = GetTickCount();
		cs_lastecm_save_request(cli, delivery, sent_at);
#ifdef CACHEEX
		LEDGER_DELIVERY_TO_LASTECM(cli->lastecm, *delivery);
#else
		cli->lastecm.dcwsrctype = delivery->srctype;
		cli->lastecm.dcwsrcid = delivery->srcid;
		memcpy(cli->lastecm.dcw, delivery->cw, sizeof(cli->lastecm.dcw));
#endif
		cli->lastecm.status = 1;
		cli->lastdcwtime = sent_at;
		cli->ecmok++;
		cli->ecmoktime += sent_at-response_recvtime;
#ifdef CACHEEX
		ledger_note_delivered(delivery, sent_at);
#endif
		debugf(getdbgflag(DBG_NEWCAMD,cli->pid,cli->id)," => cw to client '%s' ch %04x:%06x:%04x (%dms)\n", cli->user, response_caid,response_provid,response_sid, sent_at-response_recvtime);
	} else {
		uint32_t sent_at;
		if (enablefreeze) cli->freeze++;
		buf[0] = response_tag;
		buf[1] = 0;
		buf[2] = 0;
		if (!cs_message_send(cli->handle, &clicd, buf, 3, cli->sessionkey)) {
			cs_disconnect_cli_unlocked(cli);
			return;
		}
		sent_at = GetTickCount();
		cs_lastecm_save_request(cli, delivery, sent_at);
		cli->lastecm.status = 0;
		cli->lastecm.dcwsrctype = DCW_SOURCE_NONE;
		cli->lastecm.dcwsrcid = 0;
		memset(cli->lastecm.dcw, 0, 16);
#ifdef CACHEEX
		memset(cli->lastecm.ecmd5, 0, sizeof(cli->lastecm.ecmd5));
		cli->lastecm.ecmlen = 0;
#endif
		debugf(getdbgflag(DBG_NEWCAMD,cli->pid,cli->id)," |> decode failed to client '%s' ch %04x:%06x:%04x (%dms)\n", cli->user, response_caid,response_provid,response_sid, sent_at-response_recvtime);
	}
	/* Publish SENT before clearing busy, under the same per-client lock. */
	cli->ecm.status = STAT_DCW_SENT;
	cli->ecm.busy = 0;
}

// Check sending cw to clients
void cs_senddcw_cli(struct cs_client_data *cli, const struct ledger_delivery_snapshot *delivery)
{
	if (!cli || newcamd_client_state_lock(cli) != 0) return;
	cs_senddcw_cli_locked(cli, delivery);
	newcamd_client_state_unlock(cli);
}

void cs_check_sendcw(ECM_DATA *ecm, const struct ledger_delivery_snapshot *delivery)
{
	if (!delivery || !delivery->lifetime_id || delivery->request != ecm) return;
	newcamd_config_read_lock();
#ifdef SRV_CSCACHE
	if (delivery->status == STAT_DCW_SUCCESS && delivery->srctype == DCW_SOURCE_CSCLIENT) {
		struct cs_client_data *source = getnewcamdclientbyid((uint32_t)delivery->srcid & 0xffffu);
		if (source && newcamd_client_state_lock(source) == 0) {
			source->cachedcw++;
			newcamd_client_state_unlock(source);
		}
	}
#endif
	struct cardserver_data *cs = cfg.cardserver;
	while (cs) {
		if (!IS_DISABLED(cs->flags) && cs->newcamd.handle > 0) {
			struct cs_client_data *cli = cs->newcamd.client;
			while (cli) {
				struct cs_client_data *next = cli->next;
				if (newcamd_client_state_lock(cli) == 0) {
					ECM_DATA *request = cli->ecm.request;
						if (!IS_DISABLED(cli->flags) && cli->handle != INVALID_SOCKET &&
						    cli->ecm.busy && cli->ecm.status == STAT_ECM_SENT &&
						    newcamd_delivery_identity_matches(
						        request, __atomic_load_n(&cli->ecm.lifetime_id, __ATOMIC_ACQUIRE),
						        delivery->request, delivery->lifetime_id) &&
						    ecm_request_lifetime_id_load(request) == delivery->lifetime_id)
						cs_senddcw_cli_locked(cli, delivery);
					newcamd_client_state_unlock(cli);
				}
				cli = next;
			}
		}
		cs = cs->next;
	}
	newcamd_config_read_unlock();
}


///////////////////////////////////////////////////////////////////////////////

void cs_recv_pipe()
{
	uint8_t buf[1024];
	struct pollfd pfd[2];
	struct cs_client_data *cli;

	do {
		int len = pipe_recv( prg.pipe.newcamd[0], buf, sizeof(buf));
		if (len>0) {
			switch(buf[0]) {
				case PIPE_WAKEUP:  // ADD NEW CLIENT
					if (len != 2) break;
					//debugf(" wakeup csmsg\n");
					break;
#ifdef EPOLL_NEWCAMD
				case PIPE_CLI_CONNECTED:  // ADD NEW FD
					{
						uint32_t client_id;
						struct epoll_event ev;
							int handle;
							if (!pipe_decode_u32_message(buf, (size_t)len, PIPE_CLI_CONNECTED, &client_id) || !client_id) break;
							cli = getnewcamdclientbyid(client_id);
							if (!cli || newcamd_client_state_lock(cli) != 0) break;
						handle = cli->handle;
						if (handle > 0) {
							memset(&ev, 0, sizeof(ev));
							 ev.events = EPOLLIN | EPOLLPRI | EPOLLRDHUP;
								 ev.data.u64 = client_id;
								if (epoll_ctl(prg.epoll.newcamd, EPOLL_CTL_ADD, handle, &ev) == -1)
								debugf(DBG_ERROR,"Err! EPOLL_CTL_ADD %s (%d)\n", cli->user, handle);
						}
						newcamd_client_state_unlock(cli);
					}
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
	memset(&ev, 0, sizeof(ev));
	ev.events = EPOLLIN | EPOLLPRI | EPOLLRDHUP;
	ev.data.u64 = 0;
	if ( epoll_ctl(prg.epoll.newcamd, EPOLL_CTL_ADD, prg.pipe.newcamd[0], &ev) == -1 ) printf("epoll_ctl");
	// Main Loop
	struct epoll_event evlist[MAX_EPOLL_EVENTS]; // epoll recv events
	while (!prg.restart) {
		newcamd_config_read_lock();
		uint64_t config_generation = cfg.cache.generation;
		newcamd_config_read_unlock();
		int ready = epoll_wait( prg.epoll.newcamd, evlist, MAX_EPOLL_EVENTS, 1004);
		if (ready < 0) {
			if ( (errno==EINTR)||(errno==EAGAIN) ) usleep(1000);
			else {
				usleep(99000);
				debugf(DBG_ERROR,"Err! epoll_wait (%d)", errno);
			}
			continue;
		}
		if (ready == 0) continue; // timeout

		newcamd_config_read_lock();
		if (config_generation != cfg.cache.generation) {
			newcamd_config_read_unlock();
			continue;
		}

			int i;
			for (i=0; i < ready; i++) {
				uint32_t client_id = (uint32_t)evlist[i].data.u64;
				if ( evlist[i].events & (EPOLLIN|EPOLLPRI) ) {
					if (!client_id) cs_recv_pipe();
					else {
						struct cs_client_data *cli = getnewcamdclientbyid(client_id);
						if (cli) cs_cli_recvmsg(cli);
					}
				}
				else if ( evlist[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR) ) {
					if (!client_id) debugf(DBG_ERROR,"Err! epoll_wait() pipe\n");
					else {
						struct cs_client_data *cli = getnewcamdclientbyid(client_id);
						if (cli) cs_disconnect_cli(cli);
					}
				}
			}
			newcamd_config_read_unlock();
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
		newcamd_config_read_lock();
		uint64_t config_generation = cfg.cache.generation;
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

		uint32_t thread_delay = cfg.delay.thread;
		newcamd_config_read_unlock();
		int retval = poll(pfd, pfdcount, 3012); // for 3seconds

		if ( retval>0 ) {
			if (thread_delay) usleep(thread_delay);
			newcamd_config_read_lock();
			if (config_generation != cfg.cache.generation) {
				newcamd_config_read_unlock();
				continue;
			}

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


