/* udpfast.h — UDP Fast Path for Cache (R22 candidate #25)
 *
 * PURPOSE: use UDP instead of TCP for cache exchange to reduce latency.
 * TCP has handshake overhead and congestion control that adds latency.
 * UDP is faster but requires reliability mechanisms.
 *
 * MECHANISM: for time-critical cache pushes, use UDP. Implement
 * simple reliability: sequence numbers, ACKs, retransmit on timeout.
 *
 * CONFIG: UDP FAST: ON (default OFF = TCP only).
 *
 * ROLLBACK: remove udpfast_send() calls; UDP fast path is inert.
 */
#ifndef MCS_UDPFAST_H
#define MCS_UDPFAST_H

#include <stdint.h>
#include <string.h>
#include "tick_math.h"

#define UDPFAST_PENDING_SIZE 64
#define UDPFAST_RETX_MS 200
#define UDPFAST_MAX_RETRIES 3

struct udpfast_pending {
    uint32_t seq;
    uint8_t data[512];
    int data_len;
    uint32_t sent_at;
    uint32_t retx_count;
    uint32_t dest_ip;
    uint16_t dest_port;
    uint8_t used;
    uint8_t acked;
};

struct udpfast_table {
    struct udpfast_pending pending[UDPFAST_PENDING_SIZE];
    uint32_t next_seq;
    int enabled;
    uint32_t total_sent;
    uint32_t total_acked;
    uint32_t total_retx;
    uint32_t total_timeouts;
};

static inline void udpfast_init(struct udpfast_table *t, int enabled)
{
    if (!t) return;
    memset(t, 0, sizeof(*t));
    t->enabled = enabled;
    t->next_seq = 1;
}

static inline int udpfast_enabled(const struct udpfast_table *t)
{
    return t && t->enabled;
}

/* Send a UDP packet (record for reliability).
 * Returns: sequence number, or 0 if disabled/full.
 */
static inline uint32_t udpfast_send(struct udpfast_table *t,
                                     const uint8_t *data, int data_len,
                                     uint32_t dest_ip, uint16_t dest_port,
                                     uint32_t now)
{
    int i, free_slot = -1;
    struct udpfast_pending *p;

    if (!udpfast_enabled(t)) return 0;
    if (!data || data_len <= 0 || data_len > 512) return 0;

    /* Find free slot */
    for (i = 0; i < UDPFAST_PENDING_SIZE; i++) {
        if (!t->pending[i].used) {
            free_slot = i;
            break;
        }
        if (t->pending[i].acked) {
            free_slot = i;
            break;
        }
    }
    if (free_slot < 0) {
        /* Reuse the pending packet with greatest modulo elapsed age. */
        int oldest = -1;
        uint32_t oldest_age = 0;
        for (i = 0; i < UDPFAST_PENDING_SIZE; i++) {
            uint32_t age = tick32_elapsed(now, t->pending[i].sent_at);
            if (oldest < 0 || age > oldest_age) {
                oldest = i;
                oldest_age = age;
            }
        }
        if (oldest < 0) return 0;
        free_slot = oldest;
    }

    p = &t->pending[free_slot];
    p->seq = t->next_seq++;
    memcpy(p->data, data, data_len);
    p->data_len = data_len;
    p->sent_at = now;
    p->retx_count = 0;
    p->dest_ip = dest_ip;
    p->dest_port = dest_port;
    p->used = 1;
    p->acked = 0;
    t->total_sent++;
    return p->seq;
}

/* Record an ACK for a sequence number */
static inline void udpfast_ack(struct udpfast_table *t, uint32_t seq)
{
    int i;
    if (!t) return;
    for (i = 0; i < UDPFAST_PENDING_SIZE; i++) {
        if (t->pending[i].used && t->pending[i].seq == seq) {
            t->pending[i].acked = 1;
            t->total_acked++;
            return;
        }
    }
}

/* Check for packets that need retransmission.
 * Returns: number of packets to retransmit.
 */
static inline int udpfast_check_retx(struct udpfast_table *t,
                                      uint32_t now,
                                      uint32_t *seqs, uint8_t **datas,
                                      int *data_lens,
                                      uint32_t *dest_ips, uint16_t *dest_ports,
                                      int max_out)
{
    int i, n = 0;
    if (!udpfast_enabled(t)) return 0;

    for (i = 0; i < UDPFAST_PENDING_SIZE && n < max_out; i++) {
        struct udpfast_pending *p = &t->pending[i];
        uint32_t age_ms;

        if (!p->used || p->acked) continue;

        /* now and sent_at are millisecond uint32 ticks. */
        age_ms = tick32_elapsed(now, p->sent_at);
        if (age_ms < UDPFAST_RETX_MS * (p->retx_count + 1u)) continue;
        if (p->retx_count >= UDPFAST_MAX_RETRIES) {
            t->total_timeouts++;
            p->used = 0;
            continue;
        }

        seqs[n] = p->seq;
        datas[n] = (uint8_t *)p->data;
        data_lens[n] = p->data_len;
        dest_ips[n] = p->dest_ip;
        dest_ports[n] = p->dest_port;
        n++;
    }
    return n;
}

/* Record a retransmission */
static inline void udpfast_record_retx(struct udpfast_table *t, uint32_t seq,
                                        uint32_t now)
{
    int i;
    if (!t) return;
    for (i = 0; i < UDPFAST_PENDING_SIZE; i++) {
        if (t->pending[i].used && t->pending[i].seq == seq) {
            t->pending[i].retx_count++;
            t->pending[i].sent_at = now;
            t->total_retx++;
            return;
        }
    }
}

/* Expire old entries */
static inline void udpfast_expire(struct udpfast_table *t,
                                   uint32_t now, uint32_t max_age)
{
    int i;
    if (!t) return;
    for (i = 0; i < UDPFAST_PENDING_SIZE; i++) {
        if (t->pending[i].used &&
            tick32_elapsed(now, t->pending[i].sent_at) > max_age) {
            t->pending[i].used = 0;
        }
    }
}

#endif /* MCS_UDPFAST_H */
