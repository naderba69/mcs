/*
 * CWPAIR (band 38) -- simulcrypt control-word pairing.
 *
 * DVB SimulCrypt broadcasts one service scrambled by ONE control word
 * protected by several CA systems in parallel (ETSI TS 101 197: the
 * SCS hands the same CW to every ECM generator). So when the requested
 * (CAID,provider,SID) has no CW from cache or servers but its
 * simulcrypt twin on another CAID just produced one, that CW is the
 * correct CW for this channel too -- serving it is serving the real
 * key, not a guess.
 *
 * Two halves, both opt-in and OFF by default (stock behaviour):
 *
 * 1) Discovery (CWDEDUP): observe every CW that is committed for a
 *    channel (setdcw commit) or stored from a cache peer, and learn
 *    "same 16 CW bytes on two different CAIDs within the window" as a
 *    candidate pair. Learning never serves anything; it only counts.
 *    The operator copies confirmed pairs into profiles by hand --
 *    no CAID/SID is ever invented here.
 *
 * 2) Serving (per profile): CWPAIR CAID + CWPAIR SID map + ACTIVE.
 *    On a total miss (cache and servers exhausted, DCW timeout) the
 *    ECM worker asks for the twin channel's freshest observed CW,
 *    bounded by CWPAIR MAXAGE, optionally hardened by STRICTCYCLE
 *    (the paired CW must share exactly one 8-byte half with the local
 *    channel's previous CW). The candidate is delivered through the
 *    normal ecm_setdcw() path, so every existing profile gate still
 *    judges it as a CW for the local CAID.
 *
 * Freshness is the whole ballgame: a CW lives one crypto period
 * (~10 s on most bouquets). cwpair_stale counts candidates refused
 * for age so the operator can see a too-slow twin feed in numbers.
 */
#ifndef CWPAIR_H
#define CWPAIR_H

#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include "field-status.h"

#define CWPAIR_MAX_CHANNELS 512
#define CWPAIR_MAX_PAIRS    256
#define CWPAIR_WINDOW_MS    20000u

struct cwpair_channel {
	uint16_t caid;
	uint32_t provid;
	uint16_t sid;
	uint8_t  cw[16];
	uint32_t tick;
	int      used;
};

struct cwpair_pair {
	uint16_t caid_a; uint32_t provid_a; uint16_t sid_a;
	uint16_t caid_b; uint32_t provid_b; uint16_t sid_b;
	unsigned matches;
	uint32_t last_tick;
};

struct cwpair_state {
	pthread_mutex_t lock;
	struct cwpair_channel channels[CWPAIR_MAX_CHANNELS];
	struct cwpair_pair pairs[CWPAIR_MAX_PAIRS];
	int discover;
	uint16_t discover_caids[32];
	int discover_n;
	unsigned long observed;
	unsigned long pair_matches;
	unsigned long served;
	unsigned long stale;
	unsigned long strict_rejected;
	unsigned long no_twin;
};

static struct cwpair_state g_cwpair = { PTHREAD_MUTEX_INITIALIZER };

static void cwpair_set_discovery(int on, const uint16_t *caids, int n)
{
	int i;
	pthread_mutex_lock(&g_cwpair.lock);
	g_cwpair.discover = on ? 1 : 0;
	g_cwpair.discover_n = 0;
	if (caids) {
		for (i = 0; i < n && i < 32; i++)
			g_cwpair.discover_caids[g_cwpair.discover_n++] = caids[i];
	}
	pthread_mutex_unlock(&g_cwpair.lock);
}

/* caller holds lock */
static int cwpair_caid_listed(uint16_t caid)
{
	int i;
	if (!g_cwpair.discover) return 0;
	if (g_cwpair.discover_n == 0) return 1;
	for (i = 0; i < g_cwpair.discover_n; i++)
		if (g_cwpair.discover_caids[i] == caid) return 1;
	return 0;
}

static struct cwpair_channel *cwpair_channel_locked(uint16_t caid, uint32_t provid, uint16_t sid, int create)
{
	int i, free_slot = -1, oldest = 0;
	uint32_t oldest_tick = 0xFFFFFFFFu;
	for (i = 0; i < CWPAIR_MAX_CHANNELS; i++) {
		struct cwpair_channel *c = &g_cwpair.channels[i];
		if (!c->used) { if (free_slot < 0) free_slot = i; continue; }
		if (c->caid == caid && c->provid == provid && c->sid == sid) return c;
		if (c->tick < oldest_tick) { oldest_tick = c->tick; oldest = i; }
	}
	if (!create) return NULL;
	i = free_slot >= 0 ? free_slot : oldest;
	memset(&g_cwpair.channels[i], 0, sizeof(g_cwpair.channels[i]));
	g_cwpair.channels[i].caid = caid;
	g_cwpair.channels[i].provid = provid;
	g_cwpair.channels[i].sid = sid;
	g_cwpair.channels[i].used = 1;
	return &g_cwpair.channels[i];
}

/* Feed one genuinely produced CW (committed delivery or cache store). */
static void cwpair_observe(uint16_t caid, uint32_t provid, uint16_t sid, const uint8_t *cw, uint32_t tick)
{
	struct cwpair_channel *self;
	int i;
	if (!cw) return;
	pthread_mutex_lock(&g_cwpair.lock);
	g_cwpair.observed++;
	self = cwpair_channel_locked(caid, provid, sid, 1);
	if (self) { memcpy(self->cw, cw, 16); self->tick = tick; }
	/* Pair learning: identical CW bytes on a different CAID inside
	 * the window is the simulcrypt signature. */
	if (cwpair_caid_listed(caid)) {
		for (i = 0; i < CWPAIR_MAX_CHANNELS; i++) {
			struct cwpair_channel *o = &g_cwpair.channels[i];
			int j, found = 0;
			if (!o->used || o == self || o->caid == caid) continue;
			if (!cwpair_caid_listed(o->caid)) continue;
			if ((uint32_t)(tick - o->tick) > CWPAIR_WINDOW_MS) continue;
			if (memcmp(o->cw, cw, 16) != 0) continue;
			for (j = 0; j < CWPAIR_MAX_PAIRS; j++) {
				struct cwpair_pair *p = &g_cwpair.pairs[j];
				if (p->caid_a == caid && p->provid_a == provid && p->sid_a == sid &&
				    p->caid_b == o->caid && p->provid_b == o->provid && p->sid_b == o->sid) {
					p->matches++; p->last_tick = tick; found = 1; break;
				}
				if (p->caid_b == caid && p->provid_b == provid && p->sid_b == sid &&
				    p->caid_a == o->caid && p->provid_a == o->provid && p->sid_a == o->sid) {
					p->matches++; p->last_tick = tick; found = 1; break;
				}
			}
			if (!found) {
				for (j = 0; j < CWPAIR_MAX_PAIRS; j++) {
					struct cwpair_pair *p = &g_cwpair.pairs[j];
					if (p->matches == 0) {
						p->caid_a = caid; p->provid_a = provid; p->sid_a = sid;
						p->caid_b = o->caid; p->provid_b = o->provid; p->sid_b = o->sid;
						p->matches = 1; p->last_tick = tick;
						break;
					}
				}
			}
			g_cwpair.pair_matches++;
		}
	}
	pthread_mutex_unlock(&g_cwpair.lock);
}

/* The twin channel's freshest observed CW for the serving path.
 * Returns 1 and fills cw_out when a usable candidate exists. */
static int cwpair_lookup(uint16_t peer_caid, uint16_t peer_sid,
                         uint16_t local_caid, uint32_t local_provid, uint16_t local_sid,
                         uint32_t maxage_ms, int strict_cycle,
                         uint8_t *cw_out, uint32_t tick)
{
	struct cwpair_channel *peer, *local;
	uint8_t cw[16];
	uint32_t age;
	int ok = 0;
	pthread_mutex_lock(&g_cwpair.lock);
	peer = cwpair_channel_locked(peer_caid, 0, peer_sid, 0);
	if (!peer && peer_caid) {
		/* Cache peers often report provider 0 while the local ECM
		 * carries the real provider; the twin lookup is by
		 * (CAID,SID) because the map the operator pasted is too. */
		int i;
		for (i = 0; i < CWPAIR_MAX_CHANNELS; i++) {
			struct cwpair_channel *c = &g_cwpair.channels[i];
			if (c->used && c->caid == peer_caid && c->sid == peer_sid &&
			    (!peer || c->tick > peer->tick)) peer = c;
		}
	}
	if (!peer) { g_cwpair.no_twin++; pthread_mutex_unlock(&g_cwpair.lock); return 0; }
	memcpy(cw, peer->cw, 16);
	age = tick - peer->tick;
	if (maxage_ms && age > maxage_ms) {
		g_cwpair.stale++;
		pthread_mutex_unlock(&g_cwpair.lock);
		return 0;
	}
	if (strict_cycle) {
		local = cwpair_channel_locked(local_caid, local_provid, local_sid, 0);
		if (local) {
			int share_first  = memcmp(local->cw, cw, 8) == 0;
			int share_second = memcmp(local->cw + 8, cw + 8, 8) == 0;
			if (share_first == share_second) {
				g_cwpair.strict_rejected++;
				pthread_mutex_unlock(&g_cwpair.lock);
				return 0;
			}
		}
	}
	memcpy(cw_out, cw, 16);
	g_cwpair.served++;
	ok = 1;
	pthread_mutex_unlock(&g_cwpair.lock);
	return ok;
}

/* Snapshot for the operator page (field-status): counters + learned
 * pairs, newest data by value copy -- no pointers escape the lock. */
int cwpair_snapshot(unsigned long *observed, unsigned long *pair_matches,
                           unsigned long *served, unsigned long *stale,
                           unsigned long *strict_rejected, unsigned long *no_twin,
                           int *discover, struct mcs_cwpair_pair *pairs, int maxpairs)
{
	int i, n = 0;
	pthread_mutex_lock(&g_cwpair.lock);
	if (observed) *observed = g_cwpair.observed;
	if (pair_matches) *pair_matches = g_cwpair.pair_matches;
	if (served) *served = g_cwpair.served;
	if (stale) *stale = g_cwpair.stale;
	if (strict_rejected) *strict_rejected = g_cwpair.strict_rejected;
	if (no_twin) *no_twin = g_cwpair.no_twin;
	if (discover) *discover = g_cwpair.discover;
	if (pairs) {
		/* Band 82: export the most-confirmed pairs first (matches
		 * desc, freshest first) so the operator confirms from the
		 * top of the list; last_tick rides along for the age. */
		for (i = 0; i < CWPAIR_MAX_PAIRS; i++) {
			struct mcs_cwpair_pair cand;
			int k;
			if (!g_cwpair.pairs[i].matches) continue;
			cand.caid_a = g_cwpair.pairs[i].caid_a;
			cand.provid_a = g_cwpair.pairs[i].provid_a;
			cand.sid_a = g_cwpair.pairs[i].sid_a;
			cand.caid_b = g_cwpair.pairs[i].caid_b;
			cand.provid_b = g_cwpair.pairs[i].provid_b;
			cand.sid_b = g_cwpair.pairs[i].sid_b;
			cand.matches = g_cwpair.pairs[i].matches;
			cand.last_tick = g_cwpair.pairs[i].last_tick;
			if (n == maxpairs) {
				if (cand.matches < pairs[n-1].matches ||
				    (cand.matches == pairs[n-1].matches && cand.last_tick <= pairs[n-1].last_tick)) continue;
				n--;
			}
			k = n;
			while (k > 0 && (pairs[k-1].matches < cand.matches ||
			       (pairs[k-1].matches == cand.matches && pairs[k-1].last_tick < cand.last_tick))) {
				pairs[k] = pairs[k-1]; k--;
			}
			pairs[k] = cand;
			n++;
		}
	}
	pthread_mutex_unlock(&g_cwpair.lock);
	return n;
}

#endif /* CWPAIR_H */
