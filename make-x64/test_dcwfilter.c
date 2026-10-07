/*
 * test_dcwfilter.c — TASK 1.10b
 *
 * Exercises the REAL ../src/dcw.c `acceptDCW()`, never a re-implementation.
 * The point of the task is that two of its four tests reject VALID control
 * words, and that both are now runtime-gated. A test that copied the filter
 * logic would prove nothing, so dcw.c is linked in and only the two gate
 * variables are driven from here.
 *
 * The gate variables are defined by this file (MCS_DCWFILTER_NOGLOBALS) so
 * that they can be assigned; in the server binary dcw.c defines them.
 *
 *   make -C make-x64 test
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

/*
 * MCS_DCWFILTER_NOGLOBALS makes dcw.c skip its own definitions of the two
 * gates, so this binary supplies them and can assign to them. Passed on the
 * compile line; the `#ifndef` in dcwfilter.h keeps the declarations visible.
 */
#ifndef MCS_DCWFILTER_NOGLOBALS
#error "compile with -DMCS_DCWFILTER_NOGLOBALS; the test must own the gates"
#endif

#include "../src/dcwfilter.h"

/* The gates, defined here for this test binary only. */
int dcw_filter_checksum = 1;
int dcw_filter_repeat   = 1;
/* TASK 3.14: this binary excludes the list snapshot, so the walk is off. */
int dcw_on_badlist(uint8_t *data) { (void)data; return 0; }

/* The real thing. */
int acceptDCW(uint8_t *data);
int checksumDCW(uint8_t *data);
int isbadDCW(uint8_t *data);
int isnullDCW(uint8_t *data);

static int passed = 0, failed = 0;

static void check(const char *what, int got, int want)
{
	if (got == want) { passed++; printf("  [ ok ] %-52s = %d\n", what, got); }
	else { failed++; printf("  [FAIL] %-52s got=%d want=%d\n", what, got, want); }
}

/*
 * A control word that satisfies every r82a test: standard 3-byte-sum checksum
 * on both halves, no all-zero half, no three equal leading bytes.
 *   01 02 03 -> checksum 06      0a 0b 0c -> checksum 21
 */
static uint8_t clean[16] = {
	0x01,0x02,0x03,0x06, 0x04,0x05,0x06,0x0f,
	0x0a,0x0b,0x0c,0x21, 0x0d,0x0e,0x0f,0x2a
};

/*
 * A control word with a repeating half: 0x11 0x11 0x11 trips isbadDCW().
 * Its checksums are still correct, so it is ONLY the repeat test that rejects
 * it — which is exactly the case the gate must be able to rescue.
 *   11 11 11 -> checksum 33      0a 0b 0c -> checksum 21
 */
static uint8_t repeating[16] = {
	0x11,0x11,0x11,0x33, 0x14,0x15,0x16,0x3f,
	0x0a,0x0b,0x0c,0x21, 0x0d,0x0e,0x0f,0x2a
};

/*
 * A control word whose checksum bytes are wrong, but which is otherwise
 * well-formed. This models a bouquet that does not use the standard checksum:
 * the key is genuine, the test is not applicable.
 */
static uint8_t badchecksum[16] = {
	0x01,0x02,0x03,0xFF, 0x04,0x05,0x06,0x0f,
	0x0a,0x0b,0x0c,0x21, 0x0d,0x0e,0x0f,0x2a
};

int main(void)
{
	printf("test_dcwfilter: real acceptDCW() gates (TASK 1.10b)\n");

	/* ---- the fixtures really are what the comments claim ---- */
	check("fixture clean passes r82a's checksum",  checksumDCW(clean), 1);
	check("fixture clean is not a repeat",         isbadDCW(clean), 0);
	check("fixture clean is not null",             isnullDCW(clean), 0);

	check("fixture repeating DOES pass checksum",  checksumDCW(repeating), 1);
	check("fixture repeating IS a repeat",         isbadDCW(repeating), 1);

	check("fixture badchecksum fails checksum",    checksumDCW(badchecksum), 0);
	check("fixture badchecksum is not a repeat",   isbadDCW(badchecksum), 0);

	/* ---- default behaviour: both gates ON, identical to r82a ---- */
	dcw_filter_checksum = 1;
	dcw_filter_repeat = 1;
	check("default: clean accepted",                acceptDCW(clean), 1);
	check("default: repeating rejected (r82a)",     acceptDCW(repeating), 0);
	check("default: badchecksum rejected (r82a)",   acceptDCW(badchecksum), 0);

	/* ---- REPEAT gate OFF rescues the genuine repeating key ---- */
	dcw_filter_repeat = 0;
	check("repeat off: repeating now accepted",     acceptDCW(repeating), 1);
	check("repeat off: clean still accepted",       acceptDCW(clean), 1);
	check("repeat off: badchecksum still rejected", acceptDCW(badchecksum), 0);
	dcw_filter_repeat = 1;

	/* ---- CHECKSUM gate OFF rescues the non-standard bouquet ---- */
	dcw_filter_checksum = 0;
	check("checksum off: badchecksum now accepted", acceptDCW(badchecksum), 1);
	check("checksum off: clean still accepted",     acceptDCW(clean), 1);
	check("checksum off: repeating still rejected", acceptDCW(repeating), 0);
	dcw_filter_checksum = 1;

	/* ---- the NULL test must NOT be gated: an all-zero CW never works ---- */
	{
		uint8_t allnull[16];
		memset(allnull, 0, 16);
		dcw_filter_checksum = 0;
		dcw_filter_repeat = 0;
		check("both off: all-zero CW still rejected", acceptDCW(allnull), 0);
		dcw_filter_checksum = 1;
		dcw_filter_repeat = 1;
	}

	/*
	 * A half-null CW is NOT rejected here, and that is r82a behaviour, not a
	 * regression: `isnullDCW()` requires zeros in BOTH halves
	 * (`(a0||a1) && (b0||b1)`). Half-null keys are handled further upstream in
	 * setdcw.c, where a non-09xx CAID is dropped outright and an 09xx one is
	 * put through `dcwcheck_nds()`.
	 *
	 * The first version of this test asserted that acceptDCW() rejects a
	 * half-null CW. It was wrong, and it failed — which is the assertion being
	 * corrected here, not the code. Recorded so the next reader does not
	 * "fix" it back.
	 */
	{
		uint8_t halfnull[16];
		memcpy(halfnull, clean, 16);
		halfnull[0] = halfnull[1] = halfnull[2] = halfnull[3] = 0;
		check("half-null CW is not an all-null CW", isnullDCW(halfnull), 0);
		dcw_filter_checksum = 0;
		dcw_filter_repeat = 0;
		check("both off: half-null passes (setdcw.c handles it)", acceptDCW(halfnull), 1);
		dcw_filter_checksum = 1;
		dcw_filter_repeat = 1;
	}

	printf("== %d/%d passed, %d failed ==\n", passed, passed + failed, failed);
	return failed ? 1 : 0;
}
