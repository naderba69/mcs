/* Execute the production cache walkers with a captured ECM pipe. */
#define main multics_unused_main
#include "../src/main.c"
#undef main
#include <assert.h>

static int sends, conflicts, source_conflicts, profile_checks, record_conflicts;
static uint32_t last_profile_provider;
static struct cardserver_data *last_profile;
struct cafamily_tab cafamily_tab;
uint64_t GetTickCount(void) { return 10000; }
uint32_t getdbgflag(int a,int b,int c) { (void)a;(void)b;(void)c;return 0; }
void debugf(uint32_t flag, char *fmt, ...) { (void)flag;(void)fmt; }
int acceptDCW_profile_ecm_trace_reason(uint8_t *cw, struct cardserver_data *cs,
                                       uint16_t caid, uint32_t provid, uint16_t sid,
                                       int *reason, int *exemption)
{ (void)cw;(void)caid;(void)sid; profile_checks++; last_profile=cs; last_profile_provider=provid;
  if (reason) *reason=DCW_VALID; if (exemption) *exemption=0; return 1; }
int checksumDCW(uint8_t *cw) { (void)cw; return 1; }
int isbadDCW(uint8_t *cw) { (void)cw; return 0; }
int dcw_profile_cycle_enabled(struct cardserver_data *cs) { (void)cs; return 0; }
int peerrep_note(uint32_t ip,uint16_t port,const char *reason) { (void)ip;(void)port;(void)reason;return 0; }
void peerrep_save(void) {}
int peerrep_stage(uint32_t ip,uint16_t port) { (void)ip;(void)port;return 0; }
void cwconsensus_set_min_sources(int n) { (void)n; }
int cwconsensus_verdict(uint16_t caid,uint16_t sid,uint32_t hash,uint8_t tag,
                        const uint8_t *cw,int peerid,int weight,uint32_t now,uint32_t window,
                        const uint8_t **loser)
{ (void)caid;(void)sid;(void)hash;(void)tag;(void)cw;(void)peerid;(void)weight;(void)now;(void)window;
  if (loser) *loser=NULL; return CWC_DELIVER; }
int cwconsensus_vote_counts(uint16_t caid,uint16_t sid,uint32_t hash,uint8_t tag,
                            const uint8_t *cw,uint32_t now,uint32_t window,
                            int *distinct,int *weight)
{ (void)caid;(void)sid;(void)hash;(void)tag;(void)cw;(void)now;(void)window;
  if (distinct) *distinct=0; if (weight) *weight=0; return 0; }
int cwconsensus_is_held(uint16_t caid,uint16_t sid,uint32_t hash,uint8_t tag,const uint8_t *cw)
{ (void)caid;(void)sid;(void)hash;(void)tag;(void)cw;return 0; }
void cwconsensus_capacity(uint32_t now,uint32_t window,struct cwconsensus_capacity *out)
{ (void)now;(void)window;memset(out,0,sizeof(*out)); }
void cwconsensus_hold_cycle(uint16_t caid,uint16_t sid,uint32_t hash,uint8_t tag,
                            const uint8_t *cw,int peerid,int cycle)
{ (void)caid;(void)sid;(void)hash;(void)tag;(void)cw;(void)peerid;(void)cycle; }
void user_trace_cache_observation(uint16_t a,uint32_t b,uint16_t c,uint32_t d,int e,const char *f,const uint8_t g[16])
{ (void)a;(void)b;(void)c;(void)d;(void)e;
  if (!strcmp(f,"cache forwarded CW to ECM pipe; delivery not confirmed")) sends++;
  if (!strcmp(f,"cache provider collision: different local provider; conflicting operation skipped")) { assert(g == NULL); conflicts++; }
  if (!strcmp(f,"cache provider collision: explicit cached provider differs from local request; conflicting operation skipped")) { assert(g == NULL); conflicts++; }
  if (!strcmp(f,"cache provider collision: known numeric providers differ; cross-cache relay skipped")) { assert(g == NULL); record_conflicts++; }
  if (!strcmp(f,"cache provider collision: explicit incoming provider conflicts with known cache/local provider; CW refused")) { source_conflicts++; }
}
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
    cfg.cache.threshold=2;
    cfg.cache.filter=0;
    cfg.cache.consensus=0;
    cfg.cache.gate_localonly=0;
    cfg.cache.gate_blockfake=0;
    cfg.cache.gate_cwcheck=0;
    /* A CacheEX source with a provider that conflicts with the waiting local
     * request must be refused before profile validation or cache mutation. */
    {
        struct cache_data entry={0}, source={0};
        ECM_DATA waiting={0};
        struct cardserver_data profile={0};
        uint8_t offered[16];
        memset(offered,0x31,sizeof(offered));
        entry.caid=source.caid=0x1884; entry.sid=source.sid=0x1136;
        entry.tag=source.tag=0x80; entry.hash=source.hash=0x1234;
        entry.recvtime=9000; entry.provid=1; entry.local_provider_set=1;
        entry.local_provider=1; entry.flags=CACHE_FLAG_SENDPIPE;
        waiting.dcwstatus=STAT_DCW_WAIT; waiting.cs=&profile; entry.ecm=&waiting;
        source.provid=2;
        table[entry.sid & MAX_CACHE_INDEX]=&entry;
        profile_checks=0;
        assert(cache_setdcw(&source,offered,NO_CYCLE,PEER_CACHEEX_SERVER|7)&DCW_ERROR);
        assert(source_conflicts==1 && profile_checks==0 && entry.cwdata==NULL);
        source.provid=0; /* zero is explicit in CacheEX, unlike providerless CSP */
        assert(cache_setdcw(&source,offered,NO_CYCLE,PEER_CACHEEX_SERVER|7)&DCW_ERROR);
        assert(source_conflicts==2 && profile_checks==0 && entry.cwdata==NULL);

        /* An explicitly-known provider zero in a cached entry must also be
         * distinguished from a missing provider field. */
        entry.local_provider_set=0; entry.local_provider=0; entry.flags=0;
        entry.ecm=NULL; entry.provid=0; entry.provider_known=1; source.provid=2;
        assert(cache_setdcw(&source,offered,NO_CYCLE,PEER_CACHEEX_SERVER|7)&DCW_ERROR);
        assert(source_conflicts==3 && profile_checks==0 && entry.cwdata==NULL);

        /* Even without a local waiter, two explicitly different nonzero
         * CacheEX provider values may not merge into one cache entry. */
        entry.provider_known=0; entry.provid=1; source.provid=2;
        assert(cache_setdcw(&source,offered,NO_CYCLE,PEER_CACHEEX_SERVER|7)&DCW_ERROR);
        assert(source_conflicts==4 && profile_checks==0 && entry.cwdata==NULL);

        /* CSP has no provider field. With a live local ECM, use that request's
         * provider for profile validation; after SENDPIPE is cleared, do not
         * trust the stale ECM pointer as a live profile association. */
        entry.local_provider_set=1; entry.local_provider=5; entry.provid=5;
        entry.flags=CACHE_FLAG_SENDPIPE; entry.ecm=&waiting; source.provid=0;
        source_conflicts=4; profile_checks=0;
        assert(cache_setdcw(&source,offered,NO_CYCLE,PEER_CSP|7)&DCW_ERROR);
        assert(profile_checks==1 && last_profile==&profile && last_profile_provider==5);
        free(entry.cwdata); entry.cwdata=NULL;
        entry.flags=0; profile_checks=0; last_profile=(struct cardserver_data *)1;
        assert(cache_setdcw(&source,offered,NO_CYCLE,PEER_CSP|7)&DCW_ERROR);
        assert(profile_checks==1 && last_profile==NULL && last_profile_provider==0);
        free(entry.cwdata); entry.cwdata=NULL;
    }
    cfg.cache.threshold=1;
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
    /* Explicit CacheEX metadata must be compared even without local waiter
     * associations; a known zero is not an unknown-provider wildcard. */
    target.local_provider_set=incoming.local_provider_set=0;
    target.provider_known=incoming.provider_known=1;
    target.provid=0; incoming.provid=1;
    target.sid=incoming.sid=0x1136; target.hash=1; incoming.hash=2;
    target.tag=incoming.tag=0x80; target.cwdata=NULL;
    cache_fetch_goodcw2(&incoming,cw,7);
    assert(sends==6 && record_conflicts==1);
    incoming.provid=0; incoming.provider_known=0;
    cache_fetch_goodcw2(&incoming,cw,7); assert(sends==7); /* unknown remains ambiguous */
    incoming.provider_known=1;
    cache_fetch_goodcw2(&incoming,cw,7); assert(sends==8); /* explicit zero matches */

    target.sid=0x1536; incoming.sid=0x1136;
    target.hash=incoming.hash=2; target.provid=1; incoming.provid=0;
    cache_fetch_goodcw3(&incoming,cw,7);
    assert(sends==8 && record_conflicts==2);
    incoming.provid=1; cache_fetch_goodcw3(&incoming,cw,7); assert(sends==9);

    /* The older-cycle walker must not reject based on a known other provider. */
    {
        struct cw_cache_data stored = {0}; cwcycle_t cycle = NO_CYCLE;
        memcpy(stored.cw, cw, sizeof(cw));
        target.sid=incoming.sid=0x1136; target.hash=1; incoming.hash=2;
        target.tag=0x80; incoming.tag=0x81; target.recvtime=1000;
        incoming.recvtime=9000; cfg.cache.filtertime=1000; target.cwdata=&stored;
        target.provider_known=incoming.provider_known=0;
        target.provid=incoming.provid=0;
        target.local_provider=0; target.local_provider_set=1;
        incoming.local_provider_set=1; incoming.local_provider=1;
        assert(cache_fetch_goodcw(&incoming,cw,&cycle)==0);
        incoming.local_provider=0;
        assert(cache_fetch_goodcw(&incoming,cw,&cycle)==DCW_ERROR);
        incoming.local_provider_set=0;
        assert(cache_fetch_goodcw(&incoming,cw,&cycle)==DCW_ERROR);

        /* The old-cycle walker must also keep distinct explicit CacheEX
         * providers separate, while preserving unknown CSP compatibility. */
        target.local_provider_set=0; target.provider_known=1; target.provid=0;
        incoming.provider_known=1; incoming.provid=1;
        assert(cache_fetch_goodcw(&incoming,cw,&cycle)==0);
        incoming.provid=0;
        assert(cache_fetch_goodcw(&incoming,cw,&cycle)==DCW_ERROR);
        incoming.provider_known=0;
        assert(cache_fetch_goodcw(&incoming,cw,&cycle)==DCW_ERROR);
    }
    assert(conflicts == 4);
    assert(record_conflicts == 2);
    puts("PASS: cross-cache relays respect known local and CacheEX provider identities");
    return 0;
}
