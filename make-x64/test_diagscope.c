/*
 * test_diagscope.c -- TASK R48 (D111): the detail scope, the copyable
 * setting hints, and the new channel counters.
 *
 * Two layers, both checked here:
 *   1. the pure rules (src/diagscope.h, src/diag.h, src/chanhealth.h);
 *   2. the wiring in cwlog.c, which is linked in: the real channel table, the
 *      real scope, and the feeds that are no-ops outside the scope.
 *
 * Built with -DMCS_DCWSTATS and linked against cwlog.c, like test_diag.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "../src/diag.h"

static int fails = 0;
static int checks = 0;

static void ok(int cond, const char *what)
{
    checks++;
    printf("%s %s\n", cond ? "  [ ok ]" : "  [FAIL]", what);
    if (!cond) fails++;
}

/* The row of one channel in a snapshot, or NULL. */
static const struct chanhealth_row *find_row(const struct chanhealth_row *rows, int n,
                                             uint32_t caid, uint32_t provid, uint16_t sid)
{
    int i;
    for (i = 0; i < n; i++)
        if (rows[i].caid == caid && rows[i].provid == provid && rows[i].sid == sid)
            return &rows[i];
    return NULL;
}

int main(void)
{
    struct diag_scope s, snap;
    struct chanhealth_tab t;
    struct chanhealth_row rows[CHANHEALTH_SLOTS];
    const struct chanhealth_row *r;
    char out[160], longname[80];
    int n, i;
    uint32_t total;

    printf("test_diagscope: the detail scope (R48, D111)\n");

    /* ---- 1. the pure scope ---- */
    memset(&s, 0, sizeof(s));
    ok(diag_scope_empty(&s), "a new scope is empty: the detail is off");
    ok(!diag_scope_match(&s, 0x1884, 0, 0x64, "cardx"), "an empty scope matches nothing");

    ok(diag_scope_add_ch(&s, 0x1884, 0x000000, 0x1136) == 0, "a channel can be added");
    ok(diag_scope_add_ch(&s, 0x1884, 0x000000, 0x1136) == 1, "adding it twice says already in scope");
    ok(diag_scope_match(&s, 0x1884, 0x000000, 0x1136, NULL), "the channel matches");
    ok(!diag_scope_match(&s, 0x1884, 0x000000, 0x1137, NULL), "a neighbouring SID does not match");
    ok(!diag_scope_match(&s, 0x1884, 0x000001, 0x1136, NULL), "a different provider does not match");

    ok(diag_scope_add_ch(&s, 1, 1, 1) == 0, "second channel");
    ok(diag_scope_add_ch(&s, 2, 2, 2) == 0, "third channel");
    ok(diag_scope_add_ch(&s, 3, 3, 3) == 0, "fourth channel: the limit");
    ok(diag_scope_add_ch(&s, 9, 9, 9) == -1, "a fifth channel is refused: the detail is bounded");
    ok(s.nch == DIAGSCOPE_CHANNELS, "exactly the limit is stored");

    ok(diag_scope_del_ch(&s, 1, 1, 1) == 0 && s.nch == 3, "a channel can be removed");
    ok(diag_scope_find_ch(&s, 3, 3, 3) == 2, "the rest keep their order after a removal");
    ok(diag_scope_del_ch(&s, 7, 7, 7) == -1, "removing a channel that is not there says so");

    ok(diag_scope_add_prof(&s, "") == -2, "an empty profile name is refused");
    memset(longname, 'p', 70);
    longname[70] = 0;
    ok(diag_scope_add_prof(&s, longname) == -2, "a profile name of 70 characters is refused");
    ok(diag_scope_add_prof(&s, "cardx") == 0, "a profile can be added");
    ok(diag_scope_add_prof(&s, "cardx") == 1, "adding a profile twice says already in scope");
    ok(diag_scope_match(&s, 0, 0, 0, "cardx"), "the profile matches by name");
    ok(!diag_scope_match(&s, 0, 0, 0, "CARDX"), "profile names are exact, not case-folded");
    ok(!diag_scope_match(&s, 0, 0, 0, NULL), "no profile name never matches a profile");
    ok(diag_scope_match(&s, 0, 0, 0, "cardx "), "a name with a blank before the bracket still matches (config form \"[ cardx ]\")");
    ok(diag_scope_add_prof(&s, "cardx \t") == 1, "the same name with trailing blanks is already in scope");
    ok(diag_scope_add_prof(&s, "b") == 0, "second profile");
    ok(diag_scope_add_prof(&s, "c") == 0, "third profile");
    ok(diag_scope_add_prof(&s, "d") == 0, "fourth profile: the limit");
    ok(diag_scope_add_prof(&s, "e") == -1, "a fifth profile is refused");
    ok(diag_scope_del_prof(&s, "nope") == -1, "removing a profile that is not there says so");
    ok(diag_scope_del_prof(&s, "cardx") == 0 && diag_scope_find_prof(&s, "d") == 2,
       "removing a profile keeps the others in order");

    diag_scope_clear(&s);
    ok(diag_scope_empty(&s), "clear turns the detail off");

    /* ---- 2. the copyable setting hint ---- */
    ok(diag_setting_hint(DIAG_DYING, 0, 0, 0, out, sizeof(out)) == 1
       && strcmp(out, "ECMLOOPING: YES") == 0,
       "a dying channel is offered ECMLOOPING: YES");
    ok(diag_setting_hint(DIAG_TIMING, 0, 3, 1500, out, sizeof(out)) == 1
       && strcmp(out, "DCW VALIDECMTIME: 1900") == 0,
       "a late card: VALIDECMTIME is the slowest answer plus 25%, up to 100 ms");
    ok(diag_setting_hint(DIAG_TIMING, 0, 3, 2000, out, sizeof(out)) == 1
       && strcmp(out, "DCW VALIDECMTIME: 2500") == 0,
       "the same rule at 2000 ms gives 2500");
    ok(diag_setting_hint(DIAG_TIMING, 0, 0, 0, out, sizeof(out)) == 1
       && strstr(out, "measure first") != NULL,
       "no measured answer yet: a hint to measure first, not a number");
    ok(diag_setting_hint(DIAG_TIMING, CWLOG_R_MINTIME, 0, 0, out, sizeof(out)) == 1
       && strstr(out, "MINTIME") != NULL,
       "a card that is too fast is pointed at MINTIME");
    ok(diag_setting_hint(DIAG_CHECKSUM, 0, 5, 900, out, sizeof(out)) == 0 && out[0] == 0,
       "the checksum finding gets NO copyable line: the page never offers to turn it off");
    ok(diag_setting_hint(DIAG_SHAPE, 0, 0, 0, out, sizeof(out)) == 0 && out[0] == 0,
       "a fake-key finding gets no copyable line");

    /* ---- 3. the new channel counters, on the pure table ---- */
    memset(&t, 0, sizeof(t));
    chanhealth_add_noanswer(&t, 0x1884, 0, 0x64, 1);
    chanhealth_add_noanswer(&t, 0x1884, 0, 0x64, 2);
    chanhealth_add_latency(&t, 0x1884, 0, 0x64, 300, 3);
    chanhealth_add_latency(&t, 0x1884, 0, 0x64, 1200, 4);
    chanhealth_add_reason(&t, 0x1884, 0, 0x64, CWLOG_R_MINTIME, 5);
    chanhealth_add_reason(&t, 0x1884, 0, 0x64, 99, 6);   /* out of range: dropped */
    chanhealth_add_reason(&t, 0x1884, 0, 0x64, -1, 7);   /* out of range: dropped */
    n = chanhealth_copy(&t, rows, CHANHEALTH_SLOTS);
    r = find_row(rows, n, 0x1884, 0, 0x64);
    ok(r != NULL, "the channel is in the table");
    if (r) {
        ok(r->noanswer == 2, "two requests with no answer are counted");
        ok(r->lat_n == 2 && r->lat_sum == 1500, "two answer times are summed");
        ok(r->lat_max == 1200, "the slowest answer is kept");
        ok(r->refused_by[CWLOG_R_MINTIME] == 1, "a refusal is counted by its reason");
        total = 0;
        for (i = 0; i <= CWLOG_R_PATTERN; i++) total += r->refused_by[i];
        ok(total == 1, "an out-of-range reason is dropped, not stored");
    }

    /* ---- 4. the wiring in cwlog.c: the real table and the real scope ---- */
    printf("test_diagscope: the wiring (cwlog.c)\n");
    chanhealth_reset();
    diagscope_clear();

    diag_note_latency(0x1884, 0, 0x64, "cardx", 400);
    diag_note_noanswer(0x1884, 0, 0x64, "cardx");
    n = chanhealth_snapshot(rows, CHANHEALTH_SLOTS);
    ok(n == 0, "outside the scope nothing is recorded: no row is even created");

    ok(diagscope_add_profile("cardx") == 0, "the profile is added to the live scope");
    diag_note_latency(0x1884, 0, 0x64, "cardx", 400);
    diag_note_latency(0x1884, 0, 0x64, "other", 9000);
    diag_note_noanswer(0x1884, 0, 0x64, "cardx");
    n = chanhealth_snapshot(rows, CHANHEALTH_SLOTS);
    r = find_row(rows, n, 0x1884, 0, 0x64);
    ok(r != NULL && r->lat_n == 1 && r->lat_max == 400,
       "a match on the profile is timed; another profile is not");
    ok(r != NULL && r->noanswer == 1, "a match on the profile counts the no-answer");

    chan_note(CWLOG_REFUSED, CWLOG_R_MINTIME, 0x2600, 0, 0x01);
    n = chanhealth_snapshot(rows, CHANHEALTH_SLOTS);
    r = find_row(rows, n, 0x2600, 0, 0x01);
    ok(r != NULL && r->refused_by[CWLOG_R_MINTIME] == 1 && r->lat_n == 0,
       "the reason counter is always on, even outside the scope");

    chanhealth_reset();
    ok(chanhealth_reset_time() != 0, "a reset stamps the time");
    diagscope_snapshot(&snap);
    ok(snap.nprof == 1, "a counter reset keeps the detail scope");
    n = chanhealth_snapshot(rows, CHANHEALTH_SLOTS);
    ok(n == 0, "a counter reset clears the channel table");

    ok(diagscope_add_channel(0x1884, 0, 0x64) == 0, "a channel can be added to the live scope");
    ok(diagscope_add_channel(0x1884, 0, 0x64) == 1, "adding it again says already in scope");
    ok(diagscope_del_channel(0x1884, 0, 0x64) == 0, "and removed again");
    ok(diagscope_del_channel(0x1884, 0, 0x64) == -1, "removing it twice says not in scope");
    ok(diagscope_del_profile("cardx") == 0, "the profile is removed from the live scope");
    diagscope_clear();
    diagscope_snapshot(&snap);
    ok(diag_scope_empty(&snap), "clear empties the live scope");

    printf("\ntest_diagscope: %s (%d checks, %d failed)\n",
           fails ? "FAILED" : "ok", checks, fails);
    return fails ? 1 : 0;
}
