/* clientbuf.h — Client CW Buffer (R22 candidate #29)
 *
 * PURPOSE: buffer CWs at the client side to handle temporary network
 * issues. When the client receives a CW, it stores it locally. If
 * the next CW is delayed (network loss, server delay), the client
 * can continue using the buffered CW until a new one arrives.
 *
 * MECHANISM: maintain a small buffer (2-3 CWs) per channel at the
 * client. When a new CW arrives, add it to the buffer. When the
 * active CW expires, use the next one from the buffer.
 *
 * CONFIG: CLIENT BUFFER: N (0 = off, 1-5 = buffer size).
 *
 * ROLLBACK: remove clientbuf_get() calls; client buffer is inert.
 */
#ifndef MCS_CLIENTBUF_H
#define MCS_CLIENTBUF_H

#include <stdint.h>
#include <string.h>

#define CLIENTBUF_MAX_SIZE 5
#define CLIENTBUF_CHANNELS 32

struct clientbuf_cw {
    uint8_t cw[16];
    uint32_t received_at;
    uint32_t expires_at;
    uint8_t used;
};

struct clientbuf_channel {
    uint16_t caid;
    uint32_t provid;
    uint16_t sid;
    struct clientbuf_cw buffer[CLIENTBUF_MAX_SIZE];
    int head;
    int buffer_size;
    uint8_t used;
};

struct clientbuf_table {
    struct clientbuf_channel channels[CLIENTBUF_CHANNELS];
    int max_size;
    uint32_t total_buffered;
    uint32_t total_used;
};

static inline void clientbuf_init(struct clientbuf_table *t, int max_size)
{
    if (!t) return;
    memset(t, 0, sizeof(*t));
    if (max_size < 0) max_size = 0;
    if (max_size > CLIENTBUF_MAX_SIZE) max_size = CLIENTBUF_MAX_SIZE;
    t->max_size = max_size;
}

static inline int clientbuf_enabled(const struct clientbuf_table *t)
{
    return t && t->max_size > 0;
}

static inline struct clientbuf_channel *
clientbuf_find(struct clientbuf_table *t, uint16_t caid,
               uint32_t provid, uint16_t sid)
{
    int i;
    if (!t) return 0;
    for (i = 0; i < CLIENTBUF_CHANNELS; i++) {
        struct clientbuf_channel *c = &t->channels[i];
        if (c->used && c->caid == caid && c->provid == provid && c->sid == sid) {
            return c;
        }
    }
    return 0;
}

static inline struct clientbuf_channel *
clientbuf_alloc(struct clientbuf_table *t, uint16_t caid,
                uint32_t provid, uint16_t sid)
{
    int i, oldest = 0;
    struct clientbuf_channel *c;

    c = clientbuf_find(t, caid, provid, sid);
    if (c) return c;

    for (i = 0; i < CLIENTBUF_CHANNELS; i++) {
        if (!t->channels[i].used) {
            c = &t->channels[i];
            c->caid = caid;
            c->provid = provid;
            c->sid = sid;
            c->buffer_size = t->max_size;
            c->used = 1;
            return c;
        }
        if (t->channels[i].head < t->channels[oldest].head) {
            oldest = i;
        }
    }
    /* Reuse oldest */
    c = &t->channels[oldest];
    memset(c, 0, sizeof(*c));
    c->caid = caid;
    c->provid = provid;
    c->sid = sid;
    c->buffer_size = t->max_size;
    c->used = 1;
    return c;
}

/* Buffer a CW for a channel */
static inline void clientbuf_add(struct clientbuf_table *t,
                                  uint16_t caid, uint32_t provid,
                                  uint16_t sid, const uint8_t cw[16],
                                  uint32_t now, uint32_t ttl_secs)
{
    struct clientbuf_channel *c;
    struct clientbuf_cw *slot;

    if (!clientbuf_enabled(t) || !cw) return;
    c = clientbuf_alloc(t, caid, provid, sid);
    if (!c) return;

    slot = &c->buffer[c->head];
    memcpy(slot->cw, cw, 16);
    slot->received_at = now;
    slot->expires_at = now + ttl_secs;
    slot->used = 1;
    c->head = (c->head + 1) % c->buffer_size;
    t->total_buffered++;
}

/* Get a buffered CW for a channel (if available and not expired).
 * Returns: 1 if a CW was retrieved, 0 if buffer empty/expired.
 */
static inline int clientbuf_get(struct clientbuf_table *t,
                                 uint16_t caid, uint32_t provid,
                                 uint16_t sid, uint32_t now,
                                 uint8_t out_cw[16])
{
    struct clientbuf_channel *c;
    struct clientbuf_cw *best = 0;
    int i;

    if (!clientbuf_enabled(t) || !out_cw) return 0;
    c = clientbuf_find(t, caid, provid, sid);
    if (!c) return 0;

    /* Find the most recent non-expired CW */
    for (i = 0; i < c->buffer_size; i++) {
        struct clientbuf_cw *slot = &c->buffer[i];
        if (!slot->used) continue;
        if (slot->expires_at <= now) continue;  /* expired */
        if (!best || slot->received_at > best->received_at) {
            best = slot;
        }
    }

    if (!best) return 0;
    memcpy(out_cw, best->cw, 16);
    t->total_used++;
    return 1;
}

/* Expire old CWs */
static inline void clientbuf_expire(struct clientbuf_table *t, uint32_t now)
{
    int i, j;
    if (!t) return;
    for (i = 0; i < CLIENTBUF_CHANNELS; i++) {
        struct clientbuf_channel *c = &t->channels[i];
        if (!c->used) continue;
        for (j = 0; j < c->buffer_size; j++) {
            struct clientbuf_cw *slot = &c->buffer[j];
            if (slot->used && slot->expires_at <= now) {
                slot->used = 0;
            }
        }
    }
}

/* Get buffer utilization for a channel (0 to buffer_size) */
static inline int clientbuf_utilization(const struct clientbuf_table *t,
                                         uint16_t caid, uint32_t provid,
                                         uint16_t sid, uint32_t now)
{
    const struct clientbuf_channel *c;
    int i, count = 0;

    if (!t) return 0;
    c = clientbuf_find((struct clientbuf_table *)t, caid, provid, sid);
    if (!c) return 0;

    for (i = 0; i < c->buffer_size; i++) {
        if (c->buffer[i].used && c->buffer[i].expires_at > now) {
            count++;
        }
    }
    return count;
}

#endif /* MCS_CLIENTBUF_H */
