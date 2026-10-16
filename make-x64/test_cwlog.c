/*
 * test_cwlog.c -- TASK R11 (D65), reconstructed and extended for R13 (D68).
 *
 * Exercises the REAL ../src/cwlog.c against the REAL ../src/cwlog.h, and
 * asserts the name tables stay word-for-word aligned with ../src/dcwstats.h
 * (reasons 0..9 are the SAME vocabulary: a ring row and a counter that
 * describe the same event must never disagree about its name).
 *
 * NOTE: cwlog.c has no declaration header of its own -- the server build
 * reaches it through implicit declarations (suppressed in PORTFLAGS). This
 * test compiles with -Wall -Wextra, so the prototypes are declared here.
 *
 *   make -C make-x64 test
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "../src/cwlog.h"
#include "../src/dcwstats.h"

void cwlog_note(int verdict, int reason, int peerid,
		uint32_t caid, uint32_t provid, uint16_t sid,
		const uint8_t *cw, uint32_t tick);
int  cwlog_count(void);
int  cwlog_capacity(void);
int  cwlog_snapshot(struct cwlog_entry *out, int max);
void cwlog_reset(void);

/* The dcwstats table this TU owns for the counter/reset checks below. */
int            dcwstats_on = 0;
unsigned long  dcwstats[DCW_REJ_COUNT];

static int passed = 0, failed = 0;

static void check(const char *what, int got, int want)
{
	if (got == want) { passed++; printf("  [ ok ] %-58s = %d\n", what, got); }
	else { failed++; printf("  [FAIL] %-58s got=%d want=%d\n", what, got, want); }
}

static void checks(const char *what, const char *got, const char *want)
{
	if (got && want && strcmp(got, want) == 0) {
		passed++; printf("  [ ok ] %-58s = %s\n", what, got);
	} else {
		failed++; printf("  [FAIL] %-58s got=%s want=%s\n", what,
		                got ? got : "(null)", want ? want : "(null)");
	}
}

int main(void)
{
	static struct cwlog_entry rows[CWLOG_CAP + 8];
	uint8_t k1[16], k2[16];
	int i, n;

	printf("test_cwlog: the verdict ring and the name tables (R11 + R13)\n");

	/* ==================================================================
	 * 1. Reasons 0..9: the ring and the counters share one vocabulary.
	 * ================================================================== */
	for (i = 0; i <= 9; i++) {
		char what[72];
		snprintf(what, sizeof(what), "reason %d matches the dcwstats name", i);
		checks(what, cwlog_reason_name(i), dcwstats_reason_name(i));
	}

	/* ==================================================================
	 * 2. Verdict names.
	 * ================================================================== */
	checks("verdict 0 is delivered", cwlog_verdict_name(CWLOG_DELIVERED), "delivered");
	checks("verdict 1 is refused",   cwlog_verdict_name(CWLOG_REFUSED),   "refused");
	checks("verdict 2 is held",      cwlog_verdict_name(CWLOG_HELD),      "held");
	checks("verdict 3 is stored",    cwlog_verdict_name(CWLOG_STORED),    "stored");
	checks("verdict 4 is disputed (R13)", cwlog_verdict_name(CWLOG_DISPUTED), "disputed");
	checks("verdict 5 is unknown",   cwlog_verdict_name(5), "unknown");
	checks("verdict -1 is unknown",  cwlog_verdict_name(-1), "unknown");

	/* ==================================================================
	 * 3. Ring-only reason names (10+).
	 * ================================================================== */
	checks("reason 10 is held-for-corroboration",
	       cwlog_reason_name(CWLOG_R_HOLD), "held-for-corroboration");
	checks("reason 11 is convicted-memory",
	       cwlog_reason_name(CWLOG_R_CONVICTED), "convicted-memory");
	checks("reason 12 is below-threshold",
	       cwlog_reason_name(CWLOG_R_THRESHOLD), "below-threshold");
	checks("reason 13 is profile-filter",
	       cwlog_reason_name(CWLOG_R_PROFILE), "profile-filter");
	checks("reason 14 is late-contradiction (R13)",
	       cwlog_reason_name(CWLOG_R_LATEDISPUTE), "late-contradiction");
	checks("reason 15 is late-truth-gate (R13)",
	       cwlog_reason_name(CWLOG_R_LATEKEY), "late-truth-gate");
	checks("reason 16 is trust-store-gate (R14)",
	       cwlog_reason_name(CWLOG_R_TRUSTSTORE), "trust-store-gate");
	checks("reason 17 is trust-relay-gate (R14)",
	       cwlog_reason_name(CWLOG_R_TRUSTRELAY), "trust-relay-gate");
	checks("reason 18 is unknown",
	       cwlog_reason_name(18), "unknown");

	/* ==================================================================
	 * 4. Ring semantics: order, overflow, bounds, reset.
	 * ================================================================== */
	cwlog_reset();
	check("an empty ring counts 0", cwlog_count(), 0);
	check("capacity is 100", cwlog_capacity(), CWLOG_CAP);

	for (i = 0; i < 16; i++) k1[i] = (uint8_t)(0xA0 + i);
	for (i = 0; i < 16; i++) k2[i] = (uint8_t)(0x50 + i);

	cwlog_note(CWLOG_DELIVERED, 0, 7, 0x1884, 0, 0x0064, k1, 1000);
	cwlog_note(CWLOG_DISPUTED, CWLOG_R_LATEDISPUTE, 7, 0x1884, 0, 0x0064, k1, 2000);
	check("two notes count 2", cwlog_count(), 2);

	n = cwlog_snapshot(rows, CWLOG_CAP);
	check("snapshot returns both rows", n, 2);
	check("newest first: row 0 tick", (int)rows[0].tick, 2000);
	check("newest first: row 1 tick", (int)rows[1].tick, 1000);
	check("row 0 verdict is disputed", rows[0].verdict, CWLOG_DISPUTED);
	check("row 0 reason is late-contradiction", rows[0].reason, CWLOG_R_LATEDISPUTE);
	check("row 0 peerid survives", rows[0].peerid, 7);
	check("row 0 key bytes survive", memcmp(rows[0].cw, k1, 16), 0);
	check("row 1 verdict is delivered", rows[1].verdict, CWLOG_DELIVERED);

	/* out-of-range verdicts are dropped, never stored */
	cwlog_note(5, 0, 1, 0x1884, 0, 1, k2, 3000);
	cwlog_note(-1, 0, 1, 0x1884, 0, 1, k2, 3000);
	check("out-of-range verdicts are dropped", cwlog_count(), 2);

	/* NULL key stores 16 zero bytes, not a crash */
	cwlog_note(CWLOG_REFUSED, CWLOG_R_LATEKEY, -1, 0x1884, 0, 1, 0, 4000);
	check("a NULL key row still lands", cwlog_count(), 3);
	n = cwlog_snapshot(rows, 1);
	check("bounded snapshot honours max", n, 1);
	check("the NULL key row is all zeroes", memcmp(rows[0].cw, k1, 16) != 0, 1);
	check("the NULL key row is 16 zero bytes",
	      memcmp(rows[0].cw, "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 16), 0);
	check("the NULL key row names its reason",
	      rows[0].reason == CWLOG_R_LATEKEY, 1);

	/* fill past capacity: drop-oldest */
	cwlog_reset();
	for (i = 0; i < CWLOG_CAP + 25; i++)
		cwlog_note(CWLOG_STORED, 0, i, 0x1884, 0, 1, k1, 1000 + i);
	check("the ring holds exactly 100 rows", cwlog_count(), CWLOG_CAP);
	n = cwlog_snapshot(rows, CWLOG_CAP);
	check("snapshot of a full ring returns 100", n, CWLOG_CAP);
	check("the newest row is kept", (int)rows[0].tick, 1000 + CWLOG_CAP + 24);
	check("the oldest survivor is row 99", (int)rows[99].tick, 1000 + 25);
	check("overflow rows were dropped (row 0 of the original run is gone)",
	      (int)rows[99].tick != 1000, 1);

	cwlog_reset();
	check("reset empties the ring", cwlog_count(), 0);
	n = cwlog_snapshot(rows, 4);
	check("a snapshot of an empty ring is empty", n, 0);
	check("a NULL snapshot argument is safe", cwlog_snapshot(0, 4), 0);
	check("a zero-max snapshot is safe", cwlog_snapshot(rows, 0), 0);

	/* ==================================================================
	 * 5. The counters' own switch and reset (the shared vocabulary's
	 *    other half): disabled writes nothing, enabled counts, reset
	 *    clears exactly the reason array.
	 * ================================================================== */
	dcwstats_on = 0;
	DCWSTATS_REJECT(DCW_REJ_CHECKSUM);
	DCWSTATS_ACCEPT();
	check("a disabled counter stays at 0", (int)dcwstats[DCW_REJ_CHECKSUM], 0);
	dcwstats_on = 1;
	DCWSTATS_REJECT(DCW_REJ_CONSENSUS);
	DCWSTATS_REJECT(DCW_REJ_CONSENSUS);
	DCWSTATS_ACCEPT();
	check("an enabled counter counts", (int)dcwstats[DCW_REJ_CONSENSUS], 2);
	check("the acceptance counter counts", (int)dcwstats[DCW_ACC_COUNT], 1);
	dcwstats_reset();
	check("reset clears every reason", (int)dcwstats[DCW_REJ_CONSENSUS], 0);
	check("reset clears the acceptance counter", (int)dcwstats[DCW_ACC_COUNT], 0);

	printf("test_cwlog: %d ok, %d failed\n", passed, failed);
	return failed ? 1 : 0;
}
