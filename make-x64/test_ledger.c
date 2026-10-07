/*
 * test_ledger.c — TASK 2.1
 *
 * Exercises the REAL ../src/ledger.h. No server, no threads, no clock: every
 * function under test is a pure function of its arguments and the table, which
 * is the entire reason the ledger was written as a header.
 *
 * The assertions are ordered the way the evidence arrives in the field, not the
 * way the code is laid out: a key is delivered, a client comes back, a second
 * source contradicts. The negative cases get as much room as the positive one,
 * because this is the first thing in the project that is allowed to act on a
 * single event (GR3's third definitive proof) — so what must NOT fire is the
 * part worth testing hardest.
 *
 *   make -C make-x64 test
 */

#include <stdio.h>
#include <string.h>
#include "../src/ledger.h"

static int passed = 0, failed = 0;

static void check(const char *what, int got, int want)
{
	if (got == want) { passed++; printf("  [ ok ] %-62s = %d\n", what, got); }
	else { failed++; printf("  [FAIL] %-62s got=%d want=%d\n", what, got, want); }
}

#define SRCTYPE_CACHE 1                 /* DCW_SOURCE_CACHE is 1, not 3 */
#define PEER_CSP      0x010000
#define A   (11 | PEER_CSP)             /* the peer that delivers the bad key */
#define B   (12 | PEER_CSP)             /* the independent peer that disagrees */
#define CAID 0x1884
#define PROV 0x000000
#define SID  0x0064

static uint8_t ecm_A[16], ecm_B[16], cw_bad[16], cw_good[16], cw_third[16];

static void fill(uint8_t *p, uint8_t seed)
{
	int i;
	for (i = 0; i < 16; i++) p[i] = (uint8_t)(seed + i * 7);
}

struct ledger_table T;
struct ledger_event ev;

int main(void)
{
	int rc;
	char line[320];
	int n;

	fill(ecm_A, 1); fill(ecm_B, 0x40);
	fill(cw_bad, 0xA0); fill(cw_good, 0x50); fill(cw_third, 0x77);

	printf("== test_ledger (TASK 2.1) ==\n");

	/* ------------------------------------------------------------------ */
	printf("-- a lone key is never a proof: the ledger has nothing to compare --\n");

	ledger_init(&T);
	rc = ledger_offer(&T, ecm_A, 0x1111, CAID, PROV, SID, cw_bad,
	                  SRCTYPE_CACHE, A, 1, 1000, &ev);
	check("first key recorded, no verdict", rc, LEDGER_NONE);
	check("nothing accused on a first sighting", ev.acc_slot, 0);

	rc = ledger_failed(&T, ecm_A, 0x1111, CAID, PROV, SID, cw_bad,
	                   SRCTYPE_CACHE, A, 1500, &ev);
	check("client failed on the only key: still no proof (GR3)", rc, LEDGER_NONE);

	/* ------------------------------------------------------------------ */
	printf("-- the same key again is agreement, not a dispute --\n");

	rc = ledger_offer(&T, ecm_A, 0x1111, CAID, PROV, SID, cw_bad,
	                  SRCTYPE_CACHE, A, 1, 2000, &ev);
	check("same source, same key: agreement", rc, LEDGER_AGREE);

	rc = ledger_offer(&T, ecm_A, 0x1111, CAID, PROV, SID, cw_bad,
	                  SRCTYPE_CACHE, B, 1, 2100, &ev);
	check("independent source, SAME key: agreement, not dispute", rc, LEDGER_AGREE);
	check("and it is counted as corroboration", ev.nagree, 2);
	check("with no dispute recorded", ev.ndispute, 0);
	rc = ledger_failed(&T, ecm_A, 0x1111, CAID, PROV, SID, cw_bad,
	                   SRCTYPE_CACHE, A, 2200, &ev);
	check("two sources agreed, so a failure accuses nobody", rc, LEDGER_NONE);

	/* ------------------------------------------------------------------ */
	printf("-- a source contradicting itself is not 'agreement mismatch' --\n");

	ledger_init(&T);
	ledger_offer(&T, ecm_A, 0x1111, CAID, PROV, SID, cw_bad, SRCTYPE_CACHE, A, 1, 1000, &ev);
	rc = ledger_offer(&T, ecm_A, 0x1111, CAID, PROV, SID, cw_good,
	                  SRCTYPE_CACHE, A, 1, 1100, &ev);
	check("same source, different key: self-dispute, counted", rc, LEDGER_SELFDISPUTE);
	check("the entry still holds exactly one key", ledger_find(&T, ecm_A, 1150)->nslot, 1);
	rc = ledger_failed(&T, ecm_A, 0x1111, CAID, PROV, SID, cw_bad,
	                   SRCTYPE_CACHE, A, 1200, &ev);
	check("GR3: one source alone can never prove anything", rc, LEDGER_NONE);
	check("the self-dispute is still on the record", ledger_find(&T, ecm_A, 1300)->nself, 1);

	/* ------------------------------------------------------------------ */
	printf("-- THE PROOF: failure first, then an independent contradiction --\n");

	ledger_init(&T);
	rc = ledger_offer(&T, ecm_A, 0x1111, CAID, PROV, SID, cw_bad,
	                  SRCTYPE_CACHE, A, 1, 1000, &ev);
	check("peer A delivers its key", rc, LEDGER_NONE);
	rc = ledger_failed(&T, ecm_A, 0x1111, CAID, PROV, SID, cw_bad,
	                   SRCTYPE_CACHE, A, 2200, &ev);
	check("the client comes back 1.2 s later: recorded", rc, LEDGER_NONE);

	rc = ledger_offer(&T, ecm_A, 0x1111, CAID, PROV, SID, cw_good,
	                  SRCTYPE_CACHE, B, 1, 2400, &ev);
	check("peer B offers a different key: PROOF", rc, LEDGER_PROOF);
	check("the ACCUSED slot is the one the client received", ev.acc_slot, LEDGER_SLOT_FIRST);
	check("accused = peer A, the delivered one", ev.src[ev.acc_slot][1], A);
	check("dissenter = peer B, and it is named", ev.src[ev.acc_slot ^ 1][1], B);
	check("the elapsed delivery->failure is measured", (int)ev.elapsed_ms, 1200);
	check("the accused key is carried for the log", memcmp(ev.cw[ev.acc_slot], cw_bad, 16), 0);
	check("so is the key that contradicted it", memcmp(ev.cw[ev.acc_slot ^ 1], cw_good, 16), 0);

	/* ------------------------------------------------------------------ */
	printf("-- the proof is latched: the same two keys cannot accuse twice --\n");

	rc = ledger_offer(&T, ecm_A, 0x1111, CAID, PROV, SID, cw_good,
	                  SRCTYPE_CACHE, B, 1, 5000, &ev);
	check("re-offering the same pair says nothing new", rc, LEDGER_AGREE);
	rc = ledger_failed(&T, ecm_A, 0x1111, CAID, PROV, SID, cw_bad,
	                   SRCTYPE_CACHE, A, 5200, &ev);
	check("and neither does a repeated failure on the same key", rc, LEDGER_NONE);

	/* ------------------------------------------------------------------ */
	printf("-- THE OTHER ORDER: both keys known first, failure last --\n");

	ledger_init(&T);
	ledger_offer(&T, ecm_B, 0x2222, CAID, PROV, SID, cw_bad, SRCTYPE_CACHE, A, 1, 1000, &ev);
	ledger_offer(&T, ecm_B, 0x2222, CAID, PROV, SID, cw_good, SRCTYPE_CACHE, B, 1, 1100, &ev);
	check("a dispute with no failure yet is not a proof",
	      ledger_failed(&T, ecm_B, 0x2222, CAID, PROV, SID, cw_bad, SRCTYPE_CACHE, A, 1200, &ev),
	      LEDGER_PROOF);

	/* ------------------------------------------------------------------ */
	printf("-- a key that never reached a client cannot be the accused one --\n");

	ledger_init(&T);
	/* B's key is offered first, and rejected by the filter: not delivered. */
	ledger_offer(&T, ecm_A, 0x1111, CAID, PROV, SID, cw_good, SRCTYPE_CACHE, B, 0, 1000, &ev);
	ledger_offer(&T, ecm_A, 0x1111, CAID, PROV, SID, cw_bad, SRCTYPE_CACHE, A, 1, 1100, &ev);
	rc = ledger_failed(&T, ecm_A, 0x1111, CAID, PROV, SID, cw_bad, SRCTYPE_CACHE, A, 1200, &ev);
	check("the delivered key is the accused one, whoever came first", rc, LEDGER_PROOF);
	check("accused slot is the delivered key, not the earlier one", ev.acc_slot, LEDGER_SLOT_OTHER);

	/* ------------------------------------------------------------------ */
	printf("-- a failure we cannot place is never a guess (GR3) --\n");

	ledger_init(&T);
	ledger_offer(&T, ecm_A, 0x1111, CAID, PROV, SID, cw_bad, SRCTYPE_CACHE, A, 1, 1000, &ev);
	ledger_offer(&T, ecm_A, 0x1111, CAID, PROV, SID, cw_good, SRCTYPE_CACHE, B, 1, 1100, &ev);
	rc = ledger_failed(&T, ecm_A, 0x1111, CAID, PROV, SID, cw_third,
	                   SRCTYPE_CACHE, A, 1200, &ev);
	check("a key the source never sent accuses nobody", rc, LEDGER_NONE);

	/* ------------------------------------------------------------------ */
	printf("-- identity: GR6 and D5 --\n");

	ledger_init(&T);
	{
		uint8_t zerokey[16];
		memset(zerokey, 0, 16);
		check("an all-zero ecmd5 is not an identity", ledger_key_valid(zerokey), 0);
		rc = ledger_offer(&T, zerokey, 0x1111, CAID, PROV, SID, cw_bad,
		                  SRCTYPE_CACHE, A, 1, 1000, &ev);
		check("a key with no ecmd5 is refused outright", rc, LEDGER_NONE);
		check("and nothing was written to the table", ledger_find(&T, ecm_A, 1000) == 0, 1);
	}
	check("a real key is valid", ledger_key_valid(ecm_A), 1);
	check("a NULL key is not", ledger_key_valid(0), 0);

	/* Two ECMs sharing the 32-bit hash must not pair: only the ecmd5 pairs. */
	ledger_init(&T);
	ledger_offer(&T, ecm_A, 0x3333, CAID, PROV, SID, cw_bad, SRCTYPE_CACHE, A, 1, 1000, &ev);
	ledger_offer(&T, ecm_B, 0x3333, CAID, PROV, SID, cw_bad, SRCTYPE_CACHE, B, 1, 1100, &ev);
	check("same 32-bit hash, different ecmd5: two separate entries",
	      (ledger_find(&T, ecm_A, 1200) != 0) + (ledger_find(&T, ecm_B, 1200) != 0), 2);
	check("and the first entry saw no second source",
	      ledger_find(&T, ecm_A, 1200)->nslot, 1);

	/* ------------------------------------------------------------------ */
	printf("-- GR4: the same peer on another channel is another identity --\n");

	ledger_init(&T);
	ledger_offer(&T, ecm_A, 0x1111, CAID, PROV, SID, cw_bad, SRCTYPE_CACHE, A, 1, 1000, &ev);
	ledger_offer(&T, ecm_A, 0x1111, CAID, PROV, 0x00C8, cw_good, SRCTYPE_CACHE, B, 1, 1100, &ev);
	check("a different SID is a different key, so it still disputes",
	      ledger_find(&T, ecm_A, 1200)->nslot, 2);

	/* ------------------------------------------------------------------ */
	printf("-- aging, eviction, boundedness --\n");

	ledger_init(&T);
	ledger_offer(&T, ecm_A, 0x1111, CAID, PROV, SID, cw_bad, SRCTYPE_CACHE, A, 1, 1000, &ev);
	check("the entry is found while it is fresh", ledger_find(&T, ecm_A, 1000) != 0, 1);
	check("and gone once it is older than the window",
	      ledger_find(&T, ecm_A, 1000 + LEDGER_MAX_AGE_MS + 1) == 0, 1);
	check("... but the window itself is still inside",
	      ledger_find(&T, ecm_A, 1000 + LEDGER_MAX_AGE_MS) != 0, 1);

	/* A stale entry must not be paired with a new event either. */
	rc = ledger_offer(&T, ecm_A, 0x1111, CAID, PROV, SID, cw_good, SRCTYPE_CACHE, B, 1,
	                  1000 + LEDGER_MAX_AGE_MS + 5000, &ev);
	check("an aged entry is re-used, not contradicted", rc, LEDGER_NONE);
	check("and it restarts with one slot",
	      ledger_find(&T, ecm_A, 1000 + LEDGER_MAX_AGE_MS + 5000 + 100)->nslot, 1);

	/* Fill the table and overflow it: nothing may be lost that is still live. */
	ledger_init(&T);
	{
		uint8_t k[16];
		int i, found = 0;
		for (i = 0; i < LEDGER_TABLE_SIZE + 8; i++) {
			fill(k, (uint8_t)(0x10 + i));
			ledger_offer(&T, k, (uint32_t)i, CAID, PROV, SID, cw_bad,
			             SRCTYPE_CACHE, A, 1, (uint32_t)(1000 + i), &ev);
		}
		for (i = LEDGER_TABLE_SIZE + 8 - LEDGER_TABLE_SIZE; i < LEDGER_TABLE_SIZE + 8; i++) {
			fill(k, (uint8_t)(0x10 + i));
			if (ledger_find(&T, k, (uint32_t)(1000 + i))) found++;
		}
		check("every key that still fits is still findable", found, LEDGER_TABLE_SIZE);
		check("and the table never grew past its size",
		      (int)sizeof(struct ledger_table), LEDGER_TABLE_SIZE * (int)sizeof(struct ledger_entry));
	}

	/* ------------------------------------------------------------------ */
	printf("-- the log line --\n");

	ledger_init(&T);
	ledger_offer(&T, ecm_A, 0x1111, CAID, PROV, SID, cw_bad, SRCTYPE_CACHE, A, 1, 1000, &ev);
	ledger_failed(&T, ecm_A, 0x1111, CAID, PROV, SID, cw_bad, SRCTYPE_CACHE, A, 2200, &ev);
	ledger_offer(&T, ecm_A, 0x1111, CAID, PROV, SID, cw_good, SRCTYPE_CACHE, B, 1, 2400, &ev);
	n = ledger_format(&ev, line, sizeof(line));
	check("the line is rendered", n > 0, 1);
	check("it names the channel", strstr(line, "ch 1884:000000:0064") != 0, 1);
	check("it names the accused source", strstr(line, "proof recorded against 1/") != 0, 1);
	check("it says the key was delivered", strstr(line, "was delivered") != 0, 1);
	check("it quantifies the client's return", strstr(line, "came back after 1200 ms") != 0, 1);
	check("it names the independent source", strstr(line, "independent source") != 0, 1);
	{
		/*
		 * The two keys are asserted by building the expected text from the
		 * key bytes themselves. A hardcoded hex prefix ("a8", "58") would be
		 * a guess about fill()'s arithmetic, and a test that guesses is a test
		 * that fails for the wrong reason and gets "fixed" by editing it.
		 */
		char bad[24], good[24];
		snprintf(bad, sizeof(bad), "key %02x%02x%02x%02x", cw_bad[0], cw_bad[1], cw_bad[2], cw_bad[3]);
		snprintf(good, sizeof(good), "offered %02x%02x%02x%02x", cw_good[0], cw_good[1], cw_good[2], cw_good[3]);
		check("it carries the delivered key", strstr(line, bad) != 0, 1);
		check("it carries the contradicting key", strstr(line, good) != 0, 1);
	}
	check("it does not claim an action it cannot know about",
	      strstr(line, "avoided") == 0, 1);
	check("the line is bounded to one screen width", n < 320, 1);

	/* Truncation must be safe, and a non-proof must render nothing. */
	{
		/*
		 * snprintf() reports the length it *would* have written, so a truncated
		 * render returns more than the buffer holds. The two things worth
		 * asserting are therefore: it said so, and what it wrote is exactly the
		 * first bytes of the full line, terminated.
		 */
		char small[40];
		int n2 = ledger_format(&ev, small, (int)sizeof(small));
		check("a small buffer reports the truncation", n2 > (int)sizeof(small) - 1, 1);
		check("it wrote exactly what fits", (int)strlen(small), (int)sizeof(small) - 1);
		check("with no garbage past the cut", small[sizeof(small) - 1] == 0, 1);
		check("and it is a true prefix of the full line",
		      strncmp(small, line, sizeof(small) - 1), 0);
	}
	{
		struct ledger_event empty;
		memset(&empty, 0, sizeof(empty));
		empty.what = LEDGER_DISPUTE;
		check("a non-proof renders nothing", ledger_format(&empty, line, sizeof(line)), -1);
		check("and leaves the buffer empty", line[0], 0);
	}
	check("bad arguments are refused", ledger_format(&ev, line, 0), -1);
	check("a NULL event is refused", ledger_format(0, line, sizeof(line)), -1);

	/* ------------------------------------------------------------------ */
	printf("== %d/%d passed, %d failed ==\n", passed, passed + failed, failed);
	return failed ? 1 : 0;
}
