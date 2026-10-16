#ifndef CWLEARN_H
#define CWLEARN_H

/*
 * CW Pattern Learning — تعلم الأنماط
 * 
 * الفكرة: تعلم نمط CWs الصحيحة لكل قناة
 * إذا CW مختلف → مشبوه
 * 
 * الميزة: يتحسن مع الوقت
 * التحدي: يحتاج training period
 */

#include <string.h>
#include <stdint.h>
#include <time.h>

#define CWLEARN_MAX_PATTERNS 256
#define CWLEARN_MIN_SAMPLES 10  /* يحتاج 10 CWs على الأقل للتعلم */
#define CWLEARN_CONFIDENCE_THRESHOLD 0.7

/* Byte pattern for a channel */
typedef struct {
    uint16_t caid;
    uint16_t provid;
    uint16_t sid;
    
    /* Byte frequency (كم مرة ظهر كل byte في كل position) */
    int byte_freq[16][256];  /* [position][byte_value] */
    int total_samples;
    
    /* Most common bytes */
    unsigned char pattern[16];  /* النمط الأكثر شيوعاً */
    double confidence[16];      /* ثقة كل position */
    
    time_t last_update;
    time_t first_seen;
} cwlearn_pattern_t;

/* Learning state */
typedef struct {
    cwlearn_pattern_t patterns[CWLEARN_MAX_PATTERNS];
    int pattern_count;
    
    int total_checks;
    int total_matches;
    int total_mismatches;
} cwlearn_state_t;

static cwlearn_state_t g_cwlearn_state = {0};

/* تهيئة */
static inline void cwlearn_init(void) {
    memset(&g_cwlearn_state, 0, sizeof(g_cwlearn_state));
}

/* البحث عن pattern */
static inline cwlearn_pattern_t *cwlearn_find_pattern(uint16_t caid, uint16_t provid, uint16_t sid) {
    int i; for (i = 0; i < g_cwlearn_state.pattern_count; i++) {
        if (g_cwlearn_state.patterns[i].caid == caid &&
            g_cwlearn_state.patterns[i].provid == provid &&
            g_cwlearn_state.patterns[i].sid == sid) {
            return &g_cwlearn_state.patterns[i];
        }
    }
    return NULL;
}

/* إضافة pattern جديد */
static inline cwlearn_pattern_t *cwlearn_add_pattern(uint16_t caid, uint16_t provid, uint16_t sid) {
    if (g_cwlearn_state.pattern_count >= CWLEARN_MAX_PATTERNS) {
        /* إزالة أقدم pattern */
        time_t oldest = time(NULL);
        int oldest_idx = 0;
        int i; for (i = 0; i < g_cwlearn_state.pattern_count; i++) {
            if (g_cwlearn_state.patterns[i].last_update < oldest) {
                oldest = g_cwlearn_state.patterns[i].last_update;
                oldest_idx = i;
            }
        }
        memset(&g_cwlearn_state.patterns[oldest_idx], 0, sizeof(cwlearn_pattern_t));
        g_cwlearn_state.patterns[oldest_idx].caid = caid;
        g_cwlearn_state.patterns[oldest_idx].provid = provid;
        g_cwlearn_state.patterns[oldest_idx].sid = sid;
        g_cwlearn_state.patterns[oldest_idx].first_seen = time(NULL);
        return &g_cwlearn_state.patterns[oldest_idx];
    }
    
    cwlearn_pattern_t *pattern = &g_cwlearn_state.patterns[g_cwlearn_state.pattern_count++];
    pattern->caid = caid;
    pattern->provid = provid;
    pattern->sid = sid;
    pattern->first_seen = time(NULL);
    return pattern;
}

/* تعلم من CW جديد */
static inline void cwlearn_learn(const unsigned char *cw, uint16_t caid, uint16_t provid, uint16_t sid) {
    cwlearn_pattern_t *pattern = cwlearn_find_pattern(caid, provid, sid);
    if (!pattern) {
        pattern = cwlearn_add_pattern(caid, provid, sid);
    }
    
    /* تحديث byte frequency */
    int i; for (i = 0; i < 16; i++) {
        pattern->byte_freq[i][cw[i]]++;
    }
    
    pattern->total_samples++;
    pattern->last_update = time(NULL);
    
    /* تحديث النمط الأكثر شيوعاً */
    if (pattern->total_samples >= CWLEARN_MIN_SAMPLES) {
        for (i = 0; i < 16; i++) {
            int max_freq = 0;
            int max_byte = 0;
            
            int b; for (b = 0; b < 256; b++) {
                if (pattern->byte_freq[i][b] > max_freq) {
                    max_freq = pattern->byte_freq[i][b];
                    max_byte = b;
                }
            }
            
            pattern->pattern[i] = (unsigned char)max_byte;
            pattern->confidence[i] = (double)max_freq / pattern->total_samples;
        }
    }
}

/* فحص إذا كان CW يطابق النمط */
static inline int cwlearn_matches(const unsigned char *cw, uint16_t caid, uint16_t provid, uint16_t sid) {
    g_cwlearn_state.total_checks++;
    
    cwlearn_pattern_t *pattern = cwlearn_find_pattern(caid, provid, sid);
    if (!pattern || pattern->total_samples < CWLEARN_MIN_SAMPLES) {
        return 1;  /* لا يوجد pattern بعد → موثوق مؤقتاً */
    }
    
    /* حساب match score */
    int matches = 0;
    double total_confidence = 0.0;
    
    int i; for (i = 0; i < 16; i++) {
        if (cw[i] == pattern->pattern[i]) {
            matches++;
            total_confidence += pattern->confidence[i];
        } else {
            /* فحص إذا كان byte قريب */
            int diff = (cw[i] > pattern->pattern[i]) ? 
                       (cw[i] - pattern->pattern[i]) : 
                       (pattern->pattern[i] - cw[i]);
            if (diff <= 2) {
                matches++;
                total_confidence += pattern->confidence[i] * 0.5;
            }
        }
    }
    
    double avg_confidence = total_confidence / 16.0;
    
    if (matches >= 12 && avg_confidence >= CWLEARN_CONFIDENCE_THRESHOLD) {
        g_cwlearn_state.total_matches++;
        return 1;  /* يطابق */
    }
    
    g_cwlearn_state.total_mismatches++;
    return 0;  /* لا يطابق */
}

/* الحصول على الإحصائيات */
static inline void cwlearn_get_stats(int *total, int *matches, int *mismatches, int *patterns) {
    if (total) *total = g_cwlearn_state.total_checks;
    if (matches) *matches = g_cwlearn_state.total_matches;
    if (mismatches) *mismatches = g_cwlearn_state.total_mismatches;
    if (patterns) *patterns = g_cwlearn_state.pattern_count;
}

/* تصفير الإحصائيات */
static inline void cwlearn_reset_stats(void) {
    g_cwlearn_state.total_checks = 0;
    g_cwlearn_state.total_matches = 0;
    g_cwlearn_state.total_mismatches = 0;
}

/* الحصول على confidence للقناة */
static inline double cwlearn_get_confidence(uint16_t caid, uint16_t provid, uint16_t sid) {
    cwlearn_pattern_t *pattern = cwlearn_find_pattern(caid, provid, sid);
    if (!pattern || pattern->total_samples < CWLEARN_MIN_SAMPLES) {
        return 0.0;  /* لا يوجد pattern بعد */
    }
    
    double total_conf = 0.0;
    int i; for (i = 0; i < 16; i++) {
        total_conf += pattern->confidence[i];
    }
    
    return total_conf / 16.0;
}

/* مسح pattern */
static inline void cwlearn_clear_pattern(uint16_t caid, uint16_t provid, uint16_t sid) {
    cwlearn_pattern_t *pattern = cwlearn_find_pattern(caid, provid, sid);
    if (pattern) {
        memset(pattern, 0, sizeof(cwlearn_pattern_t));
    }
}

#endif /* CWLEARN_H */
