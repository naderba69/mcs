#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "../src/cache-window.h"

int main(void)
{
    const uint32_t profile = 7;
    const uint16_t caid = 0x1814;
    const uint32_t preset = 280;
    uint32_t now = 1000;
    unsigned i;
    uint32_t learned;

    r48_cache_window_reset();

    /* D111 A1 precedence: explicit CACHE TIMEOUT is most specific, then
     * explicit SERVER TIMEOUT, then the CAID preset. */
    assert(r48_cache_window_configured_ms(preset, 400, 1, 1200, 1) == 400);
    assert(r48_cache_window_configured_ms(preset, 0, 0, 1200, 1) == 1200);
    assert(r48_cache_window_configured_ms(preset, 0, 0, 0, 0) == preset);
    assert(r48_cache_window_configured_ms(preset, 400, 0, 0, 0) == preset);

    /* No unsourced sample-count threshold: one observation in each real
     * distribution is sufficient to produce a bounded hourly EWMA update. */
    {
        const uint32_t one_sample_profile = profile + 2;
        r48_cache_window_note_cache(one_sample_profile, caid, 100, now);
        r48_cache_window_note_card(one_sample_profile, caid, 400, now);
        assert(r48_cache_window_get_timeout(one_sample_profile, caid, preset,
                                            preset, now + 3600000u, 1) == 258);
    }

    /* Concurrent producers timestamp before taking the module lock. A sample
     * or query that acquires it out of timestamp order must not expire the
     * profile slot or erase the fresher sample pair. */
    {
        const uint32_t reordered_profile = profile + 3;
        const uint32_t first = 5000;
        r48_cache_window_reset();
        r48_cache_window_note_cache(reordered_profile, caid, 100, first);
        r48_cache_window_note_card(reordered_profile, caid, 400, first + 2u);
        r48_cache_window_note_cache(reordered_profile, caid, 102, first + 1u);
        assert(r48_cache_window_get_timeout(reordered_profile, caid, preset,
                                            500, first + 1u, 1) == preset);
        assert(r48_cache_window_get_timeout(reordered_profile, caid, preset,
                                            500, first + 3600002u, 1) == 258);
    }

    /* A1: nonzero profile CACHE TIMEOUT overrides the CAID preset. This
     * applies with learning OFF too; the estimator must not silently undo the
     * delivered 1814 profile's explicit 400 ms value. */
    {
        const uint32_t explicit_profile = profile + 1;
        const uint32_t explicit_timeout = 400;
        uint32_t baseline = r48_cache_window_baseline_ms(preset,
                                                          explicit_timeout);
        assert(baseline == 400);
        assert(r48_cache_window_baseline_ms(preset, 0) == preset);
        assert(r48_cache_window_baseline_ms(preset, 240) == 240);
        assert(r48_cache_window_get_timeout(explicit_profile, caid, baseline,
                                            explicit_timeout, now, 0) == 400);
        assert(r48_cache_window_get_timeout(explicit_profile, caid, baseline,
                                            explicit_timeout, now, 1) == 400);

        /* The first evidence-backed update is seeded from the profile value,
         * then moves only 1/8 toward the measured target. */
        for (i = 0; i < 20; i++) {
            r48_cache_window_note_cache(explicit_profile, caid, 100 + i,
                                        now + i);
            r48_cache_window_note_card(explicit_profile, caid, 400 + i,
                                       now + i);
        }
        assert(r48_cache_window_get_timeout(explicit_profile, caid, baseline,
                                            explicit_timeout,
                                            now + 3600000u, 1) == 365);
    }

    /* Default-off returns the already-resolved timeout, independent of a
     * ceiling argument that belongs only to the opt-in learner. */
    assert(r48_cache_window_get_timeout(profile, caid, preset, 100,
                                        now, 0) == 280);

    /* An armed estimate may shorten a positive first wake, but must not delay
     * the legacy immediate wake used by a zero CACHE TIMEOUT or raise the
     * operator's existing initial schedule. */
    assert(r48_cache_window_initial_schedule_ms(400, 365, 1) == 365);
    assert(r48_cache_window_initial_schedule_ms(400, 500, 1) == 400);
    assert(r48_cache_window_initial_schedule_ms(400, 365, 0) == 400);
    assert(r48_cache_window_initial_schedule_ms(0, 365, 1) == 0);

    /* Opt-in cold start uses the effective profile/preset baseline, constrained
     * by the operator ceiling. */
    assert(r48_cache_window_get_timeout(profile, caid, preset, 0,
                                        now, 1) == 280);
    assert(r48_cache_window_get_timeout(profile, caid, preset, 240,
                                        now, 1) == 240);
    assert(r48_cache_window_get_timeout(profile, caid, preset, 500,
                                        now, 1) == 280);
    /* A valid explicit sub-60ms CACHE TIMEOUT remains a hard ceiling. The
     * 60ms estimator floor applies only when the operator ceiling permits it. */
    assert(r48_cache_window_get_timeout(profile, caid, preset, 30,
                                        now, 1) == 30);
    assert(r48_cache_window_get_timeout(profile, caid, 30, 30,
                                        now, 1) == 30);
    {
        const uint32_t capped_profile = profile + 4;
        r48_cache_window_note_cache(capped_profile, caid, 20, now);
        r48_cache_window_note_card(capped_profile, caid, 100, now);
        assert(r48_cache_window_get_timeout(capped_profile, caid, preset, 30,
                                            now + 3600000u, 1) == 30);
    }

    /* Both distributions are required; one source cannot teach the window. */
    for (i = 0; i < 20; i++)
        r48_cache_window_note_cache(profile, caid, 100 + i, now + i);
    assert(r48_cache_window_get_timeout(profile, caid, preset, 500,
                                        now + 3600000u, 1) == 280);

    /* Target is cache p95 (118ms); the first update moves 1/8 from preset. */
    for (i = 0; i < 20; i++) {
        r48_cache_window_note_cache(profile, caid, 100 + i,
                                    now + 3601000u + i);
        r48_cache_window_note_card(profile, caid, 400 + i,
                                   now + 3601000u + i);
    }
    learned = r48_cache_window_get_timeout(profile, caid, preset, 500,
                                           now + 7201000u, 1);
    assert(learned == 260);

    /* Hourly update is not a per-ECM twitch; its 1/8 EWMA damps a new target. */
    for (i = 0; i < 20; i++) {
        r48_cache_window_note_cache(profile, caid, 300 + i,
                                    now + 7201001u + i);
        r48_cache_window_note_card(profile, caid, 800 + i,
                                   now + 7201001u + i);
    }
    learned = r48_cache_window_get_timeout(profile, caid, preset, 500,
                                           now + 10801000u, 1);
    assert(learned == 267);
    assert(r48_cache_window_get_timeout(profile, caid, preset, 130,
                                        now + 10801000u, 1) == 130);

    /* A stale estimate is discarded when one hourly evidence window is thin. */
    {
        uint32_t first_update = now + 3600000u;
        uint32_t second_update = first_update + 3600000u;
        r48_cache_window_reset();
        for (i = 0; i < 20; i++) {
            r48_cache_window_note_cache(profile, caid, 100 + i, now + i);
            r48_cache_window_note_card(profile, caid, 400 + i, now + i);
        }
        assert(r48_cache_window_get_timeout(profile, caid, preset, 500,
                                            first_update, 1) == 260);
        for (i = 0; i < 20; i++)
            r48_cache_window_note_cache(profile, caid, 100 + i, first_update);
        assert(r48_cache_window_get_timeout(profile, caid, preset, 500,
                                            second_update, 1) == 280);
        for (i = 0; i < 20; i++)
            r48_cache_window_note_card(profile, caid, 400 + i,
                                       second_update + 1u + i);
        assert(r48_cache_window_get_timeout(profile, caid, preset, 500,
                                            second_update + 21u, 1) == 280);
    }

    /* A different profile or CAID never inherits this learned window. */
    assert(r48_cache_window_get_timeout(profile + 1, caid, preset, 500,
                                        now + 10801000u, 1) == 280);
    assert(r48_cache_window_get_timeout(profile, 0x0500, 250, 500,
                                        now + 10801000u, 1) == 250);

    /* Once samples age out, the learned fact is discarded and the preset wins. */
    assert(r48_cache_window_get_timeout(profile, caid, preset, 500,
                                        now + 10802020u, 1) == 280);

    /* The fixed ring keeps only the latest 64 observations. */
    r48_cache_window_reset();
    for (i = 0; i < 80; i++) {
        r48_cache_window_note_cache(profile, caid, 100 + i, now + i);
        r48_cache_window_note_card(profile, caid, 500 + i, now + i);
    }
    assert(r48_cache_window_get_timeout(profile, caid, preset, 500,
                                        now + 3600000u, 1) == 267);

    /* Monotonic 32-bit tick wrap does not make fresh samples look stale. */
    {
        uint32_t near_wrap = UINT32_MAX - 10000u;
        r48_cache_window_reset();
        for (i = 0; i < 20; i++) {
            r48_cache_window_note_cache(profile, caid, 100 + i,
                                        near_wrap + i);
            r48_cache_window_note_card(profile, caid, 400 + i,
                                       near_wrap + i);
        }
        assert(r48_cache_window_get_timeout(profile, caid, preset, 500,
                                            near_wrap + 3600000u, 1) == 260);
    }

    /* The fixed profile/CAID table evicts its least-recently-active slot. */
    {
        uint32_t learned_at = now + 3600000u;
        r48_cache_window_reset();
        for (i = 0; i < 20; i++) {
            r48_cache_window_note_cache(profile, caid, 100 + i, now + i);
            r48_cache_window_note_card(profile, caid, 400 + i, now + i);
        }
        assert(r48_cache_window_get_timeout(profile, caid, preset, 500,
                                            learned_at, 1) == 260);
        r48_cache_window_note_cache(profile, caid, 100, learned_at);
        for (i = 1; i <= R48_CACHE_WINDOW_SLOTS; i++)
            r48_cache_window_note_cache(profile + i, caid, 100,
                                        learned_at + i);
        assert(r48_cache_window_get_timeout(profile, caid, preset, 500,
                                            learned_at + R48_CACHE_WINDOW_SLOTS + 1,
                                            1) == 280);
    }

    r48_cache_window_reset();
    puts("PASS: R48 cache window is opt-in, CAID/profile scoped, bounded, hourly and conservative");
    return 0;
}
