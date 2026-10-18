/*
 * test_dcwtiming.c -- TASK R44: DCW VALIDECMTIME and ENABLE ECMLOOPING, pure.
 *
 * dcw_arrives_too_late(): off (0) is stock and never refuses; a CW inside the
 * limit passes; one past it is refused; the boundary itself passes; a clock
 * that steps backwards never refuses.
 *
 * ecmloop_may_retry(): off is stock; a round is allowed only when no server
 * is still awaiting an answer; at most ECMLOOP_MAX extra rounds.
 */
#include <stdio.h>
#include <string.h>
#include "../src/dcwtiming.h"

static int fails = 0, total = 0;
#define CK(cond) do { total++; if (!(cond)) { fails++; \
	printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

int main(void)
{
	/* VALIDECMTIME: off is stock */
	CK(dcw_arrives_too_late(5000, 1000, 0) == 0);
	CK(dcw_arrives_too_late(5000, 1000, -5) == 0);

	/* inside the limit passes, boundary passes, one past is refused */
	CK(dcw_arrives_too_late(1500, 1000, 800) == 0);   /* 500 ms */
	CK(dcw_arrives_too_late(1800, 1000, 800) == 0);   /* exactly 800 ms */
	CK(dcw_arrives_too_late(1801, 1000, 800) == 1);   /* 801 ms */
	CK(dcw_arrives_too_late(9000, 1000, 800) == 1);   /* far too late */

	/* a clock that stepped backwards must never refuse a key */
	CK(dcw_arrives_too_late(500, 1000, 800) == 0);

	/* ECMLOOPING: off is stock, whatever the counters say */
	CK(ecmloop_may_retry(0, 0, 1, 0) == 0);
	CK(ecmloop_may_retry(0, 0, 1, 3) == 0);

	/* on: only when nobody is still awaiting an answer */
	CK(ecmloop_may_retry(1, 0, 1, 1) == 0);
	CK(ecmloop_may_retry(1, 0, 1, 2) == 0);
	CK(ecmloop_may_retry(1, 0, 1, 0) == 1);
	CK(ecmloop_may_retry(1, 0, 0, 0) == 0);   /* no server was asked: no round */

	/* on: bounded by ECMLOOP_MAX extra rounds */
	CK(ecmloop_may_retry(1, 1, 1, 0) == 1);
	CK(ecmloop_may_retry(1, ECMLOOP_MAX, 1, 0) == 0);
	CK(ecmloop_may_retry(1, ECMLOOP_MAX + 1, 1, 0) == 0);

	printf("test_dcwtiming: %d checks, %d failed\n", total, fails);
	return fails ? 1 : 0;
}
