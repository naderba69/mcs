/* preconn.h — Connection Pre-establish (R22 candidate #26)
 *
 * PURPOSE: pre-establish TCP connections to servers before they are
 * needed. This eliminates the TCP handshake latency when a server is
 * first queried.
 *
 * MECHANISM: maintain a pool of pre-established connections to each
 * server. When a query arrives, use a pre-established connection
 * instead of opening a new one.
 *
 * CONFIG: PRE-CONNECT: N (0 = off, 1-5 = pool size per server).
 *
 * ROLLBACK: remove preconn_acquire() calls; pre-connect is inert.
 */
#ifndef MCS_PRECONN_H
#define MCS_PRECONN_H

#include <stdint.h>
#include <string.h>

#define PRECONN_SERVERS 16
#define PRECONN_POOL_SIZE 5

struct preconn_slot {
    int fd;  /* file descriptor, -1 = not connected */
    uint32_t established_at;
    uint8_t used;
    uint8_t in_use;  /* currently being used by a query */
};

struct preconn_server {
    int server_id;
    uint32_t host_ip;
    uint16_t port;
    struct preconn_slot pool[PRECONN_POOL_SIZE];
    int pool_size;
    uint32_t last_connect;
    uint8_t used;
};

struct preconn_table {
    struct preconn_server servers[PRECONN_SERVERS];
    int enabled;
    uint32_t total_acquired;
    uint32_t total_established;
};

static inline void preconn_init(struct preconn_table *t, int enabled)
{
    int i, j;
    if (!t) return;
    memset(t, 0, sizeof(*t));
    t->enabled = enabled;
    for (i = 0; i < PRECONN_SERVERS; i++) {
        for (j = 0; j < PRECONN_POOL_SIZE; j++) {
            t->servers[i].pool[j].fd = -1;
        }
    }
}

static inline int preconn_enabled(const struct preconn_table *t)
{
    return t && t->enabled;
}

/* Register a server for pre-connection */
static inline void preconn_register(struct preconn_table *t, int server_id,
                                     uint32_t host_ip, uint16_t port,
                                     int pool_size)
{
    int i, free_slot = -1;
    struct preconn_server *s;

    if (!t) return;
    if (pool_size > PRECONN_POOL_SIZE) pool_size = PRECONN_POOL_SIZE;

    for (i = 0; i < PRECONN_SERVERS; i++) {
        if (t->servers[i].used && t->servers[i].server_id == server_id) {
            /* Update existing */
            t->servers[i].host_ip = host_ip;
            t->servers[i].port = port;
            t->servers[i].pool_size = pool_size;
            return;
        }
        if (!t->servers[i].used && free_slot < 0) {
            free_slot = i;
        }
    }

    if (free_slot < 0) return;  /* table full */

    s = &t->servers[free_slot];
    s->server_id = server_id;
    s->host_ip = host_ip;
    s->port = port;
    s->pool_size = pool_size;
    s->used = 1;
}

/* Acquire a pre-established connection.
 * Returns: fd (>= 0) if available, -1 if no connection available.
 */
static inline int preconn_acquire(struct preconn_table *t, int server_id,
                                   uint32_t now)
{
    int i;
    struct preconn_server *s = 0;
    struct preconn_slot *best = 0;

    if (!preconn_enabled(t)) return -1;

    for (i = 0; i < PRECONN_SERVERS; i++) {
        if (t->servers[i].used && t->servers[i].server_id == server_id) {
            s = &t->servers[i];
            break;
        }
    }
    if (!s) return -1;

    /* Find an available connection */
    for (i = 0; i < s->pool_size; i++) {
        struct preconn_slot *slot = &s->pool[i];
        if (slot->fd >= 0 && !slot->in_use) {
            if (!best || slot->established_at < best->established_at) {
                best = slot;  /* prefer oldest connection (most likely ready) */
            }
        }
    }

    if (!best) return -1;

    best->in_use = 1;
    t->total_acquired++;
    return best->fd;
}

/* Release a connection back to the pool */
static inline void preconn_release(struct preconn_table *t, int server_id,
                                    int fd)
{
    int i, j;
    if (!t) return;

    for (i = 0; i < PRECONN_SERVERS; i++) {
        struct preconn_server *s = &t->servers[i];
        if (!s->used || s->server_id != server_id) continue;
        for (j = 0; j < s->pool_size; j++) {
            if (s->pool[j].fd == fd) {
                s->pool[j].in_use = 0;
                return;
            }
        }
    }
}

/* Record a new connection established */
static inline void preconn_established(struct preconn_table *t, int server_id,
                                        int fd, uint32_t now)
{
    int i, j;
    struct preconn_server *s = 0;
    int free_slot = -1;

    if (!t) return;

    for (i = 0; i < PRECONN_SERVERS; i++) {
        if (t->servers[i].used && t->servers[i].server_id == server_id) {
            s = &t->servers[i];
            break;
        }
    }
    if (!s) return;

    for (j = 0; j < s->pool_size; j++) {
        if (s->pool[j].fd < 0) {
            free_slot = j;
            break;
        }
    }
    if (free_slot < 0) return;  /* pool full */

    s->pool[free_slot].fd = fd;
    s->pool[free_slot].established_at = now;
    s->pool[free_slot].used = 1;
    s->pool[free_slot].in_use = 0;
    s->last_connect = now;
    t->total_established++;
}

#endif /* MCS_PRECONN_H */
