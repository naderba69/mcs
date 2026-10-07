
///////////////////////////////////////////////////////////////////////////////
// THREAD REREAD CONFIG
///////////////////////////////////////////////////////////////////////////////

#ifdef INOTIFY
#include "inotify/inotify.h"
#include "inotify/inotify-syscalls.h"
#else
#include <sys/inotify.h>
#endif
/*
static void displayInotifyEvent(struct inotify_event *i)
{
    printf("    wd =%2d; ", i->wd);
    if (i->cookie > 0)
        printf("cookie =%4d; ", i->cookie);

    printf("mask = ");
    if (i->mask & IN_ACCESS)        printf("IN_ACCESS ");
    if (i->mask & IN_ATTRIB)        printf("IN_ATTRIB ");
    if (i->mask & IN_CLOSE_NOWRITE) printf("IN_CLOSE_NOWRITE ");
    if (i->mask & IN_CLOSE_WRITE)   printf("IN_CLOSE_WRITE ");
    if (i->mask & IN_CREATE)        printf("IN_CREATE ");
    if (i->mask & IN_DELETE)        printf("IN_DELETE ");
    if (i->mask & IN_DELETE_SELF)   printf("IN_DELETE_SELF ");
    if (i->mask & IN_IGNORED)       printf("IN_IGNORED ");
    if (i->mask & IN_ISDIR)         printf("IN_ISDIR ");
    if (i->mask & IN_MODIFY)        printf("IN_MODIFY ");
    if (i->mask & IN_MOVE_SELF)     printf("IN_MOVE_SELF ");
    if (i->mask & IN_MOVED_FROM)    printf("IN_MOVED_FROM ");
    if (i->mask & IN_MOVED_TO)      printf("IN_MOVED_TO ");
    if (i->mask & IN_OPEN)          printf("IN_OPEN ");
    if (i->mask & IN_Q_OVERFLOW)    printf("IN_Q_OVERFLOW ");
    if (i->mask & IN_UNMOUNT)       printf("IN_UNMOUNT ");
    printf("\n");

    if (i->len > 0)
        printf("        name = %s\n", i->name);
}
*/

/* TASK 4.1. main used to sleep 100 ms and hope read_config had
 * finished. Under ASan that loses: main sees no profile and exits
 * on a config that has one. The release/acquire pair orders the
 * parser's writes before main reads cfg.cardserver. Not a new lock,
 * and not a change to how the ECM threads share state. */
static int cfg_ready;

static void mark_config_ready(void)
{
	__atomic_store_n(&cfg_ready, 1, __ATOMIC_RELEASE);
}

static int wait_config_ready(void)
{
	int i;

	for (i = 0; i < 500; i++) {
		if (__atomic_load_n(&cfg_ready, __ATOMIC_ACQUIRE)) return 1;
		usleep(10000);
	}
	return 0;
}

void *reread_config_thread(void *param)
{
	prg.pid_cfg = syscall(SYS_gettid);
	//prg.tid_cfg = pthread_self();
	prctl(PR_SET_NAME,"Config Thread",0,0,0);

	init_config(&cfg);
	read_config(&cfg);
	dcw_badlist_commit(cfg.bad_dcw);	/* TASK 3.14 — the list is committed */
	mark_config_ready();			/* TASK 4.1 — main may now look */

	/*
	 * The missing-profile check happens HERE, not in main(), and that is the
	 * whole point of it being here. main() can only inspect cfg.cardserver
	 * after start_thread_config() has returned, and it has no way to know
	 * whether this thread has finished parsing -- so it sleeps 100 ms and
	 * hopes. This thread sleeps 100 ms too, one line below. Whichever wins
	 * decides the outcome, and this was measured: a loop of 30 runs with an
	 * empty config gave exit 1 twenty-nine times and SIGSEGV once.
	 *
	 * The crash was not in the race itself but downstream of it:
	 * check_config() calls get_cache_caids(), which dereferenced
	 * cfg->cardserver without testing it (fixed in config.c). Doing the
	 * check at the point where the profile list becomes known removes both
	 * the race and the window.
	 */
	if ( !cfg.cardserver ) {
		fprintf( stderr, "\nError: no profile configured.\n"
		                 "  Add at least one profile to %s, for example:\n\n"
		                 "    [ myprofile ]\n"
		                 "    CAID: 1884\n"
		                 "    PORT: 15001\n"
		                 "    USER: user pass\n\n",
		                 config_file );
		exit(1);
	}

	usleep(100000);
	check_config(&cfg);
	cfg_set_id_counters(&cfg);

	/*
	 * TASK 2.10 -- restore the trust lifecycle snapshot exactly once, here:
	 * the config (and with it TRUST-PERSIST / TRUST-PERSIST-FILE) is now
	 * parsed, and no server thread exists yet, so no verdict can be acted
	 * on while the state is being read back. A SIGHUP reread skips this.
	 */
	lifecycle_load();

	int fd = inotify_init(); //1(IN_NONBLOCK);
	while (1) {

		struct filename_data *fs = cfg.files;
		while (fs) {
			if (!fs->nowatch) fs->wd = inotify_add_watch(fd,fs->name, IN_CLOSE_WRITE|IN_IGNORED);
			fs = fs->next;
		}

		struct inotify_event *event;
		char buf[1024];
		int changed = 0;

		do {
			int len = read(fd,buf,1024);
			int i = 0;
			while (i<len) {
				event = (struct inotify_event *) &buf[i];
				struct filename_data *fs = cfg.files;
				while (fs) {
					if (!fs->nowatch)
					if (event->wd==fs->wd) {
						if (event->mask & IN_CLOSE_WRITE) changed = 1;
			            if (event->mask & IN_IGNORED) {
							inotify_rm_watch(fd, fs->wd);
							fs->wd = inotify_add_watch(fd,fs->name, IN_CLOSE_WRITE|IN_IGNORED);
						}
						break;
					}
					fs = fs->next;
				}				
				i += sizeof(struct inotify_event) + event->len;
			}
			usleep(30000);
	    } while (!changed);

		if (changed) {
			debugf(getdbgflag(DBG_CONFIG,0,0)," Config file Changed...\n");
			struct filename_data *fs = cfg.files;
			while (fs) {
				if (!fs->nowatch) inotify_rm_watch(fd, fs->wd);
				fs = fs->next;
			}
			free_filenames( &cfg );
			reread_config(&cfg);
			sleep(1);
			/*
			 * Graceful degradation on a reload that lost the profile section:
			 * say so loudly instead of quietly serving nothing. Editors that
			 * truncate-then-save make this easy to hit by accident.
			 */
			if ( !cfg.cardserver )
				debugf( DBG_ERROR, " !!! CONFIG: reloaded config has no profile section -- the server is serving nothing until it is fixed\n" );
			check_config(&cfg);
			cfg_set_id_counters(&cfg);
		}
	}
	return NULL;
}


int start_thread_config()
{
	create_thread(PACKED_MPTR(&prg, pthread_t, tid_cfg), (threadfn)reread_config_thread,NULL);
	return 0;
}


