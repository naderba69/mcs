#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "cwquarantine.h"
#include "clientbuf.h"
#include "cwreplace.h"
#include "cwcrosschannel.h"

static int failures;
#define CHECK(condition, message) do { \
    if (condition) printf("  PASS: %s\n", message); \
    else { printf("  FAIL: %s\n", message); failures++; } \
} while (0)

static void test_cwquarantine_ticks(void)
{
    struct cwquarantine_table table;
    struct cwquarantine_channel *channel;
    uint8_t cw[16] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
    uint8_t out[16];
    uint32_t held_at = UINT32_MAX - 1000u;
    int released;

    cwquarantine_init(&table, 3);
    CHECK(cwquarantine_hold(&table, 0x1884, 0, 0x0065, cw, held_at, 50, 0),
          "quarantine accepts a CW");
    channel = cwquarantine_find(&table, 0x1884, 0, 0x0065);
    CHECK(channel && channel->ring[0].expires_at == held_at + 3000u,
          "seconds are converted to millisecond ticks across wrap");
    released = cwquarantine_release(&table, 0x1884, 0, 0x0065,
                                     held_at + 2999u, out);
    CHECK(!released, "unverified CW stays held before modular deadline");
    released = cwquarantine_release(&table, 0x1884, 0, 0x0065,
                                     held_at + 3000u, out);
    CHECK(released && !memcmp(out, cw, sizeof(cw)),
          "CW releases at its modular deadline");

    CHECK(cwquarantine_hold(&table, 0x1884, 0, 0x0066, cw, held_at, 50, 0),
          "second quarantined CW is recorded");
    cwquarantine_expire(&table, held_at + 3000u, 3000u);
    channel = cwquarantine_find(&table, 0x1884, 0, 0x0066);
    CHECK(channel && channel->expired_count == 0,
          "elapsed-age expiry preserves the strict greater-than boundary");
    cwquarantine_expire(&table, held_at + 3001u, 3000u);
    CHECK(channel && channel->expired_count == 1,
          "elapsed-age expiry is wrap-safe");

    CHECK(cwquarantine_hold(&table, 0x1884, 0, 0x0067, cw, held_at, 50, 0),
          "verified-path CW is recorded");
    cwquarantine_verify(&table, 0x1884, 0, 0x0067, cw);
    released = cwquarantine_release(&table, 0x1884, 0, 0x0067,
                                     held_at + 1u, out);
    CHECK(released, "verification still releases before the timer");

    cwquarantine_init(&table, 3);
    CHECK(cwquarantine_find(&table, 0, 0, 0) == NULL,
          "unused zero-key slot is not exposed as a quarantine channel");
    CHECK(cwquarantine_hold(&table, 0, 0, 0, cw, 0, 50, 0) &&
          cwquarantine_find(&table, 0, 0, 0) != NULL,
          "channel key zero remains representable with an explicit used flag");

    cwquarantine_init(&table, 3);
    {
        uint32_t now = 100u;
        int i;
        for (i = 0; i < CWQUARANTINE_CHANNELS; i++) {
            uint32_t age = (i == 0) ? 50000u :
                           (uint32_t)(CWQUARANTINE_CHANNELS - i);
            cwquarantine_hold(&table, 0x1884, 0, (uint16_t)(0x400 + i),
                              cw, now - age, 50, 0);
        }
        cwquarantine_hold(&table, 0x1884, 0, 0x500, cw, now, 50, 0);
        CHECK(cwquarantine_find(&table, 0x1884, 0, 0x400) == NULL &&
              cwquarantine_find(&table, 0x1884, 0, 0x401) != NULL &&
              cwquarantine_find(&table, 0x1884, 0, 0x500) != NULL,
              "full quarantine channel table evicts greatest elapsed age across rollover");
    }
}

static void test_clientbuf_ticks(void)
{
    struct clientbuf_table table;
    uint8_t cw1[16], cw2[16], out[16];
    uint32_t first_at = UINT32_MAX - 10000u;
    uint32_t now;
    struct clientbuf_channel *channel;

    memset(cw1, 0x11, sizeof(cw1));
    memset(cw2, 0x22, sizeof(cw2));
    clientbuf_init(&table, 3);
    clientbuf_add(&table, 0x1884, 0, 0x0065, cw1, first_at, 30);
    clientbuf_add(&table, 0x1884, 0, 0x0065, cw2, first_at + 10u, 30);
    channel = clientbuf_find(&table, 0x1884, 0, 0x0065);
    CHECK(channel && channel->buffer[0].expires_at == first_at + 30000u,
          "client TTL seconds become milliseconds across wrap");

    now = first_at + 15u;
    CHECK(clientbuf_get(&table, 0x1884, 0, 0x0065, now, out) &&
          !memcmp(out, cw2, sizeof(cw2)),
          "modular age selects the most recent CW after rollover");
    CHECK(clientbuf_utilization(&table, 0x1884, 0, 0x0065, now) == 2,
          "utilization counts live entries across rollover");

    now = first_at + 30005u;
    CHECK(clientbuf_get(&table, 0x1884, 0, 0x0065, now, out) &&
          !memcmp(out, cw2, sizeof(cw2)),
          "older expired entry does not hide the newer live CW");
    CHECK(clientbuf_utilization(&table, 0x1884, 0, 0x0065, now) == 1,
          "utilization excludes an expired entry");

    now = first_at + 30010u;
    CHECK(!clientbuf_get(&table, 0x1884, 0, 0x0065, now, out),
          "CW is expired at the exact modular deadline");
    clientbuf_expire(&table, now);
    CHECK(clientbuf_utilization(&table, 0x1884, 0, 0x0065, now) == 0,
          "expiry sweep and utilization agree at deadline");

    clientbuf_add(&table, 0x1884, 0, 0x0066, cw1, now, UINT32_MAX);
    channel = clientbuf_find(&table, 0x1884, 0, 0x0066);
    CHECK(channel && (uint32_t)(channel->buffer[0].expires_at - now) <= INT32_MAX,
          "extreme TTL is capped within signed modular deadline range");
}

static void test_cwreplace_ticks(void)
{
    struct cwreplace_table table;
    uint8_t old_cw[16], recent_cw[16], fake_cw[16], out[16];
    memset(old_cw, 0x11, sizeof(old_cw));
    memset(recent_cw, 0x22, sizeof(recent_cw));
    memset(fake_cw, 0x33, sizeof(fake_cw));
    cwreplace_init(&table);
    cwreplace_record(&table, 0x1884, 0, 0x0065, old_cw, UINT32_MAX-20u, 1);
    cwreplace_record(&table, 0x1884, 0, 0x0065, recent_cw, 10u, 1);
    CHECK(cwreplace_offer(&table, 0x1884, 0, 0x0065, fake_cw, out,
                          20u, 100u) && !memcmp(out, recent_cw, sizeof(out)),
          "replacement chooses the most recent CW across rollover");
}

static void test_cwcrosschannel_eviction_ticks(void)
{
    struct cwcc_table table;
    uint8_t old_cw[16], recent_cw[16], new_cw[16];
    uint32_t now = 100u;
    int i, allocations_ok = 1;

    cwcrosschannel_init(&table);
    memset(new_cw, 0xff, sizeof(new_cw));
    CHECK(cwcc_alloc(NULL, new_cw, now) == NULL &&
          cwcc_alloc(&table, NULL, now) == NULL,
          "cross-channel allocator rejects NULL inputs");
    memset(old_cw, 0, sizeof(old_cw));
    memset(recent_cw, 0, sizeof(recent_cw));
    for (i = 0; i < CWCC_ENTRIES; i++) {
        uint8_t cw[16] = {0};
        uint32_t age = (i == 0) ? 3000000000u : (uint32_t)(i + 1);
        struct cwcc_entry *entry;
        cw[0] = (uint8_t)(i >> 8);
        cw[1] = (uint8_t)(i + 1);
        if (i == 0) memcpy(old_cw, cw, sizeof(cw));
        if (i == 1) memcpy(recent_cw, cw, sizeof(cw));
        entry = cwcc_alloc(&table, cw, now-age);
        if (!entry) allocations_ok = 0;
        else entry->last_seen = now-age;
    }
    CHECK(allocations_ok, "cross-channel table fills before eviction");
    memset(new_cw, 0xff, sizeof(new_cw));
    CHECK(cwcc_alloc(&table, new_cw, now) != NULL,
          "full cross-channel table accepts the next entry");
    CHECK(cwcc_find(&table, old_cw) == NULL && cwcc_find(&table, recent_cw) != NULL,
          "cross-channel eviction selects greatest unsigned elapsed age");
}

int main(void)
{
    printf("== CW timer/eviction tick tests ==\n");
    test_cwquarantine_ticks();
    test_clientbuf_ticks();
    test_cwreplace_ticks();
    test_cwcrosschannel_eviction_ticks();
    printf("%s: %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
