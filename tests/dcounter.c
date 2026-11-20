/*
 * Band 120 unit package: DoubleCheck closes a slot; it does not "time out" a
 * decided check.
 *
 * Pinned property (each half is a field number from the 2026-11-20 22:08
 * snapshot, uptime 7m34s -- the first snapshot whose counters could be trusted):
 *   1. `started 3317 / timeouts 3276` looked like "98.8% of checks die in a
 *      timeout". It was the reclaim sweep counting EVERY slot it took back,
 *      including checks that had already agreed. So an agreed check must close
 *      with timeouts UNmoved and closed++ (this is what the old code got wrong);
 *   2. a check that never got a second source must still be counted as a
 *      timeout, once, when its slot is reclaimed -- the number keeps its
 *      original meaning, it just stops being a closure counter;
 *   3. closed >= timeouts always, and (closed - timeouts) is the number of
 *      decisions that were reached and merely left in the table.
 *
 * Reclaim happens inside ecm_doublecheck_start(), so each phase ends by starting
 * one more check -- exactly the production shape, no test-only seam.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

static uint32_t tick_base_done = 0;
uint32_t GetTickCount(void)
{
	struct timespec ts;
	uint32_t now;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	now = (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
	if (!tick_base_done) { tick_base_done = 1; }
	return now;
}

#include "../src/ecm-doublecheck.h"

static int fails = 0;
#define CHECK(cond, what) do { if (!(cond)) { \
	printf("FAIL: %s\n", (what)); fails++; } } while (0)

static void set_cw(uint8_t *cw, int seed)
{
	int i;
	for (i = 0; i < 16; i++) cw[i] = (unsigned char)((seed * 37 + i * 11) & 0xFF);
}

/* DOUBLECHECK_TIMEOUT_MS is 3000; the sweep reclaims at 2x. Sleep past it. */
#define AGE_MS 6400
static void age(void)
{
	struct timespec ts;
	ts.tv_sec = AGE_MS / 1000;
	ts.tv_nsec = (long)(AGE_MS % 1000) * 1000000L;
	nanosleep(&ts, 0);
}

int main(void)
{
	uint8_t one[16], other[16];
	doublecheck_pending_t *p1, *p2;

	set_cw(one, 3);
	set_cw(other, 4);

	/* 1. a check that AGREED must not be counted as a timeout */
	p1 = ecm_doublecheck_start(0x1111u, 0x1814, 0x005211, 0x13AE);
	CHECK(p1 != 0, "first check starts");
	/* add_result returns the verification STATE (1 once agreed), not an ok
	 * flag -- so the pins below are on the counters and the entry, which is
	 * what the page reads. */
	(void)ecm_doublecheck_add_result(p1, 1, one, 100);
	(void)ecm_doublecheck_add_result(p1, 2, one, 140);
	CHECK(p1->result_count == 2, "two sources recorded on one check");
	CHECK(p1->verified == 1, "the entry itself says verified");
	CHECK(g_doublecheck.verified_ok == 1, "two sources, same CW -> agreement");
	CHECK(g_doublecheck.timeouts == 0 && g_doublecheck.closed == 0,
	      "nothing reclaimed yet");

	age();
	p2 = ecm_doublecheck_start(0x2222u, 0x1814, 0x005211, 0x13AE);
	CHECK(p2 != 0, "second check starts after the reclaim window");
	CHECK(g_doublecheck.closed == 1, "the agreed slot was reclaimed -- counted as a closure");
	CHECK(g_doublecheck.timeouts == 0,
	      "band 120: an AGREED check must not also count as a timeout (this is the bug)");

	/* 2. a check that never got a second source IS a timeout -- once */
	age();
	{
		doublecheck_pending_t *p3 = ecm_doublecheck_start(0x3333u, 0x1802, 0, 0x79A5);
		CHECK(p3 != 0, "third check starts");
		CHECK(g_doublecheck.closed == 2, "second reclaim");
		CHECK(g_doublecheck.timeouts == 1, "the undecided one is the timeout");
		CHECK(g_doublecheck.total_checks == 3, "three checks started");
		CHECK(g_doublecheck.closed >= g_doublecheck.timeouts,
		      "closures bound the timeouts from above -- the page relies on this");
	}

	/* 3. mismatch is a decision too: it must not become a timeout either */
	{
		doublecheck_pending_t *p4 = ecm_doublecheck_start(0x4444u, 0x1814, 0x005211, 0x13AE);
		CHECK(p4 != 0, "fourth check starts");
		ecm_doublecheck_add_result(p4, 1, one, 100);
		ecm_doublecheck_add_result(p4, 2, other, 130);
		CHECK(g_doublecheck.verified_fail == 1, "two sources, different CW -> mismatch");
		age();
		{
			doublecheck_pending_t *p5 = ecm_doublecheck_start(0x5555u, 0x0B00, 0, 0x025B);
			CHECK(p5 != 0, "fifth check starts");
			/* reclaims so far: p1 agreed, p2 undecided, p3 undecided,
			 * p4 mismatched => four closures, TWO of them timeouts. */
			CHECK(g_doublecheck.closed == 4, "every slot the sweep took back");
			CHECK(g_doublecheck.timeouts == 2,
			      "only the undecided ones (p2, p3); mismatch decided => no timeout");
			CHECK(g_doublecheck.closed - g_doublecheck.timeouts == 2,
			      "two closures were decided checks sitting in the table");
		}
	}

	if (fails) { printf("dcounter: %d FAILURES\n", fails); return 1; }
	printf("dcounter: PASS (closures vs timeouts: agreed/mismatched never time out)\n");
	return 0;
}
