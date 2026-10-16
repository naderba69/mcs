/* cwpriority.h — CW Priority Queue (R22 candidate #41)
 *
 * PURPOSE: manage CW delivery as a priority queue. When multiple CWs
 * are available for a channel, deliver them in priority order. This
 * ensures the best CW is always delivered first.
 *
 * MECHANISM: heap-based priority queue per channel. Priority is
 * computed from confidence score, source reputation, verification
 * status, and age.
 *
 * CONFIG: automatic — priority queue is always active.
 *
 * ROLLBACK: remove cwpriority_offer() calls; priority queue is inert.
 */
#ifndef MCS_CWPRIORITY_H
#define MCS_CWPRIORITY_H

#include <stdint.h>
#include <string.h>

#define CWPRIORITY_MAX_QUEUE 16
#define CWPRIORITY_CHANNELS 32

struct cwpriority_item {
    uint8_t cw[16];
    int priority;
    int source_id;
    int verified;
    uint32_t received_at;
    uint8_t used;
};

struct cwpriority_queue {
    uint16_t caid;
    uint32_t provid;
    uint16_t sid;
    struct cwpriority_item items[CWPRIORITY_MAX_QUEUE];
    int count;
    uint8_t used;
};

struct cwpriority_table {
    struct cwpriority_queue queues[CWPRIORITY_CHANNELS];
};

static inline void cwpriority_init(struct cwpriority_table *t)
{
    if (!t) return;
    memset(t, 0, sizeof(*t));
}

static inline struct cwpriority_queue *
cwpriority_find(struct cwpriority_table *t, uint16_t caid,
                uint32_t provid, uint16_t sid)
{
    int i;
    if (!t) return 0;
    for (i = 0; i < CWPRIORITY_CHANNELS; i++) {
        struct cwpriority_queue *q = &t->queues[i];
        if (q->used && q->caid == caid && q->provid == provid && q->sid == sid) {
            return q;
        }
    }
    return 0;
}

static inline struct cwpriority_queue *
cwpriority_alloc(struct cwpriority_table *t, uint16_t caid,
                 uint32_t provid, uint16_t sid)
{
    int i, oldest = 0;
    struct cwpriority_queue *q;

    q = cwpriority_find(t, caid, provid, sid);
    if (q) return q;

    for (i = 0; i < CWPRIORITY_CHANNELS; i++) {
        if (!t->queues[i].used) {
            q = &t->queues[i];
            q->caid = caid;
            q->provid = provid;
            q->sid = sid;
            q->used = 1;
            return q;
        }
        if (t->queues[i].count < t->queues[oldest].count) {
            oldest = i;
        }
    }
    /* Reuse oldest */
    q = &t->queues[oldest];
    memset(q, 0, sizeof(*q));
    q->caid = caid;
    q->provid = provid;
    q->sid = sid;
    q->used = 1;
    return q;
}

/* Compute priority from confidence score, verification, and age */
static inline int cwpriority_compute(int confidence, int verified,
                                      uint32_t age_secs)
{
    int priority = confidence;
    if (verified) priority += 20;
    /* Penalize old CWs: -1 per 10 seconds */
    if (age_secs > 0) priority -= (int)(age_secs / 10);
    if (priority < 0) priority = 0;
    if (priority > 120) priority = 120;
    return priority;
}

/* Offer a CW to the priority queue.
 * Returns: 1 if this CW should be delivered now (highest priority),
 *          0 if queued as alternative.
 */
static inline int cwpriority_offer(struct cwpriority_table *t,
                                    uint16_t caid, uint32_t provid,
                                    uint16_t sid, const uint8_t cw[16],
                                    int confidence, int source_id,
                                    int verified, uint32_t now)
{
    struct cwpriority_queue *q;
    struct cwpriority_item *slot;
    struct cwpriority_item *best = 0;
    int i, priority, free_slot = -1;

    if (!t || !cw) return 1;

    q = cwpriority_alloc(t, caid, provid, sid);
    if (!q) return 1;

    priority = cwpriority_compute(confidence, verified, 0);

    /* Find free slot or replace worst */
    for (i = 0; i < CWPRIORITY_MAX_QUEUE; i++) {
        if (!q->items[i].used) {
            free_slot = i;
            break;
        }
        if (free_slot < 0 || q->items[i].priority < q->items[free_slot].priority) {
            free_slot = i;
        }
    }

    if (free_slot < 0) free_slot = 0;

    slot = &q->items[free_slot];
    memcpy(slot->cw, cw, 16);
    slot->priority = priority;
    slot->source_id = source_id;
    slot->verified = verified;
    slot->received_at = now;
    slot->used = 1;
    if (q->count < CWPRIORITY_MAX_QUEUE) q->count++;

    /* Find best item */
    for (i = 0; i < CWPRIORITY_MAX_QUEUE; i++) {
        if (!q->items[i].used) continue;
        if (!best || q->items[i].priority > best->priority) {
            best = &q->items[i];
        }
    }

    /* If this CW is the best, deliver it now */
    if (best == slot) return 1;
    return 0;
}

/* Pop the highest-priority CW from the queue.
 * Returns: 1 if a CW was popped, 0 if queue empty.
 */
static inline int cwpriority_pop(struct cwpriority_table *t,
                                  uint16_t caid, uint32_t provid,
                                  uint16_t sid, uint8_t out_cw[16])
{
    struct cwpriority_queue *q;
    struct cwpriority_item *best = 0;
    int i;

    if (!t || !out_cw) return 0;
    q = cwpriority_find(t, caid, provid, sid);
    if (!q) return 0;

    for (i = 0; i < CWPRIORITY_MAX_QUEUE; i++) {
        if (!q->items[i].used) continue;
        if (!best || q->items[i].priority > best->priority) {
            best = &q->items[i];
        }
    }

    if (!best) return 0;
    memcpy(out_cw, best->cw, 16);
    best->used = 0;
    q->count--;
    return 1;
}

/* Peek at the highest-priority CW without removing it */
static inline int cwpriority_peek(const struct cwpriority_table *t,
                                   uint16_t caid, uint32_t provid,
                                   uint16_t sid, uint8_t out_cw[16])
{
    const struct cwpriority_queue *q;
    const struct cwpriority_item *best = 0;
    int i;

    if (!t || !out_cw) return 0;
    q = cwpriority_find((struct cwpriority_table *)t, caid, provid, sid);
    if (!q) return 0;

    for (i = 0; i < CWPRIORITY_MAX_QUEUE; i++) {
        if (!q->items[i].used) continue;
        if (!best || q->items[i].priority > best->priority) {
            best = &q->items[i];
        }
    }

    if (!best) return 0;
    memcpy(out_cw, best->cw, 16);
    return 1;
}

#endif /* MCS_CWPRIORITY_H */
