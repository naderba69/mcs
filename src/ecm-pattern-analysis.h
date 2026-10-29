/*
 * ecm-pattern-analysis.h — ECM Pattern Analysis for Attack Detection
 *
 * PURPOSE: Detect coordinated attacks by analyzing ECM patterns over time.
 *
 * ATTACK PATTERNS DETECTED:
 *
 * 1. EMM-SHARED Attack (from LinuxSat-Support Thread #151206):
 *    "They infect shared cards through a cycle programmed with cw fakes"
 *    Pattern: EMM arrives → 3-5 seconds → burst of fake CWs → recovery
 *    Detection: Track EMM timing, flag CWs arriving 3-5s after EMM
 *
 * 2. Timeout Recovery Attack (from LinuxSat-Support, CAID 1810):
 *    "gives a 5000s timeout and just before returning to the correct
 *     cycle... it again gives 5000s timeout and starts viewing"
 *    Pattern: Unusual timeout → brief fake CW burst → normal resumes
 *    Detection: Flag CWs immediately after abnormal timeouts
 *
 * 3. Burst Fake Attack (common across all CAIDs):
 *    Multiple fake CWs arrive in rapid succession from same source
 *    Pattern: 3+ consecutive bad CWs from same source within 10 seconds
 *    Detection: Sliding window counter per source
 *
 * 4. Cache Poisoning Storm (from Digital-Eliteboard):
 *    Fake CWs pushed via CacheEx to poison multiple nodes
 *    Pattern: Same bad CW hash from multiple CacheEx peers rapidly
 *    Detection: Cross-peer CW hash correlation
 *
 * 5. Provider Punishment (from LinuxSat-Support, Movistar+):
 *    Provider sends mix of real and fake CWs to confuse sharing
 *    Pattern: Alternating real/fake CWs with ~50% fake rate
 *    Detection: Statistical anomaly in success/failure ratio
 *
 * BENEFITS:
 * - Early warning before black screen cascade
 * - Automatic source quarantine
 * - Pattern-based learning (improves over time)
 */

#ifndef ECM_PATTERN_ANALYSIS_H
#define ECM_PATTERN_ANALYSIS_H

#include <stdint.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

/* ============================================================================
 * CONFIGURATION
 * ============================================================================ */

#define ECMPAT_MAX_SOURCES 128
#define ECMPAT_MAX_CAIDS 64
#define ECMPAT_HISTORY_SIZE 32       /* Rolling history per source */
#define ECMPAT_EMM_WINDOW_MS 5000    /* EMM → fake CW window (5s) */
#define ECMPAT_BURST_THRESHOLD 3     /* 3+ bad CWs = burst attack */
#define ECMPAT_BURST_WINDOW_MS 10000 /* Burst detection window (10s) */
#define ECMPAT_QUARANTINE_MS 30000   /* Quarantine duration (30s) */
#define ECMPAT_POISON_THRESHOLD 5    /* 5+ same bad CW = poisoning */

/* ============================================================================
 * ATTACK TYPE ENUMERATION
 * ============================================================================ */

typedef enum {
    ECMPAT_ATTACK_NONE = 0,
    ECMPAT_ATTACK_EMM_SHARED = 1,    /* EMM injection attack */
    ECMPAT_ATTACK_TIMEOUT_RECOVERY = 2, /* Post-timeout fake burst */
    ECMPAT_ATTACK_BURST_FAKE = 3,    /* Rapid fake CW burst */
    ECMPAT_ATTACK_CACHE_POISON = 4,  /* CacheEx poisoning storm */
    ECMPAT_ATTACK_PROVIDER_PUNISH = 5, /* Provider punishment cycle */
    ECMPAT_ATTACK_STATISTICAL = 6    /* Statistical anomaly */
} ecmpat_attack_type_t;

/* ============================================================================
 * SOURCE TRACKING
 * ============================================================================ */

struct ecmpat_event {
    uint32_t timestamp_ms;
    uint16_t caid;
    uint32_t provid;
    uint16_t sid;
    uint8_t cw_hash[8];         /* First 8 bytes as fingerprint */
    int is_success;             /* 1 = good CW, 0 = bad CW */
    int is_emm;                 /* 1 = EMM event */
    uint8_t ecm_byte0;          /* First byte of ECM (odd/even) */
};

struct ecmpat_source {
    uint32_t source_id;
    uint16_t caid;              /* Primary CAID for this source */
    
    /* Rolling event history */
    struct ecmpat_event history[ECMPAT_HISTORY_SIZE];
    int history_head;
    int history_count;
    
    /* Counters */
    uint64_t total_events;
    uint64_t good_cws;
    uint64_t bad_cws;
    uint64_t emm_events;
    
    /* Timing */
    uint32_t last_emm_time;     /* Last EMM timestamp */
    uint32_t last_good_cw_time;
    uint32_t last_bad_cw_time;
    uint32_t last_timeout_time;
    
    /* Burst tracking */
    int consecutive_bad;        /* Consecutive bad CWs */
    uint32_t burst_start_time;  /* When burst started */
    
    /* Attack state */
    ecmpat_attack_type_t current_attack;
    uint32_t quarantine_until;  /* Source quarantined until this time */
    int attack_count;           /* Total attacks detected */
    
    /* Statistical tracking */
    double success_rate;        /* Rolling success rate (0.0-1.0) */
    double fake_rate;           /* Rolling fake rate (0.0-1.0) */
    
    uint8_t active;
};

/* ============================================================================
 * CAID TRACKING
 * ============================================================================ */

struct ecmpat_caid_stats {
    uint16_t caid;
    uint64_t total_cws;
    uint64_t total_fakes;
    uint64_t total_emms;
    uint64_t total_attacks;
    uint32_t last_attack_time;
    ecmpat_attack_type_t last_attack_type;
    uint8_t active;
};

/* ============================================================================
 * GLOBAL STATE
 * ============================================================================ */

struct ecmpat_state {
    struct ecmpat_source sources[ECMPAT_MAX_SOURCES];
    struct ecmpat_caid_stats caids[ECMPAT_MAX_CAIDS];
    int source_count;
    int caid_count;
    pthread_mutex_t lock;
    
    /* Global statistics */
    uint64_t total_events;
    uint64_t total_attacks_detected;
    uint64_t total_quarantines;
    uint64_t total_cws_blocked;
    
    /* Attack type counters */
    uint64_t attacks_by_type[7]; /* Indexed by ecmpat_attack_type_t */
};

static struct ecmpat_state g_ecmpat = {0};

/* ============================================================================
 * INITIALIZATION
 * ============================================================================ */

static inline void ecmpat_init(void)
{
    memset(&g_ecmpat, 0, sizeof(g_ecmpat));
    pthread_mutex_init(&g_ecmpat.lock, NULL);
}

/* ============================================================================
 * SOURCE MANAGEMENT
 * ============================================================================ */

static inline struct ecmpat_source *ecmpat_find_source(uint32_t source_id)
{
    int i;
    for (i = 0; i < g_ecmpat.source_count; i++) {
        if (g_ecmpat.sources[i].active &&
            g_ecmpat.sources[i].source_id == source_id) {
            return &g_ecmpat.sources[i];
        }
    }
    return NULL;
}

static inline struct ecmpat_source *ecmpat_get_or_create(uint32_t source_id)
{
    struct ecmpat_source *src = ecmpat_find_source(source_id);
    if (src) return src;
    
    if (g_ecmpat.source_count >= ECMPAT_MAX_SOURCES) {
        /* Evict oldest */
        int oldest = 0;
        uint32_t oldest_time = 0xFFFFFFFF;
        int i;
        for (i = 0; i < g_ecmpat.source_count; i++) {
            if (g_ecmpat.sources[i].history_count > 0) {
                int last = (g_ecmpat.sources[i].history_head - 1 + ECMPAT_HISTORY_SIZE) % ECMPAT_HISTORY_SIZE;
                if (g_ecmpat.sources[i].history[last].timestamp_ms < oldest_time) {
                    oldest_time = g_ecmpat.sources[i].history[last].timestamp_ms;
                    oldest = i;
                }
            }
        }
        memset(&g_ecmpat.sources[oldest], 0, sizeof(struct ecmpat_source));
        src = &g_ecmpat.sources[oldest];
    } else {
        src = &g_ecmpat.sources[g_ecmpat.source_count++];
    }
    
    src->source_id = source_id;
    src->active = 1;
    src->success_rate = 0.5;  /* Start neutral */
    src->fake_rate = 0.0;
    return src;
}

/* ============================================================================
 * CAID STATS MANAGEMENT
 * ============================================================================ */

static inline struct ecmpat_caid_stats *ecmpat_get_caid_stats(uint16_t caid)
{
    int i;
    for (i = 0; i < g_ecmpat.caid_count; i++) {
        if (g_ecmpat.caids[i].active && g_ecmpat.caids[i].caid == caid) {
            return &g_ecmpat.caids[i];
        }
    }
    if (g_ecmpat.caid_count < ECMPAT_MAX_CAIDS) {
        struct ecmpat_caid_stats *cs = &g_ecmpat.caids[g_ecmpat.caid_count++];
        cs->caid = caid;
        cs->active = 1;
        return cs;
    }
    return NULL;
}

/* ============================================================================
 * EVENT RECORDING
 * ============================================================================ */

static inline void ecmpat_record_event(struct ecmpat_source *src,
                                         uint16_t caid, uint32_t provid,
                                         uint16_t sid,
                                         const uint8_t *cw_hash,
                                         int is_success, int is_emm,
                                         uint8_t ecm_byte0,
                                         uint32_t now_ms)
{
    /* Add to rolling history */
    struct ecmpat_event *evt = &src->history[src->history_head];
    evt->timestamp_ms = now_ms;
    evt->caid = caid;
    evt->provid = provid;
    evt->sid = sid;
    if (cw_hash) memcpy(evt->cw_hash, cw_hash, 8);
    evt->is_success = is_success;
    evt->is_emm = is_emm;
    evt->ecm_byte0 = ecm_byte0;
    
    src->history_head = (src->history_head + 1) % ECMPAT_HISTORY_SIZE;
    if (src->history_count < ECMPAT_HISTORY_SIZE) src->history_count++;
    
    src->total_events++;
    src->caid = caid;
    
    if (is_emm) {
        src->emm_events++;
        src->last_emm_time = now_ms;
    } else if (is_success) {
        src->good_cws++;
        src->last_good_cw_time = now_ms;
        src->consecutive_bad = 0;  /* Reset burst counter */
    } else {
        src->bad_cws++;
        src->last_bad_cw_time = now_ms;
        src->consecutive_bad++;
        if (src->consecutive_bad == 1) {
            src->burst_start_time = now_ms;
        }
    }
    
    /* Update rolling success rate (exponential moving average) */
    double alpha = 0.1;
    if (!is_emm) {
        src->success_rate = alpha * (is_success ? 1.0 : 0.0) + (1.0 - alpha) * src->success_rate;
        src->fake_rate = 1.0 - src->success_rate;
    }
}

/* ============================================================================
 * ATTACK DETECTION ALGORITHMS
 * ============================================================================ */

/*
 * Attack 1: EMM-SHARED Injection
 * Pattern: EMM arrives → within 3-5 seconds → fake CWs start
 */
static inline int ecmpat_detect_emm_shared(struct ecmpat_source *src,
                                            int is_emm, int is_success,
                                            uint32_t now_ms)
{
    if (is_emm) return 0;  /* EMM itself is not an attack */
    
    /* Check if we recently received an EMM */
    if (src->last_emm_time == 0) return 0;
    
    uint32_t elapsed = now_ms - src->last_emm_time;
    
    /* If CW arrived within EMM window AND it's bad → EMM-SHARED attack */
    if (elapsed < ECMPAT_EMM_WINDOW_MS && !is_success) {
        return 1;
    }
    
    return 0;
}

/*
 * Attack 2: Timeout Recovery
 * Pattern: Long timeout → brief fake CW burst → normal resumes
 */
static inline int ecmpat_detect_timeout_recovery(struct ecmpat_source *src,
                                                   int is_success,
                                                   uint32_t now_ms)
{
    if (is_success) return 0;  /* Good CWs are fine */
    
    /* Check if we had a recent timeout (>5 seconds gap) */
    if (src->last_good_cw_time > 0) {
        uint32_t gap = now_ms - src->last_good_cw_time;
        if (gap > 5000 && gap < 30000 && src->consecutive_bad >= 1) {
            return 1;
        }
    }
    
    return 0;
}

/*
 * Attack 3: Burst Fake
 * Pattern: 3+ consecutive bad CWs within 10 seconds
 */
static inline int ecmpat_detect_burst_fake(struct ecmpat_source *src,
                                            uint32_t now_ms)
{
    if (src->consecutive_bad < ECMPAT_BURST_THRESHOLD) return 0;
    
    uint32_t burst_duration = now_ms - src->burst_start_time;
    if (burst_duration < ECMPAT_BURST_WINDOW_MS) {
        return 1;
    }
    
    return 0;
}

/*
 * Attack 4: Cache Poisoning Storm
 * Pattern: Same bad CW hash seen from multiple sources rapidly
 */
static inline int ecmpat_detect_cache_poison(const uint8_t *cw_hash,
                                              uint32_t now_ms)
{
    int i, j;
    int match_count = 0;
    
    if (!cw_hash) return 0;
    
    /* Check if same CW hash appears in multiple sources recently */
    for (i = 0; i < g_ecmpat.source_count; i++) {
        struct ecmpat_source *src = &g_ecmpat.sources[i];
        if (!src->active) continue;
        
        /* Check recent events for this hash */
        int start = (src->history_head - src->history_count + ECMPAT_HISTORY_SIZE) % ECMPAT_HISTORY_SIZE;
        for (j = 0; j < src->history_count && j < 10; j++) {
            int idx = (start + src->history_count - 1 - j) % ECMPAT_HISTORY_SIZE;
            struct ecmpat_event *evt = &src->history[idx];
            if (!evt->is_success && !evt->is_emm &&
                (now_ms - evt->timestamp_ms) < 5000 &&
                memcmp(evt->cw_hash, cw_hash, 8) == 0) {
                match_count++;
                break;  /* Count each source only once */
            }
        }
    }
    
    return (match_count >= ECMPAT_POISON_THRESHOLD) ? 1 : 0;
}

/*
 * Attack 5: Provider Punishment
 * Pattern: Success rate drops below 40% over last 20 events
 * (Normal should be >70%)
 */
static inline int ecmpat_detect_provider_punish(struct ecmpat_source *src)
{
    /* Need at least 10 events for statistical significance */
    if (src->total_events < 10) return 0;
    
    /* If success rate drops below 40%, something is wrong */
    if (src->success_rate < 0.40 && src->fake_rate > 0.50) {
        return 1;
    }
    
    return 0;
}

/* ============================================================================
 * QUARANTINE MANAGEMENT
 * ============================================================================ */

static inline void ecmpat_quarantine_source(struct ecmpat_source *src,
                                             uint32_t now_ms)
{
    src->quarantine_until = now_ms + ECMPAT_QUARANTINE_MS;
    src->attack_count++;
    g_ecmpat.total_quarantines++;
}

static inline int ecmpat_is_quarantined(struct ecmpat_source *src,
                                          uint32_t now_ms)
{
    if (src->quarantine_until == 0) return 0;
    if (now_ms > src->quarantine_until) {
        src->quarantine_until = 0;  /* Quarantine expired */
        return 0;
    }
    return 1;
}

/* ============================================================================
 * MAIN ANALYSIS FUNCTION
 * ============================================================================ */

/*
 * Analyze an incoming CW/EMM event for attack patterns
 *
 * Returns:
 *   0 = No attack detected, CW is safe
 *   1 = Attack detected, CW should be BLOCKED
 *   2 = Source is quarantined, all CWs should be BLOCKED
 *
 * attack_type_out: filled with the detected attack type
 */
static inline int ecmpat_analyze(uint32_t source_id,
                                  uint16_t caid, uint32_t provid, uint16_t sid,
                                  const uint8_t *cw,
                                  int is_success, int is_emm,
                                  uint8_t ecm_byte0,
                                  uint32_t now_ms,
                                  ecmpat_attack_type_t *attack_type_out)
{
    int attack = 0;
    ecmpat_attack_type_t attack_type = ECMPAT_ATTACK_NONE;
    
    pthread_mutex_lock(&g_ecmpat.lock);
    
    g_ecmpat.total_events++;
    
    struct ecmpat_source *src = ecmpat_get_or_create(source_id);
    struct ecmpat_caid_stats *cstats = ecmpat_get_caid_stats(caid);
    
    /* Check if source is quarantined */
    if (ecmpat_is_quarantined(src, now_ms)) {
        g_ecmpat.total_cws_blocked++;
        pthread_mutex_unlock(&g_ecmpat.lock);
        if (attack_type_out) *attack_type_out = ECMPAT_ATTACK_NONE;
        return 2;  /* Quarantined */
    }
    
    /* Compute CW hash for pattern matching */
    uint8_t cw_hash[8] = {0};
    if (cw) {
        memcpy(cw_hash, cw, 8);
    }
    
    /* Record the event */
    ecmpat_record_event(src, caid, provid, sid, cw_hash,
                        is_success, is_emm, ecm_byte0, now_ms);
    
    /* Update CAID stats */
    if (cstats) {
        cstats->total_cws++;
        if (is_emm) cstats->total_emms++;
        if (!is_success && !is_emm) cstats->total_fakes++;
    }
    
    /* Run attack detection algorithms */
    
    /* 1. EMM-SHARED Attack */
    if (ecmpat_detect_emm_shared(src, is_emm, is_success, now_ms)) {
        attack = 1;
        attack_type = ECMPAT_ATTACK_EMM_SHARED;
    }
    
    /* 2. Timeout Recovery Attack */
    if (!attack && ecmpat_detect_timeout_recovery(src, is_success, now_ms)) {
        attack = 1;
        attack_type = ECMPAT_ATTACK_TIMEOUT_RECOVERY;
    }
    
    /* 3. Burst Fake Attack */
    if (!attack && ecmpat_detect_burst_fake(src, now_ms)) {
        attack = 1;
        attack_type = ECMPAT_ATTACK_BURST_FAKE;
    }
    
    /* 4. Cache Poisoning Storm */
    if (!attack && !is_success && !is_emm) {
        if (ecmpat_detect_cache_poison(cw_hash, now_ms)) {
            attack = 1;
            attack_type = ECMPAT_ATTACK_CACHE_POISON;
        }
    }
    
    /* 5. Provider Punishment */
    if (!attack && ecmpat_detect_provider_punish(src)) {
        attack = 1;
        attack_type = ECMPAT_ATTACK_PROVIDER_PUNISH;
    }
    
    /* If attack detected, quarantine the source */
    if (attack) {
        ecmpat_quarantine_source(src, now_ms);
        src->current_attack = attack_type;
        g_ecmpat.total_attacks_detected++;
        g_ecmpat.attacks_by_type[attack_type]++;
        
        if (cstats) {
            cstats->total_attacks++;
            cstats->last_attack_time = now_ms;
            cstats->last_attack_type = attack_type;
        }
    }
    
    pthread_mutex_unlock(&g_ecmpat.lock);
    
    if (attack_type_out) *attack_type_out = attack_type;
    return attack ? 1 : 0;
}

/* ============================================================================
 * QUERY FUNCTIONS
 * ============================================================================ */

/*
 * Get human-readable attack type name
 */
static inline const char *ecmpat_attack_name(ecmpat_attack_type_t type)
{
    switch (type) {
        case ECMPAT_ATTACK_NONE:             return "none";
        case ECMPAT_ATTACK_EMM_SHARED:       return "EMM-SHARED injection";
        case ECMPAT_ATTACK_TIMEOUT_RECOVERY: return "timeout recovery";
        case ECMPAT_ATTACK_BURST_FAKE:       return "burst fake CW";
        case ECMPAT_ATTACK_CACHE_POISON:     return "cache poisoning storm";
        case ECMPAT_ATTACK_PROVIDER_PUNISH:  return "provider punishment";
        case ECMPAT_ATTACK_STATISTICAL:      return "statistical anomaly";
        default:                             return "unknown";
    }
}

/*
 * Check if a source is currently safe (not quarantined)
 */
static inline int ecmpat_source_is_safe(uint32_t source_id, uint32_t now_ms)
{
    pthread_mutex_lock(&g_ecmpat.lock);
    struct ecmpat_source *src = ecmpat_find_source(source_id);
    int safe = 1;
    if (src && ecmpat_is_quarantined(src, now_ms)) {
        safe = 0;
    }
    pthread_mutex_unlock(&g_ecmpat.lock);
    return safe;
}

/*
 * Get global attack statistics
 */
static inline void ecmpat_get_stats(uint64_t *total_events,
                                     uint64_t *total_attacks,
                                     uint64_t *total_blocked,
                                     uint64_t *total_quarantines)
{
    pthread_mutex_lock(&g_ecmpat.lock);
    if (total_events) *total_events = g_ecmpat.total_events;
    if (total_attacks) *total_attacks = g_ecmpat.total_attacks_detected;
    if (total_blocked) *total_blocked = g_ecmpat.total_cws_blocked;
    if (total_quarantines) *total_quarantines = g_ecmpat.total_quarantines;
    pthread_mutex_unlock(&g_ecmpat.lock);
}

#endif /* ECM_PATTERN_ANALYSIS_H */
