/* cwreuse.c -- Band 58 regression for half-CW cross-channel reuse.
 *
 * Fake generators recycle a small pool of 8-byte halves across channels.
 * Drives the real supersmartfilter_check() with every other SSF check
 * disabled:
 *   - a half reappearing on a DIFFERENT channel (any slot) inside the
 *     window, as part of a different full CW, flags;
 *   - same-channel re-sightings never flag;
 *   - the identical FULL CW on another channel is the simulcrypt-twin
 *     case (band 38 / TASK 1.3) and is NOT flagged here;
 *   - a stale sighting (window expired) adopts the new channel silently;
 *   - zero halves are never tracked (a shared zero half is not reuse);
 *   - SF_REUSE_CAIDS scope excludes unlisted CAIDs entirely;
 *   - log mode accepts + counts; mode 2 rejects with reason 12;
 *   - per-profile override beats the global mode in both directions.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

struct ssf_config;
#include "../src/supersmartfilter.h"
struct ssf_config ssf_cfg; /* definition for the extern in the header */

struct ssf_cw_history ssf_history[SSF_MAX_HISTORY];
int ssf_history_index;
struct ssf_peer_stats ssf_peers[SSF_MAX_PEERS];
int ssf_peer_count;
struct ssf_multicheck_entry ssf_mc_table[SSF_MAX_MC_ENTRIES];
int ssf_mc_count;
struct ssf_march_entry ssf_march_table[SSF_MAX_MARCH_ENTRIES];
int ssf_march_index;
struct ssf_dup_entry ssf_dup_table[SSF_MAX_DUP_ENTRIES];
int ssf_dup_index;
struct ssf_reuse_entry ssf_reuse_table[SSF_MAX_REUSE_ENTRIES];
int ssf_reuse_index;
struct ssf_stats ssf_stats;
/* Band 104/105: CW_DUP + CW_REUSE trace rings and the session hook */
struct ssf_cwdup_event ssf_cwdup_events[SSF_CWDUP_EVENT_MAX];
int ssf_cwdup_event_head;
uint64_t ssf_cwdup_event_total;
struct ssf_cwdup_event ssf_cwreuse_events[SSF_CWREUSE_EVENT_MAX];
int ssf_cwreuse_event_head;
uint64_t ssf_cwreuse_event_total;
/* Band 106: MARCHCHECK trace ring (stub for the unit test) */
struct ssf_cwdup_event ssf_marchcheck_events[SSF_MARCHCHECK_EVENT_MAX];
int ssf_marchcheck_event_head;
uint64_t ssf_marchcheck_event_total;
static int trace_hook_calls;
void user_trace_cache_observation(uint16_t caid, uint32_t provider,
                                  uint16_t sid, uint32_t hash,
                                  int peerid, const char *detail,
                                  const uint8_t cw[16])
{
    (void)caid; (void)provider; (void)sid; (void)hash; (void)peerid; (void)detail; (void)cw;
    trace_hook_calls++;
}
pthread_mutex_t ssf_lock = PTHREAD_MUTEX_INITIALIZER;

static int fails = 0;
#define EXPECT(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); fails++; } } while (0)

static const uint8_t H1[8] = {0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88};
static const uint8_t H2[8] = {0x99,0xAA,0xBB,0xCC,0xDD,0xEE,0xFF,0x10};
static const uint8_t H3[8] = {0x21,0x32,0x43,0x54,0x65,0x76,0x87,0x98};
static const uint8_t H4[8] = {0x31,0x42,0x53,0x64,0x75,0x86,0x97,0xA8};
static const uint8_t ZZ[8] = {0,0,0,0,0,0,0,0};

static void mk(uint8_t cw[16], const uint8_t *a, const uint8_t *b)
{
    memcpy(cw, a, 8);
    memcpy(cw + 8, b, 8);
}

static void setup(int global_mode, int window_ms)
{
    supersmartfilter_init();
    ssf_cfg.filter_level = 2;
    ssf_cfg.blockfakecws = 0;
    ssf_cfg.sf_entropy = 0;
    ssf_cfg.sf_pattern = 0;
    ssf_cfg.sf_dupseen = 0;
    ssf_cfg.sf_multicheck = 0;
    ssf_cfg.sf_marchcheck = 0;
    ssf_cfg.sf_cwdup = 0;
    ssf_cfg.sf_cwfmt_auto = 0;
    ssf_cfg.sf_cwfmt_enforce = 0;
    ssf_cfg.sf_cwreuse = global_mode;
    ssf_cfg.sf_cwreuse_window_ms = window_ms;
}

static int prof_window = 0; /* Band 65: per-profile window ms (0=inherit) */
static int check(const uint8_t *cw, uint16_t caid, uint16_t sid,
                 uint32_t now_ms, int prof_reuse, int *reason)
{
    return supersmartfilter_check(cw, NULL, caid, 0, sid, 0, now_ms, reason, 0, 0, 0, prof_reuse, 0, 0xFFFFFFFFu, 0, prof_window, 0);
}

int main(void)
{
    int reason = 0;
    struct ssf_stats st;
    uint8_t x[16], y[16], z[16];

    /* --- Cross-channel half reuse: log flags, reject refuses (12) --- */
    setup(1, 120000);
    mk(x, H1, H2); mk(y, H1, H3); mk(z, H4, H1); /* z: H1 in the other slot */
    EXPECT(check(x, 0x183D, 0x3001, 100000, 0, &reason) == 0, "log: X on ch1");
    EXPECT(check(x, 0x183D, 0x3001, 101000, 0, &reason) == 0, "log: X again same channel");
    supersmartfilter_get_stats(&st);
    EXPECT(st.cwreuse_flagged == 0, "log: same channel never flags");
    EXPECT(check(y, 0x183D, 0x3002, 102000, 0, &reason) == 0, "log: Y accepted (log mode)");
    supersmartfilter_get_stats(&st);
    EXPECT(st.cwreuse_flagged == 1, "log: half on another channel flagged");
    EXPECT(trace_hook_calls >= 1, "log: reuse flag reached the trace hook (band 105)");
    EXPECT(ssf_cwreuse_event_total >= 1, "log: reuse flag recorded in the trace ring (band 105)");
    EXPECT(st.rejected_cwreuse == 0, "log: nothing rejected");

    setup(2, 120000);
    EXPECT(check(x, 0x183D, 0x3001, 100000, 0, &reason) == 0, "rej: X on ch1");
    EXPECT(check(y, 0x183D, 0x3002, 102000, 0, &reason) == 1, "rej: Y rejected");
    EXPECT(reason == 12, "rej: reason code 12");
    EXPECT(check(z, 0x183D, 0x3003, 103000, 0, &reason) == 1, "rej: half in other slot also rejected");
    supersmartfilter_get_stats(&st);
    EXPECT(st.rejected_cwreuse == 2, "rej: two reuse rejections");

    /* --- Full-CW twin on another channel: NOT this detector --- */
    setup(2, 120000);
    EXPECT(check(x, 0x183D, 0x3001, 100000, 0, &reason) == 0, "twin: X on ch1");
    EXPECT(check(x, 0x183E, 0x3002, 102000, 0, &reason) == 0, "twin: identical full CW elsewhere accepted");
    supersmartfilter_get_stats(&st);
    EXPECT(st.cwreuse_flagged == 0, "twin: full-CW match never flags half reuse");

    /* --- Stale sighting adopts silently --- */
    setup(2, 5000);
    EXPECT(check(x, 0x183D, 0x3001, 100000, 0, &reason) == 0, "stale: X on ch1");
    EXPECT(check(y, 0x183D, 0x3002, 106000, 0, &reason) == 0, "stale: Y after window accepted");
    supersmartfilter_get_stats(&st);
    EXPECT(st.cwreuse_flagged == 0, "stale: expired window never flags");

    /* --- Zero halves are not tracked --- */
    setup(2, 120000);
    mk(x, H1, ZZ); mk(y, H2, ZZ);
    EXPECT(check(x, 0x183D, 0x3001, 100000, 0, &reason) == 0, "zero: half-nulled on ch1");
    EXPECT(check(y, 0x183D, 0x3002, 102000, 0, &reason) == 0, "zero: shared zero half is not reuse");

    /* --- CAID scope: unlisted CAIDs are invisible to the detector --- */
    setup(2, 120000);
    ssf_cfg.sf_reuse_caids[0] = 0x1802;
    ssf_cfg.sf_reuse_caids_count = 1;
    mk(x, H1, H2); mk(y, H1, H3);
    EXPECT(check(x, 0x183D, 0x3001, 100000, 0, &reason) == 0, "scope: X unlisted CAID");
    EXPECT(check(y, 0x183D, 0x3002, 102000, 0, &reason) == 0, "scope: Y unlisted CAID not flagged");
    supersmartfilter_get_stats(&st);
    EXPECT(st.cwreuse_flagged == 0, "scope: no flags outside SF_REUSE_CAIDS");
    EXPECT(check(x, 0x1802, 0x3001, 103000, 0, &reason) == 0, "scope: X listed CAID");
    EXPECT(check(y, 0x1802, 0x3002, 104000, 0, &reason) == 1, "scope: Y listed CAID flagged");

    /* --- Profile override in both directions --- */
    setup(0, 120000); /* global off, profile reject */
    EXPECT(check(x, 0x183D, 0x3001, 100000, 3, &reason) == 0, "prof: X");
    EXPECT(check(y, 0x183D, 0x3002, 102000, 3, &reason) == 1, "prof: override 3 rejects, global off");
    setup(2, 120000); /* global reject, profile off */
    EXPECT(check(x, 0x183D, 0x3001, 100000, 1, &reason) == 0, "prof-off: X");
    EXPECT(check(y, 0x183D, 0x3002, 102000, 1, &reason) == 0, "prof-off: override 1 disables");
    supersmartfilter_get_stats(&st);
    EXPECT(st.cwreuse_flagged == 0, "prof-off: no flags when profile forces off");

    /* --- Band 65: per-CAID window wins over the global window --- */
    /* Fresh half pair (H3/H4 family) so no earlier sighting interferes:
     * under the tiny global window the +5s second sighting is stale and
     * silently adopted; under the profile window it is reuse. */
    setup(2, 1000);
    mk(x, H3, H1); mk(y, H3, H2);
    EXPECT(check(x, 0x183D, 0x3011, 100000, 0, &reason) == 0, "prof-win: X on ch1");
    EXPECT(check(y, 0x183D, 0x3012, 105000, 0, &reason) == 0, "prof-win: stale under global window");
    setup(2, 1000);
    prof_window = 120000;
    EXPECT(check(x, 0x183D, 0x3011, 200000, 0, &reason) == 0, "prof-win: X on ch1 (profile window)");
    reason = 0;
    EXPECT(check(y, 0x183D, 0x3012, 205000, 0, &reason) == 1, "prof-win: flagged under profile window");
    EXPECT(reason == 12, "prof-win: reason 12");
    prof_window = 0;

    if (fails) { printf("cwreuse: %d FAILURES\n", fails); return 1; }
    printf("cwreuse: PASS (cross-channel half flags, twin/stale/zero/scope safe, override)\n");
    return 0;
}
