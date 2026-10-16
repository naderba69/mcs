/*
 * make-x64/test_chanhealth.c -- TASK R15 (D70)
 *
 * Channel health: the per-channel (caid, provid, sid) delivery/refusal/
 * hold rates and the "dying of refusals" alert. This test drives the REAL
 * src/chanhealth.h (compiled straight into this binary -- the layer is a
 * pure header, like cwlate.h and ptguard.h before it), never a copy of
 * its rules.
 *
 * What the roadmap's acceptance sentence demands of this layer:
 *   "the dead channel is named in the log and the page before a viewer
 *    complains"  ->  the alert must FIRE (the caller prints the name),
 *   "consecutive refusals and zero delivery"  ->  the streak is the
 *    window and delivery resets it,
 *   "no setting key; the counters are the surface"  ->  nothing here
 *    reads a config flag; there is none to read.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "../src/chanhealth.h"

static int fails = 0;
static int checks = 0;

static void ok(int cond, const char *what)
{
	checks++;
	printf("%s %s\n", cond ? "  [ ok ]" : "  [FAIL]", what);
	if (!cond) fails++;
}

int main(void)
{
	struct chanhealth_tab t;
	struct chanhealth_row rows[CHANHEALTH_SLOTS];
	struct chanhealth_row *r;
	int f, n, i;
	uint32_t now = 1000;

	memset(&t, 0, sizeof(t));

	/* --- the alert ladder: 5 consecutive refusals, zero delivery --- */
	f = chanhealth_note(&t, CWLOG_REFUSED, 0x0100, 0x000000, 0x0065, now++);
	ok(f == 0, "refusal 1: no alert below the streak");
	f = chanhealth_note(&t, CWLOG_REFUSED, 0x0100, 0x000000, 0x0065, now++);
	f |= chanhealth_note(&t, CWLOG_REFUSED, 0x0100, 0x000000, 0x0065, now++);
	f |= chanhealth_note(&t, CWLOG_REFUSED, 0x0100, 0x000000, 0x0065, now++);
	ok(f == 0, "refusals 2..4: still silent");
	f = chanhealth_note(&t, CWLOG_REFUSED, 0x0100, 0x000000, 0x0065, now++);
	ok(f == 1, "refusal 5: the alert FIRES (the caller names the channel)");

	r = chanhealth_lookup(&t, 0x0100, 0x000000, 0x0065, now);
	ok(r && r->streak == 5 && r->refused == 5 && r->delivered == 0,
	   "the alert row is the roadmap's shape: streak 5, refused 5, delivered 0");
	ok(r && r->alerted == 1, "the episode is latched after firing");
	ok(r && CHANHEALTH_DYING(r), "the dying flag is up while the streak holds");

	f = chanhealth_note(&t, CWLOG_REFUSED, 0x0100, 0x000000, 0x0065, now++);
	ok(f == 0, "refusal 6: latched -- one line per episode (GR9)");
	ok(r->streak == 6, "the streak keeps growing while latched");
	ok(t.alerts == 1, "the table counts one episode");

	/* --- a delivery ends the episode and re-arms the line --- */
	f = chanhealth_note(&t, CWLOG_DELIVERED, 0x0100, 0x000000, 0x0065, now++);
	ok(f == 0 && r->delivered == 1 && r->streak == 0 && r->alerted == 0,
	   "delivery: counted, streak reset, the latch re-armed");
	ok(!CHANHEALTH_DYING(r), "the dying flag clears on recovery");

	for (i = 0; i < 4; i++)
		f = chanhealth_note(&t, CWLOG_REFUSED, 0x0100, 0x000000, 0x0065, now++);
	ok(f == 0, "a second drought stays silent below 5");
	f = chanhealth_note(&t, CWLOG_REFUSED, 0x0100, 0x000000, 0x0065, now++);
	ok(f == 1, "a second episode fires again at the streak");
	ok(t.alerts == 2, "two episodes, two lines");

	/* --- holds: counted, but neither noise nor a picture --- */
	memset(&t, 0, sizeof(t));
	now = 5000;
	ok(chanhealth_note(&t, CWLOG_HELD, 0x0100, 0x000000, 0x0066, now++) == 0,
	   "a hold never fires");
	r = chanhealth_lookup(&t, 0x0100, 0x000000, 0x0066, now);
	ok(r && r->held == 1 && r->streak == 0, "a hold is not a refusal");
	chanhealth_note(&t, CWLOG_REFUSED, 0x0100, 0x000000, 0x0066, now++);
	chanhealth_note(&t, CWLOG_REFUSED, 0x0100, 0x000000, 0x0066, now++);
	chanhealth_note(&t, CWLOG_HELD, 0x0100, 0x000000, 0x0066, now++);
	chanhealth_note(&t, CWLOG_REFUSED, 0x0100, 0x000000, 0x0066, now++);
	chanhealth_note(&t, CWLOG_REFUSED, 0x0100, 0x000000, 0x0066, now++);
	f = chanhealth_note(&t, CWLOG_REFUSED, 0x0100, 0x000000, 0x0066, now++);
	ok(f == 1, "holds inside a drought do not break the refusal streak");
	ok(r->streak == 5 && r->held == 2 && r->refused == 5,
	   "the mixed drought counts refusals in streak and holds in held");

	/* --- per-channel isolation --- */
	memset(&t, 0, sizeof(t));
	now = 9000;
	for (i = 0; i < 4; i++) {
		chanhealth_note(&t, CWLOG_REFUSED, 0x0100, 0x000000, 0x0064, now);
		chanhealth_note(&t, CWLOG_REFUSED, 0x1884, 0x000000, 0x0065, now);
		now++;
	}
	ok(chanhealth_note(&t, CWLOG_REFUSED, 0x0100, 0x000000, 0x0064, now++) == 1,
	   "channel A's fifth refusal fires A's alert");
	ok(chanhealth_note(&t, CWLOG_REFUSED, 0x1884, 0x000000, 0x0065, now++) == 1,
	   "channel B's fifth refusal fires B's own alert");
	ok(t.alerts == 2 && t.notes == 10, "two channels, two episodes, ten notes");

	/* --- verdicts that are neither picture nor noise cost nothing --- */
	memset(&t, 0, sizeof(t));
	ok(chanhealth_note(&t, CWLOG_STORED, 0x0100, 0x000000, 0x0070, 1) == 0,
	   "STORED is ignored");
	ok(chanhealth_note(&t, CWLOG_DISPUTED, 0x0100, 0x000000, 0x0070, 1) == 0,
	   "DISPUTED is ignored");
	ok(chanhealth_note(&t, 99, 0x0100, 0x000000, 0x0070, 1) == 0,
	   "an unknown verdict is ignored");
	ok(t.notes == 0, "ignored verdicts never reach a counter");
	n = chanhealth_copy(&t, rows, CHANHEALTH_SLOTS);
	ok(n == 0, "ignored verdicts claim no row");

	/* --- the boundary of the alert rule --- */
	memset(&t, 0, sizeof(t));
	now = 20000;
	for (i = 0; i < CHANHEALTH_STREAK - 1; i++)
		chanhealth_note(&t, CWLOG_REFUSED, 0x0100, 0x000000, 0x0071, now++);
	r = chanhealth_lookup(&t, 0x0100, 0x000000, 0x0071, now);
	ok(r && !CHANHEALTH_DYING(r), "streak 4 is not dying");
	chanhealth_note(&t, CWLOG_REFUSED, 0x0100, 0x000000, 0x0071, now++);
	ok(CHANHEALTH_DYING(r), "streak 5 IS dying -- exactly at the threshold");

	/* --- the table: LRU eviction, 24 slots --- */
	memset(&t, 0, sizeof(t));
	now = 30000;
	for (i = 0; i < CHANHEALTH_SLOTS; i++) {
		chanhealth_note(&t, CWLOG_DELIVERED, 0x0100, 0x000000,
				(uint16_t)(0x0100 + i), now++);
	}
	ok(t.evicts == 0, "24 channels fit exactly");
	n = chanhealth_copy(&t, rows, CHANHEALTH_SLOTS);
	ok(n == CHANHEALTH_SLOTS, "copy returns every live row");
	/* one more channel evicts the OLDEST (the first noted) */
	chanhealth_note(&t, CWLOG_DELIVERED, 0x0100, 0x000000, 0x0999, now++);
	ok(t.evicts == 1, "channel 25 evicts exactly one row");
	n = chanhealth_copy(&t, rows, CHANHEALTH_SLOTS);
	ok(n == CHANHEALTH_SLOTS, "the table stays at capacity");
	r = chanhealth_lookup(&t, 0x0100, 0x000000, 0x0999, now);
	ok(r && r->delivered == 1, "the new channel claimed the victim's slot");
	r = chanhealth_lookup(&t, 0x0100, 0x000000, 0x0100, now);
	ok(r && r->delivered == 0 && r->used == 1 && r->sid == 0x0100,
	   "the evicted channel comes back with fresh counters (the victim was the oldest)");

	/* --- the copy bound --- */
	memset(&t, 0, sizeof(t));
	now = 40000;
	for (i = 0; i < 5; i++)
		chanhealth_note(&t, CWLOG_DELIVERED, 0x0100, 0x000000,
				(uint16_t)(0x0200 + i), now++);
	n = chanhealth_copy(&t, rows, 3);
	ok(n == 3, "copy honours max");
	ok(t.notes == 5, "the notes counter sees every counted verdict");

	printf("test_chanhealth: %s (%d checks)\n", fails ? "FAIL" : "ok",
	       checks);
	return fails ? 1 : 0;
}
