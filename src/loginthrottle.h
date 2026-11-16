/*
 * loginthrottle.h -- TASK R3 (D57): progressive login throttling and an
 * optional allowlist for the HTTP and telnet doors.
 *
 * The problem (the R-round surface audit said it first): both doors answer
 * a wrong password instantly and forever -- a keyboard cracker can try
 * hundreds of passwords a second against the same IP, and the only trace
 * is the client disconnecting. No attempt lockout, no delay, no list.
 *
 * The remedy, opt-in like every protection in this project:
 *
 *   HTTP LOGIN DELAY: 250      (ms base; 0 = off, the default)
 *   TELNET LOGIN DELAY: 250
 *   HTTP LOGIN ALLOW: 127.0.0.1 192.168.1.10   (optional; empty = off)
 *   TELNET LOGIN ALLOW: 127.0.0.1
 *
 * The delay is PROGRESSIVE and PER SOURCE IP: the n-th consecutive failed
 * login from an IP waits base<<(n-1) ms, capped (LOGIN_DELAY_CAP), before
 * the failure is answered. Both doors run one thread per connection, so
 * the wait burns the attacker's own thread, never a shared one. A good
 * login resets the counter (an admin's typo costs one base delay, never
 * more). A slot quiet for LOGIN_SLOT_EXPIRE ms is reused from zero. The
 * allowlist, when non-empty, closes connections from unlisted IPs before
 * any credential is read -- one log line, nothing else.
 *
 * OFF (the default) answers failures exactly as stock: instant, silent.
 *
 * GR1/GR3 shape: every counted delay and every allowlist refusal says so
 * once, with the IP and the reason; nothing is scored or remembered
 * beyond the delay table itself.
 */

#ifndef MCS_LOGINTHROTTLE_H
#define MCS_LOGINTHROTTLE_H

#include <stdint.h>

#define LOGIN_ALLOW_MAX    16      /* IPs per list; the parser notices when full */
#define LOGIN_DELAY_CAP    8000    /* ms; the doubling stops here */
#define LOGIN_SLOT_EXPIRE  600000  /* ms of quiet before a slot forgets (10 min) */
#define LOGIN_SLOTS        64      /* fixed table, house style */

struct login_allow {
	int      count;
	uint32_t ip[LOGIN_ALLOW_MAX];
};

/* ms clock, injectable so the unit test can drive expiry */
#ifdef MCS_LT_NOGLOBALS
uint32_t lt_now(void);
#else
uint32_t lt_now(void);
#endif

/* --- allowlist ------------------------------------------------------------ */

void login_allow_reset(struct login_allow *lst);
int  login_allow_add(struct login_allow *lst, uint32_t ip);   /* 0 = list full */
int  login_allowed(const struct login_allow *lst, uint32_t ip); /* empty list = every IP passes */

/* --- failure table --------------------------------------------------------- */

uint32_t login_note_failure(uint32_t ip);  /* returns the consecutive-failure count incl. this one */

/* --- reload shield (band 77) ------------------------------------------------
 * Field report: the operator saves the config in the web editor; the
 * inotify reload churns every door for a few seconds (listeners and
 * client tables rebuilt, established clients dropped and reconnecting,
 * duplicate-login rejects while the old session is cleaned up). Those
 * transients are indistinguishable from brute force to a per-IP failure
 * counter, so the operator's own IP -- shared by his browser and his
 * receiver -- tripped the CCcam and web bans at the very moment of the
 * save. While a reload runs, and for LOGIN_RELOAD_GRACE_MS after it,
 * failure/attempt counting is suspended on EVERY door (both tables in
 * loginthrottle.c). Enforcement of bans already in force is unchanged,
 * and successes still clear counters. Only the config thread raises
 * the shield; a reload is an operator action, so the grace window is
 * not attacker-reachable. */
#define LOGIN_RELOAD_GRACE_MS 10000
void login_reload_shield_begin(void);
void login_reload_shield_end(void);
int  login_reload_shielded(void);
void     login_note_success(uint32_t ip);  /* forgets the IP's counter (a good login) */

/* --- hard ban (band 57) -----------------------------------------------------
 * Opt-in per door via config (HTTP LOGIN FAILS / HTTP LOGIN BAN). When the
 * consecutive-failure count reaches `threshold` (>0), the slot is banned
 * until now + ban_seconds. login_note_failure_ex is the atomic form: the
 * count and the ban decision happen under the same lock, so two doors
 * cannot race past the threshold. `*banned_now` (may be NULL) reports
 * whether THIS failure triggered (or extended) the ban. A successful
 * login clears both the count and any ban. Enforcement (closing the
 * door pre-auth) is the caller's job, gated on its own config, so
 * setting the threshold back to 0 disarms instantly. */
uint32_t login_note_failure_ex(uint32_t ip, uint32_t threshold,
                               uint32_t ban_seconds, int *banned_now);
int      login_banned(uint32_t ip);        /* 1 while a ban is in force */
void     login_ban_status(uint64_t *total_bans, int *active_bans);

/* pure schedule, exposed for the unit test: fails<=0 or base<=0 -> 0 */
uint32_t login_delay_ms(uint32_t base, uint32_t fails);

/* --- per-protocol bans (band 60) --------------------------------------------
 * The band-57 ban is deliberately HTTP-only. This table gives every door
 * its OWN failure count and ban deadline for a source IP: an attacker
 * banned from telnet can still be a legitimate CCcam client, and a
 * CCcam brute-forcer does not lock the operator out of the web door.
 * A protocol's counter counts consecutive failures (or, where the door
 * cannot attribute failure precisely, consecutive attempts -- the door
 * documents which) without an intervening success on THAT protocol.
 * Threshold 0 at the door = the door never bans (stock). */
enum {
	PB_HTTP = 0, PB_TELNET, PB_CCCAM, PB_NEWCAMD,
	PB_MGCAMD, PB_CS378X, PB_CAMD35, PB_CACHEEX, PB_COUNT
};
const char *protoban_name(int proto);
uint32_t protoban_note_failure(uint32_t ip, int proto, uint32_t threshold,
                               uint32_t ban_seconds, int *banned_now);
void     protoban_note_success(uint32_t ip, int proto);
int      protoban_banned(uint32_t ip, int proto);
int      protoban_unban(uint32_t ip, int proto); /* proto -1 = all; returns cleared count */
struct protoban_view { uint32_t ip; uint8_t proto; uint32_t seconds_left; uint32_t fails; };

/* --- login attempt audit (band 62) ------------------------------------------
 * A 32-entry ring of real credential attempts on the interactive doors
 * (http, telnet): when, from where, which user name, success or failure.
 * Session-token requests are NOT audited (they would flood the ring);
 * only an actual Basic/password verification is. Shown as a section on
 * /user-trace -- deliberately not a separate page (one diagnostic
 * source). RAM only; a restart clears it, like the R7 sessions. */
#define LOGIN_AUDIT_MAX 32
struct login_audit_entry {
	uint32_t when;      /* unix seconds */
	uint32_t ip;
	uint8_t proto;
	uint8_t success;
	char user[32];
};
void loginaudit_note(int proto, uint32_t ip, const char *user, int success);
int  loginaudit_snapshot(struct login_audit_entry *out, int max); /* newest first */
int      protoban_snapshot(struct protoban_view *out, int max); /* active bans */
uint64_t protoban_total(void);

#endif /* MCS_LOGINTHROTTLE_H */
