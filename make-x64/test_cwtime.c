/*
 * test_cwtime.c -- TASK 2.3
 *
 * Exercises the REAL ../src/cwtime.h. The header is a pure function of its
 * arguments and holds its own fixed table, so the whole of it -- ring, window,
 * verdicts, report windows, evidence -- is testable with no server, no threads
 * and no real clock. `now` is a parameter everywhere, which is what makes the
 * "one report per minute" rule a testable fact instead of a hope.
 *
 * WHAT THIS SUITE IS FOR. Not "does it fire" -- that part is easy to get right
 * and easy to see. The work is in the negative space: a timing detector that
 * accuses a real card is worse than no detector, because the operator then has
 * to choose between a working source and a log line they cannot trust. So the
 * heavy half of this file is the evidence that steady, slow, jittery, mixed and
 * short-windowed traffic all stay silent, and that the one verdict allowed to
 * move a score is the one that describes something a card physically cannot do.
 *
 *   make -C make-x64 test
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../src/cwtime.h"

static int pass_n = 0, fail_n = 0;

#define CHECK(cond, name) do { \
	if (cond) { pass_n++; printf("  [ ok ] %s\n", name); } \
	else { fail_n++; printf("  [FAIL] %s\n", name); } \
} while (0)

/* ------------------------------------------------------------------------ */
/* Premises: the invariants the rest of the file relies on                  */
/* ------------------------------------------------------------------------ */

static void t_premises(void)
{
	struct cwt_table t;

	printf("-- 1. premises --\n");

	/* A window that is not a power of two is fine, but the ring arithmetic
	 * (head % CWT_WINDOW) is only tested at the real size, so pin it. */
	CHECK(CWT_WINDOW == 16, "the window is the 16 replies the header documents");
	/*
	 * The band is pinned on purpose. It is a measured constant (the noise floor
	 * of the path that produces the measurement -- see the header), and a change
	 * to it should be a decision someone makes deliberately, with fresh numbers,
	 * not something that drifts because a test stopped looking.
	 */
	CHECK(CWT_FLAT_SPREAD == 5, "the flatness band is the measured 5 ms, not 1 and not 20");
	CHECK(CWT_SCOREMASK == CWT_FAST,
	      "only FAST may move a score; FLAT is evidence for the log (GR3)");
	CHECK((CWT_FAST & CWT_DEGEN) == 0, "the two verdicts are separate bits");
	CHECK(CWT_SLOTS >= 32, "the table holds more sources than a real config has");

	/* The table must be .bss-sized, not heap-sized: GR9. */
	printf("  sizeof(cwt_table) = %u, cwt_slot = %u, cwt_evidence = %u\n",
	       (unsigned)sizeof(struct cwt_table),
	       (unsigned)sizeof(struct cwt_slot),
	       (unsigned)sizeof(struct cwt_evidence));
	CHECK(sizeof(struct cwt_table) < 16384, "the whole table is a few KB, not a heap");

	cwt_init(&t);
	CHECK(t.s[0].n == 0 && t.s[0].seen == 0 && t.s[0].head == 0,
	      "init leaves an empty ring (and it is what .bss already held)");
}

/* ------------------------------------------------------------------------ */
/* FAST: the only physical statement this module makes                      */
/* ------------------------------------------------------------------------ */

static void t_fast(void)
{
	struct cwt_table t;
	struct cwt_evidence ev;
	unsigned m;
	uint32_t now = 1000000;

	printf("-- 2. faster than a card can be --\n");

	cwt_init(&t);

	/* The premise of the whole verdict: with no floor configured there is no
	 * physical statement to make, so nothing fires however fast the answer. */
	m = cwt_seen(&t, 1, 0, 0x1884, 0, 0x64, 0, now, &ev);
	CHECK(m == CWT_NONE, "floor 0 (the option off): a 0 ms reply says nothing");
	CHECK(ev.fresh == 0, "and nothing is reported");

	/* With a floor, the same reply is impossible. */
	m = cwt_seen(&t, 1, 0, 0x1884, 0, 0x64, 20, now, &ev);
	CHECK((m & CWT_FAST) != 0, "floor 20: a 0 ms reply is CWT_FAST");
	CHECK((ev.fresh & CWT_FAST) != 0, "the first one is fresh, so it is reported");
	CHECK(ev.lat_ms == 0 && ev.floor_ms == 20, "the evidence carries both numbers");
	CHECK(ev.fast == 1 && ev.seen == 2,
	      "the counters say 1 under the floor out of 2 measured");

	/* The boundary, both sides. */
	cwt_init(&t);
	m = cwt_seen(&t, 1, 19, 0x1884, 0, 0x64, 20, now, &ev);
	CHECK((m & CWT_FAST) != 0, "19 ms with a 20 ms floor is still impossible");
	cwt_init(&t);
	m = cwt_seen(&t, 1, 20, 0x1884, 0, 0x64, 20, now, &ev);
	CHECK((m & CWT_FAST) == 0, "20 ms with a 20 ms floor is not: the test is < floor");
	cwt_init(&t);
	m = cwt_seen(&t, 1, 21, 0x1884, 0, 0x64, 20, now, &ev);
	CHECK(m == CWT_NONE, "21 ms is a normal reply and produces nothing at all");

	/* A card that answers slowly never trips it, however many replies. */
	cwt_init(&t);
	{
		int i; unsigned hits = 0;
		for (i = 0; i < 64; i++)
			if (cwt_seen(&t, 1, (uint32_t)(60 + (i % 7)), 0x1884, 0, 0x64,
			             20, now + (uint32_t)i * 100, &ev) & CWT_FAST) hits++;
		CHECK(hits == 0, "64 honest replies (60-66 ms, 20 ms floor): zero verdicts");
		CHECK(ev.fast == 0 && ev.seen == 64, "and the counters agree");
		CHECK(ev.min_ms == 60 && ev.max_ms == 66,
		      "the lifetime min/max are the real ones, not the window's");
	}
}

/* ------------------------------------------------------------------------ */
/* The report window: saying it once, not once per reply                    */
/* ------------------------------------------------------------------------ */

static void t_report_window(void)
{
	struct cwt_table t;
	struct cwt_evidence ev;
	unsigned m, reports = 0;
	uint32_t now = 5000;
	int i;

	printf("-- 3. one report per source and channel per minute --\n");

	cwt_init(&t);

	/* 200 identical impossible replies inside one minute. */
	for (i = 0; i < 200; i++) {
		m = cwt_seen(&t, 2, 1, 0x1884, 0, 0x64, 20, now, &ev);
		if (ev.fresh & CWT_FAST) reports++;
	}
	CHECK(reports == 1, "200 impossible replies in one minute earn ONE log line");
	CHECK(ev.verdict & CWT_FAST, "but the verdict is still formed on every reply");
	CHECK(ev.seen == 200 && ev.fast == 200, "and every one of them is counted");
	CHECK(m & CWT_FAST, "the last call reports the verdict as well");

	/* After the window, the source is re-reported: a faker is never forgotten,
	 * it is just not repeated. */
	now += CWT_REPORT_WINDOW;
	m = cwt_seen(&t, 2, 1, 0x1884, 0, 0x64, 20, now, &ev);
	CHECK((ev.fresh & CWT_FAST) != 0, "a minute later it is said again");

	/* A different channel is a different accusation, so it gets its own line:
	 * the score moves per service (GR4), and the log has to match the score. */
	m = cwt_seen(&t, 2, 1, 0x1884, 0, 0x65, 20, now, &ev);
	CHECK((ev.fresh & CWT_FAST) != 0, "the same source on another SID is reported separately");
	m = cwt_seen(&t, 2, 1, 0x1884, 0, 0x65, 20, now, &ev);
	CHECK((ev.fresh & CWT_FAST) == 0, "and that one is then suppressed in its turn");

	/* A different provider is a different service too. */
	m = cwt_seen(&t, 2, 1, 0x1884, 0x000001, 0x65, 20, now, &ev);
	CHECK((ev.fresh & CWT_FAST) != 0, "a different PROVID is a different service");

	/* Another source is another slot: one source's minute must not silence
	 * another's first warning. */
	m = cwt_seen(&t, 3, 1, 0x1884, 0, 0x64, 20, now, &ev);
	CHECK((ev.fresh & CWT_FAST) != 0, "a second source gets its own first report");

	/* Out of range: never judge, never crash, never silently invent a slot. */
	cwt_init(&t);
	CHECK(cwt_seen(&t, -1, 1, 0x1884, 0, 0x64, 20, now, &ev) == CWT_NONE,
	      "a negative source id is refused, not indexed");
	CHECK(cwt_seen(&t, CWT_SLOTS, 1, 0x1884, 0, 0x64, 20, now, &ev) == CWT_NONE,
	      "an id past the table is refused too");
	CHECK(cwt_seen(NULL, 1, 1, 0x1884, 0, 0x64, 20, now, &ev) == CWT_NONE,
	      "a NULL table is refused");
	CHECK(cwt_seen(&t, 1, 1, 0x1884, 0, 0x64, 20, now, NULL) == CWT_FAST,
	      "and a NULL evidence pointer is allowed (the verdict is still formed)");
}

/* ------------------------------------------------------------------------ */
/* FLAT: counted, logged, and deliberately never scored                     */
/* ------------------------------------------------------------------------ */

static void t_flat(void)
{
	struct cwt_table t;
	struct cwt_evidence ev;
	unsigned m;
	int i;
	uint32_t now = 0;

	printf("-- 4. a metronome is evidence, not proof --\n");

	/* The verdict itself: a full window with no spread at all. */
	cwt_init(&t);
	for (i = 0; i < CWT_WINDOW - 1; i++)
		m = cwt_seen(&t, 4, 40, 0x1884, 0, 0x64, 0, now + (uint32_t)i, &ev);
	CHECK((m & CWT_DEGEN) == 0,
	      "the 15th identical reply is still not a flat window: it is not full yet");
	CHECK(ev.n == CWT_WINDOW - 1, "and the evidence says how full it is");

	m = cwt_seen(&t, 4, 40, 0x1884, 0, 0x64, 0, now + 100, &ev);
	CHECK((m & CWT_DEGEN) != 0, "the 16th makes it flat");
	CHECK(ev.min_ms == 40 && ev.max_ms == 40 && ev.distinct == 1,
	      "the evidence names the window: 40..40 ms, one distinct value");

	/* The band, both sides. The jitter pattern is built so the window's min and
	 * max are exactly the values named, rather than hoping a modulus produces
	 * them. */
	cwt_init(&t);
	for (i = 0; i < CWT_WINDOW; i++)
		m = cwt_seen(&t, 4, (uint32_t)(40 + (i % 2)), 0x1884, 0, 0x64, 0,
		             now + (uint32_t)i, &ev);
	CHECK((m & CWT_DEGEN) != 0, "a 1 ms spread over a full window is a metronome");

	cwt_init(&t);
	for (i = 0; i < CWT_WINDOW; i++)
		m = cwt_seen(&t, 4, (uint32_t)((i == CWT_WINDOW - 1) ? 45 : 40),
		             0x1884, 0, 0x64, 0, now + (uint32_t)i, &ev);
	CHECK((m & CWT_DEGEN) != 0, "and so is a 5 ms spread: the band is the measured noise floor");

	cwt_init(&t);
	for (i = 0; i < CWT_WINDOW; i++)
		m = cwt_seen(&t, 4, (uint32_t)((i == CWT_WINDOW - 1) ? 46 : 40),
		             0x1884, 0, 0x64, 0, now + (uint32_t)i, &ev);
	CHECK((m & CWT_DEGEN) == 0, "6 ms is outside the band: that is a card, not a clock");

	cwt_init(&t);
	for (i = 0; i < CWT_WINDOW; i++)
		m = cwt_seen(&t, 4, (uint32_t)((i % 2) ? 40 : 60), 0x1884, 0, 0x64, 0,
		             now + (uint32_t)i, &ev);
	CHECK((m & CWT_DEGEN) == 0, "a 20 ms spread is honest jitter and stays silent");

	/* And FLAT alone must never be able to move a score, whatever it says. */
	CHECK((CWT_DEGEN & CWT_SCOREMASK) == 0,
	      "CWT_DEGEN is not in the scoring mask (the whole point of the refusal)");

	/* A steady source also has to be re-reported only once per window. */
	cwt_init(&t);
	for (i = 0; i < 100; i++)
		m = cwt_seen(&t, 4, 40, 0x1884, 0, 0x64, 0, now + (uint32_t)i, &ev);
	CHECK((ev.fresh & CWT_DEGEN) == 0,
	      "once reported, a steady source does not repeat itself every reply");
	CHECK((ev.fresh & CWT_FAST) == 0, "and it never becomes FAST: there is no floor");

	/* Both verdicts at once is a legal combination and names itself. */
	CHECK(!strcmp(cwt_name(CWT_FAST | CWT_DEGEN), "FAST+FLAT"),
	      "a source can be both fast and flat, and the name says so");
}

/* ------------------------------------------------------------------------ */
/* The negative space: everything a real card does                          */
/* ------------------------------------------------------------------------ */

static void t_no_false_positives(void)
{
	struct cwt_table t;
	struct cwt_evidence ev;
	unsigned m, hits = 0;
	int i;
	uint32_t now = 0;

	printf("-- 5. what a real card looks like --\n");

	/* 200 replies of a card with honest jitter around 60 ms, floor on. The
	 * 9 ms span here is deliberately just outside the flatness band too, so this
	 * case exercises both halves of the file's promise at once. */
	cwt_init(&t);
	for (i = 0; i < 200; i++) {
		uint32_t lat = (uint32_t)(57 + (i * 7) % 9);   /* 57..65, no pattern */
		m = cwt_seen(&t, 5, lat, 0x1884, 0, 0x64, 20, now + (uint32_t)i * 10, &ev);
		if (m) hits++;
	}
	CHECK(hits == 0, "200 jittery honest replies: not one verdict of any kind");

	/* A card that starts fast and settles: the window has to fill with the real
	 * latency before anything could be said about it. */
	cwt_init(&t);
	for (i = 0; i < CWT_WINDOW; i++)
		m = cwt_seen(&t, 5, (uint32_t)(i * 3), 0x1884, 0, 0x64, 0,
		             now + (uint32_t)i, &ev);
	CHECK((m & CWT_DEGEN) == 0, "a warming-up card is not a metronome");

	/* An occasional impossible-looking one: a single 0 ms reply among honest
	 * ones is reported once and never again for that channel in the window --
	 * the module does not decide whether it was a measurement artefact, and it
	 * does not escalate. */
	/*
	 * A card whose latency is genuinely constant. This is the single most
	 * important case in the file: such a card DOES trip FLAT -- real hardware
	 * can be that regular -- and it must never be able to trip anything that
	 * moves a score. That is the whole reason FLAT is excluded from
	 * CWT_SCOREMASK, and this assertion is where that decision is checked.
	 */
	cwt_init(&t);
	for (i = 0; i < 30; i++)
		m = cwt_seen(&t, 6, 60, 0x1884, 0, 0x64, 20, now + (uint32_t)i, &ev);
	CHECK((m & CWT_FAST) == 0, "a steady 60 ms card is never FAST: 60 > the 20 ms floor");
	CHECK((m & CWT_DEGEN) != 0, "but it does look flat, which is why FLAT cannot score");
	CHECK((CWT_DEGEN & CWT_SCOREMASK) == 0, "and FLAT is not in the scoring mask");

	/* Flatness is a property of the server, not of the channel: a busy steady
	 * server must not emit one line per channel. */
	cwt_init(&t);
	for (i = 0; i < CWT_WINDOW; i++)
		cwt_seen(&t, 6, 60, 0x1884, 0, 0x64, 20, now + (uint32_t)i, &ev);
	CHECK((ev.fresh & CWT_DEGEN) != 0, "the first flat window is reported");
	for (i = 0; i < CWT_WINDOW; i++)
		cwt_seen(&t, 6, 60, 0x1884, 0, 0x65, 20, now + 100 + (uint32_t)i, &ev);
	CHECK((ev.fresh & CWT_DEGEN) == 0,
	      "and a different channel does not make the same server flat AGAIN");
	for (i = 0; i < CWT_WINDOW; i++)
		cwt_seen(&t, 6, 60, 0x1884, 0, 0x64, 20, now + CWT_REPORT_WINDOW + (uint32_t)i, &ev);
	CHECK((ev.fresh & CWT_DEGEN) != 0, "but after the window it is said once more");

	/* A reply with no request behind it: srv->lastecmtime is 0 before the first
	 * request is sent, and the caller must not hand us the resulting nonsense.
	 * Here we prove the module is harmless if it does: the value is just a
	 * number, and a huge one is above any floor. */
	m = cwt_seen(&t, 6, 0xFFFFFFFFu, 0x1884, 0, 0x64, 20, now + 100, &ev);
	CHECK((m & CWT_FAST) == 0, "an absurdly large latency is not treated as fast");

	/* Interleaved sources must not share a window. */
	cwt_init(&t);
	{
		unsigned a = 0, b = 0;
		for (i = 0; i < CWT_WINDOW; i++) {
			m = cwt_seen(&t, 7, 40, 0x1884, 0, 0x64, 0, now + (uint32_t)i, &ev);
			if (m & CWT_DEGEN) a = 1;
			m = cwt_seen(&t, 8, (uint32_t)(40 + i), 0x1884, 0, 0x64, 0,
			             now + (uint32_t)i, &ev);
			if (m & CWT_DEGEN) b = 1;
		}
		CHECK(a == 1, "the steady source in slot 7 is judged on its own replies");
		CHECK(b == 0, "and the jittery one in slot 8 is not tainted by it");
	}
}

/* ------------------------------------------------------------------------ */
/* The evidence line                                                        */
/* ------------------------------------------------------------------------ */

static void t_evidence(void)
{
	struct cwt_table t;
	struct cwt_evidence ev;
	char line[256];
	int n, i;

	printf("-- 6. the evidence --\n");

	cwt_init(&t);
	for (i = 0; i < 4; i++)
		cwt_seen(&t, 9, 2, 0x1884, 0, 0x64, 20, 1000 + (uint32_t)i, &ev);

	CHECK(cwt_seen(&t, 9, 2, 0x1884, 0, 0x64, 20, 1004, &ev) != 0,
	      "the fifth impossible reply carries a verdict to format");

	n = cwt_format(&ev, line, (int)sizeof(line));
	CHECK(n > 0, "the formatter produces a line");
	CHECK(strstr(line, "server 9") != NULL, "it names the source");
	CHECK(strstr(line, "in 2 ms") != NULL && strstr(line, "floor 20 ms") != NULL,
	      "it names the measurement and the floor it was judged against");
	CHECK(strstr(line, "5 replies") != NULL, "it says how many replies the window holds");
	CHECK(strstr(line, "5 under the floor") != NULL,
	      "and how many of the source's replies were impossible (all five here)");
	CHECK(strstr(line, "1 distinct") != NULL, "and how regular the window is");

	/* Bounded, and the truncation contract every other module in this tree
	 * uses: the return is the length that would have been written. */
	/*
	 * The truncation contract: snprintf's return is the length it would have
	 * written, so a caller can always tell truncation from success. 128 bytes
	 * is used rather than something tiny only so that the compiler does not
	 * report the deliberate truncation as a compile-time defect.
	 */
	{
		/*
		 * `cap` is opaque on purpose: this IS the truncation test, and a
		 * compiler that folds the constant reports the deliberate truncation
		 * as a compile-time defect. The contract being checked is snprintf's:
		 * the return is the length it WOULD have written, so the caller can
		 * always tell a truncated line from a complete one.
		 */
		volatile int cap = 40;
		char small[64];
		n = cwt_format(&ev, small, cap);
		CHECK(n > cap && strlen(small) == (size_t)(cap - 1),
		      "a short buffer truncates cleanly and the return says so");
	}
	CHECK(cwt_format(&ev, NULL, 32) == 0, "a NULL buffer is refused, not written to");

	CHECK(!strcmp(cwt_name(0), "none"), "no verdict names itself 'none'");
	CHECK(!strcmp(cwt_name(CWT_FAST), "FAST"), "FAST names itself");
	CHECK(!strcmp(cwt_name(CWT_DEGEN), "FLAT"), "DEGEN is shown as FLAT");
	CHECK(!strcmp(cwt_name(CWT_FAST | CWT_DEGEN), "FAST+FLAT"),
	      "and both together name both");
}

int main(void)
{
	printf("== TASK 2.3 -- card-server latency forensics ==\n\n");

	t_premises();
	t_fast();
	t_report_window();
	t_flat();
	t_no_false_positives();
	t_evidence();

	printf("\n== %d/%d passed, %d failed ==\n", pass_n, pass_n + fail_n, fail_n);
	return fail_n ? 1 : 0;
}
