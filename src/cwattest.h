/*
 * cwattest.h -- TASK R30 (D81), receiver attestation memory: when
 * INDEPENDENT clients, one after another, come back too soon after
 * being handed the SAME key bytes on the same channel, the bytes
 * themselves are filed -- for a bounded time -- as attested-bad.
 *
 * THE HOLE THIS CLOSES
 *
 * Negative memory (cwneg.h, TASK 2.6) files a key only on a GR3
 * definitive proof: a ledger dispute resolved by a client failure,
 * or a CW-reuse contradiction. The flood this project keeps meeting
 * in the field produces NEITHER: nine lines echo ONE manufactured
 * key, so no independent source ever disagrees, and the ledger can
 * never prove anything. The key is delivered, the client re-asks,
 * the entry survives, the next client gets the same bytes. Consensus
 * (D62/D80) slows the early delivery and the pattern gate (D79)
 * kills shaped fakes, but a random-shaped echoed fake has no shape
 * and plenty of "independent" voters.
 *
 * What DOES exist in that scenario, unused until now, is the
 * receivers themselves. One client re-asking is inference (GR3: no
 * hard action). Two or more DISTINCT clients re-asking after the
 * same bytes is independent receivers attesting the same fact --
 * one broken box cannot file a key alone, and a good key is never
 * re-asked for by several receivers at once.
 *
 * WHAT THIS FILE DOES
 *
 * A fixed table keyed like cwneg (FNV-1a digest of the 16 bytes +
 * CAID/PROVID; the SID is evidence, never a gate -- see cwneg.h for
 * the measured reason). `cwattest_note()` records one hard-retry
 * attestation from one client against the delivered bytes;
 * `cwattest_hit()` answers "these bytes are attested-bad for this
 * channel and the filing has not expired". The ingest gate
 * (cache_setdcw, beside the negative-memory gate) refuses a hit
 * exactly like a proven key -- before storage, agreement counting
 * or delivery -- and the filing also files a purge mark against the
 * recorded origin so the already-stored entry stops being served.
 *
 * SCOPE DECISIONS, and why they are safe
 *
 *   - Opt-in only (`CACHE ATTEST: <2..4>`), default OFF: until the
 *     operator arms it, nothing is counted and nothing is refused,
 *     and every decision is stock. GR3 is honoured by the shape of
 *     the evidence (independent receivers, never one client, never
 *     a timeout -- the caller only offers RETRY_HARD events where a
 *     key really was delivered), by the threshold, and by the TTL.
 *
 *   - The filing EXPIRES (default CWATTEST_TTL_MS). Proof marks in
 *     cwneg never expire because a proof cannot become false; an
 *     attestation is strong evidence, not a proof, so it must be
 *     able to heal. After the TTL the same bytes may be offered
 *     again and must earn their filing anew.
 *
 *   - Attestations must arrive inside one TTL of each other: a
 *     suspicion whose window lapses starts over. Slow-drip echoes
 *     spread over hours cannot assemble a filing.
 *
 *   - Distinctness is per client connection id as recorded at
 *     delivery time (GR1: the origin scored/key filed is the one
 *     that DELIVERED the bytes, never a later current source).
 *
 *   - Nothing here scores or disables a source. The existing trust
 *     engine keeps that job (the same RETRY_HARD already feeds it
 *     a SOFT event); this file only remembers bytes, temporarily.
 *
 * COST (GR9)
 *
 * Fixed .bss, no allocation, no I/O. Touched from client threads
 * (notes) and the cache thread (gate), so the caller serialises it
 * with one leaf mutex held only across the arithmetic.
 */
#ifndef MCS_CWATTEST_H
#define MCS_CWATTEST_H

#include <stdint.h>
#include <string.h>
#include <stdio.h>

#define CWATTEST_SLOTS        64       /* attested keys remembered at once   */
#define CWATTEST_MAX_CLIENTS  4        /* distinct attesters tracked per key */
#define CWATTEST_TTL_MS       300000u  /* filing lifetime: 5 minutes         */
#define CWATTEST_WINDOW       60000u   /* ms: one refusal line per key/window */

struct cwattest_slot {
	int      used;
	uint64_t kdigest;        /* FNV-1a over the 16 key bytes                */
	uint8_t  key8[8];        /* the key's first half, for the evidence line  */
	uint16_t caid;
	uint32_t provid;
	uint16_t sid;            /* evidence only (first attested service)       */
	uint8_t  origin_type;    /* who DELIVERED it (evidence, GR8)             */
	uint32_t origin_id;
	uint32_t clients[CWATTEST_MAX_CLIENTS]; /* distinct attester ids      */
	int      nclients;       /* distinct attesters so far                    */
	uint32_t first_ticks;    /* first attestation of this suspicion          */
	uint32_t filed_ticks;    /* when the threshold was crossed (0 = not yet) */
	uint32_t last_ticks;     /* last note or hit, for LRU                    */
	uint32_t hits;           /* offers refused since the filing              */
	uint32_t rep_ticks;      /* last time a refusal line was written         */
	uint32_t reports;        /* refusal lines written, ever                  */
};

struct cwattest_table {
	struct cwattest_slot s[CWATTEST_SLOTS];
};

/* Filled by cwattest_note(); the caller decides what the crossing earns. */
struct cwattest_ev {
	int      distinct;       /* distinct attesters after this note           */
	int      crossed;        /* 1 when THIS note crossed the threshold       */
	int      filed;          /* 1 when the key stands filed after this note  */
	uint8_t  key8[8];
	uint16_t caid;
	uint32_t provid;
	uint16_t sid;
	uint8_t  origin_type;
	uint32_t origin_id;
};

/* Filled on every HIT by cwattest_hit(). */
struct cwattest_hit_ev {
	unsigned fired;          /* 1 when this hit earned the line              */
	uint8_t  key8[8];
	uint16_t caid;
	uint32_t provid;
	uint16_t sid;
	uint8_t  origin_type;
	uint32_t origin_id;
	int      nclients;       /* attesters that filed it                      */
	uint32_t age_ms;         /* now - filed_ticks                            */
	uint32_t hits;
	uint32_t reports;
};

__attribute__((unused))
static void cwattest_init(struct cwattest_table *t)
{
	if (t) memset(t, 0, sizeof(*t));
}

/* Same fingerprint as cwneg (duplicated on purpose: the two modules
 * must be independently unit-testable, cwneg.h's own precedent). */
__attribute__((unused))
static uint64_t cwattest_digest(const uint8_t *cw)
{
	uint64_t h = 1469598103934665603ULL;
	int i;

	for (i = 0; i < 16; i++) {
		h ^= (uint64_t)cw[i];
		h *= 1099511628211ULL;
	}
	return h;
}

/* A filed slot whose filing has lapsed is no longer a filing; a
 * suspicion older than one TTL can no longer be completed. Both are
 * treated as absent by note and hit alike. */
__attribute__((unused))
static int cwattest_slot_live(const struct cwattest_slot *s, uint32_t now,
                              uint32_t ttl_ms)
{
	if (!s->used) return 0;
	if (s->filed_ticks)
		return (uint32_t)(now - s->filed_ticks) <= ttl_ms;
	return (uint32_t)(now - s->first_ticks) <= ttl_ms;
}

/*
 * Record one receiver attestation: client `clientid` re-asked too
 * soon after being delivered these bytes (the caller guarantees a
 * real delivery and a hard retry; this file classifies nothing).
 * `required` is the armed threshold of DISTINCT clients (2..4).
 *
 * Fills `ev` (when non-NULL). Returns ev.distinct, or 0 when the
 * note changed nothing (a repeat attestation from a client already
 * counted, or bad arguments).
 */
__attribute__((unused))
static int cwattest_note(struct cwattest_table *t, const uint8_t *cw,
                         uint16_t caid, uint32_t provid, uint16_t sid,
                         uint8_t origin_type, uint32_t origin_id,
                         uint32_t clientid, int required, uint32_t now,
                         uint32_t ttl_ms, struct cwattest_ev *ev)
{
	struct cwattest_slot *slot = NULL, *victim = NULL;
	uint64_t d;
	uint32_t oldest = 0;
	int i, freepos = -1, known = 0;

	if (ev) memset(ev, 0, sizeof(*ev));
	if (!t || !cw || required < 2) return 0;
	if (required > CWATTEST_MAX_CLIENTS) required = CWATTEST_MAX_CLIENTS;
	d = cwattest_digest(cw);

	for (i = 0; i < CWATTEST_SLOTS; i++) {
		struct cwattest_slot *s = &t->s[i];
		uint32_t age;

		if (!s->used) {
			if (freepos < 0) freepos = i;
			continue;
		}
		if (s->kdigest == d && s->caid == caid && s->provid == provid &&
		    cwattest_slot_live(s, now, ttl_ms)) {
			slot = s;
			break;
		}
		age = (uint32_t)(now - s->last_ticks);
		if (age >= oldest) { oldest = age; victim = s; }
	}

	if (!slot) {
		if (freepos < 0) {
			if (!victim) return 0;
			freepos = (int)(victim - t->s);
		}
		slot = &t->s[freepos];
		memset(slot, 0, sizeof(*slot));
		slot->used        = 1;
		slot->kdigest     = d;
		memcpy(slot->key8, cw, 8);
		slot->caid        = caid;
		slot->provid      = provid;
		slot->sid         = sid;
		slot->origin_type = origin_type;
		slot->origin_id   = origin_id;
		slot->first_ticks = now;
	}

	for (i = 0; i < slot->nclients; i++)
		if (slot->clients[i] == clientid) known = 1;

	if (!known && slot->nclients < CWATTEST_MAX_CLIENTS) {
		slot->clients[slot->nclients++] = clientid;
		slot->last_ticks = now;
		if (!slot->filed_ticks && slot->nclients >= required) {
			slot->filed_ticks = now;
			if (ev) ev->crossed = 1;
		}
	} else if (!known) {
		return 0; /* attester list full (required<=4 makes this unreachable) */
	}

	if (ev) {
		ev->distinct    = slot->nclients;
		ev->filed       = slot->filed_ticks ? 1 : 0;
		memcpy(ev->key8, slot->key8, 8);
		ev->caid        = slot->caid;
		ev->provid      = slot->provid;
		ev->sid         = slot->sid;
		ev->origin_type = slot->origin_type;
		ev->origin_id   = slot->origin_id;
	}
	return known ? 0 : slot->nclients;
}

/*
 * Are these bytes attested-bad for this channel, within the filing
 * lifetime? Returns 1 and fills `out` (when non-NULL) on a hit.
 */
__attribute__((unused))
static int cwattest_hit(struct cwattest_table *t, const uint8_t *cw,
                        uint16_t caid, uint32_t provid, uint16_t sid,
                        uint32_t now, uint32_t ttl_ms,
                        struct cwattest_hit_ev *out)
{
	struct cwattest_slot *s;
	uint64_t d;
	int i;

	if (out) memset(out, 0, sizeof(*out));
	if (!t || !cw) return 0;
	d = cwattest_digest(cw);

	for (i = 0; i < CWATTEST_SLOTS; i++) {
		s = &t->s[i];
		if (!s->used || !s->filed_ticks) continue;
		if (s->kdigest == d && s->caid == caid && s->provid == provid &&
		    (uint32_t)(now - s->filed_ticks) <= ttl_ms) {
			s->hits++;
			s->last_ticks = now;
			if (out) {
				out->fired = (!s->rep_ticks ||
				              (uint32_t)(now - s->rep_ticks) >= CWATTEST_WINDOW) ? 1u : 0u;
				if (out->fired) {
					s->rep_ticks = now;
					s->reports++;
				}
				memcpy(out->key8, s->key8, 8);
				out->caid       = caid;
				out->provid     = provid;
				out->sid        = sid;
				out->origin_type = s->origin_type;
				out->origin_id   = s->origin_id;
				out->nclients   = s->nclients;
				out->age_ms     = (uint32_t)(now - s->filed_ticks);
				out->hits       = s->hits;
				out->reports    = s->reports;
			}
			return 1;
		}
	}
	return 0;
}

__attribute__((unused))
static int cwattest_count(const struct cwattest_table *t, uint32_t now,
                          uint32_t ttl_ms)
{
	int i, n = 0;

	if (!t) return 0;
	for (i = 0; i < CWATTEST_SLOTS; i++)
		if (t->s[i].filed_ticks && cwattest_slot_live(&t->s[i], now, ttl_ms)) n++;
	return n;
}

/* The filing sentence (GR8): who attested, how many, against whose
 * delivery, and that the filing expires -- attestation, not proof. */
__attribute__((unused))
static int cwattest_format_filed(const struct cwattest_ev *ev, int required,
                                 uint32_t ttl_ms, char *out, int outlen)
{
	static const char hx[] = "0123456789ABCDEF";
	char keyhex[17];
	int i;

	if (!out || outlen <= 0) return 0;
	out[0] = 0;
	if (!ev) return 0;
	for (i = 0; i < 8; i++) {
		keyhex[i * 2]     = hx[ev->key8[i] >> 4];
		keyhex[i * 2 + 1] = hx[ev->key8[i] & 0x0F];
	}
	keyhex[16] = 0;

	return snprintf(out, (size_t)outlen,
		"CW ATTEST FILED: ch %04x:%06x:%04x key %s.. -- %d independent clients "
		"re-asked too soon after being handed these bytes (delivered on source "
		"%d/%u); the bytes are filed for %u s and will not be stored or served "
		"again until the filing lapses (receiver attestation at threshold %d, "
		"not a GR3 proof: no score moved, no source disabled)",
		ev->caid, ev->provid, ev->sid, keyhex, ev->distinct,
		ev->origin_type, ev->origin_id, ttl_ms / 1000u, required);
}

/* The refusal sentence, cwneg's wording discipline: attestation-grade
 * evidence, a bounded lifetime, and no source action. */
__attribute__((unused))
static int cwattest_format(const struct cwattest_hit_ev *ev, uint32_t ttl_ms,
                           char *out, int outlen)
{
	static const char hx[] = "0123456789ABCDEF";
	char keyhex[17];
	int i;

	if (!out || outlen <= 0) return 0;
	out[0] = 0;
	if (!ev) return 0;
	for (i = 0; i < 8; i++) {
		keyhex[i * 2]     = hx[ev->key8[i] >> 4];
		keyhex[i * 2 + 1] = hx[ev->key8[i] & 0x0F];
	}
	keyhex[16] = 0;

	return snprintf(out, (size_t)outlen,
		"CW ATTEST: ch %04x:%06x:%04x -- key %s.. was filed by %d independent "
		"clients' re-asks (delivered on source %d/%u, filed %u s ago, lapses "
		"at %u s) and a cache source offered it again: refused before any "
		"client, hit #%u for this key. Attestation memory only: nothing was "
		"scored and no source was disabled",
		ev->caid, ev->provid, ev->sid, keyhex, ev->nclients,
		ev->origin_type, ev->origin_id, ev->age_ms / 1000u, ttl_ms / 1000u,
		ev->hits);
}

#endif /* MCS_CWATTEST_H */
