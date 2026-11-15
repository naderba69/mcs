/* camd35-send-guard.c -- regression for the camd35/cs378x send length guard.
 *
 * Before the fix, camd35_send/camd35_sendto/cs378x_send copied `len` bytes
 * from the caller's frame into a fixed 1024-byte stack buffer with no bound
 * check (and crc32 read buf[20..len) even for len<20). A frame longer than
 * the buffer smashed the stack. This test drives all three entry points with
 * oversized and undersized lengths under ASan/UBSan: with the guard they
 * must refuse (return 0 / send nothing); without it ASan aborts.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "aes.h"
#include "md5.h"
/* zlib-compatible crc32 provided by src/crc32.c (linked by the .sh). */
unsigned long crc32(unsigned long crc, const unsigned char *buf, unsigned int len);

/* Stubs for the socket helpers msg-camd35.c links against in production. */
static int g_sent = 0;
int send_nonb(int sock, uint8_t *buf, int len, int to)
{
    (void)sock; (void)buf; (void)to;
    g_sent += len;
    return 1;
}
int recv_nonb(int sock, uint8_t *buf, int len, int timeout)
{
    (void)sock; (void)buf; (void)len; (void)timeout;
    return -1;
}

#include "../src/msg-camd35.c"

int main(void)
{
    AES_KEY ekey, dkey;
    uint8_t key[16] = {0};
    aes_set_keys(key, &dkey, &ekey);

    static uint8_t big[4096];
    memset(big, 0xAB, sizeof(big));
    /* A valid-shaped header area so an unguarded copy would proceed. */
    memset(big, 0, 64);

    int fails = 0;

    /* Oversized: must be refused, nothing sent. */
    g_sent = 0;
    if (cs378x_send(-1, &ekey, 0x11223344, big, 2000) != 0) { printf("FAIL cs378x oversized accepted\n"); fails++; }
    if (g_sent != 0) { printf("FAIL cs378x oversized sent %d bytes\n", g_sent); fails++; }
    if (camd35_sendto(-1, 0x7f000001, 1234, &ekey, 0x11223344, big, 2000) != 0) { printf("FAIL sendto oversized accepted\n"); fails++; }

    /* Undersized: crc32(buf+20, len-20) would read out of bounds. */
    if (cs378x_send(-1, &ekey, 0x11223344, big, 10) != 0) { printf("FAIL cs378x undersized accepted\n"); fails++; }
    if (camd35_sendto(-1, 0x7f000001, 1234, &ekey, 0x11223344, big, 0) != 0) { printf("FAIL sendto undersized accepted\n"); fails++; }

    /* NULL buffer. */
    if (cs378x_send(-1, &ekey, 0x11223344, NULL, 100) != 0) { printf("FAIL cs378x NULL accepted\n"); fails++; }

    /* Normal frame still sends. */
    g_sent = 0;
    uint8_t frame[64];
    memset(frame, 0, sizeof(frame));
    if (cs378x_send(-1, &ekey, 0x11223344, frame, 20 + 16) != 1) { printf("FAIL cs378x normal refused\n"); fails++; }
    if (g_sent <= 0) { printf("FAIL cs378x normal sent nothing\n"); fails++; }

    if (fails) { printf("camd35-send-guard: %d FAILURES\n", fails); return 1; }
    printf("camd35-send-guard: PASS (oversized/undersized/NULL refused, normal frame sent)\n");
    return 0;
}
