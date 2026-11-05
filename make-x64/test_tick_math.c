/* Regression coverage for the shared 32-bit GetTickCount arithmetic used by
 * ECM scheduling and Radegast deadline selection. */
#include <stdint.h>
#include <stdio.h>
#include "../src/tick_math.h"

static int passed, failed;

#define CHECK(condition, description) do { \
	if (condition) { passed++; printf("  [ ok ] %s\n", description); } \
	else { failed++; printf("  [FAIL] %s\n", description); } \
} while (0)

int main(void)
{
	const uint32_t before_wrap = UINT32_MAX - 15u;
	const uint32_t after_wrap = 0x10u;

	printf("test_tick_math: wrap-safe elapsed time and deadlines\n");
	CHECK(tick32_elapsed(after_wrap, before_wrap) == 32u,
	      "elapsed subtraction returns the exact interval across rollover");
	CHECK(tick32_elapsed(after_wrap, before_wrap) > 31u &&
	      tick32_elapsed(after_wrap, before_wrap) <= 32u,
	      "elapsed thresholds preserve strict/inclusive boundaries across rollover");
	CHECK(tick32_elapsed(after_wrap, before_wrap) >
	      tick32_elapsed(after_wrap, 0x04u),
	      "server ordering treats a high pre-wrap timestamp as older than a low post-wrap timestamp");
	CHECK(tick32_elapsed(after_wrap, before_wrap) >= 32u,
	      "server time-per-ECM delay reaches its threshold across rollover");
	CHECK(tick32_elapsed_less_than(5u, UINT32_MAX - 4u, 11u),
	      "MINTIME rejects a 10 ms response across wrap when threshold is 11 ms");
	CHECK(!tick32_elapsed_less_than(5u, UINT32_MAX - 4u, 10u),
	      "MINTIME accepts exactly the configured threshold across wrap");
	CHECK(!tick32_elapsed_less_than(5u, UINT32_MAX - 4u, 0u),
	      "a zero MINTIME threshold never rejects");
	CHECK(!tick32_deadline_reached(0x0fu, after_wrap),
	      "a wrapped deadline one millisecond ahead is not due");
	CHECK(tick32_deadline_reached(after_wrap, after_wrap),
	      "a deadline is due at the exact tick");
	CHECK(tick32_deadline_reached(after_wrap, before_wrap),
	      "a deadline before rollover is recognized as elapsed");
	CHECK(tick32_deadline_before(0x05u, 0x20u) &&
	      !tick32_deadline_before(0x20u, 0x05u),
	      "relative deadline ordering works when the earlier value wrapped low");
	CHECK(tick32_until(0x10u, before_wrap) == 32,
	      "future deadline distance is positive across rollover");
	CHECK(tick32_until(before_wrap, after_wrap) == -32,
	      "past deadline distance is negative across rollover");

	printf("== %d/%d passed, %d failed ==\n", passed, passed + failed, failed);
	return failed ? 1 : 0;
}
