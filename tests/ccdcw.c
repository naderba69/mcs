/*
 * ccdcw.c -- TASK R41: a minimal CCcam CLIENT that asks for control words.
 *
 * WHY THIS EXISTS. Every existing rig in this directory speaks either
 * newcamd (ncclient) or the cache-exchange dialects (cachepeer, cxpeer,
 * r107peer). None of them drives the CCcam ECM/DCW path in srv-cccam.c
 * -- which is the path the field report came from: a CCcam 2.3.2
 * receiver showing a black screen while the server page said "Decode
 * success".  The gate under test (checkfreeze_setdcw, DCW CHECK) lives
 * in that path and nowhere else, so it was untestable until now.
 *
 * WHAT IT DOES. Logs in exactly like cxpeer.c (same handshake as
 * cli-cccam.c cc_connect_srv()), sends CC_MSG_CLI_INFO, then for each
 * requested ECM: sends CC_MSG_ECM_REQUEST and waits for the answer.
 *   - cmd 0x01, 16 bytes  -> a DCW was sent to the client   (printed "DCW <hex>")
 *   - cmd 0xfe / 0xff     -> the server answered NOK          (printed "NOK")
 *   - nothing within N ms -> no answer at all                 (printed "TIMEOUT")
 *
 * THE ECM. Deterministic, the ncclient/cxpeer formula, with a caller
 * chosen tag byte:  ecm[0]=tag, ecm[1]=0, ecm[2]=61,
 * ecm[i]=i*7+1+(sid&0xff) for i=3..63.  A different tag means a
 * different ECM identity (hash) for the SAME channel -- which is what a
 * real stream does when it flips 0x80/0x81 between repeats inside one
 * crypto period.
 *
 * USAGE
 *   ccdcw <host> <port> <user> <pass> <caid_hex> <sid_hex> <tag> [tag...]
 *   each <tag> is sent as one ECM request, in order, each after the
 *   previous answer (or its timeout).
 *
 * exit code 0 always (the Makefile asserts on the printed verdicts).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>
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

/* Reverse the CW obfuscation before advancing the additional stream step.
 * This fixture requests card ID zero; keep the general transform explicit. */
static void cc_crypt_cw_fixture(const uint8_t nodeid[8], uint32_t card_id, uint8_t cw[16])
{
    int i; uint8_t nod[8];
    for (i = 0; i < 8; i++) nod[i] = nodeid[7-i];
    for (i = 0; i < 16; i++) {
        int n = (i & 1) ? ((i == 15) ? nod[i>>1] >> 4 :
                    (nod[i>>1] >> 4) | (nod[(i>>1)+1] << 4)) : nod[i>>1];
        uint8_t v = cw[i] ^ (n & 0xff);
        if (i & 1) v = (uint8_t)~v;
        cw[i] = (uint8_t)((card_id >> (2*i)) ^ v);
    }
}

/* ------------------------------------------------------------------ */
/* Wire constants from src/msg-cccam.h                                 */
#define CC_MSG_ECM_REQUEST  0x01
#define CC_MSG_CLI_INFO     0x00
#define CC_MSG_ECM_NOK1     0xfe
#define CC_MSG_ECM_NOK2     0xff
#define CC_MSG_NO_HEADER    0xffff

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

static int cc_msg_recv(int fd, struct cc_crypt_block *rb, uint8_t *buf)
{
	uint8_t netbuf[2048];
	int len, dlen;
	if (recv_all(fd, netbuf, 4, 6000) != 4) return -1;
	cc_decrypt(rb, netbuf, 4);
	dlen = (netbuf[2] << 8) | netbuf[3];
	if (dlen) {
		if (recv_all(fd, netbuf + 4, dlen, 6000) != dlen) return -1;
		cc_decrypt(rb, netbuf + 4, dlen);
		len = dlen + 4;
	} else len = 4;
	memcpy(buf, netbuf, len);
	return len;
}

static int cc_msg_send(int fd, struct cc_crypt_block *sb, int cmd, int len, uint8_t *buf)
{
	uint8_t netbuf[2048];
	memset(netbuf, 0, len + 4);
	if (cmd == CC_MSG_NO_HEADER) memcpy(netbuf, buf, len);
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
static void ecm_build(uint16_t sid, uint8_t tag, int seed, uint8_t *ecm)
{
	int i;
	memset(ecm, 0, 64);
	ecm[0] = tag;
	ecm[1] = 0;
	ecm[2] = 61;
	/* The identity hash starts at byte 3 (ecmdata.c), so a payload that
	 * only flipped the tag would be the SAME ECM to the server. Vary the
	 * body with the tag: a different tag AND a different payload is one
	 * new ECM of the same channel, which is what the stream sends. */
	/* TASK R43 (D106): `seed` makes a SECOND body for the same tag,
	 * so a test can ask a genuinely new ECM (new identity hash) of
	 * the same channel instead of re-asking the delivered one. */
	for (i = 3; i < 64; i++) ecm[i] = (uint8_t)(i * 7 + 1 + (sid & 0xFF) + (tag & 0x0F) + seed * 0x40);
	/* Preserve uniqueness beyond four seeds despite byte wraparound. */
	ecm[3] ^= (uint8_t)((unsigned)seed >> 2);
}

static int tcp_connect(const char *host, int port)
{
	struct sockaddr_in srv;
	struct hostent *he;
	int fd, one = 1;
	memset(&srv, 0, sizeof(srv));
	srv.sin_family = AF_INET;
	srv.sin_port = htons(port);
	if (inet_aton(host, &srv.sin_addr) == 0) {
		he = gethostbyname(host);
		if (!he) return -1;
		memcpy(&srv.sin_addr, he->h_addr_list[0], sizeof(srv.sin_addr));
	}
	fd = socket(PF_INET, SOCK_STREAM, 0);
	if (fd < 0) return -1;
	setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
	if (connect(fd, (struct sockaddr *)&srv, sizeof(srv)) < 0) { close(fd); return -1; }
	return fd;
}

int main(int argc, char **argv)
{
	uint8_t seed[24], hash[24], buf[512], payload[128], ecm[64];
	struct cc_crypt_block sendblock, recvblock;
	SHA_CTX ctx;
	int fd, i, r, ecmno;
	uint8_t nodeid[8] = { 0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8 };
	uint16_t caid, sid;

	if (argc < 8) {
		fprintf(stderr, "usage: ccdcw <host> <port> <user> <pass> <caid_hex> <sid_hex> <tag> [tag...]\n");
		return 2;
	}
	caid = (uint16_t)strtoul(argv[5], NULL, 16);
	sid  = (uint16_t)strtoul(argv[6], NULL, 16);

	fd = tcp_connect(argv[1], atoi(argv[2]));
	if (fd < 0) { printf("ccdcw: [FAIL] connect %s:%s\n", argv[1], argv[2]); return 1; }

	if (recv_all(fd, seed, 16, 6000) != 16) {
		printf("ccdcw: [FAIL] no 16-byte seed\n"); close(fd); return 1;
	}
	cc_crypt_xor(seed);
	SHA1_Init(&ctx);
	SHA1_Update(&ctx, seed, 16);
	SHA1_Final(hash, &ctx);
	cc_crypt_init(&recvblock, hash, 20);
	cc_decrypt(&recvblock, seed, 16);
	cc_crypt_init(&sendblock, seed, 16);
	cc_decrypt(&sendblock, hash, 20);

	if (cc_msg_send(fd, &sendblock, CC_MSG_NO_HEADER, 20, hash) < 0) goto connfail;
	memset(buf, 0, 32);
	memcpy(buf, argv[3], strlen(argv[3]) > 20 ? 20 : strlen(argv[3]));
	if (cc_msg_send(fd, &sendblock, CC_MSG_NO_HEADER, 20, buf) < 0) goto connfail;
	{
		char pwd[64];
		uint8_t magic[8];
		memset(pwd, 0, sizeof(pwd));
		strncpy(pwd, argv[4], 63);
		cc_encrypt(&sendblock, (uint8_t *)pwd, strlen(pwd));
		memset(magic, 0, sizeof(magic));
		memcpy(magic, "CCcam\0", 6);
		if (cc_msg_send(fd, &sendblock, CC_MSG_NO_HEADER, 6, magic) < 0) goto connfail;
	}
	if (recv_all(fd, buf, 20, 6000) != 20) { printf("ccdcw: [FAIL] no pwd ack\n"); close(fd); return 1; }
	cc_decrypt(&recvblock, buf, 20);
	if (memcmp(buf, "CCcam", 5)) {
		printf("ccdcw: [FAIL] login rejected\n"); close(fd); return 1;
	}
	printf("ccdcw: [ ok ] cccam login as '%s'\n", argv[3]);

	/* CC_MSG_CLI_INFO: user20 + nodeid8 + wantemus1 + version32 + who3 + build32 */
	memset(buf, 0, 128);
	memcpy(buf, argv[3], strlen(argv[3]) > 20 ? 20 : strlen(argv[3]));
	memcpy(buf + 20, nodeid, 8);
	buf[28] = 0;
	memcpy(buf + 29, "2.3.2", 5);
	if (cc_msg_send(fd, &sendblock, CC_MSG_CLI_INFO, 64, buf) < 0) goto connfail;

	for (ecmno = 7; ecmno < argc; ecmno++) {
		/* Optional delay for isolated CW-age regressions. */
		if (ecmno > 7 && getenv("CCDCW_BETWEEN_MS")) {
			unsigned long ms = strtoul(getenv("CCDCW_BETWEEN_MS"), NULL, 10);
			if (ms <= 30000) usleep((useconds_t)ms * 1000);
		}
		/* Isolated multi-channel trace fixture: keep one CCcam connection. */
		if (ecmno > 7 && getenv("CCDCW_NEXT_SID"))
			sid = (uint16_t)strtoul(getenv("CCDCW_NEXT_SID"), NULL, 16);
		/* "0x80" or "0x80:2" -- the suffix is the body seed (R43). */
		char *pend = NULL;
		int seed = 0;
		int tag = (int)strtol(argv[ecmno], &pend, 0);
		if (pend && *pend == ':') seed = (int)strtol(pend + 1, NULL, 0);
		int got_dcw = 0, got_nok = 0;

		ecm_build(sid, (uint8_t)tag, seed, ecm);
		memset(payload, 0, sizeof(payload));
		payload[0] = caid >> 8;
		payload[1] = caid & 0xff;
		/* provid (4) = 0, cardid (4) = 0x0000 (profile by caid:prov) */
		payload[10] = sid >> 8;
		payload[11] = sid & 0xff;
		memcpy(payload + 13, ecm, 64);
		if (cc_msg_send(fd, &sendblock, CC_MSG_ECM_REQUEST, 13 + 64, payload) < 0) goto connfail;

		for (i = 0; i < 8; i++) {
			r = cc_msg_recv(fd, &recvblock, buf);
			if (r < 0) break;
			if (r >= 4 && buf[1] == CC_MSG_ECM_REQUEST && r >= 20) {
				/* the DCW payload carries the stock "additional crypto
				 * step" (srv-cccam.c does cc_encrypt(buf,16) after the
				 * send; cli-cccam.c mirrors it with cc_decrypt). Consume
				 * those 16 keystream bytes or every later frame is read
				 * one message behind. */
				uint8_t delivered[16];
				cc_crypt_cw_fixture(nodeid, 0, buf + 4);
				memcpy(delivered, buf + 4, sizeof(delivered));
				cc_decrypt(&recvblock, buf + 4, r - 4);
				if (getenv("CCDCW_PRINT_CW")) {
					int j;
					printf("ccdcw: ECM%d CW=", ecmno - 6);
					for (j=0; j<16; j++) printf("%02X", delivered[j]);
					printf("\n");
				}
				got_dcw = 1; break;
			}
			if (r >= 2 && (buf[1] == CC_MSG_ECM_NOK1 || buf[1] == CC_MSG_ECM_NOK2)) { got_nok = 1; break; }
			/* anything else (keepalive, card list...) -> keep waiting */
		}
		if (got_dcw) printf("ccdcw: ECM%d tag=%02X DCW\n", ecmno - 6, tag);
		else if (got_nok) printf("ccdcw: ECM%d tag=%02X NOK\n", ecmno - 6, tag);
		else printf("ccdcw: ECM%d tag=%02X TIMEOUT\n", ecmno - 6, tag);
	}

	close(fd);
	return 0;

connfail:
	printf("ccdcw: [FAIL] connection lost\n");
	close(fd);
	return 1;
}
