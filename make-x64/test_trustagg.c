/*
 * TASK 2.8 -- the unit suite for the cache-peer trust engine, coarse tier
 * (../src/trustagg.h).
 *
 * What is pinned here, in order of how much it would hurt to get wrong:
 *
 *   1. The arithmetic is trust.h's, not a new policy. SOFT halves toward the
 *      floor, PROOF steps 25, GOOD climbs a thirty-second with the minimum-1
 *      rule, floor and ceiling hold. The aggregation must never invent its
 *      own idea of guilt -- a coarse score that condemned differently from
 *      the fine one would make the fan-out argue with the delivery gates.
 *   2. The two tiers stay independent. A fine-tier event and a coarse-tier
 *      event live in different tables; the coarse key is (srctype, srcid,
 *      CAID) and two CAIDs of one peer never share an entry (that is what
 *      makes the view finer than per-server, GR4).
 *   3. The crossing and its brake. Below the line + outside the window =
 *      exactly one crossing, evidence filled, suppression armed. Inside the
 *      window: silent. After the window: the next event re-arms. A GOOD
 *      event can never arm the brake. Unknown sources are always askable.
 *   4. The recovery story. GOODs climb the same slow staircase as the fine
 *      tier; once above the line the peer is askable even mid-window, and a
 *      suppression that expired lets the test request through.
 *   5. The table: LRU eviction that never refuses the next peer, count()
 *      truthfulness, reset().
 *   6. The sentence: built from the evidence, names source/CAID/counts and
 *      says what the brake does and that nothing is disconnected; survives
 *      a short buffer.
 *   7. Degenerate inputs.
 */
#include "../src/trustagg.h"

#include <stdio.h>
#include <string.h>

static int passed = 0, failed = 0;

#define CHECK(cond, what) do { \
	if (cond) { passed++; printf("  [ ok ] %s\n", what); } \
	else { failed++; printf("  [FAIL] %s\n", what); } \
} while (0)

int main(void)
{
	struct trustagg_table t;
	struct trustagg_evidence ev;
	int crossed, p, b;

	printf("trustagg: the coarse tier of the cache-peer trust engine\n");

	/* --- 1. the arithmetic is trust.h's --------------------------------- */
	trustagg_reset(&t);
	crossed = trustagg_note(&t, TRUST_EV_PROOF, 1, 5, 0x1884, 1000, &ev);
	CHECK(crossed == 0, "one proof on a fresh peer: counted, not yet a crossing");
	CHECK(ev.score == TRUST_START - TRUST_PROOF_STEP,
	      "the PROOF step is trust.h's flat 25");
	{
		struct trust_entry *fine;
		struct trust_table fine_tab;
		memset(&fine_tab, 0, sizeof(fine_tab));
		trust_record(&fine_tab, TRUST_EV_PROOF, 1, 5, 0x1884, 7, 99, 1000);
		trust_record(&fine_tab, TRUST_EV_SOFT, 1, 5, 0x1884, 7, 99, 1001);
		fine = trust_find(&fine_tab, 1, 5, 0x1884, 7, 99);
		trustagg_note(&t, TRUST_EV_SOFT, 1, 5, 0x1884, 1001, &ev);
		CHECK(fine && ev.score == fine->score,
		      "coarse SOFT arithmetic equals the fine tier's, step for step");
		trust_record(&fine_tab, TRUST_EV_GOOD, 1, 5, 0x1884, 7, 99, 1002);
		trustagg_note(&t, TRUST_EV_GOOD, 1, 5, 0x1884, 1002, &ev);
		fine = trust_find(&fine_tab, 1, 5, 0x1884, 7, 99);
		CHECK(fine && ev.score == fine->score,
		      "coarse GOOD arithmetic equals the fine tier's, step for step");
	}
	{
		/* floor and ceiling, on the coarse tier */
		struct trustagg_table f;
		int i;
		trustagg_reset(&f);
		for (i = 0; i < 6; i++)
			trustagg_note(&f, TRUST_EV_PROOF, 1, 9, 0x1884, 2000 + i, &ev);
		CHECK(ev.score == TRUST_FLOOR, "proofs stop at the floor: recovery stays possible");
		for (i = 0; i < 400; i++)
			trustagg_note(&f, TRUST_EV_GOOD, 1, 9, 0x1884, 2000 + i, &ev);
		CHECK(ev.score == TRUST_CEILING, "goods stop at the ceiling: no source becomes immune");
	}

	/* --- 2. the coarse key and its independence ------------------------- */
	trustagg_reset(&t);
	trustagg_note(&t, TRUST_EV_PROOF, 1, 5, 0x1884, 1000, &ev);
	trustagg_note(&t, TRUST_EV_PROOF, 1, 5, 0x0500, 1001, &ev);
	trustagg_count(&t, &p, &b);
	CHECK(p == 2, "one peer, two CAIDs: two entries (finer than per-server)");
	CHECK(trustagg_ask(&t, 1, 5, 0x1884, 1002) == 1,
	      "the first CAID is untouched by the second's events (GR4, live)");
	{
		struct trustagg_entry *e1 = trustagg_find(&t, 1, 5, 0x1884);
		struct trustagg_entry *e2 = trustagg_find(&t, 1, 5, 0x0500);
		CHECK(e1 && e2 && e1->nproof == 1 && e2->nproof == 1,
		      "neither CAID's proof count leaked into the other");
	}
	CHECK(trustagg_find(&t, 1, 6, 0x1884) == NULL,
	      "a different srcid is a different counterparty");
	CHECK(trustagg_find(&t, 2, 5, 0x1884) == NULL,
	      "a different srctype is a different counterparty");

	/* --- 3. the crossing and the brake ---------------------------------- */
	trustagg_reset(&t);
	trustagg_note(&t, TRUST_EV_PROOF, 1, 5, 0x1884, 1000, &ev);   /* 60 -> 35 */
	CHECK(ev.score >= TRUST_ACTIONABLE,
	      "one proof leaves the peer above the line (35 > 25)");
	crossed = trustagg_note(&t, TRUST_EV_PROOF, 1, 5, 0x1884, 1100, &ev); /* -> 10 */
	CHECK(crossed == 1, "the second proof crosses: the crossing is the event");
	CHECK(ev.score < TRUST_ACTIONABLE, "the evidence carries the fallen score");
	CHECK(ev.nproof == 2 && ev.nsoft == 0 && ev.ngood == 0,
	      "the evidence carries the counts that did it");
	CHECK(ev.srctype == 1 && ev.srcid == 5 && ev.caid == 0x1884,
	      "the evidence carries the identity (GR1/GR4)");
	CHECK(ev.suppress_ms == TRUSTAGG_SUPPRESS_MS, "the evidence carries the window");
	CHECK(trustagg_ask(&t, 1, 5, 0x1884, 1200) == 0,
	      "the fan-out skips the peer while the suppression runs");
	CHECK(trustagg_ask(&t, 1, 5, 0x1884, 2000) == 0,
	      "still inside the window: still skipped");
	crossed = trustagg_note(&t, TRUST_EV_SOFT, 1, 5, 0x1884, 2500, &ev);
	CHECK(crossed == 0, "an event inside the window never writes a second line");
	CHECK(trustagg_ask(&t, 1, 5, 0x1884, 1100 + TRUSTAGG_SUPPRESS_MS) == 1,
	      "the window ends: one request tests the peer again");
	crossed = trustagg_note(&t, TRUST_EV_SOFT, 1, 5, 0x1884,
	                        1100 + TRUSTAGG_SUPPRESS_MS + 500, &ev);
	CHECK(crossed == 1, "still poisoning after the test: the next event re-arms");
	CHECK(trustagg_ask(&t, 1, 5, 0x1884,
	                   1100 + TRUSTAGG_SUPPRESS_MS + 600) == 0,
	      "and the brake is running again");
	{
		/* GOOD can never arm the brake, even from above nothing */
		struct trustagg_table g;
		trustagg_reset(&g);
		crossed = trustagg_note(&g, TRUST_EV_GOOD, 1, 7, 0x1884, 1000, &ev);
		CHECK(crossed == 0 && trustagg_ask(&g, 1, 7, 0x1884, 1001) == 1,
		      "a GOOD event never arms the brake");
	}
	{
		/* the unknown peer is always askable */
		CHECK(trustagg_ask(&t, 1, 42, 0x1884, 1200) == 1,
		      "a source with no entry is askable: unseen is not guilty");
	}

	/* --- 4. the recovery story ------------------------------------------ */
	trustagg_reset(&t);
	trustagg_note(&t, TRUST_EV_PROOF, 1, 5, 0x1884, 1000, &ev);
	crossed = trustagg_note(&t, TRUST_EV_PROOF, 1, 5, 0x1884, 1100, &ev);
	CHECK(crossed == 1, "crossed for the recovery story");
	{
		int i, open_at = -1;
		uint32_t tick = 1100;
		/* goods arrive only after the window -- the one test request each */
		tick = 1100 + TRUSTAGG_SUPPRESS_MS;
		CHECK(trustagg_ask(&t, 1, 5, 0x1884, tick) == 1,
		      "the test request goes out");
		for (i = 0; i < 200; i++) {
			trustagg_note(&t, TRUST_EV_GOOD, 1, 5, 0x1884, tick + i, &ev);
			if (ev.score >= TRUST_ACTIONABLE) { open_at = i; break; }
		}
		CHECK(open_at >= 0, "goods climb back above the line");
		CHECK(open_at <= 60, "the climb takes tens of goods, not hundreds (slow on purpose)");
		CHECK(trustagg_ask(&t, 1, 5, 0x1884, tick + open_at) == 1,
		      "above the line, the peer is askable again");
		crossed = trustagg_note(&t, TRUST_EV_GOOD, 1, 5, 0x1884, tick + open_at + 1, &ev);
		CHECK(crossed == 0, "and no line is written on the way up");
	}

	/* --- 5. the table ---------------------------------------------------- */
	{
		struct trustagg_table full;
		int i, saw_evict = 1;
		trustagg_reset(&full);
		for (i = 0; i < TRUSTAGG_TABLE_SIZE; i++)
			trustagg_note(&full, TRUST_EV_GOOD, 1, i + 1, 0x1884, 1000 + i, &ev);
		trustagg_count(&full, &p, &b);
		CHECK(p == TRUSTAGG_TABLE_SIZE, "the table fills to its size");
		/* oldest is srcid 1 (tick 1000); touch srcid 2, then add one more */
		trustagg_note(&full, TRUST_EV_GOOD, 1, 2, 0x1884, 5000, &ev);
		trustagg_note(&full, TRUST_EV_GOOD, 1, TRUSTAGG_TABLE_SIZE + 1, 0x1884, 5001, &ev);
		if (trustagg_find(&full, 1, 2, 0x1884) == NULL) saw_evict = 0;
		CHECK(trustagg_find(&full, 1, 1, 0x1884) == NULL && saw_evict,
		      "the OLDEST entry is evicted, never the worst-scored, and never the just-touched");
		trustagg_count(&full, &p, &b);
		CHECK(p == TRUSTAGG_TABLE_SIZE, "a full table always accepts the next peer");
	}
	trustagg_reset(&t);
	trustagg_count(&t, &p, &b);
	CHECK(p == 0 && b == 0, "reset empties the table and the counters");

	/* --- 6. the sentence -------------------------------------------------- */
	{
		char line[512];
		int n;
		trustagg_reset(&t);
		trustagg_note(&t, TRUST_EV_PROOF, 1, 5, 0x1884, 1000, &ev);
		trustagg_note(&t, TRUST_EV_PROOF, 1, 5, 0x1884, 1100, &ev);
		n = trustagg_format(&ev, line, (int)sizeof(line));
		CHECK(n > 0, "the sentence builds");
		CHECK(strstr(line, "source 1/5") != NULL, "it names the source the way every layer does");
		CHECK(strstr(line, "caid 1884") != NULL, "it names the CAID the habit was seen on");
		CHECK(strstr(line, "2 proof(s)") != NULL, "it names the counts that did it");
		CHECK(strstr(line, "skipped for 60 seconds") != NULL, "it says what the brake does and for how long");
		CHECK(strstr(line, "nothing is disconnected") != NULL, "it says what the brake is NOT");
		CHECK(strstr(line, "no client was touched") != NULL, "it says delivery was not touched");
		CHECK(strstr(line, "win the requests back") != NULL, "it says how it ends");
		n = trustagg_format(&ev, line, 24);
		CHECK(n >= 0 && strlen(line) < 24, "it survives a short buffer, terminated");
	}

	/* --- 7. degenerate inputs -------------------------------------------- */
	{
		char line[64];
		CHECK(trustagg_format(NULL, line, (int)sizeof(line)) < 0, "NULL evidence is refused");
		CHECK(trustagg_format(&ev, NULL, 64) < 0, "NULL buffer is refused");
		CHECK(trustagg_format(&ev, line, 0) < 0, "a zero buffer is refused");
		crossed = trustagg_note(&t, TRUST_EV_NONE, 1, 5, 0x1884, 9000, &ev);
		CHECK(crossed == 0, "an ask-only event writes nothing");
	}

	printf("  %d ok, %d failed\n", passed, failed);
	return failed ? 1 : 0;
}
