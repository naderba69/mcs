/* blackscreen.h — Black Screen Detection (R22 candidate #34)
 *
 * PURPOSE: detect when a client is receiving a black screen (no video
 * output) despite receiving CWs. This indicates the CW is wrong
 * (fake/expired) even though it passed all structural checks.
 *
 * MECHANISM: monitor client feedback — if a client reports "no video"
 * or if we detect repeated ECM requests for the same channel without
 * successful decryption, flag it as a black screen event.
 *
 * CONFIG: automatic — no configuration needed. Hooks into chanhealth
 * and cwlate to trigger instant replacement.
 *
 * ROLLBACK: remove blackscreen_detect() calls; the layer is read-only.
 */
#ifndef MCS_BLACKSCREEN_H
#define MCS_BLACKSCREEN_H

#include <stdint.h>
#include <string.h>

#define BLACKSCREEN_MAX_EVENTS 64
#define BLACKSCREEN_WINDOW_SECS 10
#define BLACKSCREEN_THRESHOLD 3

struct blackscreen_event {
    uint16_t caid;
    uint32_t provid;
    uint16_t sid;
    uint32_t first_detected;
    uint32_t last_detected;
    uint32_t count;
    uint8_t resolved;
};

struct blackscreen_table {
    struct blackscreen_event events[BLACKSCREEN_MAX_EVENTS];
    int count;
    uint32_t total_detections;
    uint32_t total_resolutions;
};

static inline void blackscreen_init(struct blackscreen_table *t)
{
    if (!t) return;
    memset(t, 0, sizeof(*t));
}

/* Detect a black screen event.
 * Returns 1 if this is a new black screen (threshold reached),
 * 0 if already tracking or below threshold.
 */
static inline int blackscreen_detect(struct blackscreen_table *t,
                                      uint16_t caid, uint32_t provid,
                                      uint16_t sid, uint32_t now)
{
    int i, slot = -1;
    if (!t) return 0;

    /* Find existing event */
    for (i = 0; i < t->count; i++) {
        if (t->events[i].caid == caid &&
            t->events[i].provid == provid &&
            t->events[i].sid == sid &&
            !t->events[i].resolved) {
            slot = i;
            break;
        }
    }

    /* Allocate new slot if not found */
    if (slot < 0) {
        if (t->count < BLACKSCREEN_MAX_EVENTS) {
            slot = t->count++;
            t->events[slot].caid = caid;
            t->events[slot].provid = provid;
            t->events[slot].sid = sid;
            t->events[slot].first_detected = now;
            t->events[slot].count = 0;
            t->events[slot].resolved = 0;
        } else {
            /* Find oldest resolved event to reuse */
            for (i = 0; i < t->count; i++) {
                if (t->events[i].resolved) {
                    slot = i;
                    t->events[slot].caid = caid;
                    t->events[slot].provid = provid;
                    t->events[slot].sid = sid;
                    t->events[slot].first_detected = now;
                    t->events[slot].count = 0;
                    t->events[slot].resolved = 0;
                    break;
                }
            }
            if (slot < 0) return 0; /* table full of active events */
        }
    }

    t->events[slot].last_detected = now;
    t->events[slot].count++;
    t->total_detections++;

    /* Check if threshold reached */
    if (t->events[slot].count == BLACKSCREEN_THRESHOLD) {
        return 1; /* new black screen detected */
    }
    return 0;
}

/* Mark a black screen as resolved (successful CW delivered) */
static inline void blackscreen_resolve(struct blackscreen_table *t,
                                        uint16_t caid, uint32_t provid,
                                        uint16_t sid)
{
    int i;
    if (!t) return;
    for (i = 0; i < t->count; i++) {
        if (t->events[i].caid == caid &&
            t->events[i].provid == provid &&
            t->events[i].sid == sid &&
            !t->events[i].resolved) {
            t->events[i].resolved = 1;
            t->total_resolutions++;
            return;
        }
    }
}

/* Check if a channel is currently in black screen state */
static inline int blackscreen_active(const struct blackscreen_table *t,
                                      uint16_t caid, uint32_t provid,
                                      uint16_t sid, uint32_t now)
{
    int i;
    if (!t) return 0;
    for (i = 0; i < t->count; i++) {
        if (t->events[i].caid == caid &&
            t->events[i].provid == provid &&
            t->events[i].sid == sid &&
            !t->events[i].resolved) {
            if ((now - t->events[i].last_detected) <= BLACKSCREEN_WINDOW_SECS) {
                return 1;
            }
        }
    }
    return 0;
}

/* Expire old events */
static inline void blackscreen_expire(struct blackscreen_table *t,
                                       uint32_t now, uint32_t max_age)
{
    int i;
    if (!t) return;
    for (i = 0; i < t->count; i++) {
        if ((now - t->events[i].last_detected) > max_age) {
            t->events[i].resolved = 1;
        }
    }
}

#endif /* MCS_BLACKSCREEN_H */
