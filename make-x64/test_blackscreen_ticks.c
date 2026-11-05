#include <stdio.h>
#include <stdint.h>
#include "../src/blackscreen-detector.h"

static int passed, failed;

#define CHECK(cond, what) do { \
    if (cond) { passed++; printf("  PASS: %s\n", what); } \
    else { failed++; printf("  FAIL: %s\n", what); } \
} while (0)

int main(void)
{
    int i, first, second, old_found, recent_found, new_found;
    struct bs_channel_stats snapshot;
    uint32_t now = 100u;

    printf("== black-screen detector modulo tick tests ==\n");
    bs_init();
    bs_record_ecm_request(0x1884, 0, 0x100, 0);
    first = bs_record_cw_failure(0x1884, 0, 0x100, 2999u);
    second = bs_record_cw_failure(0x1884, 0, 0x100, 3001u);
    CHECK(first == 0 && second == 1 &&
          bs_is_blackscreen(0x1884, 0, 0x100, 3001u),
          "no-CW failure timing is based on the first ECM, including tick zero");

    bs_init();
    bs_record_cw_success(0x1884, 0, 0x101, UINT32_MAX - 1000u, 25);
    first = bs_record_cw_failure(0x1884, 0, 0x101, 1999u);
    second = bs_record_cw_failure(0x1884, 0, 0x101, 2000u);
    CHECK(first == 0 && second == 1 &&
          bs_is_blackscreen(0x1884, 0, 0x101, 2000u),
          "black-screen timeout crosses tick rollover with strict timeout boundary");

    bs_init();
    for (i = 0; i < BS_MAX_TRACKED_CHANNELS; i++) {
        uint32_t age = (i == 0) ? 50000u : (uint32_t)(BS_MAX_TRACKED_CHANNELS - i);
        bs_record_ecm_request(0x1884, 0, (uint16_t)(0x200 + i), now - age);
    }
    bs_record_ecm_request(0x1884, 0, 0x300, now);
    old_found = recent_found = new_found = 0;
    for (i = 0; i < BS_MAX_TRACKED_CHANNELS; i++) {
        if (!bs_channels[i].active) continue;
        if (bs_channels[i].sid == 0x200) old_found = 1;
        if (bs_channels[i].sid == 0x201) recent_found = 1;
        if (bs_channels[i].sid == 0x300) new_found = 1;
    }
    CHECK(!old_found && recent_found && new_found &&
          bs_stats.active_channels == BS_MAX_TRACKED_CHANNELS,
          "full black-screen channel table evicts greatest elapsed age across rollover");

    i = bs_channel_count;
    CHECK(!bs_get_channel_stats(0x1884, 0, 0x7fff, &snapshot) &&
          bs_channel_count == i,
          "statistics query does not allocate a phantom channel entry");
    CHECK(!bs_get_channel_stats(0x1884, 0, 0x300, NULL),
          "NULL channel-stat output is safely rejected");

    printf("%d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
