#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <stdint.h>

#include "dcwfilter.h"
#include "dcw.h"

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
int acceptDCW_for(uint8_t *data, int checksum_mode, int repeat_mode)
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

	if ( checksum && !checksumDCW(data)) {
#ifdef MCS_DCWSTATS
		DCWSTATS_REJECT(DCW_REJ_CHECKSUM);
#endif
		return 0;
	}
	if ( isnullDCW(data) ) {
#ifdef MCS_DCWSTATS
		DCWSTATS_REJECT(DCW_REJ_NULL);
#endif
		return 0;
	}
	if ( repeat && isbadDCW(data) ) {
#ifdef MCS_DCWSTATS
		DCWSTATS_REJECT(DCW_REJ_REPEAT);
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
#endif
		return 0;
	}
#ifdef MCS_DCWSTATS
	DCWSTATS_ACCEPT();
#endif
	return 1;
}

int acceptDCW(uint8_t *data)
{
	return acceptDCW_for(data, DCWFILTER_INHERIT, DCWFILTER_INHERIT);
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


