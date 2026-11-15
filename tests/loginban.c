/* loginban.c -- Band 57 regression for the HTTP login hard ban.
 *
 * Compiles the production src/loginthrottle.c with MCS_LT_NOGLOBALS and
 * an injected clock, and proves:
 *   - below the threshold nothing bans, and the count is exact;
 *   - at the threshold the ban triggers atomically (banned_now) and
 *     login_banned holds until the deadline, then lapses;
 *   - threshold 0 = stock R3 behaviour (never bans);
 *   - a success clears count AND ban;
 *   - a quiet slot past LOGIN_SLOT_EXPIRE forgets count and ban;
 *   - ban_status counts total and active bans;
 *   - the plain R3 entry point is unchanged.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static uint32_t fake_now = 1000000;
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
    uint64_t total = 0;
    int active = 0;

    /* below threshold: exact counts, no ban */
    EXPECT(login_note_failure_ex(IP_A, 3, 60, &banned) == 1 && !banned, "f1 no ban");
    EXPECT(login_note_failure_ex(IP_A, 3, 60, &banned) == 2 && !banned, "f2 no ban");
    EXPECT(!login_banned(IP_A), "not banned below threshold");

    /* at threshold: ban fires, holds, then lapses at the deadline */
    EXPECT(login_note_failure_ex(IP_A, 3, 60, &banned) == 3 && banned, "f3 bans");
    EXPECT(login_banned(IP_A), "banned at threshold");
    fake_now += 59000;
    EXPECT(login_banned(IP_A), "still banned at +59s");
    fake_now += 2000;
    EXPECT(!login_banned(IP_A), "ban lapsed at +61s");

    /* status counters */
    login_ban_status(&total, &active);
    EXPECT(total == 1, "one ban recorded");
    EXPECT(active == 0, "no active ban after lapse");

    /* threshold 0: stock, never bans however many failures */
    {
        int i;
        for (i = 0; i < 20; i++) login_note_failure_ex(IP_B, 0, 60, &banned);
        EXPECT(!banned && !login_banned(IP_B), "threshold 0 never bans");
    }

    /* success clears count and an in-force ban (reset the count first:
     * the threshold-0 loop above left it at 20, which is itself proof
     * the shared counter kept counting while disarmed) */
    login_note_success(IP_B);
    login_note_failure_ex(IP_B, 2, 600, &banned);
    EXPECT(login_note_failure_ex(IP_B, 2, 600, &banned) == 2 && banned, "B banned");
    EXPECT(login_banned(IP_B), "B in force");
    login_note_success(IP_B);
    EXPECT(!login_banned(IP_B), "success lifts the ban");
    EXPECT(login_note_failure_ex(IP_B, 5, 60, &banned) == 1, "success reset the count");

    /* quiet expiry forgets everything */
    login_note_failure_ex(IP_A, 2, 3600, &banned);
    login_note_failure_ex(IP_A, 2, 3600, &banned);
    EXPECT(login_banned(IP_A), "A banned again");
    fake_now += LOGIN_SLOT_EXPIRE + 1000;
    EXPECT(login_note_failure_ex(IP_A, 5, 60, &banned) == 1, "quiet slot restarts at 1");

    /* plain R3 entry point unchanged */
    EXPECT(login_note_failure(IP_A) == 2, "plain failure counts");

    /* other IPs unaffected */
    EXPECT(!login_banned(0x0A000099u), "unknown IP not banned");
    login_ban_status(&total, &active);
    EXPECT(total >= 3, "total bans accumulate");

    if (fails) { printf("loginban: %d FAILURES\n", fails); return 1; }
    printf("loginban: PASS (threshold ban, lapse, stock-off, success clears, expiry forgets)\n");
    return 0;
}
