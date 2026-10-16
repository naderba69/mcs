#ifndef CWCROSSVAL_H
#define CWCROSSVAL_H

/*
 * CW Cross-validation — التحقق المتقاطع
 * 
 * الفكرة: طلب CW من مصادر متعددة
 * مقارنة النتائج
 * الأغلبية تفوز
 * 
 * الميزة: دقة عالية (95%+)
 * التحدي: latency إضافي (3 طلبات بدلاً من 1)
 */

#include <string.h>
#include <stdint.h>

#define CWCROSSVAL_MAX_SOURCES 5
#define CWCROSSVAL_MIN_AGREEMENT 2  /* على الأقل 2 مصادر يجب أن تتفق */

/* Source response */
typedef struct {
    uint32_t source_id;
    unsigned char cw[16];
    int responded;
    int response_time_ms;
} cwcrossval_source_t;

/* Cross-validation state */
typedef struct {
    int total_validations;
    int successful;
    int failed;
    int no_agreement;
    double avg_agreement_count;
} cwcrossval_state_t;

static cwcrossval_state_t g_cwcrossval_state = {0};

/* تهيئة */
static inline void cwcrossval_init(void) {
    memset(&g_cwcrossval_state, 0, sizeof(g_cwcrossval_state));
}

/* مقارنة CWs */
static inline int cwcrossval_cw_equal(const unsigned char *cw1, const unsigned char *cw2) {
    return (memcmp(cw1, cw2, 16) == 0);
}

/* التحقق المتقاطع */
static inline int cwcrossval_validate(cwcrossval_source_t *sources, 
                                       int source_count,
                                       unsigned char *cw_out) {
    if (source_count < 2) return 0;  /* يحتاج على الأقل مصدرين */
    
    g_cwcrossval_state.total_validations++;
    
    /* حساب التكرارات */
    int max_count = 0;
    int max_index = -1;
    
    int i; for (i = 0; i < source_count; i++) {
        if (!sources[i].responded) continue;
        
        int count = 0;
        int j; for (j = 0; j < source_count; j++) {
            if (!sources[j].responded) continue;
            
            if (cwcrossval_cw_equal(sources[i].cw, sources[j].cw)) {
                count++;
            }
        }
        
        if (count > max_count) {
            max_count = count;
            max_index = i;
        }
    }
    
    /* فحص إذا كان هناك اتفاق */
    if (max_count >= CWCROSSVAL_MIN_AGREEMENT) {
        memcpy(cw_out, sources[max_index].cw, 16);
        g_cwcrossval_state.successful++;
        g_cwcrossval_state.avg_agreement_count = 
            (g_cwcrossval_state.avg_agreement_count * (g_cwcrossval_state.successful - 1) + max_count)
            / g_cwcrossval_state.successful;
        return max_count;  /* عدد المصادر المتفقة */
    }
    
    /* لا يوجد اتفاق */
    g_cwcrossval_state.no_agreement++;
    return 0;
}

/* تسجيل فشل */
static inline void cwcrossval_log_failure(void) {
    g_cwcrossval_state.failed++;
}

/* الحصول على الإحصائيات */
static inline void cwcrossval_get_stats(int *total, int *successful, int *failed, 
                                         int *no_agreement, double *avg_agreement) {
    if (total) *total = g_cwcrossval_state.total_validations;
    if (successful) *successful = g_cwcrossval_state.successful;
    if (failed) *failed = g_cwcrossval_state.failed;
    if (no_agreement) *no_agreement = g_cwcrossval_state.no_agreement;
    if (avg_agreement) *avg_agreement = g_cwcrossval_state.avg_agreement_count;
}

/* تصفير الإحصائيات */
static inline void cwcrossval_reset_stats(void) {
    g_cwcrossval_state.total_validations = 0;
    g_cwcrossval_state.successful = 0;
    g_cwcrossval_state.failed = 0;
    g_cwcrossval_state.no_agreement = 0;
    g_cwcrossval_state.avg_agreement_count = 0.0;
}

/* حساب average response time */
static inline double cwcrossval_get_avg_response_time(cwcrossval_source_t *sources, int source_count) {
    double total_time = 0.0;
    int count = 0;
    
    int i; for (i = 0; i < source_count; i++) {
        if (sources[i].responded && sources[i].response_time_ms > 0) {
            total_time += sources[i].response_time_ms;
            count++;
        }
    }
    
    return (count > 0) ? (total_time / count) : 0.0;
}

#endif /* CWCROSSVAL_H */
