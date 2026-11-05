#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "../src/emm-cache.h"

static int passed, failed;

#define CHECK(condition, message) do { \
    if (condition) { passed++; printf("  PASS: %s\n", message); } \
    else { failed++; printf("  FAIL: %s\n", message); } \
} while (0)

int main(void)
{
    const uint8_t emm[] = {0x82, 0x10, 0x05, 0xaa, 0x55};
    uint8_t hash[16] = {0};
    emm_cache_stats_t stats;
    emm_cache_entry_t *entry;
    time_t future;
    int i;

    printf("== EMM cache regression tests ==\n");
    emm_cache_init(1, 1, 0);
    CHECK(emm_cache_should_write(0x0500, 0x123456, emm, sizeof(emm), 1),
          "first EMM is allowed and inserted");
    emm_cache_get_stats(&stats);
    CHECK(stats.current_entries == 1 && stats.peak_entries == 1,
          "adding an unused slot updates current and peak entry counts");
    emm_cache_record_success(0x0500, 0x123456, emm, sizeof(emm));
    CHECK(!emm_cache_should_write(0x0500, 0x123456, emm, sizeof(emm), 1),
          "successful first write is skipped at the configured rewrite limit");

    /* Simulate a wall-clock rollback: every occupied entry is in the future
     * relative to time(NULL). Full-table insertion must still find a victim. */
    future = time(NULL) + 3600;
    for (i = 0; i < EMM_CACHE_MAX_ENTRIES; i++) {
        emm_cache_entry_t *e = &g_emm_cache.entries[i];
        e->used = 1;
        e->first_written = future + i;
        e->last_written = future + i;
    }
    g_emm_cache.stats.current_entries = EMM_CACHE_MAX_ENTRIES;
    g_emm_cache.stats.peak_entries = EMM_CACHE_MAX_ENTRIES;
    entry = emm_cache_add(0x0500, 0x654321, hash, 2, 8);
    CHECK(entry == &g_emm_cache.entries[0],
          "full table evicts the oldest future-timestamp entry after clock rollback");
    emm_cache_get_stats(&stats);
    CHECK(stats.current_entries == EMM_CACHE_MAX_ENTRIES,
          "replacing an occupied entry leaves the entry count unchanged");

    printf("%d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
