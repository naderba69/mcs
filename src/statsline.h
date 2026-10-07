/*
 * statsline.h -- TASK 1.9, the STATS-WINDOW option.
 *
 * One line, once per configured window, that says what the Phase 1 layers have
 * seen and done. Nothing here decides anything: the snapshot is filled by
 * stats_snapshot_fill() in main.c (which owns the tables), the line is rendered
 * by statsline_format() below, and the tick that drives it lives on the cache
 * thread's existing three-second wakeup.
 *
 * Why it exists at all. Everything Phase 1 added is quiet by design: a healthy
 * server logs nothing, which is correct and also indistinguishable from a server
 * whose new code never ran. The counters that prove otherwise are readable from
 * the HTTP thread and, from Phase 4, from the web UI -- neither of which helps an
 * operator watching a log file at two in the morning, which is when a poisoned
 * cache actually gets noticed. This is the cheapest possible answer: one line per
 * window, off unless asked for.
 *
 * Why it is a pure formatter. Same reason dcwstruct.h is a pure scan: the string
 * that an operator will read must be verifiable without a running server, and a
 * printf with eleven arguments is exactly the kind of code that quietly prints a
 * column of zeroes.
 *
 * GR9: no allocation, no locks, one snprintf into a caller-provided buffer, and
 * it runs once per window on a thread that is already awake.
 */
#ifndef MCS_STATSLINE_H
#define MCS_STATSLINE_H

#include <stdint.h>
#include <stdio.h>

/*
 * Every field is a plain counter read without a lock. A value can be one update
 * stale, which for a line that exists to show a trend rather than to be evidence
 * is the same trade the rest of the counters in this project make.
 */
struct stats_snapshot {
	uint32_t window_ms;          /* 0 would mean "off"; the ticker never calls then */

	/* TASK 1.8 -- structural pre-filters as soft signals */
	unsigned long struct_anomalous;   /* deliveries that looked impossible */
	unsigned long struct_suppressed;  /* repeats the dedupe kept quiet */

	/* TASK 1.7 -- TRUSTED-CACHE-FIRST */
	unsigned long deferrals;          /* stored keys left to the card servers */

	/* TASK 1.5 -- cache purge by origin */
	int           purge_live;         /* marks in force right now */
	unsigned long purge_marked;       /* marks placed, cumulative */
	unsigned long purge_released;     /* lifted because the source recovered */
	unsigned long purge_expired;      /* lifted because SERVICE-BLACKLIST-TIME ran out */

	/* TASK 1.6 / 1.9 -- rotation, and the BAD-CW-LIMIT tolerance */
	unsigned long rot_skipped;        /* times a source was actually avoided */
	unsigned long rot_capped;         /* times avoidance was refused by the cap */
	unsigned long rot_below;          /* bad events counted but below the limit */
	int           rot_channels;       /* channels with at least one counted source */
	int           rot_limit;          /* the configured BAD-CW-LIMIT */

	/* TASK 1.4 -- trust */
	int           trust_live;         /* sources with a trust entry */

	/*
	 * TASK 2.1 -- the agreement ledger. Four counters and a miss counter,
	 * because the interesting number is not "how many disputes" but the ratio
	 * between them: a proof is a dispute the client confirmed, and a miss is
	 * one the ledger could not place at all.
	 */
	unsigned long agr_agree;          /* two sources produced the same key */
	unsigned long agr_dispute;        /* two sources disagreed             */
	unsigned long agr_self;           /* one source contradicted itself    */
	unsigned long agr_proof;          /* a contradiction the client confirmed */
	unsigned long agr_miss;           /* failures the ledger could not place */

	/*
	 * TASK 2.2 -- entropy and collision forensics. `impossible` counts keys
	 * that no generator produces (<= 5 distinct byte values); `derived` counts
	 * deliveries within a few bit flips of a key the SAME source sent for
	 * another ECM, and `foreign` the same thing across two sources. The two
	 * are separate because only the first may move a score (GR1).
	 */
	unsigned long cwen_impossible;
	unsigned long cwen_derived;
	unsigned long cwen_foreign;
	unsigned long cwt_fast;          /* replies faster than a card can be    */
	unsigned long cwt_flat;          /* replies that looked like a metronome */
	unsigned long cwt_suppressed;    /* reports the report window kept quiet */

	/*
	 * TASK 2.4 -- plausibility. `full` is the healthy majority (4 of 4 group
	 * sums hold) and is here on purpose: a line that only ever shows the
	 * failures cannot be read as a rate. `one_group` counts the keys that
	 * carry the verdict, `weak` the keys that never had the shape at all --
	 * which is what an honest bouquet that does not use the SUM rule
	 * produces continuously, and is therefore never scored.
	 */
	unsigned long cwp_full;
	unsigned long cwp_one_group;
	unsigned long cwp_weak;
	unsigned long cwp_reports;       /* pattern lines actually written       */

	/*
	 * TASK 2.5 -- cache key cycle/parity visibility (observe only). `cyc_*`
	 * are what a peer OFFERED (keys, of which marked, of which contradicting
	 * the pending ECM's declared half); `cycs_*` are what the cache HANDED to
	 * waiting clients (keys, of which handed despite contradicting, of which
	 * unverifiable because nobody declared a cycle). All observe-only.
	 */
	unsigned long cyc_keys;
	unsigned long cyc_marked;
	unsigned long cyc_contra;
	unsigned long cycs_keys;
	unsigned long cycs_stale;
	unsigned long cycs_unverif;
	unsigned long cyc_reports;

	/*
	 * TASK 2.6 -- negative memory ("never twice"): `neg_live` keys are
	 * currently remembered as proven poison, `neg_marks` have been filed in
	 * total, `neg_hits` offers were refused before any client, `neg_lines`
	 * refusal lines were written.
	 */
	unsigned long neg_live;
	unsigned long neg_marks;
	unsigned long neg_hits;
	unsigned long neg_lines;

	/*
	 * TASK 2.7 -- complement-mirror detection. `cm_keys` scanned, `cm_mirror`
	 * carrying the verdict, `cm_reports` pattern lines written, `cm_suppressed`
	 * patterns inside the report window.
	 */
	unsigned long cm_keys;
	unsigned long cm_mirror;
	unsigned long cm_reports;
	unsigned long cm_suppressed;

	/*
	 * TASK 2.8 -- the cache-peer trust engine, coarse tier. `ta_peers` are
	 * the counterparties the (source, CAID) tier knows, `ta_below` sit below
	 * the actionable line right now, `ta_skipped` cache requests the brake
	 * refused (always 0 unless PEER-TRUST-REQUESTS is ON), `ta_lines` are
	 * the crossing sentences written.
	 */
	unsigned long ta_peers;
	unsigned long ta_below;
	unsigned long ta_skipped;
	unsigned long ta_lines;

	/*
	 * TASK 2.9 -- cache protocol guard. `cg_short` datagrams were shorter
	 * than their type's minimum and were dropped before any parse;
	 * `cg_unknown` packets came from unconfigured senders (counted; stock
	 * drops them exactly as before).
	 */
	unsigned long cg_short;
	unsigned long cg_unknown;

	/*
	 * TASK 2.10 -- the trust lifecycle. `tl_fine`/`tl_coarse`/`tl_neg` are
	 * the restored entry counts (how much standing and how many convictions
	 * survived the last restart), `tl_bad` the snapshot lines refused;
	 * `tl_faded` idle entries relaxed last window; `tl_saves`/`tl_savefails`
	 * the snapshot writes. All zero unless the lifecycle is in use.
	 */
	unsigned long tl_fine;
	unsigned long tl_coarse;
	unsigned long tl_neg;
	unsigned long tl_bad;
	unsigned long tl_faded;
	unsigned long tl_saves;
	unsigned long tl_savefails;
};

/*
 * Render the line. Returns the number of characters written (excluding the
 * terminator), or -1 on a bad argument. Never writes past `outlen`, and always
 * terminates when outlen > 0.
 *
 * The wording is deliberately the operator's, not the code's: "skipped",
 * "released", "below limit" rather than the field names, because the reader is
 * deciding whether their server is behaving, not reading this struct.
 */
static int statsline_format(const struct stats_snapshot *s, char *out, int outlen)
{
	int n;

	if (!s || !out || outlen <= 0) return -1;

	n = snprintf(out, (size_t)outlen,
		" !!! PHASE1 STATS: %us | struct %lu (%lu repeat%s suppressed)"
		" | cache defer %lu"
		" | purge %d live (%lu marked, %lu released, %lu expired)"
		" | rotate %lu skipped, %lu refused, %lu below limit of %d"
		" | trust %d source%s"
		" | agreement %lu agree, %lu dispute, %lu self, %lu proof, %lu unplaced"
		" | keys %lu impossible, %lu near (same), %lu near (other)"
		" | timing %lu fast, %lu flat (%lu repeat%s suppressed)"
		" | shape %lu full, %lu one-group (%lu limited), %lu pattern%s"
		" | cycle offered %lu (%lu marked, %lu contra)"
		", handed %lu (%lu stale, %lu unverified, %lu line%s)"
		" | negative %lu live (%lu proven, %lu refused, %lu line%s)"
		" | mirror %lu keys, %lu complement-built (%lu pattern%s)"
		" | peertrust %lu peer%s, %lu below the line, %lu skipped"
		" | cache guard %lu short, %lu unconfigured"
		" | lifecycle %lu fine, %lu coarse, %lu negative restored, %lu bad"
		" | fade %lu, snapshots %lu ok, %lu failed",
		(unsigned)(s->window_ms / 1000),
		s->struct_anomalous,
		s->struct_suppressed, (s->struct_suppressed == 1) ? "" : "s",
		s->deferrals,
		s->purge_live, s->purge_marked, s->purge_released, s->purge_expired,
		s->rot_skipped, s->rot_capped, s->rot_below, s->rot_limit,
		s->trust_live, (s->trust_live == 1) ? "" : "s",
		s->agr_agree, s->agr_dispute, s->agr_self, s->agr_proof, s->agr_miss,
		s->cwen_impossible, s->cwen_derived, s->cwen_foreign,
		s->cwt_fast, s->cwt_flat, s->cwt_suppressed,
		(s->cwt_suppressed == 1) ? "" : "s",
		s->cwp_full, s->cwp_one_group, s->cwp_weak, s->cwp_reports,
		(s->cwp_reports == 1) ? "" : "s",
		s->cyc_keys, s->cyc_marked, s->cyc_contra,
		s->cycs_keys, s->cycs_stale, s->cycs_unverif,
		s->cyc_reports, (s->cyc_reports == 1) ? "" : "s",
		s->neg_live, s->neg_marks, s->neg_hits,
		s->neg_lines, (s->neg_lines == 1) ? "" : "s",
		s->cm_keys, s->cm_mirror,
		s->cm_reports, (s->cm_reports == 1) ? "" : "s",
		s->ta_peers, (s->ta_peers == 1) ? "" : "s",
		s->ta_below, s->ta_skipped,
		s->cg_short, s->cg_unknown,
		s->tl_fine, s->tl_coarse, s->tl_neg, s->tl_bad,
		s->tl_faded, s->tl_saves, s->tl_savefails);

	if (n < 0) return -1;
	/* snprintf returns what it WOULD have written; report what it did. */
	return (n >= outlen) ? outlen - 1 : n;
}

#endif /* MCS_STATSLINE_H */
