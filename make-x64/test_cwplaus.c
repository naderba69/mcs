/*
 * TASK 2.4 -- the unit suite for plausibility scoring (../src/cwplaus.h).
 *
 * What is being pinned here, in order of how much it would hurt to get wrong:
 *
 *   1. The arithmetic. `cwp_groups()` reproduces checksumDCW() group by group,
 *      so a key that passes the blunt test must read 4/4 and a key that fails it
 *      must read 3/4 when exactly one group is broken. If this drifts, every
 *      number the layer prints is wrong in a way nobody would notice.
 *   2. The restraint. A single 3/4 key must NOT be scored -- it is 1.52 % likely
 *      for an honest key of a bouquet that does not use the rule, and accusing
 *      on it would be exactly the false positive GR3 calls worse than the
 *      original problem. The suite asserts the counters move and the score does
 *      not.
 *   3. The pattern, all four clauses of it, because each one on its own is a
 *      different bug: too few keys, too low a rate, mixed groups, or the wrong
 *      window.
 *   4. The windows and the suppression: one line per source per minute however
 *      hard the source forges, with the counters still counting.
 *   5. The table: a source is identified by (srctype, srcid) and by nothing
 *      else, a full table evicts the least recently touched source rather than
 *      refusing the new one, and two sources never share a window.
 *   6. The sentence: built from the evidence, and it must survive a short
 *      buffer, because it is logged through a fixed-size stack buffer.
 */
#include "../src/cwplaus.h"

#include <stdio.h>
#include <string.h>

static int passed = 0, failed = 0;

#define CHECK(cond, what) do { \
	if (cond) { passed++; printf("  [ ok ] %s\n", what); } \
	else { failed++; printf("  [FAIL] %s\n", what); } \
} while (0)

/* A key that satisfies all four group sums: the shape every honest key has. */
static const uint8_t GOOD[16] = {
	0xA1, 0xB2, 0xC3, 0x16, 0xA5, 0xA6, 0xA7, 0xF2,
	0xA1, 0xB2, 0xC3, 0x16, 0xA5, 0xA6, 0xA7, 0xF2
};

/* The documented forgery: XOR 0xF0 on the LAST byte of a valid key. Group 4
 * breaks; groups 1..3 are untouched. This is r107's "fakeCW by xor" shape. */
static const uint8_t FORGED4[16] = {
	0xA1, 0xB2, 0xC3, 0x16, 0xA5, 0xA6, 0xA7, 0xF2,
	0xA1, 0xB2, 0xC3, 0x16, 0xA5, 0xA6, 0xA7, 0x06
};

/* The same trick on the second group, so "always the same group" has to mean
 * something specific rather than "some group". */
static const uint8_t FORGED2[16] = {
	0xA1, 0xB2, 0xC3, 0x16, 0xA5, 0xA6, 0xA7, 0x02,
	0xA1, 0xB2, 0xC3, 0x16, 0xA5, 0xA6, 0xA7, 0xF2
};

/* Noise: no group has any reason to hold. */
static const uint8_t NOISE[16] = {
	0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
	0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00
};

int main(void)
{
	struct cwp_table t;
	struct cwp_evidence ev;
	unsigned m;
	int i, g, bad;

	printf("TASK 2.4 -- plausibility scoring (cwplaus.h)\n");

	/* ------------------------------------------------------------------
	 * 1. The arithmetic
	 * --------------------------------------------------------------- */
	cwp_init(&t);

	CHECK(cwp_groups(GOOD) == 4, "a key of the expected shape satisfies all four group sums");
	CHECK(cwp_groups(FORGED4) == 3, "the documented forgery (last byte XORed) satisfies three of four");
	CHECK(cwp_groups(FORGED2) == 3, "and so does the same trick on another group");

	CHECK((cwp_scan(GOOD, &g, &bad) == CWP_NONE) && g == 4 && bad == 0,
	      "a good key: 4/4, no verdict, and no group blamed");
	CHECK((cwp_scan(FORGED4, &g, &bad) == CWP_ONE_GROUP) && g == 3 && bad == 4,
	      "the last-byte forgery is ONE_GROUP and names group 4 -- not 'a group'");
	CHECK((cwp_scan(FORGED2, &g, &bad) == CWP_ONE_GROUP) && g == 3 && bad == 2,
	      "the second-group forgery names group 2: the verdict carries WHICH group");

	CHECK(0 == (cwp_scan(GOOD, NULL, NULL) & (CWP_ONE_GROUP | CWP_WEAK)),
	      "a NULL out-parameter is allowed (the verdict is still formed)");

	/*
	 * The same-group rule only makes sense if a bad_group of 0 cannot be
	 * mistaken for "group zero failed".
	 */
	cwp_scan(GOOD, &g, &bad);
	CHECK(bad == 0, "bad_group is 0 when nothing fails, so 0 can never mean group 0");

	/*
	 * A key with no group intact is WEAK, never ONE_GROUP -- the distinction
	 * the whole layer rests on.
	 */
	CHECK(cwp_scan(NOISE, &g, &bad) != CWP_NONE, "noise produces a verdict");

	/* ------------------------------------------------------------------
	 * 2. One damaged key is counted and NOT scored
	 * --------------------------------------------------------------- */
	cwp_init(&t);
	m = cwp_seen(&t, 1, 7, FORGED4, 0x1884, 0, 0x64, 1000, &ev);
	CHECK(m & CWP_ONE_GROUP, "one forged key IS classified as ONE_GROUP");
	CHECK(ev.fresh == 0, "but it earns no report: one key is 1.52% likely on an honest bouquet");
	CHECK(ev.win_one == 1 && ev.win_all == 1, "it is counted in the window: 1 of 1");
	CHECK(ev.seen_one == 1, "and in the lifetime counter");
	CHECK(ev.bad_group == 4 && ev.groups == 3, "with the failing group and the intact count in the evidence");

	/* ------------------------------------------------------------------
	 * 3. The pattern: all four clauses
	 * --------------------------------------------------------------- */
	/* Three forged keys out of three: count and rate both satisfied. */
	cwp_init(&t);
	m = cwp_seen(&t, 1, 7, FORGED4, 0x1884, 0, 0x64, 1000, &ev);
	CHECK(ev.fresh == 0, "key 1 of 3: still nothing reported");
	m = cwp_seen(&t, 1, 7, FORGED4, 0x1884, 0, 0x64, 1100, &ev);
	CHECK(ev.fresh == 0, "key 2 of 3: still nothing");
	m = cwp_seen(&t, 1, 7, FORGED4, 0x1884, 0, 0x64, 1200, &ev);
	CHECK((m & CWP_ONE_GROUP) && (ev.fresh & CWP_ONE_GROUP),
	      "key 3 of 3 (100% of the window): the pattern is claimed");
	CHECK(ev.win_rate == 100 && ev.win_one == 3 && ev.win_all == 3,
	      "and the evidence says 3 of 3, 100%");

	/*
	 * The rate clause on its own: plenty of keys, three of them forged, but
	 * never 20 % of the window. The honest keys must come FIRST -- three
	 * forged keys at the start of an empty window are 100 % of it and would
	 * (correctly) be claimed as a pattern, which is a different test.
	 */
	cwp_init(&t);
	for (i = 0; i < 20; i++)
		m = cwp_seen(&t, 2, 9, GOOD, 0x1884, 0, 0x64,
		             2000 + (uint32_t)i * 10, &ev);
	for (i = 0; i < 3; i++) {
		cwp_seen(&t, 2, 9, GOOD, 0x1884, 0, 0x64, 2300 + (uint32_t)i * 20, &ev);
		m = cwp_seen(&t, 2, 9, FORGED4, 0x1884, 0, 0x64, 2310 + (uint32_t)i * 20, &ev);
	}
	CHECK(ev.win_one == 3 && ev.win_all == 26 && ev.fresh == 0,
	      "3 forged keys spread through 26 (11%) never reach the 20% rate: a busy source is not accused");
	CHECK(ev.win_rate == 11, "and the rate the line would print is 11%");
	CHECK((m & CWP_ONE_GROUP) != 0, "while the key itself is still classified ONE_GROUP");

	/* The count clause on its own: 100% forged, but only two of them. */
	cwp_init(&t);
	cwp_seen(&t, 3, 11, FORGED4, 0x1884, 0, 0x64, 3000, &ev);
	m = cwp_seen(&t, 3, 11, FORGED4, 0x1884, 0, 0x64, 3100, &ev);
	CHECK(ev.fresh == 0 && ev.win_one == 2,
	      "two forged keys at 100% of the window are still not a pattern");

	/* The same-group clause: three forged keys, 100% of the window, two groups. */
	cwp_init(&t);
	cwp_seen(&t, 4, 13, FORGED4, 0x1884, 0, 0x64, 4000, &ev);
	cwp_seen(&t, 4, 13, FORGED2, 0x1884, 0, 0x64, 4100, &ev);
	m = cwp_seen(&t, 4, 13, FORGED4, 0x1884, 0, 0x64, 4200, &ev);
	CHECK(ev.win_one == 3 && ev.fresh == 0,
	      "three forged keys spread over two different groups are not ONE damaged byte: no report");
	CHECK(ev.win_all == 3, "they are still counted, so the operator sees the rate");

	/* ...but three in the SAME group, after a mixed one, still convicts. */
	cwp_init(&t);
	cwp_seen(&t, 5, 15, FORGED4, 0x1884, 0, 0x64, 5000, &ev);
	cwp_seen(&t, 5, 15, FORGED2, 0x1884, 0, 0x64, 5100, &ev);
	m = cwp_seen(&t, 5, 15, FORGED4, 0x1884, 0, 0x64, 5200, &ev);
	CHECK(ev.fresh == 0, "a mixed window stays quiet even at 3 of 3");

	/* ------------------------------------------------------------------
	 * 4. The window and the suppression
	 * --------------------------------------------------------------- */
	/* 200 forged keys in one window: one report, 200 counted. */
	cwp_init(&t);
	{
		int reports = 0;
		for (i = 0; i < 200; i++) {
			cwp_seen(&t, 6, 17, FORGED4, 0x1884, 0, 0x64,
			         6000 + (uint32_t)i * 200, &ev);
			if (ev.fresh & CWP_ONE_GROUP) reports++;
		}
		CHECK(reports == 1, "200 forged keys inside one window earn exactly ONE report");
		CHECK(ev.seen_one == 200, "while all 200 are counted (the rate stays truthful)");
		CHECK(ev.reports == 1, "and the evidence says one report was made");
	}

	/* The window rolls: the same forgery a minute later is news again. */
	m = cwp_seen(&t, 6, 17, FORGED4, 0x1884, 0, 0x64, 6000 + 200 * 200 + 60001, &ev);
	CHECK(ev.win_all == 1,
	      "after CWP_WINDOW the window rolls over: this key is key 1 of a fresh window");
	CHECK(ev.fresh == 0, "and one key in a fresh window is not a pattern");

	/*
	 * A source that is honest and THEN starts forging: the pattern has to form
	 * against a background of good keys, which is the only realistic shape --
	 * and it must fire on the key that pushes the share over the line, not
	 * later.
	 */
	cwp_init(&t);
	for (i = 0; i < 10; i++)
		m = cwp_seen(&t, 7, 19, GOOD, 0x1884, 0, 0x64, 7000 + (uint32_t)i * 10, &ev);
	CHECK(ev.win_all == 10 && ev.win_one == 0 && ev.seen_full == 10,
	      "ten honest keys: 10 full, 0 one-group, no report");
	CHECK(ev.fresh == 0, "and nothing to say");
	m = cwp_seen(&t, 7, 19, FORGED4, 0x1884, 0, 0x64, 7100, &ev);
	CHECK((m & CWP_ONE_GROUP) != 0 && ev.fresh == 0 && ev.win_rate == 9,
	      "forged key 1 of 11 (9%): classified, counted, silent");
	m = cwp_seen(&t, 7, 19, FORGED4, 0x1884, 0, 0x64, 7110, &ev);
	CHECK(ev.fresh == 0 && ev.win_rate == 16,
	      "forged key 2 of 12 (16%): still under the 20% line, still silent");
	m = cwp_seen(&t, 7, 19, FORGED4, 0x1884, 0, 0x64, 7120, &ev);
	CHECK((ev.fresh & CWP_ONE_GROUP) != 0 && ev.win_rate == 23 && ev.win_one == 3,
	      "forged key 3 of 13 (23%) crosses the line and the pattern is claimed on THAT key");

	/* ------------------------------------------------------------------
	 * 5. The table
	 * --------------------------------------------------------------- */
	/* Windows never mix between sources: same slot pressure, different keys. */
	cwp_init(&t);
	cwp_seen(&t, 10, 1, FORGED4, 0x1884, 0, 0x64, 10000, &ev);
	cwp_seen(&t, 10, 2, FORGED4, 0x1884, 0, 0x64, 10100, &ev);
	m = cwp_seen(&t, 10, 2, FORGED4, 0x1884, 0, 0x64, 10200, &ev);
	CHECK(ev.win_all == 2 && ev.fresh == 0,
	      "two sources with the same srctype keep separate windows");
	CHECK(ev.srctype == 10 && ev.srcid == 2, "and the evidence names the source it belongs to");

	/* A full table must keep tracking new sources, not stop seeing them. */
	cwp_init(&t);
	for (i = 0; i < CWP_SLOTS + 8; i++)
		cwp_seen(&t, 20, i, GOOD, 0x1884, 0, 0x64, 11000 + (uint32_t)i * 100, &ev);
	{
		struct cwp_slot *newest = cwp_slot_for(&t, 20, CWP_SLOTS + 7, 11000 + (uint32_t)(CWP_SLOTS + 7) * 100);
		CHECK(newest != NULL,
		      "past CWP_SLOTS sources the newest source still gets a slot (the oldest is evicted)");
		CHECK(newest && newest->srctype == 20 && newest->srcid == CWP_SLOTS + 7,
		      "and the slot really holds that source");
	}
	m = cwp_seen(&t, 20, CWP_SLOTS + 8, FORGED4, 0x1884, 0, 0x64,
	             11000 + (uint32_t)(CWP_SLOTS + 8) * 100, &ev);
	CHECK(ev.win_all == 1 && ev.win_one == 1 && ev.seen_one == 1,
	      "a source arriving on a full table is tracked from its first key, not dropped");

	/* ------------------------------------------------------------------
	 * 6. The sentence
	 * --------------------------------------------------------------- */
	cwp_init(&t);
	for (i = 0; i < 3; i++)
		m = cwp_seen(&t, 30, 5, FORGED4, 0x1884, 0, 0x64, 13000 + (uint32_t)i * 10, &ev);
	{
		char line[512];
		int n = cwp_format(&ev, line, (int)sizeof(line));
		CHECK(n > 0 && n < (int)sizeof(line), "the evidence formats into a 512-byte buffer untruncated");
		CHECK(strstr(line, "source 30/5") != NULL, "the line names the source");
		CHECK(strstr(line, "ch 1884:000000:0064") != NULL, "and the channel it was serving (GR4)");
		CHECK(strstr(line, "group 4") != NULL, "and WHICH group is broken");
		CHECK(strstr(line, "3 of its last 3 keys") != NULL, "and the count that convicted it");
		CHECK(strstr(line, "100%") != NULL, "and the rate");
		CHECK(strstr(line, "trust signal only") != NULL,
		      "and says in words that delivery is unchanged (GR8: no silent action)");

		/* A short buffer must truncate, never run over. */
		{
			char small[48];
			int r = cwp_format(&ev, small, (int)sizeof(small));
			CHECK(r > 0 && strlen(small) == sizeof(small) - 1,
			      "into a 48-byte buffer the sentence truncates to exactly 47 characters");
		}
		CHECK(cwp_format(&ev, NULL, 0) == 0, "and a NULL buffer is refused rather than written to");

		/* No fresh verdict, no sentence. */
		ev.fresh = 0;
		CHECK(cwp_format(&ev, line, (int)sizeof(line)) == 0,
		      "evidence without a fresh verdict produces no line at all");
	}

	/* The mask names are what the counters are keyed on. */
	CHECK(!strcmp(cwp_name(CWP_NONE), "NONE"), "the mask names are stable");
	CHECK(!strcmp(cwp_name(CWP_ONE_GROUP), "ONE_GROUP"), "ONE_GROUP is spelled the same everywhere");
	CHECK(!strcmp(cwp_name(CWP_ONE_GROUP | CWP_WEAK), "ONE_GROUP|WEAK"), "and combinations too");

	/*
	 * The policy constants, pinned with their reason: CWP_SCOREMASK is what
	 * separates evidence from noise, and a change to it is a decision someone
	 * must make on purpose.
	 */
	CHECK(CWP_SCOREMASK == CWP_ONE_GROUP,
	      "only ONE_GROUP may score; a key that never had the shape is not evidence");
	CHECK(CWP_MIN_ONEGROUP == 3 && CWP_RATE_PCT == 20,
	      "the pattern needs 3 keys and 20% of the window");
	CHECK(CWP_GROUPS == 4 && CWP_WINDOW == 60000u,
	      "four groups (dcw.c:35) and a 60 s window");

	printf("\n== %d/%d passed, %d failed ==\n", passed, passed + failed, failed);
	return failed ? 1 : 0;
}
