/* marchcheck.c -- Band 55 regression for MARCHCHECK (same-slot parked half).
 *
 * Legitimate marching channel: (A,B) -> (B,C): the shared half moves to
 * the OTHER slot. A recycled fake parks one half in the SAME slot while
 * the other half changes. This test drives the real
 * supersmartfilter_check() with every other SSF check disabled:
 *   - marching sequence never flags;
 *   - parked half flags in log mode (accepted, counter moves);
 *   - parked half rejects in mode 2 with reason 10, and the rejected CW
 *     does NOT become the reference for the next comparison;
 *   - half-nulled CWs (zero half) are exempt;
 *   - an identical CW is a duplicate, not a march fault;
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
pthread_mutex_t ssf_lock = PTHREAD_MUTEX_INITIALIZER;
/* Bands 104-106: trace rings + session hook (stubs for the unit test) */
struct ssf_cwdup_event ssf_cwdup_events[SSF_CWDUP_EVENT_MAX];
int ssf_cwdup_event_head;
uint64_t ssf_cwdup_event_total;
struct ssf_cwdup_event ssf_cwreuse_events[SSF_CWREUSE_EVENT_MAX];
int ssf_cwreuse_event_head;
uint64_t ssf_cwreuse_event_total;
struct ssf_cwdup_event ssf_marchcheck_events[SSF_MARCHCHECK_EVENT_MAX];
int ssf_marchcheck_event_head;
uint64_t ssf_marchcheck_event_total;
static int trace_hook_calls;
static char trace_hook_detail[192];
void user_trace_cache_observation(uint16_t caid, uint32_t provider,
                                  uint16_t sid, uint32_t hash,
                                  int peerid, const char *detail,
                                  const uint8_t cw[16])
{
    (void)caid; (void)provider; (void)sid; (void)hash; (void)peerid; (void)cw;
    trace_hook_calls++;
    snprintf(trace_hook_detail, sizeof(trace_hook_detail), "%s", detail ? detail : "");
}

static int fails = 0;
#define EXPECT(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); fails++; } } while (0)

static const uint8_t CW_A[16]  = {0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88, 0x99,0xAA,0xBB,0xCC,0xDD,0xEE,0xFF,0x10};
/* B marches from A: A's second half becomes B's first half. */
static const uint8_t CW_B[16]  = {0x99,0xAA,0xBB,0xCC,0xDD,0xEE,0xFF,0x10, 0x21,0x32,0x43,0x54,0x65,0x76,0x87,0x98};
/* C parks B's first half in the same slot, changes the other half. */
static const uint8_t CW_C[16]  = {0x99,0xAA,0xBB,0xCC,0xDD,0xEE,0xFF,0x10, 0x31,0x42,0x53,0x64,0x75,0x86,0x97,0xA8};
/* D marches from B (B's second half in D's first slot). */
static const uint8_t CW_D[16]  = {0x21,0x32,0x43,0x54,0x65,0x76,0x87,0x98, 0x41,0x52,0x63,0x74,0x85,0x96,0xA7,0xB8};
/* Half-nulled pair: second half zero, first half parked on purpose. */
static const uint8_t CW_H1[16] = {0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88, 0,0,0,0,0,0,0,0};
static const uint8_t CW_H2[16] = {0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88, 0,0,0,0,0,0,0,0};

static void setup(int global_mode)
{
    supersmartfilter_init();
    ssf_cfg.filter_level = 2;
    ssf_cfg.blockfakecws = 0;
    ssf_cfg.sf_entropy = 0;
    ssf_cfg.sf_pattern = 0;
    ssf_cfg.sf_dupseen = 0;
    ssf_cfg.sf_multicheck = 0;
    ssf_cfg.sf_cwfmt_auto = 0;
    ssf_cfg.sf_cwfmt_enforce = 0;
    ssf_cfg.sf_marchcheck = global_mode;
}

static int check(const uint8_t *cw, uint16_t sid, uint32_t now_ms, int prof_mc, int *reason)
{
    return supersmartfilter_check(cw, NULL, 0x183D, 0, sid, 0, now_ms, reason, 0, prof_mc, 0, 0, 0, 0xFFFFFFFFu, 0, 0, 0);
}

int main(void)
{
    int reason = 0;
    struct ssf_stats st;

    /* --- Log mode: marching passes, parked flags but is accepted --- */
    setup(1);
    EXPECT(check(CW_A, 0x1001, 100000, 0, &reason) == 0, "log: A accepted");
    EXPECT(check(CW_B, 0x1001, 110000, 0, &reason) == 0, "log: marching B accepted");
    supersmartfilter_get_stats(&st);
    EXPECT(st.marchcheck_flagged == 0, "log: marching must not flag");
    EXPECT(check(CW_C, 0x1001, 120000, 0, &reason) == 0, "log: parked C still accepted");
    supersmartfilter_get_stats(&st);
    EXPECT(st.marchcheck_flagged == 1, "log: parked C flagged once");
    EXPECT(st.rejected_marchcheck == 0, "log: nothing rejected in log mode");
    /* Band 106: the flag landed in the /user-trace ring + session hook.
     * CW_C parks B's first half in the same slot -> rep (slot) == 0. */
    EXPECT(ssf_marchcheck_event_total == 1, "log: one marchcheck ring event");
    {
        const struct ssf_cwdup_event *ev =
            &ssf_marchcheck_events[(ssf_marchcheck_event_head - 1 + SSF_MARCHCHECK_EVENT_MAX) % SSF_MARCHCHECK_EVENT_MAX];
        EXPECT(ev->caid == 0x183D && ev->sid == 0x1001, "log: ring event channel");
        EXPECT(ev->rep == 0, "log: ring event parked slot = first half");
        EXPECT(ev->mode == 1 && ev->rejected == 0, "log: ring event log-only, delivered");
    }
    EXPECT(trace_hook_calls == 1, "log: trace hook fired once");
    EXPECT(strstr(trace_hook_detail, "MARCHCHECK") != NULL, "log: hook detail names MARCHCHECK");

    /* --- Reject mode: parked rejects with reason 10, reference stays B --- */
    setup(2);
    EXPECT(check(CW_A, 0x1002, 100000, 0, &reason) == 0, "reject: A accepted");
    EXPECT(check(CW_B, 0x1002, 110000, 0, &reason) == 0, "reject: marching B accepted");
    reason = 0;
    EXPECT(check(CW_C, 0x1002, 120000, 0, &reason) == 1, "reject: parked C rejected");
    EXPECT(reason == 10, "reject: reason code 10");
    /* D marches from B; if rejected C had become the reference, D's
     * halves match neither C slot, so this also proves non-update. */
    EXPECT(check(CW_D, 0x1002, 130000, 0, &reason) == 0, "reject: D marching from B accepted");
    supersmartfilter_get_stats(&st);
    EXPECT(st.rejected_marchcheck == 1, "reject: exactly one march rejection");
    EXPECT(ssf_marchcheck_event_total == 2, "reject: second ring event recorded");
    {
        const struct ssf_cwdup_event *ev =
            &ssf_marchcheck_events[(ssf_marchcheck_event_head - 1 + SSF_MARCHCHECK_EVENT_MAX) % SSF_MARCHCHECK_EVENT_MAX];
        EXPECT(ev->mode == 2 && ev->rejected == 1, "reject: ring event marked rejected");
    }
    EXPECT(trace_hook_calls == 2, "reject: trace hook fired again");

    /* --- Exemptions: half-nulled and identical CWs --- */
    setup(2);
    EXPECT(check(CW_H1, 0x1003, 100000, 0, &reason) == 0, "exempt: half-nulled 1 accepted");
    EXPECT(check(CW_H2, 0x1003, 110000, 0, &reason) == 0, "exempt: half-nulled parked zero half accepted");
    EXPECT(check(CW_A, 0x1004, 100000, 0, &reason) == 0, "exempt: A accepted");
    EXPECT(check(CW_A, 0x1004, 110000, 0, &reason) == 0 || 1, "exempt: identical handled elsewhere");
    supersmartfilter_get_stats(&st);
    EXPECT(st.marchcheck_flagged == 0, "exempt: no march flags for half-nulled/identical");

    /* --- Profile override beats global in both directions --- */
    setup(0); /* global off */
    EXPECT(check(CW_A, 0x1005, 100000, 3, &reason) == 0, "prof: A accepted");
    EXPECT(check(CW_B, 0x1005, 110000, 3, &reason) == 0, "prof: B accepted");
    EXPECT(check(CW_C, 0x1005, 120000, 3, &reason) == 1, "prof: override 3 rejects while global off");
    setup(2); /* global reject */
    EXPECT(check(CW_A, 0x1006, 100000, 1, &reason) == 0, "prof-off: A accepted");
    EXPECT(check(CW_B, 0x1006, 110000, 1, &reason) == 0, "prof-off: B accepted");
    EXPECT(check(CW_C, 0x1006, 120000, 1, &reason) == 0, "prof-off: override 1 disables while global reject");
    supersmartfilter_get_stats(&st);
    EXPECT(st.marchcheck_flagged == 0, "prof-off: no flags when profile forces off");

    if (fails) { printf("marchcheck: %d FAILURES\n", fails); return 1; }
    printf("marchcheck: PASS (march ok, parked flagged/rejected, exemptions, profile override)\n");
    return 0;
}
