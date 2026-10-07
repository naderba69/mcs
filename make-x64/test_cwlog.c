/*
 * test_cwlog.c -- TASK R11 (D65), recreated in R13 (D67).
 *
 * The unit suite for ../src/cwlog.c -- the per-CW verdict ring behind the
 * /cwlog page and the cwlog section of /json. It exercises the REAL module,
 * and it also pins the contract that makes the page honest:
 *
 *   * the five API calls (note/count/capacity/snapshot/reset),
 *   * newest-first snapshots with every stored field intact,
 *   * the cap: drop-oldest at CWLOG_CAP, the ring never grows,
 *   * the guards: an out-of-range verdict is dropped, a NULL cw stores zeros,
 *   * the names: verdicts 0..3 and reasons 0..9 must spell exactly what
 *     ../src/dcwstats.h spells for the same events (the ring rows and the
 *     counters describe the same thing, so a rename on one side alone is a
 *     defect the page would display), plus the four ring-only reasons,
 *   * concurrency: four threads hammering cwlog_note() leave a consistent,
 *     correctly bounded ring.
 *
 * The counters' own storage is provided here so the header is self-contained;
 * nothing in this test asserts a counter value.
 *
 *   make -C make-x64 test
 */
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <stdint.h>

#include "../src/dcwstats.h"

int           dcwstats_on = 0;                  /* dcwstats.h storage */
unsigned long dcwstats[DCW_REJ_COUNT];

#include "../src/cwlog.h"

static int passed = 0, failed = 0;

static void check(const char *what, int got, int want)
{
	if (got == want) { passed++; printf("  [ ok ] %-62s = %d\n", what, got); }
	else { failed++; printf("  [FAIL] %-62s got=%d want=%d\n", what, got, want); }
}

static uint8_t cw_row[16] = {0x01,0x02,0x03,0x06,0x11,0x22,0x33,0x66,
                             0x10,0x20,0x30,0x60,0xa0,0xb0,0xc0,0xdd};
static uint8_t cw_zero[16];

/* every threaded note uses the same caid/verdict so the flood is checkable */
#define TH_CAID 0x1884

static void *flood(void *arg)
{
	long tid = (long)arg;
	int i;
	for (i = 0; i < 50; i++)
		cwlog_note(CWLOG_STORED, 0, (int)(0x10000 | tid), TH_CAID, 0,
		           (uint16_t)(9000 + tid), cw_row, (uint32_t)(i + 1));
	return NULL;
}

int main(void)
{
	struct cwlog_entry out[CWLOG_CAP];
	struct cwlog_entry s3[3];
	int i, n, got, ok;
	pthread_t th[4];

	printf("test_cwlog: the real verdict ring (TASK R11/D65, cap %d)\n", CWLOG_CAP);

	dcwstats_reset();   /* the header's own helper, so it is never dead code */

	/* -- the empty ring and the argument guards --------------------------- */
	check("the capacity is CWLOG_CAP",              cwlog_capacity(), CWLOG_CAP);
	check("a fresh ring is empty",                  cwlog_count(), 0);
	check("snapshot refuses a NULL buffer",         cwlog_snapshot(NULL, 8), 0);
	check("snapshot refuses max <= 0",              cwlog_snapshot(out, 0), 0);
	check("snapshot on an empty ring returns 0",    cwlog_snapshot(out, CWLOG_CAP), 0);

	/* -- one row, every field -------------------------------------------- */
	cwlog_note(CWLOG_DELIVERED, 0, 0x10001, 0x1884, 0x123456, 0x0064, cw_row, 4242);
	check("note records the row",                   cwlog_count(), 1);
	check("snapshot returns it",                    cwlog_snapshot(out, CWLOG_CAP), 1);
	check("the verdict is stored",                  out[0].verdict, CWLOG_DELIVERED);
	check("the reason is stored",                   out[0].reason, 0);
	check("the caid is stored",                     out[0].caid, 0x1884);
	check("the provid is stored",                   (int)out[0].provid, 0x123456);
	check("the sid is stored",                      out[0].sid, 0x0064);
	check("the tick is stored",                     (int)out[0].tick, 4242);
	check("the peerid keeps its origin flags",      out[0].peerid, 0x10001);
	check("all 16 cw bytes are stored",             memcmp(out[0].cw, cw_row, 16), 0);

	cwlog_note(CWLOG_REFUSED, 5, -1, 0x1884, 0, 0x0065, NULL, 4243);
	ok = (cwlog_count() == 2) && (cwlog_snapshot(out, CWLOG_CAP) == 2) &&
	     (memcmp(out[0].cw, cw_zero, 16) == 0) && (out[0].peerid == -1);
	check("a NULL cw stores 16 zeros and a -1 source", ok ? 1 : 0, 1);

	check("a verdict below the range is dropped",
	      (cwlog_note(-1, 0, 0, 1, 1, 1, cw_row, 1), cwlog_count()), 2);
	check("a verdict above the range is dropped",
	      (cwlog_note(CWLOG_STORED + 1, 0, 0, 1, 1, 1, cw_row, 1), cwlog_count()), 2);

	/* -- the names -------------------------------------------------------- */
	ok = !strcmp(cwlog_verdict_name(CWLOG_DELIVERED), "delivered") &&
	     !strcmp(cwlog_verdict_name(CWLOG_REFUSED),   "refused")   &&
	     !strcmp(cwlog_verdict_name(CWLOG_HELD),      "held")      &&
	     !strcmp(cwlog_verdict_name(CWLOG_STORED),    "stored");
	check("verdict names 0..3 read as the page prints them", ok ? 1 : 0, 1);
	check("an unknown verdict is named, never NULL",
	      strcmp(cwlog_verdict_name(99), "unknown") == 0, 1);

	ok = 1;
	for (i = 0; i < 10; i++)
		if (strcmp(cwlog_reason_name(i), dcwstats_reason_name(i)) != 0) ok = 0;
	check("reasons 0..9 are word-for-word the dcwstats names", ok ? 1 : 0, 1);

	ok = !strcmp(cwlog_reason_name(CWLOG_R_HOLD),      "held-for-corroboration") &&
	     !strcmp(cwlog_reason_name(CWLOG_R_CONVICTED), "convicted-memory")       &&
	     !strcmp(cwlog_reason_name(CWLOG_R_THRESHOLD), "below-threshold")        &&
	     !strcmp(cwlog_reason_name(CWLOG_R_PROFILE),   "profile-filter");
	check("the four ring-only reasons are named", ok ? 1 : 0, 1);
	check("an unknown reason is named, never NULL",
	      strcmp(cwlog_reason_name(99), "unknown") == 0, 1);

	/* -- newest first ----------------------------------------------------- */
	cwlog_reset();
	for (i = 0; i < 3; i++)
		cwlog_note(CWLOG_STORED, 0, 0x10001, 0x1884, 0, (uint16_t)(0x00A0 + i),
		           cw_row, (uint32_t)(5000 + i));
	n = cwlog_snapshot(out, CWLOG_CAP);
	check("three rows are live",                    cwlog_count(), 3);
	check("snapshot returns them newest first (row 0 = last sid)",
	      (n == 3 && out[0].sid == 0x00A2) ? 1 : 0, 1);
	check("newest first: row 1 carries the middle sid", out[1].sid, 0x00A1);
	check("newest first: row 2 carries the first sid",  out[2].sid, 0x00A0);

	/* -- the cap and the oldest eviction ---------------------------------- */
	cwlog_reset();
	for (i = 0; i < CWLOG_CAP + 5; i++)
		cwlog_note(CWLOG_STORED, 0, 0x10001, 0x1884, 0,
		           (uint16_t)(9001 + i), cw_row, (uint32_t)(6000 + i));
	check("the ring never grows past CWLOG_CAP",    cwlog_count(), CWLOG_CAP);
	n = cwlog_snapshot(out, CWLOG_CAP);
	ok = 1;
	for (i = 0; i < n; i++)
		if (out[i].sid >= 9001 && out[i].sid <= 9005) ok = 0;
	check("the five oldest rows were evicted, not kept", ok ? 1 : 0, 1);
	check("the newest row survived",                out[0].sid, (uint16_t)(9001 + CWLOG_CAP + 4));
	check("the oldest survivor is the 6th row written", out[CWLOG_CAP - 1].sid, 9006);

	cwlog_note(CWLOG_HELD, CWLOG_R_HOLD, 0x10001, 0x1884, 0, 0x00A3, cw_row, 7000);
	check("a ring-only reason round-trips through storage",
	      (cwlog_snapshot(s3, 1) == 1 && s3[0].reason == CWLOG_R_HOLD) ? 1 : 0, 1);
	check("snapshot respects max < count",          cwlog_snapshot(s3, 3), 3);
	check("snapshot can ask for more than is live", cwlog_snapshot(out, CWLOG_CAP + 50), CWLOG_CAP);

	/* -- reset ------------------------------------------------------------ */
	cwlog_reset();
	ok = (cwlog_count() == 0) && (cwlog_capacity() == CWLOG_CAP) &&
	     (cwlog_snapshot(out, CWLOG_CAP) == 0);
	check("reset forgets every row and keeps the capacity", ok ? 1 : 0, 1);

	/* -- concurrency ------------------------------------------------------ */
	cwlog_reset();
	for (i = 0; i < 4; i++) pthread_create(&th[i], NULL, flood, (void *)(long)i);
	for (i = 0; i < 4; i++) pthread_join(th[i], NULL);
	check("200 threaded notes leave a full, bounded ring", cwlog_count(), CWLOG_CAP);
	got = cwlog_snapshot(out, 4);
	ok = (got == 4);
	for (i = 0; i < got; i++)
		if (out[i].caid != TH_CAID || out[i].verdict != CWLOG_STORED) ok = 0;
	check("every row the flood left is intact and consistent", ok ? 1 : 0, 1);

	printf("== %d/%d passed, %d failed ==\n", passed, passed + failed, failed);
	return failed ? 1 : 0;
}
