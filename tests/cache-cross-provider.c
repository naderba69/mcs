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
int cwconsensus_verdict(uint16_t caid,uint16_t sid,uint32_t provid,int provider_known,
                        uint32_t hash,uint8_t tag,const uint8_t *cw,int peerid,int weight,
                        uint32_t now,uint32_t window,const uint8_t **loser)
{ (void)caid;(void)sid;(void)provid;(void)provider_known;(void)hash;(void)tag;(void)cw;
  (void)peerid;(void)weight;(void)now;(void)window;
  if (loser) *loser=NULL; return CWC_DELIVER; }
int cwconsensus_vote_counts(uint16_t caid,uint16_t sid,uint32_t provid,int provider_known,
                            uint32_t hash,uint8_t tag,const uint8_t *cw,uint32_t now,
                            uint32_t window,int *distinct,int *weight)
{ (void)caid;(void)sid;(void)provid;(void)provider_known;(void)hash;(void)tag;
  (void)cw;(void)now;(void)window;
  if (distinct) *distinct=0; if (weight) *weight=0; return 0; }
int cwconsensus_is_held(uint16_t caid,uint16_t sid,uint32_t provid,int provider_known,
                        uint32_t hash,uint8_t tag,const uint8_t *cw)
{ (void)caid;(void)sid;(void)provid;(void)provider_known;(void)hash;(void)tag;(void)cw;return 0; }
void cwconsensus_capacity(uint32_t now,uint32_t window,struct cwconsensus_capacity *out)
{ (void)now;(void)window;memset(out,0,sizeof(*out)); }
void cwconsensus_hold_cycle(uint16_t caid,uint16_t sid,uint32_t provid,int provider_known,
                            uint32_t hash,uint8_t tag,const uint8_t *cw,int peerid,int cycle)
{ (void)caid;(void)sid;(void)provid;(void)provider_known;(void)hash;(void)tag;
  (void)cw;(void)peerid;(void)cycle; }
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
    {
        uint8_t wire[64];
        uint32_t scope_provider=99;
        int wire_len, expected_wire_len=16+(int)sizeof(void*);
        struct cache_data decoded={0}, scope={0}, existing={0}, incoming={0};
#ifdef CACHEEX
        expected_wire_len += 16;
#endif
        ECM_DATA local={0};

        local.ecm[0]=0x80; local.sid=0x1136; local.caid=0x1884;
        local.hash=0x12345678; local.provid=0; local.cw1cycle=0x80;
        local.provider_known=1;
        wire_len=put_ecm2cache(PIPE_CACHE_FIND,wire,&local,0,NULL);
        get_ecm2cache(wire,&decoded,NULL);
        assert(wire_len==expected_wire_len && decoded.provider_known &&
               decoded.provid==0 && decoded.cwcycle==CW1CYCLE);

        local.provider_known=0; local.cw1cycle=0;
        wire_len=put_ecm2cache(PIPE_CACHE_FIND,wire,&local,0,NULL);
        get_ecm2cache(wire,&decoded,NULL);
        assert(!decoded.provider_known && decoded.provid==0 && decoded.cwcycle==NO_CYCLE);

        assert(!cache_consensus_provider_scope(&scope,&scope_provider) && scope_provider==0);
        scope.provider_known=1; scope.provid=0;
        assert(cache_consensus_provider_scope(&scope,&scope_provider) && scope_provider==0);
        scope.local_provider_set=1; scope.local_provider=0x123456;
        assert(cache_consensus_provider_scope(&scope,&scope_provider) && scope_provider==0x123456);

        existing.provider_known=1; existing.provid=5;
        incoming.provid=0; incoming.provider_known=0;
        assert(!cache_local_provider_collision(&existing,&incoming));
        cache_attach_local_provider(&existing,&incoming);
        assert(existing.provider_known && existing.provid==5 && !existing.local_provider_set);
        incoming.provider_known=1;
        assert(cache_local_provider_collision(&existing,&incoming));
        conflicts=0; /* Ignore the helper's intentional trace in later totals. */
    }
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
    /* The CSP wire identity is provider-less (numeric zero), but this live
     * waiter is already attached to provider 5. Evidence from known provider
     * zero must not be applied to it, and evidence for provider 5 must not be
     * missed just because the CSP packet itself carries no provider. */
    {
        struct cache_data entry={0}, source={0};
        ECM_DATA waiting={0};
        struct cardserver_data profile={0};
        struct cwneg_ev nev;
        struct cwattest_ev aev;
        uint8_t offered0[16], offered5[16];
        unsigned long before;
        memset(offered0,0x41,sizeof(offered0));
        memset(offered5,0x42,sizeof(offered5));
        entry.caid=source.caid=0x1884; entry.sid=source.sid=0x1136;
        entry.tag=source.tag=0x80; entry.hash=source.hash=0x7171;
        entry.recvtime=9000; entry.provid=5; entry.provider_known=1;
        entry.local_provider_set=1; entry.local_provider=5;
        entry.flags=CACHE_FLAG_SENDPIPE; entry.ecm=&waiting;
        waiting.dcwstatus=STAT_DCW_WAIT; waiting.cs=&profile;
        source.provid=0; source.provider_known=0;
        table[entry.sid & MAX_CACHE_INDEX]=&entry;
        cfg.cache.threshold=2; cfg.cache.attest=0;

        cwneg_init(&cwneg_tab);
        cwneg_mark(&cwneg_tab,offered0,entry.caid,0, 1,entry.sid,
                   DCW_SOURCE_CACHE,1,1000);
        before=cwneg_hits;
        assert(cache_setdcw(&source,offered0,NO_CYCLE,PEER_CSP|7)&DCW_ERROR);
        assert(cwneg_hits==before); /* provider-0 proof cannot cross to known 5 */
        free(entry.cwdata); entry.cwdata=NULL;

        entry.hash=source.hash=0x7172;
        cwneg_init(&cwneg_tab);
        cwneg_mark(&cwneg_tab,offered5,entry.caid,5, 1,entry.sid,
                   DCW_SOURCE_CACHE,1,1000);
        before=cwneg_hits;
        assert(cache_setdcw(&source,offered5,NO_CYCLE,PEER_CSP|7)&DCW_ERROR);
        assert(cwneg_hits==before+1); /* provider-5 proof is retained for CSP */
        free(entry.cwdata); entry.cwdata=NULL;

        cwneg_init(&cwneg_tab); /* isolate attestation from earlier hard proof marks */
        cfg.cache.attest=2;
        entry.hash=source.hash=0x7173;
        cwattest_init(&cwattest_tab);
        cwattest_note(&cwattest_tab,offered0,entry.caid,0, 1,entry.sid,
                      DCW_SOURCE_CACHE,1,101,2,1000,CWATTEST_TTL_MS,&aev);
        cwattest_note(&cwattest_tab,offered0,entry.caid,0, 1,entry.sid,
                      DCW_SOURCE_CACHE,1,102,2,1100,CWATTEST_TTL_MS,&aev);
        before=cwattest_hits;
        assert(cache_setdcw(&source,offered0,NO_CYCLE,PEER_CSP|7)&DCW_ERROR);
        assert(cwattest_hits==before); /* provider-0 filing cannot cross to 5 */
        free(entry.cwdata); entry.cwdata=NULL;

        entry.hash=source.hash=0x7174;
        cwattest_init(&cwattest_tab);
        cwattest_note(&cwattest_tab,offered5,entry.caid,5, 1,entry.sid,
                      DCW_SOURCE_CACHE,1,101,2,1000,CWATTEST_TTL_MS,&aev);
        cwattest_note(&cwattest_tab,offered5,entry.caid,5, 1,entry.sid,
                      DCW_SOURCE_CACHE,1,102,2,1100,CWATTEST_TTL_MS,&aev);
        before=cwattest_hits;
        assert(cache_setdcw(&source,offered5,NO_CYCLE,PEER_CSP|7)&DCW_ERROR);
        assert(cwattest_hits==before+1); /* provider-5 filing is retained */
        free(entry.cwdata); entry.cwdata=NULL;
        cfg.cache.attest=0;

        /* Gate scope must distinguish an explicitly-known provider zero from
         * no provider context, for both hard proof memory and expiring
         * attestation. */
        entry.hash=source.hash=0x7175;
        entry.local_provider_set=1; entry.local_provider=0;
        entry.provid=0; entry.provider_known=1; entry.flags=0; entry.ecm=NULL;
        source.provid=0; source.provider_known=0;
        cwneg_init(&cwneg_tab);
        cwneg_mark(&cwneg_tab,offered0,entry.caid,0, 0,entry.sid,
                   DCW_SOURCE_CACHE,1,1000);
        before=cwneg_hits;
        assert(cache_setdcw(&source,offered0,NO_CYCLE,PEER_CSP|7)&DCW_ERROR);
        assert(cwneg_hits==before); /* unknown proof cannot become known zero */
        free(entry.cwdata); entry.cwdata=NULL;

        entry.hash=source.hash=0x7176;
        cwneg_init(&cwneg_tab);
        cwneg_mark(&cwneg_tab,offered0,entry.caid,0, 1,entry.sid,
                   DCW_SOURCE_CACHE,1,1000);
        before=cwneg_hits;
        assert(cache_setdcw(&source,offered0,NO_CYCLE,PEER_CSP|7)&DCW_ERROR);
        assert(cwneg_hits==before+1); /* known zero matches known zero */
        free(entry.cwdata); entry.cwdata=NULL;

        cwneg_init(&cwneg_tab); /* isolate attestation from provider-zero proof marks */
        cfg.cache.attest=2;
        entry.hash=source.hash=0x7177;
        cwattest_init(&cwattest_tab);
        cwattest_note(&cwattest_tab,offered0,entry.caid,0, 0,entry.sid,
                      DCW_SOURCE_CACHE,1,101,2,1000,CWATTEST_TTL_MS,&aev);
        cwattest_note(&cwattest_tab,offered0,entry.caid,0, 0,entry.sid,
                      DCW_SOURCE_CACHE,1,102,2,1100,CWATTEST_TTL_MS,&aev);
        before=cwattest_hits;
        assert(cache_setdcw(&source,offered0,NO_CYCLE,PEER_CSP|7)&DCW_ERROR);
        assert(cwattest_hits==before); /* unknown filing cannot become known zero */
        free(entry.cwdata); entry.cwdata=NULL;

        entry.hash=source.hash=0x7178;
        cwattest_init(&cwattest_tab);
        cwattest_note(&cwattest_tab,offered0,entry.caid,0, 1,entry.sid,
                      DCW_SOURCE_CACHE,1,101,2,1000,CWATTEST_TTL_MS,&aev);
        cwattest_note(&cwattest_tab,offered0,entry.caid,0, 1,entry.sid,
                      DCW_SOURCE_CACHE,1,102,2,1100,CWATTEST_TTL_MS,&aev);
        before=cwattest_hits;
        assert(cache_setdcw(&source,offered0,NO_CYCLE,PEER_CSP|7)&DCW_ERROR);
        assert(cwattest_hits==before+1); /* known-zero filing matches known zero */
        free(entry.cwdata); entry.cwdata=NULL;
        cfg.cache.attest=0;

        /* Without a local/metadata provider identity, CSP remains unknown:
         * it matches unknown evidence but cannot consume known-zero evidence. */
        entry.hash=source.hash=0x7179;
        entry.local_provider_set=0; entry.local_provider=0;
        entry.provid=0; entry.provider_known=0; entry.flags=0; entry.ecm=NULL;
        cwneg_init(&cwneg_tab);
        cwneg_mark(&cwneg_tab,offered0,entry.caid,0, 1,entry.sid,
                   DCW_SOURCE_CACHE,1,1000);
        before=cwneg_hits;
        assert(cache_setdcw(&source,offered0,NO_CYCLE,PEER_CSP|7)&DCW_ERROR);
        assert(cwneg_hits==before);
        free(entry.cwdata); entry.cwdata=NULL;
        entry.hash=source.hash=0x717a;
        cwneg_init(&cwneg_tab);
        cwneg_mark(&cwneg_tab,offered0,entry.caid,0, 0,entry.sid,
                   DCW_SOURCE_CACHE,1,1000);
        before=cwneg_hits;
        assert(cache_setdcw(&source,offered0,NO_CYCLE,PEER_CSP|7)&DCW_ERROR);
        assert(cwneg_hits==before+1);
        free(entry.cwdata); entry.cwdata=NULL;
    }
    /* The CSP reuse-proof branch has no provider field on the wire. Its
     * negative-memory mark must therefore use the cache entry's established
     * local identity, not the CSP request's numeric zero. */
    {
        struct cwreuse_table reuse={0};
        struct cache_data csp_context={0};
        uint8_t key[16], ecm_a[16]={1}, ecm_b[16]={2};
        uint32_t provider=0;
        int provider_known;
        memset(key,0x53,sizeof(key));
        csp_context.local_provider_set=1; csp_context.local_provider=5;
        provider_known=cache_consensus_provider_scope(&csp_context,&provider);
        assert(provider_known && provider==5);
        assert(cwreuse_offer_scoped(&reuse,key,ecm_a,1,0x1884,0x1001,
                                    provider,provider_known,1000)==CWREUSE_NONE);
        assert(cwreuse_offer_scoped(&reuse,key,ecm_b,1,0x1884,0x1002,
                                    provider,provider_known,2000)==CWREUSE_PROOF);
        cwreuse_reset(&reuse);
        assert(cwreuse_offer_scoped(&reuse,key,ecm_a,1,0x1884,0x1001,4,1,1000)==CWREUSE_NONE);
        assert(cwreuse_offer_scoped(&reuse,key,ecm_b,1,0x1884,0x1002,
                                    provider,provider_known,2000)==CWREUSE_NONE);
        cwneg_init(&cwneg_tab);
        cwn_note(DCW_SOURCE_CACHE,PEER_CSP|9,key,0x1884,provider,provider_known,0x1002,2000);
        assert(cwneg_hit(&cwneg_tab,key,0x1884,5,1,0x1003,2100,NULL)==1);
        assert(cwneg_hit(&cwneg_tab,key,0x1884,0,1,0x1003,2100,NULL)==0);

        /* CSP's wire provid is zero, but the stored cache record is attached
         * to provider 5. The reuse proof must sweep that effective local
         * scope; sweeping numeric wire zero would leave the proven candidate
         * available to later cache lookups. */
        {
            struct cache_data stored={0};
            struct cw_cache_data candidate={0};
            struct purge_table purge={0};
            stored.caid=0x1884; stored.sid=0x1002; stored.provid=provider;
            stored.provider_known=1; stored.local_provider_set=1;
            stored.local_provider=provider; stored.next=stored.prev=&stored;
            stored.cwdata=&candidate; candidate.peerid=PEER_CSP|9;
            table[stored.sid & MAX_CACHE_INDEX]=&stored;
            assert(cache_purge_mark_scoped(&purge,DCW_SOURCE_CACHE,PEER_CSP|9,
                                           stored.caid,stored.sid,provider,1,3000)==1);
            assert(cache_purge_sweep_scoped(&purge,DCW_SOURCE_CACHE,PEER_CSP|9,
                                            stored.caid,stored.sid,provider,1,DCW_ERROR)==1);
            assert(candidate.status & DCW_ERROR);
            table[stored.sid & MAX_CACHE_INDEX]=NULL;
        }
    }
    /* Exercise the production CSP reuse-proof helper with a providerless wire
     * request matched to a cache record attached to provider 5. All proof
     * effects, including trust lookup scope and the purge sweep, must use 5. */
    {
        struct cache_data stored={0}, req={0};
        struct cachepeer_data peer={0};
        struct host_data host={0};
        struct cw_cache_data *candidate;
        uint8_t key[16], ecm_a[16]={0xa1}, ecm_b[16]={0xb2};
        int idx=0x1002 & MAX_CACHE_INDEX;
        int status;

        memset(key,0x53,sizeof(key));
        stored.caid=req.caid=0x1884; stored.sid=req.sid=0x1002;
        stored.tag=req.tag=0x80; stored.hash=req.hash=0x7a01;
        stored.recvtime=9000; stored.provid=5; stored.provider_known=1;
        stored.local_provider_set=1; stored.local_provider=5;
        memcpy(stored.ecmd5,ecm_b,sizeof(ecm_b));
        stored.next=stored.prev=&stored;
        req.provid=0; req.provider_known=0; /* absent on CSP wire */
        table[idx]=&stored;

        peer.id=9; peer.host=&host; peer.port=12345;
        cfg.cache.alivetime=45000; cfg.cache.threshold=1;
        cfg.cache.filter=0; cfg.cache.consensus=0; cfg.cache.attest=0;
        cfg.cache.gate_localonly=0; cfg.cache.gate_blockfake=0;
        cfg.cache.gate_cwcheck=0; cfg.cache.dcwcheck2=0;
        cfg.cache.dcwcheck3=0; cfg.cache.forward=0;
        cwneg_init(&cwneg_tab); cache_purge_init(&purge_tab);
        cwreuse_reset(&cwreuse_tab); cwreuse_proofs=0;
        trust_reset(&trust_tab); trustagg_reset(&trustagg_tab);
        assert(cwreuse_offer_scoped(&cwreuse_tab,key,ecm_a,1,0x1884,0x1001,
                                    5,1,9000)==CWREUSE_NONE);

        /* The included production code runs synchronously here; no cache
         * worker is started in this isolated test process. */
        status=cache_setdcw(&req,key,NO_CYCLE,PEER_CSP|9);
        assert(!(status&DCW_ERROR) && stored.cwdata!=NULL);
        cache_csp_reuse_check(&peer,&req,key);

        assert(cwreuse_proofs==1);
        assert(trust_score_lk(&trust_tab,DCW_SOURCE_CACHE,PEER_CSP|9,
                              0x1884,5,0x1002)==TRUST_START-TRUST_PROOF_STEP);
        assert(trust_score_lk(&trust_tab,DCW_SOURCE_CACHE,PEER_CSP|9,
                              0x1884,0,0x1002)==-1);
        assert(cwneg_hit(&cwneg_tab,key,0x1884,5,1,0x1003,10001,NULL)==1);
        assert(cwneg_hit(&cwneg_tab,key,0x1884,0,1,0x1003,10001,NULL)==0);
        assert(cache_purge_should_withhold_scoped(&purge_tab,DCW_SOURCE_CACHE,
                    PEER_CSP|9,0x1884,0x1002,5,1)==1);
        assert(cache_purge_should_withhold_scoped(&purge_tab,DCW_SOURCE_CACHE,
                    PEER_CSP|9,0x1884,0x1002,0,0)==0);
        candidate=stored.cwdata;
        assert(candidate && (candidate->status&DCW_ERROR));
        table[idx]=NULL;
        free(candidate);
    }

    /* Scoped purge walks must not let an unknown-provider proof mark the
     * same numeric-zero channel entry whose provider is explicitly known, or
     * vice versa. Both records share the cache bucket and origin. */
    {
        struct cache_data known_zero={0}, unknown={0};
        struct cw_cache_data known_cw={0}, unknown_cw={0};
        struct purge_table purge={0};
        uint16_t sid=0x1003;
        int idx=sid & MAX_CACHE_INDEX;
        known_zero.caid=unknown.caid=0x1884;
        known_zero.sid=unknown.sid=sid;
        known_zero.provider_known=1;
        known_zero.local_provider_set=1; known_zero.local_provider=0;
        unknown.provid=0; unknown.provider_known=0;
        known_zero.next=&unknown; known_zero.prev=&unknown;
        unknown.next=&known_zero; unknown.prev=&known_zero;
        known_zero.cwdata=&known_cw; unknown.cwdata=&unknown_cw;
        known_cw.peerid=unknown_cw.peerid=PEER_CSP|11;
        table[idx]=&known_zero;

        assert(cache_purge_mark_scoped(&purge,DCW_SOURCE_CACHE,PEER_CSP|11,
                                       0x1884,sid,0,0,4000)==1);
        assert(cache_purge_sweep_scoped(&purge,DCW_SOURCE_CACHE,PEER_CSP|11,
                                        0x1884,sid,0,0,DCW_ERROR)==1);
        assert((unknown_cw.status & DCW_ERROR) && !(known_cw.status & DCW_ERROR));

        cache_purge_init(&purge);
        known_cw.status=unknown_cw.status=0;
        assert(cache_purge_mark_scoped(&purge,DCW_SOURCE_CACHE,PEER_CSP|11,
                                       0x1884,sid,0,1,4100)==1);
        assert(cache_purge_sweep_scoped(&purge,DCW_SOURCE_CACHE,PEER_CSP|11,
                                        0x1884,sid,0,1,DCW_ERROR)==1);
        assert((known_cw.status & DCW_ERROR) && !(unknown_cw.status & DCW_ERROR));
        table[idx]=NULL;
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
    puts("PASS: cache provider identities isolate CSP, CacheEX, negative-memory, attestation and reuse paths");
    return 0;
}
