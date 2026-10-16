#ifndef CWANOMALY_H
#define CWANOMALY_H

/*
 * CW Anomaly Detection — كشف الشذوذ
 * 
 * التوصية قصيرة المدى #1: إضافة anomaly detection
 * 
 * المشكلة: threshold-based alerting فقط
 * الحل: statistical anomaly detection
 * 
 * الميزة: كشف مبكر، تقليل false positives، تنبيهات ذكية
 */

#include <stdint.h>
#include <math.h>
#include <string.h>

#define CWANOMALY_MAX_METRICS 50
#define CWANOMALY_HISTORY_SIZE 100
#define CWANOMALY_ZSCORE_THRESHOLD 3.0

/* Metric types */
#define CWANOMALY_METRIC_CW_COUNT      0
#define CWANOMALY_METRIC_LATENCY       1
#define CWANOMALY_METRIC_ERROR_RATE    2
#define CWANOMALY_METRIC_PEER_SCORE    3
#define CWANOMALY_METRIC_CACHE_HIT     4

/* Anomaly severity */
#define CWANOMALY_SEVERITY_NORMAL   0
#define CWANOMALY_SEVERITY_LOW      1
#define CWANOMALY_SEVERITY_MEDIUM   2
#define CWANOMALY_SEVERITY_HIGH     3
#define CWANOMALY_SEVERITY_CRITICAL 4

/* Metric history */
typedef struct {
    double values[CWANOMALY_HISTORY_SIZE];
    int count;
    int index;
    double mean;
    double stddev;
    double min;
    double max;
} cwanomaly_metric_t;

/* Anomaly event */
typedef struct {
    int metric_id;
    double value;
    double zscore;
    int severity;
    time_t timestamp;
    char description[256];
} cwanomaly_event_t;

/* Anomaly state */
typedef struct {
    cwanomaly_metric_t metrics[CWANOMALY_MAX_METRICS];
    int metric_count;
    
    cwanomaly_event_t recent_anomalies[100];
    int anomaly_index;
    
    int total_anomalies;
    int total_critical;
    int total_high;
    int total_medium;
    int total_low;
    
    int enabled;
    double zscore_threshold;
} cwanomaly_state_t;

static cwanomaly_state_t g_cwanomaly_state = {0};

/* تهيئة */
static inline void cwanomaly_init(void) {
    memset(&g_cwanomaly_state, 0, sizeof(g_cwanomaly_state));
    g_cwanomaly_state.enabled = 1;
    g_cwanomaly_state.zscore_threshold = CWANOMALY_ZSCORE_THRESHOLD;
}

/* تسجيل metric */
static inline int cwanomaly_register_metric(const char *name) {
    if (g_cwanomaly_state.metric_count >= CWANOMALY_MAX_METRICS) {
        return -1;
    }
    
    int id = g_cwanomaly_state.metric_count++;
    cwanomaly_metric_t *metric = &g_cwanomaly_state.metrics[id];
    
    memset(metric, 0, sizeof(cwanomaly_metric_t));
    metric->min = 1e9;
    metric->max = -1e9;
    
    return id;
}

/* تحديث الإحصائيات */
static inline void cwanomaly_update_stats(cwanomaly_metric_t *metric) {
    if (metric->count == 0) return;
    
    /* Calculate mean */
    double sum = 0.0;
    int n = (metric->count < CWANOMALY_HISTORY_SIZE) ? metric->count : CWANOMALY_HISTORY_SIZE;
    int i;
    for (i = 0; i < n; i++) {
        sum += metric->values[i];
    }
    metric->mean = sum / n;
    
    /* Calculate stddev */
    double variance = 0.0;
    for (i = 0; i < n; i++) {
        double diff = metric->values[i] - metric->mean;
        variance += diff * diff;
    }
    metric->stddev = sqrt(variance / n);
}

/* إضافة قيمة */
static inline void cwanomaly_add_value(int metric_id, double value) {
    if (!g_cwanomaly_state.enabled || metric_id < 0 || metric_id >= g_cwanomaly_state.metric_count) {
        return;
    }
    
    cwanomaly_metric_t *metric = &g_cwanomaly_state.metrics[metric_id];
    
    /* Add to circular buffer */
    metric->values[metric->index] = value;
    metric->index = (metric->index + 1) % CWANOMALY_HISTORY_SIZE;
    if (metric->count < CWANOMALY_HISTORY_SIZE) {
        metric->count++;
    }
    
    /* Update min/max */
    if (value < metric->min) metric->min = value;
    if (value > metric->max) metric->max = value;
    
    /* Update statistics */
    cwanomaly_update_stats(metric);
}

/* حساب z-score */
static inline double cwanomaly_zscore(int metric_id, double value) {
    if (metric_id < 0 || metric_id >= g_cwanomaly_state.metric_count) {
        return 0.0;
    }
    
    cwanomaly_metric_t *metric = &g_cwanomaly_state.metrics[metric_id];
    
    if (metric->count < 10 || metric->stddev < 1e-6) {
        return 0.0;  /* Not enough data */
    }
    
    return (value - metric->mean) / metric->stddev;
}

/* كشف الشذوذ */
static inline int cwanomaly_detect(int metric_id, double value) {
    if (!g_cwanomaly_state.enabled || metric_id < 0 || metric_id >= g_cwanomaly_state.metric_count) {
        return 0;
    }
    
    double zscore = cwanomaly_zscore(metric_id, value);
    double abs_zscore = (zscore < 0) ? -zscore : zscore;
    
    if (abs_zscore >= g_cwanomaly_state.zscore_threshold) {
        /* Anomaly detected */
        int severity;
        if (abs_zscore >= 5.0) {
            severity = CWANOMALY_SEVERITY_CRITICAL;
            g_cwanomaly_state.total_critical++;
        } else if (abs_zscore >= 4.0) {
            severity = CWANOMALY_SEVERITY_HIGH;
            g_cwanomaly_state.total_high++;
        } else if (abs_zscore >= 3.0) {
            severity = CWANOMALY_SEVERITY_MEDIUM;
            g_cwanomaly_state.total_medium++;
        } else {
            severity = CWANOMALY_SEVERITY_LOW;
            g_cwanomaly_state.total_low++;
        }
        
        /* Record anomaly */
        cwanomaly_event_t *event = &g_cwanomaly_state.recent_anomalies[g_cwanomaly_state.anomaly_index];
        event->metric_id = metric_id;
        event->value = value;
        event->zscore = zscore;
        event->severity = severity;
        event->timestamp = time(NULL);
        snprintf(event->description, sizeof(event->description),
                 "Anomaly detected: metric=%d, value=%.2f, zscore=%.2f",
                 metric_id, value, zscore);
        
        g_cwanomaly_state.anomaly_index = 
            (g_cwanomaly_state.anomaly_index + 1) % 100;
        g_cwanomaly_state.total_anomalies++;
        
        return severity;
    }
    
    return CWANOMALY_SEVERITY_NORMAL;
}

/* الحصول على إحصائيات metric */
static inline void cwanomaly_get_metric_stats(int metric_id, double *mean, double *stddev, 
                                                double *min, double *max, int *count) {
    if (metric_id < 0 || metric_id >= g_cwanomaly_state.metric_count) {
        return;
    }
    
    cwanomaly_metric_t *metric = &g_cwanomaly_state.metrics[metric_id];
    
    if (mean) *mean = metric->mean;
    if (stddev) *stddev = metric->stddev;
    if (min) *min = metric->min;
    if (max) *max = metric->max;
    if (count) *count = metric->count;
}

/* الحصول على الإحصائيات العامة */
static inline void cwanomaly_get_stats(int *total, int *critical, int *high, int *medium, int *low) {
    if (total) *total = g_cwanomaly_state.total_anomalies;
    if (critical) *critical = g_cwanomaly_state.total_critical;
    if (high) *high = g_cwanomaly_state.total_high;
    if (medium) *medium = g_cwanomaly_state.total_medium;
    if (low) *low = g_cwanomaly_state.total_low;
}

/* تصفير الإحصائيات */
static inline void cwanomaly_reset_stats(void) {
    g_cwanomaly_state.total_anomalies = 0;
    g_cwanomaly_state.total_critical = 0;
    g_cwanomaly_state.total_high = 0;
    g_cwanomaly_state.total_medium = 0;
    g_cwanomaly_state.total_low = 0;
}

/* تعيين threshold */
static inline void cwanomaly_set_threshold(double threshold) {
    if (threshold > 0.0) {
        g_cwanomaly_state.zscore_threshold = threshold;
    }
}

#endif /* CWANOMALY_H */
