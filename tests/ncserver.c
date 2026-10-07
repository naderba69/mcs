/*
 * ncserver.c — a real Newcamd card server for MultiCS, built from MultiCS's own
 * code, the mirror image of ncclient.c.
 *
 * WHY THIS EXISTS. Two things have been blocked on the absence of a card server:
 *
 *   1. TASK 2.3 (latency forensics) cannot be tested at all without something
 *      that answers an ECM on a schedule the test controls. The verdict is a
 *      statement about how long a card takes, so the harness has to BE the card
 *      and decide how long it takes.
 *   2. TASK 1.9's routing half (`rotation_should_avoid_limited()` is consulted
 *      in srvtab_arrange(), i.e. only when a card server is being chosen) has
 *      never been shown live: the existing harness has a mock cache peer and
 *      mock clients, but nothing that MultiCS would ever pick as a DECODER.
 *
 * HOW IT IS BUILT. It links the server's own des.c/msg-newcamd.c/md5.c, exactly
 * like ncclient.c, so the framing and the cipher are identical by construction
 * rather than by careful copying. The handshake is the one cli-newcamd.c performs,
 * read from that file and implemented from the other side:
 *
 *   1. accept, send a 14-byte keymod (no 'MCS' markers: this is a generic
 *      newcamd server, which is what a real one is)
 *   2. read MSG_CLIENT_2_SERVER_LOGIN, derive the session key from the md5-crypt
 *      password in the message -- cli-newcamd.c:98 does the same
 *   3. send MSG_CLIENT_2_SERVER_LOGIN_ACK
 *   4. answer MSG_CARD_DATA_REQ with MSG_CARD_DATA for the configured CAID
 *   5. on each ECM (0x80/0x81): sleep for the delay this reply is scheduled to
 *      take, then answer 0x81 with a valid control word whose 4th/8th/12th/16th
 *      bytes satisfy checksumDCW() -- a key that failed the checksum would be
 *      dropped before MultiCS could measure anything
 *
 * THE DELAY SCRIPT is the whole point: `NS_DELAYS` is a comma-separated list of
 * per-reply delays in ms, and the last value repeats forever ("63*" would be
 * nicer, but a list is easier to read in a Makefile). A phase-based target sets
 * it once and each reply takes its turn, so the same connection -- and therefore
 * the same server slot and the same window -- carries honest jitter, then a
 * metronome, then instant answers.
 *
 *   usage: ncserver <listen_port> [key_hex] [caid_hex] [provid_hex]
 *   env:   NS_DELAYS   "60,63,61,..."  per-reply delay in ms (default "60")
 *          NS_VERBOSE  1 to print every reply with its delay
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <stdarg.h>
#include <errno.h>
#include <poll.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <ctype.h>

#include "../src/des.h"
#include "../src/md5.h"
/*
 * msg-newcamd.h sizes its buffers with MAX_ECM_SIZE, which normally arrives from
 * common.h. This harness includes no server headers at all -- that is what keeps
 * it a client's mirror image rather than a copy of the server -- so it supplies
 * the same constant common.h does, with the value read from common.h:41.
 */
#define MAX_ECM_SIZE 700
#include "../src/msg-newcamd.h"

/* ---- stubs for what msg-newcamd.c calls ----------------------------------- */

static int verbose = 0;

int send_nonb(int sock, uint8_t *buf, int len, int to)
{
	int sent = 0, n;
	(void)to;
	while (sent < len) {
		n = (int)send(sock, buf + sent, (size_t)(len - sent), MSG_NOSIGNAL);
		if (n <= 0) { if (errno == EINTR) continue; return 0; }
		sent += n;
	}
	return len;
}

/*
 * recv_nonb/2 -- the same shape as sockets.h:21 and the same implementation as
 * ncclient.c's stub: poll first, then one recv, returning what arrived. The
 * server's own sockets.c is not linked, deliberately: this harness must not
 * depend on any of the server's state to be a faithful peer.
 */
int recv_nonb(int sock, uint8_t *buf, int len, int timeout)
{
	struct pollfd pfd;
	int got = 0, n;

	while (got < len) {
		pfd.fd = sock; pfd.events = POLLIN; pfd.revents = 0;
		n = poll(&pfd, 1, timeout);
		if (n <= 0) return got;
		n = (int)recv(sock, buf + got, (size_t)(len - got), 0);
		if (n <= 0) { if (n < 0 && errno == EINTR) continue; return got; }
		got += n;
	}
	return got;
}

/* what msg-newcamd.c's own debug lines call; no-ops here (NS_VERBOSE is mine) */
void debugf(int dbg, char *fmt, ...)
{
	va_list ap;
	(void)dbg;
	if (!verbose) return;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
}
void debughex(uint8_t *buf, int len)
{
	int i;
	if (!verbose) return;
	for (i = 0; i < len; i++) fprintf(stderr, "%02X ", buf[i]);
	fprintf(stderr, "\n");
}
int getdbgflag(int a, int b, int c) { (void)a; (void)b; (void)c; return 0; }

static long ms_now(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec * 1000L + tv.tv_usec / 1000L;
}

/* ---- the keymod the server sends (cli-newcamd.c:36-40 reads it) ----------- */

static void make_keymod(uint8_t keymod[14])
{
	int i;
	/*
	 * Random, and deliberately WITHOUT MultiCS's 'MCS' markers. A real newcamd
	 * server has no reason to advertise itself, and the client's marker check
	 * is exactly what a peer would have to reproduce to be mistaken for
	 * MultiCS -- so the mock stays on the honest side of that.
	 */
	for (i = 0; i < 14; i++) keymod[i] = (uint8_t)(rand() & 0xFF);
}

/*
 * A control word that will pass checksumDCW(): each 4th byte is the sum of the
 * three before it. isbadDCW()'s repeated-triple rule is satisfied by varying the
 * bytes, and the value is tied to the SID so two different services never get a
 * key close enough to look like a derived one (TASK 2.2's NEAR would otherwise
 * fire on this harness's own traffic, which would make two tests lie).
 */
static void make_cw(uint8_t cw[16], uint16_t sid)
{
	int i, q;
	for (i = 0; i < 16; i++) cw[i] = (uint8_t)(0x30 + ((sid + i * 13) & 0x3F));
	for (q = 0; q < 4; q++)
		cw[q * 4 + 3] = (uint8_t)(cw[q * 4] + cw[q * 4 + 1] + cw[q * 4 + 2]);
}

/* ---- the delay script ------------------------------------------------------ */

static int delays[256];
static int ndelays = 0;

static void load_delays(void)
{
	const char *s = getenv("NS_DELAYS");
	if (!s || !*s) { delays[0] = 60; ndelays = 1; return; }
	while (*s && ndelays < 256) {
		delays[ndelays++] = atoi(s);
		while (*s && *s != ',') s++;
		if (*s == ',') s++;
	}
	if (!ndelays) { delays[0] = 60; ndelays = 1; }
}

static int delay_for(unsigned long reply_n)
{
	if (!ndelays) return 0;
	return delays[reply_n < (unsigned long)ndelays ? (int)reply_n : ndelays - 1];
}

/* ---- main ------------------------------------------------------------------ */

int main(int argc, char **argv)
{
	int lfd, fd, one = 1;
	struct sockaddr_in a;
	unsigned char key[14];
	unsigned char keymod[14];
	unsigned char sessionkey[16];
	unsigned char buf[CWS_NETMSGSIZE];
	char user[64], pass[64];
	unsigned long replies = 0;
	int port, caid, provid;
	long t0, t1;

	port = (argc > 1) ? atoi(argv[1]) : 17000;
	caid = (argc > 3) ? (int)strtol(argv[3], NULL, 16) : 0x1884;
	provid = (argc > 4) ? (int)strtol(argv[4], NULL, 16) : 0x000000;
	memset(key, 0, sizeof(key));
	if (argc > 2 && strlen(argv[2]) >= 28) {
		int i;
		for (i = 0; i < 14; i++)
			sscanf(argv[2] + i * 2, "%2hhx", &key[i]);
	}
	if (getenv("NS_VERBOSE")) verbose = atoi(getenv("NS_VERBOSE"));
	load_delays();
	srand(4321);   /* reproducible keymods: a failing run can be replayed */

	printf("NCSERVER: listening on %d, caid %04X provid %06X, %d delay step(s): ",
	       port, caid, provid, ndelays);
	{ int i; for (i = 0; i < ndelays; i++) printf("%d%s", delays[i], (i + 1 < ndelays) ? "," : ""); }
	printf(" ms\n");
	fflush(stdout);

	lfd = socket(AF_INET, SOCK_STREAM, 0);
	if (lfd < 0) { perror("socket"); return 2; }
	setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	memset(&a, 0, sizeof(a));
	a.sin_family = AF_INET;
	a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	a.sin_port = htons((uint16_t)port);
	if (bind(lfd, (struct sockaddr *)&a, sizeof(a)) < 0) { perror("bind"); return 2; }
	if (listen(lfd, 4) < 0) { perror("listen"); return 2; }

	for (;;) {
		fd = accept(lfd, NULL, NULL);
		if (fd < 0) { if (errno == EINTR) continue; perror("accept"); return 2; }
		setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
		printf("NCSERVER: client connected -- a newcamd server connection\n");
		fflush(stdout);

		/* 1. keymod */
		make_keymod(keymod);
		if (!send_nonb(fd, keymod, 14, 500)) { close(fd); continue; }
		des_login_key_get(keymod, key, 14, sessionkey);

		/* 2. login */
		{
			int len = cs_message_receive(fd, NULL, buf, sessionkey, 10000);
			char *p;
			if (len < 3 || buf[0] != MSG_CLIENT_2_SERVER_LOGIN) {
				printf("NCSERVER: login not received (len=%d type=0x%02X)\n", len, len > 0 ? buf[0] : 0);
				fflush(stdout);
				close(fd); continue;
			}
			strncpy(user, (char *)&buf[3], sizeof(user) - 1);
			user[sizeof(user) - 1] = 0;
			p = (char *)&buf[3] + strlen((char *)&buf[3]) + 1;
			strncpy(pass, p, sizeof(pass) - 1);
			pass[sizeof(pass) - 1] = 0;
			printf("NCSERVER: login from user '%s'\n", user);
			fflush(stdout);
			/*
			 * THE ORDER HERE IS THE WHOLE TRICK, and getting it wrong cost a
			 * debugging session: srv-newcamd.c:181-190 sends the LOGIN_ACK
			 * encrypted with the KEYMOD-derived key and re-derives the session
			 * key from the md5-crypt password only AFTERWARDS. Doing it the
			 * other way round produced an ACK the client could not decrypt, and
			 * the client simply closed the connection -- which looks exactly
			 * like a server that crashed, and looks nothing like a key.
			 */
			{
				unsigned char ack[3];
				ack[0] = MSG_CLIENT_2_SERVER_LOGIN_ACK; ack[1] = 0; ack[2] = 0;
				if (!cs_message_send(fd, NULL, ack, 3, sessionkey)) { close(fd); continue; }
			}
			/* cli-newcamd.c:98 -- and from here on, every message uses this. */
			des_login_key_get(key, (uint8_t *)pass, (int)strlen(pass), sessionkey);
		}

		/* 4/5. the message loop */
		for (;;) {
			struct cs_custom_data cd;
			int len;

			memset(&cd, 0, sizeof(cd));
			len = cs_message_receive(fd, &cd, buf, sessionkey, 60000);
			if (len <= 0) {
				printf("NCSERVER: client gone (len=%d) after %lu replies\n", len, replies);
				fflush(stdout);
				close(fd);
				break;
			}

			if (buf[0] == MSG_CARD_DATA_REQ) {
				unsigned char out[15 + 11];  /* one provider is enough */
				memset(out, 0, sizeof(out));
				out[0] = MSG_CARD_DATA;
				out[4] = (unsigned char)(caid >> 8);
				out[5] = (unsigned char)caid;
				out[14] = 1;
				out[15] = (unsigned char)(provid >> 16);
				out[16] = (unsigned char)(provid >> 8);
				out[17] = (unsigned char)provid;
				cs_message_send(fd, &cd, out, 15 + 11, sessionkey);
				printf("NCSERVER: advertised card %04X%06X -- MultiCS may now use this slot\n",
				       caid, provid);
				fflush(stdout);
				continue;
			}

			if (buf[0] != 0x80 && buf[0] != 0x81) {
				if (verbose)
					printf("NCSERVER: ignored message type 0x%02X (len %d)\n", buf[0], len);
				continue;
			}

			/* An ECM. This is the reply the whole target measures. */
			{
				unsigned char out[19];
				uint8_t cw[16];
				uint16_t sid = cd.sid;  /* the service the client asked for */
				int d = delay_for(replies);

				/*
				 * The key is tied to the SID so that two services never get
				 * keys close enough to look like a derived pair: TASK 2.2's
				 * NEAR would otherwise fire on this harness's own traffic and
				 * two targets would then be testing each other by accident.
				 */
				make_cw(cw, sid);

				t0 = ms_now();
				if (d > 0) usleep((useconds_t)d * 1000);
				t1 = ms_now();

				memset(out, 0, sizeof(out));
				out[0] = 0x81;
				out[1] = 0;
				out[2] = 0x10;
				memcpy(out + 3, cw, 16);
				if (!cs_message_send(fd, &cd, out, 19, sessionkey)) {
					printf("NCSERVER: send failed\n");
					close(fd);
					break;
				}
				replies++;
				if (verbose)
					printf("NCSERVER: reply %lu ch %04x:%06x:%04x delayed %d ms (measured %ld ms)\n",
					       replies, cd.caid, cd.provid, sid, d, t1 - t0);
				fflush(stdout);
			}
		}
	}

	return 0;
}
