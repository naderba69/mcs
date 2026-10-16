/* cwemergency.h — Emergency CW Source (R22 candidate #40)
 *
 * PURPOSE: when all normal sources fail for a channel, fall back to
 * an emergency source (e.g., a local card reader, a premium server,
 * or a backup peer). This is the "last resort" to avoid black screens.
 *
 * MECHANISM: track consecutive failures per channel. When failures
 * exceed a threshold, trigger emergency source.
 *
 * CONFIG: CW EMERGENCY: ON (default OFF).
 *
 * ROLLBACK: remove cwemergency_trigger() calls; emergency is inert.
 */
#ifndef MCS_CWEMERGENCY_H
#define MCS_CWEMERGENCY_H

#include <stdint.h>
#include <string.h>

#define CWEMERGENCY_CHANNELS 32
#define CWEMERGENCY_THRESHOLD 3  /* consecutive failures before emergency */
#define CWEMERGENCY_COOLDOWN 30  /* seconds between emergency triggers */

struct cwemergency_channel {
    uint16_t caid;
    uint32_t provid;
    uint16_t sid;
    uint32_t consecutive_failures;
    uint32_t last_trigger;
    uint32_t total_triggers;
    uint8_t used;
};

struct cwemergency_table {
    struct cwemergency_channel channels[CWEMERGENCY_CHANNELS];
    int enabled;
    uint32_t total_triggers;
};

static inline void cwemergency_init(struct cwemergency_table *t, int enabled)
{
    if (!t) return;
    memset(t, 0, sizeof(*t));
    t->enabled = enabled;
}

static inline int cwemergency_enabled(const struct cwemergency_table *t)
{
    return t && t->enabled;
}

static inline struct cwemergency_channel *
cwemergency_find(struct cwemergency_table *t, uint16_t caid,
                 uint32_t provid, uint16_t sid)
{
    int i;
    if (!t) return 0;
    for (i = 0; i < CWEMERGENCY_CHANNELS; i++) {
        struct cwemergency_channel *c = &t->channels[i];
        if (c->used && c->caid == caid && c->provid == provid && c->sid == sid) {
            return c;
        }
    }
    return 0;
}

static inline struct cwemergency_channel *
cwemergency_alloc(struct cwemergency_table *t, uint16_t caid,
                  uint32_t provid, uint16_t sid)
{
    int i, oldest = 0;
    struct cwemergency_channel *c;

    c = cwemergency_find(t, caid, provid, sid);
    if (c) return c;

    for (i = 0; i < CWEMERGENCY_CHANNELS; i++) {
        if (!t->channels[i].used) {
            c = &t->channels[i];
            c->caid = caid;
            c->provid = provid;
            c->sid = sid;
            c->used = 1;
            return c;
        }
        if (t->channels[i].total_triggers < t->channels[oldest].total_triggers) {
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

/* Record a failure for a channel */
static inline void cwemergency_record_failure(struct cwemergency_table *t,
                                                uint16_t caid, uint32_t provid,
                                                uint16_t sid)
{
    struct cwemergency_channel *c;
    if (!cwemergency_enabled(t)) return;
    c = cwemergency_alloc(t, caid, provid, sid);
    if (!c) return;
    c->consecutive_failures++;
}

/* Record a success — reset failure counter */
static inline void cwemergency_record_success(struct cwemergency_table *t,
                                               uint16_t caid, uint32_t provid,
                                               uint16_t sid)
{
    struct cwemergency_channel *c;
    if (!t) return;
    c = cwemergency_find(t, caid, provid, sid);
    if (!c) return;
    c->consecutive_failures = 0;
}

/* Check if emergency source should be triggered.
 * Returns: 1 if triggered, 0 otherwise.
 */
static inline int cwemergency_trigger(struct cwemergency_table *t,
                                       uint16_t caid, uint32_t provid,
                                       uint16_t sid, uint32_t now)
{
    struct cwemergency_channel *c;

    if (!cwemergency_enabled(t)) return 0;
    c = cwemergency_find(t, caid, provid, sid);
    if (!c) return 0;

    /* Check threshold */
    if (c->consecutive_failures < CWEMERGENCY_THRESHOLD) return 0;

    /* Check cooldown */
    if (c->last_trigger > 0 && (now - c->last_trigger) < CWEMERGENCY_COOLDOWN) {
        return 0;
    }

    /* Trigger! */
    c->last_trigger = now;
    c->total_triggers++;
    t->total_triggers++;
    c->consecutive_failures = 0;  /* reset after trigger */
    return 1;
}

#endif /* MCS_CWEMERGENCY_H */
