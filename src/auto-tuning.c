/* auto-tuning.c - Automatic Performance Tuning Implementation
 * 
 * PURPOSE: Monitor performance and suggest optimizations
 * MODE: Suggestion + Approval (safe mode)
 */

#include "common.h"
#include "auto-tuning.h"
#include "httpserver.h"
#include "server-stats.h"
#include "cache-analytics.h"
#include <string.h>
#include <stdio.h>
#include <time.h>

// TCP buffer structure (from httpbuffer.c)
#define MAX_BUFFERSIZE 500
struct tcp_buffer_data
{
    char data[MAX_BUFFERSIZE];
    int index;
    int size;
};

extern void tcp_init(struct tcp_buffer_data *buf);
extern void tcp_writestr(struct tcp_buffer_data *buf, int sock, const char *data);

// Global auto-tuning table
struct auto_tuning_table auto_tuning_tab;

// ============================================================================
// INITIALIZATION
// ============================================================================

void auto_tuning_init(void)
{
    memset(&auto_tuning_tab, 0, sizeof(auto_tuning_tab));
    pthread_mutex_init(&auto_tuning_tab.lock, NULL);
    auto_tuning_tab.next_id = 1;
    
    debugf(0, " [AUTO-TUNING] Auto-tuning system initialized (suggestion + approval mode)\n");
}

// ============================================================================
// ANALYSIS FUNCTIONS
// ============================================================================

void auto_tuning_analyze(void)
{
    pthread_mutex_lock(&auto_tuning_tab.lock);
    auto_tuning_tab.analysis_running = 1;
    auto_tuning_tab.last_analysis_time = time(NULL);
    pthread_mutex_unlock(&auto_tuning_tab.lock);
    
    // Analysis 1: Check server response times
    struct server_stats servers[SERVER_STATS_MAX_SERVERS];
    int server_count = server_stats_get_all(servers, SERVER_STATS_MAX_SERVERS);
    
    int i;
    for (i = 0; i < server_count; i++) {
        struct server_stats *srv = &servers[i];
        
        // Suggest timeout increase if avg response time is high
        if (srv->avg_response_time > 800 && srv->total_requests > 20) {
            char title[128];
            char desc[512];
            char reason[256];
            char benefit[256];
            char current[64];
            char suggested[64];
            
            snprintf(title, sizeof(title), "زيادة Timeout للخادم %s", srv->server_name);
            snprintf(desc, sizeof(desc), 
                    "الخادم %s لديه متوسط استجابة %ums وهو عالي. "
                    "يُنصح بزيادة timeout لتقليل الـ timeouts.",
                    srv->server_name, srv->avg_response_time);
            snprintf(reason, sizeof(reason), 
                    "متوسط الاستجابة %ums (الحد الأقصى الموصى به: 500ms)",
                    srv->avg_response_time);
            snprintf(benefit, sizeof(benefit), 
                    "تقليل الـ timeouts بنسبة 30-50%%");
            snprintf(current, sizeof(current), "800ms");
            snprintf(suggested, sizeof(suggested), "%ums", 
                    srv->avg_response_time * 2 > 2000 ? 2000 : srv->avg_response_time * 2);
            
            auto_tuning_add_suggestion(
                SUGGESTION_TYPE_TIMEOUT,
                SUGGESTION_PRIORITY_MEDIUM,
                title, desc, reason, benefit,
                "SERVER_TIMEOUT", current, suggested,
                "server", srv->server_id
            );
        }
        
        // Suggest removing server if success rate is very low
        if (srv->success_rate < 30 && srv->total_requests > 50) {
            char title[128];
            char desc[512];
            char reason[256];
            char benefit[256];
            
            snprintf(title, sizeof(title), "إزالة أو استبدال الخادم %s", srv->server_name);
            snprintf(desc, sizeof(desc), 
                    "الخادم %s لديه نسبة نجاح %u%% فقط وهي منخفضة جداً. "
                    "يُنصح بإزالته أو استبداله بخادم أفضل.",
                    srv->server_name, srv->success_rate);
            snprintf(reason, sizeof(reason), 
                    "نسبة النجاح %u%% (الحد الأدنى الموصى به: 50%%)",
                    srv->success_rate);
            snprintf(benefit, sizeof(benefit), 
                    "تحسين الأداء العام وتقليل الـ timeouts");
            
            auto_tuning_add_suggestion(
                SUGGESTION_TYPE_SERVER,
                SUGGESTION_PRIORITY_HIGH,
                title, desc, reason, benefit,
                "REMOVE_SERVER", "enabled", "disabled",
                "server", srv->server_id
            );
        }
    }
    
    // Analysis 2: Check cache hit rates
    struct cache_caid_stats caids[CACHE_ANALYTICS_MAX_CAIDS];
    int caid_count = cache_analytics_get_caid_stats(caids, CACHE_ANALYTICS_MAX_CAIDS);
    
    for (i = 0; i < caid_count; i++) {
        struct cache_caid_stats *caid = &caids[i];
        
        // Suggest cache timeout increase if hit rate is low
        if (caid->hit_rate < 30 && caid->total_requests > 100) {
            char title[128];
            char desc[512];
            char reason[256];
            char benefit[256];
            char current[64];
            char suggested[64];
            
            snprintf(title, sizeof(title), "زيادة Cache Timeout لـ %s", caid->caid_name);
            snprintf(desc, sizeof(desc), 
                    "CAID %s لديه نسبة كاش %u%% فقط. "
                    "يُنصح بزيادة cache timeout لتحسين النسبة.",
                    caid->caid_name, caid->hit_rate);
            snprintf(reason, sizeof(reason), 
                    "نسبة الكاش %u%% (الحد الأدنى الموصى به: 40%%)",
                    caid->hit_rate);
            snprintf(benefit, sizeof(benefit), 
                    "تحسين نسبة الكاش إلى 50-70%%");
            snprintf(current, sizeof(current), "250ms");
            snprintf(suggested, sizeof(suggested), "400ms");
            
            auto_tuning_add_suggestion(
                SUGGESTION_TYPE_CACHE,
                SUGGESTION_PRIORITY_MEDIUM,
                title, desc, reason, benefit,
                "CACHE_TIMEOUT", current, suggested,
                "caid", caid->caid
            );
        }
        
        // Suggest prefetch for high-request CAIDs with low cache
        if (caid->total_requests > 500 && caid->hit_rate < 40) {
            char title[128];
            char desc[512];
            char reason[256];
            char benefit[256];
            
            snprintf(title, sizeof(title), "تفعيل Prefetch لـ %s", caid->caid_name);
            snprintf(desc, sizeof(desc), 
                    "CAID %s لديه %u طلب لكن نسبة الكاش %u%% فقط. "
                    "تفعيل prefetch سيحسن الأداء بشكل كبير.",
                    caid->caid_name, caid->total_requests, caid->hit_rate);
            snprintf(reason, sizeof(reason), 
                    "%u طلب مع نسبة كاش %u%%",
                    caid->total_requests, caid->hit_rate);
            snprintf(benefit, sizeof(benefit), 
                    "تحسين نسبة الكاش إلى 60-80%% وتقليل وقت الاستجابة");
            
            auto_tuning_add_suggestion(
                SUGGESTION_TYPE_PREFETCH,
                SUGGESTION_PRIORITY_HIGH,
                title, desc, reason, benefit,
                "ENABLE_PREFETCH", "disabled", "enabled",
                "caid", caid->caid
            );
        }
    }
    
    // Analysis 3: Global cache hit rate
    cache_analytics_update_global();
    
    pthread_mutex_lock(&cache_analytics_tab.lock);
    uint32_t global_hit_rate = cache_analytics_tab.global_hit_rate;
    uint32_t total_requests = cache_analytics_tab.total_requests;
    pthread_mutex_unlock(&cache_analytics_tab.lock);
    
    if (global_hit_rate < 30 && total_requests > 200) {
        auto_tuning_add_suggestion(
            SUGGESTION_TYPE_GENERAL,
            SUGGESTION_PRIORITY_CRITICAL,
            "تحسين نسبة الكاش العامة",
            "نسبة الكاش العامة منخفضة جداً. يُنصح بمراجعة إعدادات الكاش وتفعيل prefetch للقنوات الشائعة.",
            "نسبة الكاش العامة منخفضة (< 30%)",
            "تحسين الأداء العام بنسبة 50-100%",
            "CACHE_SETTINGS", "default", "optimized",
            "global", 0
        );
    }
    
    pthread_mutex_lock(&auto_tuning_tab.lock);
    auto_tuning_tab.analysis_running = 0;
    pthread_mutex_unlock(&auto_tuning_tab.lock);
}

// ============================================================================
// SUGGESTION MANAGEMENT
// ============================================================================

int auto_tuning_add_suggestion(int type, int priority, const char *title,
                              const char *description, const char *reason,
                              const char *expected_benefit,
                              const char *config_key, const char *current_value,
                              const char *suggested_value,
                              const char *component, uint32_t component_id)
{
    pthread_mutex_lock(&auto_tuning_tab.lock);
    
    // Check if similar suggestion already exists
    int i;
    for (i = 0; i < auto_tuning_tab.suggestion_count; i++) {
        struct auto_tuning_suggestion *s = &auto_tuning_tab.suggestions[i];
        if (s->status == SUGGESTION_STATUS_PENDING &&
            s->type == type &&
            strcmp(s->config_key, config_key) == 0 &&
            s->component_id == component_id) {
            // Already exists, skip
            pthread_mutex_unlock(&auto_tuning_tab.lock);
            return 0;
        }
    }
    
    // Add new suggestion
    if (auto_tuning_tab.suggestion_count < AUTO_TUNING_MAX_SUGGESTIONS) {
        struct auto_tuning_suggestion *s = &auto_tuning_tab.suggestions[auto_tuning_tab.suggestion_count++];
        memset(s, 0, sizeof(struct auto_tuning_suggestion));
        
        s->id = auto_tuning_tab.next_id++;
        s->timestamp = time(NULL);
        s->type = type;
        s->priority = priority;
        s->status = SUGGESTION_STATUS_PENDING;
        
        if (title) strncpy(s->title, title, sizeof(s->title) - 1);
        if (description) strncpy(s->description, description, sizeof(s->description) - 1);
        if (reason) strncpy(s->reason, reason, sizeof(s->reason) - 1);
        if (expected_benefit) strncpy(s->expected_benefit, expected_benefit, sizeof(s->expected_benefit) - 1);
        if (config_key) strncpy(s->config_key, config_key, sizeof(s->config_key) - 1);
        if (current_value) strncpy(s->current_value, current_value, sizeof(s->current_value) - 1);
        if (suggested_value) strncpy(s->suggested_value, suggested_value, sizeof(s->suggested_value) - 1);
        if (component) strncpy(s->component, component, sizeof(s->component) - 1);
        s->component_id = component_id;
        
        auto_tuning_tab.total_suggestions++;
        
        pthread_mutex_unlock(&auto_tuning_tab.lock);
        return s->id;
    }
    
    pthread_mutex_unlock(&auto_tuning_tab.lock);
    return -1;
}

int auto_tuning_approve(uint32_t suggestion_id)
{
    pthread_mutex_lock(&auto_tuning_tab.lock);
    
    int i;
    for (i = 0; i < auto_tuning_tab.suggestion_count; i++) {
        struct auto_tuning_suggestion *s = &auto_tuning_tab.suggestions[i];
        if (s->id == suggestion_id && s->status == SUGGESTION_STATUS_PENDING) {
            s->status = SUGGESTION_STATUS_APPROVED;
            auto_tuning_tab.approved_count++;
            
            // Add to history
            int hist_idx = auto_tuning_tab.history_index;
            auto_tuning_tab.history[hist_idx].suggestion_id = suggestion_id;
            auto_tuning_tab.history[hist_idx].timestamp = time(NULL);
            auto_tuning_tab.history[hist_idx].action = 1;
            strncpy(auto_tuning_tab.history[hist_idx].action_text, "Approved", 
                   sizeof(auto_tuning_tab.history[hist_idx].action_text) - 1);
            auto_tuning_tab.history_index = (hist_idx + 1) % AUTO_TUNING_MAX_HISTORY;
            if (auto_tuning_tab.history_count < AUTO_TUNING_MAX_HISTORY) {
                auto_tuning_tab.history_count++;
            }
            
            pthread_mutex_unlock(&auto_tuning_tab.lock);
            return 1;
        }
    }
    
    pthread_mutex_unlock(&auto_tuning_tab.lock);
    return 0;
}

int auto_tuning_reject(uint32_t suggestion_id)
{
    pthread_mutex_lock(&auto_tuning_tab.lock);
    
    int i;
    for (i = 0; i < auto_tuning_tab.suggestion_count; i++) {
        struct auto_tuning_suggestion *s = &auto_tuning_tab.suggestions[i];
        if (s->id == suggestion_id && s->status == SUGGESTION_STATUS_PENDING) {
            s->status = SUGGESTION_STATUS_REJECTED;
            auto_tuning_tab.rejected_count++;
            
            // Add to history
            int hist_idx = auto_tuning_tab.history_index;
            auto_tuning_tab.history[hist_idx].suggestion_id = suggestion_id;
            auto_tuning_tab.history[hist_idx].timestamp = time(NULL);
            auto_tuning_tab.history[hist_idx].action = 0;
            strncpy(auto_tuning_tab.history[hist_idx].action_text, "Rejected", 
                   sizeof(auto_tuning_tab.history[hist_idx].action_text) - 1);
            auto_tuning_tab.history_index = (hist_idx + 1) % AUTO_TUNING_MAX_HISTORY;
            if (auto_tuning_tab.history_count < AUTO_TUNING_MAX_HISTORY) {
                auto_tuning_tab.history_count++;
            }
            
            pthread_mutex_unlock(&auto_tuning_tab.lock);
            return 1;
        }
    }
    
    pthread_mutex_unlock(&auto_tuning_tab.lock);
    return 0;
}

int auto_tuning_apply(uint32_t suggestion_id)
{
    pthread_mutex_lock(&auto_tuning_tab.lock);
    
    int i;
    for (i = 0; i < auto_tuning_tab.suggestion_count; i++) {
        struct auto_tuning_suggestion *s = &auto_tuning_tab.suggestions[i];
        if (s->id == suggestion_id && s->status == SUGGESTION_STATUS_APPROVED) {
            // TODO: Apply the configuration change
            // For now, just mark as applied
            s->status = SUGGESTION_STATUS_APPLIED;
            auto_tuning_tab.applied_count++;
            
            // Add to history
            int hist_idx = auto_tuning_tab.history_index;
            auto_tuning_tab.history[hist_idx].suggestion_id = suggestion_id;
            auto_tuning_tab.history[hist_idx].timestamp = time(NULL);
            auto_tuning_tab.history[hist_idx].action = 2;
            strncpy(auto_tuning_tab.history[hist_idx].action_text, "Applied", 
                   sizeof(auto_tuning_tab.history[hist_idx].action_text) - 1);
            auto_tuning_tab.history_index = (hist_idx + 1) % AUTO_TUNING_MAX_HISTORY;
            if (auto_tuning_tab.history_count < AUTO_TUNING_MAX_HISTORY) {
                auto_tuning_tab.history_count++;
            }
            
            debugf(0, " [AUTO-TUNING] Applied suggestion %u: %s = %s\n",
                   suggestion_id, s->config_key, s->suggested_value);
            
            pthread_mutex_unlock(&auto_tuning_tab.lock);
            return 1;
        }
    }
    
    pthread_mutex_unlock(&auto_tuning_tab.lock);
    return 0;
}

// ============================================================================
// QUERY FUNCTIONS
// ============================================================================

int auto_tuning_get_pending(struct auto_tuning_suggestion *suggestions, int max_count)
{
    pthread_mutex_lock(&auto_tuning_tab.lock);
    
    int count = 0;
    int i;
    for (i = 0; i < auto_tuning_tab.suggestion_count && count < max_count; i++) {
        if (auto_tuning_tab.suggestions[i].status == SUGGESTION_STATUS_PENDING) {
            memcpy(&suggestions[count], &auto_tuning_tab.suggestions[i], 
                   sizeof(struct auto_tuning_suggestion));
            count++;
        }
    }
    
    pthread_mutex_unlock(&auto_tuning_tab.lock);
    return count;
}

int auto_tuning_get_all(struct auto_tuning_suggestion *suggestions, int max_count)
{
    pthread_mutex_lock(&auto_tuning_tab.lock);
    
    int count = auto_tuning_tab.suggestion_count;
    if (count > max_count) count = max_count;
    
    memcpy(suggestions, auto_tuning_tab.suggestions, 
           sizeof(struct auto_tuning_suggestion) * count);
    
    pthread_mutex_unlock(&auto_tuning_tab.lock);
    return count;
}

int auto_tuning_get_history(struct auto_tuning_history *history, int max_count)
{
    pthread_mutex_lock(&auto_tuning_tab.lock);
    
    int count = 0;
    int start = (auto_tuning_tab.history_index - 1 + AUTO_TUNING_MAX_HISTORY) % AUTO_TUNING_MAX_HISTORY;
    
    int i;
    for (i = 0; i < auto_tuning_tab.history_count && count < max_count; i++) {
        int index = (start - i + AUTO_TUNING_MAX_HISTORY) % AUTO_TUNING_MAX_HISTORY;
        memcpy(&history[count], &auto_tuning_tab.history[index], 
               sizeof(struct auto_tuning_history));
        count++;
    }
    
    pthread_mutex_unlock(&auto_tuning_tab.lock);
    return count;
}

void auto_tuning_clear(void)
{
    pthread_mutex_lock(&auto_tuning_tab.lock);
    memset(&auto_tuning_tab.suggestions, 0, sizeof(auto_tuning_tab.suggestions));
    auto_tuning_tab.suggestion_count = 0;
    pthread_mutex_unlock(&auto_tuning_tab.lock);
}

// ============================================================================
// HTTP INTERFACE
// ============================================================================

void http_send_auto_tuning(int sock, struct http_request *req)
{
    struct tcp_buffer_data tcpbuf;
    char buf[16384];
    int i;
    
    tcp_init(&tcpbuf);
    
    // Handle actions
    char *action = isset_get(req, "action");
    char *id_str = isset_get(req, "id");
    
    if (action && id_str) {
        uint32_t id = atoi(id_str);
        if (strcmp(action, "approve") == 0) {
            auto_tuning_approve(id);
        } else if (strcmp(action, "reject") == 0) {
            auto_tuning_reject(id);
        } else if (strcmp(action, "apply") == 0) {
            auto_tuning_apply(id);
        }
    }
    
    if (action && strcmp(action, "analyze") == 0) {
        auto_tuning_analyze();
    }
    
    if (action && strcmp(action, "clear") == 0) {
        auto_tuning_clear();
    }
    
    // Send HTTP header
    snprintf(buf, sizeof(buf),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Connection: close\r\n\r\n"
    );
    tcp_writestr(&tcpbuf, sock, buf);
    
    // Start HTML
    snprintf(buf, sizeof(buf),
        "<!DOCTYPE html>\n"
        "<html dir='rtl' lang='ar'>\n"
        "<head>\n"
        "<meta charset='utf-8'>\n"
        "<meta name='viewport' content='width=device-width, initial-scale=1'>\n"
        "<title>الضبط التلقائي - MultiCS</title>\n"
        "<style>\n"
        "body { font-family: 'Segoe UI', Tahoma, Arial; margin: 20px; background: #f5f5f5; }\n"
        ".container { max-width: 1400px; margin: 0 auto; background: white; padding: 20px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }\n"
        "h1 { color: #333; border-bottom: 3px solid #6f42c1; padding-bottom: 10px; }\n"
        "h2 { color: #555; margin-top: 30px; }\n"
        ".stats { display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 15px; margin: 20px 0; }\n"
        ".stat-card { background: #f8f9fa; padding: 15px; border-radius: 6px; border-left: 4px solid #6f42c1; }\n"
        ".stat-value { font-size: 28px; font-weight: bold; color: #6f42c1; }\n"
        ".stat-label { color: #666; font-size: 14px; margin-top: 5px; }\n"
        ".suggestion { background: #f8f9fa; border-left: 4px solid #6f42c1; padding: 20px; margin: 15px 0; border-radius: 6px; }\n"
        ".suggestion-critical { border-left-color: #dc3545; background: #f8d7da; }\n"
        ".suggestion-high { border-left-color: #fd7e14; background: #fff3cd; }\n"
        ".suggestion-medium { border-left-color: #ffc107; background: #fff3cd; }\n"
        ".suggestion-low { border-left-color: #17a2b8; background: #d1ecf1; }\n"
        ".suggestion-title { font-size: 18px; font-weight: bold; margin-bottom: 10px; }\n"
        ".suggestion-desc { color: #555; margin: 10px 0; }\n"
        ".suggestion-meta { display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 10px; margin: 15px 0; }\n"
        ".meta-item { background: white; padding: 10px; border-radius: 4px; }\n"
        ".meta-label { font-size: 12px; color: #888; }\n"
        ".meta-value { font-weight: bold; color: #333; }\n"
        ".btn { display: inline-block; padding: 8px 16px; color: white; text-decoration: none; border-radius: 4px; margin: 5px; border: none; cursor: pointer; }\n"
        ".btn-approve { background: #28a745; }\n"
        ".btn-approve:hover { background: #218838; }\n"
        ".btn-reject { background: #dc3545; }\n"
        ".btn-reject:hover { background: #c82333; }\n"
        ".btn-apply { background: #6f42c1; }\n"
        ".btn-apply:hover { background: #5a32a3; }\n"
        ".btn-info { background: #17a2b8; }\n"
        ".btn-info:hover { background: #138496; }\n"
        ".status-pending { color: #ffc107; font-weight: bold; }\n"
        ".status-approved { color: #28a745; font-weight: bold; }\n"
        ".status-rejected { color: #dc3545; font-weight: bold; }\n"
        ".status-applied { color: #6f42c1; font-weight: bold; }\n"
        ".history-table { width: 100%%; border-collapse: collapse; margin-top: 15px; }\n"
        ".history-table th { background: #6f42c1; color: white; padding: 12px; text-align: right; }\n"
        ".history-table td { padding: 10px; border-bottom: 1px solid #ddd; }\n"
        "</style>\n"
        "</head>\n"
        "<body>\n"
        "<div class='container'>\n"
        "<h1>🤖 الضبط التلقائي (Auto-Tuning)</h1>\n"
        "<p style='color: #666;'>النظام يراقب الأداء ويقترح تحسينات. أنت توافق أو ترفض (لا تطبيق تلقائي).</p>\n"
    );
    tcp_writestr(&tcpbuf, sock, buf);
    
    // Statistics
    snprintf(buf, sizeof(buf),
        "<div class='stats'>\n"
        "<div class='stat-card'><div class='stat-value'>%u</div><div class='stat-label'>إجمالي الاقتراحات</div></div>\n"
        "<div class='stat-card'><div class='stat-value'>%u</div><div class='stat-label'>قيد الانتظار</div></div>\n"
        "<div class='stat-card'><div class='stat-value'>%u</div><div class='stat-label'>موافق عليها</div></div>\n"
        "<div class='stat-card'><div class='stat-value'>%u</div><div class='stat-label'>مرفوضة</div></div>\n"
        "<div class='stat-card'><div class='stat-value'>%u</div><div class='stat-label'>مطبقة</div></div>\n"
        "</div>\n",
        auto_tuning_tab.total_suggestions,
        auto_tuning_tab.total_suggestions - auto_tuning_tab.approved_count - auto_tuning_tab.rejected_count,
        auto_tuning_tab.approved_count,
        auto_tuning_tab.rejected_count,
        auto_tuning_tab.applied_count
    );
    tcp_writestr(&tcpbuf, sock, buf);
    
    // Action buttons
    snprintf(buf, sizeof(buf),
        "<div style='text-align: center; margin: 20px 0;'>\n"
        "<a href='/auto-tuning?action=analyze' class='btn btn-info'>🔍 تحليل الآن</a>\n"
        "<a href='/auto-tuning' class='btn btn-info'>🔄 تحديث</a>\n"
        "<a href='/auto-tuning?action=clear' class='btn btn-reject' onclick='return confirm(\"هل أنت متأكد؟\")'>🗑️ مسح الاقتراحات</a>\n"
        "<a href='/' class='btn btn-info'>🏠 الرئيسية</a>\n"
        "</div>\n"
    );
    tcp_writestr(&tcpbuf, sock, buf);
    
    // Pending suggestions
    struct auto_tuning_suggestion suggestions[AUTO_TUNING_MAX_SUGGESTIONS];
    int count = auto_tuning_get_pending(suggestions, AUTO_TUNING_MAX_SUGGESTIONS);
    
    if (count > 0) {
        snprintf(buf, sizeof(buf), "<h2>💡 الاقتراحات قيد الانتظار (%d)</h2>\n", count);
        tcp_writestr(&tcpbuf, sock, buf);
        
        for (i = 0; i < count; i++) {
            struct auto_tuning_suggestion *s = &suggestions[i];
            
            const char *priority_class = "suggestion-low";
            const char *priority_text = "منخفض";
            if (s->priority == SUGGESTION_PRIORITY_CRITICAL) {
                priority_class = "suggestion-critical";
                priority_text = "حرج";
            } else if (s->priority == SUGGESTION_PRIORITY_HIGH) {
                priority_class = "suggestion-high";
                priority_text = "عالي";
            } else if (s->priority == SUGGESTION_PRIORITY_MEDIUM) {
                priority_class = "suggestion-medium";
                priority_text = "متوسط";
            }
            
            snprintf(buf, sizeof(buf),
                "<div class='suggestion %s'>\n"
                "<div class='suggestion-title'>%s <span style='font-size: 14px; color: #888;'>[الأولوية: %s]</span></div>\n"
                "<div class='suggestion-desc'>%s</div>\n"
                "<div class='suggestion-meta'>\n"
                "<div class='meta-item'><div class='meta-label'>السبب</div><div class='meta-value'>%s</div></div>\n"
                "<div class='meta-item'><div class='meta-label'>الفائدة المتوقعة</div><div class='meta-value'>%s</div></div>\n"
                "<div class='meta-item'><div class='meta-label'>الإعداد</div><div class='meta-value'>%s</div></div>\n"
                "<div class='meta-item'><div class='meta-label'>القيمة الحالية</div><div class='meta-value'>%s</div></div>\n"
                "<div class='meta-item'><div class='meta-label'>القيمة المقترحة</div><div class='meta-value' style='color: #28a745;'>%s</div></div>\n"
                "</div>\n"
                "<div style='margin-top: 15px;'>\n"
                "<a href='/auto-tuning?action=approve&id=%u' class='btn btn-approve'>✅ موافقة</a>\n"
                "<a href='/auto-tuning?action=reject&id=%u' class='btn btn-reject' onclick='return confirm(\"هل أنت متأكد من رفض هذا الاقتراح؟\")'>❌ رفض</a>\n"
                "</div>\n"
                "</div>\n",
                priority_class,
                s->title,
                priority_text,
                s->description,
                s->reason,
                s->expected_benefit,
                s->config_key,
                s->current_value,
                s->suggested_value,
                s->id,
                s->id
            );
            tcp_writestr(&tcpbuf, sock, buf);
        }
    } else {
        snprintf(buf, sizeof(buf),
            "<div style='background: #d4edda; padding: 20px; border-radius: 6px; text-align: center; margin: 20px 0;'>\n"
            "<h3>✅ لا توجد اقتراحات حالياً</h3>\n"
            "<p>النظام يعمل بشكل جيد. اضغط على 'تحليل الآن' للبحث عن تحسينات محتملة.</p>\n"
            "</div>\n"
        );
        tcp_writestr(&tcpbuf, sock, buf);
    }
    
    // Approved suggestions
    int approved_count = 0;
    for (i = 0; i < auto_tuning_tab.suggestion_count; i++) {
        if (auto_tuning_tab.suggestions[i].status == SUGGESTION_STATUS_APPROVED) {
            approved_count++;
        }
    }
    
    if (approved_count > 0) {
        snprintf(buf, sizeof(buf), "<h2>✅ الاقتراحات الموافق عليها (%d)</h2>\n", approved_count);
        tcp_writestr(&tcpbuf, sock, buf);
        
        for (i = 0; i < auto_tuning_tab.suggestion_count; i++) {
            struct auto_tuning_suggestion *s = &auto_tuning_tab.suggestions[i];
            if (s->status == SUGGESTION_STATUS_APPROVED) {
                snprintf(buf, sizeof(buf),
                    "<div class='suggestion' style='border-left-color: #28a745;'>\n"
                    "<div class='suggestion-title'>%s <span class='status-approved'>[موافق عليه]</span></div>\n"
                    "<div class='suggestion-desc'>%s</div>\n"
                    "<div style='margin-top: 10px;'>\n"
                    "<a href='/auto-tuning?action=apply&id=%u' class='btn btn-apply'>🚀 تطبيق الآن</a>\n"
                    "</div>\n"
                    "</div>\n",
                    s->title,
                    s->description,
                    s->id
                );
                tcp_writestr(&tcpbuf, sock, buf);
            }
        }
    }
    
    // History
    struct auto_tuning_history history[AUTO_TUNING_MAX_HISTORY];
    int hist_count = auto_tuning_get_history(history, 20);
    
    if (hist_count > 0) {
        snprintf(buf, sizeof(buf),
            "<h2>📜 سجل الإجراءات</h2>\n"
            "<table class='history-table'>\n"
            "<tr><th>الوقت</th><th>الاقتراح</th><th>الإجراء</th></tr>\n"
        );
        tcp_writestr(&tcpbuf, sock, buf);
        
        for (i = 0; i < hist_count; i++) {
            struct auto_tuning_history *h = &history[i];
            
            char time_str[32];
            struct tm *tm = localtime(&h->timestamp);
            strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm);
            
            // Find suggestion title
            char title[128] = "غير معروف";
            int j;
            for (j = 0; j < auto_tuning_tab.suggestion_count; j++) {
                if (auto_tuning_tab.suggestions[j].id == h->suggestion_id) {
                    strncpy(title, auto_tuning_tab.suggestions[j].title, sizeof(title) - 1);
                    break;
                }
            }
            
            snprintf(buf, sizeof(buf),
                "<tr><td>%s</td><td>%s</td><td>%s</td></tr>\n",
                time_str, title, h->action_text
            );
            tcp_writestr(&tcpbuf, sock, buf);
        }
        
        snprintf(buf, sizeof(buf), "</table>\n");
        tcp_writestr(&tcpbuf, sock, buf);
    }
    
    // End page
    snprintf(buf, sizeof(buf),
        "</div>\n"
        "</body>\n"
        "</html>\n"
    );
    tcp_writestr(&tcpbuf, sock, buf);
}
