#ifndef CWROLLBACK_H
#define CWROLLBACK_H

/*
 * CW Instant Rollback — استرداد فوري
 * 
 * الفكرة: احتفظ بآخر CW صحيح
 * عند اكتشاف شاشة سوداء → استرجع CW القديم فوراً
 * تقليل وقت الشاشة السوداء
 * 
 * الميزة: استرداد فوري (< 100ms)
 * التحدي: CW القديم قد يكون منتهي الصلاحية
 */

#include <string.h>
#include <stdint.h>
#include <time.h>

#define CWROLLBACK_MAX_HISTORY 5  /* احتفظ بآخر 5 CWs */
#define CWROLLBACK_MAX_AGE_SEC 30  /* CW أقدم من 30 ثانية → غير صالح */

/* CW history entry */
typedef struct {
    unsigned char cw[16];
    uint16_t caid;
    uint16_t provid;
    uint16_t sid;
    time_t timestamp;
    int valid;
    int rollback_count;  /* كم مرة تم استرجاعه */
} cwrollback_entry_t;

/* Rollback state */
typedef struct {
    cwrollback_entry_t history[CWROLLBACK_MAX_HISTORY];
    int current_index;
    int total_rollbacks;
    int successful_rollbacks;
    int failed_rollbacks;
    int expired_cws;
} cwrollback_state_t;

static cwrollback_state_t g_cwrollback_state = {0};

/* تهيئة */
static inline void cwrollback_init(void) {
    memset(&g_cwrollback_state, 0, sizeof(g_cwrollback_state));
}

/* حفظ CW جديد في history */
static inline void cwrollback_save(const unsigned char *cw,
                                    uint16_t caid,
                                    uint16_t provid,
                                    uint16_t sid) {
    cwrollback_entry_t *entry = &g_cwrollback_state.history[g_cwrollback_state.current_index];
    
    memcpy(entry->cw, cw, 16);
    entry->caid = caid;
    entry->provid = provid;
    entry->sid = sid;
    entry->timestamp = time(NULL);
    entry->valid = 1;
    entry->rollback_count = 0;
    
    /* تقدم في ring buffer */
    g_cwrollback_state.current_index = 
        (g_cwrollback_state.current_index + 1) % CWROLLBACK_MAX_HISTORY;
}

/* البحث عن CW صالح للقناة */
static inline cwrollback_entry_t *cwrollback_find_valid(uint16_t caid,
                                                         uint16_t provid,
                                                         uint16_t sid) {
    time_t now = time(NULL);
    
    /* ابحث في history (من الأحدث إلى الأقدم) */
    int i; for (i = 0; i < CWROLLBACK_MAX_HISTORY; i++) {
        int idx = (g_cwrollback_state.current_index - 1 - i + CWROLLBACK_MAX_HISTORY) 
                  % CWROLLBACK_MAX_HISTORY;
        
        cwrollback_entry_t *entry = &g_cwrollback_state.history[idx];
        
        if (!entry->valid) continue;
        
        /* فحص العمر */
        double age = difftime(now, entry->timestamp);
        if (age > CWROLLBACK_MAX_AGE_SEC) {
            entry->valid = 0;
            g_cwrollback_state.expired_cws++;
            continue;
        }
        
        /* فحص القناة */
        if (entry->caid == caid && entry->sid == sid) {
            return entry;
        }
    }
    
    return NULL;
}

/* استرجاع CW قديم */
static inline int cwrollback_rollback(unsigned char *cw_out,
                                       uint16_t caid,
                                       uint16_t provid,
                                       uint16_t sid) {
    g_cwrollback_state.total_rollbacks++;
    
    cwrollback_entry_t *entry = cwrollback_find_valid(caid, provid, sid);
    if (!entry) {
        g_cwrollback_state.failed_rollbacks++;
        return 0;  /* لا يوجد CW صالح */
    }
    
    /* استرجاع CW */
    memcpy(cw_out, entry->cw, 16);
    entry->rollback_count++;
    g_cwrollback_state.successful_rollbacks++;
    
    return 1;
}

/* الحصول على الإحصائيات */
static inline void cwrollback_get_stats(int *total, int *success, int *failed, int *expired) {
    if (total) *total = g_cwrollback_state.total_rollbacks;
    if (success) *success = g_cwrollback_state.successful_rollbacks;
    if (failed) *failed = g_cwrollback_state.failed_rollbacks;
    if (expired) *expired = g_cwrollback_state.expired_cws;
}

/* تصفير الإحصائيات */
static inline void cwrollback_reset_stats(void) {
    g_cwrollback_state.total_rollbacks = 0;
    g_cwrollback_state.successful_rollbacks = 0;
    g_cwrollback_state.failed_rollbacks = 0;
    g_cwrollback_state.expired_cws = 0;
}

/* الحصول على عدد CWs في history */
static inline int cwrollback_get_history_count(void) {
    int count = 0;
    time_t now = time(NULL);
    
    int i; for (i = 0; i < CWROLLBACK_MAX_HISTORY; i++) {
        if (g_cwrollback_state.history[i].valid) {
            double age = difftime(now, g_cwrollback_state.history[i].timestamp);
            if (age <= CWROLLBACK_MAX_AGE_SEC) {
                count++;
            }
        }
    }
    
    return count;
}

#endif /* CWROLLBACK_H */
