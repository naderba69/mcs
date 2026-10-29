/* Exercise the real configuration delivery gate with deliberately unaligned
 * profile storage. acceptDCW_pattern is stubbed to expose its checksum mode. */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <netinet/in.h>
#include <assert.h>
#include <stdio.h>
#include "common.h"
#include <poll.h>
#include "ecmdata.h"
#include "msg-cccam.h"
#include "config.h"
#include "dcw.h"
int acceptDCW_pattern(uint8_t *data, uint16_t caid, int checksum, int repeat, int modern, int pattern)
{
    (void)data; (void)caid; (void)repeat; (void)modern; (void)pattern;
    return checksum;
}
int main(void)
{
    static unsigned char raw[sizeof(struct cardserver_data) + 8] __attribute__((aligned(8)));
    struct cardserver_data *cs = (struct cardserver_data *)(void *)(raw + 1);
    uint8_t cw[16] = {0};
    cs->option.dcwfilter_checksum = DCWFILTER_OFF;
    cs->option.skipcwc = 1;
    cs->option.skipcwc_only_active = 1;
    cs->option.skipcwc_only[0] = 0x1136;
    cs->option.skipcwc_only_total = 1;
    assert(acceptDCW_profile_ca_sid(cw, cs, 0x1884, 0x1136) == DCWFILTER_ON);
    assert(acceptDCW_profile_ca_sid(cw, cs, 0x1884, 0x1137) == DCWFILTER_OFF);
    cs->option.skipcwc_excl_active = 1;
    cs->option.skipcwc_excl[0] = 0x1136;
    cs->option.skipcwc_excl_total = 1;
    assert(acceptDCW_profile_ca_sid(cw, cs, 0x1884, 0x1136) == DCWFILTER_OFF);
    cs->option.skipcwc_excl_active = 0;
    assert(acceptDCW_profile_ca_sid(cw, cs, 0x2610, 0x1136) == DCWFILTER_OFF);
    cs->option.skipcwc = 0;
    assert(acceptDCW_profile_ca_sid(cw, cs, 0x1884, 0x1136) == DCWFILTER_OFF);
    puts("packed profile SID gate passed");
    return 0;
}
