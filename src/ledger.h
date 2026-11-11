/*
 * ledger.h -- bounded source evidence for one exact ECM lifetime.
 *
 * The ledger deliberately models four different facts:
 *   candidate: a source offered a CW (before policy filters);
 *   accepted:  the CW passed every decode/acceptance filter;
 *   delivered: a successful protocol send handed that CW to a client;
 *   failed:    that same client retried the exact ECM after that delivery.
 *
 * A proof is possible only for a failed delivered non-reference candidate when
 * an accepted line-server reference has a different CW for the same complete
 * ECM identity. Peer/peer agreement never proves either source. Merely being
 * offered is never enough to corroborate, accuse, or complete a proof. Failures
 * are matched by ECM identity, CW bytes, source type, and source ID; they never
 * create ledger entries.
 *
 * The table is fixed-size and allocation-free. Callers serialize access with
 * trust_lock. Timestamps are 32-bit GetTickCount values; all age and elapsed
 * calculations use unsigned subtraction and are correct across wrap for the
 * deliberately short ledger window.
 */
#ifndef MCS_LEDGER_H
#define MCS_LEDGER_H

#include <string.h>
#include <stdio.h>
#include <stdint.h>

#define LEDGER_TABLE_SIZE       32
#define LEDGER_CANDIDATE_SLOTS  16
#define LEDGER_MAX_AGE_MS       20000u

#define LEDGER_NONE               0
#define LEDGER_AGREE              1
#define LEDGER_DISPUTE            2
#define LEDGER_SELFDISPUTE        3
#define LEDGER_PROOF              4
#define LEDGER_ACCEPTED           5
#define LEDGER_DELIVERED          6
#define LEDGER_FAILURE_RECORDED   7
#define LEDGER_FULL               8
#define LEDGER_REFERENCE_AGREEMENT 9

#define LEDGER_SLOT_FIRST         0
#define LEDGER_SLOT_OTHER         1
#define LEDGER_SLOT_NONE         -1

/* All fields that disambiguate a request are saved, not re-read through an
 * ECM pool pointer after that pool slot could have been reused. */
struct ledger_identity {
	uint8_t  ecmd5[16];
	uint32_t hash;
	uint32_t provid;
	uint16_t caid;
	uint16_t sid;
	uint16_t ecmlen;
	uint8_t  tag;
	uint8_t  provider_known;
	/* Captured per-profile opt-in; deliberately not part of ECM identity.
	 * It is copied to an accepted candidate, never merged across the shared
	 * ledger entry. */
	uint8_t  cache_evidence_enabled;
};

struct ledger_candidate {
	uint8_t  offered_cw[16];
	uint8_t  accepted_cw[16];
	uint32_t candidate_at;
	uint32_t accepted_at;
	uint32_t delivered_at;
	uint32_t failed_at;
	int      srctype;
	int      srcid;
	uint8_t  used;
	uint8_t  accepted;
	uint8_t  delivered;
	uint8_t  failed;
	uint8_t  proof_done;
	uint8_t  reference_credited;
	/* Latched on first acceptance: a later coalesced OFF/ON profile cannot
	 * retroactively arm this candidate's already-observed ledger evidence. */
	uint8_t  cache_evidence_enabled;
};

struct ledger_entry {
	struct ledger_identity identity;
	uint32_t seen_at;
	struct ledger_candidate candidate[LEDGER_CANDIDATE_SLOTS];
	uint16_t nagree;
	uint16_t ndispute;
	uint16_t nself;
	uint8_t  nslot;
	uint8_t  used;
};

struct ledger_table {
	struct ledger_entry e[LEDGER_TABLE_SIZE];
};

/* A value snapshot suitable for acting/logging after trust_lock is released. */
struct ledger_event {
	int      what;
	struct ledger_identity identity;
	uint8_t  cw[2][16];
	int      src[2][2];             /* [candidate][0]=type, [1]=source ID */
	uint8_t  accepted[2];
	uint8_t  delivered[2];
	uint8_t  failed[2];
	int      acc_slot;
	int      nslot;
	int      nagree;
	int      ndispute;
	int      nself;
	uint8_t  source_agreement;
	uint8_t  independent_dispute;
	uint8_t  self_dispute;
	uint8_t  matched_delivery;
	uint32_t elapsed_ms;
	uint32_t ticks;
};

static inline int ledger_key_valid(const uint8_t *ecmd5)
{
	int i;
	if (!ecmd5) return 0;
	for (i = 0; i < 16; i++) if (ecmd5[i]) return 1;
	return 0;
}

static inline int ledger_identity_valid(const struct ledger_identity *id)
{
	return id && ledger_key_valid(id->ecmd5) && id->ecmlen != 0;
}

static inline int ledger_identity_equal(const struct ledger_identity *a,
                                        const struct ledger_identity *b)
{
	return a && b && memcmp(a->ecmd5, b->ecmd5, 16) == 0 &&
	       a->hash == b->hash && a->caid == b->caid &&
	       a->provid == b->provid &&
	       !!a->provider_known == !!b->provider_known &&
	       a->sid == b->sid && a->tag == b->tag && a->ecmlen == b->ecmlen;
}

/* Callers timestamp before taking trust_lock; concurrent events may therefore
 * enter the ledger in a different order. A delta within the short 20s ledger
 * window of 2^32 is a stale captured tick, not an expired row. */
static inline uint32_t ledger_elapsed(uint32_t now, uint32_t then)
{
	uint32_t elapsed = (uint32_t)(now - then);
	if (elapsed > UINT32_MAX - LEDGER_MAX_AGE_MS) return 0;
	return elapsed;
}

static inline void ledger_touch(struct ledger_entry *entry, uint32_t ticks_now)
{
	if (entry && ledger_elapsed(ticks_now, entry->seen_at) > 0)
		entry->seen_at = ticks_now;
}

__attribute__((unused))
static void ledger_init(struct ledger_table *t)
{
	if (t) memset(t, 0, sizeof(*t));
}

/* Scan the small fixed table instead of relying on a hash probe chain. This
 * keeps identity collisions and reclaimed entries straightforward and bounded. */
static inline struct ledger_entry *ledger_find(struct ledger_table *t,
                                                const struct ledger_identity *id,
                                                uint32_t ticks_now)
{
	int i;
	if (!t || !ledger_identity_valid(id)) return NULL;
	for (i = 0; i < LEDGER_TABLE_SIZE; i++) {
		struct ledger_entry *e = &t->e[i];
		if (!e->used) continue;
		if (ledger_elapsed(ticks_now, e->seen_at) > LEDGER_MAX_AGE_MS) continue;
		if (ledger_identity_equal(&e->identity, id)) return e;
	}
	return NULL;
}

/* Insert for candidate offers only. A live entry is evicted only when all
 * slots are occupied; the largest unsigned elapsed age is the oldest. */
static inline struct ledger_entry *ledger_slot_for(struct ledger_table *t,
                                                    const struct ledger_identity *id,
                                                    uint32_t ticks_now)
{
	int i, free_slot = -1, victim = -1;
	uint32_t oldest_age = 0;
	struct ledger_entry *found;

	if (!t || !ledger_identity_valid(id)) return NULL;
	found = ledger_find(t, id, ticks_now);
	if (found) return found;

	for (i = 0; i < LEDGER_TABLE_SIZE; i++) {
		struct ledger_entry *e = &t->e[i];
		uint32_t age;
		if (!e->used || ledger_elapsed(ticks_now, e->seen_at) > LEDGER_MAX_AGE_MS) {
			if (free_slot < 0) free_slot = i;
			continue;
		}
		age = ledger_elapsed(ticks_now, e->seen_at);
		if (victim < 0 || age > oldest_age) {
			oldest_age = age;
			victim = i;
		}
	}
	if (free_slot >= 0) victim = free_slot;
	if (victim < 0) return NULL;

	memset(&t->e[victim], 0, sizeof(t->e[victim]));
	t->e[victim].identity = *id;
	t->e[victim].identity.provider_known = !!id->provider_known;
	/* Opt-in belongs to the accepted candidate, not the shared ECM row. */
	t->e[victim].identity.cache_evidence_enabled = 0;
	t->e[victim].seen_at = ticks_now;
	t->e[victim].used = 1;
	return &t->e[victim];
}

static inline void ledger_event_reset(struct ledger_event *ev)
{
	if (!ev) return;
	memset(ev, 0, sizeof(*ev));
	ev->acc_slot = LEDGER_SLOT_NONE;
}

static inline void ledger_snapshot(const struct ledger_entry *e,
                                   struct ledger_event *ev)
{
	int i;
	if (!e || !ev) return;
	ev->identity = e->identity;
	ev->nslot = e->nslot;
	ev->nagree = e->nagree;
	ev->ndispute = e->ndispute;
	ev->nself = e->nself;
	for (i = 0; i < 2 && i < e->nslot; i++) {
		memcpy(ev->cw[i], e->candidate[i].accepted ?
		       e->candidate[i].accepted_cw : e->candidate[i].offered_cw, 16);
		ev->src[i][0] = e->candidate[i].srctype;
		ev->src[i][1] = e->candidate[i].srcid;
		ev->accepted[i] = e->candidate[i].accepted;
		ev->delivered[i] = e->candidate[i].delivered;
		ev->failed[i] = e->candidate[i].failed;
	}
}

static inline int ledger_source_equal(const struct ledger_candidate *a,
                                      int srctype, int srcid)
{
	return a && a->srctype == srctype && a->srcid == srcid;
}

static inline int ledger_candidate_find_offer(const struct ledger_entry *e,
                                              const uint8_t *cw,
                                              int srctype, int srcid)
{
	int i;
	if (!e || !cw) return -1;
	for (i = 0; i < e->nslot; i++)
		if (ledger_source_equal(&e->candidate[i], srctype, srcid) &&
		    memcmp(e->candidate[i].offered_cw, cw, 16) == 0) return i;
	return -1;
}

static inline int ledger_candidate_find_accepted(const struct ledger_entry *e,
                                                 const uint8_t *cw,
                                                 int srctype, int srcid)
{
	int i;
	if (!e || !cw) return -1;
	for (i = 0; i < e->nslot; i++)
		if (e->candidate[i].accepted &&
		    ledger_source_equal(&e->candidate[i], srctype, srcid) &&
		    memcmp(e->candidate[i].accepted_cw, cw, 16) == 0) return i;
	return -1;
}

/* Fill a proof snapshot with the actual failed delivery first, then its
 * accepted line-server reference. No table pointers escape this function. */
static inline void ledger_proof_event(const struct ledger_entry *e,
                                      int delivered_slot, int alternative_slot,
                                      uint32_t ticks_now,
                                      struct ledger_event *ev)
{
	const struct ledger_candidate *delivered = &e->candidate[delivered_slot];
	const struct ledger_candidate *alternative = &e->candidate[alternative_slot];
	ledger_event_reset(ev);
	ledger_snapshot(e, ev);
	ev->identity.cache_evidence_enabled = delivered->cache_evidence_enabled;
	memcpy(ev->cw[0], delivered->accepted_cw, 16);
	memcpy(ev->cw[1], alternative->accepted_cw, 16);
	ev->src[0][0] = delivered->srctype;
	ev->src[0][1] = delivered->srcid;
	ev->src[1][0] = alternative->srctype;
	ev->src[1][1] = alternative->srcid;
	ev->accepted[0] = delivered->accepted;
	ev->delivered[0] = delivered->delivered;
	ev->failed[0] = delivered->failed;
	ev->accepted[1] = alternative->accepted;
	ev->delivered[1] = alternative->delivered;
	ev->failed[1] = alternative->failed;
	ev->what = LEDGER_PROOF;
	ev->acc_slot = LEDGER_SLOT_FIRST;
	ev->matched_delivery = 1;
	ev->elapsed_ms = (uint32_t)(delivered->failed_at - delivered->delivered_at);
	ev->ticks = ticks_now;
}

/* Return one newly completed proof. Call repeatedly to drain multiple distinct
 * delivered candidates that independently meet the exact evidence predicate. */
static inline int ledger_next_proof(struct ledger_table *t,
                                    const struct ledger_identity *id,
                                    uint32_t ticks_now,
                                    int reference_srctype,
                                    struct ledger_event *ev)
{
	struct ledger_entry *e = ledger_find(t, id, ticks_now);
	int d, a;
	if (!ev) return LEDGER_NONE;
	ledger_event_reset(ev);
	if (!e || !reference_srctype) return LEDGER_NONE;
	for (d = 0; d < e->nslot; d++) {
		struct ledger_candidate *delivered = &e->candidate[d];
		if (!delivered->delivered || !delivered->failed || delivered->proof_done ||
		    delivered->srctype == reference_srctype) continue;
		for (a = 0; a < e->nslot; a++) {
			struct ledger_candidate *alternative = &e->candidate[a];
			if (!alternative->accepted || alternative->srctype != reference_srctype ||
			    a == d) continue;
			if (ledger_source_equal(alternative, delivered->srctype, delivered->srcid)) continue;
			if (memcmp(alternative->accepted_cw, delivered->accepted_cw, 16) == 0) continue;
			delivered->proof_done = 1;
			ledger_proof_event(e, d, a, ticks_now, ev);
			return LEDGER_PROOF;
		}
	}
	return LEDGER_NONE;
}

/* Emit one cache/reference match only after both candidate records have passed
 * acceptance. Other source classes cannot consume credit or pollute cache
 * reputation. Offers and peer-peer agreement can never return this event. */
static inline int ledger_next_reference_agreement(
		struct ledger_table *t, const struct ledger_identity *id,
		uint32_t ticks_now, int candidate_srctype,
		int reference_srctype, struct ledger_event *ev)
{
	struct ledger_entry *e = ledger_find(t, id, ticks_now);
	int p, r;
	if (!ev) return LEDGER_NONE;
	ledger_event_reset(ev);
	if (!e || !candidate_srctype || !reference_srctype ||
	    candidate_srctype == reference_srctype) return LEDGER_NONE;
	for (p = 0; p < e->nslot; p++) {
		struct ledger_candidate *peer = &e->candidate[p];
		if (!peer->accepted || peer->reference_credited ||
		    peer->srctype != candidate_srctype) continue;
		for (r = 0; r < e->nslot; r++) {
			struct ledger_candidate *reference = &e->candidate[r];
			if (!reference->accepted || reference->srctype != reference_srctype ||
			    memcmp(peer->accepted_cw, reference->accepted_cw, 16) != 0) continue;
			peer->reference_credited = 1;
			ledger_snapshot(e, ev);
			ev->identity.cache_evidence_enabled = peer->cache_evidence_enabled;
			memcpy(ev->cw[0], peer->accepted_cw, 16);
			memcpy(ev->cw[1], reference->accepted_cw, 16);
			ev->src[0][0] = peer->srctype;
			ev->src[0][1] = peer->srcid;
			ev->src[1][0] = reference->srctype;
			ev->src[1][1] = reference->srcid;
			ev->accepted[0] = peer->accepted;
			ev->accepted[1] = reference->accepted;
			ev->what = LEDGER_REFERENCE_AGREEMENT;
			ev->source_agreement = 1;
			ev->ticks = ticks_now;
			return LEDGER_REFERENCE_AGREEMENT;
		}
	}
	return LEDGER_NONE;
}

/* Record a source offer before any content/policy filters. A distinct source
 * and identical CW is corroboration; a different CW is only a dispute record.
 * This function never returns PROOF and never marks a candidate accepted or
 * delivered. */
__attribute__((unused))
static int ledger_candidate(struct ledger_table *t,
                            const struct ledger_identity *id,
                            const uint8_t *cw, int srctype, int srcid,
                            uint32_t ticks_now, struct ledger_event *ev)
{
	struct ledger_entry *e;
	int i, exact, same_source_diff = 0, other_source_same = 0;
	int other_source_diff = 0, slot = -1;

	ledger_event_reset(ev);
	if (!t || !ev || !ledger_identity_valid(id) || !cw || srctype == 0) return LEDGER_NONE;
	e = ledger_slot_for(t, id, ticks_now);
	if (!e) return LEDGER_NONE;
	ledger_touch(e, ticks_now);

	exact = ledger_candidate_find_offer(e, cw, srctype, srcid);
	if (exact >= 0) {
		ledger_snapshot(e, ev);
		ev->what = LEDGER_NONE;
		return LEDGER_NONE; /* duplicate observation is not new evidence */
	}
	for (i = 0; i < e->nslot; i++) {
		struct ledger_candidate *old = &e->candidate[i];
		int same_source = ledger_source_equal(old, srctype, srcid);
		int same_key = memcmp(old->offered_cw, cw, 16) == 0;
		if (same_source && !same_key) same_source_diff = 1;
		if (!same_source && same_key) other_source_same = 1;
		if (!same_source && !same_key) other_source_diff = 1;
		if (slot < 0 && !old->used) slot = i;
	}
	if (slot < 0 && e->nslot < LEDGER_CANDIDATE_SLOTS) slot = e->nslot;
	if (slot < 0 || slot >= LEDGER_CANDIDATE_SLOTS) {
		ledger_snapshot(e, ev);
		ev->what = LEDGER_FULL;
		return LEDGER_FULL;
	}
	memset(&e->candidate[slot], 0, sizeof(e->candidate[slot]));
	memcpy(e->candidate[slot].offered_cw, cw, 16);
	e->candidate[slot].srctype = srctype;
	e->candidate[slot].srcid = srcid;
	e->candidate[slot].candidate_at = ticks_now;
	e->candidate[slot].used = 1;
	if (slot == e->nslot) e->nslot++;

	if (other_source_same) {
		if (e->nagree < UINT16_MAX) e->nagree++;
		ev->source_agreement = 1;
	}
	if (other_source_diff) {
		if (e->ndispute < UINT16_MAX) e->ndispute++;
		ev->independent_dispute = 1;
	}
	if (same_source_diff) {
		if (e->nself < UINT16_MAX) e->nself++;
		ev->self_dispute = 1;
	}
	ledger_snapshot(e, ev);
	/* Preserve event deltas across the snapshot copy. */
	ev->source_agreement = (uint8_t)other_source_same;
	ev->independent_dispute = (uint8_t)other_source_diff;
	ev->self_dispute = (uint8_t)same_source_diff;
	if (other_source_diff) ev->what = LEDGER_DISPUTE;
	else if (same_source_diff) ev->what = LEDGER_SELFDISPUTE;
	else if (other_source_same) ev->what = LEDGER_AGREE;
	else ev->what = LEDGER_NONE;
	return ev->what;
}

/* Acceptance is recorded only after every configured decode filter passes. */
__attribute__((unused))
static int ledger_accept(struct ledger_table *t,
                         const struct ledger_identity *id,
                         const uint8_t *offered_cw, const uint8_t *accepted_cw,
                         int srctype, int srcid,
                         uint32_t ticks_now, int reference_srctype,
                         struct ledger_event *ev)
{
	struct ledger_entry *e;
	int slot, rc;
	ledger_event_reset(ev);
	if (!t || !ev) return LEDGER_NONE;
	e = ledger_find(t, id, ticks_now);
	if (!e) return LEDGER_NONE;
	if (!offered_cw || !accepted_cw) return LEDGER_NONE;
	slot = ledger_candidate_find_offer(e, offered_cw, srctype, srcid);
	if (slot < 0) { ledger_snapshot(e, ev); return LEDGER_NONE; }
	ledger_touch(e, ticks_now);
	if (!e->candidate[slot].accepted) {
		e->candidate[slot].accepted = 1;
		e->candidate[slot].accepted_at = ticks_now;
		memcpy(e->candidate[slot].accepted_cw, accepted_cw, 16);
		/* The first successful acceptance owns this candidate's opt-in state.
		 * A later coalesced profile must not retroactively arm an OFF fact. */
		e->candidate[slot].cache_evidence_enabled =
		    (uint8_t)!!id->cache_evidence_enabled;
	}
	rc = ledger_next_proof(t, id, ticks_now, reference_srctype, ev);
	if (rc == LEDGER_PROOF) return rc;
	ledger_event_reset(ev);
	ledger_snapshot(e, ev);
	ev->what = LEDGER_ACCEPTED;
	ev->ticks = ticks_now;
	return LEDGER_ACCEPTED;
}

/* Delivery is committed only after the transport reports a successful send.
 * A source cannot be called delivered merely because it won selection. */
__attribute__((unused))
static int ledger_delivered(struct ledger_table *t,
                            const struct ledger_identity *id,
                            const uint8_t *cw, int srctype, int srcid,
                            uint32_t ticks_now, int reference_srctype,
                            struct ledger_event *ev)
{
	struct ledger_entry *e;
	int slot, rc;
	ledger_event_reset(ev);
	if (!t || !ev) return LEDGER_NONE;
	e = ledger_find(t, id, ticks_now);
	if (!e) return LEDGER_NONE;
	slot = ledger_candidate_find_accepted(e, cw, srctype, srcid);
	if (slot < 0 || !e->candidate[slot].accepted) {
		ledger_snapshot(e, ev);
		return LEDGER_NONE;
	}
	ledger_touch(e, ticks_now);
	if (!e->candidate[slot].delivered) {
		e->candidate[slot].delivered = 1;
		e->candidate[slot].delivered_at = ticks_now;
	}
	rc = ledger_next_proof(t, id, ticks_now, reference_srctype, ev);
	if (rc == LEDGER_PROOF) return rc;
	ledger_event_reset(ev);
	ledger_snapshot(e, ev);
	ev->what = LEDGER_DELIVERED;
	ev->ticks = ticks_now;
	return LEDGER_DELIVERED;
}

/* A failure must identify the exact ECM, CW, source, and successful delivery
 * timestamp saved by the transport. No lookup-by-hash, no source-only match,
 * no entry creation, and no proof for an unaccepted/rejected offer. */
__attribute__((unused))
static int ledger_failed(struct ledger_table *t,
                         const struct ledger_identity *id,
                         const uint8_t *cw, int srctype, int srcid,
                         uint32_t delivered_at, uint32_t ticks_now,
                         int reference_srctype, struct ledger_event *ev)
{
	struct ledger_entry *e;
	int slot, rc;
	ledger_event_reset(ev);
	if (!t || !ev) return LEDGER_NONE;
	e = ledger_find(t, id, ticks_now);
	if (!e) return LEDGER_NONE;
	slot = ledger_candidate_find_accepted(e, cw, srctype, srcid);
	if (slot < 0 || !e->candidate[slot].accepted || !e->candidate[slot].delivered) {
		ledger_snapshot(e, ev);
		return LEDGER_NONE;
	}
	ledger_touch(e, ticks_now);
	e->candidate[slot].failed = 1;
	e->candidate[slot].failed_at = ticks_now;
	/* The protocol's saved last-delivery timestamp is client-specific. Use it
	 * for elapsed time rather than pretending all clients got the key together. */
	e->candidate[slot].delivered_at = delivered_at;
	ledger_snapshot(e, ev);
	ev->matched_delivery = 1;
	ev->ticks = ticks_now;
	rc = ledger_next_proof(t, id, ticks_now, reference_srctype, ev);
	if (rc == LEDGER_PROOF) return rc;
	ledger_event_reset(ev);
	ledger_snapshot(e, ev);
	ev->what = LEDGER_FAILURE_RECORDED;
	ev->matched_delivery = 1;
	ev->ticks = ticks_now;
	return LEDGER_FAILURE_RECORDED;
}

static inline int ledger_format(const struct ledger_event *ev,
                                char *out, int outlen)
{
	int n;
	if (!ev || !out || outlen <= 0) return -1;
	if (ev->what != LEDGER_PROOF || ev->acc_slot != LEDGER_SLOT_FIRST) {
		out[0] = 0;
		return -1;
	}
	n = snprintf(out, (size_t)outlen,
		" !!! AGREEMENT MISMATCH: ch %04x:provider-%s%06x:%04x tag %02x len %u hash %08x ecm %02x%02x%02x%02x"
		" | delivered source %d/%d key %02x%02x%02x%02x%02x%02x%02x%02x"
		" accepted=%s delivered=%s failed=%s after %u ms"
			" | line reference %d/%d key %02x%02x%02x%02x%02x%02x%02x%02x accepted=%s"
		" | proof recorded against %d/%d",
		(unsigned)ev->identity.caid,
		ev->identity.provider_known ? "known-" : "unknown-",
		(unsigned)ev->identity.provid, (unsigned)ev->identity.sid,
		(unsigned)ev->identity.tag, (unsigned)ev->identity.ecmlen,
		(unsigned)ev->identity.hash,
		ev->identity.ecmd5[0], ev->identity.ecmd5[1], ev->identity.ecmd5[2], ev->identity.ecmd5[3],
		ev->src[0][0], ev->src[0][1],
		ev->cw[0][0], ev->cw[0][1], ev->cw[0][2], ev->cw[0][3],
		ev->cw[0][4], ev->cw[0][5], ev->cw[0][6], ev->cw[0][7],
		ev->accepted[0] ? "yes" : "no", ev->delivered[0] ? "yes" : "no",
		ev->failed[0] ? "yes" : "no", (unsigned)ev->elapsed_ms,
		ev->src[1][0], ev->src[1][1],
		ev->cw[1][0], ev->cw[1][1], ev->cw[1][2], ev->cw[1][3],
		ev->cw[1][4], ev->cw[1][5], ev->cw[1][6], ev->cw[1][7],
		ev->accepted[1] ? "yes" : "no", ev->src[0][0], ev->src[0][1]);
	return n;
}

#endif /* MCS_LEDGER_H */
