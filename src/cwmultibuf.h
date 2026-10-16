#ifndef CWMULTIBUF_H
#define CWMULTIBUF_H

/*
 * Multi-CW Buffer — buffer متعدد
 * 
 * الفكرة: احتفظ بآخر 3 CWs صحيحة
 * عند فشل CW الحالي → جرب التالي
 * تقليل وقت الشاشة السوداء
 * 
 * الميزة: استرداد سريع بدون طلب جديد
 * التحدي: يحتاج ذاكرة إضافية (48 bytes فقط)
 */

#include <string.h>
#include <stdint.h>
#include <time.h>

#define CWMULTIBUF_SIZE 3
#define CWMULTIBUF_MAX_AGE_SEC 30

/* CW buffer entry */
typedef struct {
    unsigned char cw[16];
    uint16_t caid;
    uint16_t provid;
    uint16_t sid;
    time_t timestamp;
    int valid;
    int try_count;  /* كم مرة تم تجريبه */
    int success_count;  /* كم مرة نجح */
} cwmultibuf_entry_t;

/* Buffer state */
typedef struct {
    cwmultibuf_entry_t buffer[CWMULTIBUF_SIZE];
    int current_index;
    int total_tries;
    int total_successes;
    int total_failures;
    int total_expirations;
} cwmultibuf_state_t;

static cwmultibuf_state_t g_cwmultibuf_state = {0};

/* تهيئة */
static inline void cwmultibuf_init(void) {
    memset(&g_cwmultibuf_state, 0, sizeof(g_cwmultibuf_state));
}

/* إضافة CW إلى buffer */
static inline void cwmultibuf_add(const unsigned char *cw,
                                   uint16_t caid,
                                   uint16_t provid,
                                   uint16_t sid) {
    cwmultibuf_entry_t *entry = &g_cwmultibuf_state.buffer[g_cwmultibuf_state.current_index];
    
    memcpy(entry->cw, cw, 16);
    entry->caid = caid;
    entry->provid = provid;
    entry->sid = sid;
    entry->timestamp = time(NULL);
    entry->valid = 1;
    entry->try_count = 0;
    entry->success_count = 0;
    
    /* تقدم في ring buffer */
    g_cwmultibuf_state.current_index = 
        (g_cwmultibuf_state.current_index + 1) % CWMULTIBUF_SIZE;
}

/* الحصول على CW التالي */
static inline int cwmultibuf_get_next(unsigned char *cw_out,
                                       uint16_t caid,
                                       uint16_t provid,
                                       uint16_t sid) {
    time_t now = time(NULL);
    
    /* ابحث عن CW صالح للقناة */
    int i; for (i = 0; i < CWMULTIBUF_SIZE; i++) {
        int idx = (g_cwmultibuf_state.current_index - 1 - i + CWMULTIBUF_SIZE) 
                  % CWMULTIBUF_SIZE;
        
        cwmultibuf_entry_t *entry = &g_cwmultibuf_state.buffer[idx];
        
        if (!entry->valid) continue;
        
        /* فحص العمر */
        double age = difftime(now, entry->timestamp);
        if (age > CWMULTIBUF_MAX_AGE_SEC) {
            entry->valid = 0;
            g_cwmultibuf_state.total_expirations++;
            continue;
        }
        
        /* فحص القناة */
        if (entry->caid == caid && entry->sid == sid) {
            /* تجنب CW الذي فشل عدة مرات */
            if (entry->try_count > 3 && entry->success_count == 0) {
                continue;  /* CW فاشل */
            }
            
            memcpy(cw_out, entry->cw, 16);
            entry->try_count++;
            g_cwmultibuf_state.total_tries++;
            return 1;
        }
    }
    
    return 0;  /* لا يوجد CW صالح */
}

/* تسجيل نجاح CW */
static inline void cwmultibuf_mark_success(const unsigned char *cw,
                                            uint16_t caid,
                                            uint16_t sid) {
    int i; for (i = 0; i < CWMULTIBUF_SIZE; i++) {
        cwmultibuf_entry_t *entry = &g_cwmultibuf_state.buffer[i];
        if (entry->valid && entry->caid == caid && entry->sid == sid) {
            if (memcmp(entry->cw, cw, 16) == 0) {
                entry->success_count++;
                g_cwmultibuf_state.total_successes++;
                return;
            }
        }
    }
}

/* تسجيل فشل CW */
static inline void cwmultibuf_mark_failure(const unsigned char *cw,
                                            uint16_t caid,
                                            uint16_t sid) {
    int i; for (i = 0; i < CWMULTIBUF_SIZE; i++) {
        cwmultibuf_entry_t *entry = &g_cwmultibuf_state.buffer[i];
        if (entry->valid && entry->caid == caid && entry->sid == sid) {
            if (memcmp(entry->cw, cw, 16) == 0) {
                g_cwmultibuf_state.total_failures++;
                return;
            }
        }
    }
}

/* الحصول على الإحصائيات */
static inline void cwmultibuf_get_stats(int *tries, int *successes, int *failures, int *expirations) {
    if (tries) *tries = g_cwmultibuf_state.total_tries;
    if (successes) *successes = g_cwmultibuf_state.total_successes;
    if (failures) *failures = g_cwmultibuf_state.total_failures;
    if (expirations) *expirations = g_cwmultibuf_state.total_expirations;
}

/* تصفير الإحصائيات */
static inline void cwmultibuf_reset_stats(void) {
    g_cwmultibuf_state.total_tries = 0;
    g_cwmultibuf_state.total_successes = 0;
    g_cwmultibuf_state.total_failures = 0;
    g_cwmultibuf_state.total_expirations = 0;
}

/* الحصول على عدد CWs صالحة */
static inline int cwmultibuf_get_valid_count(void) {
    int count = 0;
    time_t now = time(NULL);
    
    int i; for (i = 0; i < CWMULTIBUF_SIZE; i++) {
        if (g_cwmultibuf_state.buffer[i].valid) {
            double age = difftime(now, g_cwmultibuf_state.buffer[i].timestamp);
            if (age <= CWMULTIBUF_MAX_AGE_SEC) {
                count++;
            }
        }
    }
    
    return count;
}

/* مسح buffer */
static inline void cwmultibuf_clear(void) {
    memset(g_cwmultibuf_state.buffer, 0, sizeof(g_cwmultibuf_state.buffer));
    g_cwmultibuf_state.current_index = 0;
}

#endif /* CWMULTIBUF_H */
