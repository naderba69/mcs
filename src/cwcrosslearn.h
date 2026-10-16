#ifndef CWCROSSLEARN_H
#define CWCROSSLEARN_H

/*
 * CW Cross-Channel Learning — التعلم عبر القنوات
 * 
 * التوصية قصيرة المدى #6: إضافة cross-channel learning
 * 
 * المشكلة: cwlearn يتعلم لكل قناة بشكل منفصل
 * الحل: مشاركة الأنماط بين القنوات المتشابهة
 * 
 * الميزة: تعلم أسرع، حماية أفضل للقنوات الجديدة
 */

#include <stdint.h>
#include <string.h>
#include <time.h>

#define CWCROSSLEARN_MAX_GROUPS 50
#define CWCROSSLEARN_MAX_CHANNELS_PER_GROUP 100

/* Channel group */
typedef struct {
    uint16_t caid;
    uint32_t provid;
    
    uint16_t sids[CWCROSSLEARN_MAX_CHANNELS_PER_GROUP];
    int sid_count;
    
    unsigned char shared_pattern[16];
    double confidence[16];
    int total_samples;
    
    time_t last_update;
} cwcrosslearn_group_t;

/* Cross-learn state */
typedef struct {
    cwcrosslearn_group_t groups[CWCROSSLEARN_MAX_GROUPS];
    int group_count;
    
    int total_learns;
    int total_matches;
    int total_mismatches;
    int total_new_channels;
    
    int enabled;
} cwcrosslearn_state_t;

static cwcrosslearn_state_t g_cwcrosslearn_state = {0};

/* تهيئة */
static inline void cwcrosslearn_init(void) {
    memset(&g_cwcrosslearn_state, 0, sizeof(g_cwcrosslearn_state));
    g_cwcrosslearn_state.enabled = 1;
}

/* البحث عن group */
static inline cwcrosslearn_group_t *cwcrosslearn_find_group(uint16_t caid, uint32_t provid) {
    int i; for (i = 0; i < g_cwcrosslearn_state.group_count; i++) {
        if (g_cwcrosslearn_state.groups[i].caid == caid &&
            g_cwcrosslearn_state.groups[i].provid == provid) {
            return &g_cwcrosslearn_state.groups[i];
        }
    }
    return NULL;
}

/* إنشاء group جديد */
static inline cwcrosslearn_group_t *cwcrosslearn_create_group(uint16_t caid, uint32_t provid) {
    if (g_cwcrosslearn_state.group_count >= CWCROSSLEARN_MAX_GROUPS) {
        return NULL;
    }
    
    cwcrosslearn_group_t *group = &g_cwcrosslearn_state.groups[g_cwcrosslearn_state.group_count++];
    memset(group, 0, sizeof(cwcrosslearn_group_t));
    group->caid = caid;
    group->provid = provid;
    group->last_update = time(NULL);
    
    return group;
}

/* إضافة قناة إلى group */
static inline int cwcrosslearn_add_channel(cwcrosslearn_group_t *group, uint16_t sid) {
    if (!group || group->sid_count >= CWCROSSLEARN_MAX_CHANNELS_PER_GROUP) {
        return -1;
    }
    
    /* Check if already exists */
    int i; for (i = 0; i < group->sid_count; i++) {
        if (group->sids[i] == sid) {
            return 0;  /* Already exists */
        }
    }
    
    group->sids[group->sid_count++] = sid;
    g_cwcrosslearn_state.total_new_channels++;
    
    return 1;  /* Added */
}

/* تعلم من CW */
static inline void cwcrosslearn_learn(uint16_t caid, uint32_t provid, uint16_t sid,
                                       const unsigned char *cw) {
    if (!g_cwcrosslearn_state.enabled) {
        return;
    }
    
    g_cwcrosslearn_state.total_learns++;
    
    cwcrosslearn_group_t *group = cwcrosslearn_find_group(caid, provid);
    if (!group) {
        group = cwcrosslearn_create_group(caid, provid);
    }
    
    if (!group) {
        return;  /* Failed to create group */
    }
    
    /* Add channel to group */
    cwcrosslearn_add_channel(group, sid);
    
    /* Update shared pattern */
    group->total_samples++;
    group->last_update = time(NULL);
    
    /* Simple averaging for pattern */
    int i; for (i = 0; i < 16; i++) {
        double old_val = group->shared_pattern[i];
        double new_val = cw[i];
        double weight = 1.0 / group->total_samples;
        
        group->shared_pattern[i] = (unsigned char)(old_val * (1.0 - weight) + new_val * weight);
        
        /* Update confidence */
        if (group->total_samples > 10) {
            int matches = 0;
            int j; for (j = 0; j < group->sid_count && j < 10; j++) {
                /* Simplified: assume we have access to historical CWs */
                matches++;  /* Placeholder */
            }
            group->confidence[i] = (double)matches / 10.0;
        }
    }
}

/* فحص إذا كان CW يطابق النمط المشترك */
static inline int cwcrosslearn_matches(uint16_t caid, uint32_t provid, uint16_t sid,
                                        const unsigned char *cw) {
    if (!g_cwcrosslearn_state.enabled) {
        return 1;  /* Allow if disabled */
    }
    
    cwcrosslearn_group_t *group = cwcrosslearn_find_group(caid, provid);
    if (!group || group->total_samples < 10) {
        return 1;  /* Not enough data, allow */
    }
    
    /* Calculate match score */
    int matches = 0;
    double total_confidence = 0.0;
    
    int i; for (i = 0; i < 16; i++) {
        int diff = (cw[i] > group->shared_pattern[i]) ? 
                   (cw[i] - group->shared_pattern[i]) : 
                   (group->shared_pattern[i] - cw[i]);
        
        if (diff <= 5) {  /* Allow some tolerance */
            matches++;
            total_confidence += group->confidence[i];
        }
    }
    
    double avg_confidence = total_confidence / 16.0;
    
    if (matches >= 12 && avg_confidence >= 0.6) {
        g_cwcrosslearn_state.total_matches++;
        return 1;  /* Matches */
    }
    
    g_cwcrosslearn_state.total_mismatches++;
    return 0;  /* Doesn't match */
}

/* الحصول على الإحصائيات */
static inline void cwcrosslearn_get_stats(int *learns, int *matches, int *mismatches, 
                                           int *new_channels, int *groups) {
    if (learns) *learns = g_cwcrosslearn_state.total_learns;
    if (matches) *matches = g_cwcrosslearn_state.total_matches;
    if (mismatches) *mismatches = g_cwcrosslearn_state.total_mismatches;
    if (new_channels) *new_channels = g_cwcrosslearn_state.total_new_channels;
    if (groups) *groups = g_cwcrosslearn_state.group_count;
}

/* تصفير الإحصائيات */
static inline void cwcrosslearn_reset_stats(void) {
    g_cwcrosslearn_state.total_learns = 0;
    g_cwcrosslearn_state.total_matches = 0;
    g_cwcrosslearn_state.total_mismatches = 0;
    g_cwcrosslearn_state.total_new_channels = 0;
}

#endif /* CWCROSSLEARN_H */
