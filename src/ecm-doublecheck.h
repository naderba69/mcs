/*
 * ecm-doublecheck.h — ECM Double Check System
 *
 * PURPOSE: Send ECM to multiple readers and verify that CWs match
 * before accepting. This provides strong protection against fake CWs.
 *
 * MECHANISM:
 * 1. Send same ECM to N readers (N >= 2)
 * 2. Collect CW responses
 * 3. Compare CWs: if they match → accept
 * 4. If mismatch → reject, try next pair
 *
 * CONFIG: double_check_caid = CAID1,CAID2,...
 *   Only applies to specified CAIDs
 *   Requires lb_nbest_readers >= 2
 *
 * BENEFITS:
 * - Near 100% protection against fake CWs
 * - Validates CW correctness
 * - Works with load balancer
 *
 * FROM FORUMS:
 * "ECM will be send to two or more readers with the same SC and
 *  the CWs will be verified against each other for defined CAID"
 *  — OSCam Wiki
 */

#ifndef ECM_DOUBLECHECK_H
#define ECM_DOUBLECHECK_H

#include <stdint.h>
#include <string.h>
#include <pthread.h>

#define DOUBLECHECK_MAX_CAIDS 64
#define DOUBLECHECK_MAX_READERS 4
#define DOUBLECHECK_TIMEOUT_MS 3000

/* Double check state for a pending ECM */
typedef struct {
    uint32_t ecm_hash;
    uint16_t caid;
    uint32_t provid;
    uint16_t sid;
    
    uint8_t cw_results[DOUBLECHECK_MAX_READERS][16];
    int reader_ids[DOUBLECHECK_MAX_READERS];
    uint32_t response_times[DOUBLECHECK_MAX_READERS];
    int result_count;
    
    uint32_t start_time;
    int verified;               /* 1 if CWs match */
    uint8_t verified_cw[16];    /* The matching CW */
    
    uint8_t active;
} doublecheck_pending_t;

/* Double check CAID list */
typedef struct {
    uint16_t caid;
    int enabled;
} doublecheck_caid_t;

/* Global state */
typedef struct {
    doublecheck_pending_t pending[256];
    doublecheck_caid_t caids[DOUBLECHECK_MAX_CAIDS];
    int caid_count;
    int enabled;
    
    pthread_mutex_t lock;
    
    /* Statistics */
    uint64_t total_checks;
    uint64_t verified_ok;
    uint64_t verified_fail;
    uint64_t timeouts;
} doublecheck_state_t;

static doublecheck_state_t g_doublecheck = {0};

/* Initialize double check system */
static inline void ecm_doublecheck_init(void) {
    memset(&g_doublecheck, 0, sizeof(g_doublecheck));
    pthread_mutex_init(&g_doublecheck.lock, NULL);
    g_doublecheck.enabled = 1;
}

/* Add a CAID to double check list */
static inline int ecm_doublecheck_add_caid(uint16_t caid) {
    if (g_doublecheck.caid_count >= DOUBLECHECK_MAX_CAIDS) {
        return -1;
    }
    
    g_doublecheck.caids[g_doublecheck.caid_count].caid = caid;
    g_doublecheck.caids[g_doublecheck.caid_count].enabled = 1;
    g_doublecheck.caid_count++;
    return 0;
}

/* Check if a CAID requires double check */
static inline int ecm_doublecheck_needed(uint16_t caid) {
    int i;
    if (!g_doublecheck.enabled) return 0;
    
    for (i = 0; i < g_doublecheck.caid_count; i++) {
        if (g_doublecheck.caids[i].enabled &&
            g_doublecheck.caids[i].caid == caid) {
            return 1;
        }
    }
    return 0;
}

/* Start a double check for an ECM */
static inline doublecheck_pending_t *ecm_doublecheck_start(uint32_t ecm_hash,
                                                            uint16_t caid,
                                                            uint32_t provid,
                                                            uint16_t sid) {
    int i;
    
    pthread_mutex_lock(&g_doublecheck.lock);
    
    /* Find empty slot */
    for (i = 0; i < 256; i++) {
        if (!g_doublecheck.pending[i].active) {
            doublecheck_pending_t *p = &g_doublecheck.pending[i];
            p->ecm_hash = ecm_hash;
            p->caid = caid;
            p->provid = provid;
            p->sid = sid;
            p->result_count = 0;
            p->verified = 0;
            p->active = 1;
            p->start_time = (uint32_t)time(NULL) * 1000;
            memset(p->cw_results, 0, sizeof(p->cw_results));
            
            g_doublecheck.total_checks++;
            pthread_mutex_unlock(&g_doublecheck.lock);
            return p;
        }
    }
    
    pthread_mutex_unlock(&g_doublecheck.lock);
    return NULL;
}

/* Record a CW response from a reader */
static inline int ecm_doublecheck_add_result(doublecheck_pending_t *p,
                                              int reader_id,
                                              const uint8_t cw[16],
                                              uint32_t response_time_ms) {
    if (!p || !p->active) return -1;
    if (p->result_count >= DOUBLECHECK_MAX_READERS) return -1;
    
    pthread_mutex_lock(&g_doublecheck.lock);
    
    int idx = p->result_count;
    memcpy(p->cw_results[idx], cw, 16);
    p->reader_ids[idx] = reader_id;
    p->response_times[idx] = response_time_ms;
    p->result_count++;
    
    /* Check if we have enough results to verify */
    if (p->result_count >= 2) {
        /* Compare first two CWs */
        if (memcmp(p->cw_results[0], p->cw_results[1], 16) == 0) {
            p->verified = 1;
            memcpy(p->verified_cw, p->cw_results[0], 16);
            g_doublecheck.verified_ok++;
        } else {
            /* Mismatch - try to find matching pair */
            int i, j;
            for (i = 0; i < p->result_count; i++) {
                for (j = i + 1; j < p->result_count; j++) {
                    if (memcmp(p->cw_results[i], p->cw_results[j], 16) == 0) {
                        p->verified = 1;
                        memcpy(p->verified_cw, p->cw_results[i], 16);
                        g_doublecheck.verified_ok++;
                        break;
                    }
                }
                if (p->verified) break;
            }
            
            if (!p->verified && p->result_count >= 2) {
                g_doublecheck.verified_fail++;
            }
        }
    }
    
    pthread_mutex_unlock(&g_doublecheck.lock);
    return p->verified ? 1 : 0;
}

/* Check if double check has been verified */
static inline int ecm_doublecheck_is_verified(doublecheck_pending_t *p,
                                               uint8_t *cw_out) {
    if (!p || !p->active) return 0;
    
    int result = p->verified;
    if (result && cw_out) {
        memcpy(cw_out, p->verified_cw, 16);
    }
    return result;
}

/* Check if double check has timed out */
static inline int ecm_doublecheck_is_timeout(doublecheck_pending_t *p) {
    if (!p || !p->active) return 1;
    
    uint32_t now = (uint32_t)time(NULL) * 1000;
    if ((now - p->start_time) > DOUBLECHECK_TIMEOUT_MS) {
        g_doublecheck.timeouts++;
        return 1;
    }
    return 0;
}

/* Cleanup completed/timeout entries */
static inline void ecm_doublecheck_cleanup(void) {
    int i;
    uint32_t now = (uint32_t)time(NULL) * 1000;
    
    pthread_mutex_lock(&g_doublecheck.lock);
    
    for (i = 0; i < 256; i++) {
        doublecheck_pending_t *p = &g_doublecheck.pending[i];
        if (p->active) {
            if (p->verified || (now - p->start_time) > DOUBLECHECK_TIMEOUT_MS * 2) {
                p->active = 0;
            }
        }
    }
    
    pthread_mutex_unlock(&g_doublecheck.lock);
}

/* Load default CAIDs that benefit from double check */
static inline void ecm_doublecheck_load_defaults(void) {
    /* NAGRA - frequent fake CWs */
    ecm_doublecheck_add_caid(0x1810);
    ecm_doublecheck_add_caid(0x1830);
    ecm_doublecheck_add_caid(0x1843);
    ecm_doublecheck_add_caid(0x1884);
    
    /* VIDEOGUARD - targeted by fake CW attacks */
    ecm_doublecheck_add_caid(0x09C4);
    ecm_doublecheck_add_caid(0x098C);
    ecm_doublecheck_add_caid(0x098D);
    
    /* VIACCESS - occasional issues */
    ecm_doublecheck_add_caid(0x0500);
}

#endif /* ECM_DOUBLECHECK_H */
