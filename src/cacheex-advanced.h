/*
 * cacheex-advanced.h — Advanced Cache Exchange Controls
 *
 * PURPOSE: Additional Cache Exchange security and optimization
 * features based on OSCam best practices from technical forums.
 *
 * FEATURES:
 * 1. cacheex_dropdiffs — Drop CWs that differ from known-good CWs
 * 2. cacheex_nopushafter — Stop pushing CWs after time limit per CAID
 * 3. double_check_caid — Double-check CWs from cache exchange
 *
 * FROM FORUMS:
 * - "cacheex_dropdiffs = 1" filters out non-matching cache entries
 *   which helps maintain cache consistency (OSCam Config Guide 2026)
 * - "cacheex_nopushafter = 09:800" stops pushing CWs for CAID 09xx
 *   after 800ms to reduce unnecessary traffic (TechKings)
 * - "double_check_caid" verifies CWs from multiple sources
 */

#ifndef CACHEEX_ADVANCED_H
#define CACHEEX_ADVANCED_H

#include <stdint.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

/* ============================================================================
 * 1. CACHEEX DROP DIFFS
 * ============================================================================ */

#define DROPDIFFS_MAX_CAIDS 128

/* Known-good CW entry */
typedef struct {
    uint16_t caid;
    uint32_t provid;
    uint16_t sid;
    uint8_t cw[16];
    uint8_t ecm_hash[16];
    time_t timestamp;
    int source_trust;           /* Trust level of source (1-10) */
} dropdiffs_entry_t;

/* Drop diffs state */
typedef struct {
    dropdiffs_entry_t entries[DROPDIFFS_MAX_CAIDS];
    int count;
    int enabled;
    pthread_mutex_t lock;
    
    /* Statistics */
    uint64_t total_checked;
    uint64_t total_dropped;
    uint64_t total_accepted;
} dropdiffs_state_t;

static dropdiffs_state_t g_dropdiffs = {0};

/* Initialize drop diffs */
static inline void cacheex_dropdiffs_init(int enabled) {
    memset(&g_dropdiffs, 0, sizeof(g_dropdiffs));
    pthread_mutex_init(&g_dropdiffs.lock, NULL);
    g_dropdiffs.enabled = enabled;
}

/* Store a known-good CW */
static inline void cacheex_dropdiffs_store_good(uint16_t caid, uint32_t provid,
                                                  uint16_t sid,
                                                  const uint8_t cw[16],
                                                  const uint8_t ecm_hash[16],
                                                  int source_trust) {
    if (!g_dropdiffs.enabled) return;
    
    pthread_mutex_lock(&g_dropdiffs.lock);
    
    /* Find existing entry or empty slot */
    int slot = -1;
    int i;
    for (i = 0; i < DROPDIFFS_MAX_CAIDS; i++) {
        if (g_dropdiffs.entries[i].caid == caid &&
            g_dropdiffs.entries[i].provid == provid &&
            g_dropdiffs.entries[i].sid == sid &&
            g_dropdiffs.entries[i].timestamp > 0) {
            slot = i;
            break;
        }
        if (slot < 0 && g_dropdiffs.entries[i].timestamp == 0) {
            slot = i;
        }
    }
    
    if (slot >= 0) {
        dropdiffs_entry_t *e = &g_dropdiffs.entries[slot];
        e->caid = caid;
        e->provid = provid;
        e->sid = sid;
        memcpy(e->cw, cw, 16);
        memcpy(e->ecm_hash, ecm_hash, 16);
        e->timestamp = time(NULL);
        e->source_trust = source_trust;
        g_dropdiffs.count++;
    }
    
    pthread_mutex_unlock(&g_dropdiffs.lock);
}

/*
 * Check if a CW from cache exchange matches a known-good CW
 * Returns: 1 = accept (matches or no reference), 0 = drop (differs)
 */
static inline int cacheex_dropdiffs_check(uint16_t caid, uint32_t provid,
                                            uint16_t sid,
                                            const uint8_t cw[16],
                                            const uint8_t ecm_hash[16]) {
    if (!g_dropdiffs.enabled) return 1;  /* Disabled = accept all */
    
    pthread_mutex_lock(&g_dropdiffs.lock);
    
    g_dropdiffs.total_checked++;
    
    /* Find reference CW */
    int i;
    for (i = 0; i < DROPDIFFS_MAX_CAIDS; i++) {
        dropdiffs_entry_t *e = &g_dropdiffs.entries[i];
        if (e->caid == caid &&
            e->provid == provid &&
            e->sid == sid &&
            e->timestamp > 0 &&
            memcmp(e->ecm_hash, ecm_hash, 16) == 0) {
            
            /* Found reference — compare CWs */
            if (memcmp(e->cw, cw, 16) == 0) {
                g_dropdiffs.total_accepted++;
                pthread_mutex_unlock(&g_dropdiffs.lock);
                return 1;  /* Match — accept */
            } else {
                g_dropdiffs.total_dropped++;
                pthread_mutex_unlock(&g_dropdiffs.lock);
                return 0;  /* Differ — drop */
            }
        }
    }
    
    /* No reference found — accept (can't judge) */
    g_dropdiffs.total_accepted++;
    pthread_mutex_unlock(&g_dropdiffs.lock);
    return 1;
}


/* ============================================================================
 * 2. CACHEEX NOPUSHAFTER
 * ============================================================================ */

#define NOPUSHAFTER_MAX_CAIDS 64

/* No-push-after entry */
typedef struct {
    uint16_t caid;
    uint16_t caid_mask;
    uint32_t max_time_ms;       /* Don't push CWs after this time */
    int enabled;
} nopushafter_entry_t;

/* No-push-after table */
typedef struct {
    nopushafter_entry_t entries[NOPUSHAFTER_MAX_CAIDS];
    int count;
    int enabled;
    
    /* Statistics */
    uint64_t total_pushes;
    uint64_t blocked_pushes;
} nopushafter_table_t;

static nopushafter_table_t g_nopushafter = {0};

/* Initialize no-push-after */
static inline void cacheex_nopushafter_init(int enabled) {
    memset(&g_nopushafter, 0, sizeof(g_nopushafter));
    g_nopushafter.enabled = enabled;
}

/* Add a no-push-after entry */
static inline int cacheex_nopushafter_add(uint16_t caid, uint16_t caid_mask,
                                            uint32_t max_time_ms) {
    if (g_nopushafter.count >= NOPUSHAFTER_MAX_CAIDS) return -1;
    
    nopushafter_entry_t *e = &g_nopushafter.entries[g_nopushafter.count];
    e->caid = caid;
    e->caid_mask = caid_mask;
    e->max_time_ms = max_time_ms;
    e->enabled = 1;
    
    g_nopushafter.count++;
    return 0;
}

/*
 * Check if a CW should be pushed to cache exchange peers
 * Returns: 1 = should push, 0 = should not push (too late)
 */
static inline int cacheex_nopushafter_check(uint16_t caid, uint32_t ecm_age_ms) {
    if (!g_nopushafter.enabled) return 1;  /* Disabled = always push */
    
    g_nopushafter.total_pushes++;
    
    int i;
    for (i = 0; i < g_nopushafter.count; i++) {
        nopushafter_entry_t *e = &g_nopushafter.entries[i];
        if (!e->enabled) continue;
        
        if ((caid & e->caid_mask) == (e->caid & e->caid_mask)) {
            if (ecm_age_ms > e->max_time_ms) {
                g_nopushafter.blocked_pushes++;
                return 0;  /* Too late — don't push */
            }
        }
    }
    
    return 1;  /* OK to push */
}

/* Load default no-push-after settings (from forums) */
static inline void cacheex_nopushafter_load_defaults(void) {
    /* VIDEOGUARD (09xx) — don't push after 500ms (fast cycles) */
    cacheex_nopushafter_add(0x0900, 0xFF00, 500);
    
    /* SECA (01xx) — don't push after 1500ms */
    cacheex_nopushafter_add(0x0100, 0xFF00, 1500);
    
    /* VIACCESS (05xx) — don't push after 1500ms */
    cacheex_nopushafter_add(0x0500, 0xFF00, 1500);
    
    /* IRDETO (06xx) — don't push after 1500ms */
    cacheex_nopushafter_add(0x0600, 0xFF00, 1500);
    
    /* CONAX (0Bxx) — don't push after 2000ms */
    cacheex_nopushafter_add(0x0B00, 0xFF00, 2000);
    
    /* CRYPTOWORKS (0Dxx) — don't push after 2000ms */
    cacheex_nopushafter_add(0x0D00, 0xFF00, 2000);
    
    /* NAGRA (17xx) — don't push after 2500ms */
    cacheex_nopushafter_add(0x1700, 0xFF00, 2500);
    
    /* NAGRA (18xx) — don't push after 2000ms */
    cacheex_nopushafter_add(0x1800, 0xFF00, 2000);
}


/* ============================================================================
 * 3. DOUBLE CHECK CAID (Cache Exchange)
 * ============================================================================ */

#define DOUBLECHECK_CEX_MAX_CAIDS 64

/* Double check entry for cache exchange */
typedef struct {
    uint16_t caid;
    int min_confirmations;       /* Minimum CW confirmations needed */
    int enabled;
} doublecheck_cex_entry_t;

/* Double check table for cache exchange */
typedef struct {
    doublecheck_cex_entry_t entries[DOUBLECHECK_CEX_MAX_CAIDS];
    int count;
    int enabled;
    
    /* Statistics */
    uint64_t total_cws;
    uint64_t confirmed_cws;
    uint64_t unconfirmed_cws;
} doublecheck_cex_table_t;

static doublecheck_cex_table_t g_doublecheck_cex = {0};

/* Initialize double check for cache exchange */
static inline void cacheex_doublecheck_init(int enabled) {
    memset(&g_doublecheck_cex, 0, sizeof(g_doublecheck_cex));
    g_doublecheck_cex.enabled = enabled;
}

/* Add a CAID that requires double check */
static inline int cacheex_doublecheck_add(uint16_t caid, int min_confirmations) {
    if (g_doublecheck_cex.count >= DOUBLECHECK_CEX_MAX_CAIDS) return -1;
    
    doublecheck_cex_entry_t *e = &g_doublecheck_cex.entries[g_doublecheck_cex.count];
    e->caid = caid;
    e->min_confirmations = min_confirmations;
    e->enabled = 1;
    
    g_doublecheck_cex.count++;
    return 0;
}

/* Check if a CAID requires double check from cache exchange */
static inline int cacheex_doublecheck_needed(uint16_t caid) {
    if (!g_doublecheck_cex.enabled) return 0;
    
    int i;
    for (i = 0; i < g_doublecheck_cex.count; i++) {
        if (g_doublecheck_cex.entries[i].enabled &&
            g_doublecheck_cex.entries[i].caid == caid) {
            return g_doublecheck_cex.entries[i].min_confirmations;
        }
    }
    return 0;  /* No double check needed */
}

/*
 * Validate a CW from cache exchange
 * cw_count = how many times this CW has been seen (from cacheex_cw_check)
 * Returns: 1 = accept, 0 = reject (not enough confirmations)
 */
static inline int cacheex_doublecheck_validate(uint16_t caid, int cw_count) {
    if (!g_doublecheck_cex.enabled) return 1;
    
    g_doublecheck_cex.total_cws++;
    
    int required = cacheex_doublecheck_needed(caid);
    if (required <= 0) return 1;  /* No check needed */
    
    if (cw_count >= required) {
        g_doublecheck_cex.confirmed_cws++;
        return 1;  /* Enough confirmations */
    }
    
    g_doublecheck_cex.unconfirmed_cws++;
    return 0;  /* Not enough confirmations */
}

/* Load default double check CAIDs */
static inline void cacheex_doublecheck_load_defaults(void) {
    /* CAIDs most targeted by fake CWs */
    cacheex_doublecheck_add(0x1810, 2);  /* Movistar+ */
    cacheex_doublecheck_add(0x1830, 2);  /* HD+ */
    cacheex_doublecheck_add(0x1843, 2);  /* HD+ Austria */
    cacheex_doublecheck_add(0x09C4, 2);  /* Sky DE V14 */
    cacheex_doublecheck_add(0x098C, 2);  /* Sky DE V13 */
    cacheex_doublecheck_add(0x098D, 2);  /* Sky DE V15 */
    cacheex_doublecheck_add(0x0500, 2);  /* TNT Sat / Bis TV */
    cacheex_doublecheck_add(0x0B00, 2);  /* Conax */
    cacheex_doublecheck_add(0x1884, 2);  /* NC+ 4K */
}


/* ============================================================================
 * UNIFIED INITIALIZATION
 * ============================================================================ */

static inline void cacheex_advanced_init(void) {
    cacheex_dropdiffs_init(1);
    cacheex_nopushafter_init(1);
    cacheex_nopushafter_load_defaults();
    cacheex_doublecheck_init(1);
    cacheex_doublecheck_load_defaults();
}

#endif /* CACHEEX_ADVANCED_H */
