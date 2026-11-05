#include <stdio.h>
#include <string.h>
#include "../src/dcwstrict.h"

struct dcwstrict_config dcwstrict_cfg;

static int passed, failed;

#define CHECK(cond, what) do { \
    if (cond) { passed++; printf("  PASS: %s\n", what); } \
    else { failed++; printf("  FAIL: %s\n", what); } \
} while (0)

int main(void)
{
    uint8_t cw[16];
    uint32_t now = 100u;
    int i;

    memset(cw, 0x5a, sizeof(cw));
    printf("== DCW strict modulo tick tests ==\n");

    dcwstrict_init();
    dcwstrict_add_stale(cw, 0x1884, 0, 0x65, UINT32_MAX - 1000u);
    CHECK(dcwstrict_check_stale(cw, 0x1884, 0, 0x65, 0xF9u) == 1,
          "stale-key age is measured across tick rollover");
    CHECK(dcwstrict_check_stale(cw, 0x1884, 0, 0x65, 0x1388u) == 0,
          "stale-key window expires at its strict greater-than boundary");

    dcwstrict_init();
    dcwstrict_add_stale(cw, 0x1884, 0, 0x65, 0);
    CHECK(dcwstrict_check_stale(cw, 0x1884, 0, 0x65, 1) == 1,
          "tick-zero stale entry is not mistaken for an unused slot");

    dcwstrict_init();
    for (i = 0; i < DCWSTRICT_MAX_STALE_ENTRIES; i++) {
        uint8_t key[16];
        uint32_t age = (i == 0) ? DCWSTRICT_STALE_WINDOW_MS :
                       (uint32_t)(DCWSTRICT_MAX_STALE_ENTRIES - i + 1);
        memset(key, 0, sizeof(key));
        key[0] = (uint8_t)(i >> 8);
        key[1] = (uint8_t)i;
        dcwstrict_add_stale(key, 0x1884, 0, (uint16_t)(i + 1), now - age);
    }
    {
        uint8_t new_key[16] = {0};
        uint8_t old_key[16] = {0};
        uint8_t recent_key[16] = {0};
        new_key[0] = 0xff; new_key[1] = 0xff;
        recent_key[0] = 0; recent_key[1] = 1;
        dcwstrict_add_stale(new_key, 0x1884, 0, 0x7000, now);
        old_key[0] = 0; old_key[1] = 0;
        CHECK(dcwstrict_check_stale(old_key, 0x1884, 0, 1, now) == 0,
              "full stale table evicts the greatest unsigned elapsed age");
        CHECK(dcwstrict_check_stale(recent_key, 0x1884, 0, 2, now) == 1,
              "full stale table preserves recently observed keys");
    }

    printf("%d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
