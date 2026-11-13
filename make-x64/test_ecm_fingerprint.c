/* test_ecm_fingerprint.c — R48 ECM fingerprint (observe-only) regression.
 * Pins: first sightings are normal, established lengths stay normal, a
 * length outside the established [min,max] after enough samples is an
 * anomaly, channels are independent, zero length is ignored, and the
 * table is bounded (fail-open past capacity).
 */
#include <stdio.h>
#include <string.h>

#include "../src/ecm-fingerprint.h"

int main(void)
{
	int fails = 0, i;
#define CHECK(cond, msg) do { \
	if (cond) printf("  [ ok ] %s\n", msg); \
	else { printf("  [FAIL] %s\n", msg); fails++; } \
} while (0)

	printf("test_ecm_fingerprint: per-channel ECM length learning\n");

	ecmfp_reset();
	CHECK(ecmfp_note(0x1884, 0x1136, 100) == 0, "first sighting normal");
	CHECK(ecmfp_note(0x1884, 0x1136, 100) == 0, "same length normal");
	CHECK(ecmfp_note(0x1884, 0x1136, 100) == 0, "third normal");
	CHECK(ecmfp_note(0x1884, 0x1136, 100) == 0, "fourth normal");
	CHECK(ecmfp_note(0x1884, 0x1136, 100) == 0, "fifth normal (established)");
	CHECK(ecmfp_note(0x1884, 0x1136, 100) == 0, "established same length normal");
	CHECK(ecmfp_note(0x1884, 0x1136, 150) == 1, "novel larger length is anomaly");
	CHECK(ecmfp_note(0x1884, 0x1136, 50) == 1, "novel smaller length is anomaly");
	CHECK(ecmfp_note(0x1884, 0x1136, 100) == 0, "in-range length normal again");
	CHECK(ecmfp_note(0x1884, 0x1136, 0) == 0, "zero length ignored");

	/* Channels are independent */
	CHECK(ecmfp_note(0x1884, 0x9999, 200) == 0, "other SID first sighting normal");
	CHECK(ecmfp_note(0x09BD, 0x1136, 200) == 0, "other CAID first sighting normal");

	/* Range widens as new lengths are learned (no longer anomalous) */
	ecmfp_reset();
	for (i = 0; i < 5; i++) ecmfp_note(0x0500, 0x0001, 64);
	CHECK(ecmfp_note(0x0500, 0x0001, 80) == 1, "80 anomalous before learning");
	CHECK(ecmfp_note(0x0500, 0x0001, 80) == 0, "80 normal once learned");

	/* Bounded table: fill past capacity, new channels fail open */
	ecmfp_reset();
	for (i = 0; i < 5; i++) ecmfp_note(0x1800, 0x1000, 100); /* establish first */
	for (i = 1; i < ECMFP_SLOTS + 10; i++)
		ecmfp_note(0x1800, (uint16_t)(0x1000 + i), 100);
	CHECK(ecmfp_note(0x1800, 0x1000, 100) == 0, "first channel still tracked");
	CHECK(ecmfp_note(0x1800, 0x1000, 999) == 1, "first channel anomaly still works");
	CHECK(ecmfp_note(0x1800, 0x2000, 100) == 0, "overflow channel fails open (normal)");

	printf("test_ecm_fingerprint: %d failed\n", fails);
	return fails ? 1 : 0;
}
