/* cafamily.h -- TASK R16 (D71): the CAID family table.
 *
 * WHY THIS EXISTS. The filters carried the CA-family rules as hard-coded
 * numbers: half-nulled keys "belong to NDS" ((caid>>8)!=9 at
 * clustredcache.c:2107 and setdcw.c:255+648), and the provider check is
 * skipped for {18,09,0B} (main.c:2294; loadbalance.c:20+43 -- the two
 * sides DISAGREE for other families). A new CA system is a code patch.
 * SCOPE §3b's fix, the mission's mandate: the numbers move into
 * multics.cfg -- `CAID FAMILY HALFNULLED: 09, 0E` /
 * `CAID FAMILY NOPROVCHK: 18, 09, 0B` -- "a config line, not a patch".
 *
 * WHAT THIS IS. One table, two flags, 256 bits each (one bit per CA
 * family -- the family is the high byte of the caid). Pure: no
 * allocation, no globals inside the module body, no threads -- the
 * ptguard/cwlate/chanhealth shape. The GLOBAL table lives in config.c
 * (the parser fills it); the inline predicates below read it. The unit
 * test defines its own table and drives it directly.
 *
 * THE TWO FLAGS:
 *   HALFNULLED -- the family's keys may legitimately be HALF-nulled (one
 *   8-byte half real, the other zero). Default {09} = NDS, stock's
 *   behaviour. (Half-nulled SEEDS for such a family get the CW0/CW1
 *   cycle marks; keys for other families are refused at the gate.)
 *   CARDPROV   -- card matching is provider-STRICT for these
 *   families (loadbalance.c). Default {01, 05} = stock's Seca/
 *   Viaccess rule, hard-coded there until TASK R39 (D98).
 *   NOPROVCHK  -- a profile matches a request WITHOUT the provider
 *   check. Default {18,09,0B} = Nagravision/NDS/Conax, stock's list
 *   at getcsbycaidprov() (main.c) -- the one site that ever used it.
 *   TASK R36 (D92) wired HALFNULLED; TASK R38 (D94) wired this flag
 *   at that same site, byte-identical by default. Card matching
 *   keeps its own inverse rule as the separate CARDPROV flag
 *   (D98): tightening IT to this list would refuse real cards
 *   whose announced providers differ, so the two questions keep
 *   two sets -- both now in this one table, both configurable.
 *
 * OWNERSHIP SEMANTICS (D71). cafamily_add() on a flag's FIRST family
 * clears that flag's defaults: the first `CAID FAMILY <FLAG>:` line owns
 * the complete set. Lines accumulate; an empty list means "none".
 * init_config() (config.c) resets the defaults at every startup.
 */
#ifndef MCS_CAFAMILY_H
#define MCS_CAFAMILY_H
#include <stdint.h>

/* Three flags (index into b[]): */
#define CAFAM_HALFNULLED 0
#define CAFAM_NOPROVCHK  1
#define CAFAM_CARDPROV   2

struct cafamily_tab {
	uint32_t b[3][8]; /* 3 x 256 bits */
	int owned[3];     /* first cafamily_add() on a flag drops its defaults */
};

/* Stock r82a defaults (D71): half-nulled keys for NDS only; the provider
 * check skipped for Nagravision/NDS/Viaccess. */
static void cafamily_init(struct cafamily_tab *t)
{
	int i;
	for (i = 0; i < 8; i++) { t->b[CAFAM_HALFNULLED][i] = 0; t->b[CAFAM_NOPROVCHK][i] = 0; }
	t->owned[CAFAM_HALFNULLED] = 0;
	t->owned[CAFAM_NOPROVCHK] = 0;
	t->owned[CAFAM_CARDPROV] = 0;
	/* HALFNULLED default: {09} -- stock r82a's hard-coded NDS rule.
	 * (A draft briefly shipped NONE here; the table had no parser and
	 * no callers then, so the change was inert. TASK R36 (D92) wired
	 * the parser and the three enforcement sites; the default stays
	 * stock, and the first `CAID FAMILY HALFNULLED:` line owns the
	 * set from there.) */
	/* NOPROVCHK default: 18, 09, 0B */
	t->b[CAFAM_HALFNULLED][0x09 >> 5] |= (uint32_t)1 << (0x09 & 31);
	/* CARDPROV default: {01, 05} -- stock's card-matching rule in
	 * loadbalance.c (provider-strict for Seca/Viaccess only). */
	t->b[CAFAM_CARDPROV][0x01 >> 5] |= (uint32_t)1 << (0x01 & 31);
	t->b[CAFAM_CARDPROV][0x05 >> 5] |= (uint32_t)1 << (0x05 & 31);
	t->b[CAFAM_NOPROVCHK][0x18 >> 5] |= (uint32_t)1 << (0x18 & 31);
	t->b[CAFAM_NOPROVCHK][0x09 >> 5] |= (uint32_t)1 << (0x09 & 31);
	t->b[CAFAM_NOPROVCHK][0x0B >> 5] |= (uint32_t)1 << (0x0B & 31);
}

static void cafamily_own(struct cafamily_tab *t, int flag)
{
	if (flag != CAFAM_HALFNULLED && flag != CAFAM_NOPROVCHK && flag != CAFAM_CARDPROV) return;
	if (!t->owned[flag]) {
		int i;
		for (i = 0; i < 8; i++) t->b[flag][i] = 0;
		t->owned[flag] = 1;
	}
}

static void cafamily_add(struct cafamily_tab *t, int flag, unsigned fam)
{
	if (flag != CAFAM_HALFNULLED && flag != CAFAM_NOPROVCHK && flag != CAFAM_CARDPROV) return;
	fam &= 0xFF;
	/* first configured family owns the set: drop the defaults */
	cafamily_own(t, flag);
	t->b[flag][fam >> 5] |= (uint32_t)1 << (fam & 31);
}

static int cafamily_has(const struct cafamily_tab *t, int flag, unsigned fam)
{
	if (flag != CAFAM_HALFNULLED && flag != CAFAM_NOPROVCHK && flag != CAFAM_CARDPROV) return 0;
	fam &= 0xFF;
	return (t->b[flag][fam >> 5] >> (fam & 31)) & 1;
}

/* The global table (defined in config.c; reset by init_config). */
extern struct cafamily_tab cafamily_tab;

/* Site-facing predicates: family = the caid's high byte. */
static int ca_halfnull_ok(unsigned caid)
{
	return cafamily_has(&cafamily_tab, CAFAM_HALFNULLED, caid >> 8);
}

static int ca_provchk_skip(unsigned caid)
{
	return cafamily_has(&cafamily_tab, CAFAM_NOPROVCHK, caid >> 8);
}

/* TASK R39 (D98): card matching (loadbalance.c) is provider-strict
 * only for these families -- stock's {01, 05}, now table-driven so
 * both provider rules live in one table. `CAID FAMILY
 * PROVIDERCHECK: 01, 05` owns this set like the other flags. */
static int ca_card_prov_required(unsigned caid)
{
	return cafamily_has(&cafamily_tab, CAFAM_CARDPROV, caid >> 8);
}

#endif /* MCS_CAFAMILY_H */
