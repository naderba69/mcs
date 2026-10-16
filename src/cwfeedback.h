/* cwfeedback.h — Client Feedback Loop (R22 candidate #36)
 *
 * PURPOSE: learn from client behavior to improve CW selection. When
 * a client sends a new ECM for the same channel shortly after
 * receiving a CW, it's a strong signal that the CW was wrong (black
 * screen). Use this feedback to penalize sources and CWs.
 *
 * MECHANISM: track (client, channel, cw) tuples. If a client sends a
 * new ECM within N seconds of receiving a CW, record it as a failure
 * for that CW and its source.
 *
 * CONFIG: automatic — hooks into ECM request path.
 *
 * ROLLBACK: remove cwfeedback_record_delivery() and
 * cwfeedback_on_ecm() calls; feedback is inert.
 */
#ifndef MCS_CWFEEDBACK_H
#define MCS_CWFEEDBACK_H

#include <stdint.h>
#include <string.h>

#define CWFEEDBACK_TABLE_SIZE 256
#define CWFEEDBACK_WINDOW_SECS 5  /* if new ECM within 5s, CW was wrong */

struct cwfeedback_entry {
    uint32_t client_ip;
    uint16_t caid;
    uint32_t provid;
    uint16_t sid;
    uint8_t cw_prefix[8];
    int source_id;         /* peer or server that provided the CW */
    uint32_t delivered_at;
    uint8_t used;
    uint8_t failed;        /* 1 = client sent new ECM (CW was wrong) */
};

struct cwfeedback_table {
    struct cwfeedback_entry entries[CWFEEDBACK_TABLE_SIZE];
    uint32_t total_deliveries;
    uint32_t total_failures;
};

static inline void cwfeedback_init(struct cwfeedback_table *t)
{
    if (!t) return;
    memset(t, 0, sizeof(*t));
}

/* Record a CW delivery to a client */
static inline void cwfeedback_record_delivery(struct cwfeedback_table *t,
                                               uint32_t client_ip,
                                               uint16_t caid,
                                               uint32_t provid,
                                               uint16_t sid,
                                               const uint8_t cw[16],
                                               int source_id,
                                               uint32_t now)
{
    int i, free_slot = -1;
    struct cwfeedback_entry *e;

    if (!t || !cw) return;

    /* Find free slot or reuse oldest */
    for (i = 0; i < CWFEEDBACK_TABLE_SIZE; i++) {
        if (!t->entries[i].used) {
            free_slot = i;
            break;
        }
    }
    if (free_slot < 0) {
        /* Reuse oldest */
        int oldest = 0;
        for (i = 1; i < CWFEEDBACK_TABLE_SIZE; i++) {
            if (t->entries[i].delivered_at < t->entries[oldest].delivered_at) {
                oldest = i;
            }
        }
        free_slot = oldest;
    }

    e = &t->entries[free_slot];
    e->client_ip = client_ip;
    e->caid = caid;
    e->provid = provid;
    e->sid = sid;
    memcpy(e->cw_prefix, cw, 8);
    e->source_id = source_id;
    e->delivered_at = now;
    e->used = 1;
    e->failed = 0;
    t->total_deliveries++;
}

/* Client sent a new ECM — check if this is feedback on a recent delivery.
 * Returns: source_id of the failed CW (for penalty), or -1 if no match.
 */
static inline int cwfeedback_on_ecm(struct cwfeedback_table *t,
                                     uint32_t client_ip,
                                     uint16_t caid, uint32_t provid,
                                     uint16_t sid, uint32_t now)
{
    int i;
    int failed_source = -1;

    if (!t) return -1;

    for (i = 0; i < CWFEEDBACK_TABLE_SIZE; i++) {
        struct cwfeedback_entry *e = &t->entries[i];
        if (!e->used || e->failed) continue;
        if (e->client_ip != client_ip) continue;
        if (e->caid != caid || e->provid != provid || e->sid != sid) continue;
        /* Check if within feedback window */
        if ((now - e->delivered_at) <= CWFEEDBACK_WINDOW_SECS) {
            e->failed = 1;
            failed_source = e->source_id;
            t->total_failures++;
            /* Don't break — mark all recent deliveries as failed */
        }
    }
    return failed_source;
}

/* Get failure rate for a source (0.0 to 1.0) */
static inline double cwfeedback_source_failure_rate(const struct cwfeedback_table *t,
                                                     int source_id)
{
    int i;
    int total = 0, failures = 0;

    if (!t) return 0.0;

    for (i = 0; i < CWFEEDBACK_TABLE_SIZE; i++) {
        const struct cwfeedback_entry *e = &t->entries[i];
        if (!e->used) continue;
        if (e->source_id != source_id) continue;
        total++;
        if (e->failed) failures++;
    }

    if (total == 0) return 0.0;
    return (double)failures / (double)total;
}

/* Expire old entries */
static inline void cwfeedback_expire(struct cwfeedback_table *t,
                                      uint32_t now, uint32_t max_age)
{
    int i;
    if (!t) return;
    for (i = 0; i < CWFEEDBACK_TABLE_SIZE; i++) {
        struct cwfeedback_entry *e = &t->entries[i];
        if (e->used && (now - e->delivered_at) > max_age) {
            e->used = 0;
        }
    }
}

#endif /* MCS_CWFEEDBACK_H */
