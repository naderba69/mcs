/*
 * ledger.h -- TASK 2.1: the agreement ledger.
 *
 * WHAT IT IS
 *   A bounded table of "who said what for this ECM". When two independent
 *   sources answer for the same ECM, the ledger compares their control words.
 *   Identical means they agree -- corroboration, worth counting. Different
 *   means they disagree, and exactly one of them is wrong.
 *
 *   On its own a disagreement proves nothing: a peer that hashes a different
 *   byte range, or a provider whose second feed sits on a different key ladder,
 *   could account for it. So the ledger does not act on a disagreement. It waits
 *   for the third fact -- the client came back for the same ECM far sooner than
 *   its crypto period -- and only then does the pair mean something:
 *
 *     the key that was DELIVERED failed the client,
 *     an INDEPENDENT source produced a DIFFERENT key for the same ECM,
 *     therefore the delivered key is the wrong one, and the source that
 *     delivered it is proven wrong by evidence outside itself.
 *
 *   That is GR3's third definitive proof, nearly verbatim: "agreement mismatch
 *   with client failure". It is the only reason this table exists.
 *
 * WHY IT IS NOT JUST THE RETRY COUNTER
 *   TASK 1.2 observes that a client re-asked. That is inference, and it can be
 *   wrong in a way that costs a good source: the client may have re-asked
 *   because a dish moved, because the viewer zapped, because the network
 *   hiccuped. A single client re-asking has no second opinion anywhere.
 *   The ledger supplies the second opinion. It is the difference between
 *   "something went wrong after this key" and "this key was wrong".
 *
 * WHY THE ACCUSATION IS ALWAYS THE DELIVERED SOURCE, NEVER THE DISSENTER
 *   A disagreement says one of two keys is bad, not which. The delivery clock
 *   says which: the key in use when the client failed is the delivered one. So
 *   the accusation always lands on the source whose key the client actually
 *   received -- never on the source that merely disagreed. GR1, from the other
 *   side: a source must not be penalised for someone else's key. The dissenting
 *   source is neither penalised nor promoted; its key is unverified, which is
 *   the truth.
 *
 * IDENTITY -- GR6, AND D5's RULE
 *   Keyed on the 128-bit ECM MD5 (`ECM_DATA.ecmd5`, computed at ecmdata.c:264
 *   over the same byte range the 32-bit hash covers). Never the 32-bit hash
 *   alone: that is a bucket index, and two ECMs landing in one bucket would
 *   pair two unrelated sources and manufacture a dispute out of a collision.
 *   A reused control word is proof (TASK 1.3); a collided hash is a
 *   coincidence, and D5 already refused to build on it.
 *   With CACHEEX compiled out there is no ecmd5, so there is no ledger: the
 *   offer functions return immediately and every caller behaves exactly as it
 *   did before this file existed.
 *
 * SAME SOURCE, TWO KEYS IS NOT AN AGREEMENT MISMATCH
 *   A source that contradicts itself is suspicious, but there is no independent
 *   second opinion in it, which is what GR3 names. Those events are counted
 *   (`nself`) and reported at diagnostic level; they never produce a proof.
 *   Strictness here is free -- the case is rare.
 *
 * PROOF LATCH
 *   One entry produces one proof. The same two keys for the same ECM cannot
 *   yield a second, different conclusion, and a client failing repeatedly on
 *   them is not new evidence -- it is the same fact re-observed. Without the
 *   latch the log would fill with the same accusation every retry. Both keys
 *   failing the client is possible; the first proof wins and the second is
 *   counted but not re-accused, because the accused source is already avoided.
 *
 * BOUNDED, ALLOCATION-FREE, LOCK-FREE INTERNALLY (GR9)
 *   LEDGER_TABLE_SIZE fixed entries in .bss, evicted longest-unseen, with an
 *   age-out of LEDGER_MAX_AGE_MS. No malloc, no I/O, no locks taken here: the
 *   caller holds the same leaf mutex the rest of the trust evidence lives under.
 *   Every function is a pure function of its arguments and the table, so the
 *   unit suite exercises it with no server, no threads and no clock.
 *
 * WHAT IT DELIBERATELY DOES NOT DO
 *   It does not decide what a proof costs. It hands the caller the accused
 *   source's identity and the two keys; the caller (main.c) records the proof,
 *   re-routes with no tolerance, and writes one log line. Keeping the verdict
 *   and the sentence apart is what lets the unit suite test the verdict with no
 *   trust engine in sight.
 */

#ifndef MCS_LEDGER_H
#define MCS_LEDGER_H

#include <string.h>
#include <stdio.h>
#include <stdint.h>   /* the test suite includes this header alone */

/*
 * Table depth. 32 entries: two sources must answer for the same ECM inside
 * LEDGER_MAX_AGE_MS for anything to happen at all, so the live population is
 * tiny -- it holds *disputed* ECMs, not all ECMs. An entry is ~156 bytes after
 * alignment, so the table is about 5 KB of .bss.
 *
 * This is a deliberate trade, not an oversight: 32 entries is a short window on
 * a busy server, so a dispute that takes longer than that to complete is
 * missed. What it reliably catches is the case that matters -- a poisoned cache
 * entry or a bad peer answering while a client is already re-asking, which all
 * lands inside a couple of seconds. A single constant, so TASK 3.5's simulators
 * can raise it once they show what a real peer population costs.
 */
#define LEDGER_TABLE_SIZE 32

/*
 * How long a disagreement stays meaningful. Two crypto periods, the same window
 * cw_reuse.h uses, because both answer the same question: is this the key in use
 * right now? A client re-ask lands 1-3 s after delivery and the contradicting
 * key usually arrives during that same retry, so 20 s is generous without
 * letting a stale pairing accuse anybody.
 */
#define LEDGER_MAX_AGE_MS 20000u

/* What one event was. */
#define LEDGER_NONE        0  /* first sighting of this ECM: nothing to compare */
#define LEDGER_AGREE       1  /* matched a key already recorded for this ECM    */
#define LEDGER_DISPUTE     2  /* contradicted it, from a DIFFERENT source       */
#define LEDGER_SELFDISPUTE 3  /* contradicted it, from the SAME source          */
#define LEDGER_PROOF       4  /* dispute + the client failed after the key      */

/* Which slot the accused key sits in. */
#define LEDGER_SLOT_FIRST 0
#define LEDGER_SLOT_OTHER 1

struct ledger_slot {
	uint8_t  cw[16];
	uint32_t at;         /* when this key was offered, for the elapsed time */
	int      srctype;
	int      srcid;
	uint8_t  delivered;  /* the caller said this key was applied to a client */
	uint8_t  failed;     /* ... and came back for the same ECM too soon      */
};

struct ledger_entry {
	uint8_t  ecmd5[16];
	uint32_t hash;       /* the 32-bit hash as well, for the log line only   */
	uint32_t provid;
	uint16_t caid;
	uint16_t sid;
	uint32_t seen_at;    /* last event on this entry, for aging and eviction */
	uint32_t failed_at;  /* when the client failed, for the log line         */
	struct ledger_slot s[2];
	unsigned short nagree;
	unsigned short ndispute;
	unsigned short nself;    /* same-source contradictions, never a proof    */
	uint8_t  nslot;          /* 0, 1 or 2                                    */
	uint8_t  proof_done;     /* latch: this entry has already had its say    */
	uint8_t  used;
};

struct ledger_table {
	struct ledger_entry e[LEDGER_TABLE_SIZE];
};

/*
 * The verdict of one offer, returned by value: a stack struct, no allocation,
 * and everything the caller needs to write the log line without reaching back
 * into the table after it has released the lock.
 */
struct ledger_event {
	int      what;              /* LEDGER_*                               */
	uint8_t  ecmd5[16];
	uint32_t hash;
	uint32_t provid;
	uint16_t caid;
	uint16_t sid;
	uint8_t  cw[2][16];         /* slot 0 is always the first key recorded */
	int      src[2][2];         /* [slot][0]=srctype, [slot][1]=srcid      */
	int      acc_slot;          /* filled on PROOF: which slot is accused  */
	uint32_t elapsed_ms;        /* filled on PROOF: delivery -> failure    */
	int      nagree, ndispute, nself;
};

/*
 * Is a 128-bit identity real?
 *
 * Deliberately a local copy of the test checkECMD5() (ecmdata.c:72) makes,
 * rather than a call to it: this header stays a pure function of its arguments
 * so the tests can link it alone, exactly as dcwstruct.h does with the trust
 * engine. An all-zero ecmd5 is what an unpopulated field looks like, and
 * treating one as an identity would pair every unpopulated ECM with every
 * other one.
 */
static inline int ledger_key_valid(const uint8_t *ecmd5)
{
	int i;
	if (!ecmd5) return 0;
	for (i = 0; i < 16; i++)
		if (ecmd5[i]) return 1;
	return 0;
}

/*
 * Cheap index from the key itself. Deliberately not a strong hash: it only
 * picks a probe start, and a poor choice costs one extra comparison. Taking the
 * first four bytes as a little-endian word avoids any alignment assumption,
 * which matters because the server is built with -fpack-struct.
 */
static inline unsigned ledger_index(const uint8_t *ecmd5)
{
	return (unsigned)((ecmd5[0] | (ecmd5[1] << 8) | (ecmd5[2] << 16) |
	                   ((unsigned)ecmd5[3] << 24)) % LEDGER_TABLE_SIZE);
}

__attribute__((unused))
static void ledger_init(struct ledger_table *t)
{
	int i;
	if (!t) return;
	memset(t, 0, sizeof(*t));
	for (i = 0; i < LEDGER_TABLE_SIZE; i++) {
		t->e[i].s[0].srctype = -1;
		t->e[i].s[1].srctype = -1;
	}
}

/*
 * Find the entry for an ECM, or NULL. Never inserts, never writes -- aging is
 * only *observed* here (a stale entry is skipped, so it can never be paired),
 * and actually reclaimed by ledger_slot_for().
 */
static inline struct ledger_entry *ledger_find(struct ledger_table *t,
                                               const uint8_t *ecmd5,
                                               uint32_t ticks_now)
{
	unsigned i, probe;

	if (!t || !ledger_key_valid(ecmd5)) return 0;

	i = ledger_index(ecmd5);
	for (probe = 0; probe < LEDGER_TABLE_SIZE; probe++) {
		struct ledger_entry *e = &t->e[(i + probe) % LEDGER_TABLE_SIZE];
		if (!e->used) continue;
		if ((ticks_now - e->seen_at) > LEDGER_MAX_AGE_MS) continue;
		if (memcmp(e->ecmd5, ecmd5, 16) == 0) return e;
	}
	return 0;
}

/*
 * Find the entry for a new event, inserting one if needed.
 *
 * A free slot in the probe sequence always wins over evicting a live entry: the
 * table exists to remember disagreements, and recycling a live one to save a
 * probe step would forget evidence the operator has not been told about yet.
 * Only when the whole sequence is live does the oldest entry go -- the policy
 * trust.h and cache_purge.h share, for the same reason.
 */
static inline struct ledger_entry *ledger_slot_for(struct ledger_table *t,
                                                   const uint8_t *ecmd5,
                                                   uint32_t ticks_now)
{
	unsigned i, probe;
	struct ledger_entry *victim = 0, *free_slot = 0;
	uint32_t oldest_age = 0;

	if (!t || !ledger_key_valid(ecmd5)) return 0;

	i = ledger_index(ecmd5);
	for (probe = 0; probe < LEDGER_TABLE_SIZE; probe++) {
		struct ledger_entry *e = &t->e[(i + probe) % LEDGER_TABLE_SIZE];
		uint32_t age;

		if (e->used) {
			if ((ticks_now - e->seen_at) > LEDGER_MAX_AGE_MS) {
				e->used = 0;    /* aged out: reusable like any free slot */
			} else {
				if (memcmp(e->ecmd5, ecmd5, 16) == 0) return e;
				age = ticks_now - e->seen_at;
				if (age > oldest_age) { oldest_age = age; victim = e; }
				continue;
			}
		}
		if (!free_slot) free_slot = e;
	}

	if (!free_slot && !victim) return 0;      /* impossible: table is full   */
	victim = free_slot ? free_slot : victim;

	/*
	 * Fresh entry. A recycled one is cleared completely, latch included: a
	 * proof against a different ECM is a different fact, and inheriting
	 * another ECM's latch would silently suppress it.
	 */
	memset(victim, 0, sizeof(*victim));
	victim->s[0].srctype = -1;
	victim->s[1].srctype = -1;
	memcpy(victim->ecmd5, ecmd5, 16);
	victim->used = 1;
	victim->seen_at = ticks_now;
	return victim;
}

/*
 * Copy an entry into the caller's event struct, so the caller can log after it
 * has released the lock and without touching the table again.
 */
static inline void ledger_snapshot(const struct ledger_entry *e,
                                   struct ledger_event *ev)
{
	int i, j;

	ev->nagree = e->nagree;
	ev->ndispute = e->ndispute;
	ev->nself = e->nself;
	for (i = 0; i < 16; i++) ev->ecmd5[i] = e->ecmd5[i];
	ev->hash = e->hash;
	ev->provid = e->provid;
	ev->caid = e->caid;
	ev->sid = e->sid;
	for (i = 0; i < 2; i++) {
		for (j = 0; j < 16; j++) ev->cw[i][j] = e->s[i].cw[j];
		ev->src[i][0] = e->s[i].srctype;
		ev->src[i][1] = e->s[i].srcid;
	}
}

/*
 * Offer one (key, source) pair for one ECM.
 *
 * Called wherever a source hands this server a control word: the two delivery
 * points in setdcw.c and the cache reply path. A key that is about to be
 * *discarded* still counts -- a source whose keys are always rejected has never
 * delivered anything, and GR2 keeps it out of the retry accounting, but its
 * disagreement with a delivered key is exactly the evidence this table is for.
 *
 * `applied` means the caller is on the path that hands this key to a client --
 * it is the delivery hook, above the acceptance test, so a key that is about to
 * be refused is recorded too. It is recorded, not trusted: the accusation itself
 * is gated by ledger_failed() matching the key the client actually received
 * (`lastecm.dcw`), so a key that never reached anybody cannot be accused no
 * matter what this flag says.
 *
 * Returns the LEDGER_* verdict. On LEDGER_PROOF the caller must act; the latch
 * means it will be told once per entry, not once per key.
 */
__attribute__((unused))
static int ledger_offer(struct ledger_table *t,
                        const uint8_t *ecmd5, uint32_t hash,
                        uint16_t caid, uint32_t provid, uint16_t sid,
                        const uint8_t *cw, int srctype, int srcid,
                        int applied, uint32_t ticks_now,
                        struct ledger_event *ev)
{
	struct ledger_entry *e;
	int i;

	memset(ev, 0, sizeof(*ev));
	ev->what = LEDGER_NONE;

	if (!t || !ledger_key_valid(ecmd5) || !cw) return LEDGER_NONE;

	e = ledger_slot_for(t, ecmd5, ticks_now);
	if (!e) return LEDGER_NONE;

	e->seen_at = ticks_now;
	e->hash = hash;
	e->caid = caid;
	e->sid = sid;
	e->provid = provid;

	/* Same key already recorded for this ECM? */
	for (i = 0; i < e->nslot; i++) {
		if (memcmp(e->s[i].cw, cw, 16) != 0) continue;
		/*
		 * From the same source or a different one, an identical key is
		 * agreement -- two independent sources agreeing is corroboration,
		 * and a later client failure cannot accuse either of them. The
		 * delivery flag is sticky: a key that reached a client stays so.
		 */
		e->nagree++;
		if (applied) e->s[i].delivered = 1;
		ledger_snapshot(e, ev);
		ev->what = LEDGER_AGREE;
		return LEDGER_AGREE;
	}

	if (e->nslot == 0) {
		e->s[0].srctype = srctype;
		e->s[0].srcid = srcid;
		e->s[0].at = ticks_now;
		memcpy(e->s[0].cw, cw, 16);
		e->s[0].delivered = (uint8_t)(applied ? 1 : 0);
		e->nslot = 1;
		ledger_snapshot(e, ev);
		ev->what = LEDGER_NONE;
		return LEDGER_NONE;
	}

	/* nslot == 1 and a different key: a dispute, or a self-dispute. */
	if (e->s[0].srctype == srctype && e->s[0].srcid == srcid) {
		e->nself++;
		ledger_snapshot(e, ev);
		ev->what = LEDGER_SELFDISPUTE;
		return LEDGER_SELFDISPUTE;
	}

	e->s[1].srctype = srctype;
	e->s[1].srcid = srcid;
	e->s[1].at = ticks_now;
	memcpy(e->s[1].cw, cw, 16);
	e->s[1].delivered = (uint8_t)(applied ? 1 : 0);
	e->nslot = 2;
	e->ndispute++;
	ledger_snapshot(e, ev);
	ev->what = LEDGER_DISPUTE;

	/*
	 * The failure may already be on record -- and it usually is, because the
	 * client's re-ask is what provoked this second key in the first place.
	 * This is the common path, and it is the whole point of the ledger: the
	 * proof completes here, when the contradicting key arrives.
	 */
	for (i = 0; i < 2; i++) {
		if (!e->s[i].failed) continue;
		if (e->proof_done) return ev->what;    /* same facts, already said */
		e->proof_done = 1;
		ledger_snapshot(e, ev);
		ev->acc_slot = i;
		ev->elapsed_ms = e->failed_at - e->s[i].at;
		ev->what = LEDGER_PROOF;
		return LEDGER_PROOF;
	}
	return LEDGER_DISPUTE;
}

/*
 * Record that a client failed after a particular key was delivered.
 *
 * `cw` is the key the client actually received (`lastecm.dcw` at the retry
 * point) and `srctype/srcid` its source. If the entry already holds a key from
 * an independent source that differs from this one, the proof is complete now;
 * otherwise the failure is remembered and a later contradicting key completes
 * it (see ledger_offer). Both arrival orders happen in the field, so both are
 * supported rather than one being assumed.
 *
 * Returns LEDGER_PROOF at most once per entry, and only when the delivered key
 * and the contradicting key came from *different* sources. A failure with no
 * second opinion stays a failure with no second opinion: that is TASK 1.2's
 * soft signal, and this function deliberately does not upgrade it.
 */
__attribute__((unused))
static int ledger_failed(struct ledger_table *t,
                         const uint8_t *ecmd5, uint32_t hash,
                         uint16_t caid, uint32_t provid, uint16_t sid,
                         const uint8_t *cw, int srctype, int srcid,
                         uint32_t ticks_now,
                         struct ledger_event *ev)
{
	struct ledger_entry *e;
	int i, slot = -1;

	memset(ev, 0, sizeof(*ev));
	ev->what = LEDGER_NONE;

	if (!t || !ledger_key_valid(ecmd5) || !cw) return LEDGER_NONE;

	e = ledger_slot_for(t, ecmd5, ticks_now);
	if (!e) return LEDGER_NONE;

	e->seen_at = ticks_now;
	e->hash = hash;
	e->caid = caid;
	e->sid = sid;
	e->provid = provid;

	/*
	 * Which recorded key did the client fail on? The delivered one, matched on
	 * the key itself. Falling back to the source alone would let a source be
	 * accused for a key it never sent.
	 */
	for (i = 0; i < e->nslot; i++)
		if (memcmp(e->s[i].cw, cw, 16) == 0) { slot = i; break; }

	if (slot < 0) {
		/*
		 * The key the client failed on is not in the ledger: the entry was
		 * evicted, aged out, or the peer hashes over a different byte range.
		 * Negative, never a guess -- no accusation is possible without the
		 * key in hand, and a wrong accusation costs a good source (GR3).
		 */
		ledger_snapshot(e, ev);
		ev->what = LEDGER_NONE;
		return LEDGER_NONE;
	}

	e->s[slot].failed = 1;
	e->s[slot].delivered = 1;
	e->failed_at = ticks_now;
	(void)srctype; (void)srcid;   /* the key identifies the slot, not the name */

	/*
	 * The other slot holds a different key only when it is a real dispute --
	 * a self-dispute never fills slot 1. Independence is re-checked here
	 * rather than trusted from the offer, because this is the function whose
	 * verdict is acted on.
	 */
	if (e->nslot == 2 && !e->proof_done) {
		int o = (slot == LEDGER_SLOT_FIRST) ? LEDGER_SLOT_OTHER : LEDGER_SLOT_FIRST;
		int independent = !(e->s[o].srctype == e->s[slot].srctype &&
		                    e->s[o].srcid == e->s[slot].srcid);
		if (independent) {
			e->proof_done = 1;
			ledger_snapshot(e, ev);
			ev->acc_slot = slot;
			ev->elapsed_ms = e->failed_at - e->s[slot].at;
			ev->what = LEDGER_PROOF;
			return LEDGER_PROOF;
		}
	}

	ledger_snapshot(e, ev);
	ev->what = LEDGER_NONE;
	return LEDGER_NONE;
}

/*
 * Render the proof line. Pure, so the sentence an operator reads is tested with
 * no server running -- the same split statsline.h uses.
 *
 * Returns the number of characters written (excluding the terminator), or -1 on
 * a bad argument. Never writes past `outlen` and always terminates when
 * outlen > 0. The full line is at most about 250 characters, so callers pass a
 * 320-byte buffer; a shorter one truncates rather than overflowing, because a
 * diagnostic string is not worth a memory-safety bug.
 *
 * Both keys are printed as their first eight bytes, the convention the TASK 1.8
 * line already uses: enough to identify the key in a log, short enough to read.
 *
 * The line ends at the proof, deliberately not at the punishment: whether the
 * source was actually avoided is reported by the rotation line, which knows
 * whether ROTATION_MAX_AVOID capped it. The evidence line must not claim an
 * action that did not happen.
 */
static inline int ledger_format(const struct ledger_event *ev,
                                char *out, int outlen)
{
	int a, o;

	if (!ev || !out || outlen <= 0) return -1;
	if (ev->what != LEDGER_PROOF) { out[0] = 0; return -1; }

	a = ev->acc_slot;
	o = a ^ 1;

	return snprintf(out, (size_t)outlen,
		" !!! AGREEMENT MISMATCH: ch %04x:%06x:%04x ecm %02x%02x%02x%02x"
		" | source %d/%d key %02x%02x%02x%02x%02x%02x%02x%02x was delivered"
		" and the client came back after %u ms"
		" | independent source %d/%d offered %02x%02x%02x%02x%02x%02x%02x%02x"
		" for the same ecm"
		" | proof recorded against %d/%d",
		(unsigned)ev->caid, (unsigned)ev->provid, (unsigned)ev->sid,
		ev->ecmd5[0], ev->ecmd5[1], ev->ecmd5[2], ev->ecmd5[3],
		ev->src[a][0], ev->src[a][1],
		ev->cw[a][0], ev->cw[a][1], ev->cw[a][2], ev->cw[a][3],
		ev->cw[a][4], ev->cw[a][5], ev->cw[a][6], ev->cw[a][7],
		(unsigned)ev->elapsed_ms,
		ev->src[o][0], ev->src[o][1],
		ev->cw[o][0], ev->cw[o][1], ev->cw[o][2], ev->cw[o][3],
		ev->cw[o][4], ev->cw[o][5], ev->cw[o][6], ev->cw[o][7],
		ev->src[a][0], ev->src[a][1]);
}

#endif /* MCS_LEDGER_H */
