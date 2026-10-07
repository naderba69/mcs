/*
 * cwcycle.h -- TASK 2.5, cache key cycle/parity visibility (OBSERVE ONLY).
 *
 * THE PROBLEM THIS MEASURES
 *
 * A control word can be completely genuine and still not open the picture. The
 * even/odd pair a card produces belongs to one crypto period; hand a client the
 * key of the PREVIOUS period, or the other half of the pair than the one its
 * ECM asks for, and the receiver cannot decrypt -- the screen stays black, the
 * client re-requests within one to three seconds, and the server has no counter
 * that moved and no line that says why.
 *
 * That is "a correct-looking key that does not open", and in a deployment that
 * leans on the cache it is the common case, not the exotic one: a cache peer
 * that is 200 ms late, or that re-serves what it fetched for the previous
 * period, sends exactly this. Nothing about the 16 bytes is wrong. 2.1 (the
 * agreement ledger) sees no disagreement, 2.2 (entropy) sees sixteen healthy
 * distinct bytes, 2.4 (plausibility) sees four intact group sums, and 1.3
 * (reuse) is blind on purpose because the SID did not change. Five layers, all
 * quiet, one black screen.
 *
 * WHAT THE PROTOCOL ALREADY CARRIES, AND WHERE THE TREE ALREADY KNOWS IT
 *
 * The marker exists end to end and has existed for 25 revisions:
 *
 *   - outbound, an ECM request tells a peer which half is wanted:
 *     `put_ecm2cache()` (clustredcache.c:656-657)
 *         !ecm->cw1cycle                     -> NO_CYCLE
 *         ecm->ecm[0] == ecm->cw1cycle       -> CW1CYCLE
 *         otherwise                          -> CW0CYCLE
 *   - inbound, a forwarding peer's reply carries the half it is sending in
 *     buf[29] (`received==30`), parsed at clustredcache.c:1710 and handed to
 *     `cache_setdcw()` as its third argument.
 *   - and the same rule is written a second time, live, in the ECM path:
 *     ecmdata.c:641-650 ("Setup Cw Cycle") derives the expected half from
 *     `ecm->ecm[0]` against `ecm->cw1cycle` and drops an ECM whose inferred
 *     cycle disagrees.
 *
 * Two live places, one rule: the expectation is a function of the ECM's own tag
 * byte. That is what `cwcy_expect()` below writes once.
 *
 * WHY THE OLD checker IS NOT SIMPLY RE-ENABLED
 *
 * `checkcycle()` sits commented out at clustredcache.c:512-526 with its call at
 * :721. It cannot express the rule above:
 *
 *   1. its constants predate the three-valued `cwcycle_t` (clustredcache.c:284,
 *      NO_CYCLE=0 / CW0CYCLE=1 / CW1CYCLE=2): after the enum was introduced,
 *      `cwcycle == 0` means "the key declares nothing" and never CW0 or CW1, so
 *      every branch of it tests the wrong thing;
 *   2. the field it reads, `cwlist[i].cwcycle`, is written nowhere in the tree
 *      (zero assignments outside the comment itself);
 *   3. its return polarity contradicts its own commented call site, which
 *      delivers when the function returns 1.
 *
 * Uncommenting it would therefore reject the right keys and accept the stale
 * ones while looking confident. D9 and TASK 1.11b already decided it stays dead;
 * this file is the measurement that has to come first, and it takes the rule
 * from the live code rather than from the comment.
 *
 * WHAT THIS FILE DOES -- AND WHAT IT REFUSES TO DO
 *
 * Per source, in a 60 s window:
 *
 *   receive point   keys offered, how many carried a marker, how many carried
 *                   one that contradicts the expectation (CWCY_CONTRA)
 *   delivery point  keys handed to a waiting client, how many were handed in
 *                   that contradictory state (CWCY_STALE), and how many could
 *                   not be judged at all because nobody declared anything
 *                   (CWCY_UNJUDGED)
 *
 * and one sentence per source per window when a contradiction happened.
 *
 * It NEVER rejects, delays, alters or scores anything, and it emits no trust
 * event on purpose: a stale key is evidence about a peer, not proof, and this
 * project does not accuse on evidence (GR3). The counters exist so that the
 * decision about what to do can be taken later on a measured base rate instead
 * of a guess -- which is the order TASK 1.11b already demanded for the cycle
 * question.
 *
 * COST (GR9)
 *
 * One fixed table of CWCY_SLOTS slots in .bss, no allocation, no I/O. The two
 * observation points run in two different threads -- the offer from the cache
 * thread (CSP reply handler), the hand-off from the setdcw thread (the
 * THREAD_DCW build delivers from there) -- so the caller serialises them with
 * one leaf mutex (cwcy_lock in main.c) held only across the arithmetic, never
 * across a log write or the trust lock.
 */
#ifndef MCS_CWCYCLE_H
#define MCS_CWCYCLE_H

#include <stdint.h>
#include <string.h>
#include <stdio.h>

#define CWCY_SLOTS      64       /* sources tracked at once                     */
#define CWCY_WINDOW     60000u   /* ms: one report per source per window         */

/* How a key's marker relates to what we expected to see. */
#define CWCY_AGREE      0        /* both marked and equal                        */
#define CWCY_CONTRA     1        /* both marked and different                    */
#define CWCY_UNJUDGED   2        /* no expectation, or the key declares nothing  */

/* Where the observation was made. */
#define CWCY_AT_RECV    0        /* a cache peer offered this key                */
#define CWCY_AT_SEND    1        /* this key was handed to a waiting client      */

struct cwcy_slot {
	int      used;
	int      srctype;
	int      srcid;
	uint32_t win_start;
	uint32_t win_keys;      /* cache keys offered this window                 */
	uint32_t win_marked;    /* ... of which carried a cycle marker            */
	uint32_t win_contra;    /* ... that contradicted the expectation          */
	uint32_t win_sends;     /* keys handed to a waiting client this window     */
	uint32_t win_stale;     /* ... handed in a contradictory state            */
	uint32_t win_unjudged;  /* ... nobody declared a cycle, so nothing to say  */
	uint32_t rep_ticks;     /* last time a contradiction was announced         */
	uint32_t reports;       /* announcements made, ever                       */
	uint32_t seen_keys;     /* lifetime counters, not per window              */
	uint32_t seen_contra;
	uint32_t seen_stale;
	uint32_t last;          /* last touch, for eviction when the table is full */
};

struct cwcy_table {
	struct cwcy_slot s[CWCY_SLOTS];
};

/*
 * Everything the log line needs. Filled on EVERY recorded observation (not only
 * when the sentence fires), so the caller's lifetime totals and the fired line
 * are built from the same numbers. `recorded` is 0 when the table refused the
 * observation -- then no other field means anything.
 */
struct cwcy_ev {
	unsigned recorded;      /* 1 once the table accepted this observation     */
	unsigned fired;         /* 1 when this observation earned the sentence    */
	int      srctype;
	int      srcid;
	int      where;         /* CWCY_AT_RECV / CWCY_AT_SEND                    */
	int      expected;      /* NO_CYCLE / CW0CYCLE / CW1CYCLE                 */
	int      observed;      /* what the key actually carried                  */
	uint16_t caid;
	uint32_t provid;
	uint16_t sid;
	uint32_t win_keys;
	uint32_t win_marked;
	uint32_t win_contra;
	uint32_t win_sends;
	uint32_t win_stale;
	uint32_t win_unjudged;
	uint32_t reports;
};

/*
 * The names used on the wire. Deliberately local: `cwcycle2str()` exists in
 * clustredcache.c but is a non-static function of that translation unit, and
 * this header is included before it.
 */
__attribute__((unused))
static const char *cwcy_str(int c)
{
	if (c == 1) return "CW0";
	if (c == 2) return "CW1";
	return "no marker";
}

/*
 * The expected half, from the ECM's own tag byte -- the rule the live code uses
 * in both places named at the top of this file. Returns NO_CYCLE when the
 * channel does not declare a cw1cycle, which is the honest answer for "the
 * operator has told us nothing about this channel's tag convention".
 */
__attribute__((unused))
static int cwcy_expect(uint8_t ecmtag, uint8_t cw1cycle)
{
	if (!cw1cycle)                    return 0;   /* NO_CYCLE */
	if (ecmtag == cw1cycle)           return 2;   /* CW1CYCLE */
	return 1;                                     /* CW0CYCLE */
}

/*
 * The half a key DECLARES about itself, sanitised to the three wire values.
 *
 * This is never read out of the key bytes: byte 15 of a control word is key
 * material (the fourth group's checksum byte), not a marker. The declaration
 * travels OUTSIDE the key -- buf[29] on the CSP wire (clustredcache.c:1710,
 * only when the peer forwards and the datagram carries it), and NO_CYCLE for
 * every cache-ex flavour in this revision. A peer that puts an arbitrary byte
 * on the wire (0x80, 0xFF) is not declaring anything checkable: that is the
 * CWCY_UNJUDGED case, silence, not a contradiction.
 */
__attribute__((unused))
static int cwcy_observed(int mark)
{
	/* the wire values, numerically: 1 = CW0CYCLE, 2 = CW1CYCLE */
	if (mark==1 || mark==2) return mark;
	return 0;
}

/*
 * The comparison, with the two ways of learning nothing kept apart from the one
 * way of learning something bad. A key that carries no marker is NOT a
 * contradiction -- most peers do not send one, and treating silence as guilt
 * would condemn the majority of a healthy mesh.
 */
__attribute__((unused))
static int cwcy_judge(int expected, int observed)
{
	if (!expected || !observed) return CWCY_UNJUDGED;
	return (expected == observed) ? CWCY_AGREE : CWCY_CONTRA;
}

__attribute__((unused))
static void cwcy_init(struct cwcy_table *t)
{
	if (t) memset(t, 0, sizeof(*t));
}

/*
 * The slot for a source, created if needed. Same shape as 2.4's table, and the
 * same lesson: the LARGEST age wins and `oldest` starts at 0, never at
 * UINT32_MAX -- a table that cannot evict refuses the next new source, which is
 * exactly when it is needed (2.4's unit suite caught that shape once already).
 */
__attribute__((unused))
static struct cwcy_slot *cwcy_slot_for(struct cwcy_table *t, int srctype,
                                       int srcid, uint32_t now)
{
	struct cwcy_slot *free_slot = NULL, *victim = NULL;
	int i;
	uint32_t oldest = 0;

	for (i = 0; i < CWCY_SLOTS; i++) {
		struct cwcy_slot *s = &t->s[i];
		uint32_t age;

		if (!s->used) {
			if (!free_slot) free_slot = s;
			continue;
		}
		if (s->srctype == srctype && s->srcid == srcid) {
			s->last = now;
			return s;
		}
		age = (uint32_t)(now - s->last);
		if (age >= oldest) {
			oldest = age;
			victim = s;
		}
	}

	if (!free_slot && victim) free_slot = victim;   /* reuse the oldest */

	if (free_slot) {
		memset(free_slot, 0, sizeof(*free_slot));
		free_slot->used = 1;
		free_slot->srctype = srctype;
		free_slot->srcid = srcid;
		free_slot->last = now;
		free_slot->win_start = now;
	}
	return free_slot;
}

/*
 * Record one observation. Returns 1 when the caller should write the sentence,
 * 0 when it should stay quiet -- and it must stay quiet for a source that keys
 * honestly, no matter how much traffic it carries. `out`, when non-NULL, is
 * filled on every RECORDED observation (out->recorded = 1), fired or not: the
 * caller's lifetime totals come from the same evidence the throttle saw.
 *
 * `where` selects the receive counters or the delivery counters; the window,
 * the contradiction count and the report throttle are shared, because the
 * question being answered is about the source, not about the phase.
 */
__attribute__((unused))
static int cwcy_note(struct cwcy_table *t, int srctype, int srcid,
                     uint16_t caid, uint32_t provid, uint16_t sid,
                     uint32_t now, int where, int expected, int observed,
                     struct cwcy_ev *out)
{
	struct cwcy_slot *s;
	int verdict;
	int fire = 0;

	if (out) memset(out, 0, sizeof(*out));
	if (!t) return 0;

	s = cwcy_slot_for(t, srctype, srcid, now);
	if (!s) return 0;                 /* table full and nothing evictable */

	if ((uint32_t)(now - s->win_start) >= CWCY_WINDOW) {
		s->win_start = now;
		s->win_keys = 0;
		s->win_marked = 0;
		s->win_contra = 0;
		s->win_sends = 0;
		s->win_stale = 0;
		s->win_unjudged = 0;
	}

	verdict = cwcy_judge(expected, observed);
	s->last = now;

	if (where == CWCY_AT_SEND) {
		s->win_sends++;
		if (verdict == CWCY_CONTRA) {
			s->win_stale++;
			s->seen_stale++;
		}
		else if (verdict == CWCY_UNJUDGED) s->win_unjudged++;
	}
	else {
		s->win_keys++;
		s->seen_keys++;
		if (observed) s->win_marked++;
		if (verdict == CWCY_CONTRA) {
			s->win_contra++;
			s->seen_contra++;
		}
	}

	/*
	 * One sentence per source per window, and only for a contradiction: a
	 * source whose keys agree, or whose keys say nothing, is never written
	 * about. The counters keep counting while the line stays throttled, so
	 * the stats line always shows the true rate.
	 */
	if ((s->win_contra || s->win_stale) &&
	    (!s->rep_ticks || (uint32_t)(now - s->rep_ticks) >= CWCY_WINDOW)) {
		s->rep_ticks = now;
		s->reports++;
		fire = 1;
	}

	if (out) {
		out->recorded = 1;
		out->fired = (unsigned)fire;
		out->srctype = srctype;
		out->srcid = srcid;
		out->where = where;
		out->expected = expected;
		out->observed = observed;
		out->caid = caid;
		out->provid = provid;
		out->sid = sid;
		out->win_keys = s->win_keys;
		out->win_marked = s->win_marked;
		out->win_contra = s->win_contra;
		out->win_sends = s->win_sends;
		out->win_stale = s->win_stale;
		out->win_unjudged = s->win_unjudged;
		out->reports = s->reports;
	}
	return fire;
}

/*
 * The sentence. It says where it saw the contradiction, what was expected and
 * what arrived, and -- in words -- that nothing was done about it, because the
 * operator reading this line has to be able to tell a measurement from an
 * action (GR8).
 */
__attribute__((unused))
static int cwcy_format(const struct cwcy_ev *ev, char *out, int outlen)
{
	const char *where;

	if (!out || outlen <= 0) return 0;
	out[0] = 0;
	if (!ev) return 0;

	where = (ev->where == CWCY_AT_SEND) ? "handed to a waiting client"
	                                     : "offered by the peer";

	return snprintf(out, (size_t)outlen,
		"CW CYCLE: source %d/%d ch %04x:%06x:%04x -- %u of %u cache key%s this "
		"window contradicted the cycle that ECM declares (expected %s, saw %s), "
		"%u key%s carried no marker; the last one was %s -- %u key%s were handed "
		"to a waiting client in that state. A key of the previous crypto period, "
		"or the other half of the even/odd pair, is genuine and still cannot open "
		"the picture. Nothing was rejected, delayed or scored: this is a "
		"measurement, not an accusation",
		ev->srctype, ev->srcid, ev->caid, ev->provid, ev->sid,
		ev->win_contra, ev->win_keys, (ev->win_keys == 1) ? "" : "s",
		cwcy_str(ev->expected), cwcy_str(ev->observed),
		ev->win_keys - ev->win_marked,
		(ev->win_keys - ev->win_marked == 1) ? "" : "s",
		where,
		ev->win_stale, (ev->win_stale == 1) ? "" : "s");
}

#endif /* MCS_CWCYCLE_H */
