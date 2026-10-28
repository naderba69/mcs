/* ecm-provider-config.h — Provider-Level ECM Configuration
 * 
 * Based on external research from 21+ specialized forums:
 * - LinuxSat-Support, Sat-Universe, Digital-Eliteboard
 * - OpenPLi, Streamboard Wiki, Elektroda
 * - TechKings, Sat-Forum (OSCam Changesets)
 * - LonasDigital, CCCam2.net, Golden-Forum
 *
 * PURPOSE: Implement all provider-specific fixes discovered:
 *   1. disablecrccws_only_for per CAID:Provider
 *   2. ecmwhitelist per CAID
 *   3. blockemm settings
 *   4. Force Even SA for 1884 (NC+ 4K)
 *   5. CAK7 Parameters for 1811 (CSAT NG)
 *   6. VideoGuard pairing support (09BD, 091F)
 *   7. fallbacktimeout_percaid
 *   8. cacheex_lg_only_tab
 *   9. cw_cache_settings per CAID
 *  10. BISS date-coded keys
 *  11. ICAM mode settings (098D)
 */

#ifndef ECM_PROVIDER_CONFIG_H
#define ECM_PROVIDER_CONFIG_H

#include <stdint.h>
#include <string.h>

// ============================================================================
// STRUCTURE: Provider-level ECM configuration
// ============================================================================

struct provider_ecm_config {
    uint16_t caid;
    uint32_t provid;
    
    // Flags from disablecrccws_only_for
    uint8_t disable_crc_check;      // Skip CRC on CW
    
    // ECM filtering
    uint8_t ecm_whitelist[16];      // Allowed ECM lengths
    uint8_t ecm_whitelist_count;
    uint8_t ecm_header_whitelist[32]; // Allowed ECM headers
    uint8_t ecm_header_count;
    
    // EMM blocking
    uint8_t block_emm_g;            // Block global EMM
    uint8_t block_emm_unknown;      // Block unknown EMM
    uint8_t block_emm_s;            // Block shared EMM
    uint8_t block_emm_u;            // Block unique EMM
    
    // Special flags
    uint8_t force_even_sa;          // 1884/1856: Force Even SA for EMM
    uint8_t accept_null_caid;       // Accept NULL CAID
    uint8_t detect_seca_nagra_tunnel; // Seca/Nagra tunnel detection (0, 1, 2)
    
    // CAK7 parameters for 1811 (CSAT NG)
    uint8_t cak7_nuid[8];           // NUID (8 bytes)
    uint8_t cak7_cwekey[16];        // CW Encryption Key (16 bytes)
    uint8_t cak7_mod1[16];          // Module 1
    uint8_t cak7_mod2[16];          // Module 2
    uint8_t cak7_key3588[16];       // Key 3588
    uint8_t cak7_key3460[16];       // Key 3460
    uint8_t cak7_key3310[16];       // Key 3310
    uint8_t cak7_data50[16];        // Data 50
    uint8_t cak7_mod50[16];         // Module 50
    uint8_t cak7_idird[8];          // IRD Identity
    uint32_t cak7_cmd0eprov;        // CMD 0E Provider
    
    // VideoGuard pairing (09BD, 091F, etc.)
    uint32_t vg_boxid;              // Box ID for pairing
    uint8_t vg_k1_key[16];          // K1 pairing key (16 bytes)
    uint8_t vg_hw_key[16];          // Hardware key (16 bytes)
    uint8_t vg_fix07;               // Fix07 flag
    uint8_t vg_paired;              // Is card paired?
    
    // Clock settings
    uint16_t mhz;                   // Reader clock (e.g., 368, 450)
    uint16_t cardmhz;               // Card native clock
    
    // Fallback timeout (per CAID)
    uint32_t fallback_timeout;      // Fallback timeout in ms
    
    // Cache settings
    uint32_t cw_cache_time;         // CW cache duration (ms)
    uint8_t cw_cache_mode;          // Cache mode (0, 1, 2)
    
    // ICAM mode (098D Sky DE)
    uint8_t icam_enabled;           // ICAM mode enabled
    uint8_t streamrelay_enabled;    // Stream relay enabled
    
    // CW cycle settings
    uint8_t cwcycle_enabled;        // CW cycle check enabled
    uint16_t cwcycle_maxlist;       // Max cycle list size
    uint8_t cwcycle_usecwcfromce;   // Use CWC from cache exchange
    
    // cacheex_lg_only
    uint8_t cacheex_lg_only;        // Long-hop cache only
    uint32_t cacheex_lg_provid;     // Provider for LG-only
};

#define MAX_PROVIDER_CONFIGS 128

struct provider_config_table {
    struct provider_ecm_config configs[MAX_PROVIDER_CONFIGS];
    int count;
};

extern struct provider_config_table provider_cfg_tab;

// ============================================================================
// LOOKUP FUNCTION
// ============================================================================

static inline struct provider_ecm_config* provider_get_config(uint16_t caid, uint32_t provid) {
    int i;
    struct provider_ecm_config *best = NULL;
    
    for (i = 0; i < provider_cfg_tab.count; i++) {
        if (provider_cfg_tab.configs[i].caid == caid) {
            // Exact provider match
            if (provider_cfg_tab.configs[i].provid == provid) {
                return &provider_cfg_tab.configs[i];
            }
            // Wildcard match (provid = 0)
            if (provider_cfg_tab.configs[i].provid == 0 && !best) {
                best = &provider_cfg_tab.configs[i];
            }
        }
    }
    return best;
}

// ============================================================================
// INITIALIZATION - All provider configs from external research
// ============================================================================

static inline void provider_config_init(void) {
    provider_cfg_tab.count = 0;
    
    // Helper macro for cleaner initialization
    #define ADD_CONFIG(caid_val, provid_val) \
        if (provider_cfg_tab.count < MAX_PROVIDER_CONFIGS) { \
            struct provider_ecm_config *c = &provider_cfg_tab.configs[provider_cfg_tab.count++]; \
            memset(c, 0, sizeof(*c)); \
            c->caid = caid_val; \
            c->provid = provid_val;
    #define END_CONFIG }
    
    // ======================================================================
    // NAGRA 1883 - Canal+ France
    // Source: LinuxSat-Support #149667, LonasDigital
    // Problem: Black screen even with valid ECM
    // Solution: disablecrccws for all 5 providers
    // ======================================================================
    
    ADD_CONFIG(0x1883, 0x003311)
        c->disable_crc_check = 1;
        c->cw_cache_time = 2500;
        c->cw_cache_mode = 2;
        c->cwcycle_enabled = 1;
        c->fallback_timeout = 1500;
    END_CONFIG
    
    ADD_CONFIG(0x1883, 0x003315)
        c->disable_crc_check = 1;
        c->cw_cache_time = 2500;
        c->cw_cache_mode = 2;
    END_CONFIG
    
    ADD_CONFIG(0x1883, 0x003317)
        c->disable_crc_check = 1;
        c->cw_cache_time = 2500;
        c->cw_cache_mode = 2;
    END_CONFIG
    
    ADD_CONFIG(0x1883, 0x003342)
        c->disable_crc_check = 1;
        c->cw_cache_time = 2500;
        c->cw_cache_mode = 2;
    END_CONFIG
    
    ADD_CONFIG(0x1883, 0x003343)
        c->disable_crc_check = 1;
        c->cw_cache_time = 2500;
        c->cw_cache_mode = 2;
    END_CONFIG
    
    // ======================================================================
    // NAGRA 1810 - Movistar+ / MovieStar
    // Source: LinuxSat-Support #151206
    // Problem: Provider attacks via EMM-SHARED + CW fakes
    // Solution: Block EMM, enable CW cycle check, disable CRC
    // ======================================================================
    
    ADD_CONFIG(0x1810, 0x000000)
        c->disable_crc_check = 1;
        c->block_emm_g = 1;
        c->block_emm_unknown = 1;
        c->cw_cache_time = 550;
        c->cw_cache_mode = 2;
        c->cwcycle_enabled = 1;
        c->cwcycle_maxlist = 4000;
        c->cwcycle_usecwcfromce = 1;
        c->fallback_timeout = 1300;
        c->mhz = 450;
        c->cardmhz = 2700;
    END_CONFIG
    
    ADD_CONFIG(0x1810, 0x004101)
        c->disable_crc_check = 1;
        c->block_emm_g = 1;
    END_CONFIG
    
    ADD_CONFIG(0x1810, 0x004001)
        c->disable_crc_check = 1;
        c->block_emm_g = 1;
    END_CONFIG
    
    // ======================================================================
    // NAGRA 1811 - CanalSat NG / CSAT
    // Source: LinuxSat-Support #151415, Elektroda #3784730
    // Problem: CAK7 cards need special parameters
    // Solution: NUID, CWEKEY, MOD1, MOD2, etc.
    // ======================================================================
    
    ADD_CONFIG(0x1811, 0x003311)
        c->disable_crc_check = 1;
        c->detect_seca_nagra_tunnel = 2;
        c->mhz = 368;
        c->cardmhz = 368;
        c->cw_cache_time = 2000;
        c->cw_cache_mode = 2;
        c->cwcycle_enabled = 1;
        c->fallback_timeout = 1200;
        c->block_emm_unknown = 1;
        /* CAK7 parameters - must be set per card */
        /* c->cak7_nuid = ... */
        /* c->cak7_cwekey = ... */
        /* c->cak7_cmd0eprov = 0x3341; */
    END_CONFIG
    
    ADD_CONFIG(0x1811, 0x003315)
        c->disable_crc_check = 1;
    END_CONFIG
    
    ADD_CONFIG(0x1811, 0x023311)
        c->disable_crc_check = 1;
    END_CONFIG
    
    ADD_CONFIG(0x1811, 0x003341)
        c->disable_crc_check = 1;
    END_CONFIG
    
    ADD_CONFIG(0x1811, 0x00331B)
        c->disable_crc_check = 1;
    END_CONFIG
    
    ADD_CONFIG(0x1811, 0x000007)
        c->disable_crc_check = 1;
    END_CONFIG
    
    ADD_CONFIG(0x1811, 0x000107)
        c->disable_crc_check = 1;
    END_CONFIG
    
    // ======================================================================
    // NAGRA 1830 - HD+ Germany
    // Source: LinuxSat-Support #142794, TechKings #147943
    // ======================================================================
    
    ADD_CONFIG(0x1830, 0x000000)
        c->disable_crc_check = 1;
        c->cw_cache_time = 1000;
        c->cw_cache_mode = 2;
        c->cwcycle_enabled = 1;
        c->cacheex_lg_only = 1;
        c->cacheex_lg_provid = 0xFFFFFE;
        c->fallback_timeout = 1000;
    END_CONFIG
    
    // ======================================================================
    // NAGRA 1884 - NC+ Poland (4K)
    // Source: LinuxSat-Support #156048, Sat-Forum Changeset 11855
    // CRITICAL: Force Even SA required for EMM to work
    // ======================================================================
    
    ADD_CONFIG(0x1884, 0x000000)
        c->disable_crc_check = 1;
        c->force_even_sa = 1;  /* CRITICAL: Changeset 11855 */
        c->cw_cache_time = 2000;
        c->cw_cache_mode = 2;
        c->cwcycle_enabled = 1;
        c->fallback_timeout = 1000;
    END_CONFIG
    
    ADD_CONFIG(0x1884, 0x000068)
        c->disable_crc_check = 1;
        c->force_even_sa = 1;
    END_CONFIG
    
    // ======================================================================
    // NAGRA 1841 - Virgin Media Cable
    // Source: LinuxSat-Support #142794
    // ======================================================================
    
    ADD_CONFIG(0x1841, 0x000000)
        c->disable_crc_check = 1;
        c->cw_cache_time = 2500;
        c->cw_cache_mode = 2;
        c->fallback_timeout = 1000;
    END_CONFIG
    
    // ======================================================================
    // NAGRA 1843 - Canal+ Poland
    // Source: Multiple configs
    // ======================================================================
    
    ADD_CONFIG(0x1843, 0x000000)
        c->disable_crc_check = 1;
        c->cw_cache_time = 2500;
        c->cw_cache_mode = 2;
        c->cwcycle_enabled = 1;
        c->cacheex_lg_only = 1;
        c->cacheex_lg_provid = 0xFFFFFE;
    END_CONFIG
    
    // ======================================================================
    // NAGRA 1819 - Movistar+
    // ======================================================================
    
    ADD_CONFIG(0x1819, 0x000000)
        c->disable_crc_check = 1;
        c->fallback_timeout = 1000;
    END_CONFIG
    
    ADD_CONFIG(0x1819, 0x00006D)
        c->disable_crc_check = 1;
    END_CONFIG
    
    ADD_CONFIG(0x1819, 0x000007)
        c->disable_crc_check = 1;
    END_CONFIG
    
    // ======================================================================
    // NAGRA 1813 - Kabel Deutschland
    // ======================================================================
    
    ADD_CONFIG(0x1813, 0x000068)
        c->disable_crc_check = 1;
    END_CONFIG
    
    // ======================================================================
    // NAGRA 1817 - Telecable
    // ======================================================================
    
    ADD_CONFIG(0x1817, 0x000000)
        c->disable_crc_check = 1;
    END_CONFIG
    
    ADD_CONFIG(0x1817, 0x00006A)
        c->disable_crc_check = 1;
    END_CONFIG
    
    // ======================================================================
    // NAGRA 1818 - Movistar+
    // ======================================================================
    
    ADD_CONFIG(0x1818, 0x000000)
        c->disable_crc_check = 1;
    END_CONFIG
    
    ADD_CONFIG(0x1818, 0x00006C)
        c->disable_crc_check = 1;
    END_CONFIG
    
    // ======================================================================
    // NAGRA 1863 - Numericable
    // ======================================================================
    
    ADD_CONFIG(0x1863, 0x003342)
        c->disable_crc_check = 1;
    END_CONFIG
    
    ADD_CONFIG(0x1863, 0x003343)
        c->disable_crc_check = 1;
    END_CONFIG
    
    // ======================================================================
    // NAGRA 1850 - Misc
    // ======================================================================
    
    ADD_CONFIG(0x1850, 0x000000)
        c->disable_crc_check = 1;
    END_CONFIG
    
    // ======================================================================
    // NAGRA 1860/1861/186A - Numericable / Orange TV
    // ======================================================================
    
    ADD_CONFIG(0x1860, 0x000000)
        c->cacheex_lg_only = 1;
        c->cacheex_lg_provid = 0xFFFFFE;
    END_CONFIG
    
    ADD_CONFIG(0x186A, 0x000000)
        c->cacheex_lg_only = 1;
        c->cacheex_lg_provid = 0xFFFFFE;
    END_CONFIG
    
    // ======================================================================
    // NAGRA 183D/183E - HD+
    // ======================================================================
    
    ADD_CONFIG(0x183D, 0x000000)
        c->cw_cache_time = 1000;
        c->cw_cache_mode = 2;
        c->cwcycle_enabled = 1;
    END_CONFIG
    
    ADD_CONFIG(0x183E, 0x000000)
        c->cw_cache_time = 1000;
        c->cw_cache_mode = 2;
        c->cwcycle_enabled = 1;
    END_CONFIG
    
    // ======================================================================
    // VIDEOGUARD 09BD - Vivacom Bulgaria
    // Source: Sat-Universe #311032, Digital-Eliteboard
    // CRITICAL: Hardware pairing required, k1 key needed
    // Also uses Irdeto 06E1 (added 2020)
    // ======================================================================
    
    ADD_CONFIG(0x09BD, 0x000000)
        c->mhz = 450;
        c->cardmhz = 3150;
        c->vg_fix07 = 0;
        c->cw_cache_time = 1000;
        c->cw_cache_mode = 2;
        c->fallback_timeout = 1200;
        c->block_emm_unknown = 1;
        /* Pairing keys must be set per card:
         * c->vg_boxid = 0x04CXXXXX;
         * c->vg_k1_key = { ... };
         * c->vg_hw_key = { ... };
         */
    END_CONFIG
    
    // ======================================================================
    // VIDEOGUARD 091F - Total TV / AirTel
    // Source: LinuxSat-Support #143022
    // Problem: CW is crypted, pairing active
    // ======================================================================
    
    ADD_CONFIG(0x091F, 0x000000)
        c->disable_crc_check = 1;
        c->cw_cache_time = 800;
        c->cw_cache_mode = 2;
        c->fallback_timeout = 1000;
    END_CONFIG
    
    // ======================================================================
    // VIDEOGUARD 098D - Sky DE (ICAM)
    // Source: CCCam2.net, LonasDigital #80763
    // Solution: ICAM mode + streamrelay
    // ======================================================================
    
    ADD_CONFIG(0x098D, 0x000000)
        c->disable_crc_check = 1;
        c->icam_enabled = 1;
        c->streamrelay_enabled = 1;
        c->cw_cache_time = 450;
        c->cw_cache_mode = 2;
        c->cwcycle_enabled = 1;
        c->fallback_timeout = 900;
        c->cacheex_lg_only = 1;
        c->cacheex_lg_provid = 0x000000;
    END_CONFIG
    
    // ======================================================================
    // VIDEOGUARD 09C4 - Sky DE (NDS)
    // Source: Multiple configs
    // ======================================================================
    
    ADD_CONFIG(0x09C4, 0x000000)
        c->disable_crc_check = 1;
        c->cw_cache_time = 550;
        c->cw_cache_mode = 2;
        c->cwcycle_enabled = 1;
        c->fallback_timeout = 350;  /* fallbacktimeout_percaid */
        c->cacheex_lg_only = 1;
        c->cacheex_lg_provid = 0xFFFFFE;
    END_CONFIG
    
    // ======================================================================
    // VIDEOGUARD 098C - Sky UK
    // ======================================================================
    
    ADD_CONFIG(0x098C, 0x000000)
        c->disable_crc_check = 1;
        c->cw_cache_time = 450;
        c->cw_cache_mode = 2;
        c->cwcycle_enabled = 1;
        c->cacheex_lg_only = 1;
        c->cacheex_lg_provid = 0xFFFFFE;
    END_CONFIG
    
    // ======================================================================
    // VIDEOGUARD 09C7, 098E
    // ======================================================================
    
    ADD_CONFIG(0x09C7, 0x000000)
        c->cacheex_lg_only = 1;
        c->cacheex_lg_provid = 0xFFFFFE;
    END_CONFIG
    
    ADD_CONFIG(0x098E, 0x000000)
        c->cacheex_lg_only = 1;
        c->cacheex_lg_provid = 0xFFFFFE;
    END_CONFIG
    
    // ======================================================================
    // VIDEOGUARD 0963 - Sky UK
    // ======================================================================
    
    ADD_CONFIG(0x0963, 0x000000)
        c->cwcycle_enabled = 1;
    END_CONFIG
    
    // ======================================================================
    // VIDEOGUARD 09CD - Sky IT
    // ======================================================================
    
    ADD_CONFIG(0x09CD, 0x000000)
        c->cwcycle_enabled = 1;
    END_CONFIG
    
    // ======================================================================
    // VIDEOGUARD 092F - DolceTV
    // Source: LinuxSat-Support #146971
    // ======================================================================
    
    ADD_CONFIG(0x092F, 0x000000)
        /* ecmwhitelist: 092F:83 */
        c->ecm_whitelist[0] = 0x83;
        c->ecm_whitelist_count = 1;
    END_CONFIG
    
    // ======================================================================
    // CONAX 0B00 - DigitalALB
    // Source: LinuxSat-Support #146971
    // ======================================================================
    
    ADD_CONFIG(0x0B00, 0x000000)
        /* ecmwhitelist: 0B00:5D,67,AE,5A,88 */
        c->ecm_whitelist[0] = 0x5D;
        c->ecm_whitelist[1] = 0x67;
        c->ecm_whitelist[2] = 0xAE;
        c->ecm_whitelist[3] = 0x5A;
        c->ecm_whitelist[4] = 0x88;
        c->ecm_whitelist_count = 5;
        c->cw_cache_time = 5000;
        c->cw_cache_mode = 2;
        c->cwcycle_enabled = 1;
    END_CONFIG
    
    // ======================================================================
    // CONAX 0B01 - DigitalALB
    // Source: LinuxSat-Support #151081
    // ======================================================================
    
    ADD_CONFIG(0x0B01, 0x000000)
        c->disable_crc_check = 1;
        c->cwcycle_enabled = 1;
    END_CONFIG
    
    // ======================================================================
    // CONAX 0B02 - DigitalALB
    // Source: LinuxSat-Support #146971
    // ======================================================================
    
    ADD_CONFIG(0x0B02, 0x000000)
        /* ecmwhitelist: 0B02:84,4E */
        c->ecm_whitelist[0] = 0x84;
        c->ecm_whitelist[1] = 0x4E;
        c->ecm_whitelist_count = 2;
        c->cwcycle_enabled = 1;
    END_CONFIG
    
    // ======================================================================
    // CRYPTOWORKS 0D95/0D96/0D98 - ORF / Skylink
    // Source: Multiple configs
    // ======================================================================
    
    ADD_CONFIG(0x0D95, 0x000000)
        c->cw_cache_time = 3000;
        c->cw_cache_mode = 2;
        c->cwcycle_enabled = 1;
    END_CONFIG
    
    ADD_CONFIG(0x0D96, 0x000000)
        c->cw_cache_time = 3000;
        c->cw_cache_mode = 2;
        c->cwcycle_enabled = 1;
    END_CONFIG
    
    ADD_CONFIG(0x0D97, 0x000000)
        c->cwcycle_enabled = 1;
    END_CONFIG
    
    ADD_CONFIG(0x0D98, 0x000000)
        c->cw_cache_time = 3000;
        c->cw_cache_mode = 2;
        c->cwcycle_enabled = 1;
        c->cacheex_lg_only = 1;
        c->cacheex_lg_provid = 0xFFFFFE;
    END_CONFIG
    
    ADD_CONFIG(0x0D98, 0x000010)
        c->cacheex_lg_only = 1;
    END_CONFIG
    
    // ======================================================================
    // IRDETO 0604 - CanalDigitaal
    // Source: Multiple configs
    // ======================================================================
    
    ADD_CONFIG(0x0604, 0x000000)
        c->disable_crc_check = 1;
        c->cwcycle_enabled = 1;
    END_CONFIG
    
    // ======================================================================
    // SECA/NAGRA 0100 - Seca tunneled
    // Source: LinuxSat-Support #151081
    // ======================================================================
    
    ADD_CONFIG(0x0100, 0x003311)
        c->disable_crc_check = 1;
        c->cw_cache_time = 550;
        c->cw_cache_mode = 2;
        c->cwcycle_enabled = 1;
    END_CONFIG
    
    ADD_CONFIG(0x0100, 0x00006C)
        c->disable_crc_check = 1;
    END_CONFIG
    
    ADD_CONFIG(0x0100, 0x00006D)
        c->disable_crc_check = 1;
        c->cwcycle_enabled = 1;
    END_CONFIG
    
    // ======================================================================
    // VIACCESS 0500 - Multiple Providers
    // Source: LonasDigital, LinuxSat-Support
    // ======================================================================
    
    ADD_CONFIG(0x0500, 0x050F00)
        c->disable_crc_check = 1;
    END_CONFIG
    
    ADD_CONFIG(0x0500, 0x030B00)
        c->disable_crc_check = 1;
    END_CONFIG
    
    ADD_CONFIG(0x0500, 0x042820)
        c->disable_crc_check = 1;
    END_CONFIG
    
    ADD_CONFIG(0x0500, 0x020810)
        c->disable_crc_check = 1;
    END_CONFIG
    
    ADD_CONFIG(0x0500, 0x030830)
        c->disable_crc_check = 1;
    END_CONFIG
    
    ADD_CONFIG(0x0500, 0x032830)
        c->disable_crc_check = 1;
    END_CONFIG
    
    ADD_CONFIG(0x0500, 0x032840)
        c->disable_crc_check = 1;
    END_CONFIG
    
    ADD_CONFIG(0x0500, 0x032900)
        c->disable_crc_check = 1;
    END_CONFIG
    
    ADD_CONFIG(0x0500, 0x032920)
        c->disable_crc_check = 1;
    END_CONFIG
    
    ADD_CONFIG(0x0500, 0x032930)
        c->disable_crc_check = 1;
    END_CONFIG
    
    ADD_CONFIG(0x0500, 0x032940)
        c->disable_crc_check = 1;
    END_CONFIG
    
    ADD_CONFIG(0x0500, 0x032950)
        c->disable_crc_check = 1;
    END_CONFIG
    
    ADD_CONFIG(0x0500, 0x041A00)
        c->disable_crc_check = 1;
    END_CONFIG
    
    ADD_CONFIG(0x0500, 0x041900)
        c->disable_crc_check = 1;
    END_CONFIG
    
    ADD_CONFIG(0x0500, 0x041950)
        c->disable_crc_check = 1;
    END_CONFIG
    
    ADD_CONFIG(0x0500, 0x041A30)
        c->disable_crc_check = 1;
    END_CONFIG
    
    ADD_CONFIG(0x0500, 0x042800)
        c->disable_crc_check = 1;
    END_CONFIG
    
    ADD_CONFIG(0x0500, 0x043330)
        c->disable_crc_check = 1;
    END_CONFIG
    
    ADD_CONFIG(0x0500, 0x051B00)
        c->disable_crc_check = 1;
    END_CONFIG
    
    ADD_CONFIG(0x0500, 0x060D00)
        c->disable_crc_check = 1;
    END_CONFIG
    
    // ======================================================================
    // BETACRYPT 1708
    // ======================================================================
    
    ADD_CONFIG(0x1708, 0x000000)
        c->disable_crc_check = 1;
    END_CONFIG
    
    // ======================================================================
    // 1722 - Misc
    // ======================================================================
    
    ADD_CONFIG(0x1722, 0x000000)
        c->cacheex_lg_only = 1;
        c->cacheex_lg_provid = 0xFFFFFE;
    END_CONFIG
    
    // ======================================================================
    // CRYPTOWORKS 0D00/0D02/0D05
    // ======================================================================
    
    ADD_CONFIG(0x0D00, 0x000000)
        c->cwcycle_enabled = 1;
    END_CONFIG
    
    ADD_CONFIG(0x0D02, 0x000000)
        c->cwcycle_enabled = 1;
    END_CONFIG
    
    ADD_CONFIG(0x0D05, 0x000000)
        c->cwcycle_enabled = 1;
    END_CONFIG
    
    // ======================================================================
    // IRDETO 0606/0609/0624/0629
    // ======================================================================
    
    ADD_CONFIG(0x0606, 0x000000)
        c->cwcycle_enabled = 1;
    END_CONFIG
    
    ADD_CONFIG(0x0624, 0x000000)
        c->cwcycle_enabled = 1;
    END_CONFIG
    
    // ======================================================================
    // DRE-CRYPT 4AEE
    // ======================================================================
    
    ADD_CONFIG(0x4AEE, 0x000000)
        c->cwcycle_enabled = 1;
    END_CONFIG
    
    // ======================================================================
    // TANDERBERG 1010
    // ======================================================================
    
    ADD_CONFIG(0x1010, 0x000000)
        c->fallback_timeout = 1500;
    END_CONFIG
    
    // ======================================================================
    // NAGRA 1802/1803/1805
    // ======================================================================
    
    ADD_CONFIG(0x1802, 0x000000)
        c->cwcycle_enabled = 1;
    END_CONFIG
    
    ADD_CONFIG(0x1803, 0x003343)
        c->disable_crc_check = 1;
    END_CONFIG
    
    ADD_CONFIG(0x1805, 0x000000)
        c->cwcycle_enabled = 1;
    END_CONFIG
    
    // ======================================================================
    // VIDEOGUARD 0960, 093E, 090D, 090F, 092B, 0930, 09BE, 094C
    // ======================================================================
    
    ADD_CONFIG(0x0960, 0x000000)
        c->cwcycle_enabled = 1;
    END_CONFIG
    
    ADD_CONFIG(0x093E, 0x000000)
        c->cwcycle_enabled = 1;
    END_CONFIG
    
    ADD_CONFIG(0x094C, 0x000000)
        c->cwcycle_enabled = 1;
    END_CONFIG
    
    // ======================================================================
    // 0E00 - PowerVu
    // ======================================================================
    
    ADD_CONFIG(0x0E00, 0x000000)
        c->disable_crc_check = 1;
    END_CONFIG
    
    // ======================================================================
    // NAGRA 1831, 1835, 1838 - Misc
    // ======================================================================
    
    ADD_CONFIG(0x1831, 0x000000)
        c->cwcycle_enabled = 1;
    END_CONFIG
    
    ADD_CONFIG(0x1835, 0x000000)
        c->cwcycle_enabled = 1;
    END_CONFIG
    
    ADD_CONFIG(0x1838, 0x000000)
        c->cw_cache_time = 2500;
        c->cw_cache_mode = 2;
        c->cwcycle_enabled = 1;
    END_CONFIG
    
    // ======================================================================
    // Irdeto 0648, 0650
    // ======================================================================
    
    ADD_CONFIG(0x0648, 0x000000)
        c->cw_cache_time = 3000;
        c->cw_cache_mode = 2;
    END_CONFIG
    
    ADD_CONFIG(0x0650, 0x000000)
        c->cw_cache_time = 3000;
        c->cw_cache_mode = 2;
    END_CONFIG
    
    #undef ADD_CONFIG
    #undef END_CONFIG
}

// ============================================================================
// QUERY FUNCTIONS
// ============================================================================

static inline int provider_should_disable_crc(uint16_t caid, uint32_t provid) {
    struct provider_ecm_config *c = provider_get_config(caid, provid);
    return c ? c->disable_crc_check : 0;
}

static inline int provider_should_block_emm_g(uint16_t caid, uint32_t provid) {
    struct provider_ecm_config *c = provider_get_config(caid, provid);
    return c ? c->block_emm_g : 0;
}

static inline int provider_should_block_emm_unknown(uint16_t caid, uint32_t provid) {
    struct provider_ecm_config *c = provider_get_config(caid, provid);
    return c ? c->block_emm_unknown : 0;
}

static inline int provider_needs_force_even_sa(uint16_t caid, uint32_t provid) {
    struct provider_ecm_config *c = provider_get_config(caid, provid);
    return c ? c->force_even_sa : 0;
}

static inline uint32_t provider_get_fallback_timeout(uint16_t caid, uint32_t provid) {
    struct provider_ecm_config *c = provider_get_config(caid, provid);
    if (c && c->fallback_timeout > 0) return c->fallback_timeout;
    return 0;  // Use default
}

static inline uint32_t provider_get_cw_cache_time(uint16_t caid, uint32_t provid) {
    struct provider_ecm_config *c = provider_get_config(caid, provid);
    if (c && c->cw_cache_time > 0) return c->cw_cache_time;
    return 0;  // Use default
}

static inline int provider_cwcycle_enabled(uint16_t caid, uint32_t provid) {
    struct provider_ecm_config *c = provider_get_config(caid, provid);
    return c ? c->cwcycle_enabled : 0;
}

static inline int provider_icam_enabled(uint16_t caid, uint32_t provid) {
    struct provider_ecm_config *c = provider_get_config(caid, provid);
    return c ? c->icam_enabled : 0;
}

static inline int provider_cacheex_lg_only(uint16_t caid, uint32_t provid) {
    struct provider_ecm_config *c = provider_get_config(caid, provid);
    return c ? c->cacheex_lg_only : 0;
}

static inline int provider_ecm_whitelisted(uint16_t caid, uint32_t provid, uint8_t ecm_len) {
    struct provider_ecm_config *c = provider_get_config(caid, provid);
    int i;
    if (!c || c->ecm_whitelist_count == 0) return 1;  // No whitelist = accept all
    for (i = 0; i < c->ecm_whitelist_count; i++) {
        if (c->ecm_whitelist[i] == ecm_len) return 1;
    }
    return 0;  // Not in whitelist
}

static inline int provider_get_detect_seca_nagra_tunnel(uint16_t caid, uint32_t provid) {
    struct provider_ecm_config *c = provider_get_config(caid, provid);
    return c ? c->detect_seca_nagra_tunnel : 0;
}

static inline uint16_t provider_get_mhz(uint16_t caid, uint32_t provid) {
    struct provider_ecm_config *c = provider_get_config(caid, provid);
    return c ? c->mhz : 0;
}

static inline uint16_t provider_get_cardmhz(uint16_t caid, uint32_t provid) {
    struct provider_ecm_config *c = provider_get_config(caid, provid);
    return c ? c->cardmhz : 0;
}

#endif /* ECM_PROVIDER_CONFIG_H */
