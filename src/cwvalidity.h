/* cwvalidity.h — CW Validity Window (R22 candidate #39)
 *
 * PURPOSE: dynamically adjust the validity window for CWs based on
 * channel behavior. A stable channel with consistent CWs can have a
 * longer validity window; a channel with frequent changes needs a
 * shorter window to avoid stale CWs.
 *
 * MECHANISM: track CW change rate per channel. Compute optimal validity
 * window = average_interval * safety_factor.
 *
 * CONFIG: automatic — adapts to each channel's behavior.
 *
 * ROLLBACK: remove cwvalidity_window() calls; returns default.
 */
#ifndef MCS_CWVALIDITY_H
#define MCS_CWVALIDITY_H

#include <stdint.h>
#include <string.h>

#define CWVALIDITY_CHANNELS 64
#define CWVALIDITY_HISTORY 10  /* last 10 CW changes */
#define CWVALIDITY_DEFAULT_SECS 10
#define CWVALIDITY_MIN_SECS 3
#define CWVALIDITY_MAX_SECS 30
#define CWVALIDITY_SAFETY_FACTOR 1.5

struct cwvalidity_channel {
    uint16_t caid;
    uint32_t provid;
    uint16_t sid;
    uint32_t last_change;
    uint32_t intervals[CWVALIDITY_HISTORY];
    int interval_count;
    int interval_head;
    uint8_t used;
};

struct cwvalidity_table {
    struct cwvalidity_channel channels[CWVALIDITY_CHANNELS];
};

static inline void cwvalidity_init(struct cwvalidity_table *t)
{
    if (!t) return;
    memset(t, 0, sizeof(*t));
}

static inline struct cwvalidity_channel *
cwvalidity_find(struct cwvalidity_table *t, uint16_t caid,
                uint32_t provid, uint16_t sid)
{
    int i;
    if (!t) return 0;
    for (i = 0; i < CWVALIDITY_CHANNELS; i++) {
        struct cwvalidity_channel *c = &t->channels[i];
        if (c->used && c->caid == caid && c->provid == provid && c->sid == sid) {
            return c;
        }
    }
    return 0;
}

static inline struct cwvalidity_channel *
cwvalidity_alloc(struct cwvalidity_table *t, uint16_t caid,
                 uint32_t provid, uint16_t sid)
{
    int i, oldest = 0;
    struct cwvalidity_channel *c;

    c = cwvalidity_find(t, caid, provid, sid);
    if (c) return c;

    for (i = 0; i < CWVALIDITY_CHANNELS; i++) {
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

/* Record a CW change for a channel */
static inline void cwvalidity_record_change(struct cwvalidity_table *t,
                                             uint16_t caid, uint32_t provid,
                                             uint16_t sid, uint32_t now)
{
    struct cwvalidity_channel *c;
    uint32_t interval;

    if (!t) return;
    c = cwvalidity_alloc(t, caid, provid, sid);
    if (!c) return;

    if (c->last_change > 0 && now > c->last_change) {
        interval = now - c->last_change;
        c->intervals[c->interval_head] = interval;
        c->interval_head = (c->interval_head + 1) % CWVALIDITY_HISTORY;
        if (c->interval_count < CWVALIDITY_HISTORY) {
            c->interval_count++;
        }
    }
    c->last_change = now;
}

/* Compute the optimal validity window for a channel */
static inline uint32_t cwvalidity_window(const struct cwvalidity_table *t,
                                          uint16_t caid, uint32_t provid,
                                          uint16_t sid)
{
    const struct cwvalidity_channel *c;
    uint32_t sum = 0;
    uint32_t avg;
    uint32_t window;
    int i;

    if (!t) return CWVALIDITY_DEFAULT_SECS;
    c = cwvalidity_find((struct cwvalidity_table *)t, caid, provid, sid);
    if (!c || c->interval_count < 2) return CWVALIDITY_DEFAULT_SECS;

    /* Compute average interval */
    for (i = 0; i < c->interval_count; i++) {
        sum += c->intervals[i];
    }
    avg = sum / (uint32_t)c->interval_count;

    /* Apply safety factor */
    window = (uint32_t)((double)avg * CWVALIDITY_SAFETY_FACTOR);

    /* Clamp to [min, max] */
    if (window < CWVALIDITY_MIN_SECS) window = CWVALIDITY_MIN_SECS;
    if (window > CWVALIDITY_MAX_SECS) window = CWVALIDITY_MAX_SECS;

    return window;
}

/* Check if a CW is still valid for a channel */
static inline int cwvalidity_is_valid(const struct cwvalidity_table *t,
                                       uint16_t caid, uint32_t provid,
                                       uint16_t sid, uint32_t cw_age)
{
    uint32_t window = cwvalidity_window(t, caid, provid, sid);
    return (cw_age <= window) ? 1 : 0;
}

#endif /* MCS_CWVALIDITY_H */
