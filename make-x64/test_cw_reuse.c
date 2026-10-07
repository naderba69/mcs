/*
 * test_cw_reuse.c — TASK 1.3
 *
 * Exercises the REAL ../src/cw_reuse.h. The emphasis is on false positives,
 * because this is one of the three proofs GR3 allows to trigger hard action: a
 * wrong proof disables a good card, which GR3 states is worse than the black
 * screen being fixed. So the "must NOT flag" cases get as much coverage as the
 * "must flag" ones.
 *
 *   make -C make-x64 test
 */

#include <stdio.h>
#include "../src/cw_reuse.h"

static int passed = 0, failed = 0;

static void check(const char *what, int got, int want)
{
	if (got == want) { passed++; printf("  [ ok ] %-62s = %d\n", what, got); }
	else { failed++; printf("  [FAIL] %-62s got=%d want=%d\n", what, got, want); }
}

static uint8_t cw_a[16] = {0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,
                           0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x00};
static uint8_t cw_b[16] = {0x0f,0x1e,0x2d,0x3c,0x4b,0x5a,0x69,0x78,
                           0x87,0x96,0xa5,0xb4,0xc3,0xd2,0xe1,0xf0};
static uint8_t md5_1[16] = {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
static uint8_t md5_2[16] = {2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
static uint8_t md5_3[16] = {3,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};

#define CAID 0x1884
#define PROV 0x000000

int main(void)
{
	struct cwreuse_table t;

	printf("test_cw_reuse: real CW-reuse detector (TASK 1.3)\n");
	printf("  table is %d entries, %u bytes of .bss\n",
		CWREUSE_TABLE_SIZE, (unsigned)sizeof(t));

	/* ---- the proof this detector exists to produce ---- */
	cwreuse_reset(&t);
	check("first sighting is never proof",
		cwreuse_offer(&t, cw_a, md5_1, 1, CAID, 100, PROV, 1000), CWREUSE_NONE);
	check("same CW, different ECM, different SID -> PROOF",
		cwreuse_offer(&t, cw_a, md5_2, 1, CAID, 200, PROV, 2500), CWREUSE_PROOF);

	/* ---- and it keeps proving, so one sighting is not a one-shot ---- */
	check("a third service on the same CW -> PROOF again",
		cwreuse_offer(&t, cw_a, md5_3, 1, CAID, 300, PROV, 3000), CWREUSE_PROOF);

	/* ---- the false positives that must never fire ---- */

	/* GR6: Irdeto multi-CHID and multi-PID services share one CW. */
	cwreuse_reset(&t);
	cwreuse_offer(&t, cw_a, md5_1, 1, CAID, 100, PROV, 1000);
	check("same CW, different ECM, SAME SID -> NONE (GR6, multi-CHID)",
		cwreuse_offer(&t, cw_a, md5_2, 1, CAID, 100, PROV, 2000), CWREUSE_NONE);

	/* Same content under a second CAID. */
	cwreuse_reset(&t);
	cwreuse_offer(&t, cw_a, md5_1, 1, 0x0900, 100, PROV, 1000);
	check("same CW, different CAID, same SID -> NONE (benign)",
		cwreuse_offer(&t, cw_a, md5_2, 1, 0x1884, 100, PROV, 2000), CWREUSE_NONE);

	/* Same SID under a second provider. */
	cwreuse_reset(&t);
	cwreuse_offer(&t, cw_a, md5_1, 1, CAID, 100, 0x000001, 1000);
	check("same CW, different PROVID, same SID -> NONE",
		cwreuse_offer(&t, cw_a, md5_2, 1, CAID, 100, 0x000002, 2000), CWREUSE_NONE);

	/* Different CW entirely. */
	cwreuse_reset(&t);
	cwreuse_offer(&t, cw_a, md5_1, 1, CAID, 100, PROV, 1000);
	check("different CW, different SID -> NONE",
		cwreuse_offer(&t, cw_b, md5_2, 1, CAID, 200, PROV, 2000), CWREUSE_NONE);

	/* Identical ECM re-offered, e.g. a retransmission. */
	cwreuse_reset(&t);
	cwreuse_offer(&t, cw_a, md5_1, 1, CAID, 100, PROV, 1000);
	check("identical ECM re-offered -> NONE",
		cwreuse_offer(&t, cw_a, md5_1, 1, CAID, 100, PROV, 1100), CWREUSE_NONE);

	/*
	 * D5: with CACHEEX off, ecmd5 is not populated. The detector must go inert
	 * rather than quietly fall back to the 32-bit hash and start producing
	 * proofs out of bucket collisions.
	 */
	cwreuse_reset(&t);
	cwreuse_offer(&t, cw_a, md5_1, 0, CAID, 100, PROV, 1000);
	check("ecmd5 invalid: first sighting -> NONE",
		cwreuse_offer(&t, cw_a, md5_1, 0, CAID, 100, PROV, 1000), CWREUSE_NONE);
	check("ecmd5 invalid: would-be proof -> NONE (D5, no 32-bit fallback)",
		cwreuse_offer(&t, cw_a, md5_2, 0, CAID, 200, PROV, 2000), CWREUSE_NONE);

	/* ---- ageing: a stale sighting is not evidence about now ---- */
	cwreuse_reset(&t);
	cwreuse_offer(&t, cw_a, md5_1, 1, CAID, 100, PROV, 1000);
	check("within the age window -> PROOF",
		cwreuse_offer(&t, cw_a, md5_2, 1, CAID, 200, PROV, 1000 + CWREUSE_MAX_AGE_MS - 100), CWREUSE_PROOF);

	cwreuse_reset(&t);
	cwreuse_offer(&t, cw_a, md5_1, 1, CAID, 100, PROV, 1000);
	check("past the age window -> NONE (entry aged out)",
		cwreuse_offer(&t, cw_a, md5_2, 1, CAID, 200, PROV, 1000 + CWREUSE_MAX_AGE_MS + 100), CWREUSE_NONE);

	/* ---- 32-bit tick wrap must not silently disable detection ---- */
	cwreuse_reset(&t);
	cwreuse_offer(&t, cw_a, md5_1, 1, CAID, 100, PROV, 0xFFFFF000u);
	check("wrap: sighting before 2^32, proof after -> PROOF",
		cwreuse_offer(&t, cw_a, md5_2, 1, CAID, 200, PROV, 0xFFFFF000u + 1500u), CWREUSE_PROOF);

	cwreuse_reset(&t);
	cwreuse_offer(&t, cw_a, md5_1, 1, CAID, 100, PROV, 0xFFFFF000u);
	check("wrap: aged out across the boundary -> NONE",
		cwreuse_offer(&t, cw_a, md5_2, 1, CAID, 200, PROV, 0xFFFFF000u + CWREUSE_MAX_AGE_MS + 500u), CWREUSE_NONE);

	/*
	 * ---- capacity: a full table still detects, it does not wedge ----
	 *
	 * Fill every slot with distinct control words, then offer a key already
	 * recorded. If eviction were broken the detector would stop answering.
	 */
	{
		uint8_t cw[16], md5[16];
		int i;
		cwreuse_reset(&t);
		for (i = 0; i < CWREUSE_TABLE_SIZE; i++) {
			memcpy(cw, cw_a, 16); cw[15] = (uint8_t)i; cw[0] = (uint8_t)(i * 7 + 1);
			memcpy(md5, md5_1, 16); md5[15] = (uint8_t)i;
			cwreuse_offer(&t, cw, md5, 1, CAID, (uint16_t)(1000 + i), PROV, 5000u + i);
		}
		memcpy(cw, cw_a, 16); cw[15] = 0; cw[0] = 1;   /* the very first key offered */
		memcpy(md5, md5_2, 16);
		check("full table: a key that survived -> still PROOF",
			cwreuse_offer(&t, cw, md5, 1, CAID, 9999, PROV, 6000), CWREUSE_PROOF);
	}

	/* ---- the table is a value type in .bss, so nothing is malloc'd (GR9) ---- */
	check("table depth is the documented 64", CWREUSE_TABLE_SIZE, 64);
	/*
	 * Pinned so a future field addition cannot silently double the footprint.
	 * The fields are 16+16+4+4+2+2+1 = 45 bytes. The real server is built with
	 * -fpack-struct, so it gets exactly 45 and a 2880-byte table; an unpacked
	 * build pads to 48 and 3072. This test is compiled the same way the server
	 * is, so it pins the packed figures -- and the assertion below is written
	 * against sizeof rather than a remembered constant, so a future flag change
	 * fails here instead of silently diverging from the binary.
	 */
	check("entry has no hidden padding beyond the fields (45 packed / 48 unpacked)",
	      (int)sizeof(struct cwreuse_entry), 45);
	check("whole table is 2880 bytes of .bss as the server allocates it",
	      (int)sizeof(struct cwreuse_table), 2880);
	/* the reuse table is a bare array -- unlike purge_table it has no counters */
	check("table is exactly slots * entry, no trailing counter or padding",
	      (int)sizeof(struct cwreuse_table),
	      (int)(CWREUSE_TABLE_SIZE * sizeof(struct cwreuse_entry)));

	printf("== %d/%d passed, %d failed ==\n", passed, passed + failed, failed);
	return failed ? 1 : 0;
}
