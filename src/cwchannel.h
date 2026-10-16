/* cwchannel.h — Channel-Aware Scoring (R22 candidate #8)
 *
 * PURPOSE: weight CW scoring by channel-specific history. A source
 * that is generally good may be bad for a specific channel (e.g.,
 * wrong card, wrong entitlement). Track per-(source, channel) success
 * rates and use them to weight CW selection.
 *
 * MECHANISM: for each (source, caid, provid, sid), track deliveries
 * and failures. Compute a channel-specific reputation score.
 *
 * CONFIG: automatic — hooks into CW delivery and feedback paths.
 *
 * ROLLBACK: remove cwchannel_score() calls; scoring is read-only.
 */
#ifndef MCS_CWCHANNEL_H
#define MCS_CWCHANNEL_H

#include <stdint.h>
#include <string.h>

#define CWCHANNEL_ENTRIES 256
#define CWCHANNEL_HISTORY 20  /* last 20 deliveries per entry */

struct cwchannel_entry {
    int source_id;
    uint16_t caid;
    uint32_t provid;
    uint16_t sid;
    uint32_t deliveries;
    uint32_t successes;
    uint8_t used;
};

struct cwchannel_table {
    struct cwchannel_entry entries[CWCHANNEL_ENTRIES];
};

static inline void cwchannel_init(struct cwchannel_table *t)
{
    if (!t) return;
    memset(t, 0, sizeof(*t));
}

static inline struct cwchannel_entry *
cwchannel_find(struct cwchannel_table *t, int source_id,
               uint16_t caid, uint32_t provid, uint16_t sid)
{
    int i;
    if (!t) return 0;
    for (i = 0; i < CWCHANNEL_ENTRIES; i++) {
        struct cwchannel_entry *e = &t->entries[i];
        if (e->used && e->source_id == source_id &&
            e->caid == caid && e->provid == provid && e->sid == sid) {
            return e;
        }
    }
    return 0;
}

static inline struct cwchannel_entry *
cwchannel_alloc(struct cwchannel_table *t, int source_id,
                uint16_t caid, uint32_t provid, uint16_t sid)
{
    int i, oldest = 0;
    struct cwchannel_entry *e;

    e = cwchannel_find(t, source_id, caid, provid, sid);
    if (e) return e;

    for (i = 0; i < CWCHANNEL_ENTRIES; i++) {
        if (!t->entries[i].used) {
            e = &t->entries[i];
            e->source_id = source_id;
            e->caid = caid;
            e->provid = provid;
            e->sid = sid;
            e->used = 1;
            return e;
        }
        if (t->entries[i].deliveries < t->entries[oldest].deliveries) {
            oldest = i;
        }
    }
    /* Reuse least-used */
    e = &t->entries[oldest];
    memset(e, 0, sizeof(*e));
    e->source_id = source_id;
    e->caid = caid;
    e->provid = provid;
    e->sid = sid;
    e->used = 1;
    return e;
}

/* Record a successful CW delivery */
static inline void cwchannel_record_success(struct cwchannel_table *t,
                                             int source_id, uint16_t caid,
                                             uint32_t provid, uint16_t sid)
{
    struct cwchannel_entry *e;
    if (!t) return;
    e = cwchannel_alloc(t, source_id, caid, provid, sid);
    if (!e) return;
    e->deliveries++;
    e->successes++;
    /* Cap at CWCHANNEL_HISTORY to keep recent history weighted */
    if (e->deliveries > CWCHANNEL_HISTORY) {
        e->deliveries = CWCHANNEL_HISTORY;
        e->successes = (e->successes * CWCHANNEL_HISTORY) / (CWCHANNEL_HISTORY + 1);
        e->successes++;
    }
}

/* Record a failed CW delivery */
static inline void cwchannel_record_failure(struct cwchannel_table *t,
                                             int source_id, uint16_t caid,
                                             uint32_t provid, uint16_t sid)
{
    struct cwchannel_entry *e;
    if (!t) return;
    e = cwchannel_alloc(t, source_id, caid, provid, sid);
    if (!e) return;
    e->deliveries++;
    if (e->deliveries > CWCHANNEL_HISTORY) {
        e->deliveries = CWCHANNEL_HISTORY;
        e->successes = (e->successes * CWCHANNEL_HISTORY) / (CWCHANNEL_HISTORY + 1);
    }
}

/* Get channel-specific reputation score (0-100) */
static inline int cwchannel_score(const struct cwchannel_table *t,
                                   int source_id, uint16_t caid,
                                   uint32_t provid, uint16_t sid)
{
    const struct cwchannel_entry *e;
    if (!t) return 50;  /* default: neutral */
    e = cwchannel_find((struct cwchannel_table *)t, source_id, caid, provid, sid);
    if (!e || e->deliveries == 0) return 50;  /* no history: neutral */
    return (int)((e->successes * 100) / e->deliveries);
}

/* Check if we have enough history to trust the score */
static inline int cwchannel_has_history(const struct cwchannel_table *t,
                                         int source_id, uint16_t caid,
                                         uint32_t provid, uint16_t sid,
                                         int min_samples)
{
    const struct cwchannel_entry *e;
    if (!t) return 0;
    e = cwchannel_find((struct cwchannel_table *)t, source_id, caid, provid, sid);
    if (!e) return 0;
    return (e->deliveries >= (uint32_t)min_samples) ? 1 : 0;
}

#endif /* MCS_CWCHANNEL_H */
