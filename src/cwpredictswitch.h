#ifndef CWPREDICTSWITCH_H
#define CWPREDICTSWITCH_H

/*
 * CW Predictive Switching — تبديل تنبؤي
 * 
 * الفكرة: تحقق قبل التطبيق
 * إذا CW مشبوه → لا تطبقه
 * ابقى على CW القديم
 * 
 * الميزة: منع الشاشة السوداء قبل حدوثها
 * التحدي: قد يرفض CWs صحيحة (false negatives)
 */

#include <string.h>
#include <stdint.h>
#include <math.h>

#define CWPREDICTSWITCH_ENTROPY_MIN 2.5
#define CWPREDICTSWITCH_ENTROPY_MAX 4.5
#define CWPREDICTSWITCH_CONFIDENCE_THRESHOLD 0.6

/* Predictive switch state */
typedef struct {
    int total_checks;
    int accepted;
    int rejected;
    int false_rejects;  /* CWs صحيحة مرفوضة */
    int true_rejects;   /* CWs فايكة مرفوضة */
} cwpredictswitch_state_t;

static cwpredictswitch_state_t g_cwpredictswitch_state = {0};

/* تهيئة */
static inline void cwpredictswitch_init(void) {
    memset(&g_cwpredictswitch_state, 0, sizeof(g_cwpredictswitch_state));
}

/* حساب entropy */
static inline double cwpredictswitch_entropy(const unsigned char *cw) {
    int freq[256] = {0};
    
    int i; for (i = 0; i < 16; i++) {
        freq[cw[i]]++;
    }
    
    double entropy = 0.0;
    for (i = 0; i < 256; i++) {
        if (freq[i] > 0) {
            double p = (double)freq[i] / 16.0;
            entropy -= p * log2(p);
        }
    }
    
    return entropy;
}

/* فحص byte distribution */
static inline int cwpredictswitch_check_distribution(const unsigned char *cw) {
    int zero_count = 0;
    int ff_count = 0;
    
    int i; for (i = 0; i < 16; i++) {
        if (cw[i] == 0x00) zero_count++;
        if (cw[i] == 0xFF) ff_count++;
    }
    
    /* إذا أكثر من 50% نفس الـ byte → مشبوه */
    if (zero_count > 8 || ff_count > 8) {
        return 0;  /* مشبوه */
    }
    
    return 1;  /* طبيعي */
}

/* فحص sequential pattern */
static inline int cwpredictswitch_check_sequential(const unsigned char *cw) {
    int ascending = 1;
    int descending = 1;
    
    int i; for (i = 1; i < 16; i++) {
        if (cw[i] != (cw[i-1] + 1)) ascending = 0;
        if (cw[i] != (cw[i-1] - 1)) descending = 0;
    }
    
    /* إذا sequential → مشبوه */
    if (ascending || descending) {
        return 0;  /* مشبوه */
    }
    
    return 1;  /* طبيعي */
}

/* فحص repeats */
static inline int cwpredictswitch_check_repeats(const unsigned char *cw) {
    /* فحص إذا كان CW كله نفس الـ byte */
    int all_same = 1;
    int i; for (i = 1; i < 16; i++) {
        if (cw[i] != cw[0]) {
            all_same = 0;
            break;
        }
    }
    
    if (all_same) {
        return 0;  /* مشبوه */
    }
    
    return 1;  /* طبيعي */
}

/* فحص شامل */
static inline int cwpredictswitch_should_apply(const unsigned char *new_cw) {
    g_cwpredictswitch_state.total_checks++;
    
    double confidence = 1.0;
    
    /* Check 1: Entropy */
    double entropy = cwpredictswitch_entropy(new_cw);
    if (entropy < CWPREDICTSWITCH_ENTROPY_MIN || entropy > CWPREDICTSWITCH_ENTROPY_MAX) {
        confidence *= 0.3;  /* خفض confidence */
    }
    
    /* Check 2: Distribution */
    if (!cwpredictswitch_check_distribution(new_cw)) {
        confidence *= 0.5;
    }
    
    /* Check 3: Sequential */
    if (!cwpredictswitch_check_sequential(new_cw)) {
        confidence *= 0.4;
    }
    
    /* Check 4: Repeats */
    if (!cwpredictswitch_check_repeats(new_cw)) {
        confidence *= 0.2;
    }
    
    /* قرار */
    if (confidence >= CWPREDICTSWITCH_CONFIDENCE_THRESHOLD) {
        g_cwpredictswitch_state.accepted++;
        return 1;  /* آمن */
    } else {
        g_cwpredictswitch_state.rejected++;
        return 0;  /* مشبوه */
    }
}

/* تسجيل false reject */
static inline void cwpredictswitch_log_false_reject(void) {
    g_cwpredictswitch_state.false_rejects++;
}

/* تسجيل true reject */
static inline void cwpredictswitch_log_true_reject(void) {
    g_cwpredictswitch_state.true_rejects++;
}

/* الحصول على الإحصائيات */
static inline void cwpredictswitch_get_stats(int *total, int *accepted, int *rejected, 
                                              int *false_rejects, int *true_rejects) {
    if (total) *total = g_cwpredictswitch_state.total_checks;
    if (accepted) *accepted = g_cwpredictswitch_state.accepted;
    if (rejected) *rejected = g_cwpredictswitch_state.rejected;
    if (false_rejects) *false_rejects = g_cwpredictswitch_state.false_rejects;
    if (true_rejects) *true_rejects = g_cwpredictswitch_state.true_rejects;
}

/* تصفير الإحصائيات */
static inline void cwpredictswitch_reset_stats(void) {
    g_cwpredictswitch_state.total_checks = 0;
    g_cwpredictswitch_state.accepted = 0;
    g_cwpredictswitch_state.rejected = 0;
    g_cwpredictswitch_state.false_rejects = 0;
    g_cwpredictswitch_state.true_rejects = 0;
}

/* الحصول على confidence score */
static inline double cwpredictswitch_get_confidence(const unsigned char *cw) {
    double confidence = 1.0;
    
    double entropy = cwpredictswitch_entropy(cw);
    if (entropy < CWPREDICTSWITCH_ENTROPY_MIN || entropy > CWPREDICTSWITCH_ENTROPY_MAX) {
        confidence *= 0.3;
    }
    
    if (!cwpredictswitch_check_distribution(cw)) {
        confidence *= 0.5;
    }
    
    if (!cwpredictswitch_check_sequential(cw)) {
        confidence *= 0.4;
    }
    
    if (!cwpredictswitch_check_repeats(cw)) {
        confidence *= 0.2;
    }
    
    return confidence;
}

#endif /* CWPREDICTSWITCH_H */
