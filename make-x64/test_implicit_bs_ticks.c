#include <stdio.h>
#include <stdint.h>
#include <pthread.h>
#include "../src/implicit-bs-detect.h"

static int passed, failed;

#define CHECK(cond, what) do { \
    if (cond) { passed++; printf("  PASS: %s\n", what); } \
    else { failed++; printf("  FAIL: %s\n", what); } \
} while (0)

int main(void)
{
    uint32_t bad_source = 0;
    uint8_t bad_hash[8] = {0};
    int result;
    struct ibs_client *client;
    struct ibs_channel *channel;

    printf("== implicit black-screen modulo tick tests ==\n");
    ibs_init();

    ibs_on_cw_sent(1, 0x1884, 0, 0x100, 42, (const uint8_t *)"12345678", 0);
    result = ibs_on_ecm_request(1, 0x1884, 0, 0x100, 1000,
                                &bad_source, bad_hash);
    CHECK(result == 1 && bad_source == 42,
          "CW sent at tick zero remains eligible for rapid-rerequest detection");

    bad_source = 0;
    ibs_on_cw_sent(2, 0x1884, 0, 0x101, 43, (const uint8_t *)"abcdefgh",
                   UINT32_MAX - 1000u);
    result = ibs_on_ecm_request(2, 0x1884, 0, 0x101, 1000,
                                &bad_source, bad_hash);
    CHECK(result == 1 && bad_source == 43,
          "rapid-rerequest elapsed time crosses the uint32 rollover");

    ibs_on_ecm_request(3, 0x1884, 0, 0x102, 0, NULL, NULL);
    result = ibs_on_ecm_request(3, 0x1884, 0, 0x102, 1000, NULL, NULL);
    client = ibs_find_client(3);
    channel = client ? ibs_find_channel(client, 0x1884, 0, 0x102) : NULL;
    CHECK(result == 0 && channel && channel->avg_interval_ms == 1000,
          "first ECM request at tick zero establishes a valid interval baseline");

    pthread_mutex_destroy(&g_ibs.lock);
    printf("%d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
