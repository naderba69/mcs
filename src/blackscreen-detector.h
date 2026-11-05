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
#include "tick_math.h"

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
    uint32_t last_used;
    uint8_t has_last_ecm;
    uint8_t has_last_cw;
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

static struct bs_channel_stats* bs_find_channel(uint16_t caid, uint32_t provid,
                                                uint16_t sid)
{
    int i;
    for (i = 0; i < bs_channel_count; i++) {
        if (bs_channels[i].active && bs_channels[i].caid == caid &&
            bs_channels[i].provid == provid && bs_channels[i].sid == sid)
            return &bs_channels[i];
    }
    return NULL;
}

static struct bs_channel_stats* bs_get_channel(uint16_t caid, uint32_t provid,
                                                uint16_t sid, uint32_t now_ms)
{
    struct bs_channel_stats *ch = bs_find_channel(caid, provid, sid);
    int i;
    if (ch) {
        ch->last_used = now_ms;
        return ch;
    }

    if (bs_channel_count < BS_MAX_TRACKED_CHANNELS) {
        ch = &bs_channels[bs_channel_count++];
        memset(ch, 0, sizeof(*ch));
        ch->caid = caid;
        ch->provid = provid;
        ch->sid = sid;
        ch->last_used = now_ms;
        ch->active = 1;
        bs_stats.active_channels++;
        return ch;
    }

    /* Prefer a free slot if one was deactivated by a future lifecycle hook. */
    for (i = 0; i < BS_MAX_TRACKED_CHANNELS; i++) {
        if (!bs_channels[i].active) {
            ch = &bs_channels[i];
            memset(ch, 0, sizeof(*ch));
            ch->caid = caid;
            ch->provid = provid;
            ch->sid = sid;
            ch->last_used = now_ms;
            ch->active = 1;
            bs_stats.active_channels++;
            return ch;
        }
    }

    /* Full table: replace the channel with greatest modulo elapsed age. */
    {
        int oldest = 0;
        uint32_t oldest_age = tick32_elapsed(now_ms, bs_channels[0].last_used);
        for (i = 1; i < BS_MAX_TRACKED_CHANNELS; i++) {
            uint32_t age = tick32_elapsed(now_ms, bs_channels[i].last_used);
            if (age > oldest_age) {
                oldest_age = age;
                oldest = i;
            }
        }
        ch = &bs_channels[oldest];
        memset(ch, 0, sizeof(*ch));
        ch->caid = caid;
        ch->provid = provid;
        ch->sid = sid;
        ch->last_used = now_ms;
        ch->active = 1;
    }
    return ch;
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
    
    struct bs_channel_stats *ch = bs_get_channel(caid, provid, sid, now_ms);
    if (ch) {
        ch->last_ecm_time = now_ms;
        ch->has_last_ecm = 1;
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
    
    struct bs_channel_stats *ch = bs_get_channel(caid, provid, sid, now_ms);
    if (ch) {
        ch->last_cw_time = now_ms;
        ch->has_last_cw = 1;
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
            if (bs_stats.channels_in_recovery > 0)
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
    
    struct bs_channel_stats *ch = bs_get_channel(caid, provid, sid, now_ms);
    if (!ch) {
        pthread_mutex_unlock(&bs_lock);
        return 0;
    }
    
    ch->consecutive_failures++;
    ch->total_failures++;
    
    int action = 0;
    
    // Use the last successful CW, or the first ECM time before any CW exists.
    uint32_t baseline = now_ms;
    int has_baseline = 0;
    uint32_t time_since_last_cw = 0;
    if (ch->has_last_cw) {
        baseline = ch->last_cw_time;
        has_baseline = 1;
    } else if (ch->has_last_ecm) {
        baseline = ch->last_ecm_time;
        has_baseline = 1;
    }
    if (has_baseline) time_since_last_cw = tick32_elapsed(now_ms, baseline);
    
    if (has_baseline && time_since_last_cw > BS_TIMEOUT_MS) {
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
    if (ch->in_recovery &&
        tick32_elapsed(now_ms, ch->recovery_start_time) > BS_RECOVERY_WINDOW_MS) {
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
    
    struct bs_channel_stats *ch = bs_find_channel(caid, provid, sid);
    if (!ch) {
        pthread_mutex_unlock(&bs_lock);
        return 0;
    }
    ch->last_used = now_ms;
    
    uint32_t baseline;
    int has_baseline = 0;
    uint32_t time_since_last_cw = 0;
    if (ch->has_last_cw) {
        baseline = ch->last_cw_time;
        has_baseline = 1;
    } else if (ch->has_last_ecm) {
        baseline = ch->last_ecm_time;
        has_baseline = 1;
    } else {
        baseline = now_ms;
    }
    if (has_baseline) time_since_last_cw = tick32_elapsed(now_ms, baseline);
    int is_bs = (has_baseline && time_since_last_cw > BS_TIMEOUT_MS &&
                 ch->consecutive_failures >= 2);
    
    pthread_mutex_unlock(&bs_lock);
    return is_bs;
}

/*
 * Get channel performance metrics
 */
static int bs_get_channel_stats(uint16_t caid, uint32_t provid, uint16_t sid,
                                 struct bs_channel_stats *out)
{
    if (!out) return 0;
    pthread_mutex_lock(&bs_lock);
    
    struct bs_channel_stats *ch = bs_find_channel(caid, provid, sid);
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
    if (!out) return;
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
    
    struct bs_channel_stats *ch = bs_find_channel(caid, provid, sid);
    if (ch) {
        ch->consecutive_failures = 0;
        ch->total_failures = 0;
        ch->total_successes = 0;
        if (ch->in_recovery && bs_stats.channels_in_recovery > 0)
            bs_stats.channels_in_recovery--;
        ch->in_recovery = 0;
        ch->has_last_cw = 0;
        ch->has_last_ecm = 0;
        ch->last_cw_time = 0;
        ch->last_ecm_time = 0;
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
