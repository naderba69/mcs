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
 * The table is small, fixed and tick-driven: 256 slots, held entries are popped by
 * window expiry, non-held entries can be reclaimed after their last activity exceeds the
 * current window; otherwise it fails open (CWC_DELIVER with no votes recorded). Availability first, always.
 */
#ifndef CWCONSENSUS_H
#define CWCONSENSUS_H

#define CWC_CAPACITY 256 /* bounded; power of two */

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
	int      cycle; /* declared cycle from the first held vote */
};

void cwconsensus_init(void);

/*
 * TASK R29 (D80): how many DISTINCT voters a key needs before the
 * no-conflict early delivery fires. The default stays 2 (D62's
 * rule, untouched stock semantics for every existing deployment).
 * Raising it (CACHE CONSENSUS SOURCES: 3..8) hardens the exact
 * flood the field reports: many lines echoing ONE manufactured key
 * must no longer masquerade as corroboration -- agreement is
 * counted in independent voters, never in arrivals, and now the
 * operator chooses how much independence a key needs before it may
 * answer a waiting client early. A key that never reaches the
 * threshold is still released at window expiry (availability
 * first), and conflicts still settle by weight, so a lone honest
 * slow line can still beat a fast fake crowd on weight evidence.
 * Set once from config; values outside 2..8 are clamped.
 */
void cwconsensus_set_min_sources(int n);

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

/* Query the candidate's live vote counts; caller must hold the cache lock.
 * Distinct peer IDs are not necessarily independent sources. */
int cwconsensus_vote_counts(uint16_t caid, uint16_t sid, uint32_t hash,
                            uint8_t tag, const uint8_t *cw, uint32_t now_ms,
                            uint32_t window_ms, int *distinct, int *weight);

/* Snapshot under the cache lock; held includes overdue holds, and retained
 * means non-held entries. These counts do not attribute entries to a client. */
struct cwconsensus_capacity {
    int used, capacity, held, overdue, reclaimable;
    uint32_t oldest_hold_ms;
};
void cwconsensus_capacity(uint32_t now_ms, uint32_t window_ms,
                          struct cwconsensus_capacity *out);

/* Preserve the first voter's wire-cycle declaration while a key is held.
 * Called under the same lock as verdict/expiry; does not record a new vote. */
void cwconsensus_hold_cycle(uint16_t caid, uint16_t sid, uint32_t hash,
                            uint8_t tag, const uint8_t *cw, int peerid, int cycle);

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
