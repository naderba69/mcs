/*
 * test_cachepref.c -- TASK 1.7
 *
 * Exercises the REAL ../src/cachepref.h, and drives the REAL ../src/trust.h to
 * produce the scores it consumes. That pairing is the point: cachepref.h is
 * three integer comparisons, and a test that fed it hand-picked numbers would
 * prove nothing about whether a real source ever actually reaches the
 * "untrusted" side of the line. So the scores here come from trust_record()
 * events, the same ones srv-newcamd.c and the cache-exchange sites emit.
 *
 *   make -C make-x64 test
 */

#include <stdio.h>
#include <string.h>
#include "../src/trust.h"
#include "../src/cachepref.h"

static int passed = 0, failed = 0;

static void check(const char *what, int got, int want)
{
	if (got == want) { passed++; printf("  [ ok ] %-58s = %d\n", what, got); }
	else { failed++; printf("  [FAIL] %-58s got=%d want=%d\n", what, got, want); }
}

/* One source identity, reused throughout. GR4's five-part key. */
#define SRC_TYPE   1        /* DCW_SOURCE_CACHE */
#define SRC_ID     0x10007  /* PEER_CSP | peer 7 */
#define SRC_CAID   0x1884
#define SRC_PROVID 0x000000
#define SRC_SID    0x00C8

/* A second identity, to show the key really is per-service and not per-peer. */
#define OTHER_SID  0x00C9

int main(void)
{
	struct trust_table t;

	printf("test_cachepref: TRUSTED-CACHE-FIRST and zap preference (TASK 1.7)\n");

	/* ==================================================================
	 * 1. Stock behaviour must be bit-for-bit unchanged when the option is
	 *    off. This is the backward-compatibility guarantee: an existing
	 *    multics.cfg without the line must not have its cache reordered.
	 * ================================================================== */
	check("OFF, untrusted, unverified -> ALLOW (stock)",
	      cachepref_allow(0, 0, 0), CACHEPREF_ALLOW);
	check("OFF, untrusted, verified   -> ALLOW (stock)",
	      cachepref_allow(0, 0, 1), CACHEPREF_ALLOW);
	check("OFF, trusted,   unverified -> ALLOW (stock)",
	      cachepref_allow(0, 1, 0), CACHEPREF_ALLOW);
	check("OFF, trusted,   verified   -> ALLOW (stock)",
	      cachepref_allow(0, 1, 1), CACHEPREF_ALLOW);

	/* ==================================================================
	 * 2. The policy with the option on. Exactly one combination defers.
	 * ================================================================== */
	check("ON, trusted,   unverified -> ALLOW (trusted cache first)",
	      cachepref_allow(1, 1, 0), CACHEPREF_ALLOW);
	check("ON, trusted,   verified   -> ALLOW (trusted cache first)",
	      cachepref_allow(1, 1, 1), CACHEPREF_ALLOW);
	check("ON, untrusted, verified   -> ALLOW (consecutiveness checked it)",
	      cachepref_allow(1, 0, 1), CACHEPREF_ALLOW);
	check("ON, untrusted, unverified -> DEFER (zap: nothing checked it)",
	      cachepref_allow(1, 0, 0), CACHEPREF_DEFER);

	/* ==================================================================
	 * 3. "Unknown" is trusted. GR3: a brand new peer has done nothing to
	 *    earn suspicion, and treating it as a suspect is precisely the
	 *    false positive that is worse than the problem being solved.
	 * ================================================================== */
	check("score -1 (never seen) is trusted",
	      cachepref_trusted(-1, TRUST_ACTIONABLE), 1);
	check("score at the actionable line is trusted",
	      cachepref_trusted(TRUST_ACTIONABLE, TRUST_ACTIONABLE), 1);
	check("score one below the line is NOT trusted",
	      cachepref_trusted(TRUST_ACTIONABLE - 1, TRUST_ACTIONABLE), 0);
	check("score at the floor is NOT trusted",
	      cachepref_trusted(TRUST_FLOOR, TRUST_ACTIONABLE), 0);
	check("score at the ceiling is trusted",
	      cachepref_trusted(TRUST_CEILING, TRUST_ACTIONABLE), 1);
	check("a fresh source starts trusted",
	      cachepref_trusted(TRUST_START, TRUST_ACTIONABLE), 1);

	/* ==================================================================
	 * 4. Composition with the real trust engine. These are the scores an
	 *    actual deployment produces, not numbers chosen to pass.
	 * ================================================================== */
	trust_reset(&t);

	/* 4a. A source never mentioned is trusted, so the cache still leads. */
	check("never-seen source: cache leads on a zap",
	      cachepref_allow(1, cachepref_trusted(
	              trust_score(&t, SRC_TYPE, SRC_ID, SRC_CAID, SRC_PROVID, SRC_SID),
	              TRUST_ACTIONABLE), 0),
	      CACHEPREF_ALLOW);

	/* 4b. One soft event (a single client re-ask) must NOT be enough.
	 *     GR3: no hard action on one soft event. From TRUST_START=60 a
	 *     soft shift of 1 lands on 35, still above the line of 25. */
	trust_record(&t, TRUST_EV_SOFT, SRC_TYPE, SRC_ID, SRC_CAID, SRC_PROVID, SRC_SID, 1000);
	check("after 1 soft event the source is still trusted",
	      cachepref_trusted(trust_score(&t, SRC_TYPE, SRC_ID, SRC_CAID, SRC_PROVID, SRC_SID),
	                        TRUST_ACTIONABLE), 1);
	check("after 1 soft event a zap still leads with the cache",
	      cachepref_allow(1, cachepref_trusted(
	              trust_score(&t, SRC_TYPE, SRC_ID, SRC_CAID, SRC_PROVID, SRC_SID),
	              TRUST_ACTIONABLE), 0),
	      CACHEPREF_ALLOW);

	/* 4c. A second soft event crosses the line: 35 - (35-10)/2 = 23 < 25. */
	trust_record(&t, TRUST_EV_SOFT, SRC_TYPE, SRC_ID, SRC_CAID, SRC_PROVID, SRC_SID, 2000);
	check("after 2 soft events the source is untrusted",
	      cachepref_trusted(trust_score(&t, SRC_TYPE, SRC_ID, SRC_CAID, SRC_PROVID, SRC_SID),
	                        TRUST_ACTIONABLE), 0);
	check("untrusted + zap -> DEFER (the poisoned-instant-entry case)",
	      cachepref_allow(1, cachepref_trusted(
	              trust_score(&t, SRC_TYPE, SRC_ID, SRC_CAID, SRC_PROVID, SRC_SID),
	              TRUST_ACTIONABLE), 0),
	      CACHEPREF_DEFER);
	check("untrusted + verified -> still ALLOW (steady state untouched)",
	      cachepref_allow(1, cachepref_trusted(
	              trust_score(&t, SRC_TYPE, SRC_ID, SRC_CAID, SRC_PROVID, SRC_SID),
	              TRUST_ACTIONABLE), 1),
	      CACHEPREF_ALLOW);

	/* 4d. GR4: the same peer on a different service is a different source.
	 *     The service that was never implicated must keep leading with the
	 *     cache -- otherwise one bad bouquet would poison a whole peer. */
	check("same peer, different SID: still trusted (GR4)",
	      cachepref_trusted(trust_score(&t, SRC_TYPE, SRC_ID, SRC_CAID, SRC_PROVID, OTHER_SID),
	                        TRUST_ACTIONABLE), 1);

	/* 4e. Recovery. GOOD events must be able to bring a source back, or the
	 *     policy would be a one-way ratchet and a source that had two bad
	 *     minutes would never lead with the cache again. */
	{
		int recovered = 0, n;
		for (n = 0; n < 40; n++) {
			trust_record(&t, TRUST_EV_GOOD, SRC_TYPE, SRC_ID, SRC_CAID, SRC_PROVID, SRC_SID, 3000 + n);
			if (cachepref_trusted(trust_score(&t, SRC_TYPE, SRC_ID, SRC_CAID, SRC_PROVID, SRC_SID),
			                      TRUST_ACTIONABLE)) { recovered = n + 1; break; }
		}
		check("GOOD events restore trust within 40 deliveries", recovered > 0, 1);
		check("and a zap leads with the cache again",
		      cachepref_allow(1, cachepref_trusted(
		              trust_score(&t, SRC_TYPE, SRC_ID, SRC_CAID, SRC_PROVID, SRC_SID),
		              TRUST_ACTIONABLE), 0),
		      CACHEPREF_ALLOW);
	}

	/* 4f. Reuse proofs. Pinned to the arithmetic rather than to an assumption:
	 *     PROOF_STEP is 25 and TRUST_START is 60, so ONE proof lands on 35,
	 *     which is still above the actionable line of 25. A first attempt at
	 *     this block asserted the opposite and was wrong.
	 *
	 *     That is deliberate, not an oversight, and the two mechanisms are
	 *     complementary rather than redundant. The specific poisoned entry is
	 *     retired by TASK 1.5's purge mark, which excludes it at all four
	 *     PIPE_CACHE_FIND sites through DCW_ERROR whether or not this option
	 *     is on -- so a proven-bad key never gets served again. What this
	 *     option governs is the *source's* standing on entries that were never
	 *     proven anything, and one proof is not yet a pattern. */
	trust_reset(&t);
	trust_record(&t, TRUST_EV_PROOF, SRC_TYPE, SRC_ID, SRC_CAID, SRC_PROVID, SRC_SID, 5000);
	check("after 1 PROOF the source is still trusted (score 35 > 25)",
	      cachepref_trusted(trust_score(&t, SRC_TYPE, SRC_ID, SRC_CAID, SRC_PROVID, SRC_SID),
	                        TRUST_ACTIONABLE), 1);
	trust_record(&t, TRUST_EV_PROOF, SRC_TYPE, SRC_ID, SRC_CAID, SRC_PROVID, SRC_SID, 6000);
	check("after 2 PROOFs the source is untrusted (score 10)",
	      cachepref_trusted(trust_score(&t, SRC_TYPE, SRC_ID, SRC_CAID, SRC_PROVID, SRC_SID),
	                        TRUST_ACTIONABLE), 0);
	check("2 proofs + zap -> DEFER",
	      cachepref_allow(1, cachepref_trusted(
	              trust_score(&t, SRC_TYPE, SRC_ID, SRC_CAID, SRC_PROVID, SRC_SID),
	              TRUST_ACTIONABLE), 0),
	      CACHEPREF_DEFER);
	check("2 proofs + verified -> still ALLOW (steady state untouched)",
	      cachepref_allow(1, cachepref_trusted(
	              trust_score(&t, SRC_TYPE, SRC_ID, SRC_CAID, SRC_PROVID, SRC_SID),
	              TRUST_ACTIONABLE), 1),
	      CACHEPREF_ALLOW);

	printf("== %d/%d passed, %d failed ==\n", passed, passed + failed, failed);
	return failed ? 1 : 0;
}
