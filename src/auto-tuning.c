/*
 * Auto-Tuning System for MultiCS
 * Version: 1.0
 * Date: 2026-10-26
 * 
 * Implementation of automatic profile tuning based on performance metrics.
 */

#include "auto-tuning.h"
#include "tools.h" /* getseconds(): monotonic clock for interval checks */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/socket.h>

// ============================================================================
// GLOBAL CONFIGURATION
// ============================================================================

auto_tuning_config_t auto_tuning_cfg;
static pthread_mutex_t auto_tuning_mutex = PTHREAD_MUTEX_INITIALIZER;

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

static auto_tuning_profile_t* find_or_create_profile(uint16_t caid, uint32_t provid) {
    // Search for existing profile
    for (uint32_t i = 0; i < auto_tuning_cfg.profile_count; i++) {
        if (auto_tuning_cfg.profiles[i].caid == caid && 
            auto_tuning_cfg.profiles[i].provid == provid) {
            return &auto_tuning_cfg.profiles[i];
        }
    }
    
    // Create new profile if space available
    if (auto_tuning_cfg.profile_count < auto_tuning_cfg.max_profiles) {
        auto_tuning_profile_t *profile = &auto_tuning_cfg.profiles[auto_tuning_cfg.profile_count++];
        memset(profile, 0, sizeof(auto_tuning_profile_t));
        profile->caid = caid;
        profile->provid = provid;
        profile->current_server_timeout = 800;  // Default
        profile->current_cache_timeout = 250;   // Default
        profile->current_valid_ecm_time = 800;  // Default
        profile->stats.last_update = getseconds();
        return profile;
    }
    
    return NULL;
}

static void calculate_metrics(auto_tuning_profile_t *profile) {
    profile_stats_t *stats = &profile->stats;
    
    if (stats->total_requests == 0) {
        profile->cache_hit_rate = 0.0f;
        profile->timeout_rate = 0.0f;
        profile->decode_success_rate = 0.0f;
        return;
    }
    
    uint32_t cache_total = stats->cache_hits + stats->cache_misses;
    if (cache_total > 0) {
        profile->cache_hit_rate = (float)stats->cache_hits / cache_total;
    } else {
        profile->cache_hit_rate = 0.0f;
    }
    
    profile->timeout_rate = (float)stats->timeouts / stats->total_requests;
    
    uint32_t decode_total = stats->decode_success + stats->decode_failed;
    if (decode_total > 0) {
        profile->decode_success_rate = (float)stats->decode_success / decode_total;
    } else {
        profile->decode_success_rate = 0.0f;
    }
}

static void tune_profile(auto_tuning_profile_t *profile) {
    // Check if we have enough samples
    if (profile->stats.total_requests < auto_tuning_cfg.min_samples) {
        return;
    }
    
    calculate_metrics(profile);
    
    int tuned = 0;
    
    // Tune cache timeout based on cache hit rate
    if (profile->cache_hit_rate < CACHE_HIT_RATE_BAD) {
        // Low cache hit rate - increase cache timeout
        if (profile->current_cache_timeout < CACHE_TIMEOUT_MAX) {
            profile->current_cache_timeout += CACHE_TIMEOUT_ADJUST_STEP;
            if (profile->current_cache_timeout > CACHE_TIMEOUT_MAX) {
                profile->current_cache_timeout = CACHE_TIMEOUT_MAX;
            }
            tuned = 1;
        }
    } else if (profile->cache_hit_rate > CACHE_HIT_RATE_GOOD) {
        // High cache hit rate - can reduce cache timeout slightly
        if (profile->current_cache_timeout > CACHE_TIMEOUT_MIN) {
            profile->current_cache_timeout -= CACHE_TIMEOUT_ADJUST_STEP / 2;
            if (profile->current_cache_timeout < CACHE_TIMEOUT_MIN) {
                profile->current_cache_timeout = CACHE_TIMEOUT_MIN;
            }
            tuned = 1;
        }
    }
    
    // Tune server timeout based on timeout rate
    if (profile->timeout_rate > TIMEOUT_RATE_HIGH) {
        // High timeout rate - increase server timeout
        if (profile->current_server_timeout < TIMEOUT_MAX) {
            profile->current_server_timeout += TIMEOUT_ADJUST_STEP;
            if (profile->current_server_timeout > TIMEOUT_MAX) {
                profile->current_server_timeout = TIMEOUT_MAX;
            }
            profile->current_valid_ecm_time = profile->current_server_timeout * 1.2;
            tuned = 1;
        }
    } else if (profile->timeout_rate < 0.05 && profile->decode_success_rate > DECODE_SUCCESS_RATE_GOOD) {
        // Low timeout rate and good decode rate - can reduce timeout slightly
        if (profile->current_server_timeout > TIMEOUT_MIN) {
            profile->current_server_timeout -= TIMEOUT_ADJUST_STEP / 2;
            if (profile->current_server_timeout < TIMEOUT_MIN) {
                profile->current_server_timeout = TIMEOUT_MIN;
            }
            profile->current_valid_ecm_time = profile->current_server_timeout * 1.2;
            tuned = 1;
        }
    }
    
    if (tuned) {
        profile->tuning_count++;
        profile->last_tune_time = getseconds();
        profile->needs_tuning = 0;
        
        // Log tuning operation
        printf("[AUTO-TUNING] Tuned CAID %04X:%06X - Server: %dms, Cache: %dms, ValidECM: %dms\n",
               profile->caid, profile->provid,
               profile->current_server_timeout,
               profile->current_cache_timeout,
               profile->current_valid_ecm_time);
        
        auto_tuning_cfg.total_tuning_operations++;
    } else {
        profile->is_optimized = 1;
    }
}

// ============================================================================
// INITIALIZATION
// ============================================================================

int auto_tuning_init(void) {
    memset(&auto_tuning_cfg, 0, sizeof(auto_tuning_config_t));
    
    auto_tuning_cfg.enabled = AUTO_TUNING_ENABLED;
    auto_tuning_cfg.interval_ms = AUTO_TUNING_INTERVAL_MS;
    auto_tuning_cfg.min_samples = AUTO_TUNING_MIN_SAMPLES;
    auto_tuning_cfg.max_profiles = 100;  // Support up to 100 profiles
    
    auto_tuning_cfg.profiles = calloc(auto_tuning_cfg.max_profiles, sizeof(auto_tuning_profile_t));
    if (!auto_tuning_cfg.profiles) {
        fprintf(stderr, "[AUTO-TUNING] Failed to allocate memory for profiles\n");
        return -1;
    }
    
    auto_tuning_cfg.last_check_time = getseconds();
    auto_tuning_cfg.total_tuning_operations = 0;
    
    printf("[AUTO-TUNING] System initialized (interval: %dms, min samples: %d)\n",
           auto_tuning_cfg.interval_ms, auto_tuning_cfg.min_samples);
    
    return 0;
}

void auto_tuning_cleanup(void) {
    if (auto_tuning_cfg.profiles) {
        free(auto_tuning_cfg.profiles);
        auto_tuning_cfg.profiles = NULL;
    }
    printf("[AUTO-TUNING] System cleanup completed\n");
}

// ============================================================================
// STATISTICS COLLECTION
// ============================================================================

void auto_tuning_record_cache_hit(uint16_t caid, uint32_t provid) {
    if (!auto_tuning_cfg.enabled) return;
    
    pthread_mutex_lock(&auto_tuning_mutex);
    auto_tuning_profile_t *profile = find_or_create_profile(caid, provid);
    if (profile) {
        profile->stats.cache_hits++;
        profile->stats.total_requests++;
        profile->stats.last_update = getseconds();
    }
    pthread_mutex_unlock(&auto_tuning_mutex);
}

void auto_tuning_record_cache_miss(uint16_t caid, uint32_t provid) {
    if (!auto_tuning_cfg.enabled) return;
    
    pthread_mutex_lock(&auto_tuning_mutex);
    auto_tuning_profile_t *profile = find_or_create_profile(caid, provid);
    if (profile) {
        profile->stats.cache_misses++;
        profile->stats.total_requests++;
        profile->stats.last_update = getseconds();
    }
    pthread_mutex_unlock(&auto_tuning_mutex);
}

void auto_tuning_record_timeout(uint16_t caid, uint32_t provid) {
    if (!auto_tuning_cfg.enabled) return;
    
    pthread_mutex_lock(&auto_tuning_mutex);
    auto_tuning_profile_t *profile = find_or_create_profile(caid, provid);
    if (profile) {
        profile->stats.timeouts++;
        profile->stats.total_requests++;
        profile->stats.last_update = getseconds();
        profile->needs_tuning = 1;
    }
    pthread_mutex_unlock(&auto_tuning_mutex);
}

void auto_tuning_record_decode_success(uint16_t caid, uint32_t provid) {
    if (!auto_tuning_cfg.enabled) return;
    
    pthread_mutex_lock(&auto_tuning_mutex);
    auto_tuning_profile_t *profile = find_or_create_profile(caid, provid);
    if (profile) {
        profile->stats.decode_success++;
        profile->stats.last_update = getseconds();
    }
    pthread_mutex_unlock(&auto_tuning_mutex);
}

void auto_tuning_record_decode_failed(uint16_t caid, uint32_t provid) {
    if (!auto_tuning_cfg.enabled) return;
    
    pthread_mutex_lock(&auto_tuning_mutex);
    auto_tuning_profile_t *profile = find_or_create_profile(caid, provid);
    if (profile) {
        profile->stats.decode_failed++;
        profile->stats.last_update = getseconds();
        profile->needs_tuning = 1;
    }
    pthread_mutex_unlock(&auto_tuning_mutex);
}

void auto_tuning_record_black_screen(uint16_t caid, uint32_t provid) {
    if (!auto_tuning_cfg.enabled) return;
    
    pthread_mutex_lock(&auto_tuning_mutex);
    auto_tuning_profile_t *profile = find_or_create_profile(caid, provid);
    if (profile) {
        profile->stats.black_screens++;
        profile->stats.last_update = getseconds();
        profile->needs_tuning = 1;
    }
    pthread_mutex_unlock(&auto_tuning_mutex);
}

// ============================================================================
// TUNING OPERATIONS
// ============================================================================

void auto_tuning_check_and_tune(void) {
    if (!auto_tuning_cfg.enabled) return;
    
    time_t now = getseconds();
    if ((now - auto_tuning_cfg.last_check_time) * 1000 < auto_tuning_cfg.interval_ms) {
        return;  // Not time to check yet
    }
    
    pthread_mutex_lock(&auto_tuning_mutex);
    
    for (uint32_t i = 0; i < auto_tuning_cfg.profile_count; i++) {
        auto_tuning_profile_t *profile = &auto_tuning_cfg.profiles[i];
        if (profile->needs_tuning || !profile->is_optimized) {
            tune_profile(profile);
        }
    }
    
    auto_tuning_cfg.last_check_time = now;
    
    pthread_mutex_unlock(&auto_tuning_mutex);
}

int auto_tuning_force_tune(uint16_t caid, uint32_t provid) {
    pthread_mutex_lock(&auto_tuning_mutex);
    
    auto_tuning_profile_t *profile = find_or_create_profile(caid, provid);
    if (!profile) {
        pthread_mutex_unlock(&auto_tuning_mutex);
        return -1;
    }
    
    tune_profile(profile);
    
    pthread_mutex_unlock(&auto_tuning_mutex);
    return 0;
}

void auto_tuning_reset_stats(uint16_t caid, uint32_t provid) {
    pthread_mutex_lock(&auto_tuning_mutex);
    
    for (uint32_t i = 0; i < auto_tuning_cfg.profile_count; i++) {
        if (auto_tuning_cfg.profiles[i].caid == caid && 
            auto_tuning_cfg.profiles[i].provid == provid) {
            memset(&auto_tuning_cfg.profiles[i].stats, 0, sizeof(profile_stats_t));
            auto_tuning_cfg.profiles[i].needs_tuning = 1;
            auto_tuning_cfg.profiles[i].is_optimized = 0;
            break;
        }
    }
    
    pthread_mutex_unlock(&auto_tuning_mutex);
}

void auto_tuning_reset_all_stats(void) {
    pthread_mutex_lock(&auto_tuning_mutex);
    
    for (uint32_t i = 0; i < auto_tuning_cfg.profile_count; i++) {
        memset(&auto_tuning_cfg.profiles[i].stats, 0, sizeof(profile_stats_t));
        auto_tuning_cfg.profiles[i].needs_tuning = 1;
        auto_tuning_cfg.profiles[i].is_optimized = 0;
    }
    
    pthread_mutex_unlock(&auto_tuning_mutex);
}

// ============================================================================
// QUERY FUNCTIONS
// ============================================================================

int auto_tuning_get_stats(uint16_t caid, uint32_t provid, profile_stats_t *stats) {
    pthread_mutex_lock(&auto_tuning_mutex);
    
    for (uint32_t i = 0; i < auto_tuning_cfg.profile_count; i++) {
        if (auto_tuning_cfg.profiles[i].caid == caid && 
            auto_tuning_cfg.profiles[i].provid == provid) {
            memcpy(stats, &auto_tuning_cfg.profiles[i].stats, sizeof(profile_stats_t));
            pthread_mutex_unlock(&auto_tuning_mutex);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&auto_tuning_mutex);
    return -1;
}

auto_tuning_profile_t* auto_tuning_get_profile(uint16_t caid, uint32_t provid) {
    for (uint32_t i = 0; i < auto_tuning_cfg.profile_count; i++) {
        if (auto_tuning_cfg.profiles[i].caid == caid && 
            auto_tuning_cfg.profiles[i].provid == provid) {
            return &auto_tuning_cfg.profiles[i];
        }
    }
    return NULL;
}

int auto_tuning_is_enabled(void) {
    return auto_tuning_cfg.enabled;
}

void auto_tuning_set_enabled(int enabled) {
    auto_tuning_cfg.enabled = enabled ? 1 : 0;
}

// ============================================================================
// REPORTING
// ============================================================================

int auto_tuning_generate_report(char *buffer, int buffer_size) {
    int written = 0;
    
    written += snprintf(buffer + written, buffer_size - written,
                       "=== Auto-Tuning System Report ===\n");
    written += snprintf(buffer + written, buffer_size - written,
                       "Status: %s\n", auto_tuning_cfg.enabled ? "ENABLED" : "DISABLED");
    written += snprintf(buffer + written, buffer_size - written,
                       "Interval: %dms\n", auto_tuning_cfg.interval_ms);
    written += snprintf(buffer + written, buffer_size - written,
                       "Min Samples: %d\n", auto_tuning_cfg.min_samples);
    written += snprintf(buffer + written, buffer_size - written,
                       "Total Profiles: %d\n", auto_tuning_cfg.profile_count);
    written += snprintf(buffer + written, buffer_size - written,
                       "Total Tuning Operations: %d\n\n", auto_tuning_cfg.total_tuning_operations);
    
    for (uint32_t i = 0; i < auto_tuning_cfg.profile_count && written < buffer_size; i++) {
        auto_tuning_profile_t *profile = &auto_tuning_cfg.profiles[i];
        calculate_metrics(profile);
        
        written += snprintf(buffer + written, buffer_size - written,
                           "Profile %04X:%06X:\n", profile->caid, profile->provid);
        written += snprintf(buffer + written, buffer_size - written,
                           "  Total Requests: %d\n", profile->stats.total_requests);
        written += snprintf(buffer + written, buffer_size - written,
                           "  Cache Hit Rate: %.1f%%\n", profile->cache_hit_rate * 100);
        written += snprintf(buffer + written, buffer_size - written,
                           "  Timeout Rate: %.1f%%\n", profile->timeout_rate * 100);
        written += snprintf(buffer + written, buffer_size - written,
                           "  Decode Success Rate: %.1f%%\n", profile->decode_success_rate * 100);
        written += snprintf(buffer + written, buffer_size - written,
                           "  Server Timeout: %dms\n", profile->current_server_timeout);
        written += snprintf(buffer + written, buffer_size - written,
                           "  Cache Timeout: %dms\n", profile->current_cache_timeout);
        written += snprintf(buffer + written, buffer_size - written,
                           "  Valid ECM Time: %dms\n", profile->current_valid_ecm_time);
        written += snprintf(buffer + written, buffer_size - written,
                           "  Tuning Count: %d\n", profile->tuning_count);
        written += snprintf(buffer + written, buffer_size - written,
                           "  Status: %s\n\n", profile->is_optimized ? "OPTIMIZED" : "TUNING");
    }
    
    return written;
}

void auto_tuning_log_stats(void) {
    char buffer[8192];
    int len = auto_tuning_generate_report(buffer, sizeof(buffer));
    if (len > 0) {
        printf("%s", buffer);
    }
}

void auto_tuning_analyze(void) {
    auto_tuning_check_and_tune();
}

// Forward declaration for httpserver integration
struct client_data;
struct http_request;

void http_send_auto_tuning(int sock, struct http_request *req) {
    char buffer[16384];
    int len = auto_tuning_generate_report(buffer, sizeof(buffer));
    if (len < 0) len = 0;
    if (len > (int)sizeof(buffer) - 1) len = (int)sizeof(buffer) - 1;
    buffer[len] = '\0';

    /* Band 98: was raw text/plain -- the only unstyled page in the UI.
     * Same design tokens as httpstyle.c, report kept as <pre>. */
    const char *header =
        "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n\r\n"
        "<!doctype html><html lang='ar' dir='rtl'><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width, initial-scale=1'>"
        "<title>Auto-Tuning - MultiCS</title>"
        "<script>(function(){try{var t=localStorage.getItem('mcs-theme');if(t)document.documentElement.setAttribute('data-theme',t);}catch(e){}})();</script>"
        "<style>"
        "body{font-family:system-ui,'Segoe UI',Tahoma,Arial,sans-serif;margin:0;padding:22px;background:linear-gradient(180deg,#f2f6fa,#e9eef3);color:#182635;}"
        "h1{color:#0f5f8a;font-size:22px;border-bottom:3px solid #12838f;padding-bottom:8px;}"
        "pre{background:#0d1520;color:#d7e6f5;border:1px solid #22344c;border-radius:10px;padding:14px 16px;overflow:auto;font:12.5px/1.6 ui-monospace,Consolas,monospace;direction:ltr;text-align:left;}"
        ".mcs-nav{background:linear-gradient(135deg,#0b2f47,#0f5f8a 60%,#12838f);margin:-22px -22px 18px;padding:10px 22px;}"
        ".mcs-nav a{color:#dcebf5;margin-left:14px;font-size:13px;font-weight:600;text-decoration:none;}"
        ".mcs-nav a:hover{color:#fff;text-decoration:underline;}"
        "[data-theme=dark] body{background:linear-gradient(180deg,#111a23,#0d1319);color:#e6edf3;}"
        "[data-theme=dark] h1{color:#6cb6e8;}"
        "@media (prefers-color-scheme:dark){:root:not([data-theme=light]) body{background:linear-gradient(180deg,#111a23,#0d1319);color:#e6edf3;}:root:not([data-theme=light]) h1{color:#6cb6e8;}}"
        "@media (max-width:760px){body{padding:12px}.mcs-nav{margin:-12px -12px 12px;padding:8px 12px}h1{font-size:19px}}"
        "</style></head><body>"
        "<div class='mcs-nav'><a href='/'>Home</a><a href='/servers'>Servers</a><a href='/profiles'>Profiles</a><a href='/diagnostics'>Diagnostics</a><a href='/editor'>Editor</a></div>"
        "<h1>Auto-Tuning</h1><pre>";
    const char *footer = "</pre></body></html>";
    send(sock, header, strlen(header), 0);
    if (len > 0) {
        send(sock, buffer, len, 0);
    }
    send(sock, footer, strlen(footer), 0);
}
