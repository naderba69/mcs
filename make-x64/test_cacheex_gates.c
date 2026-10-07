/*
 * test_cacheex_gates.c -- TASK R1: the three exchange-gate predicates.
 *
 * These are the pure decision helpers from src/cacheex_gates.h. The
 * enforcement site (cache_setdcw) is exercised live by the `ce` target
 * in tests/stability.mk; here we pin the DECISION TABLE so a future edit
 * cannot silently widen a gate.
 *
 * Family classification (cex_is_exchange):
 *   - PEER_CSP alone            -> NOT exchange (CSP has its own machinery)
 *   - any other origin flag      -> exchange
 *   - CSP flag + client flag     -> NOT exchange (CSP wins: impossible in
 *                                   practice, the origins are exclusive)
 *   - no origin flag at all      -> NOT exchange (defensive)
 *
 * LOCAL_ONLY: drops only when armed + exchange + no waiting ECM. An
 * unarmed gate NEVER drops, even for unsolicited pushes.
 *
 * BLOCK_FAKE_CW: rejects only when armed + exchange + tight tests failed.
 * An armed gate never rejects a key that passes both tests.
 *
 * CWCHECK: floor is max(profile, global); floor < 1 disarms; the floor
 * can never be weakened below CACHE THRESHOLD; CEX_CHECK_MAX is the cap
 * the parser clamps to.
 */
#include <stdio.h>
#include "cacheex_gates.h"

static int fails = 0;
#define OK(cond, name) do { \
	if (cond) printf("  [ ok ] %s\n", name); \
	else { printf("  [FAIL] %s\n", name); fails++; } \
} while (0)

int main(void)
{
	printf("test_cacheex_gates: family classification\n");
	OK( !cex_is_exchange(PEER_CSP | 7),                        "CSP origin is not exchange" );
	OK( !cex_is_exchange(PEER_CSP),                            "bare CSP is not exchange" );
	OK(  cex_is_exchange(PEER_CCCAM_CLIENT | 3),               "cccam client is exchange" );
	OK(  cex_is_exchange(PEER_CAMD35_CLIENT | 4),              "camd35 client is exchange" );
	OK(  cex_is_exchange(PEER_CS378X_CLIENT | 5),              "cs378x client is exchange" );
	OK(  cex_is_exchange(PEER_CACHEEX_SERVER | 6),             "cacheex server is exchange" );
	OK( !cex_is_exchange(0),                                   "zero origin is not exchange" );
	OK( !cex_is_exchange(PEER_CSP | PEER_CCCAM_CLIENT),        "CSP flag dominates" );

	printf("test_cacheex_gates: LOCAL_ONLY truth table\n");
	OK(  cex_localonly_drop(1, 1, 0),  "armed+exchange+no waiter -> drop" );
	OK( !cex_localonly_drop(1, 1, 1),  "armed+exchange+waiter -> keep" );
	OK( !cex_localonly_drop(1, 0, 0),  "armed+CSP+no waiter -> keep (out of scope)" );
	OK( !cex_localonly_drop(0, 1, 0),  "unarmed -> never drops" );

	printf("test_cacheex_gates: BLOCK_FAKE_CW truth table\n");
	OK(  cex_fake_reject(1, 1, 0),  "armed+exchange+tests failed -> reject" );
	OK( !cex_fake_reject(1, 1, 1),  "armed+exchange+tests passed -> keep" );
	OK( !cex_fake_reject(1, 0, 0),  "armed+CSP -> never rejects here" );
	OK( !cex_fake_reject(0, 1, 0),  "unarmed -> never rejects" );

	printf("test_cacheex_gates: CWCHECK floor\n");
	OK(  cex_check_below_floor(2, 1, 1, 1), "below floor 2 -> hold" );
	OK( !cex_check_below_floor(2, 1, 1, 2), "at floor 2 -> serve" );
	OK( !cex_check_below_floor(2, 1, 1, 5), "past floor -> serve" );
	OK(  cex_check_below_floor(2, 3, 1, 2), "global 3 beats profile 2 -> hold at 2" );
	OK( !cex_check_below_floor(2, 3, 1, 3), "global 3 floor reached -> serve" );
	OK( !cex_check_below_floor(0, 1, 1, 1), "floor 0 disarms" );
	OK( !cex_check_below_floor(2, 1, 0, 1), "CSP arrivals never held" );
	OK( CEX_CHECK_MAX == 5,                 "parser cap is 5" );

	if (fails) { printf("test_cacheex_gates: %d FAIL\n", fails); return 1; }
	printf("test_cacheex_gates: all ok\n");
	return 0;
}
