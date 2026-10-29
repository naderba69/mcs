/* diagnostics.c - Advanced Analytics and Diagnostics System Implementation
 * 
 * PURPOSE: Comprehensive system diagnostics and troubleshooting
 */

#include "common.h"
#include "debug.h"
#include "diagnostics.h"
#include "httpserver.h"
#include "server-health.h"
#include "caid-optimizations.h"
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

// Global diagnostics report
struct diagnostic_report diag_report;

// ============================================================================
// INITIALIZATION
// ============================================================================

void diagnostics_init(void)
{
    memset(&diag_report, 0, sizeof(diag_report));
    diag_report.stats.start_time = time(NULL);
    diag_report.stats.min_response_time = 999999;
    strcpy(diag_report.status, "Initializing");
    
    debugf(0, " [DIAG] Diagnostics system initialized\n");
}

// ============================================================================
// UPDATE & CALCULATIONS
// ============================================================================

void diagnostics_update(void)
{
    time_t now = time(NULL);
    diag_report.stats.last_update = now;
    diag_report.stats.uptime_seconds = now - diag_report.stats.start_time;
    
    // Calculate rates
    diagnostics_calculate_rates(&diag_report.stats);
    
    // Detect problems
    diagnostics_detect_problems(&diag_report);
    
    // Generate recommendations
    diagnostics_generate_recommendations(&diag_report);
    
    // Update health score
    diag_report.stats.system_health = diagnostics_get_health();
}

void diagnostics_calculate_rates(struct diagnostic_stats *stats)
{
    // ECM success rate
    if (stats->total_ecm_requests > 0) {
        stats->ecm_success_rate = (stats->successful_ecm * 100) / stats->total_ecm_requests;
    } else {
        stats->ecm_success_rate = 0;
    }
    
    // Cache hit rate
    uint32_t total_cache = stats->cache_hits + stats->cache_misses;
    if (total_cache > 0) {
        stats->cache_hit_rate = (stats->cache_hits * 100) / total_cache;
    } else {
        stats->cache_hit_rate = 0;
    }
    
    // CW valid rate
    if (stats->total_cw_received > 0) {
        stats->cw_valid_rate = (stats->valid_cw * 100) / stats->total_cw_received;
    } else {
        stats->cw_valid_rate = 0;
    }
}

// ============================================================================
// PROBLEM DETECTION
// ============================================================================

void diagnostics_detect_problems(struct diagnostic_report *report)
{
    report->issue_count = 0;
    
    // Check 1: Low ECM success rate
    if (report->stats.ecm_success_rate < 50 && report->stats.total_ecm_requests > 10) {
        diagnostics_add_issue(
            "ECM success rate is very low",
            "Critical",
            "Check server connections and CAID configurations. Verify that servers are responding.",
            1001
        );
    }
    
    // Check 2: Low cache hit rate
    if (report->stats.cache_hit_rate < 20 && (report->stats.cache_hits + report->stats.cache_misses) > 50) {
        diagnostics_add_issue(
            "Cache hit rate is low",
            "Warning",
            "Consider enabling prefetch for popular channels. Check cache timeout settings.",
            1002
        );
    }
    
    // Check 3: High timeout rate
    if (report->stats.total_ecm_requests > 0) {
        uint8_t timeout_rate = (report->stats.timeout_ecm * 100) / report->stats.total_ecm_requests;
        if (timeout_rate > 30) {
            diagnostics_add_issue(
                "High ECM timeout rate",
                "Critical",
                "Servers are not responding in time. Check server health and increase timeouts.",
                1003
            );
        }
    }
    
    // Check 4: Many disconnected servers
    if (report->stats.total_servers > 0) {
        uint8_t disconnect_rate = (report->stats.disconnected_servers * 100) / report->stats.total_servers;
        if (disconnect_rate > 50) {
            diagnostics_add_issue(
                "Many servers are disconnected",
                "Critical",
                "Check network connectivity and server credentials. Verify server addresses.",
                1004
            );
        }
    }
    
    // Check 5: High invalid CW rate
    if (report->stats.total_cw_received > 0) {
        uint8_t invalid_rate = (report->stats.invalid_cw * 100) / report->stats.total_cw_received;
        if (invalid_rate > 20) {
            diagnostics_add_issue(
                "High invalid CW rate",
                "Warning",
                "Servers may be sending bad CWs. Check server reliability and enable stricter validation.",
                1005
            );
        }
    }
    
    // Check 6: Slow response times
    if (report->stats.avg_response_time > 1000 && report->stats.successful_ecm > 10) {
        diagnostics_add_issue(
            "Average response time is slow",
            "Warning",
            "Consider using faster servers or enabling parallel ECM requests.",
            1006
        );
    }
    
    // Check 7: Many lazy servers
    if (report->stats.lazy_servers > 0) {
        diagnostics_add_issue(
            "Lazy servers detected",
            "Info",
            "Some servers have low success rates. Consider removing unreliable servers.",
            1007
        );
    }
}

// ============================================================================
// RECOMMENDATIONS ENGINE
// ============================================================================

void diagnostics_generate_recommendations(struct diagnostic_report *report)
{
    report->recommendation_count = 0;
    
    // Recommendation based on ECM success rate
    if (report->stats.ecm_success_rate < 70) {
        diagnostics_add_recommendation(
            "Enable server health monitoring to automatically prioritize reliable servers"
        );
    }
    
    // Recommendation based on cache hit rate
    if (report->stats.cache_hit_rate < 30) {
        diagnostics_add_recommendation(
            "Enable predictive warming for frequently watched channels"
        );
    }
    
    // Recommendation based on response time
    if (report->stats.avg_response_time > 800) {
        diagnostics_add_recommendation(
            "Consider enabling parallel ECM requests to improve response times"
        );
    }
    
    // Recommendation based on timeout rate
    if (report->stats.timeout_ecm > report->stats.successful_ecm) {
        diagnostics_add_recommendation(
            "Increase server timeouts or add more reliable servers"
        );
    }
    
    // General recommendations
    if (report->stats.uptime_seconds > 3600 && report->stats.system_health > 80) {
        diagnostics_add_recommendation(
            "System is performing well. Continue monitoring for optimal performance."
        );
    }
}

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

void diagnostics_add_issue(const char *description, const char *severity, 
                          const char *recommendation, int code)
{
    if (diag_report.issue_count >= 20) return;
    
    struct diagnostic_issue *issue = &diag_report.issues[diag_report.issue_count++];
    strncpy(issue->description, description, sizeof(issue->description) - 1);
    strncpy(issue->severity, severity, sizeof(issue->severity) - 1);
    strncpy(issue->recommendation, recommendation, sizeof(issue->recommendation) - 1);
    issue->issue_code = code;
}

void diagnostics_add_recommendation(const char *rec)
{
    if (diag_report.recommendation_count >= 10) return;
    
    strncpy(diag_report.recommendations[diag_report.recommendation_count], 
            rec, sizeof(diag_report.recommendations[0]) - 1);
    diag_report.recommendation_count++;
}

uint8_t diagnostics_get_health(void)
{
    int score = 100;
    
    // Deduct for low ECM success rate
    if (diag_report.stats.ecm_success_rate < 50) {
        score -= 40;
    } else if (diag_report.stats.ecm_success_rate < 70) {
        score -= 20;
    }
    
    // Deduct for high timeout rate
    if (diag_report.stats.total_ecm_requests > 0) {
        uint8_t timeout_rate = (diag_report.stats.timeout_ecm * 100) / diag_report.stats.total_ecm_requests;
        if (timeout_rate > 30) {
            score -= 30;
        } else if (timeout_rate > 15) {
            score -= 15;
        }
    }
    
    // Deduct for disconnected servers
    if (diag_report.stats.total_servers > 0) {
        uint8_t disconnect_rate = (diag_report.stats.disconnected_servers * 100) / diag_report.stats.total_servers;
        if (disconnect_rate > 50) {
            score -= 20;
        } else if (disconnect_rate > 25) {
            score -= 10;
        }
    }
    
    // Ensure score is in range 0-100
    if (score < 0) score = 0;
    if (score > 100) score = 100;
    
    return (uint8_t)score;
}

const char* diagnostics_get_status(void)
{
    uint8_t health = diag_report.stats.system_health;
    
    if (health >= 80) {
        strcpy(diag_report.status, "Healthy");
    } else if (health >= 50) {
        strcpy(diag_report.status, "Degraded");
    } else {
        strcpy(diag_report.status, "Critical");
    }
    
    return diag_report.status;
}

void diagnostics_generate_report(struct diagnostic_report *report)
{
    diagnostics_update();
    *report = diag_report;
}

// ============================================================================
// HTTP INTERFACE
// ============================================================================

void http_send_diagnostics(int sock, struct http_request *req)
{
    struct tcp_buffer_data tcpbuf;
    char buf[8192];
    char temp[1024];
    int i;
    
    tcp_init(&tcpbuf);
    
    // Update diagnostics
    diagnostics_update();
    
    // Send HTTP header
    snprintf(buf, sizeof(buf),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Connection: close\r\n\r\n"
    );
    tcp_writestr(&tcpbuf, sock, buf);
    
    // Page title
    snprintf(buf, sizeof(buf),
        "<!DOCTYPE html>\n"
        "<html>\n"
        "<head>\n"
        "<meta charset='utf-8'>\n"
        "<meta name='viewport' content='width=device-width, initial-scale=1'>\n"
        "<title>التحليلات والتشخيص - MultiCS</title>\n"
        "<style>\n"
        "body { font-family: Arial, sans-serif; margin: 20px; background: #f5f5f5; }\n"
        ".container { max-width: 1200px; margin: 0 auto; background: white; padding: 20px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }\n"
        "h1 { color: #333; border-bottom: 3px solid #007bff; padding-bottom: 10px; }\n"
        "h2 { color: #555; margin-top: 30px; }\n"
        ".health-score { font-size: 48px; font-weight: bold; text-align: center; margin: 20px 0; }\n"
        ".health-good { color: #28a745; }\n"
        ".health-warning { color: #ffc107; }\n"
        ".health-critical { color: #dc3545; }\n"
        ".stat-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 15px; margin: 20px 0; }\n"
        ".stat-card { background: #f8f9fa; padding: 15px; border-radius: 6px; border-left: 4px solid #007bff; }\n"
        ".stat-value { font-size: 24px; font-weight: bold; color: #333; }\n"
        ".stat-label { color: #666; font-size: 14px; margin-top: 5px; }\n"
        ".issue { background: #fff3cd; border-left: 4px solid #ffc107; padding: 15px; margin: 10px 0; border-radius: 4px; }\n"
        ".issue-critical { background: #f8d7da; border-left-color: #dc3545; }\n"
        ".issue-info { background: #d1ecf1; border-left-color: #17a2b8; }\n"
        ".issue-title { font-weight: bold; margin-bottom: 5px; }\n"
        ".recommendation { background: #d4edda; border-left: 4px solid #28a745; padding: 15px; margin: 10px 0; border-radius: 4px; }\n"
        ".progress-bar { background: #e9ecef; height: 20px; border-radius: 10px; overflow: hidden; margin: 10px 0; }\n"
        ".progress-fill { height: 100%%; transition: width 0.3s; }\n"
        ".btn { display: inline-block; padding: 10px 20px; background: #007bff; color: white; text-decoration: none; border-radius: 4px; margin: 5px; }\n"
        ".btn:hover { background: #0056b3; }\n"
        "</style>\n"
        "</head>\n"
        "<body>\n"
        "<div class='container'>\n"
        "<h1>🔍 التحليلات والتشخيص المتقدم</h1>\n"
    );
    tcp_writestr(&tcpbuf, sock, buf);
    
    // Health score
    uint8_t health = diag_report.stats.system_health;
    const char *health_class = health >= 80 ? "health-good" : (health >= 50 ? "health-warning" : "health-critical");
    
    snprintf(buf, sizeof(buf),
        "<div class='health-score %s'>%d%%</div>\n"
        "<p style='text-align: center; font-size: 18px; color: #666;'>حالة النظام: <strong>%s</strong></p>\n"
        "<div class='progress-bar'>\n"
        "<div class='progress-fill %s' style='width: %d%%; background: %s;'></div>\n"
        "</div>\n",
        health_class, health,
        diagnostics_get_status(),
        health_class, health,
        health >= 80 ? "#28a745" : (health >= 50 ? "#ffc107" : "#dc3545")
    );
    tcp_writestr(&tcpbuf, sock, buf);
    
    // Statistics grid
    snprintf(buf, sizeof(buf),
        "<h2>📊 الإحصائيات الحية</h2>\n"
        "<div class='stat-grid'>\n"
        "<div class='stat-card'><div class='stat-value'>%u</div><div class='stat-label'>إجمالي طلبات ECM</div></div>\n"
        "<div class='stat-card'><div class='stat-value'>%u</div><div class='stat-label'>طلبات ناجحة</div></div>\n"
        "<div class='stat-card'><div class='stat-value'>%u</div><div class='stat-label'>طلبات فاشلة</div></div>\n"
        "<div class='stat-card'><div class='stat-value'>%u</div><div class='stat-label'>انتهاء المهلة</div></div>\n"
        "<div class='stat-card'><div class='stat-value'>%d%%</div><div class='stat-label'>نسبة النجاح</div></div>\n"
        "<div class='stat-card'><div class='stat-value'>%d%%</div><div class='stat-label'>نسبة الكاش</div></div>\n"
        "<div class='stat-card'><div class='stat-value'>%ums</div><div class='stat-label'>متوسط الاستجابة</div></div>\n"
        "<div class='stat-card'><div class='stat-value'>%u</div><div class='stat-label'>الخوادم المتصلة</div></div>\n"
        "</div>\n",
        diag_report.stats.total_ecm_requests,
        diag_report.stats.successful_ecm,
        diag_report.stats.failed_ecm,
        diag_report.stats.timeout_ecm,
        diag_report.stats.ecm_success_rate,
        diag_report.stats.cache_hit_rate,
        diag_report.stats.avg_response_time,
        diag_report.stats.connected_servers
    );
    tcp_writestr(&tcpbuf, sock, buf);
    
    // Issues section
    if (diag_report.issue_count > 0) {
        snprintf(buf, sizeof(buf), "<h2>⚠️ المشاكل المكتشفة</h2>\n");
        tcp_writestr(&tcpbuf, sock, buf);
        
        for (i = 0; i < diag_report.issue_count; i++) {
            struct diagnostic_issue *issue = &diag_report.issues[i];
            const char *issue_class = strcmp(issue->severity, "Critical") == 0 ? "issue-critical" : 
                                     (strcmp(issue->severity, "Info") == 0 ? "issue-info" : "");
            
            snprintf(buf, sizeof(buf),
                "<div class='issue %s'>\n"
                "<div class='issue-title'>%s [%s]</div>\n"
                "<div>%s</div>\n"
                "<div style='margin-top: 10px; color: #666;'><strong>التوصية:</strong> %s</div>\n"
                "</div>\n",
                issue_class,
                issue->description,
                issue->severity,
                issue->recommendation,
                issue->recommendation
            );
            tcp_writestr(&tcpbuf, sock, buf);
        }
    }
    
    // Recommendations section
    if (diag_report.recommendation_count > 0) {
        snprintf(buf, sizeof(buf), "<h2>💡 التوصيات</h2>\n");
        tcp_writestr(&tcpbuf, sock, buf);
        
        for (i = 0; i < diag_report.recommendation_count; i++) {
            snprintf(buf, sizeof(buf),
                "<div class='recommendation'>\n"
                "<strong>%d.</strong> %s\n"
                "</div>\n",
                i + 1,
                diag_report.recommendations[i]
            );
            tcp_writestr(&tcpbuf, sock, buf);
        }
    }
    
    // Refresh button
    snprintf(buf, sizeof(buf),
        "<div style='text-align: center; margin-top: 30px;'>\n"
        "<a href='/diagnostics' class='btn'>🔄 تحديث التحليلات</a>\n"
        "<a href='/' class='btn'>🏠 الصفحة الرئيسية</a>\n"
        "</div>\n"
        "<script>setTimeout(function(){ location.reload(); }, 30000);</script>\n"
        "</div>\n"
        "</body>\n"
        "</html>\n"
    );
    tcp_writestr(&tcpbuf, sock, buf);
}
