
///////////////////////////////////////////////////////////////////////////////
// THREAD RESOLVE DNS
///////////////////////////////////////////////////////////////////////////////

static int dns_take_due_host(char hostname[256])
{
	struct host_data *host;
	uint32_t now = getseconds();
	int found = 0;
	pthread_mutex_lock(&prg.lockdnsth);
	for (host = cfg.host; host; host = host->next) {
		if (host->checkiptime <= now) {
			snprintf(hostname, 256, "%s", host->name);
			found = 1;
			break;
		}
	}
	pthread_mutex_unlock(&prg.lockdnsth);
	return found;
}

static void dns_resolve_hostname(const char hostname[256])
{
	struct host_data *host;
	unsigned int newip;
	uint32_t now;

	/* gethostbyname is serialized separately and may block. Never hold
	 * lockdnsth while doing resolver or network work. */
	pthread_mutex_lock(&prg.lockdns);
	newip = hostname2ip((char *)hostname);
	pthread_mutex_unlock(&prg.lockdns);
	usleep(10000);
	now = getseconds();

	/* Re-find by immutable name: reload/auto-add may have changed the list
	 * while DNS ran, so do not carry a host-list pointer across the lookup. */
	pthread_mutex_lock(&prg.lockdnsth);
	for (host = cfg.host; host; host = host->next)
		if (!strcmp(host->name, hostname)) break;
	if (host) {
		if (!newip) {
			host->checkiptime = now + 60;
			host->ip = 0;
		} else if (newip != host->ip) {
			host->checkiptime = now + 300;
			host->ip = newip;
		} else {
			host->checkiptime = now + 600;
		}
	}
	pthread_mutex_unlock(&prg.lockdnsth);
}

void *dns_thread(void *param)
{
	prg.pid_dns = syscall(SYS_gettid);
	//prg.tid_dns = pthread_self();
	prctl(PR_SET_NAME,"Lookup",0,0,0);

	do {
		char hostname[256];
		while (dns_take_due_host(hostname))
			dns_resolve_hostname(hostname);
		sleep(10);
	} while (1);
	return NULL;
}


int start_thread_dns()
{
	create_thread(PACKED_MPTR(&prg, pthread_t, tid_dns), (threadfn)dns_thread,NULL);
	return 0;
}
