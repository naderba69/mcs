/* cwreplace.h — Instant CW Replacement (R22 candidate #28)
 *
 * PURPOSE: when a CW is detected as fake (by cwverify, cwlate, or any
 * other layer), immediately push a replacement CW to the client without
 * waiting for a new ECM request. This eliminates the black screen
 * window that currently exists between "fake CW delivered" and
 * "correct CW arrives".
 *
 * MECHANISM: maintain a small ring buffer of the last N CWs delivered
 * per (caid, provid, sid). When the current CW is flagged as fake,
 * search the ring for the most recent alternative that passed
 * verification. Push it to all clients currently watching that channel.
 *
 * SCOPE: called from the same points that flag a CW as fake
 * (cwlog_refuse, cwlate_gate). The push uses the existing delivery
 * path (cs_senddcw_cli) so no protocol changes are needed.
 *
 * THREAD SAFETY: ring buffer is under the existing ecm lock.
 *
 * CONFIG: opt-in per profile via CW REPLACE: ON (default OFF = stock).
 *
 * ROLLBACK: remove cwreplace_offer() calls; the ring buffer is inert
 * when not read.
 */
#ifndef MCS_CWREPLACE_H
#define MCS_CWREPLACE_H

#include <stdint.h>
#include <string.h>

#define CWREPLACE_RING_SIZE 8   /* last 8 CWs per channel */

struct cwreplace_entry {
    uint8_t cw[16];
    uint32_t tick;         /* when it was delivered (monotonic seconds) */
    uint8_t verified;      /* 1 = passed cwverify, 0 = unknown */
    uint8_t used;          /* 1 = slot in use */
};

struct cwreplace_channel {
    uint16_t caid;
    uint32_t provid;
    uint16_t sid;
    struct cwreplace_entry ring[CWREPLACE_RING_SIZE];
    int head;              /* next write position */
    uint32_t replacements; /* total replacements made */
};

#define CWREPLACE_CHANNELS 32   /* LRU channels */

struct cwreplace_table {
    struct cwreplace_channel slots[CWREPLACE_CHANNELS];
    int lru[CWREPLACE_CHANNELS]; /* LRU order (0 = most recent) */
};

static inline void cwreplace_init(struct cwreplace_table *t)
{
    int i;
    if (!t) return;
    memset(t, 0, sizeof(*t));
    for (i = 0; i < CWREPLACE_CHANNELS; i++) t->lru[i] = i;
}

/* Find or allocate a channel slot */
static inline struct cwreplace_channel *
cwreplace_find(struct cwreplace_table *t, uint16_t caid,
               uint32_t provid, uint16_t sid)
{
    int i, oldest;
    if (!t) return 0;

    for (i = 0; i < CWREPLACE_CHANNELS; i++) {
        struct cwreplace_channel *c = &t->slots[i];
        if (c->caid == caid && c->provid == provid && c->sid == sid) {
            return c;
        }
    }
    /* Allocate: use the slot with the oldest replacement */
    oldest = 0;
    for (i = 1; i < CWREPLACE_CHANNELS; i++) {
        if (t->slots[i].replacements < t->slots[oldest].replacements) {
            oldest = i;
        }
    }
    /* Clear and reuse */
    memset(&t->slots[oldest], 0, sizeof(t->slots[oldest]));
    t->slots[oldest].caid = caid;
    t->slots[oldest].provid = provid;
    t->slots[oldest].sid = sid;
    return &t->slots[oldest];
}

/* Record a CW delivery into the ring buffer */
static inline void cwreplace_record(struct cwreplace_table *t,
                                     uint16_t caid, uint32_t provid,
                                     uint16_t sid, const uint8_t cw[16],
                                     uint32_t tick, int verified)
{
    struct cwreplace_channel *c;
    struct cwreplace_entry *e;
    int i;

    if (!t || !cw) return;
    c = cwreplace_find(t, caid, provid, sid);
    if (!c) return;

    /* Don't record duplicates of the current CW */
    for (i = 0; i < CWREPLACE_RING_SIZE; i++) {
        if (c->ring[i].used && memcmp(c->ring[i].cw, cw, 16) == 0) {
            c->ring[i].tick = tick;
            if (verified) c->ring[i].verified = 1;
            return;
        }
    }

    e = &c->ring[c->head];
    memcpy(e->cw, cw, 16);
    e->tick = tick;
    e->verified = verified ? 1 : 0;
    e->used = 1;
    c->head = (c->head + 1) % CWREPLACE_RING_SIZE;
}

/* Find the best replacement CW for a channel.
 * current_cw: the CW that was just flagged as fake (to exclude)
 * out_cw: receives the replacement CW (16 bytes)
 * Returns: 1 if a replacement was found, 0 otherwise.
 */
static inline int cwreplace_offer(struct cwreplace_table *t,
                                   uint16_t caid, uint32_t provid,
                                   uint16_t sid,
                                   const uint8_t current_cw[16],
                                   uint8_t out_cw[16],
                                   uint32_t now, uint32_t max_age)
{
    struct cwreplace_channel *c;
    struct cwreplace_entry *best = 0;
    int i;

    if (!t || !current_cw || !out_cw) return 0;
    c = cwreplace_find(t, caid, provid, sid);
    if (!c) return 0;

    for (i = 0; i < CWREPLACE_RING_SIZE; i++) {
        struct cwreplace_entry *e = &c->ring[i];
        if (!e->used) continue;
        /* Exclude the current (fake) CW */
        if (memcmp(e->cw, current_cw, 16) == 0) continue;
        /* Exclude expired entries */
        if (max_age > 0 && (now - e->tick) > max_age) continue;
        /* Prefer verified entries */
        if (!best) { best = e; continue; }
        if (e->verified && !best->verified) { best = e; continue; }
        if (e->tick > best->tick) best = e;
    }

    if (!best) return 0;
    memcpy(out_cw, best->cw, 16);
    c->replacements++;
    return 1;
}

/* Snapshot for reporting */
struct cwreplace_snapshot {
    uint16_t caid;
    uint32_t provid;
    uint16_t sid;
    uint32_t replacements;
    int ring_used;
};

static inline int cwreplace_snapshot(struct cwreplace_table *t,
                                      struct cwreplace_snapshot *out,
                                      int max_entries)
{
    int i, n = 0;
    if (!t || !out) return 0;
    for (i = 0; i < CWREPLACE_CHANNELS && n < max_entries; i++) {
        struct cwreplace_channel *c = &t->slots[i];
        int used = 0, j;
        if (!c->caid) continue;
        for (j = 0; j < CWREPLACE_RING_SIZE; j++) {
            if (c->ring[j].used) used++;
        }
        out[n].caid = c->caid;
        out[n].provid = c->provid;
        out[n].sid = c->sid;
        out[n].replacements = c->replacements;
        out[n].ring_used = used;
        n++;
    }
    return n;
}

#endif /* MCS_CWREPLACE_H */
