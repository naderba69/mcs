/*
 * cachepeer.c -- a minimal CSP cache peer, for runtime-testing the CW path.
 *
 * WHY THIS EXISTS. Every protocol that carries a control word in MultiCS is
 * either encrypted or needs a real card. Without a CW ever being delivered,
 * `cli->lastecm.status` stays 0, and every branch of TASK 1.2 (retry
 * classification), 1.4 (trust) and 1.6 (rotation) is gated on it -- so those
 * code paths were reachable in unit tests but never at runtime. This peer is
 * what delivers the first CW, and it does it through the real
 * `cache_recvmsg()` path rather than by poking at internals.
 *
 * It is deliberately dumb. It speaks just enough of the CSP UDP protocol
 * (clustredcache.c:1549) to be accepted as a peer and to answer a request:
 *
 *   1. Answer the SERVER's ping. MultiCS initiates: cache_send_ping()
 *                            (clustredcache.c:1041) sends TYPE_PINGREQ with
 *                            buf[1..2]="MC", and the first one goes out
 *                            immediately because `!peer->lastpingsent`
 *                            (:2307). The reply must be TYPE_HELLO_ACK
 *                            carrying the peer id the server put in
 *                            buf[4..5]. That handler is what sets
 *                            `peer->recvport` AND takes `peer->ping` above 0
 *                            -- and `cache_send_request()` skips any peer
 *                            whose ping is 0, so nothing is ever asked of a
 *                            peer that has not answered a ping.
 *
 *                            Do NOT ping first. Receiving a TYPE_PINGREQ sets
 *                            `peer->ping = 0` (:1848), so a peer that opens
 *                            the exchange itself actively suppresses the
 *                            state it needs. This harness tried that first and
 *                            the server received the ECM, asked nobody, and
 *                            reported decode-failed -- which looks exactly
 *                            like "the CW path is broken".
 *
 *                            The CRC at buf[6..8] is deliberately wrong. It
 *                            only decides `peer->ismultics` (:1891); recvport
 *                            and the ping accounting happen either way, and a
 *                            third-party CSP peer is the more honest model.
 *   2. TYPE_CARD_LIST (0x13) buf[1]=1 resets, then 4-byte (caid<<16)|provid
 *                            entries. `peer->nbcards` stays 0 otherwise, and
 *                            peer_card_binarysearch() returns 0 for nbcards==0
 *                            (:841) -- so the server would never even ask.
 *   3. TYPE_REPLY     (2)    echoes tag/sid/onid/caid/hash straight back out
 *                            of the TYPE_REQUEST it received, plus 16 CW
 *                            bytes at buf[13..28]. Length must be >= 29.
 *
 * The identifiers are echoed, not guessed: the server sends the request first,
 * so the reply is guaranteed to correlate with the ECM the client actually
 * asked about. That is what makes this a test of the delivery path and not a
 * test of whether I can compute MultiCS's ECM hash by hand.
 *
 * Build/run: see the `cachecw` target in the Makefile.
 *   argv[1] server cache port   argv[2] this peer's port
 *   argv[3] 32 hex chars of CW  argv[4] replies to send before exiting
 *
 * One line is printed per event so the Makefile can assert on them.
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
#define TYPE_PINGRPL   4
#define TYPE_CARD_LIST 0x13
#define TYPE_HELLO_ACK 0x10

static int hex2nib(int c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

/* 32 hex chars -> 16 bytes. Returns 0 on success. */
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

/* Mirrors checksumDCW() in dcw.c. Kept here rather than linked, because
 * linking dcw.c would drag in the server's config globals. */
/*
 * TASK 2.2 -- how many bits differ between two keys.
 *
 * The peer prints this rather than making the test budget for it: the whole
 * point of the entropy/collision target is that the server measures the same
 * distance the harness intended, and a number repeated on both sides from one
 * source of truth is the only way to see a disagreement between them.
 */
static int cw_popcount64(uint64_t v)
{
	v = v - ((v >> 1) & 0x5555555555555555ULL);
	v = (v & 0x3333333333333333ULL) + ((v >> 2) & 0x3333333333333333ULL);
	v = (v + (v >> 4)) & 0x0F0F0F0F0F0F0F0FULL;
	return (int)((v * 0x0101010101010101ULL) >> 56);
}

static int cw_bitdiff(const uint8_t a[16], const uint8_t b[16])
{
	int i, n = 0;
	for (i = 0; i < 16; i++) n += cw_popcount64((uint64_t)(uint8_t)(a[i] ^ b[i]));
	return n;
}

static int cw_checksum_ok(const uint8_t d[16])
{
	return d[3]  == (uint8_t)((d[0]  + d[1]  + d[2] ) & 0xFF)
	    && d[7]  == (uint8_t)((d[4]  + d[5]  + d[6] ) & 0xFF)
	    && d[11] == (uint8_t)((d[8]  + d[9]  + d[10]) & 0xFF)
	    && d[15] == (uint8_t)((d[12] + d[13] + d[14]) & 0xFF);
}

/*
 * TASK 1.7 harness support: step to the next key of the same stream.
 *
 * Only cw[0] moves, and cw[3] is recomputed so the SUM checksum still holds --
 * a rotated CW that failed checksumDCW() would be dropped silently and the test
 * would read as "the policy did not fire". The other three quarters are left
 * alone, which also keeps both halves non-null so acceptDCW() still passes.
 */
static void cw_rotate(uint8_t d[16])
{
	d[0] = (uint8_t)(d[0] + 1);
	d[3] = (uint8_t)((d[0] + d[1] + d[2]) & 0xFF);
}

int main(int argc, char **argv)
{
	int srvport = 15901, myport = 15902, maxrep = 1;
	/* A CW that passes acceptDCW(). Two things have to be true and neither
	 * is obvious:
	 *
	 *  - Non-null in both halves. A CW with a null half is treated as an
	 *    NDS half-key and rejected outright for any caid whose high byte
	 *    is not 9 (clustredcache.c:1512).
	 *  - It must satisfy checksumDCW() (dcw.c), because
	 *    `dcw_filter_checksum` defaults to 1 (config.c:213). The rule is a
	 *    SUM, not the XOR-0xF0 that the r107 changelog describes:
	 *        cw[3]=(cw[0]+cw[1]+cw[2])&0xFF   cw[7]=(cw[4]+cw[5]+cw[6])&0xFF
	 *        cw[11]=(cw[8]+cw[9]+cw[10])&0xFF cw[15]=(cw[12]+cw[13]+cw[14])&0xFF
	 *    A CW that fails it is dropped inside cache_setdcw() before anything
	 *    is logged, which is indistinguishable from "the delivery path is
	 *    broken" -- hence the explicit check and warning below.
	 */
	uint8_t cw[16] = { 0x11,0x22,0x33,0x66, 0x44,0x55,0x66,0xFF,
	                   0x77,0x88,0x99,0x98, 0xAA,0xBB,0xCC,0x31 };
	unsigned char buf[512];
	struct sockaddr_in srv, me, from;
	socklen_t flen;
	int fd, i, sent = 0, pinged = 0, cards = 0;
	/*
	 * TASK 1.7: unsolicited re-push.
	 *
	 * Why the harness needs it. A reply to TYPE_REQUEST is delivered by
	 * cache_setdcw(), not by PIPE_CACHE_FIND, so a purely request-driven peer
	 * never exercises the four sites TRUSTED-CACHE-FIRST gates. In production
	 * the entry is already in the cache when our client zaps in, because
	 * somebody else's client asked for that channel first and the peer pushed
	 * the key to everyone. Re-pushing a remembered identity on a timer is what
	 * reproduces that, and rotating the key each time is what makes the stored
	 * entry servable again: a cw_cache_data node is skipped once DCW_SENT is
	 * set on it, so the same key twice would never reach the gate.
	 */
	int repush_ms = 0, repushed = 0, have_id = 0;
	int repush_same = 0;
	/*
	 * TASK 2.4: CP_FORGED_EVERY=N makes every Nth re-push carry a key that has
	 * been damaged the way the r107 notes describe a forger damaging one --
	 * the last byte XORed, so exactly one of the four group sums breaks and the
	 * key still looks like a key. Combined with CP_REPUSH_SAME=0 the pushed
	 * keys are also all different, so nothing downstream can dismiss them as a
	 * repeat of one key. This is the only way to feed a plausible forger's
	 * traffic into the server without a client to ask for it.
	 */
	int forged_every = 0;
	/*
	 * TASK 2.4: CP_FRESH_KEY=1 answers every ECM with a DIFFERENT key that is
	 * still structurally plausible.
	 *
	 * Why the harness needs this: a peer that answers several different ECMs
	 * with one key is, correctly, a CW-reuse proof to TASK 1.3, which purges and
	 * re-routes -- so the client's next request is never answered and a test
	 * that wanted N deliveries gets one. (That is not a bug: it was observed
	 * live at 23:58, four replies, one delivery, one reuse proof.) A forger
	 * worth defending against edits each key, so the harness must too.
	 *
	 * The key is built so that the plausibility layer's verdict is the ONLY one
	 * it can earn: three of the four group sums are recomputed to hold and the
	 * fourth is broken by XOR on the last byte -- the exact shape the r107 notes
	 * describe. How far each key is from the previous one is printed, because
	 * anything within 16 bit flips would trip TASK 2.2's collision detector
	 * instead, and the target asserts that it does not.
	 */
	int fresh_key = 0;
	/*
	 * CP_FORGED_FRESH_EVERY=N: only every Nth fresh key is damaged; the rest
	 * have all four group sums recomputed and are therefore perfectly valid and
	 * deliverable. That is how the restraint phase is built -- a source that
	 * damages one key in eleven is 9 % of its traffic, under the plausibility
	 * layer's 20 % line, and the ten good keys must still reach clients.
	 */
	int fresh_forge_every = 1;
	unsigned fresh_n = 0;
	uint8_t fresh_prev[16];
	int have_prev = 0;
	/*
	 * TASK 2.5: CP_CYCLE_MARK=1|2 -- declare the key's cycle half on the wire
	 * (30-byte replies). The point is to be able to declare the WRONG half:
	 * a key that is perfectly formed and still cannot open the picture.
	 */
	int cycle_mark = 0;
	/*
	 * TASK 2.7: CP_MIRROR=1 -- every fresh reply key is MIRROR-BUILT and
	 * REPAIRED: half 2 is the bitwise complement of half 1, and the two
	 * group sums the complement breaks are fixed afterwards. The result
	 * passes checksumDCW (4 of 4 groups), so it is DELIVERED -- the exact
	 * construction that walks past every structural layer, including 2.4,
	 * and the reason 2.7 exists.
	 */
	int mirror_mode = 0;
	/*
	 * TASK 2.9: CP_STRANGER=1 -- never answer pings and never advertise, so
	 * the server has no idea who we are; every packet we send is a packet
	 * from an unconfigured sender. CP_SHORT_REPLY=1 -- the FIRST reply is
	 * sent as 8 bytes: shorter than TYPE_REPLY's 13-byte minimum, the exact
	 * datagram the ingress gate must drop before any parse.
	 */
	int stranger = 0;
	int short_reply = 0;
	int short_done = 0;
	/*
	 * TASK 2.2 -- a peer that holds a DIFFERENT key for a different channel.
	 *
	 * A real cache peer's cache is per channel, not per peer: it answers the
	 * channel it holds a key for, and the key it holds for another channel is a
	 * different one. Modelling that is what makes the collision test possible
	 * without restarting the peer (a peer that disappears can be disabled by the
	 * server, which would turn a forensics test into a ping-timeout test): the
	 * first ECM answered fixes the identity, and every different ECM after it is
	 * answered from the alternate key.
	 */
	uint8_t alt_cw[16];
	int have_alt = 0;
	unsigned char first_hash[4];
	int have_first = 0;
	unsigned char lastid[11];
	uint32_t last_push = 0;
	/* The caid this peer claims to share. Must match the profile's CAID or
	 * peer_card_binarysearch() fails and the server never sends a request. */
	uint32_t caprov = 0x18840000u;

	if (argc > 1) srvport = atoi(argv[1]);
	if (argc > 2) myport  = atoi(argv[2]);
	if (argc > 3 && argv[3][0]) {
		if (parse_cw(argv[3], cw)) { printf("PEER: bad cw hex '%s'\n", argv[3]); return 2; }
	}
	if (argc > 4) maxrep = atoi(argv[4]);
	if (argc > 5) caprov = (uint32_t)strtoul(argv[5], NULL, 16);
	if (getenv("CP_REPUSH_MS")) repush_ms = atoi(getenv("CP_REPUSH_MS"));

	/*
	 * CP_REPUSH_SAME=1 keeps the configured CW across unsolicited pushes
	 * instead of rotating it.
	 *
	 * The rotation exists so a peer looks like a peer with a stream of fresh
	 * keys (TASK 1.7 needs that: a *different* key for the same service is how
	 * a source is distrusted in the first place). TASK 1.8 needs the opposite:
	 * the same key arriving again, which is what a real peer does when it is
	 * asked the same question twice, and the only shape in which repeat
	 * suppression can be observed from outside the process.
	 */
	if (getenv("CP_REPUSH_SAME")) repush_same = atoi(getenv("CP_REPUSH_SAME"));
	if (getenv("CP_FORGED_EVERY")) forged_every = atoi(getenv("CP_FORGED_EVERY"));
	if (getenv("CP_FRESH_KEY")) fresh_key = atoi(getenv("CP_FRESH_KEY"));
	if (getenv("CP_FORGED_FRESH_EVERY")) fresh_forge_every = atoi(getenv("CP_FORGED_FRESH_EVERY"));
	if (getenv("CP_CYCLE_MARK")) cycle_mark = atoi(getenv("CP_CYCLE_MARK"));
	if (getenv("CP_MIRROR")) mirror_mode = atoi(getenv("CP_MIRROR"));
	if (getenv("CP_STRANGER")) stranger = atoi(getenv("CP_STRANGER"));
	if (getenv("CP_SHORT_REPLY")) short_reply = atoi(getenv("CP_SHORT_REPLY"));
	if (getenv("CP_ALT_CW")) {
		if (parse_cw(getenv("CP_ALT_CW"), alt_cw)) {
			printf("PEER: bad alt cw hex '%s'\n", getenv("CP_ALT_CW"));
			return 2;
		}
		have_alt = 1;
		printf("PEER: alt cw=");
		for (i = 0; i < 16; i++) printf("%02X", alt_cw[i]);
		printf(" distance=%d bits from the primary", cw_bitdiff(cw, alt_cw));
		if (!cw_checksum_ok(alt_cw))
			printf(" (WARNING: fails checksumDCW and will be dropped silently)");
		printf("\n");
	}

	/*
	 * TASK 1.7: CP_PUSH_HASH / CP_PUSH_SID / CP_PUSH_CAID seed an identity to
	 * push for *before* this server has asked about it.
	 *
	 * Why that is the only way to reach a PIPE_CACHE_FIND serve. Both the FIND
	 * and the REQUEST handler set CACHE_FLAG_SENDPIPE on the cache entry, FIND
	 * always runs first, and from then on cache_setdcw() marks DCW_SENT on every
	 * CW that arrives for that entry. A node with DCW_SENT set is skipped by all
	 * four FIND scans, so once our own server has asked about a hash, no later
	 * push for it can ever be served out of the cache again. In production the
	 * entry arrives already populated, because *another* server's client asked
	 * for that channel first and the peer relayed the key. Seeding the identity
	 * reproduces exactly that, and the hash is taken from the server's own log
	 * rather than recomputed here, so the harness is not guessing at the
	 * server's hash function.
	 */
	if (getenv("CP_PUSH_HASH") && getenv("CP_PUSH_SID")) {
		const char *h = getenv("CP_PUSH_HASH");
		const char *sd = getenv("CP_PUSH_SID");
		const char *cd = getenv("CP_PUSH_CAID");
		const char *tg = getenv("CP_PUSH_TAG");
		unsigned long hv = strtoul(h, NULL, 16);
		unsigned long sv = strtoul(sd, NULL, 16);
		unsigned long cv = cd ? strtoul(cd, NULL, 16) : 0x1884;
		unsigned long tv = tg ? strtoul(tg, NULL, 16) : 0x80;
		memset(lastid, 0, sizeof(lastid));
		lastid[0]  = (unsigned char)tv;
		lastid[1]  = (unsigned char)(sv >> 8);  lastid[2]  = (unsigned char)sv;
		/* lastid[3..4] = onid, left 0 */
		lastid[5]  = (unsigned char)(cv >> 8);  lastid[6]  = (unsigned char)cv;
		lastid[7]  = (unsigned char)(hv >> 24); lastid[8]  = (unsigned char)(hv >> 16);
		lastid[9]  = (unsigned char)(hv >> 8);  lastid[10] = (unsigned char)hv;
		have_id = 1;
		printf("PEER: seeded push identity ch %04lX:%04lX hash %08lX tag %02lX\n", cv, sv, hv, tv);
	}

	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0) { perror("socket"); return 2; }

	memset(&me, 0, sizeof(me));
	me.sin_family = AF_INET;
	me.sin_port = htons((uint16_t)myport);
	me.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	/* Binding to the fixed port is not optional: the server identifies a
	 * peer by (ip, recvport), and recvport is whatever source port it saw
	 * the ping arrive on. */
	if (bind(fd, (struct sockaddr *)&me, sizeof(me)) < 0) { perror("bind"); return 2; }

	memset(&srv, 0, sizeof(srv));
	srv.sin_family = AF_INET;
	srv.sin_port = htons((uint16_t)srvport);
	srv.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	printf("PEER: bound 127.0.0.1:%d, target 127.0.0.1:%d, cw=", myport, srvport);
	for (i = 0; i < 16; i++) printf("%02X", cw[i]);
	printf("\n");
	if (!cw_checksum_ok(cw))
		printf("PEER: WARNING this CW fails checksumDCW and acceptDCW() will "
		       "drop it silently -- the server will report decode-failed and it "
		       "will look like the delivery path is broken\n");
	fflush(stdout);

	for (;;) {
		struct pollfd pfd;
		int r;

		/* CP_PINGFIRST=1 sends one ping of our own. It is a diagnostic,
		 * not the normal path: a reply proves the server's sendto works
		 * at all, which is a different question from whether its own
		 * ping reaches us. */
		if (!pinged && getenv("CP_PINGFIRST")) {
			memset(buf, 0, 13);
			buf[0] = TYPE_PINGREQ;
			buf[11] = (unsigned char)(myport >> 8);
			buf[12] = (unsigned char)(myport & 0xFF);
			sendto(fd, buf, 13, 0, (struct sockaddr *)&srv, sizeof(srv));
			printf("PEER: CP_PINGFIRST sent 13 bytes to 127.0.0.1:%d\n", srvport);
			fflush(stdout);
			pinged = 1;
		}

		pfd.fd = fd; pfd.events = POLLIN; pfd.revents = 0;
		r = poll(&pfd, 1, (repush_ms > 0 && repush_ms < 1000) ? repush_ms : 1000);
		if (r < 0) { if (errno == EINTR) continue; perror("poll"); return 2; }

		if (r == 0) {
			/*
			 * Monotonic-ish clock: the server is on the same host, so
			 * GetTickCount() and clock_gettime() agree closely enough for a
			 * test interval, and neither wraps inside a test run.
			 */
			struct timespec ts;
			uint32_t now;
			clock_gettime(CLOCK_MONOTONIC, &ts);
			now = (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);

			if (repush_ms > 0 && have_id
			    && (uint32_t)(now - last_push) >= (uint32_t)repush_ms) {
				unsigned char rep[29], sent_cw[16];
				int forged = 0;
				last_push = now;
				if (!repush_same) cw_rotate(cw);
				memcpy(sent_cw, cw, 16);
				if (forged_every > 0 && repushed > 0 && (repushed % forged_every) == 0) {
					sent_cw[15] ^= 0xF0;   /* breaks group 4 and nothing else */
					forged = 1;
				}
				memset(rep, 0, sizeof(rep));
				rep[0] = TYPE_REPLY;
				memcpy(rep + 1, lastid, 11);
				rep[12] = lastid[0];          /* integrity: buf[12]==buf[1] */
				memcpy(rep + 13, sent_cw, 16);
				/* TASK R4: an unsolicited repush carries the declared cycle
				 * mark too when the harness asked for one (30-byte wire),
				 * so the ladder rig can pump marked keys. Default stays the
				 * 29-byte stock shape. */
				if (cycle_mark) {
					unsigned char rep30[30];
					memcpy(rep30, rep, 29);
					rep30[29] = (unsigned char)cycle_mark;
					sendto(fd, rep30, 30, 0, (struct sockaddr *)&srv, sizeof(srv));
					repushed++;
					printf("PEER: unsolicited push #%d sid %04X hash %02X%02X%02X%02X cw %02X%02X mark %d\n",
					       repushed, (unsigned)((lastid[1] << 8) | lastid[2]),
					       lastid[7], lastid[8], lastid[9], lastid[10],
					       sent_cw[0], sent_cw[1], cycle_mark);
					fflush(stdout);
					continue;
				}
				sendto(fd, rep, 29, 0, (struct sockaddr *)&srv, sizeof(srv));
				repushed++;
				printf("PEER: unsolicited push #%d sid %04X hash %02X%02X%02X%02X cw %02X%02X%s\n",
				       repushed, (unsigned)((lastid[1] << 8) | lastid[2]),
				       lastid[7], lastid[8], lastid[9], lastid[10],
				       sent_cw[0], sent_cw[1],
				       forged ? " FORGED (group 4 of 4 broken)" : "");
				fflush(stdout);
				continue;
			}
			if (sent >= maxrep && cards && !repush_ms) break;
			continue;
		}

		flen = sizeof(from);
		memset(buf, 0, sizeof(buf));
		{
			ssize_t n = recvfrom(fd, buf, sizeof(buf), 0, (struct sockaddr *)&from, &flen);
			if (n < 2) continue;

			if (buf[0] == TYPE_PINGREQ && n >= 9) {
				if (stranger) {
					/* TASK 2.9: a stranger does not announce itself. */
					continue;
				}
				pinged = 0; /* a real server ping supersedes the diagnostic */
				unsigned char ack[16];
				memset(ack, 0, sizeof(ack));
				if (buf[1] == 'M' && buf[2] == 'C') {
					ack[0] = TYPE_HELLO_ACK;
					printf("PEER: got MultiCS ping (peer id %02X%02X)\n", buf[4], buf[5]);
				} else {
					ack[0] = TYPE_PINGRPL;
					printf("PEER: got plain CSP ping (peer id %02X%02X)\n", buf[4], buf[5]);
				}
				ack[4] = buf[4];                   /* echo the peer id back */
				ack[5] = buf[5];
				/* buf[6..8] left zero: a deliberately wrong CRC. */
				sendto(fd, ack, 16, 0, (struct sockaddr *)&srv, sizeof(srv));
				pinged = 1;
				printf("PEER: sent %s -- recvport and ping are now set server-side\n",
				       ack[0] == TYPE_HELLO_ACK ? "TYPE_HELLO_ACK" : "TYPE_PINGRPL");
				fflush(stdout);
				if (!cards) {
					unsigned char cl[6];
					memset(cl, 0, sizeof(cl));
					cl[0] = TYPE_CARD_LIST;
					cl[1] = 1;                       /* reset the list */
					cl[2] = (unsigned char)(caprov >> 24);
					cl[3] = (unsigned char)(caprov >> 16);
					cl[4] = (unsigned char)(caprov >> 8);
					cl[5] = (unsigned char)(caprov);
					sendto(fd, cl, 6, 0, (struct sockaddr *)&srv, sizeof(srv));
					cards = 1;
					printf("PEER: advertised card %08X\n", caprov);
					fflush(stdout);
				}
				continue;
			}

			if (buf[0] == TYPE_REQUEST && n >= 12) {
				/* TASK 2.8: the pt target counts these to prove the brake. */
				printf("PEER: got request sid %02X%02X hash %02X%02X%02X%02X\n",
				       buf[2], buf[3], buf[8], buf[9], buf[10], buf[11]);
				unsigned char rep[30];
				int replen = 29;
				memset(rep, 0, sizeof(rep));
				rep[0] = TYPE_REPLY;
				memcpy(rep + 1, buf + 1, 11);   /* tag,sid,onid,caid,hash */
				/*
				 * A seeded identity wins. Without this guard the first request
				 * we answer would overwrite it, and the entry we need to see
				 * deferred would never be pushed again.
				 */
				if (!getenv("CP_PUSH_HASH")) {
					memcpy(lastid, buf + 1, 11);    /* TASK 1.7: for re-pushes */
					have_id = 1;
				}
				{
					struct timespec ts0;
					clock_gettime(CLOCK_MONOTONIC, &ts0);
					last_push = (uint32_t)(ts0.tv_sec * 1000 + ts0.tv_nsec / 1000000);
				}
				rep[12] = buf[1];               /* integrity: buf[12]==buf[1] */
				/*
				 * TASK 2.2: which key does this peer hold for THIS channel?
				 * The first ECM answered defines it; a different ECM is served from
				 * the alternate key when one is configured.
				 */
				{
					uint8_t *use = cw;
					if (!have_first) {
						memcpy(first_hash, buf + 8, 4);
						have_first = 1;
					}
					else if (have_alt && memcmp(first_hash, buf + 8, 4)) {
						use = alt_cw;
						printf("PEER: serving the ALT key: different ecm (hash %02X%02X%02X%02X)\n",
						       buf[8], buf[9], buf[10], buf[11]);
					}
					if (fresh_key) {
						uint32_t x = 0x5EED1234u + (uint32_t)(++fresh_n) * 2654435761u;
						int bi;
						for (bi = 0; bi < 16; bi++) {
							x = x * 1103515245u + 12345u;
							use[bi] = (uint8_t)(x >> 16);
						}
						{
							/*
							 * N > 0: every Nth key is damaged. N <= 0 (or unset
							 * to 0): every key is repaired, which is how the
							 * honesty control gets four DIFFERENT valid keys --
							 * one key served to four ECMs is a CW-reuse proof
							 * to TASK 1.3, and the first version of this
							 * harness did exactly that and tripped it.
							 */
							int forge = (fresh_forge_every > 0) &&
							            ((fresh_n % (unsigned)fresh_forge_every) == 0);
							/* three group sums hold... */
							use[3]  = (uint8_t)(use[0] + use[1] + use[2]);
							use[7]  = (uint8_t)(use[4] + use[5] + use[6]);
							use[11] = (uint8_t)(use[8] + use[9] + use[10]);
							if (!forge)
								/* ...and the fourth is repaired: a valid, deliverable key */
								use[15] = (uint8_t)(use[12] + use[13] + use[14]);
							else
								/* ...or broken by one byte, as the r107 notes warn */
								use[15] ^= 0xF0;

						if (mirror_mode) {
							/*
							 * TASK 2.7 -- rebuild the key as a mirror: half 2 becomes
							 * the complement of half 1, then the two sums the
							 * complement breaks are repaired. The key keeps all four
							 * sums, so it is deliverable -- and it carries 6/6
							 * complement pairs for 2.7 to see.
							 */
							int mi;
							for (mi = 0; mi < 8; mi++)
								use[8 + mi] = (uint8_t)~use[mi];
							use[11] = (uint8_t)(use[8] + use[9] + use[10]);
							use[15] = (uint8_t)(use[12] + use[13] + use[14]);
						}
							if (have_prev)
								printf("PEER: fresh key #%u: %d bit(s) from the previous one, groups %s, group 4 %s\n",
								       fresh_n, cw_bitdiff(fresh_prev, use),
							       mirror_mode ? "1-4 intact (mirror-built)" : "1-3 intact",
								       forge ? "BROKEN" : "repaired (valid key)");
							else
								printf("PEER: fresh key #%u: groups %s, group 4 %s\n",
								       fresh_n, mirror_mode ? "1-4 intact (mirror-built)" : "1-3 intact", forge ? "BROKEN" : "repaired (valid key)");
						}
						memcpy(fresh_prev, use, 16);
						have_prev = 1;
					}
					memcpy(rep + 13, use, 16);
				}
				/*
				 * TASK 2.5: CP_CYCLE_MARK=1|2 makes every reply 30 bytes
				 * whose last byte declares the key's cycle half on the wire
				 * (buf[29]; the server reads it only for fwd peers). 0 keeps
				 * the old 29-byte, undeclared reply. The value is sent as
				 * declared -- the POINT of the mode is that 2 can contradict
				 * a request that declared CW0.
				 */
				if (cycle_mark == 1 || cycle_mark == 2) {
					rep[29] = (unsigned char)cycle_mark;
					replen = 30;
				}
				if (short_reply && !short_done) {
					/* TASK 2.9: the first reply goes out truncated to 8
					 * bytes -- below the 13-byte minimum for TYPE_REPLY. */
					sendto(fd, rep, 8, 0, (struct sockaddr *)&srv, sizeof(srv));
					short_done = 1;
					printf("PEER: sent a SHORT reply (8 bytes) for ch %04X:%04X\n",
					       (unsigned)((buf[6] << 8) | buf[7]),
					       (unsigned)((buf[4] << 8) | buf[5]));
					fflush(stdout);
					continue;   /* not counted as an answer: it was not one */
				}
				/*
		 * TASK 3.16: CP_ANSWER_DELAY_MS holds this reply so it lands
		 * near the ECM's timeout. Unset, the reply goes out at once.
		 */
		{
			const char *d = getenv("CP_ANSWER_DELAY_MS");
			int ms = d ? atoi(d) : 0;
			if (ms > 0 && ms <= 5000) usleep((useconds_t)ms * 1000);
		}
		sendto(fd, rep, replen, 0, (struct sockaddr *)&srv, sizeof(srv));
				sent++;
				printf("PEER: replied CW for ch %04X:%04X sid %04X hash %02X%02X%02X%02X mark %d (%d/%d)\n",
				       (unsigned)((buf[6] << 8) | buf[7]),
				       (unsigned)((buf[4] << 8) | buf[5]),
				       (unsigned)((buf[2] << 8) | buf[3]),
				       buf[8], buf[9], buf[10], buf[11],
				       (replen == 30) ? cycle_mark : 0, sent, maxrep);
				fflush(stdout);
				/*
				 * With re-pushes enabled this must NOT exit: the whole point is
				 * to keep feeding the cache after the request has been answered.
				 * The loop is bounded by the caller killing us, like the server.
				 */
				if (sent >= maxrep && !repush_ms) break;
				continue;
			}

			printf("PEER: ignored packet type %u (len %d)\n", (unsigned)buf[0], (int)n);
			fflush(stdout);
		}
	}

	close(fd);
	printf("PEER: done, %d CW(s) pushed, %d unsolicited, %d ping(s) answered\n",
	       sent, repushed, pinged);
	return sent > 0 ? 0 : 1;
}
