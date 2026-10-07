/*
 * cache_relay.h -- GR10 guard for cache-peer CW re-push.
 *
 * Problem (verified in the r82a tree)
 * -----------------------------------
 * The cache has two paths that send a control word to a cache peer:
 *
 *   Path A -- answer a peer's cache REQUEST from what we hold.
 *             clustredcache.c:1571, gated by cfg.cache.forward.
 *
 *   Path B -- re-push a CW we just RECEIVED from a peer to every other
 *             peer that has `fwd` set.
 *             clustredcache.c:1619-1621 -> cache_send_fwdreply() at :950.
 *             **Not gated by anything.**
 *
 * Path B is the poison-amplification route: one peer that pushes a bad CW
 * has it rebroadcast to the whole mesh, so purging the local cache (Phase 1
 * TASK 1.5) cannot help while the relay keeps re-seeding it.
 *
 * Fix
 * ---
 * Path B now honours the same documented `CACHE FORWARD` switch that already
 * governs path A (parsed at config.c:2921, hot-reloaded at config.c:6059).
 * No new config key is introduced: two keys for one concept is how this bug
 * survived in the first place. The default is 0, so a default config is
 * GR10-compliant without any configuration.
 *
 * This file holds only the decision, as a pure function with no
 * dependencies, so the real predicate can be unit-tested without linking
 * the cache subsystem.
 */
#ifndef MCS_CACHE_RELAY_H
#define MCS_CACHE_RELAY_H

/*
 * Decide whether a CW that arrived FROM a cache peer may be re-pushed to
 * other cache peers.
 *
 *   forward_cfg  -- cfg.cache.forward (`CACHE FORWARD: ON/OFF`)
 *   src_fwd      -- the sending peer's `fwd` flag (cachepeer_data.fwd)
 *
 * The `!src_fwd` term is the pre-existing r82 condition and is preserved
 * unchanged: a peer that forwards on its own does not need our relay.
 * What this adds is the `forward_cfg` term.
 */
static int cache_should_relay(int forward_cfg, int src_fwd)
{
	/*
	 * Normalise, do not merely test. cfg.cache.forward comes from
	 * parse_boolean() so it is 0 or 1 in practice, but a guard that
	 * decides whether to amplify a peer's CW must fail SAFE: any value
	 * that is not clearly "on" means "do not relay". A bare !forward_cfg
	 * would treat a negative as "on", which is the wrong way round.
	 */
	if (forward_cfg <= 0) return 0;   /* GR10: never amplify a peer's CW */
	if (src_fwd)          return 0;   /* unchanged r82 condition         */
	return 1;
}

#endif /* MCS_CACHE_RELAY_H */
