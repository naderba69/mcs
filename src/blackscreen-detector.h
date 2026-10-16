/*
 * Black Screen Detector - Advanced Detection & Quick Recovery
 * 
 * Detects black screen conditions and triggers quick recovery:
 * - CW timeout detection (> 3 seconds)
 * - Consecutive failure tracking
 * - Quick switch to alternate sources
 * - Broadcast ECM to all servers on persistent failure
 * 
 * Features:
 * - Early black screen detection
 * - Automatic source switching
 * - Intelligent retry logic
 * - Statistics tracking
 */

#ifndef BLACKSCREEN_DETECTOR_H
#define BLACKSCREEN_DETECTOR_H

#include <stdint.h>
#include <string.h>
#include <pthread.h>

// ============================================================================
// Configuration
// ============================================================================

#define BS_MAX_TRACKED_CHANNELS 256
#define BS_TIMEOUT_MS 3000           // 3 seconds CW timeout
#define BS_QUICK_SWITCH_THRESHOLD 2  // Switch after 2 failures
#define BS_BROADCAST_THRESHOLD 4     // Broadcast after 4 failures
#define BS_RECOVERY_WINDOW_MS 10000  // 10 seconds recovery window

// ============================================================================
// Data Structures
// ============================================================================

struct bs_channel_stats {
    uint16_t caid;
    uint32_t provid;
    uint16_t sid;
    
    // Failure tracking
    uint32_t last_ecm_time;
    uint32_t last_cw_time;
    int consecutive_failures;
    int total_failures;
    int total_successes;
    
    // Recovery state
    int in_recovery;
    uint32_t recovery_start_time;
    int quick_switch_attempted;
    int broadcast_attempted;
    
    // Performance
    uint32_t avg_response_time_ms;
    uint32_t last_response_time_ms;
    
    // Active flag
    int active;
};

struct bs_global_stats {
    uint64_t total_blackscreens_detected;
    uint64_t total_quick_switches;
    uint64_t total_broadcasts;
    uint64_t total_recoveries;
    uint64_t total_permanent_failures;
    
    uint32_t active_channels;
    uint32_t channels_in_recovery;
};

// ============================================================================
// Global State
// ============================================================================

static struct bs_channel_stats bs_channels[BS_MAX_TRACKED_CHANNELS];
static int bs_channel_count = 0;
static struct bs_global_stats bs_stats;
static pthread_mutex_t bs_lock = PTHREAD_MUTEX_INITIALIZER;
static int bs_enabled = 1;

// ============================================================================
// Channel Tracking
// ============================================================================

static struct bs_channel_stats* bs_get_channel(uint16_t caid, uint32_t provid, uint16_t sid)
{
    int i;
    
    // Find existing
    for (i = 0; i < bs_channel_count; i++) {
        if (bs_channels[i].active &&
            bs_channels[i].caid == caid &&
            bs_channels[i].provid == provid &&
            bs_channels[i].sid == sid) {
            return &bs_channels[i];
        }
    }
    
    // Create new
    if (bs_channel_count < BS_MAX_TRACKED_CHANNELS) {
        struct bs_channel_stats *ch = &bs_channels[bs_channel_count++];
        memset(ch, 0, sizeof(*ch));
        ch->caid = caid;
        ch->provid = provid;
        ch->sid = sid;
        ch->active = 1;
        bs_stats.active_channels++;
        return ch;
    }
    
    // Find inactive slot
    for (i = 0; i < BS_MAX_TRACKED_CHANNELS; i++) {
        if (!bs_channels[i].active) {
            struct bs_channel_stats *ch = &bs_channels[i];
            memset(ch, 0, sizeof(*ch));
            ch->caid = caid;
            ch->provid = provid;
            ch->sid = sid;
            ch->active = 1;
            bs_stats.active_channels++;
            return ch;
        }
    }
    
    return NULL;
}

// ============================================================================
// Detection Functions
// ============================================================================

/*
 * Record ECM request
 */
static void bs_record_ecm_request(uint16_t caid, uint32_t provid, uint16_t sid, uint32_t now_ms)
{
    if (!bs_enabled) return;
    
    pthread_mutex_lock(&bs_lock);
    
    struct bs_channel_stats *ch = bs_get_channel(caid, provid, sid);
    if (ch) {
        ch->last_ecm_time = now_ms;
    }
    
    pthread_mutex_unlock(&bs_lock);
}

/*
 * Record successful CW reception
 */
static void bs_record_cw_success(uint16_t caid, uint32_t provid, uint16_t sid,
                                  uint32_t now_ms, uint32_t response_time_ms)
{
    if (!bs_enabled) return;
    
    pthread_mutex_lock(&bs_lock);
    
    struct bs_channel_stats *ch = bs_get_channel(caid, provid, sid);
    if (ch) {
        ch->last_cw_time = now_ms;
        ch->consecutive_failures = 0;
        ch->total_successes++;
        ch->last_response_time_ms = response_time_ms;
        
        // Update average response time
        if (ch->avg_response_time_ms == 0) {
            ch->avg_response_time_ms = response_time_ms;
        } else {
            ch->avg_response_time_ms = (ch->avg_response_time_ms * 7 + response_time_ms * 3) / 10;
        }
        
        // Clear recovery state
        if (ch->in_recovery) {
            ch->in_recovery = 0;
            bs_stats.total_recoveries++;
            bs_stats.channels_in_recovery--;
        }
    }
    
    pthread_mutex_unlock(&bs_lock);
}

/*
 * Record CW failure
 * Returns: action to take
 *   0 = no action
 *   1 = quick switch to alternate source
 *   2 = broadcast to all servers
 *   3 = permanent failure (give up)
 */
static int bs_record_cw_failure(uint16_t caid, uint32_t provid, uint16_t sid, uint32_t now_ms)
{
    if (!bs_enabled) return 0;
    
    pthread_mutex_lock(&bs_lock);
    
    struct bs_channel_stats *ch = bs_get_channel(caid, provid, sid);
    if (!ch) {
        pthread_mutex_unlock(&bs_lock);
        return 0;
    }
    
    ch->consecutive_failures++;
    ch->total_failures++;
    
    int action = 0;
    
    // Check for black screen condition
    uint32_t time_since_last_cw = now_ms - ch->last_cw_time;
    
    if (time_since_last_cw > BS_TIMEOUT_MS) {
        // Black screen detected!
        bs_stats.total_blackscreens_detected++;
        
        // Enter recovery mode
        if (!ch->in_recovery) {
            ch->in_recovery = 1;
            ch->recovery_start_time = now_ms;
            bs_stats.channels_in_recovery++;
        }
        
        // Decide action based on failure count
        if (ch->consecutive_failures >= BS_BROADCAST_THRESHOLD) {
            // Broadcast to all servers
            if (!ch->broadcast_attempted) {
                ch->broadcast_attempted = 1;
                action = 2;
                bs_stats.total_broadcasts++;
            } else if (ch->consecutive_failures >= BS_BROADCAST_THRESHOLD + 3) {
                // Give up after broadcast + 3 more failures
                action = 3;
                bs_stats.total_permanent_failures++;
            }
        }
        else if (ch->consecutive_failures >= BS_QUICK_SWITCH_THRESHOLD) {
            // Quick switch to alternate source
            if (!ch->quick_switch_attempted) {
                ch->quick_switch_attempted = 1;
                action = 1;
                bs_stats.total_quick_switches++;
            }
        }
    }
    
    // Check if recovery window expired
    if (ch->in_recovery && (now_ms - ch->recovery_start_time) > BS_RECOVERY_WINDOW_MS) {
        // Reset recovery state for next attempt
        ch->quick_switch_attempted = 0;
        ch->broadcast_attempted = 0;
        ch->recovery_start_time = now_ms;
    }
    
    pthread_mutex_unlock(&bs_lock);
    return action;
}

/*
 * Check if channel is in black screen condition
 */
static int bs_is_blackscreen(uint16_t caid, uint32_t provid, uint16_t sid, uint32_t now_ms)
{
    if (!bs_enabled) return 0;
    
    pthread_mutex_lock(&bs_lock);
    
    struct bs_channel_stats *ch = bs_get_channel(caid, provid, sid);
    if (!ch) {
        pthread_mutex_unlock(&bs_lock);
        return 0;
    }
    
    uint32_t time_since_last_cw = now_ms - ch->last_cw_time;
    int is_bs = (time_since_last_cw > BS_TIMEOUT_MS && ch->consecutive_failures >= 2);
    
    pthread_mutex_unlock(&bs_lock);
    return is_bs;
}

/*
 * Get channel performance metrics
 */
static int bs_get_channel_stats(uint16_t caid, uint32_t provid, uint16_t sid,
                                 struct bs_channel_stats *out)
{
    pthread_mutex_lock(&bs_lock);
    
    struct bs_channel_stats *ch = bs_get_channel(caid, provid, sid);
    if (ch) {
        memcpy(out, ch, sizeof(*out));
        pthread_mutex_unlock(&bs_lock);
        return 1;
    }
    
    pthread_mutex_unlock(&bs_lock);
    return 0;
}

/*
 * Get global statistics
 */
static void bs_get_global_stats(struct bs_global_stats *out)
{
    pthread_mutex_lock(&bs_lock);
    memcpy(out, &bs_stats, sizeof(*out));
    pthread_mutex_unlock(&bs_lock);
}

/*
 * Reset channel statistics
 */
static void bs_reset_channel(uint16_t caid, uint32_t provid, uint16_t sid)
{
    pthread_mutex_lock(&bs_lock);
    
    struct bs_channel_stats *ch = bs_get_channel(caid, provid, sid);
    if (ch) {
        ch->consecutive_failures = 0;
        ch->total_failures = 0;
        ch->total_successes = 0;
        ch->in_recovery = 0;
        ch->quick_switch_attempted = 0;
        ch->broadcast_attempted = 0;
    }
    
    pthread_mutex_unlock(&bs_lock);
}

// ============================================================================
// Initialization
// ============================================================================

static void bs_init(void)
{
    memset(bs_channels, 0, sizeof(bs_channels));
    memset(&bs_stats, 0, sizeof(bs_stats));
    bs_channel_count = 0;
    bs_enabled = 1;
}

#endif // BLACKSCREEN_DETECTOR_H
