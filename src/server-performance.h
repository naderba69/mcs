/*
 * Server Performance Tracker - Intelligent Server Selection
 * 
 * Tracks and ranks servers by performance:
 * - Response time tracking (avg, min, max, last)
 * - Success rate calculation
 * - Automatic server ranking
 * - Smart server selection based on history
 * 
 * Features:
 * - Real-time performance metrics
 * - Weighted scoring system
 * - Channel-specific performance tracking
 * - Automatic degradation detection
 */

#ifndef SERVER_PERFORMANCE_H
#define SERVER_PERFORMANCE_H

#include <stdint.h>
#include <string.h>
#include <pthread.h>

// ============================================================================
// Configuration
// ============================================================================

#define SP_MAX_SERVERS 128
#define SP_MAX_CHANNELS_PER_SERVER 64
#define SP_HISTORY_WINDOW 100  // Last 100 requests per server
#define SP_DEGRADATION_THRESHOLD 5000  // 5 seconds = degraded

// ============================================================================
// Data Structures
// ============================================================================

struct sp_channel_perf {
    uint16_t caid;
    uint32_t provid;
    uint16_t sid;
    
    uint32_t total_requests;
    uint32_t total_successes;
    uint32_t avg_response_ms;
    uint32_t last_response_ms;
    uint32_t min_response_ms;
    uint32_t max_response_ms;
    
    uint32_t last_success_time;
    int active;
};

struct sp_server_stats {
    uint32_t server_id;
    char server_name[64];
    
    // Global performance
    uint64_t total_requests;
    uint64_t total_successes;
    uint32_t avg_response_ms;
    uint32_t last_response_ms;
    uint32_t min_response_ms;
    uint32_t max_response_ms;
    
    // Recent performance (last 100 requests)
    uint32_t recent_responses[SP_HISTORY_WINDOW];
    int recent_index;
    int recent_count;
    
    // Success rate
    int success_rate_percent;  // 0-100
    
    // Scoring
    int score;  // 0-1000 (higher = better)
    int rank;   // 1 = best
    
    // Status
    int is_degraded;
    int is_active;
    uint32_t last_activity;
    
    // Channel-specific performance
    struct sp_channel_perf channels[SP_MAX_CHANNELS_PER_SERVER];
    int channel_count;
};

struct sp_global_stats {
    uint32_t total_servers;
    uint32_t active_servers;
    uint32_t degraded_servers;
    
    uint64_t total_requests;
    uint64_t total_successes;
    
    uint32_t avg_response_global;
    int avg_success_rate;
};

// ============================================================================
// Global State
// ============================================================================

static struct sp_server_stats sp_servers[SP_MAX_SERVERS];
static int sp_server_count = 0;
static struct sp_global_stats sp_global;
static pthread_mutex_t sp_lock = PTHREAD_MUTEX_INITIALIZER;
static int sp_enabled = 1;

// ============================================================================
// Server Management
// ============================================================================

static struct sp_server_stats* sp_get_server(uint32_t server_id)
{
    int i;
    for (i = 0; i < sp_server_count; i++) {
        if (sp_servers[i].server_id == server_id && sp_servers[i].is_active) {
            return &sp_servers[i];
        }
    }
    return NULL;
}

static struct sp_server_stats* sp_create_server(uint32_t server_id, const char *name)
{
    if (sp_server_count >= SP_MAX_SERVERS) return NULL;
    
    struct sp_server_stats *srv = &sp_servers[sp_server_count++];
    memset(srv, 0, sizeof(*srv));
    srv->server_id = server_id;
    if (name) strncpy(srv->server_name, name, sizeof(srv->server_name)-1);
    srv->is_active = 1;
    srv->min_response_ms = 0xFFFFFFFF;
    
    sp_global.total_servers++;
    sp_global.active_servers++;
    
    return srv;
}

// ============================================================================
// Performance Recording
// ============================================================================

/*
 * Record successful CW response from server
 */
static void sp_record_success(uint32_t server_id, uint16_t caid, uint32_t provid,
                               uint16_t sid, uint32_t response_ms, uint32_t now)
{
    if (!sp_enabled) return;
    
    pthread_mutex_lock(&sp_lock);
    
    struct sp_server_stats *srv = sp_get_server(server_id);
    if (!srv) {
        srv = sp_create_server(server_id, NULL);
    }
    if (!srv) {
        pthread_mutex_unlock(&sp_lock);
        return;
    }
    
    // Update global stats
    srv->total_requests++;
    srv->total_successes++;
    srv->last_response_ms = response_ms;
    srv->last_activity = now;
    
    if (response_ms < srv->min_response_ms) srv->min_response_ms = response_ms;
    if (response_ms > srv->max_response_ms) srv->max_response_ms = response_ms;
    
    // Update average (weighted)
    if (srv->avg_response_ms == 0) {
        srv->avg_response_ms = response_ms;
    } else {
        srv->avg_response_ms = (srv->avg_response_ms * 7 + response_ms * 3) / 10;
    }
    
    // Update recent history
    srv->recent_responses[srv->recent_index] = response_ms;
    srv->recent_index = (srv->recent_index + 1) % SP_HISTORY_WINDOW;
    if (srv->recent_count < SP_HISTORY_WINDOW) srv->recent_count++;
    
    // Update success rate
    srv->success_rate_percent = (int)((srv->total_successes * 100) / srv->total_requests);
    
    // Check degradation
    srv->is_degraded = (srv->avg_response_ms > SP_DEGRADATION_THRESHOLD);
    
    // Update channel-specific stats
    struct sp_channel_perf *ch = NULL;
    int i;
    for (i = 0; i < srv->channel_count; i++) {
        if (srv->channels[i].caid == caid &&
            srv->channels[i].provid == provid &&
            srv->channels[i].sid == sid) {
            ch = &srv->channels[i];
            break;
        }
    }
    
    if (!ch && srv->channel_count < SP_MAX_CHANNELS_PER_SERVER) {
        ch = &srv->channels[srv->channel_count++];
        memset(ch, 0, sizeof(*ch));
        ch->caid = caid;
        ch->provid = provid;
        ch->sid = sid;
        ch->min_response_ms = 0xFFFFFFFF;
        ch->active = 1;
    }
    
    if (ch) {
        ch->total_requests++;
        ch->total_successes++;
        ch->last_response_ms = response_ms;
        ch->last_success_time = now;
        
        if (response_ms < ch->min_response_ms) ch->min_response_ms = response_ms;
        if (response_ms > ch->max_response_ms) ch->max_response_ms = response_ms;
        
        if (ch->avg_response_ms == 0) {
            ch->avg_response_ms = response_ms;
        } else {
            ch->avg_response_ms = (ch->avg_response_ms * 7 + response_ms * 3) / 10;
        }
    }
    
    // Update global stats
    sp_global.total_requests++;
    sp_global.total_successes++;
    
    pthread_mutex_unlock(&sp_lock);
}

/*
 * Record failed CW response from server
 */
static void sp_record_failure(uint32_t server_id, uint16_t caid, uint32_t provid,
                               uint16_t sid, uint32_t now)
{
    if (!sp_enabled) return;
    
    pthread_mutex_lock(&sp_lock);
    
    struct sp_server_stats *srv = sp_get_server(server_id);
    if (!srv) {
        pthread_mutex_unlock(&sp_lock);
        return;
    }
    
    srv->total_requests++;
    srv->last_activity = now;
    
    // Update success rate
    srv->success_rate_percent = (int)((srv->total_successes * 100) / srv->total_requests);
    
    // Update channel-specific stats
    int i;
    for (i = 0; i < srv->channel_count; i++) {
        if (srv->channels[i].caid == caid &&
            srv->channels[i].provid == provid &&
            srv->channels[i].sid == sid) {
            srv->channels[i].total_requests++;
            break;
        }
    }
    
    sp_global.total_requests++;
    
    pthread_mutex_unlock(&sp_lock);
}

// ============================================================================
// Server Selection
// ============================================================================

/*
 * Calculate server score (0-1000)
 * Higher = better
 */
static int sp_calculate_score(struct sp_server_stats *srv)
{
    int score = 1000;
    
    // Penalty for slow response (max -500)
    if (srv->avg_response_ms > 0) {
        int penalty = (srv->avg_response_ms / 10);  // 1 point per 10ms
        if (penalty > 500) penalty = 500;
        score -= penalty;
    }
    
    // Penalty for low success rate (max -400)
    if (srv->success_rate_percent < 100) {
        int penalty = (100 - srv->success_rate_percent) * 4;
        score -= penalty;
    }
    
    // Bonus for recent good performance
    if (srv->recent_count > 10 && srv->last_response_ms < srv->avg_response_ms) {
        score += 50;
    }
    
    // Penalty for degradation
    if (srv->is_degraded) {
        score -= 200;
    }
    
    if (score < 0) score = 0;
    if (score > 1000) score = 1000;
    
    return score;
}

/*
 * Get best server for a channel
 * Returns: server_id or 0 if no server available
 */
static uint32_t sp_get_best_server(uint16_t caid, uint32_t provid, uint16_t sid)
{
    if (!sp_enabled) return 0;
    
    pthread_mutex_lock(&sp_lock);
    
    uint32_t best_server = 0;
    int best_score = -1;
    int i;
    
    for (i = 0; i < sp_server_count; i++) {
        struct sp_server_stats *srv = &sp_servers[i];
        if (!srv->is_active) continue;
        
        // Check if server has history with this channel
        int has_channel_history = 0;
        int j;
        for (j = 0; j < srv->channel_count; j++) {
            if (srv->channels[j].caid == caid &&
                srv->channels[j].provid == provid &&
                srv->channels[j].sid == sid) {
                has_channel_history = 1;
                
                // Use channel-specific score
                int channel_score = sp_calculate_score(srv);
                
                // Bonus for channel-specific success
                if (srv->channels[j].total_successes > 0) {
                    int channel_success = (int)((srv->channels[j].total_successes * 100) /
                                                srv->channels[j].total_requests);
                    channel_score += (channel_success - srv->success_rate_percent);
                }
                
                if (channel_score > best_score) {
                    best_score = channel_score;
                    best_server = srv->server_id;
                }
                break;
            }
        }
        
        // If no channel history, use global score
        if (!has_channel_history) {
            int global_score = sp_calculate_score(srv);
            if (global_score > best_score) {
                best_score = global_score;
                best_server = srv->server_id;
            }
        }
    }
    
    pthread_mutex_unlock(&sp_lock);
    return best_server;
}

/*
 * Get server rank (1 = best)
 */
static int sp_get_server_rank(uint32_t server_id)
{
    pthread_mutex_lock(&sp_lock);
    
    struct sp_server_stats *target = sp_get_server(server_id);
    if (!target) {
        pthread_mutex_unlock(&sp_lock);
        return -1;
    }
    
    int target_score = sp_calculate_score(target);
    int rank = 1;
    int i;
    
    for (i = 0; i < sp_server_count; i++) {
        if (sp_servers[i].server_id != server_id && sp_servers[i].is_active) {
            int score = sp_calculate_score(&sp_servers[i]);
            if (score > target_score) {
                rank++;
            }
        }
    }
    
    pthread_mutex_unlock(&sp_lock);
    return rank;
}

/*
 * Get statistics
 */
static void sp_get_global_stats(struct sp_global_stats *out)
{
    pthread_mutex_lock(&sp_lock);
    
    memcpy(out, &sp_global, sizeof(*out));
    
    // Calculate averages
    if (sp_global.total_requests > 0) {
        out->avg_success_rate = (int)((sp_global.total_successes * 100) / sp_global.total_requests);
    }
    
    pthread_mutex_unlock(&sp_lock);
}

// ============================================================================
// Initialization
// ============================================================================

static void sp_init(void)
{
    memset(sp_servers, 0, sizeof(sp_servers));
    memset(&sp_global, 0, sizeof(sp_global));
    sp_server_count = 0;
    sp_enabled = 1;
}

#endif // SERVER_PERFORMANCE_H
