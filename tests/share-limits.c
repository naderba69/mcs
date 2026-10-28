/* Regression for the parser's per-CAID provider limit. */
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "common.h"
#include "parser.h"
struct sharelimit_data { uint16_t caid; uint32_t provid; uint8_t uphops; };
extern char currentline[];
void parse_option_shares(struct sharelimit_data *limits);
static int warnings;
uint32_t getdbgflag(int a, int b, int c) { (void)a; (void)b; (void)c; return 0; }
void debugf(uint32_t flag, char *fmt, ...) { (void)flag; (void)fmt; warnings++; }
int main(void)
{
    struct sharelimit_data limits[100];
    strcpy(currentline, "1884:01&02&03&04&05&06&07&08&09&0A&0B&0C&0D&0E&0F&10&11&12:2,0500:22:1;");
    iparser = currentline;
    memset(limits, 0, sizeof(limits));
    parse_option_shares(limits);
    assert(warnings == 1);
    for (int i = 0; i < 16; i++) {
        assert(limits[i].caid == 0x1884 && limits[i].provid == (unsigned)i + 1 && limits[i].uphops == 2);
    }
    assert(limits[16].caid == 0x0500 && limits[16].provid == 0x22 && limits[16].uphops == 1);
    assert(limits[17].caid == 0xffff);
    assert(*iparser == ';');
    puts("share provider limit passed");
    return 0;
}
