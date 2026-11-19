#ifndef __CACHE_CHAN_PREVCW_H
#define __CACHE_CHAN_PREVCW_H
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
static struct mcs_chprev_slot mcs_chprev_tab[MCS_CHPREV_SLOTS];

struct mcs_chprev_stats {
	unsigned long puts, hits, stale, dropped;
};
static struct mcs_chprev_stats mcs_chprev_st;

static inline unsigned int mcs_chprev_ttl(unsigned int cfg_ttl)
{
	return cfg_ttl >= 2000u ? cfg_ttl : MCS_CHPREV_DEF_TTL_MS;
}
/* reload journal may zero the table; not wired to config by default */
static inline void mcs_chprev_reset(void)
{
	memset(mcs_chprev_tab, 0, sizeof(mcs_chprev_tab));
	memset(&mcs_chprev_st, 0, sizeof(mcs_chprev_st));
}
static inline void mcs_chprev_get_stats(struct mcs_chprev_stats *st)
{
	*st = mcs_chprev_st;
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
static inline void mcs_chprev_put(uint16_t caid, uint32_t provid, uint16_t sid,
                                  const uint8_t *cw, unsigned int cfg_ttl)
{
	uint32_t key = mcs_chprev_key(caid, provid, sid);
	uint32_t i = key % MCS_CHPREV_SLOTS;
	int probe;
	unsigned int now = (unsigned int)MCS_CHPREV_NOW();
	unsigned int ttl = mcs_chprev_ttl(cfg_ttl);
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
			return;
		}
		if ((unsigned int)(now - s->stamp_ms) > ttl) {
			/* stale by TTL: take it over */
			s->key = key;
			memcpy(s->cw, cw, 16);
			s->stamp_ms = now;
			mcs_chprev_st.stale++;
			return;
		}
	}
	mcs_chprev_st.dropped++;
}

static inline const uint8_t *mcs_chprev_get(uint16_t caid, uint32_t provid,
                                           uint16_t sid, unsigned int cfg_ttl)
{
	uint32_t key = mcs_chprev_key(caid, provid, sid);
	uint32_t i = key % MCS_CHPREV_SLOTS;
	int probe;
	unsigned int now = (unsigned int)MCS_CHPREV_NOW();
	unsigned int ttl = mcs_chprev_ttl(cfg_ttl);
	for (probe = 0; probe < 8; probe++, i = (i + 1) % MCS_CHPREV_SLOTS) {
		struct mcs_chprev_slot *s = &mcs_chprev_tab[i];
		if (!s->key) return 0;                 /* never inserted past here */
		if (s->key != key) continue;
		if ((unsigned int)(now - s->stamp_ms) > ttl) return 0;
		mcs_chprev_st.hits++;
		return s->cw;
	}
	return 0;
}
#endif
