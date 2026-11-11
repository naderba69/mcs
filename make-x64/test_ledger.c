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
#include "../src/r48-source-reputation.h"

static int passed, failed;

static void check(const char *what, int got, int want)
{
	if (got == want) { passed++; printf("  [ ok ] %-66s = %d\n", what, got); }
	else { failed++; printf("  [FAIL] %-66s got=%d want=%d\n", what, got, want); }
}

#define SRCTYPE_CACHE 1
#define SRCTYPE_SERVER 2
#define SRCTYPE_CLIENT 3
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
static struct r48_source_rep_table reputation;
static struct r48_source_rep_snapshot reputation_snapshot;

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

static int candidate_source(const struct ledger_identity *id, const uint8_t *cw,
                            int srctype, int srcid, uint32_t ticks)
{
	return ledger_candidate(&table, id, cw, srctype, srcid, ticks, &event);
}

static int next_reference(const struct ledger_identity *id, uint32_t ticks,
                          struct ledger_event *ev)
{
	return ledger_next_reference_agreement(&table, id, ticks, SRCTYPE_CACHE,
	                                       SRCTYPE_SERVER, ev);
}

static int candidate(const struct ledger_identity *id, const uint8_t *cw,
                     int src, uint32_t ticks)
{
	return candidate_source(id, cw, SRCTYPE_CACHE, src, ticks);
}

static int accept_source(const struct ledger_identity *id, const uint8_t *offered,
                         const uint8_t *accepted, int srctype, int srcid,
                         uint32_t ticks)
{
	return ledger_accept(&table, id, offered, accepted, srctype, srcid,
	                     ticks, SRCTYPE_SERVER, &event);
}

static int accept_key(const struct ledger_identity *id, const uint8_t *offered,
                      const uint8_t *accepted, int src, uint32_t ticks)
{
	return accept_source(id, offered, accepted, SRCTYPE_CACHE, src, ticks);
}

static int deliver_source(const struct ledger_identity *id, const uint8_t *cw,
                          int srctype, int srcid, uint32_t ticks)
{
	return ledger_delivered(&table, id, cw, srctype, srcid, ticks,
	                        SRCTYPE_SERVER, &event);
}

static int deliver_key(const struct ledger_identity *id, const uint8_t *cw,
                       int src, uint32_t ticks)
{
	return deliver_source(id, cw, SRCTYPE_CACHE, src, ticks);
}

static int fail_source(const struct ledger_identity *id, const uint8_t *cw,
                       int srctype, int srcid,
                       uint32_t delivered_at, uint32_t ticks)
{
	return ledger_failed(&table, id, cw, srctype, srcid, delivered_at,
	                     ticks, SRCTYPE_SERVER, &event);
}

static int fail_key(const struct ledger_identity *id, const uint8_t *cw,
                    int src, uint32_t delivered_at, uint32_t ticks)
{
	return fail_source(id, cw, SRCTYPE_CACHE, src, delivered_at, ticks);
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
	struct ledger_entry *entry;

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

	printf("-- lock acquisition may reorder captured monotonic ticks --\n");
	prep();
	candidate(&id_a, cw_a, A, 1200);
	check("a slightly older captured tick still finds the live ledger row",
	      ledger_find(&table, &id_a, 1199) != NULL, 1);
	check("a stale duplicate does not replace the row's evidence timestamp",
	      candidate(&id_a, cw_a, A, 1199), LEDGER_NONE);
	check("stale duplicate preserves its original candidate timestamp",
	      ledger_find(&table, &id_a, 1200)->candidate[0].candidate_at, 1200);
	check("stale duplicate does not move the live-row timestamp backward",
	      ledger_find(&table, &id_a, 1200)->seen_at, 1200);

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
		check("peer-peer disagreement is recorded, not proof",
		      fail_key(&id_a, cw_a, A, 3040, 4200), LEDGER_FAILURE_RECORDED);
		check("peer-peer evidence cannot produce a reference proof",
		      ledger_next_proof(&table, &id_a, 4200, SRCTYPE_SERVER, &event), LEDGER_NONE);
		check("an accepted line-server candidate is the only proof anchor",
		      candidate_source(&id_a, cw_c, SRCTYPE_SERVER, 77, 4210), LEDGER_DISPUTE);
		check("accepting the line-server's different CW completes proof",
		      accept_source(&id_a, cw_c, cw_c, SRCTYPE_SERVER, 77, 4220), LEDGER_PROOF);
		check("accused source is the exact delivered peer", event.src[0][1], A);
		check("alternative is the line-server reference type", event.src[1][0], SRCTYPE_SERVER);
		check("alternative source ID is the accepted line", event.src[1][1], 77);
		check("both evidence records are accepted", event.accepted[0] && event.accepted[1], 1);
		check("delivered candidate was actually sent", event.delivered[0], 1);
		check("failed candidate matches client failure", event.failed[0], 1);
	check("elapsed time is from saved successful send across unsigned subtraction",
	      (int)event.elapsed_ms, 1160);

	printf("-- a peer disagreement cannot accuse the line-server reference --\n");
	prep();
	candidate_source(&id_a, cw_a, SRCTYPE_SERVER, 90, 4300);
	candidate(&id_a, cw_b, B, 4310);
	accept_source(&id_a, cw_a, cw_a, SRCTYPE_SERVER, 90, 4320);
	accept_key(&id_a, cw_b, cw_b, B, 4330);
	deliver_source(&id_a, cw_a, SRCTYPE_SERVER, 90, 4340);
	check("client failure after line-server delivery is not peer proof",
	      fail_source(&id_a, cw_a, SRCTYPE_SERVER, 90, 4340, 5400),
	      LEDGER_FAILURE_RECORDED);
	check("a peer alternative cannot mark the reference as convicted",
	      ledger_next_proof(&table, &id_a, 5400, SRCTYPE_SERVER, &event), LEDGER_NONE);

	printf("-- only accepted agreement with the line anchor earns source credit --\n");
	prep();
	r48_source_rep_init(&reputation);
	id_a.cache_evidence_enabled = 0;
	candidate(&id_a, cw_a, A, 5500);
	candidate(&id_a, cw_a, B, 5510);
	accept_key(&id_a, cw_a, cw_a, A, 5520);
	accept_key(&id_a, cw_a, cw_a, B, 5530);
	check("accepted peer-peer agreement alone earns no reference credit",
	      next_reference(&id_a, 5530, &event), LEDGER_NONE);
	check("peer-peer agreement does not create an R48 reputation row",
	      r48_source_rep_query(&reputation, SRCTYPE_CACHE, A, CAID, PROV,
	                           1, 5530, &reputation_snapshot), 0);
	candidate_source(&id_a, cw_a, SRCTYPE_SERVER, 91, 5540);
	check("line anchor passes normal acceptance before credit",
	      accept_source(&id_a, cw_a, cw_a, SRCTYPE_SERVER, 91, 5550), LEDGER_ACCEPTED);
	check("first peer is credited only after matching the line anchor",
	      next_reference(&id_a, 5550, &event),
	      LEDGER_REFERENCE_AGREEMENT);
	check("credit event identifies the peer candidate", event.src[0][1], A);
	check("credit event identifies the line reference", event.src[1][1], 91);
	check("both matching candidates are accepted", event.accepted[0] && event.accepted[1], 1);
	check("default-off CACHE EVIDENCE suppresses R48 fact collection",
	      r48_source_rep_note_opt_in(&reputation,
	          event.identity.cache_evidence_enabled, event.src[0][0],
	          event.src[0][1], event.identity.caid, event.identity.provid,
	          event.identity.provider_known,
	          R48_SOURCE_REP_REFERENCE_MATCH, event.ticks), 0);
	check("default-off leaves no reputation row",
	      r48_source_rep_query(&reputation, SRCTYPE_CACHE, A, CAID, PROV,
	                           1, 5550, &reputation_snapshot), 0);
	check("second peer may earn its own line-anchored credit",
	      next_reference(&id_a, 5550, &event),
	      LEDGER_REFERENCE_AGREEMENT);
	check("second credit is for the other peer", event.src[0][1], B);
	check("a candidate is credited at most once",
	      next_reference(&id_a, 5550, &event), LEDGER_NONE);

	printf("-- only cache sources can receive line-reference credit --\n");
	prep();
	r48_source_rep_init(&reputation);
	id_a.cache_evidence_enabled = 1;
	candidate_source(&id_a, cw_a, SRCTYPE_CLIENT, 22, 5551);
	candidate_source(&id_a, cw_a, SRCTYPE_SERVER, 96, 5552);
	accept_source(&id_a, cw_a, cw_a, SRCTYPE_CLIENT, 22, 5553);
	accept_source(&id_a, cw_a, cw_a, SRCTYPE_SERVER, 96, 5554);
	check("accepted non-cache source cannot consume cache reference credit",
	      ledger_next_reference_agreement(&table, &id_a, 5554,
	          SRCTYPE_CACHE, SRCTYPE_SERVER, &event), LEDGER_NONE);
	entry = ledger_find(&table, &id_a, 5554);
	check("non-cache candidate remains uncredited",
	      entry ? entry->candidate[0].reference_credited : -1, 0);
	candidate_source(&id_a, cw_a, SRCTYPE_CACHE, A, 5555);
	accept_source(&id_a, cw_a, cw_a, SRCTYPE_CACHE, A, 5556);
	check("cache candidate remains eligible after non-cache candidate",
	      next_reference(&id_a, 5556, &event), LEDGER_REFERENCE_AGREEMENT);
	check("reference event is for the cache source",
	      event.src[0][0], SRCTYPE_CACHE);
	check("cache source alone creates an R48 reputation row",
	      r48_source_rep_note_opt_in(&reputation,
	          event.identity.cache_evidence_enabled, event.src[0][0],
	          event.src[0][1], event.identity.caid, event.identity.provid,
	          event.identity.provider_known,
	          R48_SOURCE_REP_REFERENCE_MATCH, event.ticks), 1);
	check("non-cache source cannot populate the R48 cache table",
	      r48_source_rep_query(&reputation, SRCTYPE_CLIENT, 22, CAID, PROV,
	                           1, 5556, &reputation_snapshot), 0);

	printf("-- R48 opt-in stays on the accepted candidate, not the shared ECM row --\n");
	{
		struct ledger_identity id_off = id_a;
		struct ledger_identity id_on = id_a;
		id_off.cache_evidence_enabled = 0;
		id_on.cache_evidence_enabled = 1;
		prep();
		r48_source_rep_init(&reputation);
		candidate_source(&id_off, cw_a, SRCTYPE_CACHE, A, 5560);
		candidate_source(&id_on, cw_a, SRCTYPE_CACHE, C, 5561);
		candidate_source(&id_off, cw_a, SRCTYPE_SERVER, 93, 5562);
		accept_source(&id_off, cw_a, cw_a, SRCTYPE_CACHE, A, 5563);
		accept_source(&id_on, cw_a, cw_a, SRCTYPE_CACHE, C, 5564);
		accept_source(&id_off, cw_a, cw_a, SRCTYPE_SERVER, 93, 5565);
		check("shared ledger still emits the OFF candidate's D-1 credit",
		      next_reference(&id_off, 5565, &event),
		      LEDGER_REFERENCE_AGREEMENT);
		check("OFF candidate does not inherit another profile's opt-in",
		      event.identity.cache_evidence_enabled, 0);
		check("OFF candidate creates no R48 fact",
		      r48_source_rep_note_opt_in(&reputation,
		          event.identity.cache_evidence_enabled, event.src[0][0],
		          event.src[0][1], event.identity.caid, event.identity.provid,
		          event.identity.provider_known,
		          R48_SOURCE_REP_REFERENCE_MATCH, event.ticks), 0);
		check("second candidate retains its own ON opt-in",
		      next_reference(&id_on, 5565, &event),
		      LEDGER_REFERENCE_AGREEMENT);
		check("ON candidate's D-1 event is eligible for R48 collection",
		      event.identity.cache_evidence_enabled, 1);
		check("ON candidate records exactly one R48 match",
		      r48_source_rep_note_opt_in(&reputation,
		          event.identity.cache_evidence_enabled, event.src[0][0],
		          event.src[0][1], event.identity.caid, event.identity.provid,
		          event.identity.provider_known,
		          R48_SOURCE_REP_REFERENCE_MATCH, event.ticks), 1);
		check("only the opted-in source has a reputation row",
		      r48_source_rep_query(&reputation, SRCTYPE_CACHE, A, CAID, PROV,
		                           1, 5565, &reputation_snapshot), 0);
		check("the opted-in source row contains one match",
		      r48_source_rep_query(&reputation, SRCTYPE_CACHE, C, CAID, PROV,
		                           1, 5565, &reputation_snapshot), 1);
	}

	printf("-- the first accepted profile fixes a candidate's opt-in for this ECM --\n");
	{
		struct ledger_identity id_off = id_a;
		struct ledger_identity id_on = id_a;
		struct ledger_entry *entry;
		id_off.cache_evidence_enabled = 0;
		id_on.cache_evidence_enabled = 1;
		prep();
		r48_source_rep_init(&reputation);
		candidate_source(&id_off, cw_a, SRCTYPE_CACHE, A, 5566);
		candidate_source(&id_off, cw_a, SRCTYPE_SERVER, 95, 5567);
		accept_source(&id_off, cw_a, cw_a, SRCTYPE_CACHE, A, 5568);
		accept_source(&id_off, cw_a, cw_a, SRCTYPE_SERVER, 95, 5569);
		check("the first OFF acceptance receives its ordinary reference match",
		      next_reference(&id_off, 5569, &event),
		      LEDGER_REFERENCE_AGREEMENT);
		check("the original OFF match does not carry R48 opt-in",
		      event.identity.cache_evidence_enabled, 0);
		check("the OFF match is not retroactively added to source reputation",
		      r48_source_rep_note_opt_in(&reputation,
		          event.identity.cache_evidence_enabled, event.src[0][0],
		          event.src[0][1], event.identity.caid, event.identity.provid,
		          event.identity.provider_known,
		          R48_SOURCE_REP_REFERENCE_MATCH, event.ticks), 0);
		check("a later ON acceptance of the same candidate remains accepted",
		      accept_source(&id_on, cw_a, cw_a, SRCTYPE_CACHE, A, 5570),
		      LEDGER_ACCEPTED);
		entry = ledger_find(&table, &id_on, 5570);
		check("later ON cannot upgrade the first accepted candidate's OFF latch",
		      entry ? entry->candidate[0].cache_evidence_enabled : -1, 0);
		check("the already-credited OFF candidate does not emit a second match",
		      next_reference(&id_on, 5570, &event), LEDGER_NONE);
		check("no R48 row is created by the later ON observation",
		      r48_source_rep_query(&reputation, SRCTYPE_CACHE, A, CAID, PROV,
		                           1, 5570, &reputation_snapshot), 0);
	}

	printf("-- proof opt-in also follows the delivered candidate --\n");
	{
		struct ledger_identity id_off = id_a;
		struct ledger_identity id_on = id_a;
		id_off.cache_evidence_enabled = 0;
		id_on.cache_evidence_enabled = 1;
		prep();
		r48_source_rep_init(&reputation);
		candidate_source(&id_off, cw_a, SRCTYPE_CACHE, A, 5570);
		candidate_source(&id_on, cw_d, SRCTYPE_CACHE, C, 5571);
		candidate_source(&id_off, cw_b, SRCTYPE_SERVER, 94, 5572);
		accept_source(&id_off, cw_a, cw_a, SRCTYPE_CACHE, A, 5573);
		deliver_source(&id_off, cw_a, SRCTYPE_CACHE, A, 5574);
		fail_source(&id_off, cw_a, SRCTYPE_CACHE, A, 5574, 5575);
		check("accepted line reference proves the OFF delivered source",
		      accept_source(&id_off, cw_b, cw_b, SRCTYPE_SERVER, 94, 5576),
		      LEDGER_PROOF);
		check("proof does not inherit unrelated profile opt-in",
		      event.identity.cache_evidence_enabled, 0);
		check("OFF proof creates no R48 source row",
		      r48_source_rep_note_opt_in(&reputation,
		          event.identity.cache_evidence_enabled, event.src[0][0],
		          event.src[0][1], event.identity.caid, event.identity.provid,
		          event.identity.provider_known,
		          R48_SOURCE_REP_REFERENCE_PROOF, event.ticks), 0);
	}

	printf("-- opt-in R48 EWMA consumes only the accepted D-1 event --\n");
	prep();
	r48_source_rep_init(&reputation);
	id_a.cache_evidence_enabled = 1;
	candidate(&id_a, cw_a, A, 5600);
	accept_key(&id_a, cw_a, cw_a, A, 5610);
	candidate_source(&id_a, cw_a, SRCTYPE_SERVER, 92, 5620);
	accept_source(&id_a, cw_a, cw_a, SRCTYPE_SERVER, 92, 5630);
	check("D-1 event still requires the accepted line reference",
	      next_reference(&id_a, 5630, &event),
	      LEDGER_REFERENCE_AGREEMENT);
	check("opt-in D-1 event updates source/provider EWMA",
	      r48_source_rep_note_opt_in(&reputation,
	          event.identity.cache_evidence_enabled, event.src[0][0],
	          event.src[0][1], event.identity.caid, event.identity.provid,
	          event.identity.provider_known,
	          R48_SOURCE_REP_REFERENCE_MATCH, event.ticks), 1);
	check("one exact reference match is visible as one match",
	      r48_source_rep_query(&reputation, SRCTYPE_CACHE, A, CAID, PROV,
	                           1, 5630, &reputation_snapshot), 1);
	check("one match begins near neutral without a threshold",
	      reputation_snapshot.score, 56);
	check("reference match does not fabricate proof count",
	      (int)reputation_snapshot.proofs, 0);
	id_a.cache_evidence_enabled = 0;

	printf("-- the alternative can arrive after failure, but only acceptance completes proof --\n");
	prep();
	candidate(&id_a, cw_a, A, 5000);
	candidate(&id_a, cw_b, B, 5050);
	accept_key(&id_a, cw_a, cw_a, A, 5100);
	deliver_key(&id_a, cw_a, A, 5200);
	check("client failure alone is not proof", fail_key(&id_a, cw_a, A, 5200, 6200), LEDGER_FAILURE_RECORDED);
		check("unaccepted line-reference offer does not complete proof",
		      candidate_source(&id_a, cw_c, SRCTYPE_SERVER, 78, 6250), LEDGER_DISPUTE);
		check("reference candidate remains unaccepted", ledger_find(&table, &id_a, 6250)->candidate[2].accepted, 0);
		check("accepting a distinct line-reference key completes proof",
		      accept_source(&id_a, cw_c, cw_c, SRCTYPE_SERVER, 78, 6300), LEDGER_PROOF);
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
			candidate_source(&id_a, cw_c, SRCTYPE_SERVER, 79, 9050);
			check("accepted different canonical line-reference proves",
			      accept_source(&id_a, cw_c, other, SRCTYPE_SERVER, 79, 9100), LEDGER_PROOF);
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
		different = id_a;
		different.cache_evidence_enabled = 1;
		check("CACHE EVIDENCE metadata does not alter ECM identity",
		      ledger_identity_equal(&id_a, &different), 1);
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
			candidate_source(&id_a, cw_b, SRCTYPE_SERVER, 80, sent+1u);
			accept_source(&id_a, cw_b, cw_b, SRCTYPE_SERVER, 80, sent+2u);
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
	candidate_source(&id_a, cw_b, SRCTYPE_SERVER, 81, 10010);
	accept_key(&id_a, cw_a, cw_a, A, 10020);
	accept_source(&id_a, cw_b, cw_b, SRCTYPE_SERVER, 81, 10030);
	deliver_key(&id_a, cw_a, A, 10040);
	check("failure forms proof", fail_key(&id_a, cw_a, A, 10040, 11240), LEDGER_PROOF);
	n = ledger_format(&event, line, sizeof(line));
	check("proof line renders", n > 0, 1);
	check("line names channel and provider scope", strstr(line, "ch 1884:provider-known-000000:0064") != NULL, 1);
	check("line identifies the delivered source", strstr(line, "delivered source 1/") != NULL, 1);
	check("line states accepted, delivered, failed", strstr(line, "accepted=yes delivered=yes failed=yes") != NULL, 1);
	check("line names the accepted line-server reference", strstr(line, "line reference 2/81") != NULL, 1);
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
