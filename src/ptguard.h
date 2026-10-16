/*
 * ptguard.h -- TASK R14 (D69), the pollution guard.
 *
 * THE HOLE THIS CLOSES
 *
 * TASK 2.8 gave the coarse trust tier (trustagg.h) exactly one consequence:
 * a peer below the actionable line on a CAID is not ASKED for it (the fan-out
 * brake, PEER-TRUST-REQUESTS) -- "pushes and replies are never gated", the
 * design said, and for the ask side that was right. But a below-line peer
 * does not need to be asked: it PUSHES. Every push is stored, counted as
 * peer agreement, piped to whoever is waiting, re-pushed to the mesh (CACHE
 * FORWARD) and answered out of the cache to every peer that asks. The fake
 * that a poisoned peer keeps offering does not stay its own lie -- within
 * one cache cycle it is THE truth our node is circulating, and the next
 * victim is served it from our hand.
 *
 * WHAT THIS FILE DOES -- and deliberately does not
 *
 *   The guard extends the coarse line's ONE consequence from the ask side to
 *   the store/relay side, behind PEER-TRUST-STORE (default OFF = stock, byte
 *   for byte). Three questions, all asked of the same trustagg standing the
 *   fan-out brake already trusts:
 *
 *   1. STORE (ptg_store_allow): may this ARRIVAL be stored? A source below
 *      the line, inside its suppression window, is refused at the ingest
 *      choke point -- before any cache entry is touched, so the key is not
 *      stored, not counted as agreement, not piped, and the relay below
 *      (cache_send_fwdreply) never runs because the caller only relays on a
 *      successful return. The window semantics are trustagg_ask()'s own: a
 *      closed window refuses, an EXPIRED window lets the very next push
 *      through as the probe -- the one test that lets GOOD events exist and
 *      the score climb back. Same policy question as the ask brake, same
 *      window, second consequence site.
 *
 *   2. RELAY (ptg_relay_allow): may this STORED key be re-pumped to a peer?
 *      The key's recorded source (cw_cache_data.peerid -- the same field the
 *      purge layer judges by) answers: a below-line source's keys are not
 *      circulated to other peers while the window is closed. The CLIENT path
 *      (PIPE_CACHE_FIND / cachepref_gate) is deliberately NOT gated here:
 *      over-rejection is a named symptom of this project (a valid key killed
 *      by our own filters is a self-inflicted black screen), and what we
 *      hand our own viewers is the fine tier's decision (TRUSTED-CACHE-FIRST)
 *      and the proofs' -- never this brake. A fresh arrival from an
 *      above-line source re-vouches the node through stock's own peerid
 *      update, so the innocent peer's copy circulates again at once.
 *
 *   3. VOTE (ptg_vote_weight): while the guard is armed, a below-line
 *      source's CACHE CONSENSUS vote weighs nothing -- it cannot help mint
 *      "the circulated truth", exactly the immunity the R4 ladder's DISTRUST
 *      already gives at its own severity, now on the coarse line's clock.
 *      Window-blind on purpose: a vote is not a push to be tested, it is a
 *      verdict to be trusted.
 *
 *   ZERO GRIEF (GR3): a refusal is not an accusation. Nothing here scores
 *   anyone, notes a trust event, touches the peerrep ladder or disconnects
 *   anyone (the standing rule). The consequence is the brake's shape -- the
 *   key does not circulate -- and nothing else. The peer keeps its line, its
 *   socket and its recovery path.
 *
 * COST (GR9)
 *
 * Three pure predicates and one fixed .bss line-window table (32 slots,
 * ~1 KB): no allocation, no I/O, no lock of its own (the callers hold their
 * established lockcache -> trust_lock order; the line throttle is a plain
 * monotonic-tick table touched under the same lockcache the sites already
 * hold). The evidence lines are built after the decision, per site, at most
 * one per (site, source, CAID) per PTG_LINE_WINDOW_MS -- the ring rows and the
 * counters are the per-event surface (GR8), the line is the human's one
 * sentence per window (GR9).
 */
#ifndef MCS_PTGUARD_H
#define MCS_PTGUARD_H

#include <stdint.h>
#include <stdio.h>

/* One evidence line per (source, CAID) per window, like the crossing
 * sentence's budget. Rows and counters are per-event; the line is not. */
#define PTG_LINE_WINDOW_MS  60000

/* Fixed line-window table: (site, srctype, srcid, caid) -> last line tick.
 * The two sites get their own budget: a store refusal and a mesh withhold
 * are different facts, and one must never silence the other. */
#define PTG_LINE_SLOTS      32

/* The two sites' identities in the table key. */
#define PTG_SITE_STORE      0
#define PTG_SITE_RELAY      1

struct ptg_lineslot {
	uint32_t last;
	int      site;
	int      srctype;
	int      srcid;
	uint16_t caid;
	uint8_t  used;
};

struct ptg_linetab {
	struct ptg_lineslot s[PTG_LINE_SLOTS];
};

static void ptg_line_init(struct ptg_linetab *t)
{
	int i;
	for (i = 0; i < PTG_LINE_SLOTS; i++) t->s[i].used = 0;
}

/*
 * Store gate: armed && the source's window is closed -> refuse. OFF (or an
 * open window) is always allowed -- the probe path is the recovery path.
 */
static inline int ptg_store_allow(int armed, int open)
{
	return !armed || open;
}

/*
 * Relay gate: same predicate at the stored-key serve sites. Kept a separate
 * name because the two sites answer different questions (an arrival vs a
 * remembered key) and the unit suite states both contracts.
 */
static inline int ptg_relay_allow(int armed, int open)
{
	return !armed || open;
}

/*
 * Consensus vote: while armed, a below-line source's vote weighs nothing.
 * Unarmed (or an above-line/unknown source) keeps its stock weight.
 */
static inline int ptg_vote_weight(int armed, int below, int base)
{
	if (armed && below) return 0;
	return base;
}

/*
 * May the caller write its evidence line for this (site, source, CAID) now?
 * Writes the window stamp when it says yes; a repeat inside the window is
 * counted by the caller's counters and rows but stays silent here. Oldest
 * slot is the eviction victim (never the busiest one -- forgetting the
 * noisiest peer first would hide exactly the peer the operator needs).
 */
static int ptg_line_due(struct ptg_linetab *t, int site, int srctype, int srcid,
                        uint16_t caid, uint32_t now)
{
	int i, free_i = -1, victim = -1;
	uint32_t oldest = 0;

	for (i = 0; i < PTG_LINE_SLOTS; i++) {
		struct ptg_lineslot *s = &t->s[i];
		if (s->used && s->site == site && s->srctype == srctype &&
		    s->srcid == srcid && s->caid == caid) {
			if ((int32_t)(now - s->last) >= (int32_t)PTG_LINE_WINDOW_MS) {
				s->last = now;
				return 1;
			}
			return 0;
		}
		if (!s->used && free_i < 0) free_i = i;
		if (s->used && (victim < 0 || (int32_t)(s->last - oldest) < 0)) {
			oldest = s->last;
			victim = i;
		}
	}
	{
		struct ptg_lineslot *s = (free_i >= 0) ? &t->s[free_i]
		                                      : &t->s[(victim < 0) ? 0 : victim];
		s->site    = site;
		s->srctype = srctype;
		s->srcid   = srcid;
		s->caid    = caid;
		s->last    = now;
		s->used    = 1;
	}
	return 1;
}

/*
 * The two evidence sentences. GR8: the evidence, named -- the source the way
 * every other layer names it, the channel, and the key's own bytes, so the
 * line can be matched against a /cwlog row or a peer log without guessing.
 * Both say what the brake is NOT (no score, no disconnect): the operator
 * reading a refusal must know the peer is not being punished.
 */
__attribute__((unused))
static int ptg_format_store(char *out, int outlen,
                            int peerid, uint16_t caid, uint32_t provid,
                            uint16_t sid, const uint8_t *cw)
{
	char hex[40];
	static const char hx[] = "0123456789ABCDEF";
	int i;

	if (!out || outlen <= 0) return -1;
	if (cw) {
		for (i = 0; i < 16; i++) {
			hex[i * 2]     = hx[cw[i] >> 4];
			hex[i * 2 + 1] = hx[cw[i] & 0x0F];
		}
		hex[32] = 0;
	} else hex[0] = 0;
	return snprintf(out, (size_t)outlen,
		"TRUST STORE: push refused at ingest -- ch %04x:%06x:%04x key %s "
		"from source %d is below the trust line on this caid (suppression "
		"window running; not stored, not relayed, no score, no disconnect)",
		caid, provid, sid, hex, peerid);
}

__attribute__((unused))
static int ptg_format_relay(char *out, int outlen,
                            int peerid, uint16_t caid, uint32_t provid,
                            uint16_t sid, const uint8_t *cw)
{
	char hex[40];
	static const char hx[] = "0123456789ABCDEF";
	int i;

	if (!out || outlen <= 0) return -1;
	if (cw) {
		for (i = 0; i < 16; i++) {
			hex[i * 2]     = hx[cw[i] >> 4];
			hex[i * 2 + 1] = hx[cw[i] & 0x0F];
		}
		hex[32] = 0;
	} else hex[0] = 0;
	return snprintf(out, (size_t)outlen,
		"TRUST RELAY: stored cw withheld from the mesh -- ch %04x:%06x:%04x "
		"key %s, recorded source %d below the trust line on this caid "
		"(clients keep their path; a re-vouch from an open source reopens "
		"circulation)",
		caid, provid, sid, hex, peerid);
}

#endif /* MCS_PTGUARD_H */
