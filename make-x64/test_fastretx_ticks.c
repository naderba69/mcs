#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "../src/fastretx.h"

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

static int contains_hash(const struct fastretx_table *t, const uint8_t hash[16])
{
    int i;
    for (i = 0; i < FASTRETX_ENTRIES; i++) {
        if (t->entries[i].used && !memcmp(t->entries[i].ecm_hash, hash, 16))
            return 1;
    }
    return 0;
}

int main(void)
{
    struct fastretx_table table;
    uint8_t hash[16], old_hash[16], recent_hash[16];
    uint16_t caids[2], sids[2];
    uint32_t provids[2];
    int servers[2], i, count;
    uint32_t start = UINT32_MAX - 100u;

    printf("== fast-retransmit modulo tick tests ==\n");
    fastretx_init(&table, 1);
    make_hash(hash, 1);
    fastretx_record_request(&table, 0x1884, 0, 0x100, hash, 7, start);
    CHECK(fastretx_check(&table, 398u, caids, provids, sids, &hash, servers, 2) == 0 &&
          fastretx_check(&table, 399u, caids, provids, sids, &hash, servers, 2) == 1,
          "first retransmit deadline is 500 ms across rollover");
    fastretx_record_retx(&table, hash);
    CHECK(fastretx_check(&table, 898u, caids, provids, sids, &hash, servers, 2) == 0 &&
          fastretx_check(&table, 899u, caids, provids, sids, &hash, servers, 2) == 1,
          "second retransmit deadline is one second after the original send");
    fastretx_record_retx(&table, hash);
    CHECK(fastretx_check(&table, 1398u, caids, provids, sids, &hash, servers, 2) == 0 &&
          fastretx_check(&table, 1399u, caids, provids, sids, &hash, servers, 2) == 0 &&
          table.total_timeouts == 1 && !contains_hash(&table, hash),
          "timeout waits through the final retry interval and retires exactly once");
    fastretx_check(&table, 2000u, caids, provids, sids, &hash, servers, 2);
    CHECK(table.total_timeouts == 1,
          "repeated fast-retransmit polling does not inflate timeout statistics");

    fastretx_init(&table, 1);
    for (i = 0; i < FASTRETX_ENTRIES; i++) {
        uint32_t age = (i == 0) ? 50000u : (uint32_t)(FASTRETX_ENTRIES - i);
        make_hash(hash, (uint32_t)i + 1u);
        fastretx_record_request(&table, 0x1884, 0, (uint16_t)(0x200 + i),
                                hash, i, 100u - age);
        if (i == 0) memcpy(old_hash, hash, 16);
        if (i == 1) memcpy(recent_hash, hash, 16);
    }
    make_hash(hash, FASTRETX_ENTRIES + 1u);
    fastretx_record_request(&table, 0x1884, 0, 0x300, hash, 999, 100u);
    count = fastretx_check(&table, 100u, caids, provids, sids, &hash, servers, 2);
    CHECK(!contains_hash(&table, old_hash) && contains_hash(&table, recent_hash) &&
          contains_hash(&table, hash) && count == 0,
          "full retransmit table evicts greatest elapsed age across rollover");

    printf("%d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
