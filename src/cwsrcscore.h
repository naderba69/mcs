#ifndef CWSRCSCORE_H
#define CWSRCSCORE_H

/*
 * CW Source Scoring — تقييم المصدر
 * 
 * الفكرة: كل peer له score ديناميكي
 * إذا أرسل CW فايك → score ينخفض
 * إذا score منخفض → تجاهل CWs منه
 * 
 * الميزة: يحمي من peers سيئين
 * التحدي: يحتاج persistence
 */

#include <string.h>
#include <stdint.h>
#include <time.h>

#define CWSRCSCORE_MAX_PEERS 256
#define CWSRCSCORE_BLACKLIST_THRESHOLD 0.2
#define CWSRCSCORE_WHITELIST_THRESHOLD 0.8
#define CWSRCSCORE_DECAY_RATE 0.01  /* score يتحسن ببطء مع الوقت */

/* Peer score structure */
typedef struct {
    uint32_t peer_id;
    int total_cws;
    int fake_cws;
    int good_cws;
    double score;  /* 0.0 - 1.0 */
    time_t last_update;
    time_t first_seen;
    int blacklisted;
    int whitelisted;
} cwsrcscore_peer_t;

/* Global state */
typedef struct {
    cwsrcscore_peer_t peers[CWSRCSCORE_MAX_PEERS];
    int peer_count;
    int total_checks;
    int total_blacklisted;
    int total_whitelisted;
} cwsrcscore_state_t;

static cwsrcscore_state_t g_cwsrcscore_state = {0};

/* البحث عن peer */
static inline cwsrcscore_peer_t *cwsrcscore_find_peer(uint32_t peer_id) {
    int i; for (i = 0; i < g_cwsrcscore_state.peer_count; i++) {
        if (g_cwsrcscore_state.peers[i].peer_id == peer_id) {
            return &g_cwsrcscore_state.peers[i];
        }
    }
    return NULL;
}

/* إضافة peer جديد */
static inline cwsrcscore_peer_t *cwsrcscore_add_peer(uint32_t peer_id) {
    if (g_cwsrcscore_state.peer_count >= CWSRCSCORE_MAX_PEERS) {
        /* إزالة أقدم peer */
        time_t oldest = time(NULL);
        int oldest_idx = 0;
        int i; for (i = 0; i < g_cwsrcscore_state.peer_count; i++) {
            if (g_cwsrcscore_state.peers[i].last_update < oldest) {
                oldest = g_cwsrcscore_state.peers[i].last_update;
                oldest_idx = i;
            }
        }
        /* استبدال */
        memset(&g_cwsrcscore_state.peers[oldest_idx], 0, sizeof(cwsrcscore_peer_t));
        g_cwsrcscore_state.peers[oldest_idx].peer_id = peer_id;
        g_cwsrcscore_state.peers[oldest_idx].score = 0.5;
        g_cwsrcscore_state.peers[oldest_idx].first_seen = time(NULL);
        return &g_cwsrcscore_state.peers[oldest_idx];
    }
    
    cwsrcscore_peer_t *peer = &g_cwsrcscore_state.peers[g_cwsrcscore_state.peer_count++];
    peer->peer_id = peer_id;
    peer->score = 0.5;  /* score افتراضي */
    peer->first_seen = time(NULL);
    peer->last_update = time(NULL);
    return peer;
}

/* تهيئة */
static inline void cwsrcscore_init(void) {
    memset(&g_cwsrcscore_state, 0, sizeof(g_cwsrcscore_state));
}

/* تحديث score بعد CW */
static inline void cwsrcscore_update(uint32_t peer_id, int was_fake) {
    g_cwsrcscore_state.total_checks++;
    
    cwsrcscore_peer_t *peer = cwsrcscore_find_peer(peer_id);
    if (!peer) {
        peer = cwsrcscore_add_peer(peer_id);
    }
    
    peer->total_cws++;
    peer->last_update = time(NULL);
    
    if (was_fake) {
        peer->fake_cws++;
        /* عقوبة فورية */
        peer->score *= 0.8;  /* خفض 20% */
    } else {
        peer->good_cws++;
        /* مكافأة */
        peer->score = peer->score * 0.9 + 0.1;  /* اقتراب من 1.0 */
    }
    
    /* حساب score دقيق */
    if (peer->total_cws > 0) {
        double accuracy = (double)peer->good_cws / peer->total_cws;
        peer->score = peer->score * 0.7 + accuracy * 0.3;
    }
    
    /* تحديث black/whitelist */
    if (peer->score < CWSRCSCORE_BLACKLIST_THRESHOLD && !peer->blacklisted) {
        peer->blacklisted = 1;
        peer->whitelisted = 0;
        g_cwsrcscore_state.total_blacklisted++;
    } else if (peer->score > CWSRCSCORE_WHITELIST_THRESHOLD && !peer->whitelisted) {
        peer->whitelisted = 1;
        peer->blacklisted = 0;
        g_cwsrcscore_state.total_whitelisted++;
    }
}

/* فحص إذا كان peer موثوق */
static inline int cwsrcscore_is_trusted(uint32_t peer_id) {
    cwsrcscore_peer_t *peer = cwsrcscore_find_peer(peer_id);
    if (!peer) return 1;  /* peer جديد → موثوق مؤقتاً */
    
    /* تطبيق decay (تحسن بطيء مع الوقت) */
    time_t now = time(NULL);
    double hours_since_update = difftime(now, peer->last_update) / 3600.0;
    if (hours_since_update > 1.0) {
        peer->score += CWSRCSCORE_DECAY_RATE * hours_since_update;
        if (peer->score > 1.0) peer->score = 1.0;
        peer->last_update = now;
    }
    
    return !peer->blacklisted;
}

/* الحصول على score */
static inline double cwsrcscore_get_score(uint32_t peer_id) {
    cwsrcscore_peer_t *peer = cwsrcscore_find_peer(peer_id);
    if (!peer) return 0.5;
    return peer->score;
}

/* إزالة من blacklist */
static inline void cwsrcscore_unblacklist(uint32_t peer_id) {
    cwsrcscore_peer_t *peer = cwsrcscore_find_peer(peer_id);
    if (peer) {
        peer->blacklisted = 0;
        peer->score = 0.3;  /* score منخفض لكن ليس blacklisted */
    }
}

/* الحصول على عدد الـ peers */
static inline int cwsrcscore_get_peer_count(void) {
    return g_cwsrcscore_state.peer_count;
}

/* الحصول على عدد الـ blacklisted */
static inline int cwsrcscore_get_blacklisted_count(void) {
    return g_cwsrcscore_state.total_blacklisted;
}

/* تصفير الإحصائيات */
static inline void cwsrcscore_reset_stats(void) {
    g_cwsrcscore_state.total_checks = 0;
    g_cwsrcscore_state.total_blacklisted = 0;
    g_cwsrcscore_state.total_whitelisted = 0;
}

#endif /* CWSRCSCORE_H */
