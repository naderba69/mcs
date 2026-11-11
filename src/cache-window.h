/*
 * cache-window.h — R48 opt-in cache wait-window estimator.
 *
 * A nonzero per-profile CACHE TIMEOUT is authoritative; otherwise the CAID
 * preset is the baseline. Once a profile explicitly arms CACHE EVIDENCE,
 * successful cache-to-live-ECM handoff times (irrespective of the candidate's
 * original source) and accepted line-server response times feed recent p95/p50
 * distributions. Learning starts from that baseline. Its lower bound is 60 ms
 * unless a smaller operator ceiling is configured, in which case the ceiling
 * remains authoritative. With the option OFF, the effective configured timeout
 * is returned unchanged. This module owns only bounded measurements; it does
 * not make cache acceptance or source-trust decisions.
 */
#ifndef MCS_CACHE_WINDOW_H
#define MCS_CACHE_WINDOW_H

#include <stdint.h>

#define R48_CACHE_WINDOW_MIN_MS       60u
#define R48_CACHE_WINDOW_PERIOD_MS    3600000u
#define R48_CACHE_WINDOW_SAMPLE_MAX   64u
#define R48_CACHE_WINDOW_SLOTS        128u

/* A configured profile CACHE TIMEOUT is more specific than SERVER TIMEOUT;
 * either explicit operator value overrides the CAID preset (D111 A1). Zero
 * means that no usable nonzero operator timeout is configured. */
static inline uint32_t r48_cache_window_configured_ms(uint32_t caid_preset_ms,
                                                       uint32_t cache_timeout_ms,
                                                       int cache_timeout_explicit,
                                                       uint32_t server_timeout_ms,
                                                       int server_timeout_explicit)
{
    if (cache_timeout_explicit && cache_timeout_ms) return cache_timeout_ms;
    if (server_timeout_explicit && server_timeout_ms) return server_timeout_ms;
    if (caid_preset_ms) return caid_preset_ms;
    return R48_CACHE_WINDOW_MIN_MS;
}

/* The first ECM worker wake must preserve the historical immediate check when
 * CACHE TIMEOUT is zero, but an armed learner may move a positive initial wait
 * earlier. It can never delay the operator's existing schedule. */
static inline uint32_t r48_cache_window_initial_schedule_ms(
        uint32_t configured_ms, uint32_t learned_ms, int enabled)
{
    if (!enabled || !configured_ms) return configured_ms;
    return learned_ms < configured_ms ? learned_ms : configured_ms;
}

/* Compatibility/helper form for direct tests and callers with only CACHE
 * TIMEOUT available. */
static inline uint32_t r48_cache_window_baseline_ms(uint32_t caid_preset_ms,
                                                     uint32_t profile_timeout_ms)
{
    return r48_cache_window_configured_ms(caid_preset_ms, profile_timeout_ms,
                                          profile_timeout_ms != 0, 0, 0);
}

/* `now_ms` is the process monotonic tick (GetTickCount-compatible). */
void r48_cache_window_reset(void);
void r48_cache_window_note_cache(uint32_t profile_id, uint16_t caid,
                                 uint32_t elapsed_ms, uint32_t now_ms);
void r48_cache_window_note_card(uint32_t profile_id, uint16_t caid,
                                uint32_t elapsed_ms, uint32_t now_ms);
uint32_t r48_cache_window_get_timeout(uint32_t profile_id, uint16_t caid,
                                      uint32_t baseline_ms, uint32_t ceiling_ms,
                                      uint32_t now_ms, int enabled);

#endif /* MCS_CACHE_WINDOW_H */
