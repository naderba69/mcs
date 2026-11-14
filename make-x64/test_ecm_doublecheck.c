/* test_ecm_doublecheck.c — ECM double-check pending-table lifecycle.
 *
 * Regression coverage for the slot leak found in production audit:
 * ecm_doublecheck_start() is called from th-ecm.c for the hardcoded
 * default CAIDs, but nothing ever freed pending slots, so after 256
 * ECMs every later start silently returned NULL. Slots must now be
 * reclaimed by age on the next start, on a monotonic ms tick (an NTP
 * wall-clock jump must not expire or pin entries).
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* Controllable monotonic tick stub (tools.c GetTickCount contract). */
static uint32_t fake_now_ms;
uint32_t GetTickCount(void) { return fake_now_ms; }

int cfg_doublecheck_caids[64];
int cfg_doublecheck_caid_count = 0;
#include "../src/ecm-doublecheck.h"

static int passed, failed;

#define CHECK(cond, what) do { \
    if (cond) { passed++; printf("  PASS: %s\n", what); } \
    else { failed++; printf("  FAIL: %s\n", what); } \
} while (0)

int main(void)
{
    int i, ok;
    doublecheck_pending_t *p;

    printf("== ecm-doublecheck pending lifecycle tests ==\n");

    fake_now_ms = 100000;
    ecm_doublecheck_init();
    ecm_doublecheck_load_defaults();

    /* No CAIDs are hardcoded any more: without config the feature is inert. */
    CHECK(!ecm_doublecheck_needed(0x1810), "no hardcoded CAID needs double check");
    CHECK(!ecm_doublecheck_needed(0x0500), "no hardcoded Viaccess either");

    /* The config bridge: parsed CAIDs are applied, then drive needed(). */
    cfg_doublecheck_caids[0] = 0x1810; cfg_doublecheck_caids[1] = 0x0500;
    cfg_doublecheck_caid_count = 2;
    CHECK(ecm_doublecheck_apply_config() == 2, "apply_config registers both CAIDs");
    CHECK(ecm_doublecheck_needed(0x1810), "configured Nagra CAID 1810 needs double check");
    CHECK(ecm_doublecheck_needed(0x0500), "configured Viaccess CAID 0500 needs double check");
    CHECK(!ecm_doublecheck_needed(0x0604), "unlisted CAID does not need double check");
    cfg_doublecheck_caid_count = 0;
    ecm_doublecheck_apply_config();
    CHECK(!ecm_doublecheck_needed(0x1810), "clearing the config list disarms the feature");
    cfg_doublecheck_caids[0] = 0x1810; cfg_doublecheck_caid_count = 1;
    ecm_doublecheck_apply_config();

    /* Fill the whole pending table: every start must succeed. */
    ok = 1;
    for (i = 0; i < 256; i++) {
        if (!ecm_doublecheck_start(0x1000 + (uint32_t)i, 0x1810, 0, 0x1))
            ok = 0;
    }
    CHECK(ok, "256 starts fill the pending table");

    /* Full table, nothing expired: next start must fail cleanly. */
    p = ecm_doublecheck_start(0xdead, 0x1810, 0, 0x1);
    CHECK(p == NULL, "full table with fresh entries returns NULL");

    /* Advance the monotonic tick past 2x timeout: the next start must
     * reclaim the expired slots instead of failing forever. */
    fake_now_ms += (DOUBLECHECK_TIMEOUT_MS * 2) + 1;
    p = ecm_doublecheck_start(0xbeef, 0x1810, 0, 0x1);
    CHECK(p != NULL, "expired slots are reclaimed on start");
    CHECK(g_doublecheck.timeouts >= 256, "reclaimed slots counted as timeouts");

    /* Timeout judgement uses elapsed monotonic time. */
    p = ecm_doublecheck_start(0x1234, 0x1810, 0, 0x1);
    CHECK(p != NULL && !ecm_doublecheck_is_timeout(p), "fresh entry is not timed out");
    fake_now_ms += DOUBLECHECK_TIMEOUT_MS + 1;
    CHECK(ecm_doublecheck_is_timeout(p), "entry older than timeout is timed out");

    /* 32-bit tick wraparound: elapsed math must stay correct. */
    fake_now_ms = 0xFFFFF000u;
    p = ecm_doublecheck_start(0x777, 0x1810, 0, 0x1);
    CHECK(p != NULL, "start near tick wrap succeeds");
    fake_now_ms += DOUBLECHECK_TIMEOUT_MS + 1; /* wraps past 2^32 */
    CHECK(ecm_doublecheck_is_timeout(p), "timeout still detected across tick wrap");

    /* find-by-hash + result comparison (the observe-mode feed). */
    {
        static const uint8_t cw_a[16] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
        static const uint8_t cw_b[16] = {9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9};
        doublecheck_pending_t *q = ecm_doublecheck_start(0xABCD, 0x1810, 0, 0x64);
        CHECK(q != NULL, "start for find/verify checks");
        CHECK(ecm_doublecheck_find(0xABCD) == q, "find returns the pending entry by hash");
        CHECK(ecm_doublecheck_find(0x9999) == NULL, "find of unknown hash returns NULL");
        CHECK(ecm_doublecheck_add_result(q, 11, cw_a, 120) == 0, "first result alone does not verify");
        CHECK(ecm_doublecheck_add_result(q, 22, cw_a, 150) == 1, "two agreeing sources verify");
        CHECK(ecm_doublecheck_is_verified(q, NULL), "is_verified reports the agreement");
        q = ecm_doublecheck_start(0xABCE, 0x1810, 0, 0x64);
        CHECK(ecm_doublecheck_add_result(q, 11, cw_a, 100) == 0, "mismatch: first result pending");
        CHECK(ecm_doublecheck_add_result(q, 22, cw_b, 100) == 0, "two disagreeing sources do not verify");
        CHECK(!ecm_doublecheck_is_verified(q, NULL), "mismatch is not reported verified");
        /* One reader feeding twice (arrival + commit feeds) is one
         * source, never a fabricated two-source agreement. */
        q = ecm_doublecheck_start(0xABD0, 0x1810, 0, 0x64);
        CHECK(ecm_doublecheck_add_result(q, 11, cw_a, 100) == 0, "dedupe: first result pending");
        CHECK(ecm_doublecheck_add_result(q, 11, cw_a, 100) == 0, "same reader again does not verify");
        CHECK(!ecm_doublecheck_is_verified(q, NULL), "same reader is not two sources");
        /* A second dispatch of the same ECM reuses its pending entry. */
        {
            doublecheck_pending_t *again = ecm_doublecheck_start(0xABD0, 0x1810, 0, 0x64);
            CHECK(again == q, "second dispatch joins the existing pending entry");
        }
        /* Decisions land in the trace-page ring: newest first. */
        {
            doublecheck_event_t ev[4];
            uint64_t ok = 0, fail = 0, tot = 0, to = 0;
            int n = ecm_doublecheck_recent_events(ev, 4, &ok, &fail, &tot, &to);
            CHECK(n == 2, "two decisions recorded for the trace page");
            CHECK(n == 2 && ev[0].kind == 2 && ev[1].kind == 1, "ring is newest-first (mismatch, then verified)");
            CHECK(ok == 1 && fail == 1, "ring snapshot carries verified/mismatch counters");
        }
    }


    printf("== %d passed, %d failed ==\n", passed, failed);
    return failed ? 1 : 0;
}
