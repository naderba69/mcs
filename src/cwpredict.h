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
                uint32_t provid, uint16_t sid)
{
    int i, oldest = 0;
    struct cwpredict_channel *c;

    c = cwpredict_find(t, caid, provid, sid);
    if (c) return c;

    for (i = 0; i < CWPREDICT_CHANNELS; i++) {
        if (!t->channels[i].used) {
            c = &t->channels[i];
            c->caid = caid;
            c->provid = provid;
            c->sid = sid;
            c->used = 1;
            return c;
        }
        if (t->channels[i].last_change < t->channels[oldest].last_change) {
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

/* Record a CW change */
static inline void cwpredict_record_change(struct cwpredict_table *t,
                                            uint16_t caid, uint32_t provid,
                                            uint16_t sid, uint32_t now)
{
    struct cwpredict_channel *c;
    if (!t) return;
    c = cwpredict_alloc(t, caid, provid, sid);
    if (!c) return;

    c->changes[c->change_head] = now;
    c->change_head = (c->change_head + 1) % CWPREDICT_HISTORY;
    if (c->change_count < CWPREDICT_HISTORY) c->change_count++;
    c->last_change = now;
}

/* Compute the average cycle time (seconds between CW changes) */
static inline uint32_t cwpredict_cycle_secs(const struct cwpredict_table *t,
                                              uint16_t caid, uint32_t provid,
                                              uint16_t sid)
{
    const struct cwpredict_channel *c;
    uint32_t total_interval = 0;
    int intervals = 0;
    int i;

    if (!t) return 0;
    c = cwpredict_find((struct cwpredict_table *)t, caid, provid, sid);
    if (!c || c->change_count < 2) return 0;

    /* Calculate intervals between consecutive changes */
    for (i = 1; i < c->change_count; i++) {
        uint32_t prev_time = c->changes[i - 1];
        uint32_t curr_time = c->changes[i];
        if (curr_time > prev_time && prev_time > 0) {
            total_interval += (curr_time - prev_time);
            intervals++;
        }
    }

    if (intervals == 0) return 0;
    return total_interval / (uint32_t)intervals;
}

/* Predict when the next CW change will occur.
 * Returns: predicted timestamp, or 0 if no prediction available.
 */
static inline uint32_t cwpredict_next(const struct cwpredict_table *t,
                                       uint16_t caid, uint32_t provid,
                                       uint16_t sid)
{
    const struct cwpredict_channel *c;
    uint32_t cycle;

    if (!t) return 0;
    c = cwpredict_find((struct cwpredict_table *)t, caid, provid, sid);
    if (!c || c->last_change == 0) return 0;

    cycle = cwpredict_cycle_secs(t, caid, provid, sid);
    if (cycle == 0) return 0;

    return c->last_change + cycle;
}

/* Check if it's time to prefetch (CWPREDICT_EARLY_MS before predicted expiry) */
static inline int cwpredict_should_prefetch(const struct cwpredict_table *t,
                                             uint16_t caid, uint32_t provid,
                                             uint16_t sid, uint32_t now)
{
    uint32_t predicted = cwpredict_next(t, caid, provid, sid);
    if (predicted == 0) return 0;
    /* Prefetch if we're within CWPREDICT_EARLY_MS of the predicted change */
    /* Convert to milliseconds for comparison */
    if ((now * 1000) + CWPREDICT_EARLY_MS >= (predicted * 1000)) return 1;
    return 0;
}

#endif /* MCS_CWPREDICT_H */
