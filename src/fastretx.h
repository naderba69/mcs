/* fastretx.h — Fast Retransmit for ECM Requests (R22 candidate #21)
 *
 * PURPOSE: detect lost ECM requests and retransmit them immediately
 * instead of waiting for the full timeout. This reduces the delay
 * when an ECM packet is lost in the network.
 *
 * MECHANISM: track pending ECM requests. If no response arrives within
 * a short window (e.g., 500ms), retransmit the ECM to the same or
 * alternative server.
 *
 * CONFIG: FAST RETX: ON (default OFF = stock timeout behavior).
 *
 * ROLLBACK: remove fastretx_check() calls; fast retx is inert.
 */
#ifndef MCS_FASTRETX_H
#define MCS_FASTRETX_H

#include <stdint.h>
#include <string.h>

#define FASTRETX_ENTRIES 64
#define FASTRETX_RETX_MS 500  /* retransmit after 500ms */
#define FASTRETX_MAX_RETRIES 2

struct fastretx_entry {
    uint16_t caid;
    uint32_t provid;
    uint16_t sid;
    uint8_t ecm_hash[16];  /* hash of ECM for dedup */
    uint32_t sent_at;
    uint32_t retx_count;
    int server_id;
    uint8_t used;
    uint8_t answered;
};

struct fastretx_table {
    struct fastretx_entry entries[FASTRETX_ENTRIES];
    int enabled;
    uint32_t total_requests;
    uint32_t total_retx;
    uint32_t total_timeouts;
};

static inline void fastretx_init(struct fastretx_table *t, int enabled)
{
    if (!t) return;
    memset(t, 0, sizeof(*t));
    t->enabled = enabled;
}

static inline int fastretx_enabled(const struct fastretx_table *t)
{
    return t && t->enabled;
}

/* Record an ECM request */
static inline void fastretx_record_request(struct fastretx_table *t,
                                            uint16_t caid, uint32_t provid,
                                            uint16_t sid,
                                            const uint8_t ecm_hash[16],
                                            int server_id, uint32_t now)
{
    int i, free_slot = -1;
    struct fastretx_entry *e;

    if (!fastretx_enabled(t) || !ecm_hash) return;

    /* Find free slot or reuse oldest */
    for (i = 0; i < FASTRETX_ENTRIES; i++) {
        if (!t->entries[i].used) {
            free_slot = i;
            break;
        }
        if (t->entries[i].answered) {
            free_slot = i;
            break;
        }
    }
    if (free_slot < 0) {
        /* Reuse oldest unanswered */
        int oldest = 0;
        for (i = 1; i < FASTRETX_ENTRIES; i++) {
            if (t->entries[i].sent_at < t->entries[oldest].sent_at) {
                oldest = i;
            }
        }
        free_slot = oldest;
    }

    e = &t->entries[free_slot];
    e->caid = caid;
    e->provid = provid;
    e->sid = sid;
    memcpy(e->ecm_hash, ecm_hash, 16);
    e->sent_at = now;
    e->retx_count = 0;
    e->server_id = server_id;
    e->used = 1;
    e->answered = 0;
    t->total_requests++;
}

/* Mark an ECM request as answered */
static inline void fastretx_mark_answered(struct fastretx_table *t,
                                           const uint8_t ecm_hash[16])
{
    int i;
    if (!t || !ecm_hash) return;
    for (i = 0; i < FASTRETX_ENTRIES; i++) {
        struct fastretx_entry *e = &t->entries[i];
        if (e->used && !e->answered && memcmp(e->ecm_hash, ecm_hash, 16) == 0) {
            e->answered = 1;
            return;
        }
    }
}

/* Check for pending requests that need retransmission.
 * Returns: number of requests to retransmit.
 * Caller must iterate and retransmit.
 */
static inline int fastretx_check(const struct fastretx_table *t,
                                  uint32_t now,
                                  uint16_t *caids, uint32_t *provids,
                                  uint16_t *sids, uint8_t (*ecm_hashes)[16],
                                  int *server_ids, int max_out)
{
    int i, n = 0;
    if (!fastretx_enabled(t)) return 0;

    for (i = 0; i < FASTRETX_ENTRIES && n < max_out; i++) {
        const struct fastretx_entry *e = &t->entries[i];
        uint32_t age_ms;

        if (!e->used || e->answered) continue;
        if (e->retx_count >= FASTRETX_MAX_RETRIES) {
            ((struct fastretx_table *)t)->total_timeouts++;
            continue;
        }

        age_ms = (now - e->sent_at) * 1000;
        if (age_ms >= FASTRETX_RETX_MS * (e->retx_count + 1)) {
            caids[n] = e->caid;
            provids[n] = e->provid;
            sids[n] = e->sid;
            memcpy(ecm_hashes[n], e->ecm_hash, 16);
            server_ids[n] = e->server_id;
            n++;
        }
    }
    return n;
}

/* Record a retransmission */
static inline void fastretx_record_retx(struct fastretx_table *t,
                                         const uint8_t ecm_hash[16])
{
    int i;
    if (!t || !ecm_hash) return;
    for (i = 0; i < FASTRETX_ENTRIES; i++) {
        struct fastretx_entry *e = &t->entries[i];
        if (e->used && !e->answered && memcmp(e->ecm_hash, ecm_hash, 16) == 0) {
            e->retx_count++;
            t->total_retx++;
            return;
        }
    }
}

#endif /* MCS_FASTRETX_H */
