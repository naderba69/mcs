/*
 * test_rotation.c — TASK 1.6 tests.
 *
 * Includes the real header. Sizes are PRINTED, not recalled: this suite has been
 * wrong about sizeof three times, and the real build uses -fpack-struct, which
 * changes the figures again (see the note in make-x64/Makefile).
 */
#include <stdio.h>
#include <stdlib.h>
#include "../src/rotation.h"

static int pass_n = 0, fail_n = 0;

#define CHECK(cond, name) do { \
	if (cond) { pass_n++; printf("  [ ok ] %s\n", name); } \
	else { fail_n++; printf("  [FAIL] %s\n", name); } \
} while (0)

#define SRV   2u
#define CAC   3u
#define CAID1 0x0500
#define CAID2 0x0604
#define SID1  100
#define SID2  200
#define PROV1 0x043800u
#define PROV2 0x000000u
#define PEER7    (7u | 0x010000u)
#define CLIENT7  (7u | 0x020000u)

static struct rotation_table tab;

static void t_sizes(void)
{
	printf("  -- sizes (measured, not remembered) --\n");
	printf("     sizeof(struct rotation_entry) = %u bytes\n",
	       (unsigned)sizeof(struct rotation_entry));
	printf("     sizeof(struct rotation_table) = %u bytes\n",
	       (unsigned)sizeof(struct rotation_table));
	/*
	 * Three counters since TASK 1.9 (avoided, capped, deferred). Kept as a
	 * composition rather than a magic number so the assertion means "no hidden
	 * padding", which is what it is for.
	 */
	CHECK(sizeof(struct rotation_table) ==
	      ROTATION_SLOTS * sizeof(struct rotation_entry) + 3 * sizeof(uint32_t),
	      "table is slots * entry + two counters (no hidden padding)");
}

static void t_no_record_no_avoid(void)
{
	rotation_init(&tab);
	CHECK(rotation_should_avoid(&tab, SRV, 1, CAID1, SID1, PROV1) == 0,
	      "an unknown source is not avoided (no record != bad)");
	CHECK(rotation_count_bad(&tab, CAID1, SID1, PROV1) == 0,
	      "and the channel has nothing recorded");
}

static void t_bad_then_avoid(void)
{
	rotation_init(&tab);
	CHECK(rotation_note_bad(&tab, SRV, 1, CAID1, SID1, PROV1, 1000) == 1,
	      "first bad record is new");
	CHECK(rotation_should_avoid(&tab, SRV, 1, CAID1, SID1, PROV1) == 1,
	      "that source is now avoided for that channel");
	CHECK(rotation_count_bad(&tab, CAID1, SID1, PROV1) == 1, "one source recorded");
	CHECK(tab.avoided == 1, "avoided counter incremented");

	CHECK(rotation_note_bad(&tab, SRV, 1, CAID1, SID1, PROV1, 2000) == 0,
	      "recording the same source again is not a new event");
	CHECK(rotation_count_bad(&tab, CAID1, SID1, PROV1) == 1,
	      "and does not double-count");
}

static void t_channel_granularity(void)
{
	rotation_init(&tab);
	rotation_note_bad(&tab, SRV, 1, CAID1, SID1, PROV1, 1000);

	CHECK(rotation_should_avoid(&tab, SRV, 1, CAID1, SID2, PROV1) == 0,
	      "a bad record on SID 100 does not touch SID 200");
	CHECK(rotation_should_avoid(&tab, SRV, 1, CAID2, SID1, PROV1) == 0,
	      "nor a different CAID");
	CHECK(rotation_should_avoid(&tab, SRV, 1, CAID1, SID1, PROV2) == 0,
	      "nor a different PROVID");
	CHECK(rotation_should_avoid(&tab, SRV, 2, CAID1, SID1, PROV1) == 0,
	      "nor a different source on the same channel");
	CHECK(rotation_should_avoid(&tab, CAC, 1, CAID1, SID1, PROV1) == 0,
	      "same id under a different source type is a different source");

	rotation_note_bad(&tab, SRV, PEER7, CAID1, SID1, PROV1, 1000);
	CHECK(rotation_should_avoid(&tab, SRV, CLIENT7, CAID1, SID1, PROV1) == 0,
	      "GR4: a CSP peer and a cacheex client with the same id are separate");
	CHECK(rotation_should_avoid(&tab, SRV, PEER7, CAID1, SID1, PROV1) == 1,
	      "and the recorded one is avoided");
}

static void t_good_clears(void)
{
	rotation_init(&tab);
	rotation_note_bad(&tab, SRV, 1, CAID1, SID1, PROV1, 1000);
	rotation_note_bad(&tab, SRV, 2, CAID1, SID1, PROV1, 1100);
	CHECK(rotation_count_bad(&tab, CAID1, SID1, PROV1) == 2, "two sources recorded");

	CHECK(rotation_note_good(&tab, CAID1, SID1, PROV1) == 2,
	      "a good delivery clears the channel and reports how many");
	CHECK(rotation_should_avoid(&tab, SRV, 1, CAID1, SID1, PROV1) == 0,
	      "so a source is not avoided forever on one bad key");
	CHECK(rotation_should_avoid(&tab, SRV, 2, CAID1, SID1, PROV1) == 0,
	      "and the second one is released too");
	CHECK(rotation_note_good(&tab, CAID1, SID1, PROV1) == 0,
	      "clearing an already-clear channel is harmless");
}

/*
 * THE SAFETY VALVE. This is the test that matters most: rotation must not be
 * able to black-screen a channel by avoiding everything that could answer it.
 */
static void t_cap_never_strands_the_channel(void)
{
	int i, avoided = 0;
	rotation_init(&tab);

	for (i = 1; i <= ROTATION_PER_SLOT; i++)
		rotation_note_bad(&tab, SRV, (uint32_t)i, CAID1, SID1, PROV1, 1000 + (uint32_t)i);

	CHECK(rotation_count_bad(&tab, CAID1, SID1, PROV1) == ROTATION_PER_SLOT,
	      "all four sources recorded against the channel");

	for (i = 1; i <= ROTATION_PER_SLOT; i++)
		if (rotation_should_avoid(&tab, SRV, (uint32_t)i, CAID1, SID1, PROV1))
			avoided++;

	CHECK(avoided == ROTATION_MAX_AVOID,
	      "exactly ROTATION_MAX_AVOID are avoided, no more");
	CHECK(avoided < ROTATION_PER_SLOT,
	      "GR3: at least one recorded source stays reachable, so the channel cannot be stranded");
	CHECK(rotation_should_avoid(&tab, SRV, (uint32_t)ROTATION_PER_SLOT, CAID1, SID1, PROV1) == 0,
	      "and it is the NEWEST record that stays reachable, not the oldest");
	CHECK(rotation_should_avoid(&tab, SRV, 1, CAID1, SID1, PROV1) == 1,
	      "the oldest, longest-known-bad source is still avoided");
	CHECK(tab.capped > 0, "the cap firing is counted, not silent");
}

static void t_eviction(void)
{
	int i, ok = 1;
	rotation_init(&tab);

	for (i = 0; i < ROTATION_SLOTS; i++)
		rotation_note_bad(&tab, SRV, 1, CAID1, 9000 + (uint16_t)i, PROV1, (uint32_t)i);

	CHECK(tab.e[ROTATION_SLOTS - 1].inuse == 1 || rotation_count_bad(&tab, CAID1, 9000 + ROTATION_SLOTS - 1, PROV1) == 1,
	      "table fills");

	for (i = 0; i < ROTATION_SLOTS; i++)
		if (rotation_count_bad(&tab, CAID1, 9000 + (uint16_t)i, PROV1) != 1) ok = 0;
	CHECK(ok, "no entry evicted while a free slot existed");

	/* refresh channel 9000 so it is newest; the victim must be 9001 */
	rotation_note_bad(&tab, SRV, 1, CAID1, 9000, PROV1, 5000);
	rotation_note_bad(&tab, SRV, 1, CAID1, 9999, PROV1, 6000);
	CHECK(rotation_count_bad(&tab, CAID1, 9000, PROV1) == 1,
	      "eviction is longest-unseen: the refreshed channel survived");
	CHECK(rotation_count_bad(&tab, CAID1, 9001, PROV1) == 0,
	      "and the longest-unseen channel was evicted");
	CHECK(rotation_count_bad(&tab, CAID1, 9999, PROV1) == 1, "the new record is live");
}

static void t_bounded_and_nullsafe(void)
{
	long i;
	rotation_init(&tab);

	for (i = 0; i < 200000; i++)
		rotation_note_bad(&tab, SRV, 1, CAID1, (uint16_t)(i & 0x3FFF), PROV1, (uint32_t)i);

	{
		int n = 0;
		for (i = 0; i < ROTATION_SLOTS; i++) if (tab.e[i].inuse) n++;
		CHECK(n == ROTATION_SLOTS, "GR9: 200k records leave exactly ROTATION_SLOTS entries");
	}

	CHECK(rotation_should_avoid(NULL, SRV, 1, CAID1, SID1, PROV1) == 0,
	      "NULL table: avoid returns 0");
	CHECK(rotation_note_bad(NULL, SRV, 1, CAID1, SID1, PROV1, 1) == 0,
	      "NULL table: note_bad returns 0");
	CHECK(rotation_note_good(NULL, CAID1, SID1, PROV1) == 0,
	      "NULL table: note_good returns 0");
	CHECK(rotation_count_bad(NULL, CAID1, SID1, PROV1) == 0,
	      "NULL table: count returns 0");
	rotation_init(NULL);
	CHECK(1, "NULL table: init does not fault");
}

int main(void)
{
	printf("test_rotation: TASK 1.6 instant rotation\n");
	t_sizes();
	t_no_record_no_avoid();
	t_bad_then_avoid();
	t_channel_granularity();
	t_good_clears();
	t_cap_never_strands_the_channel();
	t_eviction();
	t_bounded_and_nullsafe();
	printf("== %d/%d passed, %d failed ==\n", pass_n, pass_n + fail_n, fail_n);
	return fail_n ? 1 : 0;
}
