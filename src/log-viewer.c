/* log-viewer.c - Live Log Viewer Implementation
 * 
 * PURPOSE: Display live logs in web interface
 */

#include "common.h"
#include "log-viewer.h"
#include "httpserver.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
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

// Global log viewer table
struct log_viewer_table log_viewer_tab;

// ============================================================================
// INITIALIZATION
// ============================================================================

void log_viewer_init(void)
{
    memset(&log_viewer_tab, 0, sizeof(log_viewer_tab));
    pthread_mutex_init(&log_viewer_tab.lock, NULL);
    
    debugf(0, " [LOG-VIEWER] Log viewer initialized (max %d entries)\n", 
           LOG_VIEWER_MAX_ENTRIES);
}

// ============================================================================
// LOG FUNCTIONS
// ============================================================================

void log_viewer_add(int level, const char *source, const char *format, ...)
{
    pthread_mutex_lock(&log_viewer_tab.lock);
    
    // Get next slot (circular buffer)
    int index = log_viewer_tab.next_index;
    struct log_entry *entry = &log_viewer_tab.entries[index];
    
    // Clear entry
    memset(entry, 0, sizeof(struct log_entry));
    
    // Fill basic info
    entry->timestamp = time(NULL);
    entry->level = level;
    
    if (source) {
        strncpy(entry->source, source, sizeof(entry->source) - 1);
    }
    
    // Format message
    va_list args;
    va_start(args, format);
    vsnprintf(entry->message, sizeof(entry->message), format, args);
    va_end(args);
    
    // Update table
    log_viewer_tab.next_index = (index + 1) % LOG_VIEWER_MAX_ENTRIES;
    if (log_viewer_tab.count < LOG_VIEWER_MAX_ENTRIES) {
        log_viewer_tab.count++;
    }
    log_viewer_tab.total_logs++;
    
    // Update statistics
    switch (level) {
        case LOG_LEVEL_ERROR:
            log_viewer_tab.error_count++;
            break;
        case LOG_LEVEL_WARNING:
            log_viewer_tab.warning_count++;
            break;
        case LOG_LEVEL_INFO:
            log_viewer_tab.info_count++;
            break;
        case LOG_LEVEL_DEBUG:
            log_viewer_tab.debug_count++;
            break;
    }
    
    pthread_mutex_unlock(&log_viewer_tab.lock);
}

// ============================================================================
// QUERY FUNCTIONS
// ============================================================================

int log_viewer_get_recent(struct log_entry *entries, int max_count)
{
    pthread_mutex_lock(&log_viewer_tab.lock);
    
    int count = 0;
    int start = (log_viewer_tab.next_index - 1 + LOG_VIEWER_MAX_ENTRIES) % LOG_VIEWER_MAX_ENTRIES;
    
    int i;
    for (i = 0; i < log_viewer_tab.count && count < max_count; i++) {
        int index = (start - i + LOG_VIEWER_MAX_ENTRIES) % LOG_VIEWER_MAX_ENTRIES;
        memcpy(&entries[count], &log_viewer_tab.entries[index], sizeof(struct log_entry));
        count++;
    }
    
    pthread_mutex_unlock(&log_viewer_tab.lock);
    return count;
}

int log_viewer_filter_level(struct log_entry *entries, int max_count, int min_level)
{
    pthread_mutex_lock(&log_viewer_tab.lock);
    
    int count = 0;
    int start = (log_viewer_tab.next_index - 1 + LOG_VIEWER_MAX_ENTRIES) % LOG_VIEWER_MAX_ENTRIES;
    
    int i;
    for (i = 0; i < log_viewer_tab.count && count < max_count; i++) {
        int index = (start - i + LOG_VIEWER_MAX_ENTRIES) % LOG_VIEWER_MAX_ENTRIES;
        struct log_entry *entry = &log_viewer_tab.entries[index];
        
        if (entry->level <= min_level) {
            memcpy(&entries[count], entry, sizeof(struct log_entry));
            count++;
        }
    }
    
    pthread_mutex_unlock(&log_viewer_tab.lock);
    return count;
}

int log_viewer_search(struct log_entry *entries, int max_count, const char *query)
{
    pthread_mutex_lock(&log_viewer_tab.lock);
    
    int count = 0;
    int start = (log_viewer_tab.next_index - 1 + LOG_VIEWER_MAX_ENTRIES) % LOG_VIEWER_MAX_ENTRIES;
    
    int i;
    for (i = 0; i < log_viewer_tab.count && count < max_count; i++) {
        int index = (start - i + LOG_VIEWER_MAX_ENTRIES) % LOG_VIEWER_MAX_ENTRIES;
        struct log_entry *entry = &log_viewer_tab.entries[index];
        
        if (strstr(entry->message, query) || strstr(entry->source, query)) {
            memcpy(&entries[count], entry, sizeof(struct log_entry));
            count++;
        }
    }
    
    pthread_mutex_unlock(&log_viewer_tab.lock);
    return count;
}

void log_viewer_clear(void)
{
    pthread_mutex_lock(&log_viewer_tab.lock);
    memset(&log_viewer_tab.entries, 0, sizeof(log_viewer_tab.entries));
    log_viewer_tab.count = 0;
    log_viewer_tab.next_index = 0;
    log_viewer_tab.total_logs = 0;
    log_viewer_tab.error_count = 0;
    log_viewer_tab.warning_count = 0;
    log_viewer_tab.info_count = 0;
    log_viewer_tab.debug_count = 0;
    pthread_mutex_unlock(&log_viewer_tab.lock);
}

int log_viewer_export(const char *filename)
{
    FILE *fp = fopen(filename, "w");
    if (!fp) return -1;
    
    pthread_mutex_lock(&log_viewer_tab.lock);
    
    fprintf(fp, "Timestamp,Level,Source,Message\n");
    
    int i;
    for (i = 0; i < log_viewer_tab.count; i++) {
        struct log_entry *entry = &log_viewer_tab.entries[i];
        
        char time_str[32];
        struct tm *tm = localtime(&entry->timestamp);
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm);
        
        const char *level_str = "UNKNOWN";
        switch (entry->level) {
            case LOG_LEVEL_ERROR: level_str = "ERROR"; break;
            case LOG_LEVEL_WARNING: level_str = "WARNING"; break;
            case LOG_LEVEL_INFO: level_str = "INFO"; break;
            case LOG_LEVEL_DEBUG: level_str = "DEBUG"; break;
        }
        
        fprintf(fp, "%s,%s,%s,\"%s\"\n",
                time_str, level_str, entry->source, entry->message);
    }
    
    pthread_mutex_unlock(&log_viewer_tab.lock);
    
    fclose(fp);
    return 0;
}

// ============================================================================
// HTTP INTERFACE
// ============================================================================

void http_send_log_viewer(int sock, struct http_request *req)
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
        "<title>عارض السجلات - MultiCS</title>\n"
        "<style>\n"
        "body { font-family: 'Courier New', monospace; margin: 20px; background: #1e1e1e; color: #d4d4d4; }\n"
        ".container { max-width: 1600px; margin: 0 auto; background: #252526; padding: 20px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.5); }\n"
        "h1 { color: #569cd6; border-bottom: 2px solid #569cd6; padding-bottom: 10px; }\n"
        ".stats { display: grid; grid-template-columns: repeat(auto-fit, minmax(150px, 1fr)); gap: 10px; margin: 20px 0; }\n"
        ".stat-card { background: #2d2d30; padding: 15px; border-radius: 6px; border-left: 4px solid #569cd6; }\n"
        ".stat-value { font-size: 24px; font-weight: bold; color: #569cd6; }\n"
        ".stat-label { color: #858585; font-size: 12px; margin-top: 5px; }\n"
        ".controls { margin: 20px 0; padding: 15px; background: #2d2d30; border-radius: 6px; }\n"
        ".btn { display: inline-block; padding: 8px 16px; background: #0e639c; color: white; text-decoration: none; border-radius: 4px; margin: 5px; border: none; cursor: pointer; }\n"
        ".btn:hover { background: #1177bb; }\n"
        ".btn-danger { background: #f48771; }\n"
        ".btn-danger:hover { background: #f48771; }\n"
        "select, input[type=text] { padding: 8px; background: #3c3c3c; color: #d4d4d4; border: 1px solid #3c3c3c; border-radius: 4px; margin: 5px; }\n"
        ".log-container { background: #1e1e1e; padding: 15px; border-radius: 6px; max-height: 600px; overflow-y: auto; font-size: 13px; }\n"
        ".log-entry { padding: 8px; margin: 5px 0; border-left: 3px solid #569cd6; background: #252526; }\n"
        ".log-error { border-left-color: #f48771; background: #2d1f1f; }\n"
        ".log-warning { border-left-color: #cca700; background: #2d2a1f; }\n"
        ".log-info { border-left-color: #569cd6; background: #1f2a2d; }\n"
        ".log-debug { border-left-color: #858585; background: #252526; }\n"
        ".log-time { color: #858585; font-size: 11px; }\n"
        ".log-level { font-weight: bold; margin: 0 10px; }\n"
        ".log-level-error { color: #f48771; }\n"
        ".log-level-warning { color: #cca700; }\n"
        ".log-level-info { color: #569cd6; }\n"
        ".log-level-debug { color: #858585; }\n"
        ".log-source { color: #4ec9b0; margin: 0 10px; }\n"
        ".log-message { color: #d4d4d4; }\n"
        "</style>\n"
        "</head>\n"
        "<body>\n"
        "<div class='container'>\n"
        "<h1>📝 عارض السجلات المباشر</h1>\n"
    );
    tcp_writestr(&tcpbuf, sock, buf);
    
    // Statistics
    snprintf(buf, sizeof(buf),
        "<div class='stats'>\n"
        "<div class='stat-card'><div class='stat-value'>%u</div><div class='stat-label'>إجمالي السجلات</div></div>\n"
        "<div class='stat-card'><div class='stat-value' style='color: #f48771;'>%u</div><div class='stat-label'>أخطاء</div></div>\n"
        "<div class='stat-card'><div class='stat-value' style='color: #cca700;'>%u</div><div class='stat-label'>تحذيرات</div></div>\n"
        "<div class='stat-card'><div class='stat-value' style='color: #569cd6;'>%u</div><div class='stat-label'>معلومات</div></div>\n"
        "<div class='stat-card'><div class='stat-value' style='color: #858585;'>%u</div><div class='stat-label'>تصحيح</div></div>\n"
        "</div>\n",
        log_viewer_tab.total_logs,
        log_viewer_tab.error_count,
        log_viewer_tab.warning_count,
        log_viewer_tab.info_count,
        log_viewer_tab.debug_count
    );
    tcp_writestr(&tcpbuf, sock, buf);
    
    // Controls
    snprintf(buf, sizeof(buf),
        "<div class='controls'>\n"
        "<form method='GET' action='/log-viewer' style='display: inline;'>\n"
        "<label>المستوى:</label>\n"
        "<select name='level'>\n"
        "<option value='3'>الكل</option>\n"
        "<option value='2'>معلومات وأعلى</option>\n"
        "<option value='1'>تحذيرات وأعلى</option>\n"
        "<option value='0'>أخطاء فقط</option>\n"
        "</select>\n"
        "<label>بحث:</label>\n"
        "<input type='text' name='search' placeholder='ابحث في السجلات...'>\n"
        "<button type='submit' class='btn'>🔍 تطبيق</button>\n"
        "</form>\n"
        "<a href='/log-viewer' class='btn'>🔄 تحديث</a>\n"
        "<a href='/log-viewer?action=clear' class='btn btn-danger' onclick='return confirm(\"هل أنت متأكد؟\")'>🗑️ مسح الكل</a>\n"
        "<a href='/' class='btn'>🏠 الرئيسية</a>\n"
        "</div>\n"
    );
    tcp_writestr(&tcpbuf, sock, buf);
    
    // Handle clear action
    char *action = isset_get(req, "action");
    if (action && strcmp(action, "clear") == 0) {
        log_viewer_clear();
        snprintf(buf, sizeof(buf), "<div style='background: #2d2a1f; padding: 15px; border-radius: 4px; margin: 20px 0; border-left: 4px solid #cca700;'>✅ تم مسح جميع السجلات</div>\n");
        tcp_writestr(&tcpbuf, sock, buf);
    }
    
    // Get filter parameters
    char *level_str = isset_get(req, "level");
    char *search = isset_get(req, "search");
    
    int min_level = LOG_LEVEL_DEBUG;
    if (level_str) {
        min_level = atoi(level_str);
    }
    
    // Get logs
    struct log_entry entries[100];
    int count;
    
    if (search && strlen(search) > 0) {
        count = log_viewer_search(entries, 100, search);
    } else {
        count = log_viewer_filter_level(entries, 100, min_level);
    }
    
    // Log container
    snprintf(buf, sizeof(buf),
        "<div class='log-container'>\n"
        "<h3>📋 آخر %d سجل</h3>\n",
        count
    );
    tcp_writestr(&tcpbuf, sock, buf);
    
    for (i = 0; i < count; i++) {
        struct log_entry *entry = &entries[i];
        
        // Format time
        char time_str[32];
        struct tm *tm = localtime(&entry->timestamp);
        strftime(time_str, sizeof(time_str), "%H:%M:%S", tm);
        
        // Level class and text
        const char *level_class = "log-debug";
        const char *level_text = "DEBUG";
        if (entry->level == LOG_LEVEL_ERROR) {
            level_class = "log-error";
            level_text = "ERROR";
        } else if (entry->level == LOG_LEVEL_WARNING) {
            level_class = "log-warning";
            level_text = "WARNING";
        } else if (entry->level == LOG_LEVEL_INFO) {
            level_class = "log-info";
            level_text = "INFO";
        }
        
        const char *level_text_class = "log-level-debug";
        if (entry->level == LOG_LEVEL_ERROR) level_text_class = "log-level-error";
        else if (entry->level == LOG_LEVEL_WARNING) level_text_class = "log-level-warning";
        else if (entry->level == LOG_LEVEL_INFO) level_text_class = "log-level-info";
        
        // Log entry
        snprintf(buf, sizeof(buf),
            "<div class='log-entry %s'>\n"
            "<span class='log-time'>[%s]</span>\n"
            "<span class='log-level %s'>%s</span>\n"
            "<span class='log-source'>[%s]</span>\n"
            "<span class='log-message'>%s</span>\n"
            "</div>\n",
            level_class,
            time_str,
            level_text_class,
            level_text,
            entry->source[0] ? entry->source : "-",
            entry->message
        );
        tcp_writestr(&tcpbuf, sock, buf);
    }
    
    // End page
    snprintf(buf, sizeof(buf),
        "</div>\n"
        "<script>setTimeout(function(){ location.reload(); }, 5000);</script>\n"
        "</div>\n"
        "</body>\n"
        "</html>\n"
    );
    tcp_writestr(&tcpbuf, sock, buf);
}
