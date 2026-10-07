/*
 * retry_detect.h — TASK 1.2, post-delivery retry classification
 *
 * None of the protocols this server speaks — Newcamd, CCcam, MGCamd — has a
 * negative acknowledgement. A client that receives a control word it cannot use
 * never says so. The only observable is behavioural: a working client asks for
 * the next ECM roughly one crypto period later (about 10 s), while a client
 * holding a bad CW re-asks for the same service within a second or two.
 *
 * That inference is the foundation of the whole trust engine, so it has to be
 * right in both directions:
 *
 *   - call a normal periodic request a "retry" and the trust engine starts
 *     punishing sources for clients behaving correctly;
 *   - call a genuine retry "normal" and a poisoned cache entry keeps being
 *     served to the same client indefinitely, which is the black screen this
 *     project exists to remove.
 *
 * Hence two windows rather than one threshold. `RETRY-WINDOW` (default 4000 ms)
 * is the hard signal: below it, the request is treated as a retry. Between it
 * and `SOFT-FAIL-WINDOW` (default 5000 ms) the request is *weakly* suspicious —
 * counted, never acted on alone. Above the soft window it is normal operation.
 * The gap exists so that jitter around a shorter-than-usual crypto period
 * cannot, by itself, move a counter that eventually disables a card. GR3.
 *
 * GR2 is enforced structurally: the classifier takes the number of CWs actually
 * delivered for the request being re-asked, and returns RETRY_NONE when that is
 * zero. A client retrying because nothing arrived is a timeout, which is a
 * separate counter and must never reach a source score.
 *
 * A header, not a function in srv-newcamd.c, because the logic has to be
 * unit-tested and because all six server protocols need the identical
 * classification.
 */
#ifndef MCS_RETRY_DETECT_H
#define MCS_RETRY_DETECT_H

#include <stdint.h>

/* Classification of one incoming ECM request from a client. */
#define RETRY_NONE  0   /* nothing delivered yet, or a normal periodic request */
#define RETRY_HARD  1   /* re-asked inside RETRY-WINDOW: count it              */
#define RETRY_SOFT  2   /* between the two windows: record, never act alone    */

/*
 * Classify an ECM request by how soon it followed the CW delivery.
 *
 *   ticks_now       current GetTickCount()
 *   delivered_at    GetTickCount() when the CW for this request was sent.
 *                   Ignored when nb_delivered is 0.
 *   nb_delivered    how many CWs were actually delivered for the request this
 *                   one is re-asking for. Zero means a timeout, not a retry.
 *   retry_window_ms hard window, from RETRY-WINDOW
 *   soft_window_ms  soft window, from SOFT-FAIL-WINDOW
 *
 * The subtraction is unsigned on purpose. GetTickCount() is milliseconds since
 * start truncated to 32 bits, so it wraps about every 49.7 days; unsigned
 * subtraction still yields the true interval across a wrap as long as the
 * interval is far shorter than the wrap period, which it always is here.
 * Saturating at the windows would instead misread a wrapped interval as
 * enormous and silently drop every retry for the rest of the process lifetime.
 *
 * A soft window at or below the hard window is a misconfiguration; it is
 * treated as "soft disabled" rather than as an error, so the hard signal keeps
 * working.
 */
static inline int retry_classify(uint32_t ticks_now,
                                 uint32_t delivered_at,
                                 int      nb_delivered,
                                 uint32_t retry_window_ms,
                                 uint32_t soft_window_ms)
{
	if (nb_delivered <= 0) return RETRY_NONE;          /* GR2: a timeout, not a retry */

	uint32_t since = ticks_now - delivered_at;         /* wrap-safe */

	if (since < retry_window_ms) return RETRY_HARD;
	if (soft_window_ms > retry_window_ms && since < soft_window_ms) return RETRY_SOFT;
	return RETRY_NONE;
}

/*
 * Depth of the per-client delivery ring.
 *
 * Fixed, and embedded in the client struct rather than malloc'd, because the
 * ECM path must not allocate (GR9) and the memory must be bounded. Four is
 * enough to survive a client that zaps away and comes back: it remembers the
 * origin of the last few deliveries so a retry can be attributed to the source
 * that actually supplied the key, not to whatever source happens to be current
 * (GR1, and GR6's requirement to key on ecm_hash rather than SID).
 */
#define RETRY_RING_DEPTH 4

struct retry_entry {
	uint32_t ecm_hash;     /* GR6: the key, never the SID alone */
	uint16_t sid;
	uint16_t caid;
	uint32_t provid;
	uint32_t delivered_at; /* GetTickCount() at delivery */
	int      srctype;      /* DCW_SOURCE_* */
	int      srcid;        /* may carry PEER_* origin flags */
	int      used;         /* 0 = slot empty */
};

struct retry_ring {
	struct retry_entry e[RETRY_RING_DEPTH];
	unsigned int next;     /* write cursor, wraps at RETRY_RING_DEPTH */
};

#endif /* MCS_RETRY_DETECT_H */
