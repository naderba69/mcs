/* cwconfidence.h — CW Confidence Scoring (R22 candidate #7)
 *
 * PURPOSE: assign a dynamic confidence score (0-100) to each CW based
 * on multiple factors: source reputation, age, verification status,
 * channel history. This allows intelligent CW selection when multiple
 * CWs are available for the same channel.
 *
 * MECHANISM: weighted scoring formula:
 *   score = base_score
 *         + source_reputation_bonus
 *         + verification_bonus
 *         - age_penalty
 *         - channel_failure_penalty
 *
 * CONFIG: automatic — scores are computed for every CW offer.
 *
 * ROLLBACK: remove cwconfidence_score() calls; scoring is read-only.
 */
#ifndef MCS_CWCONFIDENCE_H
#define MCS_CWCONFIDENCE_H

#include <stdint.h>

/* Confidence score components */
#define CWCONF_BASE_SCORE          50
#define CWCONF_VERIFIED_BONUS      30
#define CWCONF_GOOD_SOURCE_BONUS   20
#define CWCONF_MAX_AGE_PENALTY     40
#define CWCONF_FAILURE_PENALTY     25
#define CWCONF_MAX_SCORE          100
#define CWCONF_MIN_SCORE            0

struct cwconfidence_factors {
    int base;              /* base score (always 50) */
    int source_bonus;      /* source reputation bonus (0-20) */
    int verified_bonus;    /* verification bonus (0 or 30) */
    int age_penalty;       /* age penalty (0-40) */
    int failure_penalty;   /* channel failure penalty (0-25) */
    int final_score;       /* final score (0-100) */
};

/* Compute confidence score for a CW.
 *
 * source_reputation: 0-100 (from trustagg/trust layer)
 * verified: 1 if CW passed cwverify, 0 otherwise
 * age_secs: seconds since CW was generated
 * max_age_secs: maximum acceptable age
 * channel_failures: recent failures on this channel
 * max_failures: failure count that triggers max penalty
 *
 * Returns: confidence score 0-100
 */
static inline int cwconfidence_score(int source_reputation,
                                      int verified,
                                      uint32_t age_secs,
                                      uint32_t max_age_secs,
                                      int channel_failures,
                                      int max_failures,
                                      struct cwconfidence_factors *factors)
{
    int score = CWCONF_BASE_SCORE;
    int source_bonus = 0;
    int verified_bonus = 0;
    int age_penalty = 0;
    int failure_penalty = 0;

    /* Source reputation bonus (0-20) */
    if (source_reputation > 0) {
        source_bonus = (source_reputation * CWCONF_GOOD_SOURCE_BONUS) / 100;
    }
    score += source_bonus;

    /* Verification bonus (0 or 30) */
    if (verified) {
        verified_bonus = CWCONF_VERIFIED_BONUS;
        score += verified_bonus;
    }

    /* Age penalty (0-40, linear) */
    if (max_age_secs > 0 && age_secs > 0) {
        if (age_secs >= max_age_secs) {
            age_penalty = CWCONF_MAX_AGE_PENALTY;
        } else {
            age_penalty = (int)((age_secs * CWCONF_MAX_AGE_PENALTY) / max_age_secs);
        }
        score -= age_penalty;
    }

    /* Channel failure penalty (0-25, linear) */
    if (max_failures > 0 && channel_failures > 0) {
        if (channel_failures >= max_failures) {
            failure_penalty = CWCONF_FAILURE_PENALTY;
        } else {
            failure_penalty = (channel_failures * CWCONF_FAILURE_PENALTY) / max_failures;
        }
        score -= failure_penalty;
    }

    /* Clamp to [0, 100] */
    if (score > CWCONF_MAX_SCORE) score = CWCONF_MAX_SCORE;
    if (score < CWCONF_MIN_SCORE) score = CWCONF_MIN_SCORE;

    if (factors) {
        factors->base = CWCONF_BASE_SCORE;
        factors->source_bonus = source_bonus;
        factors->verified_bonus = verified_bonus;
        factors->age_penalty = age_penalty;
        factors->failure_penalty = failure_penalty;
        factors->final_score = score;
    }

    return score;
}

/* Compare two CWs by confidence score.
 * Returns: 1 if a is better, -1 if b is better, 0 if equal.
 */
static inline int cwconfidence_compare(int score_a, int score_b)
{
    if (score_a > score_b) return 1;
    if (score_a < score_b) return -1;
    return 0;
}

/* Check if a CW meets minimum confidence threshold */
static inline int cwconfidence_acceptable(int score, int min_threshold)
{
    return score >= min_threshold;
}

#endif /* MCS_CWCONFIDENCE_H */
