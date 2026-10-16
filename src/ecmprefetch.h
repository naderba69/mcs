/* ecmprefetch.h — ECM Prefetch for Fast Channel Switching (R22 candidate #16)
 *
 * PURPOSE: prefetch ECM requests for channels adjacent to the current
 * one, so when the user zaps to a nearby channel the CW is already
 * being fetched (or already in cache). This reduces zap time from
 * ~200-500ms to ~0ms for adjacent channels.
 *
 * MECHANISM: on each zap (detected from client ECM requests), enqueue
 * prefetch requests for SID±1, ±2 (configurable depth). Prefetches
 * are low-priority and cancelled if a real request arrives for that
 * channel.
 *
 * SCOPE: the prefetch engine sits between the client ECM path and the
 * server selection. It never delays a real request.
 *
 * CONFIG: ECM PREFETCH: N (0 = off, 1-5 = depth, default 0).
 *
 * ROLLBACK: remove ecmprefetch_enqueue() call; the queue is inert.
 */
#ifndef MCS_ECMPREFETCH_H
#define MCS_ECMPREFETCH_H

#include <stdint.h>
#include <string.h>

#define ECMPREFETCH_QUEUE_SIZE 64
#define ECMPREFETCH_MAX_DEPTH 5

struct ecmprefetch_entry {
    uint16_t caid;
    uint32_t provid;
    uint16_t sid;
    uint32_t enqueued_at;
    uint8_t depth;         /* 1 = SID±1, 2 = SID±2, etc. */
    uint8_t state;         /* 0=queued, 1=sent, 2=answered, 3=cancelled */
};

#define ECMPREFETCH_QUEUED   0
#define ECMPREFETCH_SENT     1
#define ECMPREFETCH_ANSWERED 2
#define ECMPREFETCH_CANCELLED 3

struct ecmprefetch_table {
    struct ecmprefetch_entry q[ECMPREFETCH_QUEUE_SIZE];
    int head, tail;
    int depth;             /* configured depth (0 = disabled) */
    uint32_t enqueued;
    uint32_t answered;
    uint32_t cancelled;
    uint32_t hits;         /* prefetch answered before real request */
};

static inline void ecmprefetch_init(struct ecmprefetch_table *t, int depth)
{
    if (!t) return;
    memset(t, 0, sizeof(*t));
    if (depth < 0) depth = 0;
    if (depth > ECMPREFETCH_MAX_DEPTH) depth = ECMPREFETCH_MAX_DEPTH;
    t->depth = depth;
}

static inline int ecmprefetch_enabled(const struct ecmprefetch_table *t)
{
    return t && t->depth > 0;
}

/* Find an entry by (caid, provid, sid) */
static inline struct ecmprefetch_entry *
ecmprefetch_find(struct ecmprefetch_table *t, uint16_t caid,
                 uint32_t provid, uint16_t sid)
{
    int i;
    if (!t) return 0;
    for (i = t->tail; i != t->head; i = (i + 1) % ECMPREFETCH_QUEUE_SIZE) {
        struct ecmprefetch_entry *e = &t->q[i];
        if (e->caid == caid && e->provid == provid && e->sid == sid) {
            return e;
        }
    }
    return 0;
}

/* Enqueue a prefetch request. Returns 1 if enqueued, 0 if duplicate/full. */
static inline int ecmprefetch_enqueue(struct ecmprefetch_table *t,
                                       uint16_t caid, uint32_t provid,
                                       uint16_t sid, uint8_t depth,
                                       uint32_t now)
{
    struct ecmprefetch_entry *e;
    int next;

    if (!ecmprefetch_enabled(t)) return 0;
    if (depth > (uint8_t)t->depth) return 0;

    /* Check for duplicate */
    if (ecmprefetch_find(t, caid, provid, sid)) return 0;

    next = (t->head + 1) % ECMPREFETCH_QUEUE_SIZE;
    if (next == t->tail) {
        /* Queue full — drop oldest */
        t->tail = (t->tail + 1) % ECMPREFETCH_QUEUE_SIZE;
    }
    e = &t->q[t->head];
    e->caid = caid;
    e->provid = provid;
    e->sid = sid;
    e->depth = depth;
    e->state = ECMPREFETCH_QUEUED;
    e->enqueued_at = now;
    t->head = next;
    t->enqueued++;
    return 1;
}

/* Enqueue all neighbors for a zap event */
static inline int ecmprefetch_on_zap(struct ecmprefetch_table *t,
                                      uint16_t caid, uint32_t provid,
                                      uint16_t sid, uint32_t now)
{
    int d, count = 0;
    if (!ecmprefetch_enabled(t)) return 0;
    for (d = 1; d <= t->depth; d++) {
        if (sid > (uint16_t)d) {
            count += ecmprefetch_enqueue(t, caid, provid, sid - d, d, now);
        }
        count += ecmprefetch_enqueue(t, caid, provid, sid + d, d, now);
    }
    return count;
}

/* Mark a prefetch as answered — returns 1 if it was a prefetch hit */
static inline int ecmprefetch_answered(struct ecmprefetch_table *t,
                                        uint16_t caid, uint32_t provid,
                                        uint16_t sid)
{
    struct ecmprefetch_entry *e;
    if (!t) return 0;
    e = ecmprefetch_find(t, caid, provid, sid);
    if (!e || e->state != ECMPREFETCH_SENT) return 0;
    e->state = ECMPREFETCH_ANSWERED;
    t->answered++;
    t->hits++;
    return 1;
}

/* Cancel prefetches for a channel (real request arrived) */
static inline int ecmprefetch_cancel(struct ecmprefetch_table *t,
                                      uint16_t caid, uint32_t provid,
                                      uint16_t sid)
{
    struct ecmprefetch_entry *e;
    if (!t) return 0;
    e = ecmprefetch_find(t, caid, provid, sid);
    if (!e) return 0;
    if (e->state == ECMPREFETCH_ANSWERED) {
        t->hits++;
        return 1; /* prefetch was already answered — this is a hit! */
    }
    e->state = ECMPREFETCH_CANCELLED;
    t->cancelled++;
    return 0;
}

/* Expire old entries */
static inline void ecmprefetch_expire(struct ecmprefetch_table *t,
                                       uint32_t now, uint32_t max_age)
{
    int i;
    if (!t) return;
    for (i = t->tail; i != t->head; i = (i + 1) % ECMPREFETCH_QUEUE_SIZE) {
        struct ecmprefetch_entry *e = &t->q[i];
        if ((now - e->enqueued_at) > max_age) {
            e->state = ECMPREFETCH_CANCELLED;
        }
    }
}

#endif /* MCS_ECMPREFETCH_H */
