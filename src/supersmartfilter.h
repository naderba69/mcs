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
#include "tick_math.h"

// ============================================================================
// Configuration
// ============================================================================

#define SSF_MAX_HISTORY 16
#define SSF_MAX_BLACKLIST 32
#define SSF_MAX_PEERS 256
#define SSF_MAX_MC_ENTRIES 256
#define SSF_HISTORY_WINDOW_MS 5000

/* R48: forward declarations for level-based helpers */
static inline uint32_t ssf_effective_window(int level, uint32_t base_ms);
static inline int ssf_backdepth(int level);

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
    int sf_marchcheck;          // Band 55 MARCHCHECK: 0=off, 1=log-only, 2=reject
    int sf_cwdup;               // Band 56 CW_DUP: 0=off, 1=log-only, 2=reject
    int sf_cwdup_rep;           // Band 56: cross-cycle repeats before flag (2..10)
    int sf_cwreuse;             // Band 58 half-CW cross-channel reuse: 0=off, 1=log, 2=reject
    int sf_cwreuse_window_ms;   // Band 58: sighting window (1000..600000)
    uint16_t sf_reuse_caids[32];// Band 58: scope; empty = all CAIDs
    int sf_reuse_caids_count;
    int sf_latecw;              // Band 59 late CW: 0=off, 1=log-only, 2=reject
    int sf_latecw_ms;           // Band 59: max ECM age at CW arrival (ms)
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
    uint8_t used;
};

struct ssf_peer_stats {
    uint32_t peer_id;
    uint64_t total_cws;
    uint64_t bad_cws;
    uint32_t blocked_until;      // 32-bit monotonic millisecond deadline
    uint8_t blocked;             // Explicit: a wrapped deadline may validly be zero
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
    uint8_t used;
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
    uint64_t marchcheck_flagged;
    uint64_t rejected_marchcheck;
    uint64_t cwdup_flagged;
    uint64_t rejected_cwdup;
    uint64_t cwreuse_flagged;
    uint64_t rejected_cwreuse;
    uint64_t latecw_flagged;
    uint64_t rejected_latecw;
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
static struct ssf_multicheck_entry ssf_mc_table[SSF_MAX_MC_ENTRIES];
static int ssf_mc_count = 0;
/* Band 55 MARCHCHECK: last full CW accepted per (caid, provid, sid).
 * A legitimate marching channel moves the shared half to the OTHER slot
 * every cycle: (A,B) -> (B,C). A recycled fake parks one half in the SAME
 * slot while the other half changes. Accessed only under ssf_lock. */
#define SSF_MAX_MARCH_ENTRIES 512
struct ssf_march_entry {
    uint16_t caid;
    uint32_t provid;
    uint16_t sid;
    uint8_t prev[16];
    uint8_t used;
};
static struct ssf_march_entry ssf_march_table[SSF_MAX_MARCH_ENTRIES];
static int ssf_march_index = 0;
/* Band 56 CW_DUP: per-channel repeat tracker. `rep` counts how many
 * NEW ECM cycles (different ecm_hash) re-delivered the same CW;
 * `other_ms` is when a different CW was last accepted for the channel.
 * A constant key on a channel with no competitor never flags, and a
 * normal two-key roll resets the run on every switch. Under ssf_lock. */
#define SSF_MAX_DUP_ENTRIES 512
#define SSF_CWDUP_COMPETITOR_WINDOW_MS 120000
struct ssf_dup_entry {
    uint16_t caid;
    uint32_t provid;
    uint16_t sid;
    uint8_t rep_cw[16];
    uint8_t last_hash[16];
    int rep;
    uint32_t other_ms;
    uint8_t used;
};
static struct ssf_dup_entry ssf_dup_table[SSF_MAX_DUP_ENTRIES];
static int ssf_dup_index = 0;
/* Band 58 CW_REUSE: ring of recent full-CW sightings. A half that
 * reappears on a DIFFERENT (caid, provid, sid) inside the window while
 * the full CW differs is the recycled-half-pool signature. A full-CW
 * match on another channel is the simulcrypt-twin case (band 38 /
 * TASK 1.3 territory) and is deliberately NOT flagged here. Zero
 * halves are never tracked. Under ssf_lock. */
#define SSF_MAX_REUSE_ENTRIES 512
struct ssf_reuse_entry {
    uint16_t caid;
    uint32_t provid;
    uint16_t sid;
    uint8_t cw[16];
    uint32_t last_ms;
    uint8_t used;
};
static struct ssf_reuse_entry ssf_reuse_table[SSF_MAX_REUSE_ENTRIES];
static int ssf_reuse_index = 0;
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

/* Check 7: Duplicate/speed check. level is the effective (profile or
 * global) filter level so per-profile FILTER settings apply here too. */
static inline int ssf_check_duplicate(const uint8_t cw[16], uint16_t caid,
                                       uint32_t provid, uint16_t sid, uint32_t now_ms,
                                       int level)
{
    int i;
    uint32_t time_window = ssf_effective_window(level, ssf_cfg.filter_time_ms);
    int bd = ssf_backdepth(level);
    
    for (i = 0; i < bd && i < ssf_cfg.sf_history; i++) {
        int idx = (ssf_history_index - 1 - i + SSF_MAX_HISTORY) % SSF_MAX_HISTORY;
        struct ssf_cw_history *h = &ssf_history[idx];
        
        if (!h->used) continue;
        
        // Time check uses modulo millisecond ticks; zero is a valid stamp.
        if (tick32_elapsed(now_ms, h->timestamp) > time_window) continue;
        
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
        
        if (!h->used) continue;
        
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
    
    // Find an existing request or a free slot.
    struct ssf_multicheck_entry *entry = NULL;
    int free_slot = -1;
    for (i = 0; i < SSF_MAX_MC_ENTRIES; i++) {
        if (!ssf_mc_table[i].used) {
            if (free_slot < 0) free_slot = i;
            continue;
        }
        if (memcmp(ssf_mc_table[i].ecm_hash, ecm_hash, 16) == 0) {
            entry = &ssf_mc_table[i];
            break;
        }
    }

    if (!entry) {
        if (free_slot < 0) {
            // Full table: retain all other evidence and replace only the oldest.
            uint32_t oldest_age = 0;
            int oldest = -1;
            for (i = 0; i < SSF_MAX_MC_ENTRIES; i++) {
                uint32_t age;
                if (!ssf_mc_table[i].used) continue;
                age = tick32_elapsed(now_ms, ssf_mc_table[i].last_seen);
                if (oldest < 0 || age > oldest_age) {
                    oldest = i;
                    oldest_age = age;
                }
            }
            if (oldest < 0) return 0;
            free_slot = oldest;
        } else {
            ssf_mc_count++;
        }
        entry = &ssf_mc_table[free_slot];
        memset(entry, 0, sizeof(*entry));
        memcpy(entry->ecm_hash, ecm_hash, 16);
        memcpy(entry->cw, cw, 16);
        entry->first_seen = now_ms;
        entry->last_seen = now_ms;
        entry->used = 1;
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
    if (tick32_elapsed(now_ms, entry->first_seen) >
        (uint32_t)ssf_cfg.sf_mc_wait_ms) {
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
    
    if (p->blocked && !tick32_deadline_reached(now, p->blocked_until)) {
        return 1;  // Blocked
    }
    
    // Auto-unblock
    if (p->blocked && tick32_deadline_reached(now, p->blocked_until)) {
        p->blocked = 0;
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
            uint32_t unblock_ms = 0;
            if (ssf_cfg.unblock_time > 0) {
                uint64_t delay = (uint64_t)(unsigned int)ssf_cfg.unblock_time * 1000u;
                unblock_ms = delay > 0x7FFFFFFFu ? 0x7FFFFFFFu : (uint32_t)delay;
            }
            if (unblock_ms) {
                p->blocked_until = now + unblock_ms;
                p->blocked = 1;
                ssf_stats.peers_blocked++;
            } else {
                p->blocked_until = 0;
                p->blocked = 0;
            }
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
/* R48: Level-based effective fast-window and backdepth (v6.0 spec).
 * Level 2: eft=100%, bd=1
 * Level 3: eft=75%, bd=2
 * Level 4: eft=60%, bd=2
 * Level 5: eft=50%, bd=3
 * Level 6: eft=100%, bd=3 (HALFNULLED gate active)
 * Level 8: eft=40%, bd=3
 * Level 9+: eft=30%, bd=3 */
static inline uint32_t ssf_effective_window(int level, uint32_t base_ms)
{
    if (level <= 2) return base_ms;
    if (level == 3) return (base_ms * 75) / 100;
    if (level == 4) return (base_ms * 60) / 100;
    if (level == 5) return (base_ms * 50) / 100;
    if (level == 6) return base_ms;
    if (level == 7) return (base_ms * 45) / 100;
    if (level == 8) return (base_ms * 40) / 100;
    return (base_ms * 30) / 100;
}

static inline int ssf_backdepth(int level)
{
    if (level <= 2) return 1;
    if (level <= 4) return 2;
    return 3;
}

/* Band 55: returns 1 when `cw` parks a half in the same slot as the
 * previous full CW for this channel while the other half changed.
 * Half-nulled CWs (either half all zero) are exempt: their zero half is
 * parked by design. An identical CW is a duplicate, not a march fault,
 * and is left to the duplicate checks. Updates the remembered CW unless
 * `update` is 0 (mode-2 reject: a rejected CW was never accepted, so it
 * must not become the reference for the next comparison). */
static int ssf_check_marchcheck(const uint8_t cw[16], uint16_t caid,
                                uint32_t provid, uint16_t sid, int update)
{
    static const uint8_t zero8[8] = {0};
    if (!memcmp(cw, zero8, 8) || !memcmp(cw + 8, zero8, 8)) return 0;
    struct ssf_march_entry *entry = NULL;
    int i;
    for (i = 0; i < SSF_MAX_MARCH_ENTRIES; i++) {
        if (ssf_march_table[i].used && ssf_march_table[i].caid == caid &&
            ssf_march_table[i].provid == provid && ssf_march_table[i].sid == sid) {
            entry = &ssf_march_table[i];
            break;
        }
    }
    if (!entry) {
        entry = &ssf_march_table[ssf_march_index];
        ssf_march_index = (ssf_march_index + 1) % SSF_MAX_MARCH_ENTRIES;
        entry->caid = caid; entry->provid = provid; entry->sid = sid;
        memcpy(entry->prev, cw, 16);
        entry->used = 1;
        return 0;
    }
    int parked = 0;
    if (memcmp(cw, entry->prev, 16) &&
        (!memcmp(cw, entry->prev, 8) || !memcmp(cw + 8, entry->prev + 8, 8)))
        parked = 1;
    /* A clean CW always becomes the new reference; a parked CW only
     * replaces it when the caller accepts it anyway (log-only mode). */
    if (!parked || update) memcpy(entry->prev, cw, 16);
    return parked;
}

/* Band 56: returns 1 when the same CW has now been delivered for at
 * least `rep_needed` new ECM cycles while a competing different CW was
 * accepted for this channel within the competitor window. Without an
 * ecm_hash the cycle boundary cannot be proven, so nothing is counted
 * (fail-open). Observation state is updated on every sighting --
 * including a mode-2 rejection -- so a stuck fake keeps being counted
 * (and rejected) instead of silently becoming the new reference. */
static int ssf_check_cwdup(const uint8_t cw[16], const uint8_t ecm_hash[16],
                           uint16_t caid, uint32_t provid, uint16_t sid,
                           uint32_t now_ms, int rep_needed)
{
    if (!ecm_hash) return 0;
    struct ssf_dup_entry *entry = NULL;
    int i;
    for (i = 0; i < SSF_MAX_DUP_ENTRIES; i++) {
        if (ssf_dup_table[i].used && ssf_dup_table[i].caid == caid &&
            ssf_dup_table[i].provid == provid && ssf_dup_table[i].sid == sid) {
            entry = &ssf_dup_table[i];
            break;
        }
    }
    if (!entry) {
        entry = &ssf_dup_table[ssf_dup_index];
        ssf_dup_index = (ssf_dup_index + 1) % SSF_MAX_DUP_ENTRIES;
        entry->caid = caid; entry->provid = provid; entry->sid = sid;
        memcpy(entry->rep_cw, cw, 16);
        memcpy(entry->last_hash, ecm_hash, 16);
        entry->rep = 0;
        entry->other_ms = 0;
        entry->used = 1;
        return 0;
    }
    if (!memcmp(cw, entry->rep_cw, 16)) {
        if (memcmp(ecm_hash, entry->last_hash, 16)) {
            entry->rep++;               /* same key, new ECM cycle */
            memcpy(entry->last_hash, ecm_hash, 16);
        }                               /* same cycle resend: no count */
    } else {
        entry->other_ms = now_ms;       /* a competitor exists */
        memcpy(entry->rep_cw, cw, 16);
        memcpy(entry->last_hash, ecm_hash, 16);
        entry->rep = 0;
    }
    return entry->rep >= rep_needed && entry->other_ms &&
           (uint32_t)(now_ms - entry->other_ms) <= SSF_CWDUP_COMPETITOR_WINDOW_MS;
}

/* Band 58: returns 1 when either non-zero half of `cw` was seen inside
 * the window on a different channel as part of a DIFFERENT full CW.
 * The sighting is always recorded (same-channel re-sightings refresh
 * their entry); a stale cross-channel sighting simply ages out of the
 * window and the new channel's entry takes over the ring slot. */
static int ssf_check_cwreuse(const uint8_t cw[16], uint16_t caid,
                             uint32_t provid, uint16_t sid, uint32_t now_ms,
                             int window_ms)
{
    static const uint8_t zero8[8] = {0};
    int i, flagged = 0;
    if (ssf_cfg.sf_reuse_caids_count > 0) {
        int in_scope = 0;
        for (i = 0; i < ssf_cfg.sf_reuse_caids_count; i++)
            if (ssf_cfg.sf_reuse_caids[i] == caid) { in_scope = 1; break; }
        if (!in_scope) return 0;
    }
    for (i = 0; i < SSF_MAX_REUSE_ENTRIES; i++) {
        struct ssf_reuse_entry *e = &ssf_reuse_table[i];
        if (!e->used) continue;
        if ((uint32_t)(now_ms - e->last_ms) > (uint32_t)window_ms) continue;
        if (e->caid == caid && e->provid == provid && e->sid == sid) continue;
        if (!memcmp(e->cw, cw, 16)) continue;  /* full-CW twin: not this detector */
        if ((!memcmp(cw, zero8, 8) && !memcmp(cw + 8, zero8, 8))) continue;
        if ((!memcmp(cw, e->cw, 8) && memcmp(cw, zero8, 8)) ||
            (!memcmp(cw, e->cw + 8, 8) && memcmp(cw, zero8, 8)) ||
            (!memcmp(cw + 8, e->cw, 8) && memcmp(cw + 8, zero8, 8)) ||
            (!memcmp(cw + 8, e->cw + 8, 8) && memcmp(cw + 8, zero8, 8)))
            flagged = 1;
    }
    /* record / refresh this channel's sighting */
    for (i = 0; i < SSF_MAX_REUSE_ENTRIES; i++) {
        struct ssf_reuse_entry *e = &ssf_reuse_table[i];
        if (e->used && e->caid == caid && e->provid == provid && e->sid == sid &&
            !memcmp(e->cw, cw, 16)) {
            e->last_ms = now_ms;
            return flagged;
        }
    }
    {
        struct ssf_reuse_entry *e = &ssf_reuse_table[ssf_reuse_index];
        ssf_reuse_index = (ssf_reuse_index + 1) % SSF_MAX_REUSE_ENTRIES;
        e->caid = caid; e->provid = provid; e->sid = sid;
        memcpy(e->cw, cw, 16);
        e->last_ms = now_ms;
        e->used = 1;
    }
    return flagged;
}

static int supersmartfilter_check(const uint8_t cw[16], const uint8_t ecm_hash[16],
                                   uint16_t caid, uint32_t provid, uint16_t sid,
                                   uint32_t peer_id, uint32_t now_ms, int *reject_reason,
                                   int profile_level, int profile_marchcheck,
                                   int profile_cwdup, int profile_cwreuse,
                                   int profile_latecw, uint32_t ecm_age_ms,
                                   int profile_latecw_ms,
                                   int profile_cwreuse_window_ms,
                                   int profile_cwdup_rep)
{
    /* R48: profile_level 0 = inherit global. Passed as a parameter
     * (not a global) so concurrent ECM workers on different profiles
     * cannot race each other's override. */
    int effective_level = profile_level ? profile_level : ssf_cfg.filter_level;
    if (effective_level == 0) {
        return 0;  // Disabled
    }
    
    pthread_mutex_lock(&ssf_lock);
    
    ssf_stats.total_checked++;
    
    // Check if peer is blocked (blocked_until is stored in the same ms tick domain).
    if (ssf_is_peer_blocked(peer_id, now_ms)) {
        ssf_stats.total_rejected++;
        pthread_mutex_unlock(&ssf_lock);
        if (reject_reason) *reject_reason = 99;  // Peer blocked
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
    
    // Check 7: Duplicate/speed (level >= 2 enables history)
    if (effective_level >= 2 && ssf_check_duplicate(cw, caid, provid, sid, now_ms, effective_level)) {
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
    
    // Check 10 (band 55): MARCHCHECK -- same-slot parked half.
    // Effective mode: profile override (1=off, 2=log-only, 3=reject)
    // wins over the global CACHE SF MARCHCHECK (0=off, 1=log, 2=reject).
    {
        int mc_mode = ssf_cfg.sf_marchcheck;
        if (profile_marchcheck == 1) mc_mode = 0;
        else if (profile_marchcheck == 2) mc_mode = 1;
        else if (profile_marchcheck == 3) mc_mode = 2;
        if (mc_mode > 0) {
            int parked = ssf_check_marchcheck(cw, caid, provid, sid, mc_mode == 1);
            if (parked) {
                ssf_stats.marchcheck_flagged++;
                if (mc_mode == 2) {
                    ssf_stats.rejected_marchcheck++;
                    rejected = 1;
                    reason = 10;
                    goto done;
                }
            }
        }
    }

    // Check 11 (band 56): CW_DUP -- constant CW across ECM cycles
    // while a competitor exists. Profile override (1=off, 2=log-only,
    // 3=reject) wins over the global CACHE SF CWDUP.
    {
        int dup_mode = ssf_cfg.sf_cwdup;
        if (profile_cwdup == 1) dup_mode = 0;
        else if (profile_cwdup == 2) dup_mode = 1;
        else if (profile_cwdup == 3) dup_mode = 2;
        if (dup_mode > 0 &&
            ssf_check_cwdup(cw, ecm_hash, caid, provid, sid, now_ms,
                            profile_cwdup_rep > 0 ? profile_cwdup_rep
                                                  : ssf_cfg.sf_cwdup_rep)) {
            ssf_stats.cwdup_flagged++;
            if (dup_mode == 2) {
                ssf_stats.rejected_cwdup++;
                rejected = 1;
                reason = 11;
                goto done;
            }
        }
    }

    // Check 12 (band 58): CW_REUSE -- a half recycled on another
    // channel. Profile override (1=off, 2=log-only, 3=reject) wins
    // over the global CACHE SF CWREUSE.
    {
        int reuse_mode = ssf_cfg.sf_cwreuse;
        if (profile_cwreuse == 1) reuse_mode = 0;
        else if (profile_cwreuse == 2) reuse_mode = 1;
        else if (profile_cwreuse == 3) reuse_mode = 2;
        if (reuse_mode > 0 &&
            ssf_check_cwreuse(cw, caid, provid, sid, now_ms,
                              profile_cwreuse_window_ms > 0
                                  ? profile_cwreuse_window_ms
                                  : ssf_cfg.sf_cwreuse_window_ms)) {
            ssf_stats.cwreuse_flagged++;
            if (reuse_mode == 2) {
                ssf_stats.rejected_cwreuse++;
                rejected = 1;
                reason = 12;
                goto done;
            }
        }
    }

    // Check 13 (band 59): LATECW -- the CW arrived after the ECM had
    // already waited longer than the threshold. ecm_age_ms is the
    // caller's tick32_elapsed(now, ecm->recvtime); UINT32_MAX means
    // "unknown" and never flags (fail-open). Profile override
    // (1=off, 2=log-only, 3=reject) wins over CACHE SF LATECW.
    // WARNING: slow-source profiles (card answers in tens of seconds)
    // must never arm this -- arm per CAID on trace evidence only.
    {
        int late_mode = ssf_cfg.sf_latecw;
        if (profile_latecw == 1) late_mode = 0;
        else if (profile_latecw == 2) late_mode = 1;
        else if (profile_latecw == 3) late_mode = 2;
        {
            int late_ms = profile_latecw_ms > 0 ? profile_latecw_ms
                                                : ssf_cfg.sf_latecw_ms;
            if (late_mode > 0 && late_ms > 0 &&
                ecm_age_ms != 0xFFFFFFFFu &&
                ecm_age_ms > (uint32_t)late_ms) {
            ssf_stats.latecw_flagged++;
            if (late_mode == 2) {
                ssf_stats.rejected_latecw++;
                rejected = 1;
                reason = 13;
                goto done;
            }
            }
        }
    }

done:
    // Record result
    if (peer_id > 0) {
        ssf_record_cw_result(peer_id, rejected, now_ms);
    }
    
    // Update history
    if (!rejected) {
        struct ssf_cw_history *h = &ssf_history[ssf_history_index];
        memcpy(h->cw, cw, 16);
        h->caid = caid;
        h->provid = provid;
        h->sid = sid;
        h->timestamp = now_ms;
        h->used = 1;
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
    memset(ssf_march_table, 0, sizeof(ssf_march_table));
    ssf_march_index = 0;
    memset(ssf_dup_table, 0, sizeof(ssf_dup_table));
    ssf_dup_index = 0;
    memset(ssf_reuse_table, 0, sizeof(ssf_reuse_table));
    ssf_reuse_index = 0;
    memset(&ssf_stats, 0, sizeof(ssf_stats));
    ssf_history_index = 0;
    ssf_peer_count = 0;
    ssf_mc_count = 0;
    
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
    ssf_cfg.sf_marchcheck = 0;
    ssf_cfg.sf_cwdup = 0;
    ssf_cfg.sf_cwdup_rep = 4;
    ssf_cfg.sf_cwreuse = 0;
    ssf_cfg.sf_cwreuse_window_ms = 120000;
    ssf_cfg.sf_latecw = 0;
    ssf_cfg.sf_latecw_ms = 8000;
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

// ============================================================================
// Snapshots (web console readouts)
// ============================================================================

static void ssf_snapshot_config(struct ssf_config *out)
{
    pthread_mutex_lock(&ssf_lock);
    memcpy(out, &ssf_cfg, sizeof(*out));
    pthread_mutex_unlock(&ssf_lock);
}

/* Copy active peers into out (at most max); returns count. */
static int ssf_snapshot_peers(struct ssf_peer_stats *out, int max)
{
    int i, n = 0;
    if (max <= 0) return 0;
    pthread_mutex_lock(&ssf_lock);
    for (i = 0; i < ssf_peer_count && n < max; i++) {
        if (ssf_peers[i].active) out[n++] = ssf_peers[i];
    }
    pthread_mutex_unlock(&ssf_lock);
    return n;
}

/* Copy CW history newest-first into out (at most max); returns count. */
static int ssf_snapshot_history(struct ssf_cw_history *out, int max)
{
    int i, n = 0;
    if (max <= 0) return 0;
    pthread_mutex_lock(&ssf_lock);
    for (i = 0; i < SSF_MAX_HISTORY && n < max; i++) {
        int idx = (ssf_history_index - 1 - i + SSF_MAX_HISTORY) % SSF_MAX_HISTORY;
        if (ssf_history[idx].used) out[n++] = ssf_history[idx];
    }
    pthread_mutex_unlock(&ssf_lock);
    return n;
}

#endif // SUPERSMARTFILTER_H
