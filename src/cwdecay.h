/* cwdecay.h — Time-Decay Trust (R22 candidate #9)
 *
 * PURPOSE: trust in a source decays over time if not reinforced. A
 * source that was good yesterday but hasn't delivered anything today
 * should not be trusted as much as a source that delivered successfully
 * 5 minutes ago.
 *
 * MECHANISM: exponential decay of trust score based on time since last
 * successful delivery. Half-life is configurable.
 *
 * CONFIG: automatic — applied to all trust scores.
 *
 * ROLLBACK: remove cwdecay_apply() calls; decay is inert (returns
 * original score).
 */
#ifndef MCS_CWDECAY_H
#define MCS_CWDECAY_H

#include <stdint.h>

#define CWDECAY_DEFAULT_HALFLIFE 3600  /* 1 hour */
#define CWDECAY_MIN_SCORE 10           /* never decay below this */

/* Apply time decay to a trust score.
 *
 * original_score: 0-100
 * last_success: timestamp of last successful delivery
 * now: current timestamp
 * halflife_secs: seconds for score to decay by 50%
 *
 * Returns: decayed score (0-100)
 */
static inline int cwdecay_apply(int original_score, uint32_t last_success,
                                 uint32_t now, uint32_t halflife_secs)
{
    uint32_t elapsed;
    int halvings;
    int decayed;
    int i;

    if (halflife_secs == 0) return original_score;
    if (last_success == 0) return CWDECAY_MIN_SCORE;
    if (now <= last_success) return original_score;

    elapsed = now - last_success;
    halvings = (int)(elapsed / halflife_secs);

    if (halvings == 0) return original_score;
    if (halvings > 10) return CWDECAY_MIN_SCORE;  /* fully decayed */

    /* Apply exponential decay: score = score / 2^halvings */
    decayed = original_score;
    for (i = 0; i < halvings; i++) {
        decayed = decayed / 2;
        if (decayed < CWDECAY_MIN_SCORE) {
            return CWDECAY_MIN_SCORE;
        }
    }
    return decayed;
}

/* Compute the "freshness" factor (0.0 to 1.0) based on time since last success.
 * 1.0 = just delivered, 0.0 = very old.
 */
static inline double cwdecay_freshness(uint32_t last_success, uint32_t now,
                                        uint32_t halflife_secs)
{
    uint32_t elapsed;
    double factor;

    if (halflife_secs == 0) return 1.0;
    if (last_success == 0) return 0.0;
    if (now <= last_success) return 1.0;

    elapsed = now - last_success;
    /* freshness = 0.5^(elapsed/halflife) */
    /* Approximate with linear for simplicity: freshness = 1 - elapsed/(2*halflife) */
    if (elapsed >= 2 * halflife_secs) return 0.0;
    factor = 1.0 - ((double)elapsed / (double)(2 * halflife_secs));
    return factor;
}

/* Check if a source is "stale" (no recent deliveries) */
static inline int cwdecay_is_stale(uint32_t last_success, uint32_t now,
                                    uint32_t max_age_secs)
{
    if (last_success == 0) return 1;
    return ((now - last_success) > max_age_secs) ? 1 : 0;
}

#endif /* MCS_CWDECAY_H */
