/* Exercise the production Radegast ECM-deduplication search. */
#define CCCAM 1
#define CACHEEX 1
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t fake_ticks = 10000;
uint32_t GetTickCount(void);

#include "../src/ecmdata.c"

uint32_t GetTickCount(void)
{
    return fake_ticks;
}

static void set_request(struct ecm_request *request, const uint8_t *ecm,
                        int ecmlen, uint16_t sid, uint16_t caid,
                        uint32_t provid, uint32_t recvtime)
{
    memset(request, 0, sizeof(*request));
    memcpy(request->ecm, ecm, (size_t)ecmlen);
    request->ecmlen = ecmlen;
    request->sid = sid;
    request->caid = caid;
    request->provid = provid;
    request->provider_known = provid != 0;
    request->recvtime = recvtime;
    request->hash = hashCode((uint8_t *)ecm + 3, ecmlen - 3);
    request->dcwstatus = STAT_DCW_WAIT;
}

int main(void)
{
    uint8_t ecm[32] = {0x80, 0, 29};
    struct ecm_request first, second;
    const uint16_t sid = 0;
    const uint16_t caid = 0x1884;
    const uint32_t provid = 0x123456;
    int i;

    for (i = 3; i < (int)sizeof(ecm); ++i) ecm[i] = (uint8_t)(i * 9 + 7);
    set_request(&first, ecm, (int)sizeof(ecm), sid, caid, provid, 9000);
    first.next = first.prev = &first;
    ecmdata = &first;

    assert(search_ecmdata_dcw(ecm, (int)sizeof(ecm), sid, caid, provid, 1) == &first);
    first.provid=0; first.provider_known=0;
    assert(search_ecmdata_dcw(ecm, (int)sizeof(ecm), sid, caid, 0, 0) == &first);
    assert(search_ecmdata_dcw(ecm, (int)sizeof(ecm), sid, caid, 0, 1) == NULL);
    first.provider_known=1;
    assert(search_ecmdata_dcw(ecm, (int)sizeof(ecm), sid, caid, 0, 1) == &first);
    assert(search_ecmdata_dcw(ecm, (int)sizeof(ecm), sid, caid, 0, 0) == NULL);
    first.provid=provid;
    assert(search_ecmdata_dcw(ecm, (int)sizeof(ecm), sid, caid + 1, provid, 1) == NULL);
    assert(search_ecmdata_dcw(ecm, (int)sizeof(ecm), sid, caid, provid + 1, 1) == NULL);
    assert(search_ecmdata_dcw(ecm, (int)sizeof(ecm), sid + 1, caid, provid, 1) == NULL);
    assert(search_ecmdata_dcw(ecm, 2, sid, caid, provid, 1) == NULL);

    first.dcwstatus = STAT_DCW_FAILED;
    assert(search_ecmdata_dcw(ecm, (int)sizeof(ecm), sid, caid, provid, 1) == NULL);

    /* Expired entries are skipped, not treated as the end of the ring. */
    set_request(&first, ecm, (int)sizeof(ecm), sid, caid, provid, 0);
    set_request(&second, ecm, (int)sizeof(ecm), sid, caid, provid, 9000);
    first.next = &second;
    first.prev = &second;
    second.next = &first;
    second.prev = &first;
    ecmdata = &first;
    fake_ticks = 20000;
    assert(search_ecmdata_dcw(ecm, (int)sizeof(ecm), sid, caid, provid, 1) == &second);

    /* The unsigned elapsed-time check remains valid across tick wrap. */
    first.next = first.prev = &first;
    set_request(&first, ecm, (int)sizeof(ecm), sid, caid, provid, UINT32_MAX - 5U);
    ecmdata = &first;
    fake_ticks = 5;
    assert(search_ecmdata_dcw(ecm, (int)sizeof(ecm), sid, caid, provid, 1) == &first);

    puts("PASS: Radegast duplicate lookup includes CAID/provider and handles expiry safely");
    return 0;
}
