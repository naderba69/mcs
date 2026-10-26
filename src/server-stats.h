/* server-stats.h - Detailed Server Statistics
 * 
 * PURPOSE: Show comprehensive statistics for each server
 * 
 * FEATURES:
 * - Success rate per server
 * - Average response time
 * - Performance graphs (over time)
 * - Server comparison
 * - Ranking (best/worst)
 */

#ifndef SERVER_STATS_H
#define SERVER_STATS_H

#include <stdint.h>
#include <time.h>
#include <pthread.h>

// Maximum servers to track
#define SERVER_STATS_MAX_SERVERS 100

// Performance history (last 60 measurements, 1 per minute = 1 hour)
#define SERVER_STATS_HISTORY_SIZE 60

// Server performance entry
struct server_perf_entry {
    uint32_t timestamp;
    uint32_t success_rate;
    uint32_t avg_response_time;
    uint32_t request_count;
};

// Server statistics
struct server_stats {
    uint32_t server_id;
    char server_name[128];
    char server_host[64];
    uint16_t server_port;
    
    // Basic stats
    uint32_t total_requests;
    uint32_t successful_requests;
    uint32_t failed_requests;
    uint32_t timeout_requests;
    
    // Response time stats
    uint32_t total_response_time;
    uint32_t min_response_time;
    uint32_t max_response_time;
    uint32_t avg_response_time;
    
    // Calculated metrics
    uint32_t success_rate;  // 0-100%
    uint32_t reliability_score;  // 0-100
    
    // Performance history
    struct server_perf_entry history[SERVER_STATS_HISTORY_SIZE];
    int history_index;
    int history_count;
    
    // Last activity
    time_t last_success_time;
    time_t last_failure_time;
    time_t last_request_time;
    
    // Status
    int is_connected;
    int is_lazy;
    char status_text[64];
};

// Server stats table
struct server_stats_table {
    struct server_stats servers[SERVER_STATS_MAX_SERVERS];
    int count;
    pthread_mutex_t lock;
    
    // Global statistics
    uint32_t total_servers;
    uint32_t connected_servers;
    uint32_t disconnected_servers;
};

extern struct server_stats_table server_stats_tab;

// Initialize server stats system
void server_stats_init(void);

// Record server request
void server_stats_record_request(uint32_t server_id, const char *server_name,
                                const char *server_host, uint16_t server_port);

// Record successful response
void server_stats_record_success(uint32_t server_id, uint32_t response_time);

// Record failed response
void server_stats_record_failure(uint32_t server_id);

// Record timeout
void server_stats_record_timeout(uint32_t server_id);

// Update server connection status
void server_stats_update_status(uint32_t server_id, int is_connected);

// Get server stats by ID
struct server_stats* server_stats_get(uint32_t server_id);

// Get all server stats (for display)
int server_stats_get_all(struct server_stats *servers, int max_count);

// Get top N servers by success rate
int server_stats_get_top(struct server_stats *servers, int max_count);

// Get bottom N servers by success rate
int server_stats_get_bottom(struct server_stats *servers, int max_count);

// Update performance history (call periodically)
void server_stats_update_history(void);

// Calculate reliability score
void server_stats_calculate_reliability(struct server_stats *stats);

// HTTP interface
struct http_request;
void http_send_server_stats(int sock, struct http_request *req);

#endif /* SERVER_STATS_H */
