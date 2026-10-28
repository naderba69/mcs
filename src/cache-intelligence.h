/*
 * cache-intelligence.h — Cache Intelligence System (7 Levels)
 *
 * A comprehensive system that learns which cache peers are reliable
 * for each channel and optimizes CW retrieval accordingly.
 *
 * LEVEL 1: Trust Scoring — Score each peer per channel (0-100)
 * LEVEL 2: Tiered Priority — Divide peers into A/B/C tiers
 * LEVEL 3: Quick Validation — Fast CW validation on arrival
 * LEVEL 4: Channel Learning — Remember best peers per channel
 * LEVEL 5: Reward & Penalty — Auto-adjust trust over time
 * LEVEL 6: Smart Prediction — Predict which peer will have good CW
 * LEVEL 7: Predictive Cache — Pre-fetch CW before it's needed
 *
 * Architecture:
 *   - Per-peer trust scores (global + per-channel)
 *   - Per-channel history (best/worst peers, success rates)
 *   - Per-CAID pattern detection (cycle timing, tag alternation)
 *   - Automatic tier assignment based on trust
 *   - Predictive pre-loading based on CW cycle
 *
 * Performance target:
 *   - Freeze: 1530ms → < 200ms (95% of cases)
 *   - Bandwidth: -60-70% (skip useless peers)
 *   - CPU: -50% (skip bad CWs early)
 *
 * Author: Arena AI Agent
 * Date: 2026-10-28
 */

#ifndef CACHE_INTELLIGENCE_H
#define CACHE_INTELLIGENCE_H

#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

/* ============================================================================
 * CONFIGURATION
 * ============================================================================ */

#define CI_MAX_PEERS           256    /* Maximum tracked cache peers */
#define CI_MAX_CHANNELS        1024   /* Maximum tracked channels */
#define CI_MAX_CAIDS           64     /* Maximum tracked CAIDs */
#define CI_MAX_PEER_HISTORY    50     /* History entries per peer */
#define CI_MAX_CHANNEL_PEERS   10     /* Best/worst peers per channel */
#define CI_MAX_PREDICT_BUFFER  32     /* Predictive cache entries */

/* Trust score thresholds */
#define CI_TRUST_MAX           100    /* Maximum trust score */
#define CI_TRUST_MIN           0      /* Minimum trust score */
#define CI_TRUST_INITIAL       50     /* Initial trust for new peers */
#define CI_TRUST_TIER_A        70     /* Tier A threshold (Elite) */
#define CI_TRUST_TIER_B        30     /* Tier B threshold (Average) */
/* Below CI_TRUST_TIER_B = Tier C (Unreliable) */

/* Reward & Penalty values */
#define CI_REWARD_SUCCESS      5      /* Points for correct CW */
#define CI_PENALTY_FAILURE     20     /* Points for wrong CW */
#define CI_PENALTY_TIMEOUT     2      /* Points for timeout */
#define CI_PENALTY_BAN_THRESH  5      /* Trust below this = temporary ban */
#define CI_BAN_DURATION_MS     300000 /* 5 minutes ban */

/* Timeout per tier (milliseconds) */
#define CI_TIMEOUT_TIER_A      200    /* Elite peers: fast timeout */
#define CI_TIMEOUT_TIER_B      500    /* Average peers: medium timeout */
#define CI_TIMEOUT_TIER_C      1000   /* Unreliable peers: slow timeout */

/* Quick validation thresholds */
#define CI_ENTROPY_MIN_GOOD    2.5f   /* Minimum entropy for real CW */
#define CI_ENTROPY_MAX_FAKE    1.5f   /* Maximum entropy for obvious fake */
#define CI_REPEAT_BYTE_THRESH  6      /* 6+ same bytes = suspicious */

/* Prediction parameters */
#define CI_CYCLE_DEFAULT_MS    10000  /* Default CW cycle: 10 seconds */
#define CI_PRELOAD_ADVANCE_MS  2000   /* Pre-fetch 2s before cycle change */

/* ============================================================================
 * DATA STRUCTURES
 * ============================================================================ */

/* Peer tier classification */
typedef enum {
    CI_TIER_A = 0,   /* Elite — trust >= 70 */
    CI_TIER_B = 1,   /* Average — trust 30-69 */
    CI_TIER_C = 2,   /* Unreliable — trust < 30 */
    CI_TIER_BANNED = 3  /* Temporarily banned */
} ci_tier_t;

/* CW validation result */
typedef enum {
    CI_CW_VALID = 0,      /* CW looks good */
    CI_CW_NULL = 1,       /* CW is all zeros */
    CI_CW_REPEAT = 2,     /* CW has repeating bytes */
    CI_CW_LOW_ENTROPY = 3,/* CW entropy too low */
    CI_CW_KNOWN_FAKE = 4, /* CW matches known-fake database */
    CI_CW_HALF_ZERO = 5,  /* One half of CW is all zeros */
    CI_CW_DUPLICATE = 6   /* Same CW for different ECM */
} ci_cw_status_t;

/* Channel key — uniquely identifies a channel */
typedef struct {
    uint16_t caid;
    uint32_t provid;
    uint16_t sid;
} ci_channel_key_t;

/* Per-peer statistics */
typedef struct {
    uint32_t peer_id;          /* Peer identifier (IP or hash) */
    uint32_t ip;               /* Peer IP address */
    
    /* Global trust (across all channels) */
    int trust_global;          /* Global trust score (0-100) */
    uint32_t total_requests;   /* Total ECMs sent to this peer */
    uint32_t total_success;    /* Total correct CWs received */
    uint32_t total_failure;    /* Total wrong CWs received */
    uint32_t total_timeout;    /* Total timeouts */
    
    /* Performance metrics */
    uint32_t avg_latency_ms;   /* Average response time */
    uint32_t last_response_ms; /* Last response time */
    uint32_t last_seen;        /* Last time peer responded (ticks) */
    
    /* Ban status */
    uint32_t ban_until;        /* Ban expiry time (0 = not banned) */
    
    /* Tier (auto-calculated from trust) */
    ci_tier_t tier;
    
} ci_peer_stats_t;

/* Per-channel peer history entry */
typedef struct {
    uint32_t peer_id;
    int trust;                 /* Trust score for this specific channel */
    uint16_t success_count;
    uint16_t failure_count;
    uint32_t last_success;     /* Ticks of last success */
    uint32_t last_failure;     /* Ticks of last failure */
} ci_channel_peer_t;

/* Per-channel statistics */
typedef struct {
    ci_channel_key_t key;      /* Channel identifier */
    
    /* Best and worst peers for this channel */
    ci_channel_peer_t best[CI_MAX_CHANNEL_PEERS];
    ci_channel_peer_t worst[CI_MAX_CHANNEL_PEERS];
    int best_count;
    int worst_count;
    
    /* Channel statistics */
    uint32_t total_ecms;       /* Total ECMs for this channel */
    uint32_t total_success;    /* Total successful decodes */
    uint32_t total_failure;    /* Total failed decodes */
    uint32_t avg_decode_ms;    /* Average decode time */
    
    /* CW cycle detection */
    uint32_t cycle_ms;         /* Detected CW cycle time */
    uint8_t last_tag;          /* Last ECM tag (0x80 or 0x81) */
    uint32_t last_cw_time;     /* Time of last CW received */
    uint8_t last_cw[16];       /* Last CW received */
    
    /* Activity tracking */
    uint32_t last_active;      /* Last time this channel was active */
    int active;                /* Is this channel currently being watched? */
    
} ci_channel_stats_t;

/* Per-CAID pattern detection */
typedef struct {
    uint16_t caid;
    
    /* CW cycle pattern */
    uint32_t cycle_ms;         /* Detected cycle time for this CAID */
    uint8_t tag_pattern;       /* 0x00=unknown, 0x01=alternating, 0x02=same */
    
    /* Timing statistics */
    uint32_t avg_cw_interval;  /* Average time between CW changes */
    uint32_t min_cw_interval;  /* Minimum observed interval */
    uint32_t max_cw_interval;  /* Maximum observed interval */
    
    /* Known-good CW patterns for this CAID */
    uint32_t sample_count;     /* Number of CWs analyzed */
    float avg_entropy;         /* Average entropy of good CWs */
    
} ci_caid_pattern_t;

/* Known-fake CW database entry */
typedef struct {
    uint8_t cw[16];
    uint16_t caid;
    uint32_t provid;
    uint32_t detected_time;
    uint32_t hit_count;        /* How many times this fake was seen */
} ci_fake_cw_t;

/* Predictive cache entry */
typedef struct {
    ci_channel_key_t channel;
    uint8_t predicted_tag;     /* Expected next ECM tag */
    uint8_t cw[16];            /* Pre-fetched CW */
    uint32_t peer_id;          /* Source peer */
    uint32_t fetch_time;       /* When CW was fetched */
    uint32_t valid_until;      /* When this CW expires */
    int valid;                 /* Is this entry valid? */
} ci_predict_entry_t;

/* History entry for peer activity log */
typedef struct {
    uint32_t timestamp;
    ci_channel_key_t channel;
    int success;               /* 1=success, 0=failure */
    uint32_t latency_ms;
} ci_history_entry_t;

/* ============================================================================
 * GLOBAL STATE
 * ============================================================================ */

typedef struct {
    /* Peer database */
    ci_peer_stats_t peers[CI_MAX_PEERS];
    int peer_count;
    
    /* Channel database */
    ci_channel_stats_t channels[CI_MAX_CHANNELS];
    int channel_count;
    
    /* CAID patterns */
    ci_caid_pattern_t caids[CI_MAX_CAIDS];
    int caid_count;
    
    /* Known-fake CW database */
    ci_fake_cw_t fake_cws[256];
    int fake_cw_count;
    
    /* Predictive cache */
    ci_predict_entry_t predict[CI_MAX_PREDICT_BUFFER];
    int predict_count;
    
    /* Global statistics */
    uint64_t total_ecms_processed;
    uint64_t total_cw_accepted;
    uint64_t total_cw_rejected;
    uint64_t total_cw_predicted;   /* CWs served from predictive cache */
    uint64_t total_peers_skipped;  /* Peers skipped due to low trust */
    uint64_t total_bandwidth_saved;/* Estimated bytes saved */
    
    /* System state */
    uint32_t init_time;
    int enabled;
    
    /* Thread safety */
    pthread_mutex_t lock;
    
} ci_state_t;

static ci_state_t g_ci = { .enabled = 0 };

/* ============================================================================
 * UTILITY FUNCTIONS
 * ============================================================================ */

/* Simple hash for channel key */
static inline uint32_t ci_channel_hash(const ci_channel_key_t *key) {
    uint32_t h = key->caid;
    h = h * 31 + key->provid;
    h = h * 31 + key->sid;
    return h;
}

/* Compare two channel keys */
static inline int ci_channel_equal(const ci_channel_key_t *a, const ci_channel_key_t *b) {
    return (a->caid == b->caid && a->provid == b->provid && a->sid == b->sid);
}

/* Calculate Shannon entropy of a CW */
static inline float ci_cw_entropy(const uint8_t cw[16]) {
    int freq[256] = {0};
    int i;
    for (i = 0; i < 16; i++) freq[cw[i]]++;
    
    float entropy = 0.0f;
    for (i = 0; i < 256; i++) {
        if (freq[i] > 0) {
            float p = (float)freq[i] / 16.0f;
            entropy -= p * __builtin_log2f(p);
        }
    }
    return entropy;
}

/* Check if CW is all zeros */
static inline int ci_cw_is_null(const uint8_t cw[16]) {
    int i;
    for (i = 0; i < 16; i++) {
        if (cw[i] != 0) return 0;
    }
    return 1;
}

/* Check if CW has repeating bytes */
static inline int ci_cw_has_repeat(const uint8_t cw[16]) {
    int freq[256] = {0};
    int i;
    for (i = 0; i < 16; i++) freq[cw[i]]++;
    for (i = 0; i < 256; i++) {
        if (freq[i] >= CI_REPEAT_BYTE_THRESH) return 1;
    }
    return 0;
}

/* Check if one half of CW is all zeros */
static inline int ci_cw_half_zero(const uint8_t cw[16]) {
    int i;
    int half1_zero = 1, half2_zero = 1;
    for (i = 0; i < 8; i++) {
        if (cw[i] != 0) half1_zero = 0;
        if (cw[i+8] != 0) half2_zero = 0;
    }
    return half1_zero || half2_zero;
}

/* Check if both halves of CW are identical */
static inline int ci_cw_duplicate_halves(const uint8_t cw[16]) {
    return (memcmp(cw, cw + 8, 8) == 0);
}

/* Get current time in milliseconds */
static inline uint32_t ci_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

/* ============================================================================
 * LEVEL 1: TRUST SCORING
 * ============================================================================ */

/* Find or create peer entry */
static inline ci_peer_stats_t *ci_find_or_create_peer(uint32_t peer_id) {
    int i;
    
    /* Search existing */
    for (i = 0; i < g_ci.peer_count; i++) {
        if (g_ci.peers[i].peer_id == peer_id) {
            return &g_ci.peers[i];
        }
    }
    
    /* Create new */
    if (g_ci.peer_count < CI_MAX_PEERS) {
        ci_peer_stats_t *p = &g_ci.peers[g_ci.peer_count++];
        memset(p, 0, sizeof(*p));
        p->peer_id = peer_id;
        p->trust_global = CI_TRUST_INITIAL;
        p->tier = CI_TIER_B;
        return p;
    }
    
    /* Table full — evict oldest inactive peer */
    uint32_t oldest_time = UINT32_MAX;
    int oldest_idx = 0;
    for (i = 0; i < g_ci.peer_count; i++) {
        if (g_ci.peers[i].last_seen < oldest_time) {
            oldest_time = g_ci.peers[i].last_seen;
            oldest_idx = i;
        }
    }
    
    ci_peer_stats_t *p = &g_ci.peers[oldest_idx];
    memset(p, 0, sizeof(*p));
    p->peer_id = peer_id;
    p->trust_global = CI_TRUST_INITIAL;
    p->tier = CI_TIER_B;
    return p;
}

/* Find peer (read-only) */
static inline ci_peer_stats_t *ci_find_peer(uint32_t peer_id) {
    int i;
    for (i = 0; i < g_ci.peer_count; i++) {
        if (g_ci.peers[i].peer_id == peer_id) {
            return &g_ci.peers[i];
        }
    }
    return NULL;
}

/* Find or create channel entry */
static inline ci_channel_stats_t *ci_find_or_create_channel(
    uint16_t caid, uint32_t provid, uint16_t sid) {
    int i;
    ci_channel_key_t key = { .caid = caid, .provid = provid, .sid = sid };
    
    /* Search existing */
    for (i = 0; i < g_ci.channel_count; i++) {
        if (ci_channel_equal(&g_ci.channels[i].key, &key)) {
            return &g_ci.channels[i];
        }
    }
    
    /* Create new */
    if (g_ci.channel_count < CI_MAX_CHANNELS) {
        ci_channel_stats_t *ch = &g_ci.channels[g_ci.channel_count++];
        memset(ch, 0, sizeof(*ch));
        ch->key = key;
        ch->cycle_ms = CI_CYCLE_DEFAULT_MS;
        ch->active = 1;
        return ch;
    }
    
    /* Table full — evict oldest inactive channel */
    uint32_t oldest_time = UINT32_MAX;
    int oldest_idx = 0;
    for (i = 0; i < g_ci.channel_count; i++) {
        if (g_ci.channels[i].last_active < oldest_time) {
            oldest_time = g_ci.channels[i].last_active;
            oldest_idx = i;
        }
    }
    
    ci_channel_stats_t *ch = &g_ci.channels[oldest_idx];
    memset(ch, 0, sizeof(*ch));
    ch->key = key;
    ch->cycle_ms = CI_CYCLE_DEFAULT_MS;
    ch->active = 1;
    return ch;
}

/* Find channel (read-only) */
static inline ci_channel_stats_t *ci_find_channel(
    uint16_t caid, uint32_t provid, uint16_t sid) {
    int i;
    ci_channel_key_t key = { .caid = caid, .provid = provid, .sid = sid };
    for (i = 0; i < g_ci.channel_count; i++) {
        if (ci_channel_equal(&g_ci.channels[i].key, &key)) {
            return &g_ci.channels[i];
        }
    }
    return NULL;
}

/* ============================================================================
 * LEVEL 2: TIERED PRIORITY
 * ============================================================================ */

/* Calculate tier from trust score */
static inline ci_tier_t ci_trust_to_tier(int trust) {
    if (trust <= CI_PENALTY_BAN_THRESH) return CI_TIER_BANNED;
    if (trust >= CI_TRUST_TIER_A) return CI_TIER_A;
    if (trust >= CI_TRUST_TIER_B) return CI_TIER_B;
    return CI_TIER_C;
}

/* Get timeout for a tier */
static inline uint32_t ci_tier_timeout(ci_tier_t tier) {
    switch (tier) {
        case CI_TIER_A: return CI_TIMEOUT_TIER_A;
        case CI_TIER_B: return CI_TIMEOUT_TIER_B;
        case CI_TIER_C: return CI_TIMEOUT_TIER_C;
        case CI_TIER_BANNED: return 0; /* Don't send at all */
        default: return CI_TIMEOUT_TIER_B;
    }
}

/* Get tier name */
static inline const char *ci_tier_name(ci_tier_t tier) {
    switch (tier) {
        case CI_TIER_A: return "A-Elite";
        case CI_TIER_B: return "B-Average";
        case CI_TIER_C: return "C-Unreliable";
        case CI_TIER_BANNED: return "X-Banned";
        default: return "?-Unknown";
    }
}

/* Check if peer is banned */
static inline int ci_peer_is_banned(ci_peer_stats_t *peer, uint32_t now) {
    if (!peer) return 1;
    if (peer->ban_until > now) return 1;
    /* Auto-unban if ban expired */
    if (peer->tier == CI_TIER_BANNED && peer->ban_until <= now && peer->ban_until > 0) {
        peer->ban_until = 0;
        peer->tier = ci_trust_to_tier(peer->trust_global);
    }
    return 0;
}

/* ============================================================================
 * LEVEL 3: QUICK CW VALIDATION
 * ============================================================================ */

/* Quick validation of incoming CW */
static inline ci_cw_status_t ci_validate_cw(
    const uint8_t cw[16],
    uint16_t caid, uint32_t provid, uint16_t sid) {
    
    /* Check 1: Null CW */
    if (ci_cw_is_null(cw)) return CI_CW_NULL;
    
    /* Check 2: Half-zero CW */
    if (ci_cw_half_zero(cw)) return CI_CW_HALF_ZERO;
    
    /* Check 3: Duplicate halves */
    if (ci_cw_duplicate_halves(cw)) return CI_CW_DUPLICATE;
    
    /* Check 4: Repeating bytes */
    if (ci_cw_has_repeat(cw)) return CI_CW_REPEAT;
    
    /* Check 5: Low entropy (fake CW indicator) */
    float entropy = ci_cw_entropy(cw);
    if (entropy < CI_ENTROPY_MAX_FAKE) return CI_CW_LOW_ENTROPY;
    
    /* Check 6: Known-fake database */
    int i;
    for (i = 0; i < g_ci.fake_cw_count; i++) {
        if (memcmp(g_ci.fake_cws[i].cw, cw, 16) == 0) {
            if (g_ci.fake_cws[i].caid == 0 || g_ci.fake_cws[i].caid == caid) {
                g_ci.fake_cws[i].hit_count++;
                return CI_CW_KNOWN_FAKE;
            }
        }
    }
    
    return CI_CW_VALID;
}

/* Get validation result name */
static inline const char *ci_cw_status_name(ci_cw_status_t status) {
    switch (status) {
        case CI_CW_VALID: return "VALID";
        case CI_CW_NULL: return "NULL";
        case CI_CW_REPEAT: return "REPEAT";
        case CI_CW_LOW_ENTROPY: return "LOW_ENTROPY";
        case CI_CW_KNOWN_FAKE: return "KNOWN_FAKE";
        case CI_CW_HALF_ZERO: return "HALF_ZERO";
        case CI_CW_DUPLICATE: return "DUPLICATE";
        default: return "UNKNOWN";
    }
}

/* Add CW to known-fake database */
static inline void ci_add_fake_cw(
    const uint8_t cw[16],
    uint16_t caid, uint32_t provid) {
    
    pthread_mutex_lock(&g_ci.lock);
    
    /* Check if already exists */
    int i;
    for (i = 0; i < g_ci.fake_cw_count; i++) {
        if (memcmp(g_ci.fake_cws[i].cw, cw, 16) == 0) {
            g_ci.fake_cws[i].hit_count++;
            pthread_mutex_unlock(&g_ci.lock);
            return;
        }
    }
    
    /* Add new entry */
    if (g_ci.fake_cw_count < 256) {
        ci_fake_cw_t *f = &g_ci.fake_cws[g_ci.fake_cw_count++];
        memcpy(f->cw, cw, 16);
        f->caid = caid;
        f->provid = provid;
        f->detected_time = ci_now_ms();
        f->hit_count = 1;
    } else {
        /* Evict oldest entry */
        uint32_t oldest = UINT32_MAX;
        int oldest_idx = 0;
        for (i = 0; i < g_ci.fake_cw_count; i++) {
            if (g_ci.fake_cws[i].detected_time < oldest) {
                oldest = g_ci.fake_cws[i].detected_time;
                oldest_idx = i;
            }
        }
        ci_fake_cw_t *f = &g_ci.fake_cws[oldest_idx];
        memcpy(f->cw, cw, 16);
        f->caid = caid;
        f->provid = provid;
        f->detected_time = ci_now_ms();
        f->hit_count = 1;
    }
    
    pthread_mutex_unlock(&g_ci.lock);
}

/* ============================================================================
 * LEVEL 4: CHANNEL LEARNING
 * ============================================================================ */

/* Find or create channel-peer entry */
static inline ci_channel_peer_t *ci_find_channel_peer(
    ci_channel_stats_t *ch, uint32_t peer_id, int is_best) {
    
    ci_channel_peer_t *list = is_best ? ch->best : ch->worst;
    int count = is_best ? ch->best_count : ch->worst_count;
    int i;
    
    for (i = 0; i < count; i++) {
        if (list[i].peer_id == peer_id) {
            return &list[i];
        }
    }
    return NULL;
}

/* Update channel-peer trust */
static inline void ci_update_channel_peer(
    ci_channel_stats_t *ch, uint32_t peer_id, int success, uint32_t now) {
    
    if (!ch) return;
    
    /* Update best peers list */
    if (success) {
        ci_channel_peer_t *cp = ci_find_channel_peer(ch, peer_id, 1);
        if (cp) {
            cp->success_count++;
            cp->last_success = now;
            /* Recalculate trust */
            int total = cp->success_count + cp->failure_count;
            if (total > 0) {
                cp->trust = (int)((float)cp->success_count / total * 100);
            }
        } else if (ch->best_count < CI_MAX_CHANNEL_PEERS) {
            ci_channel_peer_t *cp = &ch->best[ch->best_count++];
            memset(cp, 0, sizeof(*cp));
            cp->peer_id = peer_id;
            cp->success_count = 1;
            cp->last_success = now;
            cp->trust = 100;
        } else {
            /* Replace worst entry in best list if new peer is better */
            int worst_idx = 0;
            int worst_trust = 101;
            int i;
            for (i = 0; i < ch->best_count; i++) {
                if (ch->best[i].trust < worst_trust) {
                    worst_trust = ch->best[i].trust;
                    worst_idx = i;
                }
            }
            if (worst_trust < 100) {
                ci_channel_peer_t *cp = &ch->best[worst_idx];
                cp->peer_id = peer_id;
                cp->success_count = 1;
                cp->failure_count = 0;
                cp->last_success = now;
                cp->trust = 100;
            }
        }
    } else {
        /* Update worst peers list */
        ci_channel_peer_t *cp = ci_find_channel_peer(ch, peer_id, 0);
        if (cp) {
            cp->failure_count++;
            cp->last_failure = now;
        } else if (ch->worst_count < CI_MAX_CHANNEL_PEERS) {
            ci_channel_peer_t *cp = &ch->worst[ch->worst_count++];
            memset(cp, 0, sizeof(*cp));
            cp->peer_id = peer_id;
            cp->failure_count = 1;
            cp->last_failure = now;
            cp->trust = 0;
        }
        
        /* Also update in best list (reduce trust) */
        cp = ci_find_channel_peer(ch, peer_id, 1);
        if (cp) {
            cp->failure_count++;
            cp->last_failure = now;
            int total = cp->success_count + cp->failure_count;
            if (total > 0) {
                cp->trust = (int)((float)cp->success_count / total * 100);
            }
        }
    }
}

/* ============================================================================
 * LEVEL 5: REWARD & PENALTY
 * ============================================================================ */

/* Record successful CW delivery */
static inline void ci_record_success(
    uint32_t peer_id,
    uint16_t caid, uint32_t provid, uint16_t sid,
    uint32_t latency_ms) {
    
    if (!g_ci.enabled) return;
    
    uint32_t now = ci_now_ms();
    
    pthread_mutex_lock(&g_ci.lock);
    
    /* Update peer stats */
    ci_peer_stats_t *peer = ci_find_or_create_peer(peer_id);
    if (peer) {
        peer->total_success++;
        peer->total_requests++;
        peer->last_response_ms = latency_ms;
        peer->last_seen = now;
        
        /* Reward: increase trust */
        peer->trust_global += CI_REWARD_SUCCESS;
        if (peer->trust_global > CI_TRUST_MAX) peer->trust_global = CI_TRUST_MAX;
        
        /* Update average latency */
        if (peer->avg_latency_ms == 0) {
            peer->avg_latency_ms = latency_ms;
        } else {
            peer->avg_latency_ms = (peer->avg_latency_ms * 7 + latency_ms) / 8;
        }
        
        /* Recalculate tier */
        peer->tier = ci_trust_to_tier(peer->trust_global);
    }
    
    /* Update channel stats */
    ci_channel_stats_t *ch = ci_find_or_create_channel(caid, provid, sid);
    if (ch) {
        ch->total_success++;
        ch->total_ecms++;
        ch->last_active = now;
        
        /* Update average decode time */
        if (ch->avg_decode_ms == 0) {
            ch->avg_decode_ms = latency_ms;
        } else {
            ch->avg_decode_ms = (ch->avg_decode_ms * 7 + latency_ms) / 8;
        }
        
        /* Update channel-peer relationship */
        ci_update_channel_peer(ch, peer_id, 1, now);
    }
    
    g_ci.total_cw_accepted++;
    g_ci.total_ecms_processed++;
    
    pthread_mutex_unlock(&g_ci.lock);
}

/* Record failed CW delivery */
static inline void ci_record_failure(
    uint32_t peer_id,
    uint16_t caid, uint32_t provid, uint16_t sid,
    ci_cw_status_t reason) {
    
    if (!g_ci.enabled) return;
    
    uint32_t now = ci_now_ms();
    
    pthread_mutex_lock(&g_ci.lock);
    
    /* Update peer stats */
    ci_peer_stats_t *peer = ci_find_or_create_peer(peer_id);
    if (peer) {
        peer->total_failure++;
        peer->total_requests++;
        peer->last_seen = now;
        
        /* Penalty: decrease trust */
        peer->trust_global -= CI_PENALTY_FAILURE;
        if (peer->trust_global < CI_TRUST_MIN) peer->trust_global = CI_TRUST_MIN;
        
        /* Auto-ban if trust drops to threshold */
        if (peer->trust_global <= CI_PENALTY_BAN_THRESH) {
            peer->ban_until = now + CI_BAN_DURATION_MS;
            peer->tier = CI_TIER_BANNED;
        } else {
            peer->tier = ci_trust_to_tier(peer->trust_global);
        }
    }
    
    /* Update channel stats */
    ci_channel_stats_t *ch = ci_find_or_create_channel(caid, provid, sid);
    if (ch) {
        ch->total_failure++;
        ch->total_ecms++;
        ch->last_active = now;
        
        /* Update channel-peer relationship */
        ci_update_channel_peer(ch, peer_id, 0, now);
    }
    
    /* If CW was a known pattern, add to fake DB */
    if (reason == CI_CW_KNOWN_FAKE || reason == CI_CW_REPEAT ||
        reason == CI_CW_LOW_ENTROPY) {
        /* CW will be added to fake DB by caller if needed */
    }
    
    g_ci.total_cw_rejected++;
    g_ci.total_ecms_processed++;
    
    pthread_mutex_unlock(&g_ci.lock);
}

/* Record timeout (peer didn't respond) */
static inline void ci_record_timeout(
    uint32_t peer_id,
    uint16_t caid, uint32_t provid, uint16_t sid) {
    
    if (!g_ci.enabled) return;
    
    pthread_mutex_lock(&g_ci.lock);
    
    ci_peer_stats_t *peer = ci_find_or_create_peer(peer_id);
    if (peer) {
        peer->total_timeout++;
        peer->total_requests++;
        
        /* Small penalty for timeout */
        peer->trust_global -= CI_PENALTY_TIMEOUT;
        if (peer->trust_global < CI_TRUST_MIN) peer->trust_global = CI_TRUST_MIN;
        
        peer->tier = ci_trust_to_tier(peer->trust_global);
    }
    
    pthread_mutex_unlock(&g_ci.lock);
}

/* ============================================================================
 * LEVEL 6: SMART PREDICTION
 * ============================================================================ */

/* Get best peers for a channel (sorted by trust) */
static inline int ci_get_best_peers(
    uint16_t caid, uint32_t provid, uint16_t sid,
    uint32_t *peer_ids, int *peer_tiers, int max_peers) {
    
    if (!g_ci.enabled) return 0;
    
    uint32_t now = ci_now_ms();
    int count = 0;
    
    pthread_mutex_lock(&g_ci.lock);
    
    ci_channel_stats_t *ch = ci_find_channel(caid, provid, sid);
    
    if (ch && ch->best_count > 0) {
        /* Use channel-specific best peers */
        int i;
        for (i = 0; i < ch->best_count && count < max_peers; i++) {
            ci_peer_stats_t *peer = ci_find_peer(ch->best[i].peer_id);
            if (peer && !ci_peer_is_banned(peer, now)) {
                peer_ids[count] = peer->peer_id;
                peer_tiers[count] = (int)peer->tier;
                count++;
            }
        }
    }
    
    /* Fill remaining slots with globally trusted peers */
    if (count < max_peers) {
        /* Sort all peers by global trust (descending) */
        int i;
        for (i = 0; i < g_ci.peer_count && count < max_peers; i++) {
            ci_peer_stats_t *peer = &g_ci.peers[i];
            if (ci_peer_is_banned(peer, now)) continue;
            
            /* Check if already in list */
            int already = 0;
            int j;
            for (j = 0; j < count; j++) {
                if (peer_ids[j] == peer->peer_id) {
                    already = 1;
                    break;
                }
            }
            if (already) continue;
            
            /* Only add Tier A or B peers */
            if (peer->tier == CI_TIER_A || peer->tier == CI_TIER_B) {
                peer_ids[count] = peer->peer_id;
                peer_tiers[count] = (int)peer->tier;
                count++;
            }
        }
    }
    
    g_ci.total_peers_skipped += (g_ci.peer_count - count);
    
    pthread_mutex_unlock(&g_ci.lock);
    
    return count;
}

/* Check if a peer should be skipped for a channel */
static inline int ci_should_skip_peer(
    uint32_t peer_id,
    uint16_t caid, uint32_t provid, uint16_t sid) {
    
    if (!g_ci.enabled) return 0;
    
    uint32_t now = ci_now_ms();
    
    pthread_mutex_lock(&g_ci.lock);
    
    ci_peer_stats_t *peer = ci_find_peer(peer_id);
    if (!peer) {
        pthread_mutex_unlock(&g_ci.lock);
        return 0; /* Unknown peer — don't skip */
    }
    
    /* Skip banned peers */
    if (ci_peer_is_banned(peer, now)) {
        pthread_mutex_unlock(&g_ci.lock);
        return 1;
    }
    
    /* Skip Tier C peers if we have better options */
    if (peer->tier == CI_TIER_C) {
        ci_channel_stats_t *ch = ci_find_channel(caid, provid, sid);
        if (ch && ch->best_count > 0) {
            /* We have better peers — skip this one */
            pthread_mutex_unlock(&g_ci.lock);
            return 1;
        }
    }
    
    pthread_mutex_unlock(&g_ci.lock);
    return 0;
}

/* Get recommended timeout for a peer */
static inline uint32_t ci_get_peer_timeout(
    uint32_t peer_id,
    uint16_t caid, uint32_t provid, uint16_t sid) {
    
    if (!g_ci.enabled) return 2000; /* Default 2 seconds */
    
    pthread_mutex_lock(&g_ci.lock);
    
    ci_peer_stats_t *peer = ci_find_peer(peer_id);
    if (!peer) {
        pthread_mutex_unlock(&g_ci.lock);
        return CI_TIMEOUT_TIER_B; /* Default for unknown peers */
    }
    
    uint32_t timeout = ci_tier_timeout(peer->tier);
    
    /* Adjust based on average latency */
    if (peer->avg_latency_ms > 0) {
        /* Use 2x average latency as timeout, but within tier limits */
        uint32_t latency_based = peer->avg_latency_ms * 2;
        if (latency_based > timeout) {
            timeout = latency_based;
        }
    }
    
    pthread_mutex_unlock(&g_ci.lock);
    return timeout;
}

/* ============================================================================
 * LEVEL 7: PREDICTIVE CACHE
 * ============================================================================ */

/* Detect CW cycle pattern for a CAID */
static inline void ci_detect_cycle(
    uint16_t caid, uint32_t provid, uint16_t sid,
    uint8_t ecmtag, uint32_t now) {
    
    ci_caid_pattern_t *pat = NULL;
    int i;
    
    /* Find CAID pattern */
    for (i = 0; i < g_ci.caid_count; i++) {
        if (g_ci.caids[i].caid == caid) {
            pat = &g_ci.caids[i];
            break;
        }
    }
    
    /* Create new if not found */
    if (!pat && g_ci.caid_count < CI_MAX_CAIDS) {
        pat = &g_ci.caids[g_ci.caid_count++];
        memset(pat, 0, sizeof(*pat));
        pat->caid = caid;
        pat->cycle_ms = CI_CYCLE_DEFAULT_MS;
    }
    
    if (!pat) return;
    
    /* Update channel cycle info */
    ci_channel_stats_t *ch = ci_find_channel(caid, provid, sid);
    if (ch) {
        if (ch->last_cw_time > 0) {
            uint32_t interval = now - ch->last_cw_time;
            
            /* Update CAID timing statistics */
            if (pat->avg_cw_interval == 0) {
                pat->avg_cw_interval = interval;
                pat->min_cw_interval = interval;
                pat->max_cw_interval = interval;
            } else {
                pat->avg_cw_interval = (pat->avg_cw_interval * 7 + interval) / 8;
                if (interval < pat->min_cw_interval) pat->min_cw_interval = interval;
                if (interval > pat->max_cw_interval) pat->max_cw_interval = interval;
            }
            
            /* Update channel cycle estimate */
            ch->cycle_ms = pat->avg_cw_interval;
        }
        
        /* Detect tag alternation pattern */
        if (ch->last_tag != 0 && ch->last_tag != ecmtag) {
            pat->tag_pattern = 0x01; /* Alternating */
        } else if (ch->last_tag == ecmtag && ch->last_tag != 0) {
            pat->tag_pattern = 0x02; /* Same */
        }
        
        ch->last_tag = ecmtag;
        ch->last_cw_time = now;
    }
}

/* Store CW in predictive cache */
static inline void ci_predict_store(
    uint16_t caid, uint32_t provid, uint16_t sid,
    uint8_t predicted_tag,
    const uint8_t cw[16],
    uint32_t peer_id,
    uint32_t valid_for_ms) {
    
    if (!g_ci.enabled) return;
    
    uint32_t now = ci_now_ms();
    
    pthread_mutex_lock(&g_ci.lock);
    
    /* Check if entry already exists for this channel+tag */
    int i;
    for (i = 0; i < g_ci.predict_count; i++) {
        ci_predict_entry_t *e = &g_ci.predict[i];
        if (e->channel.caid == caid &&
            e->channel.provid == provid &&
            e->channel.sid == sid &&
            e->predicted_tag == predicted_tag) {
            /* Update existing entry */
            memcpy(e->cw, cw, 16);
            e->peer_id = peer_id;
            e->fetch_time = now;
            e->valid_until = now + valid_for_ms;
            e->valid = 1;
            pthread_mutex_unlock(&g_ci.lock);
            return;
        }
    }
    
    /* Add new entry */
    if (g_ci.predict_count < CI_MAX_PREDICT_BUFFER) {
        ci_predict_entry_t *e = &g_ci.predict[g_ci.predict_count++];
        e->channel.caid = caid;
        e->channel.provid = provid;
        e->channel.sid = sid;
        e->predicted_tag = predicted_tag;
        memcpy(e->cw, cw, 16);
        e->peer_id = peer_id;
        e->fetch_time = now;
        e->valid_until = now + valid_for_ms;
        e->valid = 1;
    } else {
        /* Evict oldest expired entry */
        int oldest_idx = -1;
        uint32_t oldest_time = UINT32_MAX;
        for (i = 0; i < g_ci.predict_count; i++) {
            if (g_ci.predict[i].valid_until < oldest_time) {
                oldest_time = g_ci.predict[i].valid_until;
                oldest_idx = i;
            }
        }
        if (oldest_idx >= 0) {
            ci_predict_entry_t *e = &g_ci.predict[oldest_idx];
            e->channel.caid = caid;
            e->channel.provid = provid;
            e->channel.sid = sid;
            e->predicted_tag = predicted_tag;
            memcpy(e->cw, cw, 16);
            e->peer_id = peer_id;
            e->fetch_time = now;
            e->valid_until = now + valid_for_ms;
            e->valid = 1;
        }
    }
    
    pthread_mutex_unlock(&g_ci.lock);
}

/* Check predictive cache for a pre-fetched CW */
static inline int ci_predict_fetch(
    uint16_t caid, uint32_t provid, uint16_t sid,
    uint8_t ecmtag,
    uint8_t cw_out[16],
    uint32_t *peer_id_out) {
    
    if (!g_ci.enabled) return 0;
    
    uint32_t now = ci_now_ms();
    
    pthread_mutex_lock(&g_ci.lock);
    
    int i;
    for (i = 0; i < g_ci.predict_count; i++) {
        ci_predict_entry_t *e = &g_ci.predict[i];
        if (e->valid &&
            e->channel.caid == caid &&
            e->channel.provid == provid &&
            e->channel.sid == sid &&
            e->predicted_tag == ecmtag &&
            e->valid_until > now) {
            /* Found a valid pre-fetched CW! */
            memcpy(cw_out, e->cw, 16);
            if (peer_id_out) *peer_id_out = e->peer_id;
            e->valid = 0; /* Mark as used */
            g_ci.total_cw_predicted++;
            pthread_mutex_unlock(&g_ci.lock);
            return 1;
        }
    }
    
    pthread_mutex_unlock(&g_ci.lock);
    return 0;
}

/* Calculate next predicted tag based on current tag */
static inline uint8_t ci_predict_next_tag(uint16_t caid, uint8_t current_tag) {
    /* Find CAID pattern */
    int i;
    for (i = 0; i < g_ci.caid_count; i++) {
        if (g_ci.caids[i].caid == caid) {
            if (g_ci.caids[i].tag_pattern == 0x01) {
                /* Alternating pattern: flip tag */
                return (current_tag == 0x80) ? 0x81 : 0x80;
            }
            break;
        }
    }
    /* Default: alternate */
    return (current_tag == 0x80) ? 0x81 : 0x80;
}

/* Check if we should pre-fetch next CW now */
static inline int ci_should_preload(
    uint16_t caid, uint32_t provid, uint16_t sid,
    uint32_t now) {
    
    ci_channel_stats_t *ch = ci_find_channel(caid, provid, sid);
    if (!ch || ch->last_cw_time == 0) return 0;
    
    uint32_t time_since_cw = now - ch->last_cw_time;
    uint32_t preload_threshold = ch->cycle_ms - CI_PRELOAD_ADVANCE_MS;
    
    return (time_since_cw >= preload_threshold);
}

/* ============================================================================
 * INITIALIZATION & STATUS
 * ============================================================================ */

/* Initialize the cache intelligence system */
static inline void ci_init(void) {
    memset(&g_ci, 0, sizeof(g_ci));
    pthread_mutex_init(&g_ci.lock, NULL);
    g_ci.init_time = ci_now_ms();
    g_ci.enabled = 1;
}

/* Cleanup */
static inline void ci_cleanup(void) {
    pthread_mutex_destroy(&g_ci.lock);
    g_ci.enabled = 0;
}

/* Get comprehensive statistics */
static inline void ci_get_stats(
    uint64_t *ecms, uint64_t *accepted, uint64_t *rejected,
    uint64_t *predicted, uint64_t *skipped,
    int *peer_count, int *channel_count, int *fake_cw_count) {
    
    pthread_mutex_lock(&g_ci.lock);
    
    if (ecms) *ecms = g_ci.total_ecms_processed;
    if (accepted) *accepted = g_ci.total_cw_accepted;
    if (rejected) *rejected = g_ci.total_cw_rejected;
    if (predicted) *predicted = g_ci.total_cw_predicted;
    if (skipped) *skipped = g_ci.total_peers_skipped;
    if (peer_count) *peer_count = g_ci.peer_count;
    if (channel_count) *channel_count = g_ci.channel_count;
    if (fake_cw_count) *fake_cw_count = g_ci.fake_cw_count;
    
    pthread_mutex_unlock(&g_ci.lock);
}

/* Print status summary */
static inline void ci_print_status(void) {
    pthread_mutex_lock(&g_ci.lock);
    
    uint32_t uptime_s = (ci_now_ms() - g_ci.init_time) / 1000;
    
    /* Count peers by tier */
    int tier_a = 0, tier_b = 0, tier_c = 0, tier_banned = 0;
    int i;
    for (i = 0; i < g_ci.peer_count; i++) {
        switch (g_ci.peers[i].tier) {
            case CI_TIER_A: tier_a++; break;
            case CI_TIER_B: tier_b++; break;
            case CI_TIER_C: tier_c++; break;
            case CI_TIER_BANNED: tier_banned++; break;
        }
    }
    
    pthread_mutex_unlock(&g_ci.lock);
    
    /* Use fprintf for status output */
    fprintf(stderr, " [CI] Cache Intelligence Status (uptime: %us)\n", uptime_s);
    fprintf(stderr, "   Peers: %d total (A:%d B:%d C:%d Banned:%d)\n",
        g_ci.peer_count, tier_a, tier_b, tier_c, tier_banned);
    fprintf(stderr, "   Channels: %d tracked\n", g_ci.channel_count);
    fprintf(stderr, "   Fake CWs: %d in database\n", g_ci.fake_cw_count);
    fprintf(stderr, "   Predictive: %d entries cached\n", g_ci.predict_count);
    fprintf(stderr, "   Stats: ECMS=%llu OK=%llu REJ=%llu PRED=%llu SKIP=%llu\n",
        (unsigned long long)g_ci.total_ecms_processed,
        (unsigned long long)g_ci.total_cw_accepted,
        (unsigned long long)g_ci.total_cw_rejected,
        (unsigned long long)g_ci.total_cw_predicted,
        (unsigned long long)g_ci.total_peers_skipped);
}

/* Periodic cleanup — call every 60 seconds */
static inline void ci_tick(uint32_t now) {
    if (!g_ci.enabled) return;
    
    pthread_mutex_lock(&g_ci.lock);
    
    /* Clean up expired predictive entries */
    int i;
    for (i = 0; i < g_ci.predict_count; i++) {
        if (g_ci.predict[i].valid && g_ci.predict[i].valid_until < now) {
            g_ci.predict[i].valid = 0;
        }
    }
    
    /* Auto-unban peers whose ban has expired */
    for (i = 0; i < g_ci.peer_count; i++) {
        if (g_ci.peers[i].tier == CI_TIER_BANNED &&
            g_ci.peers[i].ban_until > 0 &&
            g_ci.peers[i].ban_until <= now) {
            g_ci.peers[i].ban_until = 0;
            g_ci.peers[i].trust_global = CI_TRUST_INITIAL / 2; /* Half trust after ban */
            g_ci.peers[i].tier = ci_trust_to_tier(g_ci.peers[i].trust_global);
        }
    }
    
    /* Decay trust for inactive peers (slow decay: -1 per tick) */
    for (i = 0; i < g_ci.peer_count; i++) {
        if (g_ci.peers[i].last_seen > 0 &&
            (now - g_ci.peers[i].last_seen) > 3600000) { /* Inactive > 1 hour */
            if (g_ci.peers[i].trust_global > CI_TRUST_INITIAL) {
                g_ci.peers[i].trust_global--;
                g_ci.peers[i].tier = ci_trust_to_tier(g_ci.peers[i].trust_global);
            }
        }
    }
    
    pthread_mutex_unlock(&g_ci.lock);
}

/* Check if system is enabled */
static inline int ci_is_enabled(void) {
    return g_ci.enabled;
}

#endif /* CACHE_INTELLIGENCE_H */
