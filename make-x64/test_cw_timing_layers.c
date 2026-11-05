#include <stdio.h>
#include "../src/cwvalidity.h"
#include "../src/zerolatency.h"
#include "../src/cwredundancy.h"
#include "../src/cwfingerprint.h"
#include "../src/chanpred.h"
#include "../src/cwretry.h"
#include "../src/zerohandoff.h"
#include "../src/cwemergency.h"
#include "../src/autoswitch.h"

static int passed, failed;

#define CHECK(cond, what) do { \
    if (cond) { passed++; printf("  PASS: %s\n", what); } \
    else { failed++; printf("  FAIL: %s\n", what); } \
} while (0)

int main(void)
{
    struct cwvalidity_table validity;
    struct zerolatency_table latency;
    struct cwredundancy_table redundancy;
    struct cwfingerprint_table fingerprint;
    struct chanpred_table prediction;
    struct cwretry_table retry;
    struct zerohandoff_table handoff;
    struct cwemergency_table emergency;
    struct autoswitch_table autoswitch;
    uint32_t now = 100u;
    int i, fill_ok;

    printf("== CW validity/zero-latency modulo tick tests ==\n");

    cwvalidity_init(&validity);
    cwvalidity_record_change(&validity, 0x1884, 0, 0x65, 0);
    cwvalidity_record_change(&validity, 0x1884, 0, 0x65, 5000u);
    cwvalidity_record_change(&validity, 0x1884, 0, 0x65, 10000u);
    cwvalidity_record_change(&validity, 0x1884, 0, 0x65, 15000u);
    CHECK(cwvalidity_window(&validity, 0x1884, 0, 0x65) == 7u,
          "validity records tick-zero changes and converts average ms to seconds");

    cwvalidity_init(&validity);
    cwvalidity_record_change(&validity, 0x1884, 0, 0x66, UINT32_MAX - 2000u);
    cwvalidity_record_change(&validity, 0x1884, 0, 0x66,
                             UINT32_MAX - 2000u + 5000u);
    cwvalidity_record_change(&validity, 0x1884, 0, 0x66,
                             UINT32_MAX - 2000u + 10000u);
    cwvalidity_record_change(&validity, 0x1884, 0, 0x66,
                             UINT32_MAX - 2000u + 15000u);
    CHECK(cwvalidity_window(&validity, 0x1884, 0, 0x66) == 7u,
          "validity intervals remain correct across tick rollover");

    cwvalidity_init(&validity);
    fill_ok = 1;
    for (i = 0; i < CWVALIDITY_CHANNELS; i++) {
        uint32_t age = (i == 0) ? 3000000000u : (uint32_t)(i + 1);
        cwvalidity_record_change(&validity, 0x1884, 0,
                                 (uint16_t)(0x100 + i), now - age);
        if (!cwvalidity_find(&validity, 0x1884, 0, (uint16_t)(0x100 + i)))
            fill_ok = 0;
    }
    CHECK(fill_ok, "validity table fills before eviction");
    cwvalidity_record_change(&validity, 0x1884, 0, 0x200, now);
    CHECK(cwvalidity_find(&validity, 0x1884, 0, 0x100) == NULL &&
          cwvalidity_find(&validity, 0x1884, 0, 0x101) != NULL,
          "validity table evicts the greatest unsigned elapsed age");

    zerolatency_init(&latency);
    zerolatency_record(&latency, 0x1884, 0, 0x65, UINT32_MAX - 3000u);
    zerolatency_record(&latency, 0x1884, 0, 0x65,
                       UINT32_MAX - 3000u + 5000u);
    zerolatency_record(&latency, 0x1884, 0, 0x65,
                       UINT32_MAX - 3000u + 10000u);
    {
        uint32_t predicted = UINT32_MAX - 3000u + 15000u;
        CHECK(zerolatency_check(&latency, 0x1884, 0, 0x65,
                                predicted - ZEROLATENCY_LEAD_MS) == 1 &&
              zerolatency_check(&latency, 0x1884, 0, 0x65,
                                predicted - ZEROLATENCY_LEAD_MS - 1u) == 0 &&
              zerolatency_check(&latency, 0x1884, 0, 0x65, predicted + 1u) == 0,
              "prepositioning is limited to the wrap-safe lead interval");
        CHECK(zerolatency_time_remaining(&latency, 0x1884, 0, 0x65,
                                         predicted - 200u) == 200u,
              "time remaining is reported in milliseconds across rollover");
        zerolatency_mark(&latency, 0x1884, 0, 0x65);
        CHECK(zerolatency_check(&latency, 0x1884, 0, 0x65,
                                predicted - 100u) == 0,
              "a prepositioned prediction is not sent twice");
    }

    zerolatency_init(&latency);
    zerolatency_record(&latency, 0x1884, 0, 0x66, UINT32_MAX - 2999u);
    zerolatency_record(&latency, 0x1884, 0, 0x66, UINT32_MAX - 1999u);
    zerolatency_record(&latency, 0x1884, 0, 0x66, UINT32_MAX - 999u);
    CHECK(zerolatency_find(&latency, 0x1884, 0, 0x66)->prediction_valid &&
          zerolatency_find(&latency, 0x1884, 0, 0x66)->predicted_next == 0 &&
          zerolatency_check(&latency, 0x1884, 0, 0x66, UINT32_MAX - 100u) == 1,
          "zero is a valid predicted tick, not the no-prediction sentinel");

    zerolatency_init(&latency);
    fill_ok = 1;
    for (i = 0; i < ZEROLATENCY_CHANNELS; i++) {
        uint32_t age = (i == 0) ? 3000000000u : (uint32_t)(i + 1);
        zerolatency_record(&latency, 0x1884, 0, (uint16_t)(0x100 + i),
                           now - age);
        if (!zerolatency_find(&latency, 0x1884, 0, (uint16_t)(0x100 + i)))
            fill_ok = 0;
    }
    CHECK(fill_ok, "zero-latency table fills before eviction");
    zerolatency_record(&latency, 0x1884, 0, 0x200, now);
    CHECK(zerolatency_find(&latency, 0x1884, 0, 0x100) == NULL &&
          zerolatency_find(&latency, 0x1884, 0, 0x101) != NULL,
          "zero-latency table evicts the greatest unsigned elapsed age");

    cwfingerprint_init(&fingerprint);
    fill_ok = 1;
    for (i = 0; i < CWFP_PEERS; i++) {
        uint32_t age = (i == 0) ? 3000000000u : (uint32_t)(i + 1);
        cwfingerprint_record_delivery(&fingerprint, i, now - age);
        if (!cwfingerprint_find(&fingerprint, i)) fill_ok = 0;
    }
    CHECK(fill_ok, "fingerprint table fills before eviction");
    cwfingerprint_record_delivery(&fingerprint, 1000, now);
    CHECK(cwfingerprint_find(&fingerprint, 0) == NULL &&
          cwfingerprint_find(&fingerprint, 1) != NULL,
          "fingerprint table evicts the greatest unsigned elapsed age");

    chanpred_init(&prediction);
    fill_ok = 1;
    for (i = 0; i < CHANPRED_CHANNELS; i++) {
        uint32_t age = (i == 0) ? 3000000000u : (uint32_t)(i + 1);
        chanpred_record(&prediction, 0x1884, 0, (uint16_t)(0x100 + i),
                        now - age);
        if (!chanpred_find(&prediction, 0x1884, 0, (uint16_t)(0x100 + i)))
            fill_ok = 0;
    }
    CHECK(fill_ok, "channel-prediction table fills before eviction");
    chanpred_record(&prediction, 0x1884, 0, 0x200, now);
    CHECK(chanpred_find(&prediction, 0x1884, 0, 0x100) == NULL &&
          chanpred_find(&prediction, 0x1884, 0, 0x101) != NULL,
          "channel-prediction table evicts the greatest unsigned elapsed age");

    cwredundancy_init(&redundancy);
    fill_ok = 1;
    for (i = 0; i < REDUNDANCY_CHANNELS; i++) {
        uint8_t cw[8] = {0};
        uint32_t age = (i == 0) ? 3000000000u : (uint32_t)(i + 1);
        cw[0] = (uint8_t)(i >> 8);
        cw[1] = (uint8_t)i;
        if (!cwredundancy_add(&redundancy, 0x1884, 0,
                              (uint16_t)(0x100 + i), cw, 50,
                              now - age, (uint8_t)i)) fill_ok = 0;
    }
    CHECK(fill_ok, "redundancy table fills before eviction");
    {
        uint8_t cw[8] = {0xff,0xff,0,0,0,0,0,0};
        cwredundancy_add(&redundancy, 0x1884, 0, 0x200, cw, 50, now, 0xff);
    }
    CHECK(cwredundancy_find(&redundancy, 0x1884, 0, 0x100) == NULL &&
          cwredundancy_find(&redundancy, 0x1884, 0, 0x101) != NULL,
          "redundancy table evicts the greatest unsigned elapsed age");

    cwretry_init(&retry);
    cwretry_record_attempt(&retry, 1, 0x1884, 0, 1, 1, UINT32_MAX);
    CHECK(!cwretry_allowed(&retry, 1, 0x1884, 0, 1, UINT32_MAX) &&
          cwretry_allowed(&retry, 1, 0x1884, 0, 1, 0),
          "retry cooldown deadline fires across the uint32 seconds rollover");
    cwretry_init(&retry);
    fill_ok = 1;
    for (i = 0; i < CWRETRY_ENTRIES; i++) {
        uint32_t age = (i == 0) ? 3000000000u : (uint32_t)(i + 1);
        if (!cwretry_record_attempt(&retry, (uint32_t)i + 1u, 0x1884, 0,
                                    (uint16_t)i, i, now - age)) fill_ok = 0;
    }
    CHECK(fill_ok, "retry table fills with live attempt records");
    cwretry_record_attempt(&retry, 1000, 0x1884, 0, 0x7000, 1000, now);
    CHECK(cwretry_find(&retry, 1, 0x1884, 0, 0) == NULL &&
          cwretry_find(&retry, 2, 0x1884, 0, 1) != NULL,
          "retry table evicts the greatest unsigned elapsed age");

    zerohandoff_init(&handoff, 1);
    {
        int backup = 900;
        for (i = 0; i < ZEROHANDOFF_CLIENTS; i++) {
            uint32_t age = (i == 0) ? 50000u : (uint32_t)(ZEROHANDOFF_CLIENTS - i);
            zerohandoff_register(&handoff, (uint32_t)i + 1u, 0x1884, 0,
                                 (uint16_t)(i + 1), (int)i, &backup, 1,
                                 now - age);
        }
        zerohandoff_register(&handoff, 1000, 0x1884, 0, 0x7000, 1000,
                             &backup, 1, now);
    }
    {
        int old_present = 0, recent_present = 0, new_present = 0;
        for (i = 0; i < ZEROHANDOFF_CLIENTS; i++) {
            if (handoff.clients[i].used && handoff.clients[i].client_ip == 1)
                old_present = 1;
            if (handoff.clients[i].used && handoff.clients[i].client_ip == 2)
                recent_present = 1;
            if (handoff.clients[i].used && handoff.clients[i].client_ip == 1000)
                new_present = 1;
        }
        CHECK(!old_present && recent_present && new_present,
              "zero-handoff client table evicts the greatest elapsed age across rollover");
    }

    cwemergency_init(&emergency, 1);
    cwemergency_record_failure(&emergency, 0x1884, 0, 0x300, 0);
    cwemergency_record_failure(&emergency, 0x1884, 0, 0x300, 0);
    cwemergency_record_failure(&emergency, 0x1884, 0, 0x300, 0);
    CHECK(cwemergency_trigger(&emergency, 0x1884, 0, 0x300, 0),
          "emergency trigger at tick zero is recorded as a real cooldown start");
    cwemergency_record_failure(&emergency, 0x1884, 0, 0x300, 10);
    cwemergency_record_failure(&emergency, 0x1884, 0, 0x300, 10);
    cwemergency_record_failure(&emergency, 0x1884, 0, 0x300, 10);
    CHECK(!cwemergency_trigger(&emergency, 0x1884, 0, 0x300, 29) &&
          cwemergency_trigger(&emergency, 0x1884, 0, 0x300, 30),
          "emergency cooldown obeys its exact modulo elapsed boundary");

    cwemergency_init(&emergency, 1);
    for (i = 0; i < CWEMERGENCY_CHANNELS; i++) {
        uint32_t age = (i == 0) ? 50000u : (uint32_t)(CWEMERGENCY_CHANNELS - i);
        cwemergency_record_failure(&emergency, 0x1884, 0,
                                   (uint16_t)(0x400 + i), now - age);
    }
    cwemergency_record_failure(&emergency, 0x1884, 0, 0x500, now);
    CHECK(cwemergency_find(&emergency, 0x1884, 0, 0x400) == NULL &&
          cwemergency_find(&emergency, 0x1884, 0, 0x401) != NULL &&
          cwemergency_find(&emergency, 0x1884, 0, 0x500) != NULL,
          "emergency channel table evicts the greatest elapsed age across rollover");

    autoswitch_init(&autoswitch, 1);
    for (i = 0; i < AUTOSWITCH_CHANNELS; i++) {
        uint32_t age = (i == 0) ? 50000u : (uint32_t)(AUTOSWITCH_CHANNELS - i);
        autoswitch_record(&autoswitch, 0x1884, 0,
                          (uint16_t)(0x600 + i), i, 1, now - age);
    }
    autoswitch_record(&autoswitch, 0x1884, 0, 0x700, 1000, 1, now);
    CHECK(autoswitch_find(&autoswitch, 0x1884, 0, 0x600) == NULL &&
          autoswitch_find(&autoswitch, 0x1884, 0, 0x601) != NULL &&
          autoswitch_find(&autoswitch, 0x1884, 0, 0x700) != NULL,
          "autoswitch channel table evicts the greatest elapsed age across rollover");

    autoswitch_init(&autoswitch, 1);
    for (i = 0; i < AUTOSWITCH_SOURCES; i++) {
        uint32_t age = (i == 0) ? 50000u : (uint32_t)(AUTOSWITCH_SOURCES - i);
        autoswitch_record(&autoswitch, 0x1884, 0, 0x800, i, 1, now - age);
    }
    autoswitch_record(&autoswitch, 0x1884, 0, 0x800, 1000, 1, now);
    {
        struct autoswitch_channel *c = autoswitch_find(&autoswitch, 0x1884, 0, 0x800);
        CHECK(c && !autoswitch_find_source(c, 0) && autoswitch_find_source(c, 1) &&
              autoswitch_find_source(c, 1000),
              "autoswitch source table evicts the greatest elapsed age across rollover");
    }

    autoswitch_init(&autoswitch, 1);
    for (i = 0; i < AUTOSWITCH_MIN_SAMPLES; i++) {
        autoswitch_record(&autoswitch, 0x1884, 0, 0x900, 1, 0, 0);
        autoswitch_record(&autoswitch, 0x1884, 0, 0x900, 2, 1, 0);
    }
    {
        struct autoswitch_channel *c = autoswitch_find(&autoswitch, 0x1884, 0, 0x900);
        if (c) {
            c->current_source = 1;
            c->last_switch = 0;
            c->switch_count = 1;
        }
        CHECK(autoswitch_should(&autoswitch, 0x1884, 0, 0x900,
                                AUTOSWITCH_COOLDOWN - 1u) == -1 &&
              autoswitch_should(&autoswitch, 0x1884, 0, 0x900,
                                AUTOSWITCH_COOLDOWN) == 2,
              "autoswitch cooldown honors a tick-zero switch and its exact deadline");
    }

    printf("%d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
