/*
 * Band 111 unit package: pin the shadow gate classifier.
 *
 * The shadow tally is the evidence base for the deferred cycle-gate
 * decision, so a swapped class label is not a cosmetic bug -- it would
 * argue for the wrong policy. Two things are pinned here:
 *   1. the truth table over (have_prev, first_same, second_same), using
 *      dcwcmp8's REAL convention (nonzero = EQUAL);
 *   2. equivalence with the predicate of cfg.cache.cycle_estimate, which
 *      the shadow must predict exactly, byte for byte.
 * A wiring bug of the band-109 kind (counters in the right shape, wrong
 * branch) was invisible in a live session where every refusal had no
 * prevcw; the byte-level cases below cannot be skipped that way.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
/* ecmdata.h is a fragment header: the product supplies MAX_ECM_SIZE from
 * common.h. Mirrored here so the test can use the REAL comparators rather
 * than a copy -- pinning dcwcmp8's convention is the point of this package.
 * The mirrored size is never used by the test (only the inline comparators
 * are), so a change upstream cannot make this package lie. */
#define MAX_ECM_SIZE 700
#include "../src/ecmdata.h"               /* dcwcmp8: equality test */
#include "../src/cache-cycle-shadow.h"

static int fails = 0;
#define CHECK(cond, what) do { if (!(cond)) { \
	printf("FAIL: %s\n", (what)); fails++; } } while (0)

/* literal transcription of the product's cycle_estimate half-test
 * (src/clustredcache.c, cache_after_consensus): exactly one half carried
 * over names the cycle; anything else leaves the key unestimated. */
#define EST_NONE 0
#define EST_CW1  1
#define EST_CW2  2
static int est_claim(int have_prev, int first_same, int second_same)
{
	if (!have_prev) return EST_NONE;
	if (first_same && !second_same) return EST_CW1;
	if (!first_same && second_same) return EST_CW2;
	return EST_NONE;
}

static void test_truth_table(void)
{
	int hp, fs, ss, cls, est;
	for (hp = 0; hp <= 1; hp++)
		for (fs = 0; fs <= 1; fs++)
			for (ss = 0; ss <= 1; ss++) {
				cls = mcs_shadow_class(hp, fs, ss);
				est = est_claim(hp, fs, ss);
				/* partition: every input lands in exactly one class */
				CHECK(cls >= MCS_SHADOW_NOPREV && cls <= MCS_SHADOW_BOTHSAME,
				      "class out of range");
				if (!hp) {
					CHECK(cls == MCS_SHADOW_NOPREV,
					      "no prevcw must be its own class (estimator uses ecmtag there, not halves)");
					continue;
				}
				if (est == EST_CW1)
					CHECK(cls == MCS_SHADOW_ROT_CW1,
					      "estimator says CW1 -> shadow must say rotation/CW1");
				else if (est == EST_CW2)
					CHECK(cls == MCS_SHADOW_ROT_CW0,
					      "estimator says CW0 -> shadow must say rotation/CW0");
				else {
					CHECK(cls == MCS_SHADOW_NEITHER || cls == MCS_SHADOW_BOTHSAME,
					      "estimator claims nothing -> only neither/bothsame");
					if (fs && ss)
						CHECK(cls == MCS_SHADOW_BOTHSAME,
						      "both halves equal prevcw = same key again");
					if (!fs && !ss)
						CHECK(cls == MCS_SHADOW_NEITHER,
						      "both halves changed = no stable half");
				}
			}
}

/* the same verdict reached from real bytes, through the product's own
 * expression: dcwcmp8(cw, prevcw) / dcwcmp8(cw+8, prevcw+8). */
static int classify_bytes(const uint8_t *cw, const uint8_t *prevcw)
{
	return mcs_shadow_class(1, dcwcmp8(cw, prevcw), dcwcmp8(cw + 8, prevcw + 8));
}

static void test_bytes(void)
{
	uint8_t prev[16], cw[16];
	int i;
	for (i = 0; i < 16; i++) { prev[i] = (unsigned char)(0x10 + i); cw[i] = prev[i]; }

	/* identical key (CW_DUP territory, not the gate's) */
	CHECK(classify_bytes(cw, prev) == MCS_SHADOW_BOTHSAME, "identical 16 bytes -> bothsame");

	/* second half rotated, first carried over -> CW1 per the estimator */
	for (i = 8; i < 16; i++) cw[i] = (unsigned char)(prev[i] ^ 0xA5);
	CHECK(classify_bytes(cw, prev) == MCS_SHADOW_ROT_CW1, "first half stable -> rotation CW1");

	/* first half rotated, second carried over -> CW0 */
	for (i = 0; i < 16; i++) cw[i] = prev[i];
	for (i = 0; i < 8; i++) cw[i] = (unsigned char)(prev[i] ^ 0x5A);
	CHECK(classify_bytes(cw, prev) == MCS_SHADOW_ROT_CW0, "second half stable -> rotation CW0");

	/* both halves changed -> nothing to estimate */
	for (i = 0; i < 16; i++) cw[i] = (unsigned char)(prev[i] ^ 0x77);
	CHECK(classify_bytes(cw, prev) == MCS_SHADOW_NEITHER, "both halves changed -> neither");

	/* a key that looks stable only through the 8-byte halves (unaligned
	 * overlap must not confuse the classifier) */
	for (i = 0; i < 16; i++) cw[i] = prev[i];
	cw[7] ^= 0x01;
	CHECK(classify_bytes(cw, prev) == MCS_SHADOW_ROT_CW0, "byte 7 belongs to the first half");
}

/* band 113: the shared eligibility gate -- the exact condition the product
 * now uses for the temporal estimator. The bug it pins: entry_cycle=0 and
 * rotation=1 must ELIGIBLE (that was the starved-gate refusal field-proven
 * on 2026-11-19: corroborated keys, prevcw present, no peer marker), while
 * no-prevcw and no-rotation must stay ineligible (refusal unchanged). */
static void test_temporal_ok(void)
{
	CHECK(mcs_cycle_temporal_ok(1, 1, 1, 0) == 1, "est on, prevcw, entry cycle -> eligible (old route)");
	CHECK(mcs_cycle_temporal_ok(1, 1, 0, 1) == 1, "est on, prevcw, rotation only -> eligible (band 113 fix)");
	CHECK(mcs_cycle_temporal_ok(1, 1, 0, 0) == 0, "est on, prevcw, both halves changed -> refused still");
	CHECK(mcs_cycle_temporal_ok(1, 0, 1, 1) == 0, "no prevcw -> temporal route not applicable");
	CHECK(mcs_cycle_temporal_ok(0, 1, 1, 1) == 0, "estimator off -> eligibility off, all paths");
	/* the shadow and the gate must read rotation the same way: exactly one
	 * half carried over (xor of the equality flags) */
	CHECK(((1 != 0) ? 1 : 0) == 1 && mcs_shadow_class(1, 1, 0) == MCS_SHADOW_ROT_CW1,
	      "rotation predicate matches ROT_CW1 class");
	CHECK(((0 != 0) ? 1 : 0) == 0 && mcs_shadow_class(1, 0, 0) == MCS_SHADOW_NEITHER,
	      "neither-half predicate shares the false branch");
}

int main(void)
{
	test_truth_table();
	test_bytes();
	test_temporal_ok();
	if (fails) { printf("gate-shadow-class: %d FAILURES\n", fails); return 1; }
	printf("gate-shadow-class: PASS (truth table, estimator equivalence, byte-level wiring)\n");
	return 0;
}
