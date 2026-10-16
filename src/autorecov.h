/* autorecov.h — Auto-Recovery (R22 candidate #35)
 *
 * PURPOSE: automatically recover from black screens without manual
 * intervention. When a black screen is detected, trigger a recovery
 * sequence: try alternative CWs, switch sources, restart ECM requests.
 *
 * MECHANISM: state machine with recovery stages:
 *   Stage 0: Normal operation
 *   Stage 1: Black screen detected — try alternative CW (cwreplace)
 *   Stage 2: Still black — switch source (autoswitch)
 *   Stage 3: Still black — restart ECM request (fastretx)
 *   Stage 4: Still black — emergency source (cwemergency)
 *
 * CONFIG: AUTO RECOVER: ON (default OFF = manual recovery).
 *
 * ROLLBACK: remove autorecov_tick() calls; recovery is inert.
 */
#ifndef MCS_AUTORECOV_H
#define MCS_AUTORECOV_H

#include <stdint.h>
#include <string.h>

#define AUTORECOV_CHANNELS 32
#define AUTORECOV_STAGE_NORMAL     0
#define AUTORECOV_STAGE_ALT_CW     1
#define AUTORECOV_STAGE_SWITCH_SRC 2
#define AUTORECOV_STAGE_RESTART    3
#define AUTORECOV_STAGE_EMERGENCY  4
#define AUTORECOV_STAGE_GIVEUP     5
#define AUTORECOV_STAGE_INTERVAL   2  /* seconds between stages */

struct autorecov_channel {
    uint16_t caid;
    uint32_t provid;
    uint16_t sid;
    int stage;
    uint32_t stage_entered;
    uint32_t recovery_count;
    uint8_t used;
};

struct autorecov_table {
    struct autorecov_channel channels[AUTORECOV_CHANNELS];
    int enabled;
    uint32_t total_recoveries;
    uint32_t total_successes;
};

static inline void autorecov_init(struct autorecov_table *t, int enabled)
{
    if (!t) return;
    memset(t, 0, sizeof(*t));
    t->enabled = enabled;
}

static inline int autorecov_enabled(const struct autorecov_table *t)
{
    return t && t->enabled;
}

static inline struct autorecov_channel *
autorecov_find(struct autorecov_table *t, uint16_t caid,
               uint32_t provid, uint16_t sid)
{
    int i;
    if (!t) return 0;
    for (i = 0; i < AUTORECOV_CHANNELS; i++) {
        struct autorecov_channel *c = &t->channels[i];
        if (c->used && c->caid == caid && c->provid == provid && c->sid == sid) {
            return c;
        }
    }
    return 0;
}

static inline struct autorecov_channel *
autorecov_alloc(struct autorecov_table *t, uint16_t caid,
                uint32_t provid, uint16_t sid)
{
    int i, oldest = 0;
    struct autorecov_channel *c;

    c = autorecov_find(t, caid, provid, sid);
    if (c) return c;

    for (i = 0; i < AUTORECOV_CHANNELS; i++) {
        if (!t->channels[i].used) {
            c = &t->channels[i];
            c->caid = caid;
            c->provid = provid;
            c->sid = sid;
            c->used = 1;
            return c;
        }
        if (t->channels[i].recovery_count < t->channels[oldest].recovery_count) {
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

/* Trigger recovery for a channel */
static inline void autorecov_trigger(struct autorecov_table *t,
                                      uint16_t caid, uint32_t provid,
                                      uint16_t sid, uint32_t now)
{
    struct autorecov_channel *c;
    if (!autorecov_enabled(t)) return;
    c = autorecov_alloc(t, caid, provid, sid);
    if (!c) return;
    if (c->stage != AUTORECOV_STAGE_NORMAL) return;  /* already recovering */
    c->stage = AUTORECOV_STAGE_ALT_CW;
    c->stage_entered = now;
    c->recovery_count++;
    t->total_recoveries++;
}

/* Advance recovery to the next stage (if enough time has passed) */
static inline int autorecov_tick(struct autorecov_table *t,
                                  uint16_t caid, uint32_t provid,
                                  uint16_t sid, uint32_t now)
{
    struct autorecov_channel *c;
    if (!autorecov_enabled(t)) return AUTORECOV_STAGE_NORMAL;
    c = autorecov_find(t, caid, provid, sid);
    if (!c || c->stage == AUTORECOV_STAGE_NORMAL) return AUTORECOV_STAGE_NORMAL;

    /* Check if enough time has passed to advance */
    if ((now - c->stage_entered) < AUTORECOV_STAGE_INTERVAL) {
        return c->stage;
    }

    /* Advance to next stage */
    c->stage++;
    c->stage_entered = now;
    if (c->stage >= AUTORECOV_STAGE_GIVEUP) {
        c->stage = AUTORECOV_STAGE_NORMAL;  /* give up, back to normal */
    }
    return c->stage;
}

/* Mark recovery as successful (black screen resolved) */
static inline void autorecov_success(struct autorecov_table *t,
                                      uint16_t caid, uint32_t provid,
                                      uint16_t sid)
{
    struct autorecov_channel *c;
    if (!t) return;
    c = autorecov_find(t, caid, provid, sid);
    if (!c) return;
    c->stage = AUTORECOV_STAGE_NORMAL;
    t->total_successes++;
}

/* Get current recovery stage for a channel */
static inline int autorecov_stage(const struct autorecov_table *t,
                                   uint16_t caid, uint32_t provid,
                                   uint16_t sid)
{
    const struct autorecov_channel *c;
    if (!t) return AUTORECOV_STAGE_NORMAL;
    c = autorecov_find((struct autorecov_table *)t, caid, provid, sid);
    if (!c) return AUTORECOV_STAGE_NORMAL;
    return c->stage;
}

#endif /* MCS_AUTORECOV_H */
