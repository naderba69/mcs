#include <stdio.h>
#include <stdint.h>
#include "../src/cache-prefetch.h"

static int passed, failed;

#define CHECK(cond, what) do { \
    if (cond) { passed++; printf("  PASS: %s\n", what); } \
    else { failed++; printf("  FAIL: %s\n", what); } \
} while (0)

static int active_sid(uint16_t sid)
{
    int i;
    for (i = 0; i < prefetch_queue_count; i++) {
        if (prefetch_queue[i].active && prefetch_queue[i].sid == sid) return 1;
    }
    return 0;
}

int main(void)
{
    uint32_t now = 100u;
    int i;

    printf("== cache-prefetch modulo tick tests ==\n");
    prefetch_init();
    for (i = 0; i < PREFETCH_MAX_QUEUE; i++) {
        uint32_t age = (i == 0) ? 50000u : (uint32_t)(PREFETCH_MAX_QUEUE - i);
        prefetch_add_to_queue(0x1884, 0, (uint16_t)(0x100 + i),
                              now - age, 2);
    }
    prefetch_add_to_queue(0x1884, 0, 0x200, now, 1);
    CHECK(!active_sid(0x100) && active_sid(0x101) && active_sid(0x200),
          "full prefetch queue evicts greatest unsigned elapsed age across rollover");
    CHECK(prefetch_queue_count == PREFETCH_MAX_QUEUE &&
          prefetch_stats.current_queue_size == PREFETCH_MAX_QUEUE &&
          prefetch_stats.total_prefetch_misses == 1,
          "queue-capacity eviction keeps active-count and miss statistics consistent");

    prefetch_init();
    prefetch_add_to_queue(0x1884, 0, 0x300, UINT32_MAX - 5u, 2);
    prefetch_cleanup(4994u);
    CHECK(active_sid(0x300) && prefetch_stats.current_queue_size == 1,
          "prefetch request remains live at its exact wrap-safe cooldown boundary");
    prefetch_cleanup(4995u);
    CHECK(!active_sid(0x300) && prefetch_stats.current_queue_size == 0 &&
          prefetch_stats.total_prefetch_misses == 1,
          "prefetch cleanup expires once after wrap-safe cooldown boundary");

    printf("%d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
