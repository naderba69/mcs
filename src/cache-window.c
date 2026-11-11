/*
 * cache-window.c — bounded, opt-in R48 cache wait-window learning.
 *
 * Samples are kept per (profile, CAID) in a fixed LRU table. The target is
 * min(cache-arrival p95, card-response p50 / 2), calculated from the most recent
 * hour. A distribution is usable as soon as it has an observation; requiring a
 * fixed sample count would invent an operator threshold not in D111. The
 * cold-start value is the effective configured timeout (explicit CACHE TIMEOUT
 * or SERVER TIMEOUT before the CAID preset); hourly learned targets use a 1/8
 * EWMA so one noisy update cannot
 * swing the window abruptly. All deadlines use wrap-safe monotonic 32-bit ticks.
 */
#include "cache-window.h"
#include "tick_math.h"

#include <pthread.h>
#include <string.h>

struct r48_window_sample {
    uint32_t value_ms;
    uint32_t at_ms;
};

struct r48_window_slot {
    uint32_t profile_id;
    uint32_t last_activity_ms;
    uint32_t last_update_ms;
    uint32_t learned_ms;
    uint16_t caid;
    uint8_t used;
    uint8_t update_clock_set;
    uint8_t learned_valid;
    uint8_t cache_count;
    uint8_t cache_head;
    uint8_t card_count;
    uint8_t card_head;
    struct r48_window_sample cache[R48_CACHE_WINDOW_SAMPLE_MAX];
    struct r48_window_sample card[R48_CACHE_WINDOW_SAMPLE_MAX];
};

static struct r48_window_slot r48_window_slots[R48_CACHE_WINDOW_SLOTS];
static pthread_mutex_t r48_window_lock = PTHREAD_MUTEX_INITIALIZER;

/* Producers timestamp before taking this lock, so a delayed worker can enter
 * after a newer sample. Because every freshness decision is bounded to one
 * hour, a modulo delta within one hour of 2^32 is an earlier timestamp from
 * that window, not an old sample. Keep those re-ordered events at the slot's
 * latest tick instead of expiring the slot or discarding fresh samples. */
static uint32_t r48_window_elapsed(uint32_t now_ms, uint32_t then_ms)
{
    uint32_t elapsed = tick32_elapsed(now_ms, then_ms);
    if (elapsed > UINT32_MAX - R48_CACHE_WINDOW_PERIOD_MS)
        return 0;
    return elapsed;
}

static uint32_t r48_window_normalize_now(uint32_t now_ms, uint32_t latest_ms)
{
    uint32_t elapsed = tick32_elapsed(now_ms, latest_ms);
    if (elapsed > UINT32_MAX - R48_CACHE_WINDOW_PERIOD_MS)
        return latest_ms;
    return now_ms;
}

static uint32_t r48_window_clamp(uint32_t value, uint32_t ceiling)
{
    uint32_t floor_ms;
    if (!ceiling) ceiling = R48_CACHE_WINDOW_MIN_MS;
    floor_ms = ceiling < R48_CACHE_WINDOW_MIN_MS ?
               ceiling : R48_CACHE_WINDOW_MIN_MS;
    if (value < floor_ms) value = floor_ms;
    if (value > ceiling) value = ceiling;
    return value;
}

/* Caller holds r48_window_lock. Expired slots are reusable facts, not bans. */
static struct r48_window_slot *r48_window_find_locked(uint32_t profile_id,
                                                       uint16_t caid,
                                                       uint32_t now_ms,
                                                       int create)
{
    struct r48_window_slot *free_slot = NULL;
    struct r48_window_slot *oldest = NULL;
    uint32_t oldest_age = 0;
    unsigned i;

    for (i = 0; i < R48_CACHE_WINDOW_SLOTS; i++) {
        struct r48_window_slot *slot = &r48_window_slots[i];
        uint32_t age;
        if (!slot->used) {
            if (!free_slot) free_slot = slot;
            continue;
        }
        age = r48_window_elapsed(now_ms, slot->last_activity_ms);
        if (age > R48_CACHE_WINDOW_PERIOD_MS) {
            memset(slot, 0, sizeof(*slot));
            if (!free_slot) free_slot = slot;
            continue;
        }
        if (slot->profile_id == profile_id && slot->caid == caid)
            return slot;
        if (!oldest || age > oldest_age) {
            oldest = slot;
            oldest_age = age;
        }
    }
    if (!create) return NULL;

    if (!free_slot) free_slot = oldest;
    if (!free_slot) return NULL;
    memset(free_slot, 0, sizeof(*free_slot));
    free_slot->profile_id = profile_id;
    free_slot->caid = caid;
    free_slot->last_activity_ms = now_ms;
    free_slot->last_update_ms = now_ms;
    free_slot->update_clock_set = 1;
    free_slot->used = 1;
    return free_slot;
}

static void r48_window_add_sample(struct r48_window_slot *slot,
                                  struct r48_window_sample *samples,
                                  uint8_t *count, uint8_t *head,
                                  uint32_t elapsed_ms, uint32_t now_ms)
{
    now_ms = r48_window_normalize_now(now_ms, slot->last_activity_ms);
    samples[*head].value_ms = elapsed_ms;
    samples[*head].at_ms = now_ms;
    *head = (uint8_t)((*head + 1u) % R48_CACHE_WINDOW_SAMPLE_MAX);
    if (*count < R48_CACHE_WINDOW_SAMPLE_MAX) (*count)++;
    slot->last_activity_ms = now_ms;
}

static unsigned r48_window_recent_count(const struct r48_window_sample *samples,
                                        uint8_t stored_count, uint32_t now_ms)
{
    unsigned count = 0;
    unsigned i;
    for (i = 0; i < stored_count; i++)
        if (r48_window_elapsed(now_ms, samples[i].at_ms) <= R48_CACHE_WINDOW_PERIOD_MS)
            count++;
    return count;
}

/* Insertion-sort the at-most-64 recent samples; p50/p95 use nearest rank. */
static uint32_t r48_window_percentile(const struct r48_window_sample *samples,
                                      uint8_t stored_count, uint32_t now_ms,
                                      unsigned percentile, unsigned *count_out)
{
    uint32_t values[R48_CACHE_WINDOW_SAMPLE_MAX];
    unsigned count = 0;
    unsigned i, j;
    unsigned rank;

    for (i = 0; i < stored_count; i++) {
        if (r48_window_elapsed(now_ms, samples[i].at_ms) <= R48_CACHE_WINDOW_PERIOD_MS)
            values[count++] = samples[i].value_ms;
    }
    if (count_out) *count_out = count;
    if (!count) return 0;

    for (i = 1; i < count; i++) {
        uint32_t value = values[i];
        j = i;
        while (j && values[j - 1] > value) {
            values[j] = values[j - 1];
            j--;
        }
        values[j] = value;
    }
    rank = (count * percentile + 99u) / 100u;
    if (!rank) rank = 1;
    if (rank > count) rank = count;
    return values[rank - 1];
}

/* Caller holds r48_window_lock. The slot is updated at most once per hour. */
static void r48_window_maybe_update_locked(struct r48_window_slot *slot,
                                            uint32_t now_ms,
                                            uint32_t baseline_ms,
                                            uint32_t ceiling_ms)
{
    unsigned cache_count = 0, card_count = 0;
    uint32_t cache_p95, card_p50, target;

    if (!slot->update_clock_set) {
        slot->last_update_ms = now_ms;
        slot->update_clock_set = 1;
        return;
    }
    if (r48_window_elapsed(now_ms, slot->last_update_ms) < R48_CACHE_WINDOW_PERIOD_MS)
        return;
    slot->last_update_ms = now_ms;

    cache_p95 = r48_window_percentile(slot->cache, slot->cache_count,
                                      now_ms, 95u, &cache_count);
    card_p50 = r48_window_percentile(slot->card, slot->card_count,
                                     now_ms, 50u, &card_count);
    if (!cache_count || !card_count) {
        /* Do not keep applying an estimate after its supporting window has
         * fallen below the evidence floor. The next valid update relearns it. */
        slot->learned_valid = 0;
        return;
    }

    target = cache_p95;
    if (card_p50 / 2u < target) target = card_p50 / 2u;
    target = r48_window_clamp(target, ceiling_ms);
    if (!slot->learned_valid) {
        /* The effective profile/preset baseline is the first EWMA state, not an
         * immediate jump to a noisy first measurement. */
        slot->learned_ms = baseline_ms;
        slot->learned_valid = 1;
    }
    /* Alpha=1/8: bounded hourly EWMA, rounded to the nearest ms. */
    slot->learned_ms = (uint32_t)(((uint64_t)slot->learned_ms * 7u +
                                   target + 4u) / 8u);
}

void r48_cache_window_reset(void)
{
    pthread_mutex_lock(&r48_window_lock);
    memset(r48_window_slots, 0, sizeof(r48_window_slots));
    pthread_mutex_unlock(&r48_window_lock);
}

void r48_cache_window_note_cache(uint32_t profile_id, uint16_t caid,
                                 uint32_t elapsed_ms, uint32_t now_ms)
{
    struct r48_window_slot *slot;
    if (!caid) return;
    pthread_mutex_lock(&r48_window_lock);
    slot = r48_window_find_locked(profile_id, caid, now_ms, 1);
    if (slot)
        r48_window_add_sample(slot, slot->cache, &slot->cache_count,
                              &slot->cache_head, elapsed_ms, now_ms);
    pthread_mutex_unlock(&r48_window_lock);
}

void r48_cache_window_note_card(uint32_t profile_id, uint16_t caid,
                                uint32_t elapsed_ms, uint32_t now_ms)
{
    struct r48_window_slot *slot;
    if (!caid || !elapsed_ms) return; /* zero is not a physical card sample */
    pthread_mutex_lock(&r48_window_lock);
    slot = r48_window_find_locked(profile_id, caid, now_ms, 1);
    if (slot)
        r48_window_add_sample(slot, slot->card, &slot->card_count,
                              &slot->card_head, elapsed_ms, now_ms);
    pthread_mutex_unlock(&r48_window_lock);
}

uint32_t r48_cache_window_get_timeout(uint32_t profile_id, uint16_t caid,
                                      uint32_t baseline_ms, uint32_t ceiling_ms,
                                      uint32_t now_ms, int enabled)
{
    struct r48_window_slot *slot;
    uint32_t ceiling, baseline, result;
    unsigned cache_count = 0, card_count = 0;

    if (!baseline_ms) baseline_ms = R48_CACHE_WINDOW_MIN_MS;
    if (!enabled || !caid) return baseline_ms;

    ceiling = ceiling_ms ? ceiling_ms : baseline_ms;
    if (!ceiling) ceiling = R48_CACHE_WINDOW_MIN_MS;
    baseline = r48_window_clamp(baseline_ms, ceiling);

    pthread_mutex_lock(&r48_window_lock);
    slot = r48_window_find_locked(profile_id, caid, now_ms, 0);
    if (!slot) {
        pthread_mutex_unlock(&r48_window_lock);
        return baseline;
    }
    now_ms = r48_window_normalize_now(now_ms, slot->last_activity_ms);
    r48_window_maybe_update_locked(slot, now_ms, baseline, ceiling);
    cache_count = r48_window_recent_count(slot->cache, slot->cache_count,
                                          now_ms);
    card_count = r48_window_recent_count(slot->card, slot->card_count,
                                         now_ms);
    result = baseline;
    if (!cache_count || !card_count)
        slot->learned_valid = 0;
    else if (slot->learned_valid)
        result = r48_window_clamp(slot->learned_ms, ceiling);
    pthread_mutex_unlock(&r48_window_lock);
    return result;
}
