/* cwfingerprint.h — Peer CW Fingerprint (R22 candidate #10)
 *
 * PURPOSE: detect systematic liars by building a fingerprint of each
 * peer's CW behavior. A peer that consistently delivers CWs that are
 * later proven fake gets a "liar fingerprint" and is penalized.
 *
 * MECHANISM: for each peer, track the ratio of CWs that were later
 * proven fake (by cwlate, cwverify, or client feedback). Build a
 * fingerprint: (total_delivered, total_proven_fake, fake_ratio).
 *
 * CONFIG: automatic — hooks into trust recording.
 *
 * ROLLBACK: remove cwfingerprint_record() calls; fingerprinting is
 * read-only.
 */
#ifndef MCS_CWFINGERPRINT_H
#define MCS_CWFINGERPRINT_H

#include <stdint.h>
#include <string.h>

#define CWFP_PEERS 64
#define CWFP_WINDOW 100  /* last 100 CWs per peer */

struct cwfingerprint_peer {
    int peer_id;
    uint32_t total_delivered;
    uint32_t total_proven_fake;
    uint32_t window_delivered;   /* in current window */
    uint32_t window_fake;        /* in current window */
    uint32_t last_update;
    uint8_t used;
};

struct cwfingerprint_table {
    struct cwfingerprint_peer peers[CWFP_PEERS];
    uint32_t total_records;
};

/* Threshold: if >30% of CWs from a peer are proven fake, flag as liar */
#define CWFP_LIAR_THRESHOLD_PERCENT 30
#define CWFP_MIN_SAMPLE 10  /* need at least 10 samples to judge */

static inline void cwfingerprint_init(struct cwfingerprint_table *t)
{
    if (!t) return;
    memset(t, 0, sizeof(*t));
}

static inline struct cwfingerprint_peer *
cwfingerprint_find(struct cwfingerprint_table *t, int peer_id)
{
    int i;
    if (!t) return 0;
    for (i = 0; i < CWFP_PEERS; i++) {
        if (t->peers[i].used && t->peers[i].peer_id == peer_id) {
            return &t->peers[i];
        }
    }
    return 0;
}

static inline struct cwfingerprint_peer *
cwfingerprint_alloc(struct cwfingerprint_table *t, int peer_id)
{
    int i, oldest = 0;
    struct cwfingerprint_peer *p;

    p = cwfingerprint_find(t, peer_id);
    if (p) return p;

    for (i = 0; i < CWFP_PEERS; i++) {
        if (!t->peers[i].used) {
            p = &t->peers[i];
            p->peer_id = peer_id;
            p->used = 1;
            return p;
        }
        if (t->peers[i].last_update < t->peers[oldest].last_update) {
            oldest = i;
        }
    }
    /* Reuse oldest */
    p = &t->peers[oldest];
    memset(p, 0, sizeof(*p));
    p->peer_id = peer_id;
    p->used = 1;
    return p;
}

/* Record a CW delivery from a peer */
static inline void cwfingerprint_record_delivery(struct cwfingerprint_table *t,
                                                  int peer_id, uint32_t now)
{
    struct cwfingerprint_peer *p;
    if (!t) return;
    p = cwfingerprint_alloc(t, peer_id);
    if (!p) return;
    p->total_delivered++;
    p->window_delivered++;
    p->last_update = now;
    t->total_records++;

    /* Reset window if full */
    if (p->window_delivered > CWFP_WINDOW) {
        p->window_delivered = 1;
        p->window_fake = 0;
    }
}

/* Record that a CW from this peer was proven fake */
static inline void cwfingerprint_record_fake(struct cwfingerprint_table *t,
                                              int peer_id, uint32_t now)
{
    struct cwfingerprint_peer *p;
    if (!t) return;
    p = cwfingerprint_find(t, peer_id);
    if (!p) return;
    p->total_proven_fake++;
    p->window_fake++;
    p->last_update = now;
}

/* Check if a peer has a "liar" fingerprint */
static inline int cwfingerprint_is_liar(const struct cwfingerprint_table *t,
                                         int peer_id)
{
    const struct cwfingerprint_peer *p;
    uint32_t percent;

    if (!t) return 0;
    p = cwfingerprint_find((struct cwfingerprint_table *)t, peer_id);
    if (!p) return 0;
    if (p->window_delivered < CWFP_MIN_SAMPLE) return 0;

    percent = (p->window_fake * 100) / p->window_delivered;
    return (percent >= CWFP_LIAR_THRESHOLD_PERCENT) ? 1 : 0;
}

/* Get the fake ratio for a peer (0-100 percent) */
static inline int cwfingerprint_fake_percent(const struct cwfingerprint_table *t,
                                              int peer_id)
{
    const struct cwfingerprint_peer *p;
    if (!t) return 0;
    p = cwfingerprint_find((struct cwfingerprint_table *)t, peer_id);
    if (!p || p->window_delivered == 0) return 0;
    return (int)((p->window_fake * 100) / p->window_delivered);
}

#endif /* MCS_CWFINGERPRINT_H */
