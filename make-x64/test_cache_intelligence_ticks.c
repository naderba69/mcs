#include <stdio.h>
#include <string.h>
#include "../src/cache-intelligence.h"

static int failures;

#define CHECK(condition, description) do { \
    if (condition) printf("PASS: %s\n", description); \
    else { printf("FAIL: %s\n", description); failures++; } \
} while (0)

int main(void)
{
    ci_peer_stats_t peer;
    ci_peer_stats_t peer_entries[3];
    ci_channel_stats_t channel_entries[3];
    ci_predict_entry_t entries[3];
    uint32_t wrapped_now = 0xFFFFFFF0u;

    memset(&peer, 0, sizeof(peer));
    peer.tier = CI_TIER_BANNED;
    peer.trust_global = 80;
    peer.ban_until = 0x00000020u;
    peer.ban_active = 1;
    CHECK(ci_peer_is_banned(&peer, wrapped_now),
          "cache-intelligence ban remains active before a wrapped deadline");
    CHECK(!ci_peer_is_banned(&peer, 0x00000020u) &&
          peer.ban_until == 0 && !peer.ban_active && peer.tier == CI_TIER_A,
          "cache-intelligence ban expires and restores tier at the wrapped deadline");

    memset(&peer, 0, sizeof(peer));
    peer.tier = CI_TIER_BANNED;
    peer.trust_global = 80;
    peer.ban_until = 0;
    peer.ban_active = 1;
    CHECK(ci_peer_is_banned(&peer, UINT32_MAX),
          "a zero-valued wrapped ban deadline is still active before tick zero");
    CHECK(!ci_peer_is_banned(&peer, 0) && !peer.ban_active,
          "a zero-valued wrapped ban deadline expires exactly at tick zero");

    memset(peer_entries, 0, sizeof(peer_entries));
    peer_entries[0].total_requests = 1;
    peer_entries[1].total_requests = 1;
    peer_entries[2].total_requests = 1;
    peer_entries[0].last_seen = 0xFFFFFFF0u;
    peer_entries[1].last_seen = 0x00000010u;
    peer_entries[2].last_seen = 0x00000020u;
    CHECK(ci_peer_victim_index(peer_entries, 3, 0x00000030u) == 0,
          "full peer table eviction selects the greatest wrap-safe idle age");
    peer_entries[1].total_requests = 0;
    peer_entries[1].last_seen = 0;
    CHECK(ci_peer_victim_index(peer_entries, 3, 0x00000030u) == 1,
          "peer entries with no recorded activity are preferred for eviction");
    peer_entries[1].total_requests = 1;
    peer_entries[0].last_seen = 0x00000010u;
    CHECK(ci_peer_victim_index(peer_entries, 3, 0x00000030u) == 1,
          "a peer first seen at tick zero is not mistaken for an unused slot");

    memset(channel_entries, 0, sizeof(channel_entries));
    channel_entries[0].total_ecms = 1;
    channel_entries[1].total_ecms = 1;
    channel_entries[2].total_ecms = 1;
    channel_entries[0].last_active = 0xFFFFFFF0u;
    channel_entries[1].last_active = 0x00000010u;
    channel_entries[2].last_active = 0x00000020u;
    CHECK(ci_channel_victim_index(channel_entries, 3, 0x00000030u) == 0,
          "full channel table eviction selects the greatest wrap-safe idle age");
    channel_entries[0].last_active = 0x00000010u;
    channel_entries[1].last_active = 0;
    CHECK(ci_channel_victim_index(channel_entries, 3, 0x00000030u) == 1,
          "a channel first active at tick zero is not mistaken for an unused slot");

    memset(entries, 0, sizeof(entries));
    entries[0].valid = entries[1].valid = entries[2].valid = 1;
    entries[0].valid_until = 0xFFFFFFF0u;
    entries[1].valid_until = 0x00000010u;
    entries[2].valid_until = 0x00000030u;
    CHECK(ci_predict_entry_live(&entries[2], wrapped_now),
          "predictive entry with a post-wrap deadline remains live before expiry");
    CHECK(!ci_predict_entry_live(&entries[2], 0x00000030u),
          "predictive entry expires exactly at its wrapped deadline");
    CHECK(ci_predict_victim_index(entries, 3, 0x00000020u) == 0,
          "predictive eviction selects the oldest expired deadline across wrap");

    entries[0].valid_until = 0x00000020u;
    entries[1].valid_until = 0x00000010u;
    entries[2].valid_until = 0x00000030u;
    CHECK(ci_predict_victim_index(entries, 3, wrapped_now) == 1,
          "when no entry is expired, predictive eviction selects the soonest deadline across wrap");
    entries[1].valid = 0;
    CHECK(ci_predict_victim_index(entries, 3, wrapped_now) == 1,
          "predictive eviction reuses an invalid slot before replacing a live entry");
    CHECK(ci_predict_victim_index(NULL, 0, wrapped_now) == -1,
          "predictive victim selection safely handles an empty table");

    memset(&g_ci, 0, sizeof(g_ci));
    pthread_mutex_init(&g_ci.lock, NULL);
    g_ci.enabled = 1;
    g_ci.peer_count = 1;
    g_ci.peers[0].tier = CI_TIER_BANNED;
    g_ci.peers[0].trust_global = 10;
    g_ci.peers[0].ban_until = 0x00000020u;
    g_ci.peers[0].ban_active = 1;
    g_ci.predict_count = 1;
    g_ci.predict[0].valid = 1;
    g_ci.predict[0].valid_until = 0x00000020u;
    ci_tick(wrapped_now);
    CHECK(g_ci.peers[0].tier == CI_TIER_BANNED && g_ci.predict[0].valid,
          "periodic cleanup preserves bans and predictive entries before wrapped deadlines");
    ci_tick(0x00000020u);
    CHECK(g_ci.peers[0].tier != CI_TIER_BANNED &&
          g_ci.peers[0].ban_until == 0 && !g_ci.predict[0].valid,
          "periodic cleanup expires bans and predictive entries at wrapped deadlines");
    {
        ci_channel_stats_t *ch;
        g_ci.channel_count = 0;
        ch = ci_find_or_create_channel(0x1884, 0, 0x123, 0);
        if (ch) ch->total_ecms = 1;
        ci_detect_cycle(0x1884, 0, 0x123, 0x80, 0);
        CHECK(ch && ch->has_cw_time && ch->last_cw_time == 0 &&
              !ci_should_preload(0x1884, 0, 0x123, 0) &&
              ci_should_preload(0x1884, 0, 0x123, 8000),
              "cycle detection and prefetch preserve a valid tick-zero CW timestamp");
        ci_detect_cycle(0x1884, 0, 0x123, 0x81, 50);
        CHECK(g_ci.caid_count == 1 && g_ci.caids[0].avg_cw_interval == 50,
              "learned CW interval uses modulo elapsed milliseconds");
    }
    {
        uint32_t now = ci_now_ms();
        uint8_t new_cw[16];
        int i, old_present = 0, recent_present = 0, new_present = 0;
        memset(g_ci.fake_cws, 0, sizeof(g_ci.fake_cws));
        g_ci.fake_cw_count = 256;
        for (i = 0; i < 256; i++) {
            uint32_t age = (i == 0) ? 3000000000u : (uint32_t)(i + 1);
            g_ci.fake_cws[i].cw[0] = (uint8_t)(i >> 8);
            g_ci.fake_cws[i].cw[1] = (uint8_t)i;
            g_ci.fake_cws[i].caid = 0x1884;
            g_ci.fake_cws[i].detected_time = now - age;
            g_ci.fake_cws[i].hit_count = 1;
        }
        memset(new_cw, 0xff, sizeof(new_cw));
        ci_add_fake_cw(new_cw, 0x1884, 0);
        for (i = 0; i < g_ci.fake_cw_count; i++) {
            if (g_ci.fake_cws[i].cw[0] == 0 && g_ci.fake_cws[i].cw[1] == 0)
                old_present = 1;
            if (g_ci.fake_cws[i].cw[0] == 0 && g_ci.fake_cws[i].cw[1] == 1)
                recent_present = 1;
            if (memcmp(g_ci.fake_cws[i].cw, new_cw, 16) == 0) new_present = 1;
        }
        CHECK(!old_present && recent_present && new_present,
              "full fake-CW table evicts greatest unsigned elapsed age across rollover");
    }
    {
        uint8_t cw[16] = {1};
        ci_predict_store(0x1884, 0, 0x777, 0x80, cw, 7, UINT32_MAX);
        CHECK(g_ci.predict_count > 1 && g_ci.predict[1].valid &&
              (uint32_t)(g_ci.predict[1].valid_until - g_ci.predict[1].fetch_time) ==
                  0x7FFFFFFFu &&
              ci_predict_entry_live(&g_ci.predict[1], g_ci.predict[1].fetch_time),
              "predictive validity is capped within the modular deadline half-range");
    }
    memset(&g_ci.peers[0], 0, sizeof(g_ci.peers[0]));
    g_ci.peer_count = 1;
    g_ci.peers[0].peer_id = 99;
    g_ci.peers[0].total_requests = 1;
    g_ci.peers[0].trust_global = 80;
    g_ci.peers[0].tier = CI_TIER_A;
    g_ci.peers[0].last_seen = 0;
    ci_tick(3600001u);
    CHECK(g_ci.peers[0].trust_global == 79,
          "inactive-peer decay recognizes a real response timestamp at tick zero");
    {
        ci_peer_stats_t *source;
        ci_channel_stats_t *channel;
        memset(g_ci.peers, 0, sizeof(g_ci.peers));
        memset(g_ci.channels, 0, sizeof(g_ci.channels));
        g_ci.peer_count = 0;
        g_ci.channel_count = 0;
        g_ci.total_cw_accepted = 0;
        g_ci.total_ecms_processed = 0;
        ci_record_success(901, 0x1814, 0x123456, 0x4321, 100, 0);
        source = ci_find_peer(901);
        channel = ci_find_channel(0x1814, 0x123456, 0x4321);
        CHECK(source && source->trust_global == CI_TRUST_INITIAL &&
              channel && channel->best_count == 0 &&
              channel->total_success == 1 && g_ci.total_cw_accepted == 1,
              "an accepted cache candidate updates operations, not GOOD reputation or rank");
        ci_record_success(901, 0x1814, 0x123456, 0x4321, 100, 1);
        CHECK(source->trust_global == CI_TRUST_INITIAL + CI_REWARD_SUCCESS &&
              channel->best_count == 1 && channel->best[0].trust == 100,
              "only a reference-confirmed success earns legacy CI reputation");
    }
    pthread_mutex_destroy(&g_ci.lock);

    printf("%d cache-intelligence tick checks failed\n", failures);
    return failures ? 1 : 0;
}
