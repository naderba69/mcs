/*
 * Band 112 unit package: the peer-skip label mapping.
 *
 * This one line dominated a real operator's trace as a wall of "skipped"
 * for requests that had in fact been sent and were merely waiting. The
 * mapping from gate values to meaning is pinned here so it cannot regress
 * into "looks like a stall" again: status 1/2 = in flight; fallow=0 and
 * sendreq=0 = the two genuine config causes; anything unparsable = the
 * caller's generic text (NULL), never a guessed diagnosis.
 */
#include <stdio.h>
#include <string.h>
#include "../src/trace-detail-labels.h"

static int fails = 0;
static void expect(const char *detail, const char *want, const char *what)
{
	const char *got = mcs_trace_label_peer_skip(detail);
	if (want && got != want && (!got || strcmp(got, want))) {
		printf("FAIL: %s\n  want %s\n  got  %s\n", what, want, got ? got : "(null)");
		fails++;
	} else if (!want && got) {
		printf("FAIL: %s -- expected fallback, got \"%s\"\n", what, got);
		fails++;
	}
}

int main(void)
{
	expect("cache peer request skipped: fallow=1 sendreq=1 status=1",
	       MCS_LABEL_PEER_WAITING, "REQ in flight must not read as a skip");
	expect("cache peer request skipped: fallow=1 sendreq=1 status=2",
	       MCS_LABEL_PEER_WAITING, "REQ2 (retry in flight) also waiting");
	expect("cache peer request skipped: fallow=0 sendreq=1 status=0",
	       MCS_LABEL_PEER_NO_CACHE, "fallowcache=0 is a config cause");
	expect("cache peer request skipped: fallow=1 sendreq=0 status=0",
	       MCS_LABEL_PEER_NO_REQ, "cachesendreq=0 is a config cause");
	/* status=0 with both gates open cannot happen in the product (it would
	 * have sent the request); no diagnosis must be invented for it. */
	expect("cache peer request skipped: fallow=1 sendreq=1 status=0", NULL,
	       "no invented label for an impossible combination");
	expect("cache peer request skipped", NULL, "truncated detail falls back");
	expect("cache peer request skipped: fallow=x sendreq=1 status=1", NULL,
	       "unparsable values fall back");
	expect("cache lookup invoked; pipe delivery and result not confirmed", NULL,
	       "other events untouched");
	if (fails) { printf("trace-labels: %d FAILURES\n", fails); return 1; }
	printf("trace-labels: PASS (peer-skip gate values map to the right meaning)\n");
	return 0;
}
