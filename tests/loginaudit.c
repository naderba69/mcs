/* loginaudit.c -- Band 62 regression for the login attempt audit ring.
 *
 * Compiles the production src/loginthrottle.c with a fake-but-real
 * clock path (time(NULL) is used for wall timestamps; only ordering
 * and content are asserted):
 *   - attempts are returned newest-first with proto/ip/user/result;
 *   - the ring keeps exactly LOGIN_AUDIT_MAX entries and wraps;
 *   - invalid protocols are ignored;
 *   - protoban/loginthrottle behaviour is untouched (spot check).
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static uint32_t fake_now = 7000000;
uint32_t lt_now(void) { return fake_now; }

#define MCS_LT_NOGLOBALS 1
#include "../src/loginthrottle.c"

static int fails = 0;
#define EXPECT(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); fails++; } } while (0)

int main(void)
{
    struct login_audit_entry out[LOGIN_AUDIT_MAX + 8];
    int n, i;

    EXPECT(loginaudit_snapshot(out, 16) == 0, "empty ring snapshots 0");

    loginaudit_note(PB_HTTP, 0x0A000001u, "admin", 0);
    loginaudit_note(PB_HTTP, 0x0A000001u, "admin", 1);
    loginaudit_note(PB_TELNET, 0x0A000002u, "root", 0);

    n = loginaudit_snapshot(out, 16);
    EXPECT(n == 3, "three attempts recorded");
    EXPECT(out[0].proto == PB_TELNET && !out[0].success, "newest first: telnet fail");
    EXPECT(out[1].proto == PB_HTTP && out[1].success, "then http success");
    EXPECT(out[2].proto == PB_HTTP && !out[2].success, "oldest: http fail");
    EXPECT(strcmp(out[1].user, "admin") == 0, "user recorded");
    EXPECT(out[0].ip == 0x0A000002u, "ip recorded");
    EXPECT(out[0].when > 0, "wall timestamp set");

    /* invalid protocol ignored */
    loginaudit_note(99, 0x0A000003u, "x", 0);
    EXPECT(loginaudit_snapshot(out, 16) == 3, "invalid proto ignored");

    /* long user names are truncated, always NUL-terminated */
    {
        char longname[80];
        memset(longname, 'a', sizeof(longname) - 1);
        longname[sizeof(longname) - 1] = 0;
        loginaudit_note(PB_HTTP, 0x0A000004u, longname, 0);
        n = loginaudit_snapshot(out, 16);
        EXPECT(n == 4 && strlen(out[0].user) == 31, "user truncated to 31 chars");
    }

    /* ring wraps at LOGIN_AUDIT_MAX, still newest-first */
    for (i = 0; i < LOGIN_AUDIT_MAX + 10; i++) {
        char u[16];
        snprintf(u, sizeof(u), "u%d", i);
        loginaudit_note(PB_TELNET, 0x0A000005u, u, i % 2);
    }
    n = loginaudit_snapshot(out, LOGIN_AUDIT_MAX + 8);
    EXPECT(n == LOGIN_AUDIT_MAX, "ring capped at LOGIN_AUDIT_MAX");
    EXPECT(strcmp(out[0].user, "u41") == 0, "newest survives wrap");
    EXPECT(strcmp(out[LOGIN_AUDIT_MAX - 1].user, "u10") == 0, "oldest in ring is u10");

    /* the rest of the module is untouched */
    {
        int b = 0;
        EXPECT(protoban_note_failure(0x0A000009u, PB_TELNET, 1, 60, &b) == 1 && b, "protoban still works");
        EXPECT(login_note_failure(0x0A000009u) == 1, "R3 counter still works");
    }

    if (fails) { printf("loginaudit: %d FAILURES\n", fails); return 1; }
    printf("loginaudit: PASS (newest-first, wrap, truncation, proto guard)\n");
    return 0;
}
