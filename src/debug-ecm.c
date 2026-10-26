/* debug-ecm.c - Detailed ECM Tracking Implementation
 * 
 * PURPOSE: Track every ECM request with full details
 */

#include "common.h"
#include "debug-ecm.h"
#include "httpserver.h"
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
extern void tcp_write(struct tcp_buffer_data *buf, int sock, const char *data, int size);

// Global debug ECM table
struct debug_ecm_table debug_ecm_tab;
static uint32_t next_request_id = 1;

// ============================================================================
// INITIALIZATION
// ============================================================================

void debug_ecm_init(void)
{
    memset(&debug_ecm_tab, 0, sizeof(debug_ecm_tab));
    pthread_mutex_init(&debug_ecm_tab.lock, NULL);
    
    debugf(0, " [DEBUG-ECM] Debug ECM system initialized (max %d entries)\n", 
           DEBUG_ECM_MAX_ENTRIES);
}

// ============================================================================
// RECORD FUNCTIONS
// ============================================================================

int debug_ecm_record_request(uint16_t caid, uint32_t provid, uint16_t sid,
                            uint32_t client_ip, const char *client_name)
{
    pthread_mutex_lock(&debug_ecm_tab.lock);
    
    // Get next slot (circular buffer)
    int index = debug_ecm_tab.next_index;
    struct debug_ecm_entry *entry = &debug_ecm_tab.entries[index];
    
    // Clear entry
    memset(entry, 0, sizeof(struct debug_ecm_entry));
    
    // Fill basic info
    entry->timestamp = time(NULL);
    entry->request_id = next_request_id++;
    entry->caid = caid;
    entry->provid = provid;
    entry->sid = sid;
    entry->client_ip = client_ip;
    entry->final_status = ECM_STATUS_PENDING;
    
    if (client_name) {
        strncpy(entry->client_name, client_name, sizeof(entry->client_name) - 1);
    }
    
    // Update table
    debug_ecm_tab.next_index = (index + 1) % DEBUG_ECM_MAX_ENTRIES;
    if (debug_ecm_tab.count < DEBUG_ECM_MAX_ENTRIES) {
        debug_ecm_tab.count++;
    }
    debug_ecm_tab.total_requests++;
    
    int request_id = entry->request_id;
    
    pthread_mutex_unlock(&debug_ecm_tab.lock);
    
    return request_id;
}

void debug_ecm_record_server_response(int request_id, uint32_t server_id,
                                     const char *server_name, uint32_t response_time,
                                     int status)
{
    pthread_mutex_lock(&debug_ecm_tab.lock);
    
    // Find entry by request_id
    int i;
    for (i = 0; i < debug_ecm_tab.count; i++) {
        struct debug_ecm_entry *entry = &debug_ecm_tab.entries[i];
        if (entry->request_id == request_id) {
            // Add server response
            if (entry->server_count < 10) {
                struct ecm_server_response *resp = &entry->servers[entry->server_count++];
                resp->server_id = server_id;
                resp->response_time = response_time;
                resp->status = status;
                if (server_name) {
                    strncpy(resp->server_name, server_name, sizeof(resp->server_name) - 1);
                }
            }
            break;
        }
    }
    
    pthread_mutex_unlock(&debug_ecm_tab.lock);
}

void debug_ecm_mark_complete(int request_id, int final_status,
                            const char *result_source, const char *error_msg)
{
    pthread_mutex_lock(&debug_ecm_tab.lock);
    
    // Find entry by request_id
    int i;
    for (i = 0; i < debug_ecm_tab.count; i++) {
        struct debug_ecm_entry *entry = &debug_ecm_tab.entries[i];
        if (entry->request_id == request_id) {
            entry->final_status = final_status;
            if (result_source) {
                strncpy(entry->result_source, result_source, sizeof(entry->result_source) - 1);
            }
            if (error_msg) {
                strncpy(entry->error_message, error_msg, sizeof(entry->error_message) - 1);
            }
            
            // Update statistics
            if (final_status == ECM_STATUS_SUCCESS) {
                debug_ecm_tab.successful_requests++;
            } else if (final_status == ECM_STATUS_FAILED || final_status == ECM_STATUS_TIMEOUT) {
                debug_ecm_tab.failed_requests++;
            }
            
            break;
        }
    }
    
    pthread_mutex_unlock(&debug_ecm_tab.lock);
}

void debug_ecm_mark_cached(int request_id)
{
    pthread_mutex_lock(&debug_ecm_tab.lock);
    
    // Find entry by request_id
    int i;
    for (i = 0; i < debug_ecm_tab.count; i++) {
        struct debug_ecm_entry *entry = &debug_ecm_tab.entries[i];
        if (entry->request_id == request_id) {
            entry->final_status = ECM_STATUS_CACHED;
            entry->cache_hit = 1;
            strncpy(entry->result_source, "cache", sizeof(entry->result_source) - 1);
            debug_ecm_tab.cached_requests++;
            break;
        }
    }
    
    pthread_mutex_unlock(&debug_ecm_tab.lock);
}

// ============================================================================
// QUERY FUNCTIONS
// ============================================================================

struct debug_ecm_entry* debug_ecm_get(int request_id)
{
    pthread_mutex_lock(&debug_ecm_tab.lock);
    
    int i;
    for (i = 0; i < debug_ecm_tab.count; i++) {
        if (debug_ecm_tab.entries[i].request_id == request_id) {
            pthread_mutex_unlock(&debug_ecm_tab.lock);
            return &debug_ecm_tab.entries[i];
        }
    }
    
    pthread_mutex_unlock(&debug_ecm_tab.lock);
    return NULL;
}

int debug_ecm_get_recent(struct debug_ecm_entry *entries, int max_count)
{
    pthread_mutex_lock(&debug_ecm_tab.lock);
    
    int count = 0;
    int start = (debug_ecm_tab.next_index - 1 + DEBUG_ECM_MAX_ENTRIES) % DEBUG_ECM_MAX_ENTRIES;
    
    int i;
    for (i = 0; i < debug_ecm_tab.count && count < max_count; i++) {
        int index = (start - i + DEBUG_ECM_MAX_ENTRIES) % DEBUG_ECM_MAX_ENTRIES;
        memcpy(&entries[count], &debug_ecm_tab.entries[index], sizeof(struct debug_ecm_entry));
        count++;
    }
    
    pthread_mutex_unlock(&debug_ecm_tab.lock);
    return count;
}

int debug_ecm_filter(struct debug_ecm_entry *entries, int max_count,
                    uint16_t caid, uint32_t provid, uint16_t sid)
{
    pthread_mutex_lock(&debug_ecm_tab.lock);
    
    int count = 0;
    int i;
    for (i = 0; i < debug_ecm_tab.count && count < max_count; i++) {
        struct debug_ecm_entry *entry = &debug_ecm_tab.entries[i];
        
        // Apply filters
        if (caid != 0 && entry->caid != caid) continue;
        if (provid != 0 && entry->provid != provid) continue;
        if (sid != 0 && entry->sid != sid) continue;
        
        memcpy(&entries[count], entry, sizeof(struct debug_ecm_entry));
        count++;
    }
    
    pthread_mutex_unlock(&debug_ecm_tab.lock);
    return count;
}

void debug_ecm_clear(void)
{
    pthread_mutex_lock(&debug_ecm_tab.lock);
    memset(&debug_ecm_tab.entries, 0, sizeof(debug_ecm_tab.entries));
    debug_ecm_tab.count = 0;
    debug_ecm_tab.next_index = 0;
    debug_ecm_tab.total_requests = 0;
    debug_ecm_tab.successful_requests = 0;
    debug_ecm_tab.failed_requests = 0;
    debug_ecm_tab.cached_requests = 0;
    pthread_mutex_unlock(&debug_ecm_tab.lock);
}

// ============================================================================
// HTTP INTERFACE
// ============================================================================

void http_send_debug_ecm(int sock, struct http_request *req)
{
    struct tcp_buffer_data tcpbuf;
    char buf[16384];
    char temp[512];
    int i, j;
    
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
        "<title>تتبع ECM التفصيلي - MultiCS</title>\n"
        "<style>\n"
        "body { font-family: 'Segoe UI', Tahoma, Arial; margin: 20px; background: #f5f5f5; }\n"
        ".container { max-width: 1400px; margin: 0 auto; background: white; padding: 20px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }\n"
        "h1 { color: #333; border-bottom: 3px solid #007bff; padding-bottom: 10px; }\n"
        "h2 { color: #555; margin-top: 30px; }\n"
        "table { width: 100%%; border-collapse: collapse; margin-top: 20px; font-size: 13px; }\n"
        "th { background: #007bff; color: white; padding: 12px 8px; text-align: right; }\n"
        "td { padding: 10px 8px; border-bottom: 1px solid #ddd; }\n"
        "tr:hover { background: #f5f5f5; }\n"
        ".status-success { color: #28a745; font-weight: bold; }\n"
        ".status-failed { color: #dc3545; font-weight: bold; }\n"
        ".status-timeout { color: #ffc107; font-weight: bold; }\n"
        ".status-cached { color: #17a2b8; font-weight: bold; }\n"
        ".status-pending { color: #6c757d; font-weight: bold; }\n"
        ".server-list { font-size: 11px; color: #666; }\n"
        ".btn { display: inline-block; padding: 8px 16px; background: #007bff; color: white; text-decoration: none; border-radius: 4px; margin: 5px; }\n"
        ".btn:hover { background: #0056b3; }\n"
        ".btn-danger { background: #dc3545; }\n"
        ".btn-danger:hover { background: #c82333; }\n"
        ".stats { display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 15px; margin: 20px 0; }\n"
        ".stat-card { background: #f8f9fa; padding: 15px; border-radius: 6px; border-left: 4px solid #007bff; }\n"
        ".stat-value { font-size: 24px; font-weight: bold; color: #333; }\n"
        ".stat-label { color: #666; font-size: 14px; margin-top: 5px; }\n"
        "</style>\n"
        "</head>\n"
        "<body>\n"
        "<div class='container'>\n"
        "<h1>🔍 تتبع ECM التفصيلي</h1>\n"
    );
    tcp_writestr(&tcpbuf, sock, buf);
    
    // Statistics
    snprintf(buf, sizeof(buf),
        "<div class='stats'>\n"
        "<div class='stat-card'><div class='stat-value'>%u</div><div class='stat-label'>إجمالي الطلبات</div></div>\n"
        "<div class='stat-card'><div class='stat-value'>%u</div><div class='stat-label'>ناجحة</div></div>\n"
        "<div class='stat-card'><div class='stat-value'>%u</div><div class='stat-label'>فاشلة</div></div>\n"
        "<div class='stat-card'><div class='stat-value'>%u</div><div class='stat-label'>من الكاش</div></div>\n"
        "</div>\n",
        debug_ecm_tab.total_requests,
        debug_ecm_tab.successful_requests,
        debug_ecm_tab.failed_requests,
        debug_ecm_tab.cached_requests
    );
    tcp_writestr(&tcpbuf, sock, buf);
    
    // Action buttons
    snprintf(buf, sizeof(buf),
        "<div style='text-align: center; margin: 20px 0;'>\n"
        "<a href='/debug-ecm' class='btn'>🔄 تحديث</a>\n"
        "<a href='/debug-ecm?action=clear' class='btn btn-danger' onclick='return confirm(\"هل أنت متأكد؟\")'>🗑️ مسح الكل</a>\n"
        "<a href='/' class='btn'>🏠 الرئيسية</a>\n"
        "</div>\n"
    );
    tcp_writestr(&tcpbuf, sock, buf);
    
    // Handle clear action
    char *action = isset_get(req, "action");
    if (action && strcmp(action, "clear") == 0) {
        debug_ecm_clear();
        snprintf(buf, sizeof(buf), "<div style='background: #d4edda; padding: 15px; border-radius: 4px; margin: 20px 0;'>✅ تم مسح جميع السجلات</div>\n");
        tcp_writestr(&tcpbuf, sock, buf);
    }
    
    // ECM table
    snprintf(buf, sizeof(buf),
        "<h2>📋 آخر %d طلب ECM</h2>\n"
        "<table>\n"
        "<tr>\n"
        "<th>#</th>\n"
        "<th>الوقت</th>\n"
        "<th>CAID</th>\n"
        "<th>Provider</th>\n"
        "<th>SID</th>\n"
        "<th>العميل</th>\n"
        "<th>الحالة</th>\n"
        "<th>المصدر</th>\n"
        "<th>الوقت (ms)</th>\n"
        "<th>الخوادم</th>\n"
        "</tr>\n",
        debug_ecm_tab.count < 50 ? debug_ecm_tab.count : 50
    );
    tcp_writestr(&tcpbuf, sock, buf);
    
    // Get recent ECMs
    struct debug_ecm_entry entries[50];
    int count = debug_ecm_get_recent(entries, 50);
    
    for (i = 0; i < count; i++) {
        struct debug_ecm_entry *entry = &entries[i];
        
        // Format time
        char time_str[32];
        struct tm *tm = localtime(&entry->timestamp);
        strftime(time_str, sizeof(time_str), "%H:%M:%S", tm);
        
        // Status class
        const char *status_class = "status-pending";
        const char *status_text = "قيد الانتظار";
        if (entry->final_status == ECM_STATUS_SUCCESS) {
            status_class = "status-success";
            status_text = "ناجح";
        } else if (entry->final_status == ECM_STATUS_FAILED) {
            status_class = "status-failed";
            status_text = "فشل";
        } else if (entry->final_status == ECM_STATUS_TIMEOUT) {
            status_class = "status-timeout";
            status_text = "انتهاء مهلة";
        } else if (entry->final_status == ECM_STATUS_CACHED) {
            status_class = "status-cached";
            status_text = "من الكاش";
        }
        
        // Server list
        char server_list[512] = "";
        if (entry->server_count > 0) {
            char temp_srv[128];
            for (j = 0; j < entry->server_count && j < 3; j++) {
                struct ecm_server_response *resp = &entry->servers[j];
                snprintf(temp_srv, sizeof(temp_srv), "%s%s:%ums ",
                        j > 0 ? "<br>" : "",
                        resp->server_name[0] ? resp->server_name : "خادم",
                        resp->response_time);
                strcat(server_list, temp_srv);
            }
            if (entry->server_count > 3) {
                snprintf(temp_srv, sizeof(temp_srv), "<br>+%d خوادم", entry->server_count - 3);
                strcat(server_list, temp_srv);
            }
        } else {
            strcpy(server_list, "-");
        }
        
        // Row
        snprintf(buf, sizeof(buf),
            "<tr>\n"
            "<td>%u</td>\n"
            "<td>%s</td>\n"
            "<td>%04X</td>\n"
            "<td>%06X</td>\n"
            "<td>%04X</td>\n"
            "<td>%s</td>\n"
            "<td class='%s'>%s</td>\n"
            "<td>%s</td>\n"
            "<td>%u</td>\n"
            "<td class='server-list'>%s</td>\n"
            "</tr>\n",
            entry->request_id,
            time_str,
            entry->caid,
            entry->provid,
            entry->sid,
            entry->client_name[0] ? entry->client_name : "-",
            status_class,
            status_text,
            entry->result_source[0] ? entry->result_source : "-",
            entry->total_time,
            server_list
        );
        tcp_writestr(&tcpbuf, sock, buf);
    }
    
    // End table and page
    snprintf(buf, sizeof(buf),
        "</table>\n"
        "<script>setTimeout(function(){ location.reload(); }, 10000);</script>\n"
        "</div>\n"
        "</body>\n"
        "</html>\n"
    );
    tcp_writestr(&tcpbuf, sock, buf);
}
