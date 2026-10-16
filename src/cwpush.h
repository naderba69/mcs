/* cwpush.h — CW Push Proactive (R22 candidate #22)
 *
 * PURPOSE: proactively push CWs to clients without waiting for a
 * request. When a CW is detected as fake (by cwverify, blackscreen,
 * or cwlate), immediately push a replacement CW to all clients
 * currently watching that channel.
 *
 * MECHANISM: hook into CW fake detection. When a fake is detected,
 * call cwpush_trigger() to push the best alternative CW to all
 * affected clients.
 *
 * CONFIG: CW PUSH: ON (default OFF = stock request-response).
 *
 * ROLLBACK: remove cwpush_trigger() calls; push is inert.
 */
#ifndef MCS_CWPUSH_H
#define MCS_CWPUSH_H

#include <stdint.h>
#include <string.h>

#define CWPUSH_MAX_CLIENTS 16
#define CWPUSH_HISTORY 32

struct cwpush_client {
    uint32_t client_ip;
    uint16_t caid;
    uint32_t provid;
    uint16_t sid;
    uint32_t watching_since;
    uint8_t used;
};

struct cwpush_event {
    uint16_t caid;
    uint32_t provid;
    uint16_t sid;
    uint8_t cw[16];
    uint32_t pushed_at;
    int client_count;
    uint8_t used;
};

struct cwpush_table {
    struct cwpush_client clients[CWPUSH_MAX_CLIENTS];
    struct cwpush_event history[CWPUSH_HISTORY];
    int history_head;
    int enabled;
    uint32_t total_pushes;
    uint32_t total_clients_pushed;
};

static inline void cwpush_init(struct cwpush_table *t, int enabled)
{
    if (!t) return;
    memset(t, 0, sizeof(*t));
    t->enabled = enabled;
}

static inline int cwpush_enabled(const struct cwpush_table *t)
{
    return t && t->enabled;
}

/* Register a client as watching a channel */
static inline void cwpush_register_client(struct cwpush_table *t,
                                           uint32_t client_ip,
                                           uint16_t caid, uint32_t provid,
                                           uint16_t sid, uint32_t now)
{
    int i, free_slot = -1;
    struct cwpush_client *c;

    if (!t) return;

    /* Check if already registered */
    for (i = 0; i < CWPUSH_MAX_CLIENTS; i++) {
        c = &t->clients[i];
        if (c->used && c->client_ip == client_ip &&
            c->caid == caid && c->provid == provid && c->sid == sid) {
            c->watching_since = now;
            return;
        }
        if (!c->used && free_slot < 0) {
            free_slot = i;
        }
    }

    /* Allocate new slot */
    if (free_slot < 0) {
        /* Replace oldest */
        int oldest = 0;
        for (i = 1; i < CWPUSH_MAX_CLIENTS; i++) {
            if (t->clients[i].watching_since < t->clients[oldest].watching_since) {
                oldest = i;
            }
        }
        free_slot = oldest;
    }

    c = &t->clients[free_slot];
    c->client_ip = client_ip;
    c->caid = caid;
    c->provid = provid;
    c->sid = sid;
    c->watching_since = now;
    c->used = 1;
}

/* Unregister a client (client zapped away) */
static inline void cwpush_unregister_client(struct cwpush_table *t,
                                             uint32_t client_ip,
                                             uint16_t caid, uint32_t provid,
                                             uint16_t sid)
{
    int i;
    if (!t) return;
    for (i = 0; i < CWPUSH_MAX_CLIENTS; i++) {
        struct cwpush_client *c = &t->clients[i];
        if (c->used && c->client_ip == client_ip &&
            c->caid == caid && c->provid == provid && c->sid == sid) {
            c->used = 0;
            return;
        }
    }
}

/* Trigger a push to all clients watching a channel.
 * Returns: number of clients that should receive the push.
 * Caller must iterate and send CWs.
 */
static inline int cwpush_trigger(struct cwpush_table *t,
                                  uint16_t caid, uint32_t provid,
                                  uint16_t sid, const uint8_t cw[16],
                                  uint32_t now,
                                  uint32_t *client_ips, int max_out)
{
    int i, n = 0;
    struct cwpush_event *ev;

    if (!cwpush_enabled(t) || !cw) return 0;

    /* Collect clients watching this channel */
    for (i = 0; i < CWPUSH_MAX_CLIENTS && n < max_out; i++) {
        struct cwpush_client *c = &t->clients[i];
        if (c->used && c->caid == caid && c->provid == provid && c->sid == sid) {
            client_ips[n++] = c->client_ip;
        }
    }

    if (n == 0) return 0;

    /* Record the push event */
    ev = &t->history[t->history_head];
    ev->caid = caid;
    ev->provid = provid;
    ev->sid = sid;
    memcpy(ev->cw, cw, 16);
    ev->pushed_at = now;
    ev->client_count = n;
    ev->used = 1;
    t->history_head = (t->history_head + 1) % CWPUSH_HISTORY;

    t->total_pushes++;
    t->total_clients_pushed += (uint32_t)n;
    return n;
}

#endif /* MCS_CWPUSH_H */
