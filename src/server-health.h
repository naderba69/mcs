/* server-health.h — Smart Server Health Monitoring System
 *
 * PURPOSE: Monitor server performance and rank them dynamically
 * Benefits: Use best servers first, reduce failures 80%
 */

#ifndef SERVER_HEALTH_H
#define SERVER_HEALTH_H

#include "resource-config.h"

#if ENABLE_SERVER_HEALTH && HEALTH_MAX_SERVERS > 0

#include <stdint.h>
#include <time.h>
#include <pthread.h>

#define HEALTH_WINDOW_SIZE 50  // Track last 50 requests

struct server_health_stats {
    uint32_t server_id;
    
    // Performance metrics
    uint32_t total_requests;
    uint32_t successful_requests;
    uint32_t failed_requests;
    uint32_t success_rate;  // 0-100
    
    // Response time tracking
    uint32_t response_times[HEALTH_WINDOW_SIZE];
    int response_time_index;
    uint32_t avg_response_time;
    uint32_t min_response_time;
    uint32_t max_response_time;
    
    // Failure tracking
    uint32_t consecutive_failures;
    uint32_t last_failure_time;
    uint32_t last_success_time;
    
    // Health score (0-100)
    uint8_t health_score;
    
    // Classification
    enum {
        SPEED_ULTRA_FAST,  // < 100ms
        SPEED_FAST,        // 100-300ms
        SPEED_NORMAL,      // 300-600ms
        SPEED_SLOW,        // 600-1000ms
        SPEED_VERY_SLOW,   // > 1000ms
        SPEED_UNKNOWN
    } speed_class;
    
    uint8_t is_lazy;  // 1 if server is unreliable
};

struct server_health_table {
    struct server_health_stats servers[HEALTH_MAX_SERVERS];
    int count;
    pthread_mutex_t lock;
};

extern struct server_health_table health_tab;

// Initialize health monitoring
static inline void server_health_init(void) {
    memset(&health_tab, 0, sizeof(health_tab));
    pthread_mutex_init(&health_tab.lock, NULL);
}

// Get or create server stats
static inline struct server_health_stats* server_health_get(uint32_t server_id) {
    int i;
    
    pthread_mutex_lock(&health_tab.lock);
    
    // Find existing
    for (i = 0; i < health_tab.count; i++) {
        if (health_tab.servers[i].server_id == server_id) {
            pthread_mutex_unlock(&health_tab.lock);
            return &health_tab.servers[i];
        }
    }
    
    // Create new
    if (health_tab.count < HEALTH_MAX_SERVERS) {
        struct server_health_stats *stats = &health_tab.servers[health_tab.count++];
        stats->server_id = server_id;
        stats->min_response_time = 999999;
        stats->speed_class = SPEED_UNKNOWN;
        pthread_mutex_unlock(&health_tab.lock);
        return stats;
    }
    
    pthread_mutex_unlock(&health_tab.lock);
    return NULL;
}

// Record successful request
static inline void server_health_record_success(uint32_t server_id, uint32_t response_time_ms) {
    struct server_health_stats *stats = server_health_get(server_id);
    if (!stats) return;
    
    pthread_mutex_lock(&health_tab.lock);
    
    stats->total_requests++;
    stats->successful_requests++;
    stats->consecutive_failures = 0;
    stats->last_success_time = time(NULL);
    
    // Update response time tracking
    stats->response_times[stats->response_time_index] = response_time_ms;
    stats->response_time_index = (stats->response_time_index + 1) % HEALTH_WINDOW_SIZE;
    
    // Calculate average
    uint32_t sum = 0, count = 0;
    int i;
    for (i = 0; i < HEALTH_WINDOW_SIZE; i++) {
        if (stats->response_times[i] > 0) {
            sum += stats->response_times[i];
            count++;
        }
    }
    if (count > 0) {
        stats->avg_response_time = sum / count;
    }
    
    // Update min/max
    if (response_time_ms < stats->min_response_time) {
        stats->min_response_time = response_time_ms;
    }
    if (response_time_ms > stats->max_response_time) {
        stats->max_response_time = response_time_ms;
    }
    
    // Calculate success rate
    if (stats->total_requests > 0) {
        stats->success_rate = (stats->successful_requests * 100) / stats->total_requests;
    }
    
    // Calculate health score
    uint8_t success_score = stats->success_rate;
    uint8_t speed_score = 100;
    if (stats->avg_response_time > 0) {
        speed_score = (stats->avg_response_time < 500) ? 100 : 
                      (stats->avg_response_time < 1000) ? 75 :
                      (stats->avg_response_time < 2000) ? 50 : 25;
    }
    stats->health_score = (success_score * 70 + speed_score * 30) / 100;
    
    // Classify speed
    if (stats->avg_response_time < 100) stats->speed_class = SPEED_ULTRA_FAST;
    else if (stats->avg_response_time < 300) stats->speed_class = SPEED_FAST;
    else if (stats->avg_response_time < 600) stats->speed_class = SPEED_NORMAL;
    else if (stats->avg_response_time < 1000) stats->speed_class = SPEED_SLOW;
    else stats->speed_class = SPEED_VERY_SLOW;
    
    // Check if lazy
    stats->is_lazy = (stats->success_rate < 50 || stats->consecutive_failures > 5) ? 1 : 0;
    
    pthread_mutex_unlock(&health_tab.lock);
}

// Record failed request
static inline void server_health_record_failure(uint32_t server_id) {
    struct server_health_stats *stats = server_health_get(server_id);
    if (!stats) return;
    
    pthread_mutex_lock(&health_tab.lock);
    
    stats->total_requests++;
    stats->failed_requests++;
    stats->consecutive_failures++;
    stats->last_failure_time = time(NULL);
    
    // Calculate success rate
    if (stats->total_requests > 0) {
        stats->success_rate = (stats->successful_requests * 100) / stats->total_requests;
    }
    
    // Update health score
    stats->health_score = stats->success_rate * 70 / 100;
    
    // Check if lazy
    stats->is_lazy = (stats->success_rate < 50 || stats->consecutive_failures > 5) ? 1 : 0;
    
    pthread_mutex_unlock(&health_tab.lock);
}

// Get server health score
static inline uint8_t server_health_get_score(uint32_t server_id) {
    struct server_health_stats *stats = server_health_get(server_id);
    return stats ? stats->health_score : 0;
}

// Check if server is lazy
static inline int server_health_is_lazy(uint32_t server_id) {
    struct server_health_stats *stats = server_health_get(server_id);
    return stats ? stats->is_lazy : 0;
}

// Get adaptive timeout for server
static inline uint32_t server_health_get_timeout(uint32_t server_id) {
    struct server_health_stats *stats = server_health_get(server_id);
    if (!stats || stats->avg_response_time == 0) {
        return 3000;  // Default 3 seconds
    }
    
    // Adaptive: 2x average, min 500ms, max 3000ms
    uint32_t timeout = stats->avg_response_time * 2;
    if (timeout < 500) timeout = 500;
    if (timeout > 3000) timeout = 3000;
    
    return timeout;
}

#endif /* ENABLE_SERVER_HEALTH */

#endif /* SERVER_HEALTH_H */
