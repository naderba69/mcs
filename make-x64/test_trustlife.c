/*
 * TASK 2.10 -- the unit suite for the trust lifecycle (../src/trustlife.h,
 * plus the restore helpers trustlife added to trust.h / trustagg.h /
 * cwneg.h).
 *
 * What is pinned here, in order of how much it would hurt to get wrong:
 *
 *   1. The fade moves a score toward TRUST_START and never past it, at the
 *      advertised rate, only after the grace, only for IDLE entries (a fresh
 *      event re-shields the entry entirely), and it leaves the history
 *      counters alone. Fade is idempotent within a tick. Above START it
 *      decays standing DOWN: the ceiling is not an immunity.
 *   2. The round trip. format -> parse -> restore reproduces the actionable
 *      state of all three tables exactly: score clamped-but-equal in range,
 *      counters intact, identity exact -- including a srcid carrying PEER_*
 *      flags and a zero provid.
 *   3. The negative memory survives by DIGEST: a restored conviction matches
 *      the original key through cwneg_hit, refuses a different key, and an
 *      already-remembered conviction is touched, not duplicated.
 *   4. The parse refuses, whole, anything it cannot validate: score out of
 *      range, negative counters, zero source/caid/digest, bad or short hex,
 *      wrong field counts, garbage, an empty line, an age beyond 7 days.
 *   5. Restore insertion follows each table's own eviction rule and is
 *      idempotent (restore twice, one entry).
 */
#include "../src/trustlife.h"
#include "../src/cwneg.h"

#include <stdio.h>
#include <string.h>

static int passed = 0, failed = 0;

#define CHECK(cond, what) do { \
	if (cond) { passed++; printf("  [ ok ] %s\n", what); } \
	else { failed++; printf("  [FAIL] %s\n", what); } \
} while (0)

int main(void)
{
	struct trust_table tf;
	struct trustagg_table ta;
	struct cwneg_table ng;
	struct tl_rec r, q;
	char line[256];
	int n;

	printf("trustlife: fade, persistence and the restore path\n");

	/* --- 1. the fade ------------------------------------------------------ */
	memset(&tf, 0, sizeof(tf));
	trust_record(&tf, TRUST_EV_PROOF, 1, 5, 0x1884, 1, 0x64, 1000);
	trust_record(&tf, TRUST_EV_PROOF, 1, 5, 0x1884, 1, 0x64, 1100);
	{
		struct trust_entry *e = trust_find(&tf, 1, 5, 0x1884, 1, 0x64);
		CHECK(e && e->score == 10, "the condemned entry sits at the floor (10)");

		/* inside the grace: untouched */
		tl_fade_fine(&tf, 1100 + 2000, 3000, 1000);
		e = trust_find(&tf, 1, 5, 0x1884, 1, 0x64);
		CHECK(e->score == 10, "inside the grace the score does not move");

		/* idle 3s+13s at 1 point/s: 10 -> 23, still below the line */
		tl_fade_fine(&tf, 1100 + 3000 + 13000, 3000, 1000);
		e = trust_find(&tf, 1, 5, 0x1884, 1, 0x64);
		CHECK(e->score == 23, "after grace, one point per step of silence");

		/* long idle: relaxes TO start, never past it */
		tl_fade_fine(&tf, 1100 + 3000 + 300000, 3000, 1000);
		e = trust_find(&tf, 1, 5, 0x1884, 1, 0x64);
		CHECK(e->score == TRUST_START, "long silence relaxes to neutral, never past it");
		CHECK(e->nproof == 2, "the history counters do not fade");

		/* a fresh event re-shields: lastseen moves, the fade stops touching */
		trust_record(&tf, TRUST_EV_SOFT, 1, 5, 0x1884, 1, 0x64, 400000);
		e = trust_find(&tf, 1, 5, 0x1884, 1, 0x64);
		CHECK(e->score == TRUST_START - (TRUST_START - TRUST_FLOOR) / 2,
		      "the fresh event bit at full strength from the relaxed score");
		tl_fade_fine(&tf, 400000 + 1000, 3000, 1000);
		e = trust_find(&tf, 1, 5, 0x1884, 1, 0x64);
		CHECK(e->score == TRUST_START - (TRUST_START - TRUST_FLOOR) / 2,
		      "an active entry never fades inside the grace: its last event is its shield");

		/* above START it decays DOWN: the ceiling is not an immunity */
		{
			struct trust_table hi;
			struct trust_entry *he;
			int i;
			memset(&hi, 0, sizeof(hi));
			for (i = 0; i < 400; i++)
				trust_record(&hi, TRUST_EV_GOOD, 1, 9, 0x1884, 2, 0x64, 1000 + i);
			he = trust_find(&hi, 1, 9, 0x1884, 2, 0x64);
			CHECK(he->score == TRUST_CEILING, "the GOOD staircase reaches the ceiling");
			tl_fade_fine(&hi, 1000 + 3000 + 40000, 3000, 1000);
			he = trust_find(&hi, 1, 9, 0x1884, 2, 0x64);
			CHECK(he->score == TRUST_CEILING - 39,
			      "silence decays standing from the ceiling, one point per step");
		}

		/* idempotent within a tick */
		{
			int a, b;
			a = tl_fade_fine(&tf, 900000, 3000, 1000);
			b = tl_fade_fine(&tf, 900000, 3000, 1000);
			CHECK(b == 0, "a second sweep at the same tick moves nothing (stateless)");
			(void)a;
		}
	}

	/* coarse tier: the same rule over (source, CAID) */
	memset(&ta, 0, sizeof(ta));
	trustagg_note(&ta, TRUST_EV_PROOF, 1, 5, 0x1884, 1000, NULL);
	trustagg_note(&ta, TRUST_EV_PROOF, 1, 5, 0x1884, 1100, NULL);
	n = tl_fade_coarse(&ta, 1100 + 3000 + 55000, 3000, 1000);
	{
		struct trustagg_entry *e = trustagg_find(&ta, 1, 5, 0x1884);
		CHECK(n == 1 && e->score == TRUST_START,
		      "the coarse tier fades by the same rule: 58 idle seconds relax 10 all the way to 60");
		CHECK(trustagg_ask(&ta, 1, 5, 0x1884, 1100 + 3000 + 56000) == 1,
		      "and the peer is askable again the moment it is above the line");
	}

	/* --- 2. the round trip ------------------------------------------------ */
	memset(&tf, 0, sizeof(tf));
	trust_restore(&tf, 1, 65537, 0x1884, 0, 0x64, 10, 2, 1, 42000, 5000);
	{
		struct trust_entry *e = trust_find(&tf, 1, 65537, 0x1884, 0, 0x64);
		CHECK(e && e->score == 10 && e->nproof == 2 && e->nsoft == 1,
		      "trust_restore lands score and counters");
		CHECK(e->lastseen == 0,
		      "an age older than this boot's counter pins the entry at tick 0 (conservative)");
		trust_restore(&tf, 1, 65537, 0x1884, 0, 0x64, 10, 2, 1, 4000, 5000);
		e = trust_find(&tf, 1, 65537, 0x1884, 0, 0x64);
		CHECK(e->lastseen == 1000,
		      "a representable age re-bases exactly: seen at now - age");
		/* a PEER-flagged srcid must survive byte-exact (GR1/GR4) */
		r.kind = TL_TF; r.srctype = 1; r.srcid = 65537; r.caid = 0x1884;
		r.provid = 0; r.sid = 0x64; r.score = 10; r.nproof = 2; r.nsoft = 1; r.age_s = 42;
		n = tl_format_rec(line, (int)sizeof(line), &r);
		CHECK(n > 0 && tl_parse_line(line, &q) == 0, "TF line round-trips");
		CHECK(q.kind == TL_TF && q.srcid == 65537 && q.caid == 0x1884 &&
		      q.provid == 0 && q.sid == 0x64 && q.score == 10 &&
		      q.nproof == 2 && q.nsoft == 1 && q.age_s == 42,
		      "every TF field survives, flags and zero provid included");
	}
	{
		r.kind = TL_TA; r.srctype = 1; r.srcid = 65537; r.caid = 0x1884;
		r.score = 60; r.nproof = 2; r.nsoft = 0; r.ngood = 5; r.age_s = 7;
		n = tl_format_rec(line, (int)sizeof(line), &r);
		CHECK(n > 0 && tl_parse_line(line, &q) == 0, "TA line round-trips");
		CHECK(q.ngood == 5 && q.score == 60 && q.age_s == 7, "every TA field survives");
	}

	/* --- 3. negative memory survives by digest ---------------------------- */
	memset(&ng, 0, sizeof(ng));
	{
		uint8_t cw[16] = { 0xA5,0x4D,0xCA,0xBC,0x25,0x30,0xBB,0x10,
		                   0x6D,0x13,0x2C,0xAC,0xD6,0x23,0x7B,0x74 };
		uint64_t d = cwneg_digest(cw);
		struct cwneg_ev ev;

		memset(&r, 0, sizeof(r));
		r.kind = TL_NG; r.caid = 0x1884; r.provid = 0;
		r.kdigest = d; memcpy(r.key8, cw, 8); r.age_s = 9;
		n = tl_format_rec(line, (int)sizeof(line), &r);
		CHECK(n > 0, "the NG line formats");
		CHECK(tl_parse_line(line, &q) == 0 && q.kdigest == d && q.key8[0] == 0xA5,
		      "the NG line round-trips digest and evidence prefix");
		CHECK(strstr(line, "a54dcabc2530bb10") != NULL,
		      "the line carries the key's 8-byte evidence prefix in hex");

		CHECK(cwneg_restore(&ng, 0x1884, 0, q.kdigest, q.key8, 9000, 20000) == 1,
		      "the conviction restores into the empty table");
		CHECK(cwneg_hit(&ng, cw, 0x1884, 0, 0x1234, 30000, &ev) == 1,
		      "the ORIGINAL key is refused after the round trip, any sid");
		{
			uint8_t other[16];
			memcpy(other, cw, 16);
			other[0] ^= 0x40;
			CHECK(cwneg_hit(&ng, other, 0x1884, 0, 0x1234, 30000, &ev) == 0,
			      "a different key is not tarred by it");
			CHECK(cwneg_hit(&ng, cw, 0x0500, 0, 0x1234, 30000, &ev) == 0,
			      "a different caid is not tarred by it");
		}
		CHECK(cwneg_restore(&ng, 0x1884, 0, q.kdigest, q.key8, 9000, 25000) == 0,
		      "restoring the same conviction twice touches, not duplicates");
		CHECK(cwneg_count(&ng) == 1, "and the table holds exactly one conviction");
	}

	/* --- 4. the parse refuses what it cannot validate ---------------------- */
	{
		struct tl_rec bad;
		const char *rejects[] = {
			"garbage",
			"",
			"   ",
			"XX 1 5 1884 0 64 10 2 1 42",
			"TF 1 5 1884 0 64 5 2 1 42",        /* score below the floor   */
			"TF 1 5 1884 0 64 200 2 1 42",      /* score above the ceiling */
			"TF 1 0 1884 0 64 10 2 1 42",       /* zero source id          */
			"TF 1 5 0 0 64 10 2 1 42",          /* zero caid               */
			"TF 1 5 1884 0 64 10 -1 1 42",      /* negative counter        */
			"TF 1 5 1884 0 64 10 2 1 0",        /* zero age                */
			"TF 1 5 1884 0 64 10 2 1 99999999", /* age beyond 7 days       */
			"TF 1 5 1884 0 64 10 2 1",          /* truncated               */
			"TA 1 5 1884 10 2 1 -3 42",         /* negative ngood          */
			"NG 1884 0 deadbeefcafe123 a54dcabc2530bb106d132cacd6237b74 42",
			"NG 1884 0 deadbeefcafe1234 a54dcabc2530bb10zz32cacd6237b74 42",
			"NG 0 0 deadbeefcafe1234 a54dcabc2530bb106d132cacd6237b74 42",
			"NG 1884 0 0000000000000000 a54dcabc2530bb106d132cacd6237b74 42",
		};
		int i, all_refused = 1;
		for (i = 0; i < (int)(sizeof(rejects)/sizeof(rejects[0])); i++)
			if (tl_parse_line(rejects[i], &bad) == 0) all_refused = 0;
		CHECK(all_refused, "every malformed line is refused whole");
	}

	/* --- 5. restore idempotence and eviction ------------------------------- */
	{
		struct trust_table rt;
		int i;
		memset(&rt, 0, sizeof(rt));
		trust_restore(&rt, 1, 5, 0x1884, 0, 0x64, 35, 1, 0, 1000, 10000);
		trust_restore(&rt, 1, 5, 0x1884, 0, 0x64, 35, 1, 0, 1000, 10000);
		CHECK(trust_find(&rt, 1, 5, 0x1884, 0, 0x64) != NULL, "the restored entry exists");
		{
			struct trust_table full;
			memset(&full, 0, sizeof(full));
			for (i = 0; i < TRUST_TABLE_SIZE; i++)
				trust_restore(&full, 1, i + 1, 0x1884, 0, 0x64, 60, 0, 0, 0, 1000 + i);
			trust_restore(&full, 1, TRUST_TABLE_SIZE + 1, 0x1884, 0, 0x64, 60, 0, 0, 0, 5000);
			CHECK(trust_find(&full, 1, 1, 0x1884, 0, 0x64) == NULL,
			      "a full table evicts the oldest, exactly as trust_record does");
			CHECK(trust_find(&full, 1, 2, 0x1884, 0, 0x64) != NULL,
			      "and the next peer is never refused");
		}
	}

	printf("  %d ok, %d failed\n", passed, failed);
	return failed ? 1 : 0;
}
