
///////////////////////////////////////////////////////////////////////////////
//
///////////////////////////////////////////////////////////////////////////////

#ifdef MONOTHREAD_ACCEPT

void *connect_cli_thread(void *param)
{
	prg.pid_connect = syscall(SYS_gettid);

	while(!prg.restart) {

		struct pollfd pfd[MAX_PFD];
		int pfdcount = 0;

#ifdef FREECCCAM_SRV
		if ( !IS_DISABLED(cfg.freecccam.server.flags)&&(cfg.freecccam.server.handle>0) ) {
				cfg.freecccam.server.ipoll = pfdcount;
				pfd[pfdcount].fd = cfg.freecccam.server.handle;
				pfd[pfdcount++].events = POLLIN | POLLPRI;
		} else cfg.freecccam.server.ipoll = -1;
#endif

#ifdef CCCAM_SRV
		struct cccam_server_data *cccam = cfg.cccam.server;
		while (cccam) {
			if ( !IS_DISABLED(cccam->flags) && (cccam->handle>0) ) {
				cccam->ipoll = pfdcount;
				pfd[pfdcount].fd = cccam->handle;
				pfd[pfdcount++].events = POLLIN | POLLPRI;
			} else cccam->ipoll = -1;
			cccam = cccam->next;
		}
#endif

#ifdef MGCAMD_SRV
		struct mgcamdserver_data *mgcamd = cfg.mgcamd.server;
		while (mgcamd) {
			if ( !IS_DISABLED(mgcamd->flags) && (mgcamd->handle>0) ) {
				mgcamd->ipoll = pfdcount;
				pfd[pfdcount].fd = mgcamd->handle;
				pfd[pfdcount++].events = POLLIN | POLLPRI;
			} else mgcamd->ipoll = -1;
			mgcamd = mgcamd->next;
		}
#endif

#ifdef CS378X_SRV
		struct camd35_server_data *cs378x = cfg.cs378x.server;
		while (cs378x) {
			if ( !IS_DISABLED(cs378x->flags) && (cs378x->handle>0) ) {
				cs378x->ipoll = pfdcount;
				pfd[pfdcount].fd = cs378x->handle;
				pfd[pfdcount++].events = POLLIN | POLLPRI;
			} else cs378x->ipoll = -1;
			cs378x = cs378x->next;
		}
#endif

		struct cardserver_data *cs = cfg.cardserver;
		while(cs) {
			if ( cs->option.fsharenewcamd && !IS_DISABLED(cs->newcamd.flags) && (cs->newcamd.handle>0) ) {
				cs->newcamd.ipoll = pfdcount;
				pfd[pfdcount].fd = cs->newcamd.handle;
				pfd[pfdcount++].events = POLLIN | POLLPRI;
			} else cs->newcamd.ipoll = -1;
			cs = cs->next;
		}


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

#ifdef CCCAM_SRV
			struct cccam_server_data *cccam = cfg.cccam.server;
			while (cccam) {
				if ( !IS_DISABLED(cccam->flags) && (cccam->handle>0) && (cccam->ipoll>=0) && (cccam->handle==pfd[cccam->ipoll].fd) ) {
					if ( pfd[cccam->ipoll].revents & (POLLIN|POLLPRI) ) cccam_srv_accept(cccam);
				}
				cccam = cccam->next;
			}
#endif

#ifdef MGCAMD_SRV
			struct mgcamdserver_data *mgcamd = cfg.mgcamd.server;
			while (mgcamd) {
				if ( !IS_DISABLED(mgcamd->flags) && (mgcamd->handle>0) && (mgcamd->ipoll>=0) && (mgcamd->handle==pfd[mgcamd->ipoll].fd) ) {
					if ( pfd[mgcamd->ipoll].revents & (POLLIN|POLLPRI) ) mgcamd_srv_accept(mgcamd);
				}
				mgcamd = mgcamd->next;
			}
#endif

#ifdef CS378X_SRV
			struct camd35_server_data *cs378x = cfg.cs378x.server;
			while (cs378x) {
				if ( !IS_DISABLED(cs378x->flags) && (cs378x->handle>0) && (cs378x->ipoll>=0) && (cs378x->handle==pfd[cs378x->ipoll].fd) ) {
					if ( pfd[cs378x->ipoll].revents & (POLLIN|POLLPRI) ) cs378x_srv_accept(cs378x);
				}
				cs378x = cs378x->next;
			}
#endif

#ifdef FREECCCAM_SRV
			if ( !IS_DISABLED(cfg.freecccam.server.flags) && (cfg.freecccam.server.handle>0) && (cfg.freecccam.server.ipoll>=0) && (cfg.freecccam.server.handle==pfd[cfg.freecccam.server.ipoll].fd) ) {
				if ( pfd[cfg.freecccam.server.ipoll].revents & (POLLIN|POLLPRI) ) freecccam_srv_accept( &cfg.freecccam.server );
			}
#endif

		}
		else if (retval<0) usleep(96000);
	}
	return NULL;
}

#endif


