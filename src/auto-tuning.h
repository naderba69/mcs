/*
 * Auto-Tuning System for MultiCS
 * Version: 1.0
 * Date: 2026-10-26
 * 
 * Automatically adjusts profile settings based on real-time performance metrics.
 * Monitors cache hit rate, timeout rate, and decode success rate to optimize
 * timeouts and cache settings dynamically.
 */

#ifndef AUTO_TUNING_H
#define AUTO_TUNING_H

#include <stdint.h>
#include <time.h>

// ============================================================================
// CONFIGURATION
// ============================================================================

#define AUTO_TUNING_ENABLED          1
#define AUTO_TUNING_INTERVAL_MS      60000    // Check every 60 seconds
#define AUTO_TUNING_MIN_SAMPLES      100      // Minimum samples before tuning

// Thresholds for tuning decisions
#define CACHE_HIT_RATE_GOOD          0.80     // 80% cache hit rate
#define CACHE_HIT_RATE_BAD           0.50     // 50% cache hit rate
#define TIMEOUT_RATE_HIGH            0.20     // 20% timeout rate
#define DECODE_SUCCESS_RATE_GOOD     0.95     // 95% decode success rate

// Adjustment limits
#define TIMEOUT_ADJUST_STEP          50       // Adjust timeout by 50ms
#define TIMEOUT_MIN                  300      // Minimum timeout
#define TIMEOUT_MAX                  5000     // Maximum timeout
#define CACHE_TIMEOUT_ADJUST_STEP    100      // Adjust cache timeout by 100ms
#define CACHE_TIMEOUT_MIN            200      // Minimum cache timeout
#define CACHE_TIMEOUT_MAX            10000    // Maximum cache timeout

// ============================================================================
// DATA STRUCTURES
// ============================================================================

typedef struct {
    uint32_t total_requests;
    uint32_t cache_hits;
    uint32_t cache_misses;
    uint32_t timeouts;
    uint32_t decode_success;
    uint32_t decode_failed;
    uint32_t black_screens;
    time_t last_update;
} profile_stats_t;

typedef struct {
    uint16_t caid;
    uint32_t provid;
    profile_stats_t stats;
    
    // Current tuned values
    uint16_t current_server_timeout;
    uint16_t current_cache_timeout;
    uint16_t current_valid_ecm_time;
    
    // Tuning history
    uint8_t tuning_count;
    time_t last_tune_time;
    
    // Performance metrics
    float cache_hit_rate;
    float timeout_rate;
    float decode_success_rate;
    
    // Status
    uint8_t needs_tuning;
    uint8_t is_optimized;
} auto_tuning_profile_t;

typedef struct {
    uint8_t enabled;
    uint32_t interval_ms;
    uint32_t min_samples;
    
    auto_tuning_profile_t *profiles;
    uint32_t profile_count;
    uint32_t max_profiles;
    
    time_t last_check_time;
    uint32_t total_tuning_operations;
} auto_tuning_config_t;

// ============================================================================
// GLOBAL CONFIGURATION
// ============================================================================

extern auto_tuning_config_t auto_tuning_cfg;

// ============================================================================
// INITIALIZATION
// ============================================================================

/**
 * Initialize the auto-tuning system
 * @return 0 on success, -1 on error
 */
int auto_tuning_init(void);

/**
 * Cleanup auto-tuning resources
 */
void auto_tuning_cleanup(void);

// ============================================================================
// STATISTICS COLLECTION
// ============================================================================

/**
 * Record a cache hit for a profile
 * @param caid CAID of the profile
 * @param provid Provider ID
 */
void auto_tuning_record_cache_hit(uint16_t caid, uint32_t provid);

/**
 * Record a cache miss for a profile
 * @param caid CAID of the profile
 * @param provid Provider ID
 */
void auto_tuning_record_cache_miss(uint16_t caid, uint32_t provid);

/**
 * Record a timeout for a profile
 * @param caid CAID of the profile
 * @param provid Provider ID
 */
void auto_tuning_record_timeout(uint16_t caid, uint32_t provid);

/**
 * Record a successful decode for a profile
 * @param caid CAID of the profile
 * @param provid Provider ID
 */
void auto_tuning_record_decode_success(uint16_t caid, uint32_t provid);

/**
 * Record a failed decode for a profile
 * @param caid CAID of the profile
 * @param provid Provider ID
 */
void auto_tuning_record_decode_failed(uint16_t caid, uint32_t provid);

/**
 * Record a black screen for a profile
 * @param caid CAID of the profile
 * @param provid Provider ID
 */
void auto_tuning_record_black_screen(uint16_t caid, uint32_t provid);

// ============================================================================
// TUNING OPERATIONS
// ============================================================================

/**
 * Check if tuning is needed and perform tuning if necessary
 * Should be called periodically (e.g., every AUTO_TUNING_INTERVAL_MS)
 */
void auto_tuning_check_and_tune(void);

/**
 * Force tuning for a specific profile
 * @param caid CAID of the profile
 * @param provid Provider ID
 * @return 0 on success, -1 on error
 */
int auto_tuning_force_tune(uint16_t caid, uint32_t provid);

/**
 * Reset statistics for a profile
 * @param caid CAID of the profile
 * @param provid Provider ID
 */
void auto_tuning_reset_stats(uint16_t caid, uint32_t provid);

/**
 * Reset all statistics
 */
void auto_tuning_reset_all_stats(void);

// ============================================================================
// QUERY FUNCTIONS
// ============================================================================

/**
 * Get current statistics for a profile
 * @param caid CAID of the profile
 * @param provid Provider ID
 * @param stats Output statistics structure
 * @return 0 on success, -1 if profile not found
 */
int auto_tuning_get_stats(uint16_t caid, uint32_t provid, profile_stats_t *stats);

/**
 * Get tuning profile for a CAID/Provider
 * @param caid CAID of the profile
 * @param provid Provider ID
 * @return Pointer to profile or NULL if not found
 */
auto_tuning_profile_t* auto_tuning_get_profile(uint16_t caid, uint32_t provid);

/**
 * Check if auto-tuning is enabled
 * @return 1 if enabled, 0 if disabled
 */
int auto_tuning_is_enabled(void);

/**
 * Enable or disable auto-tuning
 * @param enabled 1 to enable, 0 to disable
 */
void auto_tuning_set_enabled(int enabled);

// ============================================================================
// REPORTING
// ============================================================================

/**
 * Generate a tuning report
 * @param buffer Output buffer
 * @param buffer_size Size of buffer
 * @return Number of bytes written
 */
int auto_tuning_generate_report(char *buffer, int buffer_size);

/**
 * Log tuning statistics
 */
void auto_tuning_log_stats(void);

/**
 * Analyze all profiles and perform tuning if needed
 * This is a convenience wrapper around auto_tuning_check_and_tune()
 */
void auto_tuning_analyze(void);

#endif // AUTO_TUNING_H
