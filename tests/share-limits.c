/* Regression for the parser's per-CAID provider limit. */
#include <stdint.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "common.h"
#include "parser.h"
struct sharelimit_data { uint16_t caid; uint32_t provid; uint8_t uphops; };
extern char currentline[];
void parse_option_shares(void *limits, size_t size);
static int warnings;
uint32_t getdbgflag(int a, int b, int c) { (void)a; (void)b; (void)c; return 0; }
void debugf(uint32_t flag, char *fmt, ...) { (void)flag; (void)fmt; warnings++; }
int main(void)
{
    struct sharelimit_data limits[100];
    strcpy(currentline, "1884:01&02&03&04&05&06&07&08&09&0A&0B&0C&0D&0E&0F&10&11&12:2,0500:22:1;");
    iparser = currentline;
    memset(limits, 0, sizeof(limits));
    parse_option_shares(limits, sizeof(limits));
    assert(warnings == 1);
    for (int i = 0; i < 16; i++) {
        assert(limits[i].caid == 0x1884 && limits[i].provid == (unsigned)i + 1 && limits[i].uphops == 2);
    }
    assert(limits[16].caid == 0x0500 && limits[16].provid == 0x22 && limits[16].uphops == 1);
    assert(limits[17].caid == 0xffff);
    assert(*iparser == ';');
    /* The hundredth slot is reserved for the sentinel. */
    {
        struct { struct sharelimit_data values[100]; unsigned char guard[8]; } bounded;
        size_t used = 0;
        memset(&bounded, 0xa5, sizeof(bounded));
        currentline[0] = 0;
        for (int group = 0; group < 7; group++) {
            int n = snprintf(currentline + used, 10240 - used, "%s%04X:",
                             group ? "," : "", 0x1800 + group);
            assert(n > 0); used += (size_t)n;
            for (int p = 0; p < 16; p++) {
                n = snprintf(currentline + used, 10240 - used, "%s%02X", p ? "&" : "", p + 1);
                assert(n > 0); used += (size_t)n;
            }
            n = snprintf(currentline + used, 10240 - used, ":2");
            assert(n > 0); used += (size_t)n;
        }
        strcpy(currentline + used, ";");
        iparser = currentline;
        warnings = 0;
        parse_option_shares(bounded.values, sizeof(bounded.values));
        assert(warnings == 1);
        assert(bounded.values[98].caid == 0x1806 && bounded.values[98].provid == 3);
        assert(bounded.values[99].caid == 0xffff);
        for (size_t i = 0; i < sizeof(bounded.guard); i++) assert(bounded.guard[i] == 0xa5);
    }
    /* Packed parent storage can put the array at an unaligned address even
     * when struct sharelimit_data has its native alignment. */
    {
        unsigned char raw[2 + 3 * sizeof(struct sharelimit_data)];
        struct sharelimit_data copy[3];
        memset(raw, 0xa5, sizeof(raw));
        strcpy(currentline, "1884:01&02&03:2;");
        iparser = currentline;
        parse_option_shares(raw + 1, sizeof(copy));
        memcpy(copy, raw + 1, sizeof(copy));
        assert(raw[0] == 0xa5 && raw[sizeof(raw)-1] == 0xa5);
        assert(copy[0].caid == 0x1884 && copy[0].provid == 1);
        assert(copy[1].caid == 0x1884 && copy[1].provid == 2);
        assert(copy[2].caid == 0xffff);
        /* The terminating element's other bytes must remain unchanged. */
        for (size_t i = offsetof(struct sharelimit_data, caid) + sizeof(uint16_t);
             i < sizeof(struct sharelimit_data); i++)
            assert(raw[1 + 2 * sizeof(struct sharelimit_data) + i] == 0xa5);
        strcpy(currentline, "1884:01:2;");
        iparser = currentline;
        memset(raw, 0xa5, sizeof(raw));
        parse_option_shares(raw + 1, 0);
        assert(raw[1] == 0xa5 && iparser == currentline);
        parse_option_shares(raw + 1, sizeof(struct sharelimit_data));
        memcpy(copy, raw + 1, sizeof(copy));
        assert(copy[0].caid == 0xffff && raw[1 + sizeof(struct sharelimit_data)] == 0xa5);
    }
    puts("share provider and destination bounds passed");
    return 0;
}
