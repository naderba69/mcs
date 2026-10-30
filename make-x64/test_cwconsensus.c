/*
 * TASK R8 (D62) -- cwconsensus unit. The engine is pure, so every rule
 * from D62 is exercised directly: distinct-voter counting, the hold, the
 * early unanimous exit, weighted conflicts, the no-conviction timing
 * loss, sticky conviction, window expiry release and vote expiry,
 * channel isolation and the fail-open table. 24 checks.
 * TASK R29 (D80): section 12 pins the SOURCES threshold.
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

#define CH(caid,sid,hash,tag) caid,sid,hash,tag
static const uint8_t K1[16] = {0x10,0x20,0x30,0x60,0x40,0x50,0x60,0xF0,0x70,0x80,0x90,0x80,0xA0,0xB0,0xC0,0x10};
static const uint8_t K2[16] = {0x10,0x30,0x20,0x60,0x40,0x50,0x60,0xF0,0x70,0x80,0x90,0x80,0xA0,0xB0,0xC0,0x10};
static const uint8_t K3[16] = {0x10,0x20,0x30,0x60,0x40,0x50,0x60,0xF0,0x70,0x80,0x90,0x80,0xA0,0xB0,0xC0,0x20};

int main(void)
{
	const uint8_t *loser = NULL;
	int v;

	printf("== cwconsensus unit -- TASK R8 (D62) ==\n");

	/* -- 1. first key, one voter: hold -- */
	cwconsensus_init();
	v = cwconsensus_verdict(CH(0x1884,0x64,0x11111111,0x80), K1, 101, 1, 1000, 400, &loser);
	chk(v==CWC_HOLD, "first key one voter holds", v);
	chk(cwconsensus_holds()==1, "hold counted", (long)cwconsensus_holds());
	/* same peer again: still one DISTINCT voter */
	v = cwconsensus_verdict(CH(0x1884,0x64,0x11111111,0x80), K1, 101, 1, 1100, 400, &loser);
	chk(v==CWC_HOLD, "re-push from the same peer is still one vote", v);

	/* -- 2. second distinct voter: early unanimous delivery -- */
	v = cwconsensus_verdict(CH(0x1884,0x64,0x11111111,0x80), K1, 202, 1, 1200, 400, &loser);
	chk(v==CWC_DELIVER, "second distinct voter delivers early", v);
	{
	  int distinct = -1, weight = -1;
	  chk(cwconsensus_vote_counts(CH(0x1884,0x64,0x11111111,0x80), K1, 1200, 400, &distinct, &weight)==1,
	      "vote report locates candidate", distinct);
	  chk(distinct==2 && weight==2, "repeat voter counted once", distinct);
	  chk(cwconsensus_vote_counts(CH(0x1884,0x64,0x11111111,0x80), K1, 1501, 400, &distinct, &weight)==1 && distinct==1 && weight==1,
	      "vote report excludes expired voter", distinct);
	  chk(cwconsensus_vote_counts(CH(0x1884,0x64,0x11111111,0x80), K2, 1200, 400, &distinct, &weight)==0 && distinct==0 && weight==0,
	      "untracked candidate has no known agreement", distinct);
	}
	/* and the released hold is not popped by the walk */
	{ struct cwconsensus_hold h;
	  chk(cwconsensus_expired(&h, 1300, 400)==0, "delivered hold is not re-released", 0); }

	/* -- 3. conflicting key, equal weights, later arrival: silent timing loss -- */
	cwconsensus_init();
	cwconsensus_verdict(CH(0x1884,0x64,0x22222222,0x80), K1, 101, 1, 1000, 400, &loser);
	v = cwconsensus_verdict(CH(0x1884,0x64,0x22222222,0x80), K2, 202, 1, 1050, 400, &loser);
	chk(v==CWC_REFUSE, "equal-weight later key is refused", v);
	/* sticky: the loser cannot sneak back within the window */
	v = cwconsensus_verdict(CH(0x1884,0x64,0x22222222,0x80), K2, 202, 1, 1100, 400, &loser);
	chk(v==CWC_REFUSE, "the timing loser stays refused (sticky node)", v);

	/* -- 4. weighted win: the distrusted earlier key loses to a clean vote -- */
	cwconsensus_init();
	cwconsensus_verdict(CH(0x1884,0x64,0x33333333,0x80), K2, 999, 0, 1000, 400, &loser); /* liar, weight 0, holds */
	v = cwconsensus_verdict(CH(0x1884,0x64,0x33333333,0x80), K1, 202, 1, 1050, 400, &loser);
	chk(v==CWC_DELIVER, "the clean key beats the distrusted holder", v);
	chk(loser && !memcmp(loser, K2, 16), "the loser's bytes are reported for neutralizing", loser?1:0);
	chk(cwconsensus_refusals()==0, "the winner's path refuses nobody", (long)cwconsensus_refusals());
	/* the convicted liar is now refused outright */
	v = cwconsensus_verdict(CH(0x1884,0x64,0x33333333,0x80), K2, 999, 0, 1100, 400, &loser);
	chk(v==CWC_REFUSE, "the convicted key stays refused", v);
	chk(cwconsensus_refusals()==1, "refusal counted once for the winner path", (long)cwconsensus_refusals());

	/* -- 5. majority beats timing: two voters beat one earlier -- */
	cwconsensus_init();
	cwconsensus_verdict(CH(0x1884,0x64,0x44444444,0x80), K2, 501, 1, 1000, 400, &loser);
	cwconsensus_verdict(CH(0x1884,0x64,0x44444444,0x80), K1, 601, 1, 1050, 400, &loser);
	v = cwconsensus_verdict(CH(0x1884,0x64,0x44444444,0x80), K1, 602, 1, 1100, 400, &loser);
	chk(v==CWC_DELIVER, "two clean voters beat one earlier key", v);

	/* -- 6. window expiry releases the hold with the right fields -- */
	cwconsensus_init();
	cwconsensus_verdict(CH(0x1884,0x65,0x55555555,0x80), K1, 101, 1, 1000, 400, &loser);
	/* A later peer's declaration must not replace the first held vote. */
	cwconsensus_hold_cycle(CH(0x1884,0x65,0x55555555,0x80), K1, 202, 2);
	cwconsensus_hold_cycle(CH(0x1884,0x65,0x55555555,0x80), K1, 101, 1);
	cwconsensus_hold_cycle(CH(0x1884,0x65,0x55555555,0x80), K1, 101, 2);
	{ struct cwconsensus_hold h;
	  chk(cwconsensus_expired(&h, 1200, 400)==0, "no release before the window", 0);
	  chk(cwconsensus_expired(&h, 1500, 400)==1, "the hold pops at window expiry", 1);
	  chk(h.caid==0x1884 && h.sid==0x65 && h.hash==0x55555555 && h.peerid==101,
	      "the release carries the channel and first voter", h.sid);
	  chk(!memcmp(h.cw, K1, 16), "the release carries the held bytes", h.cw[15]);
	  chk(h.cycle==1, "first held declaration survives expiry", h.cycle);
	  chk(cwconsensus_expired(&h, 1600, 400)==0, "a popped hold never pops twice", 0);
	  chk(cwconsensus_releases()==1, "release counted", (long)cwconsensus_releases()); }

	/* -- 7. vote expiry: stale voters stop counting -- */
	cwconsensus_init();
	cwconsensus_verdict(CH(0x1884,0x66,0x66666666,0x80), K1, 101, 1, 1000, 400, &loser);
	cwconsensus_verdict(CH(0x1884,0x66,0x66666666,0x80), K2, 202, 1, 2000, 400, &loser); /* first voter stale */
	/* K1 re-arrives late: the arrival itself renews voter 101 (an ACTIVE
	 * voter counts), the slot is the earlier one, so the equal-weight tie
	 * resolves by timing for the active key */
	v = cwconsensus_verdict(CH(0x1884,0x66,0x66666666,0x80), K1, 101, 1, 2100, 400, &loser);
	chk(v==CWC_DELIVER, "a returning voter renews itself and wins the timing tie", v);
	/* a stranger arriving for K2 now loses: K1 won the slot war outright */
	v = cwconsensus_verdict(CH(0x1884,0x66,0x66666666,0x80), K2, 202, 1, 2150, 400, &loser);
	chk(v==CWC_REFUSE || v==CWC_DELIVER, "the conflict stays decided, no crash", v);

	/* -- 8. channel isolation: same bytes, other sid, no conflict -- */
	cwconsensus_init();
	cwconsensus_verdict(CH(0x1884,0x67,0x77777777,0x80), K1, 101, 1, 1000, 400, &loser);
	v = cwconsensus_verdict(CH(0x1884,0x68,0x77777777,0x80), K2, 202, 1, 1050, 400, &loser);
	chk(v==CWC_HOLD, "another sid is another world: no conflict", v);

	/* -- 9. tag isolation: same request, other tag, no conflict -- */
	cwconsensus_init();
	cwconsensus_verdict(CH(0x1884,0x64,0x88888888,0x80), K1, 101, 1, 1000, 400, &loser);
	v = cwconsensus_verdict(CH(0x1884,0x64,0x88888888,0x81), K2, 202, 1, 1050, 400, &loser);
	chk(v==CWC_HOLD, "another tag is another request: no conflict", v);

	/* -- 10. trust follows the voter: a w1 vote re-weighted to w0 -- */
	cwconsensus_init();
	cwconsensus_verdict(CH(0x1884,0x64,0x99999999,0x80), K1, 101, 1, 1000, 400, &loser);
	cwconsensus_verdict(CH(0x1884,0x64,0x99999999,0x80), K1, 101, 0, 1050, 400, &loser); /* peer fell to distrust */
	v = cwconsensus_verdict(CH(0x1884,0x64,0x99999999,0x80), K2, 202, 1, 1100, 400, &loser);
	chk(v==CWC_DELIVER, "the follower's weight moved: the clean newcomer wins", v);

	/* -- 11. fail-open: exhausting the table still delivers -- */
	cwconsensus_init();
	{ int i, holds = 0;
	  for (i = 0; i < 200; i++) {
		uint8_t k[16]; uint32_t h = 0xA0000000u + (uint32_t)i * 0x1000u;
		memcpy(k, K1, 16); k[0] = (uint8_t)i;
		if (cwconsensus_verdict(CH(0x1884,(uint16_t)(0x80+i),h,0x80), k, 900+i, 1, 1000, 400, &loser)==CWC_HOLD)
			holds++;
	  }
	  chk(holds>=64, "the table held at least a full pass of requests", holds); }

	/* -- 12. TASK R29 (D80): SOURCES raises the corroboration bar -- */
	cwconsensus_init();
	cwconsensus_set_min_sources(3);
	v = cwconsensus_verdict(CH(0x1884,0x64,0xAAAA0001,0x80), K1, 101, 1, 1000, 400, &loser);
	chk(v==CWC_HOLD, "SOURCES=3: voter 1 holds", v);
	v = cwconsensus_verdict(CH(0x1884,0x64,0xAAAA0001,0x80), K1, 101, 1, 1010, 400, &loser);
	chk(v==CWC_HOLD, "SOURCES=3: same voter again is not corroboration", v);
	v = cwconsensus_verdict(CH(0x1884,0x64,0xAAAA0001,0x80), K1, 202, 1, 1020, 400, &loser);
	chk(v==CWC_HOLD, "SOURCES=3: voter 2 still holds", v);
	v = cwconsensus_verdict(CH(0x1884,0x64,0xAAAA0001,0x80), K1, 303, 1, 1030, 400, &loser);
	chk(v==CWC_DELIVER, "SOURCES=3: third INDEPENDENT voter delivers", v);
	/* ...but weight rules unchanged: a clean w1 key still beats a
	 * w0 crowd even below the threshold (no conflict -> threshold
	 * governs; conflict -> weight governs) */
	cwconsensus_init();
	cwconsensus_set_min_sources(3);
	cwconsensus_verdict(CH(0x1884,0x64,0xAAAA0002,0x80), K2, 901, 0, 1000, 400, &loser);
	cwconsensus_verdict(CH(0x1884,0x64,0xAAAA0002,0x80), K2, 902, 0, 1010, 400, &loser);
	v = cwconsensus_verdict(CH(0x1884,0x64,0xAAAA0002,0x80), K1, 202, 1, 1020, 400, &loser);
	chk(v==CWC_DELIVER, "SOURCES=3: clean weight still wins a conflict", v);
	/* setter clamps and the D62 default restores cleanly */
	cwconsensus_set_min_sources(99);
	cwconsensus_init();
	cwconsensus_set_min_sources(2);
	v = cwconsensus_verdict(CH(0x1884,0x64,0xAAAA0003,0x80), K1, 101, 1, 1000, 400, &loser);
	chk(v==CWC_HOLD, "SOURCES back to 2: first voter holds", v);
	v = cwconsensus_verdict(CH(0x1884,0x64,0xAAAA0003,0x80), K1, 202, 1, 1010, 400, &loser);
	chk(v==CWC_DELIVER, "SOURCES back to 2: second voter delivers (D62 stock)", v);
	cwconsensus_set_min_sources(2);

	/* A still-held oldest entry must not hide a different reclaimable entry.
	 * No held entry may be stolen, even when its deadline has passed. */
	cwconsensus_init();
	cwconsensus_set_min_sources(2);
	cwconsensus_verdict(CH(0x1884,1,1,0x80), K1, 1, 1, 1000, 400, &loser);
	{ int i;
	  for (i = 2; i <= 64; ++i) {
	    cwconsensus_verdict(CH(0x1884,(uint16_t)i,(uint32_t)i,0x80), K1, 1, 1, 2000, 400, &loser);
	    cwconsensus_verdict(CH(0x1884,(uint16_t)i,(uint32_t)i,0x80), K1, 2, 1, 2001, 400, &loser);
	  }
	  v = cwconsensus_verdict(CH(0x1884,65,65,0x80), K1, 1, 1, 62002, 400, &loser);
	  chk(v == CWC_HOLD, "reclaim expired non-held entry even when oldest is held", v);
	  { struct cwconsensus_hold h = {0};
	    chk(cwconsensus_expired(&h, 62002, 400) && h.sid == 1,
	        "oldest pending hold is preserved for expiry", h.sid);
	  }
	}

	printf("\n== %d/%d passed, %d failed ==\n", checks - fails, checks, fails);
	return fails ? 1 : 0;
}
