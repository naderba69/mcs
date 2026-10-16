#ifndef CWEXPLAIN_H
#define CWEXPLAIN_H

/*
 * CW Explain — طبقة التفسير
 * 
 * التوصية قصيرة المدى #2: إضافة explainability layer
 * 
 * المشكلة: ML هو black box، صعوبة فهم القرارات
 * الحل: تفسير كل قرار
 * 
 * الميزة: شفافية، ثقة، سهولة debugging
 */

#include <stdint.h>
#include <string.h>
#include <stdio.h>

#define CWEXPLAIN_MAX_REASONS 10
#define CWEXPLAIN_MAX_DESC 256

/* Decision types */
#define CWEXPLAIN_DECISION_ACCEPT  0
#define CWEXPLAIN_DECISION_REJECT  1
#define CWEXPLAIN_DECISION_WARN    2

/* Reason structure */
typedef struct {
    char description[CWEXPLAIN_MAX_DESC];
    double weight;      /* importance of this reason (-1.0 to 1.0) */
    int is_positive;    /* 1 = supports decision, 0 = against */
} cwexplain_reason_t;

/* Explanation structure */
typedef struct {
    int decision;
    double confidence;  /* 0.0 to 1.0 */
    
    cwexplain_reason_t reasons[CWEXPLAIN_MAX_REASONS];
    int reason_count;
    
    uint16_t caid;
    uint16_t sid;
    uint32_t peer_id;
    
    time_t timestamp;
    
    char summary[512];
} cwexplain_t;

/* Global state */
typedef struct {
    cwexplain_t recent_explanations[100];
    int explanation_index;
    
    int total_explanations;
    int total_accepts;
    int total_rejects;
    int total_warns;
    
    int enabled;
} cwexplain_state_t;

static cwexplain_state_t g_cwexplain_state = {0};

/* تهيئة */
static inline void cwexplain_init(void) {
    memset(&g_cwexplain_state, 0, sizeof(g_cwexplain_state));
    g_cwexplain_state.enabled = 1;
}

/* إنشاء explanation جديد */
static inline cwexplain_t *cwexplain_create(int decision, double confidence,
                                              uint16_t caid, uint16_t sid, uint32_t peer_id) {
    if (!g_cwexplain_state.enabled) {
        return NULL;
    }
    
    cwexplain_t *explain = &g_cwexplain_state.recent_explanations[g_cwexplain_state.explanation_index];
    memset(explain, 0, sizeof(cwexplain_t));
    
    explain->decision = decision;
    explain->confidence = confidence;
    explain->caid = caid;
    explain->sid = sid;
    explain->peer_id = peer_id;
    explain->timestamp = time(NULL);
    explain->reason_count = 0;
    
    g_cwexplain_state.explanation_index = 
        (g_cwexplain_state.explanation_index + 1) % 100;
    g_cwexplain_state.total_explanations++;
    
    if (decision == CWEXPLAIN_DECISION_ACCEPT) {
        g_cwexplain_state.total_accepts++;
    } else if (decision == CWEXPLAIN_DECISION_REJECT) {
        g_cwexplain_state.total_rejects++;
    } else if (decision == CWEXPLAIN_DECISION_WARN) {
        g_cwexplain_state.total_warns++;
    }
    
    return explain;
}

/* إضافة سبب */
static inline int cwexplain_add_reason(cwexplain_t *explain, const char *description,
                                        double weight, int is_positive) {
    if (!explain || explain->reason_count >= CWEXPLAIN_MAX_REASONS) {
        return -1;
    }
    
    cwexplain_reason_t *reason = &explain->reasons[explain->reason_count++];
    strncpy(reason->description, description, CWEXPLAIN_MAX_DESC - 1);
    reason->description[CWEXPLAIN_MAX_DESC - 1] = '\0';
    reason->weight = weight;
    reason->is_positive = is_positive;
    
    return 0;
}

/* إنشاء ملخص */
static inline void cwexplain_generate_summary(cwexplain_t *explain) {
    if (!explain) return;
    
    char *decision_str;
    if (explain->decision == CWEXPLAIN_DECISION_ACCEPT) {
        decision_str = "ACCEPT";
    } else if (explain->decision == CWEXPLAIN_DECISION_REJECT) {
        decision_str = "REJECT";
    } else {
        decision_str = "WARN";
    }
    
    int offset = snprintf(explain->summary, sizeof(explain->summary),
                          "Decision: %s (confidence: %.2f)\n"
                          "Channel: %04X:%04X, Peer: %u\n"
                          "Reasons (%d):\n",
                          decision_str, explain->confidence,
                          explain->caid, explain->sid, explain->peer_id,
                          explain->reason_count);
    
    int i; for (i = 0; i < explain->reason_count && offset < sizeof(explain->summary) - 100; i++) {
        cwexplain_reason_t *reason = &explain->reasons[i];
        offset += snprintf(explain->summary + offset, sizeof(explain->summary) - offset,
                          "  %d. [%s] %s (weight: %.2f)\n",
                          i + 1,
                          reason->is_positive ? "+" : "-",
                          reason->description,
                          reason->weight);
    }
}

/* الحصول على القرار كنص */
static inline const char *cwexplain_decision_str(int decision) {
    switch (decision) {
        case CWEXPLAIN_DECISION_ACCEPT: return "ACCEPT";
        case CWEXPLAIN_DECISION_REJECT: return "REJECT";
        case CWEXPLAIN_DECISION_WARN: return "WARN";
        default: return "UNKNOWN";
    }
}

/* طباعة explanation */
static inline void cwexplain_print(const cwexplain_t *explain) {
    if (!explain) return;
    
    printf("%s\n", explain->summary);
}

/* الحصول على آخر explanation */
static inline const cwexplain_t *cwexplain_get_last(void) {
    if (g_cwexplain_state.total_explanations == 0) {
        return NULL;
    }
    
    int idx = (g_cwexplain_state.explanation_index - 1 + 100) % 100;
    return &g_cwexplain_state.recent_explanations[idx];
}

/* الحصول على الإحصائيات */
static inline void cwexplain_get_stats(int *total, int *accepts, int *rejects, int *warns) {
    if (total) *total = g_cwexplain_state.total_explanations;
    if (accepts) *accepts = g_cwexplain_state.total_accepts;
    if (rejects) *rejects = g_cwexplain_state.total_rejects;
    if (warns) *warns = g_cwexplain_state.total_warns;
}

/* تصفير الإحصائيات */
static inline void cwexplain_reset_stats(void) {
    g_cwexplain_state.total_explanations = 0;
    g_cwexplain_state.total_accepts = 0;
    g_cwexplain_state.total_rejects = 0;
    g_cwexplain_state.total_warns = 0;
}

/* مثال على الاستخدام */
/*
cwexplain_t *explain = cwexplain_create(CWEXPLAIN_DECISION_REJECT, 0.85, 0x1884, 0x1234, 5);
if (explain) {
    cwexplain_add_reason(explain, "Low entropy (2.1 < 2.5)", -0.3, 0);
    cwexplain_add_reason(explain, "Source score low (0.15)", -0.4, 0);
    cwexplain_add_reason(explain, "Pattern mismatch", -0.15, 0);
    cwexplain_generate_summary(explain);
    cwexplain_print(explain);
}
*/

#endif /* CWEXPLAIN_H */
