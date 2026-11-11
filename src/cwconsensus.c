/*
 * TASK R8 (D62) -- implementation of cwconsensus.h. Pure logic, no
 * locks: every caller runs under the cache ingest path (lockcache held
 * by the receive/worker thread) and the housekeeping walk runs in the
 * same thread family -- the same single-writer discipline the R6 queue
 * already established for this path. No allocation: a fixed slot table
 * that fails OPEN when full (deliver, count nothing).
 */
#include <string.h>
#include <stdint.h>

#include "cwconsensus.h"
#include "tick_math.h"

#define CWC_SLOTS   CWC_CAPACITY
#define CWC_MAXV     8   /* distinct voters tracked per key */
#define CWC_NOUSE    0
#define CWC_LIVE     1

struct cw_voter { uint32_t tick; int peerid; uint32_t group; int weight; };

static int cwc_independence = 0;
void cwconsensus_set_independence(int on) { cwc_independence = !!on; }

struct cw_slot {
	int      used;
	int      lost;        /* convicted within this window: refuse always */
	int      holding;     /* delivered nothing yet (release walk owns it) */
	int      cycle_set, cycle; /* first held voter's declaration */
	uint16_t caid, sid;
	uint32_t provid, hash;
	uint8_t  provider_known, tag;
	uint8_t  cw[16];
	uint32_t first_tick;  /* window birth */
	uint32_t tick;        /* last arrival */
	int      narrivals;
	int      nv;
	struct cw_voter v[CWC_MAXV];
};

static struct cw_slot cwc_tab[CWC_SLOTS];
static unsigned long cwc_n_votes, cwc_n_holds, cwc_n_releases, cwc_n_refusals;

/* R48 D-4 channel quarantine: 64 channel slots, 2 conflicts/10min -> 60s hold. */
#define CWC_Q_SLOTS 64
#define CWC_Q_CONFLICT_WINDOW 600000u
#define CWC_Q_HOLD_MS 60000u
#define CWC_STORM_RATE 20
#define CWC_STORM_WINDOW 1000u
struct cwc_qslot {
	int used;
	uint16_t caid, sid;
	uint32_t provid;
	uint8_t provider_known;
	uint32_t conflict_tick[2];
	int nconflicts;
	uint32_t quarantine_until;
	uint32_t last;
	uint32_t rate_window_start;
	int rate_count;
	uint32_t last_hash;
	uint8_t last_tag;
	uint8_t hash_set;
};
static struct cwc_qslot cwc_qtab[CWC_Q_SLOTS];
static int cwc_quarantine = 0;
static int cwc_storm_guard = 0;
static int cwc_rotation_freeze = 0;
void cwconsensus_set_quarantine(int on) { cwc_quarantine = !!on; }
void cwconsensus_set_storm_guard(int on) { cwc_storm_guard = !!on; }
void cwconsensus_set_rotation_freeze(int on) { cwc_rotation_freeze = !!on; }

void cwconsensus_init(void)
{
	memset(cwc_tab, 0, sizeof(cwc_tab));
	memset(cwc_qtab, 0, sizeof(cwc_qtab));
	cwc_n_votes = cwc_n_holds = cwc_n_releases = cwc_n_refusals = 0;
}

static struct cwc_qslot *cwc_qslot_for(uint16_t caid, uint16_t sid,
				       uint32_t provid, int provider_known,
				       uint32_t now, int create)
{
	int i;
	struct cwc_qslot *free_slot = NULL, *victim = NULL;
	uint32_t oldest = 0;
	provider_known = !!provider_known;
	if (!provider_known) provid = 0;
	for (i = 0; i < CWC_Q_SLOTS; i++) {
		struct cwc_qslot *s = &cwc_qtab[i];
		uint32_t age;
		if (!s->used) {
			if (!free_slot) free_slot = s;
			continue;
		}
		if (s->caid == caid && s->sid == sid &&
		    s->provider_known == provider_known &&
		    (!provider_known || s->provid == provid)) {
			s->last = now;
			return s;
		}
		age = (uint32_t)(now - s->last);
		if (age >= oldest) { oldest = age; victim = s; }
	}
	if (!create) return NULL;
	{
		struct cwc_qslot *s = free_slot ? free_slot : victim;
		if (!s) return NULL;
		memset(s, 0, sizeof(*s));
		s->used = 1;
		s->caid = caid; s->sid = sid; s->provid = provid;
		s->provider_known = (uint8_t)provider_known;
		s->last = now;
		return s;
	}
}

static int cwc_q_is_quarantined(uint16_t caid, uint16_t sid,
				 uint32_t provid, int provider_known,
				 uint32_t now_ms)
{
	struct cwc_qslot *s = cwc_qslot_for(caid, sid, provid, provider_known, now_ms, 0);
	if (!s || !s->quarantine_until) return 0;
	return tick32_delta(now_ms, s->quarantine_until) < 0;
}

int cwconsensus_channel_quarantined(uint16_t caid, uint16_t sid,
				    uint32_t provid, int provider_known,
				    uint32_t now_ms)
{
	if (!cwc_quarantine) return 0;
	/* Channel scope (caid+sid+provider) or provider scope (caid+provider,
	 * sid=0xFFFF wildcard): either quarantines the cache candidate. */
	if (cwc_q_is_quarantined(caid, sid, provid, provider_known, now_ms)) return 1;
	if (provider_known && cwc_q_is_quarantined(caid, 0xFFFF, provid, provider_known, now_ms)) return 1;
	return 0;
}

static void cwc_q_note_one(uint16_t caid, uint16_t sid,
			   uint32_t provid, int provider_known,
			   uint32_t hash, uint8_t tag, uint32_t now)
{
	struct cwc_qslot *s = cwc_qslot_for(caid, sid, provid, provider_known, now, 1);
	if (!s) return;
	/* R48 rotation freeze: a new hash/tag (crypto period change) freezes
	 * prior conflicts/quarantine for this scope; relearn from scratch. */
	if (cwc_rotation_freeze && s->hash_set &&
	    (s->last_hash != hash || s->last_tag != tag)) {
		s->nconflicts = 0;
		s->quarantine_until = 0;
	}
	s->last_hash = hash;
	s->last_tag = tag;
	s->hash_set = 1;
	if (s->nconflicts > 0 &&
	    (uint32_t)(now - s->conflict_tick[0]) > CWC_Q_CONFLICT_WINDOW) {
		s->conflict_tick[0] = s->conflict_tick[1];
		s->nconflicts--;
	}
	if (s->nconflicts >= 2) {
		s->quarantine_until = now + CWC_Q_HOLD_MS;
		return;
	}
	s->conflict_tick[s->nconflicts++] = now;
	if (s->nconflicts >= 2)
		s->quarantine_until = now + CWC_Q_HOLD_MS;
}

static void cwc_q_note_conflict(uint16_t caid, uint16_t sid,
				uint32_t provid, int provider_known,
				uint32_t hash, uint8_t tag, uint32_t now)
{
	if (!cwc_quarantine && !cwc_rotation_freeze) return;
	/* Channel scope + provider scope (sid wildcard): two weighted
	 * conflicts on one channel quarantine the channel; two across
	 * any channels of one provider quarantine the provider. */
	cwc_q_note_one(caid, sid, provid, provider_known, hash, tag, now);
	if (provider_known)
		cwc_q_note_one(caid, 0xFFFF, provid, provider_known, hash, tag, now);
}

unsigned long cwconsensus_votes(void)    { return cwc_n_votes; }
unsigned long cwconsensus_holds(void)    { return cwc_n_holds; }
unsigned long cwconsensus_releases(void) { return cwc_n_releases; }
unsigned long cwconsensus_refusals(void) { return cwc_n_refusals; }

static int cwc_fresh(uint32_t now, uint32_t born, uint32_t window)
{
	return tick32_elapsed(now, born) <= window;
}

static int cwc_slotof(uint16_t caid, uint16_t sid, uint32_t provid,
		      int provider_known, uint32_t hash, uint8_t tag,
		      const uint8_t *cw)
{
	provider_known = !!provider_known;
	if (!provider_known) provid = 0;
	uint32_t h = (uint32_t)(caid * 0x9E3779B1u) ^ (hash * 0x85EBCA6Bu)
		   ^ (uint32_t)(sid << 16) ^ (uint32_t)(tag << 8)
		   ^ (provid * 0xC2B2AE35u) ^ (provider_known ? 0x27D4EB2Fu : 0)
		   ^ ((uint32_t)cw[0] | ((uint32_t)cw[15] << 8));
	int i, start = (int)(h & (CWC_SLOTS - 1));
	for (i = 0; i < CWC_SLOTS; i++) {
		struct cw_slot *s = &cwc_tab[(start + i) & (CWC_SLOTS - 1)];
		if (!s->used) continue;
		if (s->caid == caid && s->sid == sid && s->hash == hash
		    && s->provider_known == provider_known
		    && (!provider_known || s->provid == provid)
		    && s->tag == tag && !memcmp(s->cw, cw, 16)) return (int)(s - cwc_tab);
	}
	return -1;
}

static int cwc_weight_sum(struct cw_slot *s, uint32_t now, uint32_t window)
{
	int i, w = 0;
	if (!cwc_independence) {
		for (i = 0; i < s->nv; i++)
			if (cwc_fresh(now, s->v[i].tick, window)) w += s->v[i].weight;
		return w;
	}
	/* Independence ON: one weight per distinct group (host IP).
	 * A flood of mirrors behind one IP is one vote, not N. */
	{
		uint32_t seen[8];
		int nseen = 0, j;
		for (i = 0; i < s->nv; i++) {
			if (!cwc_fresh(now, s->v[i].tick, window)) continue;
			for (j = 0; j < nseen; j++) if (seen[j] == s->v[i].group) break;
			if (j < nseen) continue;
			if (nseen < 8) seen[nseen++] = s->v[i].group;
			w += s->v[i].weight;
		}
	}
	return w;
}

/* best conflicting key on the SAME request: highest weight, then earliest */
static struct cw_slot *cwc_best_conflict(uint16_t caid, uint16_t sid,
			uint32_t provid, int provider_known, uint32_t hash,
			uint8_t tag, const uint8_t *cw, uint32_t now, uint32_t window)
{
	struct cw_slot *best = NULL;
	int i;
	for (i = 0; i < CWC_SLOTS; i++) {
		struct cw_slot *s = &cwc_tab[i];
		if (s->used != CWC_LIVE || s->lost) continue;
		if (!s->nv) continue;
		if (s->caid != caid || s->sid != sid || s->hash != hash || s->tag != tag
		    || s->provider_known != provider_known
		    || (provider_known && s->provid != provid)) continue;
		if (!memcmp(s->cw, cw, 16)) continue;
		/* presence follows LAST activity (a key that keeps arriving stays
		 * visible); the corroboration DEADLINE itself stays on first_tick */
		if (!cwc_fresh(now, s->tick, window)) continue;
		if (!best) { best = s; continue; }
		int wm = cwc_weight_sum(s, now, window);
		int wb = cwc_weight_sum(best, now, window);
		if (wm > wb ||
		    (wm == wb && tick32_elapsed(now, s->first_tick) >
		                  tick32_elapsed(now, best->first_tick)))
			best = s;
	}
	return best;
}

static struct cw_slot *cwc_alloc(uint16_t caid, uint16_t sid, uint32_t provid,
			int provider_known, uint32_t hash, uint8_t tag,
			const uint8_t *cw, uint32_t now, uint32_t window)
{
	int i, start;
	struct cw_slot *free_slot = NULL, *oldest = NULL;
	uint32_t oldest_age = 0;
	provider_known = !!provider_known;
	if (!provider_known) provid = 0;
	start = (int)(((uint32_t)(caid * 0x9E3779B1u) ^ (hash * 0x85EBCA6Bu)
		     ^ ((uint32_t)sid << 16) ^ ((uint32_t)tag << 8)
		     ^ (provid * 0xC2B2AE35u) ^ (provider_known ? 0x27D4EB2Fu : 0)
		     ^ ((uint32_t)cw[0] | ((uint32_t)cw[15] << 8))) & (CWC_SLOTS - 1));
	for (i = 0; i < CWC_SLOTS; i++) {
		struct cw_slot *s = &cwc_tab[(start + i) & (CWC_SLOTS - 1)];
		if (s->used != CWC_LIVE) { free_slot = s; break; }
		if (!s->holding && !cwc_fresh(now, s->tick, window)) {
			uint32_t age = tick32_elapsed(now, s->tick);
			if (!oldest || age > oldest_age) {
				oldest = s;
				oldest_age = age;
			}
		}
	}
	if (!free_slot) {
		/* reclaim the oldest EXPIRED slot; never steal a live hold */
		if (oldest) free_slot = oldest;
	}
	if (!free_slot) return NULL; /* fail open */
	memset(free_slot, 0, sizeof(*free_slot));
	free_slot->used = CWC_LIVE;
	free_slot->caid = caid; free_slot->sid = sid; free_slot->provid = provid;
	free_slot->provider_known = (uint8_t)provider_known;
	free_slot->hash = hash; free_slot->tag = tag;
	memcpy(free_slot->cw, cw, 16);
	free_slot->first_tick = now;
	return free_slot;
}

/* TASK R29 (D80): the early-delivery corroboration threshold, in
 * DISTINCT voters. Default 2 (D62). See cwconsensus.h. */
static int cwc_min_sources = 2;

void cwconsensus_set_min_sources(int n)
{
	if (n < 2) n = 2;
	if (n > 8) n = 8;
	cwc_min_sources = n;
}

int cwconsensus_verdict_group(uint16_t caid, uint16_t sid, uint32_t provid,
			int provider_known, uint32_t hash, uint8_t tag,
			const uint8_t *cw, int peerid, uint32_t group, int weight,
			uint32_t now_ms, uint32_t window_ms,
			const uint8_t **loser_cw)
{
	provider_known = !!provider_known;
	if (!provider_known) provid = 0;
	if (!group) group = (uint32_t)(peerid & 0xFFFF) + 0x10000u;
	if (loser_cw) *loser_cw = NULL;
	cwc_n_votes++;
	/* R48 D-4 quarantine: cache candidates held while channel is
	 * quarantined (cache source only; caller never blocks card servers). */
	if (cwconsensus_channel_quarantined(caid, sid, provid, provider_known, now_ms)) {
		cwc_n_holds++;
		return CWC_HOLD;
	}
	/* R48 storm guard: per-channel rate limit (cache source only). */
	if (cwc_storm_guard) {
		struct cwc_qslot *qs = cwc_qslot_for(caid, sid, provid, provider_known, now_ms, 1);
		if (qs) {
			if (!qs->rate_window_start ||
			    tick32_elapsed(now_ms, qs->rate_window_start) >= CWC_STORM_WINDOW) {
				qs->rate_window_start = now_ms;
				qs->rate_count = 0;
			}
			qs->rate_count++;
			if (qs->rate_count > CWC_STORM_RATE) {
				cwc_n_holds++;
				return CWC_HOLD;
			}
		}
	}
	int idx = cwc_slotof(caid, sid, provid, provider_known, hash, tag, cw);
	struct cw_slot *s;

	/* A completed decision is only evidence within its idle window. Do not
	 * resurrect stale voters or sticky losses on an identical later key.
	 * A held slot belongs to the expiry walker and must never be stolen. */
	if (idx >= 0 && !cwc_tab[idx].holding &&
	    !cwc_fresh(now_ms, cwc_tab[idx].tick, window_ms)) {
		cwc_tab[idx].used = CWC_NOUSE;
		idx = -1;
	}
	if (idx < 0) {
		s = cwc_alloc(caid, sid, provid, provider_known,
			      hash, tag, cw, now_ms, window_ms);
		if (!s) return CWC_DELIVER; /* table exhausted: fail open */
		idx = (int)(s - cwc_tab);
	}
	s = &cwc_tab[idx];
	if (s->lost) { cwc_n_refusals++; return CWC_REFUSE; }

	/* record the arrival + voter */
	s->tick = now_ms;
	s->narrivals++;
	{
		int i, found = -1;
		for (i = 0; i < s->nv; i++) if (s->v[i].peerid == peerid) { found = i; break; }
		if (found >= 0) {
			s->v[found].tick = now_ms;
			s->v[found].weight = weight; /* trust moves, follow it */
			s->v[found].group = group;
		} else if (s->nv < CWC_MAXV) {
			s->v[s->nv].peerid = peerid;
			s->v[s->nv].group = group;
			s->v[s->nv].weight = weight;
			s->v[s->nv].tick = now_ms;
			s->nv++;
		}
	}

	{
		struct cw_slot *cf = cwc_best_conflict(caid, sid, provid,
						       provider_known, hash, tag, cw,
						       now_ms, window_ms);
		if (!cf) {
			if (cwc_weight_sum(s, now_ms, window_ms) >= cwc_min_sources) {
				s->holding = 0;
				return CWC_DELIVER; /* corroborated: early unanimous */
			}
			if (!s->holding) { s->holding = 1; cwc_n_holds++; }
			return CWC_HOLD;
		}
		/* conflict: weighted decision, timing only breaks silence */
		int my_w  = cwc_weight_sum(s, now_ms, window_ms);
		int ot_w  = cwc_weight_sum(cf, now_ms, window_ms);
		int i_earlier = tick32_elapsed(now_ms, s->first_tick) >
		                tick32_elapsed(now_ms, cf->first_tick);
		if (my_w > ot_w || (my_w == ot_w && i_earlier)) {
			/* I win: neutralize + convict the loser only on WEIGHT */
			s->holding = 0;
			if (my_w > ot_w) {
				cf->lost = 1; cf->holding = 0;
				if (loser_cw) *loser_cw = cf->cw;
			}
			return CWC_DELIVER;
		}
		/* I lost */
		if (my_w < ot_w) {
			s->lost = 1; s->holding = 0;
			cwc_n_refusals++;
			/* R48 D-4: weighted loss counts toward channel quarantine. */
			cwc_q_note_conflict(caid, sid, provid, provider_known, hash, tag, now_ms);
			if (loser_cw) *loser_cw = cf->cw; /* winner, for the log */
			return CWC_REFUSE;
		}
		/* timing loss: refuse silently, convict nobody */
		s->holding = 0;
		cwc_n_refusals++;
		return CWC_REFUSE;
	}
}

int cwconsensus_verdict(uint16_t caid, uint16_t sid, uint32_t provid,
			int provider_known, uint32_t hash, uint8_t tag,
			const uint8_t *cw, int peerid, int weight,
			uint32_t now_ms, uint32_t window_ms,
			const uint8_t **loser_cw)
{
	return cwconsensus_verdict_group(caid, sid, provid, provider_known,
		hash, tag, cw, peerid, 0, weight, now_ms, window_ms, loser_cw);
}

int cwconsensus_vote_counts(uint16_t caid, uint16_t sid, uint32_t provid,
                            int provider_known, uint32_t hash, uint8_t tag,
                            const uint8_t *cw, uint32_t now_ms,
                            uint32_t window_ms, int *distinct, int *weight)
{
    int idx, i, count = 0, weighted = 0;
    if (distinct) *distinct = 0;
    if (weight) *weight = 0;
    if (!cw) return 0;
    idx = cwc_slotof(caid, sid, provid, provider_known, hash, tag, cw);
    if (idx < 0) return 0;
    if (!cwc_independence) {
        for (i = 0; i < cwc_tab[idx].nv; ++i) {
            struct cw_voter *v = &cwc_tab[idx].v[i];
            if (cwc_fresh(now_ms, v->tick, window_ms)) {
                ++count;
                weighted += v->weight;
            }
        }
    } else {
        uint32_t seen[8];
        int nseen = 0, j;
        for (i = 0; i < cwc_tab[idx].nv; ++i) {
            struct cw_voter *v = &cwc_tab[idx].v[i];
            if (!cwc_fresh(now_ms, v->tick, window_ms)) continue;
            for (j = 0; j < nseen; j++) if (seen[j] == v->group) break;
            if (j < nseen) continue;
            if (nseen < 8) seen[nseen++] = v->group;
            ++count;
            weighted += v->weight;
        }
    }
    if (distinct) *distinct = count;
    if (weight) *weight = weighted;
    return 1;
}

int cwconsensus_is_held(uint16_t caid, uint16_t sid, uint32_t provid,
                        int provider_known, uint32_t hash, uint8_t tag,
                        const uint8_t *cw)
{
    int idx;
    if (!cw) return 0;
    idx = cwc_slotof(caid, sid, provid, provider_known, hash, tag, cw);
    if (idx >= 0 && cwc_tab[idx].holding && !cwc_tab[idx].lost) return 1;
    /* An unknown CSP hold cannot corroborate a known provider, but it must
     * continue blocking this same CW after an entry gains local provider
     * context. This is a conservative hold, not a cross-scope vote. */
    if (provider_known) {
        idx = cwc_slotof(caid, sid, 0, 0, hash, tag, cw);
        if (idx >= 0 && cwc_tab[idx].holding && !cwc_tab[idx].lost) return 1;
    }
    return 0;
}

void cwconsensus_capacity(uint32_t now_ms, uint32_t window_ms,
                          struct cwconsensus_capacity *out)
{
    int i;
    memset(out, 0, sizeof(*out));
    out->capacity = CWC_SLOTS;
    for (i = 0; i < CWC_SLOTS; ++i) {
        struct cw_slot *s = &cwc_tab[i];
        uint32_t age;
        if (s->used != CWC_LIVE) continue;
        ++out->used;
        if (!s->holding) {
            if (!cwc_fresh(now_ms, s->tick, window_ms)) ++out->reclaimable;
            continue;
        }
        ++out->held;
        if (!cwc_fresh(now_ms, s->first_tick, window_ms)) ++out->overdue;
        age = tick32_elapsed(now_ms, s->first_tick);
        if (age > out->oldest_hold_ms) out->oldest_hold_ms = age;
    }
}

void cwconsensus_hold_cycle(uint16_t caid, uint16_t sid, uint32_t provid,
                            int provider_known, uint32_t hash, uint8_t tag,
                            const uint8_t *cw, int peerid, int cycle)
{
    int idx = cwc_slotof(caid, sid, provid, provider_known, hash, tag, cw);
    struct cw_slot *s;
    if (idx < 0) return;
    s = &cwc_tab[idx];
    if (s->holding && !s->cycle_set && s->nv && s->v[0].peerid == peerid) {
        s->cycle = cycle;
        s->cycle_set = 1;
    }
}

int cwconsensus_expired(struct cwconsensus_hold *out, uint32_t now_ms,
			uint32_t window_ms)
{
	int i;
	for (i = 0; i < CWC_SLOTS; i++) {
		struct cw_slot *s = &cwc_tab[i];
		if (s->used != CWC_LIVE || !s->holding || s->lost) continue;
		if (cwc_fresh(now_ms, s->first_tick, window_ms)) continue;
		out->caid = s->caid; out->sid = s->sid; out->provid = s->provid;
		out->provider_known = s->provider_known;
		out->hash = s->hash; out->tag = s->tag;
		memcpy(out->cw, s->cw, 16);
		out->peerid = s->nv ? s->v[0].peerid : 0;
		out->cycle = s->cycle_set ? s->cycle : 0;
		s->used = CWC_NOUSE; /* consumed: no double release */
		cwc_n_releases++;
		return 1;
	}
	return 0;
}
