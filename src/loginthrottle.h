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
void     login_note_success(uint32_t ip);  /* forgets the IP's counter (a good login) */

/* pure schedule, exposed for the unit test: fails<=0 or base<=0 -> 0 */
uint32_t login_delay_ms(uint32_t base, uint32_t fails);

#endif /* MCS_LOGINTHROTTLE_H */
