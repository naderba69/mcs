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
 *   NOPROVCHK  -- a card of this family matches a request WITHOUT the
 *   provider check. Default {18,09,0B} = Nagravision/NDS/Viaccess,
 *   stock's list.
 *
 * OWNERSHIP SEMANTICS (D71). cafamily_add() on a flag's FIRST family
 * clears that flag's defaults: the first `CAID FAMILY <FLAG>:` line owns
 * the complete set. Lines accumulate; an empty list means "none".
 * init_config() (config.c) resets the defaults at every startup.
 */
#ifndef MCS_CAFAMILY_H
#define MCS_CAFAMILY_H
#include <stdint.h>

/* Two flags (index into b[]): */
#define CAFAM_HALFNULLED 0
#define CAFAM_NOPROVCHK  1

struct cafamily_tab {
	uint32_t b[2][8]; /* 2 x 256 bits */
	int owned[2];     /* first cafamily_add() on a flag drops its defaults */
};

/* Stock r82a defaults (D71): half-nulled keys for NDS only; the provider
 * check skipped for Nagravision/NDS/Viaccess. */
static void cafamily_init(struct cafamily_tab *t)
{
	int i;
	for (i = 0; i < 8; i++) { t->b[CAFAM_HALFNULLED][i] = 0; t->b[CAFAM_NOPROVCHK][i] = 0; }
	t->owned[CAFAM_HALFNULLED] = 0;
	t->owned[CAFAM_NOPROVCHK] = 0;
	/* HALFNULLED default: NONE (removed 09 to reject all half-nulled CW) */
	/* Users can add specific CAIDs via CAID FAMILY HALFNULLED in config */
	/* NOPROVCHK default: 18, 09, 0B */
	t->b[CAFAM_NOPROVCHK][0x18 >> 5] |= (uint32_t)1 << (0x18 & 31);
	t->b[CAFAM_NOPROVCHK][0x09 >> 5] |= (uint32_t)1 << (0x09 & 31);
	t->b[CAFAM_NOPROVCHK][0x0B >> 5] |= (uint32_t)1 << (0x0B & 31);
}

static void cafamily_own(struct cafamily_tab *t, int flag)
{
	if (flag != CAFAM_HALFNULLED && flag != CAFAM_NOPROVCHK) return;
	if (!t->owned[flag]) {
		int i;
		for (i = 0; i < 8; i++) t->b[flag][i] = 0;
		t->owned[flag] = 1;
	}
}

static void cafamily_add(struct cafamily_tab *t, int flag, unsigned fam)
{
	if (flag != CAFAM_HALFNULLED && flag != CAFAM_NOPROVCHK) return;
	fam &= 0xFF;
	/* first configured family owns the set: drop the defaults */
	cafamily_own(t, flag);
	t->b[flag][fam >> 5] |= (uint32_t)1 << (fam & 31);
}

static int cafamily_has(const struct cafamily_tab *t, int flag, unsigned fam)
{
	if (flag != CAFAM_HALFNULLED && flag != CAFAM_NOPROVCHK) return 0;
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

#endif /* MCS_CAFAMILY_H */
