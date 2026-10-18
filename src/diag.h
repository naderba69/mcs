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
#define DIAG_NOANSWER_PCT  30   /* TASK R65: no-answer share of attempts at or above */
#define DIAG_SLOW_MS       1000 /* TASK R65: average card answer at or above (ms)   */
/* DCWFILTER values, as dcw.h defines them: the profile's checksum tristate. */
#define DIAG_CSUM_INHERIT  0
#define DIAG_CSUM_ON       1
#define DIAG_CSUM_OFF      2

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
	DIAG_NOANSWER,		/* TASK R65: the card or server does not answer  */
	DIAG_SLOW,		/* TASK R65: the card answers, but slowly        */
	DIAG_FINDINGS
};

/* What the page knows about one channel. */
struct diag_evidence {
	uint32_t noanswer;		/* TASK R65: channel table, detail scope only */
	uint32_t lat_n;			/* TASK R65: answers measured (detail scope)  */
	uint32_t lat_avg;		/* TASK R65: average answer ms               */
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
	case DIAG_NOANSWER: case DIAG_SLOW:
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

	/* TASK R65: two findings from the table's own measured counters. */
	if (e->noanswer && diag_pct_ge(e->noanswer, total + e->noanswer, DIAG_NOANSWER_PCT))
		mask |= 1u << DIAG_NOANSWER;
	if (e->lat_n >= DIAG_MIN_SAMPLE && e->lat_avg >= DIAG_SLOW_MS)
		mask |= 1u << DIAG_SLOW;

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
		"البطاقة تناقض الكاش", "أسباب متفرقة", "بلا جواب من البطاقة", "بطاقة بطيئة"
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
		"أسباب متفرقة بلا سبب غالب. راجع السجل الخام في /cwlog.",
		"البطاقة أو الخادم لا يجيب على نسبة من الطلبات. راجع الاتصال بالبطاقة أو الخادم، و SERVER TIMEOUT. لا تغيّر من هذه الصفحة. هذا الحكم يظهر بالتفاصيل فقط.",
		"متوسط زمن البطاقة فوق الحد. قِس قبل أي تغيير على DCW TIMEOUT أو DCW VALIDECMTIME. هذا الحكم يظهر بالتفاصيل فقط."
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

/*
 * TASK R65: the page's new pure rules. Each one is a plain function with a
 * test in make-x64/test_diag.c; the page only formats their results.
 */

/* Delivered share of the decided verdicts (delivered + refused), 0..100.
 * -1 when there is no decided verdict yet: no share, not 0%. */
__attribute__((unused))
static int diag_ratio_pct(uint32_t delivered, uint32_t refused)
{
	uint32_t t = delivered + refused;

	if (!t) return -1;
	return (int)(delivered * 100u / t);
}

/* How many of n ring ticks fall within the last span_ms of now. Subtraction
 * is unsigned, so a tick that wraps or lies ahead of now is never counted. */
__attribute__((unused))
static uint32_t diag_recent(const uint32_t *tick, int n, uint32_t now, uint32_t span_ms)
{
	uint32_t c = 0;
	int i;

	for (i = 0; i < n; i++)
		if (now - tick[i] <= span_ms) c++;
	return c;
}

/* The effective checksum gate of a profile, as dcw.c decides it:
 *   ON  -> on;   OFF -> off;
 *   INHERIT -> the global gate, switched off only when CA MODERN is armed
 *              AND the system's control word carries no CSA checksum.
 * Returns 1 or 0. */
__attribute__((unused))
static int diag_checksum_effective(int mode, int global_on, int ca_modern, int checksum_expected)
{
	if (mode == DIAG_CSUM_ON) return 1;
	if (mode == DIAG_CSUM_OFF) return 0;
	if (!global_on) return 0;
	if (ca_modern && !checksum_expected) return 0;
	return 1;
}

/* What a ring reason means, in plain words, for the legend on the page.
 * Reason 10 is a hold, not a refusal, and says so. */
__attribute__((unused))
static const char *diag_reason_meaning(int reason)
{
	static const char *const text[DIAG_REASONS] = {
		"قُبل المفتاح",
		"فشل فحص checksum: بايتات التحقق في المفتاح غير صحيحة",
		"مفتاح صفري أو نصفه صفر",
		"المفتاح يكرر 3 بايتات (نمط مزيف)",
		"قائمة مفاتيح غير صالحة",
		"CACHEEX: بوابة LOCAL_ONLY منعت المفتاح",
		"CACHEEX: بوابة BLOCK_FAKE_CW منعت المفتاح",
		"CACHEEX: ينتظر تأكيداً قبل القبول",
		"تناقض في دورة المفاتيح",
		"خلاف بين النظراء على المفتاح",
		"محجوز بانتظار مؤكد ثانٍ (ليس رفضاً)",
		"ذاكرة نظير مُدان: رُفض لأن النظير أخطأ من قبل",
		"أقل من CACHE THRESHOLD",
		"فلتر البروفايل منع المفتاح",
		"تناقض متأخر مع الكاش",
		"بوابة الحقيقة المتأخرة منعت المفتاح",
		"بوابة مخزن الثقة منعت المفتاح",
		"بوابة ترحيل الثقة منعت المفتاح",
		"البطاقة أسرع من DCW MINTIME",
		"البطاقة أبطأ من المهلة (DCW VALIDECMTIME)",
		"نمط المفتاح مزيف (DCWFILTER PATTERN)"
	};
	return (reason >= 0 && reason < DIAG_REASONS) ? text[reason] : "?";
}

/* Profile-level warnings. Each one is a fact read from a profile, judged
 * against the family it serves. The page lists them; nothing applies them. */
enum diag_pwarn {
	PW_CA_MODERN_OFF,	/* CA MODERN off on a system that needs it   */
	PW_DCW_CHECK_OFF,	/* DCW CHECK off: the structural check is gone */
	PW_CSA_CHECKSUM_OFF,	/* checksum effectively off on a CSA system  */
	PW_CYCLE_OFF,		/* DCWFILTER CYCLE off                        */
	PW_HALFNULL_NONVG,	/* HALFNULLED on for a non-VideoGuard system  */
	PW_SKIPCWC_OFF,		/* ENABLE SKIPCWC off (information)           */
	PW_ECM_LEN_OFF,		/* ECM CHECK LENGTH off (information)         */
	PW_FINDINGS
};

struct diag_pfacts {
	int checksum_expected;	/* 1: the system's CW carries CSA checksum bytes */
	int checksum_eff;	/* diag_checksum_effective() of the profile      */
	int vg_csa;		/* 1: VideoGuard CSA (not iCam)                   */
	int ca_modern;
	int dcw_check;
	int cycle;
	int skipcwc;
	int halfnulled;
	int checkecmlength;
};

/* The warnings of one profile, as a bitmask (1 << PW_*). */
__attribute__((unused))
static uint32_t diag_profile_warn(const struct diag_pfacts *f)
{
	uint32_t m = 0;

	if (!f->ca_modern && !f->checksum_expected) m |= 1u << PW_CA_MODERN_OFF;
	if (!f->dcw_check) m |= 1u << PW_DCW_CHECK_OFF;
	if (!f->checksum_eff && f->checksum_expected) m |= 1u << PW_CSA_CHECKSUM_OFF;
	if (!f->cycle) m |= 1u << PW_CYCLE_OFF;
	if (f->halfnulled && !f->vg_csa) m |= 1u << PW_HALFNULL_NONVG;
	if (!f->skipcwc) m |= 1u << PW_SKIPCWC_OFF;
	if (!f->checkecmlength) m |= 1u << PW_ECM_LEN_OFF;
	return m;
}

/* 2 = act (a fake or checksum gate is off), 1 = look, 0 = information. */
__attribute__((unused))
static int diag_pwarn_severity(enum diag_pwarn w)
{
	return (w == PW_SKIPCWC_OFF || w == PW_ECM_LEN_OFF) ? 0 : 1;
}

__attribute__((unused))
static const char *diag_pwarn_label(enum diag_pwarn w)
{
	static const char *const name[PW_FINDINGS] = {
		"CA MODERN معطّل على نظام يحتاجه",
		"DCW CHECK معطّل",
		"checksum موقوف على نظام CSA",
		"DCWFILTER CYCLE معطّل",
		"HALFNULLED على نظام غير VideoGuard",
		"SKIPCWC معطّل",
		"ECM CHECK LENGTH معطّل"
	};
	return (w >= 0 && w < PW_FINDINGS) ? name[w] : "?";
}

__attribute__((unused))
static const char *diag_pwarn_advice(enum diag_pwarn w)
{
	static const char *const text[PW_FINDINGS] = {
		"على نظام AES أو CONST بدون CA MODERN يُطبَّق فحص checksum خطأً فتُرفض مفاتيح صحيحة. راجع هذه الباقة.",
		"DCW CHECK هو الفحص البنيوي للمفتاح. إيقافه يسمح بمفاتيح مشوهة. أبقِه ما لم يثبت القياس خلافه.",
		"checksum موقوف على باقة CSA، فتمرّ المفاتيح التالفة. أبقِه إلا إذا أثبت القياس أنه يرفض مفاتيح صحيحة.",
		"DCWFILTER CYCLE معطّل: لا تُكشف تناقضات الدورة. راجع قبل الإبقاء عليه.",
		"HALFNULLED مسموح به لغير VideoGuard CSA. تحقّق من العائلة قبل الإبقاء عليه.",
		"SKIPCWC معطّل: لا تُفرض بطارية checksum عند التسليم. معلومة فقط.",
		"ECM CHECK LENGTH معطّل: لا تُفحص أطوال قوائم ECM. معلومة فقط، ولا تُغيَّر دون قياس الأطوال."
	};
	return (w >= 0 && w < PW_FINDINGS) ? text[w] : "?";
}

#endif /* MCS_DIAG_H */
