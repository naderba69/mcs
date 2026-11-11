/*
 * test_peerrep.c -- legacy peerrep TTL, persistence, and re-entry contract.
 *
 * Links the production ../src/peerrep.c. The legacy table is IP:port scoped
 * and intentionally separate from R48's source x provider facts: its event
 * counters must not become persistent active quarantine on restart or after a
 * TTL expires. This test does NOT claim D111 D-4 is complete (provider/channel
 * scope, clean-sample exit, 30% cap, and storm handling remain separate work).
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

static void wipe(void)
{
	remove("/tmp/pr-unit/rep");
	remove("/tmp/pr-unit/multics.peers");
}

int main(void)
{
	uint32_t now;
	int i, d, iso, ban;

	mkdir("/tmp/pr-unit", 0775);   /* EEXIST is fine */
	wipe();

	printf("-- 1. off by default: no gate, no accounting --\n");
	peerrep_defaults();
	check("ask_ok passes",  peerrep_ask_ok(IP_A, PORT, 100u) == 1);
	check("push_ok passes", peerrep_push_ok(IP_A, PORT, 100u) == 1);
	check("note is a no-op", peerrep_note(IP_A, PORT, "cycle-contradiction", 100u) == 0);
	check("count is 0", peerrep_count() == 0);

	printf("-- 2. bounded stages, deadline wrap, and fresh-event re-entry --\n");
	peerrep_defaults();
	peerrep_configure(1, "/tmp/pr-unit/rep", 3, 6, 10);
	/* Deliberately place the 15-minute deadline across uint32 tick wrap. */
	now = 0xfffffff0u;
	check("stranger starts at stage 0", peerrep_stage(IP_A, PORT, now) == PEERREP_STAGE_NONE);
	check("event 1", peerrep_note(IP_A, PORT, "cycle-contradiction", now) == 0);
	now += 1u;
	check("event 2", peerrep_note(IP_A, PORT, "cycle-contradiction", now) == 0);
	check("ask allowed before threshold", peerrep_ask_ok(IP_A, PORT, now) == 1);
	now += 1u;
	check("event 3 raises temporary DISTRUST",
	      peerrep_note(IP_A, PORT, "cycle-contradiction", now) == PEERREP_STAGE_DISTRUST);
	{
		uint32_t expires = now + PEERREP_DISTRUST_TTL_MS;
		check("DISTRUST active immediately", peerrep_ask_ok(IP_A, PORT, now) == 0);
		check("DISTRUST active one tick before expiry",
		      peerrep_stage(IP_A, PORT, expires - 1u) == PEERREP_STAGE_DISTRUST);
		check("DISTRUST expires exactly at 15m and requests resume",
		      peerrep_ask_ok(IP_A, PORT, expires) == 1 &&
		      peerrep_stage(IP_A, PORT, expires) == PEERREP_STAGE_NONE);
		check("push remains allowed after DISTRUST expiry",
		      peerrep_push_ok(IP_A, PORT, expires) == 1);
		now = expires;
	}

	/* Old lifetime events must not re-arm the ladder. Fresh events are needed. */
	for (i = 1; i <= 10; i++) {
		int got;
		now += 1u;
		got = peerrep_note(IP_A, PORT, "cycle-contradiction", now);
		if (i == 1 || i == 2)
			check("post-expiry re-entry does not reuse old lifetime count", got == 0);
		if (i == 3)
			check("three new events re-enter at DISTRUST", got == PEERREP_STAGE_DISTRUST);
		if (i == 6)
			check("six new events escalate to ISOLATE", got == PEERREP_STAGE_ISOLATE);
		if (i == 10)
			check("ten new events reach legacy BAN threshold as temporary isolate",
			      got == PEERREP_STAGE_BAN);
	}
	check("stage 3 refuses cache pushes temporarily",
	      peerrep_push_ok(IP_A, PORT, now) == 0);
	check("stage 3 is still a request pause, not a persisted ban",
	      peerrep_ask_ok(IP_A, PORT, now) == 0);

	printf("-- 3. isolated pushes cannot renew TTL; fresh evidence can --\n");
	{
		uint32_t first_expiry = now + PEERREP_ISOLATE_TTL_MS;
		check("legacy BAN threshold remains isolated before expiry",
		      peerrep_push_ok(IP_A, PORT, first_expiry - 1u) == 0);
		check("a gated push is diagnostic only and cannot extend the TTL",
		      peerrep_note(IP_A, PORT, "push-while-isolated", first_expiry - 100u) == 0 &&
		      peerrep_stage(IP_A, PORT, first_expiry - 1u) == PEERREP_STAGE_BAN);
		check("push gate reopens at the original 60m deadline",
		      peerrep_push_ok(IP_A, PORT, first_expiry) == 1 &&
		      peerrep_stage(IP_A, PORT, first_expiry) == PEERREP_STAGE_NONE);
		check("one post-expiry event cannot re-arm from lifetime history",
		      peerrep_note(IP_A, PORT, "cycle-contradiction", first_expiry + 1u) == 0 &&
		      peerrep_stage(IP_A, PORT, first_expiry + 1u) == PEERREP_STAGE_NONE);
		now = first_expiry + 1u;
		for (i = 0; i < 10; i++) {
			now += 1u;
			peerrep_note(IP_A, PORT, "cycle-contradiction", now);
		}
		check("fresh evidence can independently re-enter temporary isolation",
		      peerrep_stage(IP_A, PORT, now) == PEERREP_STAGE_BAN);
		{
			uint32_t first_deadline = now + PEERREP_ISOLATE_TTL_MS;
			uint32_t renewed_at = first_deadline - 100u;
			uint32_t renewed_deadline = renewed_at + PEERREP_ISOLATE_TTL_MS;
			check("confirmed new contradiction renews the bounded TTL",
			      peerrep_note(IP_A, PORT, "cycle-contradiction", renewed_at) == 0 &&
			      peerrep_stage(IP_A, PORT, first_deadline) == PEERREP_STAGE_BAN);
			check("renewal still expires at its own deadline",
			      peerrep_push_ok(IP_A, PORT, renewed_deadline - 1u) == 0 &&
			      peerrep_push_ok(IP_A, PORT, renewed_deadline) == 1 &&
			      peerrep_stage(IP_A, PORT, renewed_deadline) == PEERREP_STAGE_NONE);
			now = renewed_deadline + 1u;
		}
	}

	printf("-- 4. persistence stores history, never an active stage --\n");
	/* Rebuild a live stage, then prove restart cannot revive it. */
	for (i = 1; i <= 10; i++) {
		now += 1u;
		peerrep_note(IP_A, PORT, "cycle-contradiction", now);
	}
	check("stage is active before persistence test",
	      peerrep_stage(IP_A, PORT, now) == PEERREP_STAGE_BAN);
	peerrep_save();
	{
		FILE *f = xopen("/tmp/pr-unit/rep", "r");
		char line[256], key[32], reason[32];
		int stored_stage = -1, stored_events = -1;
		(void)fgets(line, sizeof(line), f); /* comment */
		(void)fgets(line, sizeof(line), f);
		fclose(f);
		if (sscanf(line, "%31s %d %d %31s", key, &stored_stage,
		           &stored_events, reason) != 4) stored_stage = -1;
		check("saved file keeps diagnostic count but writes stage 0",
		      stored_stage == PEERREP_STAGE_NONE && stored_events > 0);
	}
	peerrep_defaults();
	peerrep_configure(1, "/tmp/pr-unit/rep", 3, 6, 10);
	check("reload finds the history row", peerrep_load() == 1);
	check("restart does not restore an active stage",
	      peerrep_stage(IP_A, PORT, now) == PEERREP_STAGE_NONE);
	check("restart reopens request and push gates",
	      peerrep_ask_ok(IP_A, PORT, now) == 1 &&
	      peerrep_push_ok(IP_A, PORT, now) == 1);
	check("one post-restart bad event cannot reuse lifetime count",
	      peerrep_note(IP_A, PORT, "cycle-contradiction", now + 1u) == 0 &&
	      peerrep_stage(IP_A, PORT, now + 1u) == PEERREP_STAGE_NONE);
	{
		uint32_t ip; uint16_t port; int stage, events; char reason[32];
		check("web/telnet record read succeeds",
		      peerrep_get(0, now + 1u, &ip, &port, &stage, &events,
		                  reason, sizeof(reason)) == 1);
		check("history identity and events survive, active stage does not",
		      ip == IP_A && port == PORT && stage == PEERREP_STAGE_NONE &&
		      events > 0 && reason[0] != 0);
	}

	printf("-- 5. legacy rows load as history only; corrupt rows are skipped --\n");
	{
		FILE *f = xopen("/tmp/pr-unit/rep", "w");
		fprintf(f, "garbage\n");
		fprintf(f, "999.999.1.1:70000 3 10 bad-port\n");
		/* A valid old persisted ISOLATE row must not revive the action. */
		fprintf(f, "%u.%u.%u.%u:%u 2 6 cycle-contradiction\n",
			0xFF&(IP_B), 0xFF&(IP_B>>8), 0xFF&(IP_B>>16),
			0xFF&(IP_B>>24), PORT);
		fclose(f);
	}
	peerrep_defaults();
	peerrep_configure(1, "/tmp/pr-unit/rep", 3, 6, 10);
	check("only the good legacy row loads", peerrep_load() == 1);
	check("legacy stage 2 is history, not active quarantine",
	      peerrep_stage(IP_B, PORT, now) == PEERREP_STAGE_NONE &&
	      peerrep_ask_ok(IP_B, PORT, now) == 1 &&
	      peerrep_push_ok(IP_B, PORT, now) == 1);

	printf("-- 6. ISOLATE also expires at the specified 60m boundary --\n");
	peerrep_defaults();
	peerrep_configure(1, "/tmp/pr-unit/rep", 1, 2, 3);
	{
		uint32_t start = now + 10u;
		uint32_t isolate_at = start + 1u;
		uint32_t expires = isolate_at + PEERREP_ISOLATE_TTL_MS;
		check("first event reaches DISTRUST", peerrep_note(IP_B, PORT, "bad", start) == PEERREP_STAGE_DISTRUST);
		check("second event reaches ISOLATE", peerrep_note(IP_B, PORT, "bad", start + 1u) == PEERREP_STAGE_ISOLATE);
		check("ISOLATE blocks immediately", peerrep_push_ok(IP_B, PORT, start + 1u) == 0);
		check("ISOLATE expires after 60m", peerrep_push_ok(IP_B, PORT, expires) == 1 &&
		      peerrep_stage(IP_B, PORT, expires) == PEERREP_STAGE_NONE);
	}

	printf("-- 7. derived path, threshold ordering, per-peer scope --\n");
	peerrep_defaults();
	peerrep_configure(1, "", 3, 6, 10);
	remove("/tmp/pr-unit/multics.peers");
	check("derived path is safe when file is absent", peerrep_load() == 0);
	{
		FILE *f = xopen("/tmp/pr-unit/multics.peers", "w");
		fprintf(f, "%u.%u.%u.%u:%u 1 3 cycle-contradiction\n",
			0xFF&(IP_A), 0xFF&(IP_A>>8), 0xFF&(IP_A>>16),
			0xFF&(IP_A>>24), PORT);
		fclose(f);
	}
	check("derived file loads as history", peerrep_load() == 1);
	check("derived-file stage is not active", peerrep_stage(IP_A, PORT, now) == PEERREP_STAGE_NONE);
	check("other IP and port are unaffected",
	      peerrep_ask_ok(IP_B, PORT, now) == 1 &&
	      peerrep_push_ok(IP_A, 1, now) == 1);

	peerrep_configure(1, "/tmp/pr-unit/rep", 0, 99, 0);
	peerrep_threshold_set('D', 0);
	peerrep_thresholds(&d, &iso, &ban);
	check("thresholds remain positive and ordered", d >= 1 && iso > d && ban > iso);

	wipe();
	printf("\n== %d/%d passed, %d failed ==\n", checks - fails, checks, fails);
	return fails ? 1 : 0;
}
