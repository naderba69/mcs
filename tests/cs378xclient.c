/*
 * cs378xclient.c — a real cs378x (TCP) / camd35 (UDP) ECM client for
 * MultiCS, built from MultiCS's own msg-camd35.c (frame format, AES
 * keys and UCRC derivation identical by construction, not by copying).
 *
 * Band 74: cs378x/camd35 had no test client in the tree, so their
 * /user-trace coverage (band 73) could not be proven live. This closes
 * that rig debt for both protocols: same payload, TCP+keepalive login
 * for cs378x, a single connected-UDP exchange for camd35.
 *
 * Usage: cs378xclient <cs378x|camd35> <host> <port> <user> <pass>
 *                     <caid-hex> <provid-hex> <sid-hex>
 * Prints "THE SERVER DELIVERED A CONTROL WORD" or "decode-failed".
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "../src/aes.h"
#include "../src/md5.h"

unsigned long crc32(unsigned long crc, const unsigned char *buf, unsigned int len);

int send_nonb(int sock, uint8_t *buf, int len, int to)
{
    int sent = 0;
    (void)to;
    while (sent < len) {
        int n = (int)send(sock, buf + sent, (size_t)(len - sent), MSG_NOSIGNAL);
        if (n <= 0) { if (errno == EINTR) continue; return 0; }
        sent += n;
    }
    return len;
}

int recv_nonb(int sock, uint8_t *buf, int len, int timeout)
{
    int got = 0;
    while (got < len) {
        struct pollfd pfd = { .fd = sock, .events = POLLIN };
        int r = poll(&pfd, 1, timeout);
        if (r <= 0) return got ? got : -1;
        int n = (int)recv(sock, buf + got, (size_t)(len - got), 0);
        if (n <= 0) return got ? got : -1;
        got += n;
    }
    return got;
}

#include "../src/msg-camd35.c"

int main(int argc, char **argv)
{
    if (argc < 9) {
        fprintf(stderr, "usage: cs378xclient <cs378x|camd35> <host> <port> <user> <pass> <caid> <provid> <sid>\n");
        return 2;
    }
    int udp = !strcmp(argv[1], "camd35");
    const char *host = argv[2];
    int port = atoi(argv[3]);
    uint16_t caid = (uint16_t)strtol(argv[6], NULL, 16);
    uint32_t provid = (uint32_t)strtol(argv[7], NULL, 16);
    uint16_t sid = (uint16_t)strtol(argv[8], NULL, 16);

    AES_KEY enc, dec;
    uint32_t ucrc;
    camd35_init_data(argv[4], argv[5], &enc, &dec, &ucrc);

    int fd = socket(AF_INET, udp ? SOCK_DGRAM : SOCK_STREAM, 0);
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)port);
    inet_pton(AF_INET, host, &a.sin_addr);
    if (connect(fd, (struct sockaddr *)&a, sizeof(a)) != 0) { perror("connect"); return 2; }

    uint8_t buf[2048];
    if (!udp) {
        /* login: a keepalive frame; server answers and marks us connected */
        memset(buf, 0, 21);
        buf[0] = CAMD_KEEPALIVE;
        if (!cs378x_send(fd, &enc, ucrc, buf, 21)) { printf("login send failed\n"); return 1; }
        int n = cs378x_recv(fd, ucrc, &dec, buf, (int)sizeof(buf));
        if (n <= 0 || buf[4] != CAMD_KEEPALIVE) { printf("decode-failed (no keepalive reply)\n"); return 1; }
        printf("login ok (keepalive reply)\n");
    }

    /* ECM request: 20-byte header + 64-byte ECM (ncclient's formula) */
    uint8_t ecm[64];
    memset(ecm, 0, sizeof(ecm));
    ecm[0] = 0x80;
    ecm[1] = (uint8_t)(((64 - 3) >> 8) & 0x0F);
    ecm[2] = (uint8_t)((64 - 3) & 0xFF);
    for (int i = 3; i < 64; i++) ecm[i] = (uint8_t)(i * 7 + 1 + (sid & 0xFF));

    memset(buf, 0, 20);
    buf[0] = CAMD_ECM_REQUEST;
    buf[8] = (uint8_t)(sid >> 8); buf[9] = (uint8_t)sid;
    buf[10] = (uint8_t)(caid >> 8); buf[11] = (uint8_t)caid;
    buf[12] = (uint8_t)(provid >> 24); buf[13] = (uint8_t)(provid >> 16);
    buf[14] = (uint8_t)(provid >> 8); buf[15] = (uint8_t)provid;
    memcpy(buf + 20, ecm, 64);
    if (udp) camd35_send(fd, &enc, ucrc, buf, 20 + 64);
    else if (!cs378x_send(fd, &enc, ucrc, buf, 20 + 64)) { printf("ecm send failed\n"); return 1; }

    /* reply: server reuses the same socket; read frames until ECM reply/denial */
    for (int tries = 0; tries < 20; tries++) {
        int n;
        if (udp) {
            n = recv_nonb(fd, buf, (int)sizeof(buf), 5000);
            if (n < 36) break;
            uint32_t u = (uint32_t)buf[0] << 24 | (uint32_t)buf[1] << 16 | (uint32_t)buf[2] << 8 | buf[3];
            if (u != ucrc) continue;
            aes_decrypt(&dec, buf + 4, n - 4);
        } else {
            n = cs378x_recv(fd, ucrc, &dec, buf, (int)sizeof(buf));
            if (n <= 0) break;
        }
        if (buf[4] == CAMD_ECM_REPLY) {
            int zero = 1;
            for (int i = 0; i < 16; i++) if (buf[24 + i]) zero = 0;
            if (!zero) { printf("THE SERVER DELIVERED A CONTROL WORD\n"); return 0; }
            printf("decode-failed (all-zero)\n"); return 1;
        }
        if (buf[4] == 0x44) { printf("decode-failed (0x44)\n"); return 1; }
        /* keepalive/other frames: keep waiting */
    }
    printf("decode-failed (no reply)\n");
    return 1;
}
