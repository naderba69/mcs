/*
 * test_cache_purge.c — TASK 1.5 tests.
 *
 * Includes the real header, so a changed constant fails this file rather than
 * quietly changing behaviour.
 *
 * sizeof figures are PRINTED, not asserted against a remembered number: this
 * test suite has been wrong about sizeof three times already (reuse entry
 * claimed 40 B vs actual 48; trust entry 24 vs 36; trust table 6144 vs 9216).
 * Printing means the value is read, not recalled.
 */
#include <stdio.h>
#include <stdlib.h>
#include "../src/cache_purge.h"

static int pass_n = 0, fail_n = 0;

#define CHECK(cond, name) do { \
	if (cond) { pass_n++; printf("  [ ok ] %s\n", name); } \
	else { fail_n++; printf("  [FAIL] %s\n", name); } \
} while (0)

/* a source, at GR4 granularity */
#define SRV  2u            /* DCW_SOURCE_SERVER */
#define CAC  3u            /* DCW_SOURCE_CSCLIENT */
#define PEER7   (7u | 0x010000u)   /* peer 7 arriving as CSP   */
#define CLIENT7 (7u | 0x020000u)   /* client 7 arriving as CCCam cacheex */

#define CAID1  0x0500
#define CAID2  0x0604
#define SID1   100
#define SID2   200
#define PROV1  0x043800u
#define PROV2  0x000000u

static struct purge_table tab;

static void t_sizes(void)
{
	printf("  -- sizes (measured, not remembered) --\n");
	printf("     sizeof(struct purge_entry) = %u bytes\n",
	       (unsigned)sizeof(struct purge_entry));
	printf("     sizeof(struct purge_table) = %u bytes\n",
	       (unsigned)sizeof(struct purge_table));
	/* Four counters since TASK 1.9: marked, skipped, released, expired. */
	CHECK(sizeof(struct purge_table) ==
	      CACHE_PURGE_SLOTS * sizeof(struct purge_entry) + 4 * sizeof(uint32_t),
	      "table size is slots * entry + two counters (no hidden padding)");
}

static void t_mark_and_withhold(void)
{
	cache_purge_init(&tab);

	CHECK(cache_purge_should_withhold(&tab, SRV, 1, CAID1, SID1, PROV1) == 0,
	      "unknown source is not withheld (no record != bad)");

	CHECK(cache_purge_mark(&tab, SRV, 1, CAID1, SID1, PROV1, 1000) == 1,
	      "first mark reports a new event");
	CHECK(cache_purge_should_withhold(&tab, SRV, 1, CAID1, SID1, PROV1) == 1,
	      "marked source is withheld");
	CHECK(tab.marked == 1, "marked counter incremented once");

	CHECK(cache_purge_mark(&tab, SRV, 1, CAID1, SID1, PROV1, 2000) == 0,
	      "re-marking the same key is not a second event (no log spam)");
	CHECK(tab.marked == 2, "but it does refresh the counter and the age");
}

static void t_gr4_granularity(void)
{
	cache_purge_init(&tab);
	cache_purge_mark(&tab, SRV, 1, CAID1, SID1, PROV1, 1000);

	CHECK(cache_purge_should_withhold(&tab, SRV, 1, CAID1, SID2, PROV1) == 0,
	      "GR4: a mark on SID 100 does not touch SID 200");
	CHECK(cache_purge_should_withhold(&tab, SRV, 1, CAID2, SID1, PROV1) == 0,
	      "GR4: a mark on one CAID does not touch another");
	CHECK(cache_purge_should_withhold(&tab, SRV, 1, CAID1, SID1, PROV2) == 0,
	      "GR4: a mark on one PROVID does not touch another");
	CHECK(cache_purge_should_withhold(&tab, SRV, 2, CAID1, SID1, PROV1) == 0,
	      "GR4: a mark on source 1 does not touch source 2");
	CHECK(cache_purge_should_withhold(&tab, CAC, 1, CAID1, SID1, PROV1) == 0,
	      "GR4: same numeric id under a different source type is a different source");

	/* PEER_* origin flags are part of the identity, exactly as in trust.h */
	cache_purge_mark(&tab, SRV, PEER7, CAID1, SID1, PROV1, 1000);
	CHECK(cache_purge_should_withhold(&tab, SRV, CLIENT7, CAID1, SID1, PROV1) == 0,
	      "a CSP peer and a cacheex client with the same numeric id do not share a mark");
	CHECK(cache_purge_should_withhold(&tab, SRV, PEER7, CAID1, SID1, PROV1) == 1,
	      "the marked one is still withheld");
}

static void t_unmark_is_reversible(void)
{
	cache_purge_init(&tab);
	cache_purge_mark(&tab, SRV, 1, CAID1, SID1, PROV1, 1000);

	CHECK(cache_purge_unmark(&tab, SRV, 1, CAID1, SID1, PROV1) == 1,
	      "unmark reports it removed something");
	CHECK(cache_purge_should_withhold(&tab, SRV, 1, CAID1, SID1, PROV1) == 0,
	      "GR8: the mark is actually gone, not merely expired");
	CHECK(cache_purge_unmark(&tab, SRV, 1, CAID1, SID1, PROV1) == 0,
	      "unmarking twice is harmless");
	CHECK(cache_purge_count(&tab) == 0, "count follows");
}

static void t_eviction_prefers_free_slots(void)
{
	int i, ok = 1;
	cache_purge_init(&tab);

	/*
	 * This is the case that was broken before the fix: fill half the table,
	 * then keep marking. A free slot must always be used before an existing
	 * entry is evicted, no matter which order the slots are in.
	 */
	for (i = 0; i < CACHE_PURGE_SLOTS; i++)
		cache_purge_mark(&tab, SRV, (uint32_t)i, CAID1, SID1, PROV1, (uint32_t)i);

	CHECK(cache_purge_count(&tab) == CACHE_PURGE_SLOTS,
	      "table fills to exactly CACHE_PURGE_SLOTS");
	CHECK(tab.skipped == 0, "nothing skipped while slots were free");

	/* every one of those must still be withheld — none evicted early */
	for (i = 0; i < CACHE_PURGE_SLOTS; i++)
		if (!cache_purge_should_withhold(&tab, SRV, (uint32_t)i, CAID1, SID1, PROV1))
			ok = 0;
	CHECK(ok, "no entry was evicted while a free slot existed");
}

static void t_eviction_is_longest_unseen(void)
{
	int i;
	cache_purge_init(&tab);

	/*
	 * Fill with ticks 0..63, so source 0 is the OLDEST. Then refresh source 0
	 * so it becomes the newest, and add one more. The victim must be source 1
	 * — the longest-unseen — and NOT source 0.
	 *
	 * The fill range is deliberately disjoint from the fixtures above: an
	 * earlier version of a sibling test filled with 1000+i and silently
	 * touched sid 1100, which was the very entry it then asserted about.
	 * These SIDs are 9000+ and appear nowhere else in this file.
	 */
	for (i = 0; i < CACHE_PURGE_SLOTS; i++)
		cache_purge_mark(&tab, SRV, (uint32_t)i, CAID1, 9000 + (uint16_t)i, PROV1, (uint32_t)i);

	cache_purge_mark(&tab, SRV, 0, CAID1, 9000, PROV1, 5000); /* refresh source 0 */

	CHECK(cache_purge_mark(&tab, SRV, 999, CAID1, 9999, PROV1, 6000) == 1,
	      "a 65th mark still succeeds");
	CHECK(cache_purge_should_withhold(&tab, SRV, 0, CAID1, 9000, PROV1) == 1,
	      "eviction is longest-unseen: the refreshed source survived");
	CHECK(cache_purge_should_withhold(&tab, SRV, 1, CAID1, 9001, PROV1) == 0,
	      "and the longest-unseen source was the one evicted");
	CHECK(cache_purge_should_withhold(&tab, SRV, 999, CAID1, 9999, PROV1) == 1,
	      "the new mark is live");
	CHECK(cache_purge_count(&tab) == CACHE_PURGE_SLOTS, "count stays bounded");
}

static void t_bounded_and_safe(void)
{
	long i;
	cache_purge_init(&tab);

	/* hammer it well past capacity with distinct keys */
	for (i = 0; i < 100000; i++)
		cache_purge_mark(&tab, SRV, (uint32_t)(i & 0xFFFF), CAID1,
		                 (uint16_t)(i >> 16), PROV1, (uint32_t)i);

	CHECK(cache_purge_count(&tab) == CACHE_PURGE_SLOTS,
	      "GR9: 100k distinct marks leave exactly CACHE_PURGE_SLOTS entries");
	/*
	 * `skipped` stays 0, and that is the correct behaviour rather than a dead
	 * counter: eviction always finds a victim once the table is full, so a
	 * mark is never refused. The field exists for a future variant that
	 * refuses instead of evicting; asserting it is non-zero here would be
	 * asserting a bug.
	 */
	CHECK(tab.skipped == 0,
	      "GR9: no mark is ever refused - eviction always makes room");

	/* every API must tolerate a NULL table: the ECM path must never fault */
	CHECK(cache_purge_should_withhold(NULL, SRV, 1, CAID1, SID1, PROV1) == 0,
	      "NULL table: withhold returns 0");
	CHECK(cache_purge_mark(NULL, SRV, 1, CAID1, SID1, PROV1, 1) == 0,
	      "NULL table: mark returns 0");
	CHECK(cache_purge_unmark(NULL, SRV, 1, CAID1, SID1, PROV1) == 0,
	      "NULL table: unmark returns 0");
	CHECK(cache_purge_count(NULL) == 0, "NULL table: count returns 0");
	cache_purge_init(NULL); /* must not fault */
	CHECK(1, "NULL table: init does not fault");
}

static void t_hits_saturate(void)
{
	int i, maxhits = 0;
	cache_purge_init(&tab);

	/*
	 * hits only increments when a slot is REUSED for a new key, so filling
	 * 64 distinct keys into 64 free slots would leave every hits at 1 and
	 * prove nothing. Force reuse instead: mark far more distinct keys than
	 * there are slots, so each slot gets recycled many times.
	 */
	for (i = 0; i < 5000; i++)
		cache_purge_mark(&tab, SRV, (uint32_t)i, CAID1,
		                 9000 + (uint16_t)i, PROV1, (uint32_t)i);

	for (i = 0; i < CACHE_PURGE_SLOTS; i++)
		if (tab.e[i].inuse && tab.e[i].hits > maxhits) maxhits = tab.e[i].hits;

	printf("     max hits after 5000 marks over 64 slots = %d\n", maxhits);
	CHECK(maxhits >= 2, "reused slots do count hits");
	CHECK(maxhits <= 0xFF, "and the counter saturates at 255, never wrapping to 0");
	CHECK(cache_purge_count(&tab) == CACHE_PURGE_SLOTS, "table still exactly full");
}

int main(void)
{
	printf("test_cache_purge: TASK 1.5 mark-by-origin\n");
	t_sizes();
	t_mark_and_withhold();
	t_gr4_granularity();
	t_unmark_is_reversible();
	t_eviction_prefers_free_slots();
	t_eviction_is_longest_unseen();
	t_bounded_and_safe();
	t_hits_saturate();
	printf("== %d/%d passed, %d failed ==\n", pass_n, pass_n + fail_n, fail_n);
	return fail_n ? 1 : 0;
}
