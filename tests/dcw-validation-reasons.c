/* Test the production validation decision and the reported first failed gate.
 * cc -std=gnu99 -O1 -g -fpack-struct -ffunction-sections -fdata-sections
 *    -fsanitize=address,undefined -fno-sanitize-recover=all -Isrc
 *    tests/dcw-validation-reasons.c src/dcw.c -Wl,--gc-sections -o /tmp/dcw-reasons */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "dcw.h"

static int check(uint8_t *cw, int checksum, int repeat, int pattern,
                 enum dcw_validation_reason expected)
{
    enum dcw_validation_reason reason = (enum dcw_validation_reason)-1;
    int with_reason = acceptDCW_pattern_reason(cw, 0x1884, checksum, repeat, 0, pattern, &reason);
    int original = acceptDCW_pattern(cw, 0x1884, checksum, repeat, 0, pattern);
    assert(with_reason == original);
    assert(reason == expected);
    return with_reason;
}
int main(void)
{
    uint8_t good[16] = {1,2,3,6, 4,5,6,15, 7,8,9,24, 10,11,12,33};
    uint8_t invalid[16], zero[16] = {0};
    unsigned i;
    dcw_badlist_publish(NULL, 0);
    assert(check(good, DCWFILTER_ON, DCWFILTER_ON, DCWFILTER_OFF, DCW_VALID));
    memcpy(invalid, good, 16); invalid[3] ^= 1;
    assert(!check(invalid, DCWFILTER_ON, DCWFILTER_ON, DCWFILTER_OFF, DCW_INVALID_CHECKSUM));
    assert(!check(zero, DCWFILTER_OFF, DCWFILTER_OFF, DCWFILTER_OFF, DCW_INVALID_NULL));
    memcpy(invalid, good, 16); invalid[0] = invalid[1] = invalid[2] = 5;
    assert(!check(invalid, DCWFILTER_OFF, DCWFILTER_ON, DCWFILTER_OFF, DCW_INVALID_REPEAT));
    dcw_badlist_publish(good, 1);
    assert(!check(good, DCWFILTER_ON, DCWFILTER_ON, DCWFILTER_OFF, DCW_INVALID_BADLIST));
    dcw_badlist_publish(NULL, 0);
    memcpy(invalid, good, 8); memcpy(invalid + 8, good, 8);
    assert(!check(invalid, DCWFILTER_ON, DCWFILTER_OFF, DCWFILTER_ON, DCW_INVALID_PATTERN));
    for (i = 0; i < 10000; ++i) {
        enum dcw_validation_reason reason;
        unsigned j; uint32_t x = i + 12345;
        for (j = 0; j < 16; ++j) { x = x * 1664525u + 1013904223u; invalid[j] = x >> 24; }
        assert(acceptDCW_pattern(invalid, 0x1884, i%3, (i/3)%3, 0, (i/9)%3) ==
               acceptDCW_pattern_reason(invalid, 0x1884, i%3, (i/3)%3, 0, (i/9)%3, &reason));
    }
    puts("CW validation reason and decision equivalence passed");
    return 0;
}
