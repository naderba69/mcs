#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>

#include <sys/time.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/prctl.h>
#include <netdb.h> 
#include <errno.h>
#include <pthread.h>
#include <poll.h>
#include <signal.h>
#include <sys/epoll.h>

#define MAX_EPOLL_EVENTS	100	// Maximum number of events to be returned from a single epoll_wait() call

#include "tools.h"
#include "debug.h"
#include "sockets.h"
#include "threads.h"
#include "convert.h"

#include "des.h"
#include "md5.h"
#include "sha1.h"

#include "msg-newcamd.h"
#ifdef CCCAM
#include "msg-cccam.h"
#endif
#ifdef RADEGAST
#include "msg-radegast.h"
#endif

#include "ecmdata.h"
#include "parser.h"
#include "config.h"
#include "httpserver.h"

#ifdef TELNET
#include "telnet.h"
#endif

#include "cacheex.h"

#include "main.h"

#include "dcw.h"
#include "dcwstruct.h"	/* TASK 1.8 structural soft signals, used by setdcw.c (included below) */
#include "cwentropy.h"	/* TASK 2.2 entropy/collision forensics, used by setdcw.c (included below) */
#include "dcwfilter.h"	/* TASK 1.8 reads the two filter gates as evidence weighting */
#include "statsline.h"	/* TASK 1.9 STATS-WINDOW summary, used by clustredcache.c (included below) */
#include "cache_relay.h"	/* GR10 guard, used by clustredcache.c (included below) */
#include "peer_origin.h"	/* TASK 1.1 origin model, used by clustredcache.c (included below) */
#include "cache_threshold.h"	/* TASK 1.11a min-peer floor, used by clustredcache.c (included below) */
#include "retry_detect.h"	/* TASK 1.2 retry classifier, used by srv-newcamd.c (included below) */
#include "cw_reuse.h"	/* TASK 1.3 reuse detector, used by srv-*.c / cli-*.c (included below) */
#include "trust.h"		/* TASK 1.4 trust engine, used by srv-*.c / cli-*.c (included below) */
#include "cache_purge.h"	/* TASK 1.5 purge-by-origin marks; clustredcache.c re-includes it for the walk */
#include "ledger.h"	/* TASK 2.1 agreement ledger, used by setdcw.c / srv-newcamd.c (included below) */
#include "cwtime.h"
#include "cwplaus.h"	/* TASK 2.4 plausibility scoring, used by setdcw.c (below) */
#include "cwcycle.h"	/* TASK 2.5 cache key cycle/parity visibility (observe only) */
#include "cwneg.h"	/* TASK 2.6 negative memory: a proven-bad key is never delivered twice */
#include "cwcm.h"	/* TASK 2.7 complement-mirror detection on delivered keys */
#include "cacheguard.h"	/* TASK 2.9 cache protocol guard, used by clustredcache.c (below) */
#include "trustlife.h"	/* TASK 2.10 trust lifecycle: fade + persistence, used by th-cfg.c / clustredcache.c */
#include "trustagg.h"	/* TASK 2.8 the cache-peer trust engine, coarse tier */
#include "rotation.h"	/* TASK 1.6 instant rotation, used by srv-newcamd.c / loadbalance.c */
#include "cachepref.h"	/* TASK 1.7 TRUSTED-CACHE-FIRST, used by clustredcache.c (included below) */

#include "pipe.c"

char config_file[256] = "/var/etc/multics.cfg";

/*
 * TASK 1.2 — the retry-classification windows, in milliseconds.
 *
 * Declared extern in config.c, which parses `RETRY-WINDOW` and
 * `SOFT-FAIL-WINDOW` and sets them; read by retry_classify() in
 * retry_detect.h. Defaults are assigned in init_config() so they are set
 * before any config file is read, and again by the parser when the option is
 * present, which is what makes a SIGHUP reload pick up a change.
 */
uint32_t retry_window_ms = 4000;
uint32_t soft_fail_window_ms = 5000;

/*
 * TASK 1.3 — the CW-reuse detector's table and counters.
 *
 * One table for the whole server rather than one per peer: a control word handed
 * out by two different sources for two different services is exactly the case
 * worth catching, and splitting the table would hide it.
 *
 * The counters are plain ints. They are written on the cache path under
 * prg.lockcache and read from the HTTP thread, which can in principle show a
 * stale value; that is the same discipline the existing cache counters use, and
 * for an observe-only counter a stale read is harmless.
 */
struct cwreuse_table cwreuse_tab;
unsigned long cwreuse_proofs = 0;

/*
 * TASK 1.4 — the trust table.
 *
 * 256 entries x 36 bytes = 9216 bytes of .bss, no allocation (GR9). One table for
 * the whole server: the key already carries CAID/PROVID/SID, so a source that is
 * good on one bouquet and bad on another occupies two entries, which is what GR4
 * requires.
 *
 * OBSERVE-ONLY in this task. Scores are computed and exposed; nothing routes on
 * them yet. GR3 forbids hard action on a single soft event, and the action layer
 * (TASK 1.5 purge, TASK 1.6 rotation) is where that policy gets written down and
 * tested, not here.
 */
struct trust_table trust_tab;

/*
 * TASK 1.7 -- a lock for the trust table, and the reason it is a defect fix
 * rather than a new feature.
 *
 * The table was written from two places under two *different* mutexes:
 * srv-newcamd.c's retry classifier holds prg.lockecm, while the cache-exchange
 * sites (clustredcache.c, srv-cccam.c, srv-camd35.c, srv-cs378x.c and the three
 * cli-*.c peers) hold prg.lockcache. Two mutexes do not exclude each other, so
 * concurrent events could interleave a read-modify-write on `score` and lose an
 * update, or let trust_find() probe an entry between its memset and its key
 * fields being filled. No crash is possible -- the entries are plain integers
 * and nothing dereferences a pointer through them -- but the score, which is
 * the only output of the engine, could be wrong.
 *
 * That was tolerable while every access was a rare write. TASK 1.7 adds a read
 * on the cache thread for every cache hit, so the window is no longer rare.
 *
 * A dedicated leaf mutex rather than reusing prg.lockcache:
 *   - lockcache is not held on the srv-newcamd.c path, and taking it there would
 *     invert the existing lockecm -> lockcache nesting order.
 *   - this one is only ever taken around trust_record()/trust_score(), which
 *     touch nothing else, so no lock is ever acquired while it is held and no
 *     cycle can form.
 *
 * The plain trust_record()/trust_score() API in trust.h is unchanged, so
 * test_trust.c still exercises it directly and single-threaded.
 */
pthread_mutex_t trust_lock = PTHREAD_MUTEX_INITIALIZER;

static inline int trust_record_lk(struct trust_table *t, int ev,
                                  int srctype, int srcid,
                                  uint16_t caid, uint32_t provid, uint16_t sid,
                                  uint32_t ticks_now)
{
	int r;
	pthread_mutex_lock(&trust_lock);
	r = trust_record(t, ev, srctype, srcid, caid, provid, sid, ticks_now);
	pthread_mutex_unlock(&trust_lock);
	return r;
}

static inline int trust_score_lk(struct trust_table *t,
                                 int srctype, int srcid,
                                 uint16_t caid, uint32_t provid, uint16_t sid)
{
	int s;
	pthread_mutex_lock(&trust_lock);
	s = trust_score(t, srctype, srcid, caid, provid, sid);
	pthread_mutex_unlock(&trust_lock);
	return s;
}

/*
 * TASK 2.8 -- the cache-peer trust engine, coarse tier.
 *
 * trust_tab above scores (source, CAID, PROVID, SID): the right granularity
 * for a delivery decision, and useless for answering "who is this
 * COUNTERPARTY?". A peer poisoning fifty services leaves fifty fine-tier
 * entries with one or two events each, none actionable, and the operator
 * has no per-peer standing anywhere. trustagg_tab sums the same events at
 * (source, CAID) -- the sender's habit, CAID kept so the view stays finer
 * than per-server (GR4) -- with trust.h's exact arithmetic, and drives
 * exactly one consequence: the cache request fan-out may skip a peer whose
 * coarse score sits below the actionable line, for a bounded window, with
 * one request going through per window to let GOOD events exist. It never
 * touches a delivery: no rejection, no delay, no disconnect (GR3).
 *
 * Everything here runs under trust_lock -- the taps sit next to
 * trust_record()/trust_record_lk() calls and share their discipline. The
 * gate runs on the cache thread under lockcache, so the order is the
 * established lockcache -> trust_lock that cachepref_gate() already uses.
 *
 * `peertrust_requests` (PEER-TRUST-REQUESTS, default OFF) is the fan-out
 * brake's switch; the aggregation, the crossing line and the stats exist
 * either way. Evidence never costs anything; only the switch can change
 * what gets asked, and the operator flips it back with one config line.
 */
struct trustagg_table trustagg_tab;
unsigned long trustagg_skipped = 0;  /* fan-out sends the brake refused  */
unsigned long trustagg_lines   = 0;  /* crossing sentences written       */
int peertrust_requests = 0;          /* PEER-TRUST-REQUESTS, default OFF */

/*
 * One event on the coarse tier, with the crossing sentence written after the
 * unlock (GR9: no I/O under the lock). This is the form for taps that do NOT
 * already hold trust_lock -- the ones sitting after a trust_record_lk() call.
 * The hooks that hold the lock call the bare trustagg_note() themselves, on
 * the same lock they are already under.
 */
static void trustagg_note_lk(int ev, int srctype, int srcid,
                             uint16_t caid, uint32_t ticks_now)
{
	struct trustagg_evidence evd;
	char line[384];
	int crossed, n;

	pthread_mutex_lock(&trust_lock);
	crossed = trustagg_note(&trustagg_tab, ev, srctype, srcid, caid,
	                        ticks_now, &evd);
	pthread_mutex_unlock(&trust_lock);

	if (crossed) {
		trustagg_lines++;
		n = trustagg_format(&evd, line, (int)sizeof(line));
		if (n > 0) debugf(DBG_ERROR, " !!! %s\n", line);
	}
}

/*
 * The fan-out's question, in wrapper form so the counter lives under the same
 * lock as the decision: may we ask this source for this CAID?
 */
static int trustagg_gate(int srctype, int srcid, uint16_t caid)
{
	int open;
	pthread_mutex_lock(&trust_lock);
	open = trustagg_ask(&trustagg_tab, srctype, srcid, caid, GetTickCount());
	if (!open) trustagg_skipped++;
	pthread_mutex_unlock(&trust_lock);
	return open;
}

/*
 * TASK 2.9 -- the cache protocol guard.
 *
 * Two holes verified in cache_recvmsg() before anything was written: five
 * packet types read offsets the datagram may not carry, so a short datagram
 * made the handlers decide on uninitialised stack memory (a REQUEST keyed by
 * a garbage hash, a PINGREQ anchoring a peer at a garbage port); and packets
 * from unconfigured senders were dropped at `if (!peer) break;` with no
 * counter and no line, so a misconfigured peer was invisible. cacheguard.h
 * holds the facts (the per-type minimums, the window); these hooks hold the
 * consequences, and there are only two: count, and at most one line per kind
 * per 60 s window. Nothing here can change what a well-formed sender
 * experiences, and nothing looks at the content of a control word -- GR3 is
 * not implicated, because "too short to decode" is not a suspicion about a
 * key. No lock: the hooks run in cache_recvmsg() on the cache thread, and
 * the stats snapshot reads them on the same thread's tick.
 */
struct cg_window cg_short_win, cg_unknown_win;
unsigned long cg_short   = 0;   /* datagrams dropped: shorter than their type's minimum */
unsigned long cg_unknown = 0;   /* packets counted from unconfigured senders (stock drops them) */
unsigned long cg_lines   = 0;   /* guard sentences written                              */

static void cg_note_short(int type, int received, uint32_t ip, int port)
{
	char line[384];
	int min = cg_minlen((uint8_t)type);

	cg_short++;
	if (cg_window_event(&cg_short_win, GetTickCount())) {
		int n = cg_format_short(line, (int)sizeof(line), type, received,
		                        min, ip, port, cg_short_win.count);
		if (n > 0) {
			cg_lines++;
			debugf(DBG_ERROR, " !!! %s\n", line);
		}
	}
}

static void cg_note_unknown(int type, uint32_t ip, int port)
{
	char line[384];

	cg_unknown++;
	if (cg_window_event(&cg_unknown_win, GetTickCount())) {
		int n = cg_format_unknown(line, (int)sizeof(line), type, ip, port,
		                          cg_unknown_win.count);
		if (n > 0) {
			cg_lines++;
			debugf(DBG_ERROR, " !!! %s\n", line);
		}
	}
}


/*
 * TASK 1.7 -- TRUSTED-CACHE-FIRST.
 *
 * 0 (the default) keeps stock r82a behaviour bit for bit: any cache hit that
 * passes the existing cycle and min-peer tests is served. 1 makes the cache
 * fast path conditional on the supplying source being trusted, but only for
 * the requests where nothing else has checked the key -- see cachepref.h.
 *
 * Global rather than per-profile, like RETRY-WINDOW: what it measures is how
 * much the operator trusts the cache as a class of source, not a property of
 * any one bouquet.
 *
 * `cachepref_defer` counts the hits that were left to the card servers. It is
 * written on the cache thread and read by the HTTP thread with no lock, which
 * can show a stale value -- the same discipline the existing cache counters
 * use, and harmless for a counter whose only job is to make the policy visible.
 */
int cachepref_enabled = 0;
unsigned long cachepref_defer = 0;

/*
 * TASK 1.8 — structural soft signals.
 *
 * The counters are declared in dcwstruct.h and defined here, next to every
 * other table this project added, so that no new entry is needed in OBJECTS.
 *
 * dcwstruct_note() is the one place that turns a scan result into evidence. It
 * lives here rather than in the header because it needs trust_record_lk(), and
 * putting it in the header would have made the pure, testable scan depend on
 * the trust engine's lock. The split is deliberate: dcwstruct.h stays a
 * function of its arguments, and test_dcwstruct.c exercises it with no trust
 * table in sight.
 *
 * Called from the two delivery points in setdcw.c, before acceptDCW(), so a key
 * that is about to be rejected is attributed too -- arguably the more important
 * half, since a rejected key leaves no trace anywhere else.
 */
unsigned long dcwstruct_hits[DCWS_BITS];
unsigned long dcwstruct_anomalous = 0;

/*
 * TASK 1.8 -- repeat-suppression state.
 *
 * A struct rather than loose globals, and initialised once at startup:
 * dcwstruct_init() must NOT be re-run on SIGHUP, because the table is
 * deliberately carried across a reload. Nothing an operator can reload makes a
 * key they have already been told about new again.
 */
struct dcwstruct_table dcwstruct_tab;
unsigned long dcwstruct_suppressed = 0;   /* repeats the table kept quiet */

/*
 * TASK 2.2 -- entropy and collision forensics. `cwentropy_tab` is the ring of
 * recent deliveries the collision half compares against; `cwentropy_rep` is a
 * second instance of TASK 1.8's de-duplicator, not a second copy of it (the
 * identity it keys on -- source, channel, verdict, key -- is exactly what this
 * task needs too, so the logic is reused rather than rewritten).
 *
 * Both live in .bss, roughly 1.5 KB and 400 bytes, and neither is re-initialised
 * on SIGHUP: a key the operator has already been told about does not become new
 * again because they edited the config.
 */
struct cwen_table cwentropy_tab;
struct dcwstruct_table cwentropy_rep;
unsigned long cwentropy_hits[CWEN_BITS];
unsigned long cwentropy_anomalous = 0;
unsigned long cwentropy_suppressed = 0;

/* The three numbers the STATS-WINDOW line shows, kept where the log can read
 * them without walking the tables. */
unsigned long cwentropy_lowdiv = 0;
unsigned long cwentropy_derived = 0;
unsigned long cwentropy_foreign = 0;


static void dcwstruct_note(struct trust_table *t, unsigned mask,
                           const uint8_t *key,
                           int srctype, int srcid,
                           uint16_t caid, uint32_t provid, uint16_t sid,
                           uint32_t ticks_now)
{
	char what[64];
	int i, report, again = 0;
	int ta_ev = 0;   /* TASK 2.8: the same event, carried past the unlock */

	if (!mask) return;

	/* Pure, so it stays outside the lock. */
	dcwstruct_name(mask, what, sizeof(what));

	/*
	 * trust_lock is taken here for a different reason than in 1.4's wrappers:
	 * this is not protecting the trust table, it is protecting dcwstruct_tab,
	 * which setdcw.c reaches from every ECM thread. The trust table is only
	 * touched while the same lock is held, so trust_record() -- the lock-free
	 * core -- is called directly rather than through trust_record_lk(), which
	 * would take a non-recursive mutex a second time and deadlock.
	 *
	 * Lock discipline: this is the leaf lock, nothing else is acquired while
	 * it is held, and debugf() is deliberately called after the unlock so no
	 * thread waits on stdout while holding it.
	 */
	pthread_mutex_lock(&trust_lock);

	report = dcwstruct_seen_once(&dcwstruct_tab, mask, srctype, srcid,
	                             caid, provid, sid, key, ticks_now);

	if (report) {
		again = (report == DCWSTRUCT_REPORT_AGAIN);
		dcwstruct_anomalous++;
		for (i = 0; i < DCWS_BITS; i++)
			if (mask & (1u << i)) dcwstruct_hits[i]++;

		/*
		 * ONE soft event per anomalous delivery, however many bits are set.
		 * Counting each bit separately would let a single absurd key move a
		 * score several steps at once, which is hard action wearing a soft
		 * event's clothes -- and GR3 is explicit that a soft signal is
		 * inference, not proof. The per-bit counters keep the detail; the
		 * score only sees "this source produced something impossible".
		 *
		 * DCWS_REPEAT is not in DCWS_SCOREMASK: isbadDCW() fires on about
		 * 1 key in 16 000 random keys, so scoring it would decay a healthy
		 * busy source on noise alone. Counted and logged, never evidence.
		 */
		if (mask & DCWS_SCOREMASK) {
			trust_record(t, TRUST_EV_SOFT, srctype, srcid, caid, provid, sid, ticks_now);
			ta_ev = TRUST_EV_SOFT;
		}
	}
	else {
		dcwstruct_suppressed++;
	}

	pthread_mutex_unlock(&trust_lock);

	/* TASK 2.8: the same event, summed one level coarser (source, CAID). */
	if (ta_ev) trustagg_note_lk(ta_ev, srctype, srcid, caid, ticks_now);

	if (!report) return;

	/*
	 * GR8: the evidence, every time. Unconditional rather than behind a debug
	 * flag because by construction this should almost never happen -- every
	 * test above is either near-impossible on a genuine key or gated on a
	 * filter the operator left enabled. If it starts scrolling, that IS the
	 * finding, and hiding it behind a flag would be exactly wrong.
	 *
	 * "same key again" is said explicitly when this is a re-report after
	 * DCWSTRUCT_WINDOW: a source still pushing the same impossible key an hour
	 * later is a different situation from a source that just produced a new
	 * one.
	 */
	debugf(DBG_ERROR," !?> CW STRUCT: %s from source %d/%d ch %04x:%06x:%04x%s (key delivered/rejected unchanged; trust signal only)\n",
		what, srctype, srcid, caid, provid, sid,
		again ? " [same key again]" : "");
}

/*
 * TASK 2.2 -- entropy and collision forensics, and the one log line each verdict
 * earns.
 *
 * The split is TASK 1.8's: cwentropy.h holds the arithmetic and the ring and is
 * a pure function of its arguments, this file holds the sentence. The
 * de-duplicator is 1.8's too, instantiated a second time -- see the table
 * declaration above.
 *
 * What is scored, and what is not, is decided by CWEN_SCOREMASK in the header
 * rather than here, on purpose: the policy and the reason it is the policy live
 * next to the probabilities that justify it. In short -- a key that cannot have
 * been generated (LOWDIV) or that this source derived from its own earlier key
 * (NEAR) is one soft event, and a near neighbour belonging to *another* source
 * (NEAR_OTHER) is logged and counted but never scored, because two sources whose
 * keys are a few bits apart do not say which one did the editing (GR1).
 *
 * GR8: the evidence is in the line, not in a debug level -- both keys, the
 * distance between them, the age of the neighbour and the ffffff identity of the
 * ECM it belonged to. A line that says "a forged key was seen" without saying
 * which two keys were compared is not evidence.
 *
 * Locking is identical to dcwstruct_note()'s and for the same reason: the ring
 * and the de-duplicator are reached from every ECM thread, so trust_lock is
 * taken around them, the trust table is touched through the lock-free core
 * (taking trust_record_lk() here would deadlock on a non-recursive mutex), and
 * the log line is written after the unlock so no thread waits on stdout holding
 * the lock.
 */
static void cwentropy_note(struct trust_table *t,
                           const uint8_t *ecmd5, const uint8_t *key,
                           int srctype, int srcid,
                           uint16_t caid, uint32_t provid, uint16_t sid,
                           uint32_t ticks_now)
{
	struct cwen_match near;
	uint64_t ecmid = 0;
	unsigned mask, scored;
	char what[64];
	int report = 0, again = 0, why = 0;
	int i;

	if (!key) return;

	/*
	 * The 64-bit prefix of the ECM MD5, never the 32-bit hash: that value is a
	 * bucket index, and two ECMs in one bucket would look like "the same ECM"
	 * and silence the comparison (D5, and the rule TASK 1.3 and 2.1 follow).
	 * No CACHEEX means no ecmd5, which means no identity, which means the
	 * collision half stays silent -- the entropy half does not need one.
	 */
	if (ecmd5) memcpy(&ecmid, ecmd5, 8);

	mask = cwen_seen(&cwentropy_tab, ecmid, key, srctype, srcid, ticks_now, &near);
	if (!mask) return;

	/* Pure, so it stays outside the lock. */
	cwen_name(mask, what, sizeof(what));

	pthread_mutex_lock(&trust_lock);

	/*
	 * The verdict is part of the de-duplicator's identity: a re-delivery of the
	 * same key that now collides with something it did not collide with before
	 * (or no longer does) is a new fact, not a repeat.
	 */
	report = dcwstruct_seen_once(&cwentropy_rep, mask, srctype, srcid,
	                             caid, provid, sid, key, ticks_now);
	if (report) {
		again = (report == DCWSTRUCT_REPORT_AGAIN);
		cwentropy_anomalous++;
		for (i = 0; i < CWEN_BITS; i++)
			if (mask & (1u << i)) cwentropy_hits[i]++;
		if (mask & CWEN_LOWDIV)      cwentropy_lowdiv++;
		if (mask & CWEN_NEAR)        cwentropy_derived++;
		if (mask & CWEN_NEAR_OTHER)  cwentropy_foreign++;

		/*
		 * ONE soft event however many bits are set: the same rule and the same
		 * reason as 1.8's, and CWEN_EXACT is not in the mask at all because
		 * TASK 1.3 is already acting on that fact as a proof.
		 */
		scored = mask & CWEN_SCOREMASK;
		if (scored) {
			trust_record(t, TRUST_EV_SOFT, srctype, srcid, caid, provid, sid, ticks_now);
		}
	}
	else {
		cwentropy_suppressed++;
	}

	pthread_mutex_unlock(&trust_lock);

	/* TASK 2.8: the same event, summed one level coarser (source, CAID);
	 * the wrapper takes trust_lock itself, and we no longer hold it. */
	if (scored) trustagg_note_lk(TRUST_EV_SOFT, srctype, srcid, caid, ticks_now);

	if (!report) return;

	if (mask & CWEN_LOWDIV) {
		debugf(DBG_ERROR," !!! CW ENTROPY: source %d/%d ch %04x:%06x:%04x delivered a key built from only %d distinct byte values (key %02x%02x%02x%02x...); a generated control word cannot look like this%s\n",
			srctype, srcid, caid, provid, sid, cwen_distinct(key),
			key[0], key[1], key[2], key[3], again ? " [same key again]" : "");
		why = 1;
	}

	if ((mask & CWEN_NEAR) || (mask & CWEN_NEAR_OTHER)) {
		debugf(DBG_ERROR," !!! CW COLLISION: source %d/%d ch %04x:%06x:%04x delivered a key %d bit%s away from the key %s delivered for ecm %016llx %u ms earlier (key %02x%02x%02x%02x... vs %02x%02x%02x%02x...)%s\n",
			srctype, srcid, caid, provid, sid, near.bits, (near.bits == 1) ? "" : "s",
			(mask & CWEN_NEAR) ? "this same source" : "another source",
			(unsigned long long)near.ecmid, (unsigned)near.age,
			key[0], key[1], key[2], key[3],
			near.key[0], near.key[1], near.key[2], near.key[3],
			again ? " [same key again]" : "");
		why = 1;
	}

	/*
	 * A mask of nothing but CWEN_EXACT reaches here with no line above: that is
	 * TASK 1.3's proof and it already has its own. Reaching this without having
	 * printed anything at all would mean the mask and the log have drifted
	 * apart, which is worth knowing about rather than passing over in silence.
	 */
	if (!why)
		debugf(DBG_ERROR," !!! CW FORENSICS: %s from source %d/%d ch %04x:%06x:%04x -- the identical-key fact belongs to the reuse detector, which logs it itself\n",
			what, srctype, srcid, caid, provid, sid);
}


/*
 * TASK 2.3 — card-server latency forensics.
 *
 * `cwt_tab` holds one slot per card server, in .bss (roughly 4 KB: 64 slots of
 * a 16-entry ring plus two report records). It is reached only from the card
 * server client threads, one slot per server id, and a server id belongs to
 * exactly one `struct server_data` -- so two threads never share a slot, which
 * is why the measurement path needs no lock at all.
 *
 * `min_card_latency_ms` is MIN-CARD-LATENCY, default 0 = OFF. With it off the
 * module still measures and still reports flatness; what it cannot do is call an
 * answer physically impossible, because that claim is only true once the
 * operator has said "I expect cards on this slot" -- see the file header of
 * cwtime.h for why that is not a default anyone should get by accident.
 */
struct cwt_table cwt_tab;
unsigned long cwtime_fast = 0;        /* replies below the floor               */
unsigned long cwtime_flat = 0;        /* replies in a flat window              */
unsigned long cwtime_reports = 0;     /* lines actually written                */
unsigned long cwtime_suppressed = 0;  /* verdicts the report window kept quiet  */
int min_card_latency_ms = 0;

/*
 * TASK 2.4 -- plausibility scoring. `cwplaus_tab` is keyed on the source alone
 * (srctype, srcid): how plausible a source's keys are is a property of the
 * source, not of the channel it happened to be answering for, and the window
 * has to be per source for the rate to mean anything. The service the key was
 * delivered for travels in the event, not in the table (GR4).
 *
 * Fixed size, .bss, no allocation, no I/O: 64 slots of counters, about 3 KB
 * (GR9). Counters rather than flags because the verdict is a rate -- one damaged
 * key is 1.52 % likely from an honest bouquet, which is why a lone one is
 * counted and three in a window are what convicts.
 */
struct cwp_table cwplaus_tab;
unsigned long cwplaus_full = 0;       /* 4/4: the expected shape               */
unsigned long cwplaus_one_group = 0;  /* exactly one group sum wrong           */
unsigned long cwplaus_weak = 0;       /* two or fewer groups hold              */
unsigned long cwplaus_reports = 0;    /* pattern lines actually written        */
unsigned long cwplaus_suppressed = 0; /* patterns inside the report window     */

/*
 * TASK 2.5 -- cache key cycle/parity visibility. `cwcycle_tab` is keyed on the
 * cache source alone (GR4: (srctype, srcid), the CAID/PROVID/SID ride the log
 * line as context, they do not split the counter).
 *
 * The two observation points run in TWO different threads: a peer offering a
 * key is observed from the cache thread (the CSP reply handler), the hand-off
 * to a waiting client from the setdcw thread (ecm_setdcwdata). Both funnel
 * through ccy_note() below, so one mutex here covers the table and the
 * lifetime counters -- a leaf lock, held for arithmetic only, never across the
 * trust lock or a log write.
 *
 * Deliberately NO trust event and NO failure counter anywhere: a stale-period
 * key is evidence about a peer, not proof (GR3), and 2.5 is the measurement
 * TASK 1.11b demanded before anyone touches the cycle question again. The
 * verdict on what to do with these numbers is a later task, taken on a measured
 * base rate instead of a guess.
 */
pthread_mutex_t cwcy_lock = PTHREAD_MUTEX_INITIALIZER;
struct cwcy_table cwcycle_tab;
unsigned long cwcy_recv_keys = 0;     /* cache keys offered by peers            */
unsigned long cwcy_recv_marked = 0;   /* ... that carried a cycle marker        */
unsigned long cwcy_recv_contra = 0;   /* ... that contradicted the expectation  */
unsigned long cwcy_send_keys = 0;     /* keys handed from cache to clients      */
unsigned long cwcy_send_stale = 0;    /* ... handed in a contradictory state    */
unsigned long cwcy_send_unjudged = 0; /* ... nobody declared a cycle for them   */
unsigned long cwcy_reports = 0;       /* contradiction lines actually written   */

/*
 * TASK 2.3 — the one place a latency becomes evidence.
 *
 * The measurement itself is upstream's: cli-newcamd.c:312 stamps `lastecmtime`
 * when the ECM request goes out and cli-newcamd.c:280 subtracts it from the
 * clock when the reply arrives. This function is handed that number and decides
 * what it means. Nothing here is on the path of a server that answers normally:
 * the table write is one array store, and the trust lock is taken only when a
 * verdict arrives, for exactly one trust_record(), released before the log line
 * is written (GR9, and the same discipline as 1.8 and 2.2).
 *
 * What is scored: CWT_SCOREMASK, which is CWT_FAST alone. A metronome is real
 * evidence for an operator -- and real hardware can be perfectly regular, which
 * the unit suite asserts -- but it is inference without physics, so it is
 * counted and printed and never allowed to decay a score (GR3). GR4 is respected
 * by construction: the event carries the (CAID, PROVID, SID) the reply was for,
 * never a bare per-server identity.
 */
static void cwtime_note(int srv_id, uint32_t lat_ms,
                        uint16_t caid, uint32_t provid, uint16_t sid)
{
	struct cwt_evidence ev;
	unsigned mask, fresh, scored;
	const char *nm;
	char tail[192];

	mask = cwt_seen(&cwt_tab, srv_id, lat_ms, caid, provid, sid,
	                (uint32_t)min_card_latency_ms, GetTickCount(), &ev);
	if (!mask) return;

	/*
	 * Counted here rather than inside the module: the module answers "what is
	 * true of this reply", these counters answer "how much of it happened", and
	 * keeping those apart is what lets the module stay a pure function of its
	 * arguments with no server globals in it -- which is in turn why the whole
	 * of it is unit-testable.
	 */
	if (mask & CWT_FAST) cwtime_fast++;
	if (mask & CWT_DEGEN) cwtime_flat++;

	fresh = ev.fresh;
	if (!fresh) {
		/*
		 * A repeat inside the report window: still a verdict, still counted,
		 * and not silent -- the stats line is where the rate is visible. A
		 * suppression counter that is never incremented looks exactly like a
		 * detector that never fires, which is the trap TASK 2.1's stats taught.
		 */
		cwtime_suppressed++;
		return;
	}

	cwtime_reports++;
	scored = fresh & CWT_SCOREMASK;
	if (scored) {
		pthread_mutex_lock(&trust_lock);
		trust_record(&trust_tab, TRUST_EV_SOFT, DCW_SOURCE_SERVER, srv_id,
		             caid, provid, sid, GetTickCount());
		pthread_mutex_unlock(&trust_lock);
		/* TASK 2.8: the same event, one level coarser. */
		trustagg_note_lk(TRUST_EV_SOFT, DCW_SOURCE_SERVER, srv_id, caid,
		             GetTickCount());
	}

	/* Both are pure, so both stay outside the lock. */
	nm = cwt_name(fresh);
	cwt_format(&ev, tail, (int)sizeof(tail));

	if (fresh & CWT_FAST)
		/*
		 * GR8: the evidence, not the conclusion. The line carries the reply,
		 * the floor it was judged against, the window and how much of the
		 * source's traffic was impossible -- so an operator can disagree with
		 * the verdict using the same numbers the verdict used.
		 */
		debugf(DBG_ERROR," !!! CW TIMING: server %d answered ch %04x:%06x:%04x faster than a card can (%s) -- an answer this quick was not produced by a card\n",
			srv_id, caid, provid, sid, tail);
	else
		/*
		 * `!?>` on purpose: this one is information. The sentence says so in
		 * words, because a line that reads like an accusation but does not
		 * affect any score is worse than either.
		 */
		debugf(DBG_ERROR," !?> CW TIMING: server %d answers like a clock (%s) -- regularity is evidence, not proof; this verdict never moves a score\n",
			srv_id, tail);
}

/*
 * TASK 2.4 -- plausibility scoring, and the one sentence a pattern earns.
 *
 * The split is TASK 1.8's and 2.2's: cwplaus.h holds the arithmetic and the
 * window and is a pure function of its arguments, this function holds the
 * counters, the lock and the wording. The hook is in setdcw.c, above
 * acceptDCW(), for the reason all three of those hooks are: the key most worth
 * examining is the one that is about to be thrown away, and a key thrown away in
 * silence is a black screen with no author.
 *
 * What this adds over 1.8's checksum bit, and why both exist:
 *
 *   1.8 asks "did this key fail the rule the operator declared?" and needs the
 *   declaration -- it is gated on DCWFILTER CHECKSUM so a bouquet that
 *   legitimately fails the rule is not punished. This layer asks a question
 *   about the key's own shape ("is exactly one of its four group sums wrong?")
 *   which means the same thing whether or not any filter is on, so it reads no
 *   gate at all. The price of needing no declaration is a weaker per-key
 *   statement, which is why one damaged key is only counted and three in a
 *   window, in the same group, at 20 % of the source's traffic are what earns a
 *   report and a soft event.
 *
 * GR4 holds by construction: the table is keyed on the source, and the event
 * carries the (CAID, PROVID, SID) that was being served. GR2 holds at the call
 * site: this is only reached when a key was delivered.
 *
 * Locking is the same leaf-lock discipline as every other note function here:
 * trust_lock around the table and the trust write, released before the line is
 * written, and trust_record() called directly rather than through
 * trust_record_lk() (which would take the non-recursive mutex twice).
 */
static void cwp_note(int srctype, int srcid, const uint8_t *key,
                     uint16_t caid, uint32_t provid, uint16_t sid,
                     uint32_t ticks_now)
{
	struct cwp_evidence ev;
	unsigned mask, fresh;
	char line[512];
	int n;

	if (!key) return;

	mask = cwp_seen(&cwplaus_tab, srctype, srcid, key, caid, provid, sid,
	                ticks_now, &ev);
	if (!mask) return;

	if (ev.verdict == CWP_NONE)        cwplaus_full++;
	else if (ev.verdict & CWP_ONE_GROUP) cwplaus_one_group++;
	else                               cwplaus_weak++;

	pthread_mutex_lock(&trust_lock);
	fresh = ev.fresh;
	if (fresh & CWP_SCOREMASK)
		trust_record(&trust_tab, TRUST_EV_SOFT, srctype, srcid, caid, provid,
		             sid, ticks_now);
	pthread_mutex_unlock(&trust_lock);

	/* TASK 2.8: the same event, summed one level coarser (source, CAID). */
	if (fresh & CWP_SCOREMASK)
		trustagg_note_lk(TRUST_EV_SOFT, srctype, srcid, caid, ticks_now);

	if (!(fresh & CWP_ONE_GROUP)) {
		/*
		 * A pattern inside the report window: counted, scored once when it
		 * first appeared, and quiet now. The counter that says how many
		 * were passed over is real, because a suppression counter that
		 * never moves looks exactly like a detector that never fired.
		 */
		if (ev.verdict & CWP_ONE_GROUP) cwplaus_suppressed++;
		return;
	}

	cwplaus_reports++;

	/*
	 * GR8: the evidence every time. The line is built by the module from the
	 * same numbers the verdict was formed on -- it cannot print a rate the
	 * decision did not use -- and the wording says out loud that delivery was
	 * not touched, because a line that looks like an action but is not one is
	 * worse than either.
	 */
	n = cwp_format(&ev, line, (int)sizeof(line));
	if (n > 0)
		debugf(DBG_ERROR," !!! %s\n", line);
}

/*
 * TASK 2.5 -- one cycle/parity observation, from either watcher thread.
 *
 * Same safe-wrapper shape as cwp_note above, with one subtraction: there is no
 * trust write here at all. The caller has already computed the expectation and
 * the observed marker from the live rule, so this function only counts,
 * throttles and formats, under its own leaf lock (see cwcy_lock above). If the
 * module ever had a defect, the worst it could do to the server is write a
 * wrong line -- it cannot reject, delay or score a key, and it never touches
 * the trust lock, so it cannot interact with the ECM path's critical section.
 */
static void ccy_note(int srctype, int srcid,
                     uint16_t caid, uint32_t provid, uint16_t sid,
                     uint32_t ticks_now, int where, int expected, int observed)
{
	struct cwcy_ev ev;
	char line[512];
	int fire, n;

	pthread_mutex_lock(&cwcy_lock);
	fire = cwcy_note(&cwcycle_tab, srctype, srcid, caid, provid, sid,
	                 ticks_now, where, expected, observed, &ev);
	if (!ev.recorded) {
		pthread_mutex_unlock(&cwcy_lock);
		return;
	}

	/* Lifetime totals: every recorded observation, fired or not. */
	if (ev.where == CWCY_AT_SEND) {
		cwcy_send_keys++;
		if (ev.expected && ev.observed && ev.expected != ev.observed)
			cwcy_send_stale++;
		else if (ev.expected && !ev.observed)
			cwcy_send_unjudged++;   /* declared channel, undeclared key */
	}
	else {
		cwcy_recv_keys++;
		if (ev.observed) cwcy_recv_marked++;
		if (ev.expected && ev.observed && ev.expected != ev.observed)
			cwcy_recv_contra++;
	}
	pthread_mutex_unlock(&cwcy_lock);

	if (!fire) return;

	cwcy_reports++;

	/*
	 * GR8: the line is built by the module from the same numbers the verdict
	 * was formed on, and its wording says out loud that nothing was done.
	 */
	n = cwcy_format(&ev, line, (int)sizeof(line));
	if (n > 0)
		debugf(DBG_ERROR," !!! %s\n", line);
}

/*
 * TASK 2.6 -- negative memory. `cwneg_tab` remembers the KEYS that a
 * definitive GR3 proof has shown to be unable to open their picture, so the
 * same bytes can never be delivered twice -- not to the client that proved
 * them, and not to another client of the same channel through a relayer or a
 * fresh cache entry. Marks arrive ONLY from the two proof kinds (the ledger's
 * LEDGER_PROOF branch and the seven reuse-proof sites), at the places those
 * proofs already act; the refusal happens in cache_setdcw() before the key is
 * stored, counted as peer agreement, or served.
 *
 * Marks are proven on several threads (cache, cache-ex clients, card-server
 * threads, the ledger path); the gate runs in the cache thread under
 * prg.lockcache. One leaf mutex covers the table, held only across the
 * arithmetic -- never across a log write, and it takes no other lock, so it
 * cannot participate in any lock order.
 *
 * Nothing here scores and nothing here disables a source (GR3): the proof
 * that filed the key already did that part. This table only remembers bytes.
 */
pthread_mutex_t cwneg_lock = PTHREAD_MUTEX_INITIALIZER;
struct cwneg_table cwneg_tab;
unsigned long cwneg_marks = 0;  /* distinct keys filed (fresh marks)        */
unsigned long cwneg_hits = 0;   /* offers refused because of a remembered key */
unsigned long cwneg_lines = 0;  /* refusal lines actually written           */

/*
 * File a proof: these bytes, on this channel, are dead. Called from the
 * ledger's PROOF branch and from every reuse-proof site, always with the key
 * the proof convicted. A fresh mark announces itself once; a re-proven key
 * stays quiet (the first proof is the evidence, and the line was written).
 */
static void cwn_note(int srctype, int srcid, const uint8_t *cw,
                     uint16_t caid, uint32_t provid, uint16_t sid,
                     uint32_t ticks_now)
{
	char keyhex[17], line[512];
	int fresh, n;

	if (!cw) return;

	pthread_mutex_lock(&cwneg_lock);
	fresh = cwneg_mark(&cwneg_tab, cw, caid, provid, sid,
	                   (uint8_t)srctype, (uint32_t)srcid, ticks_now);
	pthread_mutex_unlock(&cwneg_lock);
	if (!fresh) return;

	cwneg_marks++;
	{
		static const char hx[] = "0123456789ABCDEF";
		int i;
		for (i = 0; i < 8; i++) {
			keyhex[i * 2]     = hx[cw[i] >> 4];
			keyhex[i * 2 + 1] = hx[cw[i] & 0x0F];
		}
		keyhex[16] = 0;
	}
	n = snprintf(line, sizeof(line),
		"CW NEGATIVE MARK: ch %04x:%06x:%04x key %s.. proven unable to open its "
		"picture on source %d/%u -- it will not be delivered again, to any "
		"client, from any source (GR3 proof memory; no score, no disable)",
		caid, provid, sid, keyhex, srctype, (unsigned)srcid);
	if (n > 0)
		debugf(DBG_ERROR," !!! %s\n", line);
}

/*
 * The gate. Returns 1 when the key is remembered poison and MUST NOT be
 * served: cache_setdcw() returns DCW_ERROR immediately after, before storage
 * and before any client can see the key. Counts every refusal; writes at most
 * one line per key per CWNEG_WINDOW.
 */
static int cwn_gate(const uint8_t *cw, uint16_t caid, uint32_t provid,
                    uint16_t sid, uint32_t ticks_now)
{
	struct cwneg_ev ev;
	char line[512];
	int hit, n;

	if (!cw) return 0;

	pthread_mutex_lock(&cwneg_lock);
	hit = cwneg_hit(&cwneg_tab, cw, caid, provid, sid, ticks_now, &ev);
	if (hit) cwneg_hits++;
	pthread_mutex_unlock(&cwneg_lock);

	if (!hit) return 0;

	if (ev.fired) {
		cwneg_lines++;
		n = cwneg_format(&ev, line, (int)sizeof(line));
		if (n > 0)
			debugf(DBG_ERROR," !!! %s\n", line);
	}
	return 1;
}

/*
 * TASK 2.7 -- complement-mirror detection. `cwcm_tab` is keyed on the source
 * alone (GR4), like 2.4's table, and the counters are plain reads from the
 * stats tick.
 *
 * A mirror key is EDIT evidence about a source: its second half is the
 * bitwise complement of its first, which is a constructed key -- and the
 * repaired variant passes every structural layer this project has, so
 * nothing else explains it. The pattern (3 mirror keys, 20 % of a source's
 * 60 s window) is a SOFT trust event, exactly like 2.4's one-group pattern;
 * a single mirror key, however unlikely by chance, convicts nothing (GR3).
 *
 * Nothing here rejects or delays anything: mirror keys pass the checksum
 * layer and are delivered exactly as before -- the point of this layer is
 * that the log finally says WHY they will not open.
 */
struct cwcm_table cwcm_tab;
unsigned long cwcm_keys = 0;      /* keys scanned                             */
unsigned long cwcm_mirror = 0;    /* keys carrying the mirror verdict         */
unsigned long cwcm_reports = 0;   /* pattern lines actually written           */
unsigned long cwcm_suppressed = 0;/* patterns inside the report window        */

/*
 * The evidence-only hook, in the same place and for the same reason as its
 * three siblings (dcwstruct/cwentropy/cwplaus): above the accept gates, so
 * the key most worth examining -- the one about to be discarded -- is
 * examined too.
 */
static void cm_note(int srctype, int srcid, const uint8_t *key,
                    uint16_t caid, uint32_t provid, uint16_t sid,
                    uint32_t ticks_now)
{
	struct cwcm_evidence ev;
	unsigned mask, fresh;
	char line[512];
	int n;

	if (!key) return;

	mask = cwcm_seen(&cwcm_tab, srctype, srcid, key, caid, provid, sid,
	                 ticks_now, &ev);
	if (!mask) {
		cwcm_keys++;
		return;
	}

	cwcm_keys++;
	if (ev.verdict & CWCM_MIRROR) cwcm_mirror++;

	pthread_mutex_lock(&trust_lock);
	fresh = ev.fresh;
	if (fresh & CWCM_SCOREMASK)
		trust_record(&trust_tab, TRUST_EV_SOFT, srctype, srcid, caid, provid,
		             sid, ticks_now);
	pthread_mutex_unlock(&trust_lock);

	/* TASK 2.8: the same event, summed one level coarser (source, CAID). */
	if (fresh & CWCM_SCOREMASK)
		trustagg_note_lk(TRUST_EV_SOFT, srctype, srcid, caid, ticks_now);

	if (!(fresh & CWCM_MIRROR)) {
		/* A pattern inside the report window: counted, scored once when it
		 * first appeared, quiet now -- and the counter is real, because a
		 * suppression counter that never moves looks like a blind detector. */
		if (ev.verdict & CWCM_MIRROR) cwcm_suppressed++;
		return;
	}

	cwcm_reports++;

	n = cwcm_format(&ev, line, (int)sizeof(line));
	if (n > 0)
		debugf(DBG_ERROR," !!! %s\n", line);
}

/*
 * TASK 1.5 — CWs proven poisoned, by origin. Fixed size, .bss, no allocation
 * on the ECM path (GR9). Marks are set only on a definitive reuse proof (GR3)
 * and lifted by cache_purge_unmark() (GR8).
 */
struct purge_table purge_tab;

/*
 * TASK 1.6 — channels whose last key was bad, by source. Read by
 * srvtab_arrange() so the retry after a bad key goes somewhere else.
 * Fixed size, .bss, no allocation on the ECM path (GR9).
 */
struct rotation_table rotation_tab;
struct ledger_table ledger_tab;
unsigned long ledger_agrees = 0;       /* two sources produced the same key   */
unsigned long ledger_disputes = 0;     /* two sources disagreed               */
unsigned long ledger_selfdisputes = 0; /* one source contradicted itself      */
unsigned long ledger_proofs = 0;       /* a contradiction the client confirmed */
unsigned long ledger_unplaceable = 0;  /* a failure whose key was not recorded */

/*
 * The counters that go with it, declared here rather than next to the ledger's
 * functions so that stats_snapshot_fill() below reads a definition instead of
 * an implicit one. (The first version of this file declared them further down
 * and the permissive flags in this build let the use compile anyway.)
 *
 * For the record, because the wrong story would be worse than none: the live
 * stats line reported "0 proof" while the proof demonstrably happened, and the
 * cause was NOT this ordering. The assignment of the fields in
 * stats_snapshot_fill() had simply never been written -- a scripted edit did
 * not land -- so the formatter printed five uninitialised stack words that
 * happened to be zero, which is exactly what an idle server would have printed.
 * A counter that is never filled is indistinguishable from a counter that is
 * always empty, which is the reason the live target asserts on these numbers
 * rather than on the absence of a crash.
 */


/*
 * TASK 1.9 — the three configuration knobs.
 *
 * Globals rather than cfg fields, following TASK 1.2's windows: they are set by
 * init_config() so a config file that never mentions them cannot change a
 * default, and they are read on paths that have no reason to care which profile
 * a client belongs to.
 *
 *   rotation_badcw_limit     BAD-CW-LIMIT. 1 = avoid on the first confirmed bad
 *                            CW, which is what TASK 1.6 shipped.
 *   purge_maxage_ms          SERVICE-BLACKLIST-TIME. 0 = a mark never expires.
 *   phase1_stats_window_ms   STATS-WINDOW. 0 = no periodic summary line.
 *
 * All three defaults are the "do exactly what you did before" value, which is
 * the brief's backward-compatibility requirement stated as three integers.
 */
int      rotation_badcw_limit   = 1;
uint32_t purge_maxage_ms        = 0;
uint32_t phase1_stats_window_ms = 0;

/* Where the previous STATS-WINDOW line was emitted, on the ticker's own clock. */
uint32_t phase1_stats_last_ms   = 0;

/*
 * Fill a snapshot from the live tables.
 *
 * Lives here rather than in statsline.h because it reads six globals and two
 * tables, and a header that reached into that much state could not be tested in
 * isolation -- which is the whole reason statsline_format() is a separate,
 * pure function.
 *
 * No lock is taken. Every field is a counter that only ever increases (or, for
 * the two live-entry counts, a bounded scan), and a line that is one update
 * stale is still an accurate description of the window it covers.
 */
/*
 * TASK 2.10 -- the trust lifecycle.
 *
 * FACET 1, FADE. The trust tiers never aged: a peer condemned once stayed
 * condemned until the LRU happened to evict it, and a peer at the ceiling
 * kept its standing indefinitely. Evidence is a statement about recent
 * behaviour, so an entry idle past TRUST-FADE-GRACE relaxes toward
 * TRUST_START -- one point per TRUST-FADE-STEP of silence, from either side,
 * recomputed statelessly from (now, lastseen) so it cannot drift. A busy
 * peer never fades (its events keep it fresh); a poisoner that keeps
 * poisoning is refreshed by its own verdicts; negative memory NEVER fades
 * (D26: it is byte-conviction, not reputation, and it has no TTL on purpose).
 *
 * FACET 2, PERSISTENCE. Everything above lives in .bss, so a restart wiped
 * the fine tier TASK 1.7 gates on, the coarse tier 2.8 brakes on, and
 * negative memory 2.6 guarantees with -- a restart was the cheapest way to
 * launder a proven key. TRUST-PERSIST (default OFF, like every behaviour
 * switch in this project) writes a bounded plain-text snapshot from the
 * cache thread's tick -- OFF the ECM path (GR9) -- to a temp file renamed
 * into place; lifecycle_load() reads it back once at startup, before any
 * thread can act on a verdict. Scores are clamped on parse; a line that
 * fails validation is counted and skipped whole. The file has no secrets
 * and no ticks -- only identities, scores, counters, and an age.
 *
 * Switches: TRUST-PERSIST (off), TRUST-PERSIST-FILE (multics.trust),
 * TRUST-PERSIST-EVERY (300 s), TRUST-FADE-GRACE (1800 s, 0 disables fade),
 * TRUST-FADE-STEP (30 s per point).
 */
int      trust_persist       = 0;
int      trust_persist_every = 300;
char     trust_persist_file[256] = "multics.trust";
uint32_t trust_fade_grace    = 1800;   /* seconds; 0 disables the fade  */
uint32_t trust_fade_step     = 30;     /* seconds per point of silence  */

static uint32_t tl_lastsave = 0;
static unsigned long tl_faded = 0, tl_saves = 0, tl_savefails = 0;

/*
 * Snapshot the three tables and leave them in trust_persist_file. The table
 * walks run under their own leaf locks; the file I/O runs outside every
 * lock, on the cache thread's tick. Returns 0 on success.
 */
static int tl_save(void)
{
	struct tl_rec rf[TRUST_TABLE_SIZE];
	struct tl_rec ra[TRUSTAGG_TABLE_SIZE];
	struct tl_rec rn[CWNEG_SLOTS];
	int nf = 0, na = 0, nn = 0, i;
	uint32_t now = GetTickCount();
	char tmp[300];
	FILE *f;

	pthread_mutex_lock(&trust_lock);
	for (i = 0; i < TRUST_TABLE_SIZE; i++) {
		struct trust_entry *e = &trust_tab.e[i];
		if (!e->used) continue;
		memset(&rf[nf], 0, sizeof(rf[nf]));
		rf[nf].kind = TL_TF;
		rf[nf].srctype = e->srctype;  rf[nf].srcid = e->srcid;
		rf[nf].caid = e->caid;        rf[nf].provid = e->provid;
		rf[nf].sid = e->sid;          rf[nf].score = e->score;
		rf[nf].nproof = e->nproof;    rf[nf].nsoft = e->nsoft;
		rf[nf].age_s = (now - e->lastseen) / 1000u;
		nf++;
	}
	for (i = 0; i < TRUSTAGG_TABLE_SIZE; i++) {
		struct trustagg_entry *e = &trustagg_tab.e[i];
		if (!e->used) continue;
		memset(&ra[na], 0, sizeof(ra[na]));
		ra[na].kind = TL_TA;
		ra[na].srctype = e->srctype;  ra[na].srcid = e->srcid;
		ra[na].caid = e->caid;        ra[na].score = e->score;
		ra[na].nproof = e->nproof;    ra[na].nsoft = e->nsoft;
		ra[na].ngood = e->ngood;
		ra[na].age_s = (now - e->lastseen) / 1000u;
		na++;
	}
	pthread_mutex_unlock(&trust_lock);

	pthread_mutex_lock(&cwneg_lock);
	for (i = 0; i < CWNEG_SLOTS; i++) {
		struct cwneg_slot *sl = &cwneg_tab.s[i];
		if (!sl->used) continue;
		memset(&rn[nn], 0, sizeof(rn[nn]));
		rn[nn].kind = TL_NG;
		rn[nn].caid = sl->caid;       rn[nn].provid = sl->provid;
		rn[nn].kdigest = sl->kdigest;
		memcpy(rn[nn].key8, sl->key8, 8);
		rn[nn].age_s = (now - sl->last_ticks) / 1000u;
		nn++;
	}
	pthread_mutex_unlock(&cwneg_lock);

	snprintf(tmp, sizeof(tmp), "%s.tmp", trust_persist_file);
	f = fopen(tmp, "w");
	if (!f) return -1;
	fprintf(f, "# multics trust lifecycle v1 -- fine / coarse / negative\n");
	for (i = 0; i < nf; i++) {
		char line[192];
		if (tl_format_rec(line, (int)sizeof(line), &rf[i]) > 0) fprintf(f, "%s\n", line);
	}
	for (i = 0; i < na; i++) {
		char line[192];
		if (tl_format_rec(line, (int)sizeof(line), &ra[i]) > 0) fprintf(f, "%s\n", line);
	}
	for (i = 0; i < nn; i++) {
		char line[192];
		if (tl_format_rec(line, (int)sizeof(line), &rn[i]) > 0) fprintf(f, "%s\n", line);
	}
	fclose(f);
	if (rename(tmp, trust_persist_file) != 0) {
		remove(tmp);
		return -1;
	}
	return 0;
}

/*
 * Read the snapshot back. Called ONCE, from the config thread, right after
 * the first successful config parse and before any server thread exists:
 * no other thread can act on a verdict while the state is being restored.
 * A missing file is not an error (first boot); a line that fails validation
 * is counted and skipped whole.
 */
static int tl_loaded_already = 0;
static unsigned long tl_loaded_fine, tl_loaded_coarse, tl_loaded_neg, tl_badlines;

static void lifecycle_load(void)
{
	FILE *f;
	char buf[512];
	uint32_t now = GetTickCount();

	if (tl_loaded_already) return;
	tl_loaded_already = 1;
	if (!trust_persist) return;

	f = fopen(trust_persist_file, "r");
	if (!f) return;

	while (fgets(buf, (int)sizeof(buf), f)) {
		struct tl_rec r;
		/* the header comment and blank lines are structure, not records */
		if (buf[0] == '#' || buf[0] == '\n' || buf[0] == 0) continue;
		if (tl_parse_line(buf, &r) != 0 || r.age_s > TL_MAX_AGE_S) {
			tl_badlines++;
			continue;
		}
		if (r.kind == TL_TF) {
			trust_restore(&trust_tab, r.srctype, r.srcid, r.caid, r.provid,
			              r.sid, r.score, r.nproof, r.nsoft,
			              r.age_s * 1000u, now);
			tl_loaded_fine++;
		}
		else if (r.kind == TL_TA) {
			trustagg_restore(&trustagg_tab, r.srctype, r.srcid, r.caid,
			                 r.score, r.nproof, r.nsoft, r.ngood,
			                 r.age_s * 1000u, now);
			tl_loaded_coarse++;
		}
		else if (r.kind == TL_NG) {
			pthread_mutex_lock(&cwneg_lock);
			if (cwneg_restore(&cwneg_tab, r.caid, r.provid, r.kdigest,
			                  r.key8, r.age_s * 1000u, now))
				cwneg_marks++;   /* a restored conviction is a filed mark */
			pthread_mutex_unlock(&cwneg_lock);
			tl_loaded_neg++;
		}
	}
	fclose(f);
	debugf(DBG_ERROR, " trust lifecycle: loaded %lu fine, %lu coarse, %lu negative from %s (%lu line(s) unusable)\n",
		tl_loaded_fine, tl_loaded_coarse, tl_loaded_neg, trust_persist_file, tl_badlines);
}

/*
 * The tick: fade, then a throttled save. Called from the cache thread's
 * existing three-second wakeup, next to phase1_stats_tick() -- off the ECM
 * path (GR9), under the same leaf locks the stats snapshot uses.
 */
static void tl_tick(uint32_t now)
{
	if (trust_fade_grace && trust_fade_step) {
		int moved;
		/* the fade walks both tables: under the same leaf locks the savers use */
		pthread_mutex_lock(&trust_lock);
		moved = tl_fade_fine(&trust_tab, now,
		                     trust_fade_grace * 1000u, trust_fade_step * 1000u)
		      + tl_fade_coarse(&trustagg_tab, now,
		                     trust_fade_grace * 1000u, trust_fade_step * 1000u);
		pthread_mutex_unlock(&trust_lock);
		tl_faded += (unsigned long)moved;
	}
	if (trust_persist && trust_persist_every > 0 &&
	    (tl_lastsave == 0 ||
	     (int32_t)(now - tl_lastsave) >= (int32_t)(trust_persist_every * 1000u))) {
		tl_lastsave = now;
		if (tl_save() == 0) tl_saves++;
		else {
			tl_savefails++;
			debugf(getdbgflag(DBG_CACHE,0,0), " cache: trust snapshot to %s failed (%lu ok, %lu failed)\n",
				trust_persist_file, tl_saves, tl_savefails);
		}
	}
}

static void stats_snapshot_fill(struct stats_snapshot *s)
{
	int i;

	s->window_ms         = phase1_stats_window_ms;
	s->struct_anomalous  = dcwstruct_anomalous;
	s->struct_suppressed = dcwstruct_suppressed;
	s->deferrals         = cachepref_defer;

	s->purge_live     = cache_purge_count(&purge_tab);
	s->purge_marked   = purge_tab.marked;
	s->purge_released = purge_tab.released;
	s->purge_expired  = purge_tab.expired;

	s->rot_skipped  = rotation_tab.avoided;
	s->rot_capped   = rotation_tab.capped;
	s->rot_below    = rotation_tab.deferred;
	s->rot_limit    = rotation_badcw_limit;
	s->rot_channels = 0;
	for (i = 0; i < ROTATION_SLOTS; i++)
		if (rotation_tab.e[i].inuse) s->rot_channels++;

	s->trust_live = 0;
	for (i = 0; i < TRUST_TABLE_SIZE; i++)
		if (trust_tab.e[i].used) s->trust_live++;

	/*
	 * TASK 2.1 -- the agreement ledger. Read without a lock, like every other
	 * field here: each is a counter that only ever increases, and a value one
	 * update stale is still an accurate description of the window it covers.
	 */
	s->agr_agree   = ledger_agrees;
	s->agr_dispute = ledger_disputes;
	s->agr_self    = ledger_selfdisputes;
	s->agr_proof   = ledger_proofs;
	s->agr_miss    = ledger_unplaceable;
	/*
	 * TASK 2.2 -- three numbers rather than one, because the two collision
	 * flavours mean different things to the operator: `derived` is a source
	 * editing its own keys (attributable, scored), `foreign` is two sources
	 * whose keys are a few bits apart (logged, never scored -- GR1). A rising
	 * `foreign` with a flat `derived` is the signature of a peer echoing
	 * another peer's key rather than of a broken card server.
	 */
	s->cwen_impossible = cwentropy_lowdiv;
	s->cwen_derived    = cwentropy_derived;
	s->cwen_foreign    = cwentropy_foreign;

	/*
	 * TASK 2.3 -- the latency counters, filled here for the same reason the two
	 * blocks above are: a snapshot that reports zeros because nobody filled it
	 * is indistinguishable from a detector that never fired. That mistake was
	 * made once already in this project (TASK 2.1) and it cost a day.
	 */
	s->cwt_fast       = cwtime_fast;
	s->cwt_flat       = cwtime_flat;
	s->cwt_suppressed = cwtime_suppressed;

	/* TASK 2.4 -- the same rule as the blocks above: filled or it did not happen. */
	s->cwp_full       = cwplaus_full;
	s->cwp_one_group  = cwplaus_one_group;
	s->cwp_weak       = cwplaus_weak;
	s->cwp_reports    = cwplaus_reports;

	/* TASK 2.5 -- cycle/parity visibility, offer side and hand-off side. */
	s->cyc_keys       = cwcy_recv_keys;
	s->cyc_marked     = cwcy_recv_marked;
	s->cyc_contra     = cwcy_recv_contra;
	s->cycs_keys      = cwcy_send_keys;
	s->cycs_stale     = cwcy_send_stale;
	s->cycs_unverif   = cwcy_send_unjudged;
	s->cyc_reports    = cwcy_reports;

	/* TASK 2.6 -- negative memory; the live count needs the table's leaf
	 * lock for one walk (called from the stats tick, not the ECM path). */
	pthread_mutex_lock(&cwneg_lock);
	s->neg_live       = (unsigned long)cwneg_count(&cwneg_tab);
	pthread_mutex_unlock(&cwneg_lock);
	s->neg_marks      = cwneg_marks;
	s->neg_hits       = cwneg_hits;
	s->neg_lines      = cwneg_lines;

	/* TASK 2.7 -- complement-mirror detection. */
	s->cm_keys        = cwcm_keys;
	s->cm_mirror      = cwcm_mirror;
	s->cm_reports     = cwcm_reports;
	s->cm_suppressed  = cwcm_suppressed;

	/*
	 * TASK 2.8 -- the coarse trust tier. The walk takes trust_lock: it runs
	 * on the stats tick, off the ECM path, and the entries live under that
	 * lock. trustagg_gate() increments trustagg_skipped under the same lock,
	 * so the read is consistent with the decision that made it.
	 */
	{
		int ta_p = 0, ta_b = 0;
		pthread_mutex_lock(&trust_lock);
		trustagg_count(&trustagg_tab, &ta_p, &ta_b);
		s->ta_skipped = trustagg_skipped;
		s->ta_lines   = trustagg_lines;
		pthread_mutex_unlock(&trust_lock);
		s->ta_peers   = (unsigned long)ta_p;
		s->ta_below   = (unsigned long)ta_b;
	}

	/* TASK 2.9 -- cache protocol guard (cache-thread-only counters). */
	s->cg_short   = cg_short;
	s->cg_unknown = cg_unknown;
	s->tl_fine = tl_loaded_fine;
	s->tl_coarse = tl_loaded_coarse;
	s->tl_neg = tl_loaded_neg;
	s->tl_bad = tl_badlines;
	s->tl_faded = tl_faded;
	s->tl_saves = tl_saves;
	s->tl_savefails = tl_savefails;
	}

/*
 * The STATS-WINDOW tick, called from the cache thread's existing three-second
 * wakeup so that no new thread and no new timer is created for it.
 *
 * Two jobs, in this order:
 *   1. lift marks that have outlived SERVICE-BLACKLIST-TIME, each with its own
 *      log line -- GR8 wants the exact evidence, and "a mark expired" without
 *      saying which one would be untraceable (GR1).
 *   2. emit the one-line summary, if the window has elapsed.
 *
 * Granularity is the cache thread's poll timeout (3001 ms), so a window shorter
 * than about three seconds is honoured to within one wakeup rather than exactly.
 * That is stated in the documentation rather than engineered around: the only
 * thing that would fix it is a thread of its own, and a thread that exists to
 * print a line every N seconds is not worth the risk on this codebase.
 */
static void phase1_stats_tick(void)
{
	uint32_t now = GetTickCount();

	if (purge_maxage_ms) {
		struct purge_entry lifted[CACHE_PURGE_SLOTS];
		int n, i;
		n = cache_purge_expire(&purge_tab, now, purge_maxage_ms,
		                       lifted, CACHE_PURGE_SLOTS);
		for (i = 0; i < n && i < CACHE_PURGE_SLOTS; i++)
			debugf(DBG_ERROR," !!! CACHE PURGE EXPIRE: ch %04x:%06x:%04x mark on source %d/%d lifted after %u ms -- the channel falls back to the normal source order\n",
				lifted[i].caid, lifted[i].provid, lifted[i].sid,
				(int)lifted[i].srctype, (int)lifted[i].srcid,
				(unsigned)purge_maxage_ms);
	}

	if (!phase1_stats_window_ms) return;
	if ((uint32_t)(now - phase1_stats_last_ms) < phase1_stats_window_ms) return;
	phase1_stats_last_ms = now;

	{
		struct stats_snapshot snap;
		/*
		 * TASK 2.3 grew this line again: the previous 320 was already at its
		 * limit (the line measured 319 characters and was being cut mid-field,
		 * which is how the truncation was noticed at all). The format is a
		 * bounded snprintf that reports what it could not write, so the only
		 * symptom of getting this wrong is a silently half-written summary.
		 */
		char line[1024];   /* the line has grown with every layer; 512 truncated it mid-segment */
		stats_snapshot_fill(&snap);
		statsline_format(&snap, line, (int)sizeof(line));
		debugf(DBG_ERROR,"%s\n", line);
	}
}

/*
 * TASK 2.1 -- the agreement ledger, and the one action a proof earns.
 *
 * LEDGER STRUCTURE. ledger.h holds the table and the whole verdict; this file
 * holds the sentence. The split is the same one TASK 1.8 used for the
 * structural scan and TASK 1.9 for the stats line: the header stays a pure
 * function of its arguments so make-x64/test_ledger.c can exercise every
 * verdict -- including every verdict that must NOT fire -- with no server, no
 * threads and no clock.
 *
 * LOCKING. trust_lock, the leaf lock TASK 1.8 already uses for dcwstruct_tab.
 * The ledger is reached from every ECM thread (setdcw.c) and from the client
 * threads (srv-newcamd.c), so it is shared state by definition. Nothing else is
 * acquired while it is held except rotation_note_bad_limited(), which is pure
 * and takes no lock of its own -- the same call the retry path already makes
 * without one.
 *
 * WHAT A PROOF IS ALLOWED TO DO, AND WHY IT IS ALLOWED TO DO IT AT ALL
 *   Every other mechanism in Phase 1 and 1.9 tolerates, defers or counts. This
 *   one acts on a single event, which is exactly what GR3 warns against -- so it
 *   acts only on GR3's own third definitive proof: a key was delivered, the
 *   client came back too soon, and an independent source produced a different
 *   key for the same ECM. That is not an inference about one client; it is a
 *   contradiction between two sources, resolved by which key was in use.
 *   Consequences, all three deliberate:
 *     - the tolerance does not apply (limit 1). BAD-CW-LIMIT counts accidents;
 *       a proof is not an accident, and requiring two proofs for one fact would
 *       only extend the black screen.
 *     - the accusation is the DELIVERED source, never the dissenter (GR1).
 *     - the event is reversible and logged with its evidence (GR8): the trust
 *       engine's recovery path lifts the score again on a good delivery, and the
 *       purge mark expires on SERVICE-BLACKLIST-TIME or when the source itself
 *       delivers a key the client accepts.
 *
 * The counters below are read by the STATS-WINDOW line, which is where an
 * operator sees a dispute rate without grepping.
 */
/*
 * Turn a proof verdict into action. Called with trust_lock held; the caller
 * writes the log line after releasing it, so no I/O happens under the lock.
 */
static void ledger_act(const struct ledger_event *ev, char *tagg, int taggsz)
{
	int acc = ev->acc_slot;
	int accst = ev->src[acc][0];
	int accid = ev->src[acc][1];
	uint32_t ticks = GetTickCount();

	ledger_proofs++;

	/*
	 * GR3's definitive proof, so the hard events are the honest ones: a real
	 * proof rather than another soft signal, and an avoidance with no tolerance.
	 * rotation_note_bad_limited() with limit 1 also makes the same source the
	 * one srvtab_arrange() skips on the very next request, which is where the
	 * black screen actually stops.
	 */
	trust_record(&trust_tab, TRUST_EV_PROOF, accst, accid,
	              ev->caid, ev->provid, ev->sid, ticks);
	/*
	 * TASK 2.8: the same proof, summed one level coarser (source, CAID).
	 * We are under the caller's trust_lock here, so this is the bare form;
	 * the sentence is formatted now (pure) and written after the unlock,
	 * in the caller's `tagg` buffer.
	 */
	if (tagg) {
		struct trustagg_evidence taev;
		if (trustagg_note(&trustagg_tab, TRUST_EV_PROOF, accst, accid,
		                  ev->caid, ticks, &taev))
			trustagg_format(&taev, tagg, taggsz);
	}
	(void)rotation_note_bad_limited(&rotation_tab, accst, accid,
	                                ev->caid, ev->sid, ev->provid, ticks, 1);

	/*
	 * The purge mark is a cache-side mechanism: it stops a poisoned entry being
	 * served to anyone else. Marking a card server here would occupy a slot
	 * that gates nothing, so the type test is not cosmetic -- the table is 32
	 * entries and a meaningless mark costs a real one.
	 */
	if (accst == DCW_SOURCE_CACHE) {
		/*
		 * TASK 2.6 -- the key the client held and that failed is now proven
		 * poison: file it, so no cache source can hand the same bytes to
		 * anyone again. Same scoping as the purge mark (cache origins only,
		 * GR1); the card-server side keeps its own rotation machinery.
		 */
		cwn_note(accst, accid, ev->cw[acc], ev->caid, ev->provid, ev->sid, ticks);
		int fresh = cache_purge_mark(&purge_tab, (uint8_t)accst, (uint32_t)accid,
		                             ev->caid, ev->sid, ev->provid, ticks);
		if (fresh)
			debugf(DBG_ERROR," !!! CACHE PURGE: ch %04x:%06x:%04x marked on source %d/%d -- an independent source produced a different key for the same ecm and the client refused the delivered one\n",
				ev->caid, ev->provid, ev->sid, accst, accid);
	}
}

/*
 * A source handed this server a control word for an ECM. `delivered` is 1 when
 * a client actually received it.
 *
 * Called from the two delivery points in setdcw.c, above the STAT_DCW_SUCCESS
 * early return, so a key that is about to be thrown away is still recorded --
 * the disagreeing key is exactly the one that usually gets thrown away.
 */
static void ledger_note_delivery(const uint8_t *ecmd5, uint32_t hash,
                                 uint16_t caid, uint32_t provid, uint16_t sid,
                                 const uint8_t *cw, int srctype, int srcid,
                                 uint32_t ticks)
{
	struct ledger_event ev;
	char line[320];
	char tagg[384];   /* TASK 2.8: the coarse-tier sentence, if one crossed */
	int rc;

	line[0] = 0; tagg[0] = 0;
	pthread_mutex_lock(&trust_lock);
	rc = ledger_offer(&ledger_tab, ecmd5, hash, caid, provid, sid, cw,
	                   srctype, srcid, 1, ticks, &ev);
	switch (rc) {
	case LEDGER_AGREE:       ledger_agrees++;       break;
	case LEDGER_DISPUTE:     ledger_disputes++;     break;
	case LEDGER_SELFDISPUTE: ledger_selfdisputes++; break;
	case LEDGER_PROOF:
		ledger_act(&ev, tagg, (int)sizeof(tagg));
		ledger_format(&ev, line, (int)sizeof(line));
		break;
	default: break;
	}
	pthread_mutex_unlock(&trust_lock);

	if (line[0])
		debugf(DBG_ERROR,"%s\n", line);
	if (tagg[0]) {
		trustagg_lines++;
		debugf(DBG_ERROR," !!! %s\n", tagg);
	}
}

/*
 * The client came back for the same ECM far sooner than its crypto period, after
 * a key from this source. Returns 1 when that failure completed a proof.
 *
 * Only RETRY_HARD is passed here, never RETRY_SOFT. A hard retry is the 1-3 s
 * window the brief describes as the observable failure; a soft one is inference
 * on top of inference, and GR3 does not allow a hard action on inference. A
 * soft retry that is real will produce a hard one next cycle anyway.
 *
 * GR2 holds at the call site: the caller only reaches this when a CW was
 * actually delivered, so a timeout is never counted as a failure of anybody.
 */
static int ledger_note_failure(const uint8_t *ecmd5, uint32_t hash,
                               uint16_t caid, uint32_t provid, uint16_t sid,
                               const uint8_t *cw, int srctype, int srcid,
                               uint32_t ticks)
{
	struct ledger_event ev;
	char line[320];
	char tagg[384];   /* TASK 2.8: the coarse-tier sentence, if one crossed */
	int rc, proof = 0;

	line[0] = 0; tagg[0] = 0;
	pthread_mutex_lock(&trust_lock);
	rc = ledger_failed(&ledger_tab, ecmd5, hash, caid, provid, sid, cw,
	                    srctype, srcid, ticks, &ev);
	if (rc == LEDGER_PROOF) {
		ledger_act(&ev, tagg, (int)sizeof(tagg));
		ledger_format(&ev, line, (int)sizeof(line));
		proof = 1;
	} else if (!ledger_find(&ledger_tab, ecmd5, ticks)) {
		/*
		 * Nothing was recorded for this ECM, so the ledger could not place the
		 * failure. Counted rather than ignored: on a real peer population this
		 * number rising means entries are being evicted or aged out before the
		 * contradiction arrives, and the table is too small.
		 */
		ledger_unplaceable++;
	}
	pthread_mutex_unlock(&trust_lock);

	if (line[0])
		debugf(DBG_ERROR,"%s\n", line);
	if (tagg[0]) {
		trustagg_lines++;
		debugf(DBG_ERROR," !!! %s\n", tagg);
	}
	return proof;
}

char cccam_nodeid[8];

int flag_debugscr;
#ifdef DEBUG_NETWORK
int flag_debugnet;
#endif
int flag_debugfile;
char debug_file[256];
char sms_file[256];
char ecm_file[256];

///
///
struct config_data cfg;
struct program_data prg;

uint32_t ecm_check_time = 0;



void srv_cstatadd( struct server_data *srv, int csid, int ok, uint32_t ecmoktime)
{
	int i;
	for(i=0; i<MAX_CSPORTS; i++) {
		if (!srv->cstat[i].csid) {
			srv->cstat[i].csid = csid;
			srv->cstat[i].ecmnb = 1;
			if (ok) {
				srv->cstat[i].ecmok = 1;
				srv->cstat[i].ecmoktime = ecmoktime;
			}
			else {
				srv->cstat[i].ecmok = 0;
				srv->cstat[i].ecmoktime = 0;
			}
			break;
		}
		else if (srv->cstat[i].csid==csid) {
			srv->cstat[i].ecmnb++;
			if (ok) {
				srv->cstat[i].ecmok++;
				srv->cstat[i].ecmoktime += ecmoktime;
			}
			break;
		}
	}
}



int card_sharelimits(struct sharelimit_data sharelimits[100], uint16_t caid, uint32_t provid)
{
	int i;
	int uphops1 = 10; // for 0:0
	int uphops2 = 10; // for caid:0
	for (i=0; i<100; i++) {
		if (sharelimits[i].caid==0xffff) break;
		if (!sharelimits[i].caid) {
			if (!sharelimits[i].provid) uphops1 = sharelimits[i].uphops;
		}
		else if (sharelimits[i].caid==caid) {
			if (sharelimits[i].provid==provid) return sharelimits[i].uphops;
			else if (!sharelimits[i].provid) uphops2 = sharelimits[i].uphops;
		}
	}
	if (uphops2<uphops1) return uphops2; else return uphops1;// Max UPHOPS
}


void cardsids_add(struct cs_card_data *card, uint32_t prov, uint16_t sid,int val)
{
	if (!sid) return;
	struct sid_data *sidata = malloc( sizeof(struct sid_data) );
	memset( sidata, 0, sizeof(struct sid_data) );

	sidata->sid=sid;
	sidata->prov=prov;
	sidata->val=val;
	sidata->next = card->sids[sid>>8];
	card->sids[sid>>8] = sidata;
}


int cardsids_update(struct cs_card_data *card, uint32_t prov, uint16_t sid,int val)
{
	if (!sid) return 0;

	struct sid_data *sidata = card->sids[sid>>8];
	while (sidata) {
		if (sidata->sid==sid)
		if (sidata->prov==prov) {
			/*
			 * TASK 3.13 -- the -100 floor was a lock, not a cap.
			 *
			 * `val>-100` skipped every later update once the score
			 * reached the floor, so the success branch that resets a
			 * negative score to 0 never ran again. With DCW MAXFAILED
			 * set, srvtab_arrange skips a card at
			 * val <= -maxfailedecm, and a score frozen at -100 stays
			 * excluded until restart: a black screen on that channel.
			 *
			 * The floor still holds: further failures do not pass -100.
			 * A success at the floor recovers to 0, the same reset the
			 * negative branch already does above the floor. The +100
			 * ceiling is untouched.
			 */
			if (sidata->val <= -100) {
				if (val > 0) {
					sidata->val = 0;
					debugf(DBG_ERROR, " !!! CARDSIDS: prov:sid %06x:%04x recovered from the floor\n", prov, sid);
				}
				else if (sidata->val < -100) sidata->val = -100;
			}
			else if (sidata->val < 100) {
				if (sidata->val>0) {
					if (val>0) sidata->val +=val;
					else sidata->val =0;
				}
				else if (sidata->val<0) {
					if (val<0) {
						sidata->val +=val;
						if (sidata->val <= -100) {
							sidata->val = -100;
							debugf(DBG_ERROR, " !!! CARDSIDS: prov:sid %06x:%04x at floor -100; a later success still recovers\n", prov, sid);
						}
					}
					else sidata->val=0;
				}
				else sidata->val +=val; // else a card that has decode success one time cannot return to decode failed
			}
			return 1;
		}
		sidata = sidata->next;
	}

	if ( !sidata && sid ) {
		cardsids_add( card, prov, sid,val);
	}
	return 1;
}


///////////////////////////////////////////////////////////////////////////////
// Common profile functions
///////////////////////////////////////////////////////////////////////////////

struct cardserver_data *getcsbycaidprov( uint16_t caid, uint32_t prov)
{
	int i;
	if (!caid) return NULL;
	struct cardserver_data *cs = cfg.cardserver;
	while (cs) {
		if (cs->card.caid==caid) {
			for(i=0; i<cs->card.nbprov;i++) if (cs->card.prov[i].id==prov) return cs;
			if ( ((cs->card.caid & 0xff00)==0x1800)
				|| ((cs->card.caid & 0xff00)==0x0900)
				|| ((cs->card.caid & 0xff00)==0x0b00) ) return cs;
		}
		cs = cs->next;
	}
	return NULL;
}


struct cardserver_data *getcsbyid(uint32_t id)
{
	if (!id) return NULL;
	struct cardserver_data *cs = cfg.cardserver;
	while (cs) {
		if (cs->id==id) return cs;
		cs = cs->next;
	}
	return NULL;
}


struct cardserver_data *getcsbyport(int port)
{
	struct cardserver_data *cs = cfg.cardserver;
	while (cs) {
		if (cs->newcamd.port==port) return cs;
		cs = cs->next;
	}
	return NULL;
}


struct cardserver_data *getcsbycaprovid(uint16_t caid, uint32_t provid)
{
	int j;
	struct cardserver_data *cs = cfg.cardserver;
	while (cs) {
		if (caid==cs->card.caid) {
			for (j=0; j<cs->card.nbprov;j++) if (provid==cs->card.prov[j].id) break;
			if (j<cs->card.nbprov) break;
		}
		cs = cs->next;
	}
	return cs;
}


void sid_newecm(ECM_DATA *ecm)
{
	if (!ecm) return;
	struct cardserver_data *cs = ecm->cs;
	if (!cs) return;
 
	if (cs->sidlist.data) {
		int i;
		struct sid_chid_ecmlen_data *sids = cs->sidlist.data;
		for(i=0;i<MAX_SIDS;i++,sids++) {
			if (!sids->sid) break;
			if ( (sids->sid==ecm->sid)&&(!sids->chid||(sids->chid==ecm->chid))&&(!sids->ecmlen||(sids->ecmlen==ecm->ecmlen)) ) {
				if (ecm->dcwstatus==STAT_DCW_SUCCESS) sids->ecmok++;
				sids->ecmnb++;
				break;
			}
		}
	}
}



struct sid_chid_ecmlen_data *sid_binarysearch( struct sid_chid_ecmlen_data *sids, int max, uint16_t sid )
{
	// Returns index of sid in sids, or -1 if not found
	int xl = 0;
	int xh = max - 1;
	//
	int yl = sids[xl].sid;
	int yh = sids[xh].sid;
	//
	int xm;
	while (yl <= sid && yh >= sid) {
		xm = (xl + xh)/2;
		int ym = sids[xm].sid;
		if (ym<sid) yl = sids[xl=xm+1].sid;
		else if (ym>sid) yh = sids[xh=xm-1].sid;
		else return &sids[xm];
	}
	if (sids[xl].sid == sid) return &sids[xl];
	return NULL; // Not found
}

int accept_sid(struct cardserver_data *cs, uint32_t provid, uint16_t sid, uint16_t chid, uint16_t ecmlen, uint8_t *cw1cycle )
{
	*cw1cycle = 0;
	if (cs->sidlist.data) {
		int accepted = 0;
		struct sid_chid_ecmlen_data *s = sid_binarysearch ( cs->sidlist.data, cs->sidlist.total, sid );
		if (s) {
			if ( (!s->chid||(s->chid==chid)) && (!s->ecmlen||(s->ecmlen==ecmlen)) ) {
				accepted = 1;
				*cw1cycle = s->cw1cycle;
			}
		}
		if (cs->sidlist.deny) return !accepted; else return accepted;
	}
	else {
		int i;
		for (i=0; i<cs->card.nbprov; i++) {
			if (provid==cs->card.prov[i].id) {
				if (cs->card.prov[i].sidlist.data) {
					int accepted = 0;
					struct sid_chid_ecmlen_data *s = sid_binarysearch ( cs->card.prov[i].sidlist.data, cs->card.prov[i].sidlist.total, sid );
					if (s) {
						if ( (!s->chid||(s->chid==chid)) && (!s->ecmlen||(s->ecmlen==ecmlen)) ) {
							accepted = 1;
							*cw1cycle = s->cw1cycle;
						}
					}
					if (cs->sidlist.deny) return !accepted; else return accepted;
 				}
				break;
			}
		}
	}

	if ( !sid && !cs->option.faccept0sid ) return 0;
	return 1;
}

///////////////////////////////////////////////////////////////////////////////
// Return
//  0: not accepted
//  1: accepted
int accept_sid0(struct cardserver_data *cs, uint16_t sid, uint16_t chid, uint16_t ecmlen, uint8_t *cw1cycle )
{
	*cw1cycle = 0;
	if (cs->sidlist.data) {
		int i;
		int accepted = 0;
		struct sid_chid_ecmlen_data *sids = cs->sidlist.data;
		for(i=0;i<MAX_SIDS;i++,sids++) {
			if (!sids->sid) {
				if (!sids->chid) {
					if (!sids->ecmlen) break; // end of sids
					else if (sids->ecmlen==ecmlen) {
						accepted = 1;
						*cw1cycle = sids->cw1cycle;
						break;
					}
				}
				else if ( (sids->chid==chid)&&(!sids->ecmlen||(sids->ecmlen==ecmlen)) ) {
					accepted = 1;
					*cw1cycle = sids->cw1cycle;
					break;
				}
			}
			else if ( (sids->sid==sid)&&(!sids->chid||(sids->chid==chid))&&(!sids->ecmlen||(sids->ecmlen==ecmlen)) ) {
				accepted = 1;
				*cw1cycle = sids->cw1cycle;
				break;
			}
		}
		if (cs->sidlist.deny) return !accepted; else return accepted;
	}
	else if ( !sid && !cs->option.faccept0sid ) return 0;
	return 1;
}

int accept_prov(struct cardserver_data *cs, uint32_t prov)
{
	int i;
	// Check for provid
	for (i=0; i<cs->card.nbprov;i++) if (prov==cs->card.prov[i].id) return 1; // found
	// not found, test provid==0
	if ( !prov && cs->option.faccept0provider ) return 1;
	return 0;
}

int accept_caid(struct cardserver_data *cs, uint16_t caid)
{
	// Check for caid, accept caid=0
	if (caid==cs->card.caid) return 1;
	if ( !caid && cs->option.faccept0caid ) return 1;
	return 0;
}

int accept_ecmlen(int ecmlen)
{
	if ( (ecmlen<20)||(ecmlen>MAX_ECM_SIZE) ) return 0;
	return 1;
}

int viaccess_checkECM( uint8_t *ecmdata, int ecmlen)
{
	int nanoea10 = 0;
	int nanof008 = 0;

	unsigned char *data = ecmdata+4;

	while ( data < (ecmdata+ecmlen) )
	{
		uint8_t nano = *data;
		int nanolen = *(data+1);
		if ( (nano==0xea)&&(nanolen==0x10) ) nanoea10 = 1;
		if ( (nano==0xf0)&&(nanolen==0x08) ) nanof008 = 1;
		////printf(" NANO: %02x LEN: %d\n", nano, nanolen);
		data += 2 + nanolen;
		if ( data > (ecmlen+ecmdata) ) return 0;
	}
	if (nanoea10 && nanof008) return 1;
	return 0;
}

int cs_check_ecmlen(struct cardserver_data *cs, int len)
{
	if (!cs->ecmlen[0]) return 1;
	int count;
	for (count=0; count<30; count++) {
		if (cs->ecmlen[count]==len) return 1;
		if (!cs->ecmlen[count]) break;
	}
	return 0;
}

char *cs_accept_ecm(struct cardserver_data *cs, uint16_t caid, uint32_t provid, uint16_t sid, uint16_t chid, uint16_t ecmlen, uint8_t *ecmdata, uint8_t *cw1cycle )
{
	//
	if (cs->option.checkecmlength) {
		int len = ((ecmdata[1]&0x0F)<<8) | ecmdata[2];
		if ( (len+3)!=ecmlen ) return("ECM length corrupted");
	}
	// ecmtag
	if ( (ecmdata[0]&0xFE)!=0x80 ) return("Invalid ECM tag");
	// check for ecm length
	if (!accept_ecmlen(ecmlen)) return("Invalid ECM length");
	// Check for caid
	if ( !accept_caid(cs,caid) ) return("Wrong caid");
	// Check for provid
	if ( !accept_prov(cs,provid) ) return("Wrong provider");
	// Check for sid
	if ( !accept_sid(cs, provid, sid, chid, ecmlen, cw1cycle) ) return("Channel denied");
	// check for length
	if ( !cs_check_ecmlen(cs, ecmlen) ) return("Wrong ecm length");
	// check for viaccess
	if (cs->option.checkecm) {
		if (caid==0x0500) if ( !viaccess_checkECM( ecmdata, ecmlen ) ) return("Invalid viaccess ecm");
	}
	return NULL;
}



///////////////////////////////////////////////////////////////////////////////
void ecm_setdcw( ECM_DATA *ecm, uint8_t dcw[16], int srctype, int srcid);
int pipe_send_cacheex_push_cache(struct cache_data *pcache, uint8_t *cw, uint8_t *nodeid);

#include "clustredcache.c"

#include "cli-common.c"
#include "cli-newcamd.c"
#ifdef CCCAM_CLI
#include "cli-cccam.c"
#endif


#if defined(CAMD35_SRV) || defined(CAMD35_CLI) || defined(CS378X_SRV) || defined(CS378X_CLI)
#include "crc32.c"
#include "msg-camd35.c"
#endif

#ifdef CAMD35_CLI
#include "cli-camd35.c"
#endif
#ifdef CS378X_CLI
#include "cli-cs378x.c"
#endif


struct connect_cli_data {
	void *server;
	int sock;
	uint32_t ip;
};

#include "srv-newcamd.c"
#ifdef MGCAMD_SRV
#include "srv-mgcamd.c"
#endif

#ifdef CCCAM_SRV
#include "srv-cccam.c"
#endif

#ifdef FREECCCAM_SRV
#include "srv-freecccam.c"
#endif

#ifdef RADEGAST_CLI
#include "cli-radegast.c"
#endif

#ifdef RADEGAST_SRV
#include "srv-radegast.c"
#endif

#ifdef CAMD35_SRV
#include "srv-camd35.c"
#endif

#ifdef CS378X_SRV
#include "srv-cs378x.c"
#endif

#ifdef CACHEEX
#include "cacheex.c"
#endif

#ifdef CCCAM_SRV
#include "srv-common.c"
#endif

#include "th-srv.c"  // Servers Connnection
#include "th-dns.c"  // Dns Resolving
#include "th-ecm.c"  // Check/send ecm request to servers & Check/send dcw to clients
#ifndef WIN32 
#include "th-cfg.c"  // Reread Config
#endif
#ifdef EXPIREDATE
#include "th-date.c"
#endif

///////////////////////////////////////////////////////////////////////////////


char *src2string(int srctype, int srcid, char *ret)
{
	static char ss1[] = "server";
	static char ss2[] = "cache peer";
	static char ss3[] = "newcamd client";

	if (srctype==DCW_SOURCE_SERVER) {
		struct server_data *srv = getsrvbyid(srcid&0xFFFF);
		if (srv)
			sprintf( ret,"server (%s:%d)", srv->host->name, srv->port);
		else
			sprintf( ret,"Unknow server (id=%d)", srcid);
		return ss1;
	}
	else if (srctype==DCW_SOURCE_CACHE) {
		if (srcid&PEER_CSP) {
			struct cachepeer_data *peer = getpeerbyid(srcid&0xFFFF);
			if (peer)
				sprintf( ret,"cache peer (%s:%d)", peer->host->name, peer->port);
			else
				sprintf( ret,"Unknown cache peer (id=%d)", srcid);
			return ss2;
		}
#ifdef CACHEEX
		else if (srcid&PEER_CCCAM_CLIENT) {
			struct cc_client_data *cli = getcecccamclientbyid(srcid&0xFFFF);
			if (cli)
				sprintf( ret,"CacheEx CCcam client '%s'", cli->user);
			else
				sprintf( ret,"Unknown CacheEx CCcam client (id=%d)", srcid);
			return "CacheEx CCcam client";
		}

#ifdef CAMD35_SRV
		else if (srcid&PEER_CAMD35_CLIENT) {
			struct camd35_client_data *cli = getcamd35clientbyid(srcid&0xFFFF);
			if (cli)
				sprintf( ret,"CacheEx Camd35 client '%s'", cli->user);
			else
				sprintf( ret,"Unknown CacheEx Camd35 client (id=%d)", srcid);
			return "CacheEx Camd35 client";
		}
#endif

#ifdef CS378X_SRV
		else if (srcid&PEER_CS378X_CLIENT) {
			struct camd35_client_data *cli = getcs378xclientbyid(srcid&0xFFFF);
			if (cli)
				sprintf( ret,"CacheEx cs378x client '%s'", cli->user);
			else
				sprintf( ret,"Unknown CacheEx cs378x client (id=%d)", srcid);
			return "CacheEx cs378x client";
		}
#endif

		else if (srcid&PEER_CACHEEX_SERVER) {
			struct server_data *srv = getcesrvbyid(srcid&0xFFFF);
			if (srv)
				sprintf( ret,"CacheEx server (%s:%d)", srv->host->name, srv->port);
			else
				sprintf( ret,"Unknow CacheEx server (id=%d)", srcid);
			return "CacheEx Server";
		}
#endif
	}
#ifdef SRV_CSCACHE
	else if (srctype==DCW_SOURCE_CSCLIENT) {
		// srcid =  (csid<<16)|cliid;
		struct cardserver_data *cs = getcsbyid( srcid>>16 );
		if (cs) {
			struct cs_client_data *cli = getnewcamdclientbyid( srcid&0xffff );
			if (cli) {
				sprintf( ret,"newcamd client '%s'", cli->user);
				return ss3;
			}
		}
		sprintf( ret,"Unknown newcamd client (id=%x)", srcid);
		return ss3;
	}
	else if (srctype==DCW_SOURCE_MGCLIENT) {
		// srcid =  (csid<<16)|cliid;
		struct mg_client_data *cli = getmgcamdclientbyid( srcid );
		if (cli)
			sprintf( ret,"mgcamd client '%s'", cli->user);
		else
			sprintf( ret,"Unknown mgcamd client (id=%d)", srcid);
		return ss3;
	}
#endif
	else if (srctype==DCW_SOURCE_CCCLIENT) {
		struct cc_client_data *cli = getcccamclientbyid(srcid);
		if (cli)
			sprintf( ret,"CCcam client '%s'", cli->user);
		else
			sprintf( ret,"Unknown CCcam client (id=%d)", srcid);
		return "CCcam client";
	}

	else {
		sprintf( ret,"Unknown Source (%d/%d)", srctype, srcid);
	}
	return NULL;
}

///////////////////////////////////////////////////////////////////////////////

pthread_t cli_tid;

int checkthread;

unsigned int seed2;

uint8_t fastrnd2()
{
  unsigned int offset = 12923+(GetTickCount()&0xff);
  unsigned int multiplier = 4079+(GetTickCount()&0xff);
  seed2 = seed2 * multiplier + offset;
  return (uint8_t)(seed2 % 0xFF);
}


void mainprocess()
{
#ifndef WIN32
	gettimeofday( &startime, NULL );
	//if (startime.tv_sec>1380237152) exit(0);
	//printf(" %ld\n", startime.tv_sec + (24*3600*5) ); exit(0);
#endif
// INIT
	pthread_mutex_init(&prg.lock, NULL);
	pthread_mutex_init(&prg.lockecm, NULL);

	pthread_mutex_init(&prg.lockcli, NULL);
	pthread_mutex_init(&prg.locksrv, NULL);

#ifdef CCCAM_SRV
	pthread_mutex_init(&prg.locksrvcc, NULL); // CC Client connection
	pthread_mutex_init(&prg.lockcccli, NULL);
#endif
#ifdef FREECCCAM_SRV
	pthread_mutex_init(&prg.locksrvfreecc, NULL); // CC Client connection
	pthread_mutex_init(&prg.lockfreecccli, NULL);
#endif

#ifdef MGCAMD_SRV
	pthread_mutex_init(&prg.locksrvmg, NULL); // Client connection
	pthread_mutex_init(&prg.lockclimg, NULL);
#endif

#ifdef RADEGAST_SRV
	pthread_mutex_init(&prg.lockrdgdsrv, NULL); // Client connection
	pthread_mutex_init(&prg.lockrdgdcli, NULL);
#endif

	// Main Loops(THREADS)
	pthread_mutex_init(&prg.lockdnsth, NULL); // DNS lookup Thread

	pthread_mutex_init(&prg.locksrvth, NULL);	// Connection to cardservers
	pthread_mutex_init(&prg.lockmain, NULL); // Messages Recv

	pthread_mutex_init(&prg.locksrvcs, NULL); // CS Client connection
	pthread_mutex_init(&prg.lockhttp, NULL); // HTTP Server

	pthread_mutex_init(&prg.lockdns, NULL);

	pthread_mutex_init(&prg.lockdcw, NULL);

	pthread_mutex_init(&prg.lockcache, 0);


	pthread_mutex_init(&prg.lockthreaddate, NULL);

#ifdef CACHEEX
	pthread_mutex_init(&prg.lockcacheex, NULL);
#endif
	gettimeofday( &prg.exectime, NULL );

	memset(&trace, 0, sizeof(struct trace_data) );

	/* Create the pipe. */

	if ( pipe(PACKED_MPTR(&prg, int, pipe.cache)) < 0 ) { perror("pipe()"); exit(1); }
	SetSoketNonBlocking(prg.pipe.cache[0]);
	SetSoketNonBlocking(prg.pipe.cache[1]);

	if ( pipe(PACKED_MPTR(&prg, int, pipe.ecm)) < 0 ) { perror("pipe()"); exit(1); }
	SetSoketNonBlocking(prg.pipe.ecm[0]);
	SetSoketNonBlocking(prg.pipe.ecm[1]);

#ifdef CACHEEX
	if ( pipe(PACKED_MPTR(&prg, int, pipe.cacheex)) < 0 ) { perror("pipe()"); exit(1); }
	SetSoketNonBlocking(prg.pipe.cacheex[0]);
	SetSoketNonBlocking(prg.pipe.cacheex[1]);
#endif

	if ( pipe(PACKED_MPTR(&prg, int, pipe.cs378x)) < 0 ) { perror("pipe()"); exit(1); }
	SetSoketNonBlocking(prg.pipe.cs378x[0]);
	SetSoketNonBlocking(prg.pipe.cs378x[1]);
	if ( pipe(PACKED_MPTR(&prg, int, pipe.cs378x_cex)) < 0 ) { perror("pipe()"); exit(1); }
	SetSoketNonBlocking(prg.pipe.cs378x_cex[0]);
	SetSoketNonBlocking(prg.pipe.cs378x_cex[1]);

	if ( pipe(PACKED_MPTR(&prg, int, pipe.cccam)) < 0 ) { perror("pipe()"); exit(1); }
	SetSoketNonBlocking(prg.pipe.cccam[0]);
	SetSoketNonBlocking(prg.pipe.cccam[1]);

	if ( pipe(PACKED_MPTR(&prg, int, pipe.mgcamd)) < 0 ) { perror("pipe()"); exit(1); }
	SetSoketNonBlocking(prg.pipe.mgcamd[0]);
	SetSoketNonBlocking(prg.pipe.mgcamd[1]);

	if ( pipe(PACKED_MPTR(&prg, int, pipe.newcamd)) < 0 ) { perror("pipe()"); exit(1); }
	SetSoketNonBlocking(prg.pipe.newcamd[0]);
	SetSoketNonBlocking(prg.pipe.newcamd[1]);

	if ( pipe(frcc_pipe) < 0 ) { perror("pipe()"); exit(1); }
	SetSoketNonBlocking(frcc_pipe[0]);
	SetSoketNonBlocking(frcc_pipe[1]);

	if ( pipe(dcwpipe) < 0 ) { perror("pipe()"); exit(1); }
	SetSoketNonBlocking(dcwpipe[0]);
	SetSoketNonBlocking(dcwpipe[1]);

	// EPOLL
#ifdef EPOLL_CACHE
	prg.epoll.cache = epoll_create( MAX_EPOLL_EVENTS );
	epoll_add(prg.epoll.cache, prg.pipe.cache[0], NULL);
#endif

	//
	srand (time(NULL));

#ifdef CCCAM 
// NODE ID: 8675e141 217e6912
	cccam_nodeid[0] = 0x11;
	cccam_nodeid[1] = 0x22;
	cccam_nodeid[2] = 0x33;
	cccam_nodeid[3] = 0x44;
	cccam_nodeid[4] = 0xff & fastrnd2();
	cccam_nodeid[5] = 0xff & fastrnd2();
	cccam_nodeid[6] = 0xff & fastrnd2();
	cccam_nodeid[7] = 0xff & fastrnd2();
#endif









#ifndef WIN32
	start_thread_config();
	wait_config_ready();	/* TASK 4.1 — was a 100 ms hope */

	/* TASK R4 (D58): the reputation ledger is the file's truth. Loaded once
	 * the config (which arms the ladder) is fully read; from here on every
	 * escalation rewrites the file atomically. */
	if (peerrep_on) {
		int pr_loaded = peerrep_load();
		debugf(0, " [PEER REP] %d record(s) loaded from %s\n", pr_loaded, peerrep_file());
	}
#endif

	/*
	 * Refuse to start without a profile instead of crashing later.
	 *
	 * With no `[ <profile> ]` section in the config, cfg.cardserver stays
	 * NULL and several startup paths dereference it, which showed up as a
	 * silent SIGSEGV (exit 139) with no diagnostic at all -- the crash
	 * handler is compiled out unless SIG_HANDLER is defined, and even then
	 * it needs a writable debug_file. An operator with a typo'd or empty
	 * config deserves a message, not a segfault.
	 */
	if ( !cfg.cardserver ) {
		fprintf( stderr, "\nError: no profile configured.\n"
			"  Add at least one profile to %s, for example:\n"
		"\n"
		"    [ myprofile ]\n"
		"    CAID: 1884\n"
		"    PORT: 15001\n"
		"    USER: user pass\n"
		"\n", config_file );
		exit(1);
	}

	init_ecmdata();

// THREADS - detached
	start_thread_dns();
	start_thread_srv();
	start_thread_recv_msg();
#ifdef EXPIREDATE
	start_thread_date();
#endif

#ifdef TELNET
	start_thread_telnet();
#endif

	start_thread_cache();

#ifdef CACHEEX
	start_thread_cacheex();
#endif

	sleep(3);

	pthread_t cli_tid;
#ifdef RADEGAST_SRV
	create_thread(&cli_tid, (threadfn)rdgd_connect_cli_thread, NULL); // Lock server
#endif

	start_thread_newcamd();

#ifdef MGCAMD_SRV
	start_thread_mgcamd();
#endif

#ifdef CCCAM_SRV
	start_thread_cccam();
#endif

#ifdef FREECCCAM_SRV
	start_thread_freecccam();
#endif

#ifdef CS378X_SRV
	start_thread_cs378x();
#endif

#ifdef CAMD35_SRV
	start_thread_camd35();
#endif

#ifdef MONOTHREAD_ACCEPT
	create_thread(&cli_tid, (threadfn)connect_cli_thread, NULL); // Lock server
#endif

	start_thread_http();

	while (!prg.restart) {
		sleep(5);
	}

}


#ifdef SIG_HANDLER

#include <execinfo.h>
#include <ucontext.h>

static void x64_sighandlerPrint(int signo, int code, ucontext_t *context, void *bt [], int bt_size)
{
	time_t ttime = time (NULL);

	FILE *fd;
	fd = fopen(debug_file, "at");
	if (!fd) {
		printf(" Error opening file\n");
		return;
	}
	fprintf(fd, "\n## %s", ctime (&ttime));
	fprintf(fd, "PID=%d\n", getpid ());
	fprintf(fd, "signo=%d/%s\n", signo, strsignal (signo));
	fprintf(fd, "code=%d (not always applicable)\n", code);
	fprintf(fd, "\nContext: 0x%08lx\n", (unsigned long) context);

	fprintf(fd,
		"R8= 0x%08lx\n"
		"R9= 0x%08lx\n"
		"R10= 0x%08lx\n"
		"R11= 0x%08lx\n"
		"R12= 0x%08lx\n"
		"R13= 0x%08lx\n"
		"R14= 0x%08lx\n"
		"R15= 0x%08lx\n"
		"RDI= 0x%08lx\n"
		"RSI= 0x%08lx\n"
		"RBP= 0x%08lx\n"
		"RBX= 0x%08lx\n"
		"RDX= 0x%08lx\n"
		"RAX= 0x%08lx\n"
		"RCX= 0x%08lx\n"
		"RSP= 0x%08lx\n"
		"RIP= 0x%08lx\n"
		"EFL= 0x%08lx\n"
		"CSGSFS= 0x%08lx\n"
		"ERR= 0x%08lx\n"
		"TRAPNO= 0x%08lx\n"
		"OLDMASK= 0x%08lx\n"
		"CR2= 0x%08lx\n",
		(uint64_t)context->uc_mcontext.gregs[REG_R8],
		(uint64_t)context->uc_mcontext.gregs[REG_R9],
		(uint64_t)context->uc_mcontext.gregs[REG_R10],
		(uint64_t)context->uc_mcontext.gregs[REG_R11],
		(uint64_t)context->uc_mcontext.gregs[REG_R12],
		(uint64_t)context->uc_mcontext.gregs[REG_R13],
		(uint64_t)context->uc_mcontext.gregs[REG_R14],
		(uint64_t)context->uc_mcontext.gregs[REG_R15],
		(uint64_t)context->uc_mcontext.gregs[REG_RDI],
		(uint64_t)context->uc_mcontext.gregs[REG_RSI],
		(uint64_t)context->uc_mcontext.gregs[REG_RBP],
		(uint64_t)context->uc_mcontext.gregs[REG_RBX],
		(uint64_t)context->uc_mcontext.gregs[REG_RDX],
		(uint64_t)context->uc_mcontext.gregs[REG_RAX],
		(uint64_t)context->uc_mcontext.gregs[REG_RCX],
		(uint64_t)context->uc_mcontext.gregs[REG_RSP],
		(uint64_t)context->uc_mcontext.gregs[REG_RIP],
		(uint64_t)context->uc_mcontext.gregs[REG_EFL],
		(uint64_t)context->uc_mcontext.gregs[REG_CSGSFS],
		(uint64_t)context->uc_mcontext.gregs[REG_ERR],
		(uint64_t)context->uc_mcontext.gregs[REG_TRAPNO],
		(uint64_t)context->uc_mcontext.gregs[REG_OLDMASK],
		(uint64_t)context->uc_mcontext.gregs[REG_CR2]
	);
	fprintf(fd, "\n%d elements in backtrace\n", bt_size);

	backtrace_symbols_fd (bt, bt_size, fileno (fd));

	fprintf(fd, "\n");
	fflush( fd );
	fclose(fd);
}

void sighandler(int signo, struct siginfo *si, void *ctx)
{
	void *bt[128];
	int bt_size;

	bt_size = backtrace (bt, sizeof(bt) );

	x64_sighandlerPrint (signo, si->si_code, (ucontext_t *) ctx, bt, bt_size);
	exit (1);
}

void install_handler (void)
{
	struct sigaction sa;
	memset(&sa, 0, sizeof(sigaction));
	sa.sa_sigaction = (void *)sighandler;
	sigemptyset (&sa.sa_mask);
	sa.sa_flags = SA_RESTART | SA_SIGINFO; //SA_ONESHOT
	sigaction(SIGSEGV, &sa, NULL);
	sigaction(SIGFPE, &sa, NULL);
	sigaction(SIGABRT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGQUIT, &sa, NULL);

	sigaction(SIGILL, &sa, NULL);
	sigaction(SIGFPE, &sa, NULL);
	sigaction(SIGUSR1, &sa, NULL);
	sigaction(SIGUSR2, &sa, NULL);
	sigaction(SIGSTOP, &sa, NULL);
	sigaction(SIGTSTP, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
}

#endif


//#include "iplock.c"

int main(int argc, char *argv[])
{
	int option_background = 0; // default
	int fork_return;
	char *args;
	int i,j;

/*
	printf(" cmp_cards = %p\n", &cmp_cards);
	printf(" cs_accept_ecm() = %p\n", &cs_accept_ecm);


	uint8_t *data_offset = (uint8_t *)&cmp_cards;
	uint32_t data_size = (uint32_t) ( ((void*)&cs_accept_ecm) - ((void*)&cmp_cards) );

	uint32_t datacrc = crc32(0L, data_offset, data_size);

	char str[101*3];
	array2hex( data_offset, str, 100);
	printf("%s\n(%d) %08x\n", str, data_size, datacrc);

	uint8_t pass[4] = { 1, 2, 3, 4 };
	message_decrypt( data_offset, data_size, pass );
	datacrc = crc32(0L, data_offset, data_size);
	array2hex( data_offset, str, 100);
	printf("%s\n(%d) %08x\n", str, data_size, datacrc);

	return 0;
*/

	char pg[] = "Multi CardServer r"REVISION_STR" - by ";
	char evil[] = "evileyes";
	char email[] = " (http://www.infosat.org)\n";


	if ( (evil[0]!='e')||(evil[1]!='v')||(evil[2]!='i')||(evil[3]!='l')||(evil[4]!='e')||(evil[5]!='y')||(evil[6]!='e')||(evil[7]!='s') ) exit(0);
	printf("%s", pg);

#ifdef SIG_HANDLER
	install_handler();
#endif

	if ( (evil[0]!='e')||(evil[1]!='v')||(evil[2]!='i')||(evil[3]!='l')||(evil[4]!='e')||(evil[5]!='y')||(evil[6]!='e')||(evil[7]!='s') ) exit(0);
	printf("%s", evil );

	flag_debugscr = 0;
	flag_debugfile = 0;
#ifdef DEBUG_NETWORK
	flag_debugnet = 0;
#endif

	printf("%s", email );
	if (IP_ADRESS) printf("*Server IP: %s\n", ip2string(IP_ADRESS)); 
	// Extract filename
	char *p = argv[0];
	char *slash = p;
	char *dot = NULL;
	while (*p) {
		if (*p=='/') slash = p+1;
		else if (*p=='.') dot = p;
		p++;
	}
	char path[255];
	if (dot>slash) memcpy( path, slash, dot-slash); else strcpy(path, slash);

#ifdef WIN32
	// Set Config name
	sprintf( config_file, "%s.cfg", path);
//	sprintf( sid_file, "/var/etc/%s.sid", path);
//	sprintf( card_file, "/var/etc/%s.card", path);
	sprintf( debug_file, "%s.log", path);
	sprintf( sms_file, "%s.sms", path);
#else
	// Set Config name
	sprintf( config_file, "/var/etc/%s.cfg", path);
//	sprintf( sid_file, "/var/etc/%s.sid", path);
//	sprintf( card_file, "/var/etc/%s.card", path);
	sprintf( debug_file, "/var/tmp/%s.log", path);
	sprintf( sms_file, "/var/tmp/%s.sms", path);
	sprintf( ecm_file, "/var/tmp/%s.ecm", path);
#endif

	// Parse Options
	for (i=1;i<argc;i++) {
		args = *(argv+i);
		if (args[0]=='-') {
			if (args[1]=='h') {
				printf("USAGE\n\tmultics [-b] [-v] [-f] [-n] [-C <configfile>]\n\
OPTIONS\n\
\t-b               run in background\n\
\t-C <configfile>  use <configfile> instead of default config file (/var/etc/multics.cfg)\n\
\t-f               write to log file (/var/tmp/multics.log)\n\
\t-n               print network packets\n\
\t-v               print on screen\n\
\t-h               this help message\n");
				return 0;
			}
			else if (args[1]=='C') {
				i++;
				if (i<argc) {
					args = *(argv+i);
					strcpy( config_file, args );
				}
			}
			else {
				for(j=1; j<strlen(args); j++) {
					if (args[j]=='b') option_background = 1;
					else if (args[j]=='v') flag_debugscr = 1;
#ifdef DEBUG_NETWORK
					else if (args[j]=='n') flag_debugnet = 1;
#endif
					else if (args[j]=='f') flag_debugfile = 1;
				}
			}
		}
	}

	if (option_background==1) {
		fork_return = fork();
		if( fork_return < 0) {
			debugf(0," unable to create child process, exiting.\n");
			exit(-1);
		}
		if (fork_return>0) {
			//debugf(" main process, exiting.\n");
			exit(0);
		}
		//else mainprocess();
	}

	prg.pid_main = getpid();

	prg.restart = 0;

	// check for load average
	while (1)
	{
		FILE *fp = fopen ("/proc/loadavg", "r");
		if (fp) {
			float avg;
			if ( fscanf(fp, "%f", &avg)>0 )
				if (avg<7) break;
			fclose(fp);
		} else break;
		sleep(3);
	}

	mainprocess();

	if (prg.restart==1) { // restart()
		if ( (evil[0]!='e')||(evil[1]!='v')||(evil[2]!='i')||(evil[3]!='l')||(evil[4]!='e')||(evil[5]!='y')||(evil[6]!='e')||(evil[7]!='s') ) exit(0);
		debugf(0," Restarting...\n");
		/*
		 * TASK 3.18 / D47. Do not join or cancel threads here.
		 * create_thread() detaches every one of them. The exit below is
		 * what ends them, after the new process has been started. A
		 * stopper can hang this restart. See D47.
		 */
		int fork_return;
		done_config(&cfg);
		fork_return = vfork();
		if ( fork_return < 0) {
			printf("unable to create child process, exiting.\n");
		}
		else if (fork_return==0) {
			fork_return = vfork();
			if (fork_return < 0) {
				printf("unable to create child process, exiting.\n");
			}
			else if (fork_return==0) {
				execvp( argv[0], argv );
				perror("execvp");
			}
			exit(0);
		}
		debugf(0," Stopped.\n");
		exit(0);
	}
	return 0;
}