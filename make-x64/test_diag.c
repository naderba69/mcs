/*
 * make-x64/test_diag.c -- TASK R47 (D110): the diagnosis rules behind /diag.
 *
 * Drives the REAL src/diag.h (compiled straight in, like test_chanhealth.c),
 * never a copy of its thresholds. Pins:
 *   - the thresholds (5 verdicts, 50% cause share, 90% healthy, 70% peer),
 *   - the ring code map, including the 10 vs 20 collision (hold vs pattern),
 *   - the wiring from cwlog_note() into the channel table (cache verdicts
 *     counted, STORED not counted, card deliveries via chan_note()).
 *
 * Built with -DMCS_DCWSTATS: cwlog_reason_name() only knows the R36/R44/R47
 * ring codes in the stats flavour, and the test checks those names.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "../src/cwlog.h"
#include "../src/chanhealth.h"
#include "../src/diag.h"

static int checks = 0, fails = 0;

static void ok(int cond, const char *what)
{
    checks++;
    if (!cond) fails++;
    printf("  [%s] %s\n", cond ? " ok " : "FAIL", what);
}

/* An evidence record with the given channel totals and no ring data. */
static struct diag_evidence ev(uint32_t delivered, uint32_t refused,
                               uint32_t held, uint32_t streak)
{
    struct diag_evidence e;
    memset(&e, 0, sizeof(e));
    e.delivered = delivered;
    e.refused = refused;
    e.held = held;
    e.streak = streak;
    return e;
}

/* defined in src/cwlog.c, not in the header (the ring feed is internal) */
void cwlog_note(int verdict, int reason, int peerid,
                uint32_t caid, uint32_t provid, uint16_t sid,
                const uint8_t *cw, uint32_t tick);

#define HAS(mask, f) (((mask) & (1u << (f))) != 0)

int main(void)
{
    struct diag_evidence e;
    uint32_t m;
    uint32_t counts[4];
    int i;

    printf("== reason codes ==\n");
    ok(diag_reason_finding(1) == DIAG_CHECKSUM, "checksum (1) -> CHECKSUM");
    ok(diag_reason_finding(2) == DIAG_SHAPE, "null (2) -> SHAPE");
    ok(diag_reason_finding(13) == DIAG_PROFILE, "profile (13) -> PROFILE");
    ok(diag_reason_finding(18) == DIAG_TIMING, "card-too-fast (18) -> TIMING");
    ok(diag_reason_finding(19) == DIAG_TIMING, "card-too-late (19) -> TIMING");
    ok(diag_reason_finding(10) == DIAG_HELD, "hold (10) -> HELD, never a refusal");
    ok(diag_reason_finding(20) == DIAG_SHAPE, "pattern (20) -> SHAPE");
    ok(diag_reason_finding(99) == DIAG_MIXED, "unknown reason -> MIXED");
    ok(CWLOG_R_HOLD == 10, "ring hold is still code 10 (no renumbering)");
    ok(CWLOG_R_PATTERN != CWLOG_R_HOLD, "pattern and hold do not collide");
    ok(strcmp(cwlog_reason_name(10), "held-for-corroboration") == 0,
       "reason 10 names the hold");
    ok(strcmp(cwlog_reason_name(20), "pattern-fake") == 0,
       "reason 20 names the pattern refusal, not the hold");
    ok(strcmp(cwlog_reason_name(18), "card-too-fast") == 0,
       "reason 18 names card-too-fast");

    printf("== thresholds ==\n");
    ok(diag_pct_ge(0, 0, 0) == 0, "empty whole is never a share");
    ok(diag_pct_ge(5, 10, 50) == 1, "exactly 50% reaches a 50% bar");
    ok(diag_pct_ge(4, 10, 50) == 0, "49.9% does not");

    printf("== classify ==\n");
    e = ev(2, 2, 0, 0);
    m = diag_classify(&e);
    ok(m == (1u << DIAG_LOW_SAMPLE), "4 verdicts -> LOW_SAMPLE only");

    e = ev(95, 5, 0, 0);
    m = diag_classify(&e);
    ok(m == (1u << DIAG_OK), "95% delivered, no streak -> OK");

    e = ev(10, 5, 0, 5);
    m = diag_classify(&e);
    ok(HAS(m, DIAG_DYING), "a streak of 5 -> DYING even with good totals");
    ok(diag_worst(m) == 2, "DYING is the worst severity (2)");

    e = ev(10, 5, 0, 0);
    e.reason[1] = 5;
    e.reason[7] = 5;
    m = diag_classify(&e);
    ok(HAS(m, DIAG_CHECKSUM), "5 of 10 refusals are checksum (50%) -> CHECKSUM");
    ok(HAS(m, DIAG_CEX), "the other 5 are CACHEEX (50%) -> CEX as well: two causes");

    e = ev(10, 5, 0, 0);
    e.reason[1] = 4;
    e.reason[5] = 1;
    m = diag_classify(&e);
    ok(HAS(m, DIAG_CHECKSUM), "4 of 5 ring refusals (80%) -> CHECKSUM by share");

    e = ev(10, 10, 0, 0);
    e.reason[1] = 4;
    e.reason[13] = 3;
    e.reason[12] = 3;
    m = diag_classify(&e);
    ok(!HAS(m, DIAG_CHECKSUM) && !HAS(m, DIAG_PROFILE) && !HAS(m, DIAG_THRESHOLD),
       "no cause reaches 50% of 10 -> no cause finding");
    ok(HAS(m, DIAG_MIXED), "refusals with no single cause -> MIXED");

    e = ev(2, 6, 0, 0);
    e.reason[20] = 6;
    e.reason[10] = 40;
    m = diag_classify(&e);
    ok(HAS(m, DIAG_SHAPE), "6 pattern refusals (20) -> SHAPE");
    ok(!HAS(m, DIAG_HELD), "the ring's 40 holds do not make a HELD cause");

    e = ev(1, 0, 9, 0);
    m = diag_classify(&e);
    ok(HAS(m, DIAG_HELD), "9 of 10 verdicts held -> HELD");
    ok(!HAS(m, DIAG_MIXED), "a clear hold is not MIXED");

    e = ev(10, 2, 0, 0);
    e.disputed = 2;
    m = diag_classify(&e);
    ok(HAS(m, DIAG_DISPUTED), "a disputed verdict in the ring -> DISPUTED");
    ok(diag_worst(m) == 1, "DISPUTED is a warning (1)");

    e = ev(3, 2, 5, 0);
    m = diag_classify(&e);
    ok(HAS(m, DIAG_HELD) && !HAS(m, DIAG_OK), "held majority is not OK");

    printf("== dominant peer ==\n");
    counts[0] = 0; counts[1] = 8; counts[2] = 1;
    ok(diag_dominant(counts, 3) == 1, "8 of 9 refusals from one peer -> dominant");
    counts[0] = 5; counts[1] = 5; counts[2] = 0;
    ok(diag_dominant(counts, 3) == -1, "a tie for the lead is not dominance");
    counts[0] = 1; counts[1] = 2; counts[2] = 0;
    ok(diag_dominant(counts, 3) == -1, "fewer than 5 refusals: no judgement");
    counts[0] = 1; counts[1] = 2; counts[2] = 3;
    ok(diag_dominant(counts, 3) == -1, "a leader under 70% is not dominant");
    for (i = 0; i < 4; i++) counts[i] = 0;
    ok(diag_dominant(counts, 4) == -1, "no refusals: no dominant peer");

    printf("== labels ==\n");
    {
        int f, all = 1;
        for (f = 0; f < DIAG_FINDINGS; f++) {
            if (!diag_label((enum diag_finding)f) || diag_label((enum diag_finding)f)[0] == 0) all = 0;
            if (!diag_advice((enum diag_finding)f) || diag_advice((enum diag_finding)f)[0] == 0) all = 0;
        }
        ok(all, "every finding has a label and an advice text");
    }

    printf("== wiring: cwlog_note -> channel table ==\n");
    {
        struct chanhealth_row rows[CHANHEALTH_SLOTS];
        int n, j, found_a = 0, found_b = 0;
        const uint8_t cw[16] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};

        for (i = 0; i < 3; i++)
            cwlog_note(CWLOG_DELIVERED, 0, 1, 0x0500, 0x000000, 0x0001, cw, 0);
        cwlog_note(CWLOG_STORED, 0, 1, 0x0500, 0x000000, 0x0001, cw, 0);
        for (i = 0; i < 6; i++)
            cwlog_note(CWLOG_REFUSED, 1, -1, 0x1884, 0x000000, 0x1136, cw, 0);
        cwlog_note(CWLOG_HELD, 10, 2, 0x1884, 0x000000, 0x1136, cw, 0);

        n = chanhealth_snapshot(rows, CHANHEALTH_SLOTS);
        for (j = 0; j < n; j++) {
            if (rows[j].caid == 0x0500 && rows[j].sid == 0x0001) {
                found_a = 1;
                ok(rows[j].delivered == 3, "3 cache deliveries counted, STORED not counted");
                ok(rows[j].refused == 0, "no refusals on the delivering channel");
            }
            if (rows[j].caid == 0x1884 && rows[j].sid == 0x1136) {
                found_b = 1;
                ok(rows[j].refused == 6, "6 card refusals counted on 1884:1136");
                ok(rows[j].streak == 6, "streak of 6 refusals");
                ok(rows[j].held == 1, "the hold is counted as held, not as a refusal");
            }
        }
        ok(found_a, "the delivering channel is in the table");
        ok(found_b, "the refusing channel is in the table");
    }

    /* ---- TASK R65: the page's new rules ---- */
    {
        struct diag_evidence e;
        uint32_t m;
        uint32_t tk[4] = { 1000, 9000, 4000, 20 };
        struct diag_pfacts f;

        /* no-answer share: 30% of attempts (answered + no answer) */
        memset(&e, 0, sizeof(e));
        e.delivered = 6; e.refused = 0; e.noanswer = 4;   /* 4 of 10 = 40% */
        m = diag_classify(&e);
        ok((m & (1u << DIAG_NOANSWER)) != 0, "R65: 40% no-answer share is flagged");
        memset(&e, 0, sizeof(e));
        e.delivered = 9; e.noanswer = 1;                  /* 1 of 10 = 10% */
        ok((diag_classify(&e) & (1u << DIAG_NOANSWER)) == 0, "R65: 10% no-answer share is not flagged");
        memset(&e, 0, sizeof(e));
        e.noanswer = 3;                                   /* 3 attempts, below MIN_SAMPLE */
        ok(diag_classify(&e) == (1u << DIAG_LOW_SAMPLE), "R65: no-answer below the sample floor stays LOW_SAMPLE");

        /* slow card: needs 5 measured answers and an average at or above 1000 ms */
        memset(&e, 0, sizeof(e));
        e.delivered = 10; e.lat_n = 5; e.lat_avg = 1400;
        ok((diag_classify(&e) & (1u << DIAG_SLOW)) != 0, "R65: average 1400 ms over 5 answers is SLOW");
        memset(&e, 0, sizeof(e));
        e.delivered = 10; e.lat_n = 4; e.lat_avg = 1400;
        ok((diag_classify(&e) & (1u << DIAG_SLOW)) == 0, "R65: 4 answers are below the sample floor for SLOW");
        memset(&e, 0, sizeof(e));
        e.delivered = 10; e.lat_n = 9; e.lat_avg = 999;
        ok((diag_classify(&e) & (1u << DIAG_SLOW)) == 0, "R65: 999 ms is not SLOW");
        ok(diag_severity(DIAG_NOANSWER) == 1 && diag_severity(DIAG_SLOW) == 1, "R65: both new findings are severity 1");
        ok(strcmp(diag_label(DIAG_NOANSWER), "?") != 0 && strcmp(diag_label(DIAG_SLOW), "?") != 0,
           "R65: both new findings have a label");
        ok(strcmp(diag_advice(DIAG_SLOW), "?") != 0 && strcmp(diag_advice(DIAG_NOANSWER), "?") != 0,
           "R65: both new findings have advice");

        /* delivered share */
        ok(diag_ratio_pct(3, 1) == 75, "R65: 3 delivered, 1 refused = 75%");
        ok(diag_ratio_pct(0, 0) == -1, "R65: no decided verdict = -1 (no share), not 0%");
        ok(diag_ratio_pct(0, 5) == 0, "R65: only refusals = 0%");

        /* ring window, wrap-safe */
        ok(diag_recent(tk, 4, 10000, 300000) == 4, "R65: all four ticks inside 5 minutes");
        ok(diag_recent(tk, 4, 10000, 5000) == 1, "R65: a 5 s window keeps only the tick 1 s old (9000)");
        ok(diag_recent(tk, 4, 2000, 1000) == 1, "R65: 1 s window at 2000 keeps only the tick at 1000 (the ones ahead of now are not counted)");
        ok(diag_recent(&tk[1], 1, 2000, 300000) == 0, "R65: a tick ahead of now is never counted");

        /* checksum effective gate, mirrors dcw.c */
        ok(diag_checksum_effective(DIAG_CSUM_INHERIT, 1, 0, 1) == 1, "R65: INHERIT, global on, CSA -> on");
        ok(diag_checksum_effective(DIAG_CSUM_INHERIT, 1, 1, 0) == 0, "R65: INHERIT, CA MODERN, AES/CONST -> off");
        ok(diag_checksum_effective(DIAG_CSUM_INHERIT, 1, 1, 1) == 1, "R65: INHERIT, CA MODERN, CSA -> on");
        ok(diag_checksum_effective(DIAG_CSUM_INHERIT, 1, 0, 0) == 1, "R65: INHERIT without CA MODERN -> on (stock)");
        ok(diag_checksum_effective(DIAG_CSUM_INHERIT, 0, 0, 1) == 0, "R65: INHERIT, global off -> off");
        ok(diag_checksum_effective(DIAG_CSUM_ON, 0, 1, 0) == 1, "R65: explicit ON wins over everything");
        ok(diag_checksum_effective(DIAG_CSUM_OFF, 1, 0, 1) == 0, "R65: explicit OFF wins over everything");

        /* reason meanings: every ring reason has text; 10 is a hold */
        {
            int r, missing = 0;
            for (r = 0; r < DIAG_REASONS; r++)
                if (!strcmp(diag_reason_meaning(r), "?")) missing++;
            ok(missing == 0, "R65: every ring reason 0..20 has a meaning");
            ok(strstr(diag_reason_meaning(CWLOG_R_HOLD), "ليس رفضاً") != NULL, "R65: reason 10 says it is a hold, not a refusal");
            ok(strcmp(diag_reason_meaning(99), "?") == 0, "R65: an unknown reason is '?'");
        }

        /* profile warnings: a healthy CSA profile raises nothing */
        memset(&f, 0, sizeof(f));
        f.checksum_expected = 1; f.checksum_eff = 1; f.ca_modern = 0; f.dcw_check = 1;
        f.cycle = 1; f.skipcwc = 1; f.checkecmlength = 1;
        ok(diag_profile_warn(&f) == 0, "R65: a healthy CSA profile raises no warning");

        f.ca_modern = 0; f.checksum_expected = 0; f.checksum_eff = 1;
        ok((diag_profile_warn(&f) & (1u << PW_CA_MODERN_OFF)) != 0, "R65: CA MODERN off on AES/CONST is warned");
        f.checksum_expected = 1;
        ok((diag_profile_warn(&f) & (1u << PW_CA_MODERN_OFF)) == 0, "R65: CA MODERN off on CSA is stock, not warned");

        f.checksum_eff = 0;
        ok((diag_profile_warn(&f) & (1u << PW_CSA_CHECKSUM_OFF)) != 0, "R65: checksum effectively off on CSA is warned");
        f.checksum_expected = 0; f.ca_modern = 1;
        ok((diag_profile_warn(&f) & (1u << PW_CSA_CHECKSUM_OFF)) == 0, "R65: checksum off on AES is the intended gate, not warned");

        f.halfnulled = 1; f.vg_csa = 0;
        ok((diag_profile_warn(&f) & (1u << PW_HALFNULL_NONVG)) != 0, "R65: HALFNULLED on a non-VideoGuard system is warned");
        f.vg_csa = 1;
        ok((diag_profile_warn(&f) & (1u << PW_HALFNULL_NONVG)) == 0, "R65: HALFNULLED on VideoGuard CSA is not warned");

        f.dcw_check = 0; f.cycle = 0; f.skipcwc = 0; f.checkecmlength = 0;
        m = diag_profile_warn(&f);
        ok((m & (1u << PW_DCW_CHECK_OFF)) && (m & (1u << PW_CYCLE_OFF)), "R65: DCW CHECK and CYCLE off are warned");
        ok((m & (1u << PW_SKIPCWC_OFF)) && (m & (1u << PW_ECM_LEN_OFF)), "R65: SKIPCWC and ECM LENGTH off are reported");
        ok(diag_pwarn_severity(PW_SKIPCWC_OFF) == 0 && diag_pwarn_severity(PW_ECM_LEN_OFF) == 0 &&
           diag_pwarn_severity(PW_DCW_CHECK_OFF) == 1, "R65: skipcwc and ECM LENGTH are information; DCW CHECK is severity 1");
        {
            int w, bad = 0;
            for (w = 0; w < PW_FINDINGS; w++)
                if (!strcmp(diag_pwarn_label(w), "?") || !strcmp(diag_pwarn_advice(w), "?")) bad++;
            ok(bad == 0, "R65: every profile warning has a label and advice");
        }
    }

    printf("\ntest_diag: %s (%d checks, %d failed)\n",
           fails ? "FAILED" : "ok", checks, fails);
    return fails ? 1 : 0;
}
