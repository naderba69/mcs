/* autoswitch.h — Automatic Source Switch (R22 candidate #32)
 *
 * PURPOSE: automatically switch to a better source when the current
 * one is performing poorly. Track per-source performance per channel
 * and switch when a better source is available.
 *
 * MECHANISM: maintain a performance score for each (source, channel).
 * When the current source's score drops below a threshold, switch to
 * the best alternative.
 *
 * CONFIG: AUTO SWITCH: ON (default OFF = manual source selection).
 *
 * ROLLBACK: remove autoswitch_should() calls; auto-switch is inert.
 */
#ifndef MCS_AUTOSWITCH_H
#define MCS_AUTOSWITCH_H

#include <stdint.h>
#include <string.h>
#include "tick_math.h"

#define AUTOSWITCH_SOURCES 8
#define AUTOSWITCH_CHANNELS 32
#define AUTOSWITCH_THRESHOLD 40  /* switch if score < 40% */
#define AUTOSWITCH_MIN_SAMPLES 5  /* need at least 5 samples */
#define AUTOSWITCH_COOLDOWN 10  /* seconds between switches */

struct autoswitch_source {
    int source_id;
    uint32_t deliveries;
    uint32_t successes;
    uint32_t failures;
    uint32_t last_used;
    uint8_t used;
};

struct autoswitch_channel {
    uint16_t caid;
    uint32_t provid;
    uint16_t sid;
    struct autoswitch_source sources[AUTOSWITCH_SOURCES];
    int current_source;
    uint32_t last_switch;
    uint32_t last_used;
    uint32_t switch_count;
    uint8_t used;
};

struct autoswitch_table {
    struct autoswitch_channel channels[AUTOSWITCH_CHANNELS];
    int enabled;
    uint32_t total_switches;
};

static inline void autoswitch_init(struct autoswitch_table *t, int enabled)
{
    if (!t) return;
    memset(t, 0, sizeof(*t));
    t->enabled = enabled;
}

static inline int autoswitch_enabled(const struct autoswitch_table *t)
{
    return t && t->enabled;
}

static inline struct autoswitch_channel *
autoswitch_find(struct autoswitch_table *t, uint16_t caid,
                uint32_t provid, uint16_t sid)
{
    int i;
    if (!t) return 0;
    for (i = 0; i < AUTOSWITCH_CHANNELS; i++) {
        struct autoswitch_channel *c = &t->channels[i];
        if (c->used && c->caid == caid && c->provid == provid && c->sid == sid) {
            return c;
        }
    }
    return 0;
}

static inline struct autoswitch_channel *
autoswitch_alloc(struct autoswitch_table *t, uint16_t caid,
                 uint32_t provid, uint16_t sid, uint32_t now)
{
    int i, oldest = 0;
    struct autoswitch_channel *c;

    c = autoswitch_find(t, caid, provid, sid);
    if (c) {
        c->last_used = now;
        return c;
    }

    for (i = 0; i < AUTOSWITCH_CHANNELS; i++) {
        if (!t->channels[i].used) {
            c = &t->channels[i];
            c->caid = caid;
            c->provid = provid;
            c->sid = sid;
            c->current_source = -1;
            c->last_used = now;
            c->used = 1;
            return c;
        }
    }
    /* Reuse the channel with greatest modulo elapsed age. */
    {
        uint32_t oldest_age = tick32_elapsed(now, t->channels[0].last_used);
        for (i = 1; i < AUTOSWITCH_CHANNELS; i++) {
            uint32_t age = tick32_elapsed(now, t->channels[i].last_used);
            if (age > oldest_age) {
                oldest_age = age;
                oldest = i;
            }
        }
    }
    /* Reuse oldest */
    c = &t->channels[oldest];
    memset(c, 0, sizeof(*c));
    c->caid = caid;
    c->provid = provid;
    c->sid = sid;
    c->current_source = -1;
    c->last_used = now;
    c->used = 1;
    return c;
}

static inline struct autoswitch_source *
autoswitch_find_source(struct autoswitch_channel *c, int source_id)
{
    int i;
    for (i = 0; i < AUTOSWITCH_SOURCES; i++) {
        if (c->sources[i].used && c->sources[i].source_id == source_id) {
            return &c->sources[i];
        }
    }
    return 0;
}

static inline struct autoswitch_source *
autoswitch_alloc_source(struct autoswitch_channel *c, int source_id,
                        uint32_t now)
{
    int i, oldest = 0;
    struct autoswitch_source *s;

    s = autoswitch_find_source(c, source_id);
    if (s) {
        s->last_used = now;
        return s;
    }

    for (i = 0; i < AUTOSWITCH_SOURCES; i++) {
        if (!c->sources[i].used) {
            s = &c->sources[i];
            s->source_id = source_id;
            s->last_used = now;
            s->used = 1;
            return s;
        }
    }
    /* Reuse the source with greatest modulo elapsed age. */
    {
        uint32_t oldest_age = tick32_elapsed(now, c->sources[0].last_used);
        for (i = 1; i < AUTOSWITCH_SOURCES; i++) {
            uint32_t age = tick32_elapsed(now, c->sources[i].last_used);
            if (age > oldest_age) {
                oldest_age = age;
                oldest = i;
            }
        }
    }
    /* Reuse oldest */
    s = &c->sources[oldest];
    memset(s, 0, sizeof(*s));
    s->source_id = source_id;
    s->last_used = now;
    s->used = 1;
    return s;
}

/* Record a delivery result for a source */
static inline void autoswitch_record(struct autoswitch_table *t,
                                      uint16_t caid, uint32_t provid,
                                      uint16_t sid, int source_id,
                                      int success, uint32_t now)
{
    struct autoswitch_channel *c;
    struct autoswitch_source *s;

    if (!autoswitch_enabled(t)) return;
    c = autoswitch_alloc(t, caid, provid, sid, now);
    if (!c) return;
    s = autoswitch_alloc_source(c, source_id, now);
    if (!s) return;

    s->deliveries++;
    if (success) s->successes++;
    else s->failures++;
}

/* Check if we should switch to a different source.
 * Returns: new source_id to switch to, or -1 if no switch needed.
 */
static inline int autoswitch_should(struct autoswitch_table *t,
                                     uint16_t caid, uint32_t provid,
                                     uint16_t sid, uint32_t now)
{
    struct autoswitch_channel *c;
    struct autoswitch_source *current = 0;
    struct autoswitch_source *best = 0;
    int current_score, best_score = -1;
    int i;

    if (!autoswitch_enabled(t)) return -1;
    c = autoswitch_find(t, caid, provid, sid);
    if (!c) return -1;
    c->last_used = now;
    if (c->current_source < 0) return -1;

    /* Check cooldown; switch_count distinguishes tick-zero from no switch. */
    if (c->switch_count > 0 &&
        tick32_elapsed(now, c->last_switch) < AUTOSWITCH_COOLDOWN) {
        return -1;
    }

    /* Get current source score */
    current = autoswitch_find_source(c, c->current_source);
    if (!current || current->deliveries < AUTOSWITCH_MIN_SAMPLES) return -1;
    current->last_used = now;
    current_score = (int)((current->successes * 100) / current->deliveries);

    /* If current source is good enough, no switch */
    if (current_score >= AUTOSWITCH_THRESHOLD) return -1;

    /* Find best alternative */
    for (i = 0; i < AUTOSWITCH_SOURCES; i++) {
        struct autoswitch_source *s = &c->sources[i];
        int score;
        if (!s->used) continue;
        s->last_used = now;
        if (s->source_id == c->current_source) continue;
        if (s->deliveries < AUTOSWITCH_MIN_SAMPLES) continue;
        score = (int)((s->successes * 100) / s->deliveries);
        if (score > best_score) {
            best_score = score;
            best = s;
        }
    }

    if (!best || best_score <= current_score) return -1;

    /* Switch! */
    c->current_source = best->source_id;
    c->last_switch = now;
    c->last_used = now;
    c->switch_count++;
    t->total_switches++;
    return best->source_id;
}

/* Set the current source for a channel */
static inline void autoswitch_set_current(struct autoswitch_table *t,
                                           uint16_t caid, uint32_t provid,
                                           uint16_t sid, int source_id,
                                           uint32_t now)
{
    struct autoswitch_channel *c;
    if (!t) return;
    c = autoswitch_alloc(t, caid, provid, sid, now);
    if (c) {
        c->current_source = source_id;
        c->last_used = now;
    }
}

#endif /* MCS_AUTOSWITCH_H */
