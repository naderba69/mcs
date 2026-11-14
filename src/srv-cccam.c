///////////////////////////////////////////////////////////////////////////////
// File: srv-cccam.c
///////////////////////////////////////////////////////////////////////////////

#include "biss.h"
#include "cccam-session.h"
#include "newcamd-lifetime.h"

///////////////////////////////////////////////////////////////////////////////
// PROTO
///////////////////////////////////////////////////////////////////////////////

void cc_senddcw_cli(struct cc_client_data *cli, const struct ledger_delivery_snapshot *delivery);
static void cc_senddcw_cli_locked(struct cc_client_data *cli,
                                  const struct ledger_delivery_snapshot *delivery,
                                  int *client_state_locked);
static int cc_msg_send_client_locked(struct cc_client_data *cli,
                                     cc_msg_cmd cmd, int len, uint8_t *buf,
                                     uint8_t *followup, int followup_len,
                                     int *client_state_locked);
static void cc_disconnect_cli_unlocked(struct cc_client_data *cli);

static uint32_t cc_session_generation_counter;

static uint32_t cc_session_generation_next(void)
{
	return cccam_session_generation_next(&cc_session_generation_counter);
}

static int cc_client_session_snapshot(struct cc_client_data *cli, int *sock,
                                      uint32_t *generation)
{
	int active;
	if (!cli || !sock || !generation || client_state_lock(cli) != 0) return 0;
	*sock = cli->handle;
	*generation = cli->session_generation;
	active = cli->connection.status > 0 && cli->handle > 0 &&
	         cli->session_generation != 0;
	client_state_unlock(cli);
	return active;
}

static int cc_client_session_matches_locked(const struct cc_client_data *cli,
                                            int sock, uint32_t generation)
{
	return cli && cccam_session_matches(cli->handle, cli->connection.status,
	                                    cli->session_generation, sock,
	                                    generation);
}

struct cccam_server_data *getcccamserverbyid(uint32_t id)
{
	struct cccam_server_data *cccam = cfg.cccam.server;
	while (cccam) {
		if (!(cccam->flags&FLAG_DELETE))
			if (cccam->id==id) return cccam;
		cccam = cccam->next;
	}
	return NULL;
}

struct cc_client_data *getcccamclientbyid(uint32_t id)
{
	struct cccam_server_data *cccam = cfg.cccam.server;
	while (cccam) {
		if (!(cccam->flags&FLAG_DELETE)) {
			struct cc_client_data *cli = cccam->client;
			while (cli) {
				if (!(cli->flags&FLAG_DELETE))
					if (cli->id==id) return cli;
				cli = cli->next;
			}
			cli = cccam->cacheexclient;
			while (cli) {
				if (!(cli->flags&FLAG_DELETE))
					if (cli->id==id) return cli;
				cli = cli->next;
			}
		}
		cccam = cccam->next;
	}
	return NULL;
}

struct cc_client_data *getcecccamclientbyid(uint32_t id)
{
	struct cccam_server_data *cccam = cfg.cccam.server;
	while (cccam) {
		if (!(cccam->flags&FLAG_DELETE)) {
			struct cc_client_data *cli = cccam->cacheexclient;
			while (cli) {
				if (!(cli->flags&FLAG_DELETE))
					if (cli->id==id) {
						debugf(0, "found cccam cacheex client '%s'\n", cli->user);
						return cli;
					}
				cli = cli->next;
			}
		}
		cccam = cccam->next;
	}
	return NULL;
}

struct cc_client_data *getcccamclientbyname(struct cccam_server_data *cccam, char *name)
{
	if (!(cccam->flags&FLAG_DELETE)) {
		uint32_t hash = hashCode( (unsigned char *)name, strlen(name) );
		struct cc_client_data *cli = cccam->client;
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
// CCCAM SERVER: DISCONNECT CLIENTS
///////////////////////////////////////////////////////////////////////////////

static void cc_disconnect_cli_unlocked(struct cc_client_data *cli)
{
	if (!cli) return;
	cli->connection.status = 0;
	uint32_t ticks = GetTickCount();
	cli->connection.uptime += ticks - cli->connection.time;
	cli->connection.lastseen = ticks; // Last Seen
	if (cli->handle > 0) close(cli->handle);
	cli->handle = -1;
#ifdef CACHEEX
	cli->pid = 0; /* pid member exists only in CACHEEX builds */
#endif
	if (cli->parent) cli->parent->clipfd.update = 1;
	debugf(0," CCcam: client '%s' disconnected \n", cli->user);
}

void cc_disconnect_cli(struct cc_client_data *cli)
{
	if (!cli || client_io_lock(cli) != 0) return;
	if (client_state_lock(cli) != 0) {
		client_io_unlock(cli);
		return;
	}
	cc_disconnect_cli_unlocked(cli);
	client_state_unlock(cli);
	client_io_unlock(cli);
}

void cc_disconnect_cli_session(struct cc_client_data *cli, int expected_sock,
                               uint32_t expected_generation)
{
	if (!cli || client_io_lock(cli) != 0) return;
	if (client_state_lock(cli) != 0) {
		client_io_unlock(cli);
		return;
	}
	if (cc_client_session_matches_locked(cli, expected_sock,
	                                     expected_generation))
		cc_disconnect_cli_unlocked(cli);
	client_state_unlock(cli);
	client_io_unlock(cli);
}


///////////////////////////////////////////////////////////////////////////////
// CCCAM SERVER: CONNECT CLIENTS
///////////////////////////////////////////////////////////////////////////////

unsigned int seed;
uint8_t fast_rnd()
{
  unsigned int offset = 12923;
  unsigned int multiplier = 4079;

  seed = seed * multiplier + offset;
  return (uint8_t)(seed % 0xFF);
}

///////////////////////////////////////////////////////////////////////////////

static int cc_sendinfo_cli_session(struct cc_client_data *cli, int expected_sock,
                                   uint32_t expected_generation,
                                   int sendversion)
{
	uint8_t buf[CC_MAXMSGSIZE];
	int result, sock;
	if (!cli || client_io_lock(cli) != 0) return 0;
	if (client_state_lock(cli) != 0) {
		client_io_unlock(cli);
		return 0;
	}
	sock = cli->handle;
	if (expected_generation ?
	    !cc_client_session_matches_locked(cli, expected_sock,
	                                      expected_generation) :
	    (sock <= 0 || cli->connection.status <= 0)) {
		client_state_unlock(cli);
		client_io_unlock(cli);
		return 0;
	}
	client_state_unlock(cli);
	memset(buf, 0, CC_MAXMSGSIZE);
	memcpy(buf, cfg.cccam.nodeid, 8 );
	memcpy(buf + 8, cfg.cccam.version, 32);		// cccam version (ascii)
	memcpy(buf + 40, cfg.cccam.build, 32);       // build number (ascii)
	if (sendversion) {
		buf[38] = REVISION >> 8;
		buf[37] = REVISION & 0xff;
		buf[36] = 0;
		buf[35] = 'S';
		buf[34] = 'C';
		buf[33] = 'M';
	}
	//debugdump(cfg.cccam.nodeid,8,"Sending server data version: %s, build: %s nodeid ", cfg.cccam.version, cfg.cccam.build);
	result = cc_msg_send(sock, &cli->sendblock, CC_MSG_SRV_INFO, 0x48, buf);
	client_io_unlock(cli);
	return result;
}

int cc_sendinfo_cli(struct cc_client_data *cli, int sendversion)
{
	return cc_sendinfo_cli_session(cli, 0, 0, sendversion);
}

///////////////////////////////////////////////////////////////////////////////

int cc_sendcard_del(struct cardserver_data *cs, struct cc_client_data *cli)
{
	uint8_t buf[4];
	int result, sock;
	if (!cli || client_io_lock(cli) != 0) return 0;
	sock = cli->handle;
	if (sock <= 0 || cli->connection.status <= 0) {
		client_io_unlock(cli);
		return 0;
	}
	buf[0] = cs->id >> 24;
	buf[1] = cs->id >> 16;
	buf[2] = cs->id >> 8;
	buf[3] = cs->id & 0xff;
	result = cc_msg_send(sock, &cli->sendblock, CC_MSG_CARD_DEL, 4, buf);
	client_io_unlock(cli);
	return result;
}


static int cc_sendcard_cli_session(struct cardserver_data *cs,
                                   struct cc_client_data *cli, int uphops,
                                   int expected_sock,
                                   uint32_t expected_generation)
{
	uint8_t buf[CC_MAXMSGSIZE];
	int result, sock;
	if (!cli || client_io_lock(cli) != 0) return 0;
	if (client_state_lock(cli) != 0) {
		client_io_unlock(cli);
		return 0;
	}
	sock = cli->handle;
	if (expected_generation ?
	    !cc_client_session_matches_locked(cli, expected_sock,
	                                      expected_generation) :
	    (sock <= 0 || cli->connection.status <= 0)) {
		client_state_unlock(cli);
		client_io_unlock(cli);
		return 0;
	}
	client_state_unlock(cli);
	memset(buf, 0, sizeof(buf));
	buf[0] = cs->id >> 24;
	buf[1] = cs->id >> 16;
	buf[2] = cs->id >> 8;
	buf[3] = cs->id & 0xff;
	buf[4] = cs->id >> 24;
	buf[5] = cs->id >> 16;
	buf[6] = cs->id >> 8;
	buf[7] = cs->id & 0xff;
	buf[8] = cs->card.caid >> 8;
	buf[9] = cs->card.caid & 0xff;
	buf[10] = uphops;
	buf[11] = cli->dnhops; // Dnhops
	//buf[20] = cs->card.nbprov;
	int j;
	int nbprov = 0;
	for (j=0; j<cs->card.nbprov; j++) {
		if ( card_sharelimits(cli->sharelimits, cs->card.caid, cs->card.prov[j].id) ) {
			//memcpy(buf + 21 + (j*7), card->provs[j], 7);
			buf[21+nbprov*7] = 0xff&(cs->card.prov[j].id>>16);
			buf[22+nbprov*7] = 0xff&(cs->card.prov[j].id>>8);
			buf[23+nbprov*7] = 0xff&(cs->card.prov[j].id);
/*
			buf[24+nbprov*7] = 0xff&(cs->card.prov[j].ua>>24);
			buf[25+nbprov*7] = 0xff&(cs->card.prov[j].ua>>16);
			buf[26+nbprov*7] = 0xff&(cs->card.prov[j].ua>>8);
			buf[27+nbprov*7] = 0xff&(cs->card.prov[j].ua);
*/
			nbprov++;
		}
	}
	if (!nbprov) {
		client_io_unlock(cli);
		return 0; // Denied
	}
	buf[20] = nbprov;
	buf[21 + (nbprov*7)] = 1;
	memcpy(buf + 22 + (nbprov*7), cfg.cccam.nodeid, 8);
	result = cc_msg_send(sock, &cli->sendblock, CC_MSG_CARD_ADD, 30 + (nbprov*7), buf);
	client_io_unlock(cli);
	return result;
}

int cc_sendcard_cli(struct cardserver_data *cs, struct cc_client_data *cli,
                    int uphops)
{
	return cc_sendcard_cli_session(cs, cli, uphops, 0, 0);
}

static void cc_sendcards_cli_session(struct cc_client_data *cli, int expected_sock,
                                     uint32_t expected_generation)
{
	int nbcard = 0;
	int i;
	struct cardserver_data *cs = cfg.cardserver;

	if (cli->csport[0]) {
		for (i = 0; i < MAX_CSPORTS; i++) {
			if (!cli->csport[i]) break;
			cs = getcsbyport(cli->csport[i]);
			if (cs && cs->option.fsharecccam && !(cli->flags & FLAG_EXPIRED) &&
			    cc_sendcard_cli_session(cs, cli, 0, expected_sock,
			                           expected_generation))
				nbcard++;
		}
	} else if (cfg.cccam.csport[0]) {
		for (i = 0; i < MAX_CSPORTS; i++) {
			if (!cfg.cccam.csport[i]) break;
			cs = getcsbyport(cfg.cccam.csport[i]);
			if (cs && cs->option.fsharecccam && !(cli->flags & FLAG_EXPIRED) &&
			    cc_sendcard_cli_session(cs, cli, 0, expected_sock,
			                           expected_generation))
				nbcard++;
		}
	} else {
		while (cs) {
			if (cs->option.fsharecccam && !(cli->flags & FLAG_EXPIRED) &&
			    cc_sendcard_cli_session(cs, cli, 0, expected_sock,
			                           expected_generation))
				nbcard++;
			cs = cs->next;
		}
	}

	debugf(getdbgflag(DBG_CCCAM,cli->parent->id,cli->id),
	       " CCcam: %d cards --> client(%s)\n", nbcard, cli->user);
}

///////////////////////////////////////////////////////////////////////////////
#ifdef CACHEEX
struct cc_cli_recv_session_args {
	struct cc_client_data *cli;
	int sock;
	uint32_t generation;
};
void *cacheex_cc_cli_recvmsg(void *param);
#endif

static void *cc_connect_cli_inner(struct connect_cli_data *param)
{
	uint8_t buf[CC_MAXMSGSIZE];
	uint8_t data[64];
	int i;
	int sendversion = 0;
	int session_current = 0;
	int current_sock;
	uint32_t current_generation;
	struct cc_crypt_block sendblock;	// crypto state block
	struct cc_crypt_block recvblock;	// crypto state block
	uint32_t connected_client_id = 0;
	uint32_t connected_session_generation = 0;
	char usr[64];
	char pwd[255];
	// Store data from param
	struct cccam_server_data *cccam = param->server;
	int sock = param->sock;
	uint32_t ip = param->ip;
	free(param);

#ifdef IPLIST
	struct ip_hacker_data *ipdata = iplist_find( cccam->iplist, ip );
	if (ipdata) iplist_newlogin( ipdata );
#endif

	//
	struct cc_client_data tmpcli;

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
#ifdef DEBUG_NETWORK
	if (flag_debugnet) {
		debugf(getdbgflag(DBG_CCCAM,cccam->id,0)," CCcam%d: Send Random Key 16\n", cccam->id);
		debughex(data, 16);
	}
#endif
	if ( !send_nonb(sock, data, 16, 500) ) {
		close(sock);
		return NULL;
	}
	//XOR init bytes with 'CCcam'
	cc_crypt_xor(data);
#ifdef DEBUG_NETWORK
	if (flag_debugnet) {
		debugf(0, " CCcam: XOR init bytes with 'CCcam'\n");
		debughex(data, 16);
	}
#endif
	//SHA1
	SHA_CTX ctx;
	SHA1_Init(&ctx);
	SHA1_Update(&ctx, data, 16);
	SHA1_Final(buf, &ctx);

	//init crypto states
	cc_crypt_init(&sendblock, buf, 20);
	cc_decrypt(&sendblock, data, 16);
		//debugf(getdbgflag(DBG_CCCAM,cccam->id,0)," CCcam%d: sendblock:cc_crypt_init \n", cccam->id); debughex(sendblock.keytable,256);
	cc_crypt_init(&recvblock, data, 16);
	cc_decrypt(&recvblock, buf, 20);
		//debugf(getdbgflag(DBG_CCCAM,cccam->id,0)," CCcam%d: recvblock:cc_crypt_init \n", cccam->id); debughex(recvblock.keytable,256);

#ifdef DEBUG_NETWORK
	if (flag_debugnet) {
		debugf(getdbgflag(DBG_CCCAM,cccam->id,0)," CCcam: SHA1 hash\n");
		debughex(buf,20);
	}
#endif
	memcpy(usr,buf,20);
	if ((i=recv_nonb(sock, buf, 20,5000)) == 20) {
#ifdef DEBUG_NETWORK
		if (flag_debugnet) {
			debugf(getdbgflag(DBG_CCCAM,cccam->id,0)," CCcam%d: receive SHA1 hash %d\n", cccam->id, i);
			debughex(buf,i);
		}
#endif
		cc_decrypt(&recvblock, buf, 20);
#ifdef DEBUG_NETWORK
		if (flag_debugnet) {
			debugf(getdbgflag(DBG_CCCAM,cccam->id,0)," Decrypted SHA1 hash (20):\n");
			debughex(buf,20);
		}
#endif
		if ( memcmp(buf,usr,20)!=0 ) {
			//debugf(getdbgflag(DBG_CCCAM,cccam->id,0)," cc_connect_cli(): wrong sha1 hash from client! (%s)\n",ip2string(ip));
			close(sock);
			return NULL;
		}
	} else {
		debugf(getdbgflag(DBG_CCCAM,cccam->id,0)," cc_connect_cli(): recv sha1 timeout\n");
		close(sock);
		return NULL;
	}

	// receive username
	i = recv_nonb(sock, buf, 20,5000);
#ifdef DEBUG_NETWORK
	if (flag_debugnet) {
		debugf( getdbgflag(DBG_CCCAM,cccam->id,0) , " CCcam%d: receive username %s -%d\n", cccam->id, ip2string(ip), i);
		debughex(buf,i);
	}
#endif
	if (i == 20) {
		cc_decrypt(&recvblock, buf, i);
		memcpy(usr,buf,20);
		usr[20] = 0;
		//strcpy(usr, (char*)buf);
		//debugf(getdbgflag(DBG_CCCAM,cccam->id,0)," cc_connect_cli(): username '%s'\n", usr);
	}
	else {
		//debugf(getdbgflag(DBG_CCCAM,cccam->id,0)," cc_connect_cli(): recv user timeout\n");
		close(sock);
		return NULL;
	}

	// test username
	for(i=0; i<20; i++) {
		if (usr[i]==0) break;
		if (usr[i]<=32) { // bad username
			close(sock);
			return NULL;
		}
	}

	// Check for username
	///pthread_mutex_lock(&prg.lockcccli);
	int found = 0;
	uint32_t hash = hashCode( (unsigned char *)usr, strlen(usr) );
	struct cc_client_data *cli = cccam->client;
	while (cli) {
		if (cli->userhash==hash)
		if (!strcmp(cli->user,usr)) {
			if (IS_DISABLED(cli->flags)) { // Connect only enabled clients
				///pthread_mutex_unlock(&prg.lockcccli);
				debugf(getdbgflag(DBG_CCCAM,cli->parent->id,cli->id)," CCcam%d: connection refused for client '%s' (%s), client disabled\n", cccam->id, usr, ip2string(ip));
				close(sock);
				return NULL;
			}
			found = 1;
			break;
		}
		cli = cli->next;
	}
	if (!found) {
		cli = cccam->cacheexclient;
		while (cli) {
			if (cli->userhash==hash)
			if (!strcmp(cli->user,usr)) {
				if (IS_DISABLED(cli->flags)) { // Connect only enabled clients
					///pthread_mutex_unlock(&prg.lockcccli);
					debugf(getdbgflag(DBG_CCCAM,cli->parent->id,cli->id)," CCcam%d: connection refused for cacheexclient '%s' (%s), client disabled\n", cccam->id, usr, ip2string(ip));
					close(sock);
					return NULL;
				}
				found = 1;
				break;
			}
			cli = cli->next;
		}
	}
	///pthread_mutex_unlock(&prg.lockcccli);

	if (!found) {
		debugf(getdbgflag(DBG_CCCAM,cccam->id,0)," CCcam%d: Unknown Client '%s' (%s)\n", cccam->id, usr, ip2string(ip));
		close(sock);
		return NULL;
	}
	memcpy( &tmpcli, cli, sizeof(struct cc_client_data));
	connected_client_id = cli->id;

	// Check for Host
	if (cli->host) {
		struct host_data *host = cli->host;
		if (host_record_client_ip(host, ip, getseconds() + 60)) {
			debugf(getdbgflag(DBG_CCCAM,cli->parent->id,cli->id)," CCcam%d: Aborted connection from Client '%s' (%s), ip refused\n", cccam->id, usr, ip2string(ip));
			cli->nbloginerror++;
			close(sock);
			return NULL;
		}
	}

	// Encrypted Password
	if ((i=recv_nonb(sock, buf, 6,5000)) == 6) {
		memset(pwd, 0, sizeof(pwd));
		strcpy(pwd, cli->pass);
		cc_encrypt(&recvblock, (uint8_t*)pwd, strlen(pwd));
		cc_decrypt(&recvblock, buf, 6);
		if ( memcmp(buf,"CCcam\0",6) ) {
			debugf(getdbgflag(DBG_CCCAM,cli->parent->id,cli->id)," CCcam%d: login failed from client '%s'\n", cccam->id, usr);
			cli->nbloginerror++;
			close(sock);
			return NULL;
		}
	}
	else {
		debugf(getdbgflag(DBG_CCCAM,cli->parent->id,cli->id)," CCcam%d: login failed from client '%s', error receiving crypted password\n", cccam->id, usr);
		cli->nbloginerror++;
		close(sock);
		return NULL;
	}

	if (cli->ip==ip) cli->nbdiffip++;


	// Send passwd ack
	memset(buf, 0, 20);
	memcpy(buf, "CCcam\0", 6);
	//debugf(getdbgflag(DBG_CCCAM,cli->parent->id,cli->id),"Server: send ack '%s'\n",buf);
	cc_encrypt(&sendblock, buf, 20);
	if (!send_nonb(sock, buf, 20, 100) ) {
		close(sock);
		return NULL;
	}
	memcpy(&tmpcli.sendblock,&sendblock,sizeof(sendblock));
	memcpy(&tmpcli.recvblock,&recvblock,sizeof(recvblock));
	debugf(getdbgflag(DBG_CCCAM,cli->parent->id,cli->id)," CCcam%d: client '%s' connected\n", cccam->id, usr);
	// Recv cli data
	memset(buf, 0, sizeof(buf));
	i = cc_msg_recv( sock, &tmpcli.recvblock, buf, 5000);
	if ( i<65 ) {
		debugf(getdbgflag(DBG_CCCAM,cli->parent->id,cli->id)," CCcam%d: Error recv cli '%s' data (%d/%d)\n", cccam->id, cli->user, i, errno);
		debughex(buf,i);
		close(sock);
		return NULL;
	}
	// Setup Client Data
	// pthread_mutex_lock(&prg.lockcccli);
	memcpy( tmpcli.nodeid, buf+24, 8);
	memcpy( tmpcli.version, buf+33, 31);
	memcpy( tmpcli.build, buf+65, 31 );
	debugf(getdbgflag(DBG_CCCAM,cli->parent->id,cli->id)," CCcam%d: client '%s' running version %s build %s\n", cccam->id, usr, tmpcli.version, tmpcli.build);  // cli->nodeid,8,
	// Check for Nodeid/CCcam Version
	if (cli->option.checknodeid)
	if (cli->option.nodeid[0] && cli->option.nodeid[7]) {
		if (memcmp(cli->option.nodeid, tmpcli.nodeid, 8)) { // diff nodeid
			debugf(getdbgflag(DBG_CCCAM,cli->parent->id,cli->id)," CCcam%d: login failed from client '%s' (%s), wrong nodeid\n", cccam->id, usr, ip2string(ip));
			cli->nbloginerror++;
			close(sock);
			return NULL;
		}
	}

	//Check Reconnection. Serialize the final session swap with response writes
	//and teardown, but never hold client state while sending on the socket.
	int reject_login = 0, reject_different_ip = 0, already_connected = 0;
	if (client_io_lock(cli) != 0) {
		close(sock);
		return NULL;
	}
	if (client_state_lock(cli) != 0) {
		client_io_unlock(cli);
		close(sock);
		return NULL;
	}
	if (cli->connection.status > 0) {
		if (tick32_elapsed(GetTickCount(), cli->connection.time) > 60000u) {
			int same_ip = cli->ip == ip;
			cc_disconnect_cli_unlocked(cli);
			if (same_ip) already_connected = 1;
			else {
				cli->nbloginerror++;
				reject_login = 1;
				reject_different_ip = 1;
			}
		} else {
			cli->nbloginerror++;
			reject_login = 1;
		}
	}
	if (!reject_login) {
		cli->nblogin++;
		memcpy(&cli->sendblock, &tmpcli.sendblock, sizeof(tmpcli.sendblock));
		memcpy(&cli->recvblock, &tmpcli.recvblock, sizeof(tmpcli.recvblock));
		memcpy(cli->nodeid, tmpcli.nodeid, 8);
		// store nodeid if not set :)
		if (!cli->option.nodeid[0] && !cli->option.nodeid[7]) {
			memcpy(cli->option.nodeid, tmpcli.nodeid, 8);
			prg.updatenodes = 1;
		}
		memcpy(cli->version, tmpcli.version, 31);
		memcpy(cli->build, tmpcli.build, 31);
			cli->cardsent = 0;
			memset(&cli->ecm, 0, sizeof(cli->ecm));
			memset(&cli->lastecm, 0, sizeof(cli->lastecm));
			cli->handle = sock;
			cli->session_generation = cc_session_generation_next();
			connected_session_generation = cli->session_generation;
			cli->connection.status = 1;
		cli->connection.time = cli->lastactivity = GetTickCount();
		cli->lastecmtime = 0;
		cli->chkrecvtime = 0;
		cli->ip = ip;
		cli->msg.len = 0;
	}
	client_state_unlock(cli);
	client_io_unlock(cli);
	if (reject_login) {
		if (reject_different_ip)
			debugf(getdbgflag(DBG_CCCAM,cli->parent->id,cli->id)," CCcam%d: Client '%s' (%s) already connected with different ip, Connection closed.\n", cccam->id, usr, ip2string(ip));
		else
			debugf(getdbgflag(DBG_CCCAM,cli->parent->id,cli->id)," CCcam%d: Client '%s' just connected, new connection closed (%s)\n", cccam->id, usr, ip2string(ip));
		close(sock);
		return NULL;
	}
	if (already_connected)
		debugf(getdbgflag(DBG_CCCAM,cli->parent->id,cli->id)," CCcam%d: Client '%s' (%s) already connected\n", cccam->id, usr, ip2string(ip));
#ifdef IPLIST
	if (ipdata) iplist_goodlogin(ipdata);
#endif

	// send cli data ack without holding client state across the TCP write
	if (client_io_lock(cli) == 0) {
		int send_ack = 0;
		if (client_state_lock(cli) == 0) {
			send_ack = cc_client_session_matches_locked(
				cli, sock, connected_session_generation);
			client_state_unlock(cli);
		}
		if (send_ack)
			cc_msg_send(sock, &cli->sendblock, CC_MSG_CLI_INFO, 0, NULL);
		client_io_unlock(cli);
	}
	//cc_msg_send( sock, &cli->sendblock, CC_MSG_BAD_ECM, 0, NULL);
	/* Preserve the legacy wire gate (version byte 28 == 'W') without
	 * assigning into the live client's version buffer during the check. */
	sendversion = tmpcli.version[28] == 'W';
	cc_sendinfo_cli_session(cli, sock, connected_session_generation,
	                        sendversion);
	//cc_msg_send( sock, &cli->sendblock, CC_MSG_BAD_ECM, 0, NULL);
	if (client_state_lock(cli) == 0) {
		if (cc_client_session_matches_locked(cli, sock,
		                                     connected_session_generation)) {
			cli->cardsent = 1;
			session_current = 1;
		}
		client_state_unlock(cli);
	}
	if (!session_current) return NULL;
	//TODO: read from client packet CC_MSG_BAD_ECM
	//len = cc_msg_recv(cli->handle, &cli->recvblock, buf, 3);
	usleep(55000);
	if (!cc_client_session_snapshot(cli, &current_sock, &current_generation) ||
	    current_sock != sock ||
	    current_generation != connected_session_generation) return NULL;
#ifdef CACHEEX
	if (!cli->cacheex_mode)
#endif
	cc_sendcards_cli_session(cli, sock, connected_session_generation);
	if (!cc_client_session_snapshot(cli, &current_sock, &current_generation) ||
	    current_sock != sock ||
	    current_generation != connected_session_generation) return NULL;

#ifdef CACHEEX
	if (cli->cacheex_mode==3) {
		struct cc_cli_recv_session_args *recv_session =
			malloc(sizeof(*recv_session));
		int start_worker = 0;
		if (!recv_session) {
			cc_disconnect_cli_session(cli, sock, connected_session_generation);
			return NULL;
		}
		recv_session->cli = cli;
		recv_session->sock = sock;
		recv_session->generation = connected_session_generation;
		/* Serialize the tid publication with session replacement. */
		if (client_io_lock(cli) != 0) {
			free(recv_session);
			cc_disconnect_cli_session(cli, sock, connected_session_generation);
			return NULL;
		}
		if (client_state_lock(cli) == 0) {
			start_worker = cc_client_session_matches_locked(
				cli, sock, connected_session_generation);
			client_state_unlock(cli);
		}
		if (!start_worker) {
			client_io_unlock(cli);
			free(recv_session);
			return NULL;
		}
		if (!create_thread(PACKED_MPTR(cli, pthread_t, tid),
		                   cacheex_cc_cli_recvmsg, recv_session)) {
			client_io_unlock(cli);
			free(recv_session);
			cc_disconnect_cli_session(cli, sock, connected_session_generation);
			return NULL;
		}
		client_io_unlock(cli);
	}
	else
	if (cli->cacheex_mode) pipe_wakeup( prg.pipe.cacheex[1] );
	else
#endif

#ifdef EPOLL_CCCAM
	pipe_send_u32(prg.pipe.cccam[1], PIPE_CLI_CONNECTED, connected_client_id);
#else
	pipe_wakeup( prg.pipe.cccam[1] );
#endif

	// update pfd data ???
	cli->parent->clipfd.update = 1;
	return cli;
}

/* The detached handshake borrows param->server and cfg.* across bounded
 * socket waits. Pin the config read side for the whole handshake so reload
 * cannot reclaim those records underneath it; the pin is re-entrant and the
 * reload writer does not take the client locks this path uses. */
void *cc_connect_cli(struct connect_cli_data *param)
{
	void *result;
	newcamd_config_read_lock();
	result = cc_connect_cli_inner(param);
	newcamd_config_read_unlock();
	return result;
}

////////////////////////////////////////////////////////////////////////////////

void cccam_srv_accept(struct cccam_server_data *srv)
{
	struct sockaddr_in newaddr;
	socklen_t socklen = sizeof(struct sockaddr);
	int newfd = accept( srv->handle, (struct sockaddr*)&newaddr, /*(socklen_t*)*/&socklen);
	if ( newfd<=0 ) {
		if ( (errno!=EAGAIN) && (errno!=EINTR) ) debugf( getdbgflag(DBG_CCCAM,srv->id,0)," CCcam%d: Accept failed (errno=%d)\n", srv->id,errno);
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
			debugf(getdbgflag(DBG_CCCAM,srv->id,0)," CCcam%d: New Connection (%s) closed, ip blocked\n", srv->id, ip2string(newip) );
			close(newfd);
		}
#ifdef IPLIST
		else if ( !iplist_accept( ipdata ) ) {
			debugf(getdbgflag(DBG_CCCAM,srv->id,0)," CCcam%d: New Connection (%s) closed, ip temporary blocked\n", srv->id, ip2string(newip) );
			close(newfd);
		}
#endif
		else {
			pthread_t srv_tid;
			if (cfg.cccam.keepalive) SetSocketKeepalive(newfd);
			SetSocketNoDelay(newfd);
			if (SetSoketNonBlocking(newfd) < 0) {
				debugf(DBG_ERROR, " CCcam%d: unable to set accepted socket nonblocking (errno=%d)\n", srv->id, errno);
				close(newfd);
				return;
			}
			//debugf(getdbgflag(DBG_CCCAM,srv->id,0)," CCcam%d: new connection (%s)\n", srv->id, ip2string(newip) );
			struct connect_cli_data *newdata = malloc( sizeof(struct connect_cli_data) );
			newdata->server = srv; 
			newdata->sock = newfd; 
			newdata->ip = newaddr.sin_addr.s_addr;
			if ( !create_thread(&srv_tid, (threadfn)cc_connect_cli,newdata) ) {
				free( newdata );
				close( newfd );
			}
		}
	}
}

void *cccam_accept_thread(void *param)
{
	prctl(PR_SET_NAME,"CCcam Accept",0,0,0);
	sleep(5);

	while(!prg.restart) {
		uint64_t config_generation;
		struct pollfd pfd[18];
		int pfdcount = 0;
		newcamd_config_read_lock();
		config_generation = cfg.cache.generation;
		struct cccam_server_data *cccam = cfg.cccam.server;
		while (cccam) {
			if ( !IS_DISABLED(cccam->flags) && (cccam->handle>0) ) {
				cccam->ipoll = pfdcount;
				pfd[pfdcount].fd = cccam->handle;
				pfd[pfdcount++].events = POLLIN | POLLPRI;
			} else cccam->ipoll = -1;
			cccam = cccam->next;
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
				struct cccam_server_data *cccam = cfg.cccam.server;
				while (cccam) {
					if ( !IS_DISABLED(cccam->flags) && (cccam->handle>0) &&
					     (cccam->ipoll>=0) && (cccam->handle==pfd[cccam->ipoll].fd) &&
					     (pfd[cccam->ipoll].revents & (POLLIN|POLLPRI)) )
						cccam_srv_accept(cccam);
					cccam = cccam->next;
				}
				newcamd_config_read_unlock();
			} else if (retval < 0) usleep(96000);
		} else sleep(1);
	}
	return NULL;
}
////////////////////////////////////////////////////////////////////////////////
// CCCAM SERVER: SEND DCW TO CLIENTS
////////////////////////////////////////////////////////////////////////////////

/* The caller owns the per-client I/O guard. Drop client state before any
 * socket write/poll, then reacquire it before publishing state changes. */
static int cc_msg_send_client_locked(struct cc_client_data *cli,
                                     cc_msg_cmd cmd, int len, uint8_t *buf,
                                     uint8_t *followup, int followup_len,
                                     int *client_state_locked)
{
	int sock, result;
	uint32_t session_generation;
	if (!cli || !client_state_locked || !*client_state_locked ||
	    len < 0 || followup_len < 0)
		return 0;
	sock = cli->handle;
	session_generation = cli->session_generation;
	if (sock <= 0 || cli->connection.status <= 0 || !session_generation)
		return 0;

	client_state_unlock(cli);
	*client_state_locked = 0;
	if (followup_len)
		result = cc_msg_send_and_encrypt(sock, &cli->sendblock, cmd, len,
		                                buf, followup, followup_len);
	else
		result = cc_msg_send(sock, &cli->sendblock, cmd, len, buf);
	if (client_state_lock(cli) != 0) abort();
	*client_state_locked = 1;
	/* The I/O guard pins this session while state is temporarily unlocked. */
	if (!cc_client_session_matches_locked(cli, sock, session_generation))
		return 0;
	return result;
}

/* CacheEX pushes originate outside the client receive loop. Serialize them with
 * every other CCcam write and teardown, and close only the session whose write
 * failed while its I/O guard still pins the descriptor. */
int cc_send_cacheex_push_cli(struct cc_client_data *cli, uint8_t *buf, int len)
{
	int client_state_locked = 0;
	int attempted = 0;
	int result = 0;
	if (!cli || !buf || len < 0 || client_io_lock(cli) != 0) return 0;
	if (client_state_lock(cli) != 0) {
		client_io_unlock(cli);
		return 0;
	}
	client_state_locked = 1;
#ifdef CACHEEX
	if (cli->cacheex_mode == 2 && cli->handle > 0 &&
	    cli->connection.status > 0) {
		attempted = 1;
		result = cc_msg_send_client_locked(cli, CC_MSG_CACHE_PUSH, len, buf,
		                                   NULL, 0, &client_state_locked);
		if (!result && cli->handle > 0 && cli->connection.status > 0)
			cc_disconnect_cli_unlocked(cli);
	}
#endif /* CACHEEX: without it there is no cache-push to attempt */
	if (client_state_locked) client_state_unlock(cli);
	client_io_unlock(cli);
	return attempted;
}

static void cc_senddcw_cli_locked(struct cc_client_data *cli,
                                  const struct ledger_delivery_snapshot *delivery,
                                  int *client_state_locked)
{
	uint8_t buf[CC_MAXMSGSIZE];
	uint64_t current_lifetime_id;
	uint16_t response_caid;
	uint32_t response_provid;
	uint8_t response_provider_known;
	uint16_t response_sid;
	uint32_t response_hash;
	uint8_t response_tag;
	uint32_t response_profile;
	uint32_t response_recvtime;
	uint32_t ticks = GetTickCount();
	int samechannel, enablefreeze = 0;

	if (!cli || !delivery || !delivery->lifetime_id ||
	    delivery->request == NULL || !client_state_locked ||
	    !*client_state_locked) return;
	current_lifetime_id = __atomic_load_n(&cli->ecm.lifetime_id, __ATOMIC_ACQUIRE);
	/* Request pointers and hashes are reusable. Validate the generation while
	 * state is locked; the I/O guard then keeps this client session stable. */
	if (current_lifetime_id != delivery->lifetime_id ||
	    !ecm_request_lifetime_matches(cli->ecm.request, delivery->request,
	                                  delivery->lifetime_id) ||
	    ecm_request_lifetime_id_load(delivery->request) != delivery->lifetime_id)
		return;
	if (IS_DISABLED(cli->flags) || cli->connection.status <= 0 ||
	    cli->handle <= 0 || !cli->ecm.busy) return;
	if (cli->ecm.status == STAT_DCW_SENT) {
		debugf(getdbgflag(DBG_CCCAM,cli->parent->id,cli->id),
		       " +> cw send failed to CCcam client '%s', cw already sent\n", cli->user);
		return;
	}

	response_caid = delivery->caid;
	response_provid = delivery->provid;
	response_provider_known = delivery->provider_known;
	response_sid = delivery->sid;
	response_hash = delivery->hash;
	response_tag = delivery->tag;
	response_profile = delivery->profile_id;
	response_recvtime = delivery->recvtime;

	samechannel = (cli->lastecm.caid == response_caid) &&
	              (cli->lastecm.prov == response_provid) &&
	              (cli->lastecm.sid == response_sid);
	if (samechannel) {
		if (cli->lastecm.hash != response_hash && cli->lastecm.tag != response_tag &&
		    cli->lastecm.status == 1 &&
		    (uint32_t)(ticks - cli->lastdcwtime) > 200u)
			enablefreeze = 1;
	}

	if (delivery->status == STAT_DCW_SUCCESS) {
		uint32_t sent_at;
		memcpy(buf, delivery->cw, 16);
		cc_crypt_cw(cli->nodeid, cli->ecm.cardid, buf);
		if (!cc_msg_send_client_locked(cli, CC_MSG_ECM_REQUEST, 16, buf,
		                               buf, 16, client_state_locked)) {
			if (delivery->trace_instance)
				user_trace_client_result(cli->id, delivery->trace_instance,
				                        delivery->hash, USER_TRACE_SEND_FAILED,
				                        delivery->srctype, delivery->srcid,
				                        ticks - response_recvtime, "CW send failed");
			cc_disconnect_cli_unlocked(cli);
			return;
		}
		sent_at = GetTickCount();
		if (!samechannel) cli->zap++;
		cli->lastecm.caid = response_caid;
		cli->lastecm.prov = response_provid;
		cli->lastecm.provider_known = response_provider_known;
		cli->lastecm.sid = response_sid;
		cli->lastecm.hash = response_hash;
		cli->lastecm.tag = response_tag;
		cli->lastecm.decodetime = ticks - response_recvtime;
		cli->lastecm.request = delivery->request;
#ifdef CACHEEX
		LEDGER_DELIVERY_TO_LASTECM(cli->lastecm, *delivery);
#else
		cli->lastecm.dcwsrctype = delivery->srctype;
		cli->lastecm.dcwsrcid = delivery->srcid;
		memcpy(cli->lastecm.dcw, delivery->cw, sizeof(cli->lastecm.dcw));
#endif
		cli->lastecm.status = 1;
		cli->ecmok++;
		cli->lastdcwtime = sent_at;
		cli->ecmoktime += sent_at - response_recvtime;
		if (delivery->trace_instance)
			user_trace_client_cw(cli->id, delivery->trace_instance, delivery->hash,
			                     delivery->srctype, delivery->srcid,
			                     sent_at - response_recvtime,
			                     "CW response sent; picture not verified", delivery->cw);
#ifdef CACHEEX
		ledger_note_delivered(delivery, sent_at);
#endif
		debugf(getdbgflagpro(DBG_CCCAM,cli->parent->id,cli->id,response_profile),
		       " => cw to CCcam client '%s' ch %04x:%06x:%04x (%dms)\n",
		       cli->user, response_caid, response_provid, response_sid,
		       sent_at - response_recvtime);
	} else {
		if (!cc_msg_send_client_locked(cli, CC_MSG_ECM_NOK1, 0, NULL,
		                               NULL, 0, client_state_locked)) {
			if (delivery->trace_instance)
				user_trace_client_result(cli->id, delivery->trace_instance,
				                        delivery->hash, USER_TRACE_SEND_FAILED,
				                        0, 0, ticks - response_recvtime,
				                        "failure response send failed");
			cc_disconnect_cli_unlocked(cli);
			return;
		}
		if (!samechannel) cli->zap++;
		if (enablefreeze) cli->freeze++;
		cli->lastecm.caid = response_caid;
		cli->lastecm.prov = response_provid;
		cli->lastecm.provider_known = response_provider_known;
		cli->lastecm.sid = response_sid;
		cli->lastecm.hash = response_hash;
		cli->lastecm.tag = response_tag;
		cli->lastecm.decodetime = ticks - response_recvtime;
		cli->lastecm.request = delivery->request;
		cli->lastecm.dcwsrctype = DCW_SOURCE_NONE;
		cli->lastecm.dcwsrcid = 0;
		cli->lastecm.status = 0;
		memset(cli->lastecm.dcw, 0, sizeof(cli->lastecm.dcw));
#ifdef CACHEEX
		memset(cli->lastecm.ecmd5, 0, sizeof(cli->lastecm.ecmd5));
		cli->lastecm.ecmlen = 0;
#endif
		if (delivery->trace_instance)
			user_trace_client_result(cli->id, delivery->trace_instance,
			                        delivery->hash, USER_TRACE_FAILED, 0, 0,
			                        ticks - response_recvtime,
			                        "decode failed; detailed reason unavailable");
		debugf(getdbgflagpro(DBG_CCCAM,cli->parent->id,cli->id,response_profile),
		       " |> decode failed to CCcam client '%s' ch %04x:%06x:%04x (%dms)\n",
		       cli->user, response_caid, response_provid, response_sid,
		       ticks - response_recvtime);
	}
	cli->ecm.busy = 0;
	cli->ecm.status = STAT_DCW_SENT;
}
void cc_senddcw_cli(struct cc_client_data *cli,
                    const struct ledger_delivery_snapshot *delivery)
{
	int client_state_locked = 0;
	if (!cli || client_io_lock(cli) != 0) return;
	if (client_state_lock(cli) == 0) {
		client_state_locked = 1;
		cc_senddcw_cli_locked(cli, delivery, &client_state_locked);
		if (client_state_locked) client_state_unlock(cli);
	}
	client_io_unlock(cli);
}

///////////////////////////////////////////////////////////////////////////////

// Check sending cw to clients
void cc_check_sendcw(ECM_DATA *ecm, const struct ledger_delivery_snapshot *delivery)
{
	if (!delivery || !delivery->lifetime_id || delivery->request != ecm) return;
	struct cccam_server_data *cccam = cfg.cccam.server;
	while (cccam) {
		if (!IS_DISABLED(cccam->flags) && cccam->handle > 0) {
			struct cc_client_data *cli = cccam->client;
			while (cli) {
				struct cc_client_data *next = cli->next;
				cc_senddcw_cli(cli, delivery);
				cli = next;
			}
		}
		cccam = cccam->next;
	}
}

///////////////////////////////////////////////////////////////////////////////
// CCCAM SERVER: RECEIVE MESSAGES FROM CLIENTS
///////////////////////////////////////////////////////////////////////////////

static int cc_capture_client_delivery_locked(
	struct ledger_delivery_snapshot *out, ECM_DATA *ecm,
	const struct cc_client_data *cli)
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

#define CC_CAPTURE_CLIENT_DELIVERY(name, request, client) \
	struct ledger_delivery_snapshot name##_storage; \
	const struct ledger_delivery_snapshot *name = \
		cc_capture_client_delivery_locked(&name##_storage, (request), (client)) ? \
		&name##_storage : NULL

void cc_store_ecmclient(ECM_DATA *ecm, struct cc_client_data *cli)
{
	uint32_t ticks = GetTickCount();
	cli->ecm.recvtime = ticks;
	cli->ecm.request = ecm;
	__atomic_store_n(&cli->ecm.lifetime_id, ecm_request_lifetime_id_load(ecm), __ATOMIC_RELEASE);
    cli->ecm.status = STAT_ECM_SENT;
	ecm_addip(ecm, cli->ip);

	/* IBS: Record ECM request from client for black screen detection.
	 * If this ECM arrives < 3s after we sent a CW for the same channel,
	 * the previous CW was WRONG (client got black screen and is retrying). */
	{
		uint32_t bad_source = 0;
		uint8_t bad_cw_hash[8] = {0};
		int bs_detected = ibs_on_ecm_request(
			cli->ip,                          /* client_id (IP as identifier) */
			ecm->caid, ecm->provid, ecm->sid, /* channel */
			ticks,                            /* now */
			&bad_source, bad_cw_hash);        /* output: bad source info */

		if (bs_detected) {
			debugf(0, " !!! [IBS-DETECT] BLACK SCREEN detected for %04X:%06X:%04X from client %08X -- previous CW from source %d was WRONG\n",
				ecm->caid, ecm->provid, ecm->sid, cli->ip, bad_source);
			/* Demote the source that gave us the bad CW */
			cwpostval_trust_bad(bad_source);
		}
	}
}


///////////////////////////////////////////////////////////////////////////////

// Receive messages from client
static void cc_cli_parsemsg_locked(struct cc_client_data *cli, uint8_t *buf, int len,
                                       int *client_state_locked)
{
	if (!cli || !buf || len < 2 || len >= CC_MAXMSGSIZE ||
	    !client_state_locked || !*client_state_locked) return;

	uint8_t data[CC_MAXMSGSIZE]; // for other use
	uint8_t cw[16];
	unsigned int cardid;

	uint32_t ticks = GetTickCount();
	cli->lastactivity = ticks;
	switch (buf[1]) {
		 case CC_MSG_ECM_REQUEST:
			cli->ecmnb++;
			cli->lastecmtime = ticks;
			if (len<20) return; // Avoid malicious peers
/*
			if (cli->ecm.busy) {
				// send decode failed
				cli->ecmdenied++;
				if ( !cc_msg_send_client_locked(cli, CC_MSG_ECM_NOK1, 0, NULL, NULL, 0,
							      client_state_locked) ) {
					debugf(getdbgflag(DBG_ERROR,0,0)," Error encountred when sending failedcw to client '%s'\n", cli->user);

					cc_disconnect_cli_unlocked(cli);
					return;
				}
				debugf(getdbgflag(DBG_CCCAM,cli->parent->id,cli->id)," <|> decode failed to CCcam client '%s', too many ecm requests\n", cli->user);

				break;
			}
*/
			cli->ecm.busy = 0;
			//Check for card availability
			memcpy( data, buf+17, len-17);
			cardid = buf[10]<<24 | buf[11]<<16 | buf[12]<<8 | buf[13];
			uint16_t caid = buf[4]<<8 | buf[5];
			uint16_t sid = buf[14]<<8 | buf[15];
			uint32_t provid = ecm_getprovid( data, len-17, caid );
			if (provid==0) provid = buf[6]<<24 | buf[7]<<16 | buf[8]<<8 | buf[9];

			uint64_t trace_session = 0, trace_attempt = 0;
			uint32_t trace_hash = 0;
			if (user_trace_matches(cli->id, caid, provid, sid)) {
				trace_hash = hashCode(data + 3, len - 20);
				trace_attempt = user_trace_begin(cli->id, caid, provid, sid,
				                                 trace_hash, &trace_session);
			}

			// Check for Profile
			struct cardserver_data *cs=getcsbyid( cardid );
			if (!cs) {
				// check for cs by caid:prov
				cs = getcsbycaidprov(caid,provid);
				if (!cs) {
					if (trace_attempt) user_trace_add(trace_session, trace_attempt, USER_TRACE_DENIED, trace_hash, 0, 0, 0, "profile not found");
					cli->ecmdenied++;
					if ( !cc_msg_send_client_locked(cli, CC_MSG_ECM_NOK1, 0, NULL, NULL, 0,
							      client_state_locked) ) {
						debugf(getdbgflag(DBG_ERROR,0,0)," Error encountred when sending failedcw to client '%s'\n", cli->user);
						cc_disconnect_cli_unlocked(cli);
						return;
					}
					debugf(getdbgflag(DBG_CCCAM,cli->parent->id,cli->id)," <|> decode failed to CCcam client '%s', card-id %x (%04x:%06x) not found\n",cli->user, cardid, caid,provid);
					break;
				}
			}
			// Check for Share
			if (!cs->option.fsharecccam) {
				if (trace_attempt) user_trace_add(trace_session, trace_attempt, USER_TRACE_DENIED, trace_hash, 0, 0, 0, "profile not shared with CCcam");
				cli->ecmdenied++;
				if ( !cc_msg_send_client_locked(cli, CC_MSG_ECM_NOK1, 0, NULL, NULL, 0,
							      client_state_locked) ) {
					debugf(getdbgflag(DBG_ERROR,0,0)," Error encountred when sending failedcw to client '%s'\n", cli->user);
					cc_disconnect_cli_unlocked(cli);
					return;
				}
				debugf(getdbgflag(DBG_CCCAM,cli->parent->id,cli->id)," <|> decode failed to CCcam client '%s', Profile '%s' disabled to client\n",cli->user, cs->name);
				break;
			}
			// Chec for ECM
			uint8_t cw1cycle;
			char *error = cs_accept_ecm(cs,caid,provid,sid,ecm_getchid(data,len-17,caid), len-17, data, &cw1cycle);
			if (error) {
				if (trace_attempt) user_trace_add(trace_session, trace_attempt, USER_TRACE_DENIED, trace_hash, 0, 0, 0, error);
				cs->ecmdenied++;
				cli->ecmdenied++;
				if (!cc_msg_send_client_locked(cli, CC_MSG_ECM_NOK1, 0, NULL, NULL, 0,
							      client_state_locked)) {
					cc_disconnect_cli_unlocked(cli);
					return;
				}
				debugf(getdbgflagpro(DBG_CCCAM,cli->parent->id,cli->id,cs->id)," <|> decode failed to CCcam client '%s' ch %04x:%06x:%04x, %s\n", cli->user, caid,provid,sid, error);
				break;
			}

			// ACCEPTED
			ecm_lock();

			// Search for ECM
			ECM_DATA *ecm = search_ecmdata_any(cs, data,  len-17, sid, caid, provid, 1);
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
					attest_note( ATTEST_TAG_CCCAM | (uint32_t)cli->id, cli->lastecm.dcw,
						cli->lastecm.caid, cli->lastecm.prov,
						cli->lastecm.provider_known, cli->lastecm.sid,
						cli->lastecm.dcwsrctype, cli->lastecm.dcwsrcid, ticks );
#ifdef CACHEEX
					LEDGER_NOTE_CLIENT_FAILURE(ecm, cli->lastecm, cli->lastdcwtime, ticks);
#endif
				}
			}
			int isnew =  ( ecm==NULL );
			int reply_failure = 0;
			uint32_t failed_hash = 0;
			if (ecm) {
				if (trace_attempt) {
					if (!ecm->trace_instance) ecm->trace_instance = user_trace_next_instance();
					if (user_trace_bind_shared(trace_session, trace_attempt, trace_hash, ecm->trace_instance))
						user_trace_add(trace_session, trace_attempt, USER_TRACE_JOINED, trace_hash, 0, 0, 0, "joined existing ECM; earlier events unavailable");
				}
				ecm->lastrecvtime = ticks;
				if (ecm->dcwstatus==STAT_DCW_FAILED) {
					if (ecm->period > cs->option.dcw.retry) {
						if (trace_attempt) {
							user_trace_add(trace_session, trace_attempt, USER_TRACE_FAILED, trace_hash, 0, 0, 0, "existing ECM already failed");
							user_trace_unbind_shared(trace_session, trace_attempt);
						}
						failed_hash = ecm->hash;
						reply_failure = 1;
					}
					else {
						ecm->period++; // RETRY
						cc_store_ecmclient(ecm, cli);
						debugf(getdbgflagpro(DBG_CCCAM,cli->parent->id,cli->id,cs->id)," <- ecm from CCcam client '%s' ch %04x:%06x:%04x:%08x**\n", cli->user, caid, provid, sid, ecm->hash);
						cli->ecm.busy=1;
						cli->ecm.hash = ecm->hash;
						cli->ecm.cardid = cardid;
						ecm_request_set_dcwstatus(ecm, STAT_DCW_WAIT);
						ecm->cachestatus = 0; //ECM_CACHE_NONE; // Resend Request
						ecm->checktime = 1; // Check NOW
						pipe_wakeup( prg.pipe.ecm[1] );
					}
				}
				else { // SUCCESS/WAIT
					cc_store_ecmclient(ecm, cli);
					debugf(getdbgflagpro(DBG_CCCAM,cli->parent->id,cli->id,cs->id)," <- ecm from CCcam client '%s' ch %04x:%06x:%04x:%08x*\n", cli->user, caid, provid, sid, ecm->hash);
					cli->ecm.busy=1;
					cli->ecm.hash = ecm->hash;
					cli->ecm.cardid = cardid;
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
								CC_CAPTURE_CLIENT_DELIVERY(delivery, ecm, cli);
								ecm_unlock();
								cc_senddcw_cli_locked(cli, delivery, client_state_locked);
								break;
							}
						}
						else {
								CC_CAPTURE_CLIENT_DELIVERY(delivery, ecm, cli);
								ecm_unlock();
								cc_senddcw_cli_locked(cli, delivery, client_state_locked);
								break;
						}
					}
				}
			}
			else {
				// Setup ECM Request for Server(s)
				ecm = store_ecmdata(cs, data, len-17, sid, caid, provid);
				if (!ecm) {
					cs->ecmdenied++;
					cli->ecmdenied++;
					if (trace_attempt) {
						user_trace_add(trace_session, trace_attempt, USER_TRACE_FAILED, trace_hash, 0, 0, 0, "ECM allocation failed; request not queued");
						user_trace_unbind_shared(trace_session, trace_attempt);
					}
					ecm_unlock();
					if (!cc_msg_send_client_locked(cli, CC_MSG_ECM_NOK1, 0, NULL, NULL, 0,
						      client_state_locked)) cc_disconnect_cli_unlocked(cli);
					break;
				}
				cs->ecmaccepted++;
				if (trace_attempt) {
					ecm->trace_instance = user_trace_next_instance();
					if (user_trace_bind_shared(trace_session, trace_attempt, trace_hash, ecm->trace_instance))
						user_trace_add(trace_session, trace_attempt, USER_TRACE_NEW, trace_hash, 0, 0, 0, "new ECM accepted");
				}
				cc_store_ecmclient(ecm, cli);
				debugf(getdbgflagpro(DBG_CCCAM,cli->parent->id,cli->id,cs->id)," <- ecm from CCcam client '%s' ch %04x:%06x:%04x:%08x\n",cli->user,caid,provid,sid, ecm->hash);
				cli->ecm.busy=1;
				cli->ecm.hash = ecm->hash;
				cli->ecm.cardid = cardid;
				ecm->cw1cycle = cw1cycle;
				ecm_request_set_dcwstatus(ecm, STAT_DCW_WAIT);

#ifdef CHECK_NEXTDCW
				if (cli->dcwcheck) checkfreeze_checkECM( ecm, cli->lastecm.request);
#endif
				if (cs->option.fallowcache) {
					uint32_t cache_initial_delay =
					    ecm_get_cache_initial_delay_ms(ecm, cs, GetTickCount());
					ecm->waitcache = 1;
					ecm_request_set_dcwstatus(ecm, STAT_DCW_WAITCACHE);
					ecm->checktime = ecm->recvtime + cache_initial_delay;
					if (ecm->trace_instance) {
						char detail[128];
						snprintf(detail, sizeof(detail), "cache wait policy: initial schedule=%u ms; static=%s; may end early or be rescheduled",
						         (unsigned)cache_initial_delay, cs->option.cachestatic ? "YES" : "NO");
						user_trace_shared_event(ecm->trace_instance, USER_TRACE_CACHE, 0, 0,
						                        GetTickCount()-ecm->recvtime, detail);
					}
					pipe_cache_find(ecm, cs);
					if (ecm->trace_instance) user_trace_shared_event(ecm->trace_instance, USER_TRACE_CACHE,
					    0, 0, GetTickCount()-ecm->recvtime, "cache lookup invoked; pipe delivery and result not confirmed");
				}
				else ecm->checktime = 1; // Check NOW
				pipe_wakeup( prg.pipe.ecm[1] );

#ifdef TESTCHANNEL
				int testchannel = ( (ecm->caid==cfg.testchn.caid)&&(ecm->provid==cfg.testchn.provid)&&(!cfg.testchn.sid||(ecm->sid==cfg.testchn.sid)) );
				if (testchannel) {
					fdebugf(" <- ecm from CCcam client '%s' ch %04x:%06x:%04x %02x:%08x\n", cli->user, ecm->caid, ecm->provid, ecm->sid, ecm->ecm[0], ecm->hash);
				}
#endif
			}

			ecm_unlock();
			if (reply_failure) {
				if (!cc_msg_send_client_locked(cli, CC_MSG_ECM_NOK1, 0, NULL, NULL, 0,
							      client_state_locked)) {
					cc_disconnect_cli_unlocked(cli);
					return;
				}
				debugf(getdbgflag(DBG_CCCAM,cli->parent->id,cli->id),
				       " <|> decode failed to CCcam client '%s' ch %04x:%06x:%04x:%08x, already failed\n",
				       cli->user, caid, provid, sid, failed_hash);
				break;
			}
			if (isnew) wakeup_sendecm();
			break;

		 case CC_MSG_KEEPALIVE:
			if ( !cc_msg_send_client_locked(cli, CC_MSG_KEEPALIVE, 0, NULL, NULL, 0,
						      client_state_locked) ) cc_disconnect_cli_unlocked( cli );
			//printf(" Keepalive from client '%s'\n",cli->user);
			break;

#ifdef CACHEEX
		 case CC_MSG_CACHE_PUSH:
			if (cli->cacheex_mode!=3) break;
			//if (buf[18]!=0) break; // Got CW
			memcpy( cw, buf+44, 16);
			if (dcw_hard_reject(cw)) break; /* TASK 3.15: profile gate is cache_setdcw */
			struct cache_data cacheex;
			memset(&cacheex, 0, sizeof(cacheex));
			cacheex.provider_known = 1; /* fixed-width packet has provider, even when zero */
			cacheex.caid = (buf[4]<<8) | buf[5];
			cacheex.provid = (buf[6]<<24) | (buf[7]<<16) | (buf[8]<<8) | buf[9];
			cacheex.sid = (buf[14]<<8) | buf[15];
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
			//
			pthread_mutex_lock( &prg.lockcache );
			int res = cache_setdcw( &cacheex, cw, NO_CYCLE, PEER_CCCAM_CLIENT | cli->id );
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
				cwn_note( DCW_SOURCE_CACHE, PEER_CCCAM_CLIENT | cli->id, cw, cacheex.caid, cacheex.provid, cacheex.provider_known, cacheex.sid, GetTickCount() );
				/*
				 * TASK 1.4 — a definitive proof scores against the source that
				 * supplied the key, at GR4 granularity. The srcid keeps its PEER_*
				 * origin flags, because a CSP peer and a cache-exchange client with
				 * the same id are different sources and must not share a score.
				 */
				trust_record_lk_scoped( &trust_tab, TRUST_EV_PROOF, DCW_SOURCE_CACHE, PEER_CCCAM_CLIENT | cli->id,
					cacheex.caid, cacheex.provid, cacheex.provider_known,
					cacheex.sid, GetTickCount() );
				/* TASK 2.8: the same proof, summed one level coarser. */
				trustagg_note_lk( TRUST_EV_PROOF, DCW_SOURCE_CACHE, PEER_CCCAM_CLIENT | cli->id,
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
				if ( cache_purge_mark_scoped( &purge_tab, DCW_SOURCE_CACHE, PEER_CCCAM_CLIENT | cli->id,
						cacheex.caid, cacheex.sid, cacheex.provid, cacheex.provider_known,
						GetTickCount() ) ) {
					int npurged = cache_purge_sweep_scoped( &purge_tab, DCW_SOURCE_CACHE, PEER_CCCAM_CLIENT | cli->id,
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
			//debugf( getdbgflag(DBG_CACHEEX, 0, 0)," CACHEEX PUSH from client '%s' %04x:%06x:%04x:%08x\n",cli->user,cacheex.caid,cacheex.provid,cacheex.sid,cacheex.hash);
			break;
#endif

		 case CC_MSG_CW_SHARE:
			// BISS Reverse Sharing: Client shares CW with Server
			{
				if (len < 25) break;  // Need at least 25 bytes
				
				uint16_t share_caid = (buf[0] << 8) | buf[1];
				uint32_t share_provid = (buf[2] << 24) | (buf[3] << 16) | (buf[4] << 8) | buf[5];
				uint16_t share_sid = (buf[6] << 8) | buf[7];
				uint8_t cw[16];
				memcpy(cw, &buf[8], 16);
				uint8_t source = buf[24];
				
				// Validate CW (basic checks)
				if (dcw_hard_reject(cw)) {
					debugf(0, "BISS SHARE: Rejected invalid CW from client '%s' for %04X:%06X:%04X\n",
						cli->user, share_caid, share_provid, share_sid);
					break;
				}
				
				// Save to BISS keys table
				if (biss_add(share_caid, share_sid, share_provid, cw)) {
					debugf(0, "BISS SHARE: Received CW from client '%s' for %04X:%06X:%04X (source=%d)\n",
						cli->user, share_caid, share_provid, share_sid, source);
					
					// Update CacheEX-specific client stats when that feature is built.
#ifdef CACHEEX
					pthread_mutex_lock(&prg.lockcacheex);
					cli->cacheex.got[0]++;
					pthread_mutex_unlock(&prg.lockcacheex);
#endif
					
					// Log for monitoring
					biss_keys.keys[biss_keys.count-1].last_used = GetTickCount();
					biss_keys.keys[biss_keys.count-1].use_count++;
				}
				break;
			}

		 case CC_MSG_BAD_ECM:
			break;

		default:
			debugf(0, " unknown message type\n"); debughex(buf, len);
	}
}




/* The caller owns the per-client I/O guard. This helper drops client state
 * around socket reads (including the optional blocking receive path), then
 * returns with state held so the parsed message can be applied atomically. */
static int cc_cli_peekmsg_locked(struct cc_client_data *cli, uint8_t *buf,
                                 int *client_state_locked)
{
	int len, disconnect = 0, incomplete = 0, sock;
	uint32_t ticks;
	if (!cli || !buf || !client_state_locked || !*client_state_locked)
		return -2;
	sock = cli->handle;
	if (sock <= 0 || cli->connection.status <= 0) return -2;

	client_state_unlock(cli);
	*client_state_locked = 0;
#ifdef RECVMSG_PEEK
	len = cc_msg_chkrecv(sock, &cli->recvblock);
	if (len == 0) disconnect = 1;
	else if (len < 0) incomplete = 1;
	else {
		len = cc_msg_recv(sock, &cli->recvblock, buf, 3000);
		if (len <= 0) disconnect = 1;
	}
#else
	len = cc_peekmsg(sock, &cli->recvblock, &cli->msg, buf);
	if (len == 0) disconnect = 1;
	else if (len < 0) incomplete = 1;
#endif
	if (client_state_lock(cli) != 0) abort();
	*client_state_locked = 1;
	if (cli->handle != sock || cli->connection.status <= 0) return -2;

	ticks = GetTickCount();
	if (disconnect) {
		debugf(getdbgflag(DBG_CCCAM,cli->parent->id,cli->id),
		       " CCcam: client '%s' read failed %d(%d)\n", cli->user, len, errno);
		cc_disconnect_cli_unlocked(cli);
		return -2;
	}
	if (incomplete) {
		uint32_t timeout = 1000u;
#ifdef RECVMSG_PEEK
		timeout = 500u;
#endif
		if (!cli->chkrecvtime) cli->chkrecvtime = ticks;
		else if ((uint32_t)(ticks - cli->chkrecvtime) > timeout) {
			debugf(getdbgflag(DBG_CCCAM,cli->parent->id,cli->id),
			       " CCcam: client '%s' read failed %d(%d)\n", cli->user, len, errno);
			cc_disconnect_cli_unlocked(cli);
			return -2;
		}
		return -1;
	}
	cli->chkrecvtime = 0;
	return len;
}

/* Receive and parse only if the event still belongs to this connection.
 * State is released before socket reads; the I/O guard pins the session. */
void cc_cli_recvmsg_session(struct cc_client_data *cli, int expected_sock,
                            uint32_t expected_generation)
{
	uint8_t buf[CC_MAXMSGSIZE];
	int client_state_locked = 0;
	int len;
	if (!cli || client_io_lock(cli) != 0) return;
	if (client_state_lock(cli) != 0) {
		client_io_unlock(cli);
		return;
	}
	client_state_locked = 1;
	if (!cc_client_session_matches_locked(cli, expected_sock,
	                                      expected_generation)) goto done;
	len = cc_cli_peekmsg_locked(cli, buf, &client_state_locked);
	if (len > 0 && len < CC_MAXMSGSIZE)
		cc_cli_parsemsg_locked(cli, buf, len, &client_state_locked);

done:
	if (client_state_locked) client_state_unlock(cli);
	client_io_unlock(cli);
}

void *cc_cli_recvmsg(struct cc_client_data *cli)
{
	int sock;
	uint32_t generation;
	if (cc_client_session_snapshot(cli, &sock, &generation))
		cc_cli_recvmsg_session(cli, sock, generation);
	return NULL;
}

#ifdef CACHEEX
// Receive messages from one CacheEX client session. The expected socket and
// generation are captured by the accepting thread before this worker is created.
void *cacheex_cc_cli_recvmsg(void *param)
{
	struct cc_cli_recv_session_args *recv_session = param;
	struct cc_client_data *cli;
	int sock;
	uint32_t generation;
	pid_t tid = (pid_t)syscall(SYS_gettid);
	if (!recv_session) return NULL;
	cli = recv_session->cli;
	sock = recv_session->sock;
	generation = recv_session->generation;
	free(recv_session);
	if (!cli || sock <= 0 || !generation) return NULL;
	if (client_state_lock(cli) == 0) {
		if (cc_client_session_matches_locked(cli, sock, generation))
			cli->pid = tid;
		client_state_unlock(cli);
	}
	for (;;) {
		struct pollfd pfd;
		int current_sock;
		uint32_t current_generation;
		int retval;
		if (!cc_client_session_snapshot(cli, &current_sock,
		                                &current_generation) ||
		    current_sock != sock || current_generation != generation)
			break;
		pfd.fd = sock;
		pfd.events = POLLIN | POLLPRI;
		retval = poll(&pfd, 1, 3005); // for 3seconds
		if (retval == 0) continue;
		if (retval < 0) {
			cc_disconnect_cli_session(cli, sock, generation);
			break;
		}
		if (pfd.revents & (POLLIN | POLLPRI))
			cc_cli_recvmsg_session(cli, sock, generation);
		else {
			cc_disconnect_cli_session(cli, sock, generation);
			break;
		}
	}
	if (client_state_lock(cli) == 0) {
		if (cc_client_session_matches_locked(cli, sock, generation))
			cli->pid = 0;
		client_state_unlock(cli);
	}
	return NULL;
}
#endif

void cc_recv_pipe()
{
	struct cccam_server_data *cccam;
	struct cardserver_data *cs;
	struct cardserver_data card_event;

	struct cc_client_data *cli;

	//debugf(getdbgflag(DBG_CCCAM,0,0)," CCcam: recv_msg from pipe\n");

	uint8_t buf[1024];
	struct pollfd pfd[2];
	do {
		int len = pipe_recv( prg.pipe.cccam[0], buf, sizeof(buf));
		if (len>0) {
			//debugf(getdbgflag(DBG_CCCAM,0,0)," CCcam: recv_msg from pipe (%d)\n", buf[0]);
			switch(buf[0]) {
				case PIPE_WAKEUP:  // ADD NEW CLIENT
					if (len != 2) break;
					//debugf(" wakeup csmsg\n");
					break;
#ifdef EPOLL_CCCAM
					case PIPE_CLI_CONNECTED:  // ADD NEW FD by stable client id
						{
							uint32_t client_id;
							uint32_t generation;
							int handle;
							struct epoll_event ev;
							if (!pipe_decode_u32_message(buf, (size_t)len, PIPE_CLI_CONNECTED, &client_id) || !client_id) break;
							cli = getcccamclientbyid(client_id);
							if (!cli || IS_DISABLED(cli->flags) || client_io_lock(cli) != 0) break;
							if (client_state_lock(cli) != 0) {
								client_io_unlock(cli);
								break;
							}
							handle = cli->handle;
							generation = cli->session_generation;
							if (handle > 0 && cli->connection.status > 0 && generation) {
								memset(&ev, 0, sizeof(ev));
								ev.events = EPOLLIN | EPOLLPRI | EPOLLRDHUP;
								ev.data.u64 = cccam_session_epoll_token(client_id,
														generation);
								client_state_unlock(cli);
								if (epoll_ctl(prg.epoll.cccam, EPOLL_CTL_ADD, handle, &ev) == -1)
									debugf(DBG_ERROR,"Err! EPOLL_CTL_ADD %s (%d) %d\n", cli->user, handle, errno);
							} else client_state_unlock(cli);
							client_io_unlock(cli);
						}
						break;
#endif
					case PIPE_CARD_DEL:
					case PIPE_CARD_ADD:
						{
							struct pipe_card_snapshot snapshot;
							struct cc_client_data *cardcli;
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
							cccam = cfg.cccam.server;
							while (cccam) {
								if (!IS_DISABLED(cccam->flags) && cccam->handle > 0) {
									cardcli = cccam->client;
									while (cardcli) {
										if (!IS_DISABLED(cardcli->flags) && cardcli->handle > 0) {
											if (buf[0] == PIPE_CARD_DEL) cc_sendcard_del(cs, cardcli);
											else cc_sendcard_cli(cs, cardcli, 0);
										}
										cardcli = cardcli->next;
									}
								}
								cccam = cccam->next;
							}
						}
						break;
			}
		}
		pfd[0].fd = prg.pipe.cccam[0];
		pfd[0].events = POLLIN | POLLPRI;
	} while (poll(pfd, 1, 3)>0);
}


///////////////////////////////////////////////////////////////////////////////

#ifdef EPOLL_CCCAM

void *cc_recvmsg_thread(void *param)
{
	int i;

	pthread_mutex_lock(&prg.locksrv);
	cfg.cccam.pid_recvmsg = syscall(SYS_gettid);
	pthread_mutex_unlock(&prg.locksrv);
	__atomic_store_n(&prg.pid_cc_msg, (pid_t)syscall(SYS_gettid), __ATOMIC_RELEASE);
	prctl(PR_SET_NAME,"CCcam RecvMSG",0,0,0);

	struct epoll_event evlist[MAX_EPOLL_EVENTS]; // epoll recv events
	prg.epoll.cccam = epoll_create( MAX_EPOLL_EVENTS );
	// Add PIPE
	struct epoll_event ev; // epoll event
	memset(&ev, 0, sizeof(ev));
	ev.events = EPOLLIN | EPOLLPRI | EPOLLRDHUP;
	ev.data.u64 = 0;
	if ( epoll_ctl(prg.epoll.cccam, EPOLL_CTL_ADD, prg.pipe.cccam[0], &ev) == -1 ) printf("epoll_ctl");

	while (!prg.restart) {
		newcamd_config_read_lock();
		uint64_t config_generation = cfg.cache.generation;
		uint32_t thread_delay = cfg.delay.thread;
		newcamd_config_read_unlock();
		int ready = epoll_wait( prg.epoll.cccam, evlist, MAX_EPOLL_EVENTS, 1002);
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
			uint64_t token = evlist[i].data.u64;
			uint32_t client_id = 0;
			uint32_t session_generation = 0;
			if (token && !cccam_session_epoll_token_decode(
					 token, &client_id, &session_generation)) continue;
			if ( evlist[i].events & (EPOLLIN|EPOLLPRI) ) {
				if (!token) cc_recv_pipe();
				else if (client_id && session_generation) {
					struct cc_client_data *cli = getcccamclientbyid(client_id);
					if (cli) cc_cli_recvmsg_session(cli, 0, session_generation);
				}
			}
			else if ( evlist[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR) ) {
				if (!token) debugf(DBG_ERROR,"Err! epoll_wait() pipe\n");
				else if (client_id && session_generation) {
					struct cc_client_data *cli = getcccamclientbyid(client_id);
					if (cli) cc_disconnect_cli_session(cli, 0, session_generation);
				}
			}
		}
		newcamd_config_read_unlock();
	}
	return NULL;
}

#else

void *cc_recvmsg_thread(void *param)
{
	struct pollfd pfd[MAX_PFD];
	int pfdcount;
	uint32_t thread_delay;

	pthread_mutex_lock(&prg.locksrv);
	cfg.cccam.pid_recvmsg = syscall(SYS_gettid);
	pthread_mutex_unlock(&prg.locksrv);
	__atomic_store_n(&prg.pid_cc_msg, (pid_t)syscall(SYS_gettid), __ATOMIC_RELEASE);
	prctl(PR_SET_NAME,"CCcam RecvMSG",0,0,0);

	while (!prg.restart) {
		uint64_t config_generation;
		newcamd_config_read_lock();
		config_generation = cfg.cache.generation;
		thread_delay = cfg.delay.thread;
		pfdcount = 0;
		// PIPE
		pfd[pfdcount].fd = prg.pipe.cccam[0];
		pfd[pfdcount++].events = POLLIN | POLLPRI;

		struct cccam_server_data *cccam = cfg.cccam.server;
		while (cccam) {
			if ( !IS_DISABLED(cccam->flags)&&(cccam->handle>0) ) {
				if (cccam->clipfd.update||cccam->clipfd.count<0) {
					cccam->clipfd.count = 0;
					struct cc_client_data *cli = cccam->client;
					while (cli && (cccam->clipfd.count<CCCAM_MAX_PFD)) {
						int sock;
						uint32_t generation;
						if (!IS_DISABLED(cli->flags) && !(cli->flags&FLAG_WORKTHREAD) &&
						    cc_client_session_snapshot(cli, &sock, &generation)) {
							int slot = cccam->clipfd.count++;
							cli->ipoll = slot;
							cccam->clipfd.pfd[slot].fd = sock;
							cccam->clipfd.pfd[slot].events = POLLIN | POLLPRI;
							cccam->clipfd.session_generation[slot] = generation;
						} else cli->ipoll = -1;
						cli = cli->next;
					}
					cccam->clipfd.update = 0;
					//debugf(getdbgflag(DBG_ERROR,0,0), " CCcam clients poll updated %d\n", cccam->clipfd.count);
				}
				cccam->clipfd.ipoll = pfdcount;
				if (cccam->clipfd.count>0) {
					memcpy( &pfd[pfdcount], cccam->clipfd.pfd, cccam->clipfd.count * sizeof(struct pollfd) );
					pfdcount += cccam->clipfd.count;
				}
			}
			cccam = cccam->next;
		}

		newcamd_config_read_unlock();
		int retval = poll(pfd, pfdcount, 3004); // for 3seconds

		if ( retval>0 ) {
			if (thread_delay) usleep(thread_delay);
			newcamd_config_read_lock();
			if (config_generation != cfg.cache.generation) {
				newcamd_config_read_unlock();
				continue;
			}

			struct cccam_server_data *cccam = cfg.cccam.server;
			while (cccam) {
				if ( !IS_DISABLED(cccam->flags)&&(cccam->handle>0)&&(cccam->clipfd.count>0) ) {
					//pthread_mutex_lock(&prg.lockcccli);
					struct cc_client_data *cccli = cccam->client;
					while (cccli) {
						if (!IS_DISABLED(cccli->flags) && !(cccli->flags&FLAG_WORKTHREAD) &&
						    cccli->ipoll >= 0 && cccli->ipoll < cccam->clipfd.count) {
							int poll_index = cccam->clipfd.ipoll + cccli->ipoll;
							int sock = cccam->clipfd.pfd[cccli->ipoll].fd;
							uint32_t generation = cccam->clipfd.session_generation[cccli->ipoll];
							if (poll_index >= 0 && poll_index < pfdcount &&
							    pfd[poll_index].fd == sock && generation) {
								if (pfd[poll_index].revents & (POLLHUP|POLLNVAL))
									cc_disconnect_cli_session(cccli, sock, generation);
								else if (pfd[poll_index].revents & (POLLIN|POLLPRI))
								cc_cli_recvmsg_session(cccli, sock, generation);
							}
						}
						///else if ( (GetTickCount()-cccli->lastactivity) > 600000 ) cc_disconnect_cli(cccli);
						cccli = cccli->next;
					}
					//pthread_mutex_unlock(&prg.lockcccli);
				}
				cccam = cccam->next;
			}
			//
			if ( pfd[0].revents & (POLLIN|POLLPRI) ) cc_recv_pipe();
			newcamd_config_read_unlock();
		}
		else if ( retval<0 ) {
			debugf(getdbgflag(DBG_CCCAM,0,0), " thread receive messages: poll error %d(errno=%d)\n", retval, errno);
			usleep(99000);
		}
	}
	return NULL;
}

#endif


///////////////////////////////////////////////////////////////////////////////
// CCCAM SERVER: START/STOP
///////////////////////////////////////////////////////////////////////////////

int start_thread_cccam()
{
	pthread_t tid;
#ifndef MONOTHREAD_ACCEPT
	create_thread(&tid, cccam_accept_thread,NULL);
#endif

	create_thread(PACKED_MPTR(&cfg, pthread_t, cccam.tid_recvmsg), cc_recvmsg_thread,NULL);
	return 0;
}



