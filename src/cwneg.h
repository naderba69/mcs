/*
 * cwneg.h -- TASK 2.6, negative memory: a key that a definitive proof has
 * shown to be unable to open its picture is never delivered again, not even
 * to another client.
 *
 * THE HOLE THIS CLOSES
 *
 * The project already proves a key bad, in exactly two GR3-sanctioned ways:
 *
 *   - the agreement ledger (TASK 2.1): an independent source produced a
 *     different key for the same ECM and the client FAILED while holding the
 *     delivered one (LEDGER_PROOF). The key the client held did not open.
 *   - the reuse detector (TASK 1.3): the same 16 bytes offered as the answer
 *     to two different ECMs on two different services. At most one of those
 *     can be true; the bytes are poison.
 *
 * What happens to a proven key today is source-shaped, not key-shaped: the
 * purge mark (TASK 1.5) is filed against the ORIGIN for the channel, and the
 * sweep sets DCW_ERROR only on cached keys whose `cwdata->peerid` is that
 * origin. Two ways through remain, and both end in the same black screen the
 * proof was supposed to end:
 *
 *   - the same bytes arrive again from a DIFFERENT source (a relayer, or the
 *     same forger behind a second identity): the sweep never touched them,
 *     because `cwdata->peerid` is the new source;
 *   - the same bytes arrive from the same source after the entry they sat in
 *     expired: a fresh entry is a fresh cwdata, and the mark is not consulted
 *     at offer time at all.
 *
 * So the proven key can be delivered a second time -- to the same client on
 * its retry, or to another client of the same channel. That is the "twice"
 * this file makes impossible.
 *
 * WHAT THIS FILE DOES
 *
 * A fixed table of CWNEG_SLOTS slots, keyed on the KEY ITSELF: a 64-bit
 * FNV-1a digest of the 16 bytes, plus the channel (CAID, PROVID, SID) as
 * context. `cwneg_mark()` files a proof; `cwneg_hit()` answers "these bytes
 * are proven poison for this channel". The serving gate (cache_setdcw, just
 * above where the entry is looked up) asks once per offered key and refuses
 * a hit before anything is stored, counted as peer agreement, or handed to a
 * client.
 *
 * SCOPE DECISIONS, and why they are safe
 *
 *   - Scoped to (CAID, PROVID) and the key bytes, NOT to the SID and NOT to
 *     the ECM hash. Measured live during this task's own target: a poison key
 *     proven on service A and re-offered on service B of the same provider
 *     sailed past a SID-scoped memory and opened a second black screen -- the
 *     SID changes with every zap, the poison does not. Real DVB keys are
 *     per-transponder: one CW stream shared by every service of the mux, so a
 *     value that failed a client on one service is the wrong-period key for
 *     all of them. GR6 is respected the right way round here: the identity is
 *     the KEY DIGEST first -- two streams sharing a SID can never suppress
 *     each other's real keys, because that would require their live keys to
 *     be byte-identical to a value that already failed a client. The SID is
 *     kept as evidence on the line, never as a gate.
 *
 *   - No TTL. The proof does not expire; the table is bounded by LRU
 *     eviction instead (128 keys of evidence is a lot to burn through).
 *
 *   - Marks are accepted ONLY from the two definitive proofs above, at the
 *     exact places those proofs already act (the ledger's PROOF branch and
 *     the seven reuse-proof sites). Nothing here fires on a score, a shape
 *     verdict, a cycle observation or a timeout (GR3).
 *
 *   - The refusal touches no score and disables no source (GR3): the origin
 *     of the proof was already scored and purged by the task that proved it.
 *     This file only remembers the bytes.
 *
 * COST (GR9)
 *
 * Fixed .bss, no allocation, no I/O. The table is touched from several
 * threads (proofs are proven on cache, cache-ex and server threads; the gate
 * runs in the cache thread under prg.lockcache), so the caller serialises it
 * with one leaf mutex (cwneg_lock in main.c) held only across the table
 * arithmetic -- never across a log write or another lock.
 */
#ifndef MCS_CWNEG_H
#define MCS_CWNEG_H

#include <stdint.h>
#include <string.h>
#include <stdio.h>

#define CWNEG_SLOTS     128      /* proven-bad keys remembered at once          */
#define CWNEG_WINDOW    60000u   /* ms: one refusal line per key per window      */

struct cwneg_slot {
	int      used;
	uint64_t kdigest;        /* FNV-1a over the 16 key bytes                */
	uint8_t  key8[8];        /* the key's first half, for the evidence line  */
	uint16_t caid;
	uint32_t provid;
	uint16_t sid;
	uint8_t  origin_type;    /* who PROVED it (evidence, GR8)               */
	uint32_t origin_id;
	uint32_t mark_ticks;     /* the moment of the proof                     */
	uint32_t last_ticks;     /* last mark or hit, for LRU                   */
	uint32_t hits;           /* offers refused since the proof              */
	uint32_t rep_ticks;      /* last time a refusal line was written        */
	uint32_t reports;        /* refusal lines written, ever                 */
};

struct cwneg_table {
	struct cwneg_slot s[CWNEG_SLOTS];
};

/* Filled on every HIT; `offer_*` are filled by the caller (the gate knows who
 * offered; the table does not store offerers). */
struct cwneg_ev {
	unsigned fired;          /* 1 when this hit earned the line             */
	uint64_t kdigest;
	uint8_t  key8[8];        /* the key's first half, for the evidence line  */
	uint16_t caid;
	uint32_t provid;
	uint16_t sid;
	uint8_t  origin_type;
	uint32_t origin_id;
	uint32_t age_ms;         /* now - mark_ticks                            */
	uint32_t hits;           /* this refusal's number                       */
	uint32_t reports;
	int      offer_type;     /* who re-offered it (filled by the caller)    */
	uint32_t offer_id;
};

__attribute__((unused))
static void cwneg_init(struct cwneg_table *t)
{
	if (t) memset(t, 0, sizeof(*t));
}

/*
 * The key's fingerprint. FNV-1a, 64-bit, over exactly the 16 bytes -- pure
 * arithmetic, no table lookups, safe everywhere (GR9).
 */
__attribute__((unused))
static uint64_t cwneg_digest(const uint8_t *cw)
{
	uint64_t h = 1469598103934665603ULL;
	int i;

	for (i = 0; i < 16; i++) {
		h ^= (uint64_t)cw[i];
		h *= 1099511628211ULL;
	}
	return h;
}

__attribute__((unused))
static int cwneg_count(const struct cwneg_table *t)
{
	int i, n = 0;

	if (!t) return 0;
	for (i = 0; i < CWNEG_SLOTS; i++) if (t->s[i].used) n++;
	return n;
}

/*
 * File a proof. Returns 1 when the key was NOT already remembered (a fresh
 * mark -- the caller may announce it), 0 when it was (the touch is recorded,
 * the first proof stays the evidence).
 *
 * A full table evicts the least recently TOUCHED slot -- marks and refusals
 * both count as touch -- because the slot with the oldest story is the one
 * whose absence costs least, and a table that cannot evict would refuse the
 * NEXT proof, which is exactly the wrong one to forget.
 */
__attribute__((unused))
static int cwneg_mark(struct cwneg_table *t, const uint8_t *cw,
                      uint16_t caid, uint32_t provid, uint16_t sid,
                      uint8_t origin_type, uint32_t origin_id, uint32_t now)
{
	struct cwneg_slot *victim = NULL;
	uint64_t d;
	uint32_t oldest = 0;
	int i, freepos = -1;

	if (!t || !cw) return 0;
	d = cwneg_digest(cw);

	for (i = 0; i < CWNEG_SLOTS; i++) {
		struct cwneg_slot *s = &t->s[i];
		uint32_t age;

		if (!s->used) {
			if (freepos < 0) freepos = i;
			continue;
		}
		if (s->kdigest == d && s->caid == caid && s->provid == provid) {
			s->last_ticks = now;      /* re-proven: still poison */
			return 0;
		}
		age = (uint32_t)(now - s->last_ticks);
		if (age >= oldest) { oldest = age; victim = s; }
	}

	if (freepos < 0) {
		if (!victim) return 0;
		freepos = (int)(victim - t->s);
	}

	{
		struct cwneg_slot *s = &t->s[freepos];
		memset(s, 0, sizeof(*s));
		s->used        = 1;
		s->kdigest     = d;
		memcpy(s->key8, cw, 8);
		s->caid        = caid;
		s->provid      = provid;
		s->sid         = sid;
		s->origin_type = origin_type;
		s->origin_id   = origin_id;
		s->mark_ticks  = now;
		s->last_ticks  = now;
		s->hits        = 0;
	}
	return 1;
}

/*
 * Is this key proven poison for this channel? Returns 1 and fills `out`
 * (when non-NULL) on a hit; the refusal itself is the caller's decision --
 * this file never delivers anything, so it never withholds anything either.
 */
__attribute__((unused))
static int cwneg_hit(struct cwneg_table *t, const uint8_t *cw,
                     uint16_t caid, uint32_t provid, uint16_t sid,
                     uint32_t now, struct cwneg_ev *out)
{
	struct cwneg_slot *s;
	uint64_t d;
	int i;

	if (out) memset(out, 0, sizeof(*out));
	if (!t || !cw) return 0;
	d = cwneg_digest(cw);

	for (i = 0; i < CWNEG_SLOTS; i++) {
		s = &t->s[i];
		if (!s->used) continue;
		if (s->kdigest == d && s->caid == caid && s->provid == provid) {
			s->hits++;
			s->last_ticks = now;
			if (out) {
				out->fired = (!s->rep_ticks ||
				              (uint32_t)(now - s->rep_ticks) >= CWNEG_WINDOW) ? 1u : 0u;
				if (out->fired) {
					s->rep_ticks = now;
					s->reports++;
				}
				out->kdigest    = d;
				memcpy(out->key8, s->key8, 8);
				out->caid       = caid;
				out->provid     = provid;
				out->sid        = sid;
				out->origin_type = s->origin_type;
				out->origin_id   = s->origin_id;
				out->age_ms     = (uint32_t)(now - s->mark_ticks);
				out->hits       = s->hits;
				out->reports    = s->reports;
			}
			return 1;
		}
	}
	return 0;
}

/*
 * The refusal sentence. The wording states the whole chain -- proven by whom,
 * how long ago, refused how many times -- and says in words that no score
 * moved and no source was disabled, because the reader must be able to tell a
 * memory from an action (GR8).
 */
__attribute__((unused))
static int cwneg_format(const struct cwneg_ev *ev, char *out, int outlen)
{
	static const char hx[] = "0123456789ABCDEF";
	char keyhex[17];
	int i;

	if (!out || outlen <= 0) return 0;
	out[0] = 0;
	if (!ev) return 0;
	/* The stored KEY PREFIX (8 of the 16 bytes), not the digest: the line
	 * must show the operator a piece of the key that was refused, and must
	 * never dress the fingerprint up as the key itself. */
	for (i = 0; i < 8; i++) {
		keyhex[i * 2]     = hx[ev->key8[i] >> 4];
		keyhex[i * 2 + 1] = hx[ev->key8[i] & 0x0F];
	}
	keyhex[16] = 0;

	return snprintf(out, (size_t)outlen,
		"CW NEGATIVE: ch %04x:%06x:%04x -- key %s.. was proven unable to open its "
		"picture (proven on source %d/%u, %u s ago) and a cache source offered "
		"it again: refused before any client, hit #%u for this key. The refusal "
		"is proof-scoped memory, not a score: nothing was scored and no source "
		"was disabled",
		ev->caid, ev->provid, ev->sid, keyhex,
		ev->origin_type, ev->origin_id, ev->age_ms / 1000u,
		ev->hits);
}

/*
 * TASK 2.10 -- restore one conviction from the persistence file. The caller
 * supplies the digest and the 8-byte evidence prefix exactly as the file held
 * them: nothing is recomputed from half a key, so a restored conviction
 * matches precisely what the original proof convicted. Insertion mirrors
 * cwneg_mark() (free slot, else least-recently-TOUNCHED loses it); an already
 * remembered (digest, CAID, PROVID) just gets its recency touched. The age
 * re-bases onto this boot's tick counter, pinned at 0 when the snapshot is
 * older than the counter (see trust_restore): the conviction stays, which is
 * the whole point of negative memory -- it has no TTL by design (D26), and
 * a restart may not become the cheapest way to launder a proven key.
 */
__attribute__((unused))
static int cwneg_restore(struct cwneg_table *t, uint16_t caid, uint32_t provid,
                         uint64_t kdigest, const uint8_t *key8,
                         uint32_t age_ms, uint32_t now)
{
	struct cwneg_slot *victim = NULL;
	uint32_t oldest = 0;
	int i, freepos = -1;

	if (!t || !key8 || !kdigest) return 0;

	for (i = 0; i < CWNEG_SLOTS; i++) {
		struct cwneg_slot *s = &t->s[i];
		uint32_t age;

		if (!s->used) {
			if (freepos < 0) freepos = i;
			continue;
		}
		if (s->kdigest == kdigest && s->caid == caid && s->provid == provid) {
			s->last_ticks = now;   /* already remembered: touch, done */
			return 0;
		}
		age = (uint32_t)(now - s->last_ticks);
		if (age >= oldest) { oldest = age; victim = s; }
	}

	if (freepos < 0) {
		if (!victim) return 0;
		freepos = (int)(victim - t->s);
	}

	{
		struct cwneg_slot *s = &t->s[freepos];
		memset(s, 0, sizeof(*s));
		s->used       = 1;
		s->kdigest    = kdigest;
		memcpy(s->key8, key8, 8);
		s->caid       = caid;
		s->provid     = provid;
		s->mark_ticks = (now > age_ms) ? (now - age_ms) : 0;
		s->last_ticks = s->mark_ticks;
		s->hits       = 0;
	}
	return 1;
}

#endif /* MCS_CWNEG_H */
