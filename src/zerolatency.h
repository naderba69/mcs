/* zerolatency.h — Zero-Latency CW Delivery (R22 candidate #23)
 *
 * PURPOSE: minimize CW delivery latency by pre-positioning CWs at the
 * client before they are needed. When a CW change is detected, the new
 * CW is already available at the client, eliminating the gap.
 *
 * MECHANISM: predict CW change timing based on channel's CW cycle
 * pattern. Send the next CW to the client 100-200ms before the current
 * CW expires. Client buffers the CW and activates it at the right time.
 *
 * CONFIG: ZEROLATENCY_LEAD_MS (default 150) — how early to send CW.
 *
 * ROLLBACK: remove zerolatency_check() calls; delivery is reactive.
 */
#ifndef MCS_ZEROLATENCY_H
#define MCS_ZEROLATENCY_H

#include <stdint.h>
#include <string.h>

#define ZEROLATENCY_CHANNELS 64
#define ZEROLATENCY_LEAD_MS  150  /* send CW 150ms before expiry */
#define ZEROLATENCY_HISTORY  8    /* track last 8 CW changes */

struct zerolatency_channel {
    uint16_t caid;
    uint32_t provid;
    uint16_t sid;
    uint32_t changes[ZEROLATENCY_HISTORY];  /* timestamps of CW changes */
    int change_count;
    int change_head;
    uint32_t last_change;
    uint32_t predicted_next;  /* predicted timestamp of next change */
    uint8_t prepositioned;    /* 1 = next CW already sent to client */
    uint8_t used;
};

struct zerolatency_table {
    struct zerolatency_channel channels[ZEROLATENCY_CHANNELS];
};

static inline void zerolatency_init(struct zerolatency_table *t)
{
    if (!t) return;
    memset(t, 0, sizeof(*t));
}

static inline struct zerolatency_channel *
zerolatency_find(struct zerolatency_table *t, uint16_t caid,
                 uint32_t provid, uint16_t sid)
{
    int i;
    if (!t) return 0;
    for (i = 0; i < ZEROLATENCY_CHANNELS; i++) {
        struct zerolatency_channel *c = &t->channels[i];
        if (c->used && c->caid == caid && c->provid == provid && c->sid == sid) {
            return c;
        }
    }
    return 0;
}

static inline struct zerolatency_channel *
zerolatency_alloc(struct zerolatency_table *t, uint16_t caid,
                  uint32_t provid, uint16_t sid)
{
    int i, oldest = 0;
    struct zerolatency_channel *c;

    c = zerolatency_find(t, caid, provid, sid);
    if (c) return c;

    for (i = 0; i < ZEROLATENCY_CHANNELS; i++) {
        if (!t->channels[i].used) {
            c = &t->channels[i];
            c->caid = caid;
            c->provid = provid;
            c->sid = sid;
            c->used = 1;
            return c;
        }
        if (t->channels[i].last_change < t->channels[oldest].last_change) {
            oldest = i;
        }
    }

    /* Evict oldest */
    c = &t->channels[oldest];
    memset(c, 0, sizeof(*c));
    c->caid = caid;
    c->provid = provid;
    c->sid = sid;
    c->used = 1;
    return c;
}

/* Record a CW change and update prediction.
 * Call this every time a new CW is delivered for a channel.
 */
static inline void zerolatency_record(struct zerolatency_table *t,
                                      uint16_t caid, uint32_t provid,
                                      uint16_t sid, uint32_t now)
{
    struct zerolatency_channel *c;
    uint32_t total_interval = 0;
    int intervals = 0;
    int i;

    if (!t) return;

    c = zerolatency_alloc(t, caid, provid, sid);
    if (!c) return;

    /* Record change timestamp */
    c->changes[c->change_head] = now;
    c->change_head = (c->change_head + 1) % ZEROLATENCY_HISTORY;
    if (c->change_count < ZEROLATENCY_HISTORY) c->change_count++;

    /* Calculate average cycle time */
    if (c->change_count >= 2) {
        for (i = 1; i < c->change_count; i++) {
            int prev = (c->change_head - c->change_count + i - 1 + ZEROLATENCY_HISTORY) % ZEROLATENCY_HISTORY;
            int curr = (c->change_head - c->change_count + i + ZEROLATENCY_HISTORY) % ZEROLATENCY_HISTORY;
            if (c->changes[curr] > c->changes[prev]) {
                total_interval += (c->changes[curr] - c->changes[prev]);
                intervals++;
            }
        }
    }

    /* Predict next change */
    if (intervals > 0 && c->change_count >= 2) {
        uint32_t avg_cycle = total_interval / (uint32_t)intervals;
        int last_idx = (c->change_head - 1 + ZEROLATENCY_HISTORY) % ZEROLATENCY_HISTORY;
        c->predicted_next = c->changes[last_idx] + avg_cycle;
    } else {
        c->predicted_next = 0;
    }

    c->last_change = now;
    c->prepositioned = 0;  /* Reset: new CW delivered, next one not yet sent */
}

/* Check if it's time to pre-position the next CW.
 * Returns: 1 = time to send next CW to client, 0 = not yet.
 *
 * Call this periodically (e.g., every 100ms) to check if we should
 * send the next CW to the client before the current one expires.
 */
static inline int zerolatency_check(struct zerolatency_table *t,
                                    uint16_t caid, uint32_t provid,
                                    uint16_t sid, uint32_t now)
{
    struct zerolatency_channel *c;
    uint32_t lead_time_ms;

    if (!t) return 0;

    c = zerolatency_find(t, caid, provid, sid);
    if (!c || c->predicted_next == 0) return 0;
    if (c->prepositioned) return 0;  /* Already sent */

    /* Check if we're within ZEROLATENCY_LEAD_MS of predicted change */
    lead_time_ms = ZEROLATENCY_LEAD_MS;
    if ((now * 1000) + lead_time_ms >= (c->predicted_next * 1000)) {
        return 1;  /* Time to pre-position */
    }

    return 0;
}

/* Mark that the next CW has been pre-positioned (sent to client).
 * Call this after successfully sending the next CW.
 */
static inline void zerolatency_mark(struct zerolatency_table *t,
                                    uint16_t caid, uint32_t provid,
                                    uint16_t sid)
{
    struct zerolatency_channel *c;
    if (!t) return;
    c = zerolatency_find(t, caid, provid, sid);
    if (c) c->prepositioned = 1;
}

/* Get predicted time until next CW change (in seconds).
 * Returns: seconds until next change, or 0 if no prediction.
 */
static inline uint32_t zerolatency_time_remaining(struct zerolatency_table *t,
                                                  uint16_t caid,
                                                  uint32_t provid,
                                                  uint16_t sid,
                                                  uint32_t now)
{
    struct zerolatency_channel *c;
    if (!t) return 0;
    c = zerolatency_find(t, caid, provid, sid);
    if (!c || c->predicted_next == 0) return 0;
    if (now >= c->predicted_next) return 0;
    return c->predicted_next - now;
}

#endif /* MCS_ZEROLATENCY_H */
