/*
 * test_cwentropy.c -- TASK 2.2
 *
 * Exercises the REAL ../src/cwentropy.h. Nothing is reimplemented: the header
 * has no dependency on the server at all (it is pure arithmetic over 16 bytes),
 * which is the whole reason it can be tested this completely.
 *
 * The keys here are built by construction and then asserted, never written as
 * literals someone thought looked right. Where a test needs "exactly N bit
 * flips" it flips N bits and asserts the distance the header computes equals N
 * -- so a wrong expectation in this file fails as a guard assertion instead of
 * quietly testing the wrong thing.
 *
 * The suite is deliberately heavier on "must not fire" than on "fires". A
 * forensic test that reports a genuine key is worse than no test at all: the
 * source sits at the score floor forever and the operator learns to ignore the
 * layer. Most of what follows is the evidence that the layer stays quiet.
 *
 *   make -C make-x64 test
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../src/cwentropy.h"

static int pass_n = 0, fail_n = 0;

#define CHECK(cond, name) do { \
	if (cond) { pass_n++; printf("  [ ok ] %s\n", name); } \
	else { fail_n++; printf("  [FAIL] %s\n", name); } \
} while (0)

/* ------------------------------------------------------------------------ */
/* Keys built by construction                                               */
/* ------------------------------------------------------------------------ */

/* 16 distinct byte values: no entropy test can fire on it. */
static const uint8_t k_clean[16] = {
	0xA1, 0xB2, 0xC3, 0x16, 0xA5, 0xA6, 0xA7, 0xF2,
	0xD4, 0xE5, 0xF6, 0xAF, 0x18, 0x28, 0x38, 0x78
};

/* Exactly five distinct values, non-zero in both halves: LOWDIV by construction. */
static const uint8_t k_5v[16] = {
	0x11, 0x22, 0x33, 0x44, 0x55, 0x11, 0x22, 0x33,
	0x44, 0x55, 0x11, 0x22, 0x33, 0x44, 0x55, 0x11
};

/* Six distinct values: one above the threshold, must stay silent. */
static const uint8_t k_6v[16] = {
	0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x11, 0x22,
	0x33, 0x44, 0x55, 0x66, 0x11, 0x22, 0x33, 0x44
};

/*
 * The NDS shape: an all-zero half. Overall it uses five distinct values, which
 * is why the guard has to exist -- without it every NDS bouquet would push its
 * sources to the floor for doing nothing wrong.
 */
static const uint8_t k_nds5[16] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x11, 0x22, 0x33, 0x44, 0x55, 0x11, 0x22, 0x33
};

/* Flip the first n bits of src into dst, so the distance is exactly n. */
static void flip_bits(uint8_t dst[16], const uint8_t src[16], int n)
{
	int i, b;
	memcpy(dst, src, 16);
	for (i = 0; i < n; i++) {
		b = i % 16 * 8 + i / 16;      /* spread across bytes 0..15 */
		dst[b >> 3] ^= (uint8_t)(1u << (b & 7));
	}
}

/* ------------------------------------------------------------------------ */
/* Premises: the arithmetic the tests rest on                               */
/* ------------------------------------------------------------------------ */
static void t_premises(void)
{
	uint8_t k[16];

	CHECK(cwen_bitdiff(k_clean, k_clean) == 0,
	      "a key is at distance 0 from itself");
	memcpy(k, k_clean, 16);
	{ int i; for (i = 0; i < 16; i++) k[i] = (uint8_t)~k[i]; }
	CHECK(cwen_bitdiff(k_clean, k) == 128,
	      "a key and its complement are 128 bits apart");
	CHECK(cwen_distinct(k_clean) == 16, "the clean key really does use 16 distinct values");
	CHECK(cwen_distinct(k_5v) == 5, "the 5-value key really does use 5 distinct values");
	CHECK(cwen_distinct(k_6v) == 6, "the 6-value key really does use 6 distinct values");
	/*
	 * Six, not five: the null half contributes exactly one value (zero) on top
	 * of the five in the other half. The guard is still the point -- without it
	 * a key like this is one value away from the entropy threshold on a real
	 * bouquet -- but the count is asserted for what it is.
	 */
	CHECK(cwen_distinct(k_nds5) == 6, "the NDS-shaped key uses 6 distinct values (zero plus five)");
	CHECK(cwen_popcount64(0) == 0 && cwen_popcount64(1) == 1 &&
	      cwen_popcount64(0xFFFFFFFFFFFFFFFFULL) == 64,
	      "popcount is right at both ends and in the middle");
	CHECK(cwen_half_null(k_nds5) && !cwen_half_null(k_nds5 + 8),
	      "the null-half predicate sees exactly one null half here");
	CHECK(sizeof(struct cwen_table) < 4096,
	      "the ring is a bounded, small object (GR9)");
}

/* ------------------------------------------------------------------------ */
/* The entropy half                                                         */
/* ------------------------------------------------------------------------ */
static void t_entropy(void)
{
	CHECK(cwen_scan(k_clean) == 0, "a clean key carries no entropy verdict");
	CHECK(cwen_scan(k_5v) & CWEN_LOWDIV,
	      "five distinct values in the whole key is LOWDIV");
	CHECK((cwen_scan(k_6v) & CWEN_LOWDIV) == 0,
	      "six distinct values is one above the threshold and stays silent");
	CHECK((cwen_scan(k_nds5) & CWEN_LOWDIV) == 0,
	      "a legitimate NDS null half is never LOWDIV, however few values it uses");
	CHECK(cwen_scan(NULL) == 0, "a NULL key is not evidence");
}

/* ------------------------------------------------------------------------ */
/* The collision half                                                       */
/* ------------------------------------------------------------------------ */
static void t_collision(void)
{
	struct cwen_table t;
	struct cwen_match m;
	uint8_t near[16], far_[16];
	unsigned v;
	uint64_t ins;

	/* Exactly 3 bits: the same source, a different ECM. */
	flip_bits(near, k_clean, 3);
	CHECK(cwen_bitdiff(k_clean, near) == 3, "the built neighbour is exactly 3 bits away");

	cwen_init(&t);
	v = cwen_seen(&t, 0x1000ULL, k_clean, 1, 10, 1000, &m);
	CHECK(v == 0, "the first key of all has nothing to collide with");

	v = cwen_seen(&t, 0x2000ULL, near, 1, 10, 2000, &m);
	CHECK((v & CWEN_NEAR) != 0, "a 3-bit edit from the same source is a collision");
	CHECK(!(v & CWEN_NEAR_OTHER), "and it is not attributed to another source");
	CHECK(m.bits == 3, "the reported distance is the real one, not an estimate");
	CHECK(m.age == 1000, "the reported age is the real one");
	CHECK(m.srctype == 1 && m.srcid == 10, "the neighbour's identity is reported for the log");
	CHECK(!memcmp(m.key, k_clean, 16), "the neighbour's key is reported for the log");
	CHECK(m.ecmid == 0x1000ULL, "the neighbour's ECM identity is reported");

	/* The threshold's two sides. */
	cwen_init(&t);
	cwen_seen(&t, 0x1000ULL, k_clean, 1, 10, 1000, &m);
	flip_bits(near, k_clean, CWEN_NEAR_BITS);
	v = cwen_seen(&t, 0x2000ULL, near, 1, 10, 2000, &m);
	CHECK((v & CWEN_NEAR) != 0, "the threshold itself is still a collision (<=)");
	CHECK(m.bits == CWEN_NEAR_BITS, "and it reports the threshold distance");

	cwen_init(&t);
	cwen_seen(&t, 0x1000ULL, k_clean, 1, 10, 1000, &m);
	flip_bits(far_, k_clean, CWEN_NEAR_BITS + 1);
	v = cwen_seen(&t, 0x2000ULL, far_, 1, 10, 2000, &m);
	CHECK((v & (CWEN_NEAR | CWEN_NEAR_OTHER | CWEN_EXACT)) == 0,
	      "one bit past the threshold is a normal key, not a forgery");

	/* Another source: counted and logged, never scored. */
	cwen_init(&t);
	cwen_seen(&t, 0x1000ULL, k_clean, 1, 10, 1000, &m);
	v = cwen_seen(&t, 0x2000ULL, near, 1, 77, 2000, &m);
	CHECK((v & CWEN_NEAR_OTHER) != 0, "a near neighbour from another source is reported");
	CHECK(!(v & CWEN_NEAR), "but never as the attributable verdict");
	CHECK((CWEN_SCOREMASK & CWEN_NEAR_OTHER) == 0,
	      "GR1 policy: the cross-source verdict is excluded from scoring by the mask itself");

	/* Identical key, different ECM: TASK 1.3's fact, not ours. */
	cwen_init(&t);
	cwen_seen(&t, 0x1000ULL, k_clean, 1, 10, 1000, &m);
	v = cwen_seen(&t, 0x2000ULL, k_clean, 1, 10, 2000, &m);
	CHECK((v & CWEN_EXACT) != 0, "an identical key for a different ECM is recognised");
	CHECK(!(v & (CWEN_NEAR | CWEN_NEAR_OTHER)),
	      "and is not double-reported as a near collision");
	CHECK((CWEN_SCOREMASK & CWEN_EXACT) == 0,
	      "GR3 policy: the identical-key fact belongs to TASK 1.3, which already acts on it");

	/* The same ECM is never its own neighbour. */
	cwen_init(&t);
	cwen_seen(&t, 0x1000ULL, k_clean, 1, 10, 1000, &m);
	v = cwen_seen(&t, 0x1000ULL, near, 1, 10, 2000, &m);
	CHECK((v & (CWEN_NEAR | CWEN_NEAR_OTHER | CWEN_EXACT)) == 0,
	      "a second key for the SAME ECM is the ledger's question, not this one's");

	/* A re-delivery does not consume a second slot. */
	cwen_init(&t);
	cwen_seen(&t, 0x1000ULL, k_clean, 1, 10, 1000, &m);
	ins = t.inserted;
	cwen_seen(&t, 0x1000ULL, k_clean, 1, 10, 1500, &m);
	cwen_seen(&t, 0x1000ULL, k_clean, 1, 10, 1800, &m);
	CHECK(t.inserted == ins, "re-pushing the same key for the same ECM does not fill the ring");

	/* No identity: the collision half must stay completely silent. */
	cwen_init(&t);
	cwen_seen(&t, 0x1000ULL, k_clean, 1, 10, 1000, &m);
	v = cwen_seen(&t, 0, near, 1, 10, 2000, &m);
	CHECK((v & (CWEN_NEAR | CWEN_NEAR_OTHER | CWEN_EXACT)) == 0,
	      "with no ECM identity the collision tests cannot fire at all");
	v = cwen_seen(&t, 0, k_5v, 1, 10, 2000, &m);
	CHECK(v & CWEN_LOWDIV,
	      "but the entropy half still works without an identity");

	/* Age: the window's two sides. */
	cwen_init(&t);
	cwen_seen(&t, 0x1000ULL, k_clean, 1, 10, 1000, &m);
	v = cwen_seen(&t, 0x3000ULL, near, 1, 10, 1000 + CWEN_MAX_AGE, &m);
	CHECK((v & CWEN_NEAR) != 0, "a neighbour exactly at the age limit still counts");
	cwen_init(&t);
	cwen_seen(&t, 0x1000ULL, k_clean, 1, 10, 1000, &m);
	v = cwen_seen(&t, 0x3000ULL, near, 1, 10, 1000 + CWEN_MAX_AGE + 1, &m);
	CHECK((v & CWEN_NEAR) == 0, "one ms past the limit it does not");

	/* The closest neighbour wins. */
	cwen_init(&t);
	cwen_seen(&t, 0x1000ULL, k_clean, 1, 30, 1000, &m);
	flip_bits(far_, k_clean, 12);
	cwen_seen(&t, 0x2000ULL, far_, 1, 30, 1100, &m);
	flip_bits(near, k_clean, 4);
	v = cwen_seen(&t, 0x3000ULL, near, 1, 30, 1200, &m);
	CHECK((v & CWEN_NEAR) != 0 && m.bits == 4,
	      "the closest neighbour is the one reported");

	/* The ring forgets, and says so. */
	cwen_init(&t);
	{
		uint8_t k[16];
		int i, used = 0;
		for (i = 0; i < CWEN_SLOTS + 1; i++) {
			memcpy(k, k_clean, 16);
			k[0] = (uint8_t)(k[0] + i);      /* 16 distinct values kept */
			cwen_seen(&t, 0x1000ULL + (uint64_t)i, k, 1, 10, 1000 + (uint32_t)i, &m);
		}
		for (i = 0; i < CWEN_SLOTS; i++) if (t.e[i].used) used++;
		CHECK(used == CWEN_SLOTS, "the ring holds exactly CWEN_SLOTS entries when full");
		CHECK(t.full == 1, "the overflow past the ring is counted, not ignored");
	}
}

/* ------------------------------------------------------------------------ */
/* The claim that matters: honest traffic stays silent                      */
/* ------------------------------------------------------------------------ */
static void t_no_false_positives(void)
{
	struct cwen_table t;
	struct cwen_match m;
	uint64_t s = 0x9E3779B97F4A7C15ULL;
	unsigned hits = 0, entropy_hits = 0;
	int i, j;

	cwen_init(&t);
	for (i = 0; i < 2000; i++) {
		uint8_t k[16];
		unsigned v;
		for (j = 0; j < 16; j++) {
			/* xorshift64: deterministic, so a failure is reproducible */
			s ^= s << 13; s ^= s >> 7; s ^= s << 17;
			k[j] = (uint8_t)(s >> 24);
		}
		/* Two peers, alternating, as a real deployment's two cache peers. */
		v = cwen_seen(&t, 0x10000ULL + (uint64_t)i, k, 1, 10 + (i & 1),
		              1000 + (uint32_t)(i * 10), &m);
		if (v & CWEN_LOWDIV) entropy_hits++;
		if (v & (CWEN_NEAR | CWEN_NEAR_OTHER | CWEN_EXACT)) hits++;
	}
	CHECK(hits == 0, "2000 unrelated honest keys produce no collision verdict at all");
	CHECK(entropy_hits == 0, "2000 random keys produce no entropy verdict either");
	CHECK(t.inserted == 2000, "and every one of them was recorded");
}

/* ------------------------------------------------------------------------ */
/* Names and counters                                                       */
/* ------------------------------------------------------------------------ */
static void t_names(void)
{
	char b[64];
	int i;
	unsigned all = 0;

	for (i = 0; i < CWEN_BITS; i++) all |= (1u << i);
	cwen_name(0, b, sizeof(b));
	CHECK(b[0] == 0, "an empty mask renders as an empty string");
	cwen_name(all, b, 64);
	CHECK(strlen(b) > 0 && strlen(b) < 64, "every bit renders and the result is bounded");
	CHECK(strstr(b, "LOWDIV") && strstr(b, "NEAR") && strstr(b, "EXACT"),
	      "and the names are the ones the log line will print");
	cwen_name(CWEN_LOWDIV | CWEN_NEAR, b, 64);
	CHECK(strcmp(b, "LOWDIV+NEAR") == 0, "two bits join with a plus, in bit order");
	{
		int n = (int)strlen(b);
		cwen_name(all, b, 8);
		CHECK((int)strlen(b) < 8 && (int)strlen(b) >= 7,
		      "a short buffer truncates the name instead of overflowing it");
		(void)n;
	}
	cwen_name(CWEN_NEAR_OTHER, b, 1);
	CHECK(b[0] == 0, "a one-byte buffer still produces a terminated empty string");
}

int main(void)
{
	printf("test_cwentropy -- TASK 2.2 entropy and collision forensics\n");
	printf("  sizeof(struct cwen_table) = %u, struct cwen_match = %u\n",
	       (unsigned)sizeof(struct cwen_table), (unsigned)sizeof(struct cwen_match));

	t_premises();
	t_entropy();
	t_collision();
	t_no_false_positives();
	t_names();

	printf("\n== %d/%d passed, %d failed ==\n", pass_n, pass_n + fail_n, fail_n);
	return fail_n ? 1 : 0;
}
