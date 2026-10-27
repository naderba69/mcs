/*
 * Web Dashboard for MultiCS
 * Version: 1.0
 * Date: 2026-10-26
 * 
 * Real-time web interface for monitoring and controlling MultiCS systems.
 * Provides live statistics, charts, and control panels.
 */

#ifndef WEB_DASHBOARD_H
#define WEB_DASHBOARD_H

#include <stdint.h>
#include <time.h>

// ============================================================================
// CONFIGURATION
// ============================================================================

#define WEB_DASHBOARD_ENABLED       1
#define WEB_DASHBOARD_PORT          8080
#define WEB_DASHBOARD_UPDATE_MS     2000    // Update every 2 seconds
#define WEB_DASHBOARD_MAX_CLIENTS   10

// ============================================================================
// DATA STRUCTURES
// ============================================================================

typedef struct {
    uint32_t total_requests;
    uint32_t successful_decodes;
    uint32_t failed_decodes;
    uint32_t cache_hits;
    uint32_t cache_misses;
    uint32_t black_screens;
    uint32_t fake_cws_detected;
    uint32_t timeouts;
    float cache_hit_rate;
    float success_rate;
    float timeout_rate;
} dashboard_stats_t;

typedef struct {
    uint16_t caid;
    uint32_t provid;
    char name[64];
    uint32_t requests;
    float success_rate;
    float cache_hit_rate;
    uint8_t status;  // 0=inactive, 1=active, 2=optimized, 3=error
} dashboard_profile_t;

typedef struct {
    uint8_t enabled;
    uint16_t port;
    uint32_t update_interval_ms;
    
    dashboard_stats_t global_stats;
    dashboard_profile_t *profiles;
    uint32_t profile_count;
    uint32_t max_profiles;
    
    time_t start_time;
    time_t last_update;
    uint32_t uptime_seconds;
    
    // System health
    float cpu_usage;
    float memory_usage;
    uint32_t active_connections;
} web_dashboard_config_t;

// ============================================================================
// GLOBAL CONFIGURATION
// ============================================================================

extern web_dashboard_config_t web_dashboard_cfg;

// ============================================================================
// INITIALIZATION
// ============================================================================

/**
 * Initialize the web dashboard system
 * @return 0 on success, -1 on error
 */
int web_dashboard_init(void);

/**
 * Start the web dashboard HTTP server
 * @return 0 on success, -1 on error
 */
int web_dashboard_start(void);

/**
 * Stop the web dashboard HTTP server
 */
void web_dashboard_stop(void);

/**
 * Cleanup web dashboard resources
 */
void web_dashboard_cleanup(void);

// ============================================================================
// STATISTICS UPDATE
// ============================================================================

/**
 * Update global statistics
 */
void web_dashboard_update_stats(void);

/**
 * Update profile-specific statistics
 * @param caid CAID of the profile
 * @param provid Provider ID
 */
void web_dashboard_update_profile(uint16_t caid, uint32_t provid);

/**
 * Record a request for dashboard statistics
 * @param caid CAID
 * @param provid Provider ID
 * @param success 1 if successful, 0 if failed
 * @param cache_hit 1 if cache hit, 0 if cache miss
 */
void web_dashboard_record_request(uint16_t caid, uint32_t provid, int success, int cache_hit);

// ============================================================================
// HTTP HANDLERS
// ============================================================================

/**
 * Handle HTTP request for dashboard
 * @param sock Socket descriptor
 * @param req HTTP request structure
 */
void http_send_dashboard(int sock, void *req);

/**
 * Handle HTTP request for statistics API (JSON)
 * @param sock Socket descriptor
 * @param req HTTP request structure
 */
void http_send_dashboard_api(int sock, void *req);

/**
 * Handle HTTP request for profile list (JSON)
 * @param sock Socket descriptor
 * @param req HTTP request structure
 */
void http_send_dashboard_profiles(int sock, void *req);

/**
 * Handle HTTP request for system health (JSON)
 * @param sock Socket descriptor
 * @param req HTTP request structure
 */
void http_send_dashboard_health(int sock, void *req);

// ============================================================================
// QUERY FUNCTIONS
// ============================================================================

/**
 * Get current dashboard statistics
 * @param stats Output statistics structure
 */
void web_dashboard_get_stats(dashboard_stats_t *stats);

/**
 * Get dashboard uptime in seconds
 * @return Uptime in seconds
 */
uint32_t web_dashboard_get_uptime(void);

/**
 * Check if web dashboard is enabled
 * @return 1 if enabled, 0 if disabled
 */
int web_dashboard_is_enabled(void);

/**
 * Enable or disable web dashboard
 * @param enabled 1 to enable, 0 to disable
 */
void web_dashboard_set_enabled(int enabled);

#endif // WEB_DASHBOARD_H
