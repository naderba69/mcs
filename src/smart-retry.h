/*
 * Smart Retry - Intelligent Retry Strategy
 * 
 * Diversifies retry sources instead of retrying same server:
 * - Attempt 1: Fastest server
 * - Attempt 2: Alternate server (different from first)
 * - Attempt 3: Cache lookup
 * - Attempt 4: Broadcast to all servers
 * - Attempt 5: Last resort (any available)
 * 
 * Features:
 * - Source diversification
 * - Adaptive timing
 * - Failure pattern detection
 * - Configurable retry limits
 */

#ifndef SMART_RETRY_H
#define SMART_RETRY_H

#include <stdint.h>
#include <string.h>
#include <pthread.h>

// ============================================================================
// Configuration
// ============================================================================

#define SR_MAX_RETRIES 5
#define SR_MAX_TRACKED_ECMS 256
#define SR_RETRY_INTERVAL_MS 500       // Base retry interval
#define SR_BACKOFF_MULTIPLIER 2        // Exponential backoff

// ============================================================================
// Retry Strategy
// ============================================================================

typedef enum {
    SR_STRATEGY_FASTEST = 1,      // Use fastest server
    SR_STRATEGY_ALTERNATE = 2,    // Use different server
    SR_STRATEGY_CACHE = 3,        // Try cache
    SR_STRATEGY_BROADCAST = 4,    // Send to all servers
    SR_STRATEGY_LAST_RESORT = 5   // Any available server
} sr_strategy_t;

// ============================================================================
// Data Structures
// ============================================================================

struct sr_ecm_retry {
    uint16_t caid;
    uint32_t provid;
    uint16_t sid;
    uint8_t ecm_hash[16];
    
    // Retry state
    int current_retry;
    int max_retries;
    uint32_t first_attempt_time;
    uint32_t last_attempt_time;
    uint32_t next_retry_time;
    
    // Server tracking
    uint32_t attempted_servers[SR_MAX_RETRIES];
    int attempted_count;
    uint32_t last_server_id;
    
    // Strategy
    sr_strategy_t current_strategy;
    
    // Active flag
    int active;
};

struct sr_stats {
    uint64_t total_retries;
    uint64_t total_first_attempt_success;
    uint64_t total_retry_success;
    uint64_t total_retry_failures;
    
    uint64_t strategy_usage[6];  // Count per strategy
    
    uint32_t avg_retries_needed;
    uint32_t current_tracked_ecms;
};

// ============================================================================
// Global State
// ============================================================================

static struct sr_ecm_retry sr_ecms[SR_MAX_TRACKED_ECMS];
static int sr_ecm_count = 0;
static struct sr_stats sr_stats;
static pthread_mutex_t sr_lock = PTHREAD_MUTEX_INITIALIZER;
static int sr_enabled = 1;

// ============================================================================
// ECM Tracking
// ============================================================================

static struct sr_ecm_retry* sr_get_ecm(const uint8_t ecm_hash[16])
{
    int i;
    for (i = 0; i < sr_ecm_count; i++) {
        if (sr_ecms[i].active && memcmp(sr_ecms[i].ecm_hash, ecm_hash, 16) == 0) {
            return &sr_ecms[i];
        }
    }
    return NULL;
}

static struct sr_ecm_retry* sr_create_ecm(const uint8_t ecm_hash[16], uint16_t caid,
                                           uint32_t provid, uint16_t sid, uint32_t now_ms)
{
    // Find empty slot
    int slot = -1;
    int i;
    
    for (i = 0; i < SR_MAX_TRACKED_ECMS; i++) {
        if (!sr_ecms[i].active) {
            slot = i;
            break;
        }
    }
    
    if (slot == -1) {
        // Remove oldest
        uint32_t oldest_time = 0xFFFFFFFF;
        for (i = 0; i < sr_ecm_count; i++) {
            if (sr_ecms[i].first_attempt_time < oldest_time) {
                oldest_time = sr_ecms[i].first_attempt_time;
                slot = i;
            }
        }
    }
    
    if (slot == -1) return NULL;
    
    struct sr_ecm_retry *ecm = &sr_ecms[slot];
    memset(ecm, 0, sizeof(*ecm));
    memcpy(ecm->ecm_hash, ecm_hash, 16);
    ecm->caid = caid;
    ecm->provid = provid;
    ecm->sid = sid;
    ecm->first_attempt_time = now_ms;
    ecm->last_attempt_time = now_ms;
    ecm->current_retry = 0;
    ecm->max_retries = SR_MAX_RETRIES;
    ecm->active = 1;
    
    if (slot >= sr_ecm_count) {
        sr_ecm_count = slot + 1;
    }
    
    sr_stats.current_tracked_ecms++;
    
    return ecm;
}

// ============================================================================
// Retry Logic
// ============================================================================

/*
 * Get next retry strategy
 */
static sr_strategy_t sr_get_next_strategy(struct sr_ecm_retry *ecm)
{
    int retry = ecm->current_retry;
    
    if (retry == 0) {
        return SR_STRATEGY_FASTEST;
    } else if (retry == 1) {
        return SR_STRATEGY_ALTERNATE;
    } else if (retry == 2) {
        return SR_STRATEGY_CACHE;
    } else if (retry == 3) {
        return SR_STRATEGY_BROADCAST;
    } else {
        return SR_STRATEGY_LAST_RESORT;
    }
}

/*
 * Check if server was already attempted
 */
static int sr_server_attempted(struct sr_ecm_retry *ecm, uint32_t server_id)
{
    int i;
    for (i = 0; i < ecm->attempted_count; i++) {
        if (ecm->attempted_servers[i] == server_id) {
            return 1;
        }
    }
    return 0;
}

/*
 * Record server attempt
 */
static void sr_record_attempt(struct sr_ecm_retry *ecm, uint32_t server_id)
{
    if (ecm->attempted_count < SR_MAX_RETRIES) {
        ecm->attempted_servers[ecm->attempted_count++] = server_id;
    }
    ecm->last_server_id = server_id;
}

/*
 * Record first attempt for an ECM
 * Returns: server_id to use (0 = no server available)
 */
static uint32_t sr_record_first_attempt(const uint8_t ecm_hash[16], uint16_t caid,
                                         uint32_t provid, uint16_t sid,
                                         uint32_t server_id, uint32_t now_ms)
{
    if (!sr_enabled) return server_id;
    
    pthread_mutex_lock(&sr_lock);
    
    struct sr_ecm_retry *ecm = sr_create_ecm(ecm_hash, caid, provid, sid, now_ms);
    if (ecm) {
        ecm->current_retry = 1;
        ecm->current_strategy = SR_STRATEGY_FASTEST;
        sr_record_attempt(ecm, server_id);
        ecm->next_retry_time = now_ms + SR_RETRY_INTERVAL_MS;
        
        sr_stats.strategy_usage[SR_STRATEGY_FASTEST]++;
    }
    
    pthread_mutex_unlock(&sr_lock);
    return server_id;
}

/*
 * Check if retry is needed and get next strategy
 * Returns: 1 if should retry, 0 if give up
 *          *next_strategy filled with strategy to use
 *          *wait_ms filled with time to wait before retry
 */
static int sr_should_retry(const uint8_t ecm_hash[16], uint32_t now_ms,
                            sr_strategy_t *next_strategy, uint32_t *wait_ms)
{
    if (!sr_enabled) return 0;
    
    pthread_mutex_lock(&sr_lock);
    
    struct sr_ecm_retry *ecm = sr_get_ecm(ecm_hash);
    if (!ecm || !ecm->active) {
        pthread_mutex_unlock(&sr_lock);
        return 0;
    }
    
    // Check if max retries reached
    if (ecm->current_retry >= ecm->max_retries) {
        ecm->active = 0;
        sr_stats.current_tracked_ecms--;
        sr_stats.total_retry_failures++;
        pthread_mutex_unlock(&sr_lock);
        return 0;
    }
    
    // Check if enough time passed
    if (now_ms < ecm->next_retry_time) {
        *wait_ms = ecm->next_retry_time - now_ms;
        pthread_mutex_unlock(&sr_lock);
        return 0;  // Not time yet
    }
    
    // Get next strategy
    ecm->current_retry++;
    ecm->current_strategy = sr_get_next_strategy(ecm);
    ecm->last_attempt_time = now_ms;
    
    // Calculate next retry time with exponential backoff
    uint32_t backoff = SR_RETRY_INTERVAL_MS;
    int i;
    for (i = 0; i < ecm->current_retry; i++) {
        backoff *= SR_BACKOFF_MULTIPLIER;
    }
    ecm->next_retry_time = now_ms + backoff;
    
    *next_strategy = ecm->current_strategy;
    *wait_ms = 0;
    
    sr_stats.total_retries++;
    sr_stats.strategy_usage[ecm->current_strategy]++;
    
    pthread_mutex_unlock(&sr_lock);
    return 1;
}

/*
 * Record successful CW reception
 */
static void sr_record_success(const uint8_t ecm_hash[16])
{
    if (!sr_enabled) return;
    
    pthread_mutex_lock(&sr_lock);
    
    struct sr_ecm_retry *ecm = sr_get_ecm(ecm_hash);
    if (ecm) {
        if (ecm->current_retry == 0 || ecm->current_retry == 1) {
            sr_stats.total_first_attempt_success++;
        } else {
            sr_stats.total_retry_success++;
        }
        
        // Update average retries
        if (sr_stats.avg_retries_needed == 0) {
            sr_stats.avg_retries_needed = ecm->current_retry;
        } else {
            sr_stats.avg_retries_needed = (sr_stats.avg_retries_needed * 7 +
                                           ecm->current_retry * 3) / 10;
        }
        
        ecm->active = 0;
        sr_stats.current_tracked_ecms--;
    }
    
    pthread_mutex_unlock(&sr_lock);
}

/*
 * Get alternate server (different from attempted ones)
 * Returns: server_id or 0 if no alternate available
 */
static uint32_t sr_get_alternate_server(const uint8_t ecm_hash[16],
                                         uint32_t (*get_server_func)(uint16_t, uint32_t, uint16_t),
                                         uint16_t caid, uint32_t provid, uint16_t sid)
{
    if (!sr_enabled || !get_server_func) return 0;
    
    pthread_mutex_lock(&sr_lock);
    
    struct sr_ecm_retry *ecm = sr_get_ecm(ecm_hash);
    if (!ecm) {
        pthread_mutex_unlock(&sr_lock);
        return get_server_func(caid, provid, sid);
    }
    
    // Try to get a server we haven't attempted
    int attempts = 0;
    uint32_t server_id;
    
    while (attempts < 10) {
        server_id = get_server_func(caid, provid, sid);
        if (server_id == 0) break;
        
        if (!sr_server_attempted(ecm, server_id)) {
            sr_record_attempt(ecm, server_id);
            pthread_mutex_unlock(&sr_lock);
            return server_id;
        }
        attempts++;
    }
    
    // No alternate found, use last resort
    pthread_mutex_unlock(&sr_lock);
    return ecm->last_server_id;
}

/*
 * Get statistics
 */
static void sr_get_stats(struct sr_stats *out)
{
    pthread_mutex_lock(&sr_lock);
    memcpy(out, &sr_stats, sizeof(*out));
    pthread_mutex_unlock(&sr_lock);
}

// ============================================================================
// Initialization
// ============================================================================

static void sr_init(void)
{
    memset(sr_ecms, 0, sizeof(sr_ecms));
    memset(&sr_stats, 0, sizeof(sr_stats));
    sr_ecm_count = 0;
    sr_enabled = 1;
}

#endif // SMART_RETRY_H
