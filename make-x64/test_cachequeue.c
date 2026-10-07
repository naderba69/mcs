/*
 * test_cachequeue.c -- TASK R6 (D60), recreated in R13 (D67).
 *
 * The unit suite for ../src/cachequeue.c -- the bounded SPSC ring that sits
 * between the cache socket drain and the cache parser. It exercises the REAL
 * module, never a copy.
 *
 * What is pinned, in the order the R6 verification lists it:
 *
 *   1. The range zero: an empty ring, a pop that must refuse, and the four
 *      invalid-argument refusals (NULL from, NULL payload, negative length,
 *      a length past CQ_BUF). Those refusals are CALLER ERRORS and are
 *      deliberately not counted as drops -- the drop counter belongs to the
 *      capacity backpressure the module exists for.
 *   2. Round-trip fidelity: length, payload (including a full CQ_BUF
 *      datagram), the sockaddr and the opaque src pointer.
 *   3. Filling: the ring takes exactly CQ_SLOTS entries, FIFO order is kept,
 *      and the next push is refused -- and counted.
 *   4. Wrapping and draining: after one pop the freed slot is reused, and a
 *      drain returns exactly the entries that were queued.
 *
 *   make -C make-x64 test
 */
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>

#include "../src/cachequeue.h"

static int passed = 0, failed = 0;

static void check(const char *what, int got, int want)
{
	if (got == want) { passed++; printf("  [ ok ] %-62s = %d\n", what, got); }
	else { failed++; printf("  [FAIL] %-62s got=%d want=%d\n", what, got, want); }
}

int main(void)
{
	struct cq_entry out;
	struct sockaddr_in from;
	unsigned char small[4];
	unsigned char big[CQ_BUF];
	int src_token = 0x1234;
	void *src = &src_token;
	int i, got, ok;

	printf("test_cachequeue: the real bounded ring (TASK R6/D60, %d slots x %d bytes)\n",
	       CQ_SLOTS, CQ_BUF);

	memset(small, 0xA5, sizeof(small));
	for (i = 0; i < CQ_BUF; i++) big[i] = (unsigned char)(i & 0xff);
	memset(&from, 0, sizeof(from));
	from.sin_family = AF_INET;
	from.sin_port = htons(1234);
	from.sin_addr.s_addr = htonl(0x0a000001);

	cq_init();

	/* -- 1. the range zero ------------------------------------------------ */
	check("fresh ring: depth is zero",                cq_depth(),    0);
	check("fresh ring: nothing counted as enqueued",  (int)cq_enqueued(), 0);
	check("fresh ring: nothing counted as dropped",   (int)cq_dropped(),  0);
	check("a pop on an empty ring refuses",           cq_pop(&out),  0);
	check("a NULL from or a NULL payload is refused",
	      !cq_push(src, NULL, small, 4) && !cq_push(src, &from, NULL, 4), 1);
	check("a negative length and one past CQ_BUF are refused",
	      !cq_push(src, &from, small, -1) && !cq_push(src, &from, small, CQ_BUF + 1), 1);
	check("those refusals are caller errors, not ring drops", (int)cq_dropped(), 0);

	/* -- 2. round-trip fidelity ------------------------------------------- */
	check("the empty datagram itself is legal (len 0)",
	      cq_push(src, &from, small, 0), 1);
	check("the ring holds exactly one entry",         cq_depth(), 1);
	check("the pop returns that entry",               cq_pop(&out), 1);
	check("the popped length is the pushed length",   out.len, 0);

	cq_init();
	got = cq_push(src, &from, big, CQ_BUF) &&
	      cq_pop(&out) && out.len == CQ_BUF && memcmp(out.buf, big, CQ_BUF) == 0;
	check("a full CQ_BUF-byte datagram round-trips intact", got ? 1 : 0, 1);
	check("the source address survives the round trip", ntohs(out.from.sin_port), 1234);
	check("the opaque src pointer survives the round trip", out.src == src ? 1 : 0, 1);

	/* -- 3. filling and FIFO ---------------------------------------------- */
	cq_init();
	ok = 1;
	for (i = 0; i < 3; i++) {
		unsigned char tag = (unsigned char)(0xA1 + i);
		if (!cq_push(src, &from, &tag, 1)) ok = 0;
	}
	for (i = 0; i < 3; i++)
		if (!cq_pop(&out) || out.buf[0] != (unsigned char)(0xA1 + i)) ok = 0;
	check("FIFO order is kept and the drained ring is empty",
	      (ok && cq_depth() == 0) ? 1 : 0, 1);

	cq_init();
	for (i = 0; i < CQ_SLOTS; i++)
		if (!cq_push(src, &from, big, CQ_BUF)) break;
	check("the ring fills to exactly CQ_SLOTS",        cq_depth(), CQ_SLOTS);
	check("a push on the full ring is refused",        cq_push(src, &from, big, CQ_BUF), 0);
	check("that refusal IS counted as a drop",         (int)cq_dropped(), 1);
	check("the refused push never counted as enqueued", (int)cq_enqueued(), CQ_SLOTS);

	/* -- 4. wrapping and draining ----------------------------------------- */
	got = cq_pop(&out) && cq_push(src, &from, big, CQ_BUF) && cq_depth() == CQ_SLOTS;
	check("after one pop the freed slot accepts the next push", got ? 1 : 0, 1);
	got = 0;
	while (cq_pop(&out)) got++;
	check("a drain returns exactly the queued entries", got, CQ_SLOTS);
	got = (cq_depth() == 0) && ((int)cq_enqueued() == CQ_SLOTS + 1) &&
	      ((int)cq_dropped() == 1) && (cq_pop(&out) == 0);
	check("an empty ring pops nothing and keeps its counters", got ? 1 : 0, 1);

	cq_init();
	got = (cq_depth() == 0) && (cq_enqueued() == 0) && (cq_dropped() == 0);
	check("cq_init clears the depth and both counters", got ? 1 : 0, 1);

	printf("== %d/%d passed, %d failed ==\n", passed, passed + failed, failed);
	return failed ? 1 : 0;
}
