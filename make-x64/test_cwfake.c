/* test_cwfake.c -- TASK R28 (D79): the structural FAKE gate.
 *
 * The enemy: generator-shaped keys that PASS checksumDCW and are
 * DELIVERED by stock -- the receiver shows a black screen and the
 * server reports success. dcwstruct/cwcm have recognised these
 * shapes for years and only ever counted them. dcw_pattern_fake()
 * (dcw.c) is the opt-in refusal verdict; this suite pins:
 *
 *   1. the fake shapes (checksum-repaired complement mirror,
 *      arithmetic staircase, same-byte, two-value and doubled-half
 *      keys) ARE shaped-fake, and the verdict agrees with the
 *      evidence scanners it mirrors (dcwstruct_scan + cwcm_pairs);
 *   2. with the gate disarmed every one of them is still ACCEPTED
 *      by acceptDCW_pattern (stock decision, bit-for-bit);
 *   3. armed globally or per-profile it refuses exactly those;
 *   4. honest keys -- including 2000 pseudo-random checksum-valid
 *      ones -- are never refused: arming the gate cannot blacken a
 *      real bouquet.
 *
 * Links the REAL ../src/dcw.c.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../src/dcw.h"
#include "../src/dcwfilter.h"
#include "../src/dcwstruct.h"
#include "../src/cwcm.h"

/* standalone link stub: dcw.c logs via debugf (debug.c in the server) */
void debugf(unsigned int flag, char *format, ...);
void debugf(unsigned int flag, char *format, ...) { (void)flag; (void)format; }

/* dcwstruct.h externs (the evidence counters live in main.c live) */
unsigned long dcwstruct_hits[DCWS_BITS];
unsigned long dcwstruct_anomalous;

static int total = 0, fail = 0;
#define CHECK(cond, name) do { \
	total++; \
	if (cond) printf("  ok  %s\n", name); \
	else { fail++; printf("  FAIL %s\n", name); } \
} while (0)

/* fixtures: every key below is checksum-VALID (built with python,
 * checksum bytes repaired) so only shape separates fake from honest */
static const uint8_t k_mirror[16] = {0x9F,0x3A,0xC7,0xA0, 0x04,0xB2,0xD5,0x8B, 0x60,0xC5,0x38,0x5D, 0xFB,0x4D,0x2A,0x72};
static const uint8_t k_mono[16]   = {0x80,0xA0,0xC0,0xE0, 0x00,0x20,0x40,0x60, 0x61,0xFA,0x09,0x64, 0x37,0x6B,0xD2,0x74}; /* half 1: constant step 0x20, checksum-repaired */
static const uint8_t k_honest[16] = {0x9F,0x3A,0xC7,0xA0, 0x04,0xB2,0xD5,0x8B, 0x61,0xFA,0x09,0x64, 0x37,0x6B,0xD2,0x74};
/* same-byte half 1; two-value half 2; both checksum-fixed */
static const uint8_t k_same[16]   = {0x80,0x80,0x80,0x80, 0x80,0x80,0x80,0x80, 0x61,0xFA,0x09,0x64, 0x37,0x6B,0xD2,0x74}; /* half 1: one repeated byte (0x80 keeps the group sums honest) */
static const uint8_t k_lowent[16] = {0x9F,0x3A,0xC7,0xA0, 0x04,0xB2,0xD5,0x8B, 0x55,0xAB,0x55,0x55, 0xAB,0x55,0xAB,0xAB}; /* half 2: exactly two distinct values */
static const uint8_t k_halves[16] = {0x9F,0x3A,0xC7,0xA0, 0x04,0xB2,0xD5,0x8B, 0x9F,0x3A,0xC7,0xA0, 0x04,0xB2,0xD5,0x8B};

static void fix_group(uint8_t *k, int g)
{
	k[g*4+3] = (uint8_t)(k[g*4] + k[g*4+1] + k[g*4+2]);
}

static uint32_t lcg_state = 0x12345678u;
static uint8_t rnd8(void)
{
	lcg_state = lcg_state * 1664525u + 1013904223u;
	return (uint8_t)(lcg_state >> 24);
}

int main(void)
{
	int i, fuzz_fakes = 0, fuzz_refused = 0;
	const uint8_t *fakes[5];
	const char *fname[5];

	fakes[0] = k_mirror; fname[0] = "mirror";
	fakes[1] = k_mono;   fname[1] = "mono";
	fakes[2] = k_same;   fname[2] = "same";
	fakes[3] = k_lowent; fname[3] = "lowent";
	fakes[4] = k_halves; fname[4] = "halves";

	printf("[test_cwfake] TASK R28 (D79) structural fake gate\n");

	/* every fixture is checksum-valid: shape alone must convict */
	for (i = 0; i < 5; i++) {
		char nm[64];
		sprintf(nm, "%s fixture passes checksumDCW", fname[i]);
		CHECK(checksumDCW((uint8_t *)fakes[i]) == 1, nm);
		sprintf(nm, "%s fixture is pattern-fake", fname[i]);
		CHECK(dcw_pattern_fake((uint8_t *)fakes[i]) == 1, nm);
		/* agreement with the evidence scanners it mirrors */
		sprintf(nm, "%s: dcwstruct/cwcm agree", fname[i]);
		if (i == 0) CHECK(cwcm_pairs(fakes[i]) >= CWCM_MIN_PAIRS, nm);
		else CHECK((dcwstruct_scan(fakes[i], 0, 0) &
			    (DCWS_SAME|DCWS_MONO|DCWS_LOWENT|DCWS_HALVES|DCWS_INV)) != 0, nm);
	}
	CHECK(dcw_pattern_fake((uint8_t *)k_honest) == 0, "honest key is not pattern-fake");
	CHECK(dcwstruct_scan(k_honest, 0, 0) == 0 && cwcm_pairs(k_honest) < CWCM_MIN_PAIRS,
	      "honest: scanners agree");

	/* 2. gate disarmed (the default): stock accepts every fake */
	dcw_filter_pattern = 0;
	for (i = 0; i < 5; i++) {
		char nm[64];
		if (i == 2) {
			/* the one shape stock already refuses: a repeated-
			 * byte half trips isbadDCW before shape matters */
			CHECK(acceptDCW_pattern((uint8_t *)fakes[i], 0x1884,
						DCWFILTER_INHERIT, DCWFILTER_INHERIT, 0,
						DCWFILTER_INHERIT) == 0,
			      "disarmed: same refused by stock repeat test already");
			continue;
		}
		sprintf(nm, "disarmed: stock still DELIVERS %s (the hole)", fname[i]);
		CHECK(acceptDCW_pattern((uint8_t *)fakes[i], 0x1884,
					DCWFILTER_INHERIT, DCWFILTER_INHERIT, 0,
					DCWFILTER_INHERIT) == 1, nm);
	}
	CHECK(acceptDCW((uint8_t *)k_mirror) == 1,
	      "plain acceptDCW still delivers the mirror fake (pre-R28 API untouched)");

	/* 3a. armed globally */
	dcw_filter_pattern = 1;
	for (i = 0; i < 5; i++) {
		char nm[64];
		sprintf(nm, "global ON refuses %s", fname[i]);
		CHECK(acceptDCW_pattern((uint8_t *)fakes[i], 0x1884,
					DCWFILTER_INHERIT, DCWFILTER_INHERIT, 0,
					DCWFILTER_INHERIT) == 0, nm);
	}
	CHECK(acceptDCW_pattern((uint8_t *)k_honest, 0x1884,
				DCWFILTER_INHERIT, DCWFILTER_INHERIT, 0,
				DCWFILTER_INHERIT) == 1, "global ON still delivers honest");

	/* 3b. armed per-profile only; global off; profile OFF wins back */
	dcw_filter_pattern = 0;
	CHECK(acceptDCW_pattern((uint8_t *)k_mirror, 0x1884,
				DCWFILTER_INHERIT, DCWFILTER_INHERIT, 0,
				DCWFILTER_ON) == 0, "profile PATTERN ON refuses mirror");
	CHECK(acceptDCW_pattern((uint8_t *)k_honest, 0x1884,
				DCWFILTER_INHERIT, DCWFILTER_INHERIT, 0,
				DCWFILTER_ON) == 1, "profile PATTERN ON delivers honest");
	dcw_filter_pattern = 1;
	CHECK(acceptDCW_pattern((uint8_t *)k_mirror, 0x1884,
				DCWFILTER_INHERIT, DCWFILTER_INHERIT, 0,
				DCWFILTER_OFF) == 1, "profile PATTERN OFF overrides global ON");

	/* hard rejects are never rescued by turning the gate off-note:
	 * a null key stays refused however the pattern gate sits */
	dcw_filter_pattern = 0;
	{
		static const uint8_t nullkey[16] = {0};
		CHECK(acceptDCW_pattern((uint8_t *)nullkey, 0x1884,
					DCWFILTER_INHERIT, DCWFILTER_INHERIT, 0,
					DCWFILTER_OFF) == 0, "null key refused regardless");
	}

	/* 4. fuzz: 2000 pseudo-random checksum-valid keys, gate armed */
	for (i = 0; i < 2000; i++) {
		uint8_t k[16];
		int j;
		for (j = 0; j < 16; j++) k[j] = rnd8();
		fix_group(k, 0); fix_group(k, 1); fix_group(k, 2); fix_group(k, 3);
		if (isnullDCW(k) || isbadDCW(k)) { i--; continue; }
		if (dcw_pattern_fake(k)) fuzz_fakes++;
		if (acceptDCW_pattern(k, 0x1884, DCWFILTER_INHERIT,
				      DCWFILTER_INHERIT, 0, DCWFILTER_ON) == 0) fuzz_refused++;
	}
	CHECK(fuzz_fakes <= 1, "fuzz: random keys are not pattern-fake (<=1 in 2000)");
	CHECK(fuzz_refused <= 1, "fuzz: armed gate refuses no honest key (<=1 in 2000)");

	printf("[test_cwfake] %d checks, %d failures\n", total, fail);
	return fail ? 1 : 0;
}
