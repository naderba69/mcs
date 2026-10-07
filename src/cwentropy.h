/*
 * cwentropy.h -- TASK 2.2: entropy and collision forensics on a delivered key.
 *
 * WHAT THIS IS NOT
 * ----------------
 * Not a filter and not a verdict. Nothing here rejects a key, and nothing here
 * changes which key reaches a client. `acceptDCW()` still decides that, exactly
 * as before. It is not a second copy of TASK 1.8 either: 1.8 asks whether a key
 * is *shaped* like a key at all (a repeated half, a constant step, two values in
 * a half), and this file asks the two questions that shape cannot answer.
 *
 * WHAT IT IS
 * ----------
 * Two observations about how a key came to exist, both of which a real key
 * essentially cannot produce:
 *
 *   CWEN_LOWDIV  The whole 16-byte key uses at most CWEN_MIN_VALUES distinct
 *                byte values. 1.8's per-half tests look inside one half; a
 *                generator that draws from a five-byte alphabet spread evenly
 *                across both halves passes all of them.
 *
 *   CWEN_NEAR    A DIFFERENT ECM delivered a key that is within CWEN_NEAR_BITS
 *                bit flips of this one. Two independent control words differ in
 *                about 64 of their 128 bits; this is the signature of a key
 *                that was built by editing another key rather than generated.
 *
 * WHY THE SECOND ONE MATTERS
 * --------------------------
 * The public r107 changelog (infosat.org/multics) describes a cache peer that
 * forges a fake CW by XOR-ing 0xF0 into the last CW byte. That is the exact
 * shape this detects: 0xF0 sets four bits in one byte, and a forger who then
 * repairs the SUM checksum moves the checksum byte to a new random value -- an
 * expected 4 more differing bits. A genuine key differs from another genuine key
 * in ~64 bits, so the whole family of "edit a real key slightly" attacks lands
 * far below the threshold while honest traffic never comes near it. CWEN_EXACT
 * (distance 0, same key for a different ECM) is deliberately NOT this file's
 * business: TASK 1.3 already treats it as a definitive proof, and reporting it
 * twice would be double evidence for one fact. It is counted so the boundary is
 * visible in the stats, and left alone.
 *
 * FALSE POSITIVES, COUNTED RATHER THAN HOPED ABOUT
 * ------------------------------------------------
 * A test that fires on real keys is worse than no test: the source sits at the
 * floor forever and the operator learns to ignore the layer. Both probabilities
 * here are counted exactly, the same way TASK 1.8's table was:
 *
 *   P(distinct values <= 5 in 16 bytes)
 *       = SUM over j=1..5 of C(256,j) * j! * S(16,j) / 256^16      (S = Stirling 2nd kind)
 *       = 3.408e-18            i.e. about 1 key in 2.9e17
 *
 *   P(two independent keys differ in <= 16 of 128 bits)
 *       = SUM over k=0..16 of C(128,k) / 2^128
 *       = 3.190e-19            i.e. about 1 pair in 3.1e18
 *
 * For scale: a very busy cache peer pushes on the order of 10^6 keys a day, so
 * the first test needs about 780 000 years of that traffic to fire once by
 * chance, and the second about 8 million years. Neither is a rate any operator
 * will ever see, which is what makes both usable as evidence.
 *
 * ATTRIBUTION (GR1) AND WHAT EACH VERDICT IS ALLOWED TO DO
 * --------------------------------------------------------
 * A near-collision between two keys does not by itself say which of the two is
 * the forgery, so the verdict carries the direction in the mask:
 *
 *   CWEN_NEAR        the earlier key came from the SAME source  -> attributable,
 *                    scored as one soft event. A source that derives its keys
 *                    from its own previous keys is producing garbage, and the
 *                    evidence points at exactly one source.
 *   CWEN_NEAR_OTHER  the earlier key came from ANOTHER source   -> counted and
 *                    logged, NEVER scored. Either source could be echoing or
 *                    editing the other's key, and penalising the wrong one is
 *                    the failure GR1 exists to prevent. The log names both and
 *                    accuses neither.
 *
 * Neither verdict is a proof, so neither takes hard action (GR3). What upgrades
 * them is the client: if the key that came out of this was delivered and the
 * client came back too soon, TASK 2.1's ledger turns the failure into the proof
 * that re-routes the source.
 *
 * GR9: two short loops over 16 bytes, a 32-entry ring of fixed size in .bss, no
 * allocation, no I/O, no locks taken here (the caller holds the leaf lock it
 * already holds for the other evidence tables). The common case -- a clean key
 * with no near neighbour -- costs one pass over the ring and returns 0.
 *
 * SCOPING NOTE, DELIBERATE: per-source statistics across many keys (a byte
 * position that never changes on one source, a bit-balance that drifts) are NOT
 * here. They need a per-source history, they are the one class of test whose
 * false-positive rate cannot be computed from first principles, and they belong
 * with TASK 2.4's plausibility scoring, where a score is already being weighed
 * against several independent weak signals instead of being decided by one.
 */

#ifndef MCS_CWENTROPY_H
#define MCS_CWENTROPY_H

#include <stdint.h>
#include <string.h>

#define CWEN_LOWDIV     0x01   /* <= CWEN_MIN_VALUES distinct byte values      */
#define CWEN_NEAR       0x02   /* near neighbour from the SAME source          */
#define CWEN_NEAR_OTHER 0x04   /* near neighbour from a DIFFERENT source       */
#define CWEN_EXACT      0x08   /* identical key, different ECM: TASK 1.3's     */
#define CWEN_BITS       4

/*
 * Which verdicts may move a trust score. CWEN_NEAR_OTHER is excluded on GR1
 * grounds (above) and CWEN_EXACT on GR3 grounds (TASK 1.3 owns it, and it is
 * already acting as a proof). What is left is decisive and attributable: the
 * key is either impossible to generate or derived by the source that sent it.
 */
#define CWEN_SCOREMASK  (CWEN_LOWDIV | CWEN_NEAR)

#define CWEN_MIN_VALUES  5     /* <= 5 distinct values in the whole 16 bytes   */
#define CWEN_NEAR_BITS   16    /* bit flips that still count as "derived"      */
#define CWEN_SLOTS       32    /* recent deliveries remembered, .bss           */
#define CWEN_MAX_AGE     120000u   /* ms; older neighbours are a weaker claim   */

/*
 * A delivery remembered for comparison. `ecmid` is the 64-bit prefix of the
 * ECM's 128-bit MD5 -- NOT the 32-bit hash, which is a bucket index and would
 * pair unrelated ECMs (D5, the same rule TASK 1.3 and 2.1 follow). 64 bits is
 * enough for a short-lived "have I seen something very close to this" ring; the
 * authoritative identity stays with the ledger, which keeps all 128.
 */
struct cwen_entry {
	int      used;
	uint64_t ecmid;
	int      srctype;
	int      srcid;
	uint32_t ticks;
	uint8_t  key[16];
};

struct cwen_table {
	struct cwen_entry e[CWEN_SLOTS];
	uint32_t next;              /* round-robin cursor for eviction */
	unsigned long inserted;     /* deliveries recorded, for the stats line */
	unsigned long full;         /* times an entry had to be evicted */
};

/* What cwen_seen() found near this key. Only valid when the mask says so. */
struct cwen_match {
	int      srctype;
	int      srcid;
	int      bits;              /* bit distance to the neighbour      */
	uint32_t age;               /* ms since that neighbour was seen   */
	uint64_t ecmid;
	uint8_t  key[16];
};

__attribute__((unused))
static void cwen_init(struct cwen_table *t)
{
	int i;
	if (!t) return;
	memset(t, 0, sizeof(*t));
	for (i = 0; i < CWEN_SLOTS; i++) {
		t->e[i].srctype = -1;
		t->e[i].srcid = -1;
	}
}

/* Population count of a 64-bit word: the classic shift-and-mask, no builtins. */
__attribute__((unused))
static int cwen_popcount64(uint64_t v)
{
	v = v - ((v >> 1) & 0x5555555555555555ULL);
	v = (v & 0x3333333333333333ULL) + ((v >> 2) & 0x3333333333333333ULL);
	v = (v + (v >> 4)) & 0x0F0F0F0F0F0F0F0FULL;
	return (int)((v * 0x0101010101010101ULL) >> 56);
}

/* Bit distance between two keys: 0..128. Zero means identical. */
__attribute__((unused))
static int cwen_bitdiff(const uint8_t *a, const uint8_t *b)
{
	uint64_t x0, x1;
	memcpy(&x0, a, 8);
	memcpy(&x1, a + 8, 8);
	{
		uint64_t y0, y1;
		memcpy(&y0, b, 8);
		memcpy(&y1, b + 8, 8);
		return cwen_popcount64(x0 ^ y0) + cwen_popcount64(x1 ^ y1);
	}
}

/*
 * How many distinct byte values the key uses.
 *
 * O(n^2) over 16 bytes on purpose: a 256-byte bitmap would cost more to clear
 * than this costs to run, and this is on the delivery path (GR9).
 */
__attribute__((unused))
static int cwen_distinct(const uint8_t *k)
{
	int i, j, n = 0;
	for (i = 0; i < 16; i++) {
		for (j = 0; j < i; j++)
			if (k[j] == k[i]) break;
		if (j == i) n++;
	}
	return n;
}

/* A half that is entirely zero: the legitimate NDS half-key shape (see 1.8). */
__attribute__((unused))
static int cwen_half_null(const uint8_t *h)
{
	int i;
	for (i = 0; i < 8; i++) if (h[i]) return 0;
	return 1;
}

/*
 * The part of the scan that needs nothing but the key.
 *
 * The null-half guard is the same one TASK 1.8 uses and for the same reason: an
 * NDS key legitimately carries an all-zero half, and an all-zero half is one
 * distinct value. Without the guard every NDS key would be four or five values
 * away from the threshold on a good day, and a bouquet on caid 09xx would push
 * its sources to the floor for doing nothing wrong.
 */
__attribute__((unused))
static unsigned cwen_scan(const uint8_t *cw)
{
	unsigned m = 0;
	if (!cw) return 0;
	if (!cwen_half_null(cw) && !cwen_half_null(cw + 8)) {
		if (cwen_distinct(cw) <= CWEN_MIN_VALUES) m |= CWEN_LOWDIV;
	}
	return m;
}

/*
 * Look for a near neighbour among recent deliveries, remember this key, and
 * return everything the caller needs for one log line.
 *
 * Order matters: the comparison runs BEFORE this key is recorded, so a key can
 * never match itself, and a re-delivery of an identical (ecmid, key) does not
 * consume a second slot -- re-pushes are constant traffic in this codebase and
 * letting them evict the ring would blind the test that just learned to look
 * backwards.
 *
 * `ecmid == 0` means the caller has no identity for this ECM (a build without
 * CACHEEX has no ecmd5). Then the collision tests cannot tell one ECM from
 * another and must not fire at all; the entropy half still works.
 *
 * The closest neighbour wins when several are in range: it is the strongest
 * statement, and the log line should carry the strongest one it has.
 */
__attribute__((unused))
static unsigned cwen_seen(struct cwen_table *t, uint64_t ecmid, const uint8_t *cw,
                          int srctype, int srcid, uint32_t now,
                          struct cwen_match *out)
{
	unsigned m;
	int i, best = -1, besti = -1, dup = 0;

	if (out) memset(out, 0, sizeof(*out));
	if (!cw) return 0;

	m = cwen_scan(cw);

	if (t && ecmid) {
		for (i = 0; i < CWEN_SLOTS; i++) {
			struct cwen_entry *e = &t->e[i];
			int d;
			if (!e->used) continue;
			if (e->ecmid == ecmid) {
				/*
				 * Same ECM. An identical key here is a re-delivery, not a
				 * finding, and a *different* key for the same ECM is a
				 * disagreement -- TASK 2.1's ledger owns that question and
				 * owns it properly, with the client's failure as the
				 * deciding fact. Nothing to say from here.
				 */
				if (!memcmp(e->key, cw, 16)) dup = 1;
				continue;
			}
			if ((uint32_t)(now - e->ticks) > CWEN_MAX_AGE) continue;
			d = cwen_bitdiff(e->key, cw);
			if (d > CWEN_NEAR_BITS) continue;
			if (best < 0 || d < best) { best = d; besti = i; }
		}
	}

	if (besti >= 0) {
		struct cwen_entry *e = &t->e[besti];
		if (best == 0) {
			m |= CWEN_EXACT;      /* TASK 1.3's fact; counted, never logged here */
		}
		else if (e->srctype == srctype && e->srcid == srcid) {
			m |= CWEN_NEAR;
		}
		else {
			m |= CWEN_NEAR_OTHER;
		}
		if (out) {
			out->srctype = e->srctype;
			out->srcid   = e->srcid;
			out->bits    = best;
			out->age     = (uint32_t)(now - e->ticks);
			out->ecmid   = e->ecmid;
			memcpy(out->key, e->key, 16);
		}
	}

	/* Remember this delivery, unless it is the same key on the same ECM. */
	if (t && !dup) {
		int pos = -1;
		for (i = 0; i < CWEN_SLOTS; i++)
			if (!t->e[i].used) { pos = i; break; }
		if (pos < 0) {
			pos = (int)(t->next % CWEN_SLOTS);
			t->next = (uint32_t)((t->next + 1) % CWEN_SLOTS);
			t->full++;
		}
		{
			struct cwen_entry *e = &t->e[pos];
			memset(e, 0, sizeof(*e));
			e->used    = 1;
			e->ecmid   = ecmid;
			e->srctype = srctype;
			e->srcid   = srcid;
			e->ticks   = now;
			memcpy(e->key, cw, 16);
		}
		t->inserted++;
	}

	return m;
}

/* "slot" = how many recorded deliveries are a near neighbour of this key. */
__attribute__((unused))
static int cwen_neighbours(const struct cwen_table *t, uint64_t ecmid,
                           const uint8_t *cw)
{
	int i, n = 0;
	if (!t || !cw) return 0;
	for (i = 0; i < CWEN_SLOTS; i++) {
		const struct cwen_entry *e = &t->e[i];
		if (!e->used || e->ecmid == ecmid) continue;
		if (cwen_bitdiff(e->key, cw) <= CWEN_NEAR_BITS) n++;
	}
	return n;
}

/*
 * Render a mask as a short token list. `out` must hold at least 40 bytes; the
 * longest string is NEAR_OTHER+LOWDIV+EXACT+NEAR -- 33 characters plus
 * separators -- so callers pass 64.
 */
__attribute__((unused))
static void cwen_name(unsigned m, char *out, int outlen)
{
	static const char *const nm[CWEN_BITS] = { "LOWDIV", "NEAR", "NEAR_OTHER", "EXACT" };
	int i, off = 0;
	if (!out || outlen < 1) return;
	out[0] = 0;
	for (i = 0; i < CWEN_BITS; i++) {
		if (!(m & (1u << i))) continue;
		if (off > 0 && off < outlen - 1) out[off++] = '+';
		{
			const char *s = nm[i];
			while (*s && off < outlen - 1) out[off++] = *s++;
		}
	}
	out[off] = 0;
}

/* Counters the stats line and the operator read. */
extern unsigned long cwentropy_hits[CWEN_BITS];
extern unsigned long cwentropy_anomalous;
extern unsigned long cwentropy_suppressed;

#endif /* MCS_CWENTROPY_H */
