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

#define CWC_SLOTS   64   /* power of two */
#define CWC_MAXV     8   /* distinct voters tracked per key */
#define CWC_NOUSE    0
#define CWC_LIVE     1

struct cw_voter { uint32_t tick; int peerid; int weight; };

struct cw_slot {
	int      used;
	int      lost;        /* convicted within this window: refuse always */
	int      holding;     /* delivered nothing yet (release walk owns it) */
	uint16_t caid, sid, provid;
	uint32_t hash;
	uint8_t  tag;
	uint8_t  cw[16];
	uint32_t first_tick;  /* window birth */
	uint32_t tick;        /* last arrival */
	int      narrivals;
	int      nv;
	struct cw_voter v[CWC_MAXV];
};

static struct cw_slot cwc_tab[CWC_SLOTS];
static unsigned long cwc_n_votes, cwc_n_holds, cwc_n_releases, cwc_n_refusals;

void cwconsensus_init(void)
{
	memset(cwc_tab, 0, sizeof(cwc_tab));
	cwc_n_votes = cwc_n_holds = cwc_n_releases = cwc_n_refusals = 0;
}

unsigned long cwconsensus_votes(void)    { return cwc_n_votes; }
unsigned long cwconsensus_holds(void)    { return cwc_n_holds; }
unsigned long cwconsensus_releases(void) { return cwc_n_releases; }
unsigned long cwconsensus_refusals(void) { return cwc_n_refusals; }

static int cwc_fresh(uint32_t now, uint32_t born, uint32_t window)
{
	return (int32_t)(now - born) <= (int32_t)window;
}

static int cwc_slotof(uint16_t caid, uint16_t sid, uint32_t hash, uint8_t tag,
		      const uint8_t *cw)
{
	uint32_t h = (uint32_t)(caid * 0x9E3779B1u) ^ (hash * 0x85EBCA6Bu)
		   ^ (uint32_t)(sid << 16) ^ (uint32_t)(tag << 8)
		   ^ ((uint32_t)cw[0] | ((uint32_t)cw[15] << 8));
	int i, start = (int)(h & (CWC_SLOTS - 1));
	for (i = 0; i < CWC_SLOTS; i++) {
		struct cw_slot *s = &cwc_tab[(start + i) & (CWC_SLOTS - 1)];
		if (!s->used) continue;
		if (s->caid == caid && s->sid == sid && s->hash == hash
		    && s->tag == tag && !memcmp(s->cw, cw, 16)) return (int)(s - cwc_tab);
	}
	return -1;
}

static int cwc_weight_sum(struct cw_slot *s, uint32_t now, uint32_t window)
{
	int i, w = 0;
	for (i = 0; i < s->nv; i++)
		if (cwc_fresh(now, s->v[i].tick, window)) w += s->v[i].weight;
	return w;
}

/* best conflicting key on the SAME request: highest weight, then earliest */
static struct cw_slot *cwc_best_conflict(uint16_t caid, uint16_t sid,
			uint32_t hash, uint8_t tag, const uint8_t *cw,
			uint32_t now, uint32_t window)
{
	struct cw_slot *best = NULL;
	int i;
	for (i = 0; i < CWC_SLOTS; i++) {
		struct cw_slot *s = &cwc_tab[i];
		if (s->used != CWC_LIVE || s->lost) continue;
		if (!s->nv) continue;
		if (s->caid != caid || s->sid != sid || s->hash != hash || s->tag != tag)
			continue;
		if (!memcmp(s->cw, cw, 16)) continue;
		/* presence follows LAST activity (a key that keeps arriving stays
		 * visible); the corroboration DEADLINE itself stays on first_tick */
		if (!cwc_fresh(now, s->tick, window)) continue;
		if (!best) { best = s; continue; }
		int wm = cwc_weight_sum(s, now, window);
		int wb = cwc_weight_sum(best, now, window);
		if ( (wm > wb) || (wm == wb && (int32_t)(s->first_tick - best->first_tick) < 0) )
			best = s;
	}
	return best;
}

static struct cw_slot *cwc_alloc(uint16_t caid, uint16_t sid, uint16_t provid,
			uint32_t hash, uint8_t tag, const uint8_t *cw,
			uint32_t now)
{
	int i, start;
	struct cw_slot *free_slot = NULL, *oldest = NULL;
	start = (int)(((uint32_t)(caid * 0x9E3779B1u) ^ (hash * 0x85EBCA6Bu)
		     ^ ((uint32_t)sid << 16) ^ ((uint32_t)tag << 8)
		     ^ ((uint32_t)cw[0] | ((uint32_t)cw[15] << 8))) & (CWC_SLOTS - 1));
	for (i = 0; i < CWC_SLOTS; i++) {
		struct cw_slot *s = &cwc_tab[(start + i) & (CWC_SLOTS - 1)];
		if (s->used != CWC_LIVE) { free_slot = s; break; }
		if (!oldest || (int32_t)(s->first_tick - oldest->first_tick) < 0) oldest = s;
	}
	if (!free_slot) {
		/* reclaim the oldest EXPIRED slot; never steal a live hold */
		if (oldest && !oldest->holding
		    && !cwc_fresh(now, oldest->first_tick, 60000)) free_slot = oldest;
	}
	if (!free_slot) return NULL; /* fail open */
	memset(free_slot, 0, sizeof(*free_slot));
	free_slot->used = CWC_LIVE;
	free_slot->caid = caid; free_slot->sid = sid; free_slot->provid = provid;
	free_slot->hash = hash; free_slot->tag = tag;
	memcpy(free_slot->cw, cw, 16);
	free_slot->first_tick = now;
	return free_slot;
}

int cwconsensus_verdict(uint16_t caid, uint16_t sid, uint32_t hash,
			uint8_t tag, const uint8_t *cw, int peerid,
			int weight, uint32_t now_ms, uint32_t window_ms,
			const uint8_t **loser_cw)
{
	int idx = cwc_slotof(caid, sid, hash, tag, cw);
	struct cw_slot *s;
	if (loser_cw) *loser_cw = NULL;
	cwc_n_votes++;

	if (idx < 0) {
		s = cwc_alloc(caid, sid, 0, hash, tag, cw, now_ms);
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
		} else if (s->nv < CWC_MAXV) {
			s->v[s->nv].peerid = peerid;
			s->v[s->nv].weight = weight;
			s->v[s->nv].tick = now_ms;
			s->nv++;
		}
	}

	{
		struct cw_slot *cf = cwc_best_conflict(caid, sid, hash, tag, cw,
						       now_ms, window_ms);
		if (!cf) {
			if (cwc_weight_sum(s, now_ms, window_ms) >= 2) {
				s->holding = 0;
				return CWC_DELIVER; /* corroborated: early unanimous */
			}
			if (!s->holding) { s->holding = 1; cwc_n_holds++; }
			return CWC_HOLD;
		}
		/* conflict: weighted decision, timing only breaks silence */
		int my_w  = cwc_weight_sum(s, now_ms, window_ms);
		int ot_w  = cwc_weight_sum(cf, now_ms, window_ms);
		int i_earlier = (int32_t)(s->first_tick - cf->first_tick) < 0;
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
			if (loser_cw) *loser_cw = cf->cw; /* winner, for the log */
			return CWC_REFUSE;
		}
		/* timing loss: refuse silently, convict nobody */
		s->holding = 0;
		cwc_n_refusals++;
		return CWC_REFUSE;
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
		out->hash = s->hash; out->tag = s->tag;
		memcpy(out->cw, s->cw, 16);
		out->peerid = s->nv ? s->v[0].peerid : 0;
		s->used = CWC_NOUSE; /* consumed: no double release */
		cwc_n_releases++;
		return 1;
	}
	return 0;
}
