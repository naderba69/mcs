/*
 * test_cache_relay.c -- GR10 guard verification (TASK 1.0b).
 *
 * Exercises the REAL predicate from ../src/cache_relay.h, the same one the
 * cache calls at clustredcache.c:1626. No re-implementation.
 *
 * The contract being pinned:
 *   - default config (CACHE FORWARD off)  -> never relay   (GR10)
 *   - the pre-existing r82 condition (!src_fwd) is preserved
 *   - CACHE FORWARD: ON restores the historical behaviour (rollback path)
 */
#include <stdio.h>
#include "../src/cache_relay.h"

static int failures = 0, total = 0;

static void check(const char *name, int got, int want)
{
	total++;
	if (got != want) { failures++; printf("  [FAIL] %-52s got=%d want=%d\n", name, got, want); }
	else             { printf("  [ ok ] %-52s = %d\n", name, got); }
}

int main(void)
{
	printf("== TASK 1.0b: GR10 cache re-push guard ==\n");

	printf("\n-- default config (CACHE FORWARD: OFF) --\n");
	check("peer fwd=0 is NOT relayed",  cache_should_relay(0, 0), 0);
	check("peer fwd=1 is NOT relayed",  cache_should_relay(0, 1), 0);

	printf("\n-- CACHE FORWARD: ON (operator opt-in / rollback path) --\n");
	check("peer fwd=0 IS relayed",      cache_should_relay(1, 0), 1);
	check("peer fwd=1 is NOT relayed",  cache_should_relay(1, 1), 0);

	printf("\n-- input robustness (the cfg field is an int, not a bool) --\n");
	check("forward=2 behaves as true",  cache_should_relay(2, 0), 1);
	check("negative forward is false",  cache_should_relay(-1, 0), 0);
	check("src_fwd=2 behaves as true",  cache_should_relay(1, 2), 0);

	printf("\n-- truth table is exhaustive over both booleans --\n");
	{
		int cfg, fwd, relayed_off = 0, relayed_on = 0;
		for (cfg = 0; cfg <= 1; cfg++)
			for (fwd = 0; fwd <= 1; fwd++)
				if (cache_should_relay(cfg, fwd)) {
					if (cfg == 0) relayed_off++; else relayed_on++;
				}
		check("nothing is relayed when FORWARD is off", relayed_off, 0);
		check("exactly one case relays when FORWARD is on", relayed_on, 1);
	}

	printf("\n== %d/%d passed, %d failed ==\n", total - failures, total, failures);
	return failures ? 1 : 0;
}
