/*
 * R48 D-4 -- cwconsensus quarantine / storm / freeze / independence unit.
 * Covers the opt-in second-degree guards that test_cwconsensus does not:
 * channel + provider quarantine, the 10-minute sliding window (regression:
 * a stale second conflict must not combine with a fresh one), rotation
 * freeze, the 20 keys/sec storm guard, and same-IP independence.
 * All guards default OFF; each section sets its flags explicitly because
 * cwconsensus_init() does not reset them.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "../src/cwconsensus.h"

static int fails = 0, checks = 0;
static void chk(int ok, const char *what, long got)
{
	checks++;
	if (!ok) { fails++; printf("  [FAIL] %s (got %ld)\n", what, got); }
	else printf("  [ ok ] %s\n", what);
}

static const uint8_t K1[16] = {0x10,0x20,0x30,0x60,0x40,0x50,0x60,0xF0,0x70,0x80,0x90,0x80,0xA0,0xB0,0xC0,0x10};
static const uint8_t K2[16] = {0x10,0x30,0x20,0x60,0x40,0x50,0x60,0xF0,0x70,0x80,0x90,0x80,0xA0,0xB0,0xC0,0x10};

/* One weighted conflict: clean key holds, distrusted key loses on weight. */
static int one_conflict(uint16_t caid, uint16_t sid, uint32_t provid, int known,
			uint32_t hash, uint32_t now)
{
	const uint8_t *loser = NULL;
	int v;
	v = cwconsensus_verdict(caid, sid, provid, known, hash, 0x80, K1, 101, 1, now, 5000, &loser);
	if (v != CWC_HOLD) return -1;
	v = cwconsensus_verdict(caid, sid, provid, known, hash, 0x80, K2, 202, 0, now + 50, 5000, &loser);
	if (v != CWC_REFUSE) return -2;
	return 0;
}

static void flags_off(void)
{
	cwconsensus_set_independence(0);
	cwconsensus_set_quarantine(0);
	cwconsensus_set_storm_guard(0);
	cwconsensus_set_rotation_freeze(0);
	cwconsensus_set_min_sources(2);
}

int main(void)
{
	const uint8_t *loser = NULL;
	int v;

	printf("== cwconsensus D-4 (quarantine/storm/freeze/independence) ==\n");

	/* -- 1. OFF defaults: two conflicts do not quarantine -- */
	cwconsensus_init(); flags_off();
	chk(one_conflict(0x1884, 0x64, 0x111111, 1, 0xA0000001, 1000) == 0, "conflict 1 refused", 0);
	chk(one_conflict(0x1884, 0x64, 0x111111, 1, 0xA0000002, 2000) == 0, "conflict 2 refused", 0);
	chk(cwconsensus_channel_quarantined(0x1884, 0x64, 0x111111, 1, 3000) == 0,
	    "OFF: channel not quarantined", 0);

	/* -- 2. channel quarantine: 2 conflicts/10min -> 60s hold -- */
	cwconsensus_init(); flags_off();
	cwconsensus_set_quarantine(1);
	chk(one_conflict(0x1884, 0x64, 0x111111, 1, 0xA0000001, 1000) == 0, "conflict 1 refused", 0);
	chk(cwconsensus_channel_quarantined(0x1884, 0x64, 0x111111, 1, 1500) == 0,
	    "one conflict does not quarantine", 0);
	chk(one_conflict(0x1884, 0x64, 0x111111, 1, 0xA0000002, 2000) == 0, "conflict 2 refused", 0);
	chk(cwconsensus_channel_quarantined(0x1884, 0x64, 0x111111, 1, 3000) == 1,
	    "two conflicts quarantine the channel", 0);
	v = cwconsensus_verdict(0x1884, 0x64, 0x111111, 1, 0xA0000009, 0x80, K1, 101, 1, 3000, 5000, &loser);
	chk(v == CWC_HOLD, "quarantined verdict holds", v);
	chk(cwconsensus_channel_quarantined(0x1884, 0x65, 0x999999, 1, 3000) == 0,
	    "other provider not quarantined", 0);
	chk(cwconsensus_channel_quarantined(0x1884, 0x64, 0x111111, 1, 63000) == 0,
	    "quarantine expires after 60s", 0);

	/* -- 3. sliding window regression: stale conflicts must expire -- */
	/* Conflicts at 1000/2000 quarantined until 62000. A single fresh
	 * conflict at 700000 (both stale, >600s) must NOT re-quarantine;
	 * the old single-shift bug quarantined here. */
	chk(one_conflict(0x1884, 0x64, 0x111111, 1, 0xA0000003, 700000) == 0, "fresh conflict refused", 0);
	chk(cwconsensus_channel_quarantined(0x1884, 0x64, 0x111111, 1, 700100) == 0,
	    "single fresh conflict after window does not quarantine", 0);
	chk(one_conflict(0x1884, 0x64, 0x111111, 1, 0xA0000004, 701000) == 0, "second fresh conflict refused", 0);
	chk(cwconsensus_channel_quarantined(0x1884, 0x64, 0x111111, 1, 701100) == 1,
	    "two fresh conflicts quarantine again", 0);

	/* -- 4. provider scope: conflicts across channels quarantine provider -- */
	cwconsensus_init(); flags_off();
	cwconsensus_set_quarantine(1);
	chk(one_conflict(0x1884, 0x64, 0x222222, 1, 0xB0000001, 1000) == 0, "provider conflict 1", 0);
	chk(one_conflict(0x1884, 0x65, 0x222222, 1, 0xB0000002, 2000) == 0, "provider conflict 2", 0);
	chk(cwconsensus_channel_quarantined(0x1884, 0x66, 0x222222, 1, 3000) == 1,
	    "third channel quarantined via provider scope", 0);
	chk(cwconsensus_channel_quarantined(0x1884, 0x66, 0x333333, 1, 3000) == 0,
	    "other provider untouched", 0);

	/* -- 5. rotation freeze: new hash clears prior conflicts -- */
	cwconsensus_init(); flags_off();
	cwconsensus_set_quarantine(1);
	cwconsensus_set_rotation_freeze(1);
	chk(one_conflict(0x1884, 0x64, 0x111111, 1, 0xC0000001, 1000) == 0, "freeze conflict 1", 0);
	chk(one_conflict(0x1884, 0x64, 0x111111, 1, 0xC0000002, 2000) == 0, "freeze conflict 2 (new hash)", 0);
	chk(cwconsensus_channel_quarantined(0x1884, 0x64, 0x111111, 1, 3000) == 0,
	    "rotation freeze prevents quarantine", 0);

	/* -- 6. storm guard: 21st key in 1s holds -- */
	cwconsensus_init(); flags_off();
	cwconsensus_set_storm_guard(1);
	{
		int i, last = -1, prev = -1;
		for (i = 0; i < 20; i++) {
			int peer = (i % 2) ? 102 : 101;
			prev = cwconsensus_verdict(0x1884, 0x64, 0x111111, 1, 0xD0000001, 0x80,
						   K1, peer, 1, 1000 + i, 5000, &loser);
		}
		chk(prev == CWC_DELIVER, "20th key delivers", prev);
		last = cwconsensus_verdict(0x1884, 0x64, 0x111111, 1, 0xD0000001, 0x80,
					   K1, 101, 1, 1020, 5000, &loser);
		chk(last == CWC_HOLD, "21st key in 1s held by storm guard", last);
		v = cwconsensus_verdict(0x1884, 0x64, 0x111111, 1, 0xD0000001, 0x80,
					K1, 101, 1, 2500, 5000, &loser);
		chk(v == CWC_DELIVER, "window reset delivers again", v);
	}

	/* -- 7. independence: same IP counts once -- */
	cwconsensus_init(); flags_off();
	cwconsensus_set_independence(1);
	v = cwconsensus_verdict_group(0x1884, 0x64, 0x111111, 1, 0xE0000001, 0x80,
				      K1, 101, 0x0A000001u, 1, 1000, 5000, &loser);
	chk(v == CWC_HOLD, "first group holds", v);
	v = cwconsensus_verdict_group(0x1884, 0x64, 0x111111, 1, 0xE0000001, 0x80,
				      K1, 102, 0x0A000001u, 1, 1100, 5000, &loser);
	chk(v == CWC_HOLD, "same group still one vote", v);
	v = cwconsensus_verdict_group(0x1884, 0x64, 0x111111, 1, 0xE0000001, 0x80,
				      K1, 103, 0x0A000002u, 1, 1200, 5000, &loser);
	chk(v == CWC_DELIVER, "second group delivers", v);

	printf("== %d/%d passed, %d failed ==\n", checks - fails, checks, fails);
	return fails ? 1 : 0;
}
