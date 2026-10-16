#ifndef CWADAPTIVE_H
#define CWADAPTIVE_H

/*
 * CW Adaptive Thresholds — Thresholds متكيفة
 * 
 * الفكرة: Thresholds تتكيف مع البيئة
 * إذا false positives كثيرة → relax thresholds
 * إذا false negatives كثيرة → tighten thresholds
 * 
 * الميزة: تكيف تلقائي مع الظروف
 * التحدي: يحتاج feedback loop
 */

#include <string.h>
#include <stdint.h>

#define CWADAPTIVE_ADJUST_INTERVAL 100  /* كل 100 check */
#define CWADAPTIVE_MAX_FALSE_RATE 0.1   /* 10% max false rate */
#define CWADAPTIVE_ADJUST_STEP 0.05     /* 5% adjustment step */

/* Adaptive thresholds */
typedef struct {
    double entropy_min;
    double entropy_max;
    int timing_threshold_ms;
    double score_threshold;
    int pattern_match_threshold;
} cwadaptive_thresholds_t;

/* Adaptive state */
typedef struct {
    cwadaptive_thresholds_t current;
    cwadaptive_thresholds_t min_limits;
    cwadaptive_thresholds_t max_limits;
    
    int total_checks;
    int false_positives;
    int false_negatives;
    int true_positives;
    int true_negatives;
    
    int adjustment_count;
} cwadaptive_state_t;

static cwadaptive_state_t g_cwadaptive_state = {0};

/* Forward declarations */
static inline void cwadaptive_adjust(void);

/* تهيئة */
static inline void cwadaptive_init(void) {
    memset(&g_cwadaptive_state, 0, sizeof(g_cwadaptive_state));
    
    /* Default thresholds */
    g_cwadaptive_state.current.entropy_min = 2.5;
    g_cwadaptive_state.current.entropy_max = 4.5;
    g_cwadaptive_state.current.timing_threshold_ms = 100;
    g_cwadaptive_state.current.score_threshold = 0.6;
    g_cwadaptive_state.current.pattern_match_threshold = 12;
    
    /* Min limits */
    g_cwadaptive_state.min_limits.entropy_min = 1.5;
    g_cwadaptive_state.min_limits.entropy_max = 3.5;
    g_cwadaptive_state.min_limits.timing_threshold_ms = 50;
    g_cwadaptive_state.min_limits.score_threshold = 0.3;
    g_cwadaptive_state.min_limits.pattern_match_threshold = 8;
    
    /* Max limits */
    g_cwadaptive_state.max_limits.entropy_min = 3.5;
    g_cwadaptive_state.max_limits.entropy_max = 5.5;
    g_cwadaptive_state.max_limits.timing_threshold_ms = 200;
    g_cwadaptive_state.max_limits.score_threshold = 0.9;
    g_cwadaptive_state.max_limits.pattern_match_threshold = 16;
}

/* تسجيل نتيجة */
static inline void cwadaptive_log_result(int predicted_fake, int actual_fake) {
    g_cwadaptive_state.total_checks++;
    
    if (predicted_fake && actual_fake) {
        g_cwadaptive_state.true_positives++;  /* صحيح: تنبأ فايك وكان فايك */
    } else if (!predicted_fake && !actual_fake) {
        g_cwadaptive_state.true_negatives++;  /* صحيح: تنبأ صحيح وكان صحيح */
    } else if (predicted_fake && !actual_fake) {
        g_cwadaptive_state.false_positives++;  /* خطأ: تنبأ فايك وكان صحيح */
    } else {
        g_cwadaptive_state.false_negatives++;  /* خطأ: تنبأ صحيح وكان فايك */
    }
    
    /* فحص إذا حان وقت التكيف */
    if (g_cwadaptive_state.total_checks % CWADAPTIVE_ADJUST_INTERVAL == 0) {
        cwadaptive_adjust();
    }
}

/* تكيف الـ thresholds */
static inline void cwadaptive_adjust(void) {
    int total = g_cwadaptive_state.total_checks;
    if (total == 0) return;
    
    double false_positive_rate = (double)g_cwadaptive_state.false_positives / total;
    double false_negative_rate = (double)g_cwadaptive_state.false_negatives / total;
    
    /* إذا false positives كثيرة → relax (أقل صرامة) */
    if (false_positive_rate > CWADAPTIVE_MAX_FALSE_RATE) {
        /* تقليل صرامة entropy */
        g_cwadaptive_state.current.entropy_min -= CWADAPTIVE_ADJUST_STEP;
        g_cwadaptive_state.current.entropy_max += CWADAPTIVE_ADJUST_STEP;
        
        /* تقليل صرامة score */
        g_cwadaptive_state.current.score_threshold -= CWADAPTIVE_ADJUST_STEP;
        
        /* تقليل صرامة pattern */
        g_cwadaptive_state.current.pattern_match_threshold -= 1;
    }
    
    /* إذا false negatives كثيرة → tighten (أكثر صرامة) */
    if (false_negative_rate > CWADAPTIVE_MAX_FALSE_RATE) {
        /* زيادة صرامة entropy */
        g_cwadaptive_state.current.entropy_min += CWADAPTIVE_ADJUST_STEP;
        g_cwadaptive_state.current.entropy_max -= CWADAPTIVE_ADJUST_STEP;
        
        /* زيادة صرامة score */
        g_cwadaptive_state.current.score_threshold += CWADAPTIVE_ADJUST_STEP;
        
        /* زيادة صرامة pattern */
        g_cwadaptive_state.current.pattern_match_threshold += 1;
    }
    
    /* تطبيق الحدود */
    if (g_cwadaptive_state.current.entropy_min < g_cwadaptive_state.min_limits.entropy_min)
        g_cwadaptive_state.current.entropy_min = g_cwadaptive_state.min_limits.entropy_min;
    if (g_cwadaptive_state.current.entropy_min > g_cwadaptive_state.max_limits.entropy_min)
        g_cwadaptive_state.current.entropy_min = g_cwadaptive_state.max_limits.entropy_min;
    
    if (g_cwadaptive_state.current.entropy_max < g_cwadaptive_state.min_limits.entropy_max)
        g_cwadaptive_state.current.entropy_max = g_cwadaptive_state.min_limits.entropy_max;
    if (g_cwadaptive_state.current.entropy_max > g_cwadaptive_state.max_limits.entropy_max)
        g_cwadaptive_state.current.entropy_max = g_cwadaptive_state.max_limits.entropy_max;
    
    if (g_cwadaptive_state.current.score_threshold < g_cwadaptive_state.min_limits.score_threshold)
        g_cwadaptive_state.current.score_threshold = g_cwadaptive_state.min_limits.score_threshold;
    if (g_cwadaptive_state.current.score_threshold > g_cwadaptive_state.max_limits.score_threshold)
        g_cwadaptive_state.current.score_threshold = g_cwadaptive_state.max_limits.score_threshold;
    
    if (g_cwadaptive_state.current.pattern_match_threshold < g_cwadaptive_state.min_limits.pattern_match_threshold)
        g_cwadaptive_state.current.pattern_match_threshold = g_cwadaptive_state.min_limits.pattern_match_threshold;
    if (g_cwadaptive_state.current.pattern_match_threshold > g_cwadaptive_state.max_limits.pattern_match_threshold)
        g_cwadaptive_state.current.pattern_match_threshold = g_cwadaptive_state.max_limits.pattern_match_threshold;
    
    g_cwadaptive_state.adjustment_count++;
    
    /* تصفير العدادات */
    g_cwadaptive_state.false_positives = 0;
    g_cwadaptive_state.false_negatives = 0;
    g_cwadaptive_state.true_positives = 0;
    g_cwadaptive_state.true_negatives = 0;
}

/* الحصول على thresholds الحالية */
static inline void cwadaptive_get_thresholds(cwadaptive_thresholds_t *thresholds) {
    *thresholds = g_cwadaptive_state.current;
}

/* الحصول على الإحصائيات */
static inline void cwadaptive_get_stats(int *total, int *fp, int *fn, int *tp, int *tn, int *adjustments) {
    if (total) *total = g_cwadaptive_state.total_checks;
    if (fp) *fp = g_cwadaptive_state.false_positives;
    if (fn) *fn = g_cwadaptive_state.false_negatives;
    if (tp) *tp = g_cwadaptive_state.true_positives;
    if (tn) *tn = g_cwadaptive_state.true_negatives;
    if (adjustments) *adjustments = g_cwadaptive_state.adjustment_count;
}

/* تصفير الإحصائيات */
static inline void cwadaptive_reset_stats(void) {
    g_cwadaptive_state.total_checks = 0;
    g_cwadaptive_state.false_positives = 0;
    g_cwadaptive_state.false_negatives = 0;
    g_cwadaptive_state.true_positives = 0;
    g_cwadaptive_state.true_negatives = 0;
    g_cwadaptive_state.adjustment_count = 0;
}

/* حساب دقة التنبؤ */
static inline double cwadaptive_get_accuracy(void) {
    int total = g_cwadaptive_state.total_checks;
    if (total == 0) return 0.0;
    
    int correct = g_cwadaptive_state.true_positives + g_cwadaptive_state.true_negatives;
    return (double)correct / total;
}

#endif /* CWADAPTIVE_H */
