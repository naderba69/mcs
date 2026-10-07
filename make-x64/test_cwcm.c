/*
 * TASK 2.7 -- the unit suite for complement-mirror detection (../src/cwcm.h).
 *
 * What is pinned here, in order of how much it would hurt to get wrong:
 *
 *   1. The arithmetic. The scan counts exactly the six free complement pairs;
 *      a repaired mirror reads 6/6 (or 5/6 when repaired in free space), an
 *      honest key reads at most 1 by chance, and the checksum arithmetic
 *      reproduced from dcw.c:35 confirms what the header claims: a pure
 *      mirror breaks exactly groups 3 and 4, each by 2. If the scan ever
 *      counts checksum-byte pairs, every repaired mirror in the world walks
 *      straight past it.
 *   2. The restraint. One mirror key -- even 6/6, even though its random-
 *      collision probability is ~8e-12 -- is counted and NOT scored: the
 *      pattern (3 keys, 20 %, one window) is what convicts, the same derived
 *      discipline as 2.4. The suite asserts the counter moves and the score
 *      does not.
 *   3. The pattern, all clauses: enough keys, enough share, one line per
 *      source per window, counters still counting while the line throttles.
 *   4. The table: keyed on (srctype, srcid), LRU eviction that never refuses
 *      the next source, two sources never share a window.
 *   5. The sentence: built from the evidence, names the construction and the
 *      pair count, and survives a short buffer.
 *   6. Degenerate inputs.
 */
#include "../src/cwcm.h"

#include <stdio.h>
#include <string.h>

static int passed = 0, failed = 0;

#define CHECK(cond, what) do { \
	if (cond) { passed++; printf("  [ ok ] %s\n", what); } \
	else { failed++; printf("  [FAIL] %s\n", what); } \
} while (0)

/* The four group sums, reproduced from dcw.c:35 for the arithmetic pins. */
static int sums4(const uint8_t *cw)
{
	return cw[3]  == (uint8_t)((cw[0] + cw[1] + cw[2]) & 0xFF) &&
	       cw[7]  == (uint8_t)((cw[4] + cw[5] + cw[6]) & 0xFF) &&
	       cw[11] == (uint8_t)((cw[8] + cw[9] + cw[10]) & 0xFF) &&
	       cw[15] == (uint8_t)((cw[12] + cw[13] + cw[14]) & 0xFF);
}

static void repair(uint8_t *cw)
{
	cw[3]  = (uint8_t)((cw[0] + cw[1] + cw[2]) & 0xFF);
	cw[7]  = (uint8_t)((cw[4] + cw[5] + cw[6]) & 0xFF);
	cw[11] = (uint8_t)((cw[8] + cw[9] + cw[10]) & 0xFF);
	cw[15] = (uint8_t)((cw[12] + cw[13] + cw[14]) & 0xFF);
}

/* A structurally honest key: two independent halves, each with valid sums. */
static void honest_key(uint8_t *k, uint8_t seed)
{
	int i;
	for (i = 0; i < 16; i++) k[i] = (uint8_t)(seed + i * 13 + (i % 3) * 57);
	repair(k);
}

/* A mirror key. Half 1 is a VALID half (its own two sums hold -- the builder
 * starts from a real half), half 2 is the complement of half 1. With
 * repair_sums the two broken groups (3 and 4) are fixed after the fact --
 * the invisible variant. With break_one_free one free pair is destroyed
 * (a mirror repaired in free space). */
static void mirror_key(uint8_t *k, uint8_t seed, int repair_sums, int break_one_free)
{
	int i;
	for (i = 0; i < 8; i++) k[i] = (uint8_t)(seed + i * 29 + (i % 2) * 41);
	k[3] = (uint8_t)((k[0] + k[1] + k[2]) & 0xFF);
	k[7] = (uint8_t)((k[4] + k[5] + k[6]) & 0xFF);
	for (i = 0; i < 8; i++) k[8 + i] = (uint8_t)~k[i];
	if (break_one_free) k[9] ^= 0x5A;          /* one free pair destroyed */
	if (repair_sums) repair(k);                /* groups 3 and 4, after the fact */
}

int main(void)
{
	struct cwcm_table t;
	struct cwcm_evidence ev;
	char line[512];
	uint8_t k[16];

	printf("test_cwcm\n");

	/* -- 1. the scan --------------------------------------------------------- */
	{
		int i, maxh = 0;
		honest_key(k, 0x10);
		CHECK(cwcm_pairs(k) <= 1, "an honest key shows at most one accidental pair");

		/* 200 varied honest keys: none may reach the verdict */
		for (i = 1; i <= 200; i++) {
			honest_key(k, (uint8_t)i);
			if (cwcm_pairs(k) > maxh) maxh = cwcm_pairs(k);
			if (cwcm_pairs(k) >= CWCM_MIN_PAIRS) break;
		}
		CHECK(cwcm_pairs(k) < CWCM_MIN_PAIRS,
		      "200 varied honest keys: none reaches the 5-pair verdict");

		mirror_key(k, 0x20, 0, 0);
		CHECK(cwcm_pairs(k) == 6, "a pure mirror carries all 6 complement pairs");
		CHECK(!sums4(k), "and -- the header's claim -- a pure mirror FAILS the group sums");
		{
			/* which groups break: 3 and 4, each by exactly 2 */
			uint8_t m3 = (uint8_t)((k[8] + k[9] + k[10]) & 0xFF);
			uint8_t m4 = (uint8_t)((k[12] + k[13] + k[14]) & 0xFF);
			CHECK(k[11] != m3 && k[15] != m4, "the broken groups are 3 and 4");
			CHECK((uint8_t)(k[11] - m3) == 2 || (uint8_t)(m3 - k[11]) == 2, "group 3 is off by exactly 2");
			CHECK((uint8_t)(k[15] - m4) == 2 || (uint8_t)(m4 - k[15]) == 2, "group 4 is off by exactly 2");
			{
				uint8_t h[16];
				memcpy(h, k, 16);
				CHECK(h[3] == (uint8_t)((h[0]+h[1]+h[2]) & 0xFF) && h[7] == (uint8_t)((h[4]+h[5]+h[6]) & 0xFF),
				      "groups 1 and 2 (half 1's own) hold");
			}
		}

		mirror_key(k, 0x30, 1, 0);
		CHECK(cwcm_pairs(k) == 6 && sums4(k),
		      "a REPAIRED mirror: 6/6 pairs AND all four sums -- invisible to the checksum layer");
		mirror_key(k, 0x40, 1, 1);
		CHECK(cwcm_pairs(k) == 5 && sums4(k),
		      "a mirror repaired in free space: 5/6 pairs, sums intact -- still caught");
		mirror_key(k, 0x50, 1, 0);
		k[13] ^= 0x80; k[10] ^= 0x33; repair(k);   /* TWO free pairs broken */
		CHECK(cwcm_pairs(k) == 4 && sums4(k),
		      "a key with 4 accidental pairs stays under the verdict (FP guard)");
	}

	/* -- 2. one mirror key is counted, not scored ----------------------------- */
	cwcm_init(&t);
	{
		unsigned m;
		mirror_key(k, 0x60, 1, 0);
		m = cwcm_seen(&t, 1, 10, k, 0x0604, 0, 0x0100, 1000, &ev);
		CHECK(m == CWCM_MIRROR, "the verdict is the mirror mask");
		CHECK(ev.fresh == 0, "and the first key scores NOTHING (restraint)");
		CHECK(ev.pairs == 6, "the evidence carries the pair count");
		mirror_key(k, 0x61, 1, 0);
		m = cwcm_seen(&t, 1, 10, k, 0x0604, 0, 0x0100, 1100, &ev);
		CHECK(m == CWCM_MIRROR && ev.fresh == 0, "the second mirror key is counted, still no line");
		CHECK(ev.win_mirror == 2 && ev.win_all == 2, "both are counted in the window");
	}

	/* -- 3. the pattern fires on the third, once ------------------------------- */
	{
		unsigned m;
		mirror_key(k, 0x62, 1, 0);
		m = cwcm_seen(&t, 1, 10, k, 0x0604, 0, 0x0100, 1200, &ev);
		CHECK(m == CWCM_MIRROR && ev.fresh == CWCM_MIRROR,
		      "the third mirror key crosses the pattern and fires");
		CHECK(ev.win_mirror == 3 && ev.win_all == 3, "3 of 3 in the window (100 % >= 20 %)");
		CHECK(ev.reports == 1, "one report announced");

		/* honest traffic inside the window dilutes but does not un-convict */
		honest_key(k, 0x7F);
		m = cwcm_seen(&t, 1, 10, k, 0x0604, 0, 0x0100, 1300, &ev);
		CHECK(m == CWCM_NONE && ev.fresh == 0, "an honest key in between is just counted");
		mirror_key(k, 0x63, 1, 0);
		m = cwcm_seen(&t, 1, 10, k, 0x0604, 0, 0x0100, 1400, &ev);
		CHECK(ev.fresh == 0, "a fourth mirror key inside the window stays throttled");
		CHECK(ev.reports == 1 && ev.win_mirror == 4 && ev.win_all == 5,
		      "the counters keep counting while the line is quiet");
		/* The next window starts empty by design: the pattern has to re-form
		 * there. Three more mirror keys, and the third fires. */
		/* 61301 clears both the traffic window AND the report throttle
		 * (rep_ticks is 1200; 61301 - 1200 >= 60000). */
		mirror_key(k, 0x64, 1, 0);
		m = cwcm_seen(&t, 1, 10, k, 0x0604, 0, 0x0100, 61301, &ev);
		CHECK(ev.fresh == 0 && ev.win_all == 1,
		      "the new window starts clean: one key is not yet a pattern");
		mirror_key(k, 0x65, 1, 0);
		cwcm_seen(&t, 1, 10, k, 0x0604, 0, 0x0100, 61302, &ev);
		mirror_key(k, 0x66, 1, 0);
		m = cwcm_seen(&t, 1, 10, k, 0x0604, 0, 0x0100, 61303, &ev);
		CHECK(ev.fresh == CWCM_MIRROR, "the pattern re-formed and fires again in the new window");
		CHECK(ev.reports == 2, "two windows, two reports");
	}

	/* -- 4. scope and isolation ------------------------------------------------ */
	cwcm_init(&t);
	{
		unsigned m1, m2;
		mirror_key(k, 0x70, 1, 0);
		m1 = cwcm_seen(&t, 1, 20, k, 0x0604, 0, 0x0100, 1000, &ev);
		mirror_key(k, 0x71, 1, 0);
		m2 = cwcm_seen(&t, 1, 21, k, 0x1801, 0, 0x0200, 1100, &ev);
		CHECK(m1 == CWCM_MIRROR && m2 == CWCM_MIRROR && ev.srcid == 21,
		      "two sources keep separate evidence");
		CHECK(ev.win_mirror == 1, "the second source's window is its own (no pattern yet)");
	}

	/* -- 5. eviction ------------------------------------------------------------- */
	cwcm_init(&t);
	{
		int i;
		for (i = 0; i < CWCM_SLOTS; i++) {
			honest_key(k, (uint8_t)(0x10 + i));
			cwcm_seen(&t, 1, 100 + i, k, 0x0604, 0, 0x0100, 1000 + (uint32_t)i, &ev);
		}
		mirror_key(k, 0x99, 1, 0);
		CHECK(cwcm_seen(&t, 1, 999, k, 0x0604, 0, 0x0100, 5000, &ev) == CWCM_MIRROR,
		      "a new source fits a full table (eviction, never refusal)");
		honest_key(k, 0x10);
		CHECK(cwcm_seen(&t, 1, 100, k, 0x0604, 0, 0x0100, 5100, &ev) == CWCM_NONE &&
		      ev.win_all == 1,
		      "the evicted (oldest) source restarts from a clean window");
	}

	/* -- 6. degenerate inputs ------------------------------------------------------ */
	cwcm_init(&t);
	CHECK(cwcm_seen(NULL, 1, 1, k, 1, 1, 1, 1, &ev) == CWCM_NONE, "no table: no verdict");
	CHECK(cwcm_seen(&t, 1, 1, NULL, 1, 1, 1, 1, &ev) == CWCM_NONE, "no key: no verdict");
	CHECK(cwcm_pairs(NULL) == 0, "the scan of no key is 0 pairs");
	{
		unsigned m = cwcm_seen(&t, 1, 2, k, 0x0604, 0, 0x0100, 1000, NULL);
		CHECK(m == CWCM_NONE || m == CWCM_MIRROR, "the note works with no evidence struct");
	}

	/* -- 7. the sentence ------------------------------------------------------------ */
	cwcm_init(&t);
	{
		int n;
		mirror_key(k, 0x80, 1, 0);
		cwcm_seen(&t, 1, 60, k, 0x0B00, 0x000331, 0x233D, 1000, &ev);
		cwcm_seen(&t, 1, 60, k, 0x0B00, 0x000331, 0x233D, 1100, &ev);
		cwcm_seen(&t, 1, 60, k, 0x0B00, 0x000331, 0x233D, 1200, &ev);
		n = cwcm_format(&ev, line, (int)sizeof(line));
		CHECK(n > 0, "the sentence renders");
		CHECK(strstr(line, "CW MIRROR:") == line, "it names the layer");
		CHECK(strstr(line, "1/60") != NULL, "it names the source");
		CHECK(strstr(line, "0b00:000331:233d") != NULL, "it names the channel");
		CHECK(strstr(line, "bitwise complement") != NULL, "it names the construction");
		CHECK(strstr(line, "6 of 6 byte pairs") != NULL, "it carries the pair count");
		CHECK(strstr(line, "REPAIRED mirror") != NULL, "it explains why the checksum layer passed it");
		CHECK(strstr(line, "cannot open its picture") != NULL, "it states the consequence");
		CHECK(strstr(line, "Delivery was not touched by this layer") != NULL,
		      "it says in words that delivery was not touched");
		n = cwcm_format(&ev, line, 32);
		CHECK(n >= 0 && strlen(line) < 32, "it survives a short buffer, terminated");
	}

	printf("  ----\n");
	printf("  %d ok, %d failed\n", passed, failed);
	return failed ? 1 : 0;
}
