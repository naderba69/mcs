/*
 * dcwstruct.h -- TASK 1.8: structural pre-filters as SOFT signals.
 *
 * WHAT THIS IS NOT
 * ----------------
 * It is not another filter. Nothing here rejects a control word, and nothing
 * here changes which key reaches a client. `acceptDCW()` in dcw.c still decides
 * that, exactly as before, with the same three tests and the same two runtime
 * gates from TASK 1.10b.
 *
 * WHAT IT IS
 * ----------
 * A cheap structural scan of a key at the moment of delivery, whose only output
 * is evidence. Until now a structurally absurd key and a genuine one that
 * happened to fail a checksum were equally invisible: `acceptDCW()` returns a
 * bare 0, and the dcwstats counters (Phase 4) say how many keys were dropped
 * for each reason but not *who* kept sending them. So a peer that pushes
 * garbage all day is scored exactly like a peer that has never pushed a bad
 * key, and the client just sees a timeout.
 *
 * The scan closes that. Its result is recorded as ONE `TRUST_EV_SOFT` per
 * anomalous delivery, attributed to the source that supplied the key (GR1), so
 * the mechanisms already shipped decide what it means: TASK 1.6 re-routes the
 * next request away from that source, and TASK 1.7 -- if the operator turns it
 * on -- stops letting it answer a channel change sight-unseen. A soft event is
 * never hard action (GR3): one event moves a score from 60 to 35, still trusted,
 * and the floor is 10, never 0.
 *
 * WHY THESE TESTS AND NOT OTHERS
 * ------------------------------
 * A structural signal is only usable if a genuine key can essentially never
 * trigger it. A test that fires even rarely on real keys becomes a systematic
 * false positive, which is worse than a sporadic one because it never recovers:
 * the source sits at the floor forever and the operator learns to ignore the
 * whole layer. On a uniformly random 16-byte key the probabilities here are:
 *
 *   DCWS_SAME    all eight bytes of a half equal      1.4e-17 per half
 *   DCWS_MONO    a half is a constant non-zero step    3.5e-15 per half
 *   DCWS_LOWENT  a half uses exactly two values        4.5e-13 per half
 *   DCWS_HALVES  the two halves are byte-identical     5.4e-20
 *   DCWS_INV     the second half is the complement     5.4e-20
 *
 * Those are counted out, not estimated. Each is (number of 8-byte patterns with
 * the property) / 256^8: 256 for SAME; 256 x 255 for MONO, since the step is
 * non-zero; (256 x 255 / 2) x (2^8 - 2) for LOWENT, choosing two values and
 * then the arrangements that use both; and 1 for the two cross-half tests. An
 * earlier draft of this table had the two cross-half figures a thousand times
 * too likely, which would have made a test that fires once in 10^19 look like
 * one that fires once in 10^17 -- the same class of mistake as an invented
 * constant, and just as invisible in the code that uses it.
 *
 * The two remaining bits are not probability arguments at all, and that is why
 * they are gated:
 *
 *   DCWS_CHECKSUM  only counted while `dcw_filter_checksum` is on. An operator
 *                  who turned that filter off did so because their bouquet does
 *                  not carry the standard checksum, so a failure there carries
 *                  no information and counting it would penalise a good source
 *                  on every single key.
 *   DCWS_REPEAT    same reasoning against `dcw_filter_repeat`.
 *
 * If the operator has disabled a filter, its verdict is not evidence. That rule
 * is the whole reason this layer can be left permanently on.
 *
 * NDS HALF-KEYS
 * -------------
 * A caid-9 (NDS) key legitimately carries an all-zero half; `ecm_setdcwdata()`
 * even has a dedicated path for it. Every per-half test above would fire on
 * that half, on every key, for every NDS bouquet. So the per-half tests are
 * skipped when a half is all zero. The cross-half tests are kept: an all-zero
 * half whose partner is all-0xFF is not an NDS key, it is a forgery.
 *
 * GR9: a handful of short loops over 16 bytes. No allocation, no I/O, no
 * growth, and the common case (a clean key) costs one pass and returns 0.
 */

#ifndef MCS_DCWSTRUCT_H
#define MCS_DCWSTRUCT_H

/*
 * Requires the two upstream predicates to be declared: `#include "dcw.h"`
 * before this header. main.c does, and test_dcwstruct.c includes dcw.c, which
 * provides them. They are called, never reimplemented -- a second copy of a
 * checksum rule is a second thing to keep in step.
 */

#include <stdint.h>
#include <string.h>

#define DCWS_CHECKSUM  0x01   /* checksumDCW() failed, and the filter is on   */
#define DCWS_REPEAT    0x02   /* isbadDCW() fired, and the filter is on       */
#define DCWS_SAME      0x04   /* a half is one repeated byte                  */
#define DCWS_MONO      0x08   /* a half is a constant non-zero step           */
#define DCWS_LOWENT    0x10   /* a half uses exactly two distinct values      */
#define DCWS_HALVES    0x20   /* both halves identical                        */
#define DCWS_INV       0x40   /* second half is the bitwise inverse of first  */
#define DCWS_BITS      7

/*
 * Which bits are allowed to move a trust score.
 *
 * DCWS_REPEAT is deliberately NOT in here, and this is the single most
 * important line in the file. `isbadDCW()` fires on a uniformly random key with
 * probability about 1 in 16 000 -- which is why a runtime gate for it had to be
 * added in TASK 1.10b in the first place. A busy cache peer pushes tens of
 * thousands of keys an hour, so on a *perfectly healthy* peer that test alone
 * would produce several events every hour and walk the score down to the floor
 * overnight. The source would then be re-routed on the strength of its own good
 * behaviour, with the operator staring at a log full of structural complaints
 * about keys that are in fact fine. That is precisely the systematic false
 * positive GR3 exists to prevent, and "it is only soft" does not rescue it: a
 * floor-level score stops a source being served at all.
 *
 * It is still counted (dcwstruct_hits), because knowing the rate is useful, and
 * it is still logged. It just never becomes evidence.
 */
#define DCWS_SCOREMASK (DCWS_CHECKSUM | DCWS_SAME | DCWS_MONO | DCWS_LOWENT | \
                        DCWS_HALVES | DCWS_INV)

/*
 * Per-bit counters plus one for "deliveries with at least one bit".
 *
 * Written on the setdcw thread and read by the HTTP thread with no lock, which
 * can show a stale value -- the same discipline dcwstats and the existing cache
 * counters use, and harmless for counters whose only job is to make the layer
 * visible. They are the operator's defence against a test that turns out to
 * fire on their bouquet: if one counter climbs while the picture is fine, that
 * test is wrong for them and the reason is on the screen.
 */
extern unsigned long dcwstruct_hits[DCWS_BITS];
extern unsigned long dcwstruct_anomalous;

/* ------------------------------------------------------------------------ */
/* Repeat suppression                                                        */
/* ------------------------------------------------------------------------ */

/*
 * Why this exists.
 *
 * The scan runs once per delivery, and the same key is delivered more than
 * once. A cache peer re-pushes the key it holds every time it is asked, and a
 * client on a stuck channel re-asks every few seconds; both re-deliver the
 * identical 16 bytes. Every one of those deliveries is a separate call into the
 * scan, and without this table each would produce another log line and another
 * soft event -- for one bad key, observed once.
 *
 * Scoring it repeatedly is not merely noisy, it is wrong: "this source produced
 * a structurally impossible key" is a fact about the source, established by the
 * first delivery. Re-reading the same key is not new evidence. The trust
 * engine's asymptotic decay already refuses to walk a score to the floor on
 * repeated soft events, but `nsoft` (Phase 2's forensics) would report a
 * thousand events where one thing happened, and the log would scroll.
 *
 * So: an identical (source, channel, key) is reported once, and thereafter only
 * once per DCWSTRUCT_WINDOW. A key still arriving an hour later is still worth
 * saying out loud -- that is a source that has not recovered -- but saying it
 * every two seconds is not.
 *
 * Bounded: DCWSTRUCT_SLOTS entries of 16 bytes plus context, in .bss, evicted
 * longest-unseen. No allocation, so GR9 holds. The table is small on purpose:
 * it is a de-duplicator for events that are supposed to be rare, not a log
 * buffer. When it overflows it forgets the oldest anomaly, which at worst means
 * reporting one again -- never failing to report.
 */
/*
 * What dcwstruct_seen_once() returns. Both non-zero values mean "report this
 * now"; the difference is only whether the operator is being told something
 * new or being reminded of something they were told at least DCWSTRUCT_WINDOW
 * ago, which the log line says out loud.
 */
#define DCWSTRUCT_SUPPRESS    0   /* a repeat, inside the window: say nothing */
#define DCWSTRUCT_REPORT_NEW  1   /* first sighting of this key             */
#define DCWSTRUCT_REPORT_AGAIN 2  /* same key, window expired: report again  */

#define DCWSTRUCT_SLOTS  8
#define DCWSTRUCT_WINDOW 60000u   /* ms; a repeat inside this is suppressed */

struct dcwstruct_entry {
	int      used;
	int      srctype;
	int      srcid;
	uint16_t caid;
	uint16_t sid;
	uint32_t provid;
	unsigned mask;
	uint8_t  key[16];
	uint32_t ticks;
	unsigned long nrep;   /* how many repeats were suppressed, for the log */
};

struct dcwstruct_table {
	struct dcwstruct_entry e[DCWSTRUCT_SLOTS];
	unsigned long suppressed;   /* total suppressed repeats, all slots */
};

__attribute__((unused))
static void dcwstruct_init(struct dcwstruct_table *t)
{
	int i;
	if (!t) return;
	for (i = 0; i < DCWSTRUCT_SLOTS; i++) {
		memset(&t->e[i], 0, sizeof(t->e[i]));
		t->e[i].srctype = -1;
	}
	t->suppressed = 0;
}

/*
 * Decide whether this delivery is worth reporting.
 *
 * Returns 1 to report (and records the sighting), 0 if it is a repeat of
 * something already reported inside the window. On an eviction the victim is
 * the longest-unseen entry, the same policy trust.h and cache_purge.h use.
 *
 * `mask` is part of the identity, not just the key: if the operator flips
 * DCWFILTER CHECKSUM over SIGHUP, the same key can start or stop failing the
 * checksum test, and that change of verdict is a new fact worth reporting.
 */
__attribute__((unused))
static int dcwstruct_seen_once(struct dcwstruct_table *t,
                               unsigned mask,
                               int srctype, int srcid,
                               uint16_t caid, uint32_t provid, uint16_t sid,
                               const uint8_t *key, uint32_t ticks_now)
{
	int i, freepos = -1, victim = -1;
	uint32_t oldest = 0xFFFFFFFFu;

	if (!t) return DCWSTRUCT_REPORT_NEW;

	for (i = 0; i < DCWSTRUCT_SLOTS; i++) {
		struct dcwstruct_entry *e = &t->e[i];
		if (!e->used) { if (freepos < 0) freepos = i; continue; }
		if (e->srctype == srctype && e->srcid == srcid &&
		    e->caid == caid && e->sid == sid && e->provid == provid &&
		    e->mask == mask && !memcmp(e->key, key, 16)) {
			if ((uint32_t)(ticks_now - e->ticks) < DCWSTRUCT_WINDOW) {
				/*
				 * Counted, but the timestamp is deliberately NOT moved.
				 *
				 * The window measures from the last REPORT, not from the last
				 * sighting. Sliding it would mean a source pushing the same
				 * impossible key every two seconds is mentioned once and never
				 * again -- and "still pushing it an hour later" is exactly the
				 * thing an operator needs to be told, because it is the
				 * difference between a source that glitched and a source that
				 * is broken. Fixing the window bounds the noise instead: one
				 * line per key per minute, per slot.
				 */
				e->nrep++;
				t->suppressed++;
				return DCWSTRUCT_SUPPRESS;
			}
			/* Outside the window: report again, quietly reset the count. */
			e->nrep = 0;
			e->ticks = ticks_now;
			return DCWSTRUCT_REPORT_AGAIN;
		}
		if (e->ticks < oldest) { oldest = e->ticks; victim = i; }
	}

	if (freepos >= 0) {
		struct dcwstruct_entry *e = &t->e[freepos];
		memset(e, 0, sizeof(*e));
		e->used = 1; e->srctype = srctype; e->srcid = srcid;
		e->caid = caid; e->sid = sid; e->provid = provid; e->mask = mask;
		memcpy(e->key, key, 16); e->ticks = ticks_now;
		return DCWSTRUCT_REPORT_NEW;
	}

	if (victim >= 0) {
		struct dcwstruct_entry *e = &t->e[victim];
		memset(e, 0, sizeof(*e));
		e->used = 1; e->srctype = srctype; e->srcid = srcid;
		e->caid = caid; e->sid = sid; e->provid = provid; e->mask = mask;
		memcpy(e->key, key, 16); e->ticks = ticks_now;
		return DCWSTRUCT_REPORT_NEW;
	}

	return DCWSTRUCT_REPORT_NEW;   /* table unusable; never swallow evidence because of a bug here */
}

/* One half, eight bytes: every byte the same. */
static inline int dcwstruct_half_same(const uint8_t *h)
{
	int i;
	for (i = 1; i < 8; i++) if (h[i] != h[0]) return 0;
	return 1;
}

/*
 * One half: a constant non-zero arithmetic step, wrapping mod 256.
 *
 * Step 0 is excluded on purpose -- that is DCWS_SAME, and reporting it under
 * two bits would double-count the same observation.
 */
static inline int dcwstruct_half_mono(const uint8_t *h)
{
	uint8_t d = (uint8_t)(h[1] - h[0]);
	int i;
	if (d == 0) return 0;
	for (i = 1; i < 8; i++)
		if ((uint8_t)(h[i] - h[i - 1]) != d) return 0;
	return 1;
}

/*
 * One half: exactly two distinct byte values.
 *
 * "Exactly" matters. Returning true for one distinct value as well would make
 * this bit a superset of DCWS_SAME, and the two counters would stop being
 * independently readable.
 */
static inline int dcwstruct_half_lowent(const uint8_t *h)
{
	uint8_t a = h[0], b = 0;
	int haveb = 0, i;
	for (i = 1; i < 8; i++) {
		if (h[i] == a) continue;
		if (haveb) { if (h[i] == b) continue; return 0; }
		b = h[i]; haveb = 1;
	}
	return haveb;
}

/*
 * The whole scan. Returns a bitmask, 0 for a clean key.
 *
 * `use_checksum` and `use_repeat` are the caller's copies of
 * `dcw_filter_checksum` and `dcw_filter_repeat`. They are parameters rather
 * than globals read directly so that this file stays a pure function of its
 * arguments and the test can exercise both settings without touching dcw.c's
 * state.
 */
static inline unsigned dcwstruct_scan(const uint8_t *cw, int use_checksum, int use_repeat)
{
	unsigned m = 0;
	int a_null = 1, b_null = 1, i;

	if (use_checksum && !checksumDCW((uint8_t *)cw)) m |= DCWS_CHECKSUM;
	if (use_repeat   && isbadDCW((uint8_t *)cw))     m |= DCWS_REPEAT;

	for (i = 0; i < 8; i++) {
		if (cw[i])     a_null = 0;
		if (cw[8 + i]) b_null = 0;
	}

	/* Cross-half tests. Meaningful even with a zero half. */
	{
		int same = 1, inv = 1;
		for (i = 0; i < 8; i++) {
			if (cw[8 + i] != cw[i]) same = 0;
			if (cw[8 + i] != (uint8_t)~cw[i]) inv = 0;
		}
		if (same) m |= DCWS_HALVES;
		if (inv)  m |= DCWS_INV;
	}

	/* Per-half tests, skipped for a legitimately zeroed NDS half. */
	if (!a_null) {
		if (dcwstruct_half_same(cw))       m |= DCWS_SAME;
		if (dcwstruct_half_mono(cw))       m |= DCWS_MONO;
		if (dcwstruct_half_lowent(cw))     m |= DCWS_LOWENT;
	}
	if (!b_null) {
		if (dcwstruct_half_same(cw + 8))   m |= DCWS_SAME;
		if (dcwstruct_half_mono(cw + 8))   m |= DCWS_MONO;
		if (dcwstruct_half_lowent(cw + 8)) m |= DCWS_LOWENT;
	}

	return m;
}

/*
 * Render a mask as a short token list for the log line, e.g. "MONO+LOWENT".
 * `out` must hold at least 44 bytes; callers pass 64. The longest possible
 * string is CHECKSUM+REPEAT+SAME+MONO+LOWENT+HALVES+INV -- 37 characters of
 * names plus 6 separators -- so 43 plus the terminator. The bound is enforced
 * on every write anyway, because an off-by-one in a diagnostic string is not
 * worth finding in production.
 */
static inline void dcwstruct_name(unsigned m, char *out, int outlen)
{
	static const char *const nm[DCWS_BITS] = {
		"CHECKSUM", "REPEAT", "SAME", "MONO", "LOWENT", "HALVES", "INV"
	};
	int i, off = 0;
	out[0] = 0;
	for (i = 0; i < DCWS_BITS; i++) {
		if (!(m & (1u << i))) continue;
		if (off > 0 && off < outlen - 1) out[off++] = '+';
		{
			const char *s = nm[i];
			while (*s && off < outlen - 1) out[off++] = *s++;
		}
	}
	out[off] = 0;
}

#endif /* MCS_DCWSTRUCT_H */
