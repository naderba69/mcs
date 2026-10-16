/* ecmcache.h — ECM Caching (R22 candidate #19)
 *
 * PURPOSE: cache ECM requests to reduce round-trips to servers. When
 * the same ECM is requested multiple times (e.g., client retry,
 * multiple clients watching the same channel), serve the cached ECM
 * response instead of re-querying servers.
 *
 * MECHANISM: LRU cache of (ecm_hash → cw) mappings. When an ECM
 * arrives, check the cache first. If found, serve immediately. If
 * not, query server and cache the result.
 *
 * CONFIG: ECM CACHE: N (0 = off, 1-1000 = cache size).
 *
 * ROLLBACK: remove ecmcache_lookup() calls; ECM cache is inert.
 */
#ifndef MCS_ECMCACHE_H
#define MCS_ECMCACHE_H

#include <stdint.h>
#include <string.h>

#define ECMCACHE_MAX_SIZE 1000
#define ECMCACHE_DEFAULT_SIZE 100
#define ECMCACHE_TTL_SECS 30

struct ecmcache_entry {
    uint8_t ecm_hash[16];
    uint8_t cw[16];
    uint16_t caid;
    uint32_t provid;
    uint16_t sid;
    uint32_t cached_at;
    uint32_t hit_count;
    uint8_t used;
};

struct ecmcache_table {
    struct ecmcache_entry *entries;
    int size;
    int count;
    uint32_t total_lookups;
    uint32_t total_hits;
    uint32_t total_misses;
    uint32_t total_stores;
};

static inline void ecmcache_init(struct ecmcache_table *t, int size)
{
    if (!t) return;
    memset(t, 0, sizeof(*t));
    if (size <= 0) size = 0;
    if (size > ECMCACHE_MAX_SIZE) size = ECMCACHE_MAX_SIZE;
    t->size = size;
    /* Note: caller must allocate entries array */
}

static inline int ecmcache_enabled(const struct ecmcache_table *t)
{
    return t && t->size > 0 && t->entries;
}

/* Lookup an ECM in the cache.
 * Returns: 1 if found (cw filled), 0 if not found.
 */
static inline int ecmcache_lookup(struct ecmcache_table *t,
                                   const uint8_t ecm_hash[16],
                                   uint32_t now, uint8_t out_cw[16])
{
    int i;
    struct ecmcache_entry *e;

    if (!ecmcache_enabled(t) || !ecm_hash || !out_cw) return 0;
    t->total_lookups++;

    for (i = 0; i < t->size; i++) {
        e = &t->entries[i];
        if (!e->used) continue;
        if (memcmp(e->ecm_hash, ecm_hash, 16) != 0) continue;
        /* Check TTL */
        if ((now - e->cached_at) > ECMCACHE_TTL_SECS) {
            e->used = 0;
            t->total_misses++;
            return 0;
        }
        /* Hit! */
        e->hit_count++;
        t->total_hits++;
        memcpy(out_cw, e->cw, 16);
        return 1;
    }
    t->total_misses++;
    return 0;
}

/* Store an ECM→CW mapping in the cache */
static inline void ecmcache_store(struct ecmcache_table *t,
                                   const uint8_t ecm_hash[16],
                                   const uint8_t cw[16],
                                   uint16_t caid, uint32_t provid,
                                   uint16_t sid, uint32_t now)
{
    int i, free_slot = -1, oldest = -1;
    struct ecmcache_entry *e;

    if (!ecmcache_enabled(t) || !ecm_hash || !cw) return;

    /* Check if already cached */
    for (i = 0; i < t->size; i++) {
        e = &t->entries[i];
        if (e->used && memcmp(e->ecm_hash, ecm_hash, 16) == 0) {
            /* Update existing */
            memcpy(e->cw, cw, 16);
            e->cached_at = now;
            return;
        }
    }

    /* Find free slot or evict oldest */
    for (i = 0; i < t->size; i++) {
        if (!t->entries[i].used) {
            free_slot = i;
            break;
        }
        if (oldest < 0 || t->entries[i].cached_at < t->entries[oldest].cached_at) {
            oldest = i;
        }
    }

    if (free_slot < 0) {
        free_slot = oldest;  /* evict oldest */
    }

    e = &t->entries[free_slot];
    memcpy(e->ecm_hash, ecm_hash, 16);
    memcpy(e->cw, cw, 16);
    e->caid = caid;
    e->provid = provid;
    e->sid = sid;
    e->cached_at = now;
    e->hit_count = 0;
    e->used = 1;
    if (t->count < t->size) t->count++;
    t->total_stores++;
}

/* Expire old entries */
static inline void ecmcache_expire(struct ecmcache_table *t, uint32_t now)
{
    int i;
    if (!ecmcache_enabled(t)) return;
    for (i = 0; i < t->size; i++) {
        struct ecmcache_entry *e = &t->entries[i];
        if (e->used && (now - e->cached_at) > ECMCACHE_TTL_SECS) {
            e->used = 0;
            t->count--;
        }
    }
}

/* Get cache hit rate (0.0 to 1.0) */
static inline double ecmcache_hit_rate(const struct ecmcache_table *t)
{
    if (!t || t->total_lookups == 0) return 0.0;
    return (double)t->total_hits / (double)t->total_lookups;
}

/* Get cache utilization (0 to size) */
static inline int ecmcache_utilization(const struct ecmcache_table *t)
{
    if (!t) return 0;
    return t->count;
}

#endif /* MCS_ECMCACHE_H */
