/* latecw.c -- Band 59 regression for LATECW (CW arriving after the ECM
 * has already waited past the threshold).
 *
 * Drives the real supersmartfilter_check() with every other SSF check
 * disabled:
 *   - age below the threshold passes in every mode;
 *   - age above flags in log mode (accepted) and rejects with reason
 *     13 in mode 2;
 *   - unknown age (UINT32_MAX) never flags (fail-open);
 *   - threshold 0 disarms even in reject mode;
 *   - per-profile override beats the global mode in both directions.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

struct ssf_config;
#include "../src/supersmartfilter.h"
struct ssf_config ssf_cfg; /* definition for the extern in the header */

static int fails = 0;
#define EXPECT(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); fails++; } } while (0)

static const uint8_t CW_A[16] = {0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88, 0x99,0xAA,0xBB,0xCC,0xDD,0xEE,0xFF,0x10};

static void setup(int global_mode, int threshold_ms)
{
    supersmartfilter_init();
    ssf_cfg.filter_level = 2;
    ssf_cfg.blockfakecws = 0;
    ssf_cfg.sf_entropy = 0;
    ssf_cfg.sf_pattern = 0;
    ssf_cfg.sf_dupseen = 0;
    ssf_cfg.sf_multicheck = 0;
    ssf_cfg.sf_marchcheck = 0;
    ssf_cfg.sf_cwdup = 0;
    ssf_cfg.sf_cwreuse = 0;
    ssf_cfg.sf_cwfmt_auto = 0;
    ssf_cfg.sf_cwfmt_enforce = 0;
    ssf_cfg.sf_latecw = global_mode;
    ssf_cfg.sf_latecw_ms = threshold_ms;
}

static uint32_t test_now = 500000;
static int prof_late_ms = 0; /* Band 65: per-profile threshold (0=inherit) */
static int check(uint32_t age_ms, int prof_late, int *reason)
{
    /* advance past the duplicate/speed history window between calls */
    test_now += 20000;
    return supersmartfilter_check(CW_A, NULL, 0x183D, 0, 0x4001, 0,
                                  test_now, reason, 0, 0, 0, 0, prof_late, age_ms, prof_late_ms, 0, 0);
}

int main(void)
{
    int reason = 0;
    struct ssf_stats st;

    /* below threshold: fine everywhere */
    setup(2, 8000);
    EXPECT(check(100, 0, &reason) == 0, "fresh CW accepted in reject mode");
    EXPECT(check(7999, 0, &reason) == 0, "just-under-threshold accepted");
    supersmartfilter_get_stats(&st);
    EXPECT(st.latecw_flagged == 0, "no flags below threshold");

    /* log mode: flags but accepts */
    setup(1, 8000);
    EXPECT(check(9000, 0, &reason) == 0, "log: late CW accepted");
    supersmartfilter_get_stats(&st);
    EXPECT(st.latecw_flagged == 1, "log: flagged once");
    EXPECT(st.rejected_latecw == 0, "log: nothing rejected");

    /* reject mode: reason 13 */
    setup(2, 8000);
    reason = 0;
    EXPECT(check(60000, 0, &reason) == 1, "reject: very late CW rejected");
    EXPECT(reason == 13, "reject: reason code 13");
    supersmartfilter_get_stats(&st);
    EXPECT(st.rejected_latecw == 1, "reject: one late rejection");

    /* unknown age: fail-open */
    setup(2, 100);
    EXPECT(check(0xFFFFFFFFu, 0, &reason) == 0, "unknown age never flags");
    supersmartfilter_get_stats(&st);
    EXPECT(st.latecw_flagged == 0, "unknown age: no flags");

    /* threshold 0: disarmed even in reject mode */
    setup(2, 0);
    EXPECT(check(60000, 0, &reason) == 0, "threshold 0 disarms");

    /* profile override both directions */
    setup(0, 8000); /* global off */
    EXPECT(check(9000, 3, &reason) == 1, "prof: override 3 rejects while global off");
    setup(2, 8000); /* global reject */
    EXPECT(check(9000, 1, &reason) == 0, "prof-off: override 1 disables while global reject");
    supersmartfilter_get_stats(&st);
    EXPECT(st.latecw_flagged == 0, "prof-off: no flags when profile forces off");

    /* Band 65: per-CAID threshold wins over the global one, both ways.
     * Slow-source CAID (60-150s answers): global 8000 would nuke it,
     * profile 200000 spares it. Fast CAID tightened to 1000. */
    setup(2, 8000);
    prof_late_ms = 200000;
    EXPECT(check(60000, 0, &reason) == 0, "prof-ms: slow CAID spared by its own threshold");
    supersmartfilter_get_stats(&st);
    EXPECT(st.latecw_flagged == 0, "prof-ms: no flag under profile threshold");
    prof_late_ms = 1000;
    reason = 0;
    EXPECT(check(5000, 0, &reason) == 1, "prof-ms: tightened CAID rejects at 5s");
    EXPECT(reason == 13, "prof-ms: reason 13");
    prof_late_ms = 0;
    EXPECT(check(5000, 0, &reason) == 0, "prof-ms: 0 inherits global (5s < 8s ok)");

    if (fails) { printf("latecw: %d FAILURES\n", fails); return 1; }
    printf("latecw: PASS (fresh ok, late flags/rejects 13, unknown fail-open, override, per-CAID ms)\n");
    return 0;
}
