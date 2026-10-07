/*
 * cw_reuse.h — TASK 1.3, definitive CW-reuse detection
 *
 * A control word is the output of a decryption keyed on a specific ECM. The same
 * 16 bytes appearing for two *different* ECMs on two *different* services within
 * seconds is not a coincidence — the space is 128 bits — it is a source handing
 * out keys from a small pool. That is one of the three proofs GR3 treats as
 * definitive, so unlike everything else in Phase 1 this one is allowed to
 * trigger hard action.
 *
 * Which is exactly why the false-positive analysis matters more than the
 * detection:
 *
 *   - The same service legitimately presents several distinct ECMs — Irdeto
 *     multi-CHID, different PIDs — and they all share one CW inside a crypto
 *     period. Keying on SID alone would call that reuse. GR6.
 *   - The same content can be carried under two CAIDs. Different CAID, same
 *     SID, same CW: plausible and benign.
 *
 * So this flags only when the **SID differs**. Same SID is never flagged,
 * whatever else changed. That deliberately under-detects — a poisoned source
 * attacking two services that happen to share an SID escapes — because a false
 * positive here disables a good card, and GR3 is explicit that this is worse
 * than the black screen being fixed.
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
 * Table depth. 64 entries; `struct cwreuse_entry` is 48 bytes after alignment
 * padding (the trailing `used` rounds the struct up from 43), so the whole
 * table is 3072 bytes of .bss.
 *
 * This is a deliberate trade, not an oversight: on a busy server 64 entries is a
 * short window, so a source that spreads bad keys thinly will be missed. What
 * this reliably catches is a source cycling a small pool of keys, which is the
 * common fake-card behaviour and the case that produces sustained black screens.
 * The depth is a single constant so it can be raised once TASK 3.5's simulators
 * show what it costs.
 */
#define CWREUSE_TABLE_SIZE 64

/* How long an entry stays meaningful. CWs rotate roughly every 10 s. */
#define CWREUSE_MAX_AGE_MS 20000u

#define CWREUSE_NONE  0   /* nothing conclusive */
#define CWREUSE_PROOF 1   /* same CW, different ECM, different SID: definitive */

struct cwreuse_entry {
	uint8_t  cw[16];
	uint8_t  ecmd5[16];
	uint32_t provid;
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
 * Offer one (CW, ECM) pair to the detector.
 *
 *   t            the table, caller-owned
 *   cw           the control word, 16 bytes
 *   ecmd5        the 128-bit MD5 of the ECM. Ignored when ecmd5_valid is 0.
 *   ecmd5_valid  0 when CACHEEX is off and ecmd5 is therefore not populated
 *   caid/sid/provid  the service the CW was delivered for
 *   ticks_now    GetTickCount()
 *
 * Returns CWREUSE_PROOF when this CW has already been seen for a different ECM
 * on a different service and the earlier sighting is still fresh; the entry is
 * then left in place so a source repeating the key keeps producing proofs.
 * Otherwise records the sighting and returns CWREUSE_NONE.
 *
 * The subtraction is unsigned so a 32-bit tick wrap still yields the true age.
 */
static inline int cwreuse_offer(struct cwreuse_table *t,
                                const uint8_t cw[16],
                                const uint8_t ecmd5[16],
                                int ecmd5_valid,
                                uint16_t caid, uint16_t sid, uint32_t provid,
                                uint32_t ticks_now)
{
	if (!ecmd5_valid) return CWREUSE_NONE;      /* D5: never fall back to the 32-bit hash */

	unsigned i = cwreuse_index(cw);
	struct cwreuse_entry *victim = &t->e[i];
	unsigned probe;

	for (probe = 0; probe < CWREUSE_TABLE_SIZE; probe++) {
		struct cwreuse_entry *e = &t->e[(i + probe) % CWREUSE_TABLE_SIZE];

		if (!e->used) { if (!victim->used) victim = e; continue; }

		/* Age out anything older than a couple of crypto periods. */
		if ((ticks_now - e->seen_at) > CWREUSE_MAX_AGE_MS) { e->used = 0; victim = e; continue; }

		if (memcmp(e->cw, cw, 16) != 0) { if (!victim->used) victim = e; continue; }

		/* Same control word. Reuse only if it is a different ECM ... */
		if (memcmp(e->ecmd5, ecmd5, 16) == 0) return CWREUSE_NONE;

		/* ... on a different service. Same SID is never proof: multi-CHID
		 * Irdeto and multi-PID services legitimately share one CW. */
		if (e->sid == sid) return CWREUSE_NONE;

		e->seen_at = ticks_now;
		return CWREUSE_PROOF;
	}

	/*
	 * No free or aged slot and no match: evict the oldest. Evicting the probe
	 * start instead would let one busy index thrash while the rest of the
	 * table sat unused, which is exactly the case where detection matters.
	 */
	{
		unsigned k, oldest = i;
		uint32_t oldest_at = t->e[i].seen_at;
		for (k = 0; k < CWREUSE_TABLE_SIZE; k++) {
			unsigned j = (i + k) % CWREUSE_TABLE_SIZE;
			if ((int32_t)(t->e[j].seen_at - oldest_at) < 0) { oldest = j; oldest_at = t->e[j].seen_at; }
		}
		victim = &t->e[oldest];
	}

	memcpy(victim->cw, cw, 16);
	memcpy(victim->ecmd5, ecmd5, 16);
	victim->caid = caid;
	victim->sid = sid;
	victim->provid = provid;
	victim->seen_at = ticks_now;
	victim->used = 1;
	return CWREUSE_NONE;
}

static inline void cwreuse_reset(struct cwreuse_table *t)
{
	memset(t, 0, sizeof(*t));
}

#endif /* MCS_CW_REUSE_H */
