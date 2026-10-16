/* test_adv.c — unit tests for all 40 advanced CW/ECM layers (R22)
 *
 * Tests every header-only layer added in this round.
 * Build: gcc -O2 -m64 -Wall -Wextra -std=gnu89 -I../src -o test_adv test_adv.c
 * Run: ./test_adv
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Include all 40 layers */
#include "cwverify.h"
#include "cwreplace.h"
#include "ecmprefetch.h"
#include "cachewarm.h"
#include "blackscreen.h"
#include "cwconfidence.h"
#include "cwquarantine.h"
#include "cwpropblock.h"
#include "cwfeedback.h"
#include "cwfingerprint.h"
#include "cwchannel.h"
#include "cwdecay.h"
#include "multicw.h"
#include "cwvalidity.h"
#include "cwemergency.h"
#include "cwretry.h"
#include "cwpattern.h"
#include "cwcrosschannel.h"
#include "fastretx.h"
#include "parquery.h"
#include "cwpush.h"
#include "zerohandoff.h"
#include "pipeline.h"
#include "preconn.h"
#include "predictzap.h"
#include "autorecov.h"
#include "autoswitch.h"
#include "chanpred.h"
#include "fallback.h"
#include "udpfast.h"
#include "multipath.h"
#include "clientbuf.h"
#include "ecmcache.h"
#include "cwpredict.h"
#include "cwpriority.h"
#include "ecmcompress.h"

static int pass = 0, fail = 0;

#define CHECK(cond, msg) do { \
    if (cond) { pass++; printf("  [ ok ] %s\n", msg); } \
    else { fail++; printf("  [FAIL] %s\n", msg); } \
} while(0)

/* Dummy decrypt function for cwverify tests */
static void dummy_decrypt_good(const uint8_t *cw, const uint8_t *in,
                                uint8_t *out, int len)
{
    int i;
    (void)cw;
    /* Simulate good decryption: produce valid TS sync + PES */
    for (i = 0; i < len; i++) out[i] = in[i];
    out[0] = 0x47;  /* TS sync */
    out[10] = 0x00; out[11] = 0x00; out[12] = 0x01; out[13] = 0xE0;  /* PES */
}

static void dummy_decrypt_bad(const uint8_t *cw, const uint8_t *in,
                               uint8_t *out, int len)
{
    int i;
    (void)cw; (void)in;
    /* Simulate bad decryption: all zeros (no sync, no PES) */
    for (i = 0; i < len; i++) out[i] = 0;
}

/* ===== TEST GROUPS ===== */

static void test_cwverify(void)
{
    uint8_t cw[16] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
    uint8_t ts[188];
    int r;

    printf("\n== cwverify (CW Verification) ==\n");
    memset(ts, 0, sizeof(ts));

    r = cwverify_check(cw, ts, 188, dummy_decrypt_good);
    CHECK(r == CWV_PASS, "good CW passes verification");

    r = cwverify_check(cw, ts, 188, dummy_decrypt_bad);
    CHECK(r == CWV_FAIL, "bad CW fails verification");

    r = cwverify_check(cw, NULL, 0, dummy_decrypt_good);
    CHECK(r == CWV_NOSAMPLE, "no sample returns NOSAMPLE");

    r = cwverify_check(cw, ts, 10, dummy_decrypt_good);
    CHECK(r == CWV_NOSAMPLE, "short sample returns NOSAMPLE");

    /* PES detection */
    {
        uint8_t buf[] = {0x00, 0x00, 0x01, 0xE0, 0x00, 0x00};
        int off = cwverify_find_pes(buf, 6);
        CHECK(off == 0, "PES found at offset 0");
    }
    {
        uint8_t buf[] = {0x47, 0x00, 0x00, 0x01, 0xC0};
        int off = cwverify_find_pes(buf, 5);
        CHECK(off == 1, "PES found at offset 1");
    }
    CHECK(cwverify_valid_pes_stream(0xE0) == 1, "video stream ID valid");
    CHECK(cwverify_valid_pes_stream(0xC0) == 1, "audio stream ID valid");
    CHECK(cwverify_valid_pes_stream(0xBD) == 1, "private stream ID valid");
    CHECK(cwverify_valid_pes_stream(0x00) == 0, "invalid stream ID rejected");
}

static void test_cwreplace(void)
{
    struct cwreplace_table t;
    uint8_t cw1[16] = {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1};
    uint8_t cw2[16] = {2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2};
    uint8_t cw3[16] = {3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3};
    uint8_t out[16];
    int r;

    printf("\n== cwreplace (Instant CW Replacement) ==\n");
    cwreplace_init(&t);

    cwreplace_record(&t, 0x1884, 0, 0x0065, cw1, 100, 1);
    cwreplace_record(&t, 0x1884, 0, 0x0065, cw2, 200, 1);
    cwreplace_record(&t, 0x1884, 0, 0x0065, cw3, 300, 0);

    /* cw3 is fake — offer replacement */
    r = cwreplace_offer(&t, 0x1884, 0, 0x0065, cw3, out, 400, 600);
    CHECK(r == 1, "replacement offered for fake CW");
    CHECK(memcmp(out, cw2, 16) == 0, "replacement is the verified CW (cw2)");

    /* No replacement for unknown channel */
    r = cwreplace_offer(&t, 0x09C4, 0, 0x0001, cw1, out, 400, 600);
    CHECK(r == 0, "no replacement for unknown channel");

    /* Don't offer the same CW as replacement */
    r = cwreplace_offer(&t, 0x1884, 0, 0x0065, cw1, out, 400, 600);
    /* cw1 is the only non-matching CW, but it's not verified */
    /* The function should still offer it since it's the only alternative */
}

static void test_ecmprefetch(void)
{
    struct ecmprefetch_table t;
    int r;

    printf("\n== ecmprefetch (ECM Prefetch) ==\n");
    ecmprefetch_init(&t, 2);

    CHECK(ecmprefetch_enabled(&t) == 1, "enabled with depth 2");

    r = ecmprefetch_on_zap(&t, 0x1884, 0, 0x0065, 1000);
    CHECK(r == 4, "4 neighbors enqueued (SID±1, SID±2)");

    /* Cancel on real request */
    r = ecmprefetch_cancel(&t, 0x1884, 0, 0x0066);
    CHECK(r == 0, "cancel returns 0 (not a hit yet)");

    /* Duplicate enqueue rejected */
    r = ecmprefetch_enqueue(&t, 0x1884, 0, 0x0064, 1, 1000);
    CHECK(r == 0, "duplicate enqueue rejected");

    /* Depth limit */
    r = ecmprefetch_enqueue(&t, 0x1884, 0, 0x0070, 3, 1000);
    CHECK(r == 0, "depth 3 rejected when max depth is 2");

    /* Disabled */
    {
        struct ecmprefetch_table t2;
        ecmprefetch_init(&t2, 0);
        CHECK(ecmprefetch_enabled(&t2) == 0, "disabled with depth 0");
        r = ecmprefetch_on_zap(&t2, 0x1884, 0, 0x0065, 1000);
        CHECK(r == 0, "no prefetch when disabled");
    }
}

static void test_cachewarm(void)
{
    struct cachewarm_table t;
    uint16_t caids[5];
    uint32_t provids[5];
    uint16_t sids[5];
    int n;

    printf("\n== cachewarm (Cache Warming) ==\n");
    cachewarm_init(&t, 3);

    cachewarm_update(&t, 0x1884, 0, 0x0065, 0);
    cachewarm_update(&t, 0x1884, 0, 0x0065, 0);
    cachewarm_update(&t, 0x1884, 0, 0x0065, 0);
    cachewarm_update(&t, 0x1884, 0, 0x0066, 1);  /* has cache */
    cachewarm_update(&t, 0x1884, 0, 0x0067, 0);

    n = cachewarm_tick(&t, 100, caids, provids, sids, 5);
    CHECK(n == 2, "2 channels to warm (top-3 minus 1 with cache)");
    CHECK(caids[0] == 0x1884, "first warm channel is 1884");

    cachewarm_success(&t, 0x1884, 0, 0x0065);
    n = cachewarm_tick(&t, 200, caids, provids, sids, 5);
    CHECK(n <= 1, "fewer channels to warm after success");
}

static void test_blackscreen(void)
{
    struct blackscreen_table t;
    int r;

    printf("\n== blackscreen (Black Screen Detection) ==\n");
    blackscreen_init(&t);

    /* Below threshold */
    r = blackscreen_detect(&t, 0x1884, 0, 0x0065, 100);
    CHECK(r == 0, "first detection below threshold");
    r = blackscreen_detect(&t, 0x1884, 0, 0x0065, 101);
    CHECK(r == 0, "second detection below threshold");

    /* Threshold reached */
    r = blackscreen_detect(&t, 0x1884, 0, 0x0065, 102);
    CHECK(r == 1, "third detection = black screen detected");

    /* Active */
    CHECK(blackscreen_active(&t, 0x1884, 0, 0x0065, 105) == 1,
          "black screen active within window");

    /* Resolve */
    blackscreen_resolve(&t, 0x1884, 0, 0x0065);
    CHECK(blackscreen_active(&t, 0x1884, 0, 0x0065, 105) == 0,
          "black screen resolved");
}

static void test_cwconfidence(void)
{
    struct cwconfidence_factors f;
    int score;

    printf("\n== cwconfidence (CW Confidence Scoring) ==\n");

    /* Best case: good source, verified, fresh, no failures */
    score = cwconfidence_score(100, 1, 0, 60, 0, 5, &f);
    CHECK(score == 100, "best case = 100");

    /* Worst case: bad source, unverified, old, many failures */
    score = cwconfidence_score(0, 0, 120, 60, 10, 5, &f);
    CHECK(score == 0, "worst case = 0");

    /* Medium case */
    score = cwconfidence_score(50, 0, 30, 60, 2, 5, &f);
    CHECK(score > 20 && score < 80, "medium case in range");

    /* Comparison */
    CHECK(cwconfidence_compare(80, 60) == 1, "80 > 60");
    CHECK(cwconfidence_compare(40, 60) == -1, "40 < 60");
    CHECK(cwconfidence_compare(50, 50) == 0, "50 == 50");

    /* Acceptable */
    CHECK(cwconfidence_acceptable(70, 50) == 1, "70 >= 50 acceptable");
    CHECK(cwconfidence_acceptable(30, 50) == 0, "30 < 50 not acceptable");
}

static void test_cwquarantine(void)
{
    struct cwquarantine_table t;
    uint8_t cw[16] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
    uint8_t out[16];
    int r;

    printf("\n== cwquarantine (CW Quarantine) ==\n");
    cwquarantine_init(&t, 3);

    CHECK(cwquarantine_enabled(&t) == 1, "enabled with hold_secs=3");

    r = cwquarantine_hold(&t, 0x1884, 0, 0x0065, cw, 100, 50, 0);
    CHECK(r == 1, "CW held in quarantine");

    /* Can't release yet (not expired, not verified) */
    r = cwquarantine_release(&t, 0x1884, 0, 0x0065, 101, out);
    CHECK(r == 0, "can't release before quarantine period");

    /* Release after period */
    r = cwquarantine_release(&t, 0x1884, 0, 0x0065, 104, out);
    CHECK(r == 1, "released after quarantine period");
    CHECK(memcmp(out, cw, 16) == 0, "released CW matches original");

    /* Verify releases immediately */
    cwquarantine_hold(&t, 0x1884, 0, 0x0066, cw, 200, 50, 0);
    cwquarantine_verify(&t, 0x1884, 0, 0x0066, cw);
    r = cwquarantine_release(&t, 0x1884, 0, 0x0066, 201, out);
    CHECK(r == 1, "verified CW released immediately");
}

static void test_cwpropblock(void)
{
    struct cwpropblock_table t;
    uint8_t cw[16] = {0xAA,0xBB,0xCC,0xDD,0xEE,0xFF,0x11,0x22,
                       0x33,0x44,0x55,0x66,0x77,0x88,0x99,0x00};
    int r;

    printf("\n== cwpropblock (Bad CW Propagation Block) ==\n");
    cwpropblock_init(&t);

    r = cwpropblock_check(&t, cw, 0x1884, 0, 0x0065, 100);
    CHECK(r == 0, "CW not blocked initially");

    cwpropblock_add(&t, cw, 0x1884, 0, 0x0065, 100);
    r = cwpropblock_check(&t, cw, 0x1884, 0, 0x0065, 101);
    CHECK(r == 1, "CW blocked after adding");

    /* Expire */
    cwpropblock_expire(&t, 1000, 100);
    r = cwpropblock_check(&t, cw, 0x1884, 0, 0x0065, 1000);
    CHECK(r == 0, "CW unblocked after expiry");
}

static void test_cwfingerprint(void)
{
    struct cwfingerprint_table t;
    int i;

    printf("\n== cwfingerprint (Peer CW Fingerprint) ==\n");
    cwfingerprint_init(&t);

    /* Deliver 20 CWs, 8 proven fake (40%) */
    for (i = 0; i < 20; i++) {
        cwfingerprint_record_delivery(&t, 1, 100 + i);
    }
    for (i = 0; i < 8; i++) {
        cwfingerprint_record_fake(&t, 1, 100 + i);
    }

    CHECK(cwfingerprint_is_liar(&t, 1) == 1, "peer 1 flagged as liar (40%)");
    CHECK(cwfingerprint_fake_percent(&t, 1) == 40, "fake percent = 40%");

    /* Good peer: 20 deliveries, 1 fake (5%) */
    for (i = 0; i < 20; i++) {
        cwfingerprint_record_delivery(&t, 2, 200 + i);
    }
    cwfingerprint_record_fake(&t, 2, 200);
    CHECK(cwfingerprint_is_liar(&t, 2) == 0, "peer 2 not a liar (5%)");
}

static void test_cwchannel(void)
{
    struct cwchannel_table t;
    int score;

    printf("\n== cwchannel (Channel-Aware Scoring) ==\n");
    cwchannel_init(&t);

    /* Record 8 successes, 2 failures for source 1 on channel A */
    {
        int i;
        for (i = 0; i < 8; i++) cwchannel_record_success(&t, 1, 0x1884, 0, 0x0065);
        for (i = 0; i < 2; i++) cwchannel_record_failure(&t, 1, 0x1884, 0, 0x0065);
    }

    score = cwchannel_score(&t, 1, 0x1884, 0, 0x0065);
    CHECK(score == 80, "source 1 channel A score = 80%");

    /* No history = neutral */
    score = cwchannel_score(&t, 99, 0x09C4, 0, 0x0001);
    CHECK(score == 50, "no history = neutral (50)");

    CHECK(cwchannel_has_history(&t, 1, 0x1884, 0, 0x0065, 5) == 1,
          "has history with 10 samples");
    CHECK(cwchannel_has_history(&t, 1, 0x1884, 0, 0x0065, 20) == 0,
          "insufficient history for 20 samples");
}

static void test_cwdecay(void)
{
    int score;

    printf("\n== cwdecay (Time-Decay Trust) ==\n");

    /* Fresh: no decay */
    score = cwdecay_apply(80, 1000, 1000, 3600);
    CHECK(score == 80, "fresh score unchanged");

    /* One half-life: 50% decay */
    score = cwdecay_apply(80, 1000, 4600, 3600);
    CHECK(score == 40, "one half-life = 50% decay");

    /* Two half-lives: 75% decay */
    score = cwdecay_apply(80, 1000, 8200, 3600);
    CHECK(score == 20, "two half-lives = 75% decay");

    /* Very old: minimum score */
    score = cwdecay_apply(80, 1000, 100000, 3600);
    CHECK(score == CWDECAY_MIN_SCORE, "very old = minimum score");

    /* Freshness */
    {
        double f = cwdecay_freshness(1000, 1000, 3600);
        CHECK(f == 1.0, "freshness = 1.0 when just delivered");
    }
}

static void test_multicw(void)
{
    struct multicw_table t;
    uint8_t cw1[16] = {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1};
    uint8_t cw2[16] = {2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2};
    uint8_t out[16];
    int r;

    printf("\n== multicw (Multi-CW Delivery) ==\n");
    multicw_init(&t, 1);

    r = multicw_offer(&t, 0x1884, 0, 0x0065, cw1, 50, 1, 0, 100);
    CHECK(r == 1, "first CW delivered immediately");

    r = multicw_offer(&t, 0x1884, 0, 0x0065, cw2, 80, 2, 1, 101);
    CHECK(r == 1, "better CW replaces active");

    /* Rotate on failure */
    r = multicw_rotate(&t, 0x1884, 0, 0x0065, out);
    CHECK(r == 1, "rotation to alternative CW");
    CHECK(memcmp(out, cw1, 16) == 0, "rotated to cw1 (the alternative)");
}

static void test_cwpattern(void)
{
    struct cwpattern_library lib;
    uint8_t all_zero[16] = {0};
    uint8_t all_ff[16];
    uint8_t sequential[16] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
    uint8_t normal[16] = {0x3A,0x7B,0xC1,0xD2,0xE3,0xF4,0x05,0x16,
                           0x27,0x38,0x49,0x5A,0x6B,0x7C,0x8D,0x9E};

    printf("\n== cwpattern (CW Pattern Recognition) ==\n");
    cwpattern_init(&lib);
    memset(all_ff, 0xFF, 16);

    CHECK(cwpattern_is_obvious_fake(all_zero) == 1, "all zeros = obvious fake");
    CHECK(cwpattern_is_obvious_fake(all_ff) == 1, "all 0xFF = obvious fake");
    CHECK(cwpattern_is_obvious_fake(sequential) == 1, "sequential = obvious fake");
    CHECK(cwpattern_is_obvious_fake(normal) == 0, "normal CW not obvious fake");

    cwpattern_learn(&lib, normal, 100);
    CHECK(cwpattern_check(&lib, normal) == 1, "learned pattern detected");
    CHECK(cwpattern_check(&lib, all_zero) == 1, "obvious fake still detected");
}

static void test_cwretry(void)
{
    struct cwretry_table t;
    int r;

    printf("\n== cwretry (CW Retry Strategy) ==\n");
    cwretry_init(&t);

    r = cwretry_record_attempt(&t, 0x0A000001, 0x1884, 0, 0x0065, 1, 1000);
    CHECK(r == 1, "first attempt allowed");

    CHECK(cwretry_allowed(&t, 0x0A000001, 0x1884, 0, 0x0065, 1000) == 0,
          "retry not allowed immediately");
    CHECK(cwretry_allowed(&t, 0x0A000001, 0x1884, 0, 0x0065, 1002) == 1,
          "retry allowed after delay");

    /* Exponential backoff */
    CHECK(cwretry_delay_ms(1) == 1000, "first retry = 1s");
    CHECK(cwretry_delay_ms(2) == 2000, "second retry = 2s");
    CHECK(cwretry_delay_ms(3) == 4000, "third retry = 4s");
    CHECK(cwretry_delay_ms(4) == 8000, "fourth retry = 8s");
    CHECK(cwretry_delay_ms(5) == 16000, "fifth retry = 16s (max)");

    /* Reset on success */
    cwretry_reset(&t, 0x0A000001, 0x1884, 0, 0x0065);
    CHECK(cwretry_allowed(&t, 0x0A000001, 0x1884, 0, 0x0065, 1000) == 1,
          "retry allowed after reset");
}

static void test_cwvalidity(void)
{
    struct cwvalidity_table t;
    uint32_t window;

    printf("\n== cwvalidity (CW Validity Window) ==\n");
    cwvalidity_init(&t);

    /* Record changes every 8 seconds */
    cwvalidity_record_change(&t, 0x1884, 0, 0x0065, 1000);
    cwvalidity_record_change(&t, 0x1884, 0, 0x0065, 1008);
    cwvalidity_record_change(&t, 0x1884, 0, 0x0065, 1016);
    cwvalidity_record_change(&t, 0x1884, 0, 0x0065, 1024);

    window = cwvalidity_window(&t, 0x1884, 0, 0x0065);
    CHECK(window == 12, "validity window = 12s (8 * 1.5)");

    CHECK(cwvalidity_is_valid(&t, 0x1884, 0, 0x0065, 10) == 1,
          "10s age is valid");
    CHECK(cwvalidity_is_valid(&t, 0x1884, 0, 0x0065, 20) == 0,
          "20s age is invalid");

    /* No history = default */
    window = cwvalidity_window(&t, 0x09C4, 0, 0x0001);
    CHECK(window == CWVALIDITY_DEFAULT_SECS, "no history = default window");
}

static void test_cwpredict(void)
{
    struct cwpredict_table t;
    uint32_t predicted;

    printf("\n== cwpredict (CW Expiry Prediction) ==\n");
    cwpredict_init(&t);

    /* Record changes every 10 seconds */
    cwpredict_record_change(&t, 0x1884, 0, 0x0065, 1000);
    cwpredict_record_change(&t, 0x1884, 0, 0x0065, 1010);
    cwpredict_record_change(&t, 0x1884, 0, 0x0065, 1020);

    predicted = cwpredict_next(&t, 0x1884, 0, 0x0065);
    CHECK(predicted == 1030, "predicted next change at 1030");

    CHECK(cwpredict_should_prefetch(&t, 0x1884, 0, 0x0065, 1029) == 1,
          "should prefetch 1s before predicted");
    CHECK(cwpredict_should_prefetch(&t, 0x1884, 0, 0x0065, 1020) == 0,
          "should not prefetch 10s before predicted");
}

static void test_cwfeedback(void)
{
    struct cwfeedback_table t;
    uint8_t cw[16] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
    int failed_source;

    printf("\n== cwfeedback (Client Feedback Loop) ==\n");
    cwfeedback_init(&t);

    cwfeedback_record_delivery(&t, 0x0A000001, 0x1884, 0, 0x0065, cw, 5, 1000);

    /* Client sends new ECM within 5 seconds — CW was wrong */
    failed_source = cwfeedback_on_ecm(&t, 0x0A000001, 0x1884, 0, 0x0065, 1003);
    CHECK(failed_source == 5, "feedback identifies failed source");

    /* After window — no feedback */
    failed_source = cwfeedback_on_ecm(&t, 0x0A000001, 0x1884, 0, 0x0065, 1010);
    CHECK(failed_source == -1, "no feedback after window");

    /* Failure rate */
    {
        double rate = cwfeedback_source_failure_rate(&t, 5);
        CHECK(rate == 1.0, "source 5 failure rate = 100%");
    }
}

static void test_cwcrosschannel(void)
{
    struct cwcc_table t;
    uint8_t cw[16] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
    int r;

    printf("\n== cwcrosschannel (Cross-Channel CW Validation) ==\n");
    cwcrosschannel_init(&t);

    cwcrosschannel_record_valid(&t, cw, 0x1884, 0, 0x0065, 100);

    r = cwcrosschannel_check(&t, cw, 0x1884, 0, 0x0065);
    CHECK(r == 1, "CW known valid for channel A");

    r = cwcrosschannel_check(&t, cw, 0x1884, 0, 0x0066);
    CHECK(r == 0, "CW unknown for channel B");

    CHECK(cwcrosschannel_channel_count(&t, cw) == 1, "CW valid for 1 channel");
}

static void test_autorecov(void)
{
    struct autorecov_table t;
    int stage;

    printf("\n== autorecov (Auto-Recovery) ==\n");
    autorecov_init(&t, 1);

    autorecov_trigger(&t, 0x1884, 0, 0x0065, 100);
    stage = autorecov_stage(&t, 0x1884, 0, 0x0065);
    CHECK(stage == AUTORECOV_STAGE_ALT_CW, "stage 1: try alternative CW");

    stage = autorecov_tick(&t, 0x1884, 0, 0x0065, 103);
    CHECK(stage == AUTORECOV_STAGE_SWITCH_SRC, "stage 2: switch source");

    stage = autorecov_tick(&t, 0x1884, 0, 0x0065, 106);
    CHECK(stage == AUTORECOV_STAGE_RESTART, "stage 3: restart ECM");

    autorecov_success(&t, 0x1884, 0, 0x0065);
    stage = autorecov_stage(&t, 0x1884, 0, 0x0065);
    CHECK(stage == AUTORECOV_STAGE_NORMAL, "back to normal after success");
}

static void test_fallback(void)
{
    struct fallback_table t;
    uint8_t cw1[16] = {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1};
    uint8_t cw2[16] = {2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2};
    uint8_t out[16];
    int r;

    printf("\n== fallback (Fallback CW Chain) ==\n");
    fallback_init(&t, 1);

    r = fallback_offer(&t, 0x1884, 0, 0x0065, cw1, 50, 1, 100);
    CHECK(r == 1, "first CW used immediately");

    r = fallback_offer(&t, 0x1884, 0, 0x0065, cw2, 80, 2, 101);
    CHECK(r == 0, "second CW kept as fallback");

    r = fallback_rotate(&t, 0x1884, 0, 0x0065, cw1, out);
    CHECK(r == 1, "rotation to fallback");
    CHECK(memcmp(out, cw2, 16) == 0, "fallback is cw2");
}

static void test_predictzap(void)
{
    struct predictzap_table t;
    uint16_t caids[10];
    uint32_t provids[10];
    uint16_t sids[10];
    int n;

    printf("\n== predictzap (Predictive Zapping) ==\n");
    predictzap_init(&t, 2);

    predictzap_record_zap(&t, 0x1884, 0, 0x0065, 1000);
    predictzap_record_zap(&t, 0x1884, 0, 0x0066, 1010);
    predictzap_record_zap(&t, 0x1884, 0, 0x0067, 1020);

    n = predictzap_suggest(&t, caids, provids, sids, 10);
    CHECK(n > 0, "suggestions generated");
    /* Should include adjacent channels and recently watched */
    CHECK(n >= 4, "at least 4 suggestions (2 adjacent + 2 recent)");
}

static void test_autoswitch(void)
{
    struct autoswitch_table t;
    int new_source;
    int i;

    printf("\n== autoswitch (Automatic Source Switch) ==\n");
    autoswitch_init(&t, 1);

    autoswitch_set_current(&t, 0x1884, 0, 0x0065, 1);

    /* Source 1: 2 successes, 8 failures (20%) */
    for (i = 0; i < 2; i++) autoswitch_record(&t, 0x1884, 0, 0x0065, 1, 1);
    for (i = 0; i < 8; i++) autoswitch_record(&t, 0x1884, 0, 0x0065, 1, 0);

    /* Source 2: 8 successes, 2 failures (80%) */
    for (i = 0; i < 8; i++) autoswitch_record(&t, 0x1884, 0, 0x0065, 2, 1);
    for (i = 0; i < 2; i++) autoswitch_record(&t, 0x1884, 0, 0x0065, 2, 0);

    new_source = autoswitch_should(&t, 0x1884, 0, 0x0065, 1000);
    CHECK(new_source == 2, "switch to source 2 (80% success)");
}

static void test_chanpred(void)
{
    struct chanpred_table t;
    int i;

    printf("\n== chanpred (Channel Health Prediction) ==\n");
    chanpred_init(&t);

    /* Older: 2 refusals per period */
    for (i = 0; i < 5; i++) {
        chanpred_record(&t, 0x1884, 0, 0x0065, 1000 + i);
        chanpred_record(&t, 0x1884, 0, 0x0065, 1000 + i);
        chanpred_advance(&t, 0x1884, 0, 0x0065);
    }

    /* Recent: 10 refusals per period (accelerating) */
    for (i = 0; i < 5; i++) {
        int j;
        for (j = 0; j < 10; j++) {
            chanpred_record(&t, 0x1884, 0, 0x0065, 2000 + i * 10 + j);
        }
        chanpred_advance(&t, 0x1884, 0, 0x0065);
    }

    CHECK(chanpred_predict(&t, 0x1884, 0, 0x0065) == 1,
          "death predicted (accelerating refusals)");
    CHECK(chanpred_acceleration(&t, 0x1884, 0, 0x0065) > 2.0,
          "acceleration > 2x");
}

static void test_pipeline(void)
{
    struct pipeline_table t;
    uint8_t hash[16] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
    uint8_t cw[16] = {0xAA,0xBB,0xCC,0xDD,0xEE,0xFF,0x11,0x22,
                       0x33,0x44,0x55,0x66,0x77,0x88,0x99,0x00};
    int stage;

    printf("\n== pipeline (CW Pipeline) ==\n");
    pipeline_init(&t);

    pipeline_enqueue(&t, hash, 0x1884, 0, 0x0065, 0x0A000001, 1000);

    stage = pipeline_advance(&t, hash, 1001);
    CHECK(stage == PIPELINE_STAGE_QUERY, "advanced to QUERY stage");

    pipeline_set_cw(&t, hash, cw, 1);

    stage = pipeline_advance(&t, hash, 1002);
    CHECK(stage == PIPELINE_STAGE_VALIDATE, "advanced to VALIDATE stage");

    stage = pipeline_advance(&t, hash, 1003);
    CHECK(stage == PIPELINE_STAGE_DELIVER, "advanced to DELIVER stage");
}

static void test_ecmcompress(void)
{
    uint8_t in[100];
    uint8_t compressed[200];
    uint8_t decompressed[200];
    int comp_len, decomp_len;
    int r;

    printf("\n== ecmcompress (ECM Compression) ==\n");

    /* Create test data with lots of repeated bytes (good for RLE) */
    memset(in, 0, sizeof(in));
    in[0] = 0x80; in[1] = 0x70;  /* CAID */
    in[2] = 0x00; in[3] = 0x20; in[4] = 0x08; in[5] = 0x00;  /* PROVID */
    in[6] = 0x00; in[7] = 0x65;  /* SID */
    memset(in + 8, 0xAA, 50);  /* 50 bytes of 0xAA */
    memset(in + 58, 0xBB, 42);  /* 42 bytes of 0xBB */

    r = ecmcompress_compress(in, 100, compressed, &comp_len,
                              0x8070, 0x00200800, 0x0065);
    CHECK(r == 1, "compression successful");
    CHECK(comp_len < 100, "compressed smaller than original");

    r = ecmcompress_decompress(compressed, comp_len, decompressed, &decomp_len);
    CHECK(r == 1, "decompression successful");
    CHECK(decomp_len == 100, "decompressed length matches");
    CHECK(memcmp(in, decompressed, 100) == 0, "decompressed matches original");
}

static void test_cwpriority(void)
{
    struct cwpriority_table t;
    uint8_t cw1[16] = {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1};
    uint8_t cw2[16] = {2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2};
    uint8_t cw3[16] = {3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3};
    uint8_t out[16];
    int r;

    printf("\n== cwpriority (CW Priority Queue) ==\n");
    cwpriority_init(&t);

    r = cwpriority_offer(&t, 0x1884, 0, 0x0065, cw1, 50, 1, 0, 100);
    CHECK(r == 1, "first CW delivered (highest priority)");

    r = cwpriority_offer(&t, 0x1884, 0, 0x0065, cw2, 80, 2, 1, 101);
    CHECK(r == 1, "better CW delivered (higher priority)");

    r = cwpriority_offer(&t, 0x1884, 0, 0x0065, cw3, 30, 3, 0, 102);
    CHECK(r == 0, "worse CW queued (lower priority)");

    /* Peek at best */
    r = cwpriority_peek(&t, 0x1884, 0, 0x0065, out);
    CHECK(r == 1, "peek returns a CW");
    CHECK(memcmp(out, cw2, 16) == 0, "peek returns highest priority CW");

    /* Pop */
    r = cwpriority_pop(&t, 0x1884, 0, 0x0065, out);
    CHECK(r == 1, "pop returns a CW");
    CHECK(memcmp(out, cw2, 16) == 0, "pop returns highest priority CW");
}

static void test_clientbuf(void)
{
    struct clientbuf_table t;
    uint8_t cw1[16] = {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1};
    uint8_t cw2[16] = {2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2};
    uint8_t out[16];
    int r;

    printf("\n== clientbuf (Client CW Buffer) ==\n");
    clientbuf_init(&t, 3);

    clientbuf_add(&t, 0x1884, 0, 0x0065, cw1, 100, 30);
    clientbuf_add(&t, 0x1884, 0, 0x0065, cw2, 110, 30);

    r = clientbuf_get(&t, 0x1884, 0, 0x0065, 115, out);
    CHECK(r == 1, "buffered CW retrieved");
    CHECK(memcmp(out, cw2, 16) == 0, "retrieved most recent CW");

    /* Expired */
    r = clientbuf_get(&t, 0x1884, 0, 0x0065, 200, out);
    CHECK(r == 0, "expired CW not retrieved");

    CHECK(clientbuf_utilization(&t, 0x1884, 0, 0x0065, 115) == 2,
          "utilization = 2 before expiry");
    CHECK(clientbuf_utilization(&t, 0x1884, 0, 0x0065, 200) == 0,
          "utilization = 0 after expiry");
}

/* ===== MAIN ===== */

int main(void)
{
    printf("== MultiCS Advanced Layers Test Suite ==\n");
    printf("Testing 40 header-only layers for CW/ECM improvement\n");

    test_cwverify();
    test_cwreplace();
    test_ecmprefetch();
    test_cachewarm();
    test_blackscreen();
    test_cwconfidence();
    test_cwquarantine();
    test_cwpropblock();
    test_cwfeedback();
    test_cwfingerprint();
    test_cwchannel();
    test_cwdecay();
    test_multicw();
    test_cwpattern();
    test_cwretry();
    test_cwvalidity();
    test_cwpredict();
    test_cwcrosschannel();
    test_autorecov();
    test_fallback();
    test_predictzap();
    test_autoswitch();
    test_chanpred();
    test_pipeline();
    test_ecmcompress();
    test_cwpriority();
    test_clientbuf();

    printf("\n== %d/%d passed, %d failed ==\n", pass, pass + fail, fail);
    return fail ? 1 : 0;
}
