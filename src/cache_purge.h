/*
 * cache_purge.h — TASK 1.5: mark cached CWs by origin (MultiCS r82a hardening)
 *
 * THE PROBLEM.
 * A poisoned cache entry is served instantly, looks statistically ideal, and is
 * never cross-checked. Nothing in r82a can remove it. Once a bad CW is in the
 * cache it is served to every client that asks for that channel until the entry
 * ages out, and every one of those clients gets a black screen.
 *
 * WHAT THIS DOES — and what it deliberately does NOT do.
 * It *marks* cached CWs; it never unlinks or frees anything.
 *
 * Marking is the right tool here for a reason that is specific to this tree:
 * `struct cw_cache_data.status` is a bitmask (DCW_ERROR 0x01, DCW_CYCLE 0x02,
 * DCW_SENT 0x10 — clustredcache.c:286-290) and the serving path already refuses a
 * CW whose status carries DCW_ERROR (clustredcache.c:1208), with the insertion
 * path short-circuiting on it too (:1441). So "do not serve this CW" is a bit
 * that already exists and is already honoured everywhere. Setting it needs no
 * struct change, no list mutation, no new lock — which is why D6 (never change
 * the layout of struct cache_data / cw_cache_data) holds without a single
 * exception here.
 *
 * Unlinking was rejected, and the reason is worth keeping: the bucket list is a
 * CIRCULAR MRU list (cache_new(), clustredcache.c:394-460; the second entry for a
 * bucket cross-links both ways at :417-423). A removal has to fix prev and next
 * or the ring breaks, and a traversal written as `for (p = tab[i]; p; p = p->next)`
 * over it never terminates. Marking buys the same client-visible result — the CW
 * stops being served — for none of that risk.
 *
 * GR3 (never hard-act on a soft event) is enforced by the CALLER's gate, and this
 * module makes it hard to get wrong: there is no API that marks on a score. The
 * only entry point is cache_purge_mark(), and the only thing that calls it is a
 * definitive proof.
 *
 * GR4: keyed on (source_type, source_id, caid, provid, sid) — the same key as
 * trust.h, never per-server.
 *
 * GR9: fixed 64-entry table in .bss. No allocation on the ECM path, no I/O, no
 * unbounded growth.
 *
 * REVERSIBILITY (GR8), and the one honest limit.
 * cache_purge_unmark() lifts a mark, so a source that recovers is not punished
 * forever. Separately, the mark is per-CW, not per-entry: cache_setdcw() zeroes
 * status only when it creates an entry for a *different* CW (:1433), while a
 * re-report of the *same* CW just bumps nbpeers (:1439). That is exactly what is
 * wanted — a good peer reporting the same poisoned CW does not launder it — but
 * it also means a source that pushes a *different* CW gets a fresh unmarked
 * entry. A different CW is a different claim and needs its own evidence, so this
 * is accepted rather than patched.
 *
 * THE LIMIT ON WHAT PURGE CAN PROVE.
 * Origin lives on cwdata->peerid, and the field is commented "fisrt peerid"
 * (sic): it records whichever peer reported that CW first, with nbpeers counting
 * the rest. So a purge by origin can only reach the CWs a source supplied first.
 * This is the strongest argument for gating on a proof rather than a score, and
 * it is recorded here so the limit is not mistaken for coverage that exists.
 */

#ifndef MCS_CACHE_PURGE_H
#define MCS_CACHE_PURGE_H

#include <string.h>
#include <stdint.h>

#define CACHE_PURGE_SLOTS   64

/*
 * A source we have proof against, at GR4 granularity.
 * `hits` is not a counter of CWs purged; it is the number of distinct
 * (caid, provid, sid) tuples this source has been marked for, so a log line can
 * say how wide the damage was. It saturates rather than wrapping.
 */
struct purge_entry {
	uint32_t srcid;   /* includes PEER_* origin flags — see ORIGIN.md */
	uint16_t caid;
	uint32_t provid;
	uint16_t sid;
	uint8_t  srctype;
	uint8_t  hits;
	uint8_t  inuse;
	uint32_t ticks;   /* last time this source was marked */
};

struct purge_table {
	struct purge_entry e[CACHE_PURGE_SLOTS];
	uint32_t marked;  /* total marks issued, for the stats line */
	/*
	 * Marks refused. Always 0 with the current eviction policy, because
	 * evicting the longest-unseen entry always makes room. Kept for a future
	 * variant that refuses instead of evicting; a non-zero value here would
	 * mean that variant is in use.
	 */
	uint32_t skipped;
	/*
	 * TASK 1.9 -- SERVICE-BLACKLIST-TIME and GR8.
	 *
	 * `released` counts marks lifted because the source proved itself again
	 * (cache_purge_unmark from the recovery path). `expired` counts marks lifted
	 * because they outlived the configured window. Both are lifts, and keeping
	 * them apart is the difference between "my peer recovered" and "my peer went
	 * quiet and the timer ran out" -- two situations an operator reacts to very
	 * differently.
	 *
	 * Both are 0 unless the option is set or a recovery happens, so a default
	 * install shows nothing here.
	 */
	uint32_t released;
	uint32_t expired;
};

/* The __attribute__((unused)) markers below are not decoration: these are
 * header-local statics that form the module's API and are exercised by the
 * unit tests in make-x64/, but main.c does not call every one of them, and
 * -Wunused-function is not silenced by -Wno-unused-variable. */
__attribute__((unused))
static void cache_purge_init(struct purge_table *t)
{
	if (!t) return;
	memset(t, 0, sizeof(*t));
}

/*
 * Should a CW supplied by this source, for this channel, be withheld?
 *
 * Returns 1 only when there is a live mark for exactly this
 * (srctype, srcid, caid, provid, sid). Never widens: a mark on SID 100 does not
 * affect SID 101, and a mark on one CAID does not affect another. That is GR4,
 * and widening it here would be how a good channel gets killed by a bad one.
 *
 * Returns 0 for an unknown source. "No record" is not "bad" — the same rule
 * trust.h follows by returning -1.
 */
static int cache_purge_should_withhold_at(const struct purge_table *t,
                                          uint8_t srctype, uint32_t srcid,
                                          uint16_t caid, uint16_t sid, uint32_t provid,
                                          uint32_t ticks_now, uint32_t max_age_ms)
{
	int i;
	if (!t) return 0;
	for (i = 0; i < CACHE_PURGE_SLOTS; i++) {
		const struct purge_entry *e = &t->e[i];
		if (!(e->inuse && e->srctype == srctype && e->srcid == srcid &&
		      e->caid == caid && e->sid == sid && e->provid == provid))
			continue;
		/*
		 * TASK 1.9 -- SERVICE-BLACKLIST-TIME.
		 *
		 * max_age_ms == 0 means "never expires", which is the default and the
		 * behaviour TASK 1.5 shipped: a mark stands until the source recovers or
		 * the table evicts it. A non-zero age is the operator bounding how long
		 * a definitive proof can keep a source quiet -- the brief's requirement
		 * that a hard action be temporary as well as reversible (GR8).
		 *
		 * Unsigned subtraction, so the comparison survives the 32-bit tick
		 * wrapping at about 49 days without special-casing.
		 */
		if (max_age_ms && (uint32_t)(ticks_now - e->ticks) >= max_age_ms)
			return 0;
		return 1;
	}
	return 0;
}

/*
 * The default: no age limit. Every existing caller and assertion goes through
 * here, so adding the option changed nothing about what they observe.
 */
static int cache_purge_should_withhold(const struct purge_table *t,
                                       uint8_t srctype, uint32_t srcid,
                                       uint16_t caid, uint16_t sid, uint32_t provid)
{
	return cache_purge_should_withhold_at(t, srctype, srcid, caid, sid, provid, 0, 0);
}

/*
 * Record a definitive proof against one (source, channel) pair.
 *
 * Returns 1 if a new mark was created, 0 if it was already marked or the table
 * is full. The caller uses the return value to decide whether to log — one log
 * line per confirmed event, never one per CW.
 *
 * Eviction is longest-unseen, for the same reason trust.h does it that way:
 * evicting the most-marked source would make a busy server forget exactly the
 * source it has the most evidence against.
 */
static int cache_purge_mark(struct purge_table *t,
                            uint8_t srctype, uint32_t srcid,
                            uint16_t caid, uint16_t sid, uint32_t provid,
                            uint32_t ticks_now)
{
	int i, freepos = -1, victim = -1;
	uint32_t oldest = 0xFFFFFFFFu;

	if (!t) return 0;

	for (i = 0; i < CACHE_PURGE_SLOTS; i++) {
		struct purge_entry *e = &t->e[i];
		if (e->inuse) {
			if (e->srctype == srctype && e->srcid == srcid &&
			    e->caid == caid && e->sid == sid && e->provid == provid) {
				e->ticks = ticks_now;
				t->marked++;
				return 0; /* already marked; not a new event */
			}
			if (e->ticks < oldest) { oldest = e->ticks; victim = i; }
		}
		else if (freepos < 0) {
			freepos = i; /* first free slot; always beats an eviction */
		}
	}

	if (freepos >= 0) victim = freepos;
	if (victim < 0) { t->skipped++; return 0; }

	{
		struct purge_entry *e = &t->e[victim];
		int fresh = !e->inuse;
		e->srctype = srctype;
		e->srcid   = srcid;
		e->caid    = caid;
		e->sid     = sid;
		e->provid  = provid;
		e->hits    = fresh ? 1 : (e->hits == 0xFF ? 0xFF : (uint8_t)(e->hits + 1));
		e->inuse   = 1;
		e->ticks   = ticks_now;
		t->marked++;
	}
	return 1;
}

/*
 * Lift a mark. Returns 1 if one was actually removed.
 * Used when a source recovers, so GR8's "reversible" is a real operation and
 * not just the mark expiring on its own.
 */
__attribute__((unused))
static int cache_purge_unmark(struct purge_table *t,
                              uint8_t srctype, uint32_t srcid,
                              uint16_t caid, uint16_t sid, uint32_t provid)
{
	int i;
	if (!t) return 0;
	for (i = 0; i < CACHE_PURGE_SLOTS; i++) {
		struct purge_entry *e = &t->e[i];
		if (e->inuse && e->srctype == srctype && e->srcid == srcid &&
		    e->caid == caid && e->sid == sid && e->provid == provid) {
			e->inuse = 0;
			t->released++;   /* TASK 1.9: a lift by recovery, not by timeout */
			return 1;
		}
	}
	return 0;
}

/*
 * TASK 1.9 -- lift every mark older than max_age_ms.
 *
 * Returns how many were lifted (0 when the option is off, which is the first
 * thing tested so the default costs one comparison per call). Up to `maxout`
 * lifted identities are copied into `out` so the caller can log one line per
 * lift with the exact evidence named, which is what GR8 asks for and is why this
 * does not simply return a count.
 *
 * Called from the statistics tick, i.e. from the cache thread, and NOT from the
 * ECM path: lifting a mark is a housekeeping act and has no business on the hot
 * path (GR9). Every entry it touches is one it just expired, so the cost is
 * bounded by how many marks can be live at once, which is CACHE_PURGE_SLOTS.
 */
__attribute__((unused))
static int cache_purge_expire(struct purge_table *t, uint32_t ticks_now,
                              uint32_t max_age_ms,
                              struct purge_entry *out, int maxout)
{
	int i, n = 0;

	if (!t) return 0;
	if (!max_age_ms) return 0;

	for (i = 0; i < CACHE_PURGE_SLOTS; i++) {
		struct purge_entry *e = &t->e[i];
		if (!e->inuse) continue;
		if ((uint32_t)(ticks_now - e->ticks) < max_age_ms) continue;
		if (out && n < maxout) out[n] = *e;
		e->inuse = 0;
		t->expired++;
		n++;
	}
	return n;
}

/* Number of live marks. Bounded by CACHE_PURGE_SLOTS by construction. */
__attribute__((unused))
static int cache_purge_count(const struct purge_table *t)
{
	int i, n = 0;
	if (!t) return 0;
	for (i = 0; i < CACHE_PURGE_SLOTS; i++) if (t->e[i].inuse) n++;
	return n;
}


/*
 * Mark every cached CW this source supplied FIRST, for this channel, as bad.
 *
 * Returns the number of CWs marked, or -1 if the source has no live mark (so
 * the caller cannot accidentally sweep on a non-event).
 *
 * WHY `markbit` IS A PARAMETER AND NOT A CONSTANT HERE.
 * DCW_ERROR is `#define`d inside clustredcache.c:286, which is *after* every
 * #include in main.c. A function in this header therefore cannot see it, and
 * redefining 0x01 here would put the same magic number in two files that can
 * drift apart. The call site lives in a .c that clustredcache.c includes, where
 * DCW_ERROR is already in scope, so it passes it in.
 *
 * This function is only compilable from a .c included *by* clustredcache.c,
 * because it uses getcachetabbycaid() and struct cache_data. That is the same
 * arrangement cache_threshold.h already relies on.
 *
 * MUST BE CALLED WITH prg.lockcache HELD. The cacheex call sites already hold it.
 *
 * The __attribute__((unused)) is not decoration: -Wunused-function is NOT
 * silenced in this build (make-x64/Makefile:32-40 silences a long list, and that
 * flag is not on it), so an uncalled static in any translation unit that includes
 * this header would warn. D1 requires new code to be -Wall -Wextra clean.
 *
 * THE TRAVERSAL. The bucket list is a CIRCULAR MRU ring (see the header
 * comment), so it is walked from the head until it comes back to the head —
 * `for (p = tab[i]; p; p = p->next)` on it never terminates.
 */
/*
 * The main include guard is CLOSED here, deliberately, and this is the crux of
 * the arrangement.
 *
 * The sweep is compiled only when clustredcache.c re-includes this header with
 * MCS_CACHE_PURGE_WALK set. If it sat inside #ifndef MCS_CACHE_PURGE_H, that
 * re-include would be a no-op -- the guard was already satisfied by the include
 * from main.c -- and the function would never be emitted. That is exactly what
 * happened first: the build linked and failed with six
 * "undefined reference to `cache_purge_sweep'" errors.
 *
 * Everything above is include-guarded as normal; only the walk below is
 * re-includable, and it is idempotent because it is a single static function
 * definition behind a macro that clustredcache.c sets exactly once.
 */
#endif /* MCS_CACHE_PURGE_H */

#ifdef MCS_CACHE_PURGE_WALK
__attribute__((unused)) static int cache_purge_sweep(struct purge_table *t,
                             uint8_t srctype, uint32_t srcid,
                             uint16_t caid, uint16_t sid, uint32_t provid,
                             uint8_t markbit)
{
	struct cache_data **cachetab;
	struct cache_data *head, *p;
	int idx, marked = 0;

	if (!t) return -1;
	if (!cache_purge_should_withhold(t, srctype, srcid, caid, sid, provid))
		return -1; /* no live mark: refuse to sweep on a non-event */

	cachetab = getcachetabbycaid(caid);
	if (!cachetab) return 0;

	/*
	 * The bucket index is sid & MAX_CACHE_INDEX and nothing else -- PROVID is
	 * not part of it (clustredcache.c:396, :475, :1185, :1253, :1308 all use
	 * the same expression). So one bucket holds several PROVIDs and the
	 * per-entry filter below is what narrows it, not the index.
	 */
	idx = sid & MAX_CACHE_INDEX;
	head = cachetab[idx];
	if (!head) return 0;

	p = head;
	do {
		if (p->sid == sid && p->provid == provid) {
			struct cw_cache_data *cwdata = p->cwdata;
			while (cwdata) {
				if (cwdata->peerid == srcid && !(cwdata->status & markbit)) {
					cwdata->status |= markbit;
					marked++;
				}
				cwdata = cwdata->next;
			}
		}
		p = p->next;
	} while (p && p != head);

	return marked;
}
#endif /* MCS_CACHE_PURGE_WALK */
