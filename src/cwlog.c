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
#include <time.h>

#include "cwlog.h"
#include "chanhealth.h"	/* TASK R15 (D70): per-channel rates, fed here */

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
	if (verdict < CWLOG_DELIVERED || verdict > CWLOG_DISPUTED) return;

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
	/* TASK R47 (D110): the per-channel counters see every channel verdict. */
	if (verdict == CWLOG_DELIVERED || verdict == CWLOG_REFUSED ||
	    verdict == CWLOG_HELD)
		chan_note(verdict, reason, caid, provid, sid);
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

/*
 * TASK R47 (D110): the per-channel table (chanhealth.h) sits beside the
 * ring behind its own mutex. Every DELIVERED / REFUSED / HELD verdict with
 * a channel is counted here: cwlog_note() feeds it for the cache verdicts,
 * and the card path calls chan_note() for its deliveries. Deliveries get
 * no ring row of their own: a ring row per delivery would push the refusal
 * verdicts out of a 100-row window in seconds.
 */
static struct chanhealth_tab cwlog_chan;
static uint32_t cwlog_chan_clock;           /* LRU clock, not wall time  */
static pthread_mutex_t cwlog_chan_mtx = PTHREAD_MUTEX_INITIALIZER;

/*
 * The alert line is defined in clustredcache.c, where debugf is visible.
 * Weak, because test_cwlog links this file alone and has no debugf.
 */
extern void cwlog_chan_alert(const struct chanhealth_row *r)
	__attribute__((weak));

/*
 * TASK R48 (D111): the detail scope. Guarded by cwlog_chan_mtx. The flag is
 * read without the lock on the hot path, so an empty scope costs one read.
 * The scope is config-free on purpose: it is set from telnet at run time and
 * survives `dcwstats reset` (the counters reset, the scope does not).
 */
static struct diag_scope cwlog_scope;
static volatile int cwlog_scope_on;        /* 1 when the scope is not empty */
static time_t cwlog_chan_reset_at;         /* 0: never reset since start    */

static void diagscope_flag(void)
{
	cwlog_scope_on = !diag_scope_empty(&cwlog_scope);
}

/*
 * One verdict into the channel table. Prints the dying-of-refusals line
 * through cwlog_chan_alert exactly once per episode (chanhealth.h). The
 * refusal is also counted by its ring reason (always on: a plain counter).
 */
void chan_note(int verdict, int reason, uint32_t caid, uint32_t provid, uint16_t sid)
{
	struct chanhealth_row fired = {0};
	int f;

	pthread_mutex_lock(&cwlog_chan_mtx);
	f = chanhealth_note(&cwlog_chan, verdict, caid, provid, sid,
			    ++cwlog_chan_clock);
	if (verdict == CWLOG_REFUSED)
		chanhealth_add_reason(&cwlog_chan, caid, provid, sid, reason,
				      cwlog_chan_clock);
	if (f)
		fired = *chanhealth_lookup(&cwlog_chan, caid, provid, sid,
					   cwlog_chan_clock);
	pthread_mutex_unlock(&cwlog_chan_mtx);
	if (f && cwlog_chan_alert)
		cwlog_chan_alert(&fired);
}

/* `telnet> dcwstats reset` clears the channel table with the counters, so
 * the /diag totals start over together with them. The ring is a live window
 * and is not cleared (its page says so). The detail scope is kept. */
void chanhealth_reset(void)
{
	pthread_mutex_lock(&cwlog_chan_mtx);
	memset(&cwlog_chan, 0, sizeof(cwlog_chan));
	cwlog_chan_reset_at = time(NULL);
	pthread_mutex_unlock(&cwlog_chan_mtx);
}

time_t chanhealth_reset_time(void)
{
	time_t t;

	pthread_mutex_lock(&cwlog_chan_mtx);
	t = cwlog_chan_reset_at;
	pthread_mutex_unlock(&cwlog_chan_mtx);
	return t;
}

/* The detail feeds. The card's answer time, and an ECM that ended with no
 * answer. Each is a no-op unless the request is in the scope. */
void diag_note_latency(uint32_t caid, uint32_t provid, uint16_t sid,
		       const char *profile, uint32_t ms)
{
	if (!cwlog_scope_on) return;
	pthread_mutex_lock(&cwlog_chan_mtx);
	if (diag_scope_match(&cwlog_scope, caid, provid, sid, profile))
		chanhealth_add_latency(&cwlog_chan, caid, provid, sid, ms,
				       ++cwlog_chan_clock);
	pthread_mutex_unlock(&cwlog_chan_mtx);
}

void diag_note_noanswer(uint32_t caid, uint32_t provid, uint16_t sid,
			const char *profile)
{
	if (!cwlog_scope_on) return;
	pthread_mutex_lock(&cwlog_chan_mtx);
	if (diag_scope_match(&cwlog_scope, caid, provid, sid, profile))
		chanhealth_add_noanswer(&cwlog_chan, caid, provid, sid,
					++cwlog_chan_clock);
	pthread_mutex_unlock(&cwlog_chan_mtx);
}

/* The scope edits, for telnet. Return values are those of diagscope.h. */
int diagscope_add_channel(uint32_t caid, uint32_t provid, uint16_t sid)
{
	int r;

	pthread_mutex_lock(&cwlog_chan_mtx);
	r = diag_scope_add_ch(&cwlog_scope, caid, provid, sid);
	diagscope_flag();
	pthread_mutex_unlock(&cwlog_chan_mtx);
	return r;
}

int diagscope_del_channel(uint32_t caid, uint32_t provid, uint16_t sid)
{
	int r;

	pthread_mutex_lock(&cwlog_chan_mtx);
	r = diag_scope_del_ch(&cwlog_scope, caid, provid, sid);
	diagscope_flag();
	pthread_mutex_unlock(&cwlog_chan_mtx);
	return r;
}

int diagscope_add_profile(const char *name)
{
	int r;

	pthread_mutex_lock(&cwlog_chan_mtx);
	r = diag_scope_add_prof(&cwlog_scope, name);
	diagscope_flag();
	pthread_mutex_unlock(&cwlog_chan_mtx);
	return r;
}

int diagscope_del_profile(const char *name)
{
	int r;

	pthread_mutex_lock(&cwlog_chan_mtx);
	r = diag_scope_del_prof(&cwlog_scope, name);
	diagscope_flag();
	pthread_mutex_unlock(&cwlog_chan_mtx);
	return r;
}

void diagscope_clear(void)
{
	pthread_mutex_lock(&cwlog_chan_mtx);
	diag_scope_clear(&cwlog_scope);
	diagscope_flag();
	pthread_mutex_unlock(&cwlog_chan_mtx);
}

void diagscope_snapshot(struct diag_scope *out)
{
	pthread_mutex_lock(&cwlog_chan_mtx);
	*out = cwlog_scope;
	pthread_mutex_unlock(&cwlog_chan_mtx);
}

/* Copy of the live channel rows (table order), bounded by max. */
int chanhealth_snapshot(struct chanhealth_row *out, int max)
{
	int n;

	pthread_mutex_lock(&cwlog_chan_mtx);
	n = chanhealth_copy(&cwlog_chan, out, max);
	pthread_mutex_unlock(&cwlog_chan_mtx);
	return n;
}
