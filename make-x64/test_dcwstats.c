/*
 * test_dcwstats.c -- Phase 4 verification.
 *
 * Compiles the REAL ../src/dcw.c twice into this one translation unit:
 *
 *   acceptDCW_stats : MCS_DCWSTATS defined -- the instrumented build
 *   acceptDCW_base  : MCS_DCWSTATS NOT defined -- unmodified r82a
 *
 * and proves two things:
 *   1. EQUIVALENCE -- for every vector the accept/reject decision of the
 *      instrumented build is identical to the unmodified r82a decision.
 *      Instrumentation must not change key handling, ever.
 *   2. ATTRIBUTION -- when enabled, each rejection is counted under the
 *      correct reason, which is the whole point of the patch.
 *
 * MCS_DCWSTATS_NOGLOBALS tells dcw.c to skip its own weak definitions of the
 * counters: `weak` resolves duplicates across object files, not within one
 * translation unit, and this file pulls dcw.c in twice.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* Storage for the counters, provided once for this translation unit. */
int           dcwstats_on = 0;
unsigned long dcwstats[10]; /* TASK R1: = DCW_REJ_COUNT; the macro is not visible this early. R8: consensus-mismatch joined, count is 10 */

/*
 * The TASK 1.10b filter gates are globals in dcw.c too, and this file pulls
 * dcw.c in twice, so they need the same treatment: define them once here and
 * tell dcw.c to skip its own copy. Both stay at their r82a defaults (1) because
 * this test's job is to prove the stats instrumentation is decision-equivalent
 * to unmodified r82a, which means the gates must not be off.
 */
int dcw_filter_checksum = 1;
int dcw_filter_repeat   = 1;
/* TASK 3.14: both included copies call this. The list is off here, so
 * the equivalence check still compares instrumentation against r82a. */
int dcw_on_badlist(uint8_t *data) { (void)data; return 0; }
#define MCS_DCWFILTER_NOGLOBALS

/* --- instrumented build --- */
#define MCS_DCWSTATS
#define MCS_DCWSTATS_NOGLOBALS
#define acceptDCW      acceptDCW_stats
#define acceptDCW_for  acceptDCW_for_stats
#define checksumDCW    checksumDCW_stats
#define isnullDCW      isnullDCW_stats
#define isbadDCW       isbadDCW_stats
#define similarcw      similarcw_stats
#define ishalfnulledcw ishalfnulledcw_stats
#include "../src/dcw.c"
#undef MCS_DCWSTATS
#undef acceptDCW
#undef acceptDCW_for
#undef checksumDCW
#undef isnullDCW
#undef isbadDCW
#undef similarcw
#undef ishalfnulledcw

/* --- unmodified r82a build --- */
#define acceptDCW      acceptDCW_base
#define acceptDCW_for  acceptDCW_for_base
#define checksumDCW    checksumDCW_base
#define isnullDCW      isnullDCW_base
#define isbadDCW       isbadDCW_base
#define similarcw      similarcw_base
#define ishalfnulledcw ishalfnulledcw_base
#include "../src/dcw.c"
#undef acceptDCW
#undef acceptDCW_for
#undef checksumDCW
#undef isnullDCW
#undef isbadDCW
#undef similarcw
#undef ishalfnulledcw

static int failures = 0, total = 0;

static void check(const char *name, long got, long want)
{
	total++;
	if (got != want) { failures++; printf("  [FAIL] %-44s got=%ld want=%ld\n", name, got, want); }
	else             { printf("  [ ok ] %-44s = %ld\n", name, got); }
}

/* build a CW whose four 4-byte groups satisfy the DVB-CSA checksum */
static void make_valid(uint8_t *cw, const uint8_t g[12])
{
	int i;
	for (i = 0; i < 4; i++) {
		cw[i*4+0] = g[i*3+0];
		cw[i*4+1] = g[i*3+1];
		cw[i*4+2] = g[i*3+2];
		cw[i*4+3] = (uint8_t)((g[i*3+0] + g[i*3+1] + g[i*3+2]) & 0xFF);
	}
}

typedef struct { const char *name; uint8_t cw[16]; } vec_t;

int main(void)
{
	vec_t v[6];
	int i;

	/* vector 0: well formed, must be accepted */
	make_valid(v[0].cw, (const uint8_t[]){0x11,0x22,0x33, 0x44,0x55,0x66,
	                                        0x77,0x88,0x99, 0xAA,0xBB,0xCC});
	v[0].name = "valid CSA-checksum CW";

	/* vector 1: all zero -> null */
	memset(v[1].cw, 0, 16);
	v[1].name = "all-zero CW";

	/* vector 2: checksum corrupted */
	make_valid(v[2].cw, (const uint8_t[]){0x11,0x22,0x33, 0x44,0x55,0x66,
	                                        0x77,0x88,0x99, 0xAA,0xBB,0xCC});
	v[2].cw[7] ^= 0x5A;
	v[2].name = "corrupted-checksum CW";

	/* vector 3: the XOR 0xF0 fake-CW attack from the r107 changelog */
	make_valid(v[3].cw, (const uint8_t[]){0x11,0x22,0x33, 0x44,0x55,0x66,
	                                        0x77,0x88,0x99, 0xAA,0xBB,0xCC});
	v[3].cw[15] ^= 0xF0;
	v[3].name = "XOR-0xF0 fake CW";

	/* vector 4: three equal non-zero bytes, checksum still valid */
	make_valid(v[4].cw, (const uint8_t[]){0x11,0x11,0x11, 0x44,0x55,0x66,
	                                        0x77,0x88,0x99, 0xAA,0xBB,0xCC});
	v[4].name = "three-equal-bytes CW";

	/* vector 5: NDS/Videoguard half-nulled, checksum valid -> accepted */
	memset(v[5].cw, 0, 16);
	v[5].cw[0]=0x11; v[5].cw[1]=0x22; v[5].cw[2]=0x33; v[5].cw[3]=0x66;
	v[5].cw[4]=0x44; v[5].cw[5]=0x55; v[5].cw[6]=0x66; v[5].cw[7]=0xFF;
	v[5].name = "NDS half-nulled CW";

	printf("== Phase 4: dcw.c instrumentation ==\n\n-- 1. equivalence with unmodified r82a --\n");
	dcwstats_on = 1;
	for (i = 0; i < 6; i++) {
		int p = acceptDCW_stats(v[i].cw);
		int b = acceptDCW_base(v[i].cw);
		check(v[i].name, p, b);
		if (p != b) printf("         instrumented=%d baseline=%d\n", p, b);
	}

	printf("\n-- 2. decision matches the documented expectation --\n");
	check("valid CW accepted",              acceptDCW_stats(v[0].cw), 1);
	check("all-zero rejected",              acceptDCW_stats(v[1].cw), 0);
	check("corrupted checksum rejected",    acceptDCW_stats(v[2].cw), 0);
	check("XOR-0xF0 fake rejected",         acceptDCW_stats(v[3].cw), 0);
	check("three-equal-bytes rejected",     acceptDCW_stats(v[4].cw), 0);
	check("NDS half-nulled accepted",       acceptDCW_stats(v[5].cw), 1);

	printf("\n-- 3. counters are OFF by default (stock behaviour) --\n");
	dcwstats_on = 0;
	dcwstats_reset();
	for (i = 0; i < 6; i++) acceptDCW_stats(v[i].cw);
	{
		unsigned long sum = 0;
		for (i = 0; i < DCW_REJ_COUNT; i++) sum += dcwstats[i];
		check("no counter touched while disabled", (long)sum, 0);
	}

	printf("\n-- 4. attribution when enabled --\n");
	dcwstats_on = 1;
	dcwstats_reset();
	for (i = 0; i < 6; i++) acceptDCW_stats(v[i].cw);
	check("checksum rejections",   (long)dcwstats[DCW_REJ_CHECKSUM], 2); /* v2 + v3 */
	check("null rejections",       (long)dcwstats[DCW_REJ_NULL],     1); /* v1      */
	check("repeat rejections",     (long)dcwstats[DCW_REJ_REPEAT],   1); /* v4      */
	check("accepted",              (long)dcwstats[DCW_ACC_COUNT],    2); /* v0 + v5 */

	printf("\n-- 5. reason names used by telnet/web output --\n");
	{
		int ok = 1;
		ok &= (strcmp(dcwstats_reason_name(DCW_REJ_CHECKSUM), "checksum") == 0);
		ok &= (strcmp(dcwstats_reason_name(DCW_REJ_NULL),     "null/half-null") == 0);
		ok &= (strcmp(dcwstats_reason_name(DCW_REJ_REPEAT),   "repeat-3-bytes") == 0);
		ok &= (strcmp(dcwstats_reason_name(DCW_REJ_BADLIST),  "bad-dcw-list") == 0);
		ok &= (strcmp(dcwstats_reason_name(DCW_REJ_CYCLE),    "cycle-contradiction") == 0); /* R2 */
		ok &= (strcmp(dcwstats_reason_name(99),               "unknown") == 0);
		check("all reason names resolve", ok, 1);
	}

	printf("\n-- 6. reset --\n");
	dcwstats_reset();
	{
		unsigned long sum = 0;
		for (i = 0; i < DCW_REJ_COUNT; i++) sum += dcwstats[i];
		check("counters cleared", (long)sum, 0);
	}

	printf("\n== %d/%d passed, %d failed ==\n", total - failures, total, failures);
	return failures ? 1 : 0;
}
