/*
 * probe_cardsids.c — TASK 3.13: call the REAL cardsids_update.
 *
 * The Makefile rule compiles production main.c with -Dmain=multics_main
 * and links it with the normal objects, handing main() to this file. What
 * runs below is the production function — no copy, no stub.
 *
 * The bite the plan asks for: the counter is shown at the floor, stays
 * there through many further failures, then one success returns it to 0.
 * The +100 ceiling and the zero-crossing rules are asserted unchanged.
 *
 *   make -C make-x64 x64/probe_cardsids && ./x64/probe_cardsids
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <poll.h>

#include "common.h"
#include "ecmdata.h"
#include "msg-cccam.h"
#include "config.h"

int cardsids_update(struct cs_card_data *card, uint32_t prov, uint16_t sid, int val);
extern int flag_debugscr;

static int score(struct cs_card_data *card, uint32_t prov, uint16_t sid)
{
	struct sid_data *s = card->sids[sid >> 8];

	while (s) {
		if (s->sid == sid && s->prov == prov) return s->val;
		s = s->next;
	}
	return 9999;
}

static int fail(const char *msg)
{
	printf("[FAIL] %s\n", msg);
	return 1;
}

int main(void)
{
	static struct cs_card_data card;
	int i, v;

	memset(&card, 0, sizeof(card));
	flag_debugscr = 1; /* the same switch -v sets, so the floor lines are visible */

	if (cardsids_update(&card, 0, 0, -1) != 0)
		return fail("sid 0 must be ignored");
	if (card.sids[0] != NULL)
		return fail("sid 0 allocated a node");

	/* sid 0x0164 lives in sids[1], so the sid-0 check above stays clean */
	for (i = 0; i < 100; i++)
		cardsids_update(&card, 0x000000, 0x0164, -1);
	v = score(&card, 0x000000, 0x0164);
	printf("counter after 100 failures: %d\n", v);
	if (v != -100) return fail("floor not reached");

	for (i = 0; i < 50; i++)
		cardsids_update(&card, 0x000000, 0x0164, -1);
	v = score(&card, 0x000000, 0x0164);
	printf("counter after 50 more failures: %d\n", v);
	if (v != -100) return fail("the floor receded");

	cardsids_update(&card, 0x000000, 0x0164, 1);
	v = score(&card, 0x000000, 0x0164);
	printf("counter after one success: %d\n", v);
	if (v != 0) return fail("a success did not recover the floor");

	/* above the floor, the old rules still hold */
	cardsids_update(&card, 0x000000, 0x0164, -1);
	cardsids_update(&card, 0x000000, 0x0164, -1);
	v = score(&card, 0x000000, 0x0164);
	printf("counter two failures from zero: %d\n", v);
	if (v != -2) return fail("two failures from zero");

	cardsids_update(&card, 0x000000, 0x0164, 1);
	v = score(&card, 0x000000, 0x0164);
	printf("counter one success from -2: %d\n", v);
	if (v != 0) return fail("a success from a negative score must reset to 0");

	cardsids_update(&card, 0x000000, 0x0164, 1);
	cardsids_update(&card, 0x000000, 0x0164, 1);
	cardsids_update(&card, 0x000000, 0x0164, 1);
	cardsids_update(&card, 0x000000, 0x0164, -1);
	v = score(&card, 0x000000, 0x0164);
	printf("counter positive then failure: %d\n", v);
	if (v != 0) return fail("a failure from a positive score must reset to 0");

	/* the +100 ceiling is not this task; it must stay a lock */
	{
		static struct cs_card_data ceil;
		memset(&ceil, 0, sizeof(ceil));
		for (i = 0; i < 100; i++)
			cardsids_update(&ceil, 0, 0x0165, 1);
		v = score(&ceil, 0, 0x0165);
		printf("ceiling after 100 successes: %d\n", v);
		if (v != 100) return fail("ceiling not reached");
		for (i = 0; i < 10; i++)
			cardsids_update(&ceil, 0, 0x0165, 1);
		v = score(&ceil, 0, 0x0165);
		printf("ceiling after 10 more: %d\n", v);
		if (v != 100) return fail("the +100 ceiling moved");
		cardsids_update(&ceil, 0, 0x0165, -1);
		v = score(&ceil, 0, 0x0165);
		printf("ceiling after a failure: %d\n", v);
		if (v != 100) return fail("the +100 ceiling must stay untouched");
	}

	/* a second provider on the same card does not share the floor */
	for (i = 0; i < 100; i++)
		cardsids_update(&card, 0x000001, 0x0164, -1);
	cardsids_update(&card, 0x000000, 0x0164, -1);
	cardsids_update(&card, 0x000000, 0x0164, -1);
	v = score(&card, 0x000000, 0x0164);
	printf("first provider while second is floored: %d\n", v);
	if (v != -2) return fail("providers share a score");
	v = score(&card, 0x000001, 0x0164);
	printf("second provider at its own floor: %d\n", v);
	if (v != -100) return fail("second provider did not floor on its own");

	printf("[ ok ] production cardsids_update: floor holds, a success recovers\n");
	return 0;
}
