/*
 * rotation.h — TASK 1.6: instant rotation away from a source that just failed
 *
 * THE PROBLEM.
 * A client that gets a bad CW shows a black screen, then re-requests the same
 * channel's ECM 1-3 s later instead of at the normal ~10 s crypto period. The
 * server answers that retry from the same place it answered the first time, so
 * the client gets the same bad key and stays black. Nothing rotates.
 *
 * WHAT THIS DOES.
 * It remembers "this source produced a bad key for this channel", so the next
 * ECM for that channel is sent to a DIFFERENT source. That is the whole of
 * MTTR <= 1 crypto period: the retry itself becomes the recovery, instead of a
 * second failure.
 *
 * THREE RULES THAT MATTER MORE THAN THE DATA STRUCTURE.
 *
 * 1. Rotation must never be able to black-screen a channel by itself (GR3).
 *    `rotation_should_avoid()` refuses to avoid a source once
 *    ROTATION_MAX_AVOID sources are already recorded for that channel. If every
 *    source were avoidable, the channel would have no one left to answer it and
 *    the "fix" would be worse than the disease. The cap is what keeps a bad
 *    inference from becoming an outage. The test pins this both ways.
 *
 * 2. Clearing is tied to evidence, not to a timer.
 *    A channel is healed when a key for it is delivered and the client does NOT
 *    retry — that is `rotation_note_good()`, called from the same
 *    classification that TASK 1.2 and 1.4 already use. A fixed expiry was
 *    rejected: any timeout chosen is either long enough to serve more bad keys
 *    or short enough to re-try the bad source before the client has had a chance
 *    to prove the new one works.
 *
 * 3. Attribution is the source recorded at delivery, never the current one.
 *    The caller passes `lastecm.dcwsrctype/dcwsrcid`. Rotating away from a source
 *    that did not supply the key would penalise the wrong party, which is GR1.
 *
 * GRANULARITY. The channel key is (caid, prov, sid), which is what
 * `sidata_getval()` and `match_card()` already key on (loadbalance.c:51, :201).
 * Source identity is (srctype, srcid) with the PEER_* origin flags intact, the
 * same identity trust.h and cache_purge.h use, so a CSP peer and a cacheex
 * client with the same numeric id are different sources (GR4).
 *
 * GR9: fixed 64 entries in .bss, no allocation on the ECM path, no I/O, no
 * unbounded growth. Every entry point tolerates a NULL table.
 *
 * COMPILED WITH -fpack-struct IN THE REAL BUILD. Its tests are compiled the same
 * way; see the note in make-x64/Makefile about sizeof disagreeing otherwise.
 */

#ifndef MCS_ROTATION_H
#define MCS_ROTATION_H

#include <string.h>
#include <stdint.h>

#define ROTATION_SLOTS      64
#define ROTATION_MAX_AVOID  3   /* never avoid more than this many per channel */
#define ROTATION_PER_SLOT   4   /* source ids remembered per channel */

struct rotation_entry {
	uint16_t caid;
	uint16_t sid;
	uint32_t prov;
	uint32_t srcid[ROTATION_PER_SLOT];
	uint8_t  srctype[ROTATION_PER_SLOT];
	/*
	 * TASK 1.9 -- BAD-CW-LIMIT. Confirmed bad-CW events seen from each recorded
	 * source on this channel, saturating at 255.
	 *
	 * Counted for every source that produces a bad key, including those below
	 * the limit: a source only enters the avoid list once its count reaches
	 * BAD-CW-LIMIT, so the counter is what makes a tolerance possible at all.
	 * Reset for the whole channel by rotation_note_good(), because "the channel
	 * works again" resets the question being asked.
	 */
	uint8_t  bad[ROTATION_PER_SLOT];
	uint8_t  n;            /* how many sources are recorded; capped at PER_SLOT */
	uint8_t  inuse;
	uint32_t ticks;        /* last time this channel was touched */
};

struct rotation_table {
	struct rotation_entry e[ROTATION_SLOTS];
	uint32_t avoided;      /* times a source was actually skipped */
	uint32_t capped;       /* times avoidance was refused because of the cap */
	/*
	 * TASK 1.9 -- bad-CW events that produced no avoidance: counted and still
	 * below BAD-CW-LIMIT, or not counted at all because the channel's per-slot
	 * array was full. With the default limit of 1 it stays 0 forever, which is
	 * how a default install can be told apart from a tuned one at a glance --
	 * and it is the number that answers "is my tolerance set too high?".
	 */
	uint32_t deferred;
};

/* The __attribute__((unused)) markers below are not decoration: these are
 * header-local statics that form the module's API and are exercised by the
 * unit tests in make-x64/, but main.c does not call every one of them, and
 * -Wunused-function is not silenced by -Wno-unused-variable. */
__attribute__((unused))
static void rotation_init(struct rotation_table *t)
{
	if (!t) return;
	memset(t, 0, sizeof(*t));
}

static struct rotation_entry *rotation_find(struct rotation_table *t,
                                            uint16_t caid, uint16_t sid, uint32_t prov)
{
	int i;
	if (!t) return 0;
	for (i = 0; i < ROTATION_SLOTS; i++)
		if (t->e[i].inuse && t->e[i].caid == caid &&
		    t->e[i].sid == sid && t->e[i].prov == prov)
			return &t->e[i];
	return 0;
}

/*
 * Should `srctype/srcid` be skipped for this channel?
 *
 * THE CAP, AND WHY IT IS POSITIONAL RATHER THAN TOTAL.
 * Only the first ROTATION_MAX_AVOID recorded sources are avoided; the most
 * recently recorded ones are not. That leaves at least one recorded-but-usable
 * source, so the channel can never be stranded by this mechanism.
 *
 * The obvious alternative -- stop avoiding anything once the cap is reached --
 * is a bug, and it was the first thing written here. It re-enables source #1,
 * which is known bad, the moment source #3 is recorded, so rotation would break
 * precisely when the most sources are failing. A cap has to release the NEWEST
 * entries, never the oldest evidence.
 *
 * `capped` counts those refusals, so a log line can say the cap fired instead of
 * the failure mysteriously stopping.
 */
static int rotation_should_avoid_limited(struct rotation_table *t,
                                         uint8_t srctype, uint32_t srcid,
                                         uint16_t caid, uint16_t sid, uint32_t prov,
                                         int limit)
{
	struct rotation_entry *e;
	int i, lim;

	if (!t) return 0;
	lim = (limit < 1) ? 1 : limit;

	e = rotation_find(t, caid, sid, prov);
	if (!e) return 0;

	for (i = 0; i < e->n && i < ROTATION_PER_SLOT; i++)
		if (e->srctype[i] == srctype && e->srcid[i] == srcid) {
			/*
			 * TASK 1.9: a source below BAD-CW-LIMIT has been counted but not
			 * yet earned avoidance. GR3 in one line -- one accident is not a
			 * verdict, and the tolerance is the operator's knob for how many
			 * accidents it takes.
			 */
			if ((int)e->bad[i] < lim) return 0;
			if (i >= ROTATION_MAX_AVOID) {
				t->capped++;
				return 0; /* newest entries stay reachable */
			}
			t->avoided++;
			return 1;
		}
	return 0;
}

/*
 * The shipped behaviour, by name: avoid as soon as one confirmed bad CW has come
 * from this source on this channel. That is what TASK 1.6 did and what a config
 * that never mentions BAD-CW-LIMIT still gets; the caller that has a configured
 * limit uses rotation_should_avoid_limited() directly.
 */
__attribute__((unused))
static int rotation_should_avoid(struct rotation_table *t,
                                 uint8_t srctype, uint32_t srcid,
                                 uint16_t caid, uint16_t sid, uint32_t prov)
{
	return rotation_should_avoid_limited(t, srctype, srcid, caid, sid, prov, 1);
}

/*
 * Record that this source produced a bad key for this channel.
 * Returns 1 if the source was newly recorded, 0 if it was already there.
 *
 * Eviction is longest-unseen, matching trust.h and cache_purge.h: evicting the
 * channel with the most recorded sources would forget exactly the channel that
 * is in the most trouble.
 */
/*
 * Find the entry for this channel, creating it if there is room.
 *
 * TASK 1.9 extracted this from rotation_note_bad() unchanged, because the bad-CW
 * counter has to be able to remember a count for a source that is not yet in the
 * avoid list -- and creating the same entry from two places would have been two
 * copies of the eviction policy to keep in step. The existing 38 assertions of
 * test_rotation.c, written before this extraction, are what proves it faithful.
 */
static struct rotation_entry *rotation_entry_get(struct rotation_table *t,
                                                uint16_t caid, uint16_t sid, uint32_t prov,
                                                uint32_t ticks_now)
{
	struct rotation_entry *e;
	int i, freepos = -1, victim = -1;
	uint32_t oldest = 0xFFFFFFFFu;

	if (!t) return NULL;

	e = rotation_find(t, caid, sid, prov);
	if (e) { e->ticks = ticks_now; return e; }

	for (i = 0; i < ROTATION_SLOTS; i++) {
		if (t->e[i].inuse) {
			if (t->e[i].ticks < oldest) { oldest = t->e[i].ticks; victim = i; }
		}
		else if (freepos < 0) freepos = i;
	}
	if (freepos >= 0) victim = freepos;
	if (victim < 0) return NULL;

	e = &t->e[victim];
	memset(e, 0, sizeof(*e));
	e->caid = caid; e->sid = sid; e->prov = prov;
	e->inuse = 1;
	e->ticks = ticks_now;
	return e;
}

/*
 * TASK 1.9 -- the bad-CW event, with a tolerance.
 *
 * One slot per source, holding the number of confirmed bad-CW events seen from
 * that source on this channel, saturating at 255. A source is *avoided* when its
 * count has reached `limit`; there is no separate avoid list, because two lists
 * would have had to be kept in step and the count is the only thing that decides.
 *
 * Returns:
 *   2  this event is the one that reached the limit -- the caller logs, once
 *   1  the source is over the limit already (a later event, not news)
 *   0  counted, still below the limit
 *
 * `limit <= 1` is the default and reproduces TASK 1.6's behaviour exactly:
 * the first confirmed bad CW reaches the limit and returns 2. That is why the
 * default cannot change what a config not mentioning BAD-CW-LIMIT does.
 *
 * A source that reaches the limit keeps its count and its slot; the channel
 * recovering (rotation_note_good) is what resets it. Earlier cuts of this
 * function kept counts for not-yet-avoided sources in slots above e->n, which
 * meant two such sources shared one slot and neither ever reached the limit --
 * the unit suite's alternating-source case is what catches that.
 */
static int rotation_note_bad_limited(struct rotation_table *t,
                                    uint8_t srctype, uint32_t srcid,
                                    uint16_t caid, uint16_t sid, uint32_t prov,
                                    uint32_t ticks_now, int limit)
{
	struct rotation_entry *e;
	int i, lim;

	if (!t) return 0;
	lim = (limit < 1) ? 1 : limit;

	e = rotation_entry_get(t, caid, sid, prov, ticks_now);
	if (!e) return 0;

	for (i = 0; i < e->n && i < ROTATION_PER_SLOT; i++) {
		if (e->srctype[i] == srctype && e->srcid[i] == srcid) {
			if (e->bad[i] < 255) e->bad[i]++;
			if (e->bad[i] < (uint8_t)lim) { t->deferred++; return 0; }
			/* Reached the limit on this very event? Only then is it news. */
			return (e->bad[i] == (uint8_t)lim) ? 2 : 1;
		}
	}

	if (e->n < ROTATION_PER_SLOT) {
		i = e->n++;
		e->srctype[i] = srctype;
		e->srcid[i]   = srcid;
		e->bad[i]     = 1;
		if (e->bad[i] < (uint8_t)lim) { t->deferred++; return 0; }
		return 2;
	}

	/*
	 * Per-slot full. The cap in rotation_should_avoid() already limits how many
	 * sources can be avoided on one channel, so refusing to count here cannot
	 * change any decision -- but it deferred an action, so it lands on the same
	 * counter as the below-limit case and the stats line shows it.
	 */
	t->deferred++;
	return 0;
}

__attribute__((unused))
static int rotation_note_bad(struct rotation_table *t,
                             uint8_t srctype, uint32_t srcid,
                             uint16_t caid, uint16_t sid, uint32_t prov,
                             uint32_t ticks_now)
{
	/*
	 * The old contract, preserved exactly: 1 means "this source was newly
	 * recorded as bad", 0 means "nothing new". At limit 1 the crossing IS the
	 * recording, so 2 maps to 1 and both other outcomes map to 0 -- which is why
	 * the assertions written for TASK 1.6 still describe this function without
	 * being touched.
	 */
	return (rotation_note_bad_limited(t, srctype, srcid, caid, sid, prov,
	                                  ticks_now, 1) == 2) ? 1 : 0;
}

/*
 * Is this source avoided on this channel, for the configured limit?
 * A count below the limit is not avoidance; a count of 0 means "never seen".
 */
__attribute__((unused))
static int rotation_count_of(struct rotation_table *t,
                             uint8_t srctype, uint32_t srcid,
                             uint16_t caid, uint16_t sid, uint32_t prov)
{
	struct rotation_entry *e;
	int i;

	if (!t) return 0;
	e = rotation_find(t, caid, sid, prov);
	if (!e) return 0;
	for (i = 0; i < e->n && i < ROTATION_PER_SLOT; i++)
		if (e->srctype[i] == srctype && e->srcid[i] == srcid)
			return (int)e->bad[i];
	return 0;
}

/*
 * The channel works again: a key was delivered and the client did not retry.
 * Clears every recorded source for that channel, so a source is not avoided
 * forever on the strength of one bad key. Returns the number cleared.
 */
static int rotation_note_good(struct rotation_table *t,
                              uint16_t caid, uint16_t sid, uint32_t prov)
{
	struct rotation_entry *e = rotation_find(t, caid, sid, prov);
	int n;
	if (!e) return 0;
	n = e->n;
	e->inuse = 0;
	return n;
}

/* How many sources are currently recorded as bad for this channel. */
__attribute__((unused))
static int rotation_count_bad(struct rotation_table *t,
                              uint16_t caid, uint16_t sid, uint32_t prov)
{
	struct rotation_entry *e = rotation_find(t, caid, sid, prov);
	return e ? e->n : 0;
}

#endif /* MCS_ROTATION_H */
