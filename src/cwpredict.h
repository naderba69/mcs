/* cwpredict.h — CW Expiry Prediction (R22 candidate #13)
 *
 * PURPOSE: predict when a CW will expire based on the channel's CW
 * cycle pattern. This allows proactive prefetching before the CW
 * actually expires, eliminating the gap between "old CW expired" and
 * "new CW arrives".
 *
 * MECHANISM: track CW change timestamps per channel. Compute the
 * average cycle time and predict the next change.
 *
 * CONFIG: automatic — adapts to each channel's pattern.
 *
 * ROLLBACK: remove cwpredict_next() calls; prediction is inert.
 */
#ifndef MCS_CWPREDICT_H
#define MCS_CWPREDICT_H

#include <stdint.h>
#include <string.h>
#include "tick_math.h"

#define CWPREDICT_CHANNELS 64
#define CWPREDICT_HISTORY 8  /* last 8 CW changes */
#define CWPREDICT_EARLY_MS 1000  /* prefetch 1s before predicted expiry */

struct cwpredict_channel {
    uint16_t caid;
    uint32_t provid;
    uint16_t sid;
    uint32_t changes[CWPREDICT_HISTORY];
    int change_count;
    int change_head;
    uint32_t last_change;
    uint8_t used;
};

struct cwpredict_table {
    struct cwpredict_channel channels[CWPREDICT_CHANNELS];
};

static inline void cwpredict_init(struct cwpredict_table *t)
{
    if (!t) return;
    memset(t, 0, sizeof(*t));
}

static inline struct cwpredict_channel *
cwpredict_find(struct cwpredict_table *t, uint16_t caid,
               uint32_t provid, uint16_t sid)
{
    int i;
    if (!t) return 0;
    for (i = 0; i < CWPREDICT_CHANNELS; i++) {
        struct cwpredict_channel *c = &t->channels[i];
        if (c->used && c->caid == caid && c->provid == provid && c->sid == sid) {
            return c;
        }
    }
    return 0;
}

static inline struct cwpredict_channel *
cwpredict_alloc(struct cwpredict_table *t, uint16_t caid,
                uint32_t provid, uint16_t sid, uint32_t now)
{
    int i, oldest = -1;
    uint32_t oldest_age = 0;
    struct cwpredict_channel *c;

    if (!t) return 0;
    c = cwpredict_find(t, caid, provid, sid);
    if (c) return c;

    for (i = 0; i < CWPREDICT_CHANNELS; i++) {
        uint32_t age;
        if (!t->channels[i].used) {
            c = &t->channels[i];
            c->caid = caid;
            c->provid = provid;
            c->sid = sid;
            c->used = 1;
            return c;
        }
        age = tick32_elapsed(now, t->channels[i].last_change);
        if (oldest < 0 || age > oldest_age) {
            oldest = i;
            oldest_age = age;
        }
    }
    /* Reuse the channel with greatest elapsed age. */
    c = &t->channels[(oldest < 0) ? 0 : oldest];
    memset(c, 0, sizeof(*c));
    c->caid = caid;
    c->provid = provid;
    c->sid = sid;
    c->used = 1;
    return c;
}

/* Record a CW change */
static inline void cwpredict_record_change(struct cwpredict_table *t,
                                            uint16_t caid, uint32_t provid,
                                            uint16_t sid, uint32_t now)
{
    struct cwpredict_channel *c;
    if (!t) return;
    c = cwpredict_alloc(t, caid, provid, sid, now);
    if (!c) return;

    c->changes[c->change_head] = now;
    c->change_head = (c->change_head + 1) % CWPREDICT_HISTORY;
    if (c->change_count < CWPREDICT_HISTORY) c->change_count++;
    c->last_change = now;
}

/* Compute the average cycle in the caller's tick units (R22 passes ms). */
static inline uint32_t cwpredict_cycle_ms(const struct cwpredict_table *t,
                                           uint16_t caid, uint32_t provid,
                                           uint16_t sid)
{
    const struct cwpredict_channel *c;
    uint64_t total_interval = 0;
    uint32_t start;
    int intervals = 0;
    int i;

    if (!t) return 0;
    c = cwpredict_find((struct cwpredict_table *)t, caid, provid, sid);
    if (!c || c->change_count < 2) return 0;

    /* Walk ring entries in insertion order; zero is a valid tick value. */
    start = (uint32_t)((c->change_head - c->change_count +
                        CWPREDICT_HISTORY) % CWPREDICT_HISTORY);
    for (i = 1; i < c->change_count; i++) {
        uint32_t prev_time = c->changes[(start + (uint32_t)i - 1u) % CWPREDICT_HISTORY];
        uint32_t curr_time = c->changes[(start + (uint32_t)i) % CWPREDICT_HISTORY];
        uint32_t interval = tick32_elapsed(curr_time, prev_time);
        /* Intervals at or above half-range are not orderable as one tick era. */
        if (interval > 0 && interval <= 0x7FFFFFFFu) {
            total_interval += interval;
            intervals++;
        }
    }

    if (intervals == 0) return 0;
    return (uint32_t)(total_interval / (uint32_t)intervals);
}

/* Compatibility helper for callers that want whole seconds from ms ticks. */
static inline uint32_t cwpredict_cycle_secs(const struct cwpredict_table *t,
                                              uint16_t caid, uint32_t provid,
                                              uint16_t sid)
{
    return cwpredict_cycle_ms(t, caid, provid, sid) / 1000u;
}

/* Return 1 when a prediction exists, even when its tick value wraps to zero. */
static inline int cwpredict_next_valid(const struct cwpredict_table *t,
                                       uint16_t caid, uint32_t provid,
                                       uint16_t sid, uint32_t *predicted)
{
    const struct cwpredict_channel *c;
    uint32_t cycle;

    if (!t || !predicted) return 0;
    c = cwpredict_find((struct cwpredict_table *)t, caid, provid, sid);
    if (!c || c->change_count == 0) return 0;
    cycle = cwpredict_cycle_ms(t, caid, provid, sid);
    if (cycle == 0) return 0;
    *predicted = c->last_change + cycle;
    return 1;
}

/* Predict when the next CW change will occur, or 0 if no prediction exists. */
static inline uint32_t cwpredict_next(const struct cwpredict_table *t,
                                       uint16_t caid, uint32_t provid,
                                       uint16_t sid)
{
    uint32_t predicted;
    return cwpredict_next_valid(t, caid, provid, sid, &predicted) ? predicted : 0;
}

/* Check if the predicted change is within the early-prefetch interval. */
static inline int cwpredict_should_prefetch(const struct cwpredict_table *t,
                                             uint16_t caid, uint32_t provid,
                                             uint16_t sid, uint32_t now)
{
    uint32_t predicted, until;
    if (!cwpredict_next_valid(t, caid, provid, sid, &predicted)) return 0;
    until = tick32_elapsed(predicted, now);
    return until <= CWPREDICT_EARLY_MS;
}

#endif /* MCS_CWPREDICT_H */
