#ifndef CWFASTPATH_H
#define CWFASTPATH_H

/*
 * CW Fast Path — المسار السريع
 * 
 * التوصية قصيرة المدى #4: إضافة fast path للحالات البسيطة
 * 
 * المشكلة: overhead عالي لكل CW (~20μs)
 * الحل: fast path للحالات الشائعة
 * 
 * الميزة: تقليل latency، تحسين الأداء
 */

#include <stdint.h>
#include <string.h>

#define CWFASTPATH_CACHE_SIZE 256
#define CWFASTPATH_HIT_THRESHOLD 10

/* Fast path entry */
typedef struct {
    uint16_t caid;
    uint16_t provid;
    uint16_t sid;
    uint32_t peer_id;
    unsigned char cw_hash[8];  /* first 8 bytes of CW hash */
    
    int hit_count;
    time_t last_seen;
    int trusted;  /* 1 if this path is trusted */
} cwfastpath_entry_t;

/* Fast path state */
typedef struct {
    cwfastpath_entry_t cache[CWFASTPATH_CACHE_SIZE];
    int cache_size;
    
    uint64_t total_checks;
    uint64_t fast_path_hits;
    uint64_t fast_path_misses;
    uint64_t slow_path_count;
    
    int enabled;
    int hit_threshold;
} cwfastpath_state_t;

static cwfastpath_state_t g_cwfastpath_state = {0};

/* تهيئة */
static inline void cwfastpath_init(void) {
    memset(&g_cwfastpath_state, 0, sizeof(g_cwfastpath_state));
    g_cwfastpath_state.enabled = 1;
    g_cwfastpath_state.hit_threshold = CWFASTPATH_HIT_THRESHOLD;
}

/* حساب hash بسيط */
static inline void cwfastpath_hash(const unsigned char *cw, unsigned char *hash_out) {
    uint32_t h = 0;
    int i; for (i = 0; i < 16; i++) {
        h = h * 31 + cw[i];
    }
    memcpy(hash_out, &h, 4);
    memcpy(hash_out + 4, &h, 4);
}

/* البحث في cache */
static inline cwfastpath_entry_t *cwfastpath_find(uint16_t caid, uint16_t provid, 
                                                    uint16_t sid, uint32_t peer_id,
                                                    const unsigned char *cw_hash) {
    int i; for (i = 0; i < g_cwfastpath_state.cache_size; i++) {
        cwfastpath_entry_t *entry = &g_cwfastpath_state.cache[i];
        if (entry->caid == caid && entry->provid == provid && 
            entry->sid == sid && entry->peer_id == peer_id &&
            memcmp(entry->cw_hash, cw_hash, 8) == 0) {
            return entry;
        }
    }
    return NULL;
}

/* إضافة إلى cache */
static inline cwfastpath_entry_t *cwfastpath_add(uint16_t caid, uint16_t provid,
                                                   uint16_t sid, uint32_t peer_id,
                                                   const unsigned char *cw_hash) {
    if (g_cwfastpath_state.cache_size >= CWFASTPATH_CACHE_SIZE) {
        /* Remove oldest entry */
        time_t oldest = time(NULL);
        int oldest_idx = 0;
        int i; for (i = 0; i < g_cwfastpath_state.cache_size; i++) {
            if (g_cwfastpath_state.cache[i].last_seen < oldest) {
                oldest = g_cwfastpath_state.cache[i].last_seen;
                oldest_idx = i;
            }
        }
        /* Replace */
        cwfastpath_entry_t *entry = &g_cwfastpath_state.cache[oldest_idx];
        entry->caid = caid;
        entry->provid = provid;
        entry->sid = sid;
        entry->peer_id = peer_id;
        memcpy(entry->cw_hash, cw_hash, 8);
        entry->hit_count = 1;
        entry->last_seen = time(NULL);
        entry->trusted = 0;
        return entry;
    }
    
    /* Add new */
    cwfastpath_entry_t *entry = &g_cwfastpath_state.cache[g_cwfastpath_state.cache_size++];
    entry->caid = caid;
    entry->provid = provid;
    entry->sid = sid;
    entry->peer_id = peer_id;
    memcpy(entry->cw_hash, cw_hash, 8);
    entry->hit_count = 1;
    entry->last_seen = time(NULL);
    entry->trusted = 0;
    return entry;
}

/* فحص إذا كان يمكن استخدام fast path */
static inline int cwfastpath_check(uint16_t caid, uint16_t provid, uint16_t sid,
                                    uint32_t peer_id, const unsigned char *cw) {
    if (!g_cwfastpath_state.enabled) {
        return 0;  /* Use slow path */
    }
    
    g_cwfastpath_state.total_checks++;
    
    unsigned char cw_hash[8];
    cwfastpath_hash(cw, cw_hash);
    
    cwfastpath_entry_t *entry = cwfastpath_find(caid, provid, sid, peer_id, cw_hash);
    
    if (entry) {
        /* Found in cache */
        entry->hit_count++;
        entry->last_seen = time(NULL);
        
        /* Check if trusted */
        if (entry->hit_count >= g_cwfastpath_state.hit_threshold && entry->trusted) {
            g_cwfastpath_state.fast_path_hits++;
            return 1;  /* Use fast path */
        }
    } else {
        /* Not found, add to cache */
        cwfastpath_add(caid, provid, sid, peer_id, cw_hash);
    }
    
    g_cwfastpath_state.fast_path_misses++;
    return 0;  /* Use slow path */
}

/* تعليم entry كـ trusted */
static inline void cwfastpath_mark_trusted(uint16_t caid, uint16_t provid, uint16_t sid,
                                            uint32_t peer_id, const unsigned char *cw) {
    unsigned char cw_hash[8];
    cwfastpath_hash(cw, cw_hash);
    
    cwfastpath_entry_t *entry = cwfastpath_find(caid, provid, sid, peer_id, cw_hash);
    if (entry) {
        entry->trusted = 1;
    }
}

/* إلغاء تعليم entry */
static inline void cwfastpath_mark_untrusted(uint16_t caid, uint16_t provid, uint16_t sid,
                                              uint32_t peer_id, const unsigned char *cw) {
    unsigned char cw_hash[8];
    cwfastpath_hash(cw, cw_hash);
    
    cwfastpath_entry_t *entry = cwfastpath_find(caid, provid, sid, peer_id, cw_hash);
    if (entry) {
        entry->trusted = 0;
    }
}

/* الحصول على الإحصائيات */
static inline void cwfastpath_get_stats(uint64_t *total, uint64_t *hits, 
                                         uint64_t *misses, uint64_t *slow) {
    if (total) *total = g_cwfastpath_state.total_checks;
    if (hits) *hits = g_cwfastpath_state.fast_path_hits;
    if (misses) *misses = g_cwfastpath_state.fast_path_misses;
    if (slow) *slow = g_cwfastpath_state.slow_path_count;
}

/* حساب hit rate */
static inline double cwfastpath_hit_rate(void) {
    if (g_cwfastpath_state.total_checks == 0) return 0.0;
    return (double)g_cwfastpath_state.fast_path_hits / g_cwfastpath_state.total_checks * 100.0;
}

/* تصفير الإحصائيات */
static inline void cwfastpath_reset_stats(void) {
    g_cwfastpath_state.total_checks = 0;
    g_cwfastpath_state.fast_path_hits = 0;
    g_cwfastpath_state.fast_path_misses = 0;
    g_cwfastpath_state.slow_path_count = 0;
}

/* مسح cache */
static inline void cwfastpath_clear_cache(void) {
    memset(g_cwfastpath_state.cache, 0, sizeof(g_cwfastpath_state.cache));
    g_cwfastpath_state.cache_size = 0;
}

#endif /* CWFASTPATH_H */
