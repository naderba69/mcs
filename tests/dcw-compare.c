/* Equality checks for packed and unaligned CW buffers. */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "common.h"
#include "ecmdata.h"

int main(void)
{
    unsigned char a[40] __attribute__((aligned(8)));
    unsigned char b[40] __attribute__((aligned(8)));
    size_t offset, changed;
    for (offset = 0; offset < 8; offset++) {
        for (changed = 0; changed < 16; changed++) {
            memset(a, 0x51, sizeof(a)); memset(b, 0x51, sizeof(b));
            assert(dcwcmp8(a + offset, b + offset));
            assert(dcwcmp16(a + offset, b + offset));
            b[offset + changed] ^= 0x10;
            assert(dcwcmp8(a + offset, b + offset) == (changed >= 8));
            assert(!dcwcmp16(a + offset, b + offset));
        }
    }
    puts("unaligned CW comparisons passed");
    return 0;
}
