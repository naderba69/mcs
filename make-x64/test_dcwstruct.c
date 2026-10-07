/*
 * test_dcwstruct.c -- TASK 1.8
 *
 * Exercises the REAL ../src/dcwstruct.h. The scan calls the REAL
 * checksumDCW()/isbadDCW() from ../src/dcw.c (linked in, not reimplemented): a
 * second copy of a checksum rule would be a second thing to keep in step, and
 * the whole point of this task is that the pre-filters stay the upstream ones.
 *
 * The keys below are not arbitrary. Each half is built so that exactly one
 * structural test can fire, and the suite asserts that it is checksum-valid
 * before it asserts anything about the scan. If the arithmetic that built a key
 * is wrong, the guard assertion fails first and says so, instead of the scan
 * quietly reporting a bit that the key was never meant to carry.
 *
 *   make -C make-x64 test
 */

#include <stdint.h>   /* dcw.h uses uint8_t and includes nothing itself */
#include <stdio.h>
#include <string.h>

#include "../src/dcw.h"
#include "../src/dcwstruct.h"

static int pass_n = 0, fail_n = 0;

#define CHECK(cond, name) do { \
	if (cond) { pass_n++; printf("  [ ok ] %s\n", name); } \
	else { fail_n++; printf("  [FAIL] %s\n", name); } \
} while (0)

/*
 * A clean half 1 and a clean half 2, both checksum-valid, both with eight
 * distinct bytes so that no per-half test can fire on them. Half 1 is also
 * non-monotone on purpose: a constant step would trip DCWS_MONO.
 */
static const uint8_t k_clean [16] = {0xA1, 0xB2, 0xC3, 0x16, 0xA5, 0xA6, 0xA7, 0xF2, 0xD4, 0xE5, 0xF6, 0xAF, 0x18, 0x28, 0x38, 0x78};
static const uint8_t k_halves[16] = {0xA1, 0xB2, 0xC3, 0x16, 0xA5, 0xA6, 0xA7, 0xF2, 0xA1, 0xB2, 0xC3, 0x16, 0xA5, 0xA6, 0xA7, 0xF2};
static const uint8_t k_inv   [16] = {0xA1, 0xB2, 0xC3, 0x16, 0xA5, 0xA6, 0xA7, 0xF2, 0x5E, 0x4D, 0x3C, 0xE9, 0x5A, 0x59, 0x58, 0x0D};
static const uint8_t k_nds   [16] = {0xA1, 0xB2, 0xC3, 0x16, 0xA5, 0xA6, 0xA7, 0xF2, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
/*
 * A half with a constant step of 0x20 that ALSO satisfies the checksum. That is
 * not free: a+3d must equal (a + (a+d) + (a+2d)) mod 256 and a+7d must equal the
 * sum of the three before it, which forces 2a = 0 and 2a = -8d (mod 256) -- so
 * a is 0x00 or 0x80 and the step is a multiple of 32. 0x80 with a step of 0x20
 * is used because it leaves no zero byte in the first three.
 */
static const uint8_t k_mono  [16] = {0x80, 0xA0, 0xC0, 0xE0, 0x00, 0x20, 0x40, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const uint8_t k_monof [16] = {0x80, 0xA0, 0xC0, 0xE0, 0x00, 0x20, 0x40, 0x60, 0xD4, 0xE5, 0xF6, 0xAF, 0x18, 0x28, 0x38, 0x78};
static const uint8_t k_badck [16] = {0xA1, 0xB2, 0xC3, 0x99, 0xA5, 0xA6, 0xA7, 0xF2, 0xD4, 0xE5, 0xF6, 0xAF, 0x18, 0x28, 0x38, 0x78};
static const uint8_t k_rep   [16] = {0x5A, 0x5A, 0x5A, 0x0E, 0x11, 0x22, 0x33, 0x66, 0xD4, 0xE5, 0xF6, 0xAF, 0x18, 0x28, 0x38, 0x78};
static const uint8_t k_same  [16] = {0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0xD4, 0xE5, 0xF6, 0xAF, 0x18, 0x28, 0x38, 0x78};
static const uint8_t k_low   [16] = {0x40, 0xC0, 0x40, 0x40, 0xC0, 0x40, 0xC0, 0xC0, 0xD4, 0xE5, 0xF6, 0xAF, 0x18, 0x28, 0x38, 0x78};

/* A key that differs from k_clean in one byte, and only in that byte. */
static uint8_t k_other[16];

static unsigned scan(const uint8_t *k) { return dcwstruct_scan(k, 1, 1); }

/* ------------------------------------------------------------------ */

static void t_premises(void)
{
	printf("  -- the premises the rest of the suite rests on --\n");
	CHECK(checksumDCW((uint8_t *)k_clean) == 1,
	      "k_clean passes the upstream checksum test (guards the arithmetic that built it)");
	CHECK(dcwstruct_half_same(k_clean) == 0 && dcwstruct_half_same(k_clean + 8) == 0,
	      "neither k_clean half is one repeated byte");
	CHECK(dcwstruct_half_mono(k_clean) == 0 && dcwstruct_half_mono(k_clean + 8) == 0,
	      "neither k_clean half is a constant step");
	CHECK(dcwstruct_half_lowent(k_clean) == 0 && dcwstruct_half_lowent(k_clean + 8) == 0,
	      "neither k_clean half is a two-value half");
	CHECK(scan(k_clean) == 0, "a clean key scans to zero: the fast path costs one pass and says nothing");
}

static void t_single_bits(void)
{
	printf("  -- one test at a time --\n");
	CHECK(scan(k_halves) == DCWS_HALVES,
	      "identical halves -> DCWS_HALVES and nothing else");
	CHECK((scan(k_inv) & DCWS_INV) && scan(k_inv) == (DCWS_INV | DCWS_CHECKSUM),
	      "inverted halves -> DCWS_INV (plus CHECKSUM: an inverted key can never satisfy it)");
	CHECK(dcwstruct_scan(k_inv, 0, 1) == DCWS_INV,
	      "and with the checksum gate off, the inversion is the only bit left");
	CHECK((scan(k_rep) & DCWS_REPEAT) && scan(k_rep) == DCWS_REPEAT,
	      "three equal leading bytes -> DCWS_REPEAT only");
	CHECK(dcwstruct_scan(k_rep, 1, 0) == 0,
	      "the same key with the repeat filter off -> no bit at all: a disabled filter is not evidence");
	CHECK((scan(k_same) & DCWS_SAME) && scan(k_same) == (DCWS_SAME | DCWS_REPEAT),
	      "one repeated byte half -> DCWS_SAME (and REPEAT: 0x80 three times is a repeat too)");
	CHECK(dcwstruct_scan(k_same, 1, 0) == DCWS_SAME,
	      "same key, repeat filter off -> DCWS_SAME survives on its own");
	CHECK(scan(k_low) == DCWS_LOWENT,
	      "a two-value half -> DCWS_LOWENT only, and not on the clean half");
	CHECK(scan(k_monof) == DCWS_MONO,
	      "a constant-step half -> DCWS_MONO only");
	CHECK(scan(k_badck) == DCWS_CHECKSUM,
	      "a bad checksum in one byte -> DCWS_CHECKSUM only");
	CHECK(dcwstruct_scan(k_badck, 0, 1) == 0,
	      "and with the checksum gate off, nothing at all");
}

static void t_nds(void)
{
	printf("  -- NDS half keys: a zero half is a shape, not an anomaly --\n");
	CHECK(scan(k_nds) == 0,
	      "clean half + all-zero half -> no signal, so NDS keys cost nothing");
	CHECK(scan(k_monof) == DCWS_MONO && scan(k_mono) == DCWS_MONO,
	      "the same half, zeroed and not zeroed, gives the same verdict: only the other half is judged");
	CHECK(dcwstruct_half_same(k_nds + 8) == 1,
	      "the zeroed half WOULD be DCWS_SAME -- which is exactly why the per-half tests skip it");
	CHECK((dcwstruct_scan(k_nds, 1, 1) & DCWS_SAME) == 0,
	      "and it does not leak into the mask through the cross-half tests either");
}

static void t_scoremask(void)
{
	unsigned m;
	printf("  -- which bits may move a score --\n");
	CHECK((DCWS_SCOREMASK & DCWS_REPEAT) == 0,
	      "DCWS_REPEAT is excluded: at 1 in 16 000 random keys it would decay a healthy source on noise");
	CHECK((DCWS_SCOREMASK & (DCWS_CHECKSUM | DCWS_SAME | DCWS_MONO | DCWS_LOWENT | DCWS_HALVES | DCWS_INV))
	      == (DCWS_CHECKSUM | DCWS_SAME | DCWS_MONO | DCWS_LOWENT | DCWS_HALVES | DCWS_INV),
	      "the other six bits all count");
	m = scan(k_rep);
	CHECK(m != 0 && (m & DCWS_SCOREMASK) == 0,
	      "a key whose ONLY finding is REPEAT produces no trust event at all");
	m = scan(k_same);
	CHECK((m & DCWS_SCOREMASK) == DCWS_SAME,
	      "and a key with REPEAT plus a real anomaly produces exactly one event's worth of evidence");
}

static void t_names(void)
{
	char b[64];
	printf("  -- the log token --\n");
	dcwstruct_name(0, b, sizeof(b));
	CHECK(b[0] == 0, "no bits -> an empty string, not a stray separator");
	dcwstruct_name(DCWS_MONO, b, sizeof(b));
	CHECK(!strcmp(b, "MONO"), "one bit -> MONO");
	dcwstruct_name(DCWS_SAME | DCWS_LOWENT, b, sizeof(b));
	CHECK(!strcmp(b, "SAME+LOWENT"), "two bits, in bit order, joined by +");
	dcwstruct_name(0x7F, b, sizeof(b));
	CHECK(!strcmp(b, "CHECKSUM+REPEAT+SAME+MONO+LOWENT+HALVES+INV"),
	      "all seven bits -> every name, in order");
	CHECK(strlen(b) == 43, "and the longest possible token is 43 characters");
	dcwstruct_name(0x7F, b, 8);
	CHECK(strlen(b) == 7 && b[7] == 0,
	      "a short buffer is filled to the last byte and NUL-terminated, never overrun");
}

static void t_table(void)
{
	struct dcwstruct_table t;
	int r;
	printf("  -- repeat suppression --\n");
	dcwstruct_init(&t);
	CHECK(t.suppressed == 0, "a fresh table has suppressed nothing");

	r = dcwstruct_seen_once(&t, DCWS_MONO, 1, 100, 0x1884, 0, 0x64, k_mono, 1000);
	CHECK(r == DCWSTRUCT_REPORT_NEW, "first sighting of a key is reported");
	r = dcwstruct_seen_once(&t, DCWS_MONO, 1, 100, 0x1884, 0, 0x64, k_mono, 1001);
	CHECK(r == DCWSTRUCT_SUPPRESS, "the same key, same source, same channel -> suppressed");
	CHECK(t.suppressed == 1, "and the suppression is counted, not silent");
	/*
	 * The suppression at tick 1001 does NOT move the entry's timestamp: the
	 * window runs from the last report, so the call below is one window after
	 * the report at 1000 and must be reported again. Had the window slid with
	 * every sighting, this would stay silent forever -- and a source pushing
	 * the same impossible key for hours is precisely what must not stay silent.
	 */
	r = dcwstruct_seen_once(&t, DCWS_MONO, 1, 100, 0x1884, 0, 0x64, k_mono,
	                        1000 + DCWSTRUCT_WINDOW);
	CHECK(r == DCWSTRUCT_REPORT_AGAIN,
	      "still arriving a window after it was last reported -> reported again, not silenced");

	r = dcwstruct_seen_once(&t, DCWS_MONO, 2, 100, 0x1884, 0, 0x64, k_mono, 1000);
	CHECK(r == DCWSTRUCT_REPORT_NEW, "the same key from a DIFFERENT source is a different fact (GR1)");
	r = dcwstruct_seen_once(&t, DCWS_MONO, 1, 100, 0x1884, 0, 0xC8, k_mono, 1000);
	CHECK(r == DCWSTRUCT_REPORT_NEW, "and on a different channel it is a different fact too (GR4)");
	r = dcwstruct_seen_once(&t, DCWS_LOWENT, 1, 100, 0x1884, 0, 0x64, k_mono, 1000);
	CHECK(r == DCWSTRUCT_REPORT_NEW,
	      "the same key re-judged differently after a filter change is new information");
	r = dcwstruct_seen_once(&t, DCWS_MONO, 1, 100, 0x1884, 0, 0x64, k_other, 1000);
	CHECK(r == DCWSTRUCT_REPORT_NEW, "a different key from the same source is reported");

	CHECK(dcwstruct_seen_once(NULL, DCWS_MONO, 1, 1, 0x1884, 0, 0x64, k_mono, 0)
	      == DCWSTRUCT_REPORT_NEW,
	      "a NULL table reports rather than swallows: a bug here must not hide evidence");
}

static void t_evict(void)
{
	struct dcwstruct_table t;
	int i, r;
	unsigned char keys[DCWSTRUCT_SLOTS + 1][16];
	printf("  -- the table is bounded and forgets the oldest --\n");
	dcwstruct_init(&t);
	for (i = 0; i <= DCWSTRUCT_SLOTS; i++) {
		memcpy(keys[i], k_mono, 16);
		keys[i][0] = (unsigned char)(0x10 + i);   /* distinct keys, same source/channel */
	}
	for (i = 0; i < DCWSTRUCT_SLOTS; i++)
		r = dcwstruct_seen_once(&t, DCWS_MONO, 1, 100, 0x1884, 0, 0x64, keys[i], (uint32_t)(1000 + i));
	CHECK(r == DCWSTRUCT_REPORT_NEW, "the table takes DCWSTRUCT_SLOTS distinct keys");
	/*
	 * keys[0] is seen again here and suppressed. Because suppression does not
	 * move a timestamp, keys[0] is still the longest-unseen entry -- which
	 * makes the eviction victim below predictable, so these assertions would
	 * catch a change of policy in either direction.
	 */
	r = dcwstruct_seen_once(&t, DCWS_MONO, 1, 100, 0x1884, 0, 0x64, keys[0], 5000);
	CHECK(r == DCWSTRUCT_SUPPRESS, "while they are all still remembered, they are all still suppressed");
	r = dcwstruct_seen_once(&t, DCWS_MONO, 1, 100, 0x1884, 0, 0x64, keys[DCWSTRUCT_SLOTS], 6000);
	CHECK(r == DCWSTRUCT_REPORT_NEW, "one more key still fits, by evicting the longest-unseen");
	r = dcwstruct_seen_once(&t, DCWS_MONO, 1, 100, 0x1884, 0, 0x64, keys[0], 6001);
	CHECK(r == DCWSTRUCT_REPORT_NEW,
	      "the evicted entry is reported again rather than forgotten forever");
	/*
	 * Re-inserting keys[0] at 6001 was itself an insertion into a full table, so
	 * it evicted the next longest-unseen entry, which is keys[1] (stamp 1001).
	 * Both halves of that are asserted, because "evicts the oldest" is only a
	 * real policy if the oldest is the one that goes.
	 */
	r = dcwstruct_seen_once(&t, DCWS_MONO, 1, 100, 0x1884, 0, 0x64, keys[1], 6002);
	CHECK(r == DCWSTRUCT_REPORT_NEW, "the next oldest went when the table filled again");
	/*
	 * keys[3], not keys[2]: the call above was itself an insertion into a full
	 * table, so it evicted keys[2]. Every REPORT_NEW against a full table takes
	 * one entry out, which is why a survivor has to be named with that in mind.
	 * A check that matches inserts nothing, so this one is stable.
	 */
	r = dcwstruct_seen_once(&t, DCWS_MONO, 1, 100, 0x1884, 0, 0x64, keys[3], 6003);
	CHECK(r == DCWSTRUCT_SUPPRESS,
	      "while an entry that was never the oldest stayed remembered");
	CHECK(sizeof(struct dcwstruct_table) < 1024,
	      "the whole table stays under a kilobyte (GR9: bounded, no allocation)");
}

int main(void)
{
	printf("test_dcwstruct -- TASK 1.8 structural pre-filters as soft signals\n");
	printf("  sizeof(struct dcwstruct_entry) = %u\n", (unsigned)sizeof(struct dcwstruct_entry));
	memset(k_other, 0, 16);
	memcpy(k_other, k_mono, 16);
	k_other[7] ^= 0x01;

	t_premises();
	t_single_bits();
	t_nds();
	t_scoremask();
	t_names();
	t_table();
	t_evict();

	printf("\n== %d/%d passed, %d failed ==\n", pass_n, pass_n + fail_n, fail_n);
	return fail_n ? 1 : 0;
}
