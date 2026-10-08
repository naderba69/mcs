/*
 * dcwstats.h -- Phase 4: rejection accounting for MultiCS r82a
 *
 * Problem this solves
 * -------------------
 * acceptDCW() (dcw.c) rejects a control word for four different reasons and
 * reports only a bare 0. When a bouquet shows a black screen, or when clients
 * get wrong codes, there is no way to tell WHY the key was dropped. The
 * operator is left guessing between:
 *   - a checksum the filter considers invalid,
 *   - three repeated bytes (isbadDCW),
 *   - a null / half-null key,
 *   - an entry in the BAD-DCW list.
 *
 * Design constraints
 * ------------------
 *   * Header-only: no new .o has to be added to OBJECTS, so the upstream
 *     Makefile stays untouched.
 *   * Zero overhead when disabled: every counter update sits behind
 *     `if (cfg_dcwstats_on)`.
 *   * No locking: the counters are plain `unsigned long`. A torn increment
 *     on a diagnostic counter is acceptable; correctness of key handling is
 *     not affected.
 */
#ifndef MCS_DCWSTATS_H
#define MCS_DCWSTATS_H

/*
 * Rejection reasons. Deliberately starts at 1 so that 0 means
 * "accepted / not counted" and cannot be confused with a reason.
 */
#define DCW_REJ_NONE       0
#define DCW_REJ_CHECKSUM   1   /* checksumDCW() failed                       */
#define DCW_REJ_NULL       2   /* isnullDCW(): key is (half) zeroed          */
#define DCW_REJ_REPEAT     3   /* isbadDCW(): three equal non-zero bytes     */
#define DCW_REJ_BADLIST    4   /* matched an entry of the BAD-DCW list       */
/* TASK R1 -- the three cache-exchange gates (cacheex_gates.h). Each one
 * counts a key that a PROFILE ARMED ITS GATE FOR; an unarmed profile
 * never lands here, so the counters stay at zero exactly like stock. */
#define DCW_REJ_CEXLOCAL   5   /* CACHEEX LOCAL_ONLY: no waiting local ECM   */
#define DCW_REJ_CEXFAKE    6   /* CACHEEX BLOCK_FAKE_CW: forced tests failed */
#define DCW_REJ_CEXCONF    7   /* CACHEEX CWCHECK: same-CW floor not reached */
/* TASK R2 (D56) -- the delivery-time cycle check: a key whose DECLARED half
 * contradicts what the ECM itself asks for, refused only where the profile
 * armed DCWFILTER CYCLE. A declared marker is evidence, not proof (GR3), so
 * the gate stays opt-in and the counter is what makes its cost visible. */
#define DCW_REJ_CYCLE      8   /* DCWFILTER CYCLE: wrong half at delivery    */
#define DCW_REJ_CONSENSUS  9   /* R8 consensus: lost a weighted cw decision  */
#define DCW_REJ_COUNT      10

/* Acceptance counter, kept alongside so a ratio can be computed. */
#define DCW_ACC_COUNT      0

/*
 * Global switch. Defaults to OFF so a stock build behaves exactly like
 * before: no writes to the counter array at all.
 * Turned on later by the config option `DCW STATS: ON`.
 */
/*
 * Defined in dcw.c (see below). Kept as plain declarations here so that this
 * header can be included more than once into a single translation unit --
 * build/test_dcwstats.c does exactly that to compare the instrumented build
 * against an unmodified one.
 */
extern int            dcwstats_on;
extern unsigned long  dcwstats[DCW_REJ_COUNT];

/* Record one rejection. Compiles to a single branch when disabled. */
#define DCWSTATS_REJECT(reason)                       \
	do {                                          \
		if (dcwstats_on) dcwstats[reason]++;   \
	} while (0)

/* Record one acceptance. */
#define DCWSTATS_ACCEPT()                             \
	do {                                          \
		if (dcwstats_on) dcwstats[DCW_ACC_COUNT]++; \
	} while (0)

/* Reset every counter (used by `telnet> dcwstats reset`). */
/* TASK R13 (D67): used by telnet.c when the counters are compiled in; the
 * unit TUs include this header without that path, so say so explicitly
 * instead of letting -Wall report it in half the test build. */
__attribute__((unused))
static void dcwstats_reset(void)
{
	int i;
	for (i = 0; i < DCW_REJ_COUNT; i++) dcwstats[i] = 0;
}

/* Human-readable name, for the telnet/web output. Never returns NULL. */
	static const char *dcwstats_reason_name(int reason)
{
	switch (reason) {
	case DCW_REJ_NONE:     return "accepted";
	case DCW_REJ_CHECKSUM: return "checksum";
	case DCW_REJ_NULL:     return "null/half-null";
	case DCW_REJ_REPEAT:   return "repeat-3-bytes";
	case DCW_REJ_BADLIST:  return "bad-dcw-list";
	case DCW_REJ_CEXLOCAL: return "cacheex-local-only";
	case DCW_REJ_CEXFAKE:  return "cacheex-fake-cw";
	case DCW_REJ_CEXCONF:  return "cacheex-confirm-wait";
	case DCW_REJ_CYCLE:    return "cycle-contradiction";
	case DCW_REJ_CONSENSUS: return "consensus-mismatch";
	default:               return "unknown";
	}
}

#endif /* MCS_DCWSTATS_H */
