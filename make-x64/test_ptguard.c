/*
 * TASK R14 (D69) -- the unit suite for the pollution guard (../src/ptguard.h).
 *
 * What is pinned here, in order of how much it would hurt to get wrong:
 *
 *   1. The three decisions. OFF is stock (every site allows before any
 *      question is asked); armed + window closed = the brake; armed + window
 *      open = the probe path, which is the recovery path. The vote veto is
 *      window-blind: a below-line source's verdict weighs nothing while the
 *      guard is armed, whatever the suppression clock says.
 *   2. The line budget (GR8/GR9): one evidence sentence per (source, CAID)
 *      per PTG_LINE_WINDOW_MS, per-source and per-CAID independent, the
 *      table survives eviction (oldest slot loses, never the busiest), and a
 *      window that has run out lets the sentence through again.
 *   3. The two sentences (GR8): they name the channel, the source and the
 *      key's own bytes, and they say what the brake is NOT -- no score, no
 *      disconnect -- so a refusal can never be misread as a punishment.
 *   4. Degenerate inputs: NULLs, zero-length buffers, a NULL key.
 */
#include "../src/ptguard.h"

#include <stdio.h>
#include <string.h>

/* the table's source-type key is opaque to the module -- any int works */
#define SRC_T 1

static int passed = 0, failed = 0;

#define CHECK(cond, what) do { \
	if (cond) { passed++; printf("  [ ok ] %s\n", what); } \
	else { failed++; printf("  [FAIL] %s\n", what); } \
} while (0)

int main(void)
{
	struct ptg_linetab tab;
	uint8_t k1[16], k2[16];
	char line[512];
	int i, n;

	printf("ptguard: the pollution guard's policy (R14)\n");

	for (i = 0; i < 16; i++) { k1[i] = (uint8_t)(0xA0 + i); k2[i] = (uint8_t)(0x10 + i); }

	/* --- 1. the three decisions ----------------------------------------- */
	CHECK(ptg_store_allow(0, 0) == 1, "OFF is stock: a closed window means nothing when the guard is not armed");
	CHECK(ptg_store_allow(0, 1) == 1, "OFF is stock: an open window allows too");
	CHECK(ptg_store_allow(1, 0) == 0, "armed + window closed: the push is refused at ingest");
	CHECK(ptg_store_allow(1, 1) == 1, "armed + window open: the probe push gets its chance (the recovery path)");
	CHECK(ptg_relay_allow(0, 0) == 1, "OFF is stock at the relay site as well");
	CHECK(ptg_relay_allow(1, 0) == 0, "armed + window closed: the stored key is not re-pumped to peers");
	CHECK(ptg_relay_allow(1, 1) == 1, "armed + window open: an open source's keys circulate");
	CHECK(ptg_vote_weight(0, 1, 3) == 3, "OFF: the vote keeps its stock weight, even below the line");
	CHECK(ptg_vote_weight(1, 1, 3) == 0, "armed + below the line: the vote weighs nothing -- it mints no truth");
	CHECK(ptg_vote_weight(1, 0, 3) == 3, "armed + above the line: full weight");
	CHECK(ptg_vote_weight(1, 1, 0) == 0, "a zeroed vote stays zeroed");

	/* --- 2. the line budget ---------------------------------------------- */
	ptg_line_init(&tab);
	CHECK(ptg_line_due(&tab, PTG_SITE_STORE, SRC_T, 7, 0x1884, 1000) == 1,
	      "the first refusal of a (source, caid) writes its sentence");
	CHECK(ptg_line_due(&tab, PTG_SITE_STORE, SRC_T, 7, 0x1884, 2000) == 0,
	      "a repeat inside the window is counted by the rows and stays silent here");
	CHECK(ptg_line_due(&tab, PTG_SITE_STORE, SRC_T, 7, 0x1884, 1000 + PTG_LINE_WINDOW_MS - 1) == 0,
	      "still inside the window at its last millisecond");
	CHECK(ptg_line_due(&tab, PTG_SITE_STORE, SRC_T, 7, 0x1884, 1000 + PTG_LINE_WINDOW_MS) == 1,
	      "the window that ran out reopens the budget");
	CHECK(ptg_line_due(&tab, PTG_SITE_STORE, SRC_T, 8, 0x1884, 1500) == 1,
	      "a different source has its own budget");
	CHECK(ptg_line_due(&tab, PTG_SITE_STORE, SRC_T, 7, 0x1885, 1500) == 1,
	      "and so does a different caid of the same source (GR4)");
	CHECK(ptg_line_due(&tab, PTG_SITE_RELAY, SRC_T, 7, 0x1884, 1600) == 1,
	      "the two SITES have separate budgets: a mesh withhold never silences the store sentence");
	CHECK(ptg_line_due(&tab, PTG_SITE_RELAY, SRC_T, 7, 0x1884, 1700) == 0,
	      "(the relay site throttles itself, exactly like the store one)");
	CHECK(ptg_line_due(&tab, PTG_SITE_STORE, SRC_T, 7, 0x1884, 3000) == 0,
	      "the original slot is untouched by its neighbours' writes");

	/* eviction: fill the table, one source re-stamps (busy), then one new key
	 * evicts the OLDEST stamp -- the quiet old neighbour, not the noisy one */
	{
		uint32_t t0 = 10000;
		uint32_t later = t0 + PTG_LINE_WINDOW_MS + 1;
		ptg_line_init(&tab);
		for (i = 0; i < PTG_LINE_SLOTS; i++)
			CHECK(ptg_line_due(&tab, PTG_SITE_STORE, SRC_T, 100 + i, 0x1884, t0 + (uint32_t)i) == 1,
			      "a fresh slot takes its sentence");
		CHECK(ptg_line_due(&tab, PTG_SITE_STORE, SRC_T, 100, 0x1884, t0 + 500) == 0,
		      "source 100 is inside its window: quiet");
		CHECK(ptg_line_due(&tab, PTG_SITE_STORE, SRC_T, 100, 0x1884, later) == 1,
		      "its window ran out: the busy source speaks again and re-stamps");
		CHECK(ptg_line_due(&tab, PTG_SITE_STORE, SRC_T, 999, 0x1884, later + 1) == 1,
		      "one past the table size: the new key still gets through (a slot is always made)");
		CHECK(ptg_line_due(&tab, PTG_SITE_STORE, SRC_T, 100, 0x1884, later + 2) == 0,
		      "the busy source survived -- its stamp is the newest, not the oldest");
		CHECK(ptg_line_due(&tab, PTG_SITE_STORE, SRC_T, 101, 0x1884, later + 3) == 1,
		      "the quiet old neighbour lost its slot: its stamp is gone, so it speaks again");
	}

	/* --- 3. the two sentences -------------------------------------------- */
	n = ptg_format_store(line, (int)sizeof(line), 7, 0x1884, 0x112233, 0x0064, k1);
	CHECK(n > 0, "the store sentence builds");
	CHECK(strstr(line, "TRUST STORE") != NULL, "it is named for the site it came from");
	CHECK(strstr(line, "1884:112233:0064") != NULL, "it names the channel the way every layer does");
	CHECK(strstr(line, "source 7") != NULL, "it names the source");
	CHECK(strstr(line, "A0A1A2A3A4A5A6A7A8A9AAABACADAEAF") != NULL,
	      "it carries the key's own bytes, to match against a /cwlog row");
	CHECK(strstr(line, "below the trust line") != NULL, "it says why");
	CHECK(strstr(line, "no score, no disconnect") != NULL,
	      "and it says what the brake is NOT -- a refusal is not a punishment");
	n = ptg_format_relay(line, (int)sizeof(line), 7, 0x1884, 0x112233, 0x0064, k2);
	CHECK(n > 0, "the relay sentence builds");
	CHECK(strstr(line, "TRUST RELAY") != NULL, "it is named for its own site");
	CHECK(strstr(line, "withheld from the mesh") != NULL, "it says what happened to the key");
	CHECK(strstr(line, "101112131415161718191A1B1C1D1E1F") != NULL, "and carries these bytes too");
	CHECK(strstr(line, "recorded source 7") != NULL, "it names the key's recorded source, not the asking peer");
	CHECK(strstr(line, "re-vouch") != NULL, "it says how circulation reopens -- the innocent peer's path back");

	/* --- 4. degenerate inputs -------------------------------------------- */
	CHECK(ptg_format_store(NULL, 64, 1, 1, 1, 1, k1) < 0, "NULL buffer is refused");
	CHECK(ptg_format_store(line, 0, 1, 1, 1, 1, k1) < 0, "a zero buffer is refused");
	CHECK(ptg_format_relay(NULL, 64, 1, 1, 1, 1, k1) < 0, "NULL buffer is refused at the relay format too");
	n = ptg_format_store(line, (int)sizeof(line), 1, 1, 1, 1, NULL);
	CHECK(n > 0 && strstr(line, "key ") != NULL, "a NULL key still builds an honest sentence");
	n = ptg_format_store(line, 32, 7, 0x1884, 0x112233, 0x0064, k1);
	CHECK(n >= 0 && strlen(line) < 32, "a short buffer truncates safely and terminates");

	printf("  %d ok, %d failed\n", passed, failed);
	return failed ? 1 : 0;
}
