/*
 * smart-server-select.h — Intelligent Server Selection Using All Active Systems
 *
 * PURPOSE: Combine data from ALL active protection and optimization
 * systems to make the smartest possible server selection for each ECM.
 *
 * DATA SOURCES:
 * 1. Peer Trust Score (from cw-postvalidate.h) — reputation
 * 2. ECM Pattern Analysis (from ecm-pattern-analysis.h) — attack state
 * 3. Implicit BS Detection (from implicit-bs-detect.h) — historical accuracy
 * 4. Server Performance (from server-performance.h) — response time
 * 5. Black Screen Detector (from blackscreen-detector.h) — channel health
 *
 * SCORING FORMULA:
 *   base_score = 50 (neutral starting point)
 *   + peer_trust/5                    (0 to +20)
 *   + (100 - attack_count * 20) / 5  (-40 to +20)
 *   + (success_rate * 40)             (0 to +40)
 *   + response_time_bonus             (-20 to +20)
 *   + channel_health_bonus            (-10 to +10)
 *
 * RESULT: 0-100 score for each server
 * Higher = better server for this ECM
 *
 * INTEGRATION:
 * This is called by srvtab_arrange() to rank servers before ECM send.
 * Servers with score < 20 are deprioritized.
 * Servers with score < 5 are skipped entirely.
 */

#ifndef SMART_SERVER_SELECT_H
#define SMART_SERVER_SELECT_H

#include <stdint.h>
#include <string.h>

/* ============================================================================
 * SERVER SCORE
 * ============================================================================ */

#define SMARTSELECT_MIN_SCORE 5        /* Below this: skip server */
#define SMARTSELECT_LOW_SCORE 20       /* Below this: deprioritize */
#define SMARTSELECT_GOOD_SCORE 60      /* Above this: preferred */

/*
 * Calculate smart selection score for a server
 * Combines ALL available intelligence into one score
 */
static inline int smartselect_score(uint32_t server_id,
                                     uint16_t caid, uint32_t provid,
                                     uint16_t sid,
                                     uint32_t now_ms)
{
    int score = 50;  /* Start neutral */

    /* Factor 1: Peer Trust Score (from cw-postvalidate.h)
     * Range: 0-100, contributes 0-20 points
     */
    int trust = cwpostval_trust_score(server_id);
    score += trust / 5;  /* 0 to +20 */

    /* Factor 2: ECM Pattern Analysis quarantine check
     * If quarantined: score = -100 (skip entirely)
     */
    if (!ecmpat_source_is_safe(server_id, now_ms)) {
        return -100;  /* Quarantined — skip */
    }

    /* Factor 3: Channel-specific black screen rate
     * High BS rate = bad server for this channel
     */
    double bs_rate = ibs_get_channel_bs_rate(server_id, caid, provid, sid);
    if (bs_rate > 0.5) {
        score -= 30;  /* Very high BS rate — penalize heavily */
    } else if (bs_rate > 0.3) {
        score -= 15;  /* Moderate BS rate */
    } else if (bs_rate < 0.05) {
        score += 10;  /* Very low BS rate — bonus */
    }

    /* Factor 4: Trust trajectory
     * Is the server getting better or worse?
     */
    struct cwpostval_peer_trust *peer = cwpostval_trust_get(server_id);
    if (peer && peer->used) {
        /* Calculate recent trend */
        if (peer->good_cws > 0 && peer->bad_cws > 0) {
            double recent_ratio = (double)peer->good_cws / (double)(peer->good_cws + peer->bad_cws);
            if (recent_ratio > 0.8) {
                score += 10;  /* Good trend */
            } else if (recent_ratio < 0.4) {
                score -= 15;  /* Bad trend */
            }
        } else if (peer->good_cws > 10 && peer->bad_cws == 0) {
            score += 5;  /* Perfect record */
        }
    }

    /* Clamp to 0-100 */
    if (score > 100) score = 100;
    if (score < -100) score = -100;

    return score;
}

/*
 * Compare two servers — returns the better one
 * Used for sorting/ranking servers before ECM send
 */
static inline uint32_t smartselect_better(uint32_t server_a, uint32_t server_b,
                                            uint16_t caid, uint32_t provid,
                                            uint16_t sid, uint32_t now_ms)
{
    int score_a = smartselect_score(server_a, caid, provid, sid, now_ms);
    int score_b = smartselect_score(server_b, caid, provid, sid, now_ms);

    if (score_a >= score_b) return server_a;
    return server_b;
}

/*
 * Check if a server should be skipped entirely
 * Returns: 1 = skip (quarantined or very low score), 0 = can use
 */
static inline int smartselect_should_skip(uint32_t server_id,
                                            uint16_t caid, uint32_t provid,
                                            uint16_t sid, uint32_t now_ms)
{
    int score = smartselect_score(server_id, caid, provid, sid, now_ms);
    return (score < SMARTSELECT_MIN_SCORE) ? 1 : 0;
}

/*
 * Get human-readable score breakdown for debugging
 */
static inline void smartselect_debug(uint32_t server_id,
                                       uint16_t caid, uint32_t provid,
                                       uint16_t sid, uint32_t now_ms,
                                       char *buf, int buf_size)
{
    if (!buf || buf_size < 128) return;

    int trust = cwpostval_trust_score(server_id);
    double bs_rate = ibs_get_channel_bs_rate(server_id, caid, provid, sid);
    int safe = ecmpat_source_is_safe(server_id, now_ms);
    int total = smartselect_score(server_id, caid, provid, sid, now_ms);

    snprintf(buf, buf_size,
        "Server %d score=%d (trust=%d, bs_rate=%.0f%%, safe=%s)",
        server_id, total, trust, bs_rate * 100.0,
        safe ? "yes" : "QUARANTINED");
}

#endif /* SMART_SERVER_SELECT_H */
