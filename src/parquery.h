/* parquery.h — Parallel Server Queries (R22 candidate #20)
 *
 * PURPOSE: query multiple servers in parallel for the same ECM and
 * use the first response that arrives. This reduces latency by
 * exploiting server diversity.
 *
 * MECHANISM: when an ECM arrives from a client, send it to all
 * available servers simultaneously. The first valid response wins;
 * cancel the others.
 *
 * CONFIG: PARALLEL QUERY: ON (default OFF = stock sequential).
 *
 * ROLLBACK: remove parquery_dispatch() calls; parallel query is inert.
 */
#ifndef MCS_PARQUERY_H
#define MCS_PARQUERY_H

#include <stdint.h>
#include <string.h>

#define PARQUERY_MAX_SERVERS 8
#define PARQUERY_ENTRIES 64

struct parquery_entry {
    uint8_t ecm_hash[16];
    uint16_t caid;
    uint32_t provid;
    uint16_t sid;
    uint32_t dispatched_at;
    int servers_queried;
    int server_ids[PARQUERY_MAX_SERVERS];
    int responded;  /* number of servers that responded */
    int winner;     /* server_id of the first responder */
    uint8_t used;
};

struct parquery_table {
    struct parquery_entry entries[PARQUERY_ENTRIES];
    int enabled;
    uint32_t total_dispatches;
    uint32_t total_wins;
};

static inline void parquery_init(struct parquery_table *t, int enabled)
{
    if (!t) return;
    memset(t, 0, sizeof(*t));
    t->enabled = enabled;
}

static inline int parquery_enabled(const struct parquery_table *t)
{
    return t && t->enabled;
}

/* Dispatch an ECM to multiple servers.
 * Returns: number of servers queried.
 */
static inline int parquery_dispatch(struct parquery_table *t,
                                     const uint8_t ecm_hash[16],
                                     uint16_t caid, uint32_t provid,
                                     uint16_t sid,
                                     const int *server_ids, int server_count,
                                     uint32_t now)
{
    int i, free_slot = -1;
    struct parquery_entry *e;
    int count;

    if (!parquery_enabled(t) || !ecm_hash) return 0;
    if (server_count <= 0) return 0;
    if (server_count > PARQUERY_MAX_SERVERS) server_count = PARQUERY_MAX_SERVERS;

    /* Find free slot */
    for (i = 0; i < PARQUERY_ENTRIES; i++) {
        if (!t->entries[i].used) {
            free_slot = i;
            break;
        }
    }
    if (free_slot < 0) {
        /* Reuse oldest */
        int oldest = 0;
        for (i = 1; i < PARQUERY_ENTRIES; i++) {
            if (t->entries[i].dispatched_at < t->entries[oldest].dispatched_at) {
                oldest = i;
            }
        }
        free_slot = oldest;
    }

    e = &t->entries[free_slot];
    memcpy(e->ecm_hash, ecm_hash, 16);
    e->caid = caid;
    e->provid = provid;
    e->sid = sid;
    e->dispatched_at = now;
    e->servers_queried = server_count;
    count = server_count;
    for (i = 0; i < server_count; i++) {
        e->server_ids[i] = server_ids[i];
    }
    e->responded = 0;
    e->winner = -1;
    e->used = 1;
    t->total_dispatches++;
    return count;
}

/* Record a response from a server.
 * Returns: 1 if this is the first response (winner), 0 otherwise.
 */
static inline int parquery_response(struct parquery_table *t,
                                     const uint8_t ecm_hash[16],
                                     int server_id)
{
    int i;
    struct parquery_entry *e;

    if (!t || !ecm_hash) return 0;

    for (i = 0; i < PARQUERY_ENTRIES; i++) {
        e = &t->entries[i];
        if (!e->used) continue;
        if (memcmp(e->ecm_hash, ecm_hash, 16) != 0) continue;

        e->responded++;
        if (e->winner < 0) {
            e->winner = server_id;
            t->total_wins++;
            return 1;  /* first response wins */
        }
        return 0;  /* not the first */
    }
    return 0;
}

/* Check if an ECM has been dispatched (for dedup) */
static inline int parquery_is_pending(const struct parquery_table *t,
                                       const uint8_t ecm_hash[16])
{
    int i;
    if (!t || !ecm_hash) return 0;
    for (i = 0; i < PARQUERY_ENTRIES; i++) {
        if (t->entries[i].used &&
            memcmp(t->entries[i].ecm_hash, ecm_hash, 16) == 0 &&
            t->entries[i].winner < 0) {
            return 1;
        }
    }
    return 0;
}

/* Expire old entries */
static inline void parquery_expire(struct parquery_table *t,
                                    uint32_t now, uint32_t max_age)
{
    int i;
    if (!t) return;
    for (i = 0; i < PARQUERY_ENTRIES; i++) {
        if (t->entries[i].used && (now - t->entries[i].dispatched_at) > max_age) {
            t->entries[i].used = 0;
        }
    }
}

#endif /* MCS_PARQUERY_H */
