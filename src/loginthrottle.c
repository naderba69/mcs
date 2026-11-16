/*
 * loginthrottle.c -- TASK R3 (D57): the failure table and the allowlists.
 * See loginthrottle.h for the contract.
 *
 * The table is 64 fixed slots under one mutex, exactly the house shape of
 * the purge and rotation tables: no allocation on the login path, the
 * oldest-quiet slot is recycled when full, and an entry that has been
 * quiet past LOGIN_SLOT_EXPIRE starts from zero again. The caller sleeps,
 * never this module: the delay is a number the door's own connection
 * thread chooses to wait before answering a failure.
 */

#include <string.h>
#include <time.h>
#include <pthread.h>

#include "loginthrottle.h"

#ifndef MCS_LT_NOGLOBALS
#include "tools.h"       /* GetTickCount */

uint32_t lt_now(void)
{
	return (uint32_t)GetTickCount();
}
#endif
/* under MCS_LT_NOGLOBALS the unit test supplies lt_now and nothing here
 * touches the real clock */

struct login_slot {
	uint32_t ip;
	uint32_t fails;
	uint32_t last;          /* lt_now() of the last failure */
	uint32_t banned_until;  /* band 57: lt_now() deadline, 0 = not banned */
};

static uint64_t lt_bans_total = 0;  /* band 57: bans ever triggered */

static struct login_slot lt_tab[LOGIN_SLOTS];
static pthread_mutex_t lt_lock = PTHREAD_MUTEX_INITIALIZER;

/* --- allowlist ------------------------------------------------------------ */

void login_allow_reset(struct login_allow *lst)
{
	lst->count = 0;
}

int login_allow_add(struct login_allow *lst, uint32_t ip)
{
	int i;
	for (i=0; i<lst->count; i++) if (lst->ip[i]==ip) return 1; /* already there */
	if (lst->count>=LOGIN_ALLOW_MAX) return 0;
	lst->ip[lst->count++] = ip;
	return 1;
}

int login_allowed(const struct login_allow *lst, uint32_t ip)
{
	int i;
	if (lst->count==0) return 1;                 /* the list is off */
	for (i=0; i<lst->count; i++) if (lst->ip[i]==ip) return 1;
	return 0;
}

/* --- failure table --------------------------------------------------------- */

uint32_t login_delay_ms(uint32_t base, uint32_t fails)
{
	uint32_t d;
	if ( (base==0) || (fails==0) ) return 0;
	d = base;
	/* base << (fails-1), overflow-safe: once at the cap, stop shifting */
	while (fails>1) {
		if (d>=LOGIN_DELAY_CAP) return LOGIN_DELAY_CAP;
		d <<= 1;
		fails--;
	}
	if (d>LOGIN_DELAY_CAP) d = LOGIN_DELAY_CAP;
	return d;
}

static uint32_t lt_note_failure(uint32_t ip, uint32_t threshold,
                                uint32_t ban_seconds, int *banned_now)
{
	int i, slot=-1, oldest=-1;
	uint32_t now = lt_now(), fails;

	if (banned_now) *banned_now = 0;
	pthread_mutex_lock(&lt_lock);
	for (i=0; i<LOGIN_SLOTS; i++) {
		if (lt_tab[i].ip==ip) { slot = i; break; }
		if (lt_tab[i].ip==0) { if (slot<0) slot = i; }       /* free slot */
		else if ( (oldest<0) || ((now - lt_tab[i].last) > (now - lt_tab[oldest].last)) ) oldest = i;
	}
	if (slot<0) slot = (oldest>=0) ? oldest : 0;             /* recycle the quietest */
	if (lt_tab[slot].ip!=ip) {
		lt_tab[slot].ip = ip;
		lt_tab[slot].fails = 0;
		lt_tab[slot].banned_until = 0;
	} else if ( (now - lt_tab[slot].last) > LOGIN_SLOT_EXPIRE ) {
		/* a quiet slot forgets: the attack is not one long story */
		lt_tab[slot].fails = 0;
		lt_tab[slot].banned_until = 0;
	}
	lt_tab[slot].fails++;
	lt_tab[slot].last = now;
	fails = lt_tab[slot].fails;
	if ( threshold && ban_seconds && fails>=threshold ) {
		lt_tab[slot].banned_until = now + ban_seconds*1000u;
		lt_bans_total++;
		if (banned_now) *banned_now = 1;
	}
	pthread_mutex_unlock(&lt_lock);
	return fails;
}

uint32_t login_note_failure(uint32_t ip)
{
	return lt_note_failure(ip, 0, 0, NULL);
}

uint32_t login_note_failure_ex(uint32_t ip, uint32_t threshold,
                               uint32_t ban_seconds, int *banned_now)
{
	return lt_note_failure(ip, threshold, ban_seconds, banned_now);
}

int login_banned(uint32_t ip)
{
	int i, banned = 0;
	uint32_t now = lt_now();
	pthread_mutex_lock(&lt_lock);
	for (i=0; i<LOGIN_SLOTS; i++) {
		if (lt_tab[i].ip==ip) {
			banned = lt_tab[i].banned_until &&
			         ((int32_t)(lt_tab[i].banned_until - now) > 0);
			break;
		}
	}
	pthread_mutex_unlock(&lt_lock);
	return banned;
}

void login_ban_status(uint64_t *total_bans, int *active_bans)
{
	int i, active = 0;
	uint32_t now = lt_now();
	pthread_mutex_lock(&lt_lock);
	for (i=0; i<LOGIN_SLOTS; i++) {
		if (lt_tab[i].banned_until &&
		    ((int32_t)(lt_tab[i].banned_until - now) > 0)) active++;
	}
	if (total_bans) *total_bans = lt_bans_total;
	if (active_bans) *active_bans = active;
	pthread_mutex_unlock(&lt_lock);
}

void login_note_success(uint32_t ip)
{
	int i;
	pthread_mutex_lock(&lt_lock);
	for (i=0; i<LOGIN_SLOTS; i++) {
		if (lt_tab[i].ip==ip) {
			lt_tab[i].fails = 0;
			lt_tab[i].banned_until = 0;   /* band 57: a good login also lifts a ban */
			break;                       /* keep last, so the slot stays warm */
		}
	}
	pthread_mutex_unlock(&lt_lock);
}

/* --- per-protocol bans (band 60) ------------------------------------------ */

#define PB_SLOTS 128

struct protoban_slot {
	uint32_t ip;
	uint32_t fails[PB_COUNT];
	uint32_t banned_until[PB_COUNT];
	uint32_t last;
};

static struct protoban_slot pb_tab[PB_SLOTS];
static pthread_mutex_t pb_lock = PTHREAD_MUTEX_INITIALIZER;
static uint64_t pb_bans_total = 0;

static const char *pb_names[PB_COUNT] = {
	"http", "telnet", "cccam", "newcamd", "mgcamd", "cs378x", "camd35", "cacheex"
};

const char *protoban_name(int proto)
{
	if (proto < 0 || proto >= PB_COUNT) return "?";
	return pb_names[proto];
}

static int pb_banned_locked(struct protoban_slot *slot, int proto, uint32_t now)
{
	return slot->banned_until[proto] &&
	       ((int32_t)(slot->banned_until[proto] - now) > 0);
}

uint32_t protoban_note_failure(uint32_t ip, int proto, uint32_t threshold,
                               uint32_t ban_seconds, int *banned_now)
{
	int i, slot = -1, oldest = -1;
	uint32_t now = lt_now(), fails;

	if (banned_now) *banned_now = 0;
	if (proto < 0 || proto >= PB_COUNT) return 0;
	pthread_mutex_lock(&pb_lock);
	for (i = 0; i < PB_SLOTS; i++) {
		if (pb_tab[i].ip == ip) { slot = i; break; }
		if (pb_tab[i].ip == 0) { if (slot < 0) slot = i; }
		else if (!pb_banned_locked(&pb_tab[i], proto, now) &&
		         (oldest < 0 || (now - pb_tab[i].last) > (now - pb_tab[oldest].last))) oldest = i;
	}
	if (slot < 0) slot = (oldest >= 0) ? oldest : 0;
	if (pb_tab[slot].ip != ip) {
		memset(&pb_tab[slot], 0, sizeof(pb_tab[slot]));
		pb_tab[slot].ip = ip;
	} else if ( (now - pb_tab[slot].last) > LOGIN_SLOT_EXPIRE &&
	            !pb_banned_locked(&pb_tab[slot], proto, now) ) {
		pb_tab[slot].fails[proto] = 0;   /* a quiet protocol forgets */
	}
	pb_tab[slot].fails[proto]++;
	pb_tab[slot].last = now;
	fails = pb_tab[slot].fails[proto];
	if ( threshold && ban_seconds && fails >= threshold ) {
		pb_tab[slot].banned_until[proto] = now + ban_seconds*1000u;
		pb_bans_total++;
		if (banned_now) *banned_now = 1;
	}
	pthread_mutex_unlock(&pb_lock);
	return fails;
}

void protoban_note_success(uint32_t ip, int proto)
{
	int i;
	if (proto < 0 || proto >= PB_COUNT) return;
	pthread_mutex_lock(&pb_lock);
	for (i = 0; i < PB_SLOTS; i++) {
		if (pb_tab[i].ip == ip) {
			pb_tab[i].fails[proto] = 0;
			pb_tab[i].banned_until[proto] = 0;
			break;
		}
	}
	pthread_mutex_unlock(&pb_lock);
}

int protoban_banned(uint32_t ip, int proto)
{
	int i, banned = 0;
	uint32_t now = lt_now();
	if (proto < 0 || proto >= PB_COUNT) return 0;
	pthread_mutex_lock(&pb_lock);
	for (i = 0; i < PB_SLOTS; i++) {
		if (pb_tab[i].ip == ip) { banned = pb_banned_locked(&pb_tab[i], proto, now); break; }
	}
	pthread_mutex_unlock(&pb_lock);
	return banned;
}

int protoban_unban(uint32_t ip, int proto)
{
	int i, cleared = 0;
	pthread_mutex_lock(&pb_lock);
	for (i = 0; i < PB_SLOTS; i++) {
		if (pb_tab[i].ip == ip) {
			int a = (proto < 0) ? 0 : proto;
			int b = (proto < 0) ? PB_COUNT - 1 : proto;
			int pr;
			for (pr = a; pr <= b; pr++) {
				if (pb_banned_locked(&pb_tab[i], pr, lt_now())) cleared++;
				pb_tab[i].banned_until[pr] = 0;
				pb_tab[i].fails[pr] = 0;
			}
			break;
		}
	}
	pthread_mutex_unlock(&pb_lock);
	return cleared;
}

int protoban_snapshot(struct protoban_view *out, int max)
{
	int i, pr, n = 0;
	uint32_t now = lt_now();
	pthread_mutex_lock(&pb_lock);
	for (i = 0; i < PB_SLOTS && n < max; i++) {
		if (!pb_tab[i].ip) continue;
		for (pr = 0; pr < PB_COUNT && n < max; pr++) {
			if (pb_banned_locked(&pb_tab[i], pr, now)) {
				out[n].ip = pb_tab[i].ip;
				out[n].proto = (uint8_t)pr;
				out[n].seconds_left = (pb_tab[i].banned_until[pr] - now) / 1000u;
				out[n].fails = pb_tab[i].fails[pr];
				n++;
			}
		}
	}
	pthread_mutex_unlock(&pb_lock);
	return n;
}

uint64_t protoban_total(void)
{
	uint64_t t;
	pthread_mutex_lock(&pb_lock);
	t = pb_bans_total;
	pthread_mutex_unlock(&pb_lock);
	return t;
}

/* --- login attempt audit (band 62) ---------------------------------------- */

static struct login_audit_entry la_ring[LOGIN_AUDIT_MAX];
static int la_head = 0;   /* next write slot */
static int la_count = 0;
static pthread_mutex_t la_lock = PTHREAD_MUTEX_INITIALIZER;

void loginaudit_note(int proto, uint32_t ip, const char *user, int success)
{
	struct login_audit_entry *e;
	if (proto < 0 || proto >= PB_COUNT) return;
	pthread_mutex_lock(&la_lock);
	e = &la_ring[la_head];
	la_head = (la_head + 1) % LOGIN_AUDIT_MAX;
	if (la_count < LOGIN_AUDIT_MAX) la_count++;
	e->when = (uint32_t)time(NULL);
	e->ip = ip;
	e->proto = (uint8_t)proto;
	e->success = success ? 1 : 0;
	if (user) {
		strncpy(e->user, user, sizeof(e->user) - 1);
		e->user[sizeof(e->user) - 1] = 0;
	} else e->user[0] = 0;
	pthread_mutex_unlock(&la_lock);
}

int loginaudit_snapshot(struct login_audit_entry *out, int max)
{
	int i, n = 0;
	pthread_mutex_lock(&la_lock);
	for (i = 0; i < la_count && n < max; i++) {
		int idx = (la_head - 1 - i + LOGIN_AUDIT_MAX * 2) % LOGIN_AUDIT_MAX;
		out[n++] = la_ring[idx];
	}
	pthread_mutex_unlock(&la_lock);
	return n;
}
