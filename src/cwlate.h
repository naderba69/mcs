/*
 * cwlate.h -- TASK R13 (D68), the late-truth loop.
 *
 * THE HOLE THIS CLOSES
 *
 * The agreement ledger (TASK 2.1) already sees the fatal fact: a cache key
 * was delivered to a client, and a card server later answered the SAME ecmd5
 * with a DIFFERENT key. Identical ECM bytes mean one key ladder and one true
 * control word -- so at least one of the two sources is objectively wrong.
 * That is the "fake wins the race" kill shot: the client holds the fake, the
 * screen is black, and the ledger entry records the disagreement...
 * and then says nothing. ledger_note_delivery() counts LEDGER_DISPUTE and
 * moves on; the log line, the conviction and the purge all wait for the
 * THIRD fact -- the client comes back to re-ask (LEDGER_PROOF).
 *
 * But a black screen that kills the viewing kills the witness with it. The
 * viewer zaps away, no retry ever arrives, no proof ever forms, nothing is
 * ever said -- and the same stored fake is served to victims 2..N from the
 * cache for as long as the entry lives (a re-push or a relaying peer mints a
 * fresh node; the DCW_SENT flag only hides the one node that already spoke).
 *
 * WHAT THIS FILE DOES -- and deliberately does not
 *
 *   1. KEY MEMORY (cwlate_key_*): a bounded table of cache keys a LOCAL CARD
 *      SERVER has contradicted on the same ecmd5 within the last
 *      CWLATE_KEY_TTL ms, keyed like D26/cwneg on (key digest, CAID, PROVID)
 *      -- the poison does not change when the SID does. Gate question:
 *      "were these exact 16 bytes contradicted by our own card pipeline for
 *      this mux moments ago?" A hit is a DEFERRAL-shaped refusal at the two
 *      cache offer/serve sites (the cwneg gate shape): the key is not
 *      stored, not counted as agreement, not handed to any client -- the
 *      request falls through to the card servers, which just proved they
 *      have a different answer. No source is silenced; a different key from
 *      the same peer flows through untouched.
 *
 *   2. PATTERN (cwlate_pat_*): per (source, CAID) windowed count of
 *      class-asymmetric disputes where THIS source was the cache side. At
 *      CWLATE_PAT_MIN disputes inside CWLATE_PAT_WIN ms the caller fires
 *      exactly ONE soft trust event (TRUST_EV_SOFT) and one line -- the
 *      mirror detector (TASK 2.7) shape: a pattern is inference, and a soft
 *      event is the only sentence inference may carry (GR3).
 *
 *   3. CLASS RULE (cwlate_suspect): the pattern and the key memory fire ONLY
 *      for card-server-vs-cache disputes, in either arrival order. The card
 *      server is the operator's own pipeline; the project's charter names
 *      the cache the weakest link. Cache-vs-cache and server-vs-server
 *      disputes are real and visible (the caller counts and names them) but
 *      score nobody: two cache peers quarrelling says which one is lying
 *      only about as often as it says the third one is, and two servers
 *      disagreeing is the rotation layer's story, not this one's (GR1).
 *
 * WHAT THIS FILE DELIBERATELY DOES NOT DO
 *
 *   It does not convict. A dispute is not a proof (GR3); the PROOF path in
 *   the ledger is untouched and keeps its own sentences. It does not mark a
 *   SID-scoped identity (D26). It does not expire a mark early because the
 *   source behaved since -- the TTL is about the KEY (CWs rotate every
 *   ~10 s; a contradicted key has no meaning past its period), not about the
 *   source's reputation. It does not touch threading or the cache layout.
 *
 * COST (GR9)
 *
 * Two fixed .bss tables (~5 KB), no allocation, no I/O. The tables are
 * touched from ECM threads (the ledger hook) and the cache thread (the two
 * gates) under one leaf mutex (cwlate_lock in main.c), never held across a
 * log write. Every function is a pure function of its arguments and the
 * table, so the unit suite runs it with no server and no clock.
 */
#ifndef MCS_CWLATE_H
#define MCS_CWLATE_H

#include <stdint.h>
#include <string.h>
#include <stdio.h>

/*
 * Source classes. Normally provided by ecmdata.h (the caller's world); the
 * fallback keeps this header a standalone include for the unit suite, exactly
 * like ledger.h's local ecmd5 check. Identical values, so an ecmdata.h
 * inclusion before or after this file is silent either way.
 */
#ifndef DCW_SOURCE_CACHE
#define DCW_SOURCE_CACHE  1
#define DCW_SOURCE_SERVER 2
#endif

/* Contradicted keys remembered at once. Same trade as cwneg: a full table
 * evicts the least recently touched, because refusing the NEXT contradicted
 * key is exactly the wrong one to forget. */
#define CWLATE_SLOTS     64

/* How long a contradicted key stays hot. Two crypto periods plus margin --
 * the same "is this the key in use right now?" question the ledger's 20 s
 * answers, a little wider because a slow card server can answer a full
 * period late and the mark must still catch the re-push. */
#define CWLATE_KEY_TTL   30000u

/* One refusal/deferral line per key per this window (the cwneg shape). */
#define CWLATE_WINDOW    60000u

/* Pattern: N class-asymmetric disputes for ONE (source, CAID) inside the
 * window = one soft event. Deliberately no delivery-rate denominator: each
 * tick is already an objectively impossible pair (same ecmd5, two keys), so
 * three of them in a minute is far stronger than any shape-only pattern this
 * project already scores (2.4, 2.7), and a rate needs a delivery hook in
 * every flavour for a signal this loud. */
#define CWLATE_PAT_MIN   3
#define CWLATE_PAT_WIN   60000u

/* The suspect class rule is cwlate_suspect() below; no constants needed. */

struct cwlate_slot {
	int      used;
	uint64_t kdigest;        /* FNV-1a over the 16 key bytes            */
	uint8_t  key8[8];        /* first half, for the evidence line       */
	uint16_t caid;
	uint32_t provid;
	uint8_t  origin_type;    /* who contradicted it (the server side)   */
	uint32_t origin_id;
	uint32_t mark_ticks;
	uint32_t last_ticks;     /* mark or hit, for LRU                    */
	uint32_t hits;           /* offers/serve attempts stopped since     */
	uint32_t rep_ticks;      /* last line written for this key          */
};

struct cwlate_keytab {
	struct cwlate_slot s[CWLATE_SLOTS];
};

struct cwlate_patslot {
	int      used;
	int      srctype;
	uint32_t srcid;
	uint16_t caid;
	uint32_t win_start;
	int      count;
	uint8_t  fired;          /* the soft event already spent this window */
	uint32_t total;          /* disputes ever, for the line              */
};

struct cwlate_pattab {
	struct cwlate_patslot s[CWLATE_SLOTS];
};

/* Filled on every hit, so the caller writes the line without re-locking. */
struct cwlate_ev {
	unsigned fired;          /* 1 when this hit earned the line         */
	uint64_t kdigest;
	uint8_t  key8[8];
	uint16_t caid;
	uint32_t provid;
	uint32_t age_ms;
	uint32_t hits;
};

__attribute__((unused))
static void cwlate_init(struct cwlate_keytab *k, struct cwlate_pattab *p)
{
	if (k) memset(k, 0, sizeof(*k));
	if (p) memset(p, 0, sizeof(*p));
}

/*
 * FNV-1a, 64-bit, over exactly the 16 key bytes -- the same fingerprint the
 * negative memory (2.6) uses, for the same reason: pure arithmetic, no
 * alignment assumption (the server is built with -fpack-struct), safe in any
 * thread and in the tests (GR9).
 */
__attribute__((unused))
static uint64_t cwlate_digest(const uint8_t *cw)
{
	uint64_t h = 1469598103934665603ULL;
	int i;

	for (i = 0; i < 16; i++) {
		h ^= (uint64_t)cw[i];
		h *= 1099511628211ULL;
	}
	return h;
}

/*
 * Which slot does this dispute point at? Exactly one shape says anything:
 * one side a card server, the other a cache source. Returns the INDEX (0 or
 * 1) of the cache side -- the suspect -- or -1 for every other pairing
 * (cache-vs-cache, server-vs-server, anything involving a client push).
 *
 * Arrival order is irrelevant: server-then-cache (the late re-push after our
 * pipeline answered) and cache-then-server (the late server answer after the
 * race was won) are the same accusation.
 */
#define CWLATE_NOSUSPECT (-1)

__attribute__((unused))
static int cwlate_suspect(int type0, int type1)
{
	int srv = 0, cch = 0;

	if (type0 == DCW_SOURCE_SERVER) srv++;
	else if (type0 == DCW_SOURCE_CACHE) cch++;
	if (type1 == DCW_SOURCE_SERVER) srv++;
	else if (type1 == DCW_SOURCE_CACHE) cch++;
	if (srv != 1 || cch != 1) return CWLATE_NOSUSPECT;
	return (type0 == DCW_SOURCE_CACHE) ? 0 : 1;
}

/*
 * File a contradicted key. Returns 1 on a FRESH mark (the caller may
 * announce it once), 0 when the key was already hot (the touch is recorded;
 * the first contradiction stays the evidence). A mark past its TTL is dead
 * and a re-mark revives it as fresh -- a new contradiction of the same bytes
 * is a new story.
 */
__attribute__((unused))
static int cwlate_key_mark(struct cwlate_keytab *t, const uint8_t *cw,
                           uint16_t caid, uint32_t provid,
                           uint8_t origin_type, uint32_t origin_id,
                           uint32_t now)
{
	struct cwlate_slot *victim = NULL;
	uint64_t d;
	uint32_t oldest = 0;
	int i, freepos = -1;

	if (!t || !cw) return 0;
	d = cwlate_digest(cw);

	for (i = 0; i < CWLATE_SLOTS; i++) {
		struct cwlate_slot *s = &t->s[i];
		uint32_t age;

		if (!s->used) {
			if (freepos < 0) freepos = i;
			continue;
		}
		if (s->kdigest == d && s->caid == caid && s->provid == provid) {
			if ((uint32_t)(now - s->mark_ticks) >= CWLATE_KEY_TTL) {
				/* expired: a re-contradiction files a fresh mark */
				s->mark_ticks = now;
				s->last_ticks = now;
				s->origin_type = origin_type;
				s->origin_id = origin_id;
				s->hits = 0;
				s->rep_ticks = 0;
				return 1;
			}
			s->last_ticks = now;
			return 0;
		}
		age = (uint32_t)(now - s->last_ticks);
		if (age >= oldest) { oldest = age; victim = s; }
	}

	if (freepos < 0) {
		if (!victim) return 0;
		freepos = (int)(victim - t->s);
	}

	{
		struct cwlate_slot *s = &t->s[freepos];

		memset(s, 0, sizeof(*s));
		s->used = 1;
		s->kdigest = d;
		memcpy(s->key8, cw, 8);
		s->caid = caid;
		s->provid = provid;
		s->origin_type = origin_type;
		s->origin_id = origin_id;
		s->mark_ticks = now;
		s->last_ticks = now;
	}
	return 1;
}

/*
 * Gate question: were these exact bytes contradicted for this mux within the
 * TTL? A hit updates LRU and the hit counter and (optionally) fills `ev`.
 * `fired` in the ev is 1 when this hit is the first to earn a line inside
 * CWLATE_WINDOW -- repeats are counted and kept quiet, the cwneg rule.
 */
__attribute__((unused))
static int cwlate_key_hit(struct cwlate_keytab *t, const uint8_t *cw,
                          uint16_t caid, uint32_t provid,
                          uint32_t now, struct cwlate_ev *ev)
{
	uint64_t d;
	int i;

	if (!t || !cw) return 0;
	d = cwlate_digest(cw);

	for (i = 0; i < CWLATE_SLOTS; i++) {
		struct cwlate_slot *s = &t->s[i];

		if (!s->used) continue;
		if (s->kdigest != d || s->caid != caid || s->provid != provid)
			continue;
		/* TTL runs from the MARK, not from the last touch: a gate hit
		 * must not extend the key's meaning past its crypto period. */
		if ((uint32_t)(now - s->mark_ticks) >= CWLATE_KEY_TTL) return 0;

		s->last_ticks = now;
		s->hits++;
		if (ev) {
			memset(ev, 0, sizeof(*ev));
			ev->kdigest = d;
			memcpy(ev->key8, s->key8, 8);
			ev->caid = s->caid;
			ev->provid = s->provid;
			ev->age_ms = (uint32_t)(now - s->mark_ticks);
			ev->hits = s->hits;
			if (!s->rep_ticks || (uint32_t)(now - s->rep_ticks) >= CWLATE_WINDOW) {
				s->rep_ticks = now;
				ev->fired = 1;
			}
		}
		return 1;
	}
	return 0;
}

__attribute__((unused))
static int cwlate_key_count(const struct cwlate_keytab *t)
{
	int i, n = 0;

	if (!t) return 0;
	for (i = 0; i < CWLATE_SLOTS; i++) if (t->s[i].used) n++;
	return n;
}

/*
 * One dispute tick for (source, CAID). Returns 1 exactly when the window's
 * CWLATE_PAT_MIN-th tick lands and the window's soft event has not been
 * spent yet -- the caller fires ONE soft event and one line, then the latch
 * keeps the rest of the window quiet. An expired window restarts at 1.
 */
__attribute__((unused))
static int cwlate_pat_tick(struct cwlate_pattab *t,
                           int srctype, uint32_t srcid, uint16_t caid,
                           uint32_t now)
{
	struct cwlate_patslot *victim = NULL;
	uint32_t oldest = 0;
	int i, freepos = -1;

	if (!t) return 0;

	for (i = 0; i < CWLATE_SLOTS; i++) {
		struct cwlate_patslot *s = &t->s[i];

		if (!s->used) {
			if (freepos < 0) freepos = i;
			continue;
		}
		if (s->srctype == srctype && s->srcid == srcid && s->caid == caid) {
			if ((uint32_t)(now - s->win_start) >= CWLATE_PAT_WIN) {
				s->win_start = now;
				s->count = 1;
				s->fired = 0;
				s->total++;
				return 0;
			}
			s->count++;
			s->total++;
			if (s->count >= CWLATE_PAT_MIN && !s->fired) {
				s->fired = 1;
				return 1;
			}
			return 0;
		}
		if ((uint32_t)(now - s->win_start) >= oldest) {
			oldest = (uint32_t)(now - s->win_start);
			victim = s;
		}
	}

	if (freepos < 0) {
		if (!victim) return 0;
		freepos = (int)(victim - t->s);
	}

	{
		struct cwlate_patslot *s = &t->s[freepos];

		memset(s, 0, sizeof(*s));
		s->used = 1;
		s->srctype = srctype;
		s->srcid = srcid;
		s->caid = caid;
		s->win_start = now;
		s->count = 1;
		s->total = 1;
	}
	return 0;
}

#endif /* MCS_CWLATE_H */
