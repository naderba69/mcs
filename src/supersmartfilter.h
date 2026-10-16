/*
 * SuperSmartFilter v6.0 - Advanced CW Validation System
 * 
 * 9-stage validation pipeline to reject fake/weak/duplicate CWs:
 * 1. Empty CW check (all zeros)
 * 2. Fake CW check (all-same-byte, known patterns)
 * 3. Weak CW check (entropy < 5 unique bytes)
 * 4. Pattern detection (repeated bytes, 00/FF patterns)
 * 5. CW format check (48-bit vs 64-bit)
 * 6. Blacklist check (forbidden byte sequences)
 * 7. Speed/duplicate check (too fast or recently seen)
 * 8. Cross-channel check (same CW for different channel)
 * 9. Multi-check (peer agreement required)
 * 
 * Plus: Smart peer banning based on bad CW ratio (not count)
 */

#ifndef SUPERSMARTFILTER_H
#define SUPERSMARTFILTER_H

#include <stdint.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

// ============================================================================
// Configuration
// ============================================================================

#define SSF_MAX_HISTORY 16
#define SSF_MAX_BLACKLIST 32
#define SSF_MAX_PEERS 256
#define SSF_HISTORY_WINDOW_MS 5000

struct ssf_config {
    int filter_level;           // 0-20, 0=disabled
    int filter_time_ms;         // Basic time window (ms)
    
    // Peer banning
    int badcwratio;             // Bad CW ratio threshold (%)
    int badcwratiomin;          // Min CWs to analyze before judging
    int minreplies;             // Min replies before banning
    int unblock_time;           // Auto-unblock time (seconds)
    int dropbadcws;             // 1=drop, 0=log only
    
    // Individual checks
    int blockfakecws;           // Block fake CWs
    int sf_entropy;             // Entropy check
    int sf_pattern;             // Pattern check
    int sf_dupseen;             // Duplicate check
    int sf_history;             // History size (1-16)
    
    // CW format
    int sf_cwfmt_auto;          // Auto-detect format
    int sf_cwfmt_enforce;       // 0=off, 1=log, 2=reject
    uint16_t sf_halfcw_caids[32]; // CAIDs requiring 48-bit CWs
    int sf_halfcw_caids_count;
    
    // Blacklist
    uint8_t sf_stringblacklist[SSF_MAX_BLACKLIST][16];
    int sf_stringblacklist_len[SSF_MAX_BLACKLIST];
    int sf_stringblacklist_count;
    
    // Multi-check
    int sf_multicheck;          // Enable multi-check
    uint16_t sf_mc_caids[32];   // CAIDs for multi-check
    int sf_mc_caids_count;
    int sf_mc_match;            // Required peer count
    int sf_mc_mode;             // 1=log, 2=reject
    int sf_mc_wait_ms;          // Wait time (ms)
};

// ============================================================================
// Data Structures
// ============================================================================

struct ssf_cw_history {
    uint8_t cw[16];
    uint16_t caid;
    uint32_t provid;
    uint16_t sid;
    uint32_t timestamp;
};

struct ssf_peer_stats {
    uint32_t peer_id;
    uint64_t total_cws;
    uint64_t bad_cws;
    uint32_t blocked_until;
    int active;
};

struct ssf_multicheck_entry {
    uint8_t ecm_hash[16];
    uint8_t cw[16];
    uint32_t peer_ids[16];
    int peer_count;
    uint32_t first_seen;
    uint32_t last_seen;
    int resolved;
};

struct ssf_stats {
    uint64_t total_checked;
    uint64_t total_rejected;
    uint64_t rejected_empty;
    uint64_t rejected_fake;
    uint64_t rejected_entropy;
    uint64_t rejected_pattern;
    uint64_t rejected_format;
    uint64_t rejected_blacklist;
    uint64_t rejected_duplicate;
    uint64_t rejected_crosschannel;
    uint64_t rejected_multicheck;
    uint64_t peers_blocked;
    uint64_t peers_unblocked;
};

// ============================================================================
// Global State
// ============================================================================

extern struct ssf_config ssf_cfg;
static struct ssf_cw_history ssf_history[SSF_MAX_HISTORY];
static int ssf_history_index = 0;
static struct ssf_peer_stats ssf_peers[SSF_MAX_PEERS];
static int ssf_peer_count = 0;
static struct ssf_multicheck_entry ssf_mc_table[256];
static int ssf_mc_count = 0;
static struct ssf_stats ssf_stats;
static pthread_mutex_t ssf_lock = PTHREAD_MUTEX_INITIALIZER;

// ============================================================================
// Check Functions
// ============================================================================

/* Check 1: Empty CW (all zeros) */
static inline int ssf_check_empty(const uint8_t cw[16])
{
    int i;
    for (i = 0; i < 16; i++) {
        if (cw[i] != 0) return 0;  // Not empty
    }
    return 1;  // Empty
}

/* Check 2: Fake CW (all-same-byte or known patterns) */
static inline int ssf_check_fake(const uint8_t cw[16])
{
    int i;
    
    // All-same-byte check
    int all_same = 1;
    for (i = 1; i < 16; i++) {
        if (cw[i] != cw[0]) {
            all_same = 0;
            break;
        }
    }
    if (all_same) return 1;  // Fake
    
    // Known pattern: FD FF FF FB
    if (cw[0] == 0xFD && cw[1] == 0xFF && cw[2] == 0xFF && cw[3] == 0xFB) {
        return 1;  // Fake
    }
    
    return 0;  // Not fake
}

/* Check 3: Entropy (unique bytes count) */
static inline int ssf_check_entropy(const uint8_t cw[16], int min_unique)
{
    int byte_count[256] = {0};
    int unique = 0;
    int i;
    
    for (i = 0; i < 16; i++) {
        if (byte_count[cw[i]] == 0) {
            unique++;
        }
        byte_count[cw[i]]++;
    }
    
    return (unique < min_unique);  // 1 = weak, 0 = ok
}

/* Check 4: Pattern detection (repeated bytes, 00/FF patterns) */
static inline int ssf_check_pattern(const uint8_t cw[16])
{
    int i;
    int count_00 = 0, count_ff = 0;
    
    // Count 00 and FF bytes
    for (i = 0; i < 16; i++) {
        if (cw[i] == 0x00) count_00++;
        if (cw[i] == 0xFF) count_ff++;
    }
    
    // Too many 00 or FF
    if (count_00 >= 12 || count_ff >= 12) {
        return 1;  // Bad pattern
    }
    
    // Check for repeated 4-byte patterns
    for (i = 0; i < 12; i++) {
        if (cw[i] == cw[i+1] && cw[i+1] == cw[i+2] && cw[i+2] == cw[i+3]) {
            return 1;  // Repeated pattern
        }
    }
    
    return 0;  // No bad pattern
}

/* Check 5: CW format (48-bit vs 64-bit) */
static inline int ssf_check_format(const uint8_t cw[16], uint16_t caid, int require_48bit)
{
    int is_48bit = 0;
    
    // Check if second half is all zeros (48-bit format)
    int i;
    int second_half_zero = 1;
    for (i = 8; i < 16; i++) {
        if (cw[i] != 0) {
            second_half_zero = 0;
            break;
        }
    }
    is_48bit = second_half_zero;
    
    if (require_48bit && !is_48bit) {
        return 1;  // Format mismatch
    }
    
    return 0;  // Format ok
}

/* Check 6: Blacklist (forbidden byte sequences) */
static inline int ssf_check_blacklist(const uint8_t cw[16])
{
    int i, j, k;
    
    for (i = 0; i < ssf_cfg.sf_stringblacklist_count; i++) {
        int len = ssf_cfg.sf_stringblacklist_len[i];
        if (len == 0 || len > 16) continue;
        
        // Search for this pattern in CW
        for (j = 0; j <= 16 - len; j++) {
            int match = 1;
            for (k = 0; k < len; k++) {
                if (cw[j+k] != ssf_cfg.sf_stringblacklist[i][k]) {
                    match = 0;
                    break;
                }
            }
            if (match) return 1;  // Blacklisted
        }
    }
    
    return 0;  // Not blacklisted
}

/* Check 7: Duplicate/speed check */
static inline int ssf_check_duplicate(const uint8_t cw[16], uint16_t caid, 
                                       uint32_t provid, uint16_t sid, uint32_t now_ms)
{
    int i;
    uint32_t time_window = ssf_cfg.filter_time_ms;
    
    for (i = 0; i < ssf_cfg.sf_history; i++) {
        int idx = (ssf_history_index - 1 - i + SSF_MAX_HISTORY) % SSF_MAX_HISTORY;
        struct ssf_cw_history *h = &ssf_history[idx];
        
        if (!h->timestamp) continue;
        
        // Time check
        if ((now_ms - h->timestamp) > time_window) continue;
        
        // Same CW?
        if (memcmp(h->cw, cw, 16) == 0) {
            // Same channel?
            if (h->caid == caid && h->provid == provid && h->sid == sid) {
                return 1;  // Duplicate (too fast)
            }
        }
    }
    
    return 0;  // Not duplicate
}

/* Check 8: Cross-channel (same CW for different channel) */
static inline int ssf_check_crosschannel(const uint8_t cw[16], uint16_t caid,
                                          uint32_t provid, uint16_t sid)
{
    int i;
    
    for (i = 0; i < SSF_MAX_HISTORY; i++) {
        struct ssf_cw_history *h = &ssf_history[i];
        
        if (!h->timestamp) continue;
        
        // Same CW but different channel?
        if (memcmp(h->cw, cw, 16) == 0) {
            if (h->caid != caid || h->provid != provid || h->sid != sid) {
                return 1;  // Cross-channel detected
            }
        }
    }
    
    return 0;  // No cross-channel
}

/* Check 9: Multi-check (peer agreement) */
static inline int ssf_check_multicheck(const uint8_t ecm_hash[16], const uint8_t cw[16],
                                        uint16_t caid, uint32_t peer_id, uint32_t now_ms)
{
    int i;
    int caid_match = 0;
    
    // Check if this CAID requires multi-check
    if (ssf_cfg.sf_mc_caids_count > 0) {
        for (i = 0; i < ssf_cfg.sf_mc_caids_count; i++) {
            if (ssf_cfg.sf_mc_caids[i] == caid) {
                caid_match = 1;
                break;
            }
        }
        if (!caid_match) return 0;  // Not required for this CAID
    }
    
    // Find or create entry
    struct ssf_multicheck_entry *entry = NULL;
    for (i = 0; i < ssf_mc_count; i++) {
        if (memcmp(ssf_mc_table[i].ecm_hash, ecm_hash, 16) == 0) {
            entry = &ssf_mc_table[i];
            break;
        }
    }
    
    if (!entry) {
        // Create new entry
        if (ssf_mc_count >= 256) {
            // Table full, remove oldest
            ssf_mc_count = 0;
        }
        entry = &ssf_mc_table[ssf_mc_count++];
        memcpy(entry->ecm_hash, ecm_hash, 16);
        memcpy(entry->cw, cw, 16);
        entry->peer_count = 0;
        entry->first_seen = now_ms;
        entry->resolved = 0;
    }
    
    // Add peer if not already present
    int peer_found = 0;
    for (i = 0; i < entry->peer_count; i++) {
        if (entry->peer_ids[i] == peer_id) {
            peer_found = 1;
            break;
        }
    }
    
    if (!peer_found && entry->peer_count < 16) {
        entry->peer_ids[entry->peer_count++] = peer_id;
    }
    
    entry->last_seen = now_ms;
    
    // Check if enough peers agree
    if (entry->peer_count >= ssf_cfg.sf_mc_match) {
        entry->resolved = 1;
        return 0;  // OK - enough peers
    }
    
    // Check timeout
    if ((now_ms - entry->first_seen) > (uint32_t)ssf_cfg.sf_mc_wait_ms) {
        return 1;  // Timeout - not enough peers
    }
    
    return 0;  // Still waiting
}

// ============================================================================
// Peer Management
// ============================================================================

static struct ssf_peer_stats* ssf_get_peer(uint32_t peer_id)
{
    int i;
    
    // Find existing
    for (i = 0; i < ssf_peer_count; i++) {
        if (ssf_peers[i].peer_id == peer_id && ssf_peers[i].active) {
            return &ssf_peers[i];
        }
    }
    
    // Create new
    if (ssf_peer_count < SSF_MAX_PEERS) {
        struct ssf_peer_stats *p = &ssf_peers[ssf_peer_count++];
        memset(p, 0, sizeof(*p));
        p->peer_id = peer_id;
        p->active = 1;
        return p;
    }
    
    return NULL;
}

static int ssf_is_peer_blocked(uint32_t peer_id, uint32_t now)
{
    struct ssf_peer_stats *p = ssf_get_peer(peer_id);
    if (!p) return 0;
    
    if (p->blocked_until && now < p->blocked_until) {
        return 1;  // Blocked
    }
    
    // Auto-unblock
    if (p->blocked_until && now >= p->blocked_until) {
        p->blocked_until = 0;
        ssf_stats.peers_unblocked++;
    }
    
    return 0;
}

static void ssf_record_cw_result(uint32_t peer_id, int is_bad, uint32_t now)
{
    struct ssf_peer_stats *p = ssf_get_peer(peer_id);
    if (!p) return;
    
    p->total_cws++;
    if (is_bad) p->bad_cws++;
    
    // Check ratio
    if (p->total_cws >= (uint64_t)ssf_cfg.badcwratiomin &&
        p->total_cws >= (uint64_t)ssf_cfg.minreplies) {
        
        int ratio = (int)((p->bad_cws * 100) / p->total_cws);
        if (ratio >= ssf_cfg.badcwratio && ssf_cfg.badcwratio > 0) {
            p->blocked_until = now + ssf_cfg.unblock_time;
            ssf_stats.peers_blocked++;
        }
    }
}

// ============================================================================
// Main Validation Function
// ============================================================================

/*
 * SuperSmartFilter - Main validation pipeline
 * 
 * Returns: 0 = ACCEPT, 1 = REJECT
 * 
 * Parameters:
 *   cw[16]          - The CW to validate
 *   ecm_hash[16]    - ECM hash (for multi-check)
 *   caid, provid, sid - Channel identification
 *   peer_id         - Source peer ID
 *   now_ms          - Current timestamp (ms)
 *   reject_reason   - Output: reason code (1-9)
 */
static int supersmartfilter_check(const uint8_t cw[16], const uint8_t ecm_hash[16],
                                   uint16_t caid, uint32_t provid, uint16_t sid,
                                   uint32_t peer_id, uint32_t now_ms, int *reject_reason)
{
    if (ssf_cfg.filter_level == 0) {
        return 0;  // Disabled
    }
    
    pthread_mutex_lock(&ssf_lock);
    
    ssf_stats.total_checked++;
    
    // Check if peer is blocked
    uint32_t now_sec = now_ms / 1000;
    if (ssf_is_peer_blocked(peer_id, now_sec)) {
        pthread_mutex_unlock(&ssf_lock);
        if (reject_reason) *reject_reason = 99;  // Peer blocked
        ssf_stats.total_rejected++;
        return 1;
    }
    
    int rejected = 0;
    int reason = 0;
    
    // Check 1: Empty
    if (ssf_check_empty(cw)) {
        ssf_stats.rejected_empty++;
        rejected = 1;
        reason = 1;
        goto done;
    }
    
    // Check 2: Fake
    if (ssf_cfg.blockfakecws && ssf_check_fake(cw)) {
        ssf_stats.rejected_fake++;
        rejected = 1;
        reason = 2;
        goto done;
    }
    
    // Check 3: Entropy
    if (ssf_cfg.sf_entropy && ssf_check_entropy(cw, 5)) {
        ssf_stats.rejected_entropy++;
        rejected = 1;
        reason = 3;
        goto done;
    }
    
    // Check 4: Pattern
    if (ssf_cfg.sf_pattern && ssf_check_pattern(cw)) {
        ssf_stats.rejected_pattern++;
        rejected = 1;
        reason = 4;
        goto done;
    }
    
    // Check 5: Format (48-bit vs 64-bit)
    if (ssf_cfg.sf_cwfmt_enforce > 0) {
        int require_48bit = 0;
        int i;
        for (i = 0; i < ssf_cfg.sf_halfcw_caids_count; i++) {
            if (ssf_cfg.sf_halfcw_caids[i] == caid) {
                require_48bit = 1;
                break;
            }
        }
        if (require_48bit && ssf_check_format(cw, caid, 1)) {
            if (ssf_cfg.sf_cwfmt_enforce == 2) {
                ssf_stats.rejected_format++;
                rejected = 1;
                reason = 5;
                goto done;
            }
        }
    }
    
    // Check 6: Blacklist
    if (ssf_check_blacklist(cw)) {
        ssf_stats.rejected_blacklist++;
        rejected = 1;
        reason = 6;
        goto done;
    }
    
    // Check 7: Duplicate/speed
    if (ssf_cfg.filter_level >= 3 && ssf_check_duplicate(cw, caid, provid, sid, now_ms)) {
        ssf_stats.rejected_duplicate++;
        rejected = 1;
        reason = 7;
        goto done;
    }
    
    // Check 8: Cross-channel
    if (ssf_cfg.sf_dupseen && ssf_check_crosschannel(cw, caid, provid, sid)) {
        ssf_stats.rejected_crosschannel++;
        rejected = 1;
        reason = 8;
        goto done;
    }
    
    // Check 9: Multi-check
    if (ssf_cfg.sf_multicheck && ecm_hash) {
        if (ssf_check_multicheck(ecm_hash, cw, caid, peer_id, now_ms)) {
            if (ssf_cfg.sf_mc_mode == 2) {
                ssf_stats.rejected_multicheck++;
                rejected = 1;
                reason = 9;
                goto done;
            }
        }
    }
    
done:
    // Record result
    if (peer_id > 0) {
        ssf_record_cw_result(peer_id, rejected, now_sec);
    }
    
    // Update history
    if (!rejected) {
        struct ssf_cw_history *h = &ssf_history[ssf_history_index];
        memcpy(h->cw, cw, 16);
        h->caid = caid;
        h->provid = provid;
        h->sid = sid;
        h->timestamp = now_ms;
        ssf_history_index = (ssf_history_index + 1) % SSF_MAX_HISTORY;
    }
    
    if (rejected) {
        ssf_stats.total_rejected++;
    }
    
    if (reject_reason) *reject_reason = reason;
    
    pthread_mutex_unlock(&ssf_lock);
    
    return rejected;
}

// ============================================================================
// Initialization
// ============================================================================

static void supersmartfilter_init(void)
{
    memset(&ssf_cfg, 0, sizeof(ssf_cfg));
    memset(ssf_history, 0, sizeof(ssf_history));
    memset(ssf_peers, 0, sizeof(ssf_peers));
    memset(ssf_mc_table, 0, sizeof(ssf_mc_table));
    memset(&ssf_stats, 0, sizeof(ssf_stats));
    
    // Defaults
    ssf_cfg.filter_level = 2;
    ssf_cfg.filter_time_ms = 500;
    ssf_cfg.badcwratio = 85;
    ssf_cfg.badcwratiomin = 300;
    ssf_cfg.minreplies = 10;
    ssf_cfg.unblock_time = 600;
    ssf_cfg.dropbadcws = 1;
    ssf_cfg.blockfakecws = 1;
    ssf_cfg.sf_entropy = 1;
    ssf_cfg.sf_pattern = 1;
    ssf_cfg.sf_dupseen = 1;
    ssf_cfg.sf_history = 8;
    ssf_cfg.sf_cwfmt_auto = 1;
    ssf_cfg.sf_cwfmt_enforce = 1;
    ssf_cfg.sf_multicheck = 0;
    ssf_cfg.sf_mc_match = 2;
    ssf_cfg.sf_mc_mode = 2;
    ssf_cfg.sf_mc_wait_ms = 500;
}

// ============================================================================
// Statistics
// ============================================================================

static void supersmartfilter_get_stats(struct ssf_stats *out)
{
    pthread_mutex_lock(&ssf_lock);
    memcpy(out, &ssf_stats, sizeof(*out));
    pthread_mutex_unlock(&ssf_lock);
}

#endif // SUPERSMARTFILTER_H
