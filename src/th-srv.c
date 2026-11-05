///////////////////////////////////////////////////////////////////////////////
// THREAD CONNECT TO SERVERS
///////////////////////////////////////////////////////////////////////////////

static void server_connect_set_status(struct server_data *srv, int status)
{
	pthread_mutex_lock(&prg.locksrv);
	srv->connection.status = status;
	pthread_mutex_unlock(&prg.locksrv);
}

void *cs_connect_srv_th(struct server_data *srv)
{
	int fd;
	// The scheduler marks this record as connecting before releasing the
	// server-state lock and starting this detached worker.
	struct host_data *host = srv->host;
	uint32_t ip, clip;
	host_state_snapshot(host, &ip, &clip, NULL);
	if (!ip) ip = clip;
	fd = CreateClientSockTcp(ip, srv->port);
	if (fd<0) {
		char *msg;
		int connect_error = errno;
		if (connect_error==ECONNREFUSED) {
			static char refused[] = "No-one listening on the remote address";
			msg = refused;
		}
		else if (connect_error==ENETUNREACH) {
			static char unreachable[] = "Network is unreachable";
			msg = unreachable;
		}
		else if (connect_error==ETIMEDOUT) {
			static char timeout[] = "Timeout while attempting connection";
			msg = timeout;
		}
		else {
			static char failed[] = "socket connection failed";
			msg = failed;
		}
		host_schedule_checkiptime_before(host, getseconds() + 60);
		pthread_mutex_lock(&prg.locksrv);
		srv->error = connect_error;
		srv->statmsg = msg;
		if (srv->connection.delay<90000) srv->connection.delay += 10000;
		srv->connection.status = 0;
		pthread_mutex_unlock(&prg.locksrv);
		debugf(getdbgflag(DBG_SERVER,0,srv->id)," server: socket connection failed to server (%s:%d)\n", srv->host->name,srv->port);
		return NULL;
	}

	//SetSocketKeepalive(fd);
	SetSocketNoDelay(fd);
	if (SetSoketNonBlocking(fd) < 0) {
		static char msg[] = "unable to set connected socket nonblocking";
		int connect_error = errno;
		pthread_mutex_lock(&prg.locksrv);
		srv->error = connect_error;
		srv->statmsg = msg;
		srv->connection.status = 0;
		pthread_mutex_unlock(&prg.locksrv);
		debugf(getdbgflag(DBG_SERVER,0,srv->id),
		       " server: unable to configure nonblocking socket for (%s:%d), errno=%d\n",
		       srv->host->name, srv->port, connect_error);
		close(fd);
		return NULL;
	}

	pthread_mutex_lock(&prg.locksrv);
	if (srv->connection.delay<90000) srv->connection.delay += 15000;
	srv->error = 0; // No error
	pthread_mutex_unlock(&prg.locksrv);

	if (srv->type==TYPE_NEWCAMD) {
		//debugf(0," Connecting to Newcamd server (%s:%d) ...\n", srv->host->name,srv->port);
		if ( cs_connect_srv(srv,fd)!=0 ) {
			debugf(getdbgflag(DBG_SERVER,0,srv->id)," server: connection failed to newcamd server (%s:%d)\n", srv->host->name,srv->port);
			server_connect_set_status(srv, 0);
			close(fd);
		}
	}
#ifdef CCCAM_CLI
	else if (srv->type==TYPE_CCCAM) {
		//debugf(0," Connecting to CCcam server (%s:%d) ...\n", srv->host->name,srv->port);
		if ( cc_connect_srv(srv,fd)!=0 ) {
			debugf(getdbgflag(DBG_SERVER,0,srv->id)," server: connection failed to CCcam server (%s:%d)\n", srv->host->name,srv->port);
			server_connect_set_status(srv, 0);
			close(fd);
		}
	}
#endif
#ifdef RADEGAST_CLI
	else if (srv->type==TYPE_RADEGAST) {
		//debugf(0," Connecting to Radegast server (%s:%d) ...\n", srv->host->name,srv->port);
		if ( rdgd_connect_srv(srv,fd)!=0 ) {
			debugf(getdbgflag(DBG_SERVER,0,srv->id)," server: connection failed to Radegast server (%s:%d)\n", srv->host->name,srv->port);
			server_connect_set_status(srv, 0);
			close(fd);
		}
	}
#endif
#ifdef CS378X_CLI
	else if (srv->type==TYPE_CS378X) {
		//debugf(0," Connecting to CS378X server (%s:%d) ...\n", srv->host->name,srv->port);
		if ( cs378x_connect_srv(srv,fd)!=0 ) {
			debugf(getdbgflag(DBG_SERVER,0,srv->id)," server: connection failed to cs378x server (%s:%d)\n", srv->host->name,srv->port);
			server_connect_set_status(srv, 0);
			close(fd);
		}
	}
#endif
	else close(fd);

	return NULL;
}


#ifdef CAMD35_CLI
void *camd35_connect_srv_th(struct server_data *srv)
{
	if (srv->type!=TYPE_CAMD35) return NULL;

	//debugf(0," Connecting to camd35 server (%s:%d) ...\n", srv->host->name,srv->port);
	struct host_data *host = srv->host;
	uint32_t ip, clip;
	host_state_snapshot(host, &ip, &clip, NULL);
	if (!ip) ip = clip;
	
	int fd =  CreateClientSockUdp( 0, 0 ); //srv->port, ip);
	if (fd<0) {
		static char msg[]= "socket creation failed";
		pthread_mutex_lock(&prg.locksrv);
		srv->statmsg = msg;
		srv->connection.delay += 10000;
		srv->connection.status = 0;
		pthread_mutex_unlock(&prg.locksrv);
		return NULL;
	}
	pthread_mutex_lock(&prg.locksrv);
	if (srv->connection.delay<90000) srv->connection.delay += 15000;
	srv->error = 0; // No error
	pthread_mutex_unlock(&prg.locksrv);

	if ( camd35_connect_srv(srv,fd)!=0 ) {
		debugf(getdbgflag(DBG_SERVER,0,srv->id)," server: connection failed to camd35 server (%s:%d)\n", srv->host->name,srv->port);
		server_connect_set_status(srv, 0);
		close( fd );
	}

	return NULL;
}
#endif


typedef void *(*server_connect_fn)(struct server_data *srv);

struct server_connect_job {
	server_connect_fn worker;
	struct server_data *srv;
};

static void *server_connect_worker(void *param)
{
	struct server_connect_job *job = (struct server_connect_job *)param;
	server_connect_fn worker = job->worker;
	struct server_data *srv = job->srv;
	free(job);
	worker(srv);
	server_connect_end();
	return NULL;
}

static void server_connect_start(pthread_t *tid, server_connect_fn worker,
                                 struct server_data *srv)
{
	struct server_connect_job *job = malloc(sizeof(*job));
	if (!job) return;
	job->worker = worker;
	job->srv = srv;
	/* Caller pins the config read side while filling this token and starting
	 * the worker; reload waits for the worker before mutating the graph. The
	 * token is unique per connection attempt and also tags its epoll events. */
	pthread_mutex_lock(&prg.locksrv);
	if (IS_DISABLED(srv->flags) || srv->connection.status != 0) {
		pthread_mutex_unlock(&prg.locksrv);
		free(job);
		return;
	}
	srv->connect_generation = server_connect_begin();
	srv->connection.status = -1;
	srv->connection.time = GetTickCount();
	pthread_mutex_unlock(&prg.locksrv);
	if (!create_thread(tid, server_connect_worker, job)) {
		server_connect_end();
		pthread_mutex_lock(&prg.locksrv);
		srv->connection.status = 0;
		pthread_mutex_unlock(&prg.locksrv);
		free(job);
	}
}

void server_set_statmsg(struct server_data *srv, char *message)
{
	if (!srv) return;
	pthread_mutex_lock(&prg.locksrv);
	srv->statmsg = message;
	pthread_mutex_unlock(&prg.locksrv);
}

int server_connection_is_enabled(struct server_data *srv, int fd)
{
	int active, enabled;
	if (!srv || fd <= 0) return 0;
	pthread_mutex_lock(&prg.locksrv);
	active = srv->connection.status > 0 && srv->handle == fd;
	enabled = active && srv->connect_generation && !IS_DISABLED(srv->flags);
	if (active && !enabled) disconnect_srv(srv);
	pthread_mutex_unlock(&prg.locksrv);
	return enabled;
}

/* Register a completed connection with the ECM loop. If the bounded control
 * pipe cannot accept the frame, retire this socket rather than leave a live but
 * unmonitored connection behind. The connection worker's active count pins the
 * server record until this function returns. */
int server_publish_connection_ready(struct server_data *srv, int fd)
{
	uint64_t generation;
	int active, ready;
	if (!srv || fd <= 0) return 0;
	pthread_mutex_lock(&prg.locksrv);
	generation = srv->connect_generation;
	active = srv->connection.status > 0 && srv->handle == fd;
	ready = active && generation && !IS_DISABLED(srv->flags);
	if (active && !ready) disconnect_srv(srv);
	pthread_mutex_unlock(&prg.locksrv);
	if (!ready) return 0;
#ifdef EPOLL_ECM
	if (pipe_send_server_connected(prg.pipe.ecm[1], srv->id, generation) > 0)
		return 1;
	debugf(DBG_ERROR, "server %u connection registration pipe failed; disconnecting\n",
	       (unsigned)srv->id);
	pthread_mutex_lock(&prg.locksrv);
	if (srv->connect_generation == generation && srv->connection.status > 0 &&
	    srv->handle == fd)
		disconnect_srv(srv);
	pthread_mutex_unlock(&prg.locksrv);
	return 0;
#else
	pipe_cmd(prg.pipe.ecm[1], PIPE_SRV_CONNECTED);
	return 1;
#endif
}

void connect_server(struct server_data *srv)
{
	uint32_t ticks = GetTickCount();
	pthread_t srv_tid;

	while (srv) {
		uint32_t host_ip, host_clip;
		int disabled, has_address, should_start = 0, blocked;
		pthread_mutex_lock(&prg.locksrv);
		disabled = IS_DISABLED(srv->flags);
		host_state_snapshot(srv->host, &host_ip, &host_clip, NULL);
		has_address = host_ip || host_clip;
		if (disabled) {
			static char msg[]= "Disabled";
			srv->statmsg = msg;
		}
		else if (!has_address) {
			static char msg[]= "Invalid Address";
			srv->statmsg = msg;
		}
		else if (!srv->connection.status &&
		         (uint32_t)(ticks - srv->connection.time) > srv->connection.delay) {
			should_start = 1;
		}
		pthread_mutex_unlock(&prg.locksrv);

		blocked = has_address && isblockedip(host_ip);
		if (!disabled && blocked) {
			static char msg[]= "Invalid Address";
			pthread_mutex_lock(&prg.locksrv);
			srv->statmsg = msg;
			pthread_mutex_unlock(&prg.locksrv);
		}
		if (!disabled && has_address && should_start && !blocked) {
#ifdef CAMD35_CLI
			if (srv->type==TYPE_CAMD35) server_connect_start(&srv_tid, camd35_connect_srv_th, srv);
			else
#endif
			server_connect_start(&srv_tid, cs_connect_srv_th, srv);
		}
		srv = srv->next;
	}
}

void *connect_servers(void *param)
{
	prg.pid_srv = syscall(SYS_gettid);
	prctl(PR_SET_NAME,"Server Conn",0,0,0);

	while (!prg.restart) {
		newcamd_config_read_lock();
		pthread_mutex_lock(&prg.locksrvth);
		connect_server(cfg.server);
		pthread_mutex_unlock(&prg.locksrvth);
		newcamd_config_read_unlock();

		sleep(1);

		newcamd_config_read_lock();
		pthread_mutex_lock(&prg.locksrvth);
		connect_server(cfg.cacheexserver);
		pthread_mutex_unlock(&prg.locksrvth);
		newcamd_config_read_unlock();
		sleep(3);
	}
	return NULL;
}


int start_thread_srv()
{
	create_thread(PACKED_MPTR(&prg, pthread_t, tid_srv), (threadfn)connect_servers,NULL);
	return 0;
}


