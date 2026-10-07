/*
 * sp-peer.c -- TASK 3.6 rig: a cache peer that proves SENDPIPE honesty, live.
 *
 * THE SCENARIO this peer drives (see the `sp` target in stability.mk):
 *
 *   1. come online like cachepeer.c does: answer the server's ping, then
 *      advertise one card. Never ping first (a peer-initiated ping actively
 *      suppresses the server-side state, see cachepeer.c).
 *   2. the server's first TYPE_REQUEST (a local client's ECM reached the
 *      cache REQUEST phase) is RECORDED, never answered. The requester is a
 *      cache-only profile, so its ECM dies within a second -- the exact
 *      moment TASK 3.6 clears the entry's SENDPIPE arm. Later requests are
 *      counted too, as evidence of the flow.
 *   3. PUSH_MS after the first request, one unsolicited TYPE_REPLY pushes
 *      a valid CW for the recorded identity. Pre-fix the stuck arm marks it
 *      DCW_SENT and pipes it at the dead waiter; post-fix it is stored
 *      unmarked, ready to serve.
 *   4. ASK_MS after the first request, the peer starts asking the server
 *      for the same hash itself (TYPE_REQUEST) and counts the TYPE_REPLYs.
 *      Pre-fix the TYPE_REQUEST auto-answer is suppressed by the stuck arm
 *      and the count is zero; post-fix the stored key answers every ask.
 *
 * One line per event, so the Makefile asserts on outcomes, not internals.
 * The identity is echoed from the server's own request -- the harness never
 * guesses the hash.
 *
 * Build: see the `sp` target. argv[1] server cache port, argv[2] my port,
 * argv[3] 32 hex chars of the CW to push.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>
#include <time.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#define TYPE_REQUEST   1
#define TYPE_REPLY     2
#define TYPE_PINGREQ   3
#define TYPE_CARD_LIST 0x13
#define TYPE_HELLO_ACK 0x10

static int hex2nib(int c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

static int parse_cw(const char *s, uint8_t out[16])
{
	int i;
	if (strlen(s) != 32) return -1;
	for (i = 0; i < 16; i++) {
		int hi = hex2nib(s[i * 2]), lo = hex2nib(s[i * 2 + 1]);
		if (hi < 0 || lo < 0) return -1;
		out[i] = (uint8_t)((hi << 4) | lo);
	}
	return 0;
}

/* Mirrors checksumDCW() in dcw.c: the pushed key must pass acceptDCW(). */
static int cw_checksum_ok(const uint8_t d[16])
{
	return d[3]  == (uint8_t)((d[0]  + d[1]  + d[2] ) & 0xFF)
	    && d[7]  == (uint8_t)((d[4]  + d[5]  + d[6] ) & 0xFF)
	    && d[11] == (uint8_t)((d[8]  + d[9]  + d[10]) & 0xFF)
	    && d[15] == (uint8_t)((d[12] + d[13] + d[14]) & 0xFF);
}

static uint32_t now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

int main(int argc, char **argv)
{
	int srvport = 15901, myport = 15902;
	/* Valid key (SUM groups), same family cachepeer.c uses. */
	uint8_t cw[16] = { 0x12,0x22,0x33,0x67, 0x44,0x55,0x66,0xFF,
	                   0x77,0x88,0x99,0x98, 0xAA,0xBB,0xCC,0x31 };
	unsigned char buf[512];
	struct sockaddr_in srv, me, from;
	socklen_t flen;
	int fd;
	int have_id = 0, pushed = 0, cards = 0;
	int asks = 0, answered = 0, reqs = 0;
	const int PUSH_MS  = 2500;  /* after the first request: the waiter (DCW TIMEOUT 1500) is dead by then */
	const int ASK_MS   = 4000;  /* after the first request  */
	const int ASK_EVERY = 600;
	const int ASK_N    = 6;
	unsigned char lastid[11];
	uint32_t t_firstreq = 0, t_lastask = 0;
	uint32_t t0 = now_ms();   /* CLOCK_MONOTONIC is since boot: all windows are relative to this */
	uint32_t caprov = 0x18840000u;

	if (argc > 1) srvport = atoi(argv[1]);
	if (argc > 2) myport  = atoi(argv[2]);
	if (argc > 3 && argv[3][0]) {
		if (parse_cw(argv[3], cw)) { printf("SP: bad cw hex '%s'\n", argv[3]); return 2; }
	}
	if (!cw_checksum_ok(cw))
		printf("SP: WARNING the pushed CW fails checksumDCW and will be dropped silently\n");

	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0) { perror("socket"); return 2; }

	memset(&me, 0, sizeof(me));
	me.sin_family = AF_INET;
	me.sin_port = htons((uint16_t)myport);
	me.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	/* Fixed bind: the server identifies the peer by (ip, recvport). */
	if (bind(fd, (struct sockaddr *)&me, sizeof(me)) < 0) { perror("bind"); return 2; }

	memset(&srv, 0, sizeof(srv));
	srv.sin_family = AF_INET;
	srv.sin_port = htons((uint16_t)srvport);
	srv.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	printf("SP: bound 127.0.0.1:%d, target 127.0.0.1:%d\n", myport, srvport);
	fflush(stdout);

	for (;;) {
		struct pollfd pfd;
		int r;
		uint32_t now = now_ms();

		if ((uint32_t)(now - t0) > 25000) {  /* failsafe: never hang the suite */
			printf("SP: TIMEOUT asks=%d answered=%d\n", asks, answered);
			fflush(stdout);
			return 1;
		}

		pfd.fd = fd; pfd.events = POLLIN; pfd.revents = 0;
		r = poll(&pfd, 1, 100);
		if (r < 0) { if (errno == EINTR) continue; perror("poll"); return 2; }

		if (r > 0) {
			flen = sizeof(from);
			memset(buf, 0, sizeof(buf));
			{
				ssize_t n = recvfrom(fd, buf, sizeof(buf), 0, (struct sockaddr *)&from, &flen);
				if (n < 2) continue;

				if (buf[0] == TYPE_PINGREQ && n >= 9) {
					unsigned char ack[16];
					memset(ack, 0, sizeof(ack));
					ack[0] = (buf[1] == 'M' && buf[2] == 'C') ? TYPE_HELLO_ACK : 4;
					ack[4] = buf[4];
					ack[5] = buf[5];
					/* buf[6..8] zero: a wrong CRC keeps us a plain CSP peer. */
					sendto(fd, ack, 16, 0, (struct sockaddr *)&srv, sizeof(srv));
					if (!cards) {
						unsigned char cl[6];
						memset(cl, 0, sizeof(cl));
						cl[0] = TYPE_CARD_LIST;
						cl[1] = 1;
						cl[2] = (unsigned char)(caprov >> 24);
						cl[3] = (unsigned char)(caprov >> 16);
						cl[4] = (unsigned char)(caprov >> 8);
						cl[5] = (unsigned char)(caprov);
						sendto(fd, cl, 6, 0, (struct sockaddr *)&srv, sizeof(srv));
						cards = 1;
						printf("SP: online, card %08X advertised\n", caprov);
						fflush(stdout);
					}
					continue;
				}

				if (buf[0] == TYPE_REQUEST && n >= 12) {
					reqs++;
					if (!have_id) {
						memcpy(lastid, buf + 1, 11);
						have_id = 1;
						t_firstreq = now_ms();
						printf("SP: REQ sid %02X%02X hash %02X%02X%02X%02X (recorded, staying silent)\n",
						       buf[2], buf[3], buf[8], buf[9], buf[10], buf[11]);
					}
					else printf("SP: REQ2.%d sid %02X%02X hash %02X%02X%02X%02X (still silent)\n",
						       reqs, buf[2], buf[3], buf[8], buf[9], buf[10], buf[11]);
					fflush(stdout);
					continue;   /* never answer a request: the scenario needs silence */
				}

				if (buf[0] == TYPE_REPLY && n >= 29) {
					answered++;
					printf("SP: ANSWERED %d cw=%02X%02X\n", answered, buf[13], buf[14]);
					fflush(stdout);
					continue;
				}

				printf("SP: ignored packet type %u (len %d)\n", (unsigned)buf[0], (int)n);
				fflush(stdout);
			}
		}

		now = now_ms();
		if (have_id && !pushed && (now - t_firstreq) >= (uint32_t)PUSH_MS) {
			unsigned char rep[29];
			memset(rep, 0, sizeof(rep));
			rep[0] = TYPE_REPLY;
			memcpy(rep + 1, lastid, 11);
			rep[12] = lastid[0];            /* integrity: buf[12]==buf[1] */
			memcpy(rep + 13, cw, 16);
			sendto(fd, rep, 29, 0, (struct sockaddr *)&srv, sizeof(srv));
			pushed = 1;
			printf("SP: PUSH cw=%02X%02X%02X%02X\n", cw[0], cw[1], cw[2], cw[3]);
			fflush(stdout);
		}

		if (have_id && asks < ASK_N && (now - t_firstreq) >= (uint32_t)ASK_MS
		    && (now - t_lastask) >= (uint32_t)ASK_EVERY) {
			unsigned char ask[12];
			memset(ask, 0, sizeof(ask));
			ask[0] = TYPE_REQUEST;
			memcpy(ask + 1, lastid, 11);
			sendto(fd, ask, 12, 0, (struct sockaddr *)&srv, sizeof(srv));
			asks++;
			t_lastask = now_ms();
			printf("SP: ASK %d for the recorded hash\n", asks);
			fflush(stdout);
		}

		if (have_id && asks >= ASK_N && (now - t_lastask) >= 800) {
			printf("SP: DONE asks=%d answered=%d reqs=%d\n", asks, answered, reqs);
			fflush(stdout);
			close(fd);
			return 0;
		}
	}
}
