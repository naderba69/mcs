/*
 * r107peer.c -- TASK R17 (D72): a minimal OSCAM-dialect camd35 cacheex
 * peer, for runtime-testing the r107 interop boundaries (per-peer
 * CACHEEX PUSHFILTER/NODELOOP/RCFILTER/CWCFWD options).
 *
 * WHY THIS EXISTS. cxpeer.c speaks the CCcam dialect; the r107/oscam
 * cacheex wire is the camd35 0x3C/0x3D/0x3E/0x3F family. This peer
 * builds and parses those frames BYTE BY BYTE from the oscam source
 * layout (gfto/oscam module-camd35-cacheex.c, verified 2026-10-12; the
 * same layout is pinned by make-x64/test_cexwire.c), so what Multics
 * sees here is exactly what a modern oscam partner sends.
 *
 * SELF-CONTAINED by harness doctrine (see cachepeer.c): AES/MD5/crc32
 * come from the tree's own aes.c / md5.c / crc32.c included directly,
 * and the ECM identity follows ncclient's deterministic formula
 * (ecm[i] = i*7 + 1 + (sid & 0xFF), i = 3..63; hash = hashCode(ecm+3,61);
 * ecmd5 = MD5(ecm+3,61)), so a push with `sid` matches the ECM an
 * ncclient run made pending.
 *
 * USAGE
 *   r107peer <host> <port> <user> <pass> <job> [job...]
 * jobs:
 *   idreq <nodehex16>                       0x3D id-request (our node @+20)
 *   push <caid> <provid> <sid> <cwhex> [rc] [cwc] [n1hex [n2hex...]]
 *       one 0x3F push (rc default 0 = E_FOUND; cwc default 0)
 *   filter <caid> <provid> <sid>            one 0x3C push-filter table
 *       (one entry; provid/sid FFFFFFFF = wildcard)
 *   expect push [<cwhex> [<cwcval> [<nodehex>]]]
 *       wait up to 8s for a 0x3F push; verify the given fields
 *   expect idreply                          wait up to 3s for a 0x3E
 *   expect none <ms>                        assert no 0x3F within ms
 *   learn-node <file>                       save the first node id of the
 *       next received push to <file> (the server's cccam_nodeid)
 *   sleep <ms>
 * exit code 0 iff every job ran and every expectation held.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "../src/aes.h"
#include "../src/aes.c"
#include "../src/md5.c"
#include "../src/crc32.c"

/* ------------------------------------------------------------------ */
/* camd35 wire (src/msg-camd35.c):  [ucrc 4 BE][AES(payload padded16)] */
/* payload: [cmd][len][..][datacrc 4 @4..7][..][data @20..];           */
/* datacrc = crc32(payload+20, len-20); AES key = MD5(pass);           */
/* ucrc = crc32(MD5(user), 16).                                        */

static AES_KEY c35_ekey, c35_dkey;
static uint32_t c35_ucrc;

static int c35_pad(int len) { return (len & 0x0f) ? (len & 0xfff0) + 0x10 : len; }

static void c35_init(const char *user, const char *pass)
{
	uint8_t md5tmp[16];
	c35_ucrc = crc32(0, MD5((uint8_t *)user, strlen(user), md5tmp), 16);
	AES_set_encrypt_key(MD5((uint8_t *)pass, strlen(pass), md5tmp), 128, &c35_ekey);
	AES_set_decrypt_key(MD5((uint8_t *)pass, strlen(pass), md5tmp), 128, &c35_dkey);
}

static int c35_send(int fd, struct sockaddr_in *srv, uint8_t *payload, int plen)
{
	uint8_t sbuf[1024];
	int newlen = c35_pad(plen);
	int i;
	uint32_t datacrc;
	memset(sbuf, 0, sizeof(sbuf));
	sbuf[0] = c35_ucrc >> 24; sbuf[1] = c35_ucrc >> 16;
	sbuf[2] = c35_ucrc >> 8; sbuf[3] = c35_ucrc;
	memcpy(sbuf + 4, payload, plen);
	memset(sbuf + 4 + plen, 0xFF, 15);
	datacrc = crc32(0, payload + 20, plen - 20);
	sbuf[8] = datacrc >> 24; sbuf[9] = datacrc >> 16;
	sbuf[10] = datacrc >> 8; sbuf[11] = datacrc;
	for (i = 0; i < newlen; i += 16) AES_encrypt(sbuf + 4 + i, sbuf + 4 + i, &c35_ekey);
	if (sendto(fd, sbuf, 4 + newlen, 0, (struct sockaddr *)srv, sizeof(*srv)) < 0) {
		printf("  [FAIL] sendto: %s\n", strerror(errno));
		return -1;
	}
	return 0;
}

/* receive one datagram; decrypt; return payload length or 0 on timeout */
static int c35_recv(int fd, uint8_t *payload, int timeout_ms)
{
	uint8_t sbuf[1400];
	struct timeval tv;
	int received, i, datalen;
	tv.tv_sec = timeout_ms / 1000;
	tv.tv_usec = (timeout_ms % 1000) * 1000;
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	received = recv(fd, sbuf, sizeof(sbuf), 0);
	if (received <= 0) return 0;
	if (received < 8) return 0;
	received -= 4;
	for (i = 0; i + 16 <= received; i += 16) AES_decrypt(sbuf + 4 + i, sbuf + 4 + i, &c35_dkey);
	memcpy(payload, sbuf + 4, received);
	/* length rules mirror the server parse */
	if (payload[4] == 0) datalen = (((payload[25] & 0x0f) << 8) | payload[26]) + 3;
	else if ((payload[4] & 0xFC) == 0x3C) datalen = payload[5] | (payload[6] << 8);
	else datalen = payload[5];
	(void)datalen;
	return received;
}

/* ------------------------------------------------------------------ */
/* the ECM identity contract (ncclient.c)                              */
static void ecm_build(uint16_t sid, uint8_t *ecm)
{
	int i;
	memset(ecm, 0, 64);
	ecm[0] = 0x80; ecm[1] = 0; ecm[2] = 61;
	for (i = 3; i < 64; i++) ecm[i] = (uint8_t)(i * 7 + 1 + (sid & 0xFF));
}

static uint32_t ecm_hash(uint16_t sid)
{
	uint8_t ecm[64];
	uint32_t h = 0;
	int i;
	ecm_build(sid, ecm);
	for (i = 3; i < 64; i++) h = 31 * h + ecm[i];
	return h;
}

static void ecm_ecmd5(uint16_t sid, uint8_t *out)
{
	uint8_t ecm[64];
	ecm_build(sid, ecm);
	MD5(ecm + 3, 61, out);
}

/* ------------------------------------------------------------------ */
/* oscam camd35 cacheex payloads (module-camd35-cacheex.c layout)      */
/* payload == the 20-byte header + `size` data bytes; wire offsets     */
/* below are payload offsets (the framed datagram carries them at +4).  */

static int build_push(uint8_t *b, uint16_t caid, uint32_t provid, uint16_t sid,
	const char *cwhex, int rc, int cwc, const uint8_t nodes[][8], int nn)
{
	int size = 16 + 4 + 16 + 1 + nn * 8;
	int i;
	memset(b, 0, 20 + size);
	b[0] = 0x3F;
	b[1] = size & 0xff;
	b[2] = size >> 8;
	b[3] = (uint8_t)rc;
	b[8] = sid >> 8; b[9] = sid & 0xff;
	b[10] = caid >> 8; b[11] = caid & 0xff;
	b[12] = provid >> 24; b[13] = provid >> 16; b[14] = provid >> 8; b[15] = provid;
	b[18] = (uint8_t)cwc;
	b[19] = 0x80;	/* the ECM tag: ecm[0] & 0xFE */;
	ecm_ecmd5(sid, b + 20);
	{
		uint32_t h = ecm_hash(sid);
		b[36] = h & 0xff; b[37] = (h >> 8) & 0xff;
		b[38] = (h >> 16) & 0xff; b[39] = h >> 24;
	}
	for (i = 0; i < 16; i++) {
		unsigned v = 0;
		sscanf(cwhex + 2 * i, "%2x", &v);
		b[40 + i] = (uint8_t)v;
	}
	b[56] = nn;
	for (i = 0; i < nn; i++) memcpy(b + 57 + 8 * i, nodes[i], 8);
	return 20 + size;
}

static int build_filter(uint8_t *b, uint16_t caid, uint32_t provid, uint16_t sid)
{
	int size = 2 + 4 * 4 * 15;	/* count + 15x4 arrays (oscam wire 242) */
	int i;
	memset(b, 0, 20 + size);
	b[0] = 0x3C;
	b[1] = size & 0xff;
	b[2] = size >> 8;
	b[20] = 0; b[21] = 1;			/* cevnum = 1 */
	b[22] = 0; b[23] = 0; b[24] = caid >> 8; b[25] = caid & 0xff;
	for (i = 0; i < 15; i++) {		/* cmask: all -1 */
		b[22 + 60 + 4 * i] = 0xFF; b[22 + 60 + 4 * i + 1] = 0xFF;
		b[22 + 60 + 4 * i + 2] = 0xFF; b[22 + 60 + 4 * i + 3] = 0xFF;
	}
	if (provid == 0xFFFFFFFFu) {
		b[22 + 120] = 0xFF; b[22 + 120 + 1] = 0xFF;
		b[22 + 120 + 2] = 0xFF; b[22 + 120 + 3] = 0xFF;
	} else {
		b[22 + 120] = provid >> 24; b[22 + 120 + 1] = provid >> 16;
		b[22 + 120 + 2] = provid >> 8; b[22 + 120 + 3] = provid;
	}
	if (sid == 0xFFFF) {
		b[22 + 180] = 0xFF; b[22 + 180 + 1] = 0xFF;
		b[22 + 180 + 2] = 0xFF; b[22 + 180 + 3] = 0xFF;
	} else {
		b[22 + 180] = 0; b[22 + 180 + 1] = 0;
		b[22 + 180 + 2] = sid >> 8; b[22 + 180 + 3] = sid & 0xff;
	}
	/* remaining entries stay caid 0 = never match */
	return 20 + size;
}

static int hex2node(const char *h, uint8_t node[8])
{
	int i;
	for (i = 0; i < 8; i++) {
		unsigned v = 0;
		if (sscanf(h + 2 * i, "%2x", &v) != 1) return -1;
		node[i] = (uint8_t)v;
	}
	return 0;
}

int main(int argc, char **argv)
{
	int fd, i, ok = 1;
	struct sockaddr_in srv;
	uint8_t payload[512], recvbuf[512];
	uint8_t nodes[10][8];
	int nnodes = 0;
	int learned = 0;
	const char *learnfile = NULL;

	setvbuf(stdout, NULL, _IONBF, 0);
	if (argc < 6) {
		printf("usage: r107peer <host> <port> <user> <pass> <job>...\n");
		return 2;
	}
	c35_init(argv[3], argv[4]);
	fd = socket(AF_INET, SOCK_DGRAM, 0);
	memset(&srv, 0, sizeof(srv));
	srv.sin_family = AF_INET;
	srv.sin_port = htons(atoi(argv[2]));
	inet_aton(argv[1], &srv.sin_addr);

	for (i = 5; i < argc; i++) {
		/* each argv element is one job string: "verb arg arg ..." */
		char jobbuf[512], *jobargv[16], *tok;
		int jobargc = 0, j;
		strncpy(jobbuf, argv[i], sizeof(jobbuf) - 1);
		jobbuf[sizeof(jobbuf) - 1] = 0;
		tok = strtok(jobbuf, " \t");
		while (tok && jobargc < 16) { jobargv[jobargc++] = tok; tok = strtok(NULL, " \t"); }
		if (!jobargc) continue;
		{
		char *av[24];
		int k;
		memset(av, 0, sizeof(av));
		for (j = 0; j < jobargc; j++) av[5 + j] = jobargv[j];
#define argv av
#define i k
		for (k = 5; k < 5 + jobargc; k++) {
		const char *job = av[k];

		if (!strcmp(job, "idreq")) {
			int plen;
			memset(payload, 0, sizeof(payload));
			payload[0] = 0x3D;
			payload[1] = 12;
			hex2node(argv[++i], payload + 20);
			plen = 20 + 12;
			if (c35_send(fd, &srv, payload, plen) < 0) ok = 0;
			else printf("  [ ok ] idreq sent\n");
		} else if (!strcmp(job, "push")) {
			int plen, rc = 0, cwc = 0;
			nnodes = 0;
			uint16_t caid = (uint16_t)strtoul(argv[++i], NULL, 16);
			uint32_t provid = strtoul(argv[++i], NULL, 16);
			uint16_t sid = (uint16_t)strtoul(argv[++i], NULL, 16);
			const char *cwhex = argv[++i];
			while (argv[i + 1] != NULL) {
				const char *a = argv[i + 1];
				if (!strncmp(a, "rc", 2) && strlen(a) > 2 && a[2] >= '0' && a[2] <= '9') {
					rc = atoi(a + 2); i++;
				} else if (!strncmp(a, "cwc", 3) && strlen(a) > 3) {
					cwc = atoi(a + 3); i++;
				} else if (!strncmp(a, "n", 1) && strlen(a) == 17) {
					hex2node(a + 1, nodes[nnodes]); nnodes++; i++;
				} else break;
			}
			if (nnodes == 0) { hex2node("1111111111111111", nodes[0]); nnodes = 1; }
			plen = build_push(payload, caid, provid, sid, cwhex, rc, cwc, nodes, nnodes);
			if (c35_send(fd, &srv, payload, plen) < 0) ok = 0;
			else printf("  [ ok ] push sent (rc=%d cwc=%d nn=%d)\n", rc, cwc, nnodes);
		} else if (!strcmp(job, "filter")) {
			int plen;
			uint16_t caid = (uint16_t)strtoul(argv[++i], NULL, 16);
			const char *ps = argv[++i], *ss = argv[++i];
			uint32_t provid = strtoul(ps, NULL, 16);
			uint16_t sid = (uint16_t)strtoul(ss, NULL, 16);
			if (!strcmp(ps, "FFFFFFFF")) provid = 0xFFFFFFFFu;
			if (!strcmp(ss, "FFFF")) sid = 0xFFFF;
			plen = build_filter(payload, caid, provid, sid);
			if (c35_send(fd, &srv, payload, plen) < 0) ok = 0;
			else printf("  [ ok ] filter sent\n");
		} else if (!strcmp(job, "expect")) {
			const char *what = argv[++i];
			if (!strcmp(what, "idreply")) {
				int got = 0, t;
				for (t = 0; t < 6 && !got; t++) {
					int r = c35_recv(fd, recvbuf, 500);
					if (r > 4 && recvbuf[0] == 0x3E) got = 1;
				}
				if (got) printf("  [ ok ] idreply received (node ");
				else { printf("  [FAIL] no idreply within 3s\n"); ok = 0; }
				if (got) {
					int k;
					for (k = 0; k < 8; k++) printf("%02x", recvbuf[20 + k]);
					printf(")\n");
					if (learnfile && !learned) {
						FILE *f = fopen(learnfile, "w");
						if (f) {
							for (k = 0; k < 8; k++) fprintf(f, "%02x", recvbuf[20 + k]);
							fprintf(f, "\n");
							fclose(f);
							learned = 1;
						}
					}
				}
			} else if (!strcmp(what, "push")) {
				/* expect push <cwhex|-> <cwc|-> <nodehex|-> : '-' skips */
				const char *want_cw = argv[++i];
				const char *want_cwc_s = argv[++i];
				const char *want_node = argv[++i];
				int want_cwc = -1;
				int got = 0, t;
				if (!strcmp(want_cw, "-")) want_cw = NULL;
				if (strcmp(want_cwc_s, "-")) want_cwc = atoi(want_cwc_s);
				if (!strcmp(want_node, "-")) want_node = NULL;
				for (t = 0; t < 16 && !got; t++) {
					int r = c35_recv(fd, recvbuf, 500);
					if (r > 4 && recvbuf[0] == 0x3F) {
						uint8_t *p = recvbuf;	/* payload (cmd at [0]) */
						got = 1;
						printf("  [ ok ] push received (cmd 0x%02x size %d rc %d cwc 0x%02x nn %d)\n",
							p[0], p[1] | (p[2] << 8), p[3], p[18], p[56]);
						/* wire-shape checks an oscam peer would make */
						if (p[0] != 0x3F) { printf("  [FAIL] wire: cmd != 0x3F\n"); ok = 0; }
						if ((p[1] | (p[2] << 8)) != 37 + 8 * p[56]) {
							printf("  [FAIL] wire: size %d != 37+8*nn\n", p[1] | (p[2] << 8)); ok = 0;
						}
						if (want_cw) {
							uint8_t w[16]; int k;
							for (k = 0; k < 16; k++) {
								unsigned v = 0; sscanf(want_cw + 2 * k, "%2x", &v); w[k] = (uint8_t)v;
							}
							if (memcmp(p + 40, w, 16)) {
								printf("  [FAIL] wire: cw mismatch\n"); ok = 0;
							}
						}
						if (want_cwc >= 0 && p[18] != (uint8_t)want_cwc) {
							printf("  [FAIL] wire: cwc 0x%02x != 0x%02x\n", p[18], want_cwc); ok = 0;
						}
						if (want_node) {
							uint8_t n[8];
							hex2node(want_node, n);
							if (memcmp(p + 57, n, 8)) {
								printf("  [FAIL] wire: nodes[0] mismatch\n"); ok = 0;
							}
						}
						if (learnfile && !learned) {
							FILE *f = fopen(learnfile, "w");
							int k;
							if (f) {
								for (k = 0; k < 8; k++) fprintf(f, "%02x", p[57 + k]);
								fprintf(f, "\n");
								fclose(f);
								learned = 1;
							}
						}
					} else if (r > 4 && recvbuf[0] == 0x3E) {
						/* ignore id replies while waiting */
						t--;
					}
				}
				if (!got) { printf("  [FAIL] no push within 8s\n"); ok = 0; }
			} else if (!strcmp(what, "none")) {
				int window = atoi(argv[++i]);
				int seen = 0, waited = 0;
				while (waited < window) {
					int r = c35_recv(fd, recvbuf, 100);
					if (r > 4 && recvbuf[0] == 0x3F) seen = 1;
					waited += 100;
				}
				if (!seen) printf("  [ ok ] no push arrived (%d ms)\n", window);
				else { printf("  [FAIL] an unexpected push arrived\n"); ok = 0; }
			}
		} else if (!strcmp(job, "learn-node")) {
			static char learnbuf[128];
			strncpy(learnbuf, argv[++i], sizeof(learnbuf) - 1);
			learnbuf[sizeof(learnbuf) - 1] = 0;
			learnfile = learnbuf;
		} else if (!strcmp(job, "sleep")) {
			usleep(atoi(argv[++i]) * 1000);
		} else {
			printf("  [FAIL] unknown job '%s'\n", job);
			ok = 0;
		}
#undef i
#undef argv
		}
		}
	}
	close(fd);
	printf(ok ? "r107peer done: OK\n" : "r107peer done: FAIL\n");
	return ok ? 0 : 1;
}
