/* cwretry.h — CW Retry Strategy (R22 candidate #37)
 *
 * PURPOSE: intelligent retry strategy for CW requests. Instead of
 * blindly retrying the same source, use exponential backoff, source
 * rotation, and priority-based selection.
 *
 * MECHANISM: track retry attempts per (client, channel). Apply:
 *   1. Exponential backoff (1s, 2s, 4s, 8s)
 *   2. Source rotation (try different sources on each retry)
 *   3. Priority boost (failed sources get lower priority)
 *
 * CONFIG: automatic — applies to all CW requests.
 *
 * ROLLBACK: remove cwretry_next() calls; returns immediate retry.
 */
#ifndef MCS_CWRETRY_H
#define MCS_CWRETRY_H

#include <stdint.h>
#include <string.h>

#define CWRETRY_MAX_ATTEMPTS 5
#define CWRETRY_BASE_DELAY_MS 1000
#define CWRETRY_MAX_DELAY_MS 16000
#define CWRETRY_ENTRIES 128

struct cwretry_entry {
    uint32_t client_ip;
    uint16_t caid;
    uint32_t provid;
    uint16_t sid;
    uint32_t attempts;
    uint32_t last_attempt;
    uint32_t next_allowed;
    int last_source;  /* source used in last attempt */
    uint8_t used;
};

struct cwretry_table {
    struct cwretry_entry entries[CWRETRY_ENTRIES];
};

static inline void cwretry_init(struct cwretry_table *t)
{
    if (!t) return;
    memset(t, 0, sizeof(*t));
}

static inline struct cwretry_entry *
cwretry_find(struct cwretry_table *t, uint32_t client_ip,
             uint16_t caid, uint32_t provid, uint16_t sid)
{
    int i;
    if (!t) return 0;
    for (i = 0; i < CWRETRY_ENTRIES; i++) {
        struct cwretry_entry *e = &t->entries[i];
        if (e->used && e->client_ip == client_ip &&
            e->caid == caid && e->provid == provid && e->sid == sid) {
            return e;
        }
    }
    return 0;
}

static inline struct cwretry_entry *
cwretry_alloc(struct cwretry_table *t, uint32_t client_ip,
              uint16_t caid, uint32_t provid, uint16_t sid)
{
    int i, oldest = 0;
    struct cwretry_entry *e;

    e = cwretry_find(t, client_ip, caid, provid, sid);
    if (e) return e;

    for (i = 0; i < CWRETRY_ENTRIES; i++) {
        if (!t->entries[i].used) {
            e = &t->entries[i];
            e->client_ip = client_ip;
            e->caid = caid;
            e->provid = provid;
            e->sid = sid;
            e->used = 1;
            return e;
        }
        if (t->entries[i].last_attempt < t->entries[oldest].last_attempt) {
            oldest = i;
        }
    }
    /* Reuse oldest */
    e = &t->entries[oldest];
    memset(e, 0, sizeof(*e));
    e->client_ip = client_ip;
    e->caid = caid;
    e->provid = provid;
    e->sid = sid;
    e->used = 1;
    return e;
}

/* Compute the next retry delay (exponential backoff) */
static inline uint32_t cwretry_delay_ms(uint32_t attempts)
{
    uint32_t delay;
    if (attempts == 0) return 0;
    delay = CWRETRY_BASE_DELAY_MS * (1U << (attempts - 1));
    if (delay > CWRETRY_MAX_DELAY_MS) delay = CWRETRY_MAX_DELAY_MS;
    return delay;
}

/* Record a retry attempt.
 * Returns: 1 if more retries allowed, 0 if max reached.
 */
static inline int cwretry_record_attempt(struct cwretry_table *t,
                                          uint32_t client_ip,
                                          uint16_t caid, uint32_t provid,
                                          uint16_t sid, int source_id,
                                          uint32_t now)
{
    struct cwretry_entry *e;
    uint32_t delay_ms;

    if (!t) return 0;
    e = cwretry_alloc(t, client_ip, caid, provid, sid);
    if (!e) return 0;

    e->attempts++;
    e->last_attempt = now;
    e->last_source = source_id;

    /* Compute next allowed time */
    delay_ms = cwretry_delay_ms(e->attempts);
    e->next_allowed = now + (delay_ms / 1000);

    return (e->attempts < CWRETRY_MAX_ATTEMPTS) ? 1 : 0;
}

/* Check if a retry is allowed now */
static inline int cwretry_allowed(const struct cwretry_table *t,
                                   uint32_t client_ip,
                                   uint16_t caid, uint32_t provid,
                                   uint16_t sid, uint32_t now)
{
    const struct cwretry_entry *e;
    if (!t) return 1;  /* no history = allowed */
    e = cwretry_find((struct cwretry_table *)t, client_ip, caid, provid, sid);
    if (!e) return 1;
    if (e->attempts >= CWRETRY_MAX_ATTEMPTS) return 0;
    return (now >= e->next_allowed) ? 1 : 0;
}

/* Reset retry counter (on success) */
static inline void cwretry_reset(struct cwretry_table *t,
                                  uint32_t client_ip,
                                  uint16_t caid, uint32_t provid,
                                  uint16_t sid)
{
    struct cwretry_entry *e;
    if (!t) return;
    e = cwretry_find(t, client_ip, caid, provid, sid);
    if (e) {
        e->attempts = 0;
        e->next_allowed = 0;
    }
}

/* Get the last source used (for rotation) */
static inline int cwretry_last_source(const struct cwretry_table *t,
                                       uint32_t client_ip,
                                       uint16_t caid, uint32_t provid,
                                       uint16_t sid)
{
    const struct cwretry_entry *e;
    if (!t) return -1;
    e = cwretry_find((struct cwretry_table *)t, client_ip, caid, provid, sid);
    if (!e) return -1;
    return e->last_source;
}

#endif /* MCS_CWRETRY_H */
