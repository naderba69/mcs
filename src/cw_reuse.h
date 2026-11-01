/*
 * cw_reuse.h — TASK 1.3, definitive CW-reuse detection
 *
 * A control word is the output of a decryption keyed on a specific ECM. The same
 * 16 bytes appearing for two *different* ECMs on two *different* services within
 * seconds can be evidence of a source handing out keys from a small pool. This
 * is one of the three proofs GR3 treats as definitive, so unlike everything else
 * in Phase 1 this one is allowed to trigger hard action.
 *
 * Which is exactly why the false-positive analysis matters more than the
 * detection:
 *
 *   - The same service legitimately presents several distinct ECMs — Irdeto
 *     multi-CHID, different PIDs — and they all share one CW inside a crypto
 *     period. Keying on SID alone would call that reuse. GR6.
 *   - CAID defines the namespace for PROVID. A numeric provider value (including
 *     zero when explicitly present) is not comparable across CAIDs, and an
 *     absent provider is not the same identity as an explicit zero.
 *
 * So this flags only when the CAID and provider scope match, the ECM differs,
 * and the SID differs. Same SID is never flagged, whatever else changed. That
 * deliberately under-detects — a poisoned source attacking two services that
 * happen to share an SID escapes — because a false positive here disables a good
 * card, and GR3 is explicit that this is worse than the black screen being fixed.
 *
 * Keyed on `ecmd5` (128-bit, `ecmdata.h:63` and `clustredcache.c:320`), never on
 * `hash`, which is a 32-bit bucket index (`clustredcache.c:314`) and collides.
 * If CACHEEX is off, ecmd5 is not populated, and the caller must pass
 * `ecmd5_valid == 0` — the detector then returns REUSE_NONE unconditionally
 * rather than silently falling back to the 32-bit hash (D5).
 *
 * Fixed size, no allocation, no I/O (GR9). The table lives in .bss.
 */
#ifndef MCS_CW_REUSE_H
#define MCS_CW_REUSE_H

#include <stdint.h>
#include <string.h>

/*
 * Table depth. 64 entries. The scoped entry is 46 bytes with the server's
 * -fpack-struct build (52 bytes with ordinary alignment), so the table uses
 * 2944 bytes of .bss in the server.
 *
 * This is a deliberate trade, not an oversight: on a busy server 64 entries is
 * a short window, so a source that spreads bad keys thinly will be missed. What
 * this reliably catches is a source cycling a small pool of keys, which is the
 * common fake-card behaviour and the case that produces sustained black screens.
 * The depth is a single constant so it can be raised once TASK 3.5's simulators
 * show what it costs.
 */
#define CWREUSE_TABLE_SIZE 64

/* How long an entry stays meaningful. CWs rotate roughly every 10 s. */
#define CWREUSE_MAX_AGE_MS 20000u

#define CWREUSE_NONE  0   /* nothing conclusive */
#define CWREUSE_PROOF 1   /* same scoped CW, different ECM and SID: definitive */

struct cwreuse_entry {
	uint8_t  cw[16];
	uint8_t  ecmd5[16];
	uint32_t provid;
	uint8_t  provider_known;
	uint32_t seen_at;
	uint16_t sid;
	uint16_t caid;
	uint8_t  used;
};

struct cwreuse_table {
	struct cwreuse_entry e[CWREUSE_TABLE_SIZE];
};

/*
 * Cheap index from the control word itself.
 *
 * Deliberately not a strong hash: this only picks a probe start, and a poor
 * choice costs one extra comparison. Taking four bytes as a little-endian word
 * avoids any alignment assumption, which matters because the server is built
 * with -fpack-struct.
 */
static inline unsigned cwreuse_index(const uint8_t cw[16])
{
	return (unsigned)((cw[0] | (cw[1] << 8) | (cw[2] << 16) | ((unsigned)cw[3] << 24))
	                  % CWREUSE_TABLE_SIZE);
}

/*
 * Offer one (CW, ECM) pair to the detector with an explicit provider-presence
 * bit. Nonzero legacy provider values are necessarily known; when unknown, the
 * numeric value is discarded so it cannot leak into another identity.
 *
 * A matching scoped sighting produces a proof only when both the ECM and SID
 * differ and the earlier sighting is still fresh. A same CW in another CAID or
 * provider scope is stored independently and is not evidence against this one.
 */
static inline int cwreuse_offer_scoped(struct cwreuse_table *t,
                                       const uint8_t cw[16],
                                       const uint8_t ecmd5[16],
                                       int ecmd5_valid,
                                       uint16_t caid, uint16_t sid,
                                       uint32_t provid, int provider_known,
                                       uint32_t ticks_now)
{
	unsigned i, probe;
	struct cwreuse_entry *free_slot = NULL, *oldest = NULL;
	uint32_t oldest_age = 0;

	if (!t || !cw || !ecmd5_valid || !ecmd5) return CWREUSE_NONE;
	provider_known = !!provider_known || provid != 0;
	if (!provider_known) provid = 0;
	i = cwreuse_index(cw);

	for (probe = 0; probe < CWREUSE_TABLE_SIZE; probe++) {
		struct cwreuse_entry *e = &t->e[(i + probe) % CWREUSE_TABLE_SIZE];
		uint32_t age;

		if (!e->used) {
			if (!free_slot) free_slot = e;
			continue;
		}

		/* Age out anything older than a couple of crypto periods. */
		age = (uint32_t)(ticks_now - e->seen_at);
		if (age > CWREUSE_MAX_AGE_MS) {
			e->used = 0;
			if (!free_slot) free_slot = e;
			continue;
		}
		if (!oldest || age > oldest_age) {
			oldest = e;
			oldest_age = age;
		}

		if (memcmp(e->cw, cw, 16) != 0) continue;

		/* Same bytes, but not the same CAID/provider identity: keep the
		 * observation separate instead of turning a cross-provider repeat
		 * into a hard proof. */
		if (e->caid != caid || e->provider_known != (uint8_t)provider_known ||
		    (provider_known && e->provid != provid)) continue;

		/* Same control word in this identity. Reuse only if it is a
		 * different ECM on a different service. */
		if (memcmp(e->ecmd5, ecmd5, 16) == 0) return CWREUSE_NONE;
		if (e->sid == sid) return CWREUSE_NONE;

		e->seen_at = ticks_now;
		return CWREUSE_PROOF;
	}

	/* A free or aged slot always beats eviction. If the table is full, evict
	 * the oldest live sighting so one busy key cannot pin the whole table. */
	if (!free_slot) free_slot = oldest;
	if (!free_slot) return CWREUSE_NONE;

	memset(free_slot, 0, sizeof(*free_slot));
	memcpy(free_slot->cw, cw, 16);
	memcpy(free_slot->ecmd5, ecmd5, 16);
	free_slot->caid = caid;
	free_slot->sid = sid;
	free_slot->provid = provid;
	free_slot->provider_known = (uint8_t)provider_known;
	free_slot->seen_at = ticks_now;
	free_slot->used = 1;
	return CWREUSE_NONE;
}

/*
 * Compatibility entry point for callers that have no provider-presence bit.
 * A zero provider is treated as unknown; code that knows the wire/API carried
 * an explicit zero must call cwreuse_offer_scoped() instead.
 */
static inline int cwreuse_offer(struct cwreuse_table *t,
                                const uint8_t cw[16],
                                const uint8_t ecmd5[16],
                                int ecmd5_valid,
                                uint16_t caid, uint16_t sid, uint32_t provid,
                                uint32_t ticks_now)
{
	return cwreuse_offer_scoped(t, cw, ecmd5, ecmd5_valid,
	                            caid, sid, provid, provid != 0, ticks_now);
}

static inline void cwreuse_reset(struct cwreuse_table *t)
{
	if (t) memset(t, 0, sizeof(*t));
}

#endif /* MCS_CW_REUSE_H */
