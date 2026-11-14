void* thread_enddate(void *param)
{
	__atomic_store_n(&prg.pid_date, (pid_t)syscall(SYS_gettid), __ATOMIC_RELEASE);
	prctl(PR_SET_NAME,"Expire Date Thread",0,0,0);

	while (!prg.restart) {
		pthread_mutex_lock(&prg.lockthreaddate);
		/* Live-TSan follow-up: this scan walks cfg.cccam/cfg.mgcamd client
		 * lists and reads/writes enddate+flags, while reread_config rewrites
		 * the same client records under the config gate
		 * (update_cccam_clients). Hold the gate for the scan. Lock order is
		 * lockthreaddate -> config gate; the reload path never takes
		 * lockthreaddate, so there is no reverse order to deadlock on. */
		user_trace_config_lock();
		time_t nowtime = time(NULL);
		/* localtime() shares one static tm across threads (live-TSan race),
		 * and this TU is compiled -fpack-struct so a local struct tm is
		 * smaller than the one libc writes (ASan stack overflow). Break the
		 * time down in the native-layout safe-time unit instead:
		 * fields = [sec,min,hour,mday,mon,year,wday,yday,isdst]. */
		int nowf[9];
		mcs_localtime_fields(nowtime, nowf);
		//strftime(buf, sizeof(buf), "%d %b %Y %H:%M", nowtm); printf(" Local Time = %s %d\n", buf, nowtm->tm_yday);

		int j = (nowf[4]<<16) | (nowf[3]<<8) | nowf[2];
		// CCcam Clients
		struct cccam_server_data *cccam = cfg.cccam.server;
		while (cccam) {
			struct cc_client_data *cli = cccam->client;
			while (cli) {
				if (!(cli->flags&FLAG_DELETE)) {
					if (cli->enddate.tm_year) {
						int i = (cli->enddate.tm_mon<<16) | (cli->enddate.tm_mday<<8) | cli->enddate.tm_hour;
						//strftime(buf, sizeof(buf), "%d %b %Y %H:%M", &cli->enddate); printf(" Client End date = %s\n", buf);
						if (cli->flags&FLAG_EXPIRED) {
							if (cli->enddate.tm_year > nowf[5]) {
								cli->flags &= ~FLAG_EXPIRED;
								debugf(getdbgflag(DBG_CCCAM,cli->parent->id,cli->id)," CCcam%d: Client '%s' Enabled\n", cccam->id, cli->user);
							}
							else if (cli->enddate.tm_year==nowf[5]) {
								if (i>j) {
									cli->flags &= ~FLAG_EXPIRED;
									debugf(getdbgflag(DBG_CCCAM,cli->parent->id,cli->id)," CCcam%d: Client '%s' Enabled\n", cccam->id, cli->user);
								}
							}
						}
						else {
							if (cli->enddate.tm_year < nowf[5]) {
								cli->flags |= FLAG_EXPIRED;
								if (cli->connection.status>0) cc_disconnect_cli( cli );
								debugf(getdbgflag(DBG_CCCAM,cli->parent->id,cli->id)," CCcam%d: Client '%s' Expired\n", cccam->id, cli->user);
							}
							else if (cli->enddate.tm_year==nowf[5]) {
								if (j>=i) {
									cli->flags |= FLAG_EXPIRED; // printf(" Client Disabled %s\n", cli->user);
									if (cli->connection.status>0) cc_disconnect_cli( cli );
									debugf(getdbgflag(DBG_CCCAM,cli->parent->id,cli->id)," CCcam%d: Client '%s' Expired\n", cccam->id, cli->user);
								}
							}
						}
					}
				}
				cli = cli->next;
			}
			cccam = cccam->next;
		}

		// MGcamd Clients
		// CCcam Clients
		struct mgcamdserver_data *mgcamd = cfg.mgcamd.server;
		while (mgcamd) {
			struct mg_client_data *cli = mgcamd->client;
			while (cli) {
				if (!(cli->flags&FLAG_DELETE)) {
					if (cli->enddate.tm_year) {
						//strftime(buf, sizeof(buf), "%d %b %Y %H:%M", &cli->enddate); printf(" Client End date = %s\n", buf);
						int i = (cli->enddate.tm_mon<<16) | (cli->enddate.tm_mday<<8) | cli->enddate.tm_hour;
						if (cli->flags&FLAG_EXPIRED) {
							if (cli->enddate.tm_year > nowf[5]) {
								cli->flags &= ~FLAG_EXPIRED; //printf(" Client Enabled %s\n", cli->user);
								debugf(getdbgflag(DBG_MGCAMD,0,cli->id)," mgcamd%d: Client '%s' Enabled\n", mgcamd->id, cli->user);
							}
							else if (cli->enddate.tm_year==nowf[5]) {
								if (i>j) {
									cli->flags &= ~FLAG_EXPIRED; //printf(" Client Enabled %s\n", cli->user);
									debugf(getdbgflag(DBG_MGCAMD,0,cli->id)," mgcamd%d: Client '%s' Enabled\n", mgcamd->id, cli->user);
								}
							}
						}
						else {
							if (cli->enddate.tm_year < nowf[5]) {
								cli->flags |= FLAG_EXPIRED;
								if (cli->connection.status>0) mg_disconnect_cli(cli);
								debugf(getdbgflag(DBG_MGCAMD,0,cli->id)," mgcamd%d: Client '%s' Expired\n", mgcamd->id, cli->user);
							}
							else if (cli->enddate.tm_year==nowf[5]) {
								if (j>=i) {
									cli->flags |= FLAG_EXPIRED;
									if (cli->connection.status>0) mg_disconnect_cli(cli);
									debugf(getdbgflag(DBG_MGCAMD,0,cli->id)," mgcamd%d: Client '%s' Expired\n", mgcamd->id, cli->user);
								}
							}
						}
					}
				}
				cli = cli->next;
			}
			mgcamd = mgcamd->next;
		}

		user_trace_config_unlock();
		pthread_mutex_unlock(&prg.lockthreaddate);
		sleep(10);

		// check for load average
		FILE *fp = fopen ("/proc/loadavg", "r");
		if (fp) {
			float avg;
			(void)fscanf(fp, "%f", &avg);
			if (avg>10) {
				flag_debugfile = 1;
				debugf( 0 , " Restart: Load average too high %01.2f\n", avg);
				prg.restart = 1;
			}
			fclose(fp);
		}

	}
	return NULL;
}

void start_thread_date()
{
	create_thread(PACKED_MPTR(&prg, pthread_t, tid_date), (threadfn)thread_enddate,NULL);
}


