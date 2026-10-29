/* diagnostics.h - Advanced Analytics and Diagnostics System
 * 
 * PURPOSE: Provide comprehensive system diagnostics and troubleshooting
 * 
 * FEATURES:
 * - Real-time performance analytics
 * - Automatic fault detection
 * - Server health assessment
 * - ECM/CW success rate tracking
 * - Bottleneck identification
 * - Recommendations engine
 */

#ifndef DIAGNOSTICS_H
#define DIAGNOSTICS_H

#include <stdint.h>
#include <time.h>
#include <pthread.h>

// ============================================================================
// DIAGNOSTICS DATA STRUCTURES
// ============================================================================

struct diagnostic_stats {
    // Time tracking
    time_t start_time;
    time_t last_update;
    uint32_t uptime_seconds;
    
    // ECM statistics
    uint32_t total_ecm_requests;
    uint32_t successful_ecm;
    uint32_t failed_ecm;
    uint32_t timeout_ecm;
    uint32_t cache_hits;
    uint32_t cache_misses;
    
    // CW statistics
    uint32_t total_cw_received;
    uint32_t valid_cw;
    uint32_t invalid_cw;
    uint32_t rejected_cw;
    
    // Server statistics
    uint32_t total_servers;
    uint32_t connected_servers;
    uint32_t disconnected_servers;
    uint32_t lazy_servers;
    
    // Performance metrics
    uint32_t avg_response_time;
    uint32_t min_response_time;
    uint32_t max_response_time;
    
    // Success rates
    uint8_t ecm_success_rate;  // 0-100%
    uint8_t cache_hit_rate;    // 0-100%
    uint8_t cw_valid_rate;     // 0-100%
    
    // Health score
    uint8_t system_health;     // 0-100%
};

struct diagnostic_issue {
    char description[256];
    char severity[32];  // "Critical", "Warning", "Info"
    char recommendation[512];
    int issue_code;
};

struct diagnostic_report {
    struct diagnostic_stats stats;
    struct diagnostic_issue issues[20];
    int issue_count;
    
    // Recommendations
    char recommendations[10][256];
    int recommendation_count;
    
    // System status
    char status[64];  // "Healthy", "Degraded", "Critical"
};

extern struct diagnostic_report diag_report;

// ============================================================================
// DIAGNOSTICS FUNCTIONS
// ============================================================================

// Initialize diagnostics system
void diagnostics_init(void);

// Update diagnostics (call periodically)
void diagnostics_update(void);

// Generate full diagnostic report
void diagnostics_generate_report(struct diagnostic_report *report);

// Get current system health (0-100)
uint8_t diagnostics_get_health(void);

// Get system status string
const char* diagnostics_get_status(void);

// Add issue to report
void diagnostics_add_issue(const char *description, const char *severity, 
                          const char *recommendation, int code);

// Add recommendation
void diagnostics_add_recommendation(const char *rec);

// Calculate success rates
void diagnostics_calculate_rates(struct diagnostic_stats *stats);

// Detect common problems
void diagnostics_detect_problems(struct diagnostic_report *report);

// Generate recommendations based on issues
void diagnostics_generate_recommendations(struct diagnostic_report *report);

// ============================================================================
// HTTP INTERFACE
// ============================================================================

// Forward declaration
struct http_request;

// Send diagnostics page
void http_send_diagnostics(int sock, struct http_request *req);

// Send diagnostics JSON (for AJAX updates)
void http_send_diagnostics_json(int sock, struct http_request *req);

#endif /* DIAGNOSTICS_H */
