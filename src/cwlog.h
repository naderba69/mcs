/*
 * cwlog.h -- TASK R11 (D65): the per-CW verdict ring.
 *
 * One question the operator kept asking that the counters cannot answer:
 * not "how many keys were refused" but "WHICH key, from WHOM, on WHICH
 * channel, and what happened to it". The counters (dcwstats) answer in
 * aggregates; the debug log answers in lines that scroll away. This ring
 * keeps the last CWLOG_CAP verdicts in memory -- channel, source peer,
 * verdict, reason, and the 16 key bytes themselves -- for one web page
 * and one /json section.
 *
 * Pure module, no server dependencies: a fixed static array, one mutex,
 * zero allocation after load. Fed ONLY from the MCS_DCWSTATS-gated sites
 * in clustredcache.c, and only when dcwstats_on is set -- the ring lives
 * and dies with the counters' own switch, so no new config key exists.
 * The stock flavour never links this .o and never sees a byte of it.
 */
#ifndef MCS_CWLOG_H
#define MCS_CWLOG_H

#include <stdint.h>

/* Ring capacity. Fixed: no config key, ~2.6 kB of .bss, one page. */
#define CWLOG_CAP       100

/* Verdicts. */
#define CWLOG_DELIVERED 0   /* piped to a waiting ECM            */
#define CWLOG_REFUSED   1   /* a gate ate it                     */
#define CWLOG_HELD      2   /* parked pending corroboration (R8) */
#define CWLOG_STORED    3   /* cached, no waiter                 */

/* Reasons 0..9 MIRROR the DCW_REJ_* numbering in dcwstats.h -- test_cwlog
 * asserts the name strings stay identical. 10+ are ring-only codes for
 * verdicts the counters never see. */
#define CWLOG_R_HOLD       10   /* consensus hold, awaiting a second voter */
#define CWLOG_R_CONVICTED  11   /* negative-memory gate (cwn_gate)         */
#define CWLOG_R_THRESHOLD  12   /* below CACHE THRESHOLD (stock skip)      */
#define CWLOG_R_PROFILE    13   /* profile/global structural filter; the
                             * specific test counted inside dcw.c        */

struct cwlog_entry {
	uint32_t tick;      /* GetTickCount() ms at the verdict      */
	uint16_t caid;
	uint32_t provid;
	uint16_t sid;
	uint8_t  verdict;   /* CWLOG_*                               */
	uint8_t  reason;    /* DCW_REJ_* or CWLOG_R_*                */
	int32_t  peerid;    /* raw cache peerid (masks in peer_origin.h);
			     * -1 when the ring has no source to name     */
	uint8_t  cw[16];    /* the key itself, so the page can show it   */
};

/* Verdict name, for the web page and /json. Never returns NULL. */
static const char *cwlog_verdict_name(int verdict)
{
	switch (verdict) {
	case CWLOG_DELIVERED: return "delivered";
	case CWLOG_REFUSED:   return "refused";
	case CWLOG_HELD:      return "held";
	case CWLOG_STORED:    return "stored";
	default:              return "unknown";
	}
}

/* Reason name. 0..9 must stay word-for-word the dcwstats names (the
 * ring rows and the counters describe the same events); 10+ are the
 * ring-only codes. Never returns NULL. */
static const char *cwlog_reason_name(int reason)
{
	switch (reason) {
	case 0:  return "accepted";
	case 1:  return "checksum";
	case 2:  return "null/half-null";
	case 3:  return "repeat-3-bytes";
	case 4:  return "bad-dcw-list";
	case 5:  return "cacheex-local-only";
	case 6:  return "cacheex-fake-cw";
	case 7:  return "cacheex-confirm-wait";
	case 8:  return "cycle-contradiction";
	case 9:  return "consensus-mismatch";
	case CWLOG_R_HOLD:      return "held-for-corroboration";
	case CWLOG_R_CONVICTED: return "convicted-memory";
	case CWLOG_R_THRESHOLD: return "below-threshold";
	case CWLOG_R_PROFILE:   return "profile-filter";
	default:                return "unknown";
	}
}

#endif /* MCS_CWLOG_H */
