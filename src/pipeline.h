/* pipeline.h — CW Pipeline (R22 candidate #27)
 *
 * PURPOSE: pipeline the ECM→CW→delivery process so that multiple
 * ECMs can be processed concurrently. This increases throughput and
 * reduces latency under load.
 *
 * MECHANISM: divide the CW processing into stages:
 *   1. ECM receive
 *   2. Server query
 *   3. CW validation
 *   4. CW delivery
 * Each stage can process a different ECM concurrently.
 *
 * CONFIG: automatic — pipeline is always active (no overhead when
 * not under load).
 *
 * ROLLBACK: remove pipeline_enqueue() calls; pipeline is inert.
 */
#ifndef MCS_PIPELINE_H
#define MCS_PIPELINE_H

#include <stdint.h>
#include <string.h>

#define PIPELINE_STAGES 4
#define PIPELINE_QUEUE_SIZE 32

#define PIPELINE_STAGE_RECEIVE    0
#define PIPELINE_STAGE_QUERY      1
#define PIPELINE_STAGE_VALIDATE   2
#define PIPELINE_STAGE_DELIVER    3

struct pipeline_entry {
    uint8_t ecm_hash[16];
    uint16_t caid;
    uint32_t provid;
    uint16_t sid;
    uint32_t client_ip;
    uint8_t cw[16];
    int source_id;
    int stage;
    uint32_t entered_at;
    uint32_t stage_times[PIPELINE_STAGES];
    uint8_t used;
    uint8_t completed;
};

struct pipeline_table {
    struct pipeline_entry queue[PIPELINE_QUEUE_SIZE];
    int head, tail;
    uint32_t total_processed;
    uint32_t total_completed;
    uint32_t stage_counts[PIPELINE_STAGES];
};

static inline void pipeline_init(struct pipeline_table *t)
{
    if (!t) return;
    memset(t, 0, sizeof(*t));
}

/* Enqueue an ECM for pipeline processing */
static inline int pipeline_enqueue(struct pipeline_table *t,
                                   const uint8_t ecm_hash[16],
                                   uint16_t caid, uint32_t provid,
                                   uint16_t sid, uint32_t client_ip,
                                   uint32_t now)
{
    struct pipeline_entry *e;
    int next;

    if (!t || !ecm_hash) return 0;

    next = (t->head + 1) % PIPELINE_QUEUE_SIZE;
    if (next == t->tail) {
        /* Queue full — drop oldest */
        t->tail = (t->tail + 1) % PIPELINE_QUEUE_SIZE;
    }

    e = &t->queue[t->head];
    memset(e, 0, sizeof(*e));
    memcpy(e->ecm_hash, ecm_hash, 16);
    e->caid = caid;
    e->provid = provid;
    e->sid = sid;
    e->client_ip = client_ip;
    e->stage = PIPELINE_STAGE_RECEIVE;
    e->entered_at = now;
    e->stage_times[PIPELINE_STAGE_RECEIVE] = now;
    e->used = 1;
    e->completed = 0;
    t->head = next;
    t->total_processed++;
    t->stage_counts[PIPELINE_STAGE_RECEIVE]++;
    return 1;
}

/* Advance an entry to the next stage */
static inline int pipeline_advance(struct pipeline_table *t,
                                    const uint8_t ecm_hash[16],
                                    uint32_t now)
{
    int i;
    if (!t || !ecm_hash) return 0;

    for (i = t->tail; i != t->head; i = (i + 1) % PIPELINE_QUEUE_SIZE) {
        struct pipeline_entry *e = &t->queue[i];
        if (!e->used || e->completed) continue;
        if (memcmp(e->ecm_hash, ecm_hash, 16) != 0) continue;

        if (e->stage < PIPELINE_STAGES - 1) {
            e->stage++;
            e->stage_times[e->stage] = now;
            t->stage_counts[e->stage]++;
            return e->stage;
        } else {
            /* Completed all stages */
            e->completed = 1;
            t->total_completed++;
            return -1;
        }
    }
    return -2;  /* not found */
}

/* Set the CW for an entry (after server query) */
static inline void pipeline_set_cw(struct pipeline_table *t,
                                    const uint8_t ecm_hash[16],
                                    const uint8_t cw[16], int source_id)
{
    int i;
    if (!t || !ecm_hash || !cw) return;

    for (i = t->tail; i != t->head; i = (i + 1) % PIPELINE_QUEUE_SIZE) {
        struct pipeline_entry *e = &t->queue[i];
        if (!e->used || e->completed) continue;
        if (memcmp(e->ecm_hash, ecm_hash, 16) == 0) {
            memcpy(e->cw, cw, 16);
            e->source_id = source_id;
            return;
        }
    }
}

/* Get entries at a specific stage (for batch processing) */
static inline int pipeline_get_stage(const struct pipeline_table *t,
                                      int stage,
                                      uint8_t (*ecm_hashes)[16],
                                      int max_out)
{
    int i, n = 0;
    if (!t) return 0;

    for (i = t->tail; i != t->head && n < max_out;
         i = (i + 1) % PIPELINE_QUEUE_SIZE) {
        const struct pipeline_entry *e = &t->queue[i];
        if (!e->used || e->completed) continue;
        if (e->stage == stage) {
            memcpy(ecm_hashes[n], e->ecm_hash, 16);
            n++;
        }
    }
    return n;
}

/* Expire old entries */
static inline void pipeline_expire(struct pipeline_table *t,
                                    uint32_t now, uint32_t max_age)
{
    int i;
    if (!t) return;
    for (i = 0; i < PIPELINE_QUEUE_SIZE; i++) {
        struct pipeline_entry *e = &t->queue[i];
        if (e->used && (now - e->entered_at) > max_age) {
            e->used = 0;
        }
    }
}

#endif /* MCS_PIPELINE_H */
