/*
 * cacheex_gates.h -- TASK R1: the three opt-in cache-exchange gates
 *
 * THE EXTERNAL CLAIMS, VERIFIED BEFORE A LINE WAS WRITTEN (D54 rule).
 * The r107-era configs in the wild carry four CACHEEX switches that the
 * official r107 reference (infosat.org/multics/index.php, fetched whole)
 * never documents: BLOCK_FAKE_CW, LOCAL_ONLY, CWCHECK, MAXHOP_LG. They
 * belong to the extended mcsql lineage, so their exact upstream semantics
 * are UNVERIFIABLE. Per the standing external-claim rule we adopt the
 * IDEA, not the patch: each gate below is defined by what THIS tree can
 * prove at a single choke point, with the r107 spellings kept so the
 * migrated operator configs become meaningful again.
 *
 * THE CHOKE POINT. Every control word that arrives through the cache-
 * exchange -- CSP UDP pushes and the cccam/camd35/cs378x cacheex pushes
 * -- lands in cache_setdcw() (clustredcache.c). Card-server answers never
 * pass here (they go through ecm_setdcw in setdcw.c). All three gates
 * live at that one function; nothing else in the data path changes.
 *
 * FAMILY. The gates apply to the TCP exchange family only
 * (cex_is_exchange(): any PEER_ORIGIN_FLAGS origin except PEER_CSP).
 * Rationale: the CSP cache already carries its own machinery (CACHE
 * FILTER / CACHE THRESHOLD / cacheguard ingress validation), while the
 * TCP exchange family had content checks only at the callers
 * (dcw_hard_reject) with no per-profile arm. An r107-style operator
 * writes CACHEEX <GATE> and means the exchange.
 *
 * THE GATES (all opt-in per profile, default OFF/0 -- stock behaviour
 * byte-for-byte identical when no gate is armed):
 *
 *   CACHEEX LOCAL_ONLY: YES
 *     An exchange push is honoured only when a LOCAL client has this
 *     ECM pending (the cache entry has a waiting ECM). Without a waiter
 *     the push is dropped BEFORE cache_new(): an exchange peer can
 *     never seed our cache DB with unsolicited keys, so it cannot
 *     pre-poison answers for future requests. This is the strongest
 *     anti-dump switch: a peer that only ever answers what we ask is
 *     unaffected -- which is exactly what a legit cacheex partner is.
 *
 *   CACHEEX BLOCK_FAKE_CW: YES
 *     For exchange arrivals the r82a content tests are forced ON
 *     regardless of the profile's DCWFILTER inherit: checksumDCW() and
 *     isbadDCW() must both pass (null and BAD-DCW list are already
 *     unconditional upstream). This is the "detected as not fake" core
 *     of the r99 note, built from this tree's own primitives -- no new
 *     heuristics invented. cwplaus/cwcm stay evidence-only (GR3); the
 *     operator asked for a block, so the block is only ever the two
 *     deterministic r82a tests, armed.
 *
 *   CACHEEX CWCHECK: N   (0 = off, capped at CEX_CHECK_MAX)
 *     An exchange arrival must be CONFIRMED: the same CW must be seen
 *     N times (the existing cwdata->nbpeers arrival counter, any cache
 *     source) before it is served. Until the floor is reached the key
 *     is counted and the normal DCW TIMEOUT flow proceeds to real
 *     servers, so a single lying peer cannot answer a channel by
 *     itself. N=1 would be the stock threshold; the gate enforces
 *     max(existing CACHE THRESHOLD, N) for exchange arrivals only.
 *
 *   MAXHOP_LG: NOT ADOPTED. No public reference documents it; the
 *   enforced send cap in this tree is CACHEEX MAXHOP. The config parser
 *   now prints a one-line notice so a migrated line is no longer
 *   silently dead (it stays inert).
 *
 * COUNTERS. Three dcwstats reasons (DCW_REJ_CEXLOCAL/CEXFAKE/CEXCONF)
 * so `telnet> dcwstats` shows exactly what each gate did.
 */
#ifndef MCS_CACHEEX_GATES_H
#define MCS_CACHEEX_GATES_H

#include "peer_origin.h"

/* CWCHECK floor cap. Above this the operator is asking for a miracle,
 * not a confirmation; the parser clamps and says so. */
#define CEX_CHECK_MAX 5

/*
 * TRUE when the srcid names a TCP cache-exchange source. CSP keeps its
 * own machinery; a zero origin (no flags) is not an exchange arrival.
 */
static inline int cex_is_exchange(int peerid)
{
	if (peerid & PEER_CSP) return 0;
	if (peerid & PEER_ORIGIN_FLAGS) return 1;
	return 0;
}

/*
 * LOCAL_ONLY verdict. Drop when armed, from the exchange family, and
 * there is no waiting local ECM behind the cache entry. `has_waiter`
 * is cache_setdcw's own (pend && pend->ecm) probe -- no new state.
 */
static inline int cex_localonly_drop(int armed, int exchange, int has_waiter)
{
	if (!armed || !exchange) return 0;
	return !has_waiter;
}

/*
 * BLOCK_FAKE_CW verdict given the two deterministic r82a tests already
 * run by the caller: `passes_tight` = checksumDCW(cw) && !isbadDCW(cw).
 * The caller only evaluates the tests when the gate is armed, so an
 * unarmed profile costs nothing.
 */
static inline int cex_fake_reject(int armed, int exchange, int passes_tight)
{
	if (!armed || !exchange) return 0;
	return !passes_tight;
}

/*
 * CWCHECK floor. Returns 1 while the arrival is still BELOW the floor
 * (caller counts and defers). `floor` is the profile's CWCHECK value;
 * `global_threshold` the CACHE THRESHOLD in force; `nbpeers` the
 * existing same-CW arrival count. The effective floor is the max of
 * the two so the gate can never WEAKEN the operator's global setting.
 */
static inline int cex_check_below_floor(int floor, int global_threshold,
					int exchange, int nbpeers)
{
	if (!exchange || floor < 1) return 0;
	if (global_threshold > floor) floor = global_threshold;
	return nbpeers < floor;
}

#endif /* MCS_CACHEEX_GATES_H */
