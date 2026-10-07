/*
 * cwlog.c -- TASK R11 (D65): the ring behind cwlog.h.
 *
 * The whole module is one fixed array, one index and one mutex. Newest
 * entries overwrite the oldest (drop-oldest, the cache is a live view,
 * not an archive). The snapshot copies under the lock so a page render
 * never walks a row the cache threads are rewriting. Written to ONLY
 * from the stats-flavoured cache verdict sites; compiled into the
 * MCS_DCWSTATS flavours only (see src/Makefile), so the stock and queue
 * binaries stay byte-identical to their R8 releases.
 */
#include <string.h>
#include <pthread.h>

#include "cwlog.h"

static struct cwlog_entry cwlog_ring[CWLOG_CAP];
static int cwlog_head;                      /* next write slot           */
static int cwlog_n;                         /* live rows, <= CWLOG_CAP   */
static pthread_mutex_t cwlog_mtx = PTHREAD_MUTEX_INITIALIZER;

/* Record one verdict. cw may be NULL (16 zero bytes are stored). An
 * out-of-range verdict is dropped rather than stored: the page and the
 * json section name verdicts by index and must never meet "unknown". */
void cwlog_note(int verdict, int reason, int peerid,
		uint32_t caid, uint32_t provid, uint16_t sid,
		const uint8_t *cw, uint32_t tick)
{
	struct cwlog_entry *e;
	if (verdict < CWLOG_DELIVERED || verdict > CWLOG_STORED) return;

	pthread_mutex_lock(&cwlog_mtx);
	e = &cwlog_ring[cwlog_head];
	memset(e, 0, sizeof(*e));
	e->tick    = tick;
	e->caid    = (uint16_t)caid;
	e->provid  = provid;
	e->sid     = sid;
	e->verdict = (uint8_t)verdict;
	e->reason  = (uint8_t)reason;
	e->peerid  = peerid;
	if (cw) memcpy(e->cw, cw, 16);
	cwlog_head = (cwlog_head + 1) % CWLOG_CAP;
	if (cwlog_n < CWLOG_CAP) cwlog_n++;
	pthread_mutex_unlock(&cwlog_mtx);
}

int cwlog_count(void)
{
	int n;
	pthread_mutex_lock(&cwlog_mtx);
	n = cwlog_n;
	pthread_mutex_unlock(&cwlog_mtx);
	return n;
}

int cwlog_capacity(void)
{
	return CWLOG_CAP;
}

/* Snapshot, newest first (out[0] = the most recent verdict). Returns
 * the number of rows written, never more than max. */
int cwlog_snapshot(struct cwlog_entry *out, int max)
{
	int i, n = 0;
	if (!out || max <= 0) return 0;
	pthread_mutex_lock(&cwlog_mtx);
	for (i = 0; i < cwlog_n && n < max; i++) {
		int idx = cwlog_head - 1 - i;
		if (idx < 0) idx += CWLOG_CAP;
		out[n++] = cwlog_ring[idx];
	}
	pthread_mutex_unlock(&cwlog_mtx);
	return n;
}

/* Forget everything (used by tests; the server never calls it -- the
 * ring is a live window, not a resettable counter). */
void cwlog_reset(void)
{
	pthread_mutex_lock(&cwlog_mtx);
	cwlog_head = 0;
	cwlog_n = 0;
	memset(cwlog_ring, 0, sizeof(cwlog_ring));
	pthread_mutex_unlock(&cwlog_mtx);
}
