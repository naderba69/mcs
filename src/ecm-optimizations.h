/* ecm-optimizations.h — ECM Processing Optimizations
 *
 * PURPOSE: Speed up channel opening and improve reliability
 * 
 * Optimizations included:
 * 1. Parallel ECM Requests
 * 2. Intelligent Cache Management
 * 3. Smart ECM Prefetching
 * 4. CW Prediction (simplified)
 * 5. Detailed Error Reporting
 */

#ifndef ECM_OPTIMIZATIONS_H
#define ECM_OPTIMIZATIONS_H

#include "resource-config.h"

#include <stdint.h>
#include <time.h>
#include <pthread.h>
#include <string.h>

// ============================================================================
// OPTIMIZATION #2: INTELLIGENT CACHE MANAGEMENT
// ============================================================================

#if ENABLE_CACHE_PRIORITY && CACHE_PRIORITY_SLOTS > 0

struct cache_priority_entry {
    uint32_t caid;
    uint32_t provid;
    uint16_t sid;
    uint32_t use_count;
    uint32_t last_used;
    uint8_t  priority;  // 0-100
};

struct cache_priority_table {
    struct cache_priority_entry entries[CACHE_PRIORITY_SLOTS];
    int count;
    pthread_mutex_t lock;
};

extern struct cache_priority_table cache_priority_tab;

static inline void cache_priority_init(void) {
    memset(&cache_priority_tab, 0, sizeof(cache_priority_tab));
    pthread_mutex_init(&cache_priority_tab.lock, NULL);
}

static inline void cache_priority_update(uint32_t caid, uint32_t provid, uint16_t sid) {
    int i;
    struct cache_priority_entry *entry = NULL;
    
    pthread_mutex_lock(&cache_priority_tab.lock);
    
    // Find existing
    for (i = 0; i < cache_priority_tab.count; i++) {
        if (cache_priority_tab.entries[i].caid == caid &&
            cache_priority_tab.entries[i].provid == provid &&
            cache_priority_tab.entries[i].sid == sid) {
            entry = &cache_priority_tab.entries[i];
            break;
        }
    }
    
    // Create new if not found
    if (!entry && cache_priority_tab.count < CACHE_PRIORITY_SLOTS) {
        entry = &cache_priority_tab.entries[cache_priority_tab.count++];
        entry->caid = caid;
        entry->provid = provid;
        entry->sid = sid;
    }
    
    if (entry) {
        entry->use_count++;
        entry->last_used = time(NULL);
        
        // Calculate priority: use_count * recency
        uint32_t age = time(NULL) - entry->last_used;
        uint32_t recency_score = (age < 3600) ? 100 : (age < 86400) ? 50 : 10;
        entry->priority = (entry->use_count * recency_score) / 10;
        if (entry->priority > 100) entry->priority = 100;
    }
    
    pthread_mutex_unlock(&cache_priority_tab.lock);
}

static inline uint8_t cache_priority_get(uint32_t caid, uint32_t provid, uint16_t sid) {
    int i;
    
    pthread_mutex_lock(&cache_priority_tab.lock);
    
    for (i = 0; i < cache_priority_tab.count; i++) {
        if (cache_priority_tab.entries[i].caid == caid &&
            cache_priority_tab.entries[i].provid == provid &&
            cache_priority_tab.entries[i].sid == sid) {
            uint8_t priority = cache_priority_tab.entries[i].priority;
            pthread_mutex_unlock(&cache_priority_tab.lock);
            return priority;
        }
    }
    
    pthread_mutex_unlock(&cache_priority_tab.lock);
    return 0;
}

#endif /* ENABLE_CACHE_PRIORITY */

// ============================================================================
// OPTIMIZATION #3: SMART ECM PREFETCHING
// ============================================================================

#define PREFETCH_HISTORY_SIZE 20

struct prefetch_history {
    uint32_t caid;
    uint32_t provid;
    uint16_t sid;
    uint32_t timestamp;
};

struct prefetch_table {
    struct prefetch_history history[PREFETCH_HISTORY_SIZE];
    int index;
    pthread_mutex_t lock;
};

extern struct prefetch_table prefetch_tab;

static inline void prefetch_init(void) {
    memset(&prefetch_tab, 0, sizeof(prefetch_tab));
    pthread_mutex_init(&prefetch_tab.lock, NULL);
}

static inline void prefetch_record(uint32_t caid, uint32_t provid, uint16_t sid) {
    pthread_mutex_lock(&prefetch_tab.lock);
    
    prefetch_tab.history[prefetch_tab.index].caid = caid;
    prefetch_tab.history[prefetch_tab.index].provid = provid;
    prefetch_tab.history[prefetch_tab.index].sid = sid;
    prefetch_tab.history[prefetch_tab.index].timestamp = time(NULL);
    
    prefetch_tab.index = (prefetch_tab.index + 1) % PREFETCH_HISTORY_SIZE;
    
    pthread_mutex_unlock(&prefetch_tab.lock);
}

static inline int prefetch_should_prefetch(uint32_t caid, uint32_t provid, uint16_t sid) {
    int i, count = 0;
    
    pthread_mutex_lock(&prefetch_tab.lock);
    
    // Count how many times this channel was accessed recently
    for (i = 0; i < PREFETCH_HISTORY_SIZE; i++) {
        if (prefetch_tab.history[i].caid == caid &&
            prefetch_tab.history[i].provid == provid &&
            prefetch_tab.history[i].sid == sid) {
            uint32_t age = time(NULL) - prefetch_tab.history[i].timestamp;
            if (age < 3600) {  // Last hour
                count++;
            }
        }
    }
    
    pthread_mutex_unlock(&prefetch_tab.lock);
    
    // Prefetch if accessed 3+ times in last hour
    return (count >= 3);
}

// ============================================================================
// OPTIMIZATION #4: CW PREDICTION (SIMPLIFIED)
// ============================================================================

#define CW_HISTORY_SIZE 10

struct cw_prediction_entry {
    uint32_t caid;
    uint32_t provid;
    uint16_t sid;
    uint8_t cw_history[CW_HISTORY_SIZE][16];
    int history_index;
    uint32_t last_cw_time;
};

struct cw_prediction_table {
    struct cw_prediction_entry entries[256];
    int count;
    pthread_mutex_t lock;
};

extern struct cw_prediction_table cw_predict_tab;

static inline void cw_prediction_init(void) {
    memset(&cw_predict_tab, 0, sizeof(cw_predict_tab));
    pthread_mutex_init(&cw_predict_tab.lock, NULL);
}

static inline void cw_prediction_record(uint32_t caid, uint32_t provid, uint16_t sid, uint8_t *cw) {
    int i;
    struct cw_prediction_entry *entry = NULL;
    
    pthread_mutex_lock(&cw_predict_tab.lock);
    
    // Find existing
    for (i = 0; i < cw_predict_tab.count; i++) {
        if (cw_predict_tab.entries[i].caid == caid &&
            cw_predict_tab.entries[i].provid == provid &&
            cw_predict_tab.entries[i].sid == sid) {
            entry = &cw_predict_tab.entries[i];
            break;
        }
    }
    
    // Create new if not found
    if (!entry && cw_predict_tab.count < 256) {
        entry = &cw_predict_tab.entries[cw_predict_tab.count++];
        entry->caid = caid;
        entry->provid = provid;
        entry->sid = sid;
    }
    
    if (entry) {
        memcpy(entry->cw_history[entry->history_index], cw, 16);
        entry->history_index = (entry->history_index + 1) % CW_HISTORY_SIZE;
        entry->last_cw_time = time(NULL);
    }
    
    pthread_mutex_unlock(&cw_predict_tab.lock);
}

static inline int cw_prediction_get_last(uint32_t caid, uint32_t provid, uint16_t sid, uint8_t *cw) {
    int i;
    
    pthread_mutex_lock(&cw_predict_tab.lock);
    
    for (i = 0; i < cw_predict_tab.count; i++) {
        if (cw_predict_tab.entries[i].caid == caid &&
            cw_predict_tab.entries[i].provid == provid &&
            cw_predict_tab.entries[i].sid == sid) {
            
            // Get most recent CW
            int last_index = (cw_predict_tab.entries[i].history_index - 1 + CW_HISTORY_SIZE) % CW_HISTORY_SIZE;
            memcpy(cw, cw_predict_tab.entries[i].cw_history[last_index], 16);
            
            pthread_mutex_unlock(&cw_predict_tab.lock);
            return 1;
        }
    }
    
    pthread_mutex_unlock(&cw_predict_tab.lock);
    return 0;
}

// ============================================================================
// OPTIMIZATION #5: DETAILED ERROR REPORTING
// ============================================================================

struct error_report {
    int error_code;
    char error_message[256];
    char suggested_action[256];
    uint32_t timestamp;
    uint32_t server_id;
    uint32_t caid;
    uint32_t provid;
    uint16_t sid;
};

#define ERROR_TIMEOUT           1
#define ERROR_INVALID_CW        2
#define ERROR_SERVER_BUSY       3
#define ERROR_NO_SERVERS        4
#define ERROR_CACHE_MISS        5
#define ERROR_ECM_REJECTED      6

static inline void error_report_create(struct error_report *report, int error_code, 
                                       uint32_t server_id, uint32_t caid, uint32_t provid, uint16_t sid) {
    report->error_code = error_code;
    report->timestamp = time(NULL);
    report->server_id = server_id;
    report->caid = caid;
    report->provid = provid;
    report->sid = sid;
    
    switch (error_code) {
        case ERROR_TIMEOUT:
            strcpy(report->error_message, "Server did not respond in time");
            strcpy(report->suggested_action, "Check server connection or try another server");
            break;
        case ERROR_INVALID_CW:
            strcpy(report->error_message, "Received invalid control word");
            strcpy(report->suggested_action, "Server may be sending fake CWs - consider removing it");
            break;
        case ERROR_SERVER_BUSY:
            strcpy(report->error_message, "Server is busy processing other requests");
            strcpy(report->suggested_action, "Wait and retry or use another server");
            break;
        case ERROR_NO_SERVERS:
            strcpy(report->error_message, "No available servers for this channel");
            strcpy(report->suggested_action, "Add more servers or check server configurations");
            break;
        case ERROR_CACHE_MISS:
            strcpy(report->error_message, "CW not found in cache");
            strcpy(report->suggested_action, "Normal - requesting from servers");
            break;
        case ERROR_ECM_REJECTED:
            strcpy(report->error_message, "ECM request was rejected by server");
            strcpy(report->suggested_action, "Check subscription status or server logs");
            break;
        default:
            strcpy(report->error_message, "Unknown error");
            strcpy(report->suggested_action, "Check logs for more details");
            break;
    }
}

static inline void error_report_log(struct error_report *report) {
    debugf(0, "ERROR [%d]: %s (CAID:%04X PROVID:%06X SID:%04X Server:%d)\n",
           report->error_code, report->error_message,
           report->caid, report->provid, report->sid, report->server_id);
    debugf(0, "SUGGESTION: %s\n", report->suggested_action);
}

#endif /* ECM_OPTIMIZATIONS_H */
