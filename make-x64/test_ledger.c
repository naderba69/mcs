/*
 * test_ledger.c -- candidate/acceptance/delivery/failure lifecycle.
 *
 * Builds the real ledger.h without the server. The suite deliberately covers
 * rejected offers, accepted-but-undelivered alternatives, source/CW mismatches,
 * request-context collisions, pool-independent saved identity, multiple
 * candidates, canonicalized CW bytes, and GetTickCount wrap.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "../src/ledger.h"

static int passed, failed;

static void check(const char *what, int got, int want)
{
	if (got == want) { passed++; printf("  [ ok ] %-66s = %d\n", what, got); }
	else { failed++; printf("  [FAIL] %-66s got=%d want=%d\n", what, got, want); }
}

#define SRCTYPE_CACHE 1
#define PEER_CSP      0x010000
#define A             (11 | PEER_CSP)
#define B             (12 | PEER_CSP)
#define C             (13 | PEER_CSP)
#define CAID          0x1884
#define PROV          0x000000
#define SID           0x0064

static uint8_t ecm_a[16], ecm_b[16];
static uint8_t cw_a[16], cw_b[16], cw_c[16], cw_d[16], cw_rejected[16];
static struct ledger_identity id_a;
static struct ledger_table table;
static struct ledger_event event;

static void fill(uint8_t *p, uint8_t seed)
{
	int i;
	for (i=0; i<16; i++) p[i] = (uint8_t)(seed + i*7);
}

static struct ledger_identity make_identity(const uint8_t *md5, uint32_t hash,
                                             uint16_t caid, uint32_t provid,
                                             int provider_known, uint16_t sid,
                                             uint8_t tag, uint16_t ecmlen)
{
	struct ledger_identity id;
	memset(&id, 0, sizeof(id));
	memcpy(id.ecmd5, md5, 16);
	id.hash = hash;
	id.caid = caid;
	id.provid = provid;
	id.provider_known = (uint8_t)!!provider_known;
	id.sid = sid;
	id.tag = tag;
	id.ecmlen = ecmlen;
	return id;
}

static int candidate(const struct ledger_identity *id, const uint8_t *cw,
                     int src, uint32_t ticks)
{
	return ledger_candidate(&table, id, cw, SRCTYPE_CACHE, src, ticks, &event);
}

static int accept_key(const struct ledger_identity *id, const uint8_t *offered,
                      const uint8_t *accepted, int src, uint32_t ticks)
{
	return ledger_accept(&table, id, offered, accepted, SRCTYPE_CACHE, src,
	                     ticks, &event);
}

static int deliver_key(const struct ledger_identity *id, const uint8_t *cw,
                       int src, uint32_t ticks)
{
	return ledger_delivered(&table, id, cw, SRCTYPE_CACHE, src, ticks, &event);
}

static int fail_key(const struct ledger_identity *id, const uint8_t *cw,
                    int src, uint32_t delivered_at, uint32_t ticks)
{
	return ledger_failed(&table, id, cw, SRCTYPE_CACHE, src, delivered_at,
	                     ticks, &event);
}

static void prep(void)
{
	ledger_init(&table);
	memset(&event, 0, sizeof(event));
}

int main(void)
{
	char line[512];
	int rc, n;

	fill(ecm_a, 1); fill(ecm_b, 0x40);
	fill(cw_a, 0xA0); fill(cw_b, 0x50); fill(cw_c, 0x77);
	fill(cw_d, 0x21); fill(cw_rejected, 0xEE);
	id_a = make_identity(ecm_a, 0x1111, CAID, PROV, 1, SID, 0x80, 64);

	printf("== test_ledger (four explicit evidence states) ==\n");

	printf("-- an offer is not acceptance or delivery --\n");
	prep();
	rc = candidate(&id_a, cw_a, A, 1000);
	check("first offered key is only a candidate", rc, LEDGER_NONE);
	check("candidate is recorded", ledger_find(&table, &id_a, 1000)->nslot, 1);
	check("candidate is not accepted", ledger_find(&table, &id_a, 1000)->candidate[0].accepted, 0);
	check("candidate is not delivered", ledger_find(&table, &id_a, 1000)->candidate[0].delivered, 0);
	check("duplicate offer is not new evidence", candidate(&id_a, cw_a, A, 1050), LEDGER_NONE);

	printf("-- rejected offers cannot accept, deliver, fail, or prove --\n");
	prep();
	candidate(&id_a, cw_rejected, B, 2000);
	candidate(&id_a, cw_a, A, 2050);
	check("a rejected offer cannot transition to accepted without a recorded candidate/source",
	      accept_key(&id_a, cw_rejected, cw_rejected, C, 2100), LEDGER_NONE);
	check("an accepted alternative may be noted but is not yet delivered",
	      accept_key(&id_a, cw_a, cw_a, A, 2150), LEDGER_ACCEPTED);
	check("delivery cannot bypass acceptance", deliver_key(&id_a, cw_rejected, B, 2200), LEDGER_NONE);
	check("a failure against a rejected, undelivered key is not recorded",
	      fail_key(&id_a, cw_rejected, B, 2000, 2300), LEDGER_NONE);
	check("rejected alternative did not complete proof", event.what, LEDGER_NONE);

	printf("-- failure must match accepted+delivered CW AND exact source --\n");
	prep();
	candidate(&id_a, cw_a, A, 3000);
	candidate(&id_a, cw_b, B, 3010);
	accept_key(&id_a, cw_a, cw_a, A, 3020);
	accept_key(&id_a, cw_b, cw_b, B, 3030);
	deliver_key(&id_a, cw_a, A, 3040);
	check("same bytes but wrong source cannot match a delivery",
	      fail_key(&id_a, cw_a, B, 3040, 3050), LEDGER_NONE);
	check("mismatched source does not set a failure", ledger_find(&table, &id_a, 3050)->candidate[0].failed, 0);
	check("matching delivered candidate plus accepted independent key proves",
	      fail_key(&id_a, cw_a, A, 3040, 4200), LEDGER_PROOF);
	check("accused source is the exact delivered source", event.src[0][1], A);
	check("alternative is from an independent source", event.src[1][1], B);
	check("both evidence records are accepted", event.accepted[0] && event.accepted[1], 1);
	check("delivered candidate was actually sent", event.delivered[0], 1);
	check("failed candidate matches client failure", event.failed[0], 1);
	check("elapsed time is from saved successful send across unsigned subtraction",
	      (int)event.elapsed_ms, 1160);

	printf("-- the alternative can arrive after failure, but only acceptance completes proof --\n");
	prep();
	candidate(&id_a, cw_a, A, 5000);
	candidate(&id_a, cw_b, B, 5050);
	accept_key(&id_a, cw_a, cw_a, A, 5100);
	deliver_key(&id_a, cw_a, A, 5200);
	check("client failure alone is not proof", fail_key(&id_a, cw_a, A, 5200, 6200), LEDGER_FAILURE_RECORDED);
	check("unaccepted conflicting offer does not complete proof",
	      candidate(&id_a, cw_c, C, 6250), LEDGER_DISPUTE);
	check("candidate remains unaccepted", ledger_find(&table, &id_a, 6250)->candidate[2].accepted, 0);
	check("accepting a distinct independent key completes proof",
	      accept_key(&id_a, cw_c, cw_c, C, 6300), LEDGER_PROOF);
	check("proof is about the delivered key, not the newly accepted alternative",
	      memcmp(event.cw[0], cw_a, 16), 0);
	check("proof alternative is the accepted key", memcmp(event.cw[1], cw_c, 16), 0);

	printf("-- candidate offers are retained beyond two CWs without overwriting --\n");
	prep();
	candidate(&id_a, cw_a, A, 7000);
	candidate(&id_a, cw_b, B, 7010);
	candidate(&id_a, cw_c, C, 7020);
	candidate(&id_a, cw_d, A, 7030); /* same-source self-dispute */
	check("four distinct source/CW offers are retained", ledger_find(&table, &id_a, 7040)->nslot, 4);
	check("first candidate bytes remain intact",
	      memcmp(ledger_find(&table, &id_a, 7040)->candidate[0].offered_cw, cw_a, 16), 0);
	check("third candidate bytes remain intact",
	      memcmp(ledger_find(&table, &id_a, 7040)->candidate[2].offered_cw, cw_c, 16), 0);
	check("self-dispute is counted separately", ledger_find(&table, &id_a, 7040)->nself, 1);

	printf("-- accepted comparison uses the canonical bytes that passed filters --\n");
	prep();
	{
		uint8_t raw_a[16], raw_b[16], canonical[16], other[16];
		fill(raw_a, 0x01); fill(raw_b, 0x31); fill(canonical, 0x55); fill(other, 0x66);
		candidate(&id_a, raw_a, A, 8000);
		candidate(&id_a, raw_b, B, 8010);
		accept_key(&id_a, raw_a, canonical, A, 8020);
		accept_key(&id_a, raw_b, canonical, B, 8030);
		deliver_key(&id_a, canonical, A, 8040);
		check("identical accepted CWs do not prove a disagreement",
		      fail_key(&id_a, canonical, A, 8040, 9000), LEDGER_FAILURE_RECORDED);
		candidate(&id_a, cw_c, C, 9050);
		check("accepted different canonical alternative proves",
		      accept_key(&id_a, cw_c, other, C, 9100), LEDGER_PROOF);
		check("proof prints canonical delivered key", memcmp(event.cw[0], canonical, 16), 0);
		check("proof prints canonical accepted alternative", memcmp(event.cw[1], other, 16), 0);
	}

	printf("-- failures never create entries or match a different ECM context --\n");
	prep();
	check("unrecorded failure does not create an entry",
	      fail_key(&id_a, cw_a, A, 1, 100), LEDGER_NONE);
	check("table remains empty", ledger_find(&table, &id_a, 100) == NULL, 1);
	candidate(&id_a, cw_a, A, 1000);
	accept_key(&id_a, cw_a, cw_a, A, 1010);
	deliver_key(&id_a, cw_a, A, 1020);
	{
		struct ledger_identity different;
		different = id_a; different.hash++;
		check("same MD5 but different hash is a distinct context",
		      ledger_find(&table, &different, 1030) == NULL, 1);
		check("mismatched context failure cannot accuse",
		      fail_key(&different, cw_a, A, 1020, 1040), LEDGER_NONE);
		different = id_a; different.caid++;
		check("different CAID is distinct", ledger_find(&table, &different, 1030) == NULL, 1);
		different = id_a; different.provid++;
		check("different provider is distinct", ledger_find(&table, &different, 1030) == NULL, 1);
		different = id_a; different.provider_known = 0;
		check("known-zero and unknown provider are distinct", ledger_find(&table, &different, 1030) == NULL, 1);
		different = id_a; different.sid++;
		check("different SID is distinct", ledger_find(&table, &different, 1030) == NULL, 1);
		different = id_a; different.tag ^= 1;
		check("different ECM tag is distinct", ledger_find(&table, &different, 1030) == NULL, 1);
		different = id_a; different.ecmlen++;
		check("different ECM length is distinct", ledger_find(&table, &different, 1030) == NULL, 1);
		different = make_identity(ecm_b, id_a.hash, CAID, PROV, 1, SID, 0x80, 64);
		check("same 32-bit hash, different MD5 is distinct",
		      ledger_find(&table, &different, 1030) == NULL, 1);
	}

	printf("-- identity validation and candidate overflow are fail-closed --\n");
	{
		uint8_t zero[16] = {0};
		struct ledger_identity invalid = id_a;
		memset(invalid.ecmd5, 0, 16);
		prep();
		check("zero ECM MD5 is invalid", ledger_identity_valid(&invalid), 0);
		check("zero ECM MD5 cannot create a candidate",
		      ledger_candidate(&table, &invalid, cw_a, SRCTYPE_CACHE, A, 1, &event), LEDGER_NONE);
		invalid = id_a; invalid.ecmlen = 0;
		check("zero ECM length is invalid", ledger_identity_valid(&invalid), 0);
		check("NULL MD5 pointer is invalid", ledger_key_valid(NULL), 0);
		check("zero MD5 bytes are invalid", ledger_key_valid(zero), 0);
	}
	prep();
	{
		uint8_t keys[LEDGER_CANDIDATE_SLOTS + 1][16];
		int i;
		for (i=0; i<LEDGER_CANDIDATE_SLOTS; i++) {
			fill(keys[i], (uint8_t)(0x10+i));
			candidate(&id_a, keys[i], A+i, (uint32_t)(2000+i));
		}
		fill(keys[LEDGER_CANDIDATE_SLOTS], 0xF0);
		check("full per-ECM candidate set refuses an extra offer",
		      candidate(&id_a, keys[LEDGER_CANDIDATE_SLOTS], C, 2100), LEDGER_FULL);
		check("full candidate set does not overwrite prior evidence",
		      ledger_find(&table, &id_a, 2100)->nslot, LEDGER_CANDIDATE_SLOTS);
	}

	printf("-- 32-bit tick wrap and bounded aging --\n");
	prep();
	{
		uint32_t sent = UINT32_MAX-500u;
		uint32_t failed_at = 2000u;
		candidate(&id_a, cw_a, A, sent);
		accept_key(&id_a, cw_a, cw_a, A, sent);
		deliver_key(&id_a, cw_a, A, sent);
		candidate(&id_a, cw_b, B, sent+1u);
		accept_key(&id_a, cw_b, cw_b, B, sent+2u);
		check("entry survives a wrap-crossing age below TTL",
		      ledger_find(&table, &id_a, failed_at) != NULL, 1);
		check("failure elapsed time is wrap-safe",
		      fail_key(&id_a, cw_a, A, sent, failed_at), LEDGER_PROOF);
		check("wrapped elapsed duration is correct",
		      (int)event.elapsed_ms, (int)(failed_at-sent));
		check("entry ages out after the wrap-safe TTL",
		      ledger_find(&table, &id_a, failed_at+LEDGER_MAX_AGE_MS+1u) == NULL, 1);
	}

	printf("-- the proof log reports state, identity, and both keys --\n");
	prep();
	candidate(&id_a, cw_a, A, 10000);
	candidate(&id_a, cw_b, B, 10010);
	accept_key(&id_a, cw_a, cw_a, A, 10020);
	accept_key(&id_a, cw_b, cw_b, B, 10030);
	deliver_key(&id_a, cw_a, A, 10040);
	check("failure forms proof", fail_key(&id_a, cw_a, A, 10040, 11240), LEDGER_PROOF);
	n = ledger_format(&event, line, sizeof(line));
	check("proof line renders", n > 0, 1);
	check("line names channel and provider scope", strstr(line, "ch 1884:provider-known-000000:0064") != NULL, 1);
	check("line identifies the delivered source", strstr(line, "delivered source 1/") != NULL, 1);
	check("line states accepted, delivered, failed", strstr(line, "accepted=yes delivered=yes failed=yes") != NULL, 1);
	check("line names the independent accepted source", strstr(line, "independent source 1/") != NULL, 1);
	check("line includes elapsed delivery-to-failure time", strstr(line, "after 1200 ms") != NULL, 1);
	check("line is bounded", n < (int)sizeof(line), 1);
	{
		char short_line[24];
		int short_n = ledger_format(&event, short_line, sizeof(short_line));
		check("small output buffer reports truncation", short_n >= (int)sizeof(short_line), 1);
		check("small output remains terminated", short_line[sizeof(short_line)-1] == 0, 1);
	}
	{
		struct ledger_event nonproof;
		memset(&nonproof, 0, sizeof(nonproof));
		check("non-proof event is not formatted",
		      ledger_format(&nonproof, line, sizeof(line)), -1);
		check("non-proof output is empty", line[0], 0);
	}

	printf("== %d/%d passed, %d failed ==\n", passed, passed+failed, failed);
	return failed ? 1 : 0;
}
