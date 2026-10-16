/* predictzap.h — Predictive Zapping (R22 candidate #18)
 *
 * PURPOSE: predict which channel the user will zap to next and
 * pre-fetch its CW. This reduces zap time to near-zero for predicted
 * channels.
 *
 * MECHANISM: track zap history and patterns. Use simple heuristics:
 *   - Users often zap to adjacent channels (SID±1, ±2)
 *   - Users often zap back to recently watched channels
 *   - Users often zap within the same CAID/PROVID group
 *
 * CONFIG: PREDICT ZAP: N (0 = off, 1-5 = prediction depth).
 *
 * ROLLBACK: remove predictzap_suggest() calls; prediction is inert.
 */
#ifndef MCS_PREDICTZAP_H
#define MCS_PREDICTZAP_H

#include <stdint.h>
#include <string.h>

#define PREDICTZAP_HISTORY 20
#define PREDICTZAP_SUGGESTIONS 8

struct predictzap_entry {
    uint16_t caid;
    uint32_t provid;
    uint16_t sid;
    uint32_t last_watched;
    uint32_t watch_count;
    uint8_t used;
};

struct predictzap_table {
    struct predictzap_entry history[PREDICTZAP_HISTORY];
    int history_head;
    int history_count;
    uint16_t current_caid;
    uint32_t current_provid;
    uint16_t current_sid;
    int depth;
    uint32_t total_predictions;
    uint32_t correct_predictions;
};

static inline void predictzap_init(struct predictzap_table *t, int depth)
{
    if (!t) return;
    memset(t, 0, sizeof(*t));
    if (depth < 0) depth = 0;
    if (depth > 5) depth = 5;
    t->depth = depth;
}

static inline int predictzap_enabled(const struct predictzap_table *t)
{
    return t && t->depth > 0;
}

/* Record a zap event (user switched to a new channel) */
static inline void predictzap_record_zap(struct predictzap_table *t,
                                          uint16_t caid, uint32_t provid,
                                          uint16_t sid, uint32_t now)
{
    struct predictzap_entry *e;
    int i;

    if (!t) return;

    /* Update current channel */
    t->current_caid = caid;
    t->current_provid = provid;
    t->current_sid = sid;

    /* Check if already in history */
    for (i = 0; i < t->history_count; i++) {
        e = &t->history[i];
        if (e->caid == caid && e->provid == provid && e->sid == sid) {
            e->last_watched = now;
            e->watch_count++;
            return;
        }
    }

    /* Add to history */
    e = &t->history[t->history_head];
    e->caid = caid;
    e->provid = provid;
    e->sid = sid;
    e->last_watched = now;
    e->watch_count = 1;
    e->used = 1;
    t->history_head = (t->history_head + 1) % PREDICTZAP_HISTORY;
    if (t->history_count < PREDICTZAP_HISTORY) t->history_count++;
}

/* Suggest channels to pre-fetch.
 * Returns: number of suggestions.
 */
static inline int predictzap_suggest(const struct predictzap_table *t,
                                      uint16_t *caids, uint32_t *provids,
                                      uint16_t *sids, int max_out)
{
    int n = 0;
    int i, j;
    struct predictzap_entry sorted[PREDICTZAP_HISTORY];
    int count;

    if (!predictzap_enabled(t)) return 0;

    /* 1. Adjacent channels (SID±1, ±2, ...) */
    for (i = 1; i <= t->depth && n < max_out; i++) {
        if (t->current_sid > (uint16_t)i) {
            caids[n] = t->current_caid;
            provids[n] = t->current_provid;
            sids[n] = t->current_sid - i;
            n++;
        }
        if (n < max_out) {
            caids[n] = t->current_caid;
            provids[n] = t->current_provid;
            sids[n] = t->current_sid + i;
            n++;
        }
    }

    /* 2. Recently watched channels (sorted by watch_count) */
    count = t->history_count;
    for (i = 0; i < count; i++) {
        sorted[i] = t->history[i];
    }
    /* Sort by watch_count descending */
    for (i = 0; i < count - 1; i++) {
        for (j = i + 1; j < count; j++) {
            if (sorted[j].watch_count > sorted[i].watch_count) {
                struct predictzap_entry tmp = sorted[i];
                sorted[i] = sorted[j];
                sorted[j] = tmp;
            }
        }
    }
    /* Add top-N recently watched (if not already adjacent) */
    for (i = 0; i < count && n < max_out; i++) {
        int already = 0;
        /* Skip if it's the current channel */
        if (sorted[i].caid == t->current_caid &&
            sorted[i].provid == t->current_provid &&
            sorted[i].sid == t->current_sid) {
            continue;
        }
        /* Skip if already suggested */
        for (j = 0; j < n; j++) {
            if (caids[j] == sorted[i].caid &&
                provids[j] == sorted[i].provid &&
                sids[j] == sorted[i].sid) {
                already = 1;
                break;
            }
        }
        if (!already) {
            caids[n] = sorted[i].caid;
            provids[n] = sorted[i].provid;
            sids[n] = sorted[i].sid;
            n++;
        }
    }

    ((struct predictzap_table *)t)->total_predictions += (uint32_t)n;
    return n;
}

/* Record that a prediction was correct (user zapped to a predicted channel) */
static inline void predictzap_correct(struct predictzap_table *t)
{
    if (!t) return;
    t->correct_predictions++;
}

/* Get prediction accuracy (0.0 to 1.0) */
static inline double predictzap_accuracy(const struct predictzap_table *t)
{
    if (!t || t->total_predictions == 0) return 0.0;
    return (double)t->correct_predictions / (double)t->total_predictions;
}

#endif /* MCS_PREDICTZAP_H */
