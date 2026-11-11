/*
 * R48 D-3 fault injection -- Sybil flood vs honest minority.
 * 47 Sybil peers behind one host IP push a fake key; 3 honest peers
 * behind distinct IPs push the real key. With independence ON the
 * Sybils count as one source and the honest key wins; with stock
 * distinct-peer counting the flood wins. This is the 3-honest-vs-
 * 47-Sybil gate from the D-3 fault-injection list, at unit level.
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
#define HASH 0x5B111111u
#define TAG 0x80

int main(void)
{
	const uint8_t *loser = NULL;
	int v, i;

	printf("== cwconsensus D-3 Sybil fault injection (3 honest vs 47 Sybil) ==\n");

	/* -- Stock (independence OFF): the flood wins -- */
	cwconsensus_init();
	cwconsensus_set_independence(0);
	cwconsensus_set_quarantine(0);
	cwconsensus_set_storm_guard(0);
	cwconsensus_set_rotation_freeze(0);
	cwconsensus_set_min_sources(2);
	/* Sybils first (fake key, distinct peerids, same group ignored). */
	for (i = 0; i < 8; i++)
		v = cwconsensus_verdict_group(CAID, SID, PROVID, 1, HASH, TAG,
					      K_FAKE, 1000 + i, 0x0A000001u, 1, 1000 + i, 5000, &loser);
	/* Honest minority (real key, distinct groups). */
	v = cwconsensus_verdict_group(CAID, SID, PROVID, 1, HASH, TAG,
				      K_REAL, 2001, 0x0A000002u, 1, 2001, 5000, &loser);
	chk(v == CWC_REFUSE, "stock: honest key refused against flood", v);
	v = cwconsensus_verdict_group(CAID, SID, PROVID, 1, HASH, TAG,
				      K_REAL, 2002, 0x0A000003u, 1, 2002, 5000, &loser);
	chk(v == CWC_REFUSE, "stock: second honest key also refused", v);

	/* -- Independence ON: Sybils count once, honest win -- */
	cwconsensus_init();
	cwconsensus_set_independence(1);
	cwconsensus_set_quarantine(0);
	cwconsensus_set_storm_guard(0);
	cwconsensus_set_rotation_freeze(0);
	cwconsensus_set_min_sources(2);
	for (i = 0; i < 8; i++)
		v = cwconsensus_verdict_group(CAID, SID, PROVID, 1, HASH, TAG,
					      K_FAKE, 1000 + i, 0x0A000001u, 1, 1000 + i, 5000, &loser);
	v = cwconsensus_verdict_group(CAID, SID, PROVID, 1, HASH, TAG,
				      K_REAL, 2001, 0x0A000002u, 1, 2001, 5000, &loser);
	chk(v == CWC_REFUSE, "independence: first honest key loses timing tie (1v1)", v);
	v = cwconsensus_verdict_group(CAID, SID, PROVID, 1, HASH, TAG,
				      K_REAL, 2002, 0x0A000003u, 1, 2002, 5000, &loser);
	chk(v == CWC_DELIVER, "independence: honest key delivers (2v1)", v);
	chk(loser && !memcmp(loser, K_FAKE, 16), "independence: fake key reported as loser", loser ? 1 : 0);

	printf("== %d/%d passed, %d failed ==\n", checks - fails, checks, fails);
	return fails ? 1 : 0;
}
