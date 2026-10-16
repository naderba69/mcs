/* fallback.h — Fallback CW Chain (R22 candidate #30)
 *
 * PURPOSE: maintain a chain of fallback CWs for each channel. When
 * the primary CW fails, automatically try the next fallback in the
 * chain without waiting for a new ECM request.
 *
 * MECHANISM: ring buffer of up to 4 CWs per channel, ordered by
 * confidence. When the active CW fails, rotate to the next fallback.
 *
 * CONFIG: FALLBACK CHAIN: ON (default OFF = single CW).
 *
 * ROLLBACK: remove fallback_offer() calls; fallback chain is inert.
 */
#ifndef MCS_FALLBACK_H
#define MCS_FALLBACK_H

#include <stdint.h>
#include <string.h>

#define FALLBACK_MAX_CHAIN 4
#define FALLBACK_CHANNELS 32

struct fallback_cw {
    uint8_t cw[16];
    int confidence;
    int source_id;
    uint32_t received_at;
    uint8_t used;
    uint8_t failed;
};

struct fallback_channel {
    uint16_t caid;
    uint32_t provid;
    uint16_t sid;
    struct fallback_cw chain[FALLBACK_MAX_CHAIN];
    int head;
    uint32_t rotations;
    uint8_t used;
};

struct fallback_table {
    struct fallback_channel channels[FALLBACK_CHANNELS];
    int enabled;
    uint32_t total_rotations;
};

static inline void fallback_init(struct fallback_table *t, int enabled)
{
    if (!t) return;
    memset(t, 0, sizeof(*t));
    t->enabled = enabled;
}

static inline int fallback_enabled(const struct fallback_table *t)
{
    return t && t->enabled;
}

static inline struct fallback_channel *
fallback_find(struct fallback_table *t, uint16_t caid,
              uint32_t provid, uint16_t sid)
{
    int i;
    if (!t) return 0;
    for (i = 0; i < FALLBACK_CHANNELS; i++) {
        struct fallback_channel *c = &t->channels[i];
        if (c->used && c->caid == caid && c->provid == provid && c->sid == sid) {
            return c;
        }
    }
    return 0;
}

static inline struct fallback_channel *
fallback_alloc(struct fallback_table *t, uint16_t caid,
               uint32_t provid, uint16_t sid)
{
    int i, oldest = 0;
    struct fallback_channel *c;

    c = fallback_find(t, caid, provid, sid);
    if (c) return c;

    for (i = 0; i < FALLBACK_CHANNELS; i++) {
        if (!t->channels[i].used) {
            c = &t->channels[i];
            c->caid = caid;
            c->provid = provid;
            c->sid = sid;
            c->used = 1;
            return c;
        }
        if (t->channels[i].rotations < t->channels[oldest].rotations) {
            oldest = i;
        }
    }
    /* Reuse oldest */
    c = &t->channels[oldest];
    memset(c, 0, sizeof(*c));
    c->caid = caid;
    c->provid = provid;
    c->sid = sid;
    c->used = 1;
    return c;
}

/* Offer a CW for the fallback chain.
 * Returns: 1 if this CW should be used now, 0 if kept as fallback.
 */
static inline int fallback_offer(struct fallback_table *t,
                                  uint16_t caid, uint32_t provid,
                                  uint16_t sid, const uint8_t cw[16],
                                  int confidence, int source_id,
                                  uint32_t now)
{
    struct fallback_channel *c;
    struct fallback_cw *slot;
    int i, has_valid = 0;

    if (!fallback_enabled(t) || !cw) return 1;

    c = fallback_alloc(t, caid, provid, sid);
    if (!c) return 1;

    /* Check if we have any valid (non-failed) CWs */
    for (i = 0; i < FALLBACK_MAX_CHAIN; i++) {
        if (c->chain[i].used && !c->chain[i].failed) {
            has_valid = 1;
            break;
        }
    }

    /* Find slot: reuse failed or oldest */
    slot = &c->chain[c->head];
    if (slot->used && slot->failed) {
        /* Reuse failed slot */
    } else {
        /* Find worst slot */
        int worst = 0;
        for (i = 1; i < FALLBACK_MAX_CHAIN; i++) {
            if (c->chain[i].confidence < c->chain[worst].confidence) {
                worst = i;
            }
        }
        slot = &c->chain[worst];
    }

    memcpy(slot->cw, cw, 16);
    slot->confidence = confidence;
    slot->source_id = source_id;
    slot->received_at = now;
    slot->used = 1;
    slot->failed = 0;
    c->head = (c->head + 1) % FALLBACK_MAX_CHAIN;

    /* If no valid CWs, use this one */
    if (!has_valid) return 1;

    /* Otherwise, keep as fallback */
    return 0;
}

/* Rotate to the next fallback CW.
 * Returns: 1 if a fallback was found, 0 if chain exhausted.
 */
static inline int fallback_rotate(struct fallback_table *t,
                                   uint16_t caid, uint32_t provid,
                                   uint16_t sid, const uint8_t current_cw[16],
                                   uint8_t out_cw[16])
{
    struct fallback_channel *c;
    struct fallback_cw *best = 0;
    int i;

    if (!fallback_enabled(t) || !current_cw || !out_cw) return 0;
    c = fallback_find(t, caid, provid, sid);
    if (!c) return 0;

    /* Mark current CW as failed */
    for (i = 0; i < FALLBACK_MAX_CHAIN; i++) {
        if (c->chain[i].used && memcmp(c->chain[i].cw, current_cw, 16) == 0) {
            c->chain[i].failed = 1;
        }
    }

    /* Find best non-failed fallback */
    for (i = 0; i < FALLBACK_MAX_CHAIN; i++) {
        if (!c->chain[i].used || c->chain[i].failed) continue;
        if (!best || c->chain[i].confidence > best->confidence) {
            best = &c->chain[i];
        }
    }

    if (!best) return 0;

    memcpy(out_cw, best->cw, 16);
    c->rotations++;
    t->total_rotations++;
    return 1;
}

#endif /* MCS_FALLBACK_H */
