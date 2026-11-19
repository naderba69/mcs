#ifndef __TRACE_DETAIL_LABELS_H
#define __TRACE_DETAIL_LABELS_H
/*
 * Band 112: labels for machine-valued trace details.
 *
 * "cache peer request skipped: fallow=.. sendreq=.. status=.." was printed
 * on EVERY scheduler pass after the peer search had already gone out
 * (status = REQ/REQ2), so an operator reading a wall of "تُجاوز طلب نظراء
 * الكاش" chased a setting that was fine. The gate values say otherwise, so
 * the text must say what the values mean. Kept here (not inline in
 * httpserver.c) so the mapping is unit-tested rather than dependent on
 * conjuring a slow-source session in a test box -- the same reason the
 * band-111 shadow predicate moved to a header.
 *
 * Wording lives in macros: a rephrase is a display change, a changed
 * STATUS->MEANING mapping is a logic change, and only the latter may break
 * this package.
 */
#define MCS_LABEL_PEER_WAITING \
	"طلب البحث أُرسل سابقًا وينتظر ردّ النظراء (status=قيد الانتظار) — ليس تجاوزًا ولا خلل إعدادات"
#define MCS_LABEL_PEER_NO_CACHE \
	"الكاش مطفأ لهذا البروفايل (fallowcache=0) فلم يُرسل طلب بحث"
#define MCS_LABEL_PEER_NO_REQ \
	"طلبات بحث الكاش معطلة للبروفايل (cachesendreq=0) فلم يُرسل طلب بحث"

/* Returns NULL when the detail is not this event or cannot be parsed --
 * the caller keeps its generic wording in that case. */
static inline const char *mcs_trace_label_peer_skip(const char *detail)
{
	unsigned int fal = 0, srq = 0, sts = 0;
	if (strncmp(detail, "cache peer request skipped:", 27)) return NULL;
	if (3 != sscanf(detail + 27, " fallow=%u sendreq=%u status=%u", &fal, &srq, &sts))
		return NULL;
	/* ECM_CACHE_REQ(1)/ECM_CACHE_REQ2(2): the search is in flight. */
	if (sts == 1 || sts == 2) return MCS_LABEL_PEER_WAITING;
	if (!fal) return MCS_LABEL_PEER_NO_CACHE;
	if (!srq) return MCS_LABEL_PEER_NO_REQ;
	return NULL;
}
#endif
