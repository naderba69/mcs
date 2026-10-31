/* Characterize (not endorse) the existing cache_check_cw rejection rule.
 * Includes the actual production implementation. Build with the x64 options
 * from src/Makefile plus -ffunction-sections -fdata-sections and --gc-sections.
 * This is an isolated deterministic test, not a production configuration. */
#define main multics_unused_main
#include "../src/main.c"
#undef main
#include <assert.h>

static uint32_t clock_ms = 100000;
uint64_t GetTickCount(void) { return clock_ms; }
int similarcw(uint8_t *a, uint8_t *b) { (void)a; (void)b; abort(); }
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
    /* Startup: now is below the 45s lifetime, but the old entry is live. */
    clock_ms = 3000; prior.recvtime = 1000; prior.local_provider = 0;
    cfg.cache.filtertime = 1000;
    assert(cache_check_cw(3000, 0x81, 0x1884, 0x5678, 0x1136, 0, cw, 2) == 0);
    /* Wrap across 2^32 milliseconds: a live old entry remains visible. */
    clock_ms = 2000; prior.recvtime = UINT32_MAX - 1999;
    assert(cache_check_cw(2000, 0x81, 0x1884, 0x5678, 0x1136, 0, cw, 2) == 0);
    /* Entry newer than the request is not an old cycle, even across wrap. */
    clock_ms = 2000; prior.recvtime = 1000;
    assert(cache_check_cw(UINT32_MAX - 100, 0x81, 0x1884, 0x5678, 0x1136, 0, cw, 2) == 1);
    /* Exact lifetime is still live; the following millisecond is expired. */
    prior.recvtime = 1000; clock_ms = 46000;
    assert(cache_check_cw(45000, 0x81, 0x1884, 0x5678, 0x1136, 0, cw, 2) == 0);
    clock_ms = 46001;
    assert(cache_check_cw(45000, 0x81, 0x1884, 0x5678, 0x1136, 0, cw, 2) == 1);
    /* The ordinary lookup shares the same wrap-safe expiry semantics. */
    {
        struct cache_data req = {0};
        req.caid = prior.caid = 0x1884; req.sid = prior.sid;
        req.tag = prior.tag; req.hash = prior.hash;
        prior.recvtime = UINT32_MAX - 999; clock_ms = 1000;
        assert(cache_fetch(&req) == &prior);
        clock_ms = 44001;
        assert(cache_fetch(&req) == NULL);
        /* The cycle check also sees live records at startup and wrap. */
        req.hash = 0x5678; req.tag = 0x81; req.recvtime = 3000;
        req.provid = prior.provid; cwcycle_t cycle = NO_CYCLE;
        prior.recvtime = 1000; clock_ms = 3000;
        assert(cache_fetch_goodcw(&req, cw, &cycle) == DCW_ERROR);
        prior.recvtime = UINT32_MAX - 1999; req.recvtime = 2000; clock_ms = 2000;
        assert(cache_fetch_goodcw(&req, cw, &cycle) == DCW_ERROR);
    }
    /* Recycling must neither reuse a live pre-wrap entry nor keep it forever. */
    {
        struct cache_data req = {0}, *first, *second, *third, *recycled;
        req.caid = 0x1884; req.sid = 0x1177; req.tag = 0x80;
        clock_ms = UINT32_MAX - 3000; req.hash = 1; first = cache_new(&req);
        clock_ms = UINT32_MAX - 2000; req.hash = 2; second = cache_new(&req);
        clock_ms = 1000; req.hash = 3; third = cache_new(&req);
        assert(first && second && third && third != first && third != second);
        clock_ms = 45000; req.hash = 4; recycled = cache_new(&req);
        assert(recycled == first && recycled->hash == 4);
    }
    puts("PASS: repeat and cycle checks, expiry and counter wrap");
    return 0;
}
