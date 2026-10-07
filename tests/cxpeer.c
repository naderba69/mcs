/*
 * cxpeer.c -- TASK R1: a minimal CCcam cache-exchange peer, for runtime-
 * testing the three exchange gates (cacheex_gates.h enforced in
 * cache_setdcw()).
 *
 * WHY THIS EXISTS. cachepeer.c speaks CSP UDP, which the gates
 * deliberately exclude (CSP has its own machinery). The exchange family
 * -- cccam/camd35/cs378x cacheex -- needed a peer that arrives through
 * the REAL srv-cccam.c login and pushes through the REAL
 * CC_MSG_CACHE_PUSH handler, so the rig exercises the same bytes a
 * r107/oscam partner would send. This file does exactly that and
 * nothing more.
 *
 * SELF-CONTAINED by harness doctrine (see cachepeer.c): the CCcam stream
 * cipher below is copied verbatim from src/msg-cccam.c, the handshake
 * mirrors cli-cccam.c's cc_connect_srv(), and SHA-1/MD5 come from the
 * tree's own sha1.c / md5.c included directly. Linking the server
 * objects would drag the whole config machine in for nothing.
 *
 * THE ECM CONTRACT. ncclient.c builds its ECM deterministically from the
 * sid alone (ecm[i] = i*7 + 1 + (sid & 0xFF), i = 3..63, ecmlen 64,
 * NC_FILL=0), and the server's cache entry is keyed by
 * hash = hashCode(ecm+3, 61) with ecmd5 = MD5(ecm+3, 61). This peer
 * reconstructs the SAME bytes from the sid, so a push with `sid` matches
 * exactly the ECM an `ncclient` run made pending, with no shared state
 * beyond the formula.
 *
 * USAGE
 *   cxpeer --hash <sid-hex>                     print the ecm hash, exit
 *   cxpeer <host> <port> <user> <pass> <job> [job...]
 * jobs:
 *   push <caid> <provid> <sid> <cwhex> [bad]    one CC_MSG_CACHE_PUSH;
 *                                               `bad` corrupts cw[15] so
 *                                               checksumDCW() fails
 *   sleep <ms>
 * exit code 0 iff every job ran.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "../src/sha1.h"
#include "../src/sha1.c"
#include "../src/md5.c"

/* ------------------------------------------------------------------ */
/* The CCcam stream cipher, copied verbatim from src/msg-cccam.c       */
/* (cc_crypt_init / cc_crypt_xor / cc_decrypt / cc_encrypt).           */
struct cc_crypt_block {
	uint8_t keytable[256];
	uint8_t state;
	uint8_t counter;
	uint8_t sum;
};
static void cc_crypt_swap(uint8_t *p1, uint8_t *p2)
{ uint8_t t = *p1; *p1 = *p2; *p2 = t; }

static void cc_crypt_init(struct cc_crypt_block *block, uint8_t *key, int len)
{
	int i;
	uint8_t j = 0;
	for (i = 0; i < 256; i++) block->keytable[i] = i;
	for (i = 0; i < 256; i++) {
		j += key[i % len] + block->keytable[i];
		cc_crypt_swap(&block->keytable[i], &block->keytable[j]);
	}
	block->state = *key;
	block->counter = 0;
	block->sum = 0;
}

static void cc_crypt_xor(uint8_t *buf)
{
	const char cccam[] = "CCcam";
	buf[8+0] = 0 * buf[0]; buf[0] ^= cccam[0];
	buf[8+1] = 1 * buf[1]; buf[1] ^= cccam[1];
	buf[8+2] = 2 * buf[2]; buf[2] ^= cccam[2];
	buf[8+3] = 3 * buf[3]; buf[3] ^= cccam[3];
	buf[8+4] = 4 * buf[4]; buf[4] ^= cccam[4];
	buf[8+5] = 5 * buf[5]; buf[5] ^= cccam[5];
	buf[8+6] = 6 * buf[6];
	buf[8+7] = 7 * buf[7];
}

static void cc_decrypt(struct cc_crypt_block *b, uint8_t *data, int len)
{
	int i; uint8_t z;
	for (i = 0; i < len; i++) {
		b->counter++;
		b->sum += b->keytable[b->counter];
		cc_crypt_swap(&b->keytable[b->counter], &b->keytable[b->sum]);
		z = data[i];
		data[i] = z ^ b->keytable[(b->keytable[b->counter] + b->keytable[b->sum]) & 0xff] ^ b->state;
		z = data[i];
		b->state = b->state ^ z;
	}
}

static void cc_encrypt(struct cc_crypt_block *b, uint8_t *data, int len)
{
	int i; uint8_t z;
	for (i = 0; i < len; i++) {
		b->counter++;
		b->sum += b->keytable[b->counter];
		cc_crypt_swap(&b->keytable[b->counter], &b->keytable[b->sum]);
		z = data[i];
		data[i] = z ^ b->keytable[(b->keytable[b->counter] + b->keytable[b->sum]) & 0xff] ^ b->state;
		b->state = b->state ^ z;
	}
}

/* ------------------------------------------------------------------ */
/* Wire constants from src/msg-cccam.h                                 */
#define CX_CCMSG_CLI_INFO    0x00
#define CX_CCMSG_CACHE_PUSH  0x81
#define CX_CCMSG_NO_HEADER   0xffff

static int recv_all(int fd, uint8_t *buf, int len, int timeout_ms)
{
	int got = 0;
	while (got < len) {
		struct timeval tv;
		int r;
		tv.tv_sec = timeout_ms / 1000;
		tv.tv_usec = (timeout_ms % 1000) * 1000;
		setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
		r = recv(fd, buf + got, len - got, 0);
		if (r <= 0) return -1;
		got += r;
	}
	return got;
}

static int send_all(int fd, uint8_t *buf, int len)
{
	int sent = 0;
	while (sent < len) {
		int r = send(fd, buf + sent, len - sent, MSG_NOSIGNAL);
		if (r <= 0) return -1;
		sent += r;
	}
	return sent;
}

/* local replications of cc_msg_recv / cc_msg_send */
static int cx_msg_recv(int fd, struct cc_crypt_block *rb, uint8_t *buf)
{
	uint8_t netbuf[2048];
	int len, dlen;
	if (recv_all(fd, netbuf, 4, 5000) != 4) return -1;
	cc_decrypt(rb, netbuf, 4);
	dlen = (netbuf[2] << 8) | netbuf[3];
	if (dlen) {
		if (recv_all(fd, netbuf + 4, dlen, 5000) != dlen) return -1;
		cc_decrypt(rb, netbuf + 4, dlen);
		len = dlen + 4;
	} else len = 4;
	memcpy(buf, netbuf, len);
	return len;
}

static int cx_msg_send(int fd, struct cc_crypt_block *sb, int cmd, int len, uint8_t *buf)
{
	uint8_t netbuf[2048];
	memset(netbuf, 0, len + 4);
	if (cmd == CX_CCMSG_NO_HEADER) memcpy(netbuf, buf, len);
	else {
		netbuf[0] = 0;
		netbuf[1] = cmd & 0xff;
		netbuf[2] = len >> 8;
		netbuf[3] = len & 0xff;
		if (buf) memcpy(netbuf + 4, buf, len);
		len += 4;
	}
	cc_encrypt(sb, netbuf, len);
	return send_all(fd, netbuf, len);
}

/* ------------------------------------------------------------------ */
/* The deterministic ECM, exactly as ncclient.c builds it (NC_FILL=0)  */
static int ecm_build(uint16_t sid, uint8_t *ecm)
{
	int i;
	memset(ecm, 0, 64);
	ecm[0] = 0x80;
	ecm[1] = 0;
	ecm[2] = 61;
	for (i = 3; i < 64; i++) ecm[i] = (uint8_t)(i * 7 + 1 + (sid & 0xFF));
	return 64;
}

static uint32_t ecm_hash(uint16_t sid)
{
	/* hashCode(ecm+3, 61) from src/ecmdata.c: h = 31*h + b */
	uint8_t ecm[64];
	int i;
	uint32_t h = 0;
	ecm_build(sid, ecm);
	for (i = 3; i < 64; i++) h = 31 * h + ecm[i];
	return h;
}

static int hex2bin(const char *hex, uint8_t *out, int want)
{
	int i;
	if ((int)strlen(hex) < want * 2) return -1;
	for (i = 0; i < want; i++) {
		unsigned v;
		char two[3];
		two[0] = hex[i*2]; two[1] = hex[i*2+1]; two[2] = 0;
		if (sscanf(two, "%x", &v) != 1) return -1;
		out[i] = (uint8_t)v;
	}
	return 0;
}

static int tcp_connect(const char *host, int port)
{
	struct sockaddr_in addr;
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	struct hostent *he;
	if (fd < 0) return -1;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons((uint16_t)port);
	he = gethostbyname(host);
	if (!he) { close(fd); return -1; }
	memcpy(&addr.sin_addr, he->h_addr_list[0], sizeof(addr.sin_addr));
	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) { close(fd); return -1; }
	return fd;
}

/* ------------------------------------------------------------------ */
static int run_jobs(const char *host, int port, const char *user,
		    const char *pass, int njobs, char **jobs)
{
	uint8_t seed[24], hash[24], buf[128], payload[96], ecm[64];
	struct cc_crypt_block sendblock, recvblock;
	SHA_CTX ctx;
	int fd, i, rc = 0;
	uint8_t nodeid[8] = { 0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8 };

	fd = tcp_connect(host, port);
	if (fd < 0) { printf("cxpeer: [FAIL] connect %s:%d\n", host, port); return 1; }

	/* handshake, mirrored from cli-cccam.c cc_connect_srv() */
	if (recv_all(fd, seed, 16, 5000) != 16) {
		printf("cxpeer: [FAIL] no 16-byte seed\n"); close(fd); return 1;
	}
	cc_crypt_xor(seed);
	SHA1_Init(&ctx);
	SHA1_Update(&ctx, seed, 16);
	SHA1_Final(hash, &ctx);
	cc_crypt_init(&recvblock, hash, 20);
	cc_decrypt(&recvblock, seed, 16);
	cc_crypt_init(&sendblock, seed, 16);
	cc_decrypt(&sendblock, hash, 20);

	if (cx_msg_send(fd, &sendblock, CX_CCMSG_NO_HEADER, 20, hash) < 0) goto connfail;
	memset(buf, 0, 32);
	memcpy(buf, user, strlen(user) > 20 ? 20 : strlen(user));
	if (cx_msg_send(fd, &sendblock, CX_CCMSG_NO_HEADER, 20, buf) < 0) goto connfail;
	/* the cli-cccam quirk, verbatim: encrypt the pass into its own
	 * buffer, but send the 6-byte "CCcam\0" magic */
	{
		char pwd[64];
		uint8_t magic[8];
		memset(pwd, 0, sizeof(pwd));
		strncpy(pwd, pass, 63);
		cc_encrypt(&sendblock, (uint8_t *)pwd, strlen(pwd));
		memset(magic, 0, sizeof(magic));
		memcpy(magic, "CCcam\0", 6);
		if (cx_msg_send(fd, &sendblock, CX_CCMSG_NO_HEADER, 6, magic) < 0) goto connfail;
	}
	if (recv_all(fd, buf, 20, 5000) != 20) { printf("cxpeer: [FAIL] no pwd ack\n"); close(fd); return 1; }
	cc_decrypt(&recvblock, buf, 20);
	{
		uint8_t expect[8];
		memset(expect, 0, sizeof(expect));
		memcpy(expect, "CCcam\0", 6);
		if (memcmp(buf, expect, 5)) {
			printf("cxpeer: [FAIL] login rejected\n"); close(fd); return 1;
		}
	}
	printf("cxpeer: [ ok ] cccam login as '%s'\n", user);

	/* CC_MSG_CLI_INFO: user20 + nodeid8 + wantemus1 + version32 + who3 + build32 */
	memset(buf, 0, 128);
	memcpy(buf, user, strlen(user) > 20 ? 20 : strlen(user));
	memcpy(buf + 20, nodeid, 8);
	buf[28] = 0;
	memcpy(buf + 29, "2.0.11", 6);
	if (cx_msg_send(fd, &sendblock, CX_CCMSG_CLI_INFO, 20 + 8 + 1 + 64, buf) < 0) goto connfail;

	for (i = 0; i < njobs; i++) {
		char *j = jobs[i];
		if (!strncmp(j, "sleep ", 6)) {
			usleep((useconds_t)atoi(j + 6) * 1000);
			printf("cxpeer: [ ok ] sleep %s ms\n", j + 6);
		}
		else if (!strncmp(j, "push ", 5)) {
			char caid[8] = {0}, provid[16] = {0}, sid[8] = {0};
			char cwhex[64] = {0}, bad[8] = {0};
			uint16_t u16caid, u16sid;
			uint32_t u32prov, h;
			uint8_t cw[16];
			if (sscanf(j + 5, "%7s %15s %7s %63s %7s",
			           caid, provid, sid, cwhex, bad) < 4) {
				printf("cxpeer: [FAIL] bad job '%s'\n", j); rc = 1; continue;
			}
			u16caid  = (uint16_t)strtoul(caid, NULL, 16);
			u32prov  = strtoul(provid, NULL, 16);
			u16sid   = (uint16_t)strtoul(sid, NULL, 16);
			if (hex2bin(cwhex, cw, 16) < 0) {
				printf("cxpeer: [FAIL] bad cw hex\n"); rc = 1; continue;
			}
			if (bad[0] == 'b') { cw[15] = (uint8_t)(cw[15] + 1); } /* checksumDCW breaks */
			h = ecm_hash(u16sid);
			memset(payload, 0, sizeof(payload));
			payload[0] = u16caid >> 8;  payload[1] = u16caid & 0xff;
			payload[2] = u32prov >> 24; payload[3] = u32prov >> 16;
			payload[4] = u32prov >> 8;  payload[5] = u32prov & 0xff;
			payload[10] = u16sid >> 8;  payload[11] = u16sid & 0xff;
			payload[12] = 0x24;
			payload[19] = 0x80;
			{
				uint8_t md[16];
				uint32_t t = h;
				MD5(ecm + 3, 61, md);   /* ecmd5, the real identity */
				memcpy(payload + 20, md, 16);
				payload[36] = (uint8_t)t; payload[37] = (uint8_t)(t >> 8);
				payload[38] = (uint8_t)(t >> 16); payload[39] = (uint8_t)(t >> 24);
			}
			memcpy(payload + 40, cw, 16);
			payload[56] = 1; /* uphop = local */
			memcpy(payload + 57, nodeid, 8);
			if (cx_msg_send(fd, &sendblock, CX_CCMSG_CACHE_PUSH, 65, payload) < 0) {
				printf("cxpeer: [FAIL] push send\n"); rc = 1; continue;
			}
			printf("cxpeer: [ ok ] push ch %04X:%06X:%04X hash %08X cw%s %.8s...\n",
			       u16caid, u32prov, u16sid, h, bad[0] == 'b' ? "(BAD)" : "", cwhex);
		}
		else { printf("cxpeer: [FAIL] unknown job '%s'\n", j); rc = 1; }
	}
	close(fd);
	return rc;
connfail:
	printf("cxpeer: [FAIL] connection lost mid-login\n");
	close(fd);
	return 1;
}

int main(int argc, char **argv)
{
	if (argc == 3 && !strcmp(argv[1], "--hash")) {
		printf("%08X\n", ecm_hash((uint16_t)strtoul(argv[2], NULL, 16)));
		return 0;
	}
	if (argc < 6) {
		fprintf(stderr, "usage: cxpeer host port user pass job [job...]\n"
		        "       cxpeer --hash <sid-hex>\n");
		return 2;
	}
	return run_jobs(argv[1], atoi(argv[2]), argv[3], argv[4], argc - 5, argv + 5);
}
