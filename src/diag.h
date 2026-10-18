/*
 * diag.h -- TASK R47 (D110): the diagnosis layer behind the /diag page.
 *
 * Pure: no sockets, no locks, no globals, no output. The page gathers two
 * sources -- the per-channel table (chanhealth.h) and the refusal ring
 * (cwlog.h) -- and asks this header what the evidence says. It never touches
 * a cache or card decision. It names a finding and a suggestion; any change
 * to a profile or a setting is the operator's choice.
 *
 * The rules are deterministic: the same evidence always gives the same
 * findings, and each finding is a plain threshold, not a score. Thresholds
 * are the DIAG_* constants below, so a test pins them.
 *
 * Stats flavour only (MCS_DCWSTATS): the page and this header are compiled
 * into the stats binaries, never into stock.
 */
#ifndef MCS_DIAG_H
#define MCS_DIAG_H

#include <stdint.h>
#include <stdio.h>
#include "cwlog.h"
#include "chanhealth.h"

#define DIAG_MIN_SAMPLE    5    /* verdicts before any judgement is made  */
#define DIAG_DYING_STREAK  CHANHEALTH_STREAK  /* same bar as the alert */
#define DIAG_SHARE_PCT     50   /* a cause owning this share of refusals  */
#define DIAG_HEALTHY_PCT   90   /* delivered share at or above: healthy   */
#define DIAG_DOMINANT_PCT  70   /* one peer owning this share of refusals */
#define DIAG_REASONS       (CWLOG_R_PATTERN + 1)   /* ring reasons 0..20 */

/* One bit per finding. A channel can carry several at once. */
enum diag_finding {
	DIAG_OK = 0,		/* the channel delivers                          */
	DIAG_LOW_SAMPLE,	/* too few verdicts to judge                     */
	DIAG_DYING,		/* a refusal streak: failing now                 */
	DIAG_CHECKSUM,		/* keys fail the checksum                        */
	DIAG_SHAPE,		/* keys look fake: null, repeat, list, cycle, pattern */
	DIAG_PROFILE,		/* the profile's own filter refuses              */
	DIAG_CONSENSUS,		/* peers disagree, or a convicted memory        */
	DIAG_CEX,		/* CACHEEX gates refuse                         */
	DIAG_TIMING,		/* the card is too fast (MINTIME) or too late   */
	DIAG_TRUST,		/* an untrusted peer may not store or relay     */
	DIAG_THRESHOLD,		/* below CACHE THRESHOLD                        */
	DIAG_HELD,		/* consensus holds: waiting for a second voter   */
	DIAG_DISPUTED,		/* the card contradicted the cache               */
	DIAG_MIXED,		/* refusals with no single cause                 */
	DIAG_FINDINGS
};

/* What the page knows about one channel. */
struct diag_evidence {
	uint32_t delivered;		/* channel table: since start or reset */
	uint32_t refused;		/* channel table                       */
	uint32_t held;			/* channel table                       */
	uint32_t streak;		/* channel table: refusals since the last delivery */
	uint32_t disputed;		/* ring window: DISPUTED verdicts      */
	uint32_t reason[DIAG_REASONS];	/* ring window: REFUSED rows by reason */
};

/*
 * The finding a ring reason belongs to. Reason 10 is CWLOG_R_HOLD and is
 * never a refusal; the pattern refusal from the card path is 20. Keeping
 * those two apart is the point of the R47 ring codes (see cwlog.h).
 */
__attribute__((unused))
static enum diag_finding diag_reason_finding(int reason)
{
	switch (reason) {
	case 1:  return DIAG_CHECKSUM;               /* checksum            */
	case 2: case 3: case 4: case 8: case 20:
		return DIAG_SHAPE;                        /* null, repeat, list, cycle, pattern */
	case 5: case 6: case 7:  return DIAG_CEX;    /* CACHEEX gates       */
	case 9: case 11:         return DIAG_CONSENSUS;
	case CWLOG_R_HOLD:       return DIAG_HELD;   /* 10: a hold, never a refusal */
	case 12:                 return DIAG_THRESHOLD;
	case 13:                 return DIAG_PROFILE;
	case 14:                 return DIAG_DISPUTED;
	case 15: case 18: case 19:
		return DIAG_TIMING;                       /* late key, too fast, too late */
	case 16: case 17:        return DIAG_TRUST;
	default:                 return DIAG_MIXED;
	}
}

/* Severity of a finding: 2 = act now, 1 = look, 0 = information only. */
__attribute__((unused))
static int diag_severity(enum diag_finding f)
{
	switch (f) {
	case DIAG_DYING:
		return 2;
	case DIAG_CHECKSUM: case DIAG_SHAPE: case DIAG_CONSENSUS:
	case DIAG_CEX: case DIAG_TRUST: case DIAG_DISPUTED: case DIAG_MIXED:
		return 1;
	default:
		return 0;
	}
}

/* part of whole, at or above pct percent. Never true for an empty whole. */
__attribute__((unused))
static int diag_pct_ge(uint32_t part, uint32_t whole, uint32_t pct)
{
	return whole > 0 && part * 100u >= whole * pct;
}

/*
 * The findings of one channel, as a bitmask (1 << finding). With too few
 * verdicts the only finding is LOW_SAMPLE. A dying channel keeps its
 * DYING bit even when its totals look healthy: the streak is the recent
 * truth, the totals are the history.
 */
__attribute__((unused))
static uint32_t diag_classify(const struct diag_evidence *e)
{
	uint32_t total = e->delivered + e->refused + e->held;
	uint32_t by[DIAG_FINDINGS];
	uint32_t rtot = 0, mask = 0, causes = 0;
	int i, f;

	if (total < DIAG_MIN_SAMPLE)
		return 1u << DIAG_LOW_SAMPLE;

	if (e->streak >= DIAG_DYING_STREAK)
		mask |= 1u << DIAG_DYING;

	if (diag_pct_ge(e->delivered, total, DIAG_HEALTHY_PCT) && !mask)
		return 1u << DIAG_OK;

	for (f = 0; f < DIAG_FINDINGS; f++) by[f] = 0;
	for (i = 0; i < DIAG_REASONS; i++) {
		if (i == 0 || i == CWLOG_R_HOLD) continue;  /* not refusals */
		by[diag_reason_finding(i)] += e->reason[i];
		rtot += e->reason[i];
	}
	/* The shares use the ring window; it is the only place reasons exist. */
	if (rtot >= DIAG_MIN_SAMPLE)
		for (f = DIAG_CHECKSUM; f < DIAG_MIXED; f++)
			if (f != DIAG_HELD && f != DIAG_DISPUTED &&
			    diag_pct_ge(by[f], rtot, DIAG_SHARE_PCT)) {
				mask |= 1u << f;
				causes++;
			}
	if (e->disputed > 0 && !(mask & (1u << DIAG_DISPUTED))) {
		mask |= 1u << DIAG_DISPUTED;
		causes++;
	}
	if (diag_pct_ge(e->held, total, DIAG_SHARE_PCT)) {
		mask |= 1u << DIAG_HELD;
		causes++;
	}
	if (!causes && e->refused > 0)
		mask |= 1u << DIAG_MIXED;
	if (!causes && !e->refused && !mask)
		mask |= 1u << DIAG_MIXED;
	return mask;
}

/* The worst severity in a finding mask. */
__attribute__((unused))
static int diag_worst(uint32_t mask)
{
	int f, w = 0;

	for (f = 0; f < DIAG_FINDINGS; f++)
		if ((mask & (1u << f)) && diag_severity(f) > w)
			w = diag_severity(f);
	return w;
}

/*
 * The peer owning most of the refusals, as an index into count[], or -1.
 * Needs DIAG_MIN_SAMPLE refusals in all and a DIAG_DOMINANT_PCT share for
 * the leader. A tie is not dominance: the first of equal maxima would
 * otherwise win by table order, so a tie returns -1.
 */
__attribute__((unused))
static int diag_dominant(const uint32_t *count, int n)
{
	uint32_t total = 0, best = 0;
	int i, bi = -1, ties = 0;

	for (i = 0; i < n; i++) {
		total += count[i];
		if (count[i] > best) { best = count[i]; bi = i; ties = 1; }
		else if (count[i] == best && best > 0) ties++;
	}
	if (ties != 1 || total < DIAG_MIN_SAMPLE)
		return -1;
	return diag_pct_ge(best, total, DIAG_DOMINANT_PCT) ? bi : -1;
}

/* Short Arabic name of a finding, for the page. */
__attribute__((unused))
static const char *diag_label(enum diag_finding f)
{
	static const char *const name[DIAG_FINDINGS] = {
		"يعمل", "عينة قليلة", "يفشل الآن", "فشل فحص checksum",
		"مفاتيح تبدو مزيفة", "بوابة البروفايل تمنع", "خلاف بين النظراء",
		"بوابة CACHEEX تمنع", "توقيت البطاقة", "نظير غير موثوق",
		"أقل من CACHE THRESHOLD", "محجوز بانتظار مؤكد",
		"البطاقة تناقض الكاش", "أسباب متفرقة"
	};
	return (f >= 0 && f < DIAG_FINDINGS) ? name[f] : "?";
}

/*
 * What to consider doing. A suggestion only: the page shows it, and nothing
 * in the server acts on it. Every entry says what to check first, because a
 * fake-key or checksum finding is usually a source problem, not a switch.
 */
__attribute__((unused))
static const char *diag_advice(enum diag_finding f)
{
	static const char *const text[DIAG_FINDINGS] = {
		"القناة تعمل. لا إجراء.",
		"عدد المحاولات أقل من 5. انتظر حتى تتراكم أحكام أكثر.",
		"القناة تفشل حالياً دون توقف. راجع الأسباب أدناه وقارن بقناة أخرى على نفس الباقة.",
		"المفاتيح تفشل فحص checksum. قبل تعطيل الفحص، تأكد أن الخلل ليس في البطاقة أو المصدر: قارن بقناة أخرى على نفس الباقة.",
		"المفاتيح تبدو مزيفة (صفرية أو متكررة أو في القائمة السوداء). راجع المصدر والبطاقة؛ لا تعطّل الفحص لأنه ما يحميك.",
		"فلتر البروفايل هو من يمنع المفاتيح. إن كانت القناة مقصودة بهذا الفلتر فالسلوك متوقع، وإلا راجع إعدادات البروفايل.",
		"النظراء يختلفون في المفتاح. راجع النظير المصدر، ولا تغيّر الإجماع من هذه الصفحة.",
		"بوابات CACHEEX تمنع المفاتيح. راجع إعدادات CACHEEX للنظير المعني.",
		"البطاقة أسرع من MINTIME أو أبطأ من VALIDECMTIME. راجع سرعة الخادم، أو حد VALIDECMTIME لبروفايل هذه القناة.",
		"نظير غير موثوق يمنع التخزين أو إعادة البث. راجع TRUST للنظير.",
		"المفاتيح أقل من حد CACHE THRESHOLD. راجع هذا الحد.",
		"مفاتيح محجوزة بانتظار مؤكد ثانٍ. إن طال الحجز فراجع عدد النظراء المؤكدة.",
		"البطاقة تناقض الكاش. قارن النظير المسؤول في /cwlog؛ القرار لك.",
		"أسباب متفرقة بلا سبب غالب. راجع السجل الخام في /cwlog."
	};
	return (f >= 0 && f < DIAG_FINDINGS) ? text[f] : "?";
}

/*
 * TASK R48 (D111): one setting to CONSIDER, written out so the operator can
 * copy it by hand. The server never applies it. A finding with no safe,
 * documented key gets an empty string, and the page then offers nothing to
 * copy. Only keys the project already documents are used:
 *   DIAG_DYING   -> ECMLOOPING: YES      (the general section, D108)
 *   DIAG_TIMING  -> card late:    DCW VALIDECMTIME: <slowest measured answer + 25%, up to 100 ms>
 *                   card too fast: DCW MINTIME: <below the fastest answer>
 *                   no measured answer yet: a hint to measure first
 *   DIAG_CHECKSUM and the rest -> nothing. Turning the checksum off is never
 *   offered from this page (see the advice text, and D110).
 * Returns 1 when out holds a line.
 */
static int diag_setting_hint(enum diag_finding f, int top_reason,
			     uint32_t lat_n, uint32_t lat_max, char *out, int len)
{
	uint32_t v;

	out[0] = 0;
	switch (f) {
	case DIAG_DYING:
		snprintf(out, len, "ECMLOOPING: YES");
		break;
	case DIAG_TIMING:
		if (top_reason == CWLOG_R_MINTIME)
			snprintf(out, len, "DCW MINTIME: (below the fastest card answer)");
		else if (lat_n) {
			v = (lat_max + lat_max / 4 + 99) / 100 * 100;
			snprintf(out, len, "DCW VALIDECMTIME: %lu", (unsigned long)v);
		}
		else
			snprintf(out, len, "(measure first: add the channel to diagscope)");
		break;
	default:
		break;
	}
	return out[0] != 0;
}

#endif /* MCS_DIAG_H */
