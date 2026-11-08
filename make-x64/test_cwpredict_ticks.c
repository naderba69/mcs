#include <stdio.h>
#include "../src/cwpredict.h"

static int passed, failed;

#define CHECK(cond, what) do { \
    if (cond) { passed++; printf("  PASS: %s\n", what); } \
    else { failed++; printf("  FAIL: %s\n", what); } \
} while (0)

int main(void)
{
    struct cwpredict_table table;
    uint32_t predicted = 0;
    int i, fill_ok = 1;
    uint32_t now = 100u;

    printf("== CW prediction modulo tick tests ==\n");
    cwpredict_init(&table);
    cwpredict_record_change(&table, 0x1884, 0, 0x65, UINT32_MAX - 3000u);
    cwpredict_record_change(&table, 0x1884, 0, 0x65,
                            UINT32_MAX - 3000u + 5000u);
    cwpredict_record_change(&table, 0x1884, 0, 0x65,
                            UINT32_MAX - 3000u + 10000u);
    CHECK(cwpredict_cycle_ms(&table, 0x1884, 0, 0x65) == 5000u,
          "cycle average uses elapsed ticks across rollover");
    CHECK(cwpredict_next_valid(&table, 0x1884, 0, 0x65, &predicted) &&
          predicted == UINT32_MAX - 3000u + 15000u,
          "next prediction wraps modularly");
    CHECK(!cwpredict_should_prefetch(&table, 0x1884, 0, 0x65,
                                     predicted - CWPREDICT_EARLY_MS - 1u) &&
          cwpredict_should_prefetch(&table, 0x1884, 0, 0x65,
                                    predicted - CWPREDICT_EARLY_MS) &&
          !cwpredict_should_prefetch(&table, 0x1884, 0, 0x65, predicted + 1u),
          "prefetch is limited to the one-second interval before the deadline");

    cwpredict_init(&table);
    cwpredict_record_change(&table, 0x1884, 0, 0x66, UINT32_MAX - 2999u);
    cwpredict_record_change(&table, 0x1884, 0, 0x66, UINT32_MAX - 1999u);
    cwpredict_record_change(&table, 0x1884, 0, 0x66, UINT32_MAX - 999u);
    CHECK(cwpredict_next_valid(&table, 0x1884, 0, 0x66, &predicted) &&
          predicted == 0 &&
          cwpredict_should_prefetch(&table, 0x1884, 0, 0x66, UINT32_MAX - 499u),
          "a valid prediction at tick zero remains usable for prefetch");

    cwpredict_init(&table);
    for (i = 0; i < 10; i++)
        cwpredict_record_change(&table, 0x1884, 0, 0x67,
                                UINT32_MAX - 10000u + (uint32_t)i * 5000u);
    CHECK(cwpredict_cycle_ms(&table, 0x1884, 0, 0x67) == 5000u,
          "cycle history remains chronological after the ring wraps");

    cwpredict_init(&table);
    for (i = 0; i < CWPREDICT_CHANNELS; i++) {
        uint32_t age = (i == 0) ? 3000000000u : (uint32_t)(i + 1);
        cwpredict_record_change(&table, 0x1884, 0, (uint16_t)(0x100 + i),
                                now - age);
        if (!cwpredict_find(&table, 0x1884, 0, (uint16_t)(0x100 + i)))
            fill_ok = 0;
    }
    CHECK(fill_ok, "prediction table fills before eviction");
    cwpredict_record_change(&table, 0x1884, 0, 0x200, now);
    CHECK(cwpredict_find(&table, 0x1884, 0, 0x100) == NULL &&
          cwpredict_find(&table, 0x1884, 0, 0x101) != NULL,
          "prediction table evicts the greatest unsigned elapsed age");

    printf("%d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
