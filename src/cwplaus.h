/*
 * TASK 2.4 -- plausibility scoring: the graded version of the blunt checksum
 * test.
 *
 * WHAT THE BLUNT TEST IS, AND WHY IT IS BLUNT
 *
 * Every control word MultiCS delivers passes through `acceptDCW()` (dcw.c:103),
 * which applies `checksumDCW()`: each of the key's four 4-byte groups must end
 * with the sum of the three bytes before it, modulo 256. That is a genuine
 * property of the keys this server handles -- a real control word from a bouquet
 * that uses the rule always satisfies all four groups -- and when the operator
 * leaves `DCWFILTER CHECKSUM` on, a key that fails it is thrown away.
 *
 * Two things are wrong with that, and they are opposite problems:
 *
 *   1. It says nothing. A key that fails is dropped in silence: the client that
 *      asked for it waits, fails to decode, and re-asks -- which is a black
 *      screen -- and the operator has no idea which source sent the bad key or
 *      how many it has sent. TASK 1.8 records the fact of a checksum failure
 *      against a source, but as one bit among several structural verdicts.
 *
 *   2. It is a yes/no answer to a question that has a shape. "Fails the
 *      checksum" covers an all-zero stub, a key that is random noise, and a key
 *      that is perfect except for ONE BYTE -- and those are three different
 *      things. The last of them is not noise. It is the signature of a
 *      deliberate edit, and it is the exact forgery this project's own research
 *      turned up: the r107 changelog warns of a peer that fabricates a fake CW
 *      by XOR-ing the last byte (see docs/ORIGIN.md and the r107 notes). XOR on
 *      byte 15 breaks group 4 and leaves the other three intact.
 *
 * WHAT THIS FILE DOES
 *
 * It counts, per source, HOW MANY of the four group sums a delivered key
 * satisfies -- 4, 3, 2, 1 or 0 -- and turns the count into a graded statement:
 *
 *     4/4  a key of the expected shape. Nothing is said, nothing is scored.
 *     3/4  one group wrong: exactly one byte's worth of damage in a key that is
 *          otherwise structurally correct. This is CWP_ONE_GROUP.
 *     <=2  neither: the key does not have the shape of this rule at all. Counted
 *          as CWP_WEAK and never scored, because it is also exactly what an
 *          honest key looks like in a bouquet that does not use the rule (see
 *          "WHY A PATTERN" below).
 *
 * WHY A PATTERN AND NOT A SINGLE KEY
 *
 * A key from a bouquet that does NOT use the SUM rule passes any given group by
 * chance with probability 1/256, so it lands on exactly 3/4 with probability
 *
 *      4 * (1/256) * (255/256)^3  =  1.52 %
 *
 * which is far too common to accuse anybody of. A single 3/4 key is therefore
 * recorded and counted and NOT scored -- it may be a damaged key, and it may be
 * an ordinary key from a bouquet this rule does not describe, and nothing in the
 * key itself can tell those apart.
 *
 * A pattern of them can. So the scored verdict requires all three of:
 *
 *     1. at least CWP_MIN_ONEGROUP (3) one-group keys from that source,
 *     2. in the same CWP_WINDOW (60 s),
 *     3. making up at least CWP_RATE_PCT (20 %) of that source's keys in the
 *        window, and
 *     4. all of them failing the SAME group.
 *
 * The third clause is what keeps a busy honest source quiet (with a thousand
 * keys in a minute, 1.52 % of them are expected to be 3/4 by chance, but they
 * would be ~1.5 % of the traffic, not 20 %), and the fourth is what makes the
 * claim a statement about one damaged byte rather than about four different
 * accidents: a forger edits one byte, so its failures land in one group.
 *
 * The worst honest case is a source with exactly 3 one-group keys in a window of
 * 15 (20 %): P(3 of 15 | p = 1.52 %) = 5.2e-4, times 1/16 for all three failing
 * the same group = 3.2e-5 per window. Above ten keys the count clause stops
 * being the binding one and the rate clause takes over, and it falls off fast:
 * 4 of 20 is 3.2e-5, 6 of 30 is 1.4e-6. So this fires on an honest source about
 * once in nine hours of continuous production, and on a forger in three keys.
 *
 * This is deliberately a *weaker* claim than TASK 1.8's checksum bit, and the
 * trade was chosen on purpose: 1.8's bit needs the operator's declaration
 * (it is gated on `DCWFILTER CHECKSUM`, so a bouquet that legitimately fails the
 * rule is not penalised), while this one does not read any filter gate at all --
 * 3/4 means the same thing whether or not the operator turned the blunt test on.
 * The price of a statement that needs no declaration is that it needs evidence
 * from more than one key, and the price of *that* is latency: the first damaged
 * key is counted, the third is what convicts.
 *
 * WHAT IT IS NOT
 *
 * It is not a filter. Nothing here rejects a control word, and nothing here can:
 * the return value is a verdict mask, the caller passes it to a counter and to a
 * soft trust event, and delivery continues exactly as the blunt test decided.
 * `acceptDCW()` is untouched. Turning this layer off would change no client's
 * picture -- which is the property every evidence-only layer in this project is
 * required to have (GR3).
 *
 * It is not per-service either. A source's plausibility is a property of the
 * source, so the table and the window are keyed on (srctype, srcid) alone. The
 * soft event it feeds is keyed on the service that was being served (GR4), and
 * that key is in the evidence struct rather than in the table.
 */

#ifndef MCS_CWPLAUS_H
#define MCS_CWPLAUS_H

#include <string.h>
#include <stdio.h>
#include <stdint.h>

/* The verdicts. */
#define CWP_NONE       0x00   /* 4/4: the expected shape                      */
#define CWP_ONE_GROUP  0x01   /* exactly one of the four group sums is wrong  */
#define CWP_WEAK       0x02   /* two or fewer groups hold                     */
#define CWP_BITS       2      /* for the per-verdict counter arrays           */

/*
 * Only CWP_ONE_GROUP may move a score. CWP_WEAK is what a bouquet that does not
 * use this rule produces on every honest key, so scoring it would decay a
 * correctly-configured source continuously -- the exact failure mode GR3 exists
 * to prevent. The mask lives here, next to the probabilities, for the same
 * reason TASK 1.8's and 2.2's do.
 */
#define CWP_SCOREMASK  (CWP_ONE_GROUP)

#define CWP_GROUPS        4       /* 4-byte groups in a control word (dcw.c:35) */
#define CWP_SLOTS         64      /* sources tracked at once                    */
#define CWP_MIN_ONEGROUP  3       /* keys needed before the pattern is claimed  */
#define CWP_RATE_PCT      20      /* ...and their share of the source's window  */
#define CWP_WINDOW        60000u  /* ms: the window the rate is measured over   */

struct cwp_slot {
	int      used;
	int      srctype;
	int      srcid;
	uint32_t win_start;     /* start of the rate window                       */
	uint32_t win_all;       /* keys counted in it                             */
	uint32_t win_one;       /* of those, one-group keys                       */
	uint32_t win_group;     /* the group those keys failed (0 = none yet)      */
	uint32_t win_mixed;     /* 1 once two different groups failed in one window */
	uint32_t rep_ticks;     /* last time the pattern was announced             */
	uint32_t reports;       /* announcements made, ever                        */
	uint32_t seen_full;     /* lifetime counters (lifetime, not per window)    */
	uint32_t seen_one;
	uint32_t seen_weak;
	uint32_t last;          /* last touch, for eviction when the table is full */
};

struct cwp_table {
	struct cwp_slot s[CWP_SLOTS];
};

/*
 * Everything the log line needs, filled only when a verdict was formed. The
 * sentence is never re-derived from the table by the caller: a line that
 * computes its own evidence can disagree with the verdict that triggered it.
 */
struct cwp_evidence {
	int      srctype;
	int      srcid;
	unsigned verdict;       /* the raw mask for this key                     */
	unsigned fresh;         /* the subset worth reporting/scoring now        */
	int      groups;        /* groups whose sum rule holds, 0..4             */
	int      bad_group;     /* 1..4: the failing group when exactly one; else 0 */
	uint16_t caid;
	uint32_t provid;
	uint16_t sid;
	uint32_t win_all;       /* the window when the verdict was formed        */
	uint32_t win_one;
	uint32_t win_rate;      /* win_one as a whole percentage, for the line   */
	uint32_t seen_full;     /* lifetime counters at that moment              */
	uint32_t seen_one;
	uint32_t seen_weak;
	uint32_t reports;
};

/* ---------------------------------------------------------------------------
 * The arithmetic
 * ------------------------------------------------------------------------ */

/*
 * How many of the four group sums hold. This reproduces `checksumDCW()`
 * (dcw.c:35) group by group instead of as one boolean, which is the whole point
 * of the file: the boolean throws away the difference this layer measures.
 *
 * Nothing is allocated, nothing is read outside the 16 bytes, no lock, no I/O --
 * it runs on the delivery path for every key of every source (GR9).
 */
__attribute__((unused))
static int cwp_groups(const uint8_t *cw)
{
	int g = 0;
	uint32_t i;

	if (!cw) return 0;
	for (i = 0; i < CWP_GROUPS; i++) {
		const uint8_t *p = cw + (i * 4);
		if (p[3] == (uint8_t)((p[0] + p[1] + p[2]) & 0xFF)) g++;
	}
	return g;
}

/*
 * The verdict for one key. `groups` receives 0..4, and `bad_group` the 1-based
 * index of the failing group when exactly one of them fails (0 otherwise, so a
 * caller cannot mistake "no failure" for "group zero").
 */
__attribute__((unused))
static unsigned cwp_scan(const uint8_t *cw, int *groups, int *bad_group)
{
	int g, i, bad = 0;

	g = cwp_groups(cw);

	if (bad_group) {
		if (g == CWP_GROUPS - 1) {
			for (i = 0; i < CWP_GROUPS; i++) {
				const uint8_t *p = cw + (i * 4);
				if (p[3] != (uint8_t)((p[0] + p[1] + p[2]) & 0xFF)) {
					bad = i + 1;   /* 1-based: "group 4 of 4" reads better */
					break;
				}
			}
		}
		*bad_group = bad;
	}
	if (groups) *groups = g;

	if (g == CWP_GROUPS)      return CWP_NONE;
	if (g == CWP_GROUPS - 1)  return CWP_ONE_GROUP;
	return CWP_WEAK;
}

__attribute__((unused))
static void cwp_init(struct cwp_table *t)
{
	if (t) memset(t, 0, sizeof(*t));
}

/*
 * The slot for a source, created if needed. A full table evicts the source that
 * was touched longest ago rather than refusing the new one, because the source
 * that has been silent for the longest is the one whose window has least to say
 * -- and a table that can silently stop tracking a *new* source would be blind
 * exactly when a fresh poisoning attempt appears (the same shape as 1.8's
 * table, deliberately).
 */
static struct cwp_slot *cwp_slot_for(struct cwp_table *t, int srctype, int srcid,
                                     uint32_t now)
{
	struct cwp_slot *free_slot = NULL, *victim = NULL;
	int i;
	uint32_t oldest = 0;

	for (i = 0; i < CWP_SLOTS; i++) {
		struct cwp_slot *s = &t->s[i];
		uint32_t age;

		if (!s->used) {
			if (!free_slot) free_slot = s;
			continue;
		}
		if (s->srctype == srctype && s->srcid == srcid) {
			s->last = now;
			return s;
		}
		/*
		 * The LARGEST age wins, and `oldest` starts at 0 rather than at
		 * UINT32_MAX. The first version started at the maximum and
		 * compared the age against it, so no slot ever qualified, no
		 * victim was ever chosen and a full table quietly stopped
		 * tracking every new source -- the unit suite's "past CWP_SLOTS
		 * sources" case is what found it. Starting at 0 also means a
		 * victim is always chosen when the table is full, whatever the
		 * clock did in the meantime.
		 */
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
 * Score one delivered key for one source.
 *
 * Returns the verdict mask; `out->fresh` holds the subset worth a log line and a
 * score change. A source that damages 100 % of its keys gets its pattern
 * announced once per CWP_WINDOW, not once per key -- the counters keep counting
 * while the line stays quiet, so the stats line still shows the true rate.
 */
__attribute__((unused))
static unsigned cwp_seen(struct cwp_table *t, int srctype, int srcid,
                         const uint8_t *cw, uint16_t caid, uint32_t provid,
                         uint16_t sid, uint32_t now,
                         struct cwp_evidence *out)
{
	struct cwp_slot *s;
	unsigned m;
	int groups = 0, bad = 0;

	if (out) memset(out, 0, sizeof(*out));
	if (!t || !cw) return CWP_NONE;

	m = cwp_scan(cw, &groups, &bad);

	s = cwp_slot_for(t, srctype, srcid, now);
	if (!s) return CWP_NONE;          /* table full and nothing evictable */

	/* The window rolls first, so this key is counted in the window it lands in. */
	if ((uint32_t)(now - s->win_start) >= CWP_WINDOW) {
		s->win_start = now;
		s->win_all = 0;
		s->win_one = 0;
		s->win_group = 0;
		s->win_mixed = 0;
	}

	s->win_all++;
	s->last = now;

	if (m == CWP_NONE)      s->seen_full++;
	else if (m & CWP_ONE_GROUP) {
		s->seen_one++;
		s->win_one++;
		if (!s->win_group)          s->win_group = (uint32_t)bad;
		else if (s->win_group != (uint32_t)bad) s->win_mixed = 1;
	}
	else                    s->seen_weak++;

	/*
	 * The pattern. All four clauses are required, and the third is computed
	 * with the integer arithmetic that makes it exact: `win_one * 100 >=
	 * win_all * CWP_RATE_PCT` never rounds in the source's favour.
	 */
	if ((m & CWP_ONE_GROUP) && !s->win_mixed &&
	    s->win_one >= CWP_MIN_ONEGROUP &&
	    (uint64_t)s->win_one * 100 >= (uint64_t)s->win_all * CWP_RATE_PCT) {
		if (!s->rep_ticks || (uint32_t)(now - s->rep_ticks) >= CWP_WINDOW) {
			s->rep_ticks = now;
			s->reports++;
			if (out) out->fresh = CWP_ONE_GROUP;
		}
	}

	if (out) {
		out->srctype = srctype;
		out->srcid = srcid;
		out->verdict = m;
		out->groups = groups;
		out->bad_group = bad;
		out->caid = caid;
		out->provid = provid;
		out->sid = sid;
		out->win_all = s->win_all;
		out->win_one = s->win_one;
		out->win_rate = s->win_all ? (s->win_one * 100u) / s->win_all : 0;
		out->seen_full = s->seen_full;
		out->seen_one = s->seen_one;
		out->seen_weak = s->seen_weak;
		out->reports = s->reports;
	}
	return m;
}

__attribute__((unused))
static const char *cwp_name(unsigned mask)
{
	static char buf[96];
	buf[0] = 0;
	if (mask & CWP_ONE_GROUP) strcat(buf, "ONE_GROUP");
	if (mask & CWP_WEAK)      strcat(buf, buf[0] ? "|WEAK" : "WEAK");
	if (!buf[0]) strcat(buf, "NONE");
	return buf;
}

/*
 * The shortest true sentence about a pattern, built from the evidence the
 * verdict was formed on. The wording carries the three numbers that make the
 * claim checkable on the spot: how many keys, in how many, and which group.
 */
__attribute__((unused))
static int cwp_format(const struct cwp_evidence *ev, char *out, int outlen)
{
	if (!out || outlen <= 0) return 0;
	out[0] = 0;
	if (!ev || !(ev->fresh & CWP_ONE_GROUP)) return 0;

	return snprintf(out, (size_t)outlen,
		"CW PLAUSIBILITY: source %d/%d ch %04x:%06x:%04x delivered %u of its last %u key%s with "
		"exactly one of four group sums wrong, always group %d -- %u%% of the window, %d group%s of "
		"%d intact on the last one -- one byte of damage in a key that is otherwise correct is an "
		"edit, not a fault; delivery is unchanged and this is a trust signal only",
		ev->srctype, ev->srcid, ev->caid, ev->provid, ev->sid,
		ev->win_one, ev->win_all, (ev->win_all == 1) ? "" : "s",
		ev->bad_group, ev->win_rate,
		ev->groups, (ev->groups == 1) ? "" : "s", CWP_GROUPS);
}

#endif /* MCS_CWPLAUS_H */
