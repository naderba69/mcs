/*
 * test_skipcwc.c -- TASK R36 (D87): the SKIPCWC SID gate, pure.
 *
 * skipcwc_applies() decides whether the delivery-time checksum
 * test engages for a (profile, SID). Cases: off is stock; ONLY and
 * EXCLUDE scope it; both compose; SID 0 (callers that do not know
 * the SID) is gated only by the broad lists, never by an ONLY list.
 */
#include <stdio.h>
#include <string.h>
#include "../src/skipcwc.h"

static int fails = 0, total = 0;
#define CK(cond) do { total++; if (!(cond)) { fails++; \
	printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

int main(void)
{
	static const uint16_t only[] = { 0x0550, 0x0551, 0x067E };
	static const uint16_t excl[] = { 0x00A6, 0x00BE };

	/* off: never applies, whatever the lists say */
	CK(skipcwc_applies(0, 0, NULL, 0, 0, NULL, 0, 0x0550) == 0);
	CK(skipcwc_applies(0, 1, only, 3, 1, excl, 2, 0x0550) == 0);

	/* on, no lists: applies to every SID */
	CK(skipcwc_applies(1, 0, NULL, 0, 0, NULL, 0, 0x0550) == 1);
	CK(skipcwc_applies(1, 0, NULL, 0, 0, NULL, 0, 0x0000) == 1);

	/* ONLY list: listed engages, unlisted does not */
	CK(skipcwc_applies(1, 1, only, 3, 0, NULL, 0, 0x0550) == 1);
	CK(skipcwc_applies(1, 1, only, 3, 0, NULL, 0, 0x067E) == 1);
	CK(skipcwc_applies(1, 1, only, 3, 0, NULL, 0, 0x1234) == 0);
	CK(skipcwc_applies(1, 1, only, 3, 0, NULL, 0, 0x0000) == 0);

	/* EXCLUDE list: listed is spared, everything else engages */
	CK(skipcwc_applies(1, 0, NULL, 0, 1, excl, 2, 0x00A6) == 0);
	CK(skipcwc_applies(1, 0, NULL, 0, 1, excl, 2, 0x0550) == 1);

	/* both: must be in ONLY and not in EXCLUDE */
	CK(skipcwc_applies(1, 1, only, 3, 1, excl, 2, 0x0551) == 1);
	CK(skipcwc_applies(1, 1, only, 3, 1, only, 3, 0x0550) == 0);
	CK(skipcwc_applies(1, 1, only, 3, 1, excl, 2, 0x1234) == 0);

	/* an active flag with an empty list behaves as written */
	CK(skipcwc_applies(1, 1, only, 0, 0, NULL, 0, 0x0550) == 0);
	CK(skipcwc_applies(1, 0, NULL, 0, 1, excl, 0, 0x0550) == 1);

	printf("test_skipcwc: %d/%d ok\n", total - fails, total);
	return fails ? 1 : 0;
}
