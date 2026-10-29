/* caid-integration.h — Integration of CAID Optimizations into Main Code Paths
 *
 * PURPOSE: Connect CAID-specific optimizations to actual ECM processing
 * 
 * Integration Points:
 * 1. Initialize CAID optimization table on startup
 * 2. Use CAID-specific timeouts in ECM processing
 * 3. Track performance metrics per CAID
 * 4. Apply CAID-specific validation rules
 */

#ifndef CAID_INTEGRATION_H
#define CAID_INTEGRATION_H

#include "caid-optimizations.h"
#include "common.h"
#include "debug.h"

// ============================================================================
// INITIALIZATION
// ============================================================================

static inline void caid_integration_init(void) {
    caid_optimizations_init();
    debugf(0, " CAID Optimizations: Initialized %d CAID-specific settings\n", caid_opt_tab.count);
}

// ============================================================================
// TIMEOUT INTEGRATION
// ============================================================================

/*
 * Use this function to get the appropriate timeout for a CAID
 * instead of using a fixed global timeout
 */
static inline uint32_t caid_integration_get_server_timeout(uint16_t caid) {
    return caid_get_timeout(caid);
}

static inline uint32_t caid_integration_get_ecm_timeout(uint16_t caid) {
    return caid_get_validecmtime(caid);
}

static inline uint32_t caid_integration_get_cache_timeout(uint16_t caid) {
    return caid_get_cache_timeout(caid);
}

// ============================================================================
// VALIDATION INTEGRATION
// ============================================================================

/*
 * Check if we should use relaxed validation for this CAID
 * Used for problematic CAIDs like 09BD (Vivacom) with half-key issues
 */
static inline int caid_integration_should_relax_validation(uint16_t caid) {
    return caid_should_use_relaxed_validation(caid);
}

/*
 * Check if half-nulled CW should be rejected for this CAID
 * Some CAIDs (like 09BD) need to accept half-nulled CW
 */
static inline int caid_integration_reject_halfnulled(uint16_t caid) {
    struct caid_settings *s = caid_get_settings(caid);
    if (s) return s->halfnulled_reject;
    return 1;  // Default: reject half-nulled
}

// ============================================================================
// PERFORMANCE TRACKING INTEGRATION
// ============================================================================

/*
 * Call this after receiving a CW to track performance
 * response_time: milliseconds to get the CW
 * success: 1 if CW was valid, 0 if failed
 */
static inline void caid_integration_track_performance(uint16_t caid, uint32_t response_time, int success) {
    caid_update_stats(caid, response_time, success);
}

// ============================================================================
// CACHE PRIORITY INTEGRATION
// ============================================================================

/*
 * Get cache priority for a CAID (0-100)
 * Higher priority = more likely to be cached and served from cache
 */
static inline uint8_t caid_integration_get_cache_priority(uint16_t caid) {
    return caid_get_cache_priority(caid);
}

// ============================================================================
// RETRY CONFIGURATION INTEGRATION
// ============================================================================

/*
 * Get the number of retries for a CAID
 */
static inline uint8_t caid_integration_get_retry_count(uint16_t caid) {
    struct caid_settings *s = caid_get_settings(caid);
    if (s) return s->dcw_retry;
    return 2;  // Default: 2 retries
}

/*
 * Get max failed attempts before marking server as failed
 */
static inline uint8_t caid_integration_get_max_failed(uint16_t caid) {
    struct caid_settings *s = caid_get_settings(caid);
    if (s) return s->dcw_maxfailed;
    return 2;  // Default: 2 max failed
}

// ============================================================================
// FEATURE FLAGS INTEGRATION
// ============================================================================

/*
 * Check if SKIPCWC (CW cycle check) should be enabled for this CAID
 */
static inline int caid_integration_enable_skipcwc(uint16_t caid) {
    struct caid_settings *s = caid_get_settings(caid);
    if (s) return s->enable_skipcwc;
    return 1;  // Default: enabled
}

/*
 * Check if CW cycle detection should be enabled for this CAID
 */
static inline int caid_integration_enable_cycle_check(uint16_t caid) {
    struct caid_settings *s = caid_get_settings(caid);
    if (s) return s->enable_cycle_check;
    return 1;  // Default: enabled
}

// ============================================================================
// LOGGING AND DEBUGGING
// ============================================================================

/*
 * Log CAID-specific settings for debugging
 */
static inline void caid_integration_log_settings(uint16_t caid) {
    struct caid_settings *s = caid_get_settings(caid);
    if (s) {
        debugf(0, " CAID %04X Settings:\n", caid);
        debugf(0, "   Timeout: %ums, ValidECM: %ums, Cache: %ums\n",
               s->server_timeout, s->dcw_validecmtime, s->cache_timeout);
        debugf(0, "   Retry: %u, MaxFailed: %u, CachePriority: %u\n",
               s->dcw_retry, s->dcw_maxfailed, s->cache_priority);
        debugf(0, "   AvgResponse: %ums, Success: %u%%, BlackScreens: %u\n",
               s->avg_response_time, s->success_rate, s->black_screen_count);
    }
}

/*
 * Get statistics string for a CAID
 */
static inline void caid_integration_get_stats_string(uint16_t caid, char *buf, int bufsize) {
    struct caid_settings *s = caid_get_settings(caid);
    if (s) {
        snprintf(buf, bufsize, "CAID %04X: Avg=%ums, Success=%u%%, BlackScreens=%u",
                 caid, s->avg_response_time, s->success_rate, s->black_screen_count);
    } else {
        snprintf(buf, bufsize, "CAID %04X: No data", caid);
    }
}

#endif /* CAID_INTEGRATION_H */
