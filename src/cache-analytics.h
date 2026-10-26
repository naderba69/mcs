/* cache-analytics.h - Advanced Cache Analytics
 * 
 * PURPOSE: Analyze cache performance and provide insights
 * 
 * FEATURES:
 * - Cache hit rate per CAID
 * - Most requested channels
 * - CW lifetime in cache
 * - Cache efficiency suggestions
 * - Export analytics data
 */

#ifndef CACHE_ANALYTICS_H
#define CACHE_ANALYTICS_H

#include <stdint.h>
#include <time.h>
#include <pthread.h>

// Maximum CAIDs to track
#define CACHE_ANALYTICS_MAX_CAIDS 200

// Maximum channels to track
#define CACHE_ANALYTICS_MAX_CHANNELS 500

// Cache statistics per CAID
struct cache_caid_stats {
    uint16_t caid;
    char caid_name[64];
    
    // Hit statistics
    uint32_t total_requests;
    uint32_t cache_hits;
    uint32_t cache_misses;
    uint32_t hit_rate;  // 0-100%
    
    // CW statistics
    uint32_t total_cw_received;
    uint32_t cw_from_cache;
    uint32_t cw_from_server;
    
    // Performance
    uint32_t avg_cache_hit_time;  // ms
    uint32_t avg_server_time;     // ms
    
    // Last activity
    time_t last_request_time;
    time_t last_hit_time;
};

// Channel statistics
struct cache_channel_stats {
    uint16_t caid;
    uint32_t provid;
    uint16_t sid;
    char channel_name[128];
    
    // Request statistics
    uint32_t total_requests;
    uint32_t cache_hits;
    uint32_t hit_rate;  // 0-100%
    
    // Last activity
    time_t last_request_time;
};

// Cache analytics table
struct cache_analytics_table {
    struct cache_caid_stats caids[CACHE_ANALYTICS_MAX_CAIDS];
    int caid_count;
    
    struct cache_channel_stats channels[CACHE_ANALYTICS_MAX_CHANNELS];
    int channel_count;
    
    pthread_mutex_t lock;
    
    // Global statistics
    uint32_t total_requests;
    uint32_t total_cache_hits;
    uint32_t total_cache_misses;
    uint32_t global_hit_rate;
    
    // Time tracking
    time_t start_time;
    time_t last_update;
};

extern struct cache_analytics_table cache_analytics_tab;

// Initialize cache analytics
void cache_analytics_init(void);

// Record cache request
void cache_analytics_record_request(uint16_t caid, uint32_t provid, uint16_t sid,
                                   int cache_hit, uint32_t response_time);

// Update CAID name
void cache_analytics_set_caid_name(uint16_t caid, const char *name);

// Update channel name
void cache_analytics_set_channel_name(uint16_t caid, uint32_t provid, uint16_t sid,
                                     const char *name);

// Get CAID statistics
int cache_analytics_get_caid_stats(struct cache_caid_stats *stats, int max_count);

// Get top channels by request count
int cache_analytics_get_top_channels(struct cache_channel_stats *stats, int max_count);

// Get top channels by cache hit rate
int cache_analytics_get_best_cached(struct cache_channel_stats *stats, int max_count);

// Get worst channels by cache hit rate
int cache_analytics_get_worst_cached(struct cache_channel_stats *stats, int max_count);

// Calculate global statistics
void cache_analytics_update_global(void);

// Get suggestions for cache optimization
int cache_analytics_get_suggestions(char suggestions[][256], int max_suggestions);

// Clear all analytics
void cache_analytics_clear(void);

// Export analytics to CSV
int cache_analytics_export_csv(const char *filename);

// HTTP interface
struct http_request;
void http_send_cache_analytics(int sock, struct http_request *req);

#endif /* CACHE_ANALYTICS_H */
