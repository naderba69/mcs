/*
 * Performance Suite - Unified Integration Layer
 * 
 * Combines all 5 performance enhancement systems:
 * 1. Cache Turbo (Hash Table) - 100x faster cache
 * 2. Black Screen Detector - Early detection & recovery
 * 3. Cache Prefetch - Intelligent CW preloading
 * 4. Server Performance - Smart server selection
 * 5. Smart Retry - Diversified retry strategy
 * 
 * This file provides unified initialization and
 * cross-system coordination.
 */

#ifndef PERFORMANCE_SUITE_H
#define PERFORMANCE_SUITE_H

#include "cache-turbo.h"
#include "blackscreen-detector.h"
#include "cache-prefetch.h"
#include "server-performance.h"
#include "smart-retry.h"

// ============================================================================
// Unified Initialization
// ============================================================================

static void performance_suite_init(void)
{
    cache_turbo_init();
    bs_init();
    prefetch_init();
    sp_init();
    sr_init();
}

// ============================================================================
// Cross-System Coordination
// ============================================================================

/*
 * Called when user starts watching a channel
 * Triggers prefetch and records activity
 */
static void perf_on_channel_view(uint16_t caid, uint32_t provid, uint16_t sid, uint32_t now_ms)
{
    // Trigger prefetch for adjacent channels
    prefetch_trigger_adjacent(caid, provid, sid, now_ms);
    
    // Trigger pattern-based prefetch
    prefetch_trigger_pattern(caid, provid, sid, now_ms);
    
    // Record ECM request for black screen detection
    bs_record_ecm_request(caid, provid, sid, now_ms);
}

/*
 * Called when CW is successfully received
 * Updates all tracking systems
 */
static void perf_on_cw_success(uint16_t caid, uint32_t provid, uint16_t sid,
                                const uint8_t cw[16], const uint8_t ecm_hash[16],
                                uint32_t server_id, uint32_t response_ms,
                                uint8_t cw_cycle, uint8_t source_type, uint32_t now_ms)
{
    // Store in turbo cache
    cache_turbo_store(caid, provid, sid, cw, ecm_hash, cw_cycle,
                      source_type, server_id, now_ms);
    
    // Record black screen success
    bs_record_cw_success(caid, provid, sid, now_ms, response_ms);
    
    // Record server performance
    sp_record_success(server_id, caid, provid, sid, response_ms, now_ms);
    
    // Record smart retry success
    if (ecm_hash) {
        sr_record_success(ecm_hash);
    }
    
    // Check if this was a prefetched channel
    prefetch_check_and_remove(caid, provid, sid);
}

/*
 * Called when CW request fails
 * Triggers recovery mechanisms
 */
static int perf_on_cw_failure(uint16_t caid, uint32_t provid, uint16_t sid,
                               const uint8_t ecm_hash[16], uint32_t server_id,
                               uint32_t now_ms)
{
    // Record server failure
    sp_record_failure(server_id, caid, provid, sid, now_ms);
    
    // Check black screen condition
    int bs_action = bs_record_cw_failure(caid, provid, sid, now_ms);
    
    // Check if should retry
    sr_strategy_t next_strategy;
    uint32_t wait_ms;
    int should_retry = 0;
    
    if (ecm_hash) {
        should_retry = sr_should_retry(ecm_hash, now_ms, &next_strategy, &wait_ms);
    }
    
    // Return combined action
    // Priority: Black Screen > Smart Retry
    if (bs_action > 0) {
        return bs_action;  // 1=quick switch, 2=broadcast
    }
    
    if (should_retry) {
        return 10 + next_strategy;  // 11-15 = retry strategies
    }
    
    return 0;  // No action
}

/*
 * Try to get CW from cache (turbo lookup)
 * Returns: 1 if found, 0 if not
 */
static int perf_cache_lookup(uint16_t caid, uint32_t provid, uint16_t sid,
                              uint8_t cw[16], uint32_t now_ms)
{
    return cache_turbo_lookup(caid, provid, sid, cw, now_ms);
}

/*
 * Get best server for a channel
 * Returns: server_id or 0
 */
static uint32_t perf_get_best_server(uint16_t caid, uint32_t provid, uint16_t sid)
{
    return sp_get_best_server(caid, provid, sid);
}

/*
 * Check if channel has black screen
 */
static int perf_is_blackscreen(uint16_t caid, uint32_t provid, uint16_t sid, uint32_t now_ms)
{
    return bs_is_blackscreen(caid, provid, sid, now_ms);
}

/*
 * Get next prefetch request
 * Returns: 1 if got request, 0 if queue empty
 */
static int perf_get_prefetch_request(uint16_t *caid, uint32_t *provid, uint16_t *sid)
{
    return prefetch_get_next(caid, provid, sid);
}

/*
 * Periodic cleanup (call every 30 seconds)
 */
static void perf_periodic_cleanup(uint32_t now_ms)
{
    cache_turbo_cleanup(now_ms);
    prefetch_cleanup(now_ms);
}

// ============================================================================
// Statistics Reporting
// ============================================================================

struct perf_suite_stats {
    struct cache_turbo_stats cache;
    struct bs_global_stats blackscreen;
    struct prefetch_stats prefetch;
    struct sp_global_stats server;
    struct sr_stats retry;
};

static void perf_get_all_stats(struct perf_suite_stats *out)
{
    cache_turbo_get_stats(&out->cache);
    bs_get_global_stats(&out->blackscreen);
    prefetch_get_stats(&out->prefetch);
    sp_get_global_stats(&out->server);
    sr_get_stats(&out->retry);
}

#endif // PERFORMANCE_SUITE_H
