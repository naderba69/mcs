/*
 * test_cachequeue.c -- TASK R6 (D60), reconstructed.
 *
 * Exercises the REAL ../src/cachequeue.c (the bounded SPSC ring between the
 * cache socket drain and the cache parser). Capacity IS the backpressure: a
 * full ring drops the NEWEST datagram and counts it -- the cache is lossy by
 * design and the oldest datagram is the one closest to being served.
 *
 *   make -C make-x64 test
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "../src/cachequeue.h"

static int passed = 0, failed = 0;

static void check(const char *what, int got, int want)
{
	if (got == want) { passed++; printf("  [ ok ] %-58s = %d\n", what, got); }
	else { failed++; printf("  [FAIL] %-58s got=%d want=%d\n", what, got, want); }
}

int main(void)
{
	struct cq_entry e;
	struct sockaddr_in a, b;
	unsigned char buf[CQ_BUF];
	int i, ok;

	printf("test_cachequeue: the bounded cache ring (TASK R6 / D60)\n");

	memset(&a, 0, sizeof(a));
	memset(&b, 0, sizeof(b));
	a.sin_port = 1; b.sin_port = 2;
	for (i = 0; i < CQ_BUF; i++) buf[i] = (unsigned char)(i & 0xFF);

	/* ==================================================================
	 * 1. Empty ring.
	 * ================================================================== */
	cq_init();
	check("an empty ring pops nothing", cq_pop(&e), 0);
	check("an empty ring has depth 0", cq_depth(), 0);
	check("enqueued starts at 0", (int)cq_enqueued(), 0);
	check("dropped starts at 0", (int)cq_dropped(), 0);

	/* ==================================================================
	 * 2. Argument guards.
	 * ================================================================== */
	check("push(NULL from) is refused", cq_push(0, 0, buf, 16), 0);
	check("push(NULL buf) is refused",
	      cq_push(0, &a, 0, 16), 0);
	check("push(len > CQ_BUF) is refused",
	      cq_push(0, &a, buf, CQ_BUF + 1), 0);
	check("push(len < 0) is refused", cq_push(0, &a, buf, -1), 0);
	check("push(len 0) is accepted", cq_push(0, &a, buf, 0), 1);
	check("pop(NULL) is refused", cq_pop(0), 0);
	check("the len-0 entry pops", cq_pop(&e), 1);
	check("nothing was dropped by the guards", (int)cq_dropped(), 0);

	/* ==================================================================
	 * 3. FIFO order and payload fidelity.
	 * ================================================================== */
	cq_init();
	for (i = 0; i < 5; i++) {
		buf[0] = (unsigned char)i;
		check("one datagram queues", cq_push((void *)(uintptr_t)(i + 1),
		      (i & 1) ? &b : &a, buf, 64), 1);
	}
	check("depth after five pushes", cq_depth(), 5);
	for (i = 0; i < 5; i++) {
		ok = cq_pop(&e);
		check("one datagram pops", ok, 1);
		check("FIFO payload order", e.buf[0] == (unsigned char)i, 1);
		check("FIFO source passthrough",
		      e.src == (void *)(uintptr_t)(i + 1), 1);
		check("the sockaddr travels with the datagram",
		      e.from.sin_port == ((i & 1) ? b.sin_port : a.sin_port), 1);
		check("the length travels with the datagram", e.len, 64);
	}
	check("the ring is empty again", cq_depth(), 0);
	check("enqueued counted every push", (int)cq_enqueued(), 5);

	/* ==================================================================
	 * 4. Capacity is the backpressure: 256 in, the 257th DROPS (the new
	 *    one, not the oldest) and is counted.
	 * ================================================================== */
	cq_init();
	for (i = 0; i < CQ_SLOTS; i++) {
		buf[0] = (unsigned char)(i & 0xFF);
		if (!cq_push(0, &a, buf, 32)) {
			check("the ring accepted every datagram up to capacity", 0, 1);
			break;
		}
	}
	check("the ring holds exactly CQ_SLOTS", cq_depth(), CQ_SLOTS);
	check("the overflow push is refused", cq_push(0, &a, buf, 32), 0);
	check("the overflow is counted as dropped", (int)cq_dropped(), 1);
	check("depth is unchanged after the drop", cq_depth(), CQ_SLOTS);

	ok = cq_pop(&e);
	check("the OLDEST datagram pops first (the new one was dropped)", ok, 1);
	check("the oldest payload survived", e.buf[0] == 0, 1);
	ok = cq_pop(&e);
	check("and the second oldest next", e.buf[0] == 1, 1);

	/* ==================================================================
	 * 5. Wrap-around: keep pushing/popping across the modulus.
	 * ================================================================== */
	cq_init();
	ok = 1;
	for (i = 0; i < CQ_SLOTS * 3; i++) {
		buf[0] = (unsigned char)(i & 0xFF);
		if (!cq_push(0, &a, buf, 8)) { ok = 0; break; }
		if (!cq_pop(&e)) { ok = 0; break; }
		if (e.buf[0] != (unsigned char)(i & 0xFF)) { ok = 0; break; }
	}
	check("three full trips around the ring stay in order", ok, 1);
	check("depth is 0 after the trips", cq_depth(), 0);
	check("every trip datagram was counted", (int)cq_enqueued(), CQ_SLOTS * 3);
	check("nothing was dropped on the trips", (int)cq_dropped(), 0);

	cq_init();
	check("init resets the counters", (int)cq_enqueued(), 0);

	printf("test_cachequeue: %d ok, %d failed\n", passed, failed);
	return failed ? 1 : 0;
}
