/* caid-optimizations.h — CAID-Specific Optimizations
 *
 * PURPOSE: Fix critical issues for problematic CAIDs
 * 
 * Priority 1 Fixes:
 * 1. Nagra (1883, 1810, 1811) - Black screen and slow response
 * 2. VideoGuard (09BD) - Half-key problems
 * 3. Enhanced timeout calculation per CAID
 * 4. Improved cache for slow CAIDs
 */

#ifndef CAID_OPTIMIZATIONS_H
#define CAID_OPTIMIZATIONS_H

#include "resource-config.h"
#include <stdint.h>
#include <time.h>

// ============================================================================
// CAID CLASSIFICATION
// ============================================================================

typedef enum {
    CAID_TYPE_NAGRA,        // 18xx - Complex, slow
    CAID_TYPE_VIDEOGUARD,   // 09xx - Medium complexity
    CAID_TYPE_VIACCESS,     // 0500 - Stable
    CAID_TYPE_CONAX,        // 0Bxx - Fast, stable
    CAID_TYPE_CRYPTOWORKS,  // 0Dxx - Medium
    CAID_TYPE_IRDETO,       // 06xx - Medium
    CAID_TYPE_SECA,         // 0100 - Stable
    CAID_TYPE_BETACRYPT,    // 1708 - Stable
    CAID_TYPE_DRECRYPT,     // 4AEE - Very fast
    CAID_TYPE_BISS,         // 2600 - No issues
    CAID_TYPE_TANDERBERG,   // 1010 - Very slow
    CAID_TYPE_UNKNOWN
} caid_type_t;

// ============================================================================
// CAID-SPECIFIC SETTINGS
// ============================================================================

struct caid_settings {
    uint16_t caid;
    caid_type_t type;
    
    // Timeout settings (milliseconds)
    uint32_t dcw_mintime;
    uint32_t dcw_validecmtime;
    uint32_t server_timeout;
    uint32_t cache_timeout;
    
    // Retry settings
    uint8_t dcw_maxfailed;
    uint8_t dcw_retry;
    
    // Feature flags
    uint8_t enable_skipcwc;
    uint8_t enable_cycle_check;
    uint8_t halfnulled_reject;
    uint8_t ecm_check_length;
    
    // Cache priority (0-100)
    uint8_t cache_priority;
    
    // Performance tracking
    uint32_t avg_response_time;
    uint32_t success_rate;  // 0-100
    uint32_t black_screen_count;
};

#define MAX_CAID_SETTINGS 64

struct caid_optimization_table {
    struct caid_settings settings[MAX_CAID_SETTINGS];
    int count;
};

extern struct caid_optimization_table caid_opt_tab;

// ============================================================================
// CAID TYPE DETECTION
// ============================================================================

static inline caid_type_t caid_get_type(uint16_t caid) {
    if (caid >= 0x1800 && caid <= 0x18FF) return CAID_TYPE_NAGRA;
    if (caid >= 0x0900 && caid <= 0x09FF) return CAID_TYPE_VIDEOGUARD;
    if (caid == 0x0500) return CAID_TYPE_VIACCESS;
    if (caid >= 0x0B00 && caid <= 0x0BFF) return CAID_TYPE_CONAX;
    if (caid >= 0x0D00 && caid <= 0x0DFF) return CAID_TYPE_CRYPTOWORKS;
    if (caid >= 0x0600 && caid <= 0x06FF) return CAID_TYPE_IRDETO;
    if (caid == 0x0100) return CAID_TYPE_SECA;
    if (caid == 0x1708) return CAID_TYPE_BETACRYPT;
    if (caid == 0x4AEE) return CAID_TYPE_DRECRYPT;
    if (caid == 0x2600) return CAID_TYPE_BISS;
    if (caid == 0x1010) return CAID_TYPE_TANDERBERG;
    return CAID_TYPE_UNKNOWN;
}

// ============================================================================
// INITIALIZATION - PRESET OPTIMIZATIONS FOR PROBLEMATIC CAIDs
// ============================================================================

static inline void caid_optimizations_init(void) {
    caid_opt_tab.count = 0;
    
    // Priority 1: Critical Nagra CAIDs
    // CAID 1883 - Canal+ France (EXTREMELY SLOW)
    {
        struct caid_settings *s = &caid_opt_tab.settings[caid_opt_tab.count++];
        s->caid = 0x1883;
        s->type = CAID_TYPE_NAGRA;
        s->dcw_mintime = 150;
        s->dcw_validecmtime = 1500;  // Increased from 1000
        s->server_timeout = 1200;     // Increased from 800
        s->cache_timeout = 350;       // Increased from 250
        s->dcw_maxfailed = 3;
        s->dcw_retry = 3;             // Increased from 2
        s->enable_skipcwc = 1;
        s->enable_cycle_check = 1;
        s->halfnulled_reject = 1;
        s->ecm_check_length = 0;
        s->cache_priority = 90;       // High priority
        s->avg_response_time = 2000;
        s->success_rate = 80;
        s->black_screen_count = 0;
    }
    
    // CAID 1810 - MovieStar (SLOW)
    {
        struct caid_settings *s = &caid_opt_tab.settings[caid_opt_tab.count++];
        s->caid = 0x1810;
        s->type = CAID_TYPE_NAGRA;
        s->dcw_mintime = 120;
        s->dcw_validecmtime = 1200;
        s->server_timeout = 1000;
        s->cache_timeout = 300;
        s->dcw_maxfailed = 3;
        s->dcw_retry = 2;
        s->enable_skipcwc = 1;
        s->enable_cycle_check = 1;
        s->halfnulled_reject = 1;
        s->ecm_check_length = 0;
        s->cache_priority = 85;
        s->avg_response_time = 1500;
        s->success_rate = 85;
        s->black_screen_count = 0;
    }
    
    // CAID 1811 - CSAT NG (SLOW AND COMPLEX)
    {
        struct caid_settings *s = &caid_opt_tab.settings[caid_opt_tab.count++];
        s->caid = 0x1811;
        s->type = CAID_TYPE_NAGRA;
        s->dcw_mintime = 120;
        s->dcw_validecmtime = 1300;
        s->server_timeout = 1100;
        s->cache_timeout = 320;
        s->dcw_maxfailed = 3;
        s->dcw_retry = 3;
        s->enable_skipcwc = 1;
        s->enable_cycle_check = 1;
        s->halfnulled_reject = 1;
        s->ecm_check_length = 0;
        s->cache_priority = 85;
        s->avg_response_time = 1600;
        s->success_rate = 82;
        s->black_screen_count = 0;
    }
    
    // CAID 1830 - HD+ (COMPLEX)
    {
        struct caid_settings *s = &caid_opt_tab.settings[caid_opt_tab.count++];
        s->caid = 0x1830;
        s->type = CAID_TYPE_NAGRA;
        s->dcw_mintime = 120;
        s->dcw_validecmtime = 1200;
        s->server_timeout = 1000;
        s->cache_timeout = 300;
        s->dcw_maxfailed = 3;
        s->dcw_retry = 2;
        s->enable_skipcwc = 1;
        s->enable_cycle_check = 1;
        s->halfnulled_reject = 1;
        s->ecm_check_length = 0;
        s->cache_priority = 85;
        s->avg_response_time = 1400;
        s->success_rate = 85;
        s->black_screen_count = 0;
    }
    
    // Priority 2: VideoGuard with Half-Key Issues
    // CAID 09BD - Vivacom (HALF-KEY PROBLEMS)
    {
        struct caid_settings *s = &caid_opt_tab.settings[caid_opt_tab.count++];
        s->caid = 0x09BD;
        s->type = CAID_TYPE_VIDEOGUARD;
        s->dcw_mintime = 100;
        s->dcw_validecmtime = 1000;
        s->server_timeout = 900;
        s->cache_timeout = 280;
        s->dcw_maxfailed = 3;
        s->dcw_retry = 2;
        s->enable_skipcwc = 1;
        s->enable_cycle_check = 1;
        s->halfnulled_reject = 0;  // CRITICAL: Allow half-nulled for 09BD
        s->ecm_check_length = 0;
        s->cache_priority = 80;
        s->avg_response_time = 1000;
        s->success_rate = 85;
        s->black_screen_count = 0;
    }
    
    // CAID 091F - TOTAL TV (SLOW)
    {
        struct caid_settings *s = &caid_opt_tab.settings[caid_opt_tab.count++];
        s->caid = 0x091F;
        s->type = CAID_TYPE_VIDEOGUARD;
        s->dcw_mintime = 120;
        s->dcw_validecmtime = 1200;
        s->server_timeout = 1000;
        s->cache_timeout = 300;
        s->dcw_maxfailed = 3;
        s->dcw_retry = 3;
        s->enable_skipcwc = 1;
        s->enable_cycle_check = 1;
        s->halfnulled_reject = 1;
        s->ecm_check_length = 0;
        s->cache_priority = 80;
        s->avg_response_time = 1400;
        s->success_rate = 85;
        s->black_screen_count = 0;
    }
    
    // Fast and Stable CAIDs (Optimized for Speed)
    // CAID 0B00 - DigitalALB (CONAX - VERY FAST)
    {
        struct caid_settings *s = &caid_opt_tab.settings[caid_opt_tab.count++];
        s->caid = 0x0B00;
        s->type = CAID_TYPE_CONAX;
        s->dcw_mintime = 80;
        s->dcw_validecmtime = 600;
        s->server_timeout = 600;
        s->cache_timeout = 200;
        s->dcw_maxfailed = 2;
        s->dcw_retry = 2;
        s->enable_skipcwc = 1;
        s->enable_cycle_check = 1;
        s->halfnulled_reject = 1;
        s->ecm_check_length = 0;
        s->cache_priority = 70;
        s->avg_response_time = 200;
        s->success_rate = 98;
        s->black_screen_count = 0;
    }
    
    // CAID 092F - DolceTV (VIDEOGUARD - FAST)
    {
        struct caid_settings *s = &caid_opt_tab.settings[caid_opt_tab.count++];
        s->caid = 0x092F;
        s->type = CAID_TYPE_VIDEOGUARD;
        s->dcw_mintime = 80;
        s->dcw_validecmtime = 700;
        s->server_timeout = 700;
        s->cache_timeout = 220;
        s->dcw_maxfailed = 2;
        s->dcw_retry = 2;
        s->enable_skipcwc = 1;
        s->enable_cycle_check = 1;
        s->halfnulled_reject = 1;
        s->ecm_check_length = 0;
        s->cache_priority = 75;
        s->avg_response_time = 400;
        s->success_rate = 95;
        s->black_screen_count = 0;
    }
    
    // CAID 098D - Sky DE (VIDEOGUARD - OPTIMIZED)
    {
        struct caid_settings *s = &caid_opt_tab.settings[caid_opt_tab.count++];
        s->caid = 0x098D;
        s->type = CAID_TYPE_VIDEOGUARD;
        s->dcw_mintime = 100;
        s->dcw_validecmtime = 800;
        s->server_timeout = 800;
        s->cache_timeout = 250;
        s->dcw_maxfailed = 2;
        s->dcw_retry = 2;
        s->enable_skipcwc = 1;
        s->enable_cycle_check = 1;
        s->halfnulled_reject = 1;
        s->ecm_check_length = 0;
        s->cache_priority = 75;
        s->avg_response_time = 500;
        s->success_rate = 92;
        s->black_screen_count = 0;
    }
    
    // CAID 1884 - NC+ 4K (NAGRA - OPTIMIZED FOR SPEED)
    {
        struct caid_settings *s = &caid_opt_tab.settings[caid_opt_tab.count++];
        s->caid = 0x1884;
        s->type = CAID_TYPE_NAGRA;
        s->dcw_mintime = 100;
        s->dcw_validecmtime = 1000;
        s->server_timeout = 800;
        s->cache_timeout = 250;
        s->dcw_maxfailed = 3;
        s->dcw_retry = 2;
        s->enable_skipcwc = 1;
        s->enable_cycle_check = 1;
        s->halfnulled_reject = 1;
        s->ecm_check_length = 0;
        s->cache_priority = 80;
        s->avg_response_time = 800;
        s->success_rate = 90;
        s->black_screen_count = 0;
    }
    
    // Priority 2: Additional Nagra Optimizations
    
    // CAID 1843 - Canal+ Poland (NAGRA - MEDIUM SPEED)
    {
        struct caid_settings *s = &caid_opt_tab.settings[caid_opt_tab.count++];
        s->caid = 0x1843;
        s->type = CAID_TYPE_NAGRA;
        s->dcw_mintime = 100;
        s->dcw_validecmtime = 1100;
        s->server_timeout = 900;
        s->cache_timeout = 280;
        s->dcw_maxfailed = 3;
        s->dcw_retry = 2;
        s->enable_skipcwc = 1;
        s->enable_cycle_check = 1;
        s->halfnulled_reject = 1;
        s->ecm_check_length = 0;
        s->cache_priority = 78;
        s->avg_response_time = 900;
        s->success_rate = 88;
        s->black_screen_count = 0;
    }
    
    // CAID 1801 - Sky Italy (NAGRA - MEDIUM)
    {
        struct caid_settings *s = &caid_opt_tab.settings[caid_opt_tab.count++];
        s->caid = 0x1801;
        s->type = CAID_TYPE_NAGRA;
        s->dcw_mintime = 100;
        s->dcw_validecmtime = 1100;
        s->server_timeout = 900;
        s->cache_timeout = 280;
        s->dcw_maxfailed = 3;
        s->dcw_retry = 2;
        s->enable_skipcwc = 1;
        s->enable_cycle_check = 1;
        s->halfnulled_reject = 1;
        s->ecm_check_length = 0;
        s->cache_priority = 78;
        s->avg_response_time = 900;
        s->success_rate = 88;
        s->black_screen_count = 0;
    }
    
    // CAID 1802 - Kabel BW (NAGRA - MEDIUM)
    {
        struct caid_settings *s = &caid_opt_tab.settings[caid_opt_tab.count++];
        s->caid = 0x1802;
        s->type = CAID_TYPE_NAGRA;
        s->dcw_mintime = 100;
        s->dcw_validecmtime = 1100;
        s->server_timeout = 900;
        s->cache_timeout = 280;
        s->dcw_maxfailed = 3;
        s->dcw_retry = 2;
        s->enable_skipcwc = 1;
        s->enable_cycle_check = 1;
        s->halfnulled_reject = 1;
        s->ecm_check_length = 0;
        s->cache_priority = 78;
        s->avg_response_time = 900;
        s->success_rate = 88;
        s->black_screen_count = 0;
    }
    
    // CAID 1861 - Orange TV (NAGRA - MEDIUM)
    {
        struct caid_settings *s = &caid_opt_tab.settings[caid_opt_tab.count++];
        s->caid = 0x1861;
        s->type = CAID_TYPE_NAGRA;
        s->dcw_mintime = 100;
        s->dcw_validecmtime = 1100;
        s->server_timeout = 900;
        s->cache_timeout = 280;
        s->dcw_maxfailed = 3;
        s->dcw_retry = 2;
        s->enable_skipcwc = 1;
        s->enable_cycle_check = 1;
        s->halfnulled_reject = 1;
        s->ecm_check_length = 0;
        s->cache_priority = 78;
        s->avg_response_time = 900;
        s->success_rate = 88;
        s->black_screen_count = 0;
    }
    
    // CAID 186A - Orange TV (NAGRA - MEDIUM)
    {
        struct caid_settings *s = &caid_opt_tab.settings[caid_opt_tab.count++];
        s->caid = 0x186A;
        s->type = CAID_TYPE_NAGRA;
        s->dcw_mintime = 100;
        s->dcw_validecmtime = 1100;
        s->server_timeout = 900;
        s->cache_timeout = 280;
        s->dcw_maxfailed = 3;
        s->dcw_retry = 2;
        s->enable_skipcwc = 1;
        s->enable_cycle_check = 1;
        s->halfnulled_reject = 1;
        s->ecm_check_length = 0;
        s->cache_priority = 78;
        s->avg_response_time = 900;
        s->success_rate = 88;
        s->black_screen_count = 0;
    }
    
    // Priority 2: Additional VideoGuard Optimizations
    
    // CAID 098C - Sky UK (VIDEOGUARD - OPTIMIZED)
    {
        struct caid_settings *s = &caid_opt_tab.settings[caid_opt_tab.count++];
        s->caid = 0x098C;
        s->type = CAID_TYPE_VIDEOGUARD;
        s->dcw_mintime = 100;
        s->dcw_validecmtime = 800;
        s->server_timeout = 800;
        s->cache_timeout = 250;
        s->dcw_maxfailed = 2;
        s->dcw_retry = 2;
        s->enable_skipcwc = 1;
        s->enable_cycle_check = 1;
        s->halfnulled_reject = 1;
        s->ecm_check_length = 0;
        s->cache_priority = 75;
        s->avg_response_time = 500;
        s->success_rate = 92;
        s->black_screen_count = 0;
    }
    
    // CAID 0963 - Sky UK (VIDEOGUARD - OPTIMIZED)
    {
        struct caid_settings *s = &caid_opt_tab.settings[caid_opt_tab.count++];
        s->caid = 0x0963;
        s->type = CAID_TYPE_VIDEOGUARD;
        s->dcw_mintime = 100;
        s->dcw_validecmtime = 800;
        s->server_timeout = 800;
        s->cache_timeout = 250;
        s->dcw_maxfailed = 2;
        s->dcw_retry = 2;
        s->enable_skipcwc = 1;
        s->enable_cycle_check = 1;
        s->halfnulled_reject = 1;
        s->ecm_check_length = 0;
        s->cache_priority = 75;
        s->avg_response_time = 500;
        s->success_rate = 92;
        s->black_screen_count = 0;
    }
    
    // CAID 093E - Sky Austria (VIDEOGUARD - OPTIMIZED)
    {
        struct caid_settings *s = &caid_opt_tab.settings[caid_opt_tab.count++];
        s->caid = 0x093E;
        s->type = CAID_TYPE_VIDEOGUARD;
        s->dcw_mintime = 100;
        s->dcw_validecmtime = 800;
        s->server_timeout = 800;
        s->cache_timeout = 250;
        s->dcw_maxfailed = 2;
        s->dcw_retry = 2;
        s->enable_skipcwc = 1;
        s->enable_cycle_check = 1;
        s->halfnulled_reject = 1;
        s->ecm_check_length = 0;
        s->cache_priority = 75;
        s->avg_response_time = 500;
        s->success_rate = 92;
        s->black_screen_count = 0;
    }
    
    // CAID 09C4 - Sky Germany (VIDEOGUARD - OPTIMIZED)
    {
        struct caid_settings *s = &caid_opt_tab.settings[caid_opt_tab.count++];
        s->caid = 0x09C4;
        s->type = CAID_TYPE_VIDEOGUARD;
        s->dcw_mintime = 100;
        s->dcw_validecmtime = 800;
        s->server_timeout = 800;
        s->cache_timeout = 250;
        s->dcw_maxfailed = 2;
        s->dcw_retry = 2;
        s->enable_skipcwc = 1;
        s->enable_cycle_check = 1;
        s->halfnulled_reject = 1;
        s->ecm_check_length = 0;
        s->cache_priority = 75;
        s->avg_response_time = 500;
        s->success_rate = 92;
        s->black_screen_count = 0;
    }
    
    // CAID 094C - Nova (VIDEOGUARD - FAST)
    {
        struct caid_settings *s = &caid_opt_tab.settings[caid_opt_tab.count++];
        s->caid = 0x094C;
        s->type = CAID_TYPE_VIDEOGUARD;
        s->dcw_mintime = 90;
        s->dcw_validecmtime = 750;
        s->server_timeout = 750;
        s->cache_timeout = 230;
        s->dcw_maxfailed = 2;
        s->dcw_retry = 2;
        s->enable_skipcwc = 1;
        s->enable_cycle_check = 1;
        s->halfnulled_reject = 1;
        s->ecm_check_length = 0;
        s->cache_priority = 73;
        s->avg_response_time = 450;
        s->success_rate = 93;
        s->black_screen_count = 0;
    }
    
    // Priority 2: CryptoWorks Optimizations
    
    // CAID 0D00 - Skylink (CRYPTOWORKS - MEDIUM)
    {
        struct caid_settings *s = &caid_opt_tab.settings[caid_opt_tab.count++];
        s->caid = 0x0D00;
        s->type = CAID_TYPE_CRYPTOWORKS;
        s->dcw_mintime = 90;
        s->dcw_validecmtime = 750;
        s->server_timeout = 750;
        s->cache_timeout = 230;
        s->dcw_maxfailed = 2;
        s->dcw_retry = 2;
        s->enable_skipcwc = 1;
        s->enable_cycle_check = 1;
        s->halfnulled_reject = 1;
        s->ecm_check_length = 0;
        s->cache_priority = 72;
        s->avg_response_time = 500;
        s->success_rate = 90;
        s->black_screen_count = 0;
    }
    
    // CAID 0D02 - ORF (CRYPTOWORKS - MEDIUM)
    {
        struct caid_settings *s = &caid_opt_tab.settings[caid_opt_tab.count++];
        s->caid = 0x0D02;
        s->type = CAID_TYPE_CRYPTOWORKS;
        s->dcw_mintime = 90;
        s->dcw_validecmtime = 750;
        s->server_timeout = 750;
        s->cache_timeout = 230;
        s->dcw_maxfailed = 2;
        s->dcw_retry = 2;
        s->enable_skipcwc = 1;
        s->enable_cycle_check = 1;
        s->halfnulled_reject = 1;
        s->ecm_check_length = 0;
        s->cache_priority = 72;
        s->avg_response_time = 500;
        s->success_rate = 90;
        s->black_screen_count = 0;
    }
    
    // CAID 0D05 - KabelKiosk (CRYPTOWORKS - MEDIUM)
    {
        struct caid_settings *s = &caid_opt_tab.settings[caid_opt_tab.count++];
        s->caid = 0x0D05;
        s->type = CAID_TYPE_CRYPTOWORKS;
        s->dcw_mintime = 90;
        s->dcw_validecmtime = 750;
        s->server_timeout = 750;
        s->cache_timeout = 230;
        s->dcw_maxfailed = 2;
        s->dcw_retry = 2;
        s->enable_skipcwc = 1;
        s->enable_cycle_check = 1;
        s->halfnulled_reject = 1;
        s->ecm_check_length = 0;
        s->cache_priority = 72;
        s->avg_response_time = 500;
        s->success_rate = 90;
        s->black_screen_count = 0;
    }
    
    // CAID 0D96 - ORF (CRYPTOWORKS - MEDIUM)
    {
        struct caid_settings *s = &caid_opt_tab.settings[caid_opt_tab.count++];
        s->caid = 0x0D96;
        s->type = CAID_TYPE_CRYPTOWORKS;
        s->dcw_mintime = 90;
        s->dcw_validecmtime = 750;
        s->server_timeout = 750;
        s->cache_timeout = 230;
        s->dcw_maxfailed = 2;
        s->dcw_retry = 2;
        s->enable_skipcwc = 1;
        s->enable_cycle_check = 1;
        s->halfnulled_reject = 1;
        s->ecm_check_length = 0;
        s->cache_priority = 72;
        s->avg_response_time = 500;
        s->success_rate = 90;
        s->black_screen_count = 0;
    }
    
    // Priority 2: Irdeto Optimizations
    
    // CAID 0604 - CanalDigitaal (IRDETO - MEDIUM)
    {
        struct caid_settings *s = &caid_opt_tab.settings[caid_opt_tab.count++];
        s->caid = 0x0604;
        s->type = CAID_TYPE_IRDETO;
        s->dcw_mintime = 90;
        s->dcw_validecmtime = 800;
        s->server_timeout = 800;
        s->cache_timeout = 250;
        s->dcw_maxfailed = 2;
        s->dcw_retry = 2;
        s->enable_skipcwc = 1;
        s->enable_cycle_check = 1;
        s->halfnulled_reject = 1;
        s->ecm_check_length = 0;
        s->cache_priority = 72;
        s->avg_response_time = 600;
        s->success_rate = 88;
        s->black_screen_count = 0;
    }
    
    // CAID 0652 - Max TV (IRDETO - MEDIUM)
    {
        struct caid_settings *s = &caid_opt_tab.settings[caid_opt_tab.count++];
        s->caid = 0x0652;
        s->type = CAID_TYPE_IRDETO;
        s->dcw_mintime = 90;
        s->dcw_validecmtime = 800;
        s->server_timeout = 800;
        s->cache_timeout = 250;
        s->dcw_maxfailed = 2;
        s->dcw_retry = 2;
        s->enable_skipcwc = 1;
        s->enable_cycle_check = 1;
        s->halfnulled_reject = 1;
        s->ecm_check_length = 0;
        s->cache_priority = 72;
        s->avg_response_time = 600;
        s->success_rate = 88;
        s->black_screen_count = 0;
    }
    
    // CAID 0664 - Viasat (IRDETO - MEDIUM)
    {
        struct caid_settings *s = &caid_opt_tab.settings[caid_opt_tab.count++];
        s->caid = 0x0664;
        s->type = CAID_TYPE_IRDETO;
        s->dcw_mintime = 90;
        s->dcw_validecmtime = 800;
        s->server_timeout = 800;
        s->cache_timeout = 250;
        s->dcw_maxfailed = 2;
        s->dcw_retry = 2;
        s->enable_skipcwc = 1;
        s->enable_cycle_check = 1;
        s->halfnulled_reject = 1;
        s->ecm_check_length = 0;
        s->cache_priority = 72;
        s->avg_response_time = 600;
        s->success_rate = 88;
        s->black_screen_count = 0;
    }
    
    // CAID 0624 - TV VLAANDEREN (IRDETO - MEDIUM)
    {
        struct caid_settings *s = &caid_opt_tab.settings[caid_opt_tab.count++];
        s->caid = 0x0624;
        s->type = CAID_TYPE_IRDETO;
        s->dcw_mintime = 90;
        s->dcw_validecmtime = 800;
        s->server_timeout = 800;
        s->cache_timeout = 250;
        s->dcw_maxfailed = 2;
        s->dcw_retry = 2;
        s->enable_skipcwc = 1;
        s->enable_cycle_check = 1;
        s->halfnulled_reject = 1;
        s->ecm_check_length = 0;
        s->cache_priority = 72;
        s->avg_response_time = 600;
        s->success_rate = 88;
        s->black_screen_count = 0;
    }
    
    // Priority 3: Complete All Remaining CAIDs
    
    // Additional Nagra (14 CAIDs)
    // CAID 1803 - Sky Deutschland
    {
        struct caid_settings *s = &caid_opt_tab.settings[caid_opt_tab.count++];
        s->caid = 0x1803;
        s->type = CAID_TYPE_NAGRA;
        s->dcw_mintime = 100;
        s->dcw_validecmtime = 1100;
        s->server_timeout = 900;
        s->cache_timeout = 280;
        s->dcw_maxfailed = 3;
        s->dcw_retry = 2;
        s->enable_skipcwc = 1;
        s->enable_cycle_check = 1;
        s->halfnulled_reject = 1;
        s->ecm_check_length = 0;
        s->cache_priority = 78;
        s->avg_response_time = 900;
        s->success_rate = 88;
        s->black_screen_count = 0;
    }
    
    // CAID 1805 - Sky Deutschland
    {
        struct caid_settings *s = &caid_opt_tab.settings[caid_opt_tab.count++];
        s->caid = 0x1805;
        s->type = CAID_TYPE_NAGRA;
        s->dcw_mintime = 100;
        s->dcw_validecmtime = 1100;
        s->server_timeout = 900;
        s->cache_timeout = 280;
        s->dcw_maxfailed = 3;
        s->dcw_retry = 2;
        s->enable_skipcwc = 1;
        s->enable_cycle_check = 1;
        s->halfnulled_reject = 1;
        s->ecm_check_length = 0;
        s->cache_priority = 78;
        s->avg_response_time = 900;
        s->success_rate = 88;
        s->black_screen_count = 0;
    }
    
    // CAID 1813 - Kabel Deutschland
    {
        struct caid_settings *s = &caid_opt_tab.settings[caid_opt_tab.count++];
        s->caid = 0x1813;
        s->type = CAID_TYPE_NAGRA;
        s->dcw_mintime = 100;
        s->dcw_validecmtime = 1100;
        s->server_timeout = 900;
        s->cache_timeout = 280;
        s->dcw_maxfailed = 3;
        s->dcw_retry = 2;
        s->enable_skipcwc = 1;
        s->enable_cycle_check = 1;
        s->halfnulled_reject = 1;
        s->ecm_check_length = 0;
        s->cache_priority = 78;
        s->avg_response_time = 900;
        s->success_rate = 88;
        s->black_screen_count = 0;
    }
    
    // CAID 1814 - Kabel Deutschland
    {
        struct caid_settings *s = &caid_opt_tab.settings[caid_opt_tab.count++];
        s->caid = 0x1814;
        s->type = CAID_TYPE_NAGRA;
        s->dcw_mintime = 100;
        s->dcw_validecmtime = 1100;
        s->server_timeout = 900;
        s->cache_timeout = 280;
        s->dcw_maxfailed = 3;
        s->dcw_retry = 2;
        s->enable_skipcwc = 1;
        s->enable_cycle_check = 1;
        s->halfnulled_reject = 1;
        s->ecm_check_length = 0;
        s->cache_priority = 78;
        s->avg_response_time = 900;
        s->success_rate = 88;
        s->black_screen_count = 0;
    }
    
    // CAID 1815 - Unitymedia
    {
        struct caid_settings *s = &caid_opt_tab.settings[caid_opt_tab.count++];
        s->caid = 0x1815;
        s->type = CAID_TYPE_NAGRA;
        s->dcw_mintime = 100;
        s->dcw_validecmtime = 1100;
        s->server_timeout = 900;
        s->cache_timeout = 280;
        s->dcw_maxfailed = 3;
        s->dcw_retry = 2;
        s->enable_skipcwc = 1;
        s->enable_cycle_check = 1;
        s->halfnulled_reject = 1;
        s->ecm_check_length = 0;
        s->cache_priority = 78;
        s->avg_response_time = 900;
        s->success_rate = 88;
        s->black_screen_count = 0;
    }
    
    // CAID 1817 - Telecable
    {
        struct caid_settings *s = &caid_opt_tab.settings[caid_opt_tab.count++];
        s->caid = 0x1817;
        s->type = CAID_TYPE_NAGRA;
        s->dcw_mintime = 100;
        s->dcw_validecmtime = 1100;
        s->server_timeout = 900;
        s->cache_timeout = 280;
        s->dcw_maxfailed = 3;
        s->dcw_retry = 2;
        s->enable_skipcwc = 1;
        s->enable_cycle_check = 1;
        s->halfnulled_reject = 1;
        s->ecm_check_length = 0;
        s->cache_priority = 78;
        s->avg_response_time = 900;
        s->success_rate = 88;
        s->black_screen_count = 0;
    }
    
    // CAID 1818 - Movistar+
    {
        struct caid_settings *s = &caid_opt_tab.settings[caid_opt_tab.count++];
        s->caid = 0x1818;
        s->type = CAID_TYPE_NAGRA;
        s->dcw_mintime = 100;
        s->dcw_validecmtime = 1100;
        s->server_timeout = 900;
        s->cache_timeout = 280;
        s->dcw_maxfailed = 3;
        s->dcw_retry = 2;
        s->enable_skipcwc = 1;
        s->enable_cycle_check = 1;
        s->halfnulled_reject = 1;
        s->ecm_check_length = 0;
        s->cache_priority = 78;
        s->avg_response_time = 900;
        s->success_rate = 88;
        s->black_screen_count = 0;
    }
    
    // CAID 1819 - Movistar+
    {
        struct caid_settings *s = &caid_opt_tab.settings[caid_opt_tab.count++];
        s->caid = 0x1819;
        s->type = CAID_TYPE_NAGRA;
        s->dcw_mintime = 100;
        s->dcw_validecmtime = 1100;
        s->server_timeout = 900;
        s->cache_timeout = 280;
        s->dcw_maxfailed = 3;
        s->dcw_retry = 2;
        s->enable_skipcwc = 1;
        s->enable_cycle_check = 1;
        s->halfnulled_reject = 1;
        s->ecm_check_length = 0;
        s->cache_priority = 78;
        s->avg_response_time = 900;
        s->success_rate = 88;
        s->black_screen_count = 0;
    }
    
    // CAID 183D - HD+
    {
        struct caid_settings *s = &caid_opt_tab.settings[caid_opt_tab.count++];
        s->caid = 0x183D;
        s->type = CAID_TYPE_NAGRA;
        s->dcw_mintime = 100;
        s->dcw_validecmtime = 1100;
        s->server_timeout = 900;
        s->cache_timeout = 280;
        s->dcw_maxfailed = 3;
        s->dcw_retry = 2;
        s->enable_skipcwc = 1;
        s->enable_cycle_check = 1;
        s->halfnulled_reject = 1;
        s->ecm_check_length = 0;
        s->cache_priority = 78;
        s->avg_response_time = 900;
        s->success_rate = 88;
        s->black_screen_count = 0;
    }
    
    // CAID 183E - HD+
    {
        struct caid_settings *s = &caid_opt_tab.settings[caid_opt_tab.count++];
        s->caid = 0x183E;
        s->type = CAID_TYPE_NAGRA;
        s->dcw_mintime = 100;
        s->dcw_validecmtime = 1100;
        s->server_timeout = 900;
        s->cache_timeout = 280;
        s->dcw_maxfailed = 3;
        s->dcw_retry = 2;
        s->enable_skipcwc = 1;
        s->enable_cycle_check = 1;
        s->halfnulled_reject = 1;
        s->ecm_check_length = 0;
        s->cache_priority = 78;
        s->avg_response_time = 900;
        s->success_rate = 88;
        s->black_screen_count = 0;
    }
    
    // CAID 1840 - Canal+
    {
        struct caid_settings *s = &caid_opt_tab.settings[caid_opt_tab.count++];
        s->caid = 0x1840;
        s->type = CAID_TYPE_NAGRA;
        s->dcw_mintime = 100;
        s->dcw_validecmtime = 1100;
        s->server_timeout = 900;
        s->cache_timeout = 280;
        s->dcw_maxfailed = 3;
        s->dcw_retry = 2;
        s->enable_skipcwc = 1;
        s->enable_cycle_check = 1;
        s->halfnulled_reject = 1;
        s->ecm_check_length = 0;
        s->cache_priority = 78;
        s->avg_response_time = 900;
        s->success_rate = 88;
        s->black_screen_count = 0;
    }
    
    // CAID 1841 - Canal+
    {
        struct caid_settings *s = &caid_opt_tab.settings[caid_opt_tab.count++];
        s->caid = 0x1841;
        s->type = CAID_TYPE_NAGRA;
        s->dcw_mintime = 100;
        s->dcw_validecmtime = 1100;
        s->server_timeout = 900;
        s->cache_timeout = 280;
        s->dcw_maxfailed = 3;
        s->dcw_retry = 2;
        s->enable_skipcwc = 1;
        s->enable_cycle_check = 1;
        s->halfnulled_reject = 1;
        s->ecm_check_length = 0;
        s->cache_priority = 78;
        s->avg_response_time = 900;
        s->success_rate = 88;
        s->black_screen_count = 0;
    }
    
    // CAID 1860 - Numericable
    {
        struct caid_settings *s = &caid_opt_tab.settings[caid_opt_tab.count++];
        s->caid = 0x1860;
        s->type = CAID_TYPE_NAGRA;
        s->dcw_mintime = 100;
        s->dcw_validecmtime = 1100;
        s->server_timeout = 900;
        s->cache_timeout = 280;
        s->dcw_maxfailed = 3;
        s->dcw_retry = 2;
        s->enable_skipcwc = 1;
        s->enable_cycle_check = 1;
        s->halfnulled_reject = 1;
        s->ecm_check_length = 0;
        s->cache_priority = 78;
        s->avg_response_time = 900;
        s->success_rate = 88;
        s->black_screen_count = 0;
    }
    
    // CAID 1880 - Cyfrowy Polsat
    {
        struct caid_settings *s = &caid_opt_tab.settings[caid_opt_tab.count++];
        s->caid = 0x1880;
        s->type = CAID_TYPE_NAGRA;
        s->dcw_mintime = 100;
        s->dcw_validecmtime = 1100;
        s->server_timeout = 900;
        s->cache_timeout = 280;
        s->dcw_maxfailed = 3;
        s->dcw_retry = 2;
        s->enable_skipcwc = 1;
        s->enable_cycle_check = 1;
        s->halfnulled_reject = 1;
        s->ecm_check_length = 0;
        s->cache_priority = 78;
        s->avg_response_time = 900;
        s->success_rate = 88;
        s->black_screen_count = 0;
    }
    
    // Additional VideoGuard (7 CAIDs)
    // CAID 090D - Sky UK
    {
        struct caid_settings *s = &caid_opt_tab.settings[caid_opt_tab.count++];
        s->caid = 0x090D;
        s->type = CAID_TYPE_VIDEOGUARD;
        s->dcw_mintime = 100;
        s->dcw_validecmtime = 800;
        s->server_timeout = 800;
        s->cache_timeout = 250;
        s->dcw_maxfailed = 2;
        s->dcw_retry = 2;
        s->enable_skipcwc = 1;
        s->enable_cycle_check = 1;
        s->halfnulled_reject = 1;
        s->ecm_check_length = 0;
        s->cache_priority = 75;
        s->avg_response_time = 500;
        s->success_rate = 92;
        s->black_screen_count = 0;
    }
    
    // CAID 090F - Sky UK
    {
        struct caid_settings *s = &caid_opt_tab.settings[caid_opt_tab.count++];
        s->caid = 0x090F;
        s->type = CAID_TYPE_VIDEOGUARD;
        s->dcw_mintime = 100;
        s->dcw_validecmtime = 800;
        s->server_timeout = 800;
        s->cache_timeout = 250;
        s->dcw_maxfailed = 2;
        s->dcw_retry = 2;
        s->enable_skipcwc = 1;
        s->enable_cycle_check = 1;
        s->halfnulled_reject = 1;
        s->ecm_check_length = 0;
        s->cache_priority = 75;
        s->avg_response_time = 500;
        s->success_rate = 92;
        s->black_screen_count = 0;
    }
    
    // CAID 092B - Sky UK
    {
        struct caid_settings *s = &caid_opt_tab.settings[caid_opt_tab.count++];
        s->caid = 0x092B;
        s->type = CAID_TYPE_VIDEOGUARD;
        s->dcw_mintime = 100;
        s->dcw_validecmtime = 800;
        s->server_timeout = 800;
        s->cache_timeout = 250;
        s->dcw_maxfailed = 2;
        s->dcw_retry = 2;
        s->enable_skipcwc = 1;
        s->enable_cycle_check = 1;
        s->halfnulled_reject = 1;
        s->ecm_check_length = 0;
        s->cache_priority = 75;
        s->avg_response_time = 500;
        s->success_rate = 92;
        s->black_screen_count = 0;
    }
    
    // CAID 0930 - Sky UK
    {
        struct caid_settings *s = &caid_opt_tab.settings[caid_opt_tab.count++];
        s->caid = 0x0930;
        s->type = CAID_TYPE_VIDEOGUARD;
        s->dcw_mintime = 100;
        s->dcw_validecmtime = 800;
        s->server_timeout = 800;
        s->cache_timeout = 250;
        s->dcw_maxfailed = 2;
        s->dcw_retry = 2;
        s->enable_skipcwc = 1;
        s->enable_cycle_check = 1;
        s->halfnulled_reject = 1;
        s->ecm_check_length = 0;
        s->cache_priority = 75;
        s->avg_response_time = 500;
        s->success_rate = 92;
        s->black_screen_count = 0;
    }
    
    // CAID 0960 - Sky UK
    {
        struct caid_settings *s = &caid_opt_tab.settings[caid_opt_tab.count++];
        s->caid = 0x0960;
        s->type = CAID_TYPE_VIDEOGUARD;
        s->dcw_mintime = 100;
        s->dcw_validecmtime = 800;
        s->server_timeout = 800;
        s->cache_timeout = 250;
        s->dcw_maxfailed = 2;
        s->dcw_retry = 2;
        s->enable_skipcwc = 1;
        s->enable_cycle_check = 1;
        s->halfnulled_reject = 1;
        s->ecm_check_length = 0;
        s->cache_priority = 75;
        s->avg_response_time = 500;
        s->success_rate = 92;
        s->black_screen_count = 0;
    }
    
    // CAID 098E - Sky UK
    {
        struct caid_settings *s = &caid_opt_tab.settings[caid_opt_tab.count++];
        s->caid = 0x098E;
        s->type = CAID_TYPE_VIDEOGUARD;
        s->dcw_mintime = 100;
        s->dcw_validecmtime = 800;
        s->server_timeout = 800;
        s->cache_timeout = 250;
        s->dcw_maxfailed = 2;
        s->dcw_retry = 2;
        s->enable_skipcwc = 1;
        s->enable_cycle_check = 1;
        s->halfnulled_reject = 1;
        s->ecm_check_length = 0;
        s->cache_priority = 75;
        s->avg_response_time = 500;
        s->success_rate = 92;
        s->black_screen_count = 0;
    }
    
    // CAID 09BE - Vivacom
    {
        struct caid_settings *s = &caid_opt_tab.settings[caid_opt_tab.count++];
        s->caid = 0x09BE;
        s->type = CAID_TYPE_VIDEOGUARD;
        s->dcw_mintime = 100;
        s->dcw_validecmtime = 1000;
        s->server_timeout = 900;
        s->cache_timeout = 280;
        s->dcw_maxfailed = 3;
        s->dcw_retry = 2;
        s->enable_skipcwc = 1;
        s->enable_cycle_check = 1;
        s->halfnulled_reject = 0;
        s->ecm_check_length = 0;
        s->cache_priority = 80;
        s->avg_response_time = 1000;
        s->success_rate = 85;
        s->black_screen_count = 0;
    }
    
    // Additional Conax (4 CAIDs)
    // CAID 0B01 - DigitalALB
    {
        struct caid_settings *s = &caid_opt_tab.settings[caid_opt_tab.count++];
        s->caid = 0x0B01;
        s->type = CAID_TYPE_CONAX;
        s->dcw_mintime = 80;
        s->dcw_validecmtime = 600;
        s->server_timeout = 600;
        s->cache_timeout = 200;
        s->dcw_maxfailed = 2;
        s->dcw_retry = 2;
        s->enable_skipcwc = 1;
        s->enable_cycle_check = 1;
        s->halfnulled_reject = 1;
        s->ecm_check_length = 0;
        s->cache_priority = 70;
        s->avg_response_time = 200;
        s->success_rate = 98;
        s->black_screen_count = 0;
    }
    
    // CAID 0B02 - DigitalALB
    {
        struct caid_settings *s = &caid_opt_tab.settings[caid_opt_tab.count++];
        s->caid = 0x0B02;
        s->type = CAID_TYPE_CONAX;
        s->dcw_mintime = 80;
        s->dcw_validecmtime = 600;
        s->server_timeout = 600;
        s->cache_timeout = 200;
        s->dcw_maxfailed = 2;
        s->dcw_retry = 2;
        s->enable_skipcwc = 1;
        s->enable_cycle_check = 1;
        s->halfnulled_reject = 1;
        s->ecm_check_length = 0;
        s->cache_priority = 70;
        s->avg_response_time = 200;
        s->success_rate = 98;
        s->black_screen_count = 0;
    }
    
    // CAID 0B0F - DigitalALB
    {
        struct caid_settings *s = &caid_opt_tab.settings[caid_opt_tab.count++];
        s->caid = 0x0B0F;
        s->type = CAID_TYPE_CONAX;
        s->dcw_mintime = 80;
        s->dcw_validecmtime = 600;
        s->server_timeout = 600;
        s->cache_timeout = 200;
        s->dcw_maxfailed = 2;
        s->dcw_retry = 2;
        s->enable_skipcwc = 1;
        s->enable_cycle_check = 1;
        s->halfnulled_reject = 1;
        s->ecm_check_length = 0;
        s->cache_priority = 70;
        s->avg_response_time = 200;
        s->success_rate = 98;
        s->black_screen_count = 0;
    }
    
    // CAID 0BAA - DigitalALB
    {
        struct caid_settings *s = &caid_opt_tab.settings[caid_opt_tab.count++];
        s->caid = 0x0BAA;
        s->type = CAID_TYPE_CONAX;
        s->dcw_mintime = 80;
        s->dcw_validecmtime = 600;
        s->server_timeout = 600;
        s->cache_timeout = 200;
        s->dcw_maxfailed = 2;
        s->dcw_retry = 2;
        s->enable_skipcwc = 1;
        s->enable_cycle_check = 1;
        s->halfnulled_reject = 1;
        s->ecm_check_length = 0;
        s->cache_priority = 70;
        s->avg_response_time = 200;
        s->success_rate = 98;
        s->black_screen_count = 0;
    }
    
    // Additional CryptoWorks (3 CAIDs)
    // CAID 0D95 - ORF
    {
        struct caid_settings *s = &caid_opt_tab.settings[caid_opt_tab.count++];
        s->caid = 0x0D95;
        s->type = CAID_TYPE_CRYPTOWORKS;
        s->dcw_mintime = 90;
        s->dcw_validecmtime = 750;
        s->server_timeout = 750;
        s->cache_timeout = 230;
        s->dcw_maxfailed = 2;
        s->dcw_retry = 2;
        s->enable_skipcwc = 1;
        s->enable_cycle_check = 1;
        s->halfnulled_reject = 1;
        s->ecm_check_length = 0;
        s->cache_priority = 72;
        s->avg_response_time = 500;
        s->success_rate = 90;
        s->black_screen_count = 0;
    }
    
    // CAID 0D97 - ORF
    {
        struct caid_settings *s = &caid_opt_tab.settings[caid_opt_tab.count++];
        s->caid = 0x0D97;
        s->type = CAID_TYPE_CRYPTOWORKS;
        s->dcw_mintime = 90;
        s->dcw_validecmtime = 750;
        s->server_timeout = 750;
        s->cache_timeout = 230;
        s->dcw_maxfailed = 2;
        s->dcw_retry = 2;
        s->enable_skipcwc = 1;
        s->enable_cycle_check = 1;
        s->halfnulled_reject = 1;
        s->ecm_check_length = 0;
        s->cache_priority = 72;
        s->avg_response_time = 500;
        s->success_rate = 90;
        s->black_screen_count = 0;
    }
    
    // CAID 0D98 - ORF
    {
        struct caid_settings *s = &caid_opt_tab.settings[caid_opt_tab.count++];
        s->caid = 0x0D98;
        s->type = CAID_TYPE_CRYPTOWORKS;
        s->dcw_mintime = 90;
        s->dcw_validecmtime = 750;
        s->server_timeout = 750;
        s->cache_timeout = 230;
        s->dcw_maxfailed = 2;
        s->dcw_retry = 2;
        s->enable_skipcwc = 1;
        s->enable_cycle_check = 1;
        s->halfnulled_reject = 1;
        s->ecm_check_length = 0;
        s->cache_priority = 72;
        s->avg_response_time = 500;
        s->success_rate = 90;
        s->black_screen_count = 0;
    }
    
    // Additional Irdeto (3 CAIDs)
    // CAID 0606 - CanalDigitaal
    {
        struct caid_settings *s = &caid_opt_tab.settings[caid_opt_tab.count++];
        s->caid = 0x0606;
        s->type = CAID_TYPE_IRDETO;
        s->dcw_mintime = 90;
        s->dcw_validecmtime = 800;
        s->server_timeout = 800;
        s->cache_timeout = 250;
        s->dcw_maxfailed = 2;
        s->dcw_retry = 2;
        s->enable_skipcwc = 1;
        s->enable_cycle_check = 1;
        s->halfnulled_reject = 1;
        s->ecm_check_length = 0;
        s->cache_priority = 72;
        s->avg_response_time = 600;
        s->success_rate = 88;
        s->black_screen_count = 0;
    }
    
    // CAID 0609 - CanalDigitaal
    {
        struct caid_settings *s = &caid_opt_tab.settings[caid_opt_tab.count++];
        s->caid = 0x0609;
        s->type = CAID_TYPE_IRDETO;
        s->dcw_mintime = 90;
        s->dcw_validecmtime = 800;
        s->server_timeout = 800;
        s->cache_timeout = 250;
        s->dcw_maxfailed = 2;
        s->dcw_retry = 2;
        s->enable_skipcwc = 1;
        s->enable_cycle_check = 1;
        s->halfnulled_reject = 1;
        s->ecm_check_length = 0;
        s->cache_priority = 72;
        s->avg_response_time = 600;
        s->success_rate = 88;
        s->black_screen_count = 0;
    }
    
    // CAID 0629 - TV VLAANDEREN
    {
        struct caid_settings *s = &caid_opt_tab.settings[caid_opt_tab.count++];
        s->caid = 0x0629;
        s->type = CAID_TYPE_IRDETO;
        s->dcw_mintime = 90;
        s->dcw_validecmtime = 800;
        s->server_timeout = 800;
        s->cache_timeout = 250;
        s->dcw_maxfailed = 2;
        s->dcw_retry = 2;
        s->enable_skipcwc = 1;
        s->enable_cycle_check = 1;
        s->halfnulled_reject = 1;
        s->ecm_check_length = 0;
        s->cache_priority = 72;
        s->avg_response_time = 600;
        s->success_rate = 88;
        s->black_screen_count = 0;
    }
    
    // Viaccess (1 CAID)
    // CAID 0500 - Multiple providers (stable)
    {
        struct caid_settings *s = &caid_opt_tab.settings[caid_opt_tab.count++];
        s->caid = 0x0500;
        s->type = CAID_TYPE_VIACCESS;
        s->dcw_mintime = 90;
        s->dcw_validecmtime = 800;
        s->server_timeout = 800;
        s->cache_timeout = 250;
        s->dcw_maxfailed = 2;
        s->dcw_retry = 2;
        s->enable_skipcwc = 1;
        s->enable_cycle_check = 1;
        s->halfnulled_reject = 1;
        s->ecm_check_length = 0;
        s->cache_priority = 75;
        s->avg_response_time = 500;
        s->success_rate = 92;
        s->black_screen_count = 0;
    }
    
    // Tanderberg (1 CAID)
    // CAID 1010 - Very slow
    {
        struct caid_settings *s = &caid_opt_tab.settings[caid_opt_tab.count++];
        s->caid = 0x1010;
        s->type = CAID_TYPE_TANDERBERG;
        s->dcw_mintime = 120;
        s->dcw_validecmtime = 1300;
        s->server_timeout = 1100;
        s->cache_timeout = 320;
        s->dcw_maxfailed = 3;
        s->dcw_retry = 3;
        s->enable_skipcwc = 1;
        s->enable_cycle_check = 1;
        s->halfnulled_reject = 1;
        s->ecm_check_length = 0;
        s->cache_priority = 85;
        s->avg_response_time = 1200;
        s->success_rate = 82;
        s->black_screen_count = 0;
    }
}

// ============================================================================
// CAID-SPECIFIC SETTINGS LOOKUP
// ============================================================================

static inline struct caid_settings* caid_get_settings(uint16_t caid) {
    int i;
    for (i = 0; i < caid_opt_tab.count; i++) {
        if (caid_opt_tab.settings[i].caid == caid) {
            return &caid_opt_tab.settings[i];
        }
    }
    return NULL;
}

// ============================================================================
// ADAPTIVE TIMEOUT CALCULATION
// ============================================================================

static inline uint32_t caid_get_timeout(uint16_t caid) {
    struct caid_settings *s = caid_get_settings(caid);
    if (s) {
        return s->server_timeout;
    }
    
    // Default timeout based on CAID type
    caid_type_t type = caid_get_type(caid);
    switch (type) {
        case CAID_TYPE_NAGRA:
        case CAID_TYPE_TANDERBERG:
            return 1000;  // Slow types
        case CAID_TYPE_VIDEOGUARD:
        case CAID_TYPE_CRYPTOWORKS:
        case CAID_TYPE_IRDETO:
            return 800;   // Medium types
        case CAID_TYPE_CONAX:
        case CAID_TYPE_DRECRYPT:
        case CAID_TYPE_BISS:
            return 600;   // Fast types
        default:
            return 800;   // Default
    }
}

static inline uint32_t caid_get_validecmtime(uint16_t caid) {
    struct caid_settings *s = caid_get_settings(caid);
    if (s) {
        return s->dcw_validecmtime;
    }
    
    caid_type_t type = caid_get_type(caid);
    switch (type) {
        case CAID_TYPE_NAGRA:
        case CAID_TYPE_TANDERBERG:
            return 1200;
        case CAID_TYPE_VIDEOGUARD:
        case CAID_TYPE_CRYPTOWORKS:
        case CAID_TYPE_IRDETO:
            return 900;
        case CAID_TYPE_CONAX:
        case CAID_TYPE_DRECRYPT:
        case CAID_TYPE_BISS:
            return 600;
        default:
            return 900;
    }
}

static inline uint32_t caid_get_cache_timeout(uint16_t caid) {
    struct caid_settings *s = caid_get_settings(caid);
    if (s) {
        return s->cache_timeout;
    }
    
    caid_type_t type = caid_get_type(caid);
    switch (type) {
        case CAID_TYPE_NAGRA:
        case CAID_TYPE_TANDERBERG:
            return 300;
        case CAID_TYPE_VIDEOGUARD:
        case CAID_TYPE_CRYPTOWORKS:
        case CAID_TYPE_IRDETO:
            return 250;
        case CAID_TYPE_CONAX:
        case CAID_TYPE_DRECRYPT:
        case CAID_TYPE_BISS:
            return 200;
        default:
            return 250;
    }
}

// ============================================================================
// PERFORMANCE TRACKING
// ============================================================================

static inline void caid_update_stats(uint16_t caid, uint32_t response_time, int success) {
    struct caid_settings *s = caid_get_settings(caid);
    if (!s) return;
    
    // Update average response time (exponential moving average)
    if (s->avg_response_time == 0) {
        s->avg_response_time = response_time;
    } else {
        s->avg_response_time = (s->avg_response_time * 7 + response_time * 3) / 10;
    }
    
    // Update success rate
    if (success) {
        s->success_rate = (s->success_rate * 99 + 100) / 100;
    } else {
        s->success_rate = (s->success_rate * 99 + 0) / 100;
        s->black_screen_count++;
    }
}

// ============================================================================
// BLACK SCREEN DETECTION AND PREVENTION
// ============================================================================

static inline int caid_should_use_relaxed_validation(uint16_t caid) {
    struct caid_settings *s = caid_get_settings(caid);
    if (!s) return 0;
    
    // Use relaxed validation for problematic CAIDs
    if (s->caid == 0x09BD) return 1;  // Vivacom half-key
    if (s->black_screen_count > 10) return 1;  // Too many black screens
    
    return 0;
}

static inline uint8_t caid_get_cache_priority(uint16_t caid) {
    struct caid_settings *s = caid_get_settings(caid);
    if (s) {
        return s->cache_priority;
    }
    return 50;  // Default priority
}

#endif /* CAID_OPTIMIZATIONS_H */
