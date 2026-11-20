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
#include <time.h>
#include <pthread.h>

/* Monotonic millisecond tick (tools.c GetTickCount, CLOCK_MONOTONIC).
 * Pending-entry ages/timeouts must never use the wall clock: an NTP or
 * DST jump would otherwise expire live checks or pin stale ones.
 * tools.h has no include guard, so declare the tick here instead. */
#if !defined(WIN32)
uint32_t GetTickCount(void);
#endif
#define DC_NOW_MS() ((uint32_t)GetTickCount())

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
    uint8_t event_flags;        /* bit0: verified event logged, bit1: mismatch logged */
} doublecheck_pending_t;

/* One decided comparison, kept for the operator pages (CCcam trace).
 * Until now the only trace of a decision was a debugf() log line; the
 * field operator asked for everything on the trace page instead of
 * hunting logs, so the last decisions live in this ring. */
#define DOUBLECHECK_EVENT_MAX 32
typedef struct {
    uint32_t timestamp;         /* wall-clock seconds, display only */
    uint16_t caid;
    uint32_t provid;
    uint16_t sid;
    int kind;                   /* 1 = two sources agree, 2 = sources disagree */
    int sources;                /* results seen when the decision was made */
} doublecheck_event_t;

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
    /* Band 120: every slot the reclaim sweep takes back, decided or not.
     * `timeouts` used to be incremented HERE for every reclaim, which made it a
     * CLOSURE counter wearing a FAILURE label: the field snapshot of
     * 2026-11-20 22:08 read "started 3317 | timeouts 3276" -- 98.8%, which I
     * (and any reader of that line) read as "almost every check ends in a
     * timeout". It cannot mean that: a check that agreed is still `active`
     * until this same sweep reclaims it 2x DOUBLECHECK_TIMEOUT_MS later. So the
     * two numbers are now separate facts, and the page prints both. */
    uint64_t closed;

    /* Recent decisions ring (for the trace page) */
    doublecheck_event_t events[DOUBLECHECK_EVENT_MAX];
    uint32_t event_seq;         /* total events recorded; slot = seq % MAX */
} doublecheck_state_t;

static doublecheck_state_t g_doublecheck = {0};

/* Caller holds g_doublecheck.lock. */
static inline void doublecheck_record_event(doublecheck_pending_t *p, int kind) {
    doublecheck_event_t *e = &g_doublecheck.events[g_doublecheck.event_seq % DOUBLECHECK_EVENT_MAX];
    e->timestamp = (uint32_t)time(NULL);
    e->caid = p->caid;
    e->provid = p->provid;
    e->sid = p->sid;
    e->kind = kind;
    e->sources = p->result_count;
    g_doublecheck.event_seq++;
}

/* Copy the newest decisions (newest first) plus the counters, for
 * display. Returns the number of events copied. */
static inline int ecm_doublecheck_recent_events(doublecheck_event_t *out, int max,
                                                uint64_t *ok, uint64_t *fail,
                                                uint64_t *total, uint64_t *timeouts) {
    int n = 0;
    uint32_t i, avail;
    pthread_mutex_lock(&g_doublecheck.lock);
    if (ok) *ok = g_doublecheck.verified_ok;
    if (fail) *fail = g_doublecheck.verified_fail;
    if (total) *total = g_doublecheck.total_checks;
    if (timeouts) *timeouts = g_doublecheck.timeouts;
    avail = g_doublecheck.event_seq < DOUBLECHECK_EVENT_MAX
          ? g_doublecheck.event_seq : DOUBLECHECK_EVENT_MAX;
    for (i = 0; i < avail && n < max; i++) {
        uint32_t seq = g_doublecheck.event_seq - 1 - i;
        out[n++] = g_doublecheck.events[seq % DOUBLECHECK_EVENT_MAX];
    }
    pthread_mutex_unlock(&g_doublecheck.lock);
    return n;
}

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
    uint32_t now = DC_NOW_MS();

    pthread_mutex_lock(&g_doublecheck.lock);

    /* Reclaim expired slots first. Nothing in the tree calls
     * ecm_doublecheck_cleanup() periodically, so without this sweep the
     * 256 slots filled once and every later start silently failed. */
    for (i = 0; i < 256; i++) {
        doublecheck_pending_t *p = &g_doublecheck.pending[i];
        if (p->active &&
            (uint32_t)(now - p->start_time) > DOUBLECHECK_TIMEOUT_MS * 2) {
            int decided = p->verified || (p->event_flags & 0x02);
            p->active = 0;
            g_doublecheck.closed++;
            /* Only an UNDECIDED closure is a timeout. event_flags bit1 marks a
             * mismatch decision, bit0 a verified one (see record_event). */
            if (!decided) g_doublecheck.timeouts++;
        }
    }

    /* One ECM, one check: a second dispatch of the same ECM (server
     * interval resend to another card) must join the existing pending
     * entry, not open a competing one -- its CW is the second source
     * this check exists to compare. */
    for (i = 0; i < 256; i++) {
        if (g_doublecheck.pending[i].active &&
            g_doublecheck.pending[i].ecm_hash == ecm_hash) {
            doublecheck_pending_t *p = &g_doublecheck.pending[i];
            pthread_mutex_unlock(&g_doublecheck.lock);
            return p;
        }
    }

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
            p->event_flags = 0;
            p->active = 1;
            p->start_time = now;
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

    {
        int j;
        for (j = 0; j < p->result_count; j++) {
            if (p->reader_ids[j] == reader_id) {
                int v = p->verified ? 1 : 0;
                pthread_mutex_unlock(&g_doublecheck.lock);
                return v;
            }
        }
    }

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
    
    /* Keep the decision for the trace page (once per kind per ECM). */
    if (p->verified && !(p->event_flags & 1)) {
        p->event_flags |= 1;
        doublecheck_record_event(p, 1);
    } else if (!p->verified && p->result_count >= 2 && !(p->event_flags & 2)) {
        p->event_flags |= 2;
        doublecheck_record_event(p, 2);
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

    uint32_t now = DC_NOW_MS();
    if ((now - p->start_time) > DOUBLECHECK_TIMEOUT_MS) {
        /* Band 120: the counter used to be incremented HERE. This predicate has
         * no callers today, but a predicate that mutates a statistic is a trap
         * -- called from a display loop it would count one stalled check once
         * per pass, and `timeouts` would stop being a number of checks at all
         * (which is exactly the class of error `Wrote=0` was). Age is still
         * answered; the accounting belongs to the sweep that closes slots. */
        return 1;
    }
    return 0;
}

/* Band 120: all reclaims (decided + undecided). The page prints it next to
 * `timeouts` so the two can never be confused again: if closed == timeouts, no
 * check ever reached a decision before its slot was taken back. */
static inline uint64_t ecm_doublecheck_closed(void) {
    uint64_t v;
    pthread_mutex_lock(&g_doublecheck.lock);
    v = g_doublecheck.closed;
    pthread_mutex_unlock(&g_doublecheck.lock);
    return v;
}

/* Cleanup completed/timeout entries */
static inline void ecm_doublecheck_cleanup(void) {
    int i;
    uint32_t now = DC_NOW_MS();
    
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

/* The CAID list comes from the operator's config only
 * ("ECM DOUBLECHECK: 1884 0500 ..." in the global section). This
 * function used to hardcode eight CAIDs; invented defaults meant the
 * feature claimed to guard channels nobody asked it to guard. It is
 * kept (cache-speed-suite calls it) as the explicit no-defaults marker:
 * no config line = the feature stays completely inert. */
static inline void ecm_doublecheck_load_defaults(void) {
}

/* Find the active pending entry for an ECM hash, or NULL. Observe-mode
 * callers feed results through ecm_doublecheck_add_result(); the entry
 * lives in the static table and ages out by the start() sweep. */
static inline doublecheck_pending_t *ecm_doublecheck_find(uint32_t ecm_hash) {
    int i;
    doublecheck_pending_t *found = NULL;
    pthread_mutex_lock(&g_doublecheck.lock);
    for (i = 0; i < 256; i++) {
        if (g_doublecheck.pending[i].active &&
            g_doublecheck.pending[i].ecm_hash == ecm_hash) {
            found = &g_doublecheck.pending[i];
            break;
        }
    }
    pthread_mutex_unlock(&g_doublecheck.lock);
    return found;
}

/* Replace the CAID list (config (re)applied at startup and on reload). */
static inline void ecm_doublecheck_reset_caids(void) {
    pthread_mutex_lock(&g_doublecheck.lock);
    g_doublecheck.caid_count = 0;
    pthread_mutex_unlock(&g_doublecheck.lock);
}

/* Parsed by config.c (separate TU); applied here in the main TU. */
extern int cfg_doublecheck_caids[64];
extern int cfg_doublecheck_caid_count;

static inline int ecm_doublecheck_apply_caids(const int *list, int count) {
    int i, n = 0;
    ecm_doublecheck_reset_caids();
    for (i = 0; i < count && i < 64; i++) {
        if (list[i] > 0 && ecm_doublecheck_add_caid((uint16_t)list[i]) == 0) n++;
    }
    return n;
}

static inline int ecm_doublecheck_apply_config(void) {
    return ecm_doublecheck_apply_caids(cfg_doublecheck_caids,
                                       cfg_doublecheck_caid_count);
}

#endif /* ECM_DOUBLECHECK_H */
