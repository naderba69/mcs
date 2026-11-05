/* Actual ECM hash and cache pipe serialization, including bit 31. */
#define main multics_unused_main
#include "../src/main.c"
#undef main
#include <assert.h>

static uint32_t reference_hash(const uint8_t *p, int n)
{
    uint64_t h = 0;
    int i;
    for (i = 0; i < n; i++) h = (h * 31 + p[i]) & UINT32_MAX;
    return (uint32_t)h;
}

int main(void)
{
    uint8_t bytes[256], wire[128], cw[16], decoded_cw[16];
    struct cache_data original, decoded;
    ECM_DATA ecm;
    const uint32_t hashes[] = {0, 1, 0x7fffffffU, 0x80000000U, 0xa0dd9a9fU, 0xffffffffU};
    int i, n, mark;
    uint32_t peerid_out=0;
    uint64_t lifetime_id = UINT64_C(0x123456789abcdef0);
    for (i = 0; i < 256; i++) bytes[i] = (uint8_t)(i * 73 + 255);
    for (n = 0; n <= 256; n++) assert(hashCode(bytes, n) == reference_hash(bytes, n));
    memset(bytes, 255, sizeof(bytes));
    assert(hashCode(bytes, 8) == 0xec45af80U);
    for (n = 0; n <= 256; n++) assert(hashCode(bytes, n) == reference_hash(bytes, n));
    memset(&ecm, 0, sizeof(ecm)); memset(cw, 0x57, sizeof(cw));
    __atomic_store_n(&ecm.lifetime_id,lifetime_id,__ATOMIC_RELEASE);
    ecm.ecm[0] = 0x80; ecm.sid = 0x1136; ecm.caid = 0x1884; ecm.provid = 0x010203;
    for (i = 0; i < (int)(sizeof(hashes)/sizeof(hashes[0])); i++) {
        ecm.hash = hashes[i]; memset(wire, 0, sizeof(wire)); memset(&decoded, 0, sizeof(decoded));
        n = put_ecm2cache(PIPE_CACHE_FIND, wire, sizeof(wire), &ecm, 0x4321, cw);
        assert(n == 16 + (int)sizeof(void*) + (int)sizeof(uint64_t) +
#ifdef CACHEEX
               16 +
#endif
               16);
        get_ecm2cache(wire, n, &decoded, decoded_cw);
        assert(decoded.hash == ecm.hash && decoded.caid == ecm.caid && decoded.sid == ecm.sid);
        assert(decoded.provid == ecm.provid && decoded.ecm == &ecm &&
               decoded.ecm_lifetime_id == lifetime_id &&
               ecm_request_lifetime_matches(decoded.ecm,&ecm,lifetime_id) &&
               !memcmp(decoded_cw,cw,16));
        original = decoded; memset(wire, 0, sizeof(wire)); memset(&decoded, 0, sizeof(decoded));
        put_cache2ecm(PIPE_CACHE_FIND_SUCCESS, wire, sizeof(wire), &original, cw, 0x01020304, CW1CYCLE);
        assert(get_cache2ecm(wire, (size_t)(12 + sizeof(void*) + sizeof(uint64_t) + 4 + 16 + 1),
                             &decoded, decoded_cw, &mark, &peerid_out));
        assert(peerid_out == UINT32_C(0x01020304));
        assert(decoded.hash == ecm.hash && decoded.caid == ecm.caid && decoded.sid == ecm.sid);
        assert(decoded.ecm == &ecm && decoded.ecm_lifetime_id == lifetime_id &&
               ecm_request_lifetime_matches(decoded.ecm,&ecm,lifetime_id) &&
               mark == CW1CYCLE && !memcmp(decoded_cw,cw,16));
    }
    puts("PASS: ECM hash modulo 2^32 and cache pipe round trips");
    return 0;
}
