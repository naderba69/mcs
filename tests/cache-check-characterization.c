/* Characterize (not endorse) the existing cache_check_cw rejection rule.
 * Includes the actual production implementation. Build with the x64 options
 * from src/Makefile plus -ffunction-sections -fdata-sections and --gc-sections.
 * This is an isolated deterministic test, not a production configuration. */
#define main multics_unused_main
#include "../src/main.c"
#undef main
#include <assert.h>

uint64_t GetTickCount(void) { return 100000; }
uint32_t getdbgflag(int a, int b, int c) { (void)a; (void)b; (void)c; return 0; }
void debugf(uint32_t flag, char *format, ...) { (void)flag; (void)format; }
int main(void)
{
    struct cache_data **table = getcachetabbycaid(0x1884);
    struct cache_data prior = {0};
    struct cw_cache_data stored = {0};
    uint8_t cw[16];
    assert(table);
    memset(cw, 0x51, sizeof(cw));
    memcpy(stored.cw, cw, sizeof(cw));
    prior.sid = 0x1136; prior.provid = 1; prior.tag = 0x80;
    prior.hash = 0x1234; prior.recvtime = 88000; prior.cwdata = &stored;
    table[prior.sid & MAX_CACHE_INDEX] = &prior;
    cfg.cache.filter = 0;
    cfg.cache.alivetime = 45000;
    cfg.cache.filtertime = 10000;
    /* Ten seconds is not enough; one millisecond later the same word is
     * rejected for an opposite-tag ECM with a different hash. Entries with
     * no local provider association retain the historical conservative rule. */
    assert(cache_check_cw(98000, 0x81, 0x1884, 0x5678, 0x1136, 2, cw, 2) == 1);
    assert(cache_check_cw(99000, 0x81, 0x1884, 0x5678, 0x1136, 2, cw, 2) == 0);
    assert(cache_check_cw(99000, 0x80, 0x1884, 0x5678, 0x1136, 2, cw, 2) == 1);
    /* Locally associated provider 1 must not reject provider 2, but must
     * continue rejecting the same older CW for provider 1. Zero is a value,
     * not a wildcard, in a local association. CSP-only is still ambiguous. */
    prior.local_provider_set = 1;
    prior.local_provider = 1;
    assert(cache_check_cw(99000, 0x81, 0x1884, 0x5678, 0x1136, 2, cw, 2) == 1);
    assert(cache_check_cw(99000, 0x81, 0x1884, 0x5678, 0x1136, 1, cw, 2) == 0);
    assert(cache_check_cw(99000, 0x81, 0x1884, 0x5678, 0x1136, 0, cw, 2) == 1);
    prior.local_provider = 0;
    assert(cache_check_cw(99000, 0x81, 0x1884, 0x5678, 0x1136, 0, cw, 2) == 0);
    assert(cache_check_cw(99000, 0x81, 0x1884, 0x5678, 0x1136, 1, cw, 2) == 1);
    puts("PASS: provider-aware local repeat check; unknown source retains legacy behavior");
    return 0;
}
