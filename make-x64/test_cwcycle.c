/*
 * TASK 2.5 -- the unit suite for cache key cycle/parity visibility
 * (../src/cwcycle.h).
 *
 * What is pinned here, in order of how much it would hurt to get wrong:
 *
 *   1. The rule. cwcy_expect() is the live rule written twice elsewhere in the
 *      tree (put_ecm2cache() and the "Setup Cw Cycle" block in ecmdata.c): the
 *      expected half is a function of the ECM's own tag byte against the
 *      channel's cw1cycle, and a channel that declares nothing expects nothing.
 *      If this drifts from those two places, every "contra" the layer reports
 *      is about an imaginary expectation.
 *   2. Silence is not guilt. A key whose sender declared nothing (the vast
 *      majority of a real mesh) and a request with no declared half are both
 *      CWCY_UNJUDGED -- counted, never accused. Only marked-and-different is a
 *      contradiction.
 *   3. Restraint. One contradiction earns exactly one line per source per
 *      window, then quiet while the counters keep counting. An honest source
 *      -- however busy -- must never earn a line at all.
 *   4. The two observation points stay separate (offered vs handed) while the
 *      window and the report throttle are shared, because the question is
 *      about the source, not the phase.
 *   5. The table: keyed on (srctype, srcid) alone; a full table evicts the
 *      least recently touched source rather than refusing the new one (the
 *      2.4 lesson); two sources never share a window.
 *   6. The sanitizer: the marker is a wire byte; anything that is not 1 or 2
 *      is "nothing declared". The layer must never read a cycle out of the key
 *      bytes themselves -- byte 15 of a control word is key material.
 *   7. The sentence survives a short buffer and says, in words, that nothing
 *      was rejected, delayed or scored.
 */
#include "../src/cwcycle.h"

#include <stdio.h>
#include <string.h>

static int passed = 0, failed = 0;

#define CHECK(cond, what) do { \
	if (cond) { passed++; printf("  [ ok ] %s\n", what); } \
	else { failed++; printf("  [FAIL] %s\n", what); } \
} while (0)

#define W 60000u    /* mirror of CWCY_WINDOW, for the tests that cross it */

int main(void)
{
	struct cwcy_table t;
	struct cwcy_ev ev;
	char line[512];

	printf("test_cwcycle\n");

	/* -- 1. the expectation rule (ecmtag vs cw1cycle) ---------------------- */
	cwcy_init(&t);

	CHECK(cwcy_expect(0x81, 0x81) == 2, "tag==cw1cycle(0x81) expects CW1");
	CHECK(cwcy_expect(0x80, 0x81) == 1, "tag!=cw1cycle(0x81) expects CW0");
	CHECK(cwcy_expect(0x80, 0x80) == 2, "tag==cw1cycle(0x80) expects CW1");
	CHECK(cwcy_expect(0x81, 0x80) == 1, "tag!=cw1cycle(0x80) expects CW0");
	CHECK(cwcy_expect(0x80, 0)    == 0, "a channel with no cw1cycle expects nothing");
	CHECK(cwcy_expect(0,    0x81) == 1, "a bare tag against 0x81 expects CW0");

	/* -- 2. the sanitizer --------------------------------------------------- */
	CHECK(cwcy_observed(0) == 0,   "marker 0 reads as nothing declared");
	CHECK(cwcy_observed(1) == 1,   "marker CW0 reads as CW0");
	CHECK(cwcy_observed(2) == 2,   "marker CW1 reads as CW1");
	CHECK(cwcy_observed(3) == 0,   "marker 3 is garbage, not a declaration");
	CHECK(cwcy_observed(0x80) == 0, "a raw tag byte (0x80) is not a declaration");
	CHECK(cwcy_observed(255) == 0, "0xFF is not a declaration");

	/* -- 3. the judge: silence is not guilt --------------------------------- */
	CHECK(cwcy_judge(0, 0) == CWCY_UNJUDGED, "nothing expected, nothing declared: unjudged");
	CHECK(cwcy_judge(2, 0) == CWCY_UNJUDGED, "expected half, undeclared key: unjudged");
	CHECK(cwcy_judge(0, 2) == CWCY_UNJUDGED, "no expectation, declared key: unjudged");
	CHECK(cwcy_judge(2, 2) == CWCY_AGREE,    "declared and equal: agree");
	CHECK(cwcy_judge(1, 1) == CWCY_AGREE,    "declared and equal (CW0): agree");
	CHECK(cwcy_judge(1, 2) == CWCY_CONTRA,   "declared CW1 for a CW0 request: contradiction");
	CHECK(cwcy_judge(2, 1) == CWCY_CONTRA,   "declared CW0 for a CW1 request: contradiction");

	/* -- 4. an honest source never earns a line ----------------------------- */
	cwcy_init(&t);
	{
		int i, fire = 0;
		for (i = 0; i < 200; i++)
			fire += cwcy_note(&t, 1, 10, 0x0604, 0, 0x0100,
			                  1000 + i, CWCY_AT_RECV, 2, 2, &ev);
		CHECK(fire == 0, "200 agreeing keys earn no line at all");
		CHECK(ev.recorded == 1 && ev.win_keys == 200, "the counters still counted");
		/* unmarked offers are counted as offered but never as marked */
		fire = cwcy_note(&t, 1, 10, 0x0604, 0, 0x0100, 1100, CWCY_AT_RECV, 2, 0, &ev);
		CHECK(fire == 0 && ev.win_keys == 201 && ev.win_marked == 200,
		      "an undeclared key is counted as offered, not as marked, and stays quiet");
	}

	/* -- 5. one contradiction fires once, then throttles --------------------- */
	cwcy_init(&t);
	{
		int fire1, fire2, fire3;
		fire1 = cwcy_note(&t, 1, 20, 0x0604, 0, 0x0100, 1000, CWCY_AT_RECV, 2, 1, &ev);
		CHECK(fire1 == 1, "the first contradiction fires");
		CHECK(ev.recorded == 1 && ev.fired == 1, "the evidence marks itself as fired");
		CHECK(ev.expected == 2 && ev.observed == 1, "the line carries what was expected and what arrived");
		CHECK(ev.win_contra == 1 && ev.reports == 1, "one contradiction, one report");
		CHECK(ev.win_marked == 1 && ev.win_keys == 1, "the marked counter moved with it");

		fire2 = cwcy_note(&t, 1, 20, 0x0604, 0, 0x0100, 2000, CWCY_AT_RECV, 2, 1, &ev);
		CHECK(fire2 == 0, "a second contradiction inside the window stays quiet");
		CHECK(ev.recorded == 1 && ev.fired == 0, "quiet evidence is still recorded");
		CHECK(ev.win_contra == 2 && ev.reports == 1, "the count advances while the line is throttled");

		fire3 = cwcy_note(&t, 1, 20, 0x0604, 0, 0x0100, 1000 + W + 1, CWCY_AT_RECV, 2, 1, &ev);
		CHECK(fire3 == 1, "a contradiction in the next window fires again");
		CHECK(ev.win_contra == 1 && ev.reports == 2, "the window rolled and the report counted");
	}

	/* -- 6. the two observation points are separate, the throttle is shared -- */
	cwcy_init(&t);
	{
		int fire;
		fire = cwcy_note(&t, 1, 30, 0x0604, 0, 0x0100, 1000, CWCY_AT_RECV, 2, 1, &ev);
		CHECK(fire == 1 && ev.where == CWCY_AT_RECV, "an offered contradiction names the offer side");
		CHECK(ev.win_sends == 0, "the offer did not touch the hand-off counters");

		/* the throttle is per source: a line was just written for this
		 * source, so a stale hand-off inside the same window stays quiet */
		fire = cwcy_note(&t, 1, 30, 0x0604, 0, 0x0100, 1100, CWCY_AT_SEND, 2, 1, &ev);
		CHECK(fire == 0 && ev.win_stale == 1 && ev.win_sends == 1,
		      "a stale hand-off counts on the hand-off counters, quiet inside the window");

		fire = cwcy_note(&t, 1, 30, 0x0604, 0, 0x0100, 1000 + W, CWCY_AT_SEND, 2, 1, &ev);
		CHECK(fire == 1 && ev.where == CWCY_AT_SEND, "the next window names the hand-off side");
	}

	/* -- 7. stale vs unverified on the hand-off side ------------------------- */
	cwcy_init(&t);
	{
		cwcy_note(&t, 1, 40, 0x0604, 0, 0x0100, 1000, CWCY_AT_SEND, 2, 0, &ev);
		CHECK(ev.win_unjudged == 1 && ev.win_stale == 0,
		      "a declared channel handed an undeclared key: unverified, not stale");
		cwcy_note(&t, 1, 40, 0x0604, 0, 0x0100, 1001, CWCY_AT_SEND, 0, 0, &ev);
		CHECK(ev.win_unjudged == 2 && ev.win_sends == 2,
		      "a channel with no declared half is unjudged too, never stale");
		/* and a stale hand-off on a fresh source fires (rep_ticks==0) */
		CHECK(cwcy_note(&t, 1, 41, 0x0604, 0, 0x0100, 1000, CWCY_AT_SEND, 2, 1, &ev) == 1,
		      "a contradictory hand-off on a new source fires immediately");
	}

	/* -- 8. the table: keying, eviction, isolation --------------------------- */
	cwcy_init(&t);
	{
		int i;
		char a[512], b[512];
		/* same srctype, different srcid: never the same window */
		cwcy_note(&t, 1, 50, 0x0604, 0, 0x0100, 1000, CWCY_AT_RECV, 2, 1, &ev);
		snprintf(a, sizeof(a), "src %d/%d", ev.srctype, ev.srcid);
		cwcy_note(&t, 1, 51, 0x1801, 0, 0x0200, 1100, CWCY_AT_RECV, 1, 2, &ev);
		snprintf(b, sizeof(b), "src %d/%d", ev.srctype, ev.srcid);
		CHECK(strcmp(a, "src 1/50") == 0 && strcmp(b, "src 1/51") == 0,
		      "two sources keep separate evidence");
		CHECK(ev.win_contra == 1, "the second source's window is its own");

		/* fill the table, then force an eviction of the LRU source */
		cwcy_init(&t);
		for (i = 0; i < CWCY_SLOTS; i++)
			cwcy_note(&t, 1, 100 + i, 0x0604, 0, 0x0100, 1000 + i, CWCY_AT_RECV, 2, 2, &ev);
		/* source 100 is the oldest untouched one; source 100+63 the newest */
		CHECK(cwcy_note(&t, 1, 999, 0x0604, 0, 0x0100, 2000, CWCY_AT_RECV, 2, 2, &ev) == 0,
		      "a new source fits after eviction and stays quiet (agreeing)");
		cwcy_note(&t, 1, 100, 0x0604, 0, 0x0100, 3000, CWCY_AT_RECV, 2, 1, &ev);
		CHECK(ev.win_contra == 1 && ev.win_keys == 1,
		      "the evicted source restarts from a clean window when it returns");
	}

	/* -- 9. degenerate inputs ------------------------------------------------ */
	cwcy_init(&t);
	CHECK(cwcy_note(NULL, 1, 1, 0, 0, 0, 0, CWCY_AT_RECV, 2, 1, &ev) == 0 && ev.recorded == 0,
	      "no table: quiet, and the evidence says nothing was recorded");
	CHECK(cwcy_note(&t, 1, 1, 0, 0, 0, 1000, 7, 2, 1, &ev) >= 0,
	      "an unknown 'where' is tolerated (counted as an offer)");
	{
		int n = cwcy_note(&t, 1, 2, 0x0604, 0, 0x0100, 1000, CWCY_AT_RECV, 2, 1, NULL);
		CHECK(n == 1, "the note works with no evidence struct at all");
	}
	{
		struct cwcy_ev zero;
		memset(&zero, 0x55, sizeof(zero));
		CHECK(cwcy_format(NULL, line, (int)sizeof(line)) == 0,
		      "format with no evidence writes nothing");
	}

	/* -- 10. the sentence ----------------------------------------------------- */
	cwcy_init(&t);
	{
		int n;
		cwcy_note(&t, 1, 60, 0x0604, 0x000100, 0x0200, 1000, CWCY_AT_RECV, 2, 1, &ev);
		n = cwcy_format(&ev, line, (int)sizeof(line));
		CHECK(n > 0, "the sentence renders");
		CHECK(strstr(line, "CW CYCLE:") == line, "it names the layer");
		CHECK(strstr(line, "1/60") != NULL, "it names the source");
		CHECK(strstr(line, "0604:000100:0200") != NULL, "it names the channel");
		CHECK(strstr(line, "CW1") != NULL && strstr(line, "CW0") != NULL,
		      "it says which half was expected and which arrived");
		CHECK(strstr(line, "Nothing was rejected, delayed or scored") != NULL,
		      "it says, in words, that nothing was done");
		CHECK(strstr(line, "measurement, not an accusation") != NULL,
		      "it says what it is");
		n = cwcy_format(&ev, line, 32);
		CHECK(n >= 0 && strlen(line) < 32, "it survives a short buffer, terminated");
	}

	printf("  ----\n");
	printf("  %d ok, %d failed\n", passed, failed);
	return failed ? 1 : 0;
}
