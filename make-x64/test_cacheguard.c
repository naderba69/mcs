/*
 * TASK 2.9 -- the unit suite for the cache protocol guard (../src/cacheguard.h).
 *
 * What is pinned here, in order of how much it would hurt to get wrong:
 *
 *   1. The minimum-length table equals the offsets the handlers really read.
 *      Too low re-opens the uninitialised-stack reads; too high starts
 *      refusing datagrams that this server's own senders produce (REQUEST
 *      12, REPLY 29, PINGREQ 13, PINGRPL 9, RESENDREQ 16, HELLO_ACK 9).
 *      Unknown types stay at the stock floor of 2 -- the guard must never
 *      invent policy for packet shapes the server does not speak.
 *   2. The window: the first event writes, the rest of the window is silent
 *      but COUNTED, the window reopens after CG_WINDOW_MS, and a boot-time
 *      event (tick 0) cannot hold it open forever.
 *   3. The sentences: exact evidence (type, both lengths, sender, count),
 *      the short drop says nothing was parsed, the unknown note says stock
 *      had already dropped it; both survive a short buffer.
 *   4. Degenerate arguments.
 */
#include "../src/cacheguard.h"

#include <stdio.h>
#include <string.h>

static int passed = 0, failed = 0;

#define CHECK(cond, what) do { \
	if (cond) { passed++; printf("  [ ok ] %s\n", what); } \
	else { failed++; printf("  [FAIL] %s\n", what); } \
} while (0)

int main(void)
{
	char line[512];
	int n;
	struct cg_window w;

	printf("cacheguard: the cache protocol guard (ingress validation)\n");

	/* --- 1. the minimum-length table ------------------------------------- */
	CHECK(cg_minlen(1) == 12, "TYPE_REQUEST needs 12 (handler reads buf[1..11])");
	CHECK(cg_minlen(2) == 13, "TYPE_REPLY needs 13 (integrity byte at buf[12])");
	CHECK(cg_minlen(3) == 13, "TYPE_PINGREQ needs 13 (port at buf[11..12])");
	CHECK(cg_minlen(4) == 6,  "TYPE_PINGRPL needs 6 (peer id at buf[4..5])");
	CHECK(cg_minlen(5) == 16, "TYPE_RESENDREQ needs 16 (upstream's own gate)");
	CHECK(cg_minlen(0x10) == 9, "TYPE_HELLO_ACK needs 9 (id + crc at buf[4..8])");
	CHECK(cg_minlen(0x13) == 2 && cg_minlen(0x92) == 2 && cg_minlen(200) == 2,
	      "unknown types keep the stock floor of 2: no invented policy");

	/* the mins never refuse what this server's own senders produce */
	CHECK(cg_minlen(1) <= 12 && cg_minlen(2) <= 29 && cg_minlen(2) <= 30,
	      "our own REQUEST (12) and REPLY (29/30) always pass");
	CHECK(cg_minlen(3) <= 13 && cg_minlen(4) <= 9 && cg_minlen(5) <= 16,
	      "our own PINGREQ (13), PINGRPL (9) and RESENDREQ (16) always pass");
	CHECK(cg_minlen(0x10) <= 9, "our own HELLO_ACK (9) always passes");

	/* --- 2. the window ---------------------------------------------------- */
	memset(&w, 0, sizeof(w));
	CHECK(cg_window_event(&w, 1000) == 1, "the first event opens the window and writes");
	CHECK(cg_window_event(&w, 2000) == 0, "an event inside the window stays silent");
	CHECK(cg_window_event(&w, 3000) == 0, "and so does the next");
	CHECK(w.count == 3, "but the window COUNTED all three (the suppression is stated)");
	CHECK(cg_window_event(&w, 1000 + CG_WINDOW_MS) == 1,
	      "an event after the window reopens it and writes again");
	CHECK(w.count == 1, "the reopened window starts counting from one");

	memset(&w, 0, sizeof(w));
	CHECK(cg_window_event(&w, 0) == 1, "an event at tick 0 writes");
	/* the tick-0 event opens at tick 1 (a zero would look like "no window"),
	 * so its window is [1, 60001): one tick longer, by construction, once */
	CHECK(cg_window_event(&w, CG_WINDOW_MS) == 0,
	      "tick-0's window runs one tick long (opened at 1, not 0)");
	CHECK(cg_window_event(&w, CG_WINDOW_MS + 1) == 1,
	      "and it cannot hold the window open beyond that: it reopens at 60001");

	/* two kinds do not share a window */
	{
		struct cg_window a, b;
		memset(&a, 0, sizeof(a)); memset(&b, 0, sizeof(b));
		(void)cg_window_event(&a, 1000);
		CHECK(cg_window_event(&b, 1000) == 1,
		      "separate kinds have separate windows (short never silences unknown)");
	}

	/* --- 3. the sentences -------------------------------------------------- */
	memset(&w, 0, sizeof(w));
	(void)cg_window_event(&w, 1000);
	(void)cg_window_event(&w, 1100);
	(void)cg_window_event(&w, 1200);
	n = cg_format_short(line, (int)sizeof(line), 2, 8, 13,
	                    0x0100007fu /* raw sin_addr: 127.0.0.1 */, 5000, w.count);
	CHECK(n > 0, "the short-drop sentence builds");
	CHECK(strstr(line, "TYPE_REPLY") != NULL, "it names the packet type");
	CHECK(strstr(line, "type 2") != NULL, "it names the numeric type for the log grep");
	CHECK(strstr(line, "8 bytes, this type needs 13") != NULL,
	      "it names both the actual and the minimum length");
	CHECK(strstr(line, "from 127.0.0.1:5000") != NULL, "it names the sender");
	CHECK(strstr(line, "nothing was parsed") != NULL,
	      "it says the datagram was not interpreted (GR8: observation, not verdict)");
	CHECK(strstr(line, "3 in this window") != NULL, "it states the window's suppression count");
	n = cg_format_short(line, 24, 2, 8, 13, 1, 2, 1);
	CHECK(n >= 0 && strlen(line) < 24, "it survives a short buffer, terminated");

	n = cg_format_unknown(line, (int)sizeof(line), 1,
	                      0x0100000au /* raw sin_addr: 10.0.0.1 */, 4444, w.count);
	CHECK(n > 0, "the unknown-sender sentence builds");
	CHECK(strstr(line, "3 packet(s) in this window") != NULL, "it states the window's count");
	CHECK(strstr(line, "TYPE_REQUEST (1)") != NULL, "it names the packet, number included");
	CHECK(strstr(line, "from 10.0.0.1:4444") != NULL, "it names the sender");
	CHECK(strstr(line, "configured peers only") != NULL, "it states the rule");
	CHECK(strstr(line, "before any layer saw it") != NULL,
	      "it says the treatment is stock's, unchanged");
	n = cg_format_unknown(line, 24, 1, 1, 2, 1);
	CHECK(n >= 0 && strlen(line) < 24, "it survives a short buffer, terminated");

	{
		/* the byte-order convention: recv_ip is raw sin_addr, so an address
		 * that prints "127.0.0.1" everywhere else in this server must print
		 * "127.0.0.1" here too -- first live run printed it reversed. */
		n = cg_format_short(line, (int)sizeof(line), 2, 8, 13, 0x0100007f, 9, 1);
		CHECK(strstr(line, "from 127.0.0.1:9") != NULL,
		      "the sender prints in iptoa order (LSB first), like every other layer");
	}

	{
		char num[64];
		n = cg_format_short(num, (int)sizeof(num), 0x92, 3, 2, 1, 2, 1);
		CHECK(n > 0 && strstr(num, "UNKNOWN-TYPE") != NULL && strstr(num, "type 146") != NULL,
		      "an unknown short type is named UNKNOWN-TYPE with its number");
	}

	/* --- 4. degenerate arguments ------------------------------------------- */
	CHECK(cg_format_short(NULL, 100, 2, 8, 13, 1, 2, 1) < 0, "NULL buffer refused (short)");
	CHECK(cg_format_unknown(NULL, 100, 2, 1, 2, 1) < 0, "NULL buffer refused (unknown)");
	CHECK(cg_format_short(line, 0, 2, 8, 13, 1, 2, 1) < 0, "zero size refused");
	CHECK(cg_format_short(line, (int)sizeof(line), 2, -1, 13, 1, 2, 1) < 0,
	      "a negative length is refused, never printed");

	printf("  %d ok, %d failed\n", passed, failed);
	return failed ? 1 : 0;
}
