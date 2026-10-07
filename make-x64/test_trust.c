/*
 * test_trust.c — TASK 1.4
 *
 * Exercises the REAL ../src/trust.h. The numbers asserted here were computed
 * from the constants in that header before the test was written, so a change to
 * the decay constants fails this file rather than quietly changing behaviour.
 *
 * The two properties the brief demands — asymmetric decay, and floor/ceiling —
 * each get their own section, plus the GR3 and GR4 guarantees.
 *
 *   make -C make-x64 test
 */

#include <stdio.h>
#include "../src/trust.h"

static int passed = 0, failed = 0;

static void check(const char *what, int got, int want)
{
	if (got == want) { passed++; printf("  [ ok ] %-64s = %d\n", what, got); }
	else { failed++; printf("  [FAIL] %-64s got=%d want=%d\n", what, got, want); }
}

#define SRCTYPE  1                 /* DCW_SOURCE_CACHE */
#define SRCID    (7 | 0x010000)    /* peer 7, PEER_CSP flag set */
#define CAID     0x1884
#define PROV     0x000000
#define SID      100

int main(void)
{
	struct trust_table t;
	int s, i;

	printf("test_trust: real trust engine (TASK 1.4)\n");
	printf("  floor=%d start=%d actionable=%d ceiling=%d; table %d x %u bytes\n",
		TRUST_FLOOR, TRUST_START, TRUST_ACTIONABLE, TRUST_CEILING,
		TRUST_TABLE_SIZE, (unsigned)sizeof(struct trust_entry));

	/* ---- the constants the rest of this file assumes ---- */
	check("floor", TRUST_FLOOR, 10);
	check("start", TRUST_START, 60);
	check("ceiling", TRUST_CEILING, 100);
	check("actionable threshold", TRUST_ACTIONABLE, 25);

	/* ---- a source nobody has seen is unknown, not bad ---- */
	trust_reset(&t);
	check("unseen source -> -1 (unknown, must not start distrusted)",
		trust_score(&t, SRCTYPE, SRCID, CAID, PROV, SID), -1);
	check("asking does not create an entry",
		trust_score(&t, SRCTYPE, SRCID, CAID, PROV, SID), -1);

	/* ---- first event creates the entry at TRUST_START ---- */
	trust_reset(&t);
	check("first event creates the entry at TRUST_START",
		trust_record(&t, TRUST_EV_NONE, SRCTYPE, SRCID, CAID, PROV, SID, 1000), TRUST_START);
	check("and it is now known",
		trust_score(&t, SRCTYPE, SRCID, CAID, PROV, SID), TRUST_START);

	/*
	 * ---- GR3: a single soft event must not be actionable ----
	 *
	 * This is the guarantee the whole design is judged on. A false positive
	 * that disables a good card is worse than the black screen being fixed.
	 */
	trust_reset(&t);
	s = trust_record(&t, TRUST_EV_SOFT, SRCTYPE, SRCID, CAID, PROV, SID, 1000);
	check("1 soft event: score", s, 35);
	check("1 soft event: NOT actionable", (s <= TRUST_ACTIONABLE) ? 1 : 0, 0);

	/* ---- soft events accumulate and become decisive ---- */
	trust_reset(&t);
	trust_record(&t, TRUST_EV_SOFT, SRCTYPE, SRCID, CAID, PROV, SID, 1000);
	s = trust_record(&t, TRUST_EV_SOFT, SRCTYPE, SRCID, CAID, PROV, SID, 2000);
	check("2 soft events: score", s, 23);
	check("2 soft events: actionable", (s <= TRUST_ACTIONABLE) ? 1 : 0, 1);
	s = trust_record(&t, TRUST_EV_SOFT, SRCTYPE, SRCID, CAID, PROV, SID, 3000);
	check("3 soft events: score", s, 17);

	/* ---- definitive proof bites immediately ---- */
	trust_reset(&t);
	s = trust_record(&t, TRUST_EV_PROOF, SRCTYPE, SRCID, CAID, PROV, SID, 1000);
	check("1 proof: score", s, 35);
	check("1 proof: not yet actionable", (s <= TRUST_ACTIONABLE) ? 1 : 0, 0);
	s = trust_record(&t, TRUST_EV_PROOF, SRCTYPE, SRCID, CAID, PROV, SID, 2000);
	check("2 proofs: at the floor", s, TRUST_FLOOR);
	check("2 proofs: actionable", (s <= TRUST_ACTIONABLE) ? 1 : 0, 1);
	s = trust_record(&t, TRUST_EV_PROOF, SRCTYPE, SRCID, CAID, PROV, SID, 3000);
	check("3 proofs: still the floor, never below", s, TRUST_FLOOR);

	/* ---- asymmetry: down fast, up slow ---- */
	trust_reset(&t);
	s = trust_record(&t, TRUST_EV_SOFT, SRCTYPE, SRCID, CAID, PROV, SID, 1000);
	s = trust_record(&t, TRUST_EV_SOFT, SRCTYPE, SRCID, CAID, PROV, SID, 2000);
	check("2 soft events took it to 23", s, 23);
	s = trust_record(&t, TRUST_EV_GOOD, SRCTYPE, SRCID, CAID, PROV, SID, 3000);
	check("1 good event moves it only to 25", s, 25);
	check("so recovery is strictly slower than decay", 25 - 23 < 60 - 35, 1);

	/* ---- recovery from the floor, with the exact event counts ---- */
	trust_reset(&t);
	trust_record(&t, TRUST_EV_PROOF, SRCTYPE, SRCID, CAID, PROV, SID, 1000);
	trust_record(&t, TRUST_EV_PROOF, SRCTYPE, SRCID, CAID, PROV, SID, 2000);
	check("at the floor before recovery", trust_score(&t, SRCTYPE, SRCID, CAID, PROV, SID), TRUST_FLOOR);
	{
		int act = -1, start = -1, ceil = -1;
		for (i = 1; i <= 200; i++) {
			s = trust_record(&t, TRUST_EV_GOOD, SRCTYPE, SRCID, CAID, PROV, SID, 2000u + i);
			if (act < 0 && s > TRUST_ACTIONABLE) act = i;
			if (start < 0 && s >= TRUST_START) start = i;
			if (ceil < 0 && s >= TRUST_CEILING) ceil = i;
		}
		check("passes actionable after 8 good results", act, 8);
		check("climbs back to TRUST_START after 36", start, 36);
		check("reaches the ceiling after 76", ceil, 76);
	}

	/*
	 * ---- the ceiling is actually reachable ----
	 *
	 * The first version of the recovery step had no minimum increment, so the
	 * shift hit zero at 69 and every source plateaued there forever: the ceiling
	 * was dead code and a penalised source could never return to where a new one
	 * starts. Pinned here so that cannot come back.
	 */
	check("ceiling was reached, not asymptotic",
		trust_score(&t, SRCTYPE, SRCID, CAID, PROV, SID) >= TRUST_CEILING, 1);

	/* ---- and no source becomes immune ---- */
	check("one proof at the ceiling still bites hard",
		trust_record(&t, TRUST_EV_PROOF, SRCTYPE, SRCID, CAID, PROV, SID, 9000), 75);

	/*
	 * ---- GR4: granularity is per (source, CAID, PROVID, SID) ----
	 *
	 * A card healthy on one bouquet and dead on another must be two scores.
	 */
	trust_reset(&t);
	trust_record(&t, TRUST_EV_PROOF, SRCTYPE, SRCID, CAID, PROV, SID, 1000);
	trust_record(&t, TRUST_EV_PROOF, SRCTYPE, SRCID, CAID, PROV, SID, 2000);
	trust_record(&t, TRUST_EV_NONE, SRCTYPE, SRCID, CAID, PROV, 200, 3000);
	check("same source, SID 100 is at the floor",
		trust_score(&t, SRCTYPE, SRCID, CAID, PROV, SID), TRUST_FLOOR);
	check("same source, SID 200 is untouched",
		trust_score(&t, SRCTYPE, SRCID, CAID, PROV, 200), TRUST_START);
	trust_reset(&t);
	trust_record(&t, TRUST_EV_PROOF, SRCTYPE, SRCID, 0x0900, PROV, SID, 1000);
	trust_record(&t, TRUST_EV_PROOF, SRCTYPE, SRCID, 0x0900, PROV, SID, 2000);
	check("same source and SID, different CAID is a different score",
		trust_score(&t, SRCTYPE, SRCID, CAID, PROV, SID), -1);

	/* ---- a different source is a different score ---- */
	trust_reset(&t);
	trust_record(&t, TRUST_EV_PROOF, SRCTYPE, SRCID, CAID, PROV, SID, 1000);
	trust_record(&t, TRUST_EV_PROOF, SRCTYPE, SRCID, CAID, PROV, SID, 2000);
	check("a different srcid starts fresh",
		trust_score(&t, SRCTYPE, SRCID + 1, CAID, PROV, SID), -1);

	/*
	 * ---- the PEER_* origin flags are part of the identity ----
	 *
	 * Peer 7 as a CSP peer and client 7 as a cache-exchange client must not
	 * share a score. If the flags were masked off here they would.
	 */
	trust_reset(&t);
	trust_record(&t, TRUST_EV_PROOF, SRCTYPE, 7 | 0x010000, CAID, PROV, SID, 1000);
	trust_record(&t, TRUST_EV_PROOF, SRCTYPE, 7 | 0x010000, CAID, PROV, SID, 2000);
	check("flagged srcid 7|PEER_CSP is at the floor",
		trust_score(&t, SRCTYPE, 7 | 0x010000, CAID, PROV, SID), TRUST_FLOOR);
	check("bare srcid 7 is a different identity",
		trust_score(&t, SRCTYPE, 7, CAID, PROV, SID), -1);

	/* ---- proof and soft counters are kept, for Phase 2 ---- */
	trust_reset(&t);
	trust_record(&t, TRUST_EV_PROOF, SRCTYPE, SRCID, CAID, PROV, SID, 1000);
	trust_record(&t, TRUST_EV_SOFT, SRCTYPE, SRCID, CAID, PROV, SID, 2000);
	trust_record(&t, TRUST_EV_SOFT, SRCTYPE, SRCID, CAID, PROV, SID, 3000);
	for (i = 0; i < 50; i++) trust_record(&t, TRUST_EV_GOOD, SRCTYPE, SRCID, CAID, PROV, SID, 4000u + i);
	{
		struct trust_entry *e = trust_find(&t, SRCTYPE, SRCID, CAID, PROV, SID);
		check("nproof survives 50 good results", e ? e->nproof : -1, 1);
		check("nsoft survives 50 good results", e ? e->nsoft : -1, 2);
	}

	/* ---- a full table evicts the longest-unseen, not the worst-scored ---- */
	/*
	 * The first version of this test failed, and the test was wrong rather than
	 * the engine. Its fill loop used `(uint16_t)(1000 + i)` for i in 0..255, which
	 * reaches sid 1100 -- the same SID as the worst-scored source -- so the loop
	 * touched that entry and made it the most recently seen. Asserting it survived
	 * eviction after that proved nothing.
	 *
	 * The real property is that eviction picks the longest-unseen and NOT the
	 * lowest-scored. So: record a badly-scored source that is recent, then fill the
	 * table with 256 entries that are all older and none of which collide with its
	 * key. A lowest-score policy would evict the bad source; a longest-unseen
	 * policy keeps it.
	 */
	trust_reset(&t);
	trust_record(&t, TRUST_EV_PROOF, SRCTYPE, SRCID, CAID, PROV, SID, 5000);
	trust_record(&t, TRUST_EV_PROOF, SRCTYPE, SRCID, CAID, PROV, SID, 5000);
	check("the worst-scored source is recorded first",
		trust_score(&t, SRCTYPE, SRCID, CAID, PROV, SID), TRUST_FLOOR);
	for (i = 0; i < TRUST_TABLE_SIZE; i++)
		trust_record(&t, TRUST_EV_NONE, SRCTYPE, SRCID, CAID, PROV, (uint16_t)(4000 + i), 1000u + i);
	check("eviction is longest-unseen, not lowest-score",
		trust_score(&t, SRCTYPE, SRCID, CAID, PROV, SID), TRUST_FLOOR);

	/* ---- the table is a fixed .bss object, nothing is malloc'd (GR9) ---- */
	check("table depth", TRUST_TABLE_SIZE, 256);
	/*
	 * 36 bytes, not the 24 the header comment originally claimed: 4+4+2+2+4*5
	 * rounds up to a 4-byte multiple. Pinned exactly so a field addition cannot
	 * silently grow the footprint.
	 */
	check("entry is 36 bytes", (int)sizeof(struct trust_entry), 36);
	check("whole table is 9216 bytes of .bss", (int)sizeof(struct trust_table), 9216);

	printf("== %d/%d passed, %d failed ==\n", passed, passed + failed, failed);
	return failed ? 1 : 0;
}
