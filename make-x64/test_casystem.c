/* test_casystem.c -- TASK R26 (D77): the modern-CA registry (casystem.h),
 * the CA-aware CW gate (dcw.c) and the BISS2 session-word table (biss.h).
 *
 * Links the REAL ../src/dcw.c and ../src/biss.c -- never a
 * re-implementation -- exactly like test_dcw links dcw.c.
 * Build is wired in make-x64/Makefile (x64/test_casystem).
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "../src/casystem.h"
#include "../src/dcw.h"
#include "../src/biss.h"

/* standalone link stub: dcw.c logs via debugf (debug.c in the server) */
void debugf(unsigned int flag, char *format, ...);
void debugf(unsigned int flag, char *format, ...) { (void)flag; (void)format; }

static int fail = 0, total = 0;

#define CHECK(cond, name) do { \
	total++; \
	if (cond) printf("  ok  %s\n", name); \
	else { fail++; printf("  FAIL %s\n", name); } \
} while (0)

/* A 16-byte key with NO valid CSA checksum in any group, not null,
 * no three equal leading bytes: the "modern AES key" shape. */
static const uint8_t aes_key[16] = {
	0x10,0x32,0x54,0x76, 0x98,0xBA,0xDC,0xFE,
	0x11,0x33,0x55,0x77, 0x99,0xBB,0xDD,0xF0
};

static void make_valid_csa(uint8_t *cw)
{
	static const uint8_t g[12] = {
		0x11,0x22,0x33, 0x44,0x55,0x66,
		0x77,0x88,0x99, 0xAA,0xBB,0xCC
	};
	int i;
	for (i = 0; i < 4; i++) {
		cw[i*4+0] = g[i*3+0];
		cw[i*4+1] = g[i*3+1];
		cw[i*4+2] = g[i*3+2];
		cw[i*4+3] = (uint8_t)(g[i*3+0] + g[i*3+1] + g[i*3+2]);
	}
}

int main(void)
{
	uint8_t cw[16], out16[16];
	uint32_t provid;
	uint16_t chid;
	/* Seca ECM: tag/len + provid at [3],[4] */
	uint8_t seca[8]  = {0x80,0x70,0x05, 0x12,0x34, 0,0,0};
	/* Viaccess ECM, second r82a position: [6]=0x40 marker, [7]=7,
	 * provid bytes at [8],[9],[10]&0xF0 */
	uint8_t via[16]  = {0x80,0x70,0x0D, 0,0,0, 0x40,7, 0x03,0x29,0x20, 0,0,0,0,0};
	/* CryptoWorks ECM: declared len 0x0D, descriptor 0x83 at [8] */
	uint8_t cw_ecm[16] = {0x80,0x70,0x0D, 0,0,0,0,0, 0x83,0x01,0x42, 0,0,0,0,0};
	uint8_t tiny[3]  = {0x80,0x70,0x01};
	static const uint8_t sw[16] = {
		0x01,0x23,0x45,0x67,0x89,0xAB,0xCD,0xEF,
		0xFE,0xDC,0xBA,0x98,0x76,0x54,0x32,0x10
	};

	printf("[test_casystem] TASK R26 (D77) modern CA registry + gate + BISS2\n");

	/* 1. registry lookup: exact beats family, unknown is safe */
	CHECK(strcmp(ca_name(0x098D), "VideoGuard iCam (Sky DE)") == 0, "exact 098D = VideoGuard iCam (Sky DE)");
	CHECK(strcmp(ca_name(0x1884), "Nagravision") == 0, "family 1884 = Nagravision");
	CHECK(strcmp(ca_name(0x0500), "Viaccess") == 0, "family 0500 = Viaccess");
	CHECK(strcmp(ca_name(0xFFFF), "Unknown") == 0, "FFFF = Unknown (never NULL)");
	CHECK(ca_find(0x1234) != NULL, "unknown CAID still returns a row");

	/* 2. CW format: who carries the DVB-CSA checksum bytes */
	CHECK(ca_cw_checksum_expected(0x1884) == 1, "Nagravision: checksum expected");
	CHECK(ca_cw_checksum_expected(0x0500) == 1, "Viaccess: checksum expected");
	CHECK(ca_cw_checksum_expected(0x090F) == 1, "generic VideoGuard 090F: checksum expected (family)");
	CHECK(ca_cw_checksum_expected(0x098D) == 0, "iCam 098D: NO checksum bytes");
	CHECK(ca_cw_checksum_expected(0x0963) == 0, "iCam 0963: NO checksum bytes");
	CHECK(ca_cw_checksum_expected(0x2610) == 0, "BISS2 2610: NO checksum bytes");
	CHECK(ca_cw_checksum_expected(0x2600) == 0, "BISS1 2600: constant CW, no checksum");
	CHECK(ca_cw_checksum_expected(0xFFFF) == 1, "unknown CAID: checksum expected (stock-safe)");

	/* 3. flags and family helpers */
	CHECK(ca_is_icam(0x098D) == 1 && ca_is_icam(0x09CD) == 1, "iCam CAIDs flagged");
	CHECK(ca_is_icam(0x090F) == 0 && ca_is_icam(0x1884) == 0, "non-iCam CAIDs not flagged");
	CHECK(ca_is_biss(0x2600) && ca_is_biss(0x2610) && !ca_is_biss(0x098D), "BISS family helper");
	CHECK(ca_is_biss2(0x2610) && !ca_is_biss2(0x2600), "BISS2 exact helper");
	CHECK(ca_ecm_hash_offset(0x1702) == 13, "BetaCrypt tunnel: hash offset 13");
	CHECK(ca_ecm_hash_offset(0x098D) == 3 && ca_ecm_hash_offset(0x1884) == 3, "others: hash offset 3");

	/* 4. bounded provid extraction (the r82a rules, guarded) */
	provid = 0xDEADBEEF;
	CHECK(ca_ecm_provid(seca, sizeof(seca), 0x0100, &provid) == 1 && provid == 0x1234, "Seca provid from ECM");
	provid = 0xDEADBEEF;
	CHECK(ca_ecm_provid(via, sizeof(via), 0x0500, &provid) == 1 && provid == 0x032920, "Viaccess provid from ECM (nano position 2)");
	provid = 0xDEADBEEF;
	CHECK(ca_ecm_provid(cw_ecm, sizeof(cw_ecm), 0x0D00, &provid) == 1 && provid == 0x42, "CryptoWorks provid from descriptor 0x83");
	provid = 0xDEADBEEF;
	CHECK(ca_ecm_provid(seca, sizeof(seca), 0x098D, &provid) == 0 && provid == 0xDEADBEEF, "VideoGuard: provid not in ECM, out untouched");
	provid = 0xDEADBEEF;
	CHECK(ca_ecm_provid(tiny, sizeof(tiny), 0x0100, &provid) == 0 && provid == 0xDEADBEEF, "3-byte ECM: no read past end, out untouched");
	CHECK(ca_ecm_provid(NULL, 16, 0x0100, &provid) == 0, "NULL ECM rejected");
	CHECK(ca_ecm_provid(seca, sizeof(seca), 0x0100, NULL) == 0, "NULL out rejected");
	CHECK(ca_ecm_provid(via, 6, 0x0500, &provid) == 0, "truncated Viaccess ECM: no extraction");

	/* 5. bounded chid extraction */
	chid = ca_ecm_chid((uint8_t[]){0x80,0x70,0x10,0,0,0,0xAB,0xCD}, 8, 0x0604);
	CHECK(chid == 0xABCD, "Irdeto chid from ECM");
	CHECK(ca_ecm_chid(tiny, sizeof(tiny), 0x0604) == 0, "truncated Irdeto ECM: chid 0, no over-read");
	CHECK(ca_ecm_chid(seca, sizeof(seca), 0x1884) == 0, "Nagravision: no chid rule");
	CHECK(ca_ecm_chid(NULL, 8, 0x0604) == 0, "NULL ECM: chid 0");

	/* 6. the CA-aware gate: stock parity + the modern rescue */
	memcpy(cw, aes_key, 16);
	CHECK(acceptDCW_for_ca(cw, 0x098D, DCWFILTER_INHERIT, DCWFILTER_INHERIT, 0) == 0,
	      "modern OFF (default): AES key on 098D still checksum-rejected = stock");
	CHECK(acceptDCW_for_ca(cw, 0x098D, DCWFILTER_INHERIT, DCWFILTER_INHERIT, 1) == 1,
	      "CA MODERN ON + iCam CAID: AES key accepted");
	CHECK(acceptDCW_for_ca(cw, 0x1884, DCWFILTER_INHERIT, DCWFILTER_INHERIT, 1) == 0,
	      "CA MODERN ON + Nagravision: checksum still enforced (CSA system)");
	CHECK(acceptDCW_for_ca(cw, 0x2610, DCWFILTER_INHERIT, DCWFILTER_INHERIT, 1) == 1,
	      "CA MODERN ON + BISS2: session word accepted");
	CHECK(acceptDCW_for_ca(cw, 0x098D, DCWFILTER_ON, DCWFILTER_INHERIT, 1) == 0,
	      "explicit DCWFILTER CHECKSUM ON beats CA MODERN (operator wins)");
	CHECK(acceptDCW_for_ca(cw, 0x1884, DCWFILTER_OFF, DCWFILTER_INHERIT, 0) == 1,
	      "explicit DCWFILTER CHECKSUM OFF still works without MODERN");
	make_valid_csa(cw);
	CHECK(acceptDCW_for_ca(cw, 0x098D, DCWFILTER_INHERIT, DCWFILTER_INHERIT, 1) == 1,
	      "valid CSA key still accepted under MODERN");
	memset(cw, 0, 16);
	CHECK(acceptDCW_for_ca(cw, 0x098D, DCWFILTER_INHERIT, DCWFILTER_INHERIT, 1) == 0,
	      "null key is NEVER rescued by CA MODERN");
	memcpy(cw, aes_key, 16);
	dcw_badlist_publish(aes_key, 1);
	CHECK(acceptDCW_for_ca(cw, 0x098D, DCWFILTER_INHERIT, DCWFILTER_INHERIT, 1) == 0,
	      "BAD-DCW-listed key is NEVER rescued by CA MODERN");
	dcw_badlist_publish(NULL, 0);

	/* 7. BISS2 table: full-word semantics, isolation from BISS1 */
	biss2_init();
	CHECK(biss2_add(0x2610, 0x1234, 0, sw) == 1, "BISS2 add");
	CHECK(biss2_get(0x2610, 0x1234, 0, out16) == 1 && memcmp(out16, sw, 16) == 0,
	      "BISS2 get returns the full 16-byte word (no repeated half)");
	CHECK(biss2_count() == 1, "BISS2 count 1");
	CHECK(biss_get(0x2610, 0x1234, 0, out16) == 0, "BISS1 table cannot see a BISS2 entry");
	CHECK(biss2_get(0x2610, 0x9999, 0, out16) == 0, "BISS2 wrong SID misses");
	CHECK(biss2_get(0x2600, 0x1234, 0, out16) == 0, "BISS2 table cannot serve BISS1 CAID entry");
	CHECK(biss2_remove(0x2610, 0x1234, 0) == 1 && biss2_count() == 0, "BISS2 remove");
	CHECK(biss2_add(0x2610, 0x1234, 0, NULL) == 0, "BISS2 NULL key refused");

	printf("[test_casystem] %d checks, %d failures\n", total, fail);
	return fail ? 1 : 0;
}
