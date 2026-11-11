/*
 * r48-source-reputation.h — D111 source-by-provider evidence and EWMA.
 *
 * This is a reference-evidence table. It is updated only by the caller after a
 * complete ledger event: an accepted cache CW that matched an accepted
 * line-server reference (positive), or a delivered CW whose client retry was
 * proved against a different accepted line-server reference (negative). Offers,
 * peer/peer agreement, and unaccepted candidates are not observations here.
 *
 * Scope is (source type, source id, CAID, provider presence/value). SID is
 * deliberately omitted: this is provider reputation, not channel reputation.
 * Source IDs retain their origin bits.
 *
 * The score is fixed-point EWMA, 0..100 with alpha=1/8 per confirmed event,
 * initialized at neutral 50. Once per elapsed minute it also moves 1/8 toward
 * neutral. A confirmed proof additionally applies D-4's 15-minute rank-only
 * delay until a clean reference match is observed; candidates remain eligible.
 * The layer has no hard-block or disconnect action.
 *
 * The table is fixed-size, allocation-free, and has no internal lock. Production
 * callers serialize it with trust_lock. The profile's CACHE EVIDENCE arm is
 * carried on each accepted ledger candidate; callers must be ON before they
 * record a fact, and an unrelated coalesced profile must not arm that event.
 */
#ifndef MCS_R48_SOURCE_REPUTATION_H
#define MCS_R48_SOURCE_REPUTATION_H

#include <stdint.h>
#include <string.h>
#include "tick_math.h"

#define R48_SOURCE_REP_SLOTS          128u
#define R48_SOURCE_REP_DECAY_MS        60000u
#define R48_SOURCE_REP_RANK_DELAY_MS  900000u  /* D111 D-4: 15 minutes */
#define R48_SOURCE_REP_ALPHA_SHIFT     3u
#define R48_SOURCE_REP_SCORE_SCALE     256u
#define R48_SOURCE_REP_NEUTRAL_Q8      (50u * R48_SOURCE_REP_SCORE_SCALE)
#define R48_SOURCE_REP_MAX_Q8          (100u * R48_SOURCE_REP_SCORE_SCALE)

#define R48_SOURCE_REP_NONE             0
#define R48_SOURCE_REP_REFERENCE_MATCH  1
#define R48_SOURCE_REP_REFERENCE_PROOF  2

struct r48_source_rep_entry {
	uint32_t provid;
	uint32_t last_evidence_ms;
	uint32_t last_decay_ms;
	uint32_t last_proof_ms;
	uint32_t clean_after_proof;
	uint32_t matches;
	uint32_t proofs;
	int32_t  srcid;
	int32_t  srctype;
	uint16_t caid;
	uint16_t score_q8;
	uint8_t  provider_known;
	uint8_t  observed;
	uint8_t  used;
};

struct r48_source_rep_table {
	struct r48_source_rep_entry entry[R48_SOURCE_REP_SLOTS];
	uint32_t evictions;
	uint32_t invalid_events;
};

struct r48_source_rep_snapshot {
	uint32_t provid;
	uint32_t matches;
	uint32_t proofs;
	uint32_t last_evidence_ms;
	uint32_t last_proof_ms;
	uint32_t clean_after_proof;
	int32_t  srcid;
	int32_t  srctype;
	uint16_t caid;
	uint8_t  score;
	uint8_t  provider_known;
	uint8_t  rank_delayed;
};

__attribute__((unused))
static void r48_source_rep_init(struct r48_source_rep_table *table)
{
	if (table) memset(table, 0, sizeof(*table));
}

static inline int r48_source_rep_key_equal(const struct r48_source_rep_entry *entry,
                                           int srctype, int srcid,
                                           uint16_t caid, uint32_t provid,
                                           int provider_known)
{
	uint8_t known = (uint8_t)!!(provider_known || provid != 0);
	return entry && entry->used && entry->srctype == srctype &&
	       entry->srcid == srcid && entry->caid == caid &&
	       entry->provid == provid && entry->provider_known == known;
}

__attribute__((unused))
static struct r48_source_rep_entry *r48_source_rep_find(
		struct r48_source_rep_table *table, int srctype, int srcid,
		uint16_t caid, uint32_t provid, int provider_known)
{
	unsigned i;
	if (!table) return NULL;
	for (i = 0; i < R48_SOURCE_REP_SLOTS; i++)
		if (r48_source_rep_key_equal(&table->entry[i], srctype, srcid,
		                             caid, provid, provider_known))
			return &table->entry[i];
	return NULL;
}

static inline uint32_t r48_source_rep_sat_inc_value(uint32_t value)
{
	return value == UINT32_MAX ? value : value + 1u;
}

/* Ledger events and rank lookups capture GetTickCount before taking
 * trust_lock, so a later timestamp can be committed first. Treat a delta that
 * is within the maximum 15-minute evidence window of 2^32 as a stale tick,
 * not as a 49-day-old fact. Genuine long forward gaps below one full counter
 * wrap remain elapsed, including gaps longer than INT32_MAX. */
static inline uint32_t r48_source_rep_elapsed(uint32_t now_ms,
                                              uint32_t then_ms)
{
	uint32_t elapsed = tick32_elapsed(now_ms, then_ms);
	if (elapsed > UINT32_MAX - R48_SOURCE_REP_RANK_DELAY_MS)
		return 0;
	return elapsed;
}

static inline int r48_source_rep_tick_at_or_after(uint32_t now_ms,
                                                   uint32_t then_ms)
{
	uint32_t elapsed = tick32_elapsed(now_ms, then_ms);
	return elapsed == 0 ||
	       elapsed <= UINT32_MAX - R48_SOURCE_REP_RANK_DELAY_MS;
}

/* D-4's first rung lowers rank for 15 minutes and requires a clean reference
 * sample before release. It never removes the candidate, so promise C still
 * has a non-empty fallback when every source is under delay. */
static inline int r48_source_rep_rank_is_delayed(
		const struct r48_source_rep_entry *entry, uint32_t now_ms)
{
	if (!entry || !entry->proofs) return 0;
	if (r48_source_rep_elapsed(now_ms, entry->last_proof_ms) <
	    R48_SOURCE_REP_RANK_DELAY_MS)
		return 1;
	return entry->clean_after_proof == 0;
}

/* Move the Q8 score toward neutral. After 64 steps the residual is below
 * 0.01 displayed score points; longer quiet periods snap that negligible
 * remainder to neutral, avoiding work proportional to process uptime. */
static inline void r48_source_rep_apply_decay(struct r48_source_rep_entry *entry,
                                               uint32_t now_ms)
{
	uint32_t elapsed, steps, applied;
	uint32_t score;
	unsigned i;
	if (!entry || !entry->used) return;
	elapsed = r48_source_rep_elapsed(now_ms, entry->last_decay_ms);
	if (!elapsed) return; /* stale asynchronous event or same tick */
	steps = elapsed / R48_SOURCE_REP_DECAY_MS;
	if (!steps) return;
	applied = steps > 64u ? 64u : steps;
	score = entry->score_q8;
	for (i = 0; i < applied; i++) {
		if (score > R48_SOURCE_REP_NEUTRAL_Q8) {
			uint32_t delta = score - R48_SOURCE_REP_NEUTRAL_Q8;
			delta = (delta * 7u + 4u) / 8u;
			score = R48_SOURCE_REP_NEUTRAL_Q8 + delta;
		} else if (score < R48_SOURCE_REP_NEUTRAL_Q8) {
			uint32_t delta = R48_SOURCE_REP_NEUTRAL_Q8 - score;
			delta = (delta * 7u + 4u) / 8u;
			score = R48_SOURCE_REP_NEUTRAL_Q8 - delta;
		} else break;
	}
	if (steps > 64u) score = R48_SOURCE_REP_NEUTRAL_Q8;
	entry->score_q8 = (uint16_t)score;
	/* Preserve the sub-minute remainder and make the addition wrap-safe. */
	entry->last_decay_ms += steps * R48_SOURCE_REP_DECAY_MS;
}

static inline struct r48_source_rep_entry *r48_source_rep_get_or_create(
		struct r48_source_rep_table *table, int srctype, int srcid,
		uint16_t caid, uint32_t provid, int provider_known,
		uint32_t now_ms)
{
	struct r48_source_rep_entry *entry;
	int i, free_slot = -1, victim = -1;
	uint32_t oldest_age = 0;
	uint8_t known = (uint8_t)!!(provider_known || provid != 0);

	entry = r48_source_rep_find(table, srctype, srcid, caid, provid, known);
	if (entry) return entry;
	for (i = 0; i < (int)R48_SOURCE_REP_SLOTS; i++) {
		uint32_t age;
		if (!table->entry[i].used) {
			if (free_slot < 0) free_slot = i;
			continue;
		}
		age = r48_source_rep_elapsed(now_ms,
		                             table->entry[i].last_evidence_ms);
		if (victim < 0 || age > oldest_age) {
			victim = i;
			oldest_age = age;
		}
	}
	if (free_slot >= 0) victim = free_slot;
	if (victim < 0) return NULL;
	if (table->entry[victim].used)
		table->evictions = r48_source_rep_sat_inc_value(table->evictions);
	entry = &table->entry[victim];
	memset(entry, 0, sizeof(*entry));
	entry->srctype = srctype;
	entry->srcid = srcid;
	entry->caid = caid;
	entry->provid = provid;
	entry->provider_known = known;
	entry->score_q8 = (uint16_t)R48_SOURCE_REP_NEUTRAL_Q8;
	entry->last_evidence_ms = now_ms;
	entry->last_decay_ms = now_ms;
	entry->used = 1;
	return entry;
}

/* Caller validates the profile opt-in and holds trust_lock. Returns 1 for a
 * recorded fact and 0 for invalid input or a table failure. */
__attribute__((unused))
static int r48_source_rep_note(struct r48_source_rep_table *table,
                               int srctype, int srcid,
                               uint16_t caid, uint32_t provid,
                               int provider_known, int event,
                               uint32_t now_ms)
{
	struct r48_source_rep_entry *entry;
	uint32_t target, score, distance;
	if (!table || !srctype || !caid ||
	    (event != R48_SOURCE_REP_REFERENCE_MATCH &&
	     event != R48_SOURCE_REP_REFERENCE_PROOF)) {
		if (table)
			table->invalid_events =
			    r48_source_rep_sat_inc_value(table->invalid_events);
		return 0;
	}
	entry = r48_source_rep_get_or_create(table, srctype, srcid, caid,
	                                     provid, provider_known, now_ms);
	if (!entry) return 0;
	r48_source_rep_apply_decay(entry, now_ms);
	score = entry->score_q8;
	target = event == R48_SOURCE_REP_REFERENCE_MATCH ?
	         R48_SOURCE_REP_MAX_Q8 : 0u;
	if (target > score) {
		distance = target - score;
		score += (distance + (1u << (R48_SOURCE_REP_ALPHA_SHIFT - 1u))) >>
		         R48_SOURCE_REP_ALPHA_SHIFT;
	} else {
		distance = score - target;
		score -= (distance + (1u << (R48_SOURCE_REP_ALPHA_SHIFT - 1u))) >>
		         R48_SOURCE_REP_ALPHA_SHIFT;
	}
	if (score > R48_SOURCE_REP_MAX_Q8) score = R48_SOURCE_REP_MAX_Q8;
	entry->score_q8 = (uint16_t)score;
	entry->observed = 1;
	if (r48_source_rep_elapsed(now_ms, entry->last_evidence_ms) > 0)
		entry->last_evidence_ms = now_ms;
	if (event == R48_SOURCE_REP_REFERENCE_MATCH) {
		entry->matches = r48_source_rep_sat_inc_value(entry->matches);
		/* Delayed ledger events can be processed out of order. Only a match
		 * with a strictly later monotonic tick can clear the proof delay. */
		if (entry->proofs &&
		    r48_source_rep_elapsed(now_ms, entry->last_proof_ms) > 0)
			entry->clean_after_proof =
			    r48_source_rep_sat_inc_value(entry->clean_after_proof);
	} else {
		int is_newest_proof = !entry->proofs ||
		                      r48_source_rep_tick_at_or_after(
		                          now_ms, entry->last_proof_ms);
		entry->proofs = r48_source_rep_sat_inc_value(entry->proofs);
		if (is_newest_proof) {
			entry->last_proof_ms = now_ms;
			entry->clean_after_proof = 0;
		}
	}
	return 1;
}

/* The profile arm is a fact-collection gate only; OFF leaves the table
 * untouched and does not treat a missing row as a bad source. */
__attribute__((unused))
static int r48_source_rep_note_opt_in(struct r48_source_rep_table *table,
                                      int enabled, int srctype, int srcid,
                                      uint16_t caid, uint32_t provid,
                                      int provider_known, int event,
                                      uint32_t now_ms)
{
	if (!enabled) return 0;
	return r48_source_rep_note(table, srctype, srcid, caid, provid,
	                           provider_known, event, now_ms);
}

/* Return a fixed-point ordering score for the evidence selector. The caller
 * holds trust_lock. Unknown sources rank at neutral; missing evidence is never
 * interpreted as a failure. The D-4 delay returns rank zero but keeps the
 * candidate eligible, and evidence decay is applied before a normal score. */
__attribute__((unused))
static uint16_t r48_source_rep_rank_q8(struct r48_source_rep_table *table,
                                       int srctype, int srcid,
                                       uint16_t caid, uint32_t provid,
                                       int provider_known, uint32_t now_ms)
{
	struct r48_source_rep_entry *entry = r48_source_rep_find(
	    table, srctype, srcid, caid, provid, provider_known);
	if (!entry || !entry->observed)
		return (uint16_t)R48_SOURCE_REP_NEUTRAL_Q8;
	r48_source_rep_apply_decay(entry, now_ms);
	if (r48_source_rep_rank_is_delayed(entry, now_ms)) return 0;
	return entry->score_q8;
}

/* Diagnostic query: unknown entries return 0 and must not be treated as bad. */
__attribute__((unused))
static int r48_source_rep_query(struct r48_source_rep_table *table,
                                int srctype, int srcid,
                                uint16_t caid, uint32_t provid,
                                int provider_known, uint32_t now_ms,
                                struct r48_source_rep_snapshot *out)
{
	struct r48_source_rep_entry *entry;
	uint32_t score;
	if (out) memset(out, 0, sizeof(*out));
	entry = r48_source_rep_find(table, srctype, srcid, caid,
	                            provid, provider_known);
	if (!entry || !entry->observed) return 0;
	r48_source_rep_apply_decay(entry, now_ms);
	if (out) {
		score = (entry->score_q8 + R48_SOURCE_REP_SCORE_SCALE / 2u) /
		        R48_SOURCE_REP_SCORE_SCALE;
		if (score > 100u) score = 100u;
		out->provid = entry->provid;
		out->matches = entry->matches;
		out->proofs = entry->proofs;
		out->last_evidence_ms = entry->last_evidence_ms;
		out->last_proof_ms = entry->last_proof_ms;
		out->clean_after_proof = entry->clean_after_proof;
		out->rank_delayed = (uint8_t)r48_source_rep_rank_is_delayed(entry, now_ms);
		out->srctype = entry->srctype;
		out->srcid = entry->srcid;
		out->caid = entry->caid;
		out->score = (uint8_t)score;
		out->provider_known = entry->provider_known;
	}
	return 1;
}

/* Bounded snapshot for the existing authenticated /json and /cwlog
 * diagnostics; this function performs no I/O. Caller holds the owner lock. */
__attribute__((unused))
static int r48_source_rep_snapshot(struct r48_source_rep_table *table,
                                   uint32_t now_ms,
                                   struct r48_source_rep_snapshot *out,
                                   int max)
{
	int i, count = 0;
	if (!table || !out || max <= 0) return 0;
	for (i = 0; i < (int)R48_SOURCE_REP_SLOTS && count < max; i++) {
		struct r48_source_rep_entry *entry = &table->entry[i];
		struct r48_source_rep_snapshot *dst;
		uint32_t score;
		if (!entry->used || !entry->observed) continue;
		r48_source_rep_apply_decay(entry, now_ms);
		dst = &out[count++];
		score = (entry->score_q8 + R48_SOURCE_REP_SCORE_SCALE / 2u) /
		        R48_SOURCE_REP_SCORE_SCALE;
		if (score > 100u) score = 100u;
		dst->provid = entry->provid;
		dst->matches = entry->matches;
		dst->proofs = entry->proofs;
		dst->last_evidence_ms = entry->last_evidence_ms;
		dst->last_proof_ms = entry->last_proof_ms;
		dst->clean_after_proof = entry->clean_after_proof;
		dst->rank_delayed = (uint8_t)r48_source_rep_rank_is_delayed(entry, now_ms);
		dst->srctype = entry->srctype;
		dst->srcid = entry->srcid;
		dst->caid = entry->caid;
		dst->score = (uint8_t)score;
		dst->provider_known = entry->provider_known;
	}
	return count;
}

#endif /* MCS_R48_SOURCE_REPUTATION_H */