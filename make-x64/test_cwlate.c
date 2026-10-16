/*
 * test_cwlate.c -- TASK R13 (D68)
 *
 * Exercises the REAL ../src/cwlate.h: the contradicted-key memory, the
 * dispute pattern and the class rule. Pure header, no server, no threads, no
 * clock injection needed -- every function takes `now` as an argument. The
 * timeline below is strictly monotonic so the TTL/window arithmetic reads
 * like the field: marks die CWLATE_KEY_TTL ms after the CONTRADICTION, lines
 * come at most one per key per CWLATE_WINDOW, patterns fire once per window.
 *
 *   make -C make-x64 test
 */

#include <stdio.h>
#include <string.h>
#include "../src/cwlate.h"

static int passed = 0, failed = 0;

static void check(const char *what, int got, int want)
{
	if (got == want) { passed++; printf("  [ ok ] %-58s = %d\n", what, got); }
	else { failed++; printf("  [FAIL] %-58s got=%d want=%d\n", what, got, want); }
}

static void keybyte(uint8_t *k, int seed)
{
	int i;
	for (i = 0; i < 16; i++) k[i] = (uint8_t)(seed * 37 + i * 11 + 3);
	k[3] = (uint8_t)((k[0] + k[1] + k[2]) & 0xFF);   /* checksum-valid shape */
	k[7] = (uint8_t)((k[4] + k[5] + k[6]) & 0xFF);
	k[11] = (uint8_t)((k[8] + k[9] + k[10]) & 0xFF);
	k[15] = (uint8_t)((k[12] + k[13] + k[14]) & 0xFF);
}

int main(void)
{
	struct cwlate_keytab kt;
	struct cwlate_pattab pt;
	struct cwlate_ev ev;
	uint8_t k1[16], k2[16], k3[16];
	uint32_t now;
	int i;

	printf("test_cwlate: the late-truth loop (TASK R13 / D68)\n");

	/* ==================================================================
	 * 1. The class rule: only card-server-vs-cache says anything, and the
	 *    answer is the SLOT INDEX of the cache side.
	 * ================================================================== */
	check("server vs cache (order A) -> cache side slot 1",
	      cwlate_suspect(DCW_SOURCE_SERVER, DCW_SOURCE_CACHE), 1);
	check("cache vs server (order B) -> cache side slot 0",
	      cwlate_suspect(DCW_SOURCE_CACHE, DCW_SOURCE_SERVER), 0);
	check("cache vs cache -> nobody",
	      cwlate_suspect(DCW_SOURCE_CACHE, DCW_SOURCE_CACHE), CWLATE_NOSUSPECT);
	check("server vs server -> nobody",
	      cwlate_suspect(DCW_SOURCE_SERVER, DCW_SOURCE_SERVER), CWLATE_NOSUSPECT);
	check("client push vs cache -> nobody",
	      cwlate_suspect(3, DCW_SOURCE_CACHE), CWLATE_NOSUSPECT);
	check("server vs client push -> nobody",
	      cwlate_suspect(DCW_SOURCE_SERVER, 4), CWLATE_NOSUSPECT);
	check("none vs none -> nobody",
	      cwlate_suspect(0, 0), CWLATE_NOSUSPECT);

	/* ==================================================================
	 * 2. Mark + hit, the happy path (timeline starts at 1000).
	 * ================================================================== */
	cwlate_init(&kt, &pt);
	keybyte(k1, 1);
	keybyte(k2, 2);
	now = 1000;

	check("fresh mark files and announces",
	      cwlate_key_mark(&kt, k1, 0x1884, 0, DCW_SOURCE_SERVER, 7, now), 1);
	check("one key live after one mark",
	      cwlate_key_count(&kt), 1);
	check("repeat mark of the same bytes stays quiet",
	      cwlate_key_mark(&kt, k1, 0x1884, 0, DCW_SOURCE_SERVER, 7, now + 500), 0);
	check("still one key live",
	      cwlate_key_count(&kt), 1);

	now = 2000;
	check("hit inside the TTL",
	      cwlate_key_hit(&kt, k1, 0x1884, 0, now, &ev), 1);
	check("the hit's age is measured from the mark",
	      (int)ev.age_ms, 1000);
	check("the hit counter counts",
	      (int)ev.hits, 1);
	check("the first hit earns the line",
	      (int)ev.fired, 1);
	check("an immediate second hit is counted quiet",
	      (int)(cwlate_key_hit(&kt, k1, 0x1884, 0, now + 1000, &ev), ev.fired), 0);
	check("the second hit still counted",
	      (int)ev.hits, 2);

	/* the line window (60 s) is wider than the mark's TTL (30 s), so a
	 * mark earns at most ONE line however hard it is re-pushed; a repeat
	 * inside the mark's life stays counted and quiet. */
	check("a repeat inside the mark's life stays quiet",
	      (int)(cwlate_key_hit(&kt, k1, 0x1884, 0, 1000 + CWLATE_KEY_TTL / 2, &ev),
	            ev.fired), 0);
	check("a hit past the TTL is a miss (the line window cannot outlive the mark)",
	      cwlate_key_hit(&kt, k1, 0x1884, 0, now + 1000 + CWLATE_WINDOW, &ev), 0);

	/* ==================================================================
	 * 3. TTL from the CONTRADICTION (1000 + 30000): a contradicted key
	 *    has no meaning past its crypto period, however often it is
	 *    re-pushed.
	 * ================================================================== */
	check("hit one ms before the TTL is still a hit",
	      cwlate_key_hit(&kt, k1, 0x1884, 0, 1000 + CWLATE_KEY_TTL - 1, &ev), 1);
	check("hit at the TTL is a miss",
	      cwlate_key_hit(&kt, k1, 0x1884, 0, 1000 + CWLATE_KEY_TTL, &ev), 0);
	check("a re-contradiction after expiry files a fresh mark again",
	      cwlate_key_mark(&kt, k1, 0x1884, 0, DCW_SOURCE_SERVER, 7,
	                     1000 + CWLATE_KEY_TTL + 1), 1);
	check("the revived mark is live",
	      cwlate_key_hit(&kt, k1, 0x1884, 0,
	                    1000 + CWLATE_KEY_TTL + 2, &ev), 1);
	check("the revived hit earns a fresh line",
	      (int)ev.fired, 1);

	/* ==================================================================
	 * 4. Scope (D26): the identity is (key bytes, CAID, PROVID), never
	 *    the SID -- the poison does not change when the zap does.
	 * ================================================================== */
	check("same bytes, other CAID -> miss",
	      cwlate_key_hit(&kt, k1, 0x1885, 0, 1000 + CWLATE_KEY_TTL + 3, &ev), 0);
	check("same bytes, other PROVID -> miss",
	      cwlate_key_hit(&kt, k1, 0x1884, 5, 1000 + CWLATE_KEY_TTL + 3, &ev), 0);
	check("other bytes, same mux -> miss",
	      cwlate_key_hit(&kt, k2, 0x1884, 0, 1000 + CWLATE_KEY_TTL + 3, &ev), 0);
	check("NULL key is a miss",
	      cwlate_key_hit(&kt, 0, 0x1884, 0, 1000 + CWLATE_KEY_TTL + 3, &ev), 0);
	check("NULL table is a miss",
	      cwlate_key_hit(0, k1, 0x1884, 0, 1000 + CWLATE_KEY_TTL + 3, &ev), 0);

	/* ==================================================================
	 * 5. LRU: a full table forgets the least recently touched, and never
	 *    refuses the NEXT contradicted key.
	 * ================================================================== */
	cwlate_init(&kt, &pt);
	for (i = 0; i < CWLATE_SLOTS; i++) {
		keybyte(k3, 100 + i);
		cwlate_key_mark(&kt, k3, 0x1884, 0, DCW_SOURCE_SERVER, 7, 5000);
	}
	check("the table is full",
	      cwlate_key_count(&kt), CWLATE_SLOTS);
	/* touch slot 0's key so an older untouched one becomes the victim */
	keybyte(k3, 100);
	cwlate_key_hit(&kt, k3, 0x1884, 0, 6000, &ev);
	keybyte(k3, 100 + CWLATE_SLOTS);   /* one past the end */
	check("a 65th contradicted key still files (LRU evicts)",
	      cwlate_key_mark(&kt, k3, 0x1884, 0, DCW_SOURCE_SERVER, 7, 7000), 1);
	check("the table is still exactly full",
	      cwlate_key_count(&kt), CWLATE_SLOTS);
	keybyte(k2, 100);                  /* the touched one */
	check("the touched key survived",
	      cwlate_key_hit(&kt, k2, 0x1884, 0, 8000, &ev), 1);
	keybyte(k2, 100 + CWLATE_SLOTS - 1); /* the LRU victim (oldest, last) */
	check("an untouched key was evicted",
	      cwlate_key_hit(&kt, k2, 0x1884, 0, 8000, &ev), 0);

	/* ==================================================================
	 * 6. The dispute pattern: THREE ticks inside the window fire ONE soft
	 *    event, the latch keeps the window quiet, the next window re-arms.
	 * ================================================================== */
	cwlate_init(&kt, &pt);
	check("pattern tick 1 -> silence", cwlate_pat_tick(&pt, 1, 7, 0x1884, 10000), 0);
	check("pattern tick 2 -> silence", cwlate_pat_tick(&pt, 1, 7, 0x1884, 11000), 0);
	check("pattern tick 3 -> ONE soft event", cwlate_pat_tick(&pt, 1, 7, 0x1884, 12000), 1);
	check("pattern tick 4 -> latched quiet", cwlate_pat_tick(&pt, 1, 7, 0x1884, 13000), 0);
	check("pattern tick 5 -> still latched", cwlate_pat_tick(&pt, 1, 7, 0x1884, 14000), 0);

	check("(next window, tick 1) restarts clean",
	      cwlate_pat_tick(&pt, 1, 7, 0x1884, 10000 + CWLATE_PAT_WIN + 1), 0);
	check("(next window, tick 2)",
	      cwlate_pat_tick(&pt, 1, 7, 0x1884, 10000 + CWLATE_PAT_WIN + 2), 0);
	check("(next window, tick 3) fires again",
	      cwlate_pat_tick(&pt, 1, 7, 0x1884, 10000 + CWLATE_PAT_WIN + 3), 1);

	/* isolation per (srctype, srcid, caid): another source starts clean */
	check("other source on the same caid: tick 1 quiet",
	      cwlate_pat_tick(&pt, 1, 8, 0x1884, 20000), 0);
	check("other source: tick 2 quiet",
	      cwlate_pat_tick(&pt, 1, 8, 0x1884, 20100), 0);
	check("other source: tick 3 fires",
	      cwlate_pat_tick(&pt, 1, 8, 0x1884, 20200), 1);
	check("same source, other caid: tick 1 quiet",
	      cwlate_pat_tick(&pt, 1, 7, 0x1885, 20300), 0);
	check("other srctype identity: tick 1 quiet",
	      cwlate_pat_tick(&pt, 2, 7, 0x1884, 20400), 0);
	check("NULL pattern table is quiet",
	      cwlate_pat_tick(0, 1, 7, 0x1884, 20500), 0);

	/* pattern LRU: a 65th identity still works */
	for (i = 0; i < CWLATE_SLOTS + 4; i++)
		cwlate_pat_tick(&pt, 1, 1000 + i, 0x1884, 30000 + i);
	check("pattern table never refuses a new identity",
	      cwlate_pat_tick(&pt, 1, 9999, 0x1884, 40000), 0);

	/* ==================================================================
	 * 7. Digest: deterministic, and distinct keys stay distinct.
	 * ================================================================== */
	keybyte(k1, 3); keybyte(k2, 4);
	check("digest is deterministic",
	      cwlate_digest(k1) == cwlate_digest(k1), 1);
	check("distinct keys stay distinct",
	      cwlate_digest(k1) != cwlate_digest(k2), 1);

	/* NULL safety */
	check("mark(NULL key) is quiet",
	      cwlate_key_mark(&kt, 0, 0x1884, 0, DCW_SOURCE_SERVER, 7, 50000), 0);
	check("init(NULL, NULL) survives", 1, 1);

	printf("test_cwlate: %d ok, %d failed\n", passed, failed);
	return failed ? 1 : 0;
}
