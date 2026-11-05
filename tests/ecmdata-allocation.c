/* Deterministically fail each fresh ECM-slot allocation and verify no list corruption. */
#define main multics_unused_main
#include "../src/main.c"
#undef main

#include <assert.h>
#include <stdlib.h>

static int fail_next_allocation;
void *__real_malloc(size_t size);

/* The allocation path uses the production logger; keep this isolated test's
 * logging sink local rather than pulling the web log viewer into the fixture. */
void log_viewer_add(int level, const char *source, const char *format, ...)
{
    (void)level;
    (void)source;
    (void)format;
}

void *__wrap_malloc(size_t size)
{
    if (fail_next_allocation) {
        fail_next_allocation = 0;
        return NULL;
    }
    return __real_malloc(size);
}

static void fail_one_allocation(void)
{
    assert(!fail_next_allocation);
    fail_next_allocation = 1;
}

int main(void)
{
    struct cardserver_data cs;
    uint8_t raw_ecm[32];
    struct ecm_request *first, *second, *third, *unknown_zero, *known_zero, *known_nonzero, *reused;
    uint8_t provider_ecm[8] = {0x80, 0, 5, 0, 0, 0, 0, 0};
    uint32_t extracted_provider = 0xFFFFFFFFu;
    int i;

    memset(&cs, 0, sizeof(cs));
    pthread_mutex_init(&prg.lockcache, NULL);
    for (i = 0; i < (int)sizeof(raw_ecm); i++) raw_ecm[i] = (uint8_t)(i * 17 + 3);
    init_ecmdata();
    totalecm = 0;

    /* A valid provider field may numerically be zero; no-provider CA families
     * and malformed ECMs instead report that the value is unknown. */
    assert(ecm_getprovid_known(provider_ecm, sizeof(provider_ecm), 0x0100,
                               &extracted_provider) && extracted_provider==0);
    assert(!ecm_getprovid_known(provider_ecm, sizeof(provider_ecm), 0x1800,
                                &extracted_provider) && extracted_provider==0);
    extracted_provider=0xFFFFFFFFu;
    assert(!ecm_getprovid_known(provider_ecm, 4, 0x0100, &extracted_provider) &&
           extracted_provider==0);

    fail_one_allocation();
    assert(store_ecmdata(&cs, raw_ecm, sizeof(raw_ecm), 0x1136, 0x1884, 0) == NULL);
    assert(ecmdata == NULL && cs.ecmdata == NULL);
    assert(totalecm == 0 && cs.totalecm == 0);

    first = store_ecmdata(&cs, raw_ecm, sizeof(raw_ecm), 0x1136, 0x1884, 0);
    assert(first && ecmdata == first && first->next == NULL && first->prev == NULL);
    assert(totalecm == 1 && cs.totalecm == 1 && cs.ecmdata == first);

    fail_one_allocation();
    assert(store_ecmdata(&cs, raw_ecm, sizeof(raw_ecm), 0x1137, 0x1884, 0) == NULL);
    assert(ecmdata == first && first->next == NULL && first->prev == NULL);
    assert(cs.ecmdata == first && totalecm == 1 && cs.totalecm == 1);

    second = store_ecmdata(&cs, raw_ecm, sizeof(raw_ecm), 0x1137, 0x1884, 0);
    assert(second && second != first && totalecm == 2 && cs.totalecm == 2);
    assert(second->next == first && second->prev == first);
    assert(first->next == second && first->prev == second);

    fail_one_allocation();
    assert(store_ecmdata(&cs, raw_ecm, sizeof(raw_ecm), 0x1138, 0x1884, 0) == NULL);
    assert(ecmdata == second && second->next == first && second->prev == first);
    assert(first->next == second && first->prev == second);
    assert(cs.totalecm == 2 && totalecm == 2);

    third = store_ecmdata(&cs, raw_ecm, sizeof(raw_ecm), 0x1138, 0x1884, 0);
    assert(third && third != first && third != second);
    assert(totalecm == 3 && cs.totalecm == 3);
    assert(third->next == second && third->prev == first);
    assert(second->prev == third && first->next == third);
    assert(first->provid==0 && first->provider_known); /* legacy protocol API has an explicit provider field */

    unknown_zero = store_ecmdata_known(&cs, raw_ecm, sizeof(raw_ecm), 0x1139, 0x1800, 0, 0);
    known_zero = store_ecmdata_known(&cs, raw_ecm, sizeof(raw_ecm), 0x1139, 0x1800, 0, 1);
    known_nonzero = store_ecmdata_known(&cs, raw_ecm, sizeof(raw_ecm), 0x1139, 0x1800, 0x123456, 0);
    assert(unknown_zero && !unknown_zero->provid && !unknown_zero->provider_known);
    assert(known_zero && !known_zero->provid && known_zero->provider_known);
    assert(known_nonzero && known_nonzero->provid==0x123456 && known_nonzero->provider_known);

    /* Cache-response deduplication must not join an unknown zero to an
     * explicit provider zero, even when every other ECM identity byte agrees. */
    assert(search_ecmdata_any(&cs, raw_ecm, sizeof(raw_ecm), 0x1139, 0x1800, 0, 0) == unknown_zero);
    assert(search_ecmdata_any(&cs, raw_ecm, sizeof(raw_ecm), 0x1139, 0x1800, 0, 1) == known_zero);
    assert(search_ecmdata_any(&cs, raw_ecm, sizeof(raw_ecm), 0x1139, 0x1800, 0x123456, 0) == known_nonzero);

    /* The CW duplicate check has the same two identities: only a prior key
     * in the matching known/unknown provider scope may reject this one. */
    {
        uint8_t prior_cw[16];
        memset(prior_cw, 0x5A, sizeof(prior_cw));
        unknown_zero->hash = 0x101; known_zero->hash = 0x202;
        memcpy(unknown_zero->cw, prior_cw, sizeof(prior_cw));
        memcpy(known_zero->cw, prior_cw, sizeof(prior_cw));
        unknown_zero->dcwstatus = STAT_DCW_SUCCESS;
        assert(ecmdata_check_cw(0x80, 0x203, 0x1800, 0, 1, 0x1139, prior_cw, 2) == 1);
        unknown_zero->dcwstatus = STAT_DCW_WAIT;
        known_zero->dcwstatus = STAT_DCW_SUCCESS;
        assert(ecmdata_check_cw(0x80, 0x203, 0x1800, 0, 0, 0x1139, prior_cw, 2) == 1);
        known_zero->dcwstatus = STAT_DCW_WAIT;
        unknown_zero->dcwstatus = STAT_DCW_SUCCESS;
        assert(ecmdata_check_cw(0x80, 0x203, 0x1800, 0, 0, 0x1139, prior_cw, 2) == 0);
        unknown_zero->dcwstatus = STAT_DCW_WAIT;
        known_zero->dcwstatus = STAT_DCW_SUCCESS;
        assert(ecmdata_check_cw(0x80, 0x203, 0x1800, 0, 1, 0x1139, prior_cw, 2) == 0);
        unknown_zero->dcwstatus = STAT_DCW_WAIT;
        known_zero->dcwstatus = STAT_DCW_WAIT;
    }

    /* Exercise the production expiry paths with a request timestamp just
     * before GetTickCount wrap and the current tick just after it. */
    {
        struct ecm_request expired_req;
        struct cardserver_data search_cs;
        struct ecm_request *saved_ecmdata = ecmdata;
        uint32_t now;
        memset(&expired_req, 0, sizeof(expired_req));
        memset(&search_cs, 0, sizeof(search_cs));
        startime.tv_sec = time(NULL);
        startime.tv_usec = 0;
        now = (uint32_t)GetTickCount();
        assert(now < 2000u);
        memcpy(expired_req.ecm, raw_ecm, sizeof(raw_ecm));
        expired_req.ecmlen = sizeof(raw_ecm);
        expired_req.hash = hashCode(raw_ecm + 3, sizeof(raw_ecm) - 3);
        expired_req.sid = 0x1136;
        expired_req.caid = 0x1884;
        expired_req.provider_known = 1;
        expired_req.recvtime = now - (TIME_ECMALIVE + 1u);
        expired_req.next = expired_req.prev = &expired_req;
        expired_req.csnext = expired_req.csprev = &expired_req;
        search_cs.ecmdata = &expired_req;
        ecmdata = &expired_req;
        assert(search_ecmdata_dcw(raw_ecm, sizeof(raw_ecm), expired_req.sid,
                                  expired_req.caid, 0, 1) == NULL);
        assert(search_ecmdata_any(&search_cs, raw_ecm, sizeof(raw_ecm),
                                  expired_req.sid, expired_req.caid, 0, 1) == NULL);
        assert(search_ecmdata_byhash(expired_req.caid, expired_req.sid,
                                     expired_req.hash) == NULL);
#ifdef CACHEEX
        assert(search_ecmdata_byecmd5(expired_req.caid, 0,
                                      expired_req.ecmd5) == NULL);
#endif
        ecmdata = saved_ecmdata;
    }

    /* Reuse an aged slot across the tick wrap. The old side-registry snapshot
     * must be retired before the pointer receives a new lifetime generation,
     * while both the global and per-cardserver ECM rings remain intact. */
    {
        uint32_t now = (uint32_t)GetTickCount();
        uint64_t old_lifetime, new_lifetime;
        uint32_t total_before = (uint32_t)totalecm;
        int active = 1, count = 0, cs_count = 0;
        struct ecm_request *p;
        assert(now < 2000u);
        assert(ecmdata->prev == first);
        old_lifetime = ecm_request_lifetime_id_load(first);
        assert(old_lifetime != 0);
        assert(cache_ecm_snapshot_publish(first, &cs));
        first->recvtime = now - (uint32_t)(TIME_ECMALIVE*2u + 30000u);
        reused = store_ecmdata(&cs, raw_ecm, sizeof(raw_ecm), 0x1140, 0x1884, 0);
        assert(reused == first && ecmdata == first);
        new_lifetime = ecm_request_lifetime_id_load(first);
        assert(new_lifetime != 0 && new_lifetime != old_lifetime);
        assert((uint32_t)totalecm == total_before && cs.totalecm == (int)total_before);

        pthread_mutex_lock(&prg.lockcache);
		assert(!cache_ecm_snapshot_read_locked(first, old_lifetime, &active, NULL));
		assert(!cache_ecm_snapshot_read_locked(first, new_lifetime, &active, NULL));
        pthread_mutex_unlock(&prg.lockcache);

        p = ecmdata;
        do {
            assert(p && p->next && p->prev);
            assert(p->next->prev == p && p->prev->next == p);
            p = p->next;
            count++;
            assert(count <= totalecm);
        } while (p != ecmdata);
        assert(count == totalecm);
        assert(cs.totalecm == totalecm);
        p = cs.ecmdata;
        do {
            assert(p && p->csnext && p->csprev);
            assert(p->csnext->csprev == p && p->csprev->csnext == p);
            p = p->csnext;
            cs_count++;
            assert(cs_count <= cs.totalecm);
        } while (p != cs.ecmdata);
        assert(cs_count == cs.totalecm);
    }

    /* The injected fault is consumed exactly once; normal allocation recovers. */
    assert(!fail_next_allocation);
    ecmdata = NULL;
    cs.ecmdata = NULL;
    free(first);
    free(second);
    free(third);
    free(unknown_zero);
    free(known_zero);
    free(known_nonzero);
    puts("PASS: ECM allocation failures and provider-known semantics");
    return 0;
}
