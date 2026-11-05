#include <stdio.h>
#include <string.h>
#include "../src/smart-retry.h"

static int passed, failed;

#define CHECK(cond, what) do { \
    if (cond) { passed++; printf("  PASS: %s\n", what); } \
    else { failed++; printf("  FAIL: %s\n", what); } \
} while (0)

static void make_hash(uint8_t hash[16], uint32_t id)
{
    memset(hash, 0, 16);
    hash[0] = (uint8_t)(id >> 24);
    hash[1] = (uint8_t)(id >> 16);
    hash[2] = (uint8_t)(id >> 8);
    hash[3] = (uint8_t)id;
}

int main(void)
{
    struct sr_stats stats;
    uint8_t hash[16];
    sr_strategy_t strategy = SR_STRATEGY_FASTEST;
    uint32_t wait_ms = 0;
    int i, ok;
    uint32_t now = 100u;

    printf("== smart-retry modulo tick tests ==\n");

    sr_init();
    for (i = 0; i < SR_MAX_TRACKED_ECMS; i++) {
        uint32_t age = (i == 0) ? 3000000000u : (uint32_t)(i + 1);
        make_hash(hash, (uint32_t)i + 1u);
        sr_record_first_attempt(hash, 0x1884, 0, (uint16_t)i,
                                (uint32_t)i + 1u, now - age);
    }
    make_hash(hash, SR_MAX_TRACKED_ECMS + 1u);
    sr_record_first_attempt(hash, 0x1884, 0, 0x7000, 9000u, now);
    make_hash(hash, 1u);
    CHECK(sr_get_ecm(hash) == NULL,
          "full retry table evicts the greatest unsigned elapsed age");
    make_hash(hash, 2u);
    CHECK(sr_get_ecm(hash) != NULL,
          "full retry table preserves a recently started request");
    sr_get_stats(&stats);
    CHECK(stats.current_tracked_ecms == SR_MAX_TRACKED_ECMS,
          "replacing an active retry entry does not inflate the live count");

    sr_init();
    make_hash(hash, 1000u);
    sr_record_first_attempt(hash, 0x1884, 0, 1, 7, UINT32_MAX - 200u);
    ok = sr_should_retry(hash, UINT32_MAX - 200u + SR_RETRY_INTERVAL_MS - 1u,
                         &strategy, &wait_ms);
    CHECK(!ok && wait_ms == 1u,
          "retry deadline remains pending one tick before rollover expiry");
    wait_ms = 0;
    ok = sr_should_retry(hash, UINT32_MAX - 200u + SR_RETRY_INTERVAL_MS,
                         &strategy, &wait_ms);
    CHECK(ok && wait_ms == 0 && strategy == SR_STRATEGY_ALTERNATE,
          "first retry chooses the alternate strategy at the wrapped deadline");
    wait_ms = 0;
    ok = sr_should_retry(hash, 299u + SR_RETRY_INTERVAL_MS * 2u - 1u,
                         &strategy, &wait_ms);
    CHECK(!ok && wait_ms == 1u,
          "second retry backoff doubles to one second");
    ok = sr_should_retry(hash, 299u + SR_RETRY_INTERVAL_MS * 2u,
                         &strategy, &wait_ms);
    CHECK(ok && strategy == SR_STRATEGY_CACHE,
          "second retry advances to cache strategy after its deadline");

    printf("%d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
