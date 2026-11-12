/*
 * R48 D-3 fault-injection boundaries.
 * 1. Distributed Sybil (distinct IPs): independence does NOT defeat it.
 *    Documents the boundary -- independence only collapses single-IP
 *    floods; a truly distributed flood still wins on weight. Quarantine
 *    and weighting are the remaining defenses (tested separately).
 * 2. Slow drip (10 keys/sec, below the 20/sec storm threshold): no
 *    storm HOLD. Proves the guard does not false-positive on slow
 *    legitimate traffic.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "../src/cwconsensus.h"

static int fails = 0, checks = 0;
static void chk(int ok, const char *what, long got)
{
	checks++;
	if (!ok) { fails++; printf("  [FAIL] %s (got %ld)\n", what, got); }
	else printf("  [ ok ] %s\n", what);
}

static const uint8_t K_REAL[16] = {0x10,0x20,0x30,0x60,0x40,0x50,0x60,0xF0,0x70,0x80,0x90,0x80,0xA0,0xB0,0xC0,0x10};
static const uint8_t K_FAKE[16] = {0x10,0x30,0x20,0x60,0x40,0x50,0x60,0xF0,0x70,0x80,0x90,0x80,0xA0,0xB0,0xC0,0x10};

#define CAID 0x1884
#define SID 0x64
#define PROVID 0x111111
#define HASH 0x5B222222u
#define TAG 0x80

int main(void)
{
	const uint8_t *loser = NULL;
	int v, i;

	printf("== cwconsensus D-3 boundaries (distributed Sybil + slow drip) ==\n");

	/* -- 1. Distributed Sybil: distinct IPs defeat independence -- */
	cwconsensus_init();
	cwconsensus_set_independence(1);
	cwconsensus_set_quarantine(0);
	cwconsensus_set_storm_guard(0);
	cwconsensus_set_rotation_freeze(0);
	cwconsensus_set_min_sources(2);
	for (i = 0; i < 8; i++)
		v = cwconsensus_verdict_group(CAID, SID, PROVID, 1, HASH, TAG,
					      K_FAKE, 1000 + i, 1000 + i, 1, 1000 + i, 5000, &loser);
	v = cwconsensus_verdict_group(CAID, SID, PROVID, 1, HASH, TAG,
				      K_REAL, 2001, 2001, 1, 2001, 5000, &loser);
	chk(v == CWC_REFUSE, "distributed Sybil: honest refused despite independence", v);
	v = cwconsensus_verdict_group(CAID, SID, PROVID, 1, HASH, TAG,
				      K_REAL, 2002, 2002, 1, 2002, 5000, &loser);
	chk(v == CWC_REFUSE, "distributed Sybil: second honest also refused", v);

	/* -- 2. Slow drip: 10 keys/sec never trips the storm guard -- */
	cwconsensus_init();
	cwconsensus_set_independence(0);
	cwconsensus_set_quarantine(0);
	cwconsensus_set_storm_guard(1);
	cwconsensus_set_rotation_freeze(0);
	cwconsensus_set_min_sources(2);
	{
		int held = 0;
		for (i = 0; i < 20; i++) {
			int peer = (i % 2) ? 102 : 101;
			v = cwconsensus_verdict(CAID, SID, PROVID, 1, HASH, TAG,
						K_REAL, peer, 1, 1000 + i * 100, 5000, &loser);
			if (i >= 2 && v == CWC_HOLD) held++;
		}
		chk(held == 0, "slow drip (10/s): no storm hold after corroboration", held);
	}

	printf("== %d/%d passed, %d failed ==\n", checks - fails, checks, fails);
	return fails ? 1 : 0;
}
