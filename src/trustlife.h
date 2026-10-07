/*
 * trustlife.h -- TASK 2.10, the trust lifecycle: evidence ages, convictions
 * do not, and both survive a restart.
 *
 * THREE FACETS, EACH WITH A STATED HALF-LIFE:
 *
 *   FADE. Trust evidence is a statement about recent behaviour, and the
 *   tables never aged: a peer condemned once stayed condemned until the LRU
 *   happened to evict it, and a peer at the ceiling kept its immunity
 *   indefinitely -- both against the doctrine written into trust.h (the
 *   floor exists so recovery stays possible, the ceiling so nobody becomes
 *   immune). The fade here is a STATELESS target computation, recomputed
 *   from (now, lastseen) on every sweep, so it cannot drift: an entry idle
 *   past the grace period relaxes toward TRUST_START -- one point per step
 *   of silence, from EITHER side. Silence is absence of evidence, not
 *   evidence of guilt or of virtue. A busy peer's events keep lastseen
 *   fresh and it never fades; a poisoner that keeps poisoning is refreshed
 *   by its own verdicts; a poisoner that goes silent is re-tested by asking
 *   it (2.8's window) and by its own arithmetic the moment it speaks again.
 *   Counters (nproof/nsoft/ngood) are history and do not fade; only the
 *   score -- the actionable part -- relaxes.
 *
 *   PERSISTENCE. Every table lives in .bss, so a restart wiped it all: the
 *   fine tier (which TASK 1.7's gate reads), the coarse tier (2.8's brake),
 *   and negative memory (2.6's "never twice" -- a restart was the cheapest
 *   way to launder a proven key). The snapshot here is a plain text file,
 *   written by the cache thread's tick OFF the ECM path (GR9) to a temp file
 *   that is renamed into place, and read back once at startup before any
 *   thread can act on a verdict. Ages re-base onto the new boot's tick
 *   counter; a snapshot older than the counter pins its entries at tick 0,
 *   which keeps evidence alive longer -- the conservative side.
 *
 *   WHAT DOES NOT FADE AND WHAT DOES NOT PERSIST ARE BOTH DELIBERATE.
 *   Negative memory has no TTL (D26): its fade list is empty, and its
 *   snapshot restores the (digest, key8, CAID, PROVID) quadruple exactly as
 *   proven -- nothing recomputed from half a key. Scores are clamped back
 *   into [FLOOR, CEILING] on parse; a line that does not validate is counted
 *   and skipped, never half-applied.
 *
 * Pure functions only -- the file I/O lives in main.c (tl_save/lifecycle_load)
 * so this header stays unit-testable without a filesystem.
 */
#ifndef MCS_TRUSTLIFE_H
#define MCS_TRUSTLIFE_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "trust.h"
#include "trustagg.h"

#define TL_MAX_AGE_S 604800u   /* a snapshot line older than 7 days is stale */

enum { TL_TF = 1, TL_TA = 2, TL_NG = 3 };

struct tl_rec {
	int      kind;
	int      srctype;
	int      srcid;
	uint16_t caid;
	uint32_t provid;
	uint16_t sid;
	int      score;
	int      nproof;
	int      nsoft;
	int      ngood;
	uint64_t kdigest;
	uint8_t  key8[8];
	uint32_t age_s;
};

/* One line per record. Returns characters written, or -1 on a bad argument. */
static inline int tl_format_rec(char *line, int sz, const struct tl_rec *r)
{
	if (!line || sz <= 0 || !r) return -1;
	switch (r->kind) {
	case TL_TF:
		return snprintf(line, (size_t)sz,
			"TF %d %d %x %x %x %d %d %d %u",
			r->srctype, r->srcid, r->caid, r->provid, r->sid,
			r->score, r->nproof, r->nsoft, r->age_s);
	case TL_TA:
		return snprintf(line, (size_t)sz,
			"TA %d %d %x %d %d %d %d %u",
			r->srctype, r->srcid, r->caid,
			r->score, r->nproof, r->nsoft, r->ngood, r->age_s);
	case TL_NG:
		return snprintf(line, (size_t)sz,
			"NG %x %x %016llx %02x%02x%02x%02x%02x%02x%02x%02x %u",
			r->caid, r->provid,
			(unsigned long long)r->kdigest,
			r->key8[0], r->key8[1], r->key8[2], r->key8[3],
			r->key8[4], r->key8[5], r->key8[6], r->key8[7],
			r->age_s);
	default:
		return -1;
	}
}

static inline int tl_hexval(int c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

/* Exactly 16 hex digits into 8 bytes; 0 on any deviation. */
static inline int tl_hex8(const char *s, uint8_t *out)
{
	int i;
	for (i = 0; i < 8; i++) {
		int hi = tl_hexval((unsigned char)s[2*i]);
		int lo = tl_hexval((unsigned char)s[2*i+1]);
		if (hi < 0 || lo < 0) return 0;
		out[i] = (uint8_t)((hi << 4) | lo);
	}
	return 1;
}

/*
 * Strict parse. Anything that does not validate is refused whole -- never
 * half-applied: wrong kind, wrong field count, score outside
 * [TRUST_FLOOR, TRUST_CEILING], a negative counter, a zero source id, a
 * zero caid, a zero digest, an age beyond TL_MAX_AGE_S. Provid may be zero
 * (CSP replies carry none); sid is accepted on TF and ignored elsewhere.
 * Returns 0 on a good line, -1 on anything else.
 */
static inline int tl_parse_line(const char *s, struct tl_rec *r)
{
	char kind[4];
	int n;

	if (!s || !r) return -1;
	memset(r, 0, sizeof(*r));

	if (sscanf(s, "%3s %n", kind, &n) != 1) return -1;

	if (!strcmp(kind, "TF")) {
		unsigned caid, provid, sid;
		if (sscanf(s + n, "%d %d %x %x %x %d %d %d %u",
		           &r->srctype, &r->srcid, &caid, &provid, &sid,
		           &r->score, &r->nproof, &r->nsoft, &r->age_s) != 9)
			return -1;
		r->kind = TL_TF;
		r->caid = (uint16_t)caid; r->provid = provid; r->sid = (uint16_t)sid;
		if (r->srctype < 1 || r->srctype > 8) return -1;
		if (r->srcid <= 0 || r->caid == 0) return -1;
		if (caid > 0xffff || sid > 0xffff) return -1;
		if (r->score < TRUST_FLOOR || r->score > TRUST_CEILING) return -1;
		if (r->nproof < 0 || r->nsoft < 0) return -1;
		if (r->age_s == 0 || r->age_s > TL_MAX_AGE_S) return -1;
		return 0;
	}
	if (!strcmp(kind, "TA")) {
		unsigned caid;
		if (sscanf(s + n, "%d %d %x %d %d %d %d %u",
		           &r->srctype, &r->srcid, &caid,
		           &r->score, &r->nproof, &r->nsoft, &r->ngood, &r->age_s) != 8)
			return -1;
		r->kind = TL_TA;
		r->caid = (uint16_t)caid;
		if (r->srctype < 1 || r->srctype > 8) return -1;
		if (r->srcid <= 0 || r->caid == 0) return -1;
		if (caid > 0xffff) return -1;
		if (r->score < TRUST_FLOOR || r->score > TRUST_CEILING) return -1;
		if (r->nproof < 0 || r->nsoft < 0 || r->ngood < 0) return -1;
		if (r->age_s == 0 || r->age_s > TL_MAX_AGE_S) return -1;
		return 0;
	}
	if (!strcmp(kind, "NG")) {
		char d[19], k[17];
		unsigned caid;
		uint64_t dig = 0;
		int i;
		if (sscanf(s + n, "%x %x %18s %16s %u",
		           &caid, &r->provid, d, k, &r->age_s) != 5)
			return -1;
		r->kind = TL_NG;
		r->caid = (uint16_t)caid;
		if (r->caid == 0 || caid > 0xffff) return -1;
		if (r->age_s == 0 || r->age_s > TL_MAX_AGE_S) return -1;
		if (strlen(d) != 16 || strlen(k) != 16) return -1;
		for (i = 0; i < 16; i++) {
			int v = tl_hexval((unsigned char)d[i]);
			if (v < 0) return -1;
			dig = (dig << 4) | (uint64_t)v;
		}
		if (dig == 0) return -1;
		if (!tl_hex8(k, r->key8)) return -1;
		r->kdigest = dig;
		return 0;
	}
	return -1;
}

/*
 * The fade, fine tier. Stateless: every sweep recomputes each entry's
 * relaxed score from (now, lastseen), so two sweeps in the same tick agree
 * and no per-entry fade bookkeeping can drift. Returns the number of
 * entries whose score actually moved.
 */
static inline int tl_fade_fine(struct trust_table *t, uint32_t now,
                               uint32_t grace_ms, uint32_t step_ms)
{
	int i, moved = 0;

	if (!t || !grace_ms || !step_ms) return 0;
	for (i = 0; i < TRUST_TABLE_SIZE; i++) {
		struct trust_entry *e = &t->e[i];
		uint32_t idle;
		int delta, points;

		if (!e->used) continue;
		idle = (uint32_t)(now - e->lastseen);
		if ((int32_t)(idle - grace_ms) < 0) continue;
		points = (int)((idle - grace_ms) / step_ms);
		if (points <= 0) continue;
		delta = e->score - TRUST_START;
		if (delta == 0) continue;
		if (delta > 0) {
			int step = (delta < points) ? delta : points;
			e->score -= step;
		} else {
			int step = (-delta < points) ? -delta : points;
			e->score += step;
		}
		moved++;
	}
	return moved;
}

/* The fade, coarse tier. Identical rule over the (source, CAID) entries. */
static inline int tl_fade_coarse(struct trustagg_table *t, uint32_t now,
                                 uint32_t grace_ms, uint32_t step_ms)
{
	int i, moved = 0;

	if (!t || !grace_ms || !step_ms) return 0;
	for (i = 0; i < TRUSTAGG_TABLE_SIZE; i++) {
		struct trustagg_entry *e = &t->e[i];
		uint32_t idle;
		int delta, points;

		if (!e->used) continue;
		idle = (uint32_t)(now - e->lastseen);
		if ((int32_t)(idle - grace_ms) < 0) continue;
		points = (int)((idle - grace_ms) / step_ms);
		if (points <= 0) continue;
		delta = e->score - TRUST_START;
		if (delta == 0) continue;
		if (delta > 0) {
			int step = (delta < points) ? delta : points;
			e->score -= step;
		} else {
			int step = (-delta < points) ? -delta : points;
			e->score += step;
		}
		moved++;
	}
	return moved;
}

#endif /* MCS_TRUSTLIFE_H */
