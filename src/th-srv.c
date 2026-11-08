///////////////////////////////////////////////////////////////////////////////
// CacheEx mode-2 receive workers
///////////////////////////////////////////////////////////////////////////////

#ifdef CACHEEX
/* The protocol handlers are included before th-srv.c in main.c. */
#ifdef CAMD35_CLI
extern void camd35_srv_recvmsg(struct server_data *srv);
#endif
#ifdef CCCAM_CLI
extern void cc_srv_recvmsg(struct server_data *srv);
#endif
#ifdef CS378X_CLI
extern void *cs378x_srv_recvmsg(struct server_data *srv);
#endif

struct server_receive_job {
	uint32_t server_id;
	uint64_t generation;
	int fd;
	int cacheex_server;
	int type;
};

static char *server_receive_poll_error_message(int error)
{
	static char bad_descriptor[] = "Receive poll failed: invalid socket";
	static char bad_address[] = "Receive poll failed: invalid address";
	static char bad_request[] = "Receive poll failed: invalid request";
	static char no_memory[] = "Receive poll failed: system resources unavailable";
	static char generic[] = "Receive poll failed";
	switch (error) {
		case EBADF: return bad_descriptor;
		case EFAULT: return bad_address;
		case EINVAL: return bad_request;
		case ENOMEM: return no_memory;
		default: return generic;
	}
}

static char *server_receive_resource_message(void)
{
	static char message[] = "Receive poll waiting for system resources";
	return message;
}

static char *server_receive_connected_message(void)
{
	static char message[] = "Connected";
	return message;
}

static void server_receive_disconnect_locked(struct server_data *srv,
                                             char *message, int error)
{
	disconnect_srv(srv);
	srv->error = error;
	srv->statmsg = message;
}

static int server_receive_socket_error(int fd)
{
	int error = 0;
	socklen_t length = sizeof(error);
	if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &length) < 0)
		return errno;
	return error;
}

static int server_receive_type_supported(int type)
{
	switch (type) {
#ifdef CAMD35_CLI
		case TYPE_CAMD35: return 1;
#endif
#ifdef CCCAM_CLI
		case TYPE_CCCAM: return 1;
#endif
#ifdef CS378X_CLI
		case TYPE_CS378X: return 1;
#endif
		default: return 0;
	}
}

/* Called only by a counted outbound connection worker. Config reload takes the
 * write lifetime gate, then drains those workers before changing either list.
 * The receiver job itself deliberately keeps no server_data pointer. */
static int server_receive_server_pool(struct server_data *wanted,
                                      int *cacheex_server)
{
	struct server_data *srv;
	int in_server = 0, in_cacheex = 0;
	for (srv = cfg.server; srv; srv = srv->next)
		if (srv == wanted) { in_server = 1; break; }
	for (srv = cfg.cacheexserver; srv; srv = srv->next)
		if (srv == wanted) { in_cacheex = 1; break; }
	if (in_server == in_cacheex) return 0;
	*cacheex_server = in_cacheex;
	return 1;
}

/* Caller owns the config read gate and prg.locksrv. */
static struct server_data *server_receive_find_locked(
	const struct server_receive_job *job)
{
	struct server_data *srv = job->cacheex_server ? cfg.cacheexserver : cfg.server;
	while (srv) {
		if (srv->id == job->server_id && srv->type == job->type &&
		    srv->connect_generation == job->generation)
			return srv;
		srv = srv->next;
	}
	return NULL;
}

static int server_receive_connection_active(
	const struct server_receive_job *job, pid_t thread_id, int publish_thread_id)
{
	struct server_data *srv;
	int active = 0;
	newcamd_config_read_lock_mutex(&prg.locksrv);
	srv = server_receive_find_locked(job);
	if (srv && srv->connection.status > 0 && srv->handle == job->fd &&
	    !IS_DISABLED(srv->flags)) {
		active = 1;
		if (publish_thread_id) srv->pid = thread_id;
	}
	newcamd_config_read_unlock_mutex(&prg.locksrv);
	return active;
}

static void server_receive_clear_thread_id(
	const struct server_receive_job *job, pid_t thread_id)
{
	struct server_data *srv;
	newcamd_config_read_lock_mutex(&prg.locksrv);
	srv = server_receive_find_locked(job);
	if (srv && srv->pid == thread_id) srv->pid = 0;
	newcamd_config_read_unlock_mutex(&prg.locksrv);
}

/* Caller owns the config read gate and prg.locksrv. */
static void server_receive_dispatch_locked(struct server_data *srv)
{
	switch (srv->type) {
#ifdef CAMD35_CLI
		case TYPE_CAMD35:
			camd35_srv_recvmsg(srv);
			return;
#endif
#ifdef CCCAM_CLI
		case TYPE_CCCAM:
			cc_srv_recvmsg(srv);
			return;
#endif
#ifdef CS378X_CLI
		case TYPE_CS378X:
			(void)cs378x_srv_recvmsg(srv);
			return;
#endif
		default:
			disconnect_srv(srv);
			return;
	}
}

static int server_receive_process_event(
	const struct server_receive_job *job, int poll_result, int poll_error,
	short revents, pid_t thread_id)
{
	struct server_data *srv;
	int keep_running = 0;
	newcamd_config_read_lock_mutex(&prg.locksrv);
	srv = server_receive_find_locked(job);
	if (srv && srv->connection.status > 0 && srv->handle == job->fd &&
	    !IS_DISABLED(srv->flags)) {
		if (poll_result < 0) {
			if (poll_error == EAGAIN || poll_error == ENOMEM) {
				/* Resource pressure is local and may clear without reconnecting
				 * a healthy peer. Keep the reason visible in the web server row. */
				srv->error = poll_error;
				srv->statmsg = server_receive_resource_message();
			}
			else {
				server_receive_disconnect_locked(srv,
					server_receive_poll_error_message(poll_error), poll_error);
			}
		}
		else {
			if (srv->statmsg == server_receive_resource_message()) {
				srv->statmsg = server_receive_connected_message();
				srv->error = 0;
			}
			if (poll_result > 0) {
				int input_ready = (revents & (POLLIN | POLLPRI)) != 0;
				if (revents & POLLNVAL) {
					static char invalid_socket[] = "Receive socket is invalid";
					server_receive_disconnect_locked(srv, invalid_socket, EBADF);
				}
				else {
					if (input_ready) server_receive_dispatch_locked(srv);
					/* poll is level-triggered. Drain any buffered final frames before
					 * retiring HUP/ERR; disconnect once the terminal event remains
					 * without readable data. */
					if (srv->connection.status > 0 && srv->handle == job->fd &&
					    !input_ready && (revents & (POLLERR | POLLHUP))) {
						if (revents & POLLERR) {
							int socket_error = server_receive_socket_error(job->fd);
							static char remote_error[] = "Remote socket error";
							if (!socket_error) socket_error = EIO;
							server_receive_disconnect_locked(srv, remote_error,
							                                  socket_error);
						}
						else {
							static char remote_closed[] = "Remote peer closed connection";
							server_receive_disconnect_locked(srv, remote_closed, 0);
						}
					}
				}
			}
		}
		keep_running = srv->connection.status > 0 && srv->handle == job->fd &&
		               srv->connect_generation == job->generation;
		if (!keep_running && srv->pid == thread_id) srv->pid = 0;
	}
	newcamd_config_read_unlock_mutex(&prg.locksrv);
	return keep_running;
}

static void *server_receive_worker(void *param)
{
	struct server_receive_job *job = (struct server_receive_job *)param;
	pid_t thread_id = (pid_t)syscall(SYS_gettid);
	if (!server_receive_connection_active(job, thread_id, 1)) goto done;
	while (!prg.restart) {
		struct pollfd pfd;
		int poll_result;
		int poll_error = 0;
		if (!server_receive_connection_active(job, thread_id, 0)) break;
		pfd.fd = job->fd;
		pfd.events = POLLIN | POLLPRI;
		pfd.revents = 0;
		poll_result = poll(&pfd, 1, job->type == TYPE_CCCAM ? 3005 : 3009);
		if (poll_result < 0) {
			poll_error = errno;
			if (poll_error == EINTR) continue; /* A signal did not end the connection. */
		}
		if (!server_receive_process_event(job, poll_result, poll_error,
		                                  pfd.revents, thread_id))
			break;
		if (poll_result < 0 && (poll_error == EAGAIN || poll_error == ENOMEM))
			usleep(10000); /* Bound retries while local poll resources recover. */
	}
	done:
	server_receive_clear_thread_id(job, thread_id);
	free(job);
	return NULL;
}
#endif /* CACHEEX */

int server_receive_thread_start(struct server_data *srv)
{
#ifdef CACHEEX
	struct server_receive_job *job;
	int cacheex_server;
	if (!srv || !server_receive_type_supported(srv->type) ||
	    !server_receive_server_pool(srv, &cacheex_server))
		return 0;
	job = (struct server_receive_job *)malloc(sizeof(*job));
	if (!job) return 0;
	pthread_mutex_lock(&prg.locksrv);
	if (srv->cacheex_mode != 2 || srv->connection.status <= 0 ||
	    srv->handle <= 0 || !srv->connect_generation ||
	    IS_DISABLED(srv->flags)) {
		pthread_mutex_unlock(&prg.locksrv);
		free(job);
		return 0;
	}
	job->server_id = srv->id;
	job->generation = srv->connect_generation;
	job->fd = srv->handle;
	job->cacheex_server = cacheex_server;
	job->type = srv->type;
	pthread_mutex_unlock(&prg.locksrv);
	if (!create_thread(PACKED_MPTR(srv, pthread_t, tid),
	                   server_receive_worker, job)) {
		free(job);
		return 0;
	}
	return 1;
#else
	(void)srv;
	return 0;
#endif
}

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
	__atomic_store_n(&prg.pid_srv, (pid_t)syscall(SYS_gettid), __ATOMIC_RELEASE);
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


