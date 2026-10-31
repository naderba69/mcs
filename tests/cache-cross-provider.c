/* Execute the production cache walkers with a captured ECM pipe. */
#define main multics_unused_main
#include "../src/main.c"
#undef main
#include <assert.h>

static int sends, conflicts;
uint64_t GetTickCount(void) { return 10000; }
uint32_t getdbgflag(int a,int b,int c) { (void)a;(void)b;(void)c;return 0; }
void debugf(uint32_t flag, char *fmt, ...) { (void)flag;(void)fmt; }
void user_trace_cache_observation(uint16_t a,uint32_t b,uint16_t c,uint32_t d,int e,const char *f,const uint8_t g[16])
{ (void)a;(void)b;(void)c;(void)d;(void)e;(void)g; if (!strcmp(f,"cache forwarded CW to ECM pipe; delivery not confirmed")) sends++;
  if (!strcmp(f,"cache provider collision: different local provider; conflicting operation skipped")) { assert(g == NULL); conflicts++; } }
int isnullDCW(uint8_t *cw) { uint8_t z[16]={0}; return !memcmp(cw,z,16); }
int similarcw(uint8_t *a,uint8_t *b) { int i, n=0; for(i=0;i<8;i++) if(a[i]==b[i])n++; return n>3; }

int main(void)
{
    struct cache_data **table = getcachetabbycaid(0x1884);
    struct cache_data target = {0}, incoming = {0};
    uint8_t cw[16];
    assert(table);
    { int fds[2]; assert(pipe(fds) == 0); prg.pipe.ecm[0]=fds[0]; prg.pipe.ecm[1]=fds[1]; }
    cfg.cache.alivetime=45000;
    target.caid=incoming.caid=0x1884; target.sid=incoming.sid=0x1136;
    target.recvtime=incoming.recvtime=9000;
    target.tag=incoming.tag=0x80; target.hash=1; incoming.hash=2;
    target.flags=CACHE_FLAG_SENDPIPE; target.cwcycle=CW1CYCLE;
    memset(target.prevcw, 0x11, 8); memset(target.prevcw+8, 0x22, 8);
    memset(cw, 0x11, 8); memset(cw+8, 0x33, 8);
    target.local_provider_set=incoming.local_provider_set=1;
    target.local_provider=0; incoming.local_provider=1;
    table[target.sid & MAX_CACHE_INDEX]=&target;
    cache_fetch_goodcw2(&incoming,cw,7);
    assert(sends==0); /* must not relay a locally known different provider */
    incoming.local_provider=0; cache_fetch_goodcw2(&incoming,cw,7); assert(sends==1);
    incoming.local_provider_set=0; cache_fetch_goodcw2(&incoming,cw,7); assert(sends==2);
    incoming.local_provider_set=1; incoming.local_provider=0; target.local_provider=1;
    cache_fetch_goodcw2(&incoming,cw,7); assert(sends==2); /* reverse, including zero */
    target.local_provider_set=0; cache_fetch_goodcw2(&incoming,cw,7); assert(sends==3);
    target.local_provider_set=1; target.local_provider=0;
    target.sid=0x1536; target.hash=incoming.hash;
    incoming.local_provider_set=1; incoming.local_provider=1;
    cache_fetch_goodcw3(&incoming,cw,7); assert(sends==3);
    incoming.local_provider=0; cache_fetch_goodcw3(&incoming,cw,7); assert(sends==4);
    incoming.local_provider_set=0; cache_fetch_goodcw3(&incoming,cw,7); assert(sends==5);
    incoming.local_provider_set=1; target.local_provider=1;
    cache_fetch_goodcw3(&incoming,cw,7); assert(sends==5); /* reverse */
    target.local_provider_set=0; cache_fetch_goodcw3(&incoming,cw,7); assert(sends==6);
    /* The older-cycle walker must not reject based on a known other provider. */
    {
        struct cw_cache_data stored = {0}; cwcycle_t cycle = NO_CYCLE;
        memcpy(stored.cw, cw, sizeof(cw));
        target.sid=incoming.sid=0x1136; target.hash=1; incoming.hash=2;
        target.tag=0x80; incoming.tag=0x81; target.recvtime=1000;
        incoming.recvtime=9000; cfg.cache.filtertime=1000; target.cwdata=&stored;
        target.local_provider=0; target.local_provider_set=1;
        incoming.local_provider_set=1; incoming.local_provider=1;
        assert(cache_fetch_goodcw(&incoming,cw,&cycle)==0);
        incoming.local_provider=0;
        assert(cache_fetch_goodcw(&incoming,cw,&cycle)==DCW_ERROR);
        incoming.local_provider_set=0;
        assert(cache_fetch_goodcw(&incoming,cw,&cycle)==DCW_ERROR);
    }
    assert(conflicts == 4);
    puts("PASS: cross-cache relays respect known local provider conflicts");
    return 0;
}
