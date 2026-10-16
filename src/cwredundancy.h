/* cwredundancy.h — CW Redundancy (R22 candidate #33)
 *
 * PURPOSE: store multiple CWs per channel to provide redundancy against
 * fake CWs. When the active CW fails, automatically switch to the next
 * best CW without waiting for a new ECM.
 *
 * MECHANISM: maintain a list of up to REDUNDANCY_MAX CWs per channel,
 * sorted by confidence score. When the active CW is detected as fake,
 * rotate to the next CW in the list.
 *
 * CONFIG: REDUNDANCY_MAX (default 4) — max CWs stored per channel.
 *
 * ROLLBACK: remove cwredundancy_get() calls; falls back to single CW.
 */
#ifndef MCS_CWREDUNDANCY_H
#define MCS_CWREDUNDANCY_H

#include <stdint.h>
#include <string.h>

#define REDUNDANCY_CHANNELS 64
#define REDUNDANCY_MAX      4   /* max CWs per channel */

struct cwredundancy_entry {
    uint8_t cw[8];
    int confidence;        /* 0-100 */
    uint32_t timestamp;    /* when received */
    uint8_t source_id;     /* which server/peer */
    uint8_t used;
};

struct cwredundancy_channel {
    uint16_t caid;
    uint32_t provid;
    uint16_t sid;
    struct cwredundancy_entry cws[REDUNDANCY_MAX];
    int cw_count;
    int active_idx;        /* index of currently active CW */
    uint8_t used;
};

struct cwredundancy_table {
    struct cwredundancy_channel channels[REDUNDANCY_CHANNELS];
};

static inline void cwredundancy_init(struct cwredundancy_table *t)
{
    if (!t) return;
    memset(t, 0, sizeof(*t));
}

static inline struct cwredundancy_channel *
cwredundancy_find(struct cwredundancy_table *t, uint16_t caid,
                  uint32_t provid, uint16_t sid)
{
    int i;
    if (!t) return 0;
    for (i = 0; i < REDUNDANCY_CHANNELS; i++) {
        struct cwredundancy_channel *c = &t->channels[i];
        if (c->used && c->caid == caid && c->provid == provid && c->sid == sid) {
            return c;
        }
    }
    return 0;
}

static inline struct cwredundancy_channel *
cwredundancy_alloc(struct cwredundancy_table *t, uint16_t caid,
                   uint32_t provid, uint16_t sid)
{
    int i, oldest = 0;
    struct cwredundancy_channel *c;

    c = cwredundancy_find(t, caid, provid, sid);
    if (c) return c;

    for (i = 0; i < REDUNDANCY_CHANNELS; i++) {
        if (!t->channels[i].used) {
            c = &t->channels[i];
            c->caid = caid;
            c->provid = provid;
            c->sid = sid;
            c->used = 1;
            return c;
        }
        /* Find oldest (by first CW timestamp) for eviction */
        if (t->channels[i].cws[0].timestamp <
            t->channels[oldest].cws[0].timestamp) {
            oldest = i;
        }
    }

    /* Evict oldest */
    c = &t->channels[oldest];
    memset(c, 0, sizeof(*c));
    c->caid = caid;
    c->provid = provid;
    c->sid = sid;
    c->used = 1;
    return c;
}

/* Add a CW to the redundancy list.
 * Returns: 1 = added, 0 = duplicate or error.
 * CWs are kept sorted by confidence (highest first).
 */
static inline int cwredundancy_add(struct cwredundancy_table *t,
                                   uint16_t caid, uint32_t provid,
                                   uint16_t sid, const uint8_t *cw,
                                   int confidence, uint32_t now,
                                   uint8_t source_id)
{
    struct cwredundancy_channel *c;
    int i, j;

    if (!t || !cw) return 0;

    c = cwredundancy_alloc(t, caid, provid, sid);
    if (!c) return 0;

    /* Check for duplicate */
    for (i = 0; i < c->cw_count; i++) {
        if (memcmp(c->cws[i].cw, cw, 8) == 0) {
            /* Update confidence if higher */
            if (confidence > c->cws[i].confidence) {
                c->cws[i].confidence = confidence;
                c->cws[i].timestamp = now;
                c->cws[i].source_id = source_id;
            }
            return 0;
        }
    }

    /* Add new CW */
    if (c->cw_count < REDUNDANCY_MAX) {
        i = c->cw_count;
        memcpy(c->cws[i].cw, cw, 8);
        c->cws[i].confidence = confidence;
        c->cws[i].timestamp = now;
        c->cws[i].source_id = source_id;
        c->cws[i].used = 1;
        c->cw_count++;
    } else {
        /* Replace lowest confidence CW if new one is better */
        int min_idx = 0;
        for (i = 1; i < REDUNDANCY_MAX; i++) {
            if (c->cws[i].confidence < c->cws[min_idx].confidence) {
                min_idx = i;
            }
        }
        if (confidence > c->cws[min_idx].confidence) {
            memcpy(c->cws[min_idx].cw, cw, 8);
            c->cws[min_idx].confidence = confidence;
            c->cws[min_idx].timestamp = now;
            c->cws[min_idx].source_id = source_id;
        }
    }

    /* Sort by confidence (descending) — simple insertion sort */
    for (i = 1; i < c->cw_count; i++) {
        struct cwredundancy_entry temp = c->cws[i];
        j = i - 1;
        while (j >= 0 && c->cws[j].confidence < temp.confidence) {
            c->cws[j + 1] = c->cws[j];
            j--;
        }
        c->cws[j + 1] = temp;
    }

    /* First CW is always active */
    c->active_idx = 0;

    return 1;
}

/* Get the currently active CW for a channel.
 * Returns: pointer to CW (8 bytes), or NULL if none.
 */
static inline const uint8_t *
cwredundancy_get(struct cwredundancy_table *t, uint16_t caid,
                 uint32_t provid, uint16_t sid)
{
    struct cwredundancy_channel *c;
    if (!t) return 0;
    c = cwredundancy_find(t, caid, provid, sid);
    if (!c || c->cw_count == 0) return 0;
    if (c->active_idx >= c->cw_count) c->active_idx = 0;
    return c->cws[c->active_idx].cw;
}

/* Rotate to the next CW (when active CW is detected as fake).
 * Returns: 1 = rotated successfully, 0 = no more CWs available.
 */
static inline int cwredundancy_rotate(struct cwredundancy_table *t,
                                      uint16_t caid, uint32_t provid,
                                      uint16_t sid)
{
    struct cwredundancy_channel *c;
    if (!t) return 0;
    c = cwredundancy_find(t, caid, provid, sid);
    if (!c || c->cw_count == 0) return 0;

    /* Remove current active CW (it's fake) */
    int i;
    for (i = c->active_idx; i < c->cw_count - 1; i++) {
        c->cws[i] = c->cws[i + 1];
    }
    c->cw_count--;

    if (c->cw_count == 0) {
        c->active_idx = 0;
        return 0;
    }

    /* Keep active_idx at 0 (highest confidence) */
    c->active_idx = 0;
    return 1;
}

/* Get the number of redundant CWs available for a channel. */
static inline int cwredundancy_count(struct cwredundancy_table *t,
                                     uint16_t caid, uint32_t provid,
                                     uint16_t sid)
{
    struct cwredundancy_channel *c;
    if (!t) return 0;
    c = cwredundancy_find(t, caid, provid, sid);
    if (!c) return 0;
    return c->cw_count;
}

#endif /* MCS_CWREDUNDANCY_H */
