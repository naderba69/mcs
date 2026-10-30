/* Synthetic sustained load; no network or claim of receiver picture. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include "cwconsensus.h"

int main(void)
{
    int i, held = 0, proceeded = 0, refused = 0;
    uint8_t cw[16] = {1};
    const uint8_t *loser;
    struct cwconsensus_hold out;
    struct timespec start, end;
    cwconsensus_init();
    cwconsensus_set_min_sources(3);
    clock_gettime(CLOCK_MONOTONIC, &start);
    /* ~167 distinct arrivals per simulated second; a 600 ms window has
     * at most 101 active holds when the expiry walk runs every arrival. */
    for (i = 0; i < 200000; ++i) {
        uint32_t now = (uint32_t)(1000 + i * 6);
        int verdict;
        while (cwconsensus_expired(&out, now, 600)) { }
        verdict = cwconsensus_verdict(0x1884, (uint16_t)(1+i%4000),
                                      (uint32_t)i+1, 0x80, cw, i%7+1, 1,
                                      now, 600, &loser);
        if (verdict == CWC_HOLD) ++held;
        else if (verdict == CWC_DELIVER) ++proceeded;
        else ++refused;
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    printf("200000 votes: held=%d fail-open=%d refused=%d elapsed=%.3f s\n",
           held, proceeded, refused, (end.tv_sec-start.tv_sec) +
           (end.tv_nsec-start.tv_nsec)/1000000000.0);
    assert(held == 200000 && proceeded == 0 && refused == 0);
    return 0;
}
