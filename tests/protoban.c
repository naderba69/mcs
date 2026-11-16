/* protoban.c -- Band 60 regression for per-protocol IP bans.
 *
 * Compiles the production src/loginthrottle.c with an injected clock
 * (MCS_LT_NOGLOBALS) and proves:
 *   - failures on one protocol never ban another protocol;
 *   - at the threshold the ban fires for that protocol only;
 *   - a success clears that protocol's count AND ban, not others';
 *   - the ban lapses at its deadline;
 *   - unban clears one protocol, or all with proto -1;
 *   - snapshot lists exactly the active bans with seconds left;
 *   - threshold 0 never bans; the band-57 HTTP entry points still work.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static uint32_t fake_now = 5000000;
uint32_t lt_now(void) { return fake_now; }

#define MCS_LT_NOGLOBALS 1
#include "../src/loginthrottle.c"

static int fails = 0;
#define EXPECT(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); fails++; } } while (0)

#define IP_A 0x0A000001u
#define IP_B 0x0A000002u

int main(void)
{
    int banned = 0;
    struct protoban_view view[16];
    int n;

    /* telnet failures do not leak into cccam */
    EXPECT(protoban_note_failure(IP_A, PB_TELNET, 3, 600, &banned) == 1 && !banned, "telnet f1");
    EXPECT(protoban_note_failure(IP_A, PB_TELNET, 3, 600, &banned) == 2 && !banned, "telnet f2");
    EXPECT(!protoban_banned(IP_A, PB_TELNET), "telnet not yet banned");
    EXPECT(protoban_note_failure(IP_A, PB_TELNET, 3, 600, &banned) == 3 && banned, "telnet f3 bans");
    EXPECT(protoban_banned(IP_A, PB_TELNET), "telnet banned");
    EXPECT(!protoban_banned(IP_A, PB_CCCAM), "cccam NOT banned by telnet failures");
    EXPECT(!protoban_banned(IP_A, PB_HTTP), "http NOT banned by telnet failures");

    /* snapshot shows exactly the one active ban */
    n = protoban_snapshot(view, 16);
    EXPECT(n == 1, "snapshot: one active ban");
    EXPECT(view[0].ip == IP_A && view[0].proto == PB_TELNET, "snapshot: it is A/telnet");
    EXPECT(view[0].seconds_left > 590 && view[0].seconds_left <= 600, "snapshot: ~600s left");
    EXPECT(protoban_total() == 1, "total bans = 1");

    /* cccam has its own count and threshold */
    EXPECT(protoban_note_failure(IP_A, PB_CCCAM, 2, 60, &banned) == 1 && !banned, "cccam f1");
    EXPECT(protoban_note_failure(IP_A, PB_CCCAM, 2, 60, &banned) == 2 && banned, "cccam f2 bans");
    n = protoban_snapshot(view, 16);
    EXPECT(n == 2, "snapshot: two active bans now");

    /* success on telnet clears telnet only */
    protoban_note_success(IP_A, PB_TELNET);
    EXPECT(!protoban_banned(IP_A, PB_TELNET), "telnet cleared by success");
    EXPECT(protoban_banned(IP_A, PB_CCCAM), "cccam still banned");

    /* unban one protocol, then lapse by clock */
    EXPECT(protoban_unban(IP_A, PB_CCCAM) == 1, "unban cccam cleared 1");
    EXPECT(!protoban_banned(IP_A, PB_CCCAM), "cccam unbanned");
    protoban_note_failure(IP_B, PB_NEWCAMD, 1, 30, &banned);
    EXPECT(protoban_banned(IP_B, PB_NEWCAMD), "B/newcamd banned");
    fake_now += 31000;
    EXPECT(!protoban_banned(IP_B, PB_NEWCAMD), "B/newcamd lapsed at +31s");
    n = protoban_snapshot(view, 16);
    EXPECT(n == 0, "snapshot empty after lapse/unban");

    /* threshold 0 never bans */
    {
        int i;
        for (i = 0; i < 50; i++) protoban_note_failure(IP_B, PB_MGCAMD, 0, 600, &banned);
        EXPECT(!protoban_banned(IP_B, PB_MGCAMD), "threshold 0 never bans");
    }

    /* unban all */
    protoban_note_failure(IP_B, PB_CS378X, 1, 600, &banned);
    protoban_note_failure(IP_B, PB_CAMD35, 1, 600, &banned);
    EXPECT(protoban_unban(IP_B, -1) == 2, "unban all cleared 2");
    EXPECT(protoban_snapshot(view, 16) == 0, "snapshot empty after unban all");

    /* names and band-57 coexistence */
    EXPECT(strcmp(protoban_name(PB_CCCAM), "cccam") == 0, "proto name");
    login_note_failure(IP_A);
    EXPECT(login_note_failure(IP_A) == 2, "band-57 counter untouched");

    if (fails) { printf("protoban: %d FAILURES\n", fails); return 1; }
    printf("protoban: PASS (per-protocol isolation, threshold, lapse, unban, snapshot)\n");
    return 0;
}
