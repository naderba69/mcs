/* cache-analytics.c - Advanced Cache Analytics Implementation
 * 
 * PURPOSE: Analyze cache performance and provide insights
 */

#include "common.h"
#include "debug.h"
#include "cache-analytics.h"
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

// Global cache analytics table
struct cache_analytics_table cache_analytics_tab;

// ============================================================================
// INITIALIZATION
// ============================================================================

void cache_analytics_init(void)
{
    memset(&cache_analytics_tab, 0, sizeof(cache_analytics_tab));
    pthread_mutex_init(&cache_analytics_tab.lock, NULL);
    cache_analytics_tab.start_time = time(NULL);
    
    debugf(0, " [CACHE-ANALYTICS] Cache analytics initialized (max %d CAIDs, %d channels)\n", 
           CACHE_ANALYTICS_MAX_CAIDS, CACHE_ANALYTICS_MAX_CHANNELS);
}

// ============================================================================
// RECORD FUNCTIONS
// ============================================================================

static struct cache_caid_stats* find_or_create_caid(uint16_t caid)
{
    int i;
    for (i = 0; i < cache_analytics_tab.caid_count; i++) {
        if (cache_analytics_tab.caids[i].caid == caid) {
            return &cache_analytics_tab.caids[i];
        }
    }
    
    if (cache_analytics_tab.caid_count < CACHE_ANALYTICS_MAX_CAIDS) {
        struct cache_caid_stats *stats = &cache_analytics_tab.caids[cache_analytics_tab.caid_count++];
        memset(stats, 0, sizeof(struct cache_caid_stats));
        stats->caid = caid;
        snprintf(stats->caid_name, sizeof(stats->caid_name), "CAID %04X", caid);
        return stats;
    }
    
    return NULL;
}

static struct cache_channel_stats* find_or_create_channel(uint16_t caid, uint32_t provid, uint16_t sid)
{
    int i;
    for (i = 0; i < cache_analytics_tab.channel_count; i++) {
        struct cache_channel_stats *ch = &cache_analytics_tab.channels[i];
        if (ch->caid == caid && ch->provid == provid && ch->sid == sid) {
            return ch;
        }
    }
    
    if (cache_analytics_tab.channel_count < CACHE_ANALYTICS_MAX_CHANNELS) {
        struct cache_channel_stats *ch = &cache_analytics_tab.channels[cache_analytics_tab.channel_count++];
        memset(ch, 0, sizeof(struct cache_channel_stats));
        ch->caid = caid;
        ch->provid = provid;
        ch->sid = sid;
        snprintf(ch->channel_name, sizeof(ch->channel_name), "%04X:%06X:%04X", caid, provid, sid);
        return ch;
    }
    
    return NULL;
}

void cache_analytics_record_request(uint16_t caid, uint32_t provid, uint16_t sid,
                                   int cache_hit, uint32_t response_time)
{
    pthread_mutex_lock(&cache_analytics_tab.lock);
    
    // Update CAID stats
    struct cache_caid_stats *caid_stats = find_or_create_caid(caid);
    if (caid_stats) {
        caid_stats->total_requests++;
        caid_stats->last_request_time = time(NULL);
        
        if (cache_hit) {
            caid_stats->cache_hits++;
            caid_stats->cw_from_cache++;
            caid_stats->avg_cache_hit_time = (caid_stats->avg_cache_hit_time * (caid_stats->cache_hits - 1) + response_time) / caid_stats->cache_hits;
            caid_stats->last_hit_time = time(NULL);
        } else {
            caid_stats->cache_misses++;
            caid_stats->cw_from_server++;
            caid_stats->avg_server_time = (caid_stats->avg_server_time * (caid_stats->cache_misses - 1) + response_time) / caid_stats->cache_misses;
        }
        
        caid_stats->total_cw_received++;
        
        if (caid_stats->total_requests > 0) {
            caid_stats->hit_rate = (caid_stats->cache_hits * 100) / caid_stats->total_requests;
        }
    }
    
    // Update channel stats
    struct cache_channel_stats *ch_stats = find_or_create_channel(caid, provid, sid);
    if (ch_stats) {
        ch_stats->total_requests++;
        ch_stats->last_request_time = time(NULL);
        
        if (cache_hit) {
            ch_stats->cache_hits++;
        }
        
        if (ch_stats->total_requests > 0) {
            ch_stats->hit_rate = (ch_stats->cache_hits * 100) / ch_stats->total_requests;
        }
    }
    
    // Update global stats
    cache_analytics_tab.total_requests++;
    if (cache_hit) {
        cache_analytics_tab.total_cache_hits++;
    } else {
        cache_analytics_tab.total_cache_misses++;
    }
    
    if (cache_analytics_tab.total_requests > 0) {
        cache_analytics_tab.global_hit_rate = (cache_analytics_tab.total_cache_hits * 100) / cache_analytics_tab.total_requests;
    }
    
    cache_analytics_tab.last_update = time(NULL);
    
    pthread_mutex_unlock(&cache_analytics_tab.lock);
}

void cache_analytics_set_caid_name(uint16_t caid, const char *name)
{
    pthread_mutex_lock(&cache_analytics_tab.lock);
    
    struct cache_caid_stats *stats = find_or_create_caid(caid);
    if (stats && name) {
        strncpy(stats->caid_name, name, sizeof(stats->caid_name) - 1);
    }
    
    pthread_mutex_unlock(&cache_analytics_tab.lock);
}

void cache_analytics_set_channel_name(uint16_t caid, uint32_t provid, uint16_t sid,
                                     const char *name)
{
    pthread_mutex_lock(&cache_analytics_tab.lock);
    
    struct cache_channel_stats *ch = find_or_create_channel(caid, provid, sid);
    if (ch && name) {
        strncpy(ch->channel_name, name, sizeof(ch->channel_name) - 1);
    }
    
    pthread_mutex_unlock(&cache_analytics_tab.lock);
}

// ============================================================================
// QUERY FUNCTIONS
// ============================================================================

int cache_analytics_get_caid_stats(struct cache_caid_stats *stats, int max_count)
{
    pthread_mutex_lock(&cache_analytics_tab.lock);
    
    int count = cache_analytics_tab.caid_count;
    if (count > max_count) count = max_count;
    
    memcpy(stats, cache_analytics_tab.caids, sizeof(struct cache_caid_stats) * count);
    
    pthread_mutex_unlock(&cache_analytics_tab.lock);
    return count;
}

// Comparison function for sorting by request count (descending)
static int compare_requests_desc(const void *a, const void *b)
{
    const struct cache_channel_stats *ca = (const struct cache_channel_stats *)a;
    const struct cache_channel_stats *cb = (const struct cache_channel_stats *)b;
    return (int)cb->total_requests - (int)ca->total_requests;
}

// Comparison function for sorting by hit rate (descending)
static int compare_hit_rate_desc(const void *a, const void *b)
{
    const struct cache_channel_stats *ca = (const struct cache_channel_stats *)a;
    const struct cache_channel_stats *cb = (const struct cache_channel_stats *)b;
    return (int)cb->hit_rate - (int)ca->hit_rate;
}

// Comparison function for sorting by hit rate (ascending)
static int compare_hit_rate_asc(const void *a, const void *b)
{
    const struct cache_channel_stats *ca = (const struct cache_channel_stats *)a;
    const struct cache_channel_stats *cb = (const struct cache_channel_stats *)b;
    return (int)ca->hit_rate - (int)cb->hit_rate;
}

int cache_analytics_get_top_channels(struct cache_channel_stats *stats, int max_count)
{
    pthread_mutex_lock(&cache_analytics_tab.lock);
    
    int count = cache_analytics_tab.channel_count;
    struct cache_channel_stats temp[CACHE_ANALYTICS_MAX_CHANNELS];
    memcpy(temp, cache_analytics_tab.channels, sizeof(struct cache_channel_stats) * count);
    
    pthread_mutex_unlock(&cache_analytics_tab.lock);
    
    qsort(temp, count, sizeof(struct cache_channel_stats), compare_requests_desc);
    
    int result_count = count < max_count ? count : max_count;
    memcpy(stats, temp, sizeof(struct cache_channel_stats) * result_count);
    
    return result_count;
}

int cache_analytics_get_best_cached(struct cache_channel_stats *stats, int max_count)
{
    pthread_mutex_lock(&cache_analytics_tab.lock);
    
    int count = cache_analytics_tab.channel_count;
    struct cache_channel_stats temp[CACHE_ANALYTICS_MAX_CHANNELS];
    memcpy(temp, cache_analytics_tab.channels, sizeof(struct cache_channel_stats) * count);
    
    pthread_mutex_unlock(&cache_analytics_tab.lock);
    
    qsort(temp, count, sizeof(struct cache_channel_stats), compare_hit_rate_desc);
    
    int result_count = count < max_count ? count : max_count;
    memcpy(stats, temp, sizeof(struct cache_channel_stats) * result_count);
    
    return result_count;
}

int cache_analytics_get_worst_cached(struct cache_channel_stats *stats, int max_count)
{
    pthread_mutex_lock(&cache_analytics_tab.lock);
    
    int count = cache_analytics_tab.channel_count;
    struct cache_channel_stats temp[CACHE_ANALYTICS_MAX_CHANNELS];
    memcpy(temp, cache_analytics_tab.channels, sizeof(struct cache_channel_stats) * count);
    
    pthread_mutex_unlock(&cache_analytics_tab.lock);
    
    qsort(temp, count, sizeof(struct cache_channel_stats), compare_hit_rate_asc);
    
    int result_count = count < max_count ? count : max_count;
    memcpy(stats, temp, sizeof(struct cache_channel_stats) * result_count);
    
    return result_count;
}

void cache_analytics_update_global(void)
{
    pthread_mutex_lock(&cache_analytics_tab.lock);
    
    if (cache_analytics_tab.total_requests > 0) {
        cache_analytics_tab.global_hit_rate = (cache_analytics_tab.total_cache_hits * 100) / cache_analytics_tab.total_requests;
    }
    
    cache_analytics_tab.last_update = time(NULL);
    
    pthread_mutex_unlock(&cache_analytics_tab.lock);
}

int cache_analytics_get_suggestions(char suggestions[][256], int max_suggestions)
{
    pthread_mutex_lock(&cache_analytics_tab.lock);
    
    int count = 0;
    
    // Suggestion 1: Low global hit rate
    if (cache_analytics_tab.global_hit_rate < 30 && cache_analytics_tab.total_requests > 100) {
        snprintf(suggestions[count++], 256, 
                "نسبة الكاش منخفضة (%u%%) - فعّل prefetch للقنوات الشائعة",
                cache_analytics_tab.global_hit_rate);
    }
    
    // Suggestion 2: Find channels with low hit rate but high requests
    int i;
    for (i = 0; i < cache_analytics_tab.channel_count && count < max_suggestions - 1; i++) {
        struct cache_channel_stats *ch = &cache_analytics_tab.channels[i];
        if (ch->total_requests > 50 && ch->hit_rate < 20) {
            snprintf(suggestions[count++], 256,
                    "القناة %s لديها %u طلب لكن نسبة الكاش %u%% فقط - أضفها إلى prefetch",
                    ch->channel_name, ch->total_requests, ch->hit_rate);
        }
    }
    
    // Suggestion 3: High server response time
    for (i = 0; i < cache_analytics_tab.caid_count && count < max_suggestions; i++) {
        struct cache_caid_stats *caid = &cache_analytics_tab.caids[i];
        if (caid->avg_server_time > 1000 && caid->cache_misses > 20) {
            snprintf(suggestions[count++], 256,
                    "%s: متوسط استجابة الخادم %ums - زود cache timeout",
                    caid->caid_name, caid->avg_server_time);
        }
    }
    
    pthread_mutex_unlock(&cache_analytics_tab.lock);
    return count;
}

void cache_analytics_clear(void)
{
    pthread_mutex_lock(&cache_analytics_tab.lock);
    memset(&cache_analytics_tab.caids, 0, sizeof(cache_analytics_tab.caids));
    memset(&cache_analytics_tab.channels, 0, sizeof(cache_analytics_tab.channels));
    cache_analytics_tab.caid_count = 0;
    cache_analytics_tab.channel_count = 0;
    cache_analytics_tab.total_requests = 0;
    cache_analytics_tab.total_cache_hits = 0;
    cache_analytics_tab.total_cache_misses = 0;
    cache_analytics_tab.global_hit_rate = 0;
    cache_analytics_tab.start_time = time(NULL);
    pthread_mutex_unlock(&cache_analytics_tab.lock);
}

int cache_analytics_export_csv(const char *filename)
{
    FILE *fp = fopen(filename, "w");
    if (!fp) return -1;
    
    pthread_mutex_lock(&cache_analytics_tab.lock);
    
    // Export CAID stats
    fprintf(fp, "=== CAID Statistics ===\n");
    fprintf(fp, "CAID,Name,Total Requests,Cache Hits,Cache Misses,Hit Rate,Avg Cache Time,Avg Server Time\n");
    
    int i;
    for (i = 0; i < cache_analytics_tab.caid_count; i++) {
        struct cache_caid_stats *caid = &cache_analytics_tab.caids[i];
        fprintf(fp, "%04X,%s,%u,%u,%u,%u%%,%ums,%ums\n",
                caid->caid, caid->caid_name, caid->total_requests,
                caid->cache_hits, caid->cache_misses, caid->hit_rate,
                caid->avg_cache_hit_time, caid->avg_server_time);
    }
    
    // Export channel stats
    fprintf(fp, "\n=== Channel Statistics ===\n");
    fprintf(fp, "CAID,Provider,SID,Name,Total Requests,Cache Hits,Hit Rate\n");
    
    for (i = 0; i < cache_analytics_tab.channel_count; i++) {
        struct cache_channel_stats *ch = &cache_analytics_tab.channels[i];
        fprintf(fp, "%04X,%06X,%04X,%s,%u,%u,%u%%\n",
                ch->caid, ch->provid, ch->sid, ch->channel_name,
                ch->total_requests, ch->cache_hits, ch->hit_rate);
    }
    
    pthread_mutex_unlock(&cache_analytics_tab.lock);
    
    fclose(fp);
    return 0;
}

// ============================================================================
// HTTP INTERFACE
// ============================================================================

void http_send_cache_analytics(int sock, struct http_request *req)
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
        "<title>تحليل الكاش - MultiCS</title>\n"
        "<style>\n"
        "body { font-family: 'Segoe UI', Tahoma, Arial; margin: 20px; background: #f5f5f5; }\n"
        ".container { max-width: 1400px; margin: 0 auto; background: white; padding: 20px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }\n"
        "h1 { color: #333; border-bottom: 3px solid #17a2b8; padding-bottom: 10px; }\n"
        "h2 { color: #555; margin-top: 30px; }\n"
        "h3 { color: #666; margin-top: 20px; }\n"
        "table { width: 100%%; border-collapse: collapse; margin-top: 15px; font-size: 13px; }\n"
        "th { background: #17a2b8; color: white; padding: 12px 8px; text-align: right; }\n"
        "td { padding: 10px 8px; border-bottom: 1px solid #ddd; }\n"
        "tr:hover { background: #f5f5f5; }\n"
        ".stats { display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 15px; margin: 20px 0; }\n"
        ".stat-card { background: #f8f9fa; padding: 15px; border-radius: 6px; border-left: 4px solid #17a2b8; }\n"
        ".stat-value { font-size: 28px; font-weight: bold; color: #17a2b8; }\n"
        ".stat-label { color: #666; font-size: 14px; margin-top: 5px; }\n"
        ".progress-bar { background: #e9ecef; height: 20px; border-radius: 10px; overflow: hidden; }\n"
        ".progress-fill { height: 100%%; transition: width 0.3s; }\n"
        ".btn { display: inline-block; padding: 8px 16px; background: #17a2b8; color: white; text-decoration: none; border-radius: 4px; margin: 5px; }\n"
        ".btn:hover { background: #138496; }\n"
        ".btn-danger { background: #dc3545; }\n"
        ".btn-danger:hover { background: #c82333; }\n"
        ".suggestion { background: #fff3cd; border-left: 4px solid #ffc107; padding: 15px; margin: 10px 0; border-radius: 4px; }\n"
        ".hit-rate-high { color: #28a745; font-weight: bold; }\n"
        ".hit-rate-medium { color: #ffc107; font-weight: bold; }\n"
        ".hit-rate-low { color: #dc3545; font-weight: bold; }\n"
        "</style>\n"
        "</head>\n"
        "<body>\n"
        "<div class='container'>\n"
        "<h1>💾 تحليل الكاش المتقدم</h1>\n"
    );
    tcp_writestr(&tcpbuf, sock, buf);
    
    // Global statistics
    snprintf(buf, sizeof(buf),
        "<div class='stats'>\n"
        "<div class='stat-card'><div class='stat-value'>%u</div><div class='stat-label'>إجمالي الطلبات</div></div>\n"
        "<div class='stat-card'><div class='stat-value'>%u</div><div class='stat-label'>Cache Hits</div></div>\n"
        "<div class='stat-card'><div class='stat-value'>%u</div><div class='stat-label'>Cache Misses</div></div>\n"
        "<div class='stat-card'><div class='stat-value'>%u%%</div><div class='stat-label'>نسبة الكاش العامة</div></div>\n"
        "</div>\n",
        cache_analytics_tab.total_requests,
        cache_analytics_tab.total_cache_hits,
        cache_analytics_tab.total_cache_misses,
        cache_analytics_tab.global_hit_rate
    );
    tcp_writestr(&tcpbuf, sock, buf);
    
    // Action buttons
    snprintf(buf, sizeof(buf),
        "<div style='text-align: center; margin: 20px 0;'>\n"
        "<a href='/cache-analytics' class='btn'>🔄 تحديث</a>\n"
        "<a href='/cache-analytics?action=clear' class='btn btn-danger' onclick='return confirm(\"هل أنت متأكد؟\")'>🗑️ مسح البيانات</a>\n"
        "<a href='/' class='btn'>🏠 الرئيسية</a>\n"
        "</div>\n"
    );
    tcp_writestr(&tcpbuf, sock, buf);
    
    // Handle clear action
    char *action = isset_get(req, "action");
    if (action && strcmp(action, "clear") == 0) {
        cache_analytics_clear();
        snprintf(buf, sizeof(buf), "<div style='background: #d4edda; padding: 15px; border-radius: 4px; margin: 20px 0;'>✅ تم مسح جميع البيانات</div>\n");
        tcp_writestr(&tcpbuf, sock, buf);
    }
    
    // Suggestions
    char suggestions[10][256];
    int suggestion_count = cache_analytics_get_suggestions(suggestions, 10);
    
    if (suggestion_count > 0) {
        snprintf(buf, sizeof(buf), "<h2>💡 اقتراحات تحسين الكاش</h2>\n");
        tcp_writestr(&tcpbuf, sock, buf);
        
        for (i = 0; i < suggestion_count; i++) {
            snprintf(buf, sizeof(buf), "<div class='suggestion'><strong>%d.</strong> %s</div>\n", i + 1, suggestions[i]);
            tcp_writestr(&tcpbuf, sock, buf);
        }
    }
    
    // CAID statistics
    snprintf(buf, sizeof(buf),
        "<h2>📊 إحصائيات CAID</h2>\n"
        "<table>\n"
        "<tr>\n"
        "<th>CAID</th>\n"
        "<th>الاسم</th>\n"
        "<th>الطلبات</th>\n"
        "<th>Cache Hits</th>\n"
        "<th>Cache Misses</th>\n"
        "<th>نسبة الكاش</th>\n"
        "<th>متوسط وقت الكاش</th>\n"
        "<th>متوسط وقت الخادم</th>\n"
        "</tr>\n"
    );
    tcp_writestr(&tcpbuf, sock, buf);
    
    struct cache_caid_stats caid_stats[CACHE_ANALYTICS_MAX_CAIDS];
    int caid_count = cache_analytics_get_caid_stats(caid_stats, CACHE_ANALYTICS_MAX_CAIDS);
    
    for (i = 0; i < caid_count; i++) {
        struct cache_caid_stats *caid = &caid_stats[i];
        
        const char *hit_rate_class = "hit-rate-low";
        if (caid->hit_rate >= 70) hit_rate_class = "hit-rate-high";
        else if (caid->hit_rate >= 40) hit_rate_class = "hit-rate-medium";
        
        snprintf(buf, sizeof(buf),
            "<tr>\n"
            "<td><strong>%04X</strong></td>\n"
            "<td>%s</td>\n"
            "<td>%u</td>\n"
            "<td>%u</td>\n"
            "<td>%u</td>\n"
            "<td class='%s'>%u%%</td>\n"
            "<td>%ums</td>\n"
            "<td>%ums</td>\n"
            "</tr>\n",
            caid->caid,
            caid->caid_name,
            caid->total_requests,
            caid->cache_hits,
            caid->cache_misses,
            hit_rate_class,
            caid->hit_rate,
            caid->avg_cache_hit_time,
            caid->avg_server_time
        );
        tcp_writestr(&tcpbuf, sock, buf);
    }
    
    snprintf(buf, sizeof(buf), "</table>\n");
    tcp_writestr(&tcpbuf, sock, buf);
    
    // Top channels by request count
    snprintf(buf, sizeof(buf),
        "<h2>🔥 أكثر القنوات طلباً</h2>\n"
        "<table>\n"
        "<tr>\n"
        "<th>#</th>\n"
        "<th>القناة</th>\n"
        "<th>CAID</th>\n"
        "<th>Provider</th>\n"
        "<th>SID</th>\n"
        "<th>الطلبات</th>\n"
        "<th>Cache Hits</th>\n"
        "<th>نسبة الكاش</th>\n"
        "</tr>\n"
    );
    tcp_writestr(&tcpbuf, sock, buf);
    
    struct cache_channel_stats top_channels[20];
    int top_count = cache_analytics_get_top_channels(top_channels, 20);
    
    for (i = 0; i < top_count; i++) {
        struct cache_channel_stats *ch = &top_channels[i];
        
        const char *hit_rate_class = "hit-rate-low";
        if (ch->hit_rate >= 70) hit_rate_class = "hit-rate-high";
        else if (ch->hit_rate >= 40) hit_rate_class = "hit-rate-medium";
        
        snprintf(buf, sizeof(buf),
            "<tr>\n"
            "<td>%d</td>\n"
            "<td><strong>%s</strong></td>\n"
            "<td>%04X</td>\n"
            "<td>%06X</td>\n"
            "<td>%04X</td>\n"
            "<td>%u</td>\n"
            "<td>%u</td>\n"
            "<td class='%s'>%u%%</td>\n"
            "</tr>\n",
            i + 1,
            ch->channel_name,
            ch->caid,
            ch->provid,
            ch->sid,
            ch->total_requests,
            ch->cache_hits,
            hit_rate_class,
            ch->hit_rate
        );
        tcp_writestr(&tcpbuf, sock, buf);
    }
    
    snprintf(buf, sizeof(buf),
        "</table>\n"
        "<script>setTimeout(function(){ location.reload(); }, 20000);</script>\n"
        "</div>\n"
        "</body>\n"
        "</html>\n"
    );
    tcp_writestr(&tcpbuf, sock, buf);
}
