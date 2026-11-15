/*
 * CACHEEX NOPUSH AFTER (band 41) -- per-CAID stale-push brake.
 *
 * OSCam's cacheex_nopushafter idea, exact-CAID only: when a CW is
 * committed later than the configured age for its CAID, the CW is
 * still delivered to the waiting client (it may be exactly what
 * they need), but it is NOT pushed onward to CacheEX servers or
 * CSP cache peers -- by then it is too close to the next crypto
 * period to be useful downstream, and pushing it only seeds other
 * caches with a dying key.
 *
 * Opt-in: an empty table (the default) never blocks anything, so
 * stock push behaviour is byte-identical until the operator writes
 *   CACHEEX NOPUSH AFTER: 1884:1200, 09CD:500
 * No family masks and no defaults are invented here: exact CAIDs
 * with operator-chosen ages only.
 */
#ifndef NOPUSHAFTER_H
#define NOPUSHAFTER_H

#include <stdint.h>
#include <pthread.h>
#include "field-status.h"

#define NOPUSH_MAX 32

struct nopush_state {
	pthread_mutex_t lock;
	struct { uint16_t caid; uint32_t ms; } entries[NOPUSH_MAX];
	int n;
	unsigned long checked;
	unsigned long blocked;
};

static struct nopush_state g_nopush = { PTHREAD_MUTEX_INITIALIZER };

static void nopushafter_configure(const uint16_t *caids, const uint32_t *ages, int n)
{
	int i;
	pthread_mutex_lock(&g_nopush.lock);
	g_nopush.n = 0;
	for (i = 0; i < n && i < NOPUSH_MAX; i++) {
		g_nopush.entries[g_nopush.n].caid = caids[i];
		g_nopush.entries[g_nopush.n].ms = ages[i];
		g_nopush.n++;
	}
	pthread_mutex_unlock(&g_nopush.lock);
}

/* 1 = this commit is too old to push for its CAID. */
static int nopushafter_blocked(uint16_t caid, uint32_t ecmtime_ms)
{
	int i, blocked = 0;
	pthread_mutex_lock(&g_nopush.lock);
	if (g_nopush.n) {
		g_nopush.checked++;
		for (i = 0; i < g_nopush.n; i++) {
			if (g_nopush.entries[i].caid == caid &&
			    ecmtime_ms > g_nopush.entries[i].ms) {
				g_nopush.blocked++;
				blocked = 1;
				break;
			}
		}
	}
	pthread_mutex_unlock(&g_nopush.lock);
	return blocked;
}

void nopushafter_snapshot(unsigned long *checked, unsigned long *blocked, int *entries)
{
	pthread_mutex_lock(&g_nopush.lock);
	if (checked) *checked = g_nopush.checked;
	if (blocked) *blocked = g_nopush.blocked;
	if (entries) *entries = g_nopush.n;
	pthread_mutex_unlock(&g_nopush.lock);
}

#endif /* NOPUSHAFTER_H */
