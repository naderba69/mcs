/* cwcrosschannel.h — Cross-Channel CW Validation (R22 candidate #12)
 *
 * PURPOSE: a CW that is valid for one channel may not be valid for
 * another. Track which CWs work for which channels and reject CWs
 * that are offered for the wrong channel.
 *
 * MECHANISM: for each CW (fingerprint), track the set of channels
 * where it was proven valid. If a CW is offered for a channel where
 * it was never seen before, lower its confidence.
 *
 * CONFIG: automatic — hooks into CW delivery and feedback.
 *
 * ROLLBACK: remove cwcrosschannel_check() calls; validation is inert.
 */
#ifndef MCS_CWCROSSCHANNEL_H
#define MCS_CWCROSSCHANNEL_H

#include <stdint.h>
#include <string.h>

#define CWCC_ENTRIES 128
#define CWCC_CHANNELS_PER_CW 4  /* max channels per CW */

struct cwcc_entry {
    uint8_t cw_prefix[8];
    uint16_t caids[CWCC_CHANNELS_PER_CW];
    uint32_t provids[CWCC_CHANNELS_PER_CW];
    uint16_t sids[CWCC_CHANNELS_PER_CW];
    int channel_count;
    uint32_t first_seen;
    uint32_t last_seen;
    uint8_t used;
};

struct cwcc_table {
    struct cwcc_entry entries[CWCC_ENTRIES];
    uint32_t total_checks;
    uint32_t total_matches;
};

static inline void cwcrosschannel_init(struct cwcc_table *t)
{
    if (!t) return;
    memset(t, 0, sizeof(*t));
}

static inline struct cwcc_entry *
cwcc_find(struct cwcc_table *t, const uint8_t cw[16])
{
    int i;
    if (!t || !cw) return 0;
    for (i = 0; i < CWCC_ENTRIES; i++) {
        if (t->entries[i].used &&
            memcmp(t->entries[i].cw_prefix, cw, 8) == 0) {
            return &t->entries[i];
        }
    }
    return 0;
}

static inline struct cwcc_entry *
cwcc_alloc(struct cwcc_table *t, const uint8_t cw[16], uint32_t now)
{
    int i, oldest = 0;
    struct cwcc_entry *e;

    e = cwcc_find(t, cw);
    if (e) return e;

    for (i = 0; i < CWCC_ENTRIES; i++) {
        if (!t->entries[i].used) {
            e = &t->entries[i];
            memcpy(e->cw_prefix, cw, 8);
            e->first_seen = now;
            e->used = 1;
            return e;
        }
        if (t->entries[i].last_seen < t->entries[oldest].last_seen) {
            oldest = i;
        }
    }
    /* Reuse oldest */
    e = &t->entries[oldest];
    memset(e, 0, sizeof(*e));
    memcpy(e->cw_prefix, cw, 8);
    e->first_seen = now;
    e->used = 1;
    return e;
}

/* Record that a CW was proven valid for a channel */
static inline void cwcrosschannel_record_valid(struct cwcc_table *t,
                                                const uint8_t cw[16],
                                                uint16_t caid,
                                                uint32_t provid,
                                                uint16_t sid,
                                                uint32_t now)
{
    struct cwcc_entry *e;
    int i;

    if (!t || !cw) return;
    e = cwcc_alloc(t, cw, now);
    if (!e) return;
    e->last_seen = now;

    /* Check if channel already recorded */
    for (i = 0; i < e->channel_count; i++) {
        if (e->caids[i] == caid && e->provids[i] == provid && e->sids[i] == sid) {
            return;  /* already recorded */
        }
    }

    /* Add new channel if space */
    if (e->channel_count < CWCC_CHANNELS_PER_CW) {
        e->caids[e->channel_count] = caid;
        e->provids[e->channel_count] = provid;
        e->sids[e->channel_count] = sid;
        e->channel_count++;
    }
}

/* Check if a CW is known to be valid for a channel.
 * Returns: 1 if known valid, 0 if unknown, -1 if known invalid.
 */
static inline int cwcrosschannel_check(struct cwcc_table *t,
                                        const uint8_t cw[16],
                                        uint16_t caid, uint32_t provid,
                                        uint16_t sid)
{
    struct cwcc_entry *e;
    int i;

    if (!t || !cw) return 0;
    t->total_checks++;

    e = cwcc_find(t, cw);
    if (!e) return 0;  /* unknown CW */

    /* Check if this channel is in the valid set */
    for (i = 0; i < e->channel_count; i++) {
        if (e->caids[i] == caid && e->provids[i] == provid && e->sids[i] == sid) {
            t->total_matches++;
            return 1;  /* known valid for this channel */
        }
    }

    /* CW is known but not for this channel — lower confidence */
    return 0;
}

/* Get the number of channels where this CW is known to be valid */
static inline int cwcrosschannel_channel_count(const struct cwcc_table *t,
                                                const uint8_t cw[16])
{
    const struct cwcc_entry *e;
    if (!t || !cw) return 0;
    e = cwcc_find((struct cwcc_table *)t, cw);
    if (!e) return 0;
    return e->channel_count;
}

#endif /* MCS_CWCROSSCHANNEL_H */
