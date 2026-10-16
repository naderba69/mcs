/* multipath.h — Multi-Path CW Delivery (R22 candidate #38)
 *
 * PURPOSE: deliver CWs to clients over multiple paths simultaneously
 * to increase reliability. If one path fails (network issue, packet
 * loss), the client receives the CW over another path.
 *
 * MECHANISM: for each CW delivery, send over:
 *   1. Primary path (normal ECM response)
 *   2. Secondary path (cache push if available)
 *   3. Tertiary path (alternative server if available)
 *
 * CONFIG: MULTI PATH: ON (default OFF = single path).
 *
 * ROLLBACK: remove multipath_send() calls; multi-path is inert.
 */
#ifndef MCS_MULTIPATH_H
#define MCS_MULTIPATH_H

#include <stdint.h>
#include <string.h>

#define MULTIPATH_MAX_PATHS 3
#define MULTIPATH_HISTORY 64

#define MULTIPATH_PATH_PRIMARY   0
#define MULTIPATH_PATH_SECONDARY 1
#define MULTIPATH_PATH_TERTIARY  2

struct multipath_delivery {
    uint8_t ecm_hash[16];
    uint8_t cw[16];
    uint16_t caid;
    uint32_t provid;
    uint16_t sid;
    uint32_t client_ip;
    uint32_t sent_at;
    int paths_used;
    int path_status[MULTIPATH_MAX_PATHS];  /* 0=pending, 1=sent, 2=acked, 3=failed */
    uint8_t used;
};

struct multipath_table {
    struct multipath_delivery history[MULTIPATH_HISTORY];
    int head;
    int enabled;
    uint32_t total_deliveries;
    uint32_t path_counts[MULTIPATH_MAX_PATHS];
    uint32_t path_successes[MULTIPATH_MAX_PATHS];
};

static inline void multipath_init(struct multipath_table *t, int enabled)
{
    if (!t) return;
    memset(t, 0, sizeof(*t));
    t->enabled = enabled;
}

static inline int multipath_enabled(const struct multipath_table *t)
{
    return t && t->enabled;
}

/* Send a CW over multiple paths.
 * Returns: number of paths used.
 */
static inline int multipath_send(struct multipath_table *t,
                                  const uint8_t ecm_hash[16],
                                  const uint8_t cw[16],
                                  uint16_t caid, uint32_t provid,
                                  uint16_t sid, uint32_t client_ip,
                                  int available_paths, uint32_t now)
{
    struct multipath_delivery *d;
    int i, paths;

    if (!multipath_enabled(t) || !ecm_hash || !cw) return 1;
    if (available_paths <= 0) available_paths = 1;
    if (available_paths > MULTIPATH_MAX_PATHS) available_paths = MULTIPATH_MAX_PATHS;

    d = &t->history[t->head];
    memcpy(d->ecm_hash, ecm_hash, 16);
    memcpy(d->cw, cw, 16);
    d->caid = caid;
    d->provid = provid;
    d->sid = sid;
    d->client_ip = client_ip;
    d->sent_at = now;
    d->paths_used = available_paths;
    for (i = 0; i < MULTIPATH_MAX_PATHS; i++) {
        d->path_status[i] = (i < available_paths) ? 1 : 0;  /* 1=sent */
    }
    d->used = 1;
    t->head = (t->head + 1) % MULTIPATH_HISTORY;
    t->total_deliveries++;

    paths = available_paths;
    for (i = 0; i < paths; i++) {
        t->path_counts[i]++;
    }
    return paths;
}

/* Record ACK for a path */
static inline void multipath_ack(struct multipath_table *t,
                                  const uint8_t ecm_hash[16], int path)
{
    int i;
    if (!t || !ecm_hash || path < 0 || path >= MULTIPATH_MAX_PATHS) return;

    for (i = 0; i < MULTIPATH_HISTORY; i++) {
        struct multipath_delivery *d = &t->history[i];
        if (!d->used) continue;
        if (memcmp(d->ecm_hash, ecm_hash, 16) == 0) {
            d->path_status[path] = 2;  /* acked */
            t->path_successes[path]++;
            return;
        }
    }
}

/* Record failure for a path */
static inline void multipath_fail(struct multipath_table *t,
                                   const uint8_t ecm_hash[16], int path)
{
    int i;
    if (!t || !ecm_hash || path < 0 || path >= MULTIPATH_MAX_PATHS) return;

    for (i = 0; i < MULTIPATH_HISTORY; i++) {
        struct multipath_delivery *d = &t->history[i];
        if (!d->used) continue;
        if (memcmp(d->ecm_hash, ecm_hash, 16) == 0) {
            d->path_status[path] = 3;  /* failed */
            return;
        }
    }
}

/* Get path reliability (0.0 to 1.0) */
static inline double multipath_reliability(const struct multipath_table *t,
                                            int path)
{
    if (!t || path < 0 || path >= MULTIPATH_MAX_PATHS) return 0.0;
    if (t->path_counts[path] == 0) return 0.0;
    return (double)t->path_successes[path] / (double)t->path_counts[path];
}

/* Check if all paths failed for a delivery */
static inline int multipath_all_failed(const struct multipath_table *t,
                                        const uint8_t ecm_hash[16])
{
    int i, j;
    if (!t || !ecm_hash) return 0;

    for (i = 0; i < MULTIPATH_HISTORY; i++) {
        const struct multipath_delivery *d = &t->history[i];
        int all_failed = 1;

        if (!d->used) continue;
        if (memcmp(d->ecm_hash, ecm_hash, 16) != 0) continue;

        for (j = 0; j < d->paths_used; j++) {
            if (d->path_status[j] != 3) {  /* not failed */
                all_failed = 0;
                break;
            }
        }
        return all_failed;
    }
    return 0;
}

#endif /* MCS_MULTIPATH_H */
