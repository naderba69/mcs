#ifndef BLACKANALYTICS_H
#define BLACKANALYTICS_H

/*
 * Blackscreen Analytics — تحليل الشاشة السوداء
 * 
 * الفكرة: تسجيل وتحليل كل شاشة سوداء
 * البحث عن أنماط (قناة معينة، peer معين، وقت معين)
 * 
 * الميزة: فهم أعمق للمشكلة
 * التحدي: يحتاج storage و processing
 */

#include <string.h>
#include <stdint.h>
#include <time.h>

#define BLACKANALYTICS_MAX_EVENTS 200
#define BLACKANALYTICS_ALERT_THRESHOLD 10  /* أكثر من 10 في الدقيقة → alert */

/* Blackscreen event details */
typedef struct {
    uint16_t caid;
    uint16_t provid;
    uint16_t sid;
    time_t timestamp;
    int duration_ms;
    uint32_t peer_id;
    int resolved;
    int resolution_method;  /* 1=rollback, 2=buffer, 3=new_cw, 4=emergency */
} blackanalytics_event_t;

/* Channel statistics */
typedef struct {
    uint16_t caid;
    uint16_t sid;
    int total_blackscreens;
    int total_duration_ms;
    time_t last_blackscreen;
} blackanalytics_channel_t;

/* Peer statistics */
typedef struct {
    uint32_t peer_id;
    int total_blackscreens;
    int total_cws_sent;
    double blackscreen_rate;
} blackanalytics_peer_t;

/* Analytics state */
typedef struct {
    blackanalytics_event_t events[BLACKANALYTICS_MAX_EVENTS];
    int event_index;
    
    blackanalytics_channel_t top_channels[10];
    int channel_count;
    
    blackanalytics_peer_t top_peers[10];
    int peer_count;
    
    int total_events;
    int total_resolved;
    int total_unresolved;
    int alerts_triggered;
    double avg_duration_ms;
} blackanalytics_state_t;

static blackanalytics_state_t g_blackanalytics_state = {0};

/* تهيئة */
static inline void blackanalytics_init(void) {
    memset(&g_blackanalytics_state, 0, sizeof(g_blackanalytics_state));
}

/* تسجيل event */
static inline void blackanalytics_log_event(uint16_t caid,
                                             uint16_t provid,
                                             uint16_t sid,
                                             int duration_ms,
                                             uint32_t peer_id,
                                             int resolved,
                                             int resolution_method) {
    g_blackanalytics_state.total_events++;
    
    blackanalytics_event_t *event = &g_blackanalytics_state.events[g_blackanalytics_state.event_index];
    event->caid = caid;
    event->provid = provid;
    event->sid = sid;
    event->timestamp = time(NULL);
    event->duration_ms = duration_ms;
    event->peer_id = peer_id;
    event->resolved = resolved;
    event->resolution_method = resolution_method;
    
    g_blackanalytics_state.event_index = 
        (g_blackanalytics_state.event_index + 1) % BLACKANALYTICS_MAX_EVENTS;
    
    if (resolved) {
        g_blackanalytics_state.total_resolved++;
    } else {
        g_blackanalytics_state.total_unresolved++;
    }
    
    /* تحديث average duration */
    if (duration_ms > 0) {
        g_blackanalytics_state.avg_duration_ms = 
            (g_blackanalytics_state.avg_duration_ms * (g_blackanalytics_state.total_events - 1) + duration_ms)
            / g_blackanalytics_state.total_events;
    }
    
    /* فحص alert threshold */
    int recent_count = 0;
    time_t now = time(NULL);
    int i; for (i = 0; i < BLACKANALYTICS_MAX_EVENTS; i++) {
        if (g_blackanalytics_state.events[i].timestamp > 0) {
            double age = difftime(now, g_blackanalytics_state.events[i].timestamp);
            if (age < 60.0) {
                recent_count++;
            }
        }
    }
    
    if (recent_count >= BLACKANALYTICS_ALERT_THRESHOLD) {
        g_blackanalytics_state.alerts_triggered++;
    }
}

/* تحليل القنوات */
static inline void blackanalytics_analyze_channels(void) {
    /* مسح البيانات القديمة */
    memset(g_blackanalytics_state.top_channels, 0, sizeof(g_blackanalytics_state.top_channels));
    g_blackanalytics_state.channel_count = 0;
    
    /* جمع الإحصائيات */
    int i; for (i = 0; i < BLACKANALYTICS_MAX_EVENTS; i++) {
        blackanalytics_event_t *event = &g_blackanalytics_state.events[i];
        if (event->timestamp == 0) continue;
        
        /* البحث عن القناة */
        int found = 0;
        int j; for (j = 0; j < g_blackanalytics_state.channel_count; j++) {
            if (g_blackanalytics_state.top_channels[j].caid == event->caid &&
                g_blackanalytics_state.top_channels[j].sid == event->sid) {
                g_blackanalytics_state.top_channels[j].total_blackscreens++;
                g_blackanalytics_state.top_channels[j].total_duration_ms += event->duration_ms;
                g_blackanalytics_state.top_channels[j].last_blackscreen = event->timestamp;
                found = 1;
                break;
            }
        }
        
        if (!found && g_blackanalytics_state.channel_count < 10) {
            blackanalytics_channel_t *ch = &g_blackanalytics_state.top_channels[g_blackanalytics_state.channel_count++];
            ch->caid = event->caid;
            ch->sid = event->sid;
            ch->total_blackscreens = 1;
            ch->total_duration_ms = event->duration_ms;
            ch->last_blackscreen = event->timestamp;
        }
    }
}

/* تحليل الـ peers */
static inline void blackanalytics_analyze_peers(void) {
    /* مسح البيانات القديمة */
    memset(g_blackanalytics_state.top_peers, 0, sizeof(g_blackanalytics_state.top_peers));
    g_blackanalytics_state.peer_count = 0;
    
    /* جمع الإحصائيات */
    int i; for (i = 0; i < BLACKANALYTICS_MAX_EVENTS; i++) {
        blackanalytics_event_t *event = &g_blackanalytics_state.events[i];
        if (event->timestamp == 0) continue;
        
        /* البحث عن الـ peer */
        int found = 0;
        int j; for (j = 0; j < g_blackanalytics_state.peer_count; j++) {
            if (g_blackanalytics_state.top_peers[j].peer_id == event->peer_id) {
                g_blackanalytics_state.top_peers[j].total_blackscreens++;
                found = 1;
                break;
            }
        }
        
        if (!found && g_blackanalytics_state.peer_count < 10) {
            blackanalytics_peer_t *peer = &g_blackanalytics_state.top_peers[g_blackanalytics_state.peer_count++];
            peer->peer_id = event->peer_id;
            peer->total_blackscreens = 1;
            peer->total_cws_sent = 0;  /* يحتاج بيانات من cwsrcscore */
            peer->blackscreen_rate = 0.0;
        }
    }
}

/* الحصول على الإحصائيات العامة */
static inline void blackanalytics_get_stats(int *total, int *resolved, int *unresolved, 
                                             int *alerts, double *avg_duration) {
    if (total) *total = g_blackanalytics_state.total_events;
    if (resolved) *resolved = g_blackanalytics_state.total_resolved;
    if (unresolved) *unresolved = g_blackanalytics_state.total_unresolved;
    if (alerts) *alerts = g_blackanalytics_state.alerts_triggered;
    if (avg_duration) *avg_duration = g_blackanalytics_state.avg_duration_ms;
}

/* الحصول على عدد events في آخر دقيقة */
static inline int blackanalytics_get_recent_count(void) {
    int count = 0;
    time_t now = time(NULL);
    
    int i; for (i = 0; i < BLACKANALYTICS_MAX_EVENTS; i++) {
        if (g_blackanalytics_state.events[i].timestamp > 0) {
            double age = difftime(now, g_blackanalytics_state.events[i].timestamp);
            if (age < 60.0) {
                count++;
            }
        }
    }
    
    return count;
}

/* تصفير الإحصائيات */
static inline void blackanalytics_reset_stats(void) {
    memset(&g_blackanalytics_state, 0, sizeof(g_blackanalytics_state));
}

/* الحصول على أسوأ قناة */
static inline int blackanalytics_get_worst_channel(uint16_t *caid, uint16_t *sid) {
    if (g_blackanalytics_state.channel_count == 0) return 0;
    
    blackanalytics_channel_t *worst = &g_blackanalytics_state.top_channels[0];
    int i; for (i = 1; i < g_blackanalytics_state.channel_count; i++) {
        if (g_blackanalytics_state.top_channels[i].total_blackscreens > worst->total_blackscreens) {
            worst = &g_blackanalytics_state.top_channels[i];
        }
    }
    
    if (caid) *caid = worst->caid;
    if (sid) *sid = worst->sid;
    return worst->total_blackscreens;
}

/* الحصول على أسوأ peer */
static inline int blackanalytics_get_worst_peer(uint32_t *peer_id) {
    if (g_blackanalytics_state.peer_count == 0) return 0;
    
    blackanalytics_peer_t *worst = &g_blackanalytics_state.top_peers[0];
    int i; for (i = 1; i < g_blackanalytics_state.peer_count; i++) {
        if (g_blackanalytics_state.top_peers[i].total_blackscreens > worst->total_blackscreens) {
            worst = &g_blackanalytics_state.top_peers[i];
        }
    }
    
    if (peer_id) *peer_id = worst->peer_id;
    return worst->total_blackscreens;
}

#endif /* BLACKANALYTICS_H */
