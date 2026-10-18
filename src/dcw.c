#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <stdint.h>

#include "dcwfilter.h"
#include "dcw.h"
#include "casystem.h"	/* TASK R26 (D77): the modern-CA registry */

/* Black screen prevention systems */
#include "blackscreen-detector.h"
#include "cwemergency.h"
#include "cwretry.h"
#include "fallback.h"

/*
 * TASK 1.10b — runtime gates for the two `acceptDCW()` filters that are known
 * to reject VALID control words on some systems.
 *
 * r82a applies `checksumDCW()` and `isbadDCW()` unconditionally at all 18 call
 * sites, with no way to turn either off. That is fine when a bouquet's keys
 * carry the standard 3-byte-sum checksum, and fatal when they legitimately do
 * not: every key is dropped, the profile shows a black screen, and the server
 * looks healthy.
 *
 * Both default to 1, so a config file written for r82a behaves identically.
 * They are plain ints rather than `cfg` fields because dcw.c is a standalone
 * translation unit that deliberately does not include config.h — which keeps
 * it unit-testable without dragging the whole server in.
 *
 * A test binary defines MCS_DCWFILTER_NOGLOBALS and supplies its own copy.
 */
#ifndef MCS_DCWFILTER_NOGLOBALS
int dcw_filter_checksum = 1;
int dcw_filter_repeat   = 1;
int dcw_filter_cycle    = 0; /* TASK R2: the cycle gate starts disarmed (D56) */
int dcw_filter_pattern  = 0; /* TASK R28 (D79): the fake-shape gate starts disarmed */
#endif


int checksumDCW(uint8_t *data)
{
    if(data[3] != (uint8_t)((data[0] + data[1] + data[2]) & 0xFF) || data[7] != (uint8_t)((data[4] + data[5] + data[6]) & 0xFF)) {
      return 0;
    }
    if(data[11] != (uint8_t)((data[8] + data[9] + data[10]) & 0xFF) || data[15] != (uint8_t)((data[12] + data[13] + data[14]) & 0xFF)) {
      return 0;
    }
    return 1;
}


int isnullDCW(uint8_t *data)
{
	int a0 = ( !data[0] && !data[1] && !data[2] );
	int a1 = ( !data[4] && !data[5] && !data[6] );
	int b0 = ( !data[8] && !data[9] && !data[10] );
	int b1 = ( !data[12] && !data[13] && !data[14] );
	return ( (a0||a1) && (b0||b1) );
}

int isbadDCW(uint8_t *data)
{
	if ( data[0]!=0 && data[0]==data[1] && data[0]==data[2] ) return 1;
	if ( data[4]!=0 && data[4]==data[5] && data[4]==data[6] ) return 1;
	if ( data[8]!=0 && data[8]==data[9] && data[8]==data[10] ) return 1;
	if ( data[12]!=0 && data[12]==data[13] && data[12]==data[14] ) return 1;
	return 0;
}

#ifdef MCS_DCWSTATS
#include "dcwstats.h"

/*
 * Storage for the counters, provided by dcw.o so that no new entry is needed
 * in OBJECTS.
 *
 * Declared `weak` so a test binary that supplies its own strong definitions
 * links without a duplicate-symbol clash.
 *
 * Guarded by MCS_DCWSTATS_NOGLOBALS for the one case `weak` cannot cover:
 * build/test_dcwstats.c includes this file twice into a single translation
 * unit, to diff the instrumented decisions against the unmodified r82a ones.
 * That translation unit defines the storage once itself.
 */
#ifndef MCS_DCWSTATS_NOGLOBALS
#if defined(__GNUC__)
#define MCS_DCWSTATS_WEAK __attribute__((weak))
#else
#define MCS_DCWSTATS_WEAK
#endif
MCS_DCWSTATS_WEAK int           dcwstats_on = 0;
MCS_DCWSTATS_WEAK unsigned long dcwstats[DCW_REJ_COUNT] = {0};
/* TASK R47 (D110): the reason of this thread's last acceptDCW_impl
 * reject (0 = none). setdcw.c reads it to name a card refusal in the
 * ring. Thread-local: each ECM thread reads only its own reject. */
__thread int dcw_last_reject_reason = 0;
#endif
#endif

/*
 * TASK 3.14 — the BAD-DCW list r82a parsed and then left commented out
 * inside acceptDCW(). The nodes live in config.c and are freed on
 * reload, so this TU keeps its own fixed table. A reader never follows
 * a pointer the config thread is about to free. An empty table (the
 * default, and a config with no BAD-DCW line) makes the walk a no-op,
 * so the decision is the one r82a made.
 *
 * The sequence counter is odd while the table is being replaced. A
 * reader that sees a torn update does not reject: a false reject is
 * worse than letting one listed key through during a reload.
 */
#ifndef MCS_DCWFILTER_NOGLOBALS
static uint8_t dcw_bad_tab[DCW_BADLIST_MAX][16];
static int dcw_bad_n;
static volatile unsigned dcw_bad_seq;

int dcw_on_badlist(uint8_t *data)
{
	unsigned seq;
	int n, i, spins;

	for (spins = 0; spins < 8; spins++) {
		seq = dcw_bad_seq;
		if (seq & 1) continue;
		__sync_synchronize();
		n = dcw_bad_n;
		if (n < 0) n = 0;
		if (n > DCW_BADLIST_MAX) n = DCW_BADLIST_MAX;
		for (i = 0; i < n; i++) {
			if (!memcmp(dcw_bad_tab[i], data, 16)) {
				__sync_synchronize();
				if (dcw_bad_seq == seq) return 1;
				goto retry;
			}
		}
		__sync_synchronize();
		if (dcw_bad_seq == seq) return 0;
retry:
		;
	}
	return 0;
}

void dcw_badlist_publish(const uint8_t *keys, int n)
{
	int i;

	if (n < 0) n = 0;
	if (n > DCW_BADLIST_MAX) n = DCW_BADLIST_MAX;
	dcw_bad_seq++;
	__sync_synchronize();
	dcw_bad_n = n;
	for (i = 0; i < n; i++)
		memcpy(dcw_bad_tab[i], keys + (i * 16), 16);
	__sync_synchronize();
	dcw_bad_seq++;
}
#endif

/*
 * Phase 4 instrumentation (2026-09-22), plus TASK 3.14.
 *
 * The first three tests are the original r82a tests, in the same order,
 * with the same early returns. The fourth is the BAD-DCW walk r82a had
 * commented out. It changes a decision only when the operator listed
 * that exact key. The optional DCWSTATS_* bookkeeping is compiled in
 * only when MCS_DCWSTATS is defined.
 *
 * Why this matters: when a bouquet shows a black screen, or when clients
 * receive wrong codes, the operator could not tell which test dropped
 * the key. With `DCW STATS: ON` the answer becomes a counter, including
 * `bad-dcw-list` for a key the operator named.
 */
/*
 * TASK R28 (D79) — dcw_pattern_fake: is this key shaped like a fake?
 *
 * One question, asked of the key alone, with the same semantics the
 * evidence scanners already use (the agreement is pinned by
 * test_cwfake against dcwstruct_scan()/cwcm_pairs):
 *   - complement mirror on the six free byte pairs (cwcm:
 *     5+/6 pairs; the checksum-repaired variant passes checksumDCW
 *     and stock delivery -- this is the classic fake generator), or
 *   - both halves byte-identical, or
 *   - a half that is one repeated byte, a constant non-zero
 *     arithmetic step, or only two distinct byte values (dcwstruct).
 *
 * Genuine control words are indistinguishable from random; the
 * documented chance of a random key tripping any of these is ~1e-12
 * or below per key. That asymmetry -- generator-shaped fakes always
 * trip, honest keys effectively never do -- is what makes refusal
 * safe to offer, opt-in, on the anti-fake path. A NDS-style zeroed
 * half skips the per-half tests exactly as in dcwstruct_scan.
 */
int dcw_pattern_fake(uint8_t *data)
{
	static const int FA[6] = {0,1,2,4,5,6};
	static const int FB[6] = {8,9,10,12,13,14};
	int pairs = 0, i, j;

	for (i = 0; i < 6; i++)
		if (data[FB[i]] == (uint8_t)~data[FA[i]]) pairs++;
	if (pairs >= 5) return 1;

	{
		int same = 1;
		for (i = 0; i < 8; i++) if (data[8+i] != data[i]) same = 0;
		if (same) return 1;
	}
	for (j = 0; j < 2; j++) {
		const uint8_t *h = data + j*8;
		int null_half = 1, all_same = 1, mono = 1, distinct = 0;
		uint8_t seen[8];
		uint8_t step;
		for (i = 0; i < 8; i++) if (h[i]) null_half = 0;
		if (null_half) continue;
		step = (uint8_t)(h[1] - h[0]);
		for (i = 1; i < 8; i++) {
			int k, known = 0;
			if (h[i] != h[0]) all_same = 0;
			if ((uint8_t)(h[i] - h[i-1]) != step) mono = 0;
			for (k = 0; k < distinct; k++) if (seen[k] == h[i]) known = 1;
			if (!known && distinct < 8) seen[distinct++] = h[i];
		}
		if (all_same) return 1;
		if (step && mono) return 1;
		if (distinct == 2) return 1;
	}
	return 0;
}

static int acceptDCW_impl(uint8_t *data, uint16_t caid, int checksum_mode,
		     int repeat_mode, int ca_modern, int pattern_mode)
{
	int checksum = dcw_filter_checksum;
	int repeat = dcw_filter_repeat;

	/* TASK 3.15 — a profile override replaces the global gate. Any
	 * other value, including 0, inherits. Null and the BAD-DCW list
	 * below are not overridable. */
	if (checksum_mode == DCWFILTER_ON) checksum = 1;
	else if (checksum_mode == DCWFILTER_OFF) checksum = 0;
	if (repeat_mode == DCWFILTER_ON) repeat = 1;
	else if (repeat_mode == DCWFILTER_OFF) repeat = 0;

	/*
	 * TASK R26 (D77) — only in INHERIT mode, only when the operator
	 * armed `CA MODERN: ON` for this profile, and only for a CA
	 * system whose genuine control words carry no DVB-CSA checksum
	 * bytes (casystem.h). An explicit DCWFILTER CHECKSUM: ON above
	 * has already forced the test and is not second-guessed here;
	 * ca_modern == 0 (the default) never reaches this branch's
	 * effect, so the decision stays bit-for-bit stock.
	 */
	if (checksum && checksum_mode == DCWFILTER_INHERIT && ca_modern &&
	    !ca_cw_checksum_expected(caid))
		checksum = 0;

	if ( checksum && !checksumDCW(data)) {
#ifdef MCS_DCWSTATS
		DCWSTATS_REJECT(DCW_REJ_CHECKSUM);
		dcw_last_reject_reason = DCW_REJ_CHECKSUM;
#endif
		return 0;
	}
	if ( isnullDCW(data) ) {
#ifdef MCS_DCWSTATS
		DCWSTATS_REJECT(DCW_REJ_NULL);
		dcw_last_reject_reason = DCW_REJ_NULL;
#endif
		return 0;
	}
	if ( repeat && isbadDCW(data) ) {
#ifdef MCS_DCWSTATS
		DCWSTATS_REJECT(DCW_REJ_REPEAT);
		dcw_last_reject_reason = DCW_REJ_REPEAT;
#endif
		return 0;
	}
	/*
	 * TASK 3.14 — after the three r82a tests, so a key that already
	 * fails the checksum is still counted as checksum, not as a list
	 * hit. memcmp, not dcwcmp16: this TU does not include ecmdata.h.
	 */
	if ( dcw_on_badlist(data) ) {
#ifdef MCS_DCWSTATS
		DCWSTATS_REJECT(DCW_REJ_BADLIST);
		dcw_last_reject_reason = DCW_REJ_BADLIST;
#endif
		return 0;
	}
	/*
	 * TASK R28 (D79) — the FAKE-shape refusal, after the r82a tests
	 * and the BAD-DCW walk so reason attribution stays stable.
	 * pattern_mode follows the DCWFILTER tristate (ON forces,
	 * OFF disables, anything else inherits the global gate, which
	 * defaults OFF): with the gate disarmed every decision here is
	 * bit-for-bit the pre-R28 one.
	 */
	{
		int pattern = dcw_filter_pattern;
		if (pattern_mode == DCWFILTER_ON) pattern = 1;
		else if (pattern_mode == DCWFILTER_OFF) pattern = 0;
		if (pattern && dcw_pattern_fake(data)) {
#ifdef MCS_DCWSTATS
			DCWSTATS_REJECT(DCW_REJ_PATTERN);
			dcw_last_reject_reason = DCW_REJ_PATTERN;
#endif
			return 0;
		}
	}
#ifdef MCS_DCWSTATS
	DCWSTATS_ACCEPT();
#endif
	return 1;
}

int acceptDCW_for_ca(uint8_t *data, uint16_t caid, int checksum_mode,
		     int repeat_mode, int ca_modern)
{
	return acceptDCW_impl(data, caid, checksum_mode, repeat_mode,
			      ca_modern, DCWFILTER_INHERIT);
}

/*
 * TASK R28 (D79) — acceptDCW_for_ca plus the per-profile FAKE-shape
 * gate (`DCWFILTER PATTERN:` tristate, see dcwfilter.h).
 */
int acceptDCW_pattern(uint8_t *data, uint16_t caid, int checksum_mode,
		      int repeat_mode, int ca_modern, int pattern_mode)
{
	return acceptDCW_impl(data, caid, checksum_mode, repeat_mode,
			      ca_modern, pattern_mode);
}

int acceptDCW_for(uint8_t *data, int checksum_mode, int repeat_mode)
{
	return acceptDCW_for_ca(data, 0, checksum_mode, repeat_mode, 0);
}

int acceptDCW(uint8_t *data)
{
	return acceptDCW_for(data, DCWFILTER_INHERIT, DCWFILTER_INHERIT);
}

/*
 * Black Screen Prevention - DCW Failure Handler
 * Tracks CW failures and triggers anti-black-screen systems
 */
static struct bs_channel_tracker {
	uint16_t caid;
	uint16_t sid;
	uint32_t provid;
	uint32_t consecutive_failures;
	uint32_t last_failure_time;
	uint32_t total_failures;
} bs_trackers[256];
static int bs_tracker_count = 0;

void dcw_on_failure(uint16_t caid, uint16_t sid, uint32_t provid)
{
	int i;
	struct bs_channel_tracker *t = NULL;
	
	/* Find existing tracker */
	for (i = 0; i < bs_tracker_count; i++) {
		if (bs_trackers[i].caid == caid && bs_trackers[i].sid == sid) {
			t = &bs_trackers[i];
			break;
		}
	}
	
	/* Create new tracker if not found */
	if (!t && bs_tracker_count < 256) {
		t = &bs_trackers[bs_tracker_count++];
		t->caid = caid;
		t->sid = sid;
		t->provid = provid;
		t->consecutive_failures = 0;
		t->last_failure_time = 0;
		t->total_failures = 0;
	}
	
	if (t) {
		t->consecutive_failures++;
		t->total_failures++;
		t->last_failure_time = time(NULL);
		
		/* Counter only: no switch is performed here (see DECISIONS.md,
		 * "dcw_on_failure ... only stamps log words"). The log says so. */
		if (t->consecutive_failures >= 2) {
			debugf(0, "CW failures %04X:%04X: %d in a row (counter only, no action taken)\n",
				caid, sid, t->consecutive_failures);
		}
	}
}

void dcw_on_success(uint16_t caid, uint16_t sid)
{
	int i;
	
	/* Reset failure counter on success */
	for (i = 0; i < bs_tracker_count; i++) {
		if (bs_trackers[i].caid == caid && bs_trackers[i].sid == sid) {
			bs_trackers[i].consecutive_failures = 0;
			break;
		}
	}
}

int dcw_needs_emergency(uint16_t caid, uint16_t sid)
{
	int i;
	
	for (i = 0; i < bs_tracker_count; i++) {
		if (bs_trackers[i].caid == caid && bs_trackers[i].sid == sid) {
			return (bs_trackers[i].consecutive_failures >= 3);
		}
	}
	return 0;
}

int similarcw( uint8_t *cw1, uint8_t *cw2 )
{
	int i;
	int count = 0;
	for(i=0; i<8; i++) if (cw1[i]==cw2[i]) count++;
	if (count>3) return 1;
	return 0;
}

// for nds
int ishalfnulledcw( uint8_t dcw[16] )
{
	char nullcw[8] = "\0\0\0\0\0\0\0\0";
	if ( !memcmp(dcw,nullcw,8) || !memcmp(dcw+8,nullcw,8) ) return 1;
	return 0;
}


#ifdef MCS_DCWSTATS
/* TASK R65: the /diag page's read-only view of the CA registry. casystem.h
 * keeps these helpers static; the stats page reads them through these three
 * wrappers, so the page and acceptDCW can never disagree about a system. */
const char *diag_ca_system(uint16_t caid)
{
	return ca_name(caid);
}

const char *diag_ca_format(uint16_t caid)
{
	return ca_cwformat_name(ca_cwformat(caid));
}

int diag_ca_checksum_expected(uint16_t caid)
{
	return ca_cw_checksum_expected(caid);
}

/* TASK R65: the global DCWFILTER CYCLE, which a profile on INHERIT follows. */
int diag_cycle_global(void)
{
	return dcw_filter_cycle ? 1 : 0;
}
#endif /* MCS_DCWSTATS */
