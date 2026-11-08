/* cachewarm.h — Cache Warming for Popular Channels (R22 candidate #17)
 *
 * PURPOSE: proactively warm the cache for frequently-watched channels
 * so the first zap to a popular channel finds a CW already in cache.
 * This eliminates the "cold start" delay.
 *
 * MECHANISM: track channel popularity from dcwstats, periodically send
 * ECM requests for the top-N channels that don't have cached CWs.
 *
 * CONFIG: CACHE WARM: N (0 = off, 1-20 = top-N channels, default 0).
 *
 * ROLLBACK: remove cachewarm_tick() call; the layer is inert.
 */
#ifndef MCS_CACHEWARM_H
#define MCS_CACHEWARM_H

#include <stdint.h>
#include <string.h>
#include "tick_math.h"

#define CACHEWARM_MAX_CHANNELS 20
#define CACHEWARM_COOLDOWN_SECS 30

struct cachewarm_channel {
    uint16_t caid;
    uint32_t provid;
    uint16_t sid;
    uint32_t last_request;
    uint32_t last_used;
    uint32_t request_count;
    uint8_t has_cached_cw;
    uint8_t last_request_valid;
};

struct cachewarm_table {
    struct cachewarm_channel channels[CACHEWARM_MAX_CHANNELS];
    int count;
    int top_n;
    uint32_t total_requests;
    uint32_t cache_hits;
};

static inline void cachewarm_init(struct cachewarm_table *t, int top_n)
{
    if (!t) return;
    memset(t, 0, sizeof(*t));
    if (top_n < 0) top_n = 0;
    if (top_n > CACHEWARM_MAX_CHANNELS) top_n = CACHEWARM_MAX_CHANNELS;
    t->top_n = top_n;
}

static inline int cachewarm_enabled(const struct cachewarm_table *t)
{
    return t && t->top_n > 0;
}

/* Update channel statistics (call from dcwstats hook) */
static inline void cachewarm_update(struct cachewarm_table *t,
                                     uint16_t caid, uint32_t provid,
                                     uint16_t sid, int has_cache,
                                     uint32_t now)
{
    int i, slot = -1;
    if (!t) return;

    /* Find existing entry */
    for (i = 0; i < t->count; i++) {
        if (t->channels[i].caid == caid &&
            t->channels[i].provid == provid &&
            t->channels[i].sid == sid) {
            slot = i;
            break;
        }
    }

    /* Allocate new slot if not found */
    if (slot < 0) {
        if (t->count < CACHEWARM_MAX_CHANNELS) {
            slot = t->count++;
        } else {
            /* Replace the channel with greatest modulo elapsed age. */
            uint32_t oldest_age = tick32_elapsed(now, t->channels[0].last_used);
            int oldest = 0;
            for (i = 1; i < t->count; i++) {
                uint32_t age = tick32_elapsed(now, t->channels[i].last_used);
                if (age > oldest_age) {
                    oldest_age = age;
                    oldest = i;
                }
            }
            slot = oldest;
        }
        memset(&t->channels[slot], 0, sizeof(t->channels[slot]));
        t->channels[slot].caid = caid;
        t->channels[slot].provid = provid;
        t->channels[slot].sid = sid;
    }

    t->channels[slot].request_count++;
    t->channels[slot].has_cached_cw = has_cache ? 1 : 0;
    t->channels[slot].last_used = now;
}

/* Tick function — call periodically (e.g., every 10 seconds).
 * Returns the number of warm-up requests that should be sent.
 * Caller must iterate and send ECM requests.
 */
static inline int cachewarm_tick(struct cachewarm_table *t,
                                  uint32_t now,
                                  uint16_t *caids, uint32_t *provids,
                                  uint16_t *sids, int max_out)
{
    int i, n = 0;
    if (!cachewarm_enabled(t) || !caids || !provids || !sids || max_out <= 0)
        return 0;

    /* Sort by request_count descending (simple bubble sort) */
    int sorted[CACHEWARM_MAX_CHANNELS];
    for (i = 0; i < t->count; i++) sorted[i] = i;
    for (i = 0; i < t->count - 1; i++) {
        int j;
        for (j = i + 1; j < t->count; j++) {
            if (t->channels[sorted[j]].request_count >
                t->channels[sorted[i]].request_count) {
                int tmp = sorted[i];
                sorted[i] = sorted[j];
                sorted[j] = tmp;
            }
        }
    }

    /* Return top-N channels without cached CWs */
    for (i = 0; i < t->count && n < t->top_n && n < max_out; i++) {
        struct cachewarm_channel *c = &t->channels[sorted[i]];
        if (c->has_cached_cw) continue;
        if (c->last_request_valid &&
            tick32_elapsed(now, c->last_request) < CACHEWARM_COOLDOWN_SECS) continue;

        caids[n] = c->caid;
        provids[n] = c->provid;
        sids[n] = c->sid;
        c->last_request = now;
        c->last_request_valid = 1;
        c->last_used = now;
        t->total_requests++;
        n++;
    }
    return n;
}

/* Mark a warm-up request as successful (CW now in cache) */
static inline void cachewarm_success(struct cachewarm_table *t,
                                      uint16_t caid, uint32_t provid,
                                      uint16_t sid)
{
    int i;
    if (!t) return;
    for (i = 0; i < t->count; i++) {
        if (t->channels[i].caid == caid &&
            t->channels[i].provid == provid &&
            t->channels[i].sid == sid) {
            t->channels[i].has_cached_cw = 1;
            t->cache_hits++;
            return;
        }
    }
}

#endif /* MCS_CACHEWARM_H */
