/*
 * cwcm.h -- TASK 2.7, complement-mirror detection on delivered keys.
 *
 * THE CONSTRUCTION, AND WHY MOST OF IT IS INVISIBLE TODAY
 *
 * A mirror key is built from eight bytes: half 1 is A, half 2 is the bitwise
 * complement of A. The arithmetic (verified against the real checksumDCW,
 * dcw.c:35):
 *
 *   - HALF 1 carries A's own group sums: groups 1 and 2 are as valid as the
 *     builder made them.
 *   - HALF 2 breaks its two group sums, each by exactly 2: the sum of three
 *     complemented bytes is (255*3 - S) mod 256 = (253 - S), while the
 *     complemented checksum byte is (255 - S). A complemented group can
 *     never satisfy the SUM rule. So the PURE mirror fails 2 of 4 groups:
 *     the checksum filter refuses it, and TASK 2.4 counts it CWP_WEAK --
 *     counted, never explained.
 *   - the REPAIRED mirror fixes the two broken checksum bytes (or one free
 *     byte plus the sums). Then all four groups hold, and the key passes
 *     every structural layer this project has: checksum (1.0) holds, 2.4
 *     reads 4/4 "full", 2.2 sees ~13 distinct healthy bytes, 2.1 has no
 *     second key to disagree with, 1.3 has no reuse. It is DELIVERED, and a
 *     delivered fabricated key is a black screen with nothing in the log.
 *     Measured in this task's probe: 0 of 50,000,000 random sum-valid keys
 *     show even 4 of 6 complement pairs.
 *
 * THE DETECTOR
 *
 * One property, checked on the key alone: how many of the six FREE byte
 * pairs -- (0,8) (1,9) (2,10) (4,12) (5,13) (6,14), free meaning "not a
 * group-sum checksum byte" -- satisfy cw[8+j] == (uint8_t)~cw[j].
 *
 *   6/6 or 5/6  a mirror key. 5/6 is a mirror repaired in free space. The
 *               false-positive rate at >=5 is ~8e-12 per key (the probe
 *               above found zero at >=4 in fifty million).
 *   <=4/6       nothing to say; honest keys sit at 0-1 by chance.
 *
 * The checksum bytes are deliberately NOT paired: a repaired mirror's
 * checksum bytes do not satisfy the complement (they are (253-S) where the
 * complement would be (255-S)), and their honesty or dishonesty is already
 * 2.4's business. This file looks only at the twelve bytes the builder
 * could not "repair away" without destroying the construction itself.
 *
 * WHAT THE LAYER DOES -- same discipline as 2.4
 *
 * Per source, in a 60 s window: how many keys were seen, how many carried
 * the mirror verdict, and one sentence per source per window when the
 * pattern holds (>= CWCM_MIN_KEYS mirror keys, >= CWCM_RATE_PCT of the
 * source's traffic, no mixing -- there is nothing to mix, the verdict is
 * single). The pattern is a soft trust event, exactly like 2.4's: a mirror
 * key is EDIT evidence about the source, not a proof, and one key alone
 * convicts nothing (GR3). The layer never rejects, delays or alters a
 * delivery -- the mirror keys PASS every filter and are delivered exactly
 * as before; what changes is that the log now says why they will not open.
 *
 * A pure mirror (which fails 2 sums) is observed here too, wherever it is
 * refused -- including the cache gate's rejecting path -- so the layer also
 * explains 2.4's weak-shape counters when the weak keys are mirrors.
 *
 * COST (GR9): fixed .bss table, no allocation, no I/O; the scan is six
 * comparisons. The table is written from the delivery threads and from the
 * cache thread (rejecting path); the caller serialises those with the trust
 * lock around the soft event, as cwp_note() does -- no new lock.
 */
#ifndef MCS_CWCM_H
#define MCS_CWCM_H

#include <stdint.h>
#include <string.h>
#include <stdio.h>

#define CWCM_MIN_PAIRS  5        /* complement pairs that make the verdict     */
#define CWCM_PAIRS      6        /* free byte pairs in a control word          */
#define CWCM_SLOTS      64       /* sources tracked at once                    */
#define CWCM_WINDOW     60000u   /* ms: the window the rate is measured over   */
#define CWCM_MIN_KEYS   3        /* mirror keys needed before the pattern      */
#define CWCM_RATE_PCT   20       /* ...and their share of the source's window  */

/* Verdict masks for one key. */
#define CWCM_NONE       0x00
#define CWCM_MIRROR     0x01
#define CWCM_SCOREMASK  (CWCM_MIRROR)

struct cwcm_slot {
	int      used;
	int      srctype;
	int      srcid;
	uint32_t win_start;
	uint32_t win_all;        /* keys seen this window                       */
	uint32_t win_mirror;     /* of those, mirror verdicts                   */
	uint32_t rep_ticks;      /* last time the pattern was announced         */
	uint32_t reports;        /* announcements made, ever                    */
	uint32_t seen_all;       /* lifetime counters                           */
	uint32_t seen_mirror;
	uint32_t last;           /* last touch, for eviction when the table is full */
};

struct cwcm_table {
	struct cwcm_slot s[CWCM_SLOTS];
};

/*
 * Everything the log line needs, filled only when the verdict was formed --
 * the same rule as 2.4: a line that recomputes its own evidence can disagree
 * with the verdict that triggered it.
 */
struct cwcm_evidence {
	int      srctype;
	int      srcid;
	unsigned verdict;        /* the raw mask for this key                   */
	unsigned fresh;          /* the subset worth reporting/scoring now      */
	int      pairs;          /* complement pairs found, 0..6                */
	uint16_t caid;
	uint32_t provid;
	uint16_t sid;
	uint32_t win_all;
	uint32_t win_mirror;
	uint32_t win_rate;       /* win_mirror as a whole percentage            */
	uint32_t seen_all;
	uint32_t seen_mirror;
	uint32_t reports;
};

/*
 * The scan: six comparisons, nothing else (GR9).
 */
__attribute__((unused))
static int cwcm_pairs(const uint8_t *cw)
{
	static const int A[6] = {0,1,2,4,5,6};
	static const int B[6] = {8,9,10,12,13,14};
	int i, n = 0;

	if (!cw) return 0;
	for (i = 0; i < 6; i++)
		if (cw[B[i]] == (uint8_t)~cw[A[i]]) n++;
	return n;
}

__attribute__((unused))
static void cwcm_init(struct cwcm_table *t)
{
	if (t) memset(t, 0, sizeof(*t));
}

/*
 * The slot for a source, created if needed. Same shape -- and same lesson --
 * as 2.4's table: the LARGEST age wins, `oldest` starts at 0, and a full
 * table always evicts rather than refusing the next source.
 */
static struct cwcm_slot *cwcm_slot_for(struct cwcm_table *t, int srctype,
                                       int srcid, uint32_t now)
{
	struct cwcm_slot *free_slot = NULL, *victim = NULL;
	int i;
	uint32_t oldest = 0;

	for (i = 0; i < CWCM_SLOTS; i++) {
		struct cwcm_slot *s = &t->s[i];
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

	if (free_slot) {
		memset(free_slot, 0, sizeof(*free_slot));
		free_slot->used = 1;
		free_slot->srctype = srctype;
		free_slot->srcid = srcid;
		free_slot->last = now;
		free_slot->win_start = now;
		return free_slot;
	}

	if (victim) {
		memset(victim, 0, sizeof(*victim));
		victim->used = 1;
		victim->srctype = srctype;
		victim->srcid = srcid;
		victim->last = now;
		victim->win_start = now;
		return victim;
	}
	return NULL;
}

/*
 * Score one delivered (or refused) key for one source. Returns the verdict
 * mask; `out->fresh` holds the subset worth a log line and a score change.
 * A source that mirrors every key gets its pattern announced once per
 * CWCM_WINDOW, not once per key; the counters keep counting while the line
 * stays quiet.
 */
__attribute__((unused))
static unsigned cwcm_seen(struct cwcm_table *t, int srctype, int srcid,
                          const uint8_t *cw, uint16_t caid, uint32_t provid,
                          uint16_t sid, uint32_t now,
                          struct cwcm_evidence *out)
{
	struct cwcm_slot *s;
	int pairs;
	unsigned m;

	if (out) memset(out, 0, sizeof(*out));
	if (!t || !cw) return CWCM_NONE;

	pairs = cwcm_pairs(cw);
	m = (pairs >= CWCM_MIN_PAIRS) ? CWCM_MIRROR : CWCM_NONE;

	s = cwcm_slot_for(t, srctype, srcid, now);
	if (!s) return CWCM_NONE;          /* table full and nothing evictable */

	/* The window rolls first, so this key is counted in the window it lands in. */
	if ((uint32_t)(now - s->win_start) >= CWCM_WINDOW) {
		s->win_start = now;
		s->win_all = 0;
		s->win_mirror = 0;
	}

	s->win_all++;
	s->last = now;
	s->seen_all++;

	if (m == CWCM_MIRROR) {
		s->win_mirror++;
		s->seen_mirror++;
	}

	/* The pattern: enough mirror keys, at enough of the source's traffic. */
	if ((m & CWCM_MIRROR) &&
	    s->win_mirror >= CWCM_MIN_KEYS &&
	    (uint64_t)s->win_mirror * 100 >= (uint64_t)s->win_all * CWCM_RATE_PCT) {
		if (!s->rep_ticks || (uint32_t)(now - s->rep_ticks) >= CWCM_WINDOW) {
			s->rep_ticks = now;
			s->reports++;
			if (out) out->fresh = CWCM_MIRROR;
		}
	}

	if (out) {
		out->srctype = srctype;
		out->srcid = srcid;
		out->verdict = m;
		out->pairs = pairs;
		out->caid = caid;
		out->provid = provid;
		out->sid = sid;
		out->win_all = s->win_all;
		out->win_mirror = s->win_mirror;
		out->win_rate = s->win_all ? (s->win_mirror * 100u) / s->win_all : 0;
		out->seen_all = s->seen_all;
		out->seen_mirror = s->seen_mirror;
		out->reports = s->reports;
	}
	return m;
}

/*
 * The sentence. It names the construction, says what it means for every
 * structural layer, and -- in words -- that this layer touched nothing.
 */
__attribute__((unused))
static int cwcm_format(const struct cwcm_evidence *ev, char *out, int outlen)
{
	if (!out || outlen <= 0) return 0;
	out[0] = 0;
	if (!ev) return 0;

	return snprintf(out, (size_t)outlen,
		"CW MIRROR: source %d/%d ch %04x:%06x:%04x -- %u of %u key%s this "
		"window are mirror-built: the second half is the bitwise complement "
		"of the first (%d of 6 byte pairs on the key that crossed the line). "
		"A complement half breaks 2 of the 4 group sums by exactly 2, so a "
		"mirror that passes the checksum layer is a REPAIRED mirror -- built, "
		"then adjusted to look structural -- and it cannot open its picture. "
		"Delivery was not touched by this layer: the keys already passed "
		"every filter",
		ev->srctype, ev->srcid, ev->caid, ev->provid, ev->sid,
		ev->win_mirror, ev->win_all, (ev->win_all == 1) ? "" : "s",
		ev->pairs);
}

#endif /* MCS_CWCM_H */
