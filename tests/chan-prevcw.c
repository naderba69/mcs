/*
 * Band 115 unit package: the per-channel last-served-CW memory.
 *
 * Pinned properties (each one is a field conclusion from the 2026-11-19
 * 03:35 snapshot, where 604333 refusals ALL had no prevcw and learnable=0):
 *   1. a served key becomes the channel reference within TTL, and nothing
 *      else does -- the put hook is only reachable on the gate-pass path;
 *   2. TTL expiry is exact (<=ttl visible, >ttl invisible): a 3.5h session
 *      must not judge tonight's key against this morning's;
 *   3. the estimator mapping (second-half stable => CW0CYCLE, first-half
 *      stable => CW1CYCLE, per the product's NOTE) agrees with
 *      mcs_shadow_class on the same bytes -- the memory must feed inputs,
 *      never a verdict;
 *   4. recycling (identical key) is NOT rotation: shadow says bothsame and
 *      the eligibility predicate denies passage -- the memory must never
 *      become a pass-through for exactly the stale keys this box fights;
 *   5. key distinctness: same caid+sid with different provid must not
 *      collide on the stored bytes (the field channel 13AE exists under
 *      both 000000 and 005211 in the same table).
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static unsigned int fake_now = 1000;
#define MCS_CHPREV_NOW() (fake_now)
#include "../src/cache-chan-prevcw.h"
#include "../src/cache-cycle-shadow.h"

static int fails = 0;
#define CHECK(cond, what) do { if (!(cond)) { \
	printf("FAIL: %s\n", (what)); fails++; } } while (0)

static void set_cw(uint8_t *cw, int seed)
{
	int i;
	for (i = 0; i < 16; i++) cw[i] = (unsigned char)((seed * 31 + i * 7) & 0xFF);
}

int main(void)
{
	uint8_t a[16], b[16], got[16];
	const uint8_t *p;

	set_cw(a, 1); set_cw(b, 2);
	mcs_chprev_reset();

	/* 1. remember + retrieve */
	mcs_chprev_put(0x1814, 0x005211, 0x13AE, a, 0 /*default TTL*/);
	p = mcs_chprev_get(0x1814, 0x005211, 0x13AE, 0);
	CHECK(p != 0, "served key must be remembered");
	if (p) { memcpy(got, p, 16); CHECK(memcmp(got, a, 16) == 0, "stored bytes exact"); }

	/* 5. provid distinguishes the channel */
	CHECK(mcs_chprev_get(0x1814, 0x000000, 0x13AE, 0) == 0,
	      "same caid+sid under other provid must not see this memory");

	/* 6. band 117: the counters are the page's whole diagnostic — a bare
	 * ON/OFF could not tell "memory never written" from "written but not
	 * needed", which is exactly what left the 51-minute field session
	 * undecided. So the accounting is pinned, not decorative. */
	mcs_chprev_reset();
	{
		struct mcs_chprev_stats z;
		mcs_chprev_get_stats(&z);
		CHECK(z.puts == 0 && z.hits == 0 && z.stale == 0 && z.dropped == 0,
		      "reset zeroes the counters");
	}
	mcs_chprev_put(0x1814, 0x005211, 0x13AE, a, 0);
	(void)mcs_chprev_get(0x1814, 0x005211, 0x13AE, 0);
	CHECK(mcs_chprev_get(0x1814, 0x005211, 0x13AE, 0) != 0, "still readable");
	mcs_chprev_get(0x1814, 0x005211, 0x13AE, 0);
	(void)mcs_chprev_get(0x0100, 0x000000, 0x9999, 0);   /* a miss must not count */
	{
		struct mcs_chprev_stats q;
		mcs_chprev_get_stats(&q);
		CHECK(q.puts == 1, "one put == one write");
		CHECK(q.hits == 3, "every successful read counts (a miss must not)");
		CHECK(q.stale == 0 && q.dropped == 0, "no TTL takeover or crowding here");
	}

	/* 2. TTL is exact */
	fake_now += MCS_CHPREV_DEF_TTL_MS;
	CHECK(mcs_chprev_get(0x1814, 0x005211, 0x13AE, 0) != 0, "at == ttl still valid");
	fake_now += 1;
	CHECK(mcs_chprev_get(0x1814, 0x005211, 0x13AE, 0) == 0, "past ttl invisible");

	/* custom ttl honored */
	mcs_chprev_put(0x1802, 0x000000, 0x0F47, a, 5000);
	fake_now += 5001;
	CHECK(mcs_chprev_get(0x1802, 0x000000, 0x0F47, 5000) == 0, "custom ttl expires");

	/* relearn refreshes */
	fake_now = 1000;
	mcs_chprev_reset();
	mcs_chprev_put(0x1814, 0x005211, 0x13AE, a, 0);
	fake_now += 29000;
	mcs_chprev_put(0x1814, 0x005211, 0x13AE, b, 0);
	fake_now += 29000;
	p = mcs_chprev_get(0x1814, 0x005211, 0x13AE, 0);
	CHECK(p && memcmp(p, b, 16) == 0, "newest served key wins and refreshes");

	/* 3+4: what the estimator does with the memory -- the mapping the
	 * product's NOTE declares, and the refusal of the two non-rotations */
	{
		uint8_t nxt[16];
		int first_same, second_same, cls;
		/* second half carried over, first rotated -> CW0CYCLE */
		memcpy(nxt, b, 16);
		memset(nxt, 0xEE, 8);
		first_same  = memcmp(nxt, b, 8) == 0;        /* dcwcmp8 == equality */
		second_same = memcmp(nxt + 8, b + 8, 8) == 0;
		cls = mcs_shadow_class(1, first_same, second_same);
		CHECK(cls == MCS_SHADOW_ROT_CW0, "second-half stable is ROT_CW0");
		CHECK(mcs_cycle_temporal_ok(1, 1, 0, (first_same != second_same) ? 1 : 0) == 1,
		      "memory-fed rotation is eligible on the new route");
		CHECK(mcs_cycle_temporal_ok(0, 1, 0, 1) == 0, "estimator-off keeps refusal");
		/* recycle: identical key -> no eligibility, shadow says bothsame */
		first_same = 1; second_same = 1;
		cls = mcs_shadow_class(1, first_same, second_same);
		CHECK(cls == MCS_SHADOW_BOTHSAME, "recycled key classifies bothsame");
		CHECK(mcs_cycle_temporal_ok(1, 1, 0, (first_same != second_same) ? 1 : 0) == 0,
		      "recycled key must NOT pass via the memory");
		/* both changed -> neither, no eligibility */
		first_same = 0; second_same = 0;
		cls = mcs_shadow_class(1, first_same, second_same);
		CHECK(cls == MCS_SHADOW_NEITHER, "fresh unrelated key classifies neither");
		CHECK(mcs_cycle_temporal_ok(1, 1, 0, (first_same != second_same) ? 1 : 0) == 0,
		      "unrelated key must NOT pass via the memory");
	}

	/* stats sanity: puts counted even on dropped collisions */
	mcs_chprev_reset();
	{
		int i;
		uint8_t k[16];
		struct mcs_chprev_stats st0, st1;
		mcs_chprev_get_stats(&st0);
		for (i = 0; i < 600; i++) {
			set_cw(k, i);
			mcs_chprev_put(0x0B00, (uint32_t)i, (uint16_t)(0x100 + i), k, 0);
		}
		mcs_chprev_get_stats(&st1);
		CHECK(st1.puts - st0.puts == 600, "puts count every offer");
		CHECK(st1.dropped >= st0.dropped, "dropped is monotonic");
	}

	if (fails) { printf("chan-prevcw: %d FAILURES\n", fails); return 1; }
	printf("chan-prevcw: PASS (remember/TTL/refresh, provid split, feed-only-inputs)\n");
	return 0;
}
