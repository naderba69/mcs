/*
 * cachex-waittime.h — Per-CAID Wait Time for Cache Exchange
 *
 * PURPOSE: Configure different wait times for each CAID family
 * before falling back to real card readers during cache exchange.
 *
 * MECHANISM: When a cache exchange request arrives, wait for the
 * configured time before sending to a real reader. This gives
 * cache peers time to respond with already-decoded CWs.
 *
 * CONFIG FORMAT (like OSCam wait_time):
 *   wait_time = 18:1500,01:1500,05:2000,0B:1500,09:300,06:1500,0D:2000
 *   Format: caid:awtime:dwtime or caid:time
 *
 * PARAMETERS:
 *   awtime = ask wait time (time to wait before asking peers)
 *   dwtime = dynamic wait time (time to wait for peer response)
 *
 * BENEFITS:
 * - Optimized wait per encryption family
 * - Reduces unnecessary card reads
 * - Faster response for fast CAIDs (09xx)
 * - More time for slow CAIDs (18xx, 0500)
 */

#ifndef CACHEX_WAITTIME_H
#define CACHEX_WAITTIME_H

#include <stdint.h>
#include <string.h>

#define CACHEX_WAITTIME_MAX 64

/* Wait time entry */
typedef struct {
    uint16_t caid;
    uint16_t caid_mask;          /* Mask for wildcard matching */
    uint16_t provid;             /* Optional provider filter */
    uint16_t sid;                /* Optional service ID filter */
    
    uint32_t awtime_ms;          /* Ask wait time */
    uint32_t dwtime_ms;          /* Dynamic wait time */
    int enabled;
} cachex_waittime_entry_t;

/* Wait time table */
typedef struct {
    cachex_waittime_entry_t entries[CACHEX_WAITTIME_MAX];
    int count;
    
    /* Defaults */
    uint32_t default_awtime_ms;
    uint32_t default_dwtime_ms;
} cachex_waittime_table_t;

static cachex_waittime_table_t g_cachex_waittime = {0};

/* Initialize wait time table */
static inline void cachex_waittime_init(void) {
    memset(&g_cachex_waittime, 0, sizeof(g_cachex_waittime));
    
    g_cachex_waittime.default_awtime_ms = 100;   /* 100ms default */
    g_cachex_waittime.default_dwtime_ms = 250;   /* 250ms default */
}

/* Add a wait time entry */
static inline int cachex_waittime_add(uint16_t caid, uint16_t caid_mask,
                                       uint16_t provid, uint16_t sid,
                                       uint32_t awtime_ms, uint32_t dwtime_ms) {
    if (g_cachex_waittime.count >= CACHEX_WAITTIME_MAX) {
        return -1;  /* Table full */
    }
    
    cachex_waittime_entry_t *e = &g_cachex_waittime.entries[g_cachex_waittime.count];
    e->caid = caid;
    e->caid_mask = caid_mask;
    e->provid = provid;
    e->sid = sid;
    e->awtime_ms = awtime_ms;
    e->dwtime_ms = dwtime_ms;
    e->enabled = 1;
    
    g_cachex_waittime.count++;
    return 0;
}

/* Find wait time for a specific CAID/PROVID/SID */
static inline cachex_waittime_entry_t *cachex_waittime_find(uint16_t caid,
                                                               uint32_t provid,
                                                               uint16_t sid) {
    int i;
    cachex_waittime_entry_t *best_match = NULL;
    int best_specificity = -1;
    
    for (i = 0; i < g_cachex_waittime.count; i++) {
        cachex_waittime_entry_t *e = &g_cachex_waittime.entries[i];
        if (!e->enabled) continue;
        
        /* Check CAID match */
        if ((caid & e->caid_mask) != (e->caid & e->caid_mask)) continue;
        
        /* Check optional PROVID match */
        if (e->provid && e->provid != provid) continue;
        
        /* Check optional SID match */
        if (e->sid && e->sid != sid) continue;
        
        /* Calculate specificity */
        int specificity = 0;
        uint16_t mask = e->caid_mask;
        while (mask) {
            specificity += mask & 1;
            mask >>= 1;
        }
        if (e->provid) specificity += 8;
        if (e->sid) specificity += 16;
        
        if (specificity > best_specificity) {
            best_specificity = specificity;
            best_match = e;
        }
    }
    
    return best_match;
}

/* Get ask wait time for a CAID */
static inline uint32_t cachex_waittime_get_awtime(uint16_t caid, uint32_t provid, uint16_t sid) {
    cachex_waittime_entry_t *e = cachex_waittime_find(caid, provid, sid);
    return e ? e->awtime_ms : g_cachex_waittime.default_awtime_ms;
}

/* Get dynamic wait time for a CAID */
static inline uint32_t cachex_waittime_get_dwtime(uint16_t caid, uint32_t provid, uint16_t sid) {
    cachex_waittime_entry_t *e = cachex_waittime_find(caid, provid, sid);
    return e ? e->dwtime_ms : g_cachex_waittime.default_dwtime_ms;
}

/*
 * Load optimized wait times based on OSCam best practices
 * From forums: Digital-Eliteboard, LinuxSat-Support, TechKings
 */
static inline void cachex_waittime_load_defaults(void) {
    /* All CAIDs default */
    cachex_waittime_add(0x0000, 0x0000, 0, 0, 50, 250);
    
    /* SECA (0100) - Slow, needs more time */
    cachex_waittime_add(0x0100, 0xFF00, 0, 0, 500, 1500);
    
    /* VIACCESS (0500) - Medium */
    cachex_waittime_add(0x0500, 0xFF00, 0, 0, 200, 2000);
    
    /* IRDETO (06xx) - Medium */
    cachex_waittime_add(0x0600, 0xFF00, 0, 0, 400, 1500);
    
    /* VIDEOGUARD (09xx) - Fast cycles */
    cachex_waittime_add(0x0900, 0xFF00, 0, 0, 100, 300);
    
    /* Specific VIDEOGUARD */
    cachex_waittime_add(0x098C, 0xFFFF, 0, 0, 150, 225);
    cachex_waittime_add(0x098D, 0xFFFF, 0, 0, 200, 300);
    cachex_waittime_add(0x09C4, 0xFFFF, 0, 0, 75, 200);
    cachex_waittime_add(0x09CD, 0xFFFF, 0, 0, 100, 260);
    cachex_waittime_add(0x0963, 0xFFFF, 0, 0, 100, 260);
    
    /* CONAX (0Bxx) - Medium */
    cachex_waittime_add(0x0B00, 0xFF00, 0, 0, 300, 1500);
    cachex_waittime_add(0x0BAA, 0xFFFF, 0, 0, 300, 700);
    
    /* CRYPTOWORKS (0Dxx) - Slow */
    cachex_waittime_add(0x0D00, 0xFF00, 0, 0, 500, 2000);
    cachex_waittime_add(0x0D95, 0xFFFF, 0, 0, 400, 600);
    cachex_waittime_add(0x0D96, 0xFFFF, 0, 0, 400, 600);
    cachex_waittime_add(0x0D98, 0xFFFF, 0, 0, 400, 600);
    
    /* NAGRA (17xx) - Medium */
    cachex_waittime_add(0x1700, 0xFF00, 0, 0, 500, 1000);
    cachex_waittime_add(0x1702, 0xFFFF, 0, 0, 500, 900);
    cachex_waittime_add(0x1708, 0xFFFF, 0, 0, 500, 1450);
    
    /* NAGRA (18xx) - Medium to slow */
    cachex_waittime_add(0x1800, 0xFF00, 0, 0, 400, 1500);
    cachex_waittime_add(0x1810, 0xFFFF, 0, 0, 400, 800);
    cachex_waittime_add(0x1830, 0xFFFF, 0, 0, 400, 550);
    cachex_waittime_add(0x1841, 0xFFFF, 0, 0, 400, 800);
    cachex_waittime_add(0x1843, 0xFFFF, 0, 0, 500, 550);
    cachex_waittime_add(0x1884, 0xFFFF, 0, 0, 500, 800);
    
    /* DRE-CRYPT (4Axx) - Medium */
    cachex_waittime_add(0x4A00, 0xFF00, 0, 0, 400, 1000);
    cachex_waittime_add(0x4AEE, 0xFFFF, 0, 0, 400, 900);
}

#endif /* CACHEX_WAITTIME_H */
