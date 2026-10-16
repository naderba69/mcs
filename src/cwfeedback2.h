#ifndef CWFEEDBACK2_H
#define CWFEEDBACK2_H

/*
 * CW Feedback 2 — آلية التغذية الراجعة
 * 
 * التوصية الفورية #3: إضافة feedback mechanism
 * 
 * المشكلة: false positives/negatives بدون تصحيح
 * الحل: feedback loop للتعلم من الأخطاء
 * 
 * الميزة: تحسين مستمر، تقليل الأخطاء
 */

#include <stdint.h>
#include <string.h>
#include <time.h>

#define CWFEEDBACK2_MAX_ENTRIES 500
#define CWFEEDBACK2_CORRECTION_WINDOW 3600  /* 1 hour */

/* Feedback types */
#define CWFEEDBACK2_FALSE_POSITIVE  0  /* rejected but was good */
#define CWFEEDBACK2_FALSE_NEGATIVE  1  /* accepted but was bad */
#define CWFEEDBACK2_TRUE_POSITIVE   2  /* rejected and was bad */
#define CWFEEDBACK2_TRUE_NEGATIVE   3  /* accepted and was good */

/* Feedback entry */
typedef struct {
    uint16_t caid;
    uint16_t provid;
    uint16_t sid;
    uint32_t peer_id;
    unsigned char cw_hash[16];
    
    int feedback_type;
    time_t timestamp;
    
    int corrected;  /* 1 if this feedback was used to correct */
    double correction_weight;
} cwfeedback2_entry_t;

/* Feedback state */
typedef struct {
    cwfeedback2_entry_t entries[CWFEEDBACK2_MAX_ENTRIES];
    int entry_count;
    int entry_index;
    
    int total_feedbacks;
    int total_false_positives;
    int total_false_negatives;
    int total_true_positives;
    int total_true_negatives;
    int total_corrections;
    
    int enabled;
    int correction_window;
} cwfeedback2_state_t;

static cwfeedback2_state_t g_cwfeedback2_state = {0};

/* تهيئة */
static inline void cwfeedback2_init(void) {
    memset(&g_cwfeedback2_state, 0, sizeof(g_cwfeedback2_state));
    g_cwfeedback2_state.enabled = 1;
    g_cwfeedback2_state.correction_window = CWFEEDBACK2_CORRECTION_WINDOW;
}

/* إضافة feedback */
static inline int cwfeedback2_add(uint16_t caid, uint16_t provid, uint16_t sid,
                                   uint32_t peer_id, const unsigned char *cw,
                                   int feedback_type) {
    if (!g_cwfeedback2_state.enabled) {
        return -1;
    }
    
    cwfeedback2_entry_t *entry = &g_cwfeedback2_state.entries[g_cwfeedback2_state.entry_index];
    
    entry->caid = caid;
    entry->provid = provid;
    entry->sid = sid;
    entry->peer_id = peer_id;
    memcpy(entry->cw_hash, cw, 16);
    entry->feedback_type = feedback_type;
    entry->timestamp = time(NULL);
    entry->corrected = 0;
    entry->correction_weight = 0.0;
    
    g_cwfeedback2_state.entry_index = 
        (g_cwfeedback2_state.entry_index + 1) % CWFEEDBACK2_MAX_ENTRIES;
    if (g_cwfeedback2_state.entry_count < CWFEEDBACK2_MAX_ENTRIES) {
        g_cwfeedback2_state.entry_count++;
    }
    
    g_cwfeedback2_state.total_feedbacks++;
    
    switch (feedback_type) {
        case CWFEEDBACK2_FALSE_POSITIVE:
            g_cwfeedback2_state.total_false_positives++;
            break;
        case CWFEEDBACK2_FALSE_NEGATIVE:
            g_cwfeedback2_state.total_false_negatives++;
            break;
        case CWFEEDBACK2_TRUE_POSITIVE:
            g_cwfeedback2_state.total_true_positives++;
            break;
        case CWFEEDBACK2_TRUE_NEGATIVE:
            g_cwfeedback2_state.total_true_negatives++;
            break;
    }
    
    return 0;
}

/* البحث عن feedback لـ CW معين */
static inline cwfeedback2_entry_t *cwfeedback2_find(uint16_t caid, uint16_t provid,
                                                      uint16_t sid, uint32_t peer_id,
                                                      const unsigned char *cw) {
    time_t now = time(NULL);
    
    int i; for (i = 0; i < g_cwfeedback2_state.entry_count; i++) {
        cwfeedback2_entry_t *entry = &g_cwfeedback2_state.entries[i];
        
        /* Check if within correction window */
        double age = difftime(now, entry->timestamp);
        if (age > g_cwfeedback2_state.correction_window) {
            continue;
        }
        
        /* Check match */
        if (entry->caid == caid && entry->provid == provid &&
            entry->sid == sid && entry->peer_id == peer_id &&
            memcmp(entry->cw_hash, cw, 16) == 0) {
            return entry;
        }
    }
    
    return NULL;
}

/* فحص إذا كان هناك false positive سابق */
static inline int cwfeedback2_has_false_positive(uint16_t caid, uint16_t provid,
                                                   uint16_t sid, uint32_t peer_id,
                                                   const unsigned char *cw) {
    cwfeedback2_entry_t *entry = cwfeedback2_find(caid, provid, sid, peer_id, cw);
    return (entry && entry->feedback_type == CWFEEDBACK2_FALSE_POSITIVE);
}

/* فحص إذا كان هناك false negative سابق */
static inline int cwfeedback2_has_false_negative(uint16_t caid, uint16_t provid,
                                                   uint16_t sid, uint32_t peer_id,
                                                   const unsigned char *cw) {
    cwfeedback2_entry_t *entry = cwfeedback2_find(caid, provid, sid, peer_id, cw);
    return (entry && entry->feedback_type == CWFEEDBACK2_FALSE_NEGATIVE);
}

/* الحصول على correction weight */
static inline double cwfeedback2_get_correction_weight(uint16_t caid, uint16_t provid,
                                                        uint16_t sid, uint32_t peer_id,
                                                        const unsigned char *cw) {
    cwfeedback2_entry_t *entry = cwfeedback2_find(caid, provid, sid, peer_id, cw);
    
    if (!entry) {
        return 0.0;  /* No feedback */
    }
    
    /* Calculate weight based on feedback type and age */
    time_t now = time(NULL);
    double age = difftime(now, entry->timestamp);
    double recency_factor = 1.0 - (age / g_cwfeedback2_state.correction_window);
    if (recency_factor < 0.0) recency_factor = 0.0;
    
    double weight = 0.0;
    
    switch (entry->feedback_type) {
        case CWFEEDBACK2_FALSE_POSITIVE:
            weight = 0.5 * recency_factor;  /* Positive correction */
            break;
        case CWFEEDBACK2_FALSE_NEGATIVE:
            weight = -0.5 * recency_factor;  /* Negative correction */
            break;
        case CWFEEDBACK2_TRUE_POSITIVE:
            weight = -0.3 * recency_factor;  /* Reinforce rejection */
            break;
        case CWFEEDBACK2_TRUE_NEGATIVE:
            weight = 0.3 * recency_factor;  /* Reinforce acceptance */
            break;
    }
    
    return weight;
}

/* تعليم feedback كـ corrected */
static inline void cwfeedback2_mark_corrected(uint16_t caid, uint16_t provid,
                                                uint16_t sid, uint32_t peer_id,
                                                const unsigned char *cw) {
    cwfeedback2_entry_t *entry = cwfeedback2_find(caid, provid, sid, peer_id, cw);
    if (entry && !entry->corrected) {
        entry->corrected = 1;
        g_cwfeedback2_state.total_corrections++;
    }
}

/* حساب accuracy */
static inline double cwfeedback2_accuracy(void) {
    int total = g_cwfeedback2_state.total_true_positives + 
                g_cwfeedback2_state.total_true_negatives +
                g_cwfeedback2_state.total_false_positives +
                g_cwfeedback2_state.total_false_negatives;
    
    if (total == 0) return 0.0;
    
    int correct = g_cwfeedback2_state.total_true_positives + 
                  g_cwfeedback2_state.total_true_negatives;
    
    return (double)correct / total * 100.0;
}

/* حساب precision */
static inline double cwfeedback2_precision(void) {
    int predicted_positive = g_cwfeedback2_state.total_true_positives + 
                             g_cwfeedback2_state.total_false_positives;
    
    if (predicted_positive == 0) return 0.0;
    
    return (double)g_cwfeedback2_state.total_true_positives / predicted_positive * 100.0;
}

/* حساب recall */
static inline double cwfeedback2_recall(void) {
    int actual_positive = g_cwfeedback2_state.total_true_positives + 
                          g_cwfeedback2_state.total_false_negatives;
    
    if (actual_positive == 0) return 0.0;
    
    return (double)g_cwfeedback2_state.total_true_positives / actual_positive * 100.0;
}

/* الحصول على الإحصائيات */
static inline void cwfeedback2_get_stats(int *total, int *fp, int *fn, int *tp, int *tn, int *corrections) {
    if (total) *total = g_cwfeedback2_state.total_feedbacks;
    if (fp) *fp = g_cwfeedback2_state.total_false_positives;
    if (fn) *fn = g_cwfeedback2_state.total_false_negatives;
    if (tp) *tp = g_cwfeedback2_state.total_true_positives;
    if (tn) *tn = g_cwfeedback2_state.total_true_negatives;
    if (corrections) *corrections = g_cwfeedback2_state.total_corrections;
}

/* تصفير الإحصائيات */
static inline void cwfeedback2_reset_stats(void) {
    g_cwfeedback2_state.total_feedbacks = 0;
    g_cwfeedback2_state.total_false_positives = 0;
    g_cwfeedback2_state.total_false_negatives = 0;
    g_cwfeedback2_state.total_true_positives = 0;
    g_cwfeedback2_state.total_true_negatives = 0;
    g_cwfeedback2_state.total_corrections = 0;
}

#endif /* CWFEEDBACK2_H */
