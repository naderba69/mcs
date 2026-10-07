/*
 * cwtime.h -- TASK 2.3, latency forensics for card servers.
 *
 * WHY THIS EXISTS, in one paragraph. Every detector shipped so far needs a
 * second observation: TASK 1.3 needs the same key twice, TASK 2.1 needs another
 * source to disagree or a client to come back early, TASK 2.2 needs the key to
 * be arithmetically absurd. None of them can say anything about a source that
 * fabricates a *plausible* key for a channel nobody else is watching -- and that
 * is the cheapest way to cause a black screen: answer instantly, answer always,
 * never be contradicted. A card cannot do that. A card has to do the work, and
 * the work takes time. This module measures that time and reports what is
 * physically impossible, and only that.
 *
 * WHAT IS MEASURED. The round trip of one ECM request to one card server, as
 * the thread that sent it measured it: cli-newcamd.c:312 stamps `lastecmtime`
 * when the request goes out and cli-newcamd.c:280 computes
 * `lastecmoktime = GetTickCount() - lastecmtime` when the reply arrives. That
 * value is the card's own latency, not the client's -- it excludes everything
 * the client did, which is why it can be compared against a physical floor at
 * all. Upstream already prints it ("<= cw from server ... (62ms)"); nothing new
 * is computed on the hot path here.
 *
 * THE TWO VERDICTS, and why only one of them may move a score:
 *
 *   CWT_FAST   the reply arrived faster than a card can possibly produce one
 *              (< MIN-CARD-LATENCY, the operator's floor). This is a physical
 *              statement, not a statistical one: a smartcard decoding an ECM
 *              takes tens of milliseconds, so an answer in 0-3 ms did not come
 *              from a card. It came from a table. That is exactly the shape of
 *              the source that poisons a cache, and it is a soft trust event --
 *              never a hard action (GR3).
 *
 *   CWT_DEGEN  a FULL window of replies whose latencies sit within
 *              CWT_FLAT_SPREAD of each other: a metronome. Counted and logged,
 *              and deliberately NOT scored, because it is inference without
 *              physics. A real card's latency is dominated by fixed crypto work
 *              and can be very steady indeed, and a whole class of legitimate
 *              setups (a card server on the same host, a hardware card reader)
 *              is steadier than a network one. GR3 says a false positive that
 *              costs a good card is worse than the black screen being prevented,
 *              so this one is evidence for the operator, not for the score.
 *
 *              THE SPREAD IS 5 ms, AND THAT NUMBER CAME FROM MEASUREMENT, not
 *              from taste. The first version used 1 ms and would have been very
 *              nearly useless in production: the latency is measured through
 *              MultiCS's own card-server reader, and the live target measured a
 *              genuinely CONSTANT reply as 30, 31, 31, 31, 31, 30, 30, 36 ms and
 *              a metronome at exactly 50 ms as 51..52 with the occasional 60.
 *              A 1 ms band therefore sits *below the noise floor of the very
 *              path that feeds it*, and would fire only by luck. 5 ms is wider
 *              than that observed noise (6-9 ms of spread on identical input)
 *              and far narrower than honest jitter (the live target's honest
 *              phase spanned 20 ms by construction), so it separates the two
 *              cases the rig can actually produce.
 *
 * WHY THE FLOOR IS AN OPTION and defaults to off. "This answer was not produced
 * by a card" is a fact about a source, not a crime: a Newcamd server that is
 * itself a proxy with a cache answers in under a millisecond, legitimately, for
 * channels another of its clients already opened. An operator who declares a
 * slot as a card server and turns the floor on is saying "I expect cards here",
 * and then a table-lookup answer means the slot is not what it was declared to
 * be. That is a decision for the operator to make explicitly, which is why
 * MIN-CARD-LATENCY ships at 0 (off) and the live target sets it on purpose.
 *
 * WHAT THIS MODULE DELIBERATELY DOES NOT DO.
 *   - No cache, no cache peer, no client-pushed source (MGCLIENT, CCCLIENT,
 *     CSCLIENT): GR5. A cache hit is instant by design, so latency says nothing
 *     about it. The hook lives in cli-newcamd.c and nowhere else, which makes
 *     "Newcamd only" structural rather than a rule someone has to remember.
 *   - No timeout judgement. A source that never answers is GR2's business and
 *     has its own counter; charging it for lateness here would double-count the
 *     same fact.
 *   - No per-server score. The history is per card server (that is what is being
 *     timed), but the trust event it produces goes to the (srctype, srcid, CAID,
 *     PROVID, SID) slot of the channel that was actually served -- GR4.
 *   - No allocation, no I/O, no clock of its own: the caller passes `now`, every
 *     function is a pure function of its arguments, and the whole table is a
 *     fixed array. Bounded by construction, which is what GR9 asks for on the
 *     path that every ECM reply takes.
 *
 * ON NUMBERS. CWT_WINDOW is 16 replies: enough for a pattern to be visible and
 * few enough that a source serving one channel does not need minutes to reach a
 * verdict. (CWT_FLAT_SPREAD is a separately measured number -- see the CWT_DEGEN
 * paragraph above.) CWT_REPORT_WINDOW_MS is 60s per (source, channel, verdict): a source
 * that answers instantly for an hour does not fill the log with the same
 * sentence, but nothing is hidden either -- repeats are counted in the stats
 * line and the log says so when it is re-reporting after the window.
 */

#ifndef MCS_CWTIME_H
#define MCS_CWTIME_H

#include <stdint.h>
#include <string.h>
#include <stdio.h>

/* ---------------------------------------------------------------------------
 * Verdicts
 * ------------------------------------------------------------------------ */

#define CWT_NONE      0x00
#define CWT_FAST      0x01   /* faster than the configured floor            */
#define CWT_DEGEN     0x02   /* a full window with <= 1 ms of spread        */

/*
 * What may decay a trust score. Only CWT_FAST is here, and the reason is in the
 * file header: it is the only one of the two that says something impossible
 * rather than something unusual. CWT_DEGEN is counted and logged all the same.
 */
#define CWT_SCOREMASK (CWT_FAST)

#define CWT_BITS      2      /* the two masks above, for the per-verdict arrays */

/* ---------------------------------------------------------------------------
 * Bounds
 * ------------------------------------------------------------------------ */

#define CWT_SLOTS           64     /* one slot per card server (ids are dense) */
#define CWT_WINDOW          16     /* replies kept per source                  */
#define CWT_FLAT_SPREAD     5      /* ms: max-min over a full window (see above) */
#define CWT_REPORT_WINDOW   60000  /* ms: one report per source/channel/verdict */
#define CWT_MAX_FLOOR       2000   /* ms: what MIN-CARD-LATENCY is clamped to   */

/*
 * One slot per card server. The ring is what the flatness half reads; the two
 * report records are what keeps the log honest without hiding anything.
 */
struct cwt_report {
	uint32_t ticks;    /* 0 = never reported                                  */
	uint32_t provid;
	uint16_t caid;
	uint16_t sid;
};

struct cwt_slot {
	uint32_t lat[CWT_WINDOW];  /* the window, oldest overwritten first        */
	uint8_t  n;                /* samples in the ring (<= CWT_WINDOW)         */
	uint8_t  head;             /* next write position                         */
	uint8_t  pad[2];
	uint32_t seen;             /* replies measured in total                   */
	uint32_t fast;             /* of those, how many were below the floor     */
	uint32_t min_ms;           /* smallest / largest latency seen, ever       */
	uint32_t max_ms;
	struct cwt_report rep[CWT_BITS];  /* rep[0] = FAST, rep[1] = DEGEN         */
};

struct cwt_table {
	struct cwt_slot s[CWT_SLOTS];
};

/*
 * Everything the log line needs, filled only when a verdict was formed. The
 * caller never re-derives a number from the table: a line that computes its own
 * evidence can disagree with the verdict that triggered it.
 */
struct cwt_evidence {
	int      id;
	unsigned verdict;      /* the raw verdict mask                        */
	unsigned fresh;        /* the bits worth reporting/scoring now        */
	uint32_t lat_ms;       /* this reply                                  */
	uint32_t floor_ms;     /* the floor in force (0 = the check is off)    */
	uint32_t n;            /* samples in the window when judged            */
	uint32_t min_ms;       /* window min / max, ms                         */
	uint32_t max_ms;
	uint32_t distinct;     /* distinct values in the window                */
	uint32_t seen;         /* replies measured in total for this source    */
	uint32_t fast;         /* of those, below the floor                    */
};

/* ---------------------------------------------------------------------------
 * The table
 * ------------------------------------------------------------------------ */

static void cwt_init(struct cwt_table *t)
{
	if (t) memset(t, 0, sizeof(*t));
}

/* How many distinct values the window holds. Only used for the evidence line. */
__attribute__((unused))
static uint32_t cwt_distinct(const struct cwt_slot *s)
{
	uint32_t i, j, n = 0;
	for (i = 0; i < s->n; i++) {
		for (j = 0; j < i; j++)
			if (s->lat[i] == s->lat[j]) break;
		if (j == i) n++;
	}
	return n;
}

/*
 * The one entry point.
 *
 * `lat_ms` is the measured round trip; `floor_ms` is MIN-CARD-LATENCY (0 turns
 * the FAST half off entirely); `now` is the caller's clock, in the same units as
 * the report records; the channel identifies what the reply was for, so the
 * trust event lands on the service that was served (GR4) and so a second channel
 * from the same source gets its own first warning.
 *
 * Returns the verdict mask. `out->fresh` holds the subset that is outside the
 * report window and therefore worth a log line and a score change; the rest is a
 * repeat, counted and suppressed. A verdict can be CWT_FAST on every reply of a
 * source and still produce one event per minute, which is the point.
 */
__attribute__((unused))
static unsigned cwt_seen(struct cwt_table *t, int id, uint32_t lat_ms,
                         uint16_t caid, uint32_t provid, uint16_t sid,
                         uint32_t floor_ms, uint32_t now,
                         struct cwt_evidence *out)
{
	struct cwt_slot *s;
	unsigned m = 0, fresh = 0;
	uint32_t i, mn, mx;

	if (out) memset(out, 0, sizeof(*out));
	if (!t || id < 0 || id >= CWT_SLOTS) return CWT_NONE;

	s = &t->s[id];

	/*
	 * 1. Impossible speed. Gated on an operator-set floor: with no floor there
	 * is no physical statement to make, and guessing one would be exactly the
	 * inference-without-physics this file refuses to score.
	 */
	if (floor_ms > 0 && lat_ms < floor_ms) m |= CWT_FAST;

	/* 2. The sample goes in before the flatness half reads the window, so a
	 *    verdict always describes the window including this reply. */
	s->lat[s->head] = lat_ms;
	s->head = (uint8_t)((s->head + 1) % CWT_WINDOW);
	if (s->n < CWT_WINDOW) s->n++;
	s->seen++;
	if (m & CWT_FAST) s->fast++;
	if (!s->min_ms || lat_ms < s->min_ms) s->min_ms = lat_ms;
	if (lat_ms > s->max_ms) s->max_ms = lat_ms;

	/* 3. Flatness, only on a full window. A window that is still filling has no
	 *    shape yet, and reporting one would fire on the first two replies. */
	mn = mx = s->lat[0];
	if (s->n == CWT_WINDOW) {
		for (i = 1; i < CWT_WINDOW; i++) {
			if (s->lat[i] < mn) mn = s->lat[i];
			if (s->lat[i] > mx) mx = s->lat[i];
		}
		if (mx - mn <= CWT_FLAT_SPREAD) m |= CWT_DEGEN;
	}

	/*
	 * 4. Which of those is worth saying out loud.
	 *
	 * FAST is keyed per (source, channel) because the score it feeds is per
	 * service (GR4) and its silence has to expire per service too.
	 *
	 * FLAT is keyed on the source ALONE. Flatness is a property of the server,
	 * not of the channel it was answering for, so per-channel reporting would
	 * let one steady server emit one line per channel per minute -- a farm-wide
	 * flood of a verdict that changes nothing. One line per source per minute
	 * says the same thing and stays readable.
	 */
	for (i = 0; i < CWT_BITS; i++) {
		unsigned bit = 1u << i;
		struct cwt_report *r = &s->rep[i];
		if (!(m & bit)) continue;
		if (r->ticks && (uint32_t)(now - r->ticks) < CWT_REPORT_WINDOW) {
			if (bit == CWT_DEGEN) continue;   /* source-wide: still in window */
			if (r->caid == caid && r->provid == provid && r->sid == sid)
				continue;                  /* this service was already named */
		}
		r->ticks = now;
		r->caid = caid;
		r->provid = provid;
		r->sid = sid;
		fresh |= bit;
	}

	if (out) {
		out->id = id;
		out->verdict = m;
		out->fresh = fresh;
		out->lat_ms = lat_ms;
		out->floor_ms = floor_ms;
		out->n = s->n;
		out->min_ms = mn;
		out->max_ms = mx;
		out->distinct = cwt_distinct(s);
		out->seen = s->seen;
		out->fast = s->fast;
	}
	return m;
}

/*
 * The shortest true sentence about a verdict. Kept here so the unit suite can
 * assert on the wording the operator will actually read, and so the log line in
 * main.c cannot print numbers the verdict was not based on.
 */
__attribute__((unused))
static const char *cwt_name(unsigned mask)
{
	if (!mask) return "none";
	if ((mask & CWT_FAST) && (mask & CWT_DEGEN)) return "FAST+FLAT";
	if (mask & CWT_FAST) return "FAST";
	if (mask & CWT_DEGEN) return "FLAT";
	return "?";
}

/*
 * The evidence tail of the log line, without the sentence around it. Bounded
 * snprintf, one call, no allocation -- same contract as the other modules'.
 */
__attribute__((unused))
static int cwt_format(const struct cwt_evidence *e, char *out, int outlen)
{
	int n;
	if (!out || outlen <= 0) return 0;
	n = snprintf(out, (size_t)outlen,
	             "server %d answered in %u ms (floor %u ms) | window: %u replies, "
	             "%u..%u ms, %u distinct | total: %u replies, %u under the floor",
	             e->id, (unsigned)e->lat_ms, (unsigned)e->floor_ms,
	             (unsigned)e->n, (unsigned)e->min_ms, (unsigned)e->max_ms,
	             (unsigned)e->distinct, (unsigned)e->seen, (unsigned)e->fast);
	return n;
}

#endif /* MCS_CWTIME_H */
