/* R59: proves the 098D (Sky DE, AES, no checksum bytes) rule in src/dcw.c.
 * Build: gcc -I src -o /tmp/t098d tests/r59-098d.c src/dcw.c && /tmp/t098d
 * Expected: CA MODERN ON accepts it (1), stock/OFF rejects it (0), CSA 1884 rejects it (0). */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "dcw.h"
void debugf(unsigned int flag, char *format, ...);
void debugf(unsigned int flag, char *format, ...) { (void)flag; (void)format; }
/* genuine-looking AES CW with NO checksum bytes that match (bytes 3,7,11,15 random) */
int main(void) {
    uint8_t cw[16] = {0x8A,0x4D,0x80,0x01,0xBA,0x70,0xA3,0x02,0xC7,0x0C,0x30,0x03,0x44,0x38,0xF4,0x05};
    int bad = (cw[3]!=((cw[0]+cw[1]+cw[2])&0xFF));
    int a_modern = acceptDCW_for_ca(cw, 0x098D, DCWFILTER_INHERIT, DCWFILTER_INHERIT, 1);
    int a_stock  = acceptDCW_for_ca(cw, 0x098D, DCWFILTER_INHERIT, DCWFILTER_INHERIT, 0);
    int a_csa_m  = acceptDCW_for_ca(cw, 0x1884, DCWFILTER_INHERIT, DCWFILTER_INHERIT, 1);
    printf("checksum bytes wrong: %d\n", bad);
    printf("098D  CA MODERN ON : accept=%d (expected 1)\n", a_modern);
    printf("098D  CA MODERN OFF: accept=%d (stock: expected 0)\n", a_stock);
    printf("1884  CA MODERN ON : accept=%d (CSA: expected 0)\n", a_csa_m);
    return 0;
}
