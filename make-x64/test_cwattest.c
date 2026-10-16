/*
 * TASK R30 (D81) -- cwattest unit. The table is pure, so every rule
 * from D81 is exercised directly: distinct-client counting, the
 * threshold crossing, one-client-cannot-file, TTL expiry of both
 * suspicions and filings, channel/key isolation, LRU bounding and
 * the evidence sentences. Checks are printed in the house style.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "../src/cwattest.h"

static int fails = 0, checks = 0;
static void chk(int ok, const char *what, long got)
{
	checks++;
	if (!ok) { fails++; printf("  [FAIL] %s (got %ld)\n", what, got); }
	else printf("  [ ok ] %s\n", what);
}

static const uint8_t K1[16] = {0x9F,0x3A,0xC7,0xA0,0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xAA,0xBB,0xCC};
static const uint8_t K2[16] = {0x9F,0x3A,0xC7,0xA0,0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xAA,0xBB,0xDD};

#define CAID 0x1884
#define PROVID 0
#define SID 0x64
#define TTL CWATTEST_TTL_MS

int main(void)
{
	struct cwattest_table t;
	struct cwattest_ev ev;
	struct cwattest_hit_ev hev;
	char line[512];
	int v;

	printf("== cwattest unit -- TASK R30 (D81) ==\n");

	/* -- 1. one client, however often, never files (GR3) -- */
	cwattest_init(&t);
	v = cwattest_note(&t, K1, CAID, PROVID, SID, 3, 77, 1001, 2, 10000, TTL, &ev);
	chk(v==1 && ev.distinct==1 && !ev.crossed && !ev.filed,
	    "first attestation counted, not filed", v);
	v = cwattest_note(&t, K1, CAID, PROVID, SID, 3, 77, 1001, 2, 11000, TTL, &ev);
	chk(v==0 && ev.distinct==1, "same client again counts nothing", v);
	v = cwattest_note(&t, K1, CAID, PROVID, SID, 3, 77, 1001, 2, 12000, TTL, &ev);
	chk(v==0 && !ev.filed, "one client alone never files", v);
	chk(cwattest_hit(&t, K1, CAID, PROVID, SID, 12000, TTL, &hev)==0,
	    "unfiled key is not refused", 0);
	chk(cwattest_count(&t, 12000, TTL)==0, "nothing filed yet",
	    cwattest_count(&t, 12000, TTL));

	/* -- 2. a second DISTINCT client crosses and files -- */
	v = cwattest_note(&t, K1, CAID, PROVID, SID, 3, 77, 1002, 2, 13000, TTL, &ev);
	chk(v==2 && ev.crossed==1 && ev.filed==1,
	    "second distinct client files the key", v);
	chk(cwattest_hit(&t, K1, CAID, PROVID, SID, 14000, TTL, &hev)==1,
	    "filed key is refused at the gate", 0);
	chk(hev.nclients==2 && hev.origin_id==77,
	    "hit evidence names the attesters and the origin", hev.nclients);
	chk(hev.fired==1, "first refusal earns its line", hev.fired);
	v = cwattest_hit(&t, K1, CAID, PROVID, SID, 15000, TTL, &hev);
	chk(v==1 && hev.fired==0 && hev.hits==2,
	    "refusals are counted, the line is rate-limited", (long)hev.hits);

	/* -- 3. identity is the bytes + provider, not the SID (cwneg rule) -- */
	chk(cwattest_hit(&t, K1, CAID, PROVID, 0x99, 16000, TTL, &hev)==1,
	    "same bytes on a sister service are still filed", 0);
	chk(cwattest_hit(&t, K2, CAID, PROVID, SID, 16000, TTL, &hev)==0,
	    "different bytes are not filed", 0);
	chk(cwattest_hit(&t, K1, 0x0100, PROVID, SID, 16000, TTL, &hev)==0,
	    "same bytes on another CA are not filed", 0);
	chk(cwattest_hit(&t, K1, CAID, 0x55, SID, 16000, TTL, &hev)==0,
	    "same bytes on another provider are not filed", 0);

	/* -- 4. the filing lapses (attestation heals, proof never does) -- */
	chk(cwattest_hit(&t, K1, CAID, PROVID, SID, 13000+TTL, TTL, &hev)==1,
	    "filing still live at the TTL edge", 0);
	chk(cwattest_hit(&t, K1, CAID, PROVID, SID, 13000+TTL+1, TTL, &hev)==0,
	    "filing lapsed one tick later", 0);
	chk(cwattest_count(&t, 13000+TTL+1, TTL)==0,
	    "lapsed filing no longer counted", 0);

	/* -- 5. after the lapse the key must earn its filing anew -- */
	v = cwattest_note(&t, K1, CAID, PROVID, SID, 3, 77, 1001, 2, 400000, TTL, &ev);
	chk(v==1 && !ev.filed, "post-lapse attestation starts over", v);
	v = cwattest_note(&t, K1, CAID, PROVID, SID, 3, 77, 1002, 2, 401000, TTL, &ev);
	chk(v==2 && ev.crossed, "and can file again on fresh attestation", v);

	/* -- 6. slow-drip attestations outside one TTL never assemble -- */
	cwattest_init(&t);
	cwattest_note(&t, K1, CAID, PROVID, SID, 3, 77, 1001, 2, 0, TTL, &ev);
	v = cwattest_note(&t, K1, CAID, PROVID, SID, 3, 77, 1002, 2, TTL+1, TTL, &ev);
	chk(v==1 && !ev.filed, "second attestation after the window starts over", v);

	/* -- 7. higher thresholds need every distinct client -- */
	cwattest_init(&t);
	cwattest_note(&t, K1, CAID, PROVID, SID, 3, 77, 1, 3, 0, TTL, &ev);
	cwattest_note(&t, K1, CAID, PROVID, SID, 3, 77, 2, 3, 1000, TTL, &ev);
	v = cwattest_note(&t, K1, CAID, PROVID, SID, 3, 77, 2, 3, 2000, TTL, &ev);
	chk(v==0 && !ev.filed, "threshold 3: two clients are not enough", v);
	v = cwattest_note(&t, K1, CAID, PROVID, SID, 3, 77, 3, 3, 3000, TTL, &ev);
	chk(v==3 && ev.crossed, "threshold 3: the third distinct client files", v);

	/* -- 8. threshold below 2 arms nothing (the caller's OFF) -- */
	cwattest_init(&t);
	v = cwattest_note(&t, K1, CAID, PROVID, SID, 3, 77, 1, 1, 0, TTL, &ev);
	chk(v==0 && cwattest_count(&t, 0, TTL)==0, "required<2 records nothing", v);

	/* -- 9. the table is bounded: LRU eviction keeps the newest -- */
	cwattest_init(&t);
	{
		int i;
		uint8_t k[16];
		memcpy(k, K1, 16);
		for (i = 0; i < CWATTEST_SLOTS + 4; i++) {
			k[15] = (uint8_t)i;
			cwattest_note(&t, k, CAID, PROVID, SID, 3, 77, 1, 2, (uint32_t)(1000*i), TTL, &ev);
			cwattest_note(&t, k, CAID, PROVID, SID, 3, 77, 2, 2, (uint32_t)(1000*i+1), TTL, &ev);
		}
		k[15] = (uint8_t)(CWATTEST_SLOTS + 3);
		chk(cwattest_hit(&t, k, CAID, PROVID, SID, 1000*(CWATTEST_SLOTS+4), TTL, &hev)==1,
		    "newest filing survives eviction", 0);
		k[15] = 0;
		chk(cwattest_hit(&t, k, CAID, PROVID, SID, 1000*(CWATTEST_SLOTS+4), TTL, &hev)==0,
		    "oldest filing was evicted, not the table wedged", 0);
	}

	/* -- 10. the evidence sentences state the whole chain -- */
	cwattest_init(&t);
	cwattest_note(&t, K1, CAID, PROVID, SID, 3, 77, 1, 2, 0, TTL, &ev);
	cwattest_note(&t, K1, CAID, PROVID, SID, 3, 77, 2, 2, 1000, TTL, &ev);
	v = cwattest_format_filed(&ev, 2, TTL, line, (int)sizeof(line));
	chk(v>0 && strstr(line, "CW ATTEST FILED") && strstr(line, "2 independent clients")
	    && strstr(line, "not a GR3 proof"), "filing sentence", v);
	v = cwattest_hit(&t, K1, CAID, PROVID, SID, 2000, TTL, &hev);
	v = cwattest_format(&hev, TTL, line, (int)sizeof(line));
	chk(v>0 && strstr(line, "CW ATTEST:") && strstr(line, "nothing was scored"),
	    "refusal sentence", v);

	/* -- 11. bad arguments are refused, never crash -- */
	v = cwattest_note(NULL, K1, CAID, PROVID, SID, 3, 77, 1, 2, 0, TTL, &ev);
	chk(v==0, "NULL table refused", v);
	v = cwattest_hit(&t, NULL, CAID, PROVID, SID, 0, TTL, &hev);
	chk(v==0, "NULL key refused", v);

	printf("== %d/%d passed, %d failed ==\n", checks-fails, checks, fails);
	return fails ? 1 : 0;
}
