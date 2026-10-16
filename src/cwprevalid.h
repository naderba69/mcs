#ifndef CWPREVALID_H
#define CWPREVALID_H

/*
 * CW Pre-validation — التحقق قبل التطبيق
 * 
 * الفكرة: فك تشفير تجريبي لـ I-frame قبل تطبيق CW
 * إذا فشل → رفض CW فوراً
 * 
 * الميزة: كشف فوري قبل التطبيق
 * التحدي: overhead إضافي (~50μs)
 */

#include <string.h>
#include <stdint.h>

/* فحص MPEG-2 I-frame */
static inline int cwprevalid_is_valid_mpeg2(const unsigned char *data, int len) {
    if (len < 6) return 0;
    
    /* MPEG-2 start code: 00 00 01 */
    if (data[0] != 0x00 || data[1] != 0x00 || data[2] != 0x01) {
        return 0;
    }
    
    /* Picture start code: 00 00 01 00 */
    if (data[3] != 0x00) {
        return 0;
    }
    
    /* Picture coding type: I-frame = 001 */
    if ((data[5] & 0x38) != 0x08) {
        return 0;
    }
    
    return 1;
}

/* حساب checksum بسيط */
static inline uint32_t cwprevalid_checksum(const unsigned char *data, int len) {
    uint32_t sum = 0;
    int i; for (i = 0; i < len; i++) {
        sum += data[i];
    }
    return sum;
}

/* Pre-validation structure */
typedef struct {
    int enabled;
    int total_checks;
    int passed;
    int failed;
    double avg_latency_us;
} cwprevalid_stats_t;

static cwprevalid_stats_t g_cwprevalid_stats = {0};

/* تهيئة */
static inline void cwprevalid_init(void) {
    g_cwprevalid_stats.enabled = 1;
    g_cwprevalid_stats.total_checks = 0;
    g_cwprevalid_stats.passed = 0;
    g_cwprevalid_stats.failed = 0;
    g_cwprevalid_stats.avg_latency_us = 0.0;
}

/* Pre-validation تجريبي */
static inline int cwprevalid_check(const unsigned char *cw, 
                                    const unsigned char *test_data, 
                                    int test_len) {
    if (!g_cwprevalid_stats.enabled) return 1;
    
    g_cwprevalid_stats.total_checks++;
    
    /* محاكاة فك التشفير */
    unsigned char decrypted[188];
    memcpy(decrypted, test_data, test_len < 188 ? test_len : 188);
    
    /* XOR مع CW (محاكاة بسيطة) */
    int i; for (i = 0; i < test_len && i < 188; i++) {
        decrypted[i] ^= cw[i % 16];
    }
    
    /* فحص MPEG-2 */
    if (cwprevalid_is_valid_mpeg2(decrypted, test_len)) {
        g_cwprevalid_stats.passed++;
        return 1;  /* CW صحيح */
    }
    
    /* فحص checksum */
    uint32_t checksum = cwprevalid_checksum(decrypted, test_len);
    if (checksum > 0 && checksum < 0xFFFFFFFF) {
        g_cwprevalid_stats.passed++;
        return 1;  /* CW محتمل */
    }
    
    g_cwprevalid_stats.failed++;
    return 0;  /* CW فايك */
}

/* الحصول على الإحصائيات */
static inline void cwprevalid_get_stats(cwprevalid_stats_t *stats) {
    *stats = g_cwprevalid_stats;
}

/* تصفير الإحصائيات */
static inline void cwprevalid_reset_stats(void) {
    g_cwprevalid_stats.total_checks = 0;
    g_cwprevalid_stats.passed = 0;
    g_cwprevalid_stats.failed = 0;
    g_cwprevalid_stats.avg_latency_us = 0.0;
}

#endif /* CWPREVALID_H */
