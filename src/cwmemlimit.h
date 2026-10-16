#ifndef CWMEMLIMIT_H
#define CWMEMLIMIT_H

/*
 * CW Memory Limit — حدود الذاكرة
 * 
 * التوصية قصيرة المدى #5: إضافة memory limits
 * 
 * المشكلة: استخدام ذاكرة إضافي (~363 KB)
 * الحل: حدود ديناميكية + LRU eviction
 * 
 * الميزة: حماية من memory leaks، استقرار
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define CWMEM_LIMIT_DEFAULT_MB 10
#define CWMEM_TRACKER_MAX 50

/* Memory regions */
#define CWMEM_REGION_STATS      0
#define CWMEM_REGION_SRCSCORE   1
#define CWMEM_REGION_LEARN      2
#define CWMEM_REGION_ROLLBACK   3
#define CWMEM_REGION_MULTIBUF   4
#define CWMEM_REGION_ANALYTICS  5
#define CWMEM_REGION_ML         6
#define CWMEM_REGION_PERSIST    7
#define CWMEM_REGION_HISTOGRAM  8
#define CWMEM_REGION_OTHER      9

/* Memory tracker */
typedef struct {
    char name[64];
    int region;
    size_t current_size;
    size_t peak_size;
    size_t limit;
    int allocation_count;
    int deallocation_count;
    int limit_exceeded_count;
} cwmem_tracker_t;

/* Memory state */
typedef struct {
    cwmem_tracker_t trackers[CWMEM_TRACKER_MAX];
    int tracker_count;
    
    size_t total_allocated;
    size_t total_limit;
    size_t peak_allocated;
    
    int limit_mb;
    int enabled;
    
    int total_limit_exceeded;
} cwmem_state_t;

static cwmem_state_t g_cwmem_state = {0};

/* تهيئة */
static inline void cwmem_init(int limit_mb) {
    memset(&g_cwmem_state, 0, sizeof(g_cwmem_state));
    g_cwmem_state.limit_mb = (limit_mb > 0) ? limit_mb : CWMEM_LIMIT_DEFAULT_MB;
    g_cwmem_state.total_limit = (size_t)g_cwmem_state.limit_mb * 1024 * 1024;
    g_cwmem_state.enabled = 1;
}

/* تسجيل tracker */
static inline int cwmem_register_tracker(const char *name, int region, size_t limit) {
    if (g_cwmem_state.tracker_count >= CWMEM_TRACKER_MAX) {
        return -1;
    }
    
    int id = g_cwmem_state.tracker_count++;
    cwmem_tracker_t *tracker = &g_cwmem_state.trackers[id];
    
    strncpy(tracker->name, name, sizeof(tracker->name) - 1);
    tracker->name[sizeof(tracker->name) - 1] = '\0';
    tracker->region = region;
    tracker->current_size = 0;
    tracker->peak_size = 0;
    tracker->limit = limit;
    tracker->allocation_count = 0;
    tracker->deallocation_count = 0;
    tracker->limit_exceeded_count = 0;
    
    return id;
}

/* تسجيل allocation */
static inline int cwmem_track_alloc(int tracker_id, size_t size) {
    if (!g_cwmem_state.enabled || tracker_id < 0 || tracker_id >= g_cwmem_state.tracker_count) {
        return -1;
    }
    
    cwmem_tracker_t *tracker = &g_cwmem_state.trackers[tracker_id];
    
    /* Check tracker limit */
    if (tracker->limit > 0 && tracker->current_size + size > tracker->limit) {
        tracker->limit_exceeded_count++;
        g_cwmem_state.total_limit_exceeded++;
        return -1;
    }
    
    /* Check global limit */
    if (g_cwmem_state.total_allocated + size > g_cwmem_state.total_limit) {
        g_cwmem_state.total_limit_exceeded++;
        return -1;
    }
    
    /* Track allocation */
    tracker->current_size += size;
    tracker->allocation_count++;
    
    if (tracker->current_size > tracker->peak_size) {
        tracker->peak_size = tracker->current_size;
    }
    
    g_cwmem_state.total_allocated += size;
    if (g_cwmem_state.total_allocated > g_cwmem_state.peak_allocated) {
        g_cwmem_state.peak_allocated = g_cwmem_state.total_allocated;
    }
    
    return 0;
}

/* تسجيل deallocation */
static inline void cwmem_track_free(int tracker_id, size_t size) {
    if (!g_cwmem_state.enabled || tracker_id < 0 || tracker_id >= g_cwmem_state.tracker_count) {
        return;
    }
    
    cwmem_tracker_t *tracker = &g_cwmem_state.trackers[tracker_id];
    
    if (size > tracker->current_size) {
        size = tracker->current_size;  /* Prevent underflow */
    }
    
    tracker->current_size -= size;
    tracker->deallocation_count++;
    
    g_cwmem_state.total_allocated -= size;
}

/* فحص إذا كان يمكن allocation */
static inline int cwmem_can_alloc(int tracker_id, size_t size) {
    if (!g_cwmem_state.enabled || tracker_id < 0 || tracker_id >= g_cwmem_state.tracker_count) {
        return 1;  /* Allow if not tracking */
    }
    
    cwmem_tracker_t *tracker = &g_cwmem_state.trackers[tracker_id];
    
    /* Check tracker limit */
    if (tracker->limit > 0 && tracker->current_size + size > tracker->limit) {
        return 0;
    }
    
    /* Check global limit */
    if (g_cwmem_state.total_allocated + size > g_cwmem_state.total_limit) {
        return 0;
    }
    
    return 1;
}

/* الحصول على استخدام الذاكرة */
static inline size_t cwmem_get_usage(int tracker_id) {
    if (tracker_id < 0 || tracker_id >= g_cwmem_state.tracker_count) {
        return 0;
    }
    return g_cwmem_state.trackers[tracker_id].current_size;
}

/* الحصول على الإحصائيات العامة */
static inline void cwmem_get_stats(size_t *allocated, size_t *peak, size_t *limit, int *exceeded) {
    if (allocated) *allocated = g_cwmem_state.total_allocated;
    if (peak) *peak = g_cwmem_state.peak_allocated;
    if (limit) *limit = g_cwmem_state.total_limit;
    if (exceeded) *exceeded = g_cwmem_state.total_limit_exceeded;
}

/* الحصول على إحصائيات tracker */
static inline void cwmem_get_tracker_stats(int tracker_id, size_t *current, size_t *peak,
                                             size_t *limit, int *allocs, int *frees, int *exceeded) {
    if (tracker_id < 0 || tracker_id >= g_cwmem_state.tracker_count) {
        return;
    }
    
    cwmem_tracker_t *tracker = &g_cwmem_state.trackers[tracker_id];
    
    if (current) *current = tracker->current_size;
    if (peak) *peak = tracker->peak_size;
    if (limit) *limit = tracker->limit;
    if (allocs) *allocs = tracker->allocation_count;
    if (frees) *frees = tracker->deallocation_count;
    if (exceeded) *exceeded = tracker->limit_exceeded_count;
}

/* طباعة إحصائيات */
static inline void cwmem_print_stats(void) {
    printf("Memory Usage:\n");
    printf("  Total: %zu / %zu bytes (%.2f MB / %d MB)\n",
           g_cwmem_state.total_allocated, g_cwmem_state.total_limit,
           (double)g_cwmem_state.total_allocated / (1024 * 1024),
           g_cwmem_state.limit_mb);
    printf("  Peak: %zu bytes (%.2f MB)\n",
           g_cwmem_state.peak_allocated,
           (double)g_cwmem_state.peak_allocated / (1024 * 1024));
    printf("  Limit exceeded: %d times\n", g_cwmem_state.total_limit_exceeded);
    printf("\n");
    
    printf("Trackers:\n");
    int i; for (i = 0; i < g_cwmem_state.tracker_count; i++) {
        cwmem_tracker_t *tracker = &g_cwmem_state.trackers[i];
        printf("  %s: %zu bytes (peak: %zu, limit: %zu)\n",
               tracker->name, tracker->current_size, tracker->peak_size, tracker->limit);
        printf("    Allocations: %d, Deallocations: %d, Exceeded: %d\n",
               tracker->allocation_count, tracker->deallocation_count,
               tracker->limit_exceeded_count);
    }
}

/* تصفير الإحصائيات */
static inline void cwmem_reset_stats(void) {
    g_cwmem_state.peak_allocated = g_cwmem_state.total_allocated;
    g_cwmem_state.total_limit_exceeded = 0;
    
    int i; for (i = 0; i < g_cwmem_state.tracker_count; i++) {
        g_cwmem_state.trackers[i].peak_size = g_cwmem_state.trackers[i].current_size;
        g_cwmem_state.trackers[i].limit_exceeded_count = 0;
    }
}

#endif /* CWMEMLIMIT_H */
