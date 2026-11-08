#include <stdio.h>
#include <stdint.h>
#include "../src/cachewarm.h"

static int passed, failed;

#define CHECK(cond, what) do { \
    if (cond) { passed++; printf("  PASS: %s\n", what); } \
    else { failed++; printf("  FAIL: %s\n", what); } \
} while (0)

static int has_sid(const struct cachewarm_table *t, uint16_t sid)
{
    int i;
    for (i = 0; i < t->count; i++) {
        if (t->channels[i].sid == sid) return 1;
    }
    return 0;
}

int main(void)
{
    struct cachewarm_table table;
    uint16_t caids[2], sids[2];
    uint32_t provids[2];
    int i, count;
    uint32_t now = 100u;

    printf("== cache-warm modulo tick tests ==\n");
    cachewarm_init(&table, 3);
    for (i = 0; i < CACHEWARM_MAX_CHANNELS; i++) {
        uint32_t age = (i == 0) ? 50000u : (uint32_t)(CACHEWARM_MAX_CHANNELS - i);
        cachewarm_update(&table, 0x1884, 0, (uint16_t)(0x100 + i), 0,
                         now - age);
    }
    cachewarm_update(&table, 0x1884, 0, 0x200, 0, now);
    CHECK(!has_sid(&table, 0x100) && has_sid(&table, 0x101) &&
          has_sid(&table, 0x200),
          "full cache-warm table evicts the greatest elapsed age across rollover");

    cachewarm_init(&table, 3);
    cachewarm_update(&table, 0x1884, 0, 0x300, 0, 0);
    count = cachewarm_tick(&table, 0, caids, provids, sids, 2);
    CHECK(count == 1 && sids[0] == 0x300,
          "never-requested channel warms immediately at tick zero");
    CHECK(cachewarm_tick(&table, 29u, caids, provids, sids, 2) == 0 &&
          cachewarm_tick(&table, 30u, caids, provids, sids, 2) == 1,
          "cache-warm cooldown distinguishes a tick-zero request from no request");

    cachewarm_init(&table, 3);
    cachewarm_update(&table, 0x1884, 0, 0x300, 0, UINT32_MAX - 10u);
    cachewarm_tick(&table, UINT32_MAX - 10u, caids, provids, sids, 2);
    CHECK(cachewarm_tick(&table, 18u, caids, provids, sids, 2) == 0 &&
          cachewarm_tick(&table, 19u, caids, provids, sids, 2) == 1,
          "cache-warm cooldown respects the exact modulo elapsed boundary across rollover");
    CHECK(cachewarm_tick(&table, 19u, NULL, provids, sids, 2) == 0 &&
          cachewarm_tick(&table, 19u, caids, provids, sids, 0) == 0,
          "cache-warm API safely ignores invalid output buffers and zero capacity");

    printf("%d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
