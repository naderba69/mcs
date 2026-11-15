/* autorecov.h — Auto-Recovery bookkeeping (R22 candidate #35)
 *
 * HONEST STATUS (band 47 audit): this module only RECORDS. Black-
 * screen failures on CCcam card servers call autorecov_trigger(),
 * successes call autorecov_success(), and the counters are shown on
 * /user-trace. The stage machine (autorecov_tick) has NO caller and
 * no code anywhere acts on a channel's stage: nothing tries an
 * alternative CW, switches a source, or restarts an ECM because of
 * this table. It is a monitor, and the page says so.
 *
 * CONFIG: AUTO RECOVER: ON|OFF (default OFF). Read live from
 * cfg_auto_recover_enabled at every call, so reloads apply and the
 * old hardcoded init-time "enabled by default" is gone.
 *
 * LOCKING (band 47): the table is written from several CCcam server
 * threads and read from the HTTP thread. One leaf mutex inside the
 * table covers every public entry point; it is held only across
 * the table arithmetic and takes no other lock.
 *
 * ROLLBACK: AUTO RECOVER: OFF (or remove the trigger/success calls);
 * the bookkeeping is inert either way.
 */
#ifndef MCS_AUTORECOV_H
#define MCS_AUTORECOV_H

#include <stdint.h>
#include <string.h>
#include <pthread.h>

extern int cfg_auto_recover_enabled;

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
    pthread_mutex_t lock;
};

static inline void autorecov_init(struct autorecov_table *t, int enabled)
{
    pthread_mutex_init(&t->lock, NULL);
    if (!t) return;
    memset(t, 0, sizeof(*t));
    t->enabled = enabled;
}

static inline int autorecov_enabled(const struct autorecov_table *t)
{
    (void)t;
    return cfg_auto_recover_enabled;
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
    if (!t || !autorecov_enabled(t)) return;
    pthread_mutex_lock(&t->lock);
    c = autorecov_alloc(t, caid, provid, sid);
    if (!c) { pthread_mutex_unlock(&t->lock); return; }
    if (c->stage != AUTORECOV_STAGE_NORMAL) { pthread_mutex_unlock(&t->lock); return; }  /* already recovering */
    c->stage = AUTORECOV_STAGE_ALT_CW;
    c->stage_entered = now;
    c->recovery_count++;
    t->total_recoveries++;
    pthread_mutex_unlock(&t->lock);
}

/* Advance recovery to the next stage (if enough time has passed) */
static inline int autorecov_tick(struct autorecov_table *t,
                                  uint16_t caid, uint32_t provid,
                                  uint16_t sid, uint32_t now)
{
    struct autorecov_channel *c;
    int stage;
    if (!t || !autorecov_enabled(t)) return AUTORECOV_STAGE_NORMAL;
    pthread_mutex_lock(&t->lock);
    c = autorecov_find(t, caid, provid, sid);
    if (!c || c->stage == AUTORECOV_STAGE_NORMAL) { pthread_mutex_unlock(&t->lock); return AUTORECOV_STAGE_NORMAL; }

    /* Check if enough time has passed to advance */
    if ((now - c->stage_entered) < AUTORECOV_STAGE_INTERVAL) {
        stage = c->stage;
        pthread_mutex_unlock(&t->lock);
        return stage;
    }

    /* Advance to next stage */
    c->stage++;
    c->stage_entered = now;
    if (c->stage >= AUTORECOV_STAGE_GIVEUP) {
        c->stage = AUTORECOV_STAGE_NORMAL;  /* give up, back to normal */
    }
    stage = c->stage;
    pthread_mutex_unlock(&t->lock);
    return stage;
}

/* Mark recovery as successful (black screen resolved) */
static inline void autorecov_success(struct autorecov_table *t,
                                      uint16_t caid, uint32_t provid,
                                      uint16_t sid)
{
    struct autorecov_channel *c;
    if (!t || !autorecov_enabled(t)) return;
    pthread_mutex_lock(&t->lock);
    c = autorecov_find(t, caid, provid, sid);
    if (!c) { pthread_mutex_unlock(&t->lock); return; }
    c->stage = AUTORECOV_STAGE_NORMAL;
    t->total_successes++;
    pthread_mutex_unlock(&t->lock);
}

/* Get current recovery stage for a channel */
static inline int autorecov_stage(const struct autorecov_table *t,
                                   uint16_t caid, uint32_t provid,
                                   uint16_t sid)
{
    const struct autorecov_channel *c;
    int stage;
    if (!t) return AUTORECOV_STAGE_NORMAL;
    pthread_mutex_lock(&((struct autorecov_table *)t)->lock);
    c = autorecov_find((struct autorecov_table *)t, caid, provid, sid);
    stage = c ? c->stage : AUTORECOV_STAGE_NORMAL;
    pthread_mutex_unlock(&((struct autorecov_table *)t)->lock);
    return stage;
}

/* Snapshot for the trace page: enable state, totals, channels
 * currently mid-"recovery" (stage != NORMAL). */
static inline void autorecov_snapshot(struct autorecov_table *t,
                                      int *enabled, uint32_t *recoveries,
                                      uint32_t *successes, int *active)
{
    int i, n = 0;
    if (enabled) *enabled = cfg_auto_recover_enabled;
    if (recoveries) *recoveries = 0;
    if (successes) *successes = 0;
    if (active) *active = 0;
    if (!t) return;
    pthread_mutex_lock(&t->lock);
    if (recoveries) *recoveries = t->total_recoveries;
    if (successes) *successes = t->total_successes;
    for (i = 0; i < AUTORECOV_CHANNELS; i++)
        if (t->channels[i].used && t->channels[i].stage != AUTORECOV_STAGE_NORMAL) n++;
    if (active) *active = n;
    pthread_mutex_unlock(&t->lock);
}

#endif /* MCS_AUTORECOV_H */
