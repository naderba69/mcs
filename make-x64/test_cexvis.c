/*
 * make-x64/test_cexvis.c -- TASK R20 (D75)
 *
 * Cacheex visibility: the per-peer five counters (push/relay/drop/
 * filter/loop) and the NAMED drop. This test drives the REAL
 * src/cexvis.h (compiled straight into this binary -- the layer is a
 * pure header, like chanhealth.h and ptguard.h before it), never a copy
 * of its rules.
 *
 * What the roadmap's acceptance sentence demands of this layer:
 *   "counters per peer (push/relay/drop/filter/loop) in /cwlog and /json
 *    and telnet -- no config key"  ->  the five counters exist per
 *    (origin, peer id) and nothing here reads a config flag,
 *   "the named drop appears in the three surfaces"  ->  a drop note
 *    carries the rule's NAME and the channel, and the row keeps it
 *    (lastev/lastreason/lastcaid) so every surface can print it.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "../src/cexvis.h"

static int fails = 0;
static int checks = 0;

static void ok(int cond, const char *what)
{
	checks++;
	printf("%s %s\n", cond ? "  [ ok ]" : "  [FAIL]", what);
	if (!cond) fails++;
}

int main(void)
{
	struct cexvis_tab t;
	struct cexvis_row rows[CEXVIS_SLOTS];
	struct cexvis_row *r;
	int f, n, i;
	uint32_t now = 1000;

	memset(&t, 0, sizeof(t));

	/* --- the five counters, one row per (origin, peer id) --- */
	r = cexvis_note(&t, PEER_CCCAM_CLIENT, 3, "cxu", CEXVIS_PUSH, "",
			0x1884, 0, 0x0064, now++);
	ok(r != NULL && r->n[CEXVIS_PUSH] == 1, "push counter records an arrival");
	cexvis_note(&t, PEER_CCCAM_CLIENT, 3, "cxu", CEXVIS_RELAY, "",
			0x1884, 0, 0x0064, now++);
	cexvis_note(&t, PEER_CCCAM_CLIENT, 3, "cxu", CEXVIS_DROP, "cacheex-fake-cw",
			0x1884, 0, 0x0065, now++);
	cexvis_note(&t, PEER_CCCAM_CLIENT, 3, "cxu", CEXVIS_FILTER, "pushfilter",
			0x1884, 0, 0x0066, now++);
	cexvis_note(&t, PEER_CCCAM_CLIENT, 3, "cxu", CEXVIS_LOOP, "nodeloop",
			0x1884, 0, 0x0067, now++);
	ok(r->n[CEXVIS_PUSH] == 1 && r->n[CEXVIS_RELAY] == 1 &&
	   r->n[CEXVIS_DROP] == 1 && r->n[CEXVIS_FILTER] == 1 &&
	   r->n[CEXVIS_LOOP] == 1, "all five counters are independent");

	/* --- THE NAMED DROP: the row keeps the rule name + channel --- */
	ok(r->lastev == CEXVIS_LOOP && !strcmp(r->lastreason, "nodeloop"),
	   "the last event is recorded with its named rule");
	cexvis_note(&t, PEER_CCCAM_CLIENT, 3, NULL, CEXVIS_DROP, "cacheex-fake-cw",
			0x1884, 0, 0x0065, now++);
	ok(r->lastev == CEXVIS_DROP && !strcmp(r->lastreason, "cacheex-fake-cw"),
	   "the named drop replaces the last event (drop: cacheex-fake-cw)");
	cexvis_note(&t, PEER_CCCAM_CLIENT, 3, NULL, CEXVIS_PUSH, "",
			0x1884, 0, 0x0068, now++);
	ok(r->lastev == CEXVIS_DROP && !strcmp(r->lastreason, "cacheex-fake-cw"),
	   "an unnamed push never buries the named drop (three-surface view)");
	ok(r->lastcaid == 0x1884 && r->lastprov == 0 && r->lastsid == 0x0065,
	   "the named drop carries the channel it hit");
	ok(!strcmp(r->name, "cxu"), "a NULL name keeps the row's display name");

	/* --- identity: (origin, peer id) never merges across origins --- */
	cexvis_note(&t, PEER_CACHEEX_SERVER, 3, "upstream", CEXVIS_PUSH, "",
			0x1884, 0, 0x0064, now++);
	n = cexvis_copy(&t, rows, CEXVIS_SLOTS);
	ok(n == 2, "the same bare id under another origin is a second row");
	f = 0;
	for (i = 0; i < n; i++)
		if (rows[i].origin == PEER_CACHEEX_SERVER && rows[i].peerid == 3 &&
		    !strcmp(rows[i].name, "upstream")) f = 1;
	ok(f, "the second row carries its own name and origin");

	/* --- a name is filled once, at the first named note --- */
	cexvis_note(&t, PEER_CCCAM_CLIENT, 3, "IMPOSTOR", CEXVIS_PUSH, "",
			0x1884, 0, 0x0068, now++);
	ok(!strcmp(r->name, "cxu"), "an established name is never overwritten");

	/* --- an unnamed NEW row falls back to the origin label --- */
	r = cexvis_note(&t, PEER_CAMD35_CLIENT, 9, NULL, CEXVIS_DROP, "rc-filter",
			0x0B00, 1, 0x0010, now++);
	ok(r && !strcmp(r->name, "camd35-client"),
	   "an unnamed new row is named by its origin label");
	ok(!strcmp(r->lastreason, "rc-filter"),
	   "and its drop is named (rc-filter)");

	/* --- event names and origin labels are the surface vocabulary --- */
	ok(!strcmp(cexvis_ev_name(CEXVIS_PUSH), "push") &&
	   !strcmp(cexvis_ev_name(CEXVIS_DROP), "drop") &&
	   !strcmp(cexvis_ev_name(CEXVIS_LOOP), "loop"),
	   "the five event names are the surface words");
	ok(!strcmp(cexvis_origin_name(PEER_CCCAM_CLIENT), "cccam-client") &&
	   !strcmp(cexvis_origin_name(PEER_CACHEEX_SERVER), "cacheex-server"),
	   "origin labels name the peer family");

	/* --- LRU eviction: the 25th row reuses the oldest --- */
	{
		uint32_t t0 = 50000;
		struct cexvis_tab u;
		memset(&u, 0, sizeof(u));
		for (i = 0; i < CEXVIS_SLOTS; i++)
			cexvis_note(&u, PEER_CCCAM_CLIENT, (uint32_t)(100 + i),
				    "x", CEXVIS_PUSH, "", 0, 0, 0, t0 + i);
		cexvis_note(&u, PEER_CCCAM_CLIENT, 777, "new", CEXVIS_RELAY, "",
				0, 0, 0, t0 + 1000);
		n = cexvis_copy(&u, rows, CEXVIS_SLOTS);
		ok(n == CEXVIS_SLOTS, "the table holds exactly SLOTS rows");
		f = 0;
		for (i = 0; i < n; i++)
			if (rows[i].peerid == 777) f = 1;
		ok(f, "the new row survived (the oldest was evicted)");
		f = 0;
		for (i = 0; i < n; i++)
			if (rows[i].peerid == 100) f = 1;
		ok(!f, "the LRU row (peer 100) was the eviction victim");
		ok(u.evicts == 1, "the eviction is counted");
	}

	/* --- copy order: most recently touched first --- */
	{
		struct cexvis_tab v;
		memset(&v, 0, sizeof(v));
		cexvis_note(&v, PEER_CCCAM_CLIENT, 1, "old", CEXVIS_PUSH, "", 0, 0, 0, 10);
		cexvis_note(&v, PEER_CCCAM_CLIENT, 2, "new", CEXVIS_PUSH, "", 0, 0, 0, 20);
		n = cexvis_copy(&v, rows, CEXVIS_SLOTS);
		ok(n == 2 && rows[0].peerid == 2 && rows[1].peerid == 1,
		   "snapshot returns the most recently touched row first");
	}

	printf("== %d/%d passed, %d failed ==\n", checks - fails, checks, fails);
	return fails ? 1 : 0;
}
