#ifndef BLACKDETECT_H
#define BLACKDETECT_H

/*
 * Blackscreen Detection — كشف الشاشة السوداء
 * 
 * الفكرة: مراقبة I-frames
 * إذا I-frame مشفر بشكل صحيح لكن لا يفك → شاشة سوداء
 * trigger استرداد
 * 
 * الميزة: كشف مبكر
 * التحدي: false positives محتملة
 */

#include <string.h>
#include <stdint.h>
#include <time.h>

#define BLACKDETECT_MAX_EVENTS 100
#define BLACKDETECT_THRESHOLD 3  /* 3 detections في دقيقة → alert */

/* Blackscreen event */
typedef struct {
    uint16_t caid;
    uint16_t provid;
    uint16_t sid;
    time_t timestamp;
    int duration_ms;
    int detected_by;  /* 1=MPEG check, 2=timeout, 3=pattern */
} blackdetect_event_t;

/* Detection state */
typedef struct {
    blackdetect_event_t events[BLACKDETECT_MAX_EVENTS];
    int event_index;
    int total_detections;
    int total_resolved;
    int total_unresolved;
    int alerts_triggered;
} blackdetect_state_t;

static blackdetect_state_t g_blackdetect_state = {0};

/* تهيئة */
static inline void blackdetect_init(void) {
    memset(&g_blackdetect_state, 0, sizeof(g_blackdetect_state));
}

/* فحص MPEG-2 header */
static inline int blackdetect_check_mpeg2(const unsigned char *iframe, int len) {
    if (len < 6) return 0;  /* غير كافٍ */
    
    /* MPEG-2 start code: 00 00 01 */
    if (iframe[0] != 0x00 || iframe[1] != 0x00 || iframe[2] != 0x01) {
        return 0;  /* ليس MPEG-2 */
    }
    
    /* Picture start code: 00 00 01 00 */
    if (iframe[3] != 0x00) {
        return 0;  /* ليس picture */
    }
    
    /* Picture coding type: I-frame = 001 */
    int pic_type = (iframe[5] >> 3) & 0x07;
    if (pic_type != 1) {
        return 0;  /* ليس I-frame */
    }
    
    /* فحص إضافي: quantizer scale */
    if (len >= 8) {
        int qscale = iframe[7] & 0x1F;
        if (qscale == 0 || qscale > 31) {
            return 0;  /* quantizer غير صحيح */
        }
    }
    
    return 1;  /* MPEG-2 صحيح */
}

/* فحص timeout */
static inline int blackdetect_check_timeout(time_t last_good_cw, int timeout_sec) {
    time_t now = time(NULL);
    double elapsed = difftime(now, last_good_cw);
    
    return (elapsed > timeout_sec);
}

/* تسجيل blackscreen event */
static inline void blackdetect_log_event(uint16_t caid,
                                          uint16_t provid,
                                          uint16_t sid,
                                          int duration_ms,
                                          int detected_by) {
    g_blackdetect_state.total_detections++;
    
    blackdetect_event_t *event = &g_blackdetect_state.events[g_blackdetect_state.event_index];
    event->caid = caid;
    event->provid = provid;
    event->sid = sid;
    event->timestamp = time(NULL);
    event->duration_ms = duration_ms;
    event->detected_by = detected_by;
    
    g_blackdetect_state.event_index = 
        (g_blackdetect_state.event_index + 1) % BLACKDETECT_MAX_EVENTS;
    
    /* فحص إذا كان معدل detections عالي */
    int recent_count = 0;
    time_t now = time(NULL);
    int i; for (i = 0; i < BLACKDETECT_MAX_EVENTS; i++) {
        if (g_blackdetect_state.events[i].timestamp > 0) {
            double age = difftime(now, g_blackdetect_state.events[i].timestamp);
            if (age < 60.0) {  /* آخر دقيقة */
                recent_count++;
            }
        }
    }
    
    if (recent_count >= BLACKDETECT_THRESHOLD) {
        g_blackdetect_state.alerts_triggered++;
    }
}

/* كشف blackscreen */
static inline int blackdetect_detect(const unsigned char *iframe,
                                      int iframe_len,
                                      time_t last_good_cw,
                                      uint16_t caid,
                                      uint16_t provid,
                                      uint16_t sid) {
    /* Method 1: MPEG-2 check */
    if (iframe && iframe_len > 0) {
        if (!blackdetect_check_mpeg2(iframe, iframe_len)) {
            blackdetect_log_event(caid, provid, sid, 0, 1);
            return 1;  /* blackscreen detected */
        }
    }
    
    /* Method 2: Timeout check */
    if (blackdetect_check_timeout(last_good_cw, 5)) {  /* 5 seconds */
        blackdetect_log_event(caid, provid, sid, 0, 2);
        return 1;  /* blackscreen detected */
    }
    
    return 0;  /* no blackscreen */
}

/* تسجيل resolution */
static inline void blackdetect_log_resolved(void) {
    g_blackdetect_state.total_resolved++;
}

/* تسجيل unresolved */
static inline void blackdetect_log_unresolved(void) {
    g_blackdetect_state.total_unresolved++;
}

/* الحصول على الإحصائيات */
static inline void blackdetect_get_stats(int *total, int *resolved, int *unresolved, int *alerts) {
    if (total) *total = g_blackdetect_state.total_detections;
    if (resolved) *resolved = g_blackdetect_state.total_resolved;
    if (unresolved) *unresolved = g_blackdetect_state.total_unresolved;
    if (alerts) *alerts = g_blackdetect_state.alerts_triggered;
}

/* تصفير الإحصائيات */
static inline void blackdetect_reset_stats(void) {
    g_blackdetect_state.total_detections = 0;
    g_blackdetect_state.total_resolved = 0;
    g_blackdetect_state.total_unresolved = 0;
    g_blackdetect_state.alerts_triggered = 0;
    memset(g_blackdetect_state.events, 0, sizeof(g_blackdetect_state.events));
    g_blackdetect_state.event_index = 0;
}

/* الحصول على عدد events في آخر دقيقة */
static inline int blackdetect_get_recent_count(void) {
    int count = 0;
    time_t now = time(NULL);
    
    int i; for (i = 0; i < BLACKDETECT_MAX_EVENTS; i++) {
        if (g_blackdetect_state.events[i].timestamp > 0) {
            double age = difftime(now, g_blackdetect_state.events[i].timestamp);
            if (age < 60.0) {
                count++;
            }
        }
    }
    
    return count;
}

#endif /* BLACKDETECT_H */
