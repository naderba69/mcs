#include <stdio.h>
#include <string.h>
#include "../src/supersmartfilter.h"

struct ssf_config ssf_cfg;

static int failures;

#define CHECK(condition, description) do { \
    if (condition) printf("PASS: %s\n", description); \
    else { printf("FAIL: %s\n", description); failures++; } \
} while (0)

static void make_hash(uint8_t hash[16], uint32_t id)
{
    memset(hash, 0, 16);
    hash[0] = (uint8_t)(id >> 24);
    hash[1] = (uint8_t)(id >> 16);
    hash[2] = (uint8_t)(id >> 8);
    hash[3] = (uint8_t)id;
}

int main(void)
{
    const uint32_t start = 0xFFFFFF00u;
    const uint32_t deadline = start + 2000u;
    struct ssf_peer_stats *peer;
    uint8_t cw[16], hash[16];
    int i, old_found, recent_found, new_found;

    supersmartfilter_init();
    ssf_cfg.badcwratio = 1;
    ssf_cfg.badcwratiomin = 1;
    ssf_cfg.minreplies = 1;
    ssf_cfg.unblock_time = 2; /* seconds */

    ssf_record_cw_result(17, 1, start);
    peer = ssf_get_peer(17);
    CHECK(peer && peer->blocked_until == deadline,
          "peer block duration is stored in milliseconds and wraps safely");
    CHECK(ssf_is_peer_blocked(17, start),
          "peer remains blocked before a post-wrap deadline");
    CHECK(ssf_is_peer_blocked(17, deadline - 1u),
          "peer remains blocked through the last millisecond before expiry");
    CHECK(!ssf_is_peer_blocked(17, deadline) && peer->blocked_until == 0,
          "peer is automatically unblocked exactly at a post-wrap deadline");
    CHECK(ssf_stats.peers_blocked == 1 && ssf_stats.peers_unblocked == 1,
          "wrap-safe block and unblock each update their counters once");

    supersmartfilter_init();
    ssf_cfg.badcwratio = 1;
    ssf_cfg.badcwratiomin = 1;
    ssf_cfg.minreplies = 1;
    ssf_cfg.unblock_time = 1;
    ssf_record_cw_result(18, 1, UINT32_MAX - 999u);
    peer = ssf_get_peer(18);
    CHECK(peer && peer->blocked && peer->blocked_until == 0,
          "a valid block deadline that wraps exactly to zero remains explicitly active");
    CHECK(ssf_is_peer_blocked(18, UINT32_MAX) &&
          !ssf_is_peer_blocked(18, 0) && !peer->blocked,
          "zero-valued wrapped block deadline expires at zero instead of disabling the block");

    memset(cw, 0x3c, sizeof(cw));
    ssf_cfg.sf_history = SSF_MAX_HISTORY;
    ssf_cfg.filter_time_ms = 50;
    memset(&ssf_history[0], 0, sizeof(ssf_history[0]));
    memcpy(ssf_history[0].cw, cw, sizeof(cw));
    ssf_history[0].caid = 0x1884;
    ssf_history[0].provid = 0;
    ssf_history[0].sid = 0x100;
    ssf_history[0].timestamp = 0;
    ssf_history[0].used = 1;
    ssf_history_index = 1;
    CHECK(ssf_check_duplicate(cw, 0x1884, 0, 0x100, 20),
          "duplicate history recognizes a valid timestamp at tick zero");
    ssf_history[0].timestamp = UINT32_MAX - 10u;
    CHECK(ssf_check_duplicate(cw, 0x1884, 0, 0x100, 39) &&
          !ssf_check_duplicate(cw, 0x1884, 0, 0x100, 40),
          "duplicate window honors the exact wrap-safe age boundary");

    supersmartfilter_init();
    ssf_cfg.sf_multicheck = 1;
    ssf_cfg.sf_mc_match = 2;
    ssf_cfg.sf_mc_wait_ms = 5;
    make_hash(hash, 77);
    CHECK(!ssf_check_multicheck(hash, cw, 0x1884, 1, 0) &&
          ssf_check_multicheck(hash, cw, 0x1884, 1, 6),
          "multi-check timeout uses first-seen tick zero as a valid baseline");

    supersmartfilter_init();
    ssf_cfg.sf_multicheck = 1;
    ssf_cfg.sf_mc_match = 2;
    ssf_cfg.sf_mc_wait_ms = 500;
    for (i = 0; i < SSF_MAX_MC_ENTRIES; i++) {
        uint32_t age = (i == 0) ? 50000u : (uint32_t)(SSF_MAX_MC_ENTRIES - i);
        make_hash(hash, (uint32_t)i + 1u);
        ssf_check_multicheck(hash, cw, 0x1884, (uint32_t)i + 1u, 100u - age);
    }
    make_hash(hash, SSF_MAX_MC_ENTRIES + 1u);
    ssf_check_multicheck(hash, cw, 0x1884, 1000, 100u);
    old_found = recent_found = new_found = 0;
    for (i = 0; i < SSF_MAX_MC_ENTRIES; i++) {
        if (!ssf_mc_table[i].used) continue;
        make_hash(hash, 1);
        if (memcmp(ssf_mc_table[i].ecm_hash, hash, 16) == 0) old_found = 1;
        make_hash(hash, 2);
        if (memcmp(ssf_mc_table[i].ecm_hash, hash, 16) == 0) recent_found = 1;
        make_hash(hash, SSF_MAX_MC_ENTRIES + 1u);
        if (memcmp(ssf_mc_table[i].ecm_hash, hash, 16) == 0) new_found = 1;
    }
    CHECK(ssf_mc_count == SSF_MAX_MC_ENTRIES && !old_found &&
          recent_found && new_found,
          "full multi-check table evicts only the greatest elapsed age across rollover");

    printf("%d SuperSmartFilter tick checks failed\n", failures);
    return failures ? 1 : 0;
}
