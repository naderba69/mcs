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
	uint32_t last;      /* lt_now() of the last failure */
};

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

uint32_t login_note_failure(uint32_t ip)
{
	int i, slot=-1, oldest=-1;
	uint32_t now = lt_now(), fails;

	pthread_mutex_lock(&lt_lock);
	for (i=0; i<LOGIN_SLOTS; i++) {
		if (lt_tab[i].ip==ip) {
			/* a quiet slot forgets: the attack is not one long story */
			if ( (now - lt_tab[i].last) > LOGIN_SLOT_EXPIRE ) lt_tab[i].fails = 0;
			lt_tab[i].fails++;
			lt_tab[i].last = now;
			fails = lt_tab[i].fails;
			pthread_mutex_unlock(&lt_lock);
			return fails;
		}
		if (lt_tab[i].ip==0) { slot = i; break; }             /* free slot */
		if ( (oldest<0) || ((now - lt_tab[i].last) > (now - lt_tab[oldest].last)) ) oldest = i;
	}
	if (slot<0) slot = (oldest>=0) ? oldest : 0;                 /* recycle the quietest */
	lt_tab[slot].ip = ip;
	lt_tab[slot].fails = 1;
	lt_tab[slot].last = now;
	fails = 1;
	pthread_mutex_unlock(&lt_lock);
	return fails;
}

void login_note_success(uint32_t ip)
{
	int i;
	pthread_mutex_lock(&lt_lock);
	for (i=0; i<LOGIN_SLOTS; i++) {
		if (lt_tab[i].ip==ip) {
			lt_tab[i].fails = 0;
			break;                       /* keep last, so the slot stays warm */
		}
	}
	pthread_mutex_unlock(&lt_lock);
}
