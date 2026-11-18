#ifndef __CACHE_CYCLE_SHADOW_H
#define __CACHE_CYCLE_SHADOW_H
/*
 * Band 111: the shadow classifier behind the post-consensus gate tally.
 *
 * cache_after_consensus() refuses an UNMARKED key when the filter cannot
 * establish its cycle. cfg.cache.cycle_estimate (opt-in, default OFF)
 * would have estimated it from prevcw instead: exactly one half carried
 * over means the other half rotated, which names the cycle. The shadow
 * counters measure, per CAID, what that estimator WOULD have claimed --
 * without deciding anything.
 *
 * This header is the single definition of that predicate so it cannot
 * drift from the estimator it predicts, and so the argument convention is
 * pinned by a unit test: dcwcmp8() returns TRUE ON EQUALITY (memcmp(...)==0),
 * the opposite of the usual cmp meaning. Getting that backwards silently
 * swaps "no stable half" and "identical to prevcw" -- invisible in a
 * session where every refusal has no prevcw at all.
 *
 * The five classes are a PARTITION of the no-cycle refusals: their sum must
 * equal the "no cycle established" count, and /user-trace prints that
 * self-check so a mis-wired tally cannot masquerade as data.
 */
#define MCS_SHADOW_NOPREV     0   /* channel has no previous CW to compare */
#define MCS_SHADOW_ROT_CW1    1   /* first half stable, second rotated */
#define MCS_SHADOW_ROT_CW0    2   /* second half stable, first rotated */
#define MCS_SHADOW_NEITHER    3   /* both halves changed: no stable half */
#define MCS_SHADOW_BOTHSAME   4   /* both halves unchanged: same key again */

/* have_prev / first_same / second_same are EQUALITY flags (nonzero = true),
 * computed exactly as the estimator computes them. */
static inline int mcs_shadow_class(int have_prev, int first_same,
                                   int second_same)
{
	if (!have_prev) return MCS_SHADOW_NOPREV;
	if (first_same != second_same)
		return first_same ? MCS_SHADOW_ROT_CW1 : MCS_SHADOW_ROT_CW0;
	return first_same ? MCS_SHADOW_BOTHSAME : MCS_SHADOW_NEITHER;
}
#endif
