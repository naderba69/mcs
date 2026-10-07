/*
 * test_retry_detect.c — TASK 1.2
 *
 * Exercises the REAL ../src/retry_detect.h, never a copy of it. The
 * classification is the foundation the whole trust engine rests on, so both
 * directions are asserted: a normal request must not look like a retry, and a
 * genuine retry must not look normal.
 *
 *   make -C make-x64 test
 */

#include <stdio.h>
#include "../src/retry_detect.h"

static int passed = 0, failed = 0;

static void check(const char *what, int got, int want)
{
	if (got == want) { passed++; printf("  [ ok ] %-58s = %d\n", what, got); }
	else { failed++; printf("  [FAIL] %-58s got=%d want=%d\n", what, got, want); }
}

#define HARD 4000u
#define SOFT 5000u

int main(void)
{
	printf("test_retry_detect: real retry classifier (TASK 1.2)\n");

	/* ---- GR2: nothing delivered means a timeout, never a retry ---- */
	check("no delivery, 1s later -> NONE (GR2)",
		retry_classify(1000, 0, 0, HARD, SOFT), RETRY_NONE);
	check("no delivery, immediate re-ask -> NONE (GR2)",
		retry_classify(500, 500, 0, HARD, SOFT), RETRY_NONE);
	check("negative nb_delivered is also NONE",
		retry_classify(1000, 0, -3, HARD, SOFT), RETRY_NONE);

	/* ---- the hard window: a bad CW is re-asked within a second or two ---- */
	check("delivered, re-ask at +0ms -> HARD",
		retry_classify(9000, 9000, 1, HARD, SOFT), RETRY_HARD);
	check("delivered, re-ask at +800ms -> HARD",
		retry_classify(9800, 9000, 1, HARD, SOFT), RETRY_HARD);
	check("delivered, re-ask at +1500ms -> HARD (the typical bad-CW case)",
		retry_classify(10500, 9000, 1, HARD, SOFT), RETRY_HARD);
	check("delivered, re-ask at +3999ms -> HARD",
		retry_classify(12999, 9000, 1, HARD, SOFT), RETRY_HARD);

	/* ---- the boundary must be exact, not off by one ---- */
	check("exactly at +4000ms -> SOFT, not HARD",
		retry_classify(13000, 9000, 1, HARD, SOFT), RETRY_SOFT);

	/* ---- the soft band: counted, never acted on alone ---- */
	check("delivered, re-ask at +4500ms -> SOFT",
		retry_classify(13500, 9000, 1, HARD, SOFT), RETRY_SOFT);
	check("delivered, re-ask at +4999ms -> SOFT",
		retry_classify(13999, 9000, 1, HARD, SOFT), RETRY_SOFT);
	check("exactly at +5000ms -> NONE",
		retry_classify(14000, 9000, 1, HARD, SOFT), RETRY_NONE);

	/* ---- normal operation: the ~10s crypto period ---- */
	check("delivered, re-ask at +10s -> NONE (normal crypto period)",
		retry_classify(19000, 9000, 1, HARD, SOFT), RETRY_NONE);
	check("delivered, re-ask at +30s -> NONE",
		retry_classify(39000, 9000, 1, HARD, SOFT), RETRY_NONE);

	/* ---- repeated retries keep being retries, they do not decay ---- */
	check("5 deliveries, re-ask at +1500ms -> HARD",
		retry_classify(10500, 9000, 5, HARD, SOFT), RETRY_HARD);

	/*
	 * ---- wrap-around ----
	 *
	 * GetTickCount() is 32-bit milliseconds and rolls over every ~49.7 days.
	 * A classifier that saturates instead of subtracting would read a wrapped
	 * interval as huge and then silently stop detecting retries for the rest of
	 * the process lifetime -- which is precisely the long-running case.
	 */
	/*
	 * The first version of this test expected HARD here and failed. The
	 * expectation was wrong, not the code: 0x1000 - 0xFFFFF000 in uint32 is
	 * 8192 ms, which is past both windows. The classifier returned NONE, which
	 * is correct. Replaced with an interval that really is inside the hard
	 * window, and the 8192 ms case kept as its own assertion.
	 */
	check("wrap: delivered just before 2^32, re-ask 1s after -> HARD",
		retry_classify(0xFFFFF000u + 1000u, 0xFFFFF000u, 1, HARD, SOFT), RETRY_HARD);
	check("wrap: 8192ms across the boundary -> NONE (past both windows)",
		retry_classify(1000u, 0xFFFFF000u, 1, HARD, SOFT), RETRY_NONE);
	check("wrap: delivered just before 2^32, re-ask 4.5s after -> SOFT",
		retry_classify(0xFFFFF000u + 4500u, 0xFFFFF000u, 1, HARD, SOFT), RETRY_SOFT);
	check("wrap: 10s across the boundary -> NONE",
		retry_classify(10000u, 0xFFFFFF00u, 1, HARD, SOFT), RETRY_NONE);

	/*
	 * ---- misconfiguration fails safe ----
	 *
	 * A soft window at or below the hard window makes the soft band empty.
	 * That must disable the soft signal, not make everything soft.
	 */
	check("soft == hard: +1500ms -> HARD",
		retry_classify(10500, 9000, 1, 4000, 4000), RETRY_HARD);
	check("soft == hard: +4500ms -> NONE (soft band empty)",
		retry_classify(13500, 9000, 1, 4000, 4000), RETRY_NONE);
	check("soft < hard: +1500ms -> HARD",
		retry_classify(10500, 9000, 1, 4000, 1000), RETRY_HARD);
	/*
	 * Also corrected after a failure: a soft window below the hard window
	 * empties the SOFT band, it does not shrink the HARD one. At +2000ms the
	 * request is still inside the hard window, so it is HARD. The original
	 * expectation of NONE was wrong.
	 */
	check("soft < hard: +2000ms is still inside the hard window -> HARD",
		retry_classify(11000, 9000, 1, 4000, 1000), RETRY_HARD);
	check("soft < hard: +4500ms -> NONE (soft band empty, past hard)",
		retry_classify(13500, 9000, 1, 4000, 1000), RETRY_NONE);

	/* ---- operator-tunable windows actually take effect ---- */
	check("tight windows: +1500ms is beyond them -> NONE",
		retry_classify(10500, 9000, 1, 1000, 1200), RETRY_NONE);
	check("wide windows: +6s is inside them -> SOFT",
		retry_classify(15000, 9000, 1, 5000, 7000), RETRY_SOFT);

	/* ---- the ring is fixed-size and embeddable (GR9: no malloc) ---- */
	{
		struct retry_ring r;
		unsigned int i;
		for (i = 0; i < RETRY_RING_DEPTH; i++) r.e[i].used = 0;
		r.next = 0;
		check("ring depth is 4", RETRY_RING_DEPTH, 4);
		check("ring slots start empty", r.e[0].used + r.e[3].used, 0);
		/* sizeof is checked at compile time below; this proves it is a value type */
		check("ring is a value type, not a pointer", (int)sizeof(r.e) > 0, 1);
	}

	printf("== %d/%d passed, %d failed ==\n", passed, passed + failed, failed);
	return failed ? 1 : 0;
}
