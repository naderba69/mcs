/*
 * test_peer_origin.c — TASK 1.1
 *
 * Exercises the REAL ../src/peer_origin.h, never a copy of it. The whole point
 * of the header is that origin encoding has exactly one definition, so a test
 * that re-declared the flags would be testing the test.
 *
 * The values asserted below are the ones r82a has always used, taken from the
 * wire-format-adjacent code in clustredcache.c and setdcw.c. If anyone ever
 * renumbers them, these asserts fail and the cache-peer protocol breaks loudly
 * here instead of quietly in the field.
 *
 *   make -C make-x64 test
 */

#include <stdio.h>
#include "../src/peer_origin.h"

static int passed = 0, failed = 0;

static void check(const char *what, int got, int want)
{
	if (got == want) { passed++; printf("  [ ok ] %-52s = 0x%X\n", what, got); }
	else { failed++; printf("  [FAIL] %-52s got=0x%X want=0x%X\n", what, got, want); }
}

int main(void)
{
	printf("test_peer_origin: real origin model (TASK 1.1)\n");

	/* ---- the flag values are wire-adjacent; pin them ---- */
	check("PEER_CSP",            PEER_CSP,            0x010000);
	check("PEER_CCCAM_CLIENT",   PEER_CCCAM_CLIENT,   0x020000);
	check("PEER_CAMD35_CLIENT",  PEER_CAMD35_CLIENT,  0x040000);
	check("PEER_CS378X_CLIENT",  PEER_CS378X_CLIENT,  0x080000);
	check("PEER_CACHEEX_SERVER", PEER_CACHEEX_SERVER, 0x100000);

	/* ---- flags must not overlap each other, or attribution is ambiguous ---- */
	check("flags are mutually exclusive",
		PEER_CSP | PEER_CCCAM_CLIENT | PEER_CAMD35_CLIENT |
		PEER_CS378X_CLIENT | PEER_CACHEEX_SERVER,
		PEER_ORIGIN_FLAGS);

	/* ---- flags must not overlap the id field, or a large peer id fakes one ---- */
	check("PEER_ORIGIN_FLAGS excludes the id field",
		PEER_ORIGIN_FLAGS & PEER_ID_MASK, 0);

	/*
	 * The largest id that can be represented must survive a round trip. This is
	 * the invariant the old bare `& 0xffff` relied on and never stated.
	 */
	check("PEER_ID_MASK is all-ones in its field", PEER_ID_MASK, 0xFFFF);
	check("max id round-trips through the mask",
		peer_origin_id(0xFFFF | PEER_CSP), 0xFFFF);

	/* ---- id recovery, the thing getpeerbyid() now depends on ---- */
	check("bare id is unchanged",          peer_origin_id(7), 7);
	check("id zero is unchanged",          peer_origin_id(0), 0);
	check("CSP peer 5 -> 5",               peer_origin_id(5 | PEER_CSP), 5);
	check("CCcam client 9 -> 9",           peer_origin_id(9 | PEER_CCCAM_CLIENT), 9);
	check("camd35 client 12 -> 12",        peer_origin_id(12 | PEER_CAMD35_CLIENT), 12);
	check("cs378x client 3 -> 3",          peer_origin_id(3 | PEER_CS378X_CLIENT), 3);
	check("cacheex server 44 -> 44",       peer_origin_id(44 | PEER_CACHEEX_SERVER), 44);

	/* ---- flag identification ---- */
	check("no flag on a bare id",          peer_origin_flag(7), 0);
	check("CSP recognised",                peer_origin_flag(5 | PEER_CSP), PEER_CSP);
	check("cacheex server recognised",     peer_origin_flag(44 | PEER_CACHEEX_SERVER), PEER_CACHEEX_SERVER);

	/*
	 * ---- the D2 switch: client-pushed vs not ----
	 *
	 * Phase 1 counts client-pushed CWs but never penalises them. Misclassifying
	 * one direction condemns a client for something the operator cannot control;
	 * misclassifying the other lets a poisoned client push escape accounting.
	 * Both directions are asserted.
	 */
	check("CSP peer is NOT a client push",
		peer_origin_is_client_push(5 | PEER_CSP), 0);
	check("cacheex server is NOT a client push",
		peer_origin_is_client_push(44 | PEER_CACHEEX_SERVER), 0);
	check("bare id is NOT a client push",
		peer_origin_is_client_push(7), 0);
	check("CCcam client IS a client push",
		peer_origin_is_client_push(9 | PEER_CCCAM_CLIENT), 1);
	check("camd35 client IS a client push",
		peer_origin_is_client_push(12 | PEER_CAMD35_CLIENT), 1);
	check("cs378x client IS a client push",
		peer_origin_is_client_push(3 | PEER_CS378X_CLIENT), 1);

	/*
	 * A peer id is 16 bits, so bit 17 can never be set by an id alone. If that
	 * ever changes, PEER_CSP starts colliding with legitimate ids and this
	 * assertion is what catches it.
	 */
	check("smallest flag is above the id field",
		(PEER_CSP & PEER_ID_MASK) == 0, 1);

	printf("== %d/%d passed, %d failed ==\n", passed, passed + failed, failed);
	return failed ? 1 : 0;
}
