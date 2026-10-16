/* chanpred.h — Channel Health Prediction (R22 candidate #31)
 *
 * PURPOSE: predict when a channel is about to die (streak of refusals)
 * before it actually reaches the threshold. This allows proactive
 * measures (source switch, emergency source) before the viewer sees
 * a black screen.
 *
 * MECHANISM: track the refusal rate per channel. If the rate is
 * accelerating (more refusals in recent history than older), predict
 * death within N seconds.
 *
 * CONFIG: automatic — hooks into chanhealth.
 *
 * ROLLBACK: remove chanpred_predict() calls; prediction is inert.
 */
#ifndef MCS_CHANPRED_H
#define MCS_CHANPRED_H

#include <stdint.h>
#include <string.h>

#define CHANPRED_CHANNELS 32
#define CHANPRED_HISTORY 10
#define CHANPRED_ACCELERATION_THRESHOLD 2.0  /* 2x acceleration = warning */

struct chanpred_channel {
    uint16_t caid;
    uint32_t provid;
    uint16_t sid;
    uint32_t refusals[CHANPRED_HISTORY];
    int history_head;
    int history_count;
    uint32_t last_update;
    int predicted_death;  /* 1 = predicted to die soon */
    uint32_t total_predictions;
    uint8_t used;
};

struct chanpred_table {
    struct chanpred_channel channels[CHANPRED_CHANNELS];
    uint32_t total_predictions;
};

static inline void chanpred_init(struct chanpred_table *t)
{
    if (!t) return;
    memset(t, 0, sizeof(*t));
}

static inline struct chanpred_channel *
chanpred_find(struct chanpred_table *t, uint16_t caid,
              uint32_t provid, uint16_t sid)
{
    int i;
    if (!t) return 0;
    for (i = 0; i < CHANPRED_CHANNELS; i++) {
        struct chanpred_channel *c = &t->channels[i];
        if (c->used && c->caid == caid && c->provid == provid && c->sid == sid) {
            return c;
        }
    }
    return 0;
}

static inline struct chanpred_channel *
chanpred_alloc(struct chanpred_table *t, uint16_t caid,
               uint32_t provid, uint16_t sid)
{
    int i, oldest = 0;
    struct chanpred_channel *c;

    c = chanpred_find(t, caid, provid, sid);
    if (c) return c;

    for (i = 0; i < CHANPRED_CHANNELS; i++) {
        if (!t->channels[i].used) {
            c = &t->channels[i];
            c->caid = caid;
            c->provid = provid;
            c->sid = sid;
            c->used = 1;
            return c;
        }
        if (t->channels[i].last_update < t->channels[oldest].last_update) {
            oldest = i;
        }
    }
    /* Reuse oldest */
    c = &t->channels[oldest];
    memset(c, 0, sizeof(*c));
    c->caid = caid;
    c->provid = provid;
    c->sid = sid;
    c->used = 1;
    return c;
}

/* Record a refusal event for a channel */
static inline void chanpred_record(struct chanpred_table *t,
                                    uint16_t caid, uint32_t provid,
                                    uint16_t sid, uint32_t now)
{
    struct chanpred_channel *c;
    if (!t) return;
    c = chanpred_alloc(t, caid, provid, sid);
    if (!c) return;

    c->refusals[c->history_head]++;
    c->last_update = now;
}

/* Advance the history window (call periodically, e.g., every 10 seconds) */
static inline void chanpred_advance(struct chanpred_table *t,
                                     uint16_t caid, uint32_t provid,
                                     uint16_t sid)
{
    struct chanpred_channel *c;
    if (!t) return;
    c = chanpred_find(t, caid, provid, sid);
    if (!c) return;

    c->history_head = (c->history_head + 1) % CHANPRED_HISTORY;
    if (c->history_count < CHANPRED_HISTORY) c->history_count++;
    c->refusals[c->history_head] = 0;
}

/* Predict if a channel is about to die.
 * Returns: 1 if death predicted, 0 otherwise.
 */
static inline int chanpred_predict(const struct chanpred_table *t,
                                    uint16_t caid, uint32_t provid,
                                    uint16_t sid)
{
    const struct chanpred_channel *c;
    uint32_t recent = 0, older = 0;
    double ratio;
    int i;

    if (!t) return 0;
    c = chanpred_find((struct chanpred_table *)t, caid, provid, sid);
    if (!c || c->history_count < 4) return 0;

    /* Compare recent half vs older half */
    for (i = 0; i < c->history_count / 2; i++) {
        recent += c->refusals[(c->history_head + CHANPRED_HISTORY - 1 - i) % CHANPRED_HISTORY];
    }
    for (i = c->history_count / 2; i < c->history_count; i++) {
        older += c->refusals[(c->history_head + CHANPRED_HISTORY - 1 - i) % CHANPRED_HISTORY];
    }

    if (older == 0) {
        /* No older refusals — if recent has refusals, that's acceleration */
        if (recent > 0) {
            ((struct chanpred_channel *)c)->predicted_death = 1;
            ((struct chanpred_table *)t)->total_predictions++;
            ((struct chanpred_channel *)c)->total_predictions++;
            return 1;
        }
        return 0;
    }

    ratio = (double)recent / (double)older;
    if (ratio >= CHANPRED_ACCELERATION_THRESHOLD) {
        ((struct chanpred_channel *)c)->predicted_death = 1;
        ((struct chanpred_table *)t)->total_predictions++;
        ((struct chanpred_channel *)c)->total_predictions++;
        return 1;
    }

    ((struct chanpred_channel *)c)->predicted_death = 0;
    return 0;
}

/* Get the refusal acceleration ratio */
static inline double chanpred_acceleration(const struct chanpred_table *t,
                                            uint16_t caid, uint32_t provid,
                                            uint16_t sid)
{
    const struct chanpred_channel *c;
    uint32_t recent = 0, older = 0;
    int i;

    if (!t) return 0.0;
    c = chanpred_find((struct chanpred_table *)t, caid, provid, sid);
    if (!c || c->history_count < 4) return 0.0;

    for (i = 0; i < c->history_count / 2; i++) {
        recent += c->refusals[(c->history_head + CHANPRED_HISTORY - 1 - i) % CHANPRED_HISTORY];
    }
    for (i = c->history_count / 2; i < c->history_count; i++) {
        older += c->refusals[(c->history_head + CHANPRED_HISTORY - 1 - i) % CHANPRED_HISTORY];
    }

    if (older == 0) return (recent > 0) ? 100.0 : 0.0;
    return (double)recent / (double)older;
}

#endif /* MCS_CHANPRED_H */
