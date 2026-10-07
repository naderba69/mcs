/*
 * cache_threshold.h — TASK 1.11a
 *
 * The cache's minimum-peer test, extracted so it can be unit-tested.
 *
 * The r82a line it replaces read:
 *
 *     if (cwdata->nbpeers != cfg.cache.threshold) return DCW_ERROR | DCW_SKIP;
 *
 * The comment above it says "TEST for min Peers", so the intent is a floor:
 * accept once at least `threshold` independent peers have supplied this same
 * CW. `!=` is not a floor. With the shipped default of 1 it happens to behave
 * like one, which is why nobody noticed, but at any threshold above 1 it
 * accepts a control word on exactly the Nth peer and then REJECTS it for every
 * peer after that. `CACHE THRESHOLD: 2` therefore does not require two peers;
 * it requires exactly two and breaks on the third.
 *
 * Worse, the rejection is `DCW_ERROR | DCW_SKIP`. Six call sites use the SKIP
 * bit to decide whether to count the event against a source
 * (`if (!(res & DCW_SKIP)) cli->cacheex.badcw++`, at srv-cccam.c:979,
 * srv-camd35.c:300, srv-cs378x.c:433, cli-cccam.c:502, cli-camd35.c:240,
 * cli-cs378x.c:310), so a below-threshold CW is deliberately not blamed on
 * anybody. That part was already correct, and it is the reason this bug stayed
 * invisible: it broke the feature without creating a false accusation.
 *
 * A header rather than a function in clustredcache.c because that file is not a
 * translation unit — main.c #includes it — and a header can be tested alone.
 */
#ifndef MCS_CACHE_THRESHOLD_H
#define MCS_CACHE_THRESHOLD_H

/*
 * Has this CW reached the minimum number of peers?
 *
 *   nbpeers   how many peers have supplied this exact CW so far, this one
 *             included. The caller increments before asking.
 *   threshold the configured floor. config.c clamps it to [1, 30]
 *             (config.c:2937-2939), so it cannot be 0 in a running server;
 *             a defensive <1 here keeps the predicate total for tests.
 *
 * Returns 1 to accept, 0 to hold the CW until another peer agrees.
 */
static inline int cache_threshold_reached(int nbpeers, int threshold)
{
	if (threshold < 1) threshold = 1;
	return nbpeers >= threshold;
}

#endif /* MCS_CACHE_THRESHOLD_H */
