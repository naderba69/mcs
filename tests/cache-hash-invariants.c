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
    for (i = 0; i < 256; i++) bytes[i] = (uint8_t)(i * 73 + 255);
    for (n = 0; n <= 256; n++) assert(hashCode(bytes, n) == reference_hash(bytes, n));
    memset(bytes, 255, sizeof(bytes));
    assert(hashCode(bytes, 8) == 0xec45af80U);
    for (n = 0; n <= 256; n++) assert(hashCode(bytes, n) == reference_hash(bytes, n));
    memset(&ecm, 0, sizeof(ecm)); memset(cw, 0x57, sizeof(cw));
    ecm.ecm[0] = 0x80; ecm.sid = 0x1136; ecm.caid = 0x1884; ecm.provid = 0x010203;
    for (i = 0; i < (int)(sizeof(hashes)/sizeof(hashes[0])); i++) {
        ecm.hash = hashes[i]; memset(wire, 0, sizeof(wire)); memset(&decoded, 0, sizeof(decoded));
        put_ecm2cache(1, wire, &ecm, 0x4321, cw);
        get_ecm2cache(wire, &decoded, decoded_cw);
        assert(decoded.hash == ecm.hash && decoded.caid == ecm.caid && decoded.sid == ecm.sid);
        assert(decoded.provid == ecm.provid && decoded.ecm == &ecm && !memcmp(decoded_cw,cw,16));
        original = decoded; memset(wire, 0, sizeof(wire)); memset(&decoded, 0, sizeof(decoded));
        put_cache2ecm(1, wire, &original, cw, 0x01020304, CW1CYCLE);
        assert(get_cache2ecm(wire, &decoded, decoded_cw, &mark) == 0x01020304);
        assert(decoded.hash == ecm.hash && decoded.caid == ecm.caid && decoded.sid == ecm.sid);
        assert(decoded.ecm == &ecm && mark == CW1CYCLE && !memcmp(decoded_cw,cw,16));
    }
    puts("PASS: ECM hash modulo 2^32 and cache pipe round trips");
    return 0;
}
