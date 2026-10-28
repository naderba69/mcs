/* server-stats.c - Detailed Server Statistics Implementation
 * 
 * PURPOSE: Show comprehensive statistics for each server
 */

#include "common.h"
#include "debug.h"
#include "server-stats.h"
#include "httpserver.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
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

// Global server stats table
struct server_stats_table server_stats_tab;

// ============================================================================
// INITIALIZATION
// ============================================================================

void server_stats_init(void)
{
    memset(&server_stats_tab, 0, sizeof(server_stats_tab));
    pthread_mutex_init(&server_stats_tab.lock, NULL);
    
    debugf(0, " [SERVER-STATS] Server statistics system initialized (max %d servers)\n", 
           SERVER_STATS_MAX_SERVERS);
}

// ============================================================================
// RECORD FUNCTIONS
// ============================================================================

static struct server_stats* find_or_create_server(uint32_t server_id, const char *server_name,
                                                  const char *server_host, uint16_t server_port)
{
    // Find existing
    int i;
    for (i = 0; i < server_stats_tab.count; i++) {
        if (server_stats_tab.servers[i].server_id == server_id) {
            return &server_stats_tab.servers[i];
        }
    }
    
    // Create new
    if (server_stats_tab.count < SERVER_STATS_MAX_SERVERS) {
        struct server_stats *stats = &server_stats_tab.servers[server_stats_tab.count++];
        memset(stats, 0, sizeof(struct server_stats));
        stats->server_id = server_id;
        stats->min_response_time = 999999;
        if (server_name) strncpy(stats->server_name, server_name, sizeof(stats->server_name) - 1);
        if (server_host) strncpy(stats->server_host, server_host, sizeof(stats->server_host) - 1);
        stats->server_port = server_port;
        server_stats_tab.total_servers++;
        return stats;
    }
    
    return NULL;
}

void server_stats_record_request(uint32_t server_id, const char *server_name,
                                const char *server_host, uint16_t server_port)
{
    pthread_mutex_lock(&server_stats_tab.lock);
    
    struct server_stats *stats = find_or_create_server(server_id, server_name, server_host, server_port);
    if (stats) {
        stats->total_requests++;
        stats->last_request_time = time(NULL);
    }
    
    pthread_mutex_unlock(&server_stats_tab.lock);
}

void server_stats_record_success(uint32_t server_id, uint32_t response_time)
{
    pthread_mutex_lock(&server_stats_tab.lock);
    
    int i;
    for (i = 0; i < server_stats_tab.count; i++) {
        if (server_stats_tab.servers[i].server_id == server_id) {
            struct server_stats *stats = &server_stats_tab.servers[i];
            stats->successful_requests++;
            stats->total_response_time += response_time;
            stats->last_success_time = time(NULL);
            
            // Update min/max
            if (response_time < stats->min_response_time) stats->min_response_time = response_time;
            if (response_time > stats->max_response_time) stats->max_response_time = response_time;
            
            // Calculate average
            if (stats->successful_requests > 0) {
                stats->avg_response_time = stats->total_response_time / stats->successful_requests;
            }
            
            // Calculate success rate
            if (stats->total_requests > 0) {
                stats->success_rate = (stats->successful_requests * 100) / stats->total_requests;
            }
            
            // Calculate reliability
            server_stats_calculate_reliability(stats);
            
            break;
        }
    }
    
    pthread_mutex_unlock(&server_stats_tab.lock);
}

void server_stats_record_failure(uint32_t server_id)
{
    pthread_mutex_lock(&server_stats_tab.lock);
    
    int i;
    for (i = 0; i < server_stats_tab.count; i++) {
        if (server_stats_tab.servers[i].server_id == server_id) {
            struct server_stats *stats = &server_stats_tab.servers[i];
            stats->failed_requests++;
            stats->last_failure_time = time(NULL);
            
            // Calculate success rate
            if (stats->total_requests > 0) {
                stats->success_rate = (stats->successful_requests * 100) / stats->total_requests;
            }
            
            // Calculate reliability
            server_stats_calculate_reliability(stats);
            
            break;
        }
    }
    
    pthread_mutex_unlock(&server_stats_tab.lock);
}

void server_stats_record_timeout(uint32_t server_id)
{
    pthread_mutex_lock(&server_stats_tab.lock);
    
    int i;
    for (i = 0; i < server_stats_tab.count; i++) {
        if (server_stats_tab.servers[i].server_id == server_id) {
            struct server_stats *stats = &server_stats_tab.servers[i];
            stats->timeout_requests++;
            stats->last_failure_time = time(NULL);
            
            // Calculate success rate
            if (stats->total_requests > 0) {
                stats->success_rate = (stats->successful_requests * 100) / stats->total_requests;
            }
            
            // Calculate reliability
            server_stats_calculate_reliability(stats);
            
            break;
        }
    }
    
    pthread_mutex_unlock(&server_stats_tab.lock);
}

void server_stats_update_status(uint32_t server_id, int is_connected)
{
    pthread_mutex_lock(&server_stats_tab.lock);
    
    int i;
    for (i = 0; i < server_stats_tab.count; i++) {
        if (server_stats_tab.servers[i].server_id == server_id) {
            struct server_stats *stats = &server_stats_tab.servers[i];
            int was_connected = stats->is_connected;
            stats->is_connected = is_connected;
            
            // Update global counters
            if (was_connected && !is_connected) {
                server_stats_tab.connected_servers--;
                server_stats_tab.disconnected_servers++;
            } else if (!was_connected && is_connected) {
                server_stats_tab.connected_servers++;
                server_stats_tab.disconnected_servers--;
            }
            
            // Update status text
            if (is_connected) {
                strcpy(stats->status_text, "متصل");
            } else {
                strcpy(stats->status_text, "مفصول");
            }
            
            break;
        }
    }
    
    pthread_mutex_unlock(&server_stats_tab.lock);
}

// ============================================================================
// QUERY FUNCTIONS
// ============================================================================

struct server_stats* server_stats_get(uint32_t server_id)
{
    pthread_mutex_lock(&server_stats_tab.lock);
    
    int i;
    for (i = 0; i < server_stats_tab.count; i++) {
        if (server_stats_tab.servers[i].server_id == server_id) {
            pthread_mutex_unlock(&server_stats_tab.lock);
            return &server_stats_tab.servers[i];
        }
    }
    
    pthread_mutex_unlock(&server_stats_tab.lock);
    return NULL;
}

int server_stats_get_all(struct server_stats *servers, int max_count)
{
    pthread_mutex_lock(&server_stats_tab.lock);
    
    int count = 0;
    int i;
    for (i = 0; i < server_stats_tab.count && count < max_count; i++) {
        memcpy(&servers[count], &server_stats_tab.servers[i], sizeof(struct server_stats));
        count++;
    }
    
    pthread_mutex_unlock(&server_stats_tab.lock);
    return count;
}

// Comparison function for sorting by success rate (descending)
static int compare_success_rate_desc(const void *a, const void *b)
{
    const struct server_stats *sa = (const struct server_stats *)a;
    const struct server_stats *sb = (const struct server_stats *)b;
    return (int)sb->success_rate - (int)sa->success_rate;
}

int server_stats_get_top(struct server_stats *servers, int max_count)
{
    pthread_mutex_lock(&server_stats_tab.lock);
    
    // Copy all servers
    int count = server_stats_tab.count;
    if (count > max_count) count = max_count;
    
    struct server_stats temp[SERVER_STATS_MAX_SERVERS];
    memcpy(temp, server_stats_tab.servers, sizeof(struct server_stats) * server_stats_tab.count);
    
    pthread_mutex_unlock(&server_stats_tab.lock);
    
    // Sort by success rate
    qsort(temp, server_stats_tab.count, sizeof(struct server_stats), compare_success_rate_desc);
    
    // Copy top N
    int result_count = count < server_stats_tab.count ? count : server_stats_tab.count;
    memcpy(servers, temp, sizeof(struct server_stats) * result_count);
    
    return result_count;
}

int server_stats_get_bottom(struct server_stats *servers, int max_count)
{
    pthread_mutex_lock(&server_stats_tab.lock);
    
    // Copy all servers
    int count = server_stats_tab.count;
    if (count > max_count) count = max_count;
    
    struct server_stats temp[SERVER_STATS_MAX_SERVERS];
    memcpy(temp, server_stats_tab.servers, sizeof(struct server_stats) * server_stats_tab.count);
    
    pthread_mutex_unlock(&server_stats_tab.lock);
    
    // Sort by success rate (ascending for bottom)
    qsort(temp, server_stats_tab.count, sizeof(struct server_stats), compare_success_rate_desc);
    
    // Copy bottom N (reverse order)
    int result_count = 0;
    int i;
    for (i = server_stats_tab.count - 1; i >= 0 && result_count < max_count; i--) {
        memcpy(&servers[result_count], &temp[i], sizeof(struct server_stats));
        result_count++;
    }
    
    return result_count;
}

void server_stats_update_history(void)
{
    pthread_mutex_lock(&server_stats_tab.lock);
    
    time_t now = time(NULL);
    int i;
    for (i = 0; i < server_stats_tab.count; i++) {
        struct server_stats *stats = &server_stats_tab.servers[i];
        
        // Add to history
        int idx = stats->history_index;
        stats->history[idx].timestamp = now;
        stats->history[idx].success_rate = stats->success_rate;
        stats->history[idx].avg_response_time = stats->avg_response_time;
        stats->history[idx].request_count = stats->total_requests;
        
        stats->history_index = (idx + 1) % SERVER_STATS_HISTORY_SIZE;
        if (stats->history_count < SERVER_STATS_HISTORY_SIZE) {
            stats->history_count++;
        }
    }
    
    pthread_mutex_unlock(&server_stats_tab.lock);
}

void server_stats_calculate_reliability(struct server_stats *stats)
{
    // Reliability = success_rate * 0.7 + response_time_score * 0.3
    uint32_t response_time_score = 100;
    if (stats->avg_response_time > 0) {
        if (stats->avg_response_time < 300) response_time_score = 100;
        else if (stats->avg_response_time < 600) response_time_score = 80;
        else if (stats->avg_response_time < 1000) response_time_score = 60;
        else if (stats->avg_response_time < 2000) response_time_score = 40;
        else response_time_score = 20;
    }
    
    stats->reliability_score = (stats->success_rate * 70 + response_time_score * 30) / 100;
    
    // Check if lazy
    stats->is_lazy = (stats->success_rate < 50 || stats->reliability_score < 40) ? 1 : 0;
}

// ============================================================================
// HTTP INTERFACE
// ============================================================================

void http_send_server_stats(int sock, struct http_request *req)
{
    struct tcp_buffer_data tcpbuf;
    char buf[16384];
    int i;
    
    tcp_init(&tcpbuf);
    
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
        "<title>إحصائيات الخوادم - MultiCS</title>\n"
        "<style>\n"
        "body { font-family: 'Segoe UI', Tahoma, Arial; margin: 20px; background: #f5f5f5; }\n"
        ".container { max-width: 1400px; margin: 0 auto; background: white; padding: 20px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }\n"
        "h1 { color: #333; border-bottom: 3px solid #007bff; padding-bottom: 10px; }\n"
        "h2 { color: #555; margin-top: 30px; }\n"
        "table { width: 100%%; border-collapse: collapse; margin-top: 20px; font-size: 13px; }\n"
        "th { background: #007bff; color: white; padding: 12px 8px; text-align: right; }\n"
        "td { padding: 10px 8px; border-bottom: 1px solid #ddd; }\n"
        "tr:hover { background: #f5f5f5; }\n"
        ".status-connected { color: #28a745; font-weight: bold; }\n"
        ".status-disconnected { color: #dc3545; font-weight: bold; }\n"
        ".reliability-high { background: #d4edda; }\n"
        ".reliability-medium { background: #fff3cd; }\n"
        ".reliability-low { background: #f8d7da; }\n"
        ".btn { display: inline-block; padding: 8px 16px; background: #007bff; color: white; text-decoration: none; border-radius: 4px; margin: 5px; }\n"
        ".btn:hover { background: #0056b3; }\n"
        ".stats { display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 15px; margin: 20px 0; }\n"
        ".stat-card { background: #f8f9fa; padding: 15px; border-radius: 6px; border-left: 4px solid #007bff; }\n"
        ".stat-value { font-size: 24px; font-weight: bold; color: #333; }\n"
        ".stat-label { color: #666; font-size: 14px; margin-top: 5px; }\n"
        ".progress-bar { background: #e9ecef; height: 20px; border-radius: 10px; overflow: hidden; }\n"
        ".progress-fill { height: 100%%; transition: width 0.3s; }\n"
        "</style>\n"
        "</head>\n"
        "<body>\n"
        "<div class='container'>\n"
        "<h1>📊 إحصائيات الخوادم المفصلة</h1>\n"
    );
    tcp_writestr(&tcpbuf, sock, buf);
    
    // Global statistics
    snprintf(buf, sizeof(buf),
        "<div class='stats'>\n"
        "<div class='stat-card'><div class='stat-value'>%u</div><div class='stat-label'>إجمالي الخوادم</div></div>\n"
        "<div class='stat-card'><div class='stat-value'>%u</div><div class='stat-label'>متصلة</div></div>\n"
        "<div class='stat-card'><div class='stat-value'>%u</div><div class='stat-label'>مفصولة</div></div>\n"
        "</div>\n",
        server_stats_tab.total_servers,
        server_stats_tab.connected_servers,
        server_stats_tab.disconnected_servers
    );
    tcp_writestr(&tcpbuf, sock, buf);
    
    // Action buttons
    snprintf(buf, sizeof(buf),
        "<div style='text-align: center; margin: 20px 0;'>\n"
        "<a href='/server-stats' class='btn'>🔄 تحديث</a>\n"
        "<a href='/' class='btn'>🏠 الرئيسية</a>\n"
        "</div>\n"
    );
    tcp_writestr(&tcpbuf, sock, buf);
    
    // Server table
    snprintf(buf, sizeof(buf),
        "<h2>📋 جميع الخوادم (%d)</h2>\n"
        "<table>\n"
        "<tr>\n"
        "<th>#</th>\n"
        "<th>الخادم</th>\n"
        "<th>العنوان</th>\n"
        "<th>الحالة</th>\n"
        "<th>الطلبات</th>\n"
        "<th>ناجح</th>\n"
        "<th>فاشل</th>\n"
        "<th>نسبة النجاح</th>\n"
        "<th>متوسط الاستجابة</th>\n"
        "<th>الموثوقية</th>\n"
        "</tr>\n",
        server_stats_tab.count
    );
    tcp_writestr(&tcpbuf, sock, buf);
    
    // Get all servers
    struct server_stats servers[SERVER_STATS_MAX_SERVERS];
    int count = server_stats_get_top(servers, SERVER_STATS_MAX_SERVERS);
    
    for (i = 0; i < count; i++) {
        struct server_stats *stats = &servers[i];
        
        // Status class
        const char *status_class = stats->is_connected ? "status-connected" : "status-disconnected";
        const char *status_text = stats->is_connected ? "متصل" : "مفصول";
        
        // Reliability class
        const char *reliability_class = "reliability-low";
        if (stats->reliability_score >= 80) reliability_class = "reliability-high";
        else if (stats->reliability_score >= 50) reliability_class = "reliability-medium";
        
        // Progress bar color
        const char *progress_color = "#dc3545";
        if (stats->success_rate >= 80) progress_color = "#28a745";
        else if (stats->success_rate >= 50) progress_color = "#ffc107";
        
        // Row
        snprintf(buf, sizeof(buf),
            "<tr class='%s'>\n"
            "<td>%d</td>\n"
            "<td><strong>%s</strong></td>\n"
            "<td>%s:%u</td>\n"
            "<td class='%s'>%s</td>\n"
            "<td>%u</td>\n"
            "<td>%u</td>\n"
            "<td>%u</td>\n"
            "<td>\n"
            "<div class='progress-bar'>\n"
            "<div class='progress-fill' style='width: %u%%; background: %s;'></div>\n"
            "</div>\n"
            "<small>%u%%</small>\n"
            "</td>\n"
            "<td>%ums (min: %ums, max: %ums)</td>\n"
            "<td><strong>%u/100</strong></td>\n"
            "</tr>\n",
            reliability_class,
            i + 1,
            stats->server_name[0] ? stats->server_name : "خادم",
            stats->server_host,
            stats->server_port,
            status_class,
            status_text,
            stats->total_requests,
            stats->successful_requests,
            stats->failed_requests,
            stats->success_rate,
            progress_color,
            stats->success_rate,
            stats->avg_response_time,
            stats->min_response_time == 999999 ? 0 : stats->min_response_time,
            stats->max_response_time,
            stats->reliability_score
        );
        tcp_writestr(&tcpbuf, sock, buf);
    }
    
    // End table and page
    snprintf(buf, sizeof(buf),
        "</table>\n"
        "<script>setTimeout(function(){ location.reload(); }, 15000);</script>\n"
        "</div>\n"
        "</body>\n"
        "</html>\n"
    );
    tcp_writestr(&tcpbuf, sock, buf);
}
