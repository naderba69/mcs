/* test_cw_timing.c -- unit test for cw-timing.h (R48).
 *
 * Verifies:
 *  1. A slow CW (>= threshold) is not flagged.
 *  2. A fast CW (< threshold) is flagged.
 *  3. Per-peer tracking is independent.
 *  4. cwtime_reset clears state.
 */
#include <stdio.h>
#include <stdint.h>
#include "../src/cw-timing.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
	if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
	else { printf("ok: %s\n", msg); } \
} while (0)

int main(void)
{
	cwtime_reset();

	/* 1. Slow CW not flagged. */
	CHECK(cwtime_anomaly_note(100, 200) == 0, "slow CW (200ms) not flagged");
	CHECK(cwtime_anomaly_note(100, 50) == 0, "CW at 50ms not flagged");
	CHECK(cwtime_anomaly_note(100, 30) == 0, "CW at exactly 30ms not flagged");

	/* 2. Fast CW flagged. */
	CHECK(cwtime_anomaly_note(100, 10) == 1, "fast CW (10ms) flagged");
	CHECK(cwtime_anomaly_note(100, 5) == 1, "fast CW (5ms) flagged");
	CHECK(cwtime_anomaly_note(100, 0) == 1, "instant CW (0ms) flagged");

	/* 3. Per-peer independence. */
	CHECK(cwtime_anomaly_note(200, 200) == 0, "different peer slow not flagged");
	CHECK(cwtime_anomaly_note(200, 10) == 1, "different peer fast flagged");

	/* 4. Reset clears. */
	cwtime_reset();
	CHECK(cwtime_anomaly_note(100, 200) == 0, "after reset, slow not flagged");

	if (failures) {
		printf("== %d failures ==\n", failures);
		return 1;
	}
	printf("== all passed ==\n");
	return 0;
}
