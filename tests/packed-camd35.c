/* Exercise the actual static config helper, with an alignment-checking peer. */
#include "../src/config.c"
#include <assert.h>
#include <stdint.h>

void camd35_init_data(char *user, char *pass, AES_KEY *enc, AES_KEY *dec, uint32_t *crc)
{
    assert(strcmp(user, "user") == 0 && strcmp(pass, "pass") == 0);
    assert((uintptr_t)enc % __alignof__(AES_KEY) == 0);
    assert((uintptr_t)dec % __alignof__(AES_KEY) == 0);
    assert((uintptr_t)crc % __alignof__(uint32_t) == 0);
    assert(enc->rd_key[0] == 0x11111111 && dec->rd_key[0] == 0x22222222);
    enc->rd_key[0] = 0x12345678;
    dec->rd_key[0] = 0xabcdef01;
    enc->rounds = 10; dec->rounds = 10; *crc = 0x99887766;
}
int main(void)
{
    unsigned char encbuf[sizeof(AES_KEY)+2] __attribute__((aligned(8)));
    unsigned char decbuf[sizeof(AES_KEY)+2] __attribute__((aligned(8)));
    unsigned char crcbuf[sizeof(uint32_t)+2] __attribute__((aligned(8)));
    AES_KEY enc, dec;
    uint32_t crc;
    memset(&enc, 0, sizeof(enc)); memset(&dec, 0, sizeof(dec));
    enc.rd_key[0] = 0x11111111; dec.rd_key[0] = 0x22222222;
    memset(encbuf, 0xa5, sizeof(encbuf)); memset(decbuf, 0xa5, sizeof(decbuf));
    memset(crcbuf, 0xa5, sizeof(crcbuf));
    memcpy(encbuf+1, &enc, sizeof(enc)); memcpy(decbuf+1, &dec, sizeof(dec));
    camd35_init_packed_data("user", "pass", encbuf+1, decbuf+1, crcbuf+1);
    memcpy(&enc, encbuf+1, sizeof(enc)); memcpy(&dec, decbuf+1, sizeof(dec));
    memcpy(&crc, crcbuf+1, sizeof(crc));
    assert(enc.rd_key[0] == 0x12345678 && dec.rd_key[0] == 0xabcdef01);
    assert(enc.rounds == 10 && dec.rounds == 10 && crc == 0x99887766);
    assert(encbuf[0] == 0xa5 && encbuf[sizeof(encbuf)-1] == 0xa5);
    assert(decbuf[0] == 0xa5 && decbuf[sizeof(decbuf)-1] == 0xa5);
    assert(crcbuf[0] == 0xa5 && crcbuf[sizeof(crcbuf)-1] == 0xa5);
    puts("packed camd35 key initialization passed");
    return 0;
}
