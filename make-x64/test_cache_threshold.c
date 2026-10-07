/*
 * test_cache_threshold.c — TASK 1.11a
 *
 * Exercises the REAL ../src/cache_threshold.h, and also re-implements the r82a
 * comparison so the two can be diffed. That is deliberate: the point of the fix
 * is that the old and new behaviour differ in exactly one situation and agree
 * everywhere else, and a test that only checks the new code cannot show that.
 *
 *   make -C make-x64 test
 */

#include <stdio.h>
#include "../src/cache_threshold.h"

static int passed = 0, failed = 0;

static void check(const char *what, int got, int want)
{
	if (got == want) { passed++; printf("  [ ok ] %-56s = %d\n", what, got); }
	else { failed++; printf("  [FAIL] %-56s got=%d want=%d\n", what, got, want); }
}

/*
 * The r82a comparison, reproduced verbatim from the line this task replaced:
 *     if (cwdata->nbpeers != cfg.cache.threshold) return DCW_ERROR | DCW_SKIP;
 * i.e. accept only on an exact match.
 */
static int r82a_threshold_reached(int nbpeers, int threshold)
{
	return nbpeers == threshold;
}

int main(void)
{
	printf("test_cache_threshold: real min-peer floor (TASK 1.11a)\n");

	/* ---- threshold 1: the shipped default. Old and new must agree. ---- */
	check("threshold 1, 1 peer -> accept",   cache_threshold_reached(1, 1), 1);
	check("threshold 1, 2 peers -> accept",  cache_threshold_reached(2, 1), 1);
	check("threshold 1, 5 peers -> accept",  cache_threshold_reached(5, 1), 1);
	check("r82a agrees at threshold 1",      r82a_threshold_reached(1, 1), 1);

	/* ---- the bug: threshold 2 accepted only the exact second peer ---- */
	check("threshold 2, 1 peer -> hold",     cache_threshold_reached(1, 2), 0);
	check("threshold 2, 2 peers -> accept",  cache_threshold_reached(2, 2), 1);
	check("threshold 2, 3 peers -> STILL accept (r82a rejected here)",
		cache_threshold_reached(3, 2), 1);
	check("r82a rejected the 3rd peer",      r82a_threshold_reached(3, 2), 0);

	check("threshold 3, 2 peers -> hold",    cache_threshold_reached(2, 3), 0);
	check("threshold 3, 3 peers -> accept",  cache_threshold_reached(3, 3), 1);
	check("threshold 3, 9 peers -> accept",  cache_threshold_reached(9, 3), 1);
	check("r82a rejected the 9th peer",      r82a_threshold_reached(9, 3), 0);

	/* ---- the floor holds at the configured maximum ---- */
	check("threshold 30, 29 peers -> hold",  cache_threshold_reached(29, 30), 0);
	check("threshold 30, 30 peers -> accept", cache_threshold_reached(30, 30), 1);
	check("threshold 30, 31 peers -> accept", cache_threshold_reached(31, 30), 1);

	/*
	 * ---- a CW can never be held forever ----
	 *
	 * Whatever the threshold, supplying one more peer must eventually accept.
	 * This is the property `!=` broke: past the threshold, more agreement made
	 * the decision worse, which is backwards for a consensus check.
	 */
	{
		int t, ok = 1;
		for (t = 1; t <= 30; t++) {
			int n;
			for (n = t; n <= t + 5; n++)
				if (!cache_threshold_reached(n, t)) ok = 0;
			if (cache_threshold_reached(t - 1, t)) ok = 0;  /* must hold below */
		}
		check("every threshold in [1,30] accepts at and above the floor", ok, 1);
	}

	/*
	 * ---- defensive: the predicate stays total outside the configured range ----
	 *
	 * config.c clamps the option to [1,30], so 0 and negatives cannot occur in a
	 * running server. They are pinned anyway so a future caller cannot turn a
	 * threshold of 0 into "accept a CW nobody has vouched for".
	 */
	check("threshold 0 is treated as 1 (hold at 0 peers)", cache_threshold_reached(0, 0), 0);
	check("threshold 0 is treated as 1 (accept at 1 peer)", cache_threshold_reached(1, 0), 1);
	check("negative threshold is treated as 1",             cache_threshold_reached(1, -5), 1);
	check("0 peers is never enough",                        cache_threshold_reached(0, 1), 0);

	/*
	 * ---- monotonicity: agreement never makes the answer worse ----
	 *
	 * The single sentence that describes the whole defect. `!=` fails this.
	 */
	{
		int monotone = 1, r82a_monotone = 1, n;
		for (n = 1; n < 40; n++) {
			if (cache_threshold_reached(n, 4) && !cache_threshold_reached(n + 1, 4)) monotone = 0;
			if (r82a_threshold_reached(n, 4) && !r82a_threshold_reached(n + 1, 4)) r82a_monotone = 0;
		}
		check("new comparison is monotone in peer count",    monotone, 1);
		check("r82a comparison was NOT monotone (the bug)",  r82a_monotone, 0);
	}

	printf("== %d/%d passed, %d failed ==\n", passed, passed + failed, failed);
	return failed ? 1 : 0;
}
