#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "../src/pipeline.h"
#include "../src/parquery.h"

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
    struct pipeline_table pipeline;
    struct parquery_table parquery;
    uint8_t hash[16], old_hash[16], recent_hash[16];
    int server = 7, i, old_found, recent_found, new_found;
    uint32_t now = 100u;

    printf("== pipeline/parquery modulo tick tests ==\n");
    pipeline_init(&pipeline);
    make_hash(hash, 1);
    pipeline_enqueue(&pipeline, hash, 0x1884, 0, 0x100, 1,
                     UINT32_MAX - 5u);
    pipeline_expire(&pipeline, 4u, 10u);
    CHECK(pipeline.queue[pipeline.tail].used,
          "pipeline entry remains live at the exact wrap-safe max age");
    pipeline_expire(&pipeline, 5u, 10u);
    CHECK(!pipeline.queue[pipeline.tail].used,
          "pipeline entry expires one tick beyond the wrap-safe max age");

    parquery_init(&parquery, 1);
    make_hash(hash, 1);
    parquery_dispatch(&parquery, hash, 0x1884, 0, 0x100, &server, 1,
                      UINT32_MAX - 5u);
    parquery_expire(&parquery, 4u, 10u);
    CHECK(parquery.entries[0].used,
          "parallel-query entry remains live at the exact wrap-safe max age");
    parquery_expire(&parquery, 5u, 10u);
    CHECK(!parquery.entries[0].used,
          "parallel-query entry expires one tick beyond the wrap-safe max age");

    parquery_init(&parquery, 1);
    for (i = 0; i < PARQUERY_ENTRIES; i++) {
        uint32_t age = (i == 0) ? 50000u : (uint32_t)(PARQUERY_ENTRIES - i);
        make_hash(hash, (uint32_t)i + 1u);
        parquery_dispatch(&parquery, hash, 0x1884, 0, (uint16_t)(0x200 + i),
                          &server, 1, now - age);
        if (i == 0) memcpy(old_hash, hash, 16);
        if (i == 1) memcpy(recent_hash, hash, 16);
    }
    make_hash(hash, PARQUERY_ENTRIES + 1u);
    parquery_dispatch(&parquery, hash, 0x1884, 0, 0x300, &server, 1, now);
    old_found = recent_found = new_found = 0;
    for (i = 0; i < PARQUERY_ENTRIES; i++) {
        if (!parquery.entries[i].used) continue;
        if (!memcmp(parquery.entries[i].ecm_hash, old_hash, 16)) old_found = 1;
        if (!memcmp(parquery.entries[i].ecm_hash, recent_hash, 16)) recent_found = 1;
        if (!memcmp(parquery.entries[i].ecm_hash, hash, 16)) new_found = 1;
    }
    CHECK(!old_found && recent_found && new_found,
          "full parallel-query table evicts the greatest elapsed age across rollover");

    printf("%d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
