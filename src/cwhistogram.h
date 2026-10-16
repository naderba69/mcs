#ifndef CWHISTOGRAM_H
#define CWHISTOGRAM_H

/*
 * CW Histogram — المدرجات التكرارية والنسب المئوية
 * 
 * التوصية قصيرة المدى #3: إضافة histograms و percentiles
 * 
 * المشكلة: limited metrics (27 counters فقط)
 * الحل: histograms مع p50, p95, p99
 * 
 * الميزة: رؤية أفضل للأداء، تحديد bottlenecks
 */

#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#define CWHISTOGRAM_MAX_BUCKETS 100
#define CWHISTOGRAM_MAX_HISTOGRAMS 20

/* Histogram types */
#define CWHISTOGRAM_LINEAR    0
#define CWHISTOGRAM_LOG       1
#define CWHISTOGRAM_CUSTOM    2

/* Histogram structure */
typedef struct {
    char name[64];
    int type;
    
    /* Buckets */
    double bucket_bounds[CWHISTOGRAM_MAX_BUCKETS + 1];
    uint64_t bucket_counts[CWHISTOGRAM_MAX_BUCKETS];
    int bucket_count;
    
    /* Statistics */
    uint64_t total_count;
    double sum;
    double min;
    double max;
    double mean;
    
    /* Percentiles (cached) */
    double p50;
    double p95;
    double p99;
    int percentiles_valid;
    
    /* Configuration */
    double min_bound;
    double max_bound;
} cwhistogram_t;

/* Global state */
typedef struct {
    cwhistogram_t histograms[CWHISTOGRAM_MAX_HISTOGRAMS];
    int histogram_count;
} cwhistogram_state_t;

static cwhistogram_state_t g_cwhistogram_state = {0};

/* تهيئة */
static inline void cwhistogram_init(void) {
    memset(&g_cwhistogram_state, 0, sizeof(g_cwhistogram_state));
}

/* إنشاء histogram خطي */
static inline int cwhistogram_create_linear(const char *name, double min, double max, int buckets) {
    if (g_cwhistogram_state.histogram_count >= CWHISTOGRAM_MAX_HISTOGRAMS) {
        return -1;
    }
    
    if (buckets > CWHISTOGRAM_MAX_BUCKETS) {
        buckets = CWHISTOGRAM_MAX_BUCKETS;
    }
    
    int id = g_cwhistogram_state.histogram_count++;
    cwhistogram_t *hist = &g_cwhistogram_state.histograms[id];
    
    memset(hist, 0, sizeof(cwhistogram_t));
    strncpy(hist->name, name, sizeof(hist->name) - 1);
    hist->type = CWHISTOGRAM_LINEAR;
    hist->bucket_count = buckets;
    hist->min_bound = min;
    hist->max_bound = max;
    hist->min = 1e9;
    hist->max = -1e9;
    
    /* Calculate bucket bounds */
    double step = (max - min) / buckets;
    int i; for (i = 0; i <= buckets; i++) {
        hist->bucket_bounds[i] = min + (i * step);
    }
    
    return id;
}

/* إنشاء histogram لوغاريتمي */
static inline int cwhistogram_create_log(const char *name, double min, double max, int buckets) {
    if (g_cwhistogram_state.histogram_count >= CWHISTOGRAM_MAX_HISTOGRAMS) {
        return -1;
    }
    
    if (buckets > CWHISTOGRAM_MAX_BUCKETS) {
        buckets = CWHISTOGRAM_MAX_BUCKETS;
    }
    
    int id = g_cwhistogram_state.histogram_count++;
    cwhistogram_t *hist = &g_cwhistogram_state.histograms[id];
    
    memset(hist, 0, sizeof(cwhistogram_t));
    strncpy(hist->name, name, sizeof(hist->name) - 1);
    hist->type = CWHISTOGRAM_LOG;
    hist->bucket_count = buckets;
    hist->min_bound = min;
    hist->max_bound = max;
    hist->min = 1e9;
    hist->max = -1e9;
    
    /* Calculate log bucket bounds */
    double log_min = (min > 0) ? log10(min) : 0;
    double log_max = log10(max);
    double step = (log_max - log_min) / buckets;
    
    int i; for (i = 0; i <= buckets; i++) {
        hist->bucket_bounds[i] = pow(10, log_min + (i * step));
    }
    
    return id;
}

/* إضافة قيمة */
static inline void cwhistogram_observe(int hist_id, double value) {
    if (hist_id < 0 || hist_id >= g_cwhistogram_state.histogram_count) {
        return;
    }
    
    cwhistogram_t *hist = &g_cwhistogram_state.histograms[hist_id];
    
    /* Update statistics */
    hist->total_count++;
    hist->sum += value;
    if (value < hist->min) hist->min = value;
    if (value > hist->max) hist->max = value;
    hist->mean = hist->sum / hist->total_count;
    
    /* Find bucket */
    int bucket = -1;
    int i; for (i = 0; i < hist->bucket_count; i++) {
        if (value < hist->bucket_bounds[i + 1]) {
            bucket = i;
            break;
        }
    }
    
    if (bucket == -1) {
        bucket = hist->bucket_count - 1;  /* Last bucket */
    }
    
    hist->bucket_counts[bucket]++;
    hist->percentiles_valid = 0;  /* Invalidate cached percentiles */
}

/* حساب percentile */
static inline double cwhistogram_percentile(int hist_id, double percentile) {
    if (hist_id < 0 || hist_id >= g_cwhistogram_state.histogram_count) {
        return 0.0;
    }
    
    cwhistogram_t *hist = &g_cwhistogram_state.histograms[hist_id];
    
    if (hist->total_count == 0) {
        return 0.0;
    }
    
    double target = hist->total_count * percentile / 100.0;
    uint64_t cumulative = 0;
    
    int i; for (i = 0; i < hist->bucket_count; i++) {
        cumulative += hist->bucket_counts[i];
        if (cumulative >= target) {
            /* Interpolate within bucket */
            double bucket_start = hist->bucket_bounds[i];
            double bucket_end = hist->bucket_bounds[i + 1];
            double fraction = (double)(target - (cumulative - hist->bucket_counts[i])) / 
                             hist->bucket_counts[i];
            return bucket_start + (bucket_end - bucket_start) * fraction;
        }
    }
    
    return hist->max_bound;
}

/* تحديث percentiles */
static inline void cwhistogram_update_percentiles(int hist_id) {
    if (hist_id < 0 || hist_id >= g_cwhistogram_state.histogram_count) {
        return;
    }
    
    cwhistogram_t *hist = &g_cwhistogram_state.histograms[hist_id];
    
    hist->p50 = cwhistogram_percentile(hist_id, 50.0);
    hist->p95 = cwhistogram_percentile(hist_id, 95.0);
    hist->p99 = cwhistogram_percentile(hist_id, 99.0);
    hist->percentiles_valid = 1;
}

/* الحصول على الإحصائيات */
static inline void cwhistogram_get_stats(int hist_id, uint64_t *count, double *sum,
                                          double *min, double *max, double *mean,
                                          double *p50, double *p95, double *p99) {
    if (hist_id < 0 || hist_id >= g_cwhistogram_state.histogram_count) {
        return;
    }
    
    cwhistogram_t *hist = &g_cwhistogram_state.histograms[hist_id];
    
    if (!hist->percentiles_valid) {
        cwhistogram_update_percentiles(hist_id);
    }
    
    if (count) *count = hist->total_count;
    if (sum) *sum = hist->sum;
    if (min) *min = hist->min;
    if (max) *max = hist->max;
    if (mean) *mean = hist->mean;
    if (p50) *p50 = hist->p50;
    if (p95) *p95 = hist->p95;
    if (p99) *p99 = hist->p99;
}

/* تصفير histogram */
static inline void cwhistogram_reset(int hist_id) {
    if (hist_id < 0 || hist_id >= g_cwhistogram_state.histogram_count) {
        return;
    }
    
    cwhistogram_t *hist = &g_cwhistogram_state.histograms[hist_id];
    
    memset(hist->bucket_counts, 0, sizeof(hist->bucket_counts));
    hist->total_count = 0;
    hist->sum = 0.0;
    hist->min = 1e9;
    hist->max = -1e9;
    hist->mean = 0.0;
    hist->percentiles_valid = 0;
}

/* طباعة histogram */
static inline void cwhistogram_print(int hist_id) {
    if (hist_id < 0 || hist_id >= g_cwhistogram_state.histogram_count) {
        return;
    }
    
    cwhistogram_t *hist = &g_cwhistogram_state.histograms[hist_id];
    
    printf("Histogram: %s\n", hist->name);
    printf("Count: %llu, Sum: %.2f, Mean: %.2f\n",
           (unsigned long long)hist->total_count, hist->sum, hist->mean);
    printf("Min: %.2f, Max: %.2f\n", hist->min, hist->max);
    
    if (hist->percentiles_valid) {
        printf("p50: %.2f, p95: %.2f, p99: %.2f\n",
               hist->p50, hist->p95, hist->p99);
    }
    
    printf("Buckets:\n");
    int i; for (i = 0; i < hist->bucket_count; i++) {
        if (hist->bucket_counts[i] > 0) {
            printf("  [%.2f - %.2f): %llu\n",
                   hist->bucket_bounds[i], hist->bucket_bounds[i + 1],
                   (unsigned long long)hist->bucket_counts[i]);
        }
    }
}

#endif /* CWHISTOGRAM_H */
