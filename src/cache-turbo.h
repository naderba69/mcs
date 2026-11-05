/*
 * Cache Turbo - High-Performance Hash Table Cache
 * 
 * Replaces linear O(n) search with O(1) hash table lookup
 * Expected improvement: 100x faster cache lookups
 * 
 * Features:
 * - FNV-1a hash function (fast, low collision)
 * - 512 buckets with chaining
 * - Auto-eviction when full
 * - Statistics tracking
 * - Thread-safe
 */

#ifndef CACHE_TURBO_H
#define CACHE_TURBO_H

#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include "tick_math.h"

// ============================================================================
// Configuration
// ============================================================================

#define CACHE_TURBO_BUCKETS 512  // Power of 2 for fast modulo
#define CACHE_TURBO_MAX_ENTRIES 2048
#define CACHE_TURBO_TTL_SECONDS 30
#define CACHE_TURBO_TTL_MS (CACHE_TURBO_TTL_SECONDS * 1000u)

// ============================================================================
// Data Structures
// ============================================================================

struct cache_turbo_entry {
    // Key
    uint16_t caid;
    uint32_t provid;
    uint16_t sid;
    
    // Value
    uint8_t cw[16];
    uint8_t ecm_hash[16];
    
    // Metadata
    uint32_t cached_at;
    uint32_t hit_count;
    uint32_t last_hit;
    uint8_t cw_cycle;
    uint8_t source_type;  // DCW_SOURCE_*
    uint32_t source_id;
    uint8_t used;
    
    // Linked list for collision handling
    struct cache_turbo_entry *next;
};

struct cache_turbo_stats {
    uint64_t total_lookups;
    uint64_t total_hits;
    uint64_t total_misses;
    uint64_t total_stores;
    uint64_t total_evictions;
    uint64_t total_expired;
    
    uint32_t current_entries;
    uint32_t peak_entries;
    
    // Performance metrics
    uint32_t avg_lookup_time_us;  // microseconds
    uint32_t max_lookup_time_us;
};

// ============================================================================
// Global State
// ============================================================================

static struct cache_turbo_entry *cache_turbo_buckets[CACHE_TURBO_BUCKETS];
static struct cache_turbo_entry cache_turbo_pool[CACHE_TURBO_MAX_ENTRIES];
static int cache_turbo_pool_used = 0;
static struct cache_turbo_stats cache_turbo_stats;
static pthread_mutex_t cache_turbo_lock = PTHREAD_MUTEX_INITIALIZER;
static int cache_turbo_enabled = 0;

// ============================================================================
// Hash Function (FNV-1a)
// ============================================================================

/*
 * FNV-1a hash - fast and good distribution
 * Reference: http://www.isthe.com/chongo/tech/comp/fnv/
 */
static inline uint32_t cache_turbo_hash(uint16_t caid, uint32_t provid, uint16_t sid)
{
    uint32_t hash = 2166136261u;  // FNV offset basis
    
    // Hash CAID (2 bytes)
    hash ^= (caid >> 8) & 0xFF;
    hash *= 16777619u;  // FNV prime
    hash ^= caid & 0xFF;
    hash *= 16777619u;
    
    // Hash PROVID (4 bytes)
    hash ^= (provid >> 24) & 0xFF;
    hash *= 16777619u;
    hash ^= (provid >> 16) & 0xFF;
    hash *= 16777619u;
    hash ^= (provid >> 8) & 0xFF;
    hash *= 16777619u;
    hash ^= provid & 0xFF;
    hash *= 16777619u;
    
    // Hash SID (2 bytes)
    hash ^= (sid >> 8) & 0xFF;
    hash *= 16777619u;
    hash ^= sid & 0xFF;
    hash *= 16777619u;
    
    // Fast modulo using bitwise AND (buckets is power of 2)
    return hash & (CACHE_TURBO_BUCKETS - 1);
}

// ============================================================================
// Pool Management
// ============================================================================

static struct cache_turbo_entry* cache_turbo_alloc_entry(uint32_t now)
{
    int i;

    /* Reuse a previously removed or expired pool slot first. */
    for (i = 0; i < cache_turbo_pool_used; i++) {
        if (!cache_turbo_pool[i].used) {
            memset(&cache_turbo_pool[i], 0, sizeof(cache_turbo_pool[i]));
            return &cache_turbo_pool[i];
        }
    }

    if (cache_turbo_pool_used < CACHE_TURBO_MAX_ENTRIES) {
        struct cache_turbo_entry *entry = &cache_turbo_pool[cache_turbo_pool_used++];
        memset(entry, 0, sizeof(*entry));
        return entry;
    }

    /* Pool full - evict the active entry with greatest modulo elapsed age. */
    {
        struct cache_turbo_entry *oldest = NULL;
        uint32_t oldest_age = 0;
        for (i = 0; i < cache_turbo_pool_used; i++) {
            uint32_t age;
            if (!cache_turbo_pool[i].used) continue;
            age = tick32_elapsed(now, cache_turbo_pool[i].cached_at);
            if (!oldest || age > oldest_age) {
                oldest = &cache_turbo_pool[i];
                oldest_age = age;
            }
        }

        if (oldest) {
            uint32_t bucket = cache_turbo_hash(oldest->caid, oldest->provid,
                                               oldest->sid);
            struct cache_turbo_entry **pp = &cache_turbo_buckets[bucket];
            while (*pp) {
                if (*pp == oldest) {
                    *pp = oldest->next;
                    break;
                }
                pp = &(*pp)->next;
            }
            cache_turbo_stats.total_evictions++;
            if (cache_turbo_stats.current_entries > 0)
                cache_turbo_stats.current_entries--;
            memset(oldest, 0, sizeof(*oldest));
            return oldest;
        }
    }

    return NULL;
}

// ============================================================================
// Core Functions
// ============================================================================

/*
 * Initialize cache turbo
 */
static void cache_turbo_init(void)
{
    memset(cache_turbo_buckets, 0, sizeof(cache_turbo_buckets));
    memset(cache_turbo_pool, 0, sizeof(cache_turbo_pool));
    memset(&cache_turbo_stats, 0, sizeof(cache_turbo_stats));
    cache_turbo_pool_used = 0;
    cache_turbo_enabled = 1;
}

/*
 * Lookup CW in cache - O(1) average case
 * Returns: 1 if found (cw filled), 0 if not found
 */
static int cache_turbo_lookup(uint16_t caid, uint32_t provid, uint16_t sid,
                               uint8_t cw[16], uint32_t now)
{
    if (!cache_turbo_enabled) return 0;
    
    pthread_mutex_lock(&cache_turbo_lock);
    
    cache_turbo_stats.total_lookups++;
    
    uint32_t bucket = cache_turbo_hash(caid, provid, sid);
    struct cache_turbo_entry **pp = &cache_turbo_buckets[bucket];
    
    // Walk the chain
    while (*pp) {
        struct cache_turbo_entry *entry = *pp;
        if (entry->caid == caid && entry->provid == provid && entry->sid == sid) {
            // TTL and now use modulo millisecond ticks.
            if (tick32_elapsed(now, entry->cached_at) > CACHE_TURBO_TTL_MS) {
                // Remove expired entry now, rather than holding a dead pool slot.
                *pp = entry->next;
                memset(entry, 0, sizeof(*entry));
                if (cache_turbo_stats.current_entries > 0)
                    cache_turbo_stats.current_entries--;
                cache_turbo_stats.total_expired++;
                pthread_mutex_unlock(&cache_turbo_lock);
                return 0;
            }
            
            // Hit!
            memcpy(cw, entry->cw, 16);
            entry->hit_count++;
            entry->last_hit = now;
            cache_turbo_stats.total_hits++;
            
            pthread_mutex_unlock(&cache_turbo_lock);
            return 1;
        }
        pp = &entry->next;
    }
    
    // Miss
    cache_turbo_stats.total_misses++;
    pthread_mutex_unlock(&cache_turbo_lock);
    return 0;
}

/*
 * Store CW in cache - O(1) average case
 */
static void cache_turbo_store(uint16_t caid, uint32_t provid, uint16_t sid,
                               const uint8_t cw[16], const uint8_t ecm_hash[16],
                               uint8_t cw_cycle, uint8_t source_type, uint32_t source_id,
                               uint32_t now)
{
    if (!cache_turbo_enabled) return;
    
    pthread_mutex_lock(&cache_turbo_lock);
    
    uint32_t bucket = cache_turbo_hash(caid, provid, sid);
    
    // Check if already exists
    struct cache_turbo_entry *entry = cache_turbo_buckets[bucket];
    while (entry) {
        if (entry->caid == caid && entry->provid == provid && entry->sid == sid) {
            // Update existing
            memcpy(entry->cw, cw, 16);
            if (ecm_hash) memcpy(entry->ecm_hash, ecm_hash, 16);
            entry->cached_at = now;
            entry->cw_cycle = cw_cycle;
            entry->source_type = source_type;
            entry->source_id = source_id;
            entry->hit_count = 0;
            
            pthread_mutex_unlock(&cache_turbo_lock);
            return;
        }
        entry = entry->next;
    }
    
    // Allocate new entry
    entry = cache_turbo_alloc_entry(now);
    if (!entry) {
        pthread_mutex_unlock(&cache_turbo_lock);
        return;
    }
    
    // Fill entry
    entry->caid = caid;
    entry->provid = provid;
    entry->sid = sid;
    memcpy(entry->cw, cw, 16);
    if (ecm_hash) memcpy(entry->ecm_hash, ecm_hash, 16);
    entry->cached_at = now;
    entry->hit_count = 0;
    entry->last_hit = 0;
    entry->cw_cycle = cw_cycle;
    entry->source_type = source_type;
    entry->source_id = source_id;
    entry->used = 1;
    
    // Add to bucket chain (prepend)
    entry->next = cache_turbo_buckets[bucket];
    cache_turbo_buckets[bucket] = entry;
    
    cache_turbo_stats.total_stores++;
    cache_turbo_stats.current_entries++;
    if (cache_turbo_stats.current_entries > cache_turbo_stats.peak_entries) {
        cache_turbo_stats.peak_entries = cache_turbo_stats.current_entries;
    }
    
    pthread_mutex_unlock(&cache_turbo_lock);
}

/*
 * Remove entry from cache
 */
static void cache_turbo_remove(uint16_t caid, uint32_t provid, uint16_t sid)
{
    if (!cache_turbo_enabled) return;
    
    pthread_mutex_lock(&cache_turbo_lock);
    
    uint32_t bucket = cache_turbo_hash(caid, provid, sid);
    struct cache_turbo_entry **pp = &cache_turbo_buckets[bucket];
    
    while (*pp) {
        struct cache_turbo_entry *entry = *pp;
        if (entry->caid == caid && entry->provid == provid && entry->sid == sid) {
            *pp = entry->next;
            memset(entry, 0, sizeof(*entry));
            if (cache_turbo_stats.current_entries > 0)
                cache_turbo_stats.current_entries--;
            pthread_mutex_unlock(&cache_turbo_lock);
            return;
        }
        pp = &(*pp)->next;
    }
    
    pthread_mutex_unlock(&cache_turbo_lock);
}

/*
 * Cleanup expired entries
 */
static void cache_turbo_cleanup(uint32_t now)
{
    if (!cache_turbo_enabled) return;
    
    pthread_mutex_lock(&cache_turbo_lock);
    
    int i;
    for (i = 0; i < CACHE_TURBO_BUCKETS; i++) {
        struct cache_turbo_entry **pp = &cache_turbo_buckets[i];
        while (*pp) {
            struct cache_turbo_entry *entry = *pp;
            if (tick32_elapsed(now, entry->cached_at) > CACHE_TURBO_TTL_MS) {
                *pp = entry->next;
                memset(entry, 0, sizeof(*entry));
                if (cache_turbo_stats.current_entries > 0)
                    cache_turbo_stats.current_entries--;
                cache_turbo_stats.total_expired++;
            } else {
                pp = &(*pp)->next;
            }
        }
    }
    
    pthread_mutex_unlock(&cache_turbo_lock);
}

/*
 * Get statistics
 */
static void cache_turbo_get_stats(struct cache_turbo_stats *out)
{
    pthread_mutex_lock(&cache_turbo_lock);
    memcpy(out, &cache_turbo_stats, sizeof(*out));
    pthread_mutex_unlock(&cache_turbo_lock);
}

#endif // CACHE_TURBO_H
