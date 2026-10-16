#ifndef MCS_SKIPCWC_H
#define MCS_SKIPCWC_H

#include <stdint.h>

/*
 * skipcwc.h -- TASK R36 (D87): the SID gate of `ENABLE SKIPCWC`.
 *
 * The R24-era configs armed whole SID lists around a CW-validity
 * test that was never implemented (`ENABLE SKIPCWC` and its
 * ONLY/EXCLUDE list keys were read by no parser). R36 implements
 * the test itself in the accept layer (see config.c's
 * acceptDCW_profile_ca_sid): on an armed profile, a CW for a gated
 * SID is delivered only if it passes the full structural battery
 * with the checksum layer FORCED -- a key that cannot prove its
 * checksum is skipped and the request stays open for another
 * source, instead of being sent to the client to become a black
 * screen. CA systems whose genuine keys carry no checksum bytes
 * (casystem.h) are never forced: there the test would reject every
 * genuine key.
 *
 * This header is the pure gate so the unit test can drive it:
 *   enabled=0            -> never applies (the default; stock)
 *   ONLY list active     -> applies only to listed SIDs
 *   EXCLUDE list active  -> applies to every SID except listed
 *   both active          -> listed by ONLY and not by EXCLUDE
 */

static int skipcwc_sid_listed(const uint16_t *list, int total, uint16_t sid)
{
	int i;
	for (i = 0; i < total; i++) if (list[i] == sid) return 1;
	return 0;
}

static int skipcwc_applies(int enabled,
			   int only_active, const uint16_t *only, int n_only,
			   int excl_active, const uint16_t *excl, int n_excl,
			   uint16_t sid)
{
	if (!enabled) return 0;
	if (only_active && !skipcwc_sid_listed(only, n_only, sid)) return 0;
	if (excl_active && skipcwc_sid_listed(excl, n_excl, sid)) return 0;
	return 1;
}

#endif /* MCS_SKIPCWC_H */
