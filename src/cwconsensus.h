/*
 * TASK R8 (D62) -- cwconsensus: the per-request arbitrer between
 * contradicting cache keys. A cache push for a pending ECM used to be
 * judged on structure alone (checksum, null, badlist, cycle); structurally
 * perfect fakes sailed through and FIRST ARRIVAL won the client. This
 * module adds the missing question: what do the OTHER witnesses say?
 *
 * One tally per (channel, ecmd5, cw) records DISTINCT voters (a flood of
 * re-pushes from one peer is still one vote -- stock nbpeers counted
 * arrivals, so one eager liar defeated CACHE THRESHOLD). Voters carry a
 * WEIGHT: the caller feeds 0 for a peer already at distrust on the R4
 * ladder, 1 otherwise -- so reputation, not timing, decides conflicts.
 *
 * Verdict contract (see cwconsensus_verdict):
 *   CWC_DELIVER -- no objection; caller delivers as stock would.
 *   CWC_HOLD    -- first key, fewer than 2 distinct voters, no conflict:
 *                  caller skips delivery (the stock CWCHECK hold shape);
 *                  cwconsensus_expired() pops it at window expiry so a
 *                  single-source channel loses only the window, never the
 *                  key (availability first). The hold blocks ONLY the
 *                  cache source; card servers are untouched.
 *   CWC_REFUSE  -- this key lost a weighted decision against a
 *                  contradicting key on the same request: caller rejects
 *                  the node, counts dcwstats[consensus-mismatch] and
 *                  convicts the sender on the ladder. Losing on WEIGHT is
 *                  evidence; losing on TIMING alone (equal weights) is
 *                  refused silently -- an honest-but-slow peer must not be
 *                  convicted for arriving second.
 *
 * Residual, documented on purpose: a structurally perfect fake that wins
 * the timing race on a cold peer delivers ONCE; its next loss on the same
 * channel still teaches the ladder nothing by itself, but ANY weighted
 * loss -- including from a cycle-contradiction record -- flips every later
 * request of the window. Consensus arbitrates ONE request; the R4 ladder
 * is the cross-request memory.
 *
 * The table is small, fixed and tick-driven: 64 slots, entries die by
 * window expiry, a full table reclaims expired slots and otherwise fails
 * open (CWC_DELIVER with no votes recorded). Availability first, always.
 */
#ifndef CWCONSENSUS_H
#define CWCONSENSUS_H

#define CWC_DELIVER 0
#define CWC_HOLD    1
#define CWC_REFUSE  2

struct cwconsensus_hold {
	uint16_t caid;
	uint16_t sid;
	uint16_t provid;
	uint32_t hash;
	uint8_t  tag;
	uint8_t  cw[16];
	int      peerid;
};

void cwconsensus_init(void);

/*
 * Record one arrival and judge it. weight = the sender's vote weight
 * (0 = distrusted on the ladder, 1 = clean/unknown). window_ms = the
 * corroboration window (caller clamps). loser_cw (may be NULL) receives
 * the losing key's bytes when this arrival WON against a held/earlier
 * conflict, so the caller can neutralize the loser's stored node.
 */
int cwconsensus_verdict(uint16_t caid, uint16_t sid, uint32_t hash,
			uint8_t tag, const uint8_t *cw, int peerid,
			int weight, uint32_t now_ms, uint32_t window_ms,
			const uint8_t **loser_cw);

/* Pop ONE hold whose window expired (holding slots only -- lost or
 * delivered slots never come back). Returns 1 when out was filled. */
int cwconsensus_expired(struct cwconsensus_hold *out, uint32_t now_ms,
			uint32_t window_ms);

/* counters for the log tally */
unsigned long cwconsensus_votes(void);
unsigned long cwconsensus_holds(void);
unsigned long cwconsensus_releases(void);
unsigned long cwconsensus_refusals(void);

#endif /* CWCONSENSUS_H */
