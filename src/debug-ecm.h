/* debug-ecm.h - Detailed ECM Tracking and Debugging
 * 
 * PURPOSE: Track every ECM request with full details
 * 
 * FEATURES:
 * - Log every ECM with CAID, Provider, SID
 * - Track response time per server
 * - Show ECM path (client → servers → result)
 * - Filter by CAID/Provider/SID
 * - Export to CSV
 */

#ifndef DEBUG_ECM_H
#define DEBUG_ECM_H

#include <stdint.h>
#include <time.h>
#include <pthread.h>

// Maximum ECM entries to keep in memory
#define DEBUG_ECM_MAX_ENTRIES 1000

// ECM status
#define ECM_STATUS_PENDING  0
#define ECM_STATUS_SUCCESS  1
#define ECM_STATUS_FAILED   2
#define ECM_STATUS_TIMEOUT  3
#define ECM_STATUS_CACHED   4

// Server response
struct ecm_server_response {
    uint32_t server_id;
    char server_name[64];
    uint32_t response_time;  // milliseconds
    int status;  // 0=pending, 1=success, 2=failed, 3=timeout
};

// ECM entry
struct debug_ecm_entry {
    // Timestamps
    time_t timestamp;
    uint32_t request_id;
    
    // ECM identification
    uint16_t caid;
    uint32_t provid;
    uint16_t sid;
    
    // Client info
    uint32_t client_ip;
    char client_name[64];
    
    // Result
    int final_status;  // ECM_STATUS_*
    uint32_t total_time;  // total milliseconds
    char result_source[32];  // "cache", "server X", "timeout"
    
    // Server responses (max 10 servers)
    struct ecm_server_response servers[10];
    int server_count;
    
    // Additional info
    int cache_hit;
    int retried;
    char error_message[128];
};

// Debug ECM table
struct debug_ecm_table {
    struct debug_ecm_entry entries[DEBUG_ECM_MAX_ENTRIES];
    int count;
    int next_index;  // circular buffer
    pthread_mutex_t lock;
    
    // Statistics
    uint32_t total_requests;
    uint32_t successful_requests;
    uint32_t failed_requests;
    uint32_t cached_requests;
};

extern struct debug_ecm_table debug_ecm_tab;

// Initialize debug ECM system
void debug_ecm_init(void);

// Record new ECM request
int debug_ecm_record_request(uint16_t caid, uint32_t provid, uint16_t sid,
                            uint32_t client_ip, const char *client_name);

// Update ECM with server response
void debug_ecm_record_server_response(int request_id, uint32_t server_id,
                                     const char *server_name, uint32_t response_time,
                                     int status);

// Mark ECM as complete
void debug_ecm_mark_complete(int request_id, int final_status,
                            const char *result_source, const char *error_msg);

// Mark ECM as cached
void debug_ecm_mark_cached(int request_id);

// Get ECM entry by ID
struct debug_ecm_entry* debug_ecm_get(int request_id);

// Get recent ECMs (for display)
int debug_ecm_get_recent(struct debug_ecm_entry *entries, int max_count);

// Filter ECMs by CAID/Provider/SID
int debug_ecm_filter(struct debug_ecm_entry *entries, int max_count,
                    uint16_t caid, uint32_t provid, uint16_t sid);

// Export ECMs to CSV
int debug_ecm_export_csv(const char *filename);

// Clear all entries
void debug_ecm_clear(void);

// ============================================================================
// HTTP INTERFACE
// ============================================================================

// Forward declaration
struct http_request;

// HTTP interface
void http_send_debug_ecm(int sock, struct http_request *req);

#endif /* DEBUG_ECM_H */
