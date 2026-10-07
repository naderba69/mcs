/*
 * peer_origin.h — TASK 1.1, the unified CW-origin model
 *
 * A cached control word arrives from one of five kinds of producer, and r82a
 * encodes WHICH ONE in the high bits of the same word that carries the peer id.
 * That encoding already works, but it was spread across four files with the
 * flags defined once, in the middle of clustredcache.c, and no single place
 * that says what the values mean. Three call sites stripped the flags before a
 * lookup and a fourth did not, so one attribution display silently never
 * rendered.
 *
 * This header is the single definition. It is a header rather than a .c because
 * clustredcache.c is not a translation unit — main.c #includes it — and because
 * a header can be unit-tested on its own.
 *
 * The id layout, for a value stored in `dcwsrcid` / `cwdata->peerid`:
 *
 *     bit  31            20 19 18 17 16 15                    0
 *          [ unused        ] [  origin flags  ] [   peer id    ]
 *
 * so `peerid | PEER_CSP` is a CSP peer's CW, and `peerid & PEER_ID_MASK`
 * recovers the id for a lookup.
 */
#ifndef MCS_PEER_ORIGIN_H
#define MCS_PEER_ORIGIN_H

/* Where a cached CW came from. Mutually exclusive. */
#define PEER_CSP             0x010000  /* a CSP-protocol cache peer        */
#define PEER_CCCAM_CLIENT    0x020000  /* a CCcam client, cache-exchange   */
#define PEER_CAMD35_CLIENT   0x040000  /* a camd35 client, cache-exchange  */
#define PEER_CS378X_CLIENT   0x080000  /* a cs378x client, cache-exchange  */
#define PEER_CACHEEX_SERVER  0x100000  /* an upstream cache-exchange server */

#define PEER_ORIGIN_FLAGS    0x1F0000  /* all five, for validation */
#define PEER_ID_MASK         0xFFFF    /* everything below the flags */

/* Origin kinds, in ascending bit order. Used for tables and counters. */
#define PEER_ORIGIN_COUNT    5

/*
 * Recover the bare peer id from a possibly-flagged word.
 *
 * Every lookup of a cached CW's origin must go through this. Doing the mask at
 * the call site instead is how the radegast "last used share" display ended up
 * calling getpeerbyid() with `peerid|PEER_CSP` and therefore never matching.
 */
static inline int peer_origin_id(int srcid)
{
	return srcid & PEER_ID_MASK;
}

/*
 * Which origin flag is set, or 0 when none is.
 *
 * Returns the flag itself rather than an index so the result can be compared
 * directly against the PEER_* constants and used in the existing
 * `if (srcid & PEER_CSP)` style of test.
 */
static inline int peer_origin_flag(int srcid)
{
	return srcid & PEER_ORIGIN_FLAGS;
}

/*
 * Was this CW pushed by a client rather than by a peer or an upstream server?
 *
 * Phase 1 counts and attributes client-pushed CWs but never penalises them
 * (DECISIONS.md D2), because a client pushing a CW is not a source the operator
 * subscribed to and cannot be held to the same standard. Phase 2 is where
 * cache-peer trust gets its own engine. This predicate is the switch between
 * the two, so it has to be exactly right.
 */
static inline int peer_origin_is_client_push(int srcid)
{
	return (srcid & (PEER_CCCAM_CLIENT | PEER_CAMD35_CLIENT | PEER_CS378X_CLIENT)) != 0;
}

#endif /* MCS_PEER_ORIGIN_H */
