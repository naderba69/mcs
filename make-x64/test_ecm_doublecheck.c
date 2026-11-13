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

    CHECK(ecm_doublecheck_needed(0x1810), "default Nagra CAID 1810 needs double check");
    CHECK(ecm_doublecheck_needed(0x0500), "default Viaccess CAID 0500 needs double check");
    CHECK(!ecm_doublecheck_needed(0x0604), "unlisted CAID does not need double check");

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

    printf("== %d passed, %d failed ==\n", passed, failed);
    return failed ? 1 : 0;
}
