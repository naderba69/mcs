#ifndef __CACHE_CHAN_PREVCW_H
#define __CACHE_CHAN_PREVCW_H

#include <string.h>   /* memset/memcpy: this header used to rely on its includer */
#include <pthread.h>  /* band 119: two writers now -- the cache relay tail AND
                       * the local delivery commit in setdcw.c, which runs on a
                       * different thread than prg.lockcache's holders */
/*
 * Band 115: per-channel memory of the last SERVED control word.
 *
 * Why this exists (the number that forced it): the operator's 2026-11-19
 * 03:35:05 snapshot refused 604333 post-consensus keys as "no cycle", with
 * ALL of them classified "no prevcw" and the learnable counter at ZERO --
 * pcache->prevcw has no local producer anywhere in the tree (only a peer's
 * 16-byte ECM2cache tail fills it, clustredcache.c:4579+), so with unmarked
 * peers the temporal estimator had nothing to measure, forever, no matter
 * how long the box ran. Meanwhile MARCHCHECK flagged 10044 half-parked
 * deliveries on the SAME channels: the rotation was visible, only the
 * per-entry memory was missing.
 *
 * The memory therefore takes what the box already proved: every key it
 * actually serves (post-gate, no error) becomes the channel's "previous
 * CW" for a short TTL. It only ever feeds the EXISTING estimator inputs --
 * a candidate still needs exactly one carried-over half to pass, so
 * recycling (identical key) and fresh garbage (both halves changed) keep
 * their old refusals. OFF by default: stock behaviour is untouched.
 *
 * Deliberately small: fixed open-addressed table, no allocation, no
 * protocol, no stock write to pcache->prevcw -- the entry's own tail value
 * always wins (the hook only fills the null case).
 */
#define MCS_CHPREV_SLOTS 256
#define MCS_CHPREV_DEF_TTL_MS 30000u

/* enable/TTL are passed at each call from cfg.cache (config.c owns them),
 * so a live reload re-reads them without a sync step of any kind. */
/* Test seam: the production clock is GetTickCount(); tests inject a fake. */
#ifndef MCS_CHPREV_NOW
#define MCS_CHPREV_NOW() GetTickCount()
#endif

struct mcs_chprev_slot {
	uint32_t key;             /* nonzero; 0 = free */
	uint8_t cw[16];           /* last served key on this channel */
	unsigned int stamp_ms;    /* low 32 bits of the serving tick */
};

struct mcs_chprev_stats {
	unsigned long puts, hits, stale, dropped, loc;
};

/*
 * Band 119 -- ONE instance for the whole program.
 *
 * Until now the table, the counters and the lock were `static` in this header,
 * and the header is included by three translation units (clustredcache.c,
 * setdcw.c, httpserver.c). `static` at file scope means INTERNAL LINKAGE: every
 * one of those objects got a private copy of its own. Consequences, both
 * confirmed from the field snapshot of 2026-11-19 (~21:27, uptime 8h28m):
 *
 *   - the counters printed on /user-trace were read out of httpserver.o's copy,
 *     which no code path writes, so `Wrote=0 Read=0` printed FOREVER no matter
 *     how much traffic the box served. The zero I acted on in the last session
 *     ("the writer never ran") is therefore NOT evidence about the writer --
 *     it is evidence about the display. Corrected here, in the open, because the
 *     same class of bug is what makes a counter line untrustworthy in general.
 *   - a writer in setdcw.c (this band's feed) and the gate reader in
 *     clustredcache.c could not have shared a table at all, and the mutex would
 *     have been two different mutexes -- i.e. no mutual exclusion whatsoever.
 *
 * The owning translation unit (clustredcache.c) defines MCS_CHAN_PREVCW_IMPL
 * before including this header; everyone else sees `extern`. No new .c file, so
 * no makefile/dep changes: the object layout is unchanged and the state is
 * simply no longer duplicated.
 */
#ifdef MCS_CHAN_PREVCW_IMPL
#define MCS_CHPREV_EXT
#else
#define MCS_CHPREV_EXT extern
#endif

MCS_CHPREV_EXT struct mcs_chprev_slot mcs_chprev_tab[MCS_CHPREV_SLOTS];
MCS_CHPREV_EXT struct mcs_chprev_stats mcs_chprev_st;
MCS_CHPREV_EXT pthread_mutex_t mcs_chprev_lk;

#ifdef MCS_CHAN_PREVCW_IMPL
struct mcs_chprev_slot mcs_chprev_tab[MCS_CHPREV_SLOTS];
struct mcs_chprev_stats mcs_chprev_st;
pthread_mutex_t mcs_chprev_lk = PTHREAD_MUTEX_INITIALIZER;
#endif

static inline unsigned int mcs_chprev_ttl(unsigned int cfg_ttl)
{
	return cfg_ttl >= 2000u ? cfg_ttl : MCS_CHPREV_DEF_TTL_MS;
}
/* reload journal may zero the table; not wired to config by default */
static inline void mcs_chprev_reset(void)
{
	pthread_mutex_lock(&mcs_chprev_lk);
	memset(mcs_chprev_tab, 0, sizeof(mcs_chprev_tab));
	memset(&mcs_chprev_st, 0, sizeof(mcs_chprev_st));
	pthread_mutex_unlock(&mcs_chprev_lk);
}
static inline void mcs_chprev_get_stats(struct mcs_chprev_stats *st)
{
	/* band 119: under the same lock as the writers. A 40-byte struct
	 * assignment is not atomic, and the whole point of the counters is that
	 * each one is trustworthy on its own -- a torn read would put `Wrote` and
	 * `local` in different instants and re-create the ambiguity this band
	 * exists to remove. */
	pthread_mutex_lock(&mcs_chprev_lk);
	*st = mcs_chprev_st;
	pthread_mutex_unlock(&mcs_chprev_lk);
}

static inline uint32_t mcs_chprev_key(uint16_t caid, uint32_t provid, uint16_t sid)
{
	uint32_t h = 2166136261u;
	int i;
	h = (h ^ caid) * 16777619u;
	for (i = 0; i < 4; i++) h = (h ^ ((provid >> (8*i)) & 0xFF)) * 16777619u;
	h = (h ^ sid) * 16777619u;
	return h ? h : 1u;   /* 0 means "free slot" -- never emit it */
}

/* called with prg.lockcache held (the gate holds it); no extra lock needed. */
/* from_local: the key was DELIVERED to one of our own clients (the committed
 * path in setdcw.c) rather than merely relayed to peers. The two feeds are
 * counted apart because they cover different channels: the relay tail sees only
 * keys that survived the cycle gate, the commit sees every answered ECM.
 * `puts` alone cannot say which of them is alive, and that ambiguity is what
 * cost the last session a wrong conclusion (see the retraction above). */
static inline void mcs_chprev_put_src(uint16_t caid, uint32_t provid, uint16_t sid,
                                      const uint8_t *cw, unsigned int cfg_ttl,
                                      int from_local)
{
	uint32_t key = mcs_chprev_key(caid, provid, sid);
	uint32_t i = key % MCS_CHPREV_SLOTS;
	int probe;
	unsigned int now = (unsigned int)MCS_CHPREV_NOW();
	unsigned int ttl = mcs_chprev_ttl(cfg_ttl);
	pthread_mutex_lock(&mcs_chprev_lk);
	if (from_local) mcs_chprev_st.loc++;
	mcs_chprev_st.puts++;
	/* linear probe, 8 steps: replace an equal key, a free slot, or the
	 * stalest occupant seen; a full collision run drops the memory rather
	 * than evicting a live channel (drop is visible in stats). */
	for (probe = 0; probe < 8; probe++, i = (i + 1) % MCS_CHPREV_SLOTS) {
		struct mcs_chprev_slot *s = &mcs_chprev_tab[i];
		if (!s->key || s->key == key) {
			s->key = key;
			memcpy(s->cw, cw, 16);
			s->stamp_ms = now;
			pthread_mutex_unlock(&mcs_chprev_lk);
			return;
		}
		if ((unsigned int)(now - s->stamp_ms) > ttl) {
			/* stale by TTL: take it over */
			s->key = key;
			memcpy(s->cw, cw, 16);
			s->stamp_ms = now;
			mcs_chprev_st.stale++;
			pthread_mutex_unlock(&mcs_chprev_lk);
			return;
		}
	}
	mcs_chprev_st.dropped++;
	pthread_mutex_unlock(&mcs_chprev_lk);
}

static inline void mcs_chprev_put(uint16_t caid, uint32_t provid, uint16_t sid,
                                  const uint8_t *cw, unsigned int cfg_ttl)
{
	mcs_chprev_put_src(caid, provid, sid, cw, cfg_ttl, 0);
}

/* Copies the bytes out (1) instead of handing back a pointer into the table:
 * with a second writer thread, a returned pointer could be overwritten under
 * the reader. Returns 1 and fills out[16] on a live hit. */
static inline int mcs_chprev_get(uint16_t caid, uint32_t provid,
                                uint16_t sid, unsigned int cfg_ttl,
                                uint8_t *out)
{
	uint32_t key = mcs_chprev_key(caid, provid, sid);
	uint32_t i = key % MCS_CHPREV_SLOTS;
	int probe;
	unsigned int now = (unsigned int)MCS_CHPREV_NOW();
	unsigned int ttl = mcs_chprev_ttl(cfg_ttl);
	int hit = 0;
	pthread_mutex_lock(&mcs_chprev_lk);
	for (probe = 0; probe < 8; probe++, i = (i + 1) % MCS_CHPREV_SLOTS) {
		struct mcs_chprev_slot *s = &mcs_chprev_tab[i];
		if (!s->key) break;                     /* never inserted past here */
		if (s->key != key) continue;
		if ((unsigned int)(now - s->stamp_ms) > ttl) break;
		mcs_chprev_st.hits++;
		if (out) memcpy(out, s->cw, 16);
		hit = 1;
		break;
	}
	pthread_mutex_unlock(&mcs_chprev_lk);
	return hit;
}
#endif
