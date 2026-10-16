/* cwquarantine.h — CW Quarantine (R22 candidate #15)
 *
 * PURPOSE: hold suspicious CWs in quarantine before serving them to
 * clients. A CW that looks suspicious (low confidence, unverified,
 * from a low-trust source) is held for a short period to allow
 * verification or a better CW to arrive.
 *
 * MECHANISM: ring buffer of quarantined CWs per channel. When a
 * quarantined CW is verified or a better CW arrives, the quarantine
 * is released. After the quarantine period expires, the CW is either
 * served (if no better option) or discarded.
 *
 * CONFIG: CW QUARANTINE: N (0 = off, 1-10 = seconds, default 0).
 *
 * ROLLBACK: remove cwquarantine_hold() calls; quarantine is inert.
 */
#ifndef MCS_CWQUARANTINE_H
#define MCS_CWQUARANTINE_H

#include <stdint.h>
#include <string.h>

#define CWQUARANTINE_RING_SIZE 4
#define CWQUARANTINE_CHANNELS 16

struct cwquarantine_entry {
    uint8_t cw[16];
    uint32_t held_at;
    uint32_t expires_at;
    int confidence;
    int verified;
    uint8_t used;
    uint8_t released;  /* 1 = served to client */
};

struct cwquarantine_channel {
    uint16_t caid;
    uint32_t provid;
    uint16_t sid;
    struct cwquarantine_entry ring[CWQUARANTINE_RING_SIZE];
    int head;
    uint32_t held_count;
    uint32_t released_count;
    uint32_t expired_count;
};

struct cwquarantine_table {
    struct cwquarantine_channel channels[CWQUARANTINE_CHANNELS];
    int hold_secs;
};

static inline void cwquarantine_init(struct cwquarantine_table *t, int hold_secs)
{
    if (!t) return;
    memset(t, 0, sizeof(*t));
    if (hold_secs < 0) hold_secs = 0;
    if (hold_secs > 10) hold_secs = 10;
    t->hold_secs = hold_secs;
}

static inline int cwquarantine_enabled(const struct cwquarantine_table *t)
{
    return t && t->hold_secs > 0;
}

static inline struct cwquarantine_channel *
cwquarantine_find(struct cwquarantine_table *t, uint16_t caid,
                  uint32_t provid, uint16_t sid)
{
    int i;
    if (!t) return 0;
    for (i = 0; i < CWQUARANTINE_CHANNELS; i++) {
        struct cwquarantine_channel *c = &t->channels[i];
        if (c->caid == caid && c->provid == provid && c->sid == sid) {
            return c;
        }
    }
    return 0;
}

static inline struct cwquarantine_channel *
cwquarantine_alloc(struct cwquarantine_table *t, uint16_t caid,
                   uint32_t provid, uint16_t sid)
{
    int i, oldest = 0;
    struct cwquarantine_channel *c;

    c = cwquarantine_find(t, caid, provid, sid);
    if (c) return c;

    for (i = 1; i < CWQUARANTINE_CHANNELS; i++) {
        if (t->channels[i].held_count < t->channels[oldest].held_count) {
            oldest = i;
        }
    }
    c = &t->channels[oldest];
    memset(c, 0, sizeof(*c));
    c->caid = caid;
    c->provid = provid;
    c->sid = sid;
    return c;
}

/* Hold a CW in quarantine.
 * Returns: 1 if held, 0 if quarantine disabled or CW already held.
 */
static inline int cwquarantine_hold(struct cwquarantine_table *t,
                                     uint16_t caid, uint32_t provid,
                                     uint16_t sid, const uint8_t cw[16],
                                     uint32_t now, int confidence,
                                     int verified)
{
    struct cwquarantine_channel *c;
    struct cwquarantine_entry *e;
    int i;

    if (!cwquarantine_enabled(t)) return 0;
    if (!cw) return 0;

    c = cwquarantine_alloc(t, caid, provid, sid);
    if (!c) return 0;

    /* Check for duplicate */
    for (i = 0; i < CWQUARANTINE_RING_SIZE; i++) {
        if (c->ring[i].used && !c->ring[i].released &&
            memcmp(c->ring[i].cw, cw, 16) == 0) {
            return 0; /* already held */
        }
    }

    e = &c->ring[c->head];
    memcpy(e->cw, cw, 16);
    e->held_at = now;
    e->expires_at = now + (uint32_t)t->hold_secs;
    e->confidence = confidence;
    e->verified = verified;
    e->used = 1;
    e->released = 0;
    c->head = (c->head + 1) % CWQUARANTINE_RING_SIZE;
    c->held_count++;
    return 1;
}

/* Release the best quarantined CW for a channel.
 * Returns: 1 if a CW was released, 0 if none available.
 */
static inline int cwquarantine_release(struct cwquarantine_table *t,
                                        uint16_t caid, uint32_t provid,
                                        uint16_t sid, uint32_t now,
                                        uint8_t out_cw[16])
{
    struct cwquarantine_channel *c;
    struct cwquarantine_entry *best = 0;
    int i;

    if (!t || !out_cw) return 0;
    c = cwquarantine_find(t, caid, provid, sid);
    if (!c) return 0;

    for (i = 0; i < CWQUARANTINE_RING_SIZE; i++) {
        struct cwquarantine_entry *e = &c->ring[i];
        if (!e->used || e->released) continue;
        /* Only release if quarantine period has passed or CW is verified */
        if (now < e->expires_at && !e->verified) continue;
        if (!best || e->confidence > best->confidence) {
            best = e;
        }
    }

    if (!best) return 0;
    memcpy(out_cw, best->cw, 16);
    best->released = 1;
    c->released_count++;
    return 1;
}

/* Expire quarantined CWs that were never released */
static inline void cwquarantine_expire(struct cwquarantine_table *t,
                                        uint32_t now, uint32_t max_age)
{
    int i, j;
    if (!t) return;
    for (i = 0; i < CWQUARANTINE_CHANNELS; i++) {
        struct cwquarantine_channel *c = &t->channels[i];
        if (!c->caid) continue;
        for (j = 0; j < CWQUARANTINE_RING_SIZE; j++) {
            struct cwquarantine_entry *e = &c->ring[j];
            if (e->used && !e->released && (now - e->held_at) > max_age) {
                e->released = 1;
                c->expired_count++;
            }
        }
    }
}

/* Verify a quarantined CW (mark it as verified so it can be released immediately) */
static inline int cwquarantine_verify(struct cwquarantine_table *t,
                                       uint16_t caid, uint32_t provid,
                                       uint16_t sid, const uint8_t cw[16])
{
    struct cwquarantine_channel *c;
    int i;

    if (!t || !cw) return 0;
    c = cwquarantine_find(t, caid, provid, sid);
    if (!c) return 0;

    for (i = 0; i < CWQUARANTINE_RING_SIZE; i++) {
        struct cwquarantine_entry *e = &c->ring[i];
        if (e->used && !e->released && memcmp(e->cw, cw, 16) == 0) {
            e->verified = 1;
            return 1;
        }
    }
    return 0;
}

#endif /* MCS_CWQUARANTINE_H */
