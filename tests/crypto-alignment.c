/* Known-answer and misalignment regressions for the packed x64 build.
 * Run with -fpack-struct; AES_KEY and SHA_CTX intentionally start at +1. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sha1.h"
#include "md5.h"
#include "aes.h"
#include "des.h"

static void from_hex(const char *hex, unsigned char *out, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        unsigned x;
        assert(sscanf(hex + i * 2, "%2x", &x) == 1);
        out[i] = (unsigned char)x;
    }
}
static void expect(const unsigned char *actual, const char *hex, size_t n)
{
    unsigned char expected[32];
    assert(n <= sizeof(expected));
    from_hex(hex, expected, n);
    assert(memcmp(actual, expected, n) == 0);
}
static void sha(const unsigned char *data, size_t n, size_t chunk, const char *expected)
{
    unsigned char raw[sizeof(SHA_CTX) + 8] __attribute__((aligned(8)));
    SHA_CTX *ctx = (SHA_CTX *)(void *)(raw + 1);
    unsigned char out[20];
    size_t i;
    SHA1_Init(ctx);
    for (i = 0; i < n; ) {
        size_t take = n - i < chunk ? n - i : chunk;
        SHA1_Update(ctx, data + i, take);
        i += take;
    }
    SHA1_Final(out, ctx);
    expect(out, expected, sizeof(out));
}
static void aes(int bits, const char *keyhex, const char *cipherhex)
{
    unsigned char raw[sizeof(AES_KEY) + 8] __attribute__((aligned(8)));
    AES_KEY *key = (AES_KEY *)(void *)(raw + 1);
    unsigned char kb[32], plain[16], out[16], recovered[16];
    from_hex(keyhex, kb, (size_t)bits / 8);
    from_hex("00112233445566778899aabbccddeeff", plain, 16);
    assert(AES_set_encrypt_key(kb, bits, key) == 0);
    AES_encrypt(plain, out, key);
    expect(out, cipherhex, 16);
    assert(AES_set_decrypt_key(kb, bits, key) == 0);
    AES_decrypt(out, recovered, key);
    assert(memcmp(recovered, plain, 16) == 0);
}
int main(void)
{
    static const unsigned char abc[] = "abc";
    static const unsigned char longmsg[] = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    static const unsigned char readonly128[] =
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    unsigned char out[16];
    unsigned char *million = malloc(1000000);
    unsigned char deskey[8], block[8], boundary[129];
    static const size_t lengths[] = {63, 64, 65, 127, 128, 129};
    static const char *sha_expected[] = {
        "6d942da0c4392b123528f2905c713a3ce28364bd", "c6138d514ffa2135bfce0ed0b8fac65669917ec7",
        "69bd728ad6e13cd76ff19751fde427b00e395746", "89d7312a903f65cd2b3e34a975e55dbea9033353",
        "e6434bc401f98603d7eda504790c98c67385d535", "3352e41cc30b40ae80108970492b21014049e625"};
    static const char *md5_expected[] = {
        "48a6295221902e8e0938f773a7185e72", "b2d3f56bc197fd985d5965079b5e7148",
        "8bd7053801c768420faf816fadba971c", "8402b21e7bc7906493bae0dac017f1f9",
        "37eff01866ba3f538421b30b7cbefcac", "46f986692847558fc38b0cece591c20f"};
    assert(million);
    memset(million, 'a', 1000000);
    sha(abc, 0, 1, "da39a3ee5e6b4b0d3255bfef95601890afd80709");
    sha(abc, 3, 1, "a9993e364706816aba3e25717850c26c9cd0d89d");
    sha(longmsg, sizeof(longmsg)-1, 7, "84983e441c3bd26ebaae4aa1f95129e5e54670f1");
    sha(readonly128, 128, 128, "ad5b3fdbcb526778c2839d2f151ea753995e26a0");
    sha(million, 1000000, 1000000, "34aa973cd4c4daa4f61eeb2bdbad27316534016f");
    sha(million, 1000000, 31, "34aa973cd4c4daa4f61eeb2bdbad27316534016f");
    for (size_t i = 0; i < 1000000; i++) assert(million[i] == 'a');
    for (size_t i = 0; i < sizeof(boundary); i++) boundary[i] = (unsigned char)i;
    for (size_t i = 0; i < sizeof(lengths)/sizeof(lengths[0]); i++) {
        sha(boundary, lengths[i], lengths[i], sha_expected[i]);
        sha(boundary, lengths[i], 17, sha_expected[i]);
        MD5(boundary, lengths[i], out);
        expect(out, md5_expected[i], 16);
    }
    {
        SHA_CTX ctx;
        SHA1_Init(&ctx);
        ctx.count[0] = 0xfffffff8U;
        SHA1_Update(&ctx, abc, 1);
        assert(ctx.count[0] == 0 && ctx.count[1] == 1);
    }
    MD5(abc, 3, out);
    expect(out, "900150983cd24fb0d6963f7d28e17f72", 16);
    MD5(million, 1000000, out);
    expect(out, "7707d6ae4e027c70eea2a935c2296f21", 16);
    free(million);
    aes(128, "000102030405060708090a0b0c0d0e0f", "69c4e0d86a7b0430d8cdb78070b4c55a");
    aes(192, "000102030405060708090a0b0c0d0e0f1011121314151617", "dda97ca4864cdfe06eaf70a0ec0d7191");
    aes(256, "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f", "8ea2b7ca516745bfeafc49904b496089");
    from_hex("133457799bbcdff1", deskey, 8);
    from_hex("0123456789abcdef", block, 8);
    doPC1(deskey);
    des(deskey, DES_IP | DES_IP_1, block);
    expect(block, "85e813540f0ab405", 8);
    des(deskey, DES_IP | DES_IP_1 | DES_RIGHT, block);
    expect(block, "0123456789abcdef", 8);
    puts("crypto known-answer and packed-alignment tests passed");
    return 0;
}
