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
/* One line per DISTINCT (status, gate) view of this ECM -- not per scheduler
 * pass. The line exists to explain why no peer request went out on a pass;
 * saying it every ~30 ms while the same request is still in flight is what
 * made a field session log ~15 copies per attempt and lose real evidence to
 * the ring (459 replaced events, 2 lost links). The key is encoded so 0 --
 * what a fresh ECM's byte holds -- can never collide with a real state, so
 * the first explanation of each kind always prints. */
static inline int mcs_trace_peerwait_log_once(unsigned char *last,
                                              unsigned char status,
                                              unsigned char fallow,
                                              unsigned char sendreq)
{
	unsigned char key = (unsigned char)(1 + (status & 3) * 4
	                                   + (fallow ? 2 : 0) + (sendreq ? 1 : 0));
	if (*last == key) return 0;
	*last = key;
	return 1;
}

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
