/*
 * ncclient.c — a real Newcamd client for MultiCS, built from MultiCS's own code.
 *
 * WHY THIS EXISTS.
 * Runtime proof of TASK 1.2, 1.3, 1.5 and 1.6 was blocked on one thing: every
 * protocol that carries a control word is encrypted, so nothing could talk to the
 * server. Rather than reimplement the crypto — and risk a client whose bytes
 * differ subtly from the server's, which would prove nothing — this links the
 * server's OWN des.c, msg-newcamd.c and md5.c. The framing and the cipher are
 * therefore identical by construction, not by careful copying.
 *
 * WHAT IT DOES, IN STAGES, so a failure points at the stage that broke:
 *   1. connect and read the 14-byte keymod
 *   2. verify the 'MCS' marker MultiCS embeds in it
 *   3. derive the login key exactly as cli-newcamd.c:41 does
 *   4. send MSG_CLIENT_2_SERVER_LOGIN with user + md5-crypt password
 *   5. report whether LOGIN_ACK came back
 *
 * send_nonb/recv_nonb/debugf are stubbed rather than linked from sockets.c and
 * debug.c, because those drag in the server's globals; the stubs are trivial and
 * their signatures are taken from sockets.h:20-21.
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

#include "../src/des.h"
#include "../src/md5.h"
#include "../src/msg-newcamd.h"

/* ---- stubs for what msg-newcamd.c calls ------------------------------------ */

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

static long ms_now(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}
static long t_start = 0;

int recv_nonb(int sock, uint8_t *buf, int len, int timeout)
{
	struct pollfd pfd;
	int got = 0, n;

	while (got < len) {
		pfd.fd = sock; pfd.events = POLLIN; pfd.revents = 0;
		n = poll(&pfd, 1, timeout);
		if (verbose)
			fprintf(stderr, "    [+%4ldms] recv_nonb(want %d, got %d): poll=%d revents=0x%X\n",
			        ms_now() - t_start, len, got, n, (unsigned)pfd.revents);
		if (n <= 0) return got;
		n = (int)recv(sock, buf + got, (size_t)(len - got), 0);
		if (verbose)
			fprintf(stderr, "    [+%4ldms]   recv -> %d (errno=%d %s)\n",
			        ms_now() - t_start, n, errno, n < 0 ? strerror(errno) : "-");
		if (n <= 0) { if (n < 0 && errno == EINTR) continue; return got; }
		got += n;
	}
	return got;
}

void debugf(int dbg, char *fmt, ...)
{
	va_list ap;
	(void)dbg;
	if (!verbose) return;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
}
void debughex(uint8_t *buf, int len) { int i; (void)buf; (void)len; if (!verbose) return; for (i=0;i<len;i++) fprintf(stderr,"%02X ",buf[i]); fprintf(stderr,"\n"); }
int  getdbgflag(int a, int b, int c) { (void)a;(void)b;(void)c; return 0; }
int  getdbgflagpro(int a, int b, int c, int d) { (void)a;(void)b;(void)c;(void)d; return 0; }
int  flag_debugnet = 0;

/*
 * Two switches stay in the shipped client. They cost nothing, and they are
 * what split the harness in two when a fault had to be localised to one side
 * of the login boundary or the other:
 *
 *   NC_NO_ECM=1        stop after LOGIN_ACK. The connection stays open, which
 *                      proves the login and the post-login session key are
 *                      correct independently of anything about the ECM.
 *   NC_KEEPALIVE=1     send a 3-byte MSG_KEEPALIVE instead of an ECM. The
 *                      server accepts that and does not run its ECM path, so
 *                      it separates "the post-login read path works" from
 *                      "the ECM itself is processed".
 *
 * The fault they isolated (a session key derived from the wrong argument to
 * des_login_key_get) is fixed; the switch that documents the correct call is
 * kept inline at stage 6.
 */

/* --------------------------------------------------------------------------- */

static int tcp_connect(const char *host, int port)
{
	struct sockaddr_in sa;
	struct hostent *he;
	int fd, one = 1;

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) { perror("socket"); return -1; }

	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_port = htons((uint16_t)port);
	he = gethostbyname(host);
	if (!he) { fprintf(stderr, "cannot resolve %s\n", host); close(fd); return -1; }
	memcpy(&sa.sin_addr, he->h_addr, (size_t)he->h_length);

	if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
		fprintf(stderr, "connect %s:%d failed: %s\n", host, port, strerror(errno));
		close(fd);
		return -1;
	}
	setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
	return fd;
}

int main(int argc, char **argv)
{
	const char *host = "127.0.0.1";
	int   port = 15501;
	const char *user = "u1", *pass = "p1", *keyhex = "0102030405060708091011121314";
	uint8_t key[14];
	int caid = 0x1884, sid = 0x0064;
	uint8_t keymod[14], sessionkey[16], buf[512], passwdcrypt[64];
	struct cs_custom_data cd;
	int fd, len, index, stage = 0;

	if (argc > 1) host = argv[1];
	if (argc > 2) port = atoi(argv[2]);
	if (argc > 3) user = argv[3];
	if (argc > 4) pass = argv[4];
	if (argc > 5) keyhex = argv[5];
	if (argc > 6) verbose = atoi(argv[6]);
	if (argc > 7) caid = (int)strtol(argv[7], NULL, 16);
	if (argc > 8) sid  = (int)strtol(argv[8], NULL, 16);

	printf("ncclient: MultiCS Newcamd client harness\n");
	t_start = ms_now();
	{
		int i;
		if (strlen(keyhex) != 28) {
			fprintf(stderr, "key must be 28 hex characters (14 bytes), got %d\n",
			        (int)strlen(keyhex));
			return 1;
		}
		for (i = 0; i < 14; i++) {
			char two[3]; unsigned v;
			two[0] = keyhex[i*2]; two[1] = keyhex[i*2+1]; two[2] = 0;
			if (sscanf(two, "%x", &v) != 1) { fprintf(stderr, "bad hex in key\n"); return 1; }
			key[i] = (uint8_t)v;
		}
	}

	/* ---- stage 1: connect, read the keymod ---- */
	fd = tcp_connect(host, port);
	if (fd < 0) { printf("  [FAIL] stage 1: could not connect\n"); return 1; }

	if (recv_nonb(fd, keymod, 14, 5000) != 14) {
		printf("  [FAIL] stage 1: server did not send the 14-byte init sequence\n");
		close(fd); return 1;
	}
	printf("  [ ok ] stage 1: connected and read the 14-byte keymod\n");
	printf("         keymod = ");
	{ int i; for (i = 0; i < 14; i++) printf("%02X", keymod[i]); printf("\n"); }
	stage = 1;

	/* ---- stage 2: the MultiCS marker ----
	 * cli-newcamd.c:33-39 -- three checksums the server embeds so a client can
	 * tell MultiCS from other Newcamd servers. If this fails the server is not
	 * the one we think we are talking to.
	 */
	{
		uint8_t a = (uint8_t)((keymod[0] ^ 'M') + keymod[1] + keymod[2]);
		uint8_t b = (uint8_t)(keymod[4] + (keymod[5] ^ 'C') + keymod[6]);
		uint8_t c = (uint8_t)(keymod[8] + keymod[9] + (keymod[10] ^ 'S'));
		if (a == keymod[3] && b == keymod[7] && c == keymod[11])
			printf("  [ ok ] stage 2: the 'MCS' marker verifies -- this is MultiCS\n");
		else {
			printf("  [FAIL] stage 2: 'MCS' marker mismatch (%02X/%02X/%02X vs %02X/%02X/%02X)\n",
			       a, b, c, keymod[3], keymod[7], keymod[11]);
			close(fd); return 1;
		}
	}
	stage = 2;

	/* ---- stage 3: the login key, exactly as cli-newcamd.c:41 ---- */
	des_login_key_get(keymod, key, 14, sessionkey);
	printf("  [ ok ] stage 3: login key derived\n");
	printf("         sessionkey = ");
	{ int i; for (i = 0; i < 16; i++) printf("%02X", sessionkey[i]); printf("\n"); }
	stage = 3;

	/* ---- stage 4: send the login ----
	 * Layout from cli-newcamd.c:51-60: type, two zero bytes, user NUL,
	 * md5-crypt password NUL. provid 0x0057484F is the "WHO" marker the real
	 * client sets when it recognised MultiCS.
	 */
	memset(&cd, 0, sizeof(cd));
	cd.sid = 0x4343;
	cd.provid = 0x0057484F;

	memset(buf, 0, sizeof(buf));
	index = 3;
	buf[0] = MSG_CLIENT_2_SERVER_LOGIN;
	/* TASK 3.3 -- clamp the name to the login frame: the server refuses
	 * >63 anyway; the harness must not overflow its own buf first. */
	{ size_t ul = strlen(user); if (ul > 63) ul = 63;
	  memcpy((char *)&buf[3], user, ul); buf[3+ul] = 0;
	  index += (int)ul + 1; }
	__md5_crypt(pass, "$1$abcdefgh$", (char *)passwdcrypt);
	strcpy((char *)buf + index, (char *)passwdcrypt);
	index += (int)strlen((char *)passwdcrypt) + 1;

	if (!cs_message_send(fd, &cd, buf, index, sessionkey)) {
		printf("  [FAIL] stage 4: cs_message_send refused the login\n");
		close(fd); return 1;
	}
	printf("  [ ok ] stage 4: login sent (%d bytes, md5-crypt = %s)\n", index, passwdcrypt);
	stage = 4;

	/* ---- stage 5: the answer ---- */
	memset(&cd, 0, sizeof(cd));
	len = cs_message_receive(fd, &cd, buf, sessionkey, 8000);
	if (len < 3) {
		printf("  [FAIL] stage 5: no usable login answer (len=%d)\n", len);
		close(fd); return 1;
	}
	if (buf[0] == MSG_CLIENT_2_SERVER_LOGIN_ACK) {
		printf("  [ ok ] stage 5: LOGIN_ACK -- the server accepted the client\n");
		stage = 5;
	}
	else if (buf[0] == MSG_CLIENT_2_SERVER_LOGIN_NAK) {
		printf("  [FAIL] stage 5: LOGIN_NAK -- credentials rejected (len=%d)\n", len);
		close(fd); return 1;
	}
	else {
		printf("  [FAIL] stage 5: unexpected reply type 0x%02X (len=%d)\n", buf[0], len);
		close(fd); return 1;
	}

	/* ---- stage 6: send an ECM request ----
	 * The rules come from cs_accept_ecm() (main.c:474-497):
	 *   - ecm[0] & 0xFE must be 0x80                      ("Invalid ECM tag")
	 *   - 20 <= ecmlen <= MAX_ECM_SIZE                    ("Invalid ECM length")
	 *   - ((ecm[1]&0x0F)<<8)|ecm[2] == ecmlen-3           ("ECM length corrupted")
	 *   - the caid must belong to the profile             ("Wrong caid")
	 * sid/caid/provid travel in the 12-byte header, not in the ECM body.
	 *
	 * The session key is re-derived first, exactly as cli-newcamd.c:98 does:
	 * after LOGIN_ACK both sides switch from the keymod-derived key to one
	 * derived from the md5-crypt password. Sending with the old key is the
	 * classic way to make a client that logs in and then goes silent.
	 */
	if (getenv("NC_NO_ECM")) { printf("  [ -- ] stage 6 skipped (NC_NO_ECM)\n"); }
	else
	{
		uint8_t ecm[64];
		struct cs_custom_data ecd;
		int ecmlen = 64, i;
		int rep, nreps, gapms, altsid, altsid2, cursid, nc_fill;

		/*
		 * Post-login re-derivation, exactly as cli-newcamd.c:98:
		 *   des_login_key_get( srv->key, passwdcrypt, strlen(passwdcrypt), sessionkey)
		 * and the server side at srv-newcamd.c:190:
		 *   des_login_key_get( cs->newcamd.key, passwdcrypt, strlen(passwdcrypt), sessionkey)
		 *
		 * THE FIRST ARGUMENT IS THE DES KEY, NOT THE PASSWORD. This was got
		 * wrong here and cost several turns: passing the password produces a
		 * key the server cannot match, `des_decrypt` fails its checksum,
		 * `cs_peekmsg` returns 0 through its `if (len < 15)` test, and the
		 * server disconnects with a bare "read failed 0" that looks exactly
		 * like the client had closed the socket. It was only proved by a
		 * temporary print of the server's own key inside cs_peekmsg.
		 */
		des_login_key_get(key, (uint8_t *)passwdcrypt,
		                  (int)strlen((char *)passwdcrypt), sessionkey);
		printf("         post-login key: des_login_key_get(deskey, passwdcrypt (len %d))\n",
		       (int)strlen((char *)passwdcrypt));
		printf("         post-login sessionkey = ");
		{ int q; for (q = 0; q < 16; q++) printf("%02X", sessionkey[q]); printf("\n"); }

		memset(ecm, 0, sizeof(ecm));
		ecm[0] = 0x80;
		ecm[1] = (uint8_t)(((ecmlen - 3) >> 8) & 0x0F);
		ecm[2] = (uint8_t)((ecmlen - 3) & 0xFF);
		/*
		 * The body must depend on the service. ecmd5 is MD5 over ecm[3..],
		 * i.e. the body only (ecmdata.c:262-264), so a body that ignores the
		 * sid gives every service the same ecmd5 -- and cwreuse_offer()
		 * treats an identical ecmd5 as "the same ECM re-sent", never as
		 * reuse. Two services would then be undetectable as two services.
		 * Real ECMs differ per service; this makes the harness match.
		 */
		/*
		 * TASK 1.7: NC_FILL adds a constant to every body byte. It changes the
		 * ECM hash -- and so the cache entry the server looks up -- while leaving
		 * sid/caid/provid alone, which lets one harness ask the SAME service
		 * under two different ECM identities. That matters because trust is keyed
		 * per service (GR4) while cache entries are keyed per hash: to observe a
		 * deferral you must distrust a peer on a service without having already
		 * consumed the cache entry you want to see deferred.
		 */
		nc_fill = 0;
		{ const char *e = getenv("NC_FILL"); if (e) nc_fill = (int)strtol(e, NULL, 0); }
		for (i = 3; i < ecmlen; i++) ecm[i] = (uint8_t)(i * 7 + 1 + (sid & 0xFF) + nc_fill);

		memset(&ecd, 0, sizeof(ecd));
		ecd.msgid = 1;
		ecd.sid   = (uint16_t)sid;
		ecd.caid  = (uint16_t)caid;
		ecd.provid = 0;

		/*
		 * NC_ECMS repeats the send/receive pair. The ECM bytes are identical
		 * every time, which is what a real client does when it fails to
		 * decode: it re-sends the ECM of the crypto period it is still in.
		 *
		 * Rep 1 is the case that used to crash the server. search_ecmdata_any()
		 * finds nothing for a profile's first ECM, so `ecm` is NULL there and
		 * the request counts as new; rep 2 finds that same ECM still in the
		 * table and takes upstream's existing-ECM branch instead. Both
		 * branches have to run before this harness can claim the ECM path
		 * works, so the default is 2.
		 */
	nreps = 2;
	/* Default stays 2. The cap is only raised so the soak can storm on
	 * one connection; a value above 8 used to be ignored. */
	{ const char *e = getenv("NC_ECMS"); if (e) { int v = atoi(e); if (v > 0 && v <= 50000) nreps = v; } }
		/*
		 * NC_ALT_SID makes rep 2 ask a DIFFERENT service on the SAME
		 * connection. Two connections cannot be used for this: a second
		 * login to the same user slot is dropped by the server about 3 ms
		 * after LOGIN_ACK, before the ECM is ever read -- upstream
		 * connection-reuse behaviour, not something this harness controls.
		 * One connection asking two services is also the more realistic
		 * zap, and it is what CW-reuse detection has to catch.
		 */
		altsid = 0;
		{ const char *e = getenv("NC_ALT_SID"); if (e) altsid = (int)strtol(e, NULL, 16); }
		/*
		 * NC_ALT_SID2 makes rep 3 ask a THIRD service on the same connection
		 * (TASK 2.6: the negative-memory target needs a request that arrives
		 * AFTER the proof, so the same poison can be offered once more).
		 */
		altsid2 = 0;
		{ const char *e = getenv("NC_ALT_SID2"); if (e) altsid2 = (int)strtol(e, NULL, 16); }
		gapms = 2000;
		{ const char *e = getenv("NC_ECM_GAP_MS"); if (e) { int v = atoi(e); if (v >= 0) gapms = v; } }

		for (rep = 0; rep < nreps; rep++)
		{
			int r;
			if (rep) usleep((unsigned)(gapms * 1000));
			ecd.msgid = (uint16_t)(rep + 1);
			/* NC_HOP=1 asks a different service on every rep of this
			 * connection. Used by the soak. Unset, the old alt-sid
			 * rules are unchanged. */
			if (getenv("NC_HOP")) {
				cursid = (sid + rep * 17) & 0xFFFF;
				if (cursid == 0) cursid = 1;
			} else
				cursid = (rep == 2 && altsid2) ? altsid2 : ((rep && altsid) ? altsid : sid);
			ecd.sid  = (uint16_t)cursid;
			/*
			 * Refill the body for this service. ecmd5 and the 32-bit hash are both
			 * computed over ecm[3..] only, so changing ecd.sid alone leaves the two
			 * requests looking like the same ECM re-sent -- which cwreuse_offer()
			 * correctly refuses to call reuse. Filling the body outside the loop made
			 * every service identical and the detector never fired.
			 */
			for (i = 3; i < ecmlen; i++) ecm[i] = (uint8_t)(i * 7 + 1 + (cursid & 0xFF) + nc_fill);
			if (getenv("NC_KEEPALIVE")) {
				unsigned char ka[8];
				struct cs_custom_data kd;
				ka[0] = MSG_KEEPALIVE; ka[1] = 0; ka[2] = 0;
				memset(&kd, 0, sizeof(kd));
				r = cs_message_send(fd, &kd, ka, 3, sessionkey);
				printf("         NC_KEEPALIVE: sent a 3-byte keepalive (0x%02X) instead of an ECM, r=%d\n",
				       (unsigned)MSG_KEEPALIVE, r);
			}
			else r = cs_message_send(fd, &ecd, ecm, ecmlen, sessionkey);
			if (!rep) {
				struct pollfd q; int qe;
				printf("         cs_message_send returned %d, socket fd=%d\n", r, fd);
				q.fd = fd; q.events = POLLIN; q.revents = 0;
				qe = poll(&q, 1, 300);
				printf("         poll(300) right after send -> %d, revents=0x%X (0x10=HUP 0x8=POLLERR)\n",
				       qe, (unsigned)q.revents);
			}
			if (!r) {
				printf("  [FAIL] stage 6: cs_message_send refused the ECM\n");
				close(fd); return 1;
			}
			printf("  [ ok ] stage 6: ECM %d/%d sent (%d bytes, ch %04X:%06X:%04X)\n",
			       rep+1, nreps, ecmlen, (unsigned)caid, 0u, (unsigned)cursid);
			stage = 6;

			/* Whatever comes back is informative. With no card server configured
			 * the profile has nothing to decode with, so a decode-failed reply is
			 * the CORRECT answer and proves the ECM was accepted and processed. */
			memset(&ecd, 0, sizeof(ecd));

			len = cs_message_receive(fd, &ecd, buf, sessionkey, 8000);
			if (len < 3) {
				printf("  [ ?? ] stage 6: no reply (len=%d, errno=%d %s)\n", len, errno, strerror(errno));
			}
			else if (buf[0] == 0x80 || buf[0] == 0x81) {
				int allzero = 1, k;
				printf("         reply type 0x%02X, len=%d, dcw=", buf[0], len);
				for (k = 3; k < len && k < 19; k++) {
					printf("%02X", buf[k]);
					if (buf[k]) allzero = 0;
				}
				printf("\n");
				if (allzero)
					printf("  [ ok ] stage 6: decode-failed (all-zero) -- expected with no card server\n");
				else {
					printf("  [ ok ] stage 6: THE SERVER DELIVERED A CONTROL WORD\n");
					stage = 7;
				}
			}
		else {
			printf("         unexpected reply type 0x%02X (len=%d)\n", buf[0], len);
		}
		/*
		 * NC_LINGER_MS stays on the socket after the reply and reads
		 * one more message. TASK 3.16 uses it to catch a key that
		 * arrives after decode-failed. Unset, the client exits as before.
		 */
		{
			const char *lg = getenv("NC_LINGER_MS");
			int w = lg ? atoi(lg) : 0;
			if (w > 0 && w <= 5000) {
				unsigned char lbuf[512];
				struct cs_custom_data lcd;
				int llen, allzero = 1, k;
				memset(&lcd, 0, sizeof(lcd));
				llen = cs_message_receive(fd, &lcd, lbuf, sessionkey, w);
				if (llen >= 3 && (lbuf[0] == 0x80 || lbuf[0] == 0x81)) {
					for (k = 3; k < llen && k < 19; k++) if (lbuf[k]) allzero = 0;
					if (allzero)
						printf("  [ ok ] linger: no control word\n");
					else
						printf("  [FAIL] linger: A LATE CONTROL WORD ARRIVED\n");
				} else {
					printf("  [ ok ] linger: no further reply (len=%d)\n", llen);
				}
			}
		}
		} /* end for (rep) */
	}

	close(fd);
	printf("== %d stages passed ==\n", stage);
	return 0;
}
