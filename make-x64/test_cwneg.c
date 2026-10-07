/*
 * TASK 2.6 -- the unit suite for negative memory (../src/cwneg.h).
 *
 * What is pinned here, in order of how much it would hurt to get wrong:
 *
 *   1. The identity. The key digest + (CAID, PROVID, SID) is what makes a key
 *      remembered: the same bytes on another channel must NOT be suppressed
 *      (that would need their real keys to be byte-identical to a proven fake
 *      -- the absurdity the design leans on), and a different key on the same
 *      channel must not be suppressed either. Any drift here either poisons
 *      honest keys or re-delivers proven ones.
 *   2. The scope of the action. mark/hit are the ONLY things this file does;
 *      there is no score, no expiry, no source concept in the decision. The
 *      refusal line must say so in words.
 *   3. The bookkeeping. A fresh mark returns 1 and a re-proof returns 0 with
 *      the first proof kept as evidence; hits count per key; one line per key
 *      per window while the counters keep counting.
 *   4. The table. LRU eviction by last touch (marks AND hits are touches), a
 *      full table always evicts (never refuses the next proof), and the
 *      evicted key's return re-files it cleanly.
 *   5. The evidence. The slot remembers who proved it and when; the line
 *      shows the stored KEY PREFIX -- never the digest dressed up as the key.
 *   6. Degenerate inputs: no table, no key, no evidence struct, short buffer.
 */
#include "../src/cwneg.h"

#include <stdio.h>
#include <string.h>

static int passed = 0, failed = 0;

#define CHECK(cond, what) do { \
	if (cond) { passed++; printf("  [ ok ] %s\n", what); } \
	else { failed++; printf("  [FAIL] %s\n", what); } \
} while (0)

static void setkey(uint8_t *k, uint8_t seed)
{
	int i;
	for (i = 0; i < 16; i++) k[i] = (uint8_t)(seed + i * 7);
}

int main(void)
{
	struct cwneg_table t;
	struct cwneg_ev ev;
	char line[512];
	uint8_t k1[16], k2[16], k3[16];

	printf("test_cwneg\n");

	setkey(k1, 0x10);
	setkey(k2, 0x80);
	setkey(k3, 0xF0);

	/* -- 1. the digest ------------------------------------------------------ */
	{
		uint8_t a[16], b[16];
		memset(a, 0x55, 16); memset(b, 0x55, 16);
		CHECK(cwneg_digest(a) == cwneg_digest(b), "identical keys digest identically");
		b[15] ^= 0x01;
		CHECK(cwneg_digest(a) != cwneg_digest(b), "one flipped bit changes the digest");
		memset(a, 0, 16);
		CHECK(cwneg_digest(a) == cwneg_digest(a), "the digest is deterministic");
	}

	/* -- 2. mark, dupe, hit -------------------------------------------------- */
	cwneg_init(&t);
	CHECK(cwneg_mark(&t, k1, 0x0604, 0x000100, 0x0064, 1, 7, 1000) == 1,
	      "the first proof files as a fresh mark");
	CHECK(cwneg_mark(&t, k1, 0x0604, 0x000100, 0x0064, 1, 7, 2000) == 0,
	      "re-proving the same key is not a fresh mark");
	CHECK(cwneg_count(&t) == 1, "and it did not occupy a second slot");
	CHECK(cwneg_hit(&t, k1, 0x0604, 0x000100, 0x0064, 2100, &ev) == 1,
	      "the proven key is remembered");
	CHECK(ev.fired == 1 && ev.hits == 1 && ev.reports == 1,
	      "the first refusal fires its line");
	CHECK(ev.origin_type == 1 && ev.origin_id == 7,
	      "the evidence remembers WHO proved the key");
	CHECK(ev.age_ms == 1100, "the evidence remembers WHEN the proof was");

	CHECK(cwneg_hit(&t, k1, 0x0604, 0x000100, 0x0064, 2200, &ev) == 1,
	      "the second offer is remembered too");
	CHECK(ev.fired == 0 && ev.hits == 2 && ev.reports == 1,
	      "and throttled: counted, no second line inside the window");

	/* -- 3. the scope: (key, CAID, PROVID), never the SID ---------------------
	 * Found live by the nt target's first run: a SID-scoped memory let the
	 * same poison open a second service (the SID changes with every zap, the
	 * poison does not). One transponder shares one CW stream, so a key that
	 * failed on one service is dead on all of them. */
	CHECK(cwneg_hit(&t, k1, 0x0604, 0x000100, 0x0065, 2300, NULL) == 1,
	      "the same bytes on another SID of the provider ARE suppressed");
	CHECK(cwneg_hit(&t, k1, 0x0604, 0x000100, 0xFFFF, 2300, NULL) == 1,
	      "any SID of the same provider: suppressed");
	CHECK(cwneg_hit(&t, k1, 0x0604, 0x000200, 0x0064, 2300, NULL) == 0,
	      "another provider is a different memory");
	CHECK(cwneg_hit(&t, k1, 0x0B00, 0x000100, 0x0064, 2300, NULL) == 0,
	      "another CAID is a different memory");
	CHECK(cwneg_hit(&t, k2, 0x0604, 0x000100, 0x0064, 2300, NULL) == 0,
	      "a different key on the same channel is not suppressed");
	{
		uint8_t k1b[16];
		memcpy(k1b, k1, 16);
		k1b[5] ^= 0x40;
		CHECK(cwneg_hit(&t, k1b, 0x0604, 0x000100, 0x0064, 2300, NULL) == 0,
		      "one edited byte makes it a different key, not a hit");
	}

	/* -- 4. window roll ------------------------------------------------------
	 * The window counts from the last LINE (rep_ticks = 2100), not from the
	 * mark: one line per key per window, however many refusals happen. */
	CHECK(cwneg_hit(&t, k1, 0x0604, 0x000100, 0x0064, 2100 + CWNEG_WINDOW, &ev) == 1,
	      "a refusal after the window has rolled fires again");
	CHECK(ev.fired == 1 && ev.reports == 2 && ev.hits == 5,
	      "the counters carried over: 5 refusals, 2 lines");

	/* -- 5. eviction is LRU by last touch ------------------------------------ */
	{
		uint8_t k[16];
		int i;
		cwneg_init(&t);
		for (i = 0; i < CWNEG_SLOTS; i++) {
			setkey(k, (uint8_t)(0x10 + i));
			CHECK(cwneg_mark(&t, k, 0x0604, 1, (uint16_t)(0x100 + i), 1, 7, 1000 + (uint32_t)i) == 1,
			      "a proof files");
		}
		CHECK(cwneg_count(&t) == CWNEG_SLOTS, "the table filled");
		setkey(k, 0x10);   /* slot 0, oldest mark, never touched since */
		cwneg_hit(&t, k, 0x0604, 1, 0x100, 5000, NULL);   /* touch it: becomes newest */
		{
			uint8_t kn[16];
			setkey(kn, 0xEE);
			CHECK(cwneg_mark(&t, kn, 0x0604, 1, 0x9999, 1, 7, 6000) == 1,
			      "a new proof fits a full table (eviction, never refusal)");
		}
		CHECK(cwneg_hit(&t, k, 0x0604, 1, 0x100, 6100, NULL) == 1,
		      "the touched key survived the eviction");
		{
			uint8_t k1s[16];
			setkey(k1s, 0x11);   /* slot 1: oldest untouched now */
			CHECK(cwneg_hit(&t, k1s, 0x0604, 1, 0x101, 6200, NULL) == 0,
			      "the least recently touched key was the one evicted");
			CHECK(cwneg_mark(&t, k1s, 0x0604, 1, 0x101, 1, 9, 6300) == 1,
			      "and it re-files cleanly when its proof comes again");
		}
	}

	/* -- 6. degenerate inputs -------------------------------------------------- */
	cwneg_init(&t);
	CHECK(cwneg_mark(NULL, k1, 1, 1, 1, 1, 1, 1) == 0, "no table: no mark");
	CHECK(cwneg_mark(&t, NULL, 1, 1, 1, 1, 1, 1) == 0, "no key: no mark");
	CHECK(cwneg_hit(NULL, k1, 1, 1, 1, 1, NULL) == 0, "no table: no hit");
	CHECK(cwneg_hit(&t, NULL, 1, 1, 1, 1, NULL) == 0, "no key: no hit");
	CHECK(cwneg_hit(&t, k1, 1, 1, 1, 1, NULL) == 0, "an unknown key is no hit, quietly");
	CHECK(cwneg_count(NULL) == 0, "count of no table is 0");
	{
		uint8_t z[16];
		memset(z, 0, 16);
		CHECK(cwneg_mark(&t, z, 0, 0, 0, 1, 1, 1000) == 1,
		      "an all-zero key can be filed (it is a key like any other)");
		CHECK(cwneg_hit(&t, z, 0, 0, 0, 1100, NULL) == 1,
		      "and it is remembered");
	}

	/* -- 7. the sentence -------------------------------------------------------- */
	cwneg_init(&t);
	{
		int n;
		setkey(k3, 0x42);
		cwneg_mark(&t, k3, 0x0B00, 0x000331, 0x233D, 1, 42, 1000);
		cwneg_hit(&t, k3, 0x0B00, 0x000331, 0x233D, 26000, &ev);
		n = cwneg_format(&ev, line, (int)sizeof(line));
		CHECK(n > 0, "the sentence renders");
		CHECK(strstr(line, "CW NEGATIVE:") == line, "it names the layer");
		CHECK(strstr(line, "0b00:000331:233d") != NULL, "it names the channel");
		CHECK(strstr(line, "was proven unable to open its picture") != NULL,
		      "it states what the proof established");
		CHECK(strstr(line, "proven on source 1/42") != NULL, "it names who proved it");
		CHECK(strstr(line, "25 s ago") != NULL, "it says how long ago");
		CHECK(strstr(line, "refused before any client") != NULL,
		      "it states where the refusal happened");
		CHECK(strstr(line, "hit #1") != NULL, "it numbers the refusal");
		CHECK(strstr(line, "not a score: nothing was scored and no source was disabled") != NULL,
		      "it says in words that no score moved and no source was disabled");
		/* the line shows the stored key PREFIX, not the digest */
		{
			char want[17];
			int i;
			static const char hx[] = "0123456789ABCDEF";
			for (i = 0; i < 8; i++) {
				want[i * 2]     = hx[k3[i] >> 4];
				want[i * 2 + 1] = hx[k3[i] & 0x0F];
			}
			want[16] = 0;
			CHECK(strstr(line, want) != NULL, "it shows the refused key's own first bytes");
		}
		n = cwneg_format(&ev, line, 24);
		CHECK(n >= 0 && strlen(line) < 24, "it survives a short buffer, terminated");
	}

	printf("  ----\n");
	printf("  %d ok, %d failed\n", passed, failed);
	return failed ? 1 : 0;
}
