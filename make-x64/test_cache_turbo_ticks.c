#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "../src/cache-turbo.h"

static int passed, failed;

#define CHECK(cond, what) do { \
    if (cond) { passed++; printf("  PASS: %s\n", what); } \
    else { failed++; printf("  FAIL: %s\n", what); } \
} while (0)

int main(void)
{
    uint8_t cw[16], out[16], hash[16];
    struct cache_turbo_stats stats;
    uint16_t recent_sid;
    int i, found;
    uint32_t start = UINT32_MAX - 100u;

    memset(cw, 0x5a, sizeof(cw));
    memset(hash, 0xa5, sizeof(hash));
    printf("== cache-turbo modulo tick tests ==\n");

    cache_turbo_init();
    cache_turbo_store(0x1884, 0, 0x100, cw, hash, 0, 0, 1, start);
    CHECK(cache_turbo_lookup(0x1884, 0, 0x100, out, start + 30000u) &&
          memcmp(out, cw, sizeof(cw)) == 0,
          "cache entry remains live through its exact 30-second millisecond TTL");
    CHECK(!cache_turbo_lookup(0x1884, 0, 0x100, out, start + 30001u),
          "cache lookup expires one millisecond beyond the wrapped TTL");
    cache_turbo_get_stats(&stats);
    CHECK(stats.current_entries == 0 && stats.total_expired == 1,
          "lookup expiration reclaims the pool slot and updates counts once");

    cache_turbo_init();
    cache_turbo_store(0x1884, 0, 0x200, cw, hash, 0, 0, 2, 0);
    CHECK(cache_turbo_lookup(0x1884, 0, 0x200, out, 0),
          "cache entry stored at tick zero is a valid hit");
    cache_turbo_remove(0x1884, 0, 0x200);
    cache_turbo_store(0x1884, 0, 0x201, cw, hash, 0, 0, 2, 1);
    cache_turbo_get_stats(&stats);
    CHECK(stats.current_entries == 1 && stats.total_evictions == 0 &&
          cache_turbo_pool_used == 1,
          "removed pool slot is reused without false eviction or count growth");

    cache_turbo_init();
    for (i = 0; i < CACHE_TURBO_MAX_ENTRIES; i++) {
        uint32_t age = (i == 0) ? 50000u :
                       (uint32_t)(CACHE_TURBO_MAX_ENTRIES - i);
        cache_turbo_store(0x1884, 0, (uint16_t)(0x300 + i),
                          cw, hash, 0, 0, (uint32_t)i, 100u - age);
    }
    recent_sid = 0x301;
    cache_turbo_store(0x1884, 0, 0x2000, cw, hash, 0, 0, 9999, 100u);
    found = cache_turbo_lookup(0x1884, 0, 0x300, out, 100u);
    CHECK(!found && cache_turbo_lookup(0x1884, 0, recent_sid, out, 100u) &&
          cache_turbo_lookup(0x1884, 0, 0x2000, out, 100u),
          "full cache pool evicts the greatest unsigned elapsed age across rollover");
    cache_turbo_get_stats(&stats);
    CHECK(stats.current_entries == CACHE_TURBO_MAX_ENTRIES &&
          stats.total_evictions == 1,
          "cache-pool replacement preserves live-entry count and records one eviction");

    printf("%d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
