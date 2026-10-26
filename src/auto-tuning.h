/* auto-tuning.h - Automatic Performance Tuning System
 * 
 * PURPOSE: Monitor performance and suggest optimizations
 * 
 * MODE: Suggestion + Approval (safe mode)
 * - System analyzes performance
 * - Suggests improvements
 * - User approves or rejects
 * - No automatic changes
 * 
 * FEATURES:
 * - Performance monitoring
 * - Bottleneck detection
 * - Smart suggestions
 * - Approval workflow
 * - Change history
 */

#ifndef AUTO_TUNING_H
#define AUTO_TUNING_H

#include <stdint.h>
#include <time.h>
#include <pthread.h>

// Maximum suggestions
#define AUTO_TUNING_MAX_SUGGESTIONS 50

// Maximum history entries
#define AUTO_TUNING_MAX_HISTORY 100

// Suggestion types
#define SUGGESTION_TYPE_TIMEOUT    1
#define SUGGESTION_TYPE_CACHE      2
#define SUGGESTION_TYPE_PREFETCH   3
#define SUGGESTION_TYPE_SERVER     4
#define SUGGESTION_TYPE_GENERAL    5

// Suggestion status
#define SUGGESTION_STATUS_PENDING   0
#define SUGGESTION_STATUS_APPROVED  1
#define SUGGESTION_STATUS_REJECTED  2
#define SUGGESTION_STATUS_APPLIED   3

// Suggestion priority
#define SUGGESTION_PRIORITY_LOW    1
#define SUGGESTION_PRIORITY_MEDIUM 2
#define SUGGESTION_PRIORITY_HIGH   3
#define SUGGESTION_PRIORITY_CRITICAL 4

// Suggestion structure
struct auto_tuning_suggestion {
    uint32_t id;
    time_t timestamp;
    int type;
    int priority;
    int status;
    
    char title[128];
    char description[512];
    char reason[256];
    char expected_benefit[256];
    
    // Configuration change
    char config_key[64];
    char current_value[64];
    char suggested_value[64];
    
    // Affected component
    char component[64];  // "server", "caid", "global"
    uint32_t component_id;  // server_id or caid
    
    // Metrics
    uint32_t current_metric;
    uint32_t expected_metric;
};

// History entry
struct auto_tuning_history {
    uint32_t suggestion_id;
    time_t timestamp;
    int action;  // 0=rejected, 1=approved, 2=applied
    char action_text[64];
};

// Auto-tuning table
struct auto_tuning_table {
    struct auto_tuning_suggestion suggestions[AUTO_TUNING_MAX_SUGGESTIONS];
    int suggestion_count;
    uint32_t next_id;
    
    struct auto_tuning_history history[AUTO_TUNING_MAX_HISTORY];
    int history_count;
    int history_index;
    
    pthread_mutex_t lock;
    
    // Statistics
    uint32_t total_suggestions;
    uint32_t approved_count;
    uint32_t rejected_count;
    uint32_t applied_count;
    
    // Last analysis
    time_t last_analysis_time;
    int analysis_running;
};

extern struct auto_tuning_table auto_tuning_tab;

// Initialize auto-tuning system
void auto_tuning_init(void);

// Run performance analysis (call periodically)
void auto_tuning_analyze(void);

// Add suggestion
int auto_tuning_add_suggestion(int type, int priority, const char *title,
                              const char *description, const char *reason,
                              const char *expected_benefit,
                              const char *config_key, const char *current_value,
                              const char *suggested_value,
                              const char *component, uint32_t component_id);

// Approve suggestion
int auto_tuning_approve(uint32_t suggestion_id);

// Reject suggestion
int auto_tuning_reject(uint32_t suggestion_id);

// Apply approved suggestion
int auto_tuning_apply(uint32_t suggestion_id);

// Get pending suggestions
int auto_tuning_get_pending(struct auto_tuning_suggestion *suggestions, int max_count);

// Get all suggestions
int auto_tuning_get_all(struct auto_tuning_suggestion *suggestions, int max_count);

// Get history
int auto_tuning_get_history(struct auto_tuning_history *history, int max_count);

// Clear all suggestions
void auto_tuning_clear(void);

// HTTP interface
struct http_request;
void http_send_auto_tuning(int sock, struct http_request *req);

#endif /* AUTO_TUNING_H */
