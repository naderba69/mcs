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
/* Band 119: this TU is now the OWNER of the state, exactly as clustredcache.c
 * is in production. If the header ever goes back to `static` file-scope
 * objects, every includer silently gets a private copy -- which is what made
 * the /user-trace line print `Wrote=0` for 8h28m while the feature ran. The
 * unit binary defines the IMPL macro so it keeps exercising the one-instance
 * shape; the header-shape guard is tests/staticstate.py. */
#define MCS_CHAN_PREVCW_IMPL
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
	int p_hit;

	set_cw(a, 1); set_cw(b, 2);
	mcs_chprev_reset();

	/* 1. remember + retrieve */
	mcs_chprev_put(0x1814, 0x005211, 0x13AE, a, 0 /*default TTL*/);
	p_hit = mcs_chprev_get(0x1814, 0x005211, 0x13AE, 0, got);
	CHECK(p_hit == 1, "served key must be remembered");
	CHECK(memcmp(got, a, 16) == 0, "stored bytes exact");

	/* 5. provid distinguishes the channel */
	CHECK(mcs_chprev_get(0x1814, 0x000000, 0x13AE, 0, got) == 0,
	      "same caid+sid under other provid must not see this memory");

	/* 5b. band 119: get() copies out under the lock, so the caller never
	 * holds a pointer into a table another thread may rewrite. */
	memset(got, 0xEE, 16);
	CHECK(mcs_chprev_get(0x1814, 0x005211, 0x13AE, 0, got) == 1 &&
	      memcmp(got, a, 16) == 0, "the copy survives the caller's own scrubbing");

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
	(void)mcs_chprev_get(0x1814, 0x005211, 0x13AE, 0, got);
	CHECK(mcs_chprev_get(0x1814, 0x005211, 0x13AE, 0, got) == 1, "still readable");
	mcs_chprev_get(0x1814, 0x005211, 0x13AE, 0, got);
	(void)mcs_chprev_get(0x0100, 0x000000, 0x9999, 0, got);   /* a miss must not count */
	{
		struct mcs_chprev_stats q;
		mcs_chprev_get_stats(&q);
		CHECK(q.puts == 1, "one put == one write");
		CHECK(q.hits == 3, "every successful read counts (a miss must not)");
		CHECK(q.stale == 0 && q.dropped == 0, "no TTL takeover or crowding here");
		CHECK(q.loc == 0, "the relay-tail writer must not be counted as a local delivery");
	}

	/* 7. band 119: the LOCAL-DELIVERY writer. The relay tail in
	 * clustredcache.c sits downstream of the cycle gate, so on a network where
	 * every key is refused at the gate it records nothing; the commit in
	 * setdcw.c runs on every answered ECM. `loc` is what lets the page tell the
	 * two feeds apart -- and it is the only number that shows whether band 119
	 * is actually in the running binary. */
	mcs_chprev_reset();
	mcs_chprev_put_src(0x1814, 0x005211, 0x13AE, b, 0, 1);
	{
		struct mcs_chprev_stats q;
		mcs_chprev_get_stats(&q);
		CHECK(q.puts == 1 && q.loc == 1, "local delivery counts as a write AND as local");
	}
	CHECK(mcs_chprev_get(0x1814, 0x005211, 0x13AE, 0, got) == 1 &&
	      memcmp(got, b, 16) == 0, "the local-delivery key is retrievable by the gate");

	/* 2. TTL is exact */
	fake_now += MCS_CHPREV_DEF_TTL_MS;
	CHECK(mcs_chprev_get(0x1814, 0x005211, 0x13AE, 0, got) == 1, "at == ttl still valid");
	fake_now += 1;
	CHECK(mcs_chprev_get(0x1814, 0x005211, 0x13AE, 0, got) == 0, "past ttl invisible");

	/* custom ttl honored */
	mcs_chprev_put(0x1802, 0x000000, 0x0F47, a, 5000);
	fake_now += 5001;
	CHECK(mcs_chprev_get(0x1802, 0x000000, 0x0F47, 5000, got) == 0, "custom ttl expires");

	/* relearn refreshes */
	fake_now = 1000;
	mcs_chprev_reset();
	mcs_chprev_put(0x1814, 0x005211, 0x13AE, a, 0);
	fake_now += 29000;
	mcs_chprev_put(0x1814, 0x005211, 0x13AE, b, 0);
	fake_now += 29000;
	p_hit = mcs_chprev_get(0x1814, 0x005211, 0x13AE, 0, got);
	CHECK(p_hit == 1 && memcmp(got, b, 16) == 0, "newest served key wins and refreshes");

	/* 8. band 120: crowding must EVICT the stalest neighbour, never drop the
	 * write. Pinned because the shipped code dropped it while its own comment
	 * described eviction, and the field then read Dropped=71429 of
	 * Wrote=109489 -- 65% of served keys lost to capacity, not to policy.
	 * Collisions are constructed on purpose (same bucket by the table's own
	 * hash), since a 8192-slot table otherwise never crowds. */
	mcs_chprev_reset();
	{
		uint32_t samep[9];            /* provids chosen to collide in one probe run */
		uint32_t want;
		uint32_t prov;
		int found = 0;
		int j;
		/* sid fixed, provid swept: provid takes part in the key, so an
		 * 8192-bucket table yields about one hit per 8192 probes -- sweeping
		 * sids (only 65536 exist) would not find nine. */
		want = mcs_chprev_key(0x1814, 0, 0x2000) % MCS_CHPREV_SLOTS;
		for (prov = 1; prov < 400000 && found < 9; prov++)
			if (mcs_chprev_key(0x1814, prov, 0x2000) % MCS_CHPREV_SLOTS == want)
				samep[found++] = prov;
		CHECK(found == 9, "nine channels hashing into one probe run");
		/* 8 live slots, strictly increasing age: the first is the stalest */
		for (j = 0; j < 8 && found == 9; j++) {
			set_cw(a, 10 + j);
			mcs_chprev_put(0x1814, samep[j], 0x2000, a, 30000);
			fake_now += 100;
		}
		set_cw(b, 99);
		if (found == 9) mcs_chprev_put(0x1814, samep[8], 0x2000, b, 30000);
		{
			struct mcs_chprev_stats e;
			mcs_chprev_get_stats(&e);
			CHECK(e.puts == 9, "the crowding write is a put like any other");
			CHECK(e.evicted == 1, "crowding counts as exactly one eviction");
			CHECK(e.dropped == 0, "band 120: nothing is dropped while a victim exists");
			CHECK(e.stale == 0, "no slot was past TTL -- this is capacity, not expiry");
		}
		CHECK(found == 9 &&
		      mcs_chprev_get(0x1814, samep[8], 0x2000, 30000, got) == 1 &&
		      memcmp(got, b, 16) == 0, "the crowding key is actually stored");
		/* The price, named rather than hidden: the evicted channel loses its
		 * memory until its own next served key -- the pre-115 state, and in
		 * any case bounded by the TTL this table was configured with. */
		CHECK(found == 9 &&
		      mcs_chprev_get(0x1814, samep[0], 0x2000, 30000, got) == 0,
		      "the evicted channel reads as no-memory, not as a wrong key");
	}

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
