#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "../src/cwpropblock.h"
#include "../src/blackscreen.h"
#include "../src/cwfeedback.h"
#include "../src/cwpattern.h"

static int passed, failed;

static void make_pattern_cw(uint8_t cw[16], unsigned int seed)
{
    static const uint8_t offsets[16] = {
        0, 2, 4, 6, 9, 12, 16, 21, 27, 34, 42, 51, 61, 72, 84, 97
    };
    int i;
    for (i = 0; i < 16; i++) cw[i] = (uint8_t)(seed + offsets[i]);
}

#define CHECK(cond, what) do { \
    if (cond) { passed++; printf("  PASS: %s\n", what); } \
    else { failed++; printf("  FAIL: %s\n", what); } \
} while (0)

int main(void)
{
    struct cwpropblock_table blocks;
    struct blackscreen_table screens;
    struct cwfeedback_table feedback;
    struct cwpattern_library patterns;
    uint8_t cw[16], old_cw[16], recent_cw[16], new_cw[16];
    uint32_t start = UINT32_MAX - 1000u;
    int i, old_found, recent_found, new_found;

    printf("== R22 candidate modulo tick tests ==\n");
    memset(cw, 0x5a, sizeof(cw));
    cwpropblock_init(&blocks);
    CHECK(cwpropblock_add(&blocks, cw, 0x1884, 0, 0x100, start),
          "propagation block is recorded");
    CHECK(cwpropblock_check(&blocks, cw, 0x1884, 0, 0x100,
                            start + CWPROPBLOCK_TTL_MS),
          "five-minute propagation block uses milliseconds and includes the TTL boundary");
    CHECK(!cwpropblock_check(&blocks, cw, 0x1884, 0, 0x100,
                             start + CWPROPBLOCK_TTL_MS + 1u),
          "propagation block expires one millisecond past TTL across rollover");

    cwpropblock_init(&blocks);
    for (i = 0; i < CWPROPBLOCK_TABLE_SIZE; i++) {
        uint32_t age = (i == 0) ? 50000u :
                       (uint32_t)(CWPROPBLOCK_TABLE_SIZE - i);
        uint8_t entry_cw[16] = {0};
        entry_cw[0] = (uint8_t)(i >> 8);
        entry_cw[1] = (uint8_t)i;
        cwpropblock_add(&blocks, entry_cw, 0x1884, 0,
                        (uint16_t)(0x200 + i), 100u - age);
        if (i == 0) memcpy(old_cw, entry_cw, 16);
        if (i == 1) memcpy(recent_cw, entry_cw, 16);
    }
    memset(new_cw, 0xff, sizeof(new_cw));
    cwpropblock_add(&blocks, new_cw, 0x1884, 0, 0x300, 100u);
    old_found = cwpropblock_check(&blocks, old_cw, 0x1884, 0, 0x200, 100u);
    recent_found = cwpropblock_check(&blocks, recent_cw, 0x1884, 0, 0x201, 100u);
    new_found = cwpropblock_check(&blocks, new_cw, 0x1884, 0, 0x300, 100u);
    CHECK(!old_found && recent_found && new_found,
          "full propagation-block table evicts greatest elapsed age across rollover");

    cwfeedback_init(&feedback);
    cwfeedback_record_delivery(&feedback, 1, 0x1884, 0, 0x380, cw, 42, 0);
    CHECK(cwfeedback_on_ecm(&feedback, 1, 0x1884, 0, 0x380, 0) == 42,
          "client feedback correlates a delivery recorded at tick zero");
    cwfeedback_init(&feedback);
    cwfeedback_record_delivery(&feedback, 2, 0x1884, 0, 0x381, cw, 43, start);
    CHECK(cwfeedback_on_ecm(&feedback, 2, 0x1884, 0, 0x381,
                            start + CWFEEDBACK_WINDOW_MS) == 43,
          "five-second feedback window is measured in milliseconds across rollover");
    cwfeedback_record_delivery(&feedback, 3, 0x1884, 0, 0x382, cw, 44, start);
    CHECK(cwfeedback_on_ecm(&feedback, 3, 0x1884, 0, 0x382,
                            start + CWFEEDBACK_WINDOW_MS + 1u) == -1,
          "feedback outside the exact wrap-safe window is not attributed");

    cwfeedback_init(&feedback);
    for (i = 0; i < CWFEEDBACK_TABLE_SIZE; i++) {
        uint32_t age = (i == 0) ? 50000u :
                       (uint32_t)(CWFEEDBACK_TABLE_SIZE - i);
        uint8_t entry_cw[16] = {0};
        entry_cw[0] = (uint8_t)(i >> 8);
        entry_cw[1] = (uint8_t)i;
        cwfeedback_record_delivery(&feedback, (uint32_t)i + 1u, 0x1884, 0,
                                   (uint16_t)(0x400 + i), entry_cw, i,
                                   100u - age);
    }
    cwfeedback_record_delivery(&feedback, 1000, 0x1884, 0, 0x500,
                               new_cw, 1000, 100u);
    old_found = recent_found = new_found = 0;
    for (i = 0; i < CWFEEDBACK_TABLE_SIZE; i++) {
        if (!feedback.entries[i].used) continue;
        if (feedback.entries[i].client_ip == 1) old_found = 1;
        if (feedback.entries[i].client_ip == 2) recent_found = 1;
        if (feedback.entries[i].client_ip == 1000) new_found = 1;
    }
    CHECK(!old_found && recent_found && new_found,
          "full feedback table evicts greatest elapsed age across rollover");

    CHECK(!cwpattern_is_obvious_fake(NULL),
          "obvious-pattern check safely rejects a NULL CW pointer");
    cwpattern_init(&patterns);
    for (i = 0; i < CWPATTERN_LIBRARY_SIZE; i++) {
        uint32_t age = (i == 0) ? 50000u :
                       (uint32_t)(CWPATTERN_LIBRARY_SIZE - i);
        uint8_t entry_cw[16];
        make_pattern_cw(entry_cw, (unsigned int)i);
        cwpattern_learn(&patterns, entry_cw, 100u - age);
        if (i == 0) memcpy(old_cw, entry_cw, 16);
        if (i == 1) memcpy(recent_cw, entry_cw, 16);
    }
    make_pattern_cw(new_cw, 100u);
    cwpattern_learn(&patterns, new_cw, 100u);
    CHECK(!cwpattern_check(&patterns, old_cw) &&
          cwpattern_check(&patterns, recent_cw) &&
          cwpattern_check(&patterns, new_cw),
          "full fake-pattern library evicts greatest elapsed age across rollover");

    blackscreen_init(&screens);
    blackscreen_detect(&screens, 0x1884, 0, 0x400, start);
    CHECK(blackscreen_active(&screens, 0x1884, 0, 0x400,
                             start + BLACKSCREEN_WINDOW_MS),
          "black-screen window uses millisecond ticks through its exact boundary");
    CHECK(!blackscreen_active(&screens, 0x1884, 0, 0x400,
                              start + BLACKSCREEN_WINDOW_MS + 1u),
          "black-screen activity window expires after wrap-safe millisecond TTL");
    blackscreen_expire(&screens, start + 10000u, 10000u);
    CHECK(!screens.events[0].resolved,
          "black-screen expiry preserves the strict greater-than boundary");
    blackscreen_expire(&screens, start + 10001u, 10000u);
    CHECK(screens.events[0].resolved,
          "black-screen expiry uses modulo elapsed milliseconds");

    printf("%d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
