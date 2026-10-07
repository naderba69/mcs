/*
 * trustagg.h — TASK 2.8, the cache-peer trust engine (the coarse tier)
 *
 * THE PROBLEM THIS ANSWERS. Every Phase-2 detector and every definitive proof
 * writes into trust_tab (trust.h) at the five-part key
 * (source_type, source_id, CAID, PROVID, SID). That granularity is exactly
 * right for delivery decisions — GR4, and 1.5/1.6/1.7 read it there — but it
 * has a blind spot a poisoner drives straight through: a cache peer poisoning
 * fifty services leaves fifty independent entries, each carrying one or two
 * events, each starting at TRUST_START. Nobody sums a sender's record across
 * its services, so the evidence exists and is never aggregated, the operator
 * cannot see a peer's standing at all, and the one place the server CHOOSES
 * its counterparties — the cache request fan-out, cache_send_request() — asks
 * every peer blindly.
 *
 * THE TWO TIERS, AND WHY THE COARSE ONE IS NOT GR4 VIOLATED. The fine tier
 * stays the authority for anything that touches a delivery; nothing here can
 * reject, delay or alter a CW. The coarse tier is keyed (source_type,
 * source_id, CAID): the habit is the SENDER's — the same peer
 * mirror-building one service mirror-builds the next (D27/D28) — and the
 * CAID is kept on purpose, which is what makes this finer than per-server:
 * GR4 forbids scoring "a server" as one blob, not a per-server-per-CAID view
 * whose only consequence is which peers we ASK. PROVID and SID are
 * deliberately out: a peer that answers for one provider and poisons another
 * must read as one counterparty, not two.
 *
 * THE ARITHMETIC IS TRUST.H'S, UNCHANGED. Same SOFT halving, same flat PROOF
 * step, same slow GOOD climb, same floor (recovery stays possible) and
 * ceiling (no source becomes immune), same TRUST_ACTIONABLE line. One
 * policy sentence, two granularities — the aggregation must never invent its
 * own idea of what is actionable.
 *
 * THE ONE CONSEQUENCE, AND ITS BRAKE. When a (peer, CAID) score falls below
 * the actionable line, the fan-out may skip that peer's share of cache
 * REQUESTS for that CAID — not a disconnect (the standing rule), not a
 * refusal, and pushes/replies are untouched. The brake is a suppression
 * window: for TRUSTAGG_SUPPRESS_MS the peer is not asked, then ONE request
 * goes through anyway, and if the peer answers with keys clients keep,
 * TRUST_EV_GOOD climbs it back above the line exactly as trust.h intends.
 * A suppressed peer that keeps poisoning is re-armed by its very next event;
 * a suppressed peer that went quiet costs the server one request per window.
 *
 * LOCKING. Every mutation runs under trust_lock — the same leaf lock every
 * other Phase-2 writer holds — through the wrappers in main.c. The ask side
 * runs on the cache thread under lockcache, so the order is the established
 * lockcache -> trust_lock that cachepref_gate() already uses. Nothing here
 * takes any other lock, so it cannot join a cycle (GR9: fixed table, .bss,
 * no allocation, no I/O on the hot path — the log sentence is built by the
 * caller after the lock is released).
 */
#ifndef MCS_TRUSTAGG_H
#define MCS_TRUSTAGG_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "trust.h"

#define TRUSTAGG_TABLE_SIZE  128   /* x ~40 bytes = ~5 KB of .bss */
#define TRUSTAGG_WINDOW_MS   60000 /* one line per entry per window          */
#define TRUSTAGG_SUPPRESS_MS 60000 /* how long the fan-out skips the peer    */

struct trustagg_entry {
	uint32_t lastseen;
	uint32_t supp_until;   /* requests skipped until this tick           */
	uint32_t rep_ticks;    /* last line written for this entry           */
	uint16_t caid;
	int      srctype;
	int      srcid;        /* may carry PEER_* flags; part of the identity */
	int      score;
	int      nproof;       /* definitive proofs aggregated               */
	int      nsoft;        /* soft events aggregated                     */
	int      ngood;        /* delivered keys clients kept                */
	uint8_t  used;
};

struct trustagg_table {
	struct trustagg_entry e[TRUSTAGG_TABLE_SIZE];
};

/*
 * What one crossing looked like — enough for the log sentence and nothing
 * more (GR8: the evidence is the counts and the score, all of it printed).
 */
struct trustagg_evidence {
	int      srctype;
	int      srcid;
	uint16_t caid;
	int      score;
	int      nproof;
	int      nsoft;
	int      ngood;
	int      suppress_ms;
};

/*
 * Odd-constant fold over the three-part key, same shape as trust_index():
 * a plain XOR would alias (srcid, caid) pairs that real configurations
 * actually produce.
 */
static inline unsigned trustagg_index(int srctype, int srcid, uint16_t caid)
{
	uint32_t h = (uint32_t)srctype * 2654435761u;
	h ^= (uint32_t)srcid * 40503u;
	h ^= (uint32_t)caid * 2246822519u;
	h ^= h >> 15;
	return (unsigned)(h % TRUSTAGG_TABLE_SIZE);
}

/*
 * Find the entry for a (source, CAID) pair, or NULL. Never inserts.
 */
static inline struct trustagg_entry *trustagg_find(struct trustagg_table *t,
                                                   int srctype, int srcid,
                                                   uint16_t caid)
{
	unsigned i = trustagg_index(srctype, srcid, caid);
	unsigned probe;
	for (probe = 0; probe < TRUSTAGG_TABLE_SIZE; probe++) {
		struct trustagg_entry *e = &t->e[(i + probe) % TRUSTAGG_TABLE_SIZE];
		if (!e->used) return NULL;
		if (e->srctype == srctype && e->srcid == srcid && e->caid == caid)
			return e;
	}
	return NULL;
}

/*
 * Record one event on the coarse tier and report whether this event CROSSED
 * the line: the entry fell below TRUST_ACTIONABLE and no line has been
 * written for it inside the window (and no suppression is still running).
 * The caller writes the sentence and lets the fan-out brake take effect —
 * the crossing is the moment the operator has to hear about, and one line
 * per window per peer is all they should ever need (GR9).
 *
 * Eviction mirrors trust.h exactly: oldest entry loses the slot, never the
 * worst-scored one — forgetting the most-condemned peer first would be the
 * opposite of what this table is for.
 */
static inline int trustagg_note(struct trustagg_table *t, int ev,
                                int srctype, int srcid, uint16_t caid,
                                uint32_t ticks_now,
                                struct trustagg_evidence *out)
{
	struct trustagg_entry *e = trustagg_find(t, srctype, srcid, caid);

	if (!e) {
		unsigned i = trustagg_index(srctype, srcid, caid);
		unsigned probe, oldest = i;
		uint32_t oldest_at = t->e[i].lastseen;

		for (probe = 0; probe < TRUSTAGG_TABLE_SIZE; probe++) {
			unsigned j = (i + probe) % TRUSTAGG_TABLE_SIZE;
			if (!t->e[j].used) { oldest = j; break; }
			if ((int32_t)(t->e[j].lastseen - oldest_at) < 0) {
				oldest = j; oldest_at = t->e[j].lastseen;
			}
		}
		e = &t->e[oldest];
		memset(e, 0, sizeof(*e));
		e->srctype = srctype;
		e->srcid = srcid;
		e->caid = caid;
		e->score = TRUST_START;
		e->used = 1;
	}

	e->lastseen = ticks_now;
	if (ev == TRUST_EV_PROOF) {
		e->score -= TRUST_PROOF_STEP;
		if (e->score < TRUST_FLOOR) e->score = TRUST_FLOOR;
		e->nproof++;
	}
	else if (ev == TRUST_EV_SOFT) {
		e->score -= (e->score - TRUST_FLOOR) >> TRUST_SOFT_SHIFT;
		if (e->score < TRUST_FLOOR) e->score = TRUST_FLOOR;
		e->nsoft++;
	}
	else if (ev == TRUST_EV_GOOD) {
		int step = (TRUST_CEILING - e->score) >> TRUST_GOOD_SHIFT;
		if (step < 1) step = 1;
		e->score += step;
		if (e->score > TRUST_CEILING) e->score = TRUST_CEILING;
		e->ngood++;
	}
	else return 0;   /* TRUST_EV_NONE: ask-only callers never come here */

	/*
	 * The evidence is filled on EVERY event, not only on a crossing: the
	 * counters are true statements about the peer at this tick whether or
	 * not this is the tick the operator hears about. The RETURN value is
	 * the crossing, and only the crossing.
	 */
	if (out) {
		out->srctype = srctype; out->srcid = srcid;
		out->caid = caid;       out->score = e->score;
		out->nproof = e->nproof; out->nsoft = e->nsoft;
		out->ngood = e->ngood;
		out->suppress_ms = TRUSTAGG_SUPPRESS_MS;
	}

	/*
	 * The crossing. A GOOD event can reach this branch only by falling
	 * below the line, which it cannot do — so the brake never arms on a
	 * success. The re-arm condition is deliberate: an entry below the line
	 * whose suppression has EXPIRED crosses again on its very next event,
	 * which is how a still-poisoning peer is caught the moment it speaks;
	 * one inside the window stays quiet.
	 */
	if (e->score < TRUST_ACTIONABLE &&
	    (e->rep_ticks == 0 ||
	     (int32_t)(ticks_now - e->rep_ticks) >= (int32_t)TRUSTAGG_WINDOW_MS) &&
	    (e->supp_until == 0 || (int32_t)(ticks_now - e->supp_until) >= 0)) {
		e->rep_ticks  = ticks_now;
		e->supp_until = ticks_now + TRUSTAGG_SUPPRESS_MS;
		return 1;
	}
	return 0;
}

/*
 * May the fan-out ask this source for this CAID? Unknown sources are always
 * askable — a peer that has done nothing has done nothing wrong (the same
 * rule trust_score() encodes with its -1). A below-the-line entry is
 * askable again the moment its suppression window has run out: that one
 * request is the test that lets GOOD events exist at all. Nothing here
 * writes, so it is safe beside any lock discipline the caller already holds.
 */
static inline int trustagg_ask(struct trustagg_table *t,
                               int srctype, int srcid, uint16_t caid,
                               uint32_t ticks_now)
{
	struct trustagg_entry *e = trustagg_find(t, srctype, srcid, caid);
	if (!e) return 1;
	if (e->score >= TRUST_ACTIONABLE) return 1;
	if (e->supp_until == 0 || (int32_t)(ticks_now - e->supp_until) >= 0)
		return 1;
	return 0;
}

/*
 * Snapshot for the stats line: how many counterparties the coarse tier
 * knows, how many sit below the actionable line right now.
 */
static inline void trustagg_count(struct trustagg_table *t,
                                  int *peers, int *below)
{
	int i, n = 0, b = 0;
	for (i = 0; i < TRUSTAGG_TABLE_SIZE; i++) {
		if (!t->e[i].used) continue;
		n++;
		if (t->e[i].score < TRUST_ACTIONABLE) b++;
	}
	*peers = n;
	*below = b;
}

/*
 * The sentence. Names the source the way every other layer does (T/ID), the
 * CAID the habit was seen on, the arithmetic that condemned it, what the
 * brake does, how long it lasts, and how it ends — an operator must be able
 * to tell a routing pause from a disconnect without reading this header.
 */
static inline int trustagg_format(const struct trustagg_evidence *evd,
                                  char *line, int sz)
{
	if (!evd || !line || sz <= 0) return -1;
	return snprintf(line, sz,
		"PEER TRUST: source %d/%d on caid %04x fell below the trust line: "
		"score %d (%d proof(s), %d soft, %d good). Its share of cache "
		"requests for this caid is skipped for %d seconds, then one request "
		"tests it again -- nothing is disconnected, no client was touched, "
		"and keys that clients keep win the requests back",
		evd->srctype, evd->srcid, evd->caid, evd->score,
		evd->nproof, evd->nsoft, evd->ngood, evd->suppress_ms / 1000);
}

static inline void trustagg_reset(struct trustagg_table *t)
{
	memset(t, 0, sizeof(*t));
}

/*
 * TASK 2.10 -- restore one coarse entry from the persistence file. Same
 * insertion policy as trustagg_note() (free slot, else oldest), state from
 * the caller, age re-based exactly as trust_restore() documents.
 */
__attribute__((unused))
static int trustagg_restore(struct trustagg_table *t, int srctype, int srcid,
                            uint16_t caid, int score, int nproof, int nsoft,
                            int ngood, uint32_t age_ms, uint32_t now)
{
	struct trustagg_entry *e = trustagg_find(t, srctype, srcid, caid);

	if (!e) {
		unsigned i = trustagg_index(srctype, srcid, caid);
		unsigned probe, oldest = i;
		uint32_t oldest_at = t->e[i].lastseen;

		for (probe = 0; probe < TRUSTAGG_TABLE_SIZE; probe++) {
			unsigned j = (i + probe) % TRUSTAGG_TABLE_SIZE;
			if (!t->e[j].used) { oldest = j; break; }
			if ((int32_t)(t->e[j].lastseen - oldest_at) < 0) {
				oldest = j; oldest_at = t->e[j].lastseen;
			}
		}
		e = &t->e[oldest];
		memset(e, 0, sizeof(*e));
		e->srctype = srctype;
		e->srcid = srcid;
		e->caid = caid;
		e->used = 1;
	}

	if (score < TRUST_FLOOR)   score = TRUST_FLOOR;
	if (score > TRUST_CEILING) score = TRUST_CEILING;
	e->score     = score;
	e->nproof    = nproof;
	e->nsoft     = nsoft;
	e->ngood     = ngood;
	e->lastseen  = (now > age_ms) ? (now - age_ms) : 0;
	/* A restored entry arrives with the brake OFF: the fan-out test request
	 * is the mechanism that re-arms or clears it, and it starts on ask. */
	e->supp_until = 0;
	e->rep_ticks  = 0;
	return 1;
}

#endif /* MCS_TRUSTAGG_H */
