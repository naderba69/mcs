/*
 * MultiCS r82a -- src/chanhealth.h
 *
 * TASK R15 (D70) -- channel health: the per-channel (caid, provid, sid)
 * delivery/refusal/hold rates, and the "dying of refusals" alert.
 *
 * The viewing-killer this closes: a channel's key supply turns to noise --
 * every offer is eaten by a gate, nobody delivers, and the first sign is a
 * viewer complaining about a black screen. By then the operators' readouts
 * show only anonymous verdicts somewhere in a ring. This layer keeps three
 * rates PER CHANNEL and, when a channel racks up CHANHEALTH_STREAK
 * consecutive refusals with no delivery in between, says its name once:
 *
 *      =(chanhealth)= ch 0100:000000:0065 DYING OF REFUSALS: streak 5,
 *                      delivered 0 refused 5 held 0
 *
 * so the dead channel is named in the log and on the page BEFORE a viewer
 * complains.
 *
 * Counting doctrine (the cwcm/cwentropy precedent, not the cwlate one):
 * the counters are unconditional -- every build, every flavour, no config
 * key (the roadmap line is explicit: "no setting key; the counters are the
 * surface"). There is nothing to arm and nothing to turn off. The ring
 * (cwlog) and its pages keep their own MCS_DCWSTATS build switch; these
 * numbers do not.
 *
 * What the three rates mean, precisely:
 *
 *   delivered -- a client ECM was satisfied and the key went out
 *                (setdcw.c, immediately before clients_check_sendcw),
 *                from ANY source: cache, card server, relay. This is the
 *                viewer-facing number; a seed stored with no waiter is
 *                not a delivery, and a key piped to a waiter is counted
 *                once, here, when it is actually sent -- not twice at the
 *                pipe and again at the commit.
 *   refused   -- a gate ate the key (the cwlog_refuse feed: the cache
 *                ingest gates, the trust-relay brake, the late-truth
 *                correction). Supply-side noise.
 *   held      -- the consensus parked the key pending a second voter.
 *
 * `streak` is consecutive REFUSED verdicts since the last delivery. Holds
 * do not touch it (a hold is neither noise nor a picture) and do not reset
 * it. The alert fires once per episode: the latch re-arms only on the next
 * delivery, so one dying channel prints one line, not one per push (GR9).
 *
 * The table is 24 channels, LRU-evicted. Deliberately NOT shared-state
 * free: note() is read-modify-write, so the caller serialises access
 * (main.c's chanhealth_lock) exactly as it serialises the other shared
 * evidence tables. The pure layer here never locks -- like ptguard.h, it
 * is a data structure and its rules, nothing else.
 */
#ifndef MCS_CHANHEALTH_H
#define MCS_CHANHEALTH_H

#include <stdint.h>
#include <string.h>

#include "cwlog.h"	/* the verdict codes: DELIVERED / REFUSED / HELD --
			 * the same vocabulary the verdict ring rows carry */

#define CHANHEALTH_SLOTS	24
#define CHANHEALTH_STREAK	5

struct chanhealth_row {
	uint32_t caid;
	uint32_t provid;
	uint16_t sid;
	uint32_t delivered;	/* client commits, any source            */
	uint32_t refused;	/* every refuse verdict on this channel  */
	uint32_t held;		/* consensus holds                       */
	uint32_t streak;	/* consecutive refusals since last delivery */
	uint32_t last;		/* tick of the last note (the LRU key)   */
	int      alerted;	/* the line already fired for this episode */
	int      used;
};

struct chanhealth_tab {
	struct chanhealth_row row[CHANHEALTH_SLOTS];
	uint32_t notes;		/* counted verdicts seen           */
	uint32_t evicts;	/* LRU rows reused                 */
	uint32_t alerts;	/* episodes that fired a line      */
};

/* The live alert state of one row: the streak is the window. */
#define CHANHEALTH_DYING(r)	((r)->streak >= (uint32_t)CHANHEALTH_STREAK)

__attribute__((unused))
static struct chanhealth_row *chanhealth_lookup(struct chanhealth_tab *t,
					uint32_t caid, uint32_t provid,
					uint16_t sid, uint32_t now)
{
	int i, free_i = -1, victim = -1;
	struct chanhealth_row *r;

	for (i = 0; i < CHANHEALTH_SLOTS; i++) {
		r = &t->row[i];
		if (r->used && r->caid == caid && r->provid == provid &&
		    r->sid == sid) {
			r->last = now;
			return r;
		}
		if (!r->used && free_i < 0) free_i = i;
		/* the oldest USED row is the eviction victim -- a slot that
		 * was never claimed can never be the oldest (the ptguard trap:
		 * an uninitialised victim of 0 hid behind row 0 being empty) */
		if (r->used && (victim < 0 ||
		    (int32_t)(r->last - t->row[victim].last) < 0))
			victim = i;
	}
	if (free_i < 0) {
		free_i = victim;
		memset(&t->row[free_i], 0, sizeof(t->row[free_i]));
		t->evicts++;
	}
	r = &t->row[free_i];
	r->caid = caid;
	r->provid = provid;
	r->sid = sid;
	r->last = now;
	r->used = 1;
	return r;
}

/*
 * One verdict, one counter. Returns 1 exactly when the dying-of-refusals
 * alert FIRES on this call (the caller prints the line); a latched episode
 * or any later refusal returns 0. STORED and DISPUTED verdicts are not
 * rates this layer keeps -- they are neither a picture nor a refusal --
 * and cost nothing at all (no lookup, no row claimed).
 */
__attribute__((unused))
static int chanhealth_note(struct chanhealth_tab *t, int verdict,
			   uint32_t caid, uint32_t provid, uint16_t sid,
			   uint32_t now)
{
	struct chanhealth_row *r;
	int fired = 0;

	if (verdict != CWLOG_DELIVERED && verdict != CWLOG_REFUSED &&
	    verdict != CWLOG_HELD)
		return 0;

	t->notes++;
	r = chanhealth_lookup(t, caid, provid, sid, now);
	if (verdict == CWLOG_DELIVERED) {
		r->delivered++;
		r->streak = 0;
		r->alerted = 0;		/* the episode is over: re-arm */
	}
	else if (verdict == CWLOG_REFUSED) {
		r->refused++;
		r->streak++;
		if (CHANHEALTH_DYING(r) && !r->alerted) {
			r->alerted = 1;
			t->alerts++;
			fired = 1;
		}
	}
	else {	/* CWLOG_HELD */
		r->held++;
	}
	return fired;
}

/* Copy of the live rows (table order), bounded by max. */
__attribute__((unused))
static int chanhealth_copy(const struct chanhealth_tab *t,
			   struct chanhealth_row *out, int max)
{
	int i, n = 0;

	for (i = 0; i < CHANHEALTH_SLOTS && n < max; i++)
		if (t->row[i].used) out[n++] = t->row[i];
	return n;
}

/*
 * The live feed and readout, implemented in main.c next to the other
 * evidence wrappers (the table is a main.c global; the wrapper holds the
 * lock and owns the alert line). chan_note() returns 1 when the alert
 * fired on this call, like the pure layer above.
 */
int chan_note(int verdict, uint32_t caid, uint32_t provid, uint16_t sid);
int chanhealth_snapshot(struct chanhealth_row *out, int max);

#endif /* MCS_CHANHEALTH_H */
