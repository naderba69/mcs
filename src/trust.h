/*
 * trust.h — TASK 1.4, per-source trust scoring
 *
 * GR4: trust is keyed on `(source_type, source_id, CAID, PROVID, SID)`, never on
 * a server as a whole. A card that is healthy on one bouquet and dead on another
 * has to be scored as two different things, or fixing the second breaks the
 * first.
 *
 * The source half of that key is the same `(srctype, srcid)` pair that
 * `src2string()` resolves (`main.c:522-624`), so what the dashboard names and
 * what this scores are guaranteed to be the same entity (D8). `srcid` may carry
 * the PEER_* origin flags; they are part of the identity and are deliberately
 * NOT masked off here.
 *
 * Two properties the brief requires:
 *
 *   ASYMMETRIC DECAY. A failure moves the score a long way, a success moves it
 *   back a little. Detection has to be fast because a poisoned cache entry
 *   serves instantly and looks statistically ideal; recovery has to be slow
 *   because briefly distrusting a recovered source costs seconds, while trusting
 *   a lying one costs a black screen.
 *
 *   FLOOR AND CEILING. The floor exists because a source must always be able to
 *   recover — without it one bad patch of traffic disables a card permanently,
 *   which is exactly what GR3 calls worse than the problem being solved. The
 *   ceiling exists because a source must never become immune: a long run of good
 *   results must not make it undetectable the moment it starts lying.
 *
 * Fixed size, no allocation, no I/O (GR9). The table lives in .bss.
 */
#ifndef MCS_TRUST_H
#define MCS_TRUST_H

#include <stdint.h>
#include <string.h>

#define TRUST_TABLE_SIZE  256   /* x 36 bytes = 9216 bytes of .bss */

#define TRUST_FLOOR        10   /* never lower, so recovery stays possible   */
#define TRUST_CEILING     100   /* never higher, so no source becomes immune */
#define TRUST_START        60   /* a new source is believed, but not trusted */

/*
 * Where a score becomes actionable. TASK 1.5/1.6 gate on this; this engine only
 * computes it.
 */
#define TRUST_ACTIONABLE   25

/*
 * Asymmetry, as shifts so it stays integer-only.
 *
 *   SOFT   halves the distance to the floor, so 5 soft events take a fresh
 *          source from 60 to about 11. GR3 forbids acting on a *single* soft
 *          event; this makes a small handful of them decisive, which is the
 *          intended reading.
 *   PROOF  subtracts a flat 25, so a source at TRUST_START is actionable after
 *          two and at the floor after three. Definitive evidence is meant to
 *          bite immediately.
 *   GOOD   adds a thirty-second of the distance to the ceiling, so climbing from
 *          the floor back to TRUST_START takes about 24 clean results, and to
 *          the ceiling about 90. Slow on purpose.
 *
 * A consequence worth stating: the score approaches the floor asymptotically
 * under repeated soft events and never quite reaches it, while proofs reach it
 * in three. That difference is deliberate — it is the arithmetic form of GR3's
 * distinction between evidence that suggests and evidence that proves.
 */
#define TRUST_SOFT_SHIFT    1
#define TRUST_PROOF_STEP   25
#define TRUST_GOOD_SHIFT    5

/*
 * Event kinds, matching GR3's distinction.
 *
 * TRUST_EV_SOFT is a retry classification (TASK 1.2): the weakest signal
 * available, and the one most exposed to false positives, because it is inferred
 * from client behaviour rather than from the key itself.
 *
 * TRUST_EV_PROOF is definitive (TASK 1.3's CW reuse, plus the two proofs Phase 2
 * adds). GR3 allows hard action on these; this engine only moves a score.
 *
 * TRUST_EV_GOOD is a delivered control word that the client did not immediately
 * re-ask for. Without it the score could only fall, which would make every
 * source eventually actionable and the whole scheme useless.
 */
#define TRUST_EV_NONE   0   /* ask only; returns the score, writes nothing */
#define TRUST_EV_GOOD   1
#define TRUST_EV_SOFT   2
#define TRUST_EV_PROOF  3

struct trust_entry {
	uint32_t provid;
	uint32_t lastseen;
	uint16_t sid;
	uint16_t caid;
	int      srctype;
	int      srcid;      /* may carry PEER_* flags; part of the identity */
	int      score;
	int      nproof;     /* definitive proofs seen; never decayed away */
	int      nsoft;      /* soft events seen; kept for Phase 2's forensics */
	int      used;
};

struct trust_table {
	struct trust_entry e[TRUST_TABLE_SIZE];
};

/*
 * Cheap identity hash over the five-part key.
 *
 * Multiplied by odd constants and then folded, because a plain XOR would alias
 * CAID/PROVID/SID triples that differ only by transposition — and real bouquet
 * data is exactly the kind that aliases under XOR.
 */
static inline unsigned trust_index(int srctype, int srcid,
                                   uint16_t caid, uint32_t provid, uint16_t sid)
{
	uint32_t h = (uint32_t)srctype * 2654435761u;
	h ^= (uint32_t)srcid * 40503u;
	h ^= (uint32_t)caid * 2246822519u;
	h ^= provid * 3266489917u;
	h ^= (uint32_t)sid * 668265263u;
	h ^= h >> 15;
	return (unsigned)(h % TRUST_TABLE_SIZE);
}

/*
 * Find the entry for a key, or NULL. Never inserts.
 */
static inline struct trust_entry *trust_find(struct trust_table *t,
                                             int srctype, int srcid,
                                             uint16_t caid, uint32_t provid, uint16_t sid)
{
	unsigned i = trust_index(srctype, srcid, caid, provid, sid);
	unsigned probe;
	for (probe = 0; probe < TRUST_TABLE_SIZE; probe++) {
		struct trust_entry *e = &t->e[(i + probe) % TRUST_TABLE_SIZE];
		if (!e->used) return NULL;
		if (e->srctype == srctype && e->srcid == srcid &&
		    e->caid == caid && e->provid == provid && e->sid == sid) return e;
	}
	return NULL;
}

/*
 * Record one event and return the resulting score.
 *
 * TRUST_EV_NONE returns the current score without writing, which lets the
 * delivery path ask "what do we think of this source?" without paying for a
 * mutation.
 *
 * A source with no entry starts at TRUST_START, created on its first *event*
 * rather than its first sighting, so the table fills with sources that actually
 * have something to say about them.
 *
 * Eviction, when the table is full and the key is new, takes the entry longest
 * unseen. Deliberately not "lowest score": evicting the worst-scored entry would
 * make a busy server forget exactly the sources it has the most evidence
 * against, which is the opposite of what a trust table is for.
 */
static inline int trust_record(struct trust_table *t, int ev,
                               int srctype, int srcid,
                               uint16_t caid, uint32_t provid, uint16_t sid,
                               uint32_t ticks_now)
{
	struct trust_entry *e = trust_find(t, srctype, srcid, caid, provid, sid);

	if (!e) {
		unsigned i = trust_index(srctype, srcid, caid, provid, sid);
		unsigned probe, oldest = i;
		uint32_t oldest_at = t->e[i].lastseen;

		for (probe = 0; probe < TRUST_TABLE_SIZE; probe++) {
			unsigned j = (i + probe) % TRUST_TABLE_SIZE;
			if (!t->e[j].used) { oldest = j; break; }
			if ((int32_t)(t->e[j].lastseen - oldest_at) < 0) {
				oldest = j; oldest_at = t->e[j].lastseen;
			}
		}
		e = &t->e[oldest];
		memset(e, 0, sizeof(*e));
		e->srctype = srctype;
		e->srcid = srcid;
		e->caid = caid;
		e->provid = provid;
		e->sid = sid;
		e->score = TRUST_START;
		e->used = 1;
	}

	e->lastseen = ticks_now;

	if (ev == TRUST_EV_NONE) return e->score;

	if (ev == TRUST_EV_PROOF) {
		e->score -= TRUST_PROOF_STEP;
		if (e->score < TRUST_FLOOR) e->score = TRUST_FLOOR;
		e->nproof++;
	}
	else if (ev == TRUST_EV_SOFT) {
		e->score -= (e->score - TRUST_FLOOR) >> TRUST_SOFT_SHIFT;
		if (e->score < TRUST_FLOOR) e->score = TRUST_FLOOR;
		e->nsoft++;
	}
	else if (ev == TRUST_EV_GOOD) {
		/*
		 * A thirty-second of the way back to the ceiling, but never less than 1.
		 *
		 * The minimum matters. Without it the shift reaches 0 at a score of 69
		 * and the source plateaus there permanently: the ceiling becomes
		 * unreachable, a source that was penalised can never climb back to where
		 * a new one starts, and the `nproof`/`nsoft` history stops being reflected
		 * in the score at all. Verified arithmetically before the test was
		 * written — the first version of this line had no minimum and stalled.
		 */
		int step = (TRUST_CEILING - e->score) >> TRUST_GOOD_SHIFT;
		if (step < 1) step = 1;
		e->score += step;
		if (e->score > TRUST_CEILING) e->score = TRUST_CEILING;
	}

	return e->score;
}

/*
 * Ask for a score without creating an entry. Returns -1 for a source never seen,
 * which callers must treat as "unknown", not as "bad": a brand new card has done
 * nothing wrong yet and must not start distrusted.
 */
static inline int trust_score(struct trust_table *t,
                              int srctype, int srcid,
                              uint16_t caid, uint32_t provid, uint16_t sid)
{
	struct trust_entry *e = trust_find(t, srctype, srcid, caid, provid, sid);
	return e ? e->score : -1;
}

static inline void trust_reset(struct trust_table *t)
{
	memset(t, 0, sizeof(*t));
}

/*
 * TASK 2.10 -- restore one entry from the persistence file (lifecycle_load).
 * Identical insertion policy to trust_record() (free slot, else the oldest
 * loses it), but the caller supplies the state, so a restart continues the
 * story instead of starting one. The age is how long the entry had been idle
 * when the snapshot was taken; it is re-based onto this boot's tick counter.
 * A tick counter younger than the age (a fresh boot against an old snapshot)
 * pins the entry at 0 -- maximally fresh -- which is the conservative side:
 * the evidence stays alive longer, never dies sooner.
 */
__attribute__((unused))
static int trust_restore(struct trust_table *t, int srctype, int srcid,
                         uint16_t caid, uint32_t provid, uint16_t sid,
                         int score, int nproof, int nsoft,
                         uint32_t age_ms, uint32_t now)
{
	struct trust_entry *e = trust_find(t, srctype, srcid, caid, provid, sid);

	if (!e) {
		unsigned i = trust_index(srctype, srcid, caid, provid, sid);
		unsigned probe, oldest = i;
		uint32_t oldest_at = t->e[i].lastseen;

		for (probe = 0; probe < TRUST_TABLE_SIZE; probe++) {
			unsigned j = (i + probe) % TRUST_TABLE_SIZE;
			if (!t->e[j].used) { oldest = j; break; }
			if ((int32_t)(t->e[j].lastseen - oldest_at) < 0) {
				oldest = j; oldest_at = t->e[j].lastseen;
			}
		}
		e = &t->e[oldest];
		memset(e, 0, sizeof(*e));
		e->srctype = srctype;
		e->srcid = srcid;
		e->caid = caid;
		e->provid = provid;
		e->sid = sid;
		e->used = 1;
	}

	if (score < TRUST_FLOOR)   score = TRUST_FLOOR;
	if (score > TRUST_CEILING) score = TRUST_CEILING;
	e->score    = score;
	e->nproof   = nproof;
	e->nsoft    = nsoft;
	e->lastseen = (now > age_ms) ? (now - age_ms) : 0;
	return 1;
}

#endif /* MCS_TRUST_H */
