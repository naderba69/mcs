/* log-viewer.h - Live Log Viewer
 * 
 * PURPOSE: Display live logs in web interface
 * 
 * FEATURES:
 * - Show live logs
 * - Filter by level (ERROR, WARNING, INFO, DEBUG)
 * - Search in logs
 * - Export logs
 * - Color-coded messages
 */

#ifndef LOG_VIEWER_H
#define LOG_VIEWER_H

#include <stdint.h>
#include <time.h>
#include <pthread.h>

// Log levels
#define LOG_LEVEL_ERROR   0
#define LOG_LEVEL_WARNING 1
#define LOG_LEVEL_INFO    2
#define LOG_LEVEL_DEBUG   3

// Maximum log entries to keep
#define LOG_VIEWER_MAX_ENTRIES 500

// Maximum message length
#define LOG_VIEWER_MAX_MSG_LEN 512

// Log entry
struct log_entry {
    time_t timestamp;
    int level;  // LOG_LEVEL_*
    char message[LOG_VIEWER_MAX_MSG_LEN];
    char source[64];  // module/function name
};

// Log viewer table
struct log_viewer_table {
    struct log_entry entries[LOG_VIEWER_MAX_ENTRIES];
    int count;
    int next_index;  // circular buffer
    pthread_mutex_t lock;
    
    // Statistics
    uint32_t total_logs;
    uint32_t error_count;
    uint32_t warning_count;
    uint32_t info_count;
    uint32_t debug_count;
};

extern struct log_viewer_table log_viewer_tab;

// Initialize log viewer
void log_viewer_init(void);

// Add log entry
void log_viewer_add(int level, const char *source, const char *format, ...);

// Get recent logs
int log_viewer_get_recent(struct log_entry *entries, int max_count);

// Filter logs by level
int log_viewer_filter_level(struct log_entry *entries, int max_count, int min_level);

// Search logs
int log_viewer_search(struct log_entry *entries, int max_count, const char *query);

// Clear all logs
void log_viewer_clear(void);

// Export logs to file
int log_viewer_export(const char *filename);

// HTTP interface
struct http_request;
char *isset_get(struct http_request *req, char *name);
void http_send_log_viewer(int sock, struct http_request *req);

#endif /* LOG_VIEWER_H */
