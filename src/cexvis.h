/*
 * MultiCS r82a -- src/cexvis.h
 *
 * TASK R20 (D75) -- cacheex exchange visibility: the per-peer five
 * counters and the NAMED drop.
 *
 * The operator sees the cache-exchange mesh only as anonymous verdicts
 * somewhere in a ring: how many pushes a partner sent us, how many we
 * re-pushed toward it, and -- the question that matters when a mesh goes
 * quiet -- WHICH push was dropped, by WHICH rule, on WHICH channel. This
 * layer keeps five counters PER EXCHANGE PEER and the last named event:
 *
 *      push   -- a cacheex push arrived from this peer (the readers)
 *      relay  -- a re-push was sent toward this peer (cacheex_push)
 *      drop   -- a push was refused; lastreason says which rule named it
 *      filter -- pushfilter (the 0x3C table) suppressed a push toward
 *                this peer (or, inbound, the peer's own table message)
 *      loop   -- nodeloop suppressed a push (their push carried our id
 *                in its history, or the send target sits in the history)
 *
 * THE NAMED DROP. Every drop note carries the rule's NAME -- the same
 * vocabulary the dcwstats/cwlog reasons use ("cacheex-fake-cw",
 * "nodeloop", "rc-filter", "no-profile", ...). The row keeps the last
 * event (kind + name + channel + tick), so all three surfaces (/cwlog
 * page, /json, telnet) can print "peer X: drop 3, last: cacheex-fake-cw
 * on 1884:000000:0064" without any extra state.
 *
 * Counting doctrine (the R15/chanhealth precedent, explicitly not the
 * cwlate one): the counters are unconditional -- every build, every
 * flavour, no config key ("the counters are the surface"). The cwlog
 * ring keeps its own MCS_DCWSTATS switch; these numbers do not. The
 * identity is (origin flag, peer id) -- the same srcid word the CW-origin
 * model carries (peer_origin.h), so a cccam client and an upstream server
 * with the same numeric id are never merged.
 *
 * The table is 24 rows, LRU-evicted. Not shared-state free: note() is
 * read-modify-write, so the caller serialises (main.c's cexvis_lock),
 * exactly like chanhealth. The pure layer here never locks.
 */
#ifndef MCS_CEXVIS_H
#define MCS_CEXVIS_H

#include <stdint.h>
#include <string.h>

#include "peer_origin.h"	/* the origin flags in the row key */

#define CEXVIS_SLOTS	24
#define CEXVIS_NAMELEN	32
#define CEXVIS_REASONLEN 28

/* The five named counters, in the order the surfaces print them. */
#define CEXVIS_PUSH	0
#define CEXVIS_RELAY	1
#define CEXVIS_DROP	2
#define CEXVIS_FILTER	3
#define CEXVIS_LOOP	4
#define CEXVIS_COUNT	5

struct cexvis_row {
	int	 origin;		/* PEER_* flag (0 = unknown/new)     */
	uint32_t peerid;		/* the bare id (PEER_ID_MASK)        */
	char	 name[CEXVIS_NAMELEN];	/* user/host, set at the first note  */
	uint32_t n[CEXVIS_COUNT];	/* push/relay/drop/filter/loop       */
	uint8_t  lastev;		/* CEXVIS_* of the last event        */
	char	 lastreason[CEXVIS_REASONLEN];	/* the named rule        */
	uint32_t lastcaid;		/* channel of the last event         */
	uint32_t lastprov;
	uint16_t lastsid;
	uint32_t last;			/* tick of the last event (LRU key)  */
	int	 used;
};

struct cexvis_tab {
	struct cexvis_row row[CEXVIS_SLOTS];
	uint32_t notes;		/* events recorded		*/
	uint32_t evicts;	/* LRU rows reused		*/
};

static const char *cexvis_ev_name(int ev)
{
	switch (ev) {
	case CEXVIS_PUSH:   return "push";
	case CEXVIS_RELAY:  return "relay";
	case CEXVIS_DROP:   return "drop";
	case CEXVIS_FILTER: return "filter";
	case CEXVIS_LOOP:   return "loop";
	}
	return "?";
}

/* Origin flag -> short surface label. */
static const char *cexvis_origin_name(int origin)
{
	switch (origin & PEER_ORIGIN_FLAGS) {
	case PEER_CCCAM_CLIENT:    return "cccam-client";
	case PEER_CAMD35_CLIENT:   return "camd35-client";
	case PEER_CS378X_CLIENT:   return "cs378x-client";
	case PEER_CACHEEX_SERVER:  return "cacheex-server";
	case PEER_CSP:             return "csp-peer";
	}
	return "peer";
}

/*
 * Record one event against (origin, peerid). `name` may be NULL (the
 * choke-point feeds -- cwlog_refuse -- carry no display name); an
 * existing row keeps its name, a new row then falls back to the origin
 * label. `reason` is the NAMED rule for drop/filter/loop (may be ""
 * for push/relay). Returns the row (for the caller's own bookkeeping)
 * or NULL on a bad event code.
 */
__attribute__((unused))
static struct cexvis_row *cexvis_note(struct cexvis_tab *t, int origin,
		uint32_t peerid, const char *name, int ev, const char *reason,
		uint32_t caid, uint32_t provid, uint16_t sid, uint32_t now)
{
	int i, free_i = -1, victim = -1;
	struct cexvis_row *r;

	if (ev < 0 || ev >= CEXVIS_COUNT) return NULL;

	for (i = 0; i < CEXVIS_SLOTS; i++) {
		r = &t->row[i];
		if (r->used && r->origin == origin && r->peerid == peerid) {
			r->last = now;
			r->n[ev]++;
			/* the last-NAMED-event block moves as a unit and only
			 * for a named event: a plain push/relay must never
			 * bury the named drop the three surfaces display */
			if (reason && reason[0]) {
				r->lastev = (uint8_t)ev;
				strncpy(r->lastreason, reason, CEXVIS_REASONLEN - 1);
				r->lastreason[CEXVIS_REASONLEN - 1] = 0;
				r->lastcaid = caid;
				r->lastprov = provid;
				r->lastsid = sid;
			}
			if (name && name[0] && !r->name[0]) {
				strncpy(r->name, name, CEXVIS_NAMELEN - 1);
				r->name[CEXVIS_NAMELEN - 1] = 0;
			}
			t->notes++;
			return r;
		}
		if (!r->used && free_i < 0) free_i = i;
		/* the oldest USED row is the eviction victim (the ptguard
		 * trap: an unclaimed slot can never be the oldest) */
		if (r->used && (victim < 0 ||
		    (int32_t)(r->last - t->row[victim].last) < 0))
			victim = i;
	}
	if (free_i < 0) {
		free_i = victim;
		t->evicts++;
	}
	if (free_i < 0) return NULL;	/* table empty and SLOTS 0 -- impossible */
	r = &t->row[free_i];
	memset(r, 0, sizeof(*r));
	r->used = 1;
	r->origin = origin;
	r->peerid = peerid;
	if (name && name[0]) {
		strncpy(r->name, name, CEXVIS_NAMELEN - 1);
		r->name[CEXVIS_NAMELEN - 1] = 0;
	} else {
		strncpy(r->name, cexvis_origin_name(origin), CEXVIS_NAMELEN - 1);
	}
	r->last = now;
	r->n[ev]++;
	if (reason && reason[0]) {
		r->lastev = (uint8_t)ev;
		strncpy(r->lastreason, reason, CEXVIS_REASONLEN - 1);
		r->lastreason[CEXVIS_REASONLEN - 1] = 0;
		r->lastcaid = caid;
		r->lastprov = provid;
		r->lastsid = sid;
	}
	t->notes++;
	return r;
}

/* Copy the used rows (LRU order: most recently touched first). */
__attribute__((unused))
static int cexvis_copy(struct cexvis_tab *t, struct cexvis_row *out, int max)
{
	int i, j, n = 0;

	for (i = 0; i < CEXVIS_SLOTS; i++)
		if (t->row[i].used) n++;
	if (n > max) n = max;
	for (i = 0; i < n; i++) {
		struct cexvis_row *best = NULL;
		for (j = 0; j < CEXVIS_SLOTS; j++) {
			struct cexvis_row *r = &t->row[j];
			int already = 0, k;
			if (!r->used) continue;
			for (k = 0; k < i; k++)
				if (out[k].origin == r->origin &&
				    out[k].peerid == r->peerid) already = 1;
			if (already) continue;
			if (!best || (int32_t)(r->last - best->last) > 0)
				best = r;
		}
		if (!best) break;
		out[i] = *best;
	}
	return i;
}

#endif /* MCS_CEXVIS_H */

/*
 * The locked wrappers live in main.c (the chanhealth.h precedent: the
 * pure layer never locks, the wrapper holds the mutex and owns nothing
 * else -- there is no alert line here, the named drop IS the surface).
 */
struct cexvis_tab;
int cexvis_note_lk(int origin, uint32_t peerid, const char *name, int ev,
		   const char *reason, uint32_t caid, uint32_t provid,
		   uint16_t sid);
int cexvis_snapshot(struct cexvis_row *out, int max);
