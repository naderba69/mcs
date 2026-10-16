/* test_cafamily.c -- TASK R16 (D71): the CAID family table (cafamily.h).
 *
 * Covers exactly what COMPAT-ar.md claims: the stock defaults
 * (HALFNULLED={09}, NOPROVCHK={18,09,0B}, CARDPROV={01,05} [D98]),
 * the ownership rule (the first
 * configured line for a flag drops its defaults and owns the set), the
 * empty-list case (own + nothing = none), accumulation, the boundary
 * families, and the site-facing predicates ca_halfnull_ok/ca_provchk_skip.
 */
#include <stdio.h>
#include <string.h>
#include "../src/cafamily.h"

/* the header externs the global; the test owns its instance */
struct cafamily_tab cafamily_tab;

static int fail = 0, total = 0;

#define CHECK(cond, name) do { \
	total++; \
	if (cond) printf("  ok  %s\n", name); \
	else { fail++; printf("  FAIL %s\n", name); } \
} while (0)

int main(void)
{
	struct cafamily_tab t;
	int i, all;

	printf("[test_cafamily] TASK R16 (D71) the CAID family table\n");

	/* 1. stock defaults (D71): reproduce r82a's hard-coded sets */
	cafamily_init(&t);
	CHECK(cafamily_has(&t, CAFAM_HALFNULLED, 0x09), "default HALFNULLED has 09");
	CHECK(!cafamily_has(&t, CAFAM_HALFNULLED, 0x08), "default HALFNULLED lacks 08");
	CHECK(!cafamily_has(&t, CAFAM_HALFNULLED, 0x0E), "default HALFNULLED lacks 0E");
	CHECK(!cafamily_has(&t, CAFAM_HALFNULLED, 0x18), "default HALFNULLED lacks 18");
	CHECK(cafamily_has(&t, CAFAM_NOPROVCHK, 0x18), "default NOPROVCHK has 18");
	CHECK(cafamily_has(&t, CAFAM_NOPROVCHK, 0x09), "default NOPROVCHK has 09");
	CHECK(cafamily_has(&t, CAFAM_NOPROVCHK, 0x0B), "default NOPROVCHK has 0B");
	CHECK(!cafamily_has(&t, CAFAM_NOPROVCHK, 0x01), "default NOPROVCHK lacks 01 (SECA strict)");
	CHECK(!cafamily_has(&t, CAFAM_NOPROVCHK, 0x05), "default NOPROVCHK lacks 05 (Via strict)");
	CHECK(!cafamily_has(&t, CAFAM_NOPROVCHK, 0x06), "default NOPROVCHK lacks 06 (the D71 delta)");
	CHECK(!cafamily_has(&t, CAFAM_NOPROVCHK, 0x0A), "default NOPROVCHK lacks 0A");

	/* 2. site predicates on the global (the exact call shapes the filters use) */
	cafamily_init(&cafamily_tab);
	CHECK(ca_halfnull_ok(0x0963), "ca_halfnull_ok(09xx) stock: NDS keys may be half-nulled");
	CHECK(!ca_halfnull_ok(0x1884), "ca_halfnull_ok(18xx) stock: refused");
	CHECK(!ca_halfnull_ok(0x0604), "ca_halfnull_ok(06xx) stock: refused");
	CHECK(ca_provchk_skip(0x1884), "ca_provchk_skip(18xx) stock: skip");
	CHECK(ca_provchk_skip(0x0963), "ca_provchk_skip(09xx) stock: skip");
	CHECK(ca_provchk_skip(0x0B00), "ca_provchk_skip(0Bxx) stock: skip");
	CHECK(!ca_provchk_skip(0x0100), "ca_provchk_skip(01xx) stock: strict (SECA)");
	CHECK(!ca_provchk_skip(0x0500), "ca_provchk_skip(05xx) stock: strict (Viaccess)");
	CHECK(!ca_provchk_skip(0x0604), "ca_provchk_skip(06xx) stock: strict (the D71 delta)");

	/* 3. ownership: the first configured family drops the flag's defaults */
	cafamily_init(&t);
	cafamily_add(&t, CAFAM_HALFNULLED, 0x18);
	CHECK(cafamily_has(&t, CAFAM_HALFNULLED, 0x18), "add 18 -> present");
	CHECK(!cafamily_has(&t, CAFAM_HALFNULLED, 0x09), "add 18 OWNS the set: stock 09 dropped");
	cafamily_add(&t, CAFAM_HALFNULLED, 0x0E);
	CHECK(cafamily_has(&t, CAFAM_HALFNULLED, 0x0E), "second add accumulates");
	CHECK(cafamily_has(&t, CAFAM_HALFNULLED, 0x18), "second add keeps the first");
	CHECK(!cafamily_has(&t, CAFAM_HALFNULLED, 0x09), "still no default 09");

	/* 4. ownership is per-flag: HALFNULLED's add does not touch NOPROVCHK */
	CHECK(cafamily_has(&t, CAFAM_NOPROVCHK, 0x18), "NOPROVCHK defaults untouched by HALFNULLED add");
	CHECK(cafamily_has(&t, CAFAM_NOPROVCHK, 0x0B), "NOPROVCHK defaults untouched (0B)");

	/* 5. empty list: own + no families = none allowed */
	cafamily_init(&t);
	cafamily_own(&t, CAFAM_HALFNULLED);
	CHECK(!cafamily_has(&t, CAFAM_HALFNULLED, 0x09), "own with empty list: 09 gone");
	CHECK(!cafamily_has(&t, CAFAM_HALFNULLED, 0x18), "own with empty list: nothing allowed");

	/* 6. init resets everything */
	cafamily_add(&t, CAFAM_NOPROVCHK, 0x06);
	cafamily_init(&t);
	CHECK(!cafamily_has(&t, CAFAM_NOPROVCHK, 0x06), "init resets ownership");
	CHECK(cafamily_has(&t, CAFAM_NOPROVCHK, 0x18), "init restores defaults (18)");

	/* 7. boundaries: families 00 and FF, and the mask */
	cafamily_init(&t);
	cafamily_own(&t, CAFAM_NOPROVCHK);
	cafamily_add(&t, CAFAM_NOPROVCHK, 0x00);
	cafamily_add(&t, CAFAM_NOPROVCHK, 0xFF);
	CHECK(cafamily_has(&t, CAFAM_NOPROVCHK, 0x00), "family 00 bit");
	CHECK(cafamily_has(&t, CAFAM_NOPROVCHK, 0xFF), "family FF bit");
	cafamily_add(&t, CAFAM_NOPROVCHK, 0x163); /* masked to 0x63 */
	CHECK(cafamily_has(&t, CAFAM_NOPROVCHK, 0x63), "family masked to 8 bits (0x163 -> 0x63)");

	/* 8. every family: add all 256, all present; other flag untouched */
	cafamily_init(&t);
	cafamily_own(&t, CAFAM_HALFNULLED);
	for (i = 0; i < 256; i++) cafamily_add(&t, CAFAM_HALFNULLED, (unsigned)i);
	all = 1;
	for (i = 0; i < 256; i++) if (!cafamily_has(&t, CAFAM_HALFNULLED, (unsigned)i)) all = 0;
	CHECK(all, "all 256 families settable");
	all = 1;
	for (i = 0; i < 256; i++) {
		int want = (i == 0x18 || i == 0x09 || i == 0x0B);
		if (cafamily_has(&t, CAFAM_NOPROVCHK, (unsigned)i) != want) all = 0;
	}
	CHECK(all, "NOPROVCHK untouched across the walk");

	/* 9. CARDPROV (D98): stock default {01,05}, predicate strict there only */
	cafamily_init(&t);
	CHECK(cafamily_has(&t, CAFAM_CARDPROV, 0x01), "default CARDPROV has 01 (SECA strict)");
	CHECK(cafamily_has(&t, CAFAM_CARDPROV, 0x05), "default CARDPROV has 05 (Viaccess strict)");
	CHECK(!cafamily_has(&t, CAFAM_CARDPROV, 0x06), "default CARDPROV lacks 06");
	CHECK(!cafamily_has(&t, CAFAM_CARDPROV, 0x09), "default CARDPROV lacks 09");
	CHECK(!cafamily_has(&t, CAFAM_CARDPROV, 0x18), "default CARDPROV lacks 18");
	/* ownership: first line owns the set */
	cafamily_add(&t, CAFAM_CARDPROV, 0x06);
	CHECK(!cafamily_has(&t, CAFAM_CARDPROV, 0x01), "PROVIDERCHECK owns: stock 01 dropped");
	CHECK(cafamily_has(&t, CAFAM_CARDPROV, 0x06), "PROVIDERCHECK owns: 06 present");
	/* owning CARDPROV leaves the other two flags' stock defaults intact */
	CHECK(cafamily_has(&t, CAFAM_HALFNULLED, 0x09), "CARDPROV add: HALFNULLED untouched");
	CHECK(cafamily_has(&t, CAFAM_NOPROVCHK, 0x18), "CARDPROV add: NOPROVCHK untouched");
	/* predicate: family = caid>>8 exactly */
	cafamily_init(&t);
	/* (global table here is the local t only via cafamily_has; predicate
	 * uses the global -- exercise has() bit-exactness instead) */
	CHECK(!cafamily_has(&t, CAFAM_CARDPROV, 0x10), "default CARDPROV lacks 10 (family nibble exact)");

	/* 10. invalid flag: no crash, has returns 0 */
	cafamily_add(&t, 3, 0x09);
	cafamily_add(&t, -1, 0x09);
	CHECK(!cafamily_has(&t, 3, 0x09) && !cafamily_has(&t, -1, 0x09), "invalid flag ignored");

	printf("[test_cafamily] done: %d/%d ok\n", total - fail, total);
	return fail ? 1 : 0;
}
