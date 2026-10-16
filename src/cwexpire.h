#ifndef CWEXPIRE_H
#define CWEXPIRE_H

/*
 * CW Expire — انتهاء صلاحية الأنماط
 * 
 * التوصية قصيرة المدى #7: إضافة pattern expiration
 * 
 * المشكلة: cwlearn يحتفظ بالأنماط للأبد
 * الحل: انتهاء صلاحية تلقائي للأنماط القديمة
 * 
 * الميزة: تكيف مع التغييرات، تقليل الذاكرة
 */

#include <stdint.h>
#include <string.h>
#include <time.h>

#define CWEXPIRE_DEFAULT_MAX_AGE 86400  /* 24 hours */
#define CWEXPIRE_DEFAULT_MIN_SAMPLES 5
#define CWEXPIRE_CHECK_INTERVAL 3600  /* 1 hour */

/* Expiration policy */
typedef struct {
    int max_age_seconds;
    int min_samples;
    int check_interval;
    time_t last_check;
    
    int total_expired;
    int total_kept;
    int total_checks;
} cwexpire_policy_t;

/* Global state */
static cwexpire_policy_t g_cwexpire_policy = {0};

/* تهيئة */
static inline void cwexpire_init(int max_age_seconds, int min_samples) {
    memset(&g_cwexpire_policy, 0, sizeof(g_cwexpire_policy));
    g_cwexpire_policy.max_age_seconds = (max_age_seconds > 0) ? max_age_seconds : CWEXPIRE_DEFAULT_MAX_AGE;
    g_cwexpire_policy.min_samples = (min_samples > 0) ? min_samples : CWEXPIRE_DEFAULT_MIN_SAMPLES;
    g_cwexpire_policy.check_interval = CWEXPIRE_CHECK_INTERVAL;
    g_cwexpire_policy.last_check = time(NULL);
}

/* فحص إذا كان يجب انتهاء صلاحية pattern */
static inline int cwexpire_should_expire(time_t last_update, int sample_count) {
    time_t now = time(NULL);
    double age = difftime(now, last_update);
    
    /* Expire if too old and not enough samples */
    if (age > g_cwexpire_policy.max_age_seconds) {
        if (sample_count < g_cwexpire_policy.min_samples) {
            return 1;  /* Expire */
        }
    }
    
    return 0;  /* Keep */
}

/* حساب age factor (0.0 to 1.0) */
static inline double cwexpire_age_factor(time_t last_update) {
    time_t now = time(NULL);
    double age = difftime(now, last_update);
    
    if (age >= g_cwexpire_policy.max_age_seconds) {
        return 1.0;  /* Fully expired */
    }
    
    return age / g_cwexpire_policy.max_age_seconds;
}

/* حساب confidence modifier بناءً على العمر */
static inline double cwexpire_confidence_modifier(time_t last_update, int sample_count) {
    double age_factor = cwexpire_age_factor(last_update);
    
    /* Reduce confidence for old patterns */
    double modifier = 1.0 - (age_factor * 0.5);  /* Max 50% reduction */
    
    /* Boost for high sample count */
    if (sample_count > 50) {
        modifier += 0.1;
    } else if (sample_count > 20) {
        modifier += 0.05;
    }
    
    /* Clamp to [0.0, 1.0] */
    if (modifier < 0.0) modifier = 0.0;
    if (modifier > 1.0) modifier = 1.0;
    
    return modifier;
}

/* فحص دوري للأنماط */
static inline int cwexpire_check_due(void) {
    time_t now = time(NULL);
    double elapsed = difftime(now, g_cwexpire_policy.last_check);
    
    return (elapsed >= g_cwexpire_policy.check_interval);
}

/* تسجيل فحص */
static inline void cwexpire_record_check(void) {
    g_cwexpire_policy.last_check = time(NULL);
    g_cwexpire_policy.total_checks++;
}

/* تسجيل انتهاء صلاحية */
static inline void cwexpire_record_expired(void) {
    g_cwexpire_policy.total_expired++;
}

/* تسجيل الاحتفاظ */
static inline void cwexpire_record_kept(void) {
    g_cwexpire_policy.total_kept++;
}

/* الحصول على الإحصائيات */
static inline void cwexpire_get_stats(int *expired, int *kept, int *checks) {
    if (expired) *expired = g_cwexpire_policy.total_expired;
    if (kept) *kept = g_cwexpire_policy.total_kept;
    if (checks) *checks = g_cwexpire_policy.total_checks;
}

/* تصفير الإحصائيات */
static inline void cwexpire_reset_stats(void) {
    g_cwexpire_policy.total_expired = 0;
    g_cwexpire_policy.total_kept = 0;
    g_cwexpire_policy.total_checks = 0;
}

/* تعيين max age */
static inline void cwexpire_set_max_age(int seconds) {
    if (seconds > 0) {
        g_cwexpire_policy.max_age_seconds = seconds;
    }
}

/* تعيين min samples */
static inline void cwexpire_set_min_samples(int samples) {
    if (samples > 0) {
        g_cwexpire_policy.min_samples = samples;
    }
}

#endif /* CWEXPIRE_H */
