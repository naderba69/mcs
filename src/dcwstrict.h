/*
 * DCW STRICT - Advanced DCW Validation System
 * 
 * Three-stage validation:
 * 1. Entropy check (reject CW with < 5 unique bytes)
 * 2. All-same-byte check (reject CW with all identical bytes)
 * 3. Stale/recycled CW check (reject CW seen within 5 seconds for same channel)
 * 
 * Plus: Trust score penalty system (-20 per fake, +2 per valid)
 */

#ifndef DCWSTRICT_H
#define DCWSTRICT_H

#include <stdint.h>
#include <string.h>
#include <pthread.h>

// ============================================================================
// Configuration
// ============================================================================

#define DCWSTRICT_MAX_STALE_ENTRIES 1024
#define DCWSTRICT_STALE_WINDOW_MS 5000

struct dcwstrict_config {
    int enabled;           // 0=off, 1=on
    int log_rejected;      // Log rejected CWs
    int trustscore;        // Enable trust score penalty
};

// ============================================================================
// Data Structures
// ============================================================================

struct dcwstrict_stale_entry {
    uint8_t cw[16];
    uint16_t caid;
    uint32_t provid;
    uint16_t sid;
    uint32_t timestamp_ms;
    uint32_t hit_count;
};

struct dcwstrict_stats {
    uint64_t total_checked;
    uint64_t total_rejected;
    uint64_t rejected_entropy;
    uint64_t rejected_allsamebyte;
    uint64_t rejected_stale;
    uint64_t trust_penalties;
    uint64_t trust_rewards;
};

// ============================================================================
// Global State
// ============================================================================

extern struct dcwstrict_config dcwstrict_cfg;
static struct dcwstrict_stale_entry dcwstrict_stale_table[DCWSTRICT_MAX_STALE_ENTRIES];
static int dcwstrict_stale_count = 0;
static struct dcwstrict_stats dcwstrict_stats;
static pthread_mutex_t dcwstrict_lock = PTHREAD_MUTEX_INITIALIZER;

// ============================================================================
// Validation Functions
// ============================================================================

/*
 * Calculate entropy (number of unique bytes)
 * Returns: count of unique bytes (0-16)
 */
static inline int dcwstrict_entropy(const uint8_t cw[16])
{
    int byte_seen[256] = {0};
    int unique_count = 0;
    int i;
    
    for (i = 0; i < 16; i++) {
        if (!byte_seen[cw[i]]) {
            byte_seen[cw[i]] = 1;
            unique_count++;
        }
    }
    
    return unique_count;
}

/*
 * Check if all bytes are identical
 * Returns: 1 if all same, 0 if different
 */
static inline int dcwstrict_allsamebyte(const uint8_t cw[16])
{
    int i;
    for (i = 1; i < 16; i++) {
        if (cw[i] != cw[0]) {
            return 0;  // Different bytes found
        }
    }
    return 1;  // All same
}

/*
 * Check if CW is stale/recycled (seen recently for same channel)
 * Returns: 1 if stale, 0 if fresh
 */
static inline int dcwstrict_check_stale(const uint8_t cw[16], uint16_t caid,
                                         uint32_t provid, uint16_t sid, uint32_t now_ms)
{
    int i;
    
    for (i = 0; i < dcwstrict_stale_count; i++) {
        struct dcwstrict_stale_entry *e = &dcwstrict_stale_table[i];
        
        // Check time window
        if ((now_ms - e->timestamp_ms) > DCWSTRICT_STALE_WINDOW_MS) {
            continue;  // Expired
        }
        
        // Check channel match
        if (e->caid == caid && e->provid == provid && e->sid == sid) {
            // Check CW match
            if (memcmp(e->cw, cw, 16) == 0) {
                e->hit_count++;
                return 1;  // Stale CW detected
            }
        }
    }
    
    return 0;  // Fresh CW
}

/*
 * Add CW to stale tracking table
 */
static inline void dcwstrict_add_stale(const uint8_t cw[16], uint16_t caid,
                                        uint32_t provid, uint16_t sid, uint32_t now_ms)
{
    // Find empty or expired slot
    int i;
    int slot = -1;
    
    for (i = 0; i < DCWSTRICT_MAX_STALE_ENTRIES; i++) {
        struct dcwstrict_stale_entry *e = &dcwstrict_stale_table[i];
        
        if (e->timestamp_ms == 0 || (now_ms - e->timestamp_ms) > DCWSTRICT_STALE_WINDOW_MS) {
            slot = i;
            break;
        }
    }
    
    if (slot == -1) {
        // Table full, overwrite oldest
        slot = 0;
        uint32_t oldest = dcwstrict_stale_table[0].timestamp_ms;
        for (i = 1; i < DCWSTRICT_MAX_STALE_ENTRIES; i++) {
            if (dcwstrict_stale_table[i].timestamp_ms < oldest) {
                oldest = dcwstrict_stale_table[i].timestamp_ms;
                slot = i;
            }
        }
    }
    
    struct dcwstrict_stale_entry *e = &dcwstrict_stale_table[slot];
    memcpy(e->cw, cw, 16);
    e->caid = caid;
    e->provid = provid;
    e->sid = sid;
    e->timestamp_ms = now_ms;
    e->hit_count = 1;
    
    if (slot >= dcwstrict_stale_count) {
        dcwstrict_stale_count = slot + 1;
    }
}

// ============================================================================
// Main Validation Function
// ============================================================================

/*
 * DCW STRICT - Main validation
 * 
 * Returns: 0 = ACCEPT, 1 = REJECT
 * 
 * Parameters:
 *   cw[16]          - The CW to validate
 *   caid, provid, sid - Channel identification
 *   now_ms          - Current timestamp (ms)
 *   reject_reason   - Output: reason string
 */
static int dcwstrict_check(const uint8_t cw[16], uint16_t caid, uint32_t provid,
                            uint16_t sid, uint32_t now_ms, const char **reject_reason)
{
    if (!dcwstrict_cfg.enabled) {
        return 0;  // Disabled
    }
    
    pthread_mutex_lock(&dcwstrict_lock);
    
    dcwstrict_stats.total_checked++;
    
    int rejected = 0;
    const char *reason = NULL;
    
    // Stage 1: Entropy check
    int entropy = dcwstrict_entropy(cw);
    if (entropy < 5) {
        dcwstrict_stats.rejected_entropy++;
        rejected = 1;
        reason = "entropy < 5";
        goto done;
    }
    
    // Stage 2: All-same-byte check
    if (dcwstrict_allsamebyte(cw)) {
        dcwstrict_stats.rejected_allsamebyte++;
        rejected = 1;
        reason = "all-same-byte";
        goto done;
    }
    
    // Stage 3: Stale/recycled CW check
    if (dcwstrict_check_stale(cw, caid, provid, sid, now_ms)) {
        dcwstrict_stats.rejected_stale++;
        rejected = 1;
        reason = "stale/recycled";
        goto done;
    }
    
done:
    if (!rejected) {
        // Add to stale tracking
        dcwstrict_add_stale(cw, caid, provid, sid, now_ms);
    } else {
        dcwstrict_stats.total_rejected++;
    }
    
    if (reject_reason) {
        *reject_reason = reason;
    }
    
    pthread_mutex_unlock(&dcwstrict_lock);
    
    return rejected;
}

// ============================================================================
// Trust Score Integration
// ============================================================================

/*
 * Apply trust score penalty for fake CW
 * Call this when a CW is rejected by DCW STRICT or SuperSmartFilter
 */
static void dcwstrict_trust_penalty(uint32_t peer_id)
{
    if (!dcwstrict_cfg.trustscore) return;
    
    pthread_mutex_lock(&dcwstrict_lock);
    dcwstrict_stats.trust_penalties++;
    pthread_mutex_unlock(&dcwstrict_lock);
    
    // Integration with trust system would go here
    // trust_adjust_score(peer_id, -20);
}

/*
 * Apply trust score reward for valid CW
 * Call this when a CW is accepted
 */
static void dcwstrict_trust_reward(uint32_t peer_id)
{
    if (!dcwstrict_cfg.trustscore) return;
    
    pthread_mutex_lock(&dcwstrict_lock);
    dcwstrict_stats.trust_rewards++;
    pthread_mutex_unlock(&dcwstrict_lock);
    
    // Integration with trust system would go here
    // trust_adjust_score(peer_id, +2);
}

// ============================================================================
// Initialization
// ============================================================================

static void dcwstrict_init(void)
{
    memset(&dcwstrict_cfg, 0, sizeof(dcwstrict_cfg));
    memset(dcwstrict_stale_table, 0, sizeof(dcwstrict_stale_table));
    memset(&dcwstrict_stats, 0, sizeof(dcwstrict_stats));
    
    // Defaults
    dcwstrict_cfg.enabled = 0;  // Off by default
    dcwstrict_cfg.log_rejected = 1;
    dcwstrict_cfg.trustscore = 1;
}

// ============================================================================
// Statistics
// ============================================================================

static void dcwstrict_get_stats(struct dcwstrict_stats *out)
{
    pthread_mutex_lock(&dcwstrict_lock);
    memcpy(out, &dcwstrict_stats, sizeof(*out));
    pthread_mutex_unlock(&dcwstrict_lock);
}

#endif // DCWSTRICT_H
