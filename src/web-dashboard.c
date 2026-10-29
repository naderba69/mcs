/*
 * Web Dashboard for MultiCS
 * Version: 1.0
 * Date: 2026-10-26
 * 
 * Implementation of web dashboard with real-time monitoring.
 */

#include "web-dashboard.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// ============================================================================
// GLOBAL CONFIGURATION
// ============================================================================

web_dashboard_config_t web_dashboard_cfg;
static pthread_mutex_t dashboard_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t dashboard_thread;
static int server_sock = -1;
static int running = 0;

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

static void calculate_global_stats(void) {
    dashboard_stats_t *stats = &web_dashboard_cfg.global_stats;
    
    if (stats->total_requests > 0) {
        stats->success_rate = (float)stats->successful_decodes / stats->total_requests * 100.0f;
        stats->timeout_rate = (float)stats->timeouts / stats->total_requests * 100.0f;
    } else {
        stats->success_rate = 0.0f;
        stats->timeout_rate = 0.0f;
    }
    
    uint32_t cache_total = stats->cache_hits + stats->cache_misses;
    if (cache_total > 0) {
        stats->cache_hit_rate = (float)stats->cache_hits / cache_total * 100.0f;
    } else {
        stats->cache_hit_rate = 0.0f;
    }
}

static void update_uptime(void) {
    time_t now = time(NULL);
    web_dashboard_cfg.uptime_seconds = (uint32_t)(now - web_dashboard_cfg.start_time);
}

static dashboard_profile_t* find_or_create_profile(uint16_t caid, uint32_t provid) {
    for (uint32_t i = 0; i < web_dashboard_cfg.profile_count; i++) {
        if (web_dashboard_cfg.profiles[i].caid == caid && 
            web_dashboard_cfg.profiles[i].provid == provid) {
            return &web_dashboard_cfg.profiles[i];
        }
    }
    
    if (web_dashboard_cfg.profile_count < web_dashboard_cfg.max_profiles) {
        dashboard_profile_t *profile = &web_dashboard_cfg.profiles[web_dashboard_cfg.profile_count++];
        memset(profile, 0, sizeof(dashboard_profile_t));
        profile->caid = caid;
        profile->provid = provid;
        snprintf(profile->name, sizeof(profile->name), "CAID %04X:%06X", caid, provid);
        profile->status = 1;  // Active
        return profile;
    }
    
    return NULL;
}

// ============================================================================
// HTML GENERATION
// ============================================================================

static int generate_dashboard_html(char *buffer, int buffer_size) {
    int written = 0;
    
    update_uptime();
    calculate_global_stats();
    
    dashboard_stats_t *stats = &web_dashboard_cfg.global_stats;
    
    written += snprintf(buffer + written, buffer_size - written,
        "<!DOCTYPE html>\n"
        "<html lang=\"ar\" dir=\"rtl\">\n"
        "<head>\n"
        "<meta charset=\"UTF-8\">\n"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
        "<title>MultiCS Dashboard</title>\n"
        "<style>\n"
        "body { font-family: Arial, sans-serif; margin: 20px; background: #f5f5f5; }\n"
        ".header { background: #2c3e50; color: white; padding: 20px; border-radius: 8px; margin-bottom: 20px; }\n"
        ".stats-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(250px, 1fr)); gap: 20px; margin-bottom: 20px; }\n"
        ".stat-card { background: white; padding: 20px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }\n"
        ".stat-value { font-size: 2em; font-weight: bold; color: #2c3e50; }\n"
        ".stat-label { color: #7f8c8d; margin-top: 5px; }\n"
        ".success { color: #27ae60; }\n"
        ".warning { color: #f39c12; }\n"
        ".error { color: #e74c3c; }\n"
        ".info { color: #3498db; }\n"
        "table { width: 100%%; background: white; border-radius: 8px; overflow: hidden; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }\n"
        "th, td { padding: 12px; text-align: right; border-bottom: 1px solid #ecf0f1; }\n"
        "th { background: #34495e; color: white; }\n"
        "tr:hover { background: #f8f9fa; }\n"
        ".status-active { color: #27ae60; font-weight: bold; }\n"
        ".status-error { color: #e74c3c; font-weight: bold; }\n"
        "</style>\n"
        "</head>\n"
        "<body>\n"
        "<div class=\"header\">\n"
        "<h1>🎯 MultiCS Dashboard</h1>\n"
        "<p>نظام مراقبة متعدد البطاقات - Multi Card Server</p>\n"
        "</div>\n"
    );
    
    // System Status
    written += snprintf(buffer + written, buffer_size - written,
        "<div class=\"stats-grid\">\n"
        "<div class=\"stat-card\">\n"
        "<div class=\"stat-value info\">%s</div>\n"
        "<div class=\"stat-label\">حالة النظام</div>\n"
        "</div>\n"
        "<div class=\"stat-card\">\n"
        "<div class=\"stat-value\">%d</div>\n"
        "<div class=\"stat-label\">وقت التشغيل (ثانية)</div>\n"
        "</div>\n"
        "<div class=\"stat-card\">\n"
        "<div class=\"stat-value\">%d</div>\n"
        "<div class=\"stat-label\">الباقات المُحسّنة</div>\n"
        "</div>\n"
        "<div class=\"stat-card\">\n"
        "<div class=\"stat-value\">%d</div>\n"
        "<div class=\"stat-label\">الاتصالات النشطة</div>\n"
        "</div>\n"
        "</div>\n",
        web_dashboard_cfg.enabled ? "✅ نشط" : "❌ متوقف",
        web_dashboard_cfg.uptime_seconds,
        web_dashboard_cfg.profile_count,
        web_dashboard_cfg.active_connections
    );
    
    // Performance Stats
    written += snprintf(buffer + written, buffer_size - written,
        "<h2>📊 إحصائيات الأداء</h2>\n"
        "<div class=\"stats-grid\">\n"
        "<div class=\"stat-card\">\n"
        "<div class=\"stat-value\">%u</div>\n"
        "<div class=\"stat-label\">إجمالي الطلبات</div>\n"
        "</div>\n"
        "<div class=\"stat-card\">\n"
        "<div class=\"stat-value success\">%.1f%%</div>\n"
        "<div class=\"stat-label\">نسبة النجاح</div>\n"
        "</div>\n"
        "<div class=\"stat-card\">\n"
        "<div class=\"stat-value info\">%.1f%%</div>\n"
        "<div class=\"stat-label\">نسبة Cache Hit</div>\n"
        "</div>\n"
        "<div class=\"stat-card\">\n"
        "<div class=\"stat-value warning\">%.1f%%</div>\n"
        "<div class=\"stat-label\">نسبة Timeout</div>\n"
        "</div>\n"
        "</div>\n",
        stats->total_requests,
        stats->success_rate,
        stats->cache_hit_rate,
        stats->timeout_rate
    );
    
    // Detailed Stats
    written += snprintf(buffer + written, buffer_size - written,
        "<h2>📈 الإحصائيات التفصيلية</h2>\n"
        "<div class=\"stats-grid\">\n"
        "<div class=\"stat-card\">\n"
        "<div class=\"stat-value success\">%u</div>\n"
        "<div class=\"stat-label\">فك تشفير ناجح</div>\n"
        "</div>\n"
        "<div class=\"stat-card\">\n"
        "<div class=\"stat-value error\">%u</div>\n"
        "<div class=\"stat-label\">فك تشفير فاشل</div>\n"
        "</div>\n"
        "<div class=\"stat-card\">\n"
        "<div class=\"stat-value\">%u</div>\n"
        "<div class=\"stat-label\">Cache Hits</div>\n"
        "</div>\n"
        "<div class=\"stat-card\">\n"
        "<div class=\"stat-value\">%u</div>\n"
        "<div class=\"stat-label\">Cache Misses</div>\n"
        "</div>\n"
        "<div class=\"stat-card\">\n"
        "<div class=\"stat-value error\">%u</div>\n"
        "<div class=\"stat-label\">Black Screens</div>\n"
        "</div>\n"
        "<div class=\"stat-card\">\n"
        "<div class=\"stat-value warning\">%u</div>\n"
        "<div class=\"stat-label\">Fake CWs المكتشفة</div>\n"
        "</div>\n"
        "</div>\n",
        stats->successful_decodes,
        stats->failed_decodes,
        stats->cache_hits,
        stats->cache_misses,
        stats->black_screens,
        stats->fake_cws_detected
    );
    
    // Profiles Table
    written += snprintf(buffer + written, buffer_size - written,
        "<h2>🎯 الباقات</h2>\n"
        "<table>\n"
        "<thead>\n"
        "<tr>\n"
        "<th>الباقة</th>\n"
        "<th>CAID</th>\n"
        "<th>Provider</th>\n"
        "<th>الطلبات</th>\n"
        "<th>نسبة النجاح</th>\n"
        "<th>Cache Hit</th>\n"
        "<th>الحالة</th>\n"
        "</tr>\n"
        "</thead>\n"
        "<tbody>\n"
    );
    
    for (uint32_t i = 0; i < web_dashboard_cfg.profile_count && i < 20; i++) {
        dashboard_profile_t *profile = &web_dashboard_cfg.profiles[i];
        const char *status_class = (profile->status == 2) ? "status-active" : 
                                   (profile->status == 3) ? "status-error" : "status-active";
        const char *status_text = (profile->status == 2) ? "✅ محسّن" : 
                                  (profile->status == 3) ? "❌ خطأ" : "✅ نشط";
        
        written += snprintf(buffer + written, buffer_size - written,
            "<tr>\n"
            "<td>%s</td>\n"
            "<td>%04X</td>\n"
            "<td>%06X</td>\n"
            "<td>%u</td>\n"
            "<td>%.1f%%</td>\n"
            "<td>%.1f%%</td>\n"
            "<td class=\"%s\">%s</td>\n"
            "</tr>\n",
            profile->name,
            profile->caid,
            profile->provid,
            profile->requests,
            profile->success_rate,
            profile->cache_hit_rate,
            status_class,
            status_text
        );
    }
    
    written += snprintf(buffer + written, buffer_size - written,
        "</tbody>\n"
        "</table>\n"
        "<script>\n"
        "setTimeout(function() { location.reload(); }, %d);\n"
        "</script>\n"
        "</body>\n"
        "</html>\n",
        web_dashboard_cfg.update_interval_ms
    );
    
    return written;
}

// ============================================================================
// HTTP SERVER
// ============================================================================

static void handle_client(int client_sock) {
    char request[4096];
    int len = recv(client_sock, request, sizeof(request) - 1, 0);
    if (len <= 0) {
        close(client_sock);
        return;
    }
    request[len] = '\0';
    
    // Check request type
    if (strstr(request, "GET /api/stats") != NULL) {
        // JSON API
        char json[8192];
        calculate_global_stats();
        dashboard_stats_t *stats = &web_dashboard_cfg.global_stats;
        
        int json_len = snprintf(json, sizeof(json),
            "{"
            "\"total_requests\":%u,"
            "\"successful_decodes\":%u,"
            "\"failed_decodes\":%u,"
            "\"cache_hits\":%u,"
            "\"cache_misses\":%u,"
            "\"black_screens\":%u,"
            "\"fake_cws\":%u,"
            "\"timeouts\":%u,"
            "\"success_rate\":%.1f,"
            "\"cache_hit_rate\":%.1f,"
            "\"timeout_rate\":%.1f,"
            "\"uptime\":%u,"
            "\"profiles\":%u"
            "}",
            stats->total_requests,
            stats->successful_decodes,
            stats->failed_decodes,
            stats->cache_hits,
            stats->cache_misses,
            stats->black_screens,
            stats->fake_cws_detected,
            stats->timeouts,
            stats->success_rate,
            stats->cache_hit_rate,
            stats->timeout_rate,
            web_dashboard_cfg.uptime_seconds,
            web_dashboard_cfg.profile_count
        );
        
        const char *header = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\n\r\n";
        send(client_sock, header, strlen(header), 0);
        send(client_sock, json, json_len, 0);
    } else {
        // HTML Dashboard
        char html[32768];
        int html_len = generate_dashboard_html(html, sizeof(html));
        
        const char *header = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n\r\n";
        send(client_sock, header, strlen(header), 0);
        if (html_len > 0) {
            send(client_sock, html, html_len, 0);
        }
    }
    
    close(client_sock);
}

static void* dashboard_server_thread(void *arg) {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    
    while (running) {
        int client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &client_len);
        if (client_sock >= 0) {
            handle_client(client_sock);
        }
    }
    
    return NULL;
}

// ============================================================================
// INITIALIZATION
// ============================================================================

int web_dashboard_init(void) {
    memset(&web_dashboard_cfg, 0, sizeof(web_dashboard_config_t));
    
    web_dashboard_cfg.enabled = WEB_DASHBOARD_ENABLED;
    web_dashboard_cfg.port = WEB_DASHBOARD_PORT;
    web_dashboard_cfg.update_interval_ms = WEB_DASHBOARD_UPDATE_MS;
    web_dashboard_cfg.max_profiles = 100;
    
    web_dashboard_cfg.profiles = calloc(web_dashboard_cfg.max_profiles, sizeof(dashboard_profile_t));
    if (!web_dashboard_cfg.profiles) {
        fprintf(stderr, "[WEB-DASHBOARD] Failed to allocate memory for profiles\n");
        return -1;
    }
    
    web_dashboard_cfg.start_time = time(NULL);
    web_dashboard_cfg.last_update = time(NULL);
    
    printf("[WEB-DASHBOARD] System initialized (port: %d, update: %dms)\n",
           web_dashboard_cfg.port, web_dashboard_cfg.update_interval_ms);
    
    return 0;
}

int web_dashboard_start(void) {
    if (!web_dashboard_cfg.enabled) {
        printf("[WEB-DASHBOARD] Dashboard is disabled\n");
        return 0;
    }
    
    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        fprintf(stderr, "[WEB-DASHBOARD] Failed to create socket\n");
        return -1;
    }
    
    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(web_dashboard_cfg.port);
    
    if (bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        fprintf(stderr, "[WEB-DASHBOARD] Failed to bind to port %d\n", web_dashboard_cfg.port);
        close(server_sock);
        return -1;
    }
    
    if (listen(server_sock, 5) < 0) {
        fprintf(stderr, "[WEB-DASHBOARD] Failed to listen\n");
        close(server_sock);
        return -1;
    }
    
    running = 1;
    if (pthread_create(&dashboard_thread, NULL, dashboard_server_thread, NULL) != 0) {
        fprintf(stderr, "[WEB-DASHBOARD] Failed to create server thread\n");
        close(server_sock);
        running = 0;
        return -1;
    }
    
    printf("[WEB-DASHBOARD] HTTP server started on port %d\n", web_dashboard_cfg.port);
    printf("[WEB-DASHBOARD] Access dashboard at: http://localhost:%d\n", web_dashboard_cfg.port);
    
    return 0;
}

void web_dashboard_stop(void) {
    running = 0;
    if (server_sock >= 0) {
        close(server_sock);
        server_sock = -1;
    }
    pthread_join(dashboard_thread, NULL);
    printf("[WEB-DASHBOARD] HTTP server stopped\n");
}

void web_dashboard_cleanup(void) {
    web_dashboard_stop();
    if (web_dashboard_cfg.profiles) {
        free(web_dashboard_cfg.profiles);
        web_dashboard_cfg.profiles = NULL;
    }
    printf("[WEB-DASHBOARD] System cleanup completed\n");
}

// ============================================================================
// STATISTICS UPDATE
// ============================================================================

void web_dashboard_update_stats(void) {
    pthread_mutex_lock(&dashboard_mutex);
    calculate_global_stats();
    update_uptime();
    web_dashboard_cfg.last_update = time(NULL);
    pthread_mutex_unlock(&dashboard_mutex);
}

void web_dashboard_update_profile(uint16_t caid, uint32_t provid) {
    pthread_mutex_lock(&dashboard_mutex);
    dashboard_profile_t *profile = find_or_create_profile(caid, provid);
    if (profile) {
        // Update profile-specific metrics here
    }
    pthread_mutex_unlock(&dashboard_mutex);
}

void web_dashboard_record_request(uint16_t caid, uint32_t provid, int success, int cache_hit) {
    pthread_mutex_lock(&dashboard_mutex);
    
    dashboard_stats_t *stats = &web_dashboard_cfg.global_stats;
    stats->total_requests++;
    
    if (success) {
        stats->successful_decodes++;
    } else {
        stats->failed_decodes++;
    }
    
    if (cache_hit) {
        stats->cache_hits++;
    } else {
        stats->cache_misses++;
    }
    
    dashboard_profile_t *profile = find_or_create_profile(caid, provid);
    if (profile) {
        profile->requests++;
        // Update profile rates
        if (profile->requests > 0) {
            profile->success_rate = (success ? 100.0f : 0.0f);  // Simplified
            profile->cache_hit_rate = (cache_hit ? 100.0f : 0.0f);  // Simplified
        }
    }
    
    pthread_mutex_unlock(&dashboard_mutex);
}

// ============================================================================
// HTTP HANDLERS
// ============================================================================

void http_send_dashboard(int sock, void *req) {
    char html[32768];
    int len = generate_dashboard_html(html, sizeof(html));
    
    const char *header = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n\r\n";
    send(sock, header, strlen(header), 0);
    if (len > 0) {
        send(sock, html, len, 0);
    }
}

void http_send_dashboard_api(int sock, void *req) {
    char json[8192];
    calculate_global_stats();
    dashboard_stats_t *stats = &web_dashboard_cfg.global_stats;
    
    int json_len = snprintf(json, sizeof(json),
        "{"
        "\"total_requests\":%u,"
        "\"successful_decodes\":%u,"
        "\"failed_decodes\":%u,"
        "\"cache_hits\":%u,"
        "\"cache_misses\":%u,"
        "\"black_screens\":%u,"
        "\"fake_cws\":%u,"
        "\"timeouts\":%u,"
        "\"success_rate\":%.1f,"
        "\"cache_hit_rate\":%.1f,"
        "\"timeout_rate\":%.1f,"
        "\"uptime\":%u,"
        "\"profiles\":%u"
        "}",
        stats->total_requests,
        stats->successful_decodes,
        stats->failed_decodes,
        stats->cache_hits,
        stats->cache_misses,
        stats->black_screens,
        stats->fake_cws_detected,
        stats->timeouts,
        stats->success_rate,
        stats->cache_hit_rate,
        stats->timeout_rate,
        web_dashboard_cfg.uptime_seconds,
        web_dashboard_cfg.profile_count
    );
    
    const char *header = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\n\r\n";
    send(sock, header, strlen(header), 0);
    send(sock, json, json_len, 0);
}

void http_send_dashboard_profiles(int sock, void *req) {
    // Implementation for profiles API
}

void http_send_dashboard_health(int sock, void *req) {
    // Implementation for health API
}

// ============================================================================
// QUERY FUNCTIONS
// ============================================================================

void web_dashboard_get_stats(dashboard_stats_t *stats) {
    pthread_mutex_lock(&dashboard_mutex);
    memcpy(stats, &web_dashboard_cfg.global_stats, sizeof(dashboard_stats_t));
    pthread_mutex_unlock(&dashboard_mutex);
}

uint32_t web_dashboard_get_uptime(void) {
    update_uptime();
    return web_dashboard_cfg.uptime_seconds;
}

int web_dashboard_is_enabled(void) {
    return web_dashboard_cfg.enabled;
}

void web_dashboard_set_enabled(int enabled) {
    web_dashboard_cfg.enabled = enabled ? 1 : 0;
}
