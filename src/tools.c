#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef WIN32

#include <windows.h>
//#include <sys/types.h>

#else

#include <fcntl.h>
#include <sys/time.h>
#include <time.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <errno.h>

#endif

#include "tools.h"

struct timeval startime;

#ifndef WIN32
static struct timespec monotonic_startime;
static pthread_once_t monotonic_start_once = PTHREAD_ONCE_INIT;
static int monotonic_start_error;

static void init_monotonic_startime(void)
{
    if (clock_gettime(CLOCK_MONOTONIC, &monotonic_startime) != 0)
        monotonic_start_error = errno ? errno : EIO;
}

int tools_init_tick_clock(void)
{
    int rc = pthread_once(&monotonic_start_once, init_monotonic_startime);
    if (rc != 0) return rc;
    return monotonic_start_error ? -1 : 0;
}

static int monotonic_elapsed_us(uint64_t *elapsed)
{
    struct timespec now;
    int64_t seconds, nanoseconds;

    if (!elapsed || tools_init_tick_clock() != 0) return -1;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return -1;

    seconds = (int64_t)now.tv_sec - (int64_t)monotonic_startime.tv_sec;
    nanoseconds = (int64_t)now.tv_nsec - (int64_t)monotonic_startime.tv_nsec;
    if (nanoseconds < 0) {
        seconds--;
        nanoseconds += 1000000000LL;
    }
    if (seconds < 0) return -1;
    *elapsed = (uint64_t)seconds * 1000000u + (uint64_t)nanoseconds / 1000u;
    return 0;
}
#endif

#ifdef WIN32

void usleep( int count )
{
  uint32_t start = (uint32_t)GetTickCount();
  uint32_t delay = count > 0 ? (uint32_t)count / 1000u : 0u;
  while ((uint32_t)((uint32_t)GetTickCount() - start) < delay) ;
}

#else

uint32_t GetTickCount(void)
{
    uint64_t elapsed_us;
    if (monotonic_elapsed_us(&elapsed_us) != 0) return 0;
    // Preserve the Windows-compatible 32-bit millisecond tick domain.
    return (uint32_t)(elapsed_us / 1000u);
}

unsigned int GetuTickCount()
{
    uint64_t elapsed_us;
    if (monotonic_elapsed_us(&elapsed_us) != 0) return 0;
    // The public return type is 32-bit on supported targets; wrap is intentional.
    return (unsigned int)elapsed_us;
}

unsigned int GetTicks(struct timeval *tv)
{
    uint64_t elapsed_us;
    (void)tv; /* Retain the legacy signature; all POSIX ticks share one clock. */
    if (monotonic_elapsed_us(&elapsed_us) != 0) return 0;
    return (unsigned int)(elapsed_us / 1000u);
}

#endif

unsigned int getseconds()
{
#ifndef WIN32
    uint64_t elapsed_us;
    if (monotonic_elapsed_us(&elapsed_us) != 0) return 0;
    return (unsigned int)(elapsed_us / 1000000u);
#else
    return (unsigned int)(GetTickCount() / 1000u);
#endif
}


void tabavg_init(struct table_average *t)
{
	memset(t, 0, sizeof(struct table_average) );
}

void tabavg_add(struct table_average *t, uint32_t value)
{
	t->tab[t->itab] = value;
	t->itab++; if (t->itab>=100) t->itab =0;
}

uint32_t tabavg_get(struct table_average *t)
{
	uint32_t min = t->tab[0];
	int i;
	for (i=1; i<100; i++) if (min>t->tab[i]) min = t->tab[i];
	if (min==0) return 0xffffffff;

	int sum = 0;
	for (i=1; i<100; i++) sum += (t->tab[i]-min);
	return (sum/100);
}


