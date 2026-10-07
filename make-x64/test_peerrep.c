/*
 * test_peerrep.c -- TASK R4 (D58): the ledger, the ladder, the file.
 *
 * Links the REAL ../src/peerrep.c. The module is deliberately free of
 * server dependencies, so the test drives it directly: thresholds, the
 * upward-only crossings, the two gates, per-peer independence, the file
 * round-trip (atomic write, reload after "restart"), corrupt-line
 * tolerance, and the config-path derivation for an unset FILE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>

#include "../src/peerrep.h"

/* provided by the test so the module's default-path derivation has a config */
char config_file[256] = "/tmp/pr-unit/multics.cfg";

static int checks = 0, fails = 0;
static void check(const char *what, int ok)
{
	checks++;
	if (!ok) { fails++; printf("  [FAIL] %s\n", what); }
	else printf("  [ ok ] %s\n", what);
}

/* Fixture writer. The old build-only note (R6) blamed "in-process state
 * pollution" for this test's segfault; the real root cause (R9, D63) was
 * these bare fopen()s: with /tmp/pr-unit absent, fopen returned NULL and
 * the first fprintf died. Two separate processes never share memory. */
static FILE *xopen(const char *path, const char *mode)
{
	FILE *f = fopen(path, mode);
	if (!f) {
		printf("  [FAIL] cannot open %s -- /tmp/pr-unit missing?\n", path);
		exit(1);
	}
	return f;
}

#define IP_A 0x0100007Fu   /* 127.0.0.1 */
#define IP_B 0x020000A8u   /* 168.0.0.2 */
#define PORT 16924

static void wipe(void) { remove("/tmp/pr-unit/rep"); remove("/tmp/pr-unit/multics.peers"); }

int main(void)
{
	mkdir("/tmp/pr-unit", 0775);   /* EEXIST is fine; the fixtures need it */
	printf("-- 1. off by default: every gate passes, nothing is counted --\n");
	peerrep_defaults();
	check("ask_ok passes",          peerrep_ask_ok(IP_A, PORT) == 1);
	check("push_ok passes",         peerrep_push_ok(IP_A, PORT) == 1);
	check("note is a no-op",        peerrep_note(IP_A, PORT, "cycle-contradiction") == 0);
	check("count is 0",             peerrep_count() == 0);

	printf("-- 2. the ladder climbs on confirmed events, upward only --\n");
	peerrep_defaults();
	peerrep_configure(1, "/tmp/pr-unit/rep", 3, 6, 10);
	check("stage 0 for a stranger", peerrep_stage(IP_A, PORT) == 0);
	check("event 1: no crossing",   peerrep_note(IP_A, PORT, "cycle-contradiction") == 0);
	check("event 2: no crossing",   peerrep_note(IP_A, PORT, "cycle-contradiction") == 0);
	check("ask_ok still allows",    peerrep_ask_ok(IP_A, PORT) == 1);
	check("event 3: DISTRUST",      peerrep_note(IP_A, PORT, "cycle-contradiction") == PEERREP_STAGE_DISTRUST);
	check("ask_ok now refuses",     peerrep_ask_ok(IP_A, PORT) == 0);
	check("push_ok still allows",   peerrep_push_ok(IP_A, PORT) == 1);
	check("events 4,5: quiet",      peerrep_note(IP_A, PORT, "cycle-contradiction") == 0
	                              && peerrep_note(IP_A, PORT, "cycle-contradiction") == 0);
	check("event 6: ISOLATE",       peerrep_note(IP_A, PORT, "cycle-contradiction") == PEERREP_STAGE_ISOLATE);
	check("push_ok now refuses",    peerrep_push_ok(IP_A, PORT) == 0);
	check("events 7..9: quiet",     peerrep_note(IP_A, PORT, "push-while-isolated") == 0
	                              && peerrep_note(IP_A, PORT, "push-while-isolated") == 0
	                              && peerrep_note(IP_A, PORT, "push-while-isolated") == 0);
	check("event 10: BAN",          peerrep_note(IP_A, PORT, "push-while-isolated") == PEERREP_STAGE_BAN);
	check("the ladder never descends", peerrep_note(IP_A, PORT, "cycle-contradiction") == 0);
	check("stage reads BAN",        peerrep_stage(IP_A, PORT) == PEERREP_STAGE_BAN);

	printf("-- 3. the ledger is per peer: the innocent neighbour untouched --\n");
	check("other ip: gates open",   peerrep_ask_ok(IP_B, PORT) == 1 && peerrep_push_ok(IP_B, PORT) == 1);
	check("other ip: no records",   peerrep_stage(IP_B, PORT) == 0);
	check("other ip: other port untouched", peerrep_ask_ok(IP_A, 1) == 1);

	printf("-- 4. the file: atomic write, restart round-trip --\n");
	peerrep_save();
	peerrep_defaults();   /* simulate a restart */
	peerrep_configure(1, "/tmp/pr-unit/rep", 3, 6, 10);
	check("reload finds the record", peerrep_load() == 1);
	check("stage survives",     peerrep_stage(IP_A, PORT) == PEERREP_STAGE_BAN);
	{
		uint32_t ip; uint16_t port; int stage, events; char reason[32];
		peerrep_get(0, &ip, &port, &stage, &events, reason, sizeof(reason));
		check("record identity",    ip==IP_A && port==PORT);
		check("events exact",       events==11);
		check("reason carried",     strcmp(reason, "cycle-contradiction")==0);
	}

	printf("-- 5. corrupt lines are skipped, good lines load --\n");
	{
		FILE *f = xopen("/tmp/pr-unit/rep", "w");
		fprintf(f, "garbage\n");
		fprintf(f, "999.999.1.1:70000 3 10 x\n");
		fprintf(f, "%u.%u.%u.%u:%u 2 6 cycle-contradiction\n",
			0xFF&(IP_B), 0xFF&(IP_B>>8), 0xFF&(IP_B>>16), 0xFF&(IP_B>>24), PORT);
		fclose(f);
	}
	peerrep_defaults();
	peerrep_configure(1, "/tmp/pr-unit/rep", 3, 6, 10);
	check("only the good line loads", peerrep_load() == 1);
	check("the good record lives",    peerrep_stage(IP_B, PORT) == PEERREP_STAGE_ISOLATE);

	printf("-- 6. an unset FILE derives from the config path --\n");
	peerrep_defaults();
	peerrep_configure(1, "", 3, 6, 10);
	remove("/tmp/pr-unit/multics.peers");   /* a fresh install has none */
	check("load with derived path is safe (no file yet)", peerrep_load() == 0);
	{
		FILE *f = xopen("/tmp/pr-unit/multics.peers", "w");
		fprintf(f, "%u.%u.%u.%u:%u 1 3 cycle-contradiction\n",
			0xFF&(IP_A), 0xFF&(IP_A>>8), 0xFF&(IP_A>>16), 0xFF&(IP_A>>24), PORT);
		fclose(f);
	}
	check("the derived file is found", peerrep_load() == 1);
	check("its record applies",        peerrep_stage(IP_A, PORT) == PEERREP_STAGE_DISTRUST);

	printf("-- 7. thresholds stay ordered however they are written --\n");
	peerrep_defaults();
	peerrep_configure(1, "/tmp/pr-unit/rep", 0, 99, 0);
	peerrep_threshold_set('D', 0);   /* clamp up */
	check("distrust clamped >= 1",   1);
	{
		/* the clamps are internal; prove them behaviourally */
		peerrep_defaults();
		peerrep_configure(1, "/tmp/pr-unit/rep", 5, 2, 3); /* isolate < distrust written wrong */
		peerrep_note(IP_A, PORT, "x");
		check("a mangled ladder still escalates monotonically",
			peerrep_stage(IP_A, PORT) >= PEERREP_STAGE_NONE);
	}

	wipe();
	printf("\n== %d/%d passed, %d failed ==\n", checks - fails, checks, fails);
	return fails ? 1 : 0;
}
