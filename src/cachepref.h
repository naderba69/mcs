/*
 * cachepref.h -- TASK 1.7: TRUSTED-CACHE-FIRST and zap preference.
 *
 * THE PROBLEM THIS SOLVES
 * -----------------------
 * The brief names the cache as the weakest link: a poisoned entry is served
 * instantly, looks statistically ideal, and is never cross-checked. Reading
 * PIPE_CACHE_FIND in clustredcache.c shows exactly why, and it is worse at a
 * channel change than in steady state:
 *
 *   - When the client already has a key for this service, `pcache->prevcw` is
 *     non-null and a cached CW is only served if it is the *successor* of it
 *     (`dcwcmp8(pcache->prevcw, cwdata->cw)` plus a differing other half). That
 *     consecutiveness test is a genuine cross-check, and it is made against the
 *     server's own decode record, which a cache peer cannot forge.
 *
 *   - When the client has just zapped, `prevcw` is null and the "NO PREVIOUS CW"
 *     branch serves a cached CW with **no consecutiveness test at all**. The
 *     only gate is `cwdata->nbpeers >= cfg.cache.threshold`, and the default
 *     threshold is 1 (config.c:205). So one poisoned push from one peer is
 *     handed to the client instantly, on the exact request where nothing can
 *     contradict it.
 *
 * So the risk is not uniform, and neither should the policy be.
 *
 * THE RULE
 * --------
 * With TRUSTED-CACHE-FIRST off (the default) nothing here changes any
 * behaviour: every combination returns ALLOW.
 *
 * With it on, a cache hit is served immediately -- cache-first -- when either
 *
 *   (a) the source that supplied it is trusted for this (caid, provid, sid), or
 *   (b) the hit was cross-checked against the previous CW.
 *
 * Otherwise the hit is deferred: it is simply not offered to the ECM thread,
 * which then falls through to the card servers exactly as it does when the
 * cache holds nothing. Deferring is the mildest action available. It removes no
 * source, marks nothing, scores nothing, and cannot black-screen a channel: the
 * worst case is that a key which would have arrived from the cache in a few
 * milliseconds arrives from a Newcamd server instead, which is the route every
 * install already relies on when the cache misses.
 *
 * Case (a) is "trusted cache first". Case (b) is what keeps the option safe to
 * switch on: in steady state an untrusted source whose key still forms a valid
 * pair with the previous one is left alone, because that pairing is evidence it
 * is telling the truth, and suppressing it would break working setups for no
 * gain. The asymmetry -- strict at a zap, permissive in steady state -- is the
 * "zap preference": the request with no cross-check available is the one that
 * gets the verified route instead.
 *
 * WHY "UNKNOWN" IS TRUSTED
 * ------------------------
 * trust_score() returns -1 for a source it has never seen. That must be ALLOW,
 * not DEFER. GR3 is explicit that a false positive which disables a good source
 * is worse than the problem being solved, and a brand new peer has done nothing
 * to earn suspicion. A source only becomes untrusted by accumulating soft
 * evidence or producing a definitive proof, both of which are recorded
 * elsewhere; nothing in this file decides that.
 *
 * GR9: two integer comparisons. No allocation, no I/O, no growth.
 */

#ifndef MCS_CACHEPREF_H
#define MCS_CACHEPREF_H

#define CACHEPREF_ALLOW  0   /* serve the cache hit now                     */
#define CACHEPREF_DEFER  1   /* leave it; the ECM goes to the card servers  */

/*
 * Is this score trusted?
 *
 * `score` is a trust_score() return: -1 for a source never seen, otherwise
 * TRUST_FLOOR..TRUST_CEILING. `actionable` is the line below which the engine
 * considers it has enough evidence to act (TRUST_ACTIONABLE).
 *
 * Split out from cachepref_allow() because the two halves have different
 * failure modes and must be testable separately: getting this one wrong turns
 * every unknown peer into a suspect, which would be the false positive GR3
 * forbids.
 */
static inline int cachepref_trusted(int score, int actionable)
{
	if (score < 0) return 1;              /* never seen: believed          */
	return (score >= actionable) ? 1 : 0;
}

/*
 * The whole policy, as one table:
 *
 *   enabled  trusted  crosschecked  ->  result
 *      0        -          -            ALLOW   (stock r82a behaviour)
 *      1        1          -            ALLOW   (trusted cache first)
 *      1        0          1            ALLOW   (consecutiveness checked it)
 *      1        0          0            DEFER   (zap: nothing checked it)
 *
 * `crosschecked` is !isnullDCW(pcache->prevcw) at the call site, i.e. the CW
 * scan that found this hit ran the successor test rather than the bare
 * "NO PREVIOUS CW" scan.
 */
static inline int cachepref_allow(int enabled, int trusted, int crosschecked)
{
	if (!enabled) return CACHEPREF_ALLOW;
	if (trusted) return CACHEPREF_ALLOW;
	if (crosschecked) return CACHEPREF_ALLOW;
	return CACHEPREF_DEFER;
}

#endif /* MCS_CACHEPREF_H */
