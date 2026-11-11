/* test_provider_config.c — R48 provider table init/query regression.
 * Verifies provider_config_init() populates the static table without
 * crashing and that query helpers return sane values. Does NOT wire
 * fallback_timeout into any timeout path (values remain inactive).
 */
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "../src/ecm-provider-config.h"

int main(void)
{
	int fails = 0;
#define CHECK(cond, msg) do { \
	if (cond) printf("  [ ok ] %s\n", msg); \
	else { printf("  [FAIL] %s\n", msg); fails++; } \
} while (0)

	printf("test_provider_config: provider table init/query\n");

	provider_config_init();
	CHECK(provider_cfg_tab.count > 50, "table populated (>50 entries)");
	CHECK(provider_cfg_tab.count <= MAX_PROVIDER_CONFIGS, "count within capacity");

	/* Known entries from the header table */
	{
		struct provider_ecm_config *c = provider_get_config(0x1883, 0x003311);
		CHECK(c != NULL, "1883/003311 found");
		if (c) {
			CHECK(c->disable_crc_check == 1, "1883/003311 disable_crc=1");
			CHECK(c->fallback_timeout == 1500, "1883/003311 fallback=1500");
		}
	}
	{
		struct provider_ecm_config *c = provider_get_config(0x1810, 0x000000);
		CHECK(c != NULL, "1810/000000 found");
		if (c) CHECK(c->fallback_timeout == 1300, "1810/000000 fallback=1300");
	}
	{
		/* Unknown CAID returns NULL (no crash) */
		struct provider_ecm_config *c = provider_get_config(0xFFFF, 0xFFFFFF);
		CHECK(c == NULL, "unknown CAID returns NULL");
	}
	{
		/* Query helpers are safe on unknown */
		CHECK(provider_should_disable_crc(0xFFFF, 0xFFFFFF) == 0, "unknown disable_crc=0");
		CHECK(provider_get_fallback_timeout(0xFFFF, 0xFFFFFF) == 0, "unknown fallback=0");
		CHECK(provider_get_cw_cache_time(0xFFFF, 0xFFFFFF) == 0, "unknown cache_time=0");
	}
	{
		/* Wildcard: 1810/004101 exact, 1810/999999 falls back to 1810/000000 */
		struct provider_ecm_config *exact = provider_get_config(0x1810, 0x004101);
		struct provider_ecm_config *wild = provider_get_config(0x1810, 0x999999);
		CHECK(exact != NULL, "1810/004101 exact found");
		CHECK(wild != NULL, "1810 unknown provider falls back to wildcard");
	}
	{
		/* Re-init is idempotent (no growth, no crash) */
		int n1 = provider_cfg_tab.count;
		provider_config_init();
		CHECK(provider_cfg_tab.count == n1, "re-init idempotent");
	}

	printf("test_provider_config: %d failed\n", fails);
	return fails ? 1 : 0;
}
