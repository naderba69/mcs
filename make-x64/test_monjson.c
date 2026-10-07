/*
 * test_monjson.c -- TASK R5 (D59): the /json document builder, pure.
 *
 * Links ../src/monjson.c only: no tree globals, no sockets. Escaping,
 * comma discipline, the merge of reputation records into peer rows,
 * overflow safety with canaries, and the exact key set of every section.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../src/monjson.h"
#include "../src/dcwstats.h"

static int npass, nfail;
static void chk(int cond, const char *what, int extra)
{
	if (cond) { npass++; printf("  [ ok ] %s = %d\n", what, extra); }
	else      { nfail++; printf("  [FAIL] %s = %d\n", what, extra); }
}

#define CANARY 0x5A
static char big[MONJSON_BUF_MAX + 2*64];

static int has(const char *hay, const char *needle)
{
	return strstr(hay, needle) != NULL;
}

int main(void)
{
	char esc[512];

	printf("== monjson unit -- TASK R5 (D59) ==\n");

	/* ---- 1. escaping: plain passthrough ---- */
	chk(monjson_escape(esc, sizeof(esc), "plain-123_4.5") == 0
		&& !strcmp(esc, "plain-123_4.5"), "escape passthrough intact", 0);

	/* ---- 2. escaping: the mandatory set ---- */
	monjson_escape(esc, sizeof(esc), "a\"b\\c\nd\te\x01\x1f\x7f");
	chk(!strcmp(esc, "a\\\"b\\\\c\\nd\\te\\u0001\\u001F\x7f"),
		"escape specials mapped", 0);

	/* ---- 3. escaping: truncation is safe and reported ---- */
	{
		char small[8];
		memset(small, 0, sizeof(small));
		char canary = CANARY;
		int rc = monjson_escape(small, (int)sizeof(small), "abcdefghij");
		chk(rc == -1 && small[7] == 0 && small[6] != 0 && canary == CANARY,
			"escape overflow: -1, NUL inside cap, canary intact", rc);
		char fit[10];
		chk(monjson_escape(fit, (int)sizeof(fit), "abcdefghi") == 0
			&& !strcmp(fit, "abcdefghi"), "escape exact fit works", 0);
	}

	/* ---- 4. minimal document: everything off, nothing inside ---- */
	{
		struct monjson_in in;
		unsigned long counters[DCW_REJ_COUNT];
		memset(&in, 0, sizeof(in));
		memset(counters, 0, sizeof(counters));
		in.dcwstats = counters;
		int len = monjson_build(big, sizeof(big), &in);
		chk(len > 0 && big[0] == '{' && big[len-1] == '}', "minimal doc braces", len);
		chk(has(big, "\"version\":\"r82\""), "version pinned", 0);
		chk(has(big, "\"uptime\":0") && has(big, "\"now\":0"), "uptime/now present", 0);
		chk(has(big, "\"dcwstats\":{\"on\":0,"), "dcwstats off", 0);
		int i, names = 0;
		for (i = 0; i < DCW_REJ_COUNT; i++)
			if (strstr(big, dcwstats_reason_name(i))) names++;
		chk(names == DCW_REJ_COUNT, "all dcwstats names present", names);
		chk(has(big, "\"peerrep\":{\"on\":0,\"thresholds\":{\"distrust\":0,\"isolate\":0,\"ban\":0},\"peers\":[]}"),
			"peerrep off shape", 0);
		chk(has(big, "\"cache_peers\":[]"), "cache_peers empty", 0);
		chk(!has(big, ",}") && !has(big, ",]") && !has(big, "[,") && !has(big, "{,"),
			"comma discipline", 0);
	}

	/* ---- 5. dcwstats counters carry values ---- */
	{
		struct monjson_in in;
		unsigned long counters[DCW_REJ_COUNT];
		memset(&in, 0, sizeof(in));
		memset(counters, 0, sizeof(counters));
		counters[0] = 12345678901UL;
		counters[DCW_REJ_CYCLE] = 7;
		in.dcwstats_on = 1;
		in.dcwstats = counters;
		int len = monjson_build(big, sizeof(big), &in);
		chk(len > 0 && has(big, "\"accepted\":12345678901")
			&& has(big, "\"cycle-contradiction\":7"), "counters carried", len);
	}

	/* ---- 6. reputation records + the row merge ---- */
	{
		struct monjson_in in;
		struct monjson_peer peers[2];
		struct monjson_rep reps[2];
		unsigned long counters[DCW_REJ_COUNT];
		memset(&in, 0, sizeof(in));
		memset(counters, 0, sizeof(counters));
		memset(peers, 0, sizeof(peers));
		memset(reps, 0, sizeof(reps));
		in.dcwstats = counters;

		strcpy(peers[0].name, "peerA");
		peers[0].ip = 0x0100007F; peers[0].port = 16923;
		peers[0].ping = 31; peers[0].cards = 4;
		peers[0].ecmnb = 100; peers[0].ecmok = 97; peers[0].hitnb = 55;
		peers[0].rep_stage = -1;
		strcpy(peers[1].name, "bad\"peer\\x");
		peers[1].ip = 0x0100007F; peers[1].port = 16924;
		peers[1].rep_stage = -1;

		reps[0].ip = 0x0100007F; reps[0].port = 16924;
		reps[0].stage = 1; reps[0].events = 7;
		strcpy(reps[0].reason, "cycle-contradiction");
		/* the merge into the row is the CALLER's job (http_send_json does
		 * it by ip:port); the builder only renders what it is given */
		peers[1].rep_stage = 1; peers[1].rep_events = 7;
		strcpy(peers[1].rep_reason, "cycle-contradiction");
		reps[1].ip = 0x0100000A; reps[1].port = 9999;
		reps[1].stage = 3; reps[1].events = 50;
		strcpy(reps[1].reason, "push-while-isolated");

		in.peerrep_on = 1; in.th_distrust = 3; in.th_isolate = 6; in.th_ban = 10;
		in.nreps = 2; in.reps = reps;
		in.npeers = 2; in.peers = peers;

		int len = monjson_build(big, sizeof(big), &in);
		chk(len > 0, "merge doc built", len);
		chk(has(big, "\"thresholds\":{\"distrust\":3,\"isolate\":6,\"ban\":10}"),
			"thresholds carried", 0);
		chk(has(big, "\"peer\":\"127.0.0.1:16924\"")
			&& has(big, "\"stage\":1") && has(big, "\"stage_name\":\"distrust\"")
			&& has(big, "\"events\":7") && has(big, "\"last\":\"cycle-contradiction\""),
			"rep record fields", 0);
		chk(has(big, "\"peer\":\"10.0.0.1:9999\"") && has(big, "\"stage_name\":\"ban\""),
			"orphan rep record kept", 0);
		chk(has(big, "\"name\":\"peerA\",\"ip\":\"127.0.0.1\",\"port\":16923,\"ping\":31,\"cards\":4,\"ecm\":100,\"ecm_ok\":97,\"hits\":55,\"disabled\":0"),
			"peer row fields in order", 0);
		chk(has(big, "\"rep_stage\":1,\"rep_stage_name\":\"distrust\",\"rep_events\":7,\"rep_last\":\"cycle-contradiction\""),
			"rep merged into matching row", 0);
		chk(has(big, "\"name\":\"bad\\\"peer\\\\x\""), "peer name escaped in row", 0);
		chk(has(big, "\"rep_stage\":-1,\"rep_stage_name\":\"\",\"rep_events\":0,\"rep_last\":\"\""),
			"unmatched row stays empty", 0);
		chk(!has(big, ",}") && !has(big, ",]"), "comma discipline with data", 0);
	}

	/* ---- 7. overflow: the whole document is abandoned ---- */
	{
		struct monjson_in in;
		struct monjson_peer peers[2];
		struct monjson_rep reps[1];
		unsigned long counters[DCW_REJ_COUNT];
		memset(&in, 0, sizeof(in));
		memset(counters, 0, sizeof(counters));
		memset(peers, 0, sizeof(peers));
		memset(reps, 0, sizeof(reps));
		in.dcwstats = counters;
		strcpy(peers[0].name, "peerA"); peers[0].port = 1; peers[0].rep_stage = -1;
		in.nreps = 1; in.reps = reps; in.npeers = 1; in.peers = peers;
		char tiny[64];
		memset(tiny, 0x41, sizeof(tiny));
		char canary = CANARY;
		int rc = monjson_build(tiny, (int)sizeof(tiny), &in);
		chk(rc == -1 && tiny[0] == 0 && canary == CANARY,
			"doc overflow: -1, empty body, canary intact", rc);
		/* cap respected byte-for-byte: the last byte (end == buf+cap-1)
		 * was never written, and the canary beyond the buffer is intact */
		chk(tiny[63] == 0x41, "overflow respected the cap", 0);
	}

	/* ---- 8. canary around a full build ---- */
	{
		struct monjson_in in;
		struct monjson_peer peers[MONJSON_MAX_PEERS];
		unsigned long counters[DCW_REJ_COUNT];
		memset(&in, 0, sizeof(in));
		memset(counters, 0, sizeof(counters));
		memset(peers, 0, sizeof(peers));
		in.dcwstats = counters;
		int i;
		for (i = 0; i < MONJSON_MAX_PEERS; i++) {
			snprintf(peers[i].name, sizeof(peers[i].name), "p%d", i);
			peers[i].port = 1000 + i;
			peers[i].rep_stage = -1;
		}
		in.npeers = MONJSON_MAX_PEERS; in.peers = peers;
		memset(big, CANARY, sizeof(big));
		int len = monjson_build(big + 64, sizeof(big) - 128, &in);
		chk(len > 0, "256-peer doc built", len);
		chk((unsigned char)big[63] == CANARY && (unsigned char)big[sizeof(big)-1] == CANARY,
			"canaries untouched around 256-peer doc", 0);
	}

	printf("\n== %d/%d passed, %d failed ==\n", npass, npass + nfail, nfail);
	return nfail ? 1 : 0;
}
