/* cwpropblock.h — Bad CW Propagation Block (R22 candidate #14)
 *
 * PURPOSE: prevent a CW that has been identified as fake from being
 * relayed to other peers. This stops the "poison amplification"
 * problem where one bad peer pollutes the entire mesh.
 *
 * MECHANISM: when a CW is flagged as fake (by cwverify, cwlate, or
 * any other layer), record its fingerprint (first 8 bytes of CW +
 * channel). Before relaying a CW to peers, check if it matches a
 * blocked fingerprint.
 *
 * CONFIG: automatic — hooks into cache_send_fwdreply() and the
 * relay path. No configuration needed.
 *
 * ROLLBACK: remove cwpropblock_check() calls; blocking is inert.
 */
#ifndef MCS_CWPROPBLOCK_H
#define MCS_CWPROPBLOCK_H

#include <stdint.h>
#include <string.h>

#define CWPROPBLOCK_TABLE_SIZE 128
#define CWPROPBLOCK_TTL_SECS 300  /* 5 minutes */

struct cwpropblock_entry {
    uint8_t cw_prefix[8];  /* first 8 bytes of CW (fingerprint) */
    uint16_t caid;
    uint32_t provid;
    uint16_t sid;
    uint32_t blocked_at;
    uint32_t block_count;  /* how many times we blocked this CW */
    uint8_t used;
};

struct cwpropblock_table {
    struct cwpropblock_entry entries[CWPROPBLOCK_TABLE_SIZE];
    uint32_t total_blocks;
    uint32_t total_entries;
};

static inline void cwpropblock_init(struct cwpropblock_table *t)
{
    if (!t) return;
    memset(t, 0, sizeof(*t));
}

/* Block a CW from being propagated.
 * Returns: 1 if newly blocked, 0 if already blocked or table full.
 */
static inline int cwpropblock_add(struct cwpropblock_table *t,
                                   const uint8_t cw[16],
                                   uint16_t caid, uint32_t provid,
                                   uint16_t sid, uint32_t now)
{
    int i, free_slot = -1;
    struct cwpropblock_entry *e;

    if (!t || !cw) return 0;

    /* Check if already blocked */
    for (i = 0; i < CWPROPBLOCK_TABLE_SIZE; i++) {
        e = &t->entries[i];
        if (!e->used) {
            if (free_slot < 0) free_slot = i;
            continue;
        }
        if (e->caid == caid && e->provid == provid && e->sid == sid &&
            memcmp(e->cw_prefix, cw, 8) == 0) {
            e->block_count++;
            e->blocked_at = now;  /* refresh TTL */
            return 0;  /* already blocked */
        }
    }

    /* Allocate new slot */
    if (free_slot < 0) {
        /* Table full — find oldest entry */
        int oldest = 0;
        for (i = 1; i < CWPROPBLOCK_TABLE_SIZE; i++) {
            if (t->entries[i].blocked_at < t->entries[oldest].blocked_at) {
                oldest = i;
            }
        }
        free_slot = oldest;
    }

    e = &t->entries[free_slot];
    memcpy(e->cw_prefix, cw, 8);
    e->caid = caid;
    e->provid = provid;
    e->sid = sid;
    e->blocked_at = now;
    e->block_count = 1;
    e->used = 1;
    t->total_entries++;
    return 1;
}

/* Check if a CW is blocked from propagation.
 * Returns: 1 if blocked, 0 if allowed.
 */
static inline int cwpropblock_check(const struct cwpropblock_table *t,
                                     const uint8_t cw[16],
                                     uint16_t caid, uint32_t provid,
                                     uint16_t sid, uint32_t now)
{
    int i;
    if (!t || !cw) return 0;

    for (i = 0; i < CWPROPBLOCK_TABLE_SIZE; i++) {
        const struct cwpropblock_entry *e = &t->entries[i];
        if (!e->used) continue;
        /* Check TTL */
        if ((now - e->blocked_at) > CWPROPBLOCK_TTL_SECS) continue;
        /* Check match */
        if (e->caid == caid && e->provid == provid && e->sid == sid &&
            memcmp(e->cw_prefix, cw, 8) == 0) {
            /* Count this block (cast away const for counter) */
            ((struct cwpropblock_entry *)e)->block_count++;
            ((struct cwpropblock_table *)t)->total_blocks++;
            return 1;  /* blocked */
        }
    }
    return 0;  /* allowed */
}

/* Expire old entries */
static inline void cwpropblock_expire(struct cwpropblock_table *t,
                                       uint32_t now, uint32_t max_age)
{
    int i;
    if (!t) return;
    for (i = 0; i < CWPROPBLOCK_TABLE_SIZE; i++) {
        struct cwpropblock_entry *e = &t->entries[i];
        if (e->used && (now - e->blocked_at) > max_age) {
            e->used = 0;
        }
    }
}

/* Check if a CW is blocked (any channel — for global block) */
static inline int cwpropblock_check_global(const struct cwpropblock_table *t,
                                            const uint8_t cw[16],
                                            uint32_t now)
{
    int i;
    if (!t || !cw) return 0;

    for (i = 0; i < CWPROPBLOCK_TABLE_SIZE; i++) {
        const struct cwpropblock_entry *e = &t->entries[i];
        if (!e->used) continue;
        if ((now - e->blocked_at) > CWPROPBLOCK_TTL_SECS) continue;
        if (memcmp(e->cw_prefix, cw, 8) == 0) {
            return 1;  /* blocked globally */
        }
    }
    return 0;
}

#endif /* MCS_CWPROPBLOCK_H */
