/* multicw.h — Multi-CW Delivery (R22 candidate #6)
 *
 * PURPOSE: when multiple CWs are available for the same channel (from
 * different sources or from cache + server), deliver the best one to
 * the client AND keep the alternatives ready. If the delivered CW
 * fails (black screen), immediately switch to the next best alternative
 * without waiting for a new ECM.
 *
 * MECHANISM: ring buffer of up to 3 CWs per channel, sorted by
 * confidence score. When the active CW is flagged as fake, rotate to
 * the next one.
 *
 * CONFIG: MULTI CW: ON (default OFF = stock single CW delivery).
 *
 * ROLLBACK: remove multicw_offer() calls; multi-CW is inert.
 */
#ifndef MCS_MULTICW_H
#define MCS_MULTICW_H

#include <stdint.h>
#include <string.h>

#define MULTICW_MAX_ALTERNATIVES 3
#define MULTICW_CHANNELS 32

struct multicw_slot {
    uint8_t cw[16];
    int confidence;
    int source_id;
    int verified;
    uint32_t received_at;
    uint8_t used;
    uint8_t active;   /* 1 = currently delivered to client */
    uint8_t failed;   /* 1 = proven fake */
};

struct multicw_channel {
    uint16_t caid;
    uint32_t provid;
    uint16_t sid;
    struct multicw_slot slots[MULTICW_MAX_ALTERNATIVES];
    int count;
    uint32_t rotations;  /* times we rotated to an alternative */
};

struct multicw_table {
    struct multicw_channel channels[MULTICW_CHANNELS];
    int enabled;
    uint32_t total_offers;
    uint32_t total_rotations;
};

static inline void multicw_init(struct multicw_table *t, int enabled)
{
    if (!t) return;
    memset(t, 0, sizeof(*t));
    t->enabled = enabled;
}

static inline int multicw_enabled(const struct multicw_table *t)
{
    return t && t->enabled;
}

static inline struct multicw_channel *
multicw_find(struct multicw_table *t, uint16_t caid,
             uint32_t provid, uint16_t sid)
{
    int i;
    if (!t) return 0;
    for (i = 0; i < MULTICW_CHANNELS; i++) {
        struct multicw_channel *c = &t->channels[i];
        if (c->caid == caid && c->provid == provid && c->sid == sid) {
            return c;
        }
    }
    return 0;
}

static inline struct multicw_channel *
multicw_alloc(struct multicw_table *t, uint16_t caid,
              uint32_t provid, uint16_t sid)
{
    int i, oldest = 0;
    struct multicw_channel *c;

    c = multicw_find(t, caid, provid, sid);
    if (c) return c;

    for (i = 0; i < MULTICW_CHANNELS; i++) {
        if (!t->channels[i].caid) {
            c = &t->channels[i];
            c->caid = caid;
            c->provid = provid;
            c->sid = sid;
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
    return c;
}

/* Offer a CW for a channel. If it's the best available, mark it active.
 * Returns: 1 if this CW should be delivered now, 0 if kept as alternative.
 */
static inline int multicw_offer(struct multicw_table *t,
                                 uint16_t caid, uint32_t provid,
                                 uint16_t sid, const uint8_t cw[16],
                                 int confidence, int source_id,
                                 int verified, uint32_t now)
{
    struct multicw_channel *c;
    struct multicw_slot *best = 0;
    int i, free_slot = -1;
    int has_active = 0;

    if (!multicw_enabled(t) || !cw) return 1; /* disabled = always deliver */

    c = multicw_alloc(t, caid, provid, sid);
    if (!c) return 1;
    t->total_offers++;

    /* Find free slot or replace worst */
    for (i = 0; i < MULTICW_MAX_ALTERNATIVES; i++) {
        if (!c->slots[i].used) {
            free_slot = i;
            break;
        }
        if (c->slots[i].active) has_active = 1;
        if (c->slots[i].failed) {
            free_slot = i;  /* reuse failed slot */
            break;
        }
    }
    if (free_slot < 0) {
        /* Replace the worst slot */
        int worst = 0;
        for (i = 1; i < MULTICW_MAX_ALTERNATIVES; i++) {
            if (c->slots[i].confidence < c->slots[worst].confidence) {
                worst = i;
            }
        }
        free_slot = worst;
    }

    /* Store the new CW */
    struct multicw_slot *s = &c->slots[free_slot];
    memcpy(s->cw, cw, 16);
    s->confidence = confidence;
    s->source_id = source_id;
    s->verified = verified;
    s->received_at = now;
    s->used = 1;
    s->active = 0;
    s->failed = 0;

    /* If no active CW, make this one active */
    if (!has_active) {
        s->active = 1;
        return 1;
    }

    /* Find the best CW */
    for (i = 0; i < MULTICW_MAX_ALTERNATIVES; i++) {
        if (!c->slots[i].used || c->slots[i].failed) continue;
        if (!best || c->slots[i].confidence > best->confidence) {
            best = &c->slots[i];
        }
    }

    /* If the new CW is better than the active one, rotate */
    if (best && best == s) {
        for (i = 0; i < MULTICW_MAX_ALTERNATIVES; i++) {
            c->slots[i].active = (&c->slots[i] == best) ? 1 : 0;
        }
        return 1;
    }

    return 0;  /* kept as alternative */
}

/* Mark the active CW as failed and rotate to the next best alternative.
 * Returns: 1 if a replacement was found, 0 if no alternatives.
 */
static inline int multicw_rotate(struct multicw_table *t,
                                  uint16_t caid, uint32_t provid,
                                  uint16_t sid, uint8_t out_cw[16])
{
    struct multicw_channel *c;
    struct multicw_slot *best = 0;
    int i;

    if (!multicw_enabled(t) || !out_cw) return 0;
    c = multicw_find(t, caid, provid, sid);
    if (!c) return 0;

    /* Mark active as failed */
    for (i = 0; i < MULTICW_MAX_ALTERNATIVES; i++) {
        if (c->slots[i].used && c->slots[i].active) {
            c->slots[i].failed = 1;
            c->slots[i].active = 0;
        }
    }

    /* Find best non-failed alternative */
    for (i = 0; i < MULTICW_MAX_ALTERNATIVES; i++) {
        if (!c->slots[i].used || c->slots[i].failed) continue;
        if (!best || c->slots[i].confidence > best->confidence) {
            best = &c->slots[i];
        }
    }

    if (!best) return 0;

    memcpy(out_cw, best->cw, 16);
    best->active = 1;
    c->rotations++;
    t->total_rotations++;
    return 1;
}

#endif /* MCS_MULTICW_H */
