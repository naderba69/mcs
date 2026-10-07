/*
 * test_phase1cfg.c -- TASK 1.9
 *
 * The three options are policy, so what is tested here is the policy: the
 * tolerance in rotation.h, the age limit in cache_purge.h, and the line that
 * reports them in statsline.h. All three are the real headers.
 *
 * The default-value rule gets its own section, because it is the brief's
 * backward-compatibility requirement: a configuration that does not mention
 * BAD-CW-LIMIT, SERVICE-BLACKLIST-TIME or STATS-WINDOW must behave exactly as
 * the code did before those options existed. "Exactly" is checkable here -- the
 * default paths are the pre-1.9 functions, unchanged.
 *
 *   make -C make-x64 test
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../src/rotation.h"
#include "../src/cache_purge.h"
#include "../src/statsline.h"

static int pass_n = 0, fail_n = 0;

#define CHECK(cond, name) do { \
	if (cond) { pass_n++; printf("  [ ok ] %s\n", name); } \
	else { fail_n++; printf("  [FAIL] %s\n", name); } \
} while (0)

#define SRV   2u
#define CAC   3u
#define PEER   (7u | 0x010000u)
#define CAID   0x1884
#define SID    0x0064
#define PROV   0x000000u

static struct rotation_table rt;
static struct purge_table pt;

/* ------------------------------------------------------------------ */

static void t_limit_semantics(void)
{
	printf("  -- BAD-CW-LIMIT: one slot per source, avoided once the count reaches the limit --\n");
	rotation_init(&rt);
	CHECK(rotation_note_bad_limited(&rt, SRV, 1, CAID, SID, PROV, 1000, 3) == 0,
	      "limit 3: the first confirmed bad CW is counted, not acted on");
	CHECK(rotation_should_avoid_limited(&rt, SRV, 1, CAID, SID, PROV, 3) == 0,
	      "and the source is not avoided yet");
	CHECK(rotation_note_bad_limited(&rt, SRV, 1, CAID, SID, PROV, 1100, 3) == 0,
	      "the second is counted too");
	CHECK(rotation_note_bad_limited(&rt, SRV, 1, CAID, SID, PROV, 1200, 3) == 2,
	      "the third returns 2: this is the event that reached the limit, so this is the one to log");
	CHECK(rotation_should_avoid_limited(&rt, SRV, 1, CAID, SID, PROV, 3) == 1,
	      "and now the source is avoided");
	CHECK(rotation_note_bad_limited(&rt, SRV, 1, CAID, SID, PROV, 1300, 3) == 1,
	      "a fourth returns 1, not 2: over the limit already, so nothing new to log");
	CHECK(rt.deferred == 2, "and exactly the two events below the limit were counted as deferred");

	/* A tolerance that is lower is not a different code path: limit 1 crosses at once. */
	rotation_init(&rt);
	CHECK(rotation_note_bad_limited(&rt, SRV, 1, CAID, SID, PROV, 1000, 1) == 2,
	      "limit 1: the first event reaches the limit immediately (TASK 1.6 behaviour)");
	CHECK(rt.deferred == 0, "and nothing was ever deferred, which is how a default install reads");
}

static void t_alternating(void)
{
	int i;
	printf("  -- two sources below the limit must not share one slot --\n");
	rotation_init(&rt);
	/*
	 * This is the case that caught a real defect while writing this task. The
	 * first cut kept counts for not-yet-avoided sources in slots above e->n, so
	 * two sources interleaved here overwrote each other's count and neither ever
	 * reached the limit -- the tolerance would have silently become "never",
	 * which is the worst possible failure for an option whose whole job is to
	 * stop a source from being avoided.
	 */
	for (i = 0; i < 3; i++) {
		rotation_note_bad_limited(&rt, SRV, 1, CAID, SID, PROV, 1000 + 2*i, 3);
		rotation_note_bad_limited(&rt, SRV, 2, CAID, SID, PROV, 1001 + 2*i, 3);
	}
	CHECK(rotation_count_of(&rt, SRV, 1, CAID, SID, PROV) == 3, "source 1 reached its own count of 3");
	CHECK(rotation_count_of(&rt, SRV, 2, CAID, SID, PROV) == 3, "source 2 reached its own count of 3");
	CHECK(rotation_should_avoid_limited(&rt, SRV, 1, CAID, SID, PROV, 3) == 1, "source 1 is avoided");
	CHECK(rotation_should_avoid_limited(&rt, SRV, 2, CAID, SID, PROV, 3) == 1, "source 2 is avoided");
}

static void t_default_path(void)
{
	printf("  -- the default path is the pre-1.9 functions, not a re-implementation --\n");
	rotation_init(&rt);
	CHECK(rotation_note_bad(&rt, SRV, 1, CAID, SID, PROV, 1000) == 1,
	      "rotation_note_bad() still returns 1 for a new source (its old contract)");
	CHECK(rotation_note_bad(&rt, SRV, 1, CAID, SID, PROV, 2000) == 0,
	      "and 0 for a source it already recorded, exactly as before");
	CHECK(rotation_should_avoid(&rt, SRV, 1, CAID, SID, PROV) == 1,
	      "the 6-argument should_avoid() avoids on the first event, i.e. limit 1");
	CHECK(rotation_count_of(&rt, SRV, 1, CAID, SID, PROV) == 2,
	      "the count is the number of events seen, 2, and stays readable above the limit rather than saturating at it");

	CHECK(rotation_note_good(&rt, CAID, SID, PROV) == 1, "the channel recovering releases it");
	CHECK(rotation_should_avoid(&rt, SRV, 1, CAID, SID, PROV) == 0,
	      "and clears the avoidance");
	CHECK(rotation_count_of(&rt, SRV, 1, CAID, SID, PROV) == 0,
	      "AND the count -- otherwise a tolerated source would carry its count across recoveries and be avoided on the next single event");
}

static void t_purge_age(void)
{
	struct purge_entry out[CACHE_PURGE_SLOTS];
	int n;

	printf("  -- SERVICE-BLACKLIST-TIME: a mark with an age limit --\n");
	cache_purge_init(&pt);
	CHECK(cache_purge_mark(&pt, CAC, PEER, CAID, SID, PROV, 10000) == 1, "a definitive proof places a mark");

	CHECK(cache_purge_should_withhold(&pt, CAC, PEER, CAID, SID, PROV) == 1,
	      "the default (no age limit) withholds regardless of age");
	CHECK(cache_purge_should_withhold_at(&pt, CAC, PEER, CAID, SID, PROV, 600000, 0) == 1,
	      "ten minutes later and still no age limit: still withheld -- TASK 1.5 behaviour, unchanged");

	CHECK(cache_purge_should_withhold_at(&pt, CAC, PEER, CAID, SID, PROV, 14000, 5000) == 1,
	      "with a 5 s limit, 4 s of age is still inside it");
	CHECK(cache_purge_should_withhold_at(&pt, CAC, PEER, CAID, SID, PROV, 15000, 5000) == 0,
	      "and exactly 5 s is out: the comparison is >=, so a window of N means N milliseconds");

	CHECK(cache_purge_expire(&pt, 14000, 0, out, CACHE_PURGE_SLOTS) == 0,
	      "expire() with the option off lifts nothing (the default costs one test)");
	CHECK(pt.expired == 0, "and counts nothing");

	n = cache_purge_expire(&pt, 20000, 5000, out, CACHE_PURGE_SLOTS);
	CHECK(n == 1, "expire() lifts the aged mark");
	CHECK(n == 1 && out[0].caid == CAID && out[0].sid == SID && out[0].provid == PROV &&
	      out[0].srctype == CAC && out[0].srcid == PEER,
	      "and reports WHICH mark it lifted, so GR8's one-log-line-per-event can name it");
	CHECK(cache_purge_count(&pt) == 0, "the table is empty again");
	CHECK(pt.expired == 1, "counted as expired, not released");
	CHECK(cache_purge_should_withhold(&pt, CAC, PEER, CAID, SID, PROV) == 0,
	      "and nothing is withheld any more");

	/* A fresh mark must survive an expiry sweep aimed at an old one. */
	CHECK(cache_purge_mark(&pt, CAC, PEER, CAID, SID, PROV, 19000) == 1, "a second proof places a new mark");
	CHECK(cache_purge_expire(&pt, 20000, 5000, out, CACHE_PURGE_SLOTS) == 0,
	      "one second old, so a 5 s sweep leaves it alone");
	CHECK(cache_purge_count(&pt) == 1, "and it is still live");
}

static void t_purge_release(void)
{
	printf("  -- the recovery release, and the two counters being different --\n");
	cache_purge_init(&pt);
	cache_purge_mark(&pt, CAC, PEER, CAID, SID, PROV, 1000);
	CHECK(cache_purge_unmark(&pt, CAC, PEER, CAID, SID, PROV) == 1, "a recovering source lifts its own mark");
	CHECK(pt.released == 1 && pt.expired == 0,
	      "counted as released, NOT as expired -- \"my peer recovered\" and \"my peer went quiet\" are different situations");
	CHECK(cache_purge_unmark(&pt, CAC, PEER, CAID, SID, PROV) == 0, "a second lift removes nothing");
	CHECK(pt.released == 1, "and does not double-count");
	CHECK(cache_purge_unmark(&pt, CAC, PEER, CAID, 0x00C8, PROV) == 0,
	      "and a mark on another channel is not touched (GR4: never wider than the channel)");
}

static void t_statsline(void)
{
	struct stats_snapshot s;
	char line[320];
	int n;

	printf("  -- STATS-WINDOW: the line an operator reads --\n");
	memset(&s, 0, sizeof(s));
	s.window_ms = 60000;
	n = statsline_format(&s, line, (int)sizeof(line));
	CHECK(n > 0 && n == (int)strlen(line), "returns the length it wrote, matching strlen");
	CHECK(strstr(line, "PHASE1 STATS") != NULL, "the line is identifiable in a log full of other lines");
	CHECK(strstr(line, "60s") != NULL, "and names the window in seconds, so a reader can turn counts into rates");
	CHECK(strstr(line, "empty") == NULL, "a completely idle window is still reported -- silence would be indistinguishable from a server whose new code never ran");

	s.struct_anomalous = 7; s.struct_suppressed = 1;
	s.deferrals = 12;
	s.purge_live = 2; s.purge_marked = 5; s.purge_released = 2; s.purge_expired = 1;
	s.rot_skipped = 4; s.rot_capped = 1; s.rot_below = 9; s.rot_limit = 3;
	s.trust_live = 17;
	statsline_format(&s, line, (int)sizeof(line));
	CHECK(strstr(line, "struct 7 (1 repeat suppressed)") != NULL,
	      "singular is singular: one suppressed repeat, not \"1 repeats\"");
	CHECK(strstr(line, "cache defer 12") != NULL, "the 1.7 deferrals are on the line");
	CHECK(strstr(line, "purge 2 live (5 marked, 2 released, 1 expired)") != NULL,
	      "marks live, and the two kinds of lift told apart");
	CHECK(strstr(line, "rotate 4 skipped, 1 refused, 9 below limit of 3") != NULL,
	      "rotation, the cap, and the BAD-CW-LIMIT tolerance all visible");
	CHECK(strstr(line, "trust 17 sources") != NULL, "and the trust table's size");

	s.trust_live = 1; s.struct_suppressed = 2;
	statsline_format(&s, line, (int)sizeof(line));
	CHECK(strstr(line, "trust 1 source") != NULL && strstr(line, "sources") == NULL,
	      "one source, not one sources");

	/* A log buffer is not a place to discover an off-by-one. */
	n = statsline_format(&s, line, 20);
	CHECK(n == 19 && line[19] == 0, "a 20-byte buffer gets 19 characters and a terminator, never more");
	CHECK(statsline_format(&s, line, 1) == 0 && line[0] == 0, "a 1-byte buffer gets an empty string");
	CHECK(statsline_format(NULL, line, (int)sizeof(line)) == -1, "NULL snapshot is an error, not a crash");
	CHECK(statsline_format(&s, NULL, 10) == -1, "NULL buffer is an error too");
}

int main(void)
{
	printf("test_phase1cfg -- TASK 1.9: BAD-CW-LIMIT, SERVICE-BLACKLIST-TIME, STATS-WINDOW\n");
	printf("  sizeof(struct rotation_entry) = %u, rotation_table = %u\n",
	       (unsigned)sizeof(struct rotation_entry), (unsigned)sizeof(struct rotation_table));
	printf("  sizeof(struct purge_entry) = %u, purge_table = %u\n",
	       (unsigned)sizeof(struct purge_entry), (unsigned)sizeof(struct purge_table));

	t_limit_semantics();
	t_alternating();
	t_default_path();
	t_purge_age();
	t_purge_release();
	t_statsline();

	printf("\n== %d/%d passed, %d failed ==\n", pass_n, pass_n + fail_n, fail_n);
	return fail_n ? 1 : 0;
}
