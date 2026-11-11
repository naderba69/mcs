/*
 * peerrep.c -- legacy IP:port reputation diagnostics with D111-bounded gates.
 * See peerrep.h for the deliberately limited contract. This module is free of
 * cache-server structures; clustredcache.c owns peer context and logging.
 */

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <limits.h>

#include "peerrep.h"
#include "tick_math.h"

extern char config_file[256];   /* main.c -- only used to derive the default file */

struct peerrep_rec {
	uint32_t ip;
	uint16_t port;
	int      stage;
	int      events;                 /* lifetime diagnostic count */
	uint32_t stage_events;            /* since the last expiry/restart */
	uint32_t stage_until;
	int      stage_until_valid;
	char     lastreason[PEERREP_REASON_MAX];
};

static struct peerrep_rec pr_tab[PEERREP_MAX_SLOTS];
static int pr_count = 0;
static pthread_mutex_t pr_lock = PTHREAD_MUTEX_INITIALIZER;

int  peerrep_on = 0;
static char pr_file[PEERREP_FILE_MAX] = "";
static int  pr_th_distrust = 5;
static int  pr_th_isolate  = 20;
static int  pr_th_ban      = 50;

/* ------------------------------------------------------------------ */

static uint32_t pr_stage_ttl(int stage)
{
	return stage == PEERREP_STAGE_DISTRUST
		? PEERREP_DISTRUST_TTL_MS : PEERREP_ISOLATE_TTL_MS;
}

/* Expiry is normalized lazily under pr_lock by every enforcement/read path. */
static void pr_expire_stage_locked(struct peerrep_rec *r, uint32_t now_ms)
{
	if (r->stage != PEERREP_STAGE_NONE && r->stage_until_valid &&
	    tick32_deadline_reached(now_ms, r->stage_until)) {
		r->stage = PEERREP_STAGE_NONE;
		r->stage_events = 0;
		r->stage_until = 0;
		r->stage_until_valid = 0;
	}
}

static void pr_set_stage_locked(struct peerrep_rec *r, int stage, uint32_t now_ms)
{
	r->stage = stage;
	r->stage_until = now_ms + pr_stage_ttl(stage);
	r->stage_until_valid = 1;
}

static void fmt_ip(char *dst, uint32_t ip)
{
	sprintf(dst, "%u.%u.%u.%u", 0xFF&(ip), 0xFF&(ip>>8), 0xFF&(ip>>16), 0xFF&(ip>>24));
}

static struct peerrep_rec *pr_find(uint32_t ip, uint16_t port)
{
	int i;
	for (i=0; i<pr_count; i++)
		if (pr_tab[i].ip==ip && pr_tab[i].port==port) return &pr_tab[i];
	return NULL;
}

/* ------------------------------------------------------------------ */

void peerrep_defaults(void)
{
	peerrep_on = 0;
	pr_file[0] = 0;
	pr_th_distrust = 5;
	pr_th_isolate  = 20;
	pr_th_ban      = 50;
	pthread_mutex_lock(&pr_lock);
	pr_count = 0;
	memset(pr_tab, 0, sizeof(pr_tab));
	pthread_mutex_unlock(&pr_lock);
}

void peerrep_file_set(const char *path)
{
	if (!path) return;
	strncpy(pr_file, path, PEERREP_FILE_MAX-1);
	pr_file[PEERREP_FILE_MAX-1] = 0;
}

void peerrep_threshold_set(char which, int val)
{
	if (which=='D') pr_th_distrust = val;
	else if (which=='I') pr_th_isolate = val;
	else if (which=='B') pr_th_ban = val;
	/* keep the ladder ordered */
	if (pr_th_distrust < 1) pr_th_distrust = 1;
	if (pr_th_isolate <= pr_th_distrust) pr_th_isolate = pr_th_distrust+1;
	if (pr_th_ban <= pr_th_isolate) pr_th_ban = pr_th_isolate+1;
}

void peerrep_thresholds(int *distrust, int *isolate, int *ban)
{
	if (distrust) *distrust = pr_th_distrust;
	if (isolate) *isolate = pr_th_isolate;
	if (ban) *ban = pr_th_ban;
}

void peerrep_configure(int on, const char *file, int distrust, int isolate, int ban)
{
	peerrep_on = on;
	if (file) {
		strncpy(pr_file, file, PEERREP_FILE_MAX-1);
		pr_file[PEERREP_FILE_MAX-1] = 0;
	}
	if (distrust < 1) distrust = 1;
	if (isolate <= distrust) isolate = distrust+1;
	if (ban <= isolate) ban = isolate+1;
	pr_th_distrust = distrust;
	pr_th_isolate  = isolate;
	pr_th_ban      = ban;
}

static void pr_derive_file(void)
{
	char *slash;
	if (pr_file[0]) return;
	slash = strrchr(config_file, '/');
	if (slash) snprintf(pr_file, PEERREP_FILE_MAX, "%.*s/multics.peers", (int)(slash-config_file), config_file);
	else       snprintf(pr_file, PEERREP_FILE_MAX, "multics.peers");
}

const char *peerrep_file(void)
{
	pr_derive_file();
	return pr_file;
}

int peerrep_load(void)
{
	FILE *f;
	char line[256];
	int loaded = 0, bad = 0;

	if (!peerrep_on) return 0;
	pr_derive_file();
	pthread_mutex_lock(&pr_lock);
	pr_count = 0;
	memset(pr_tab, 0, sizeof(pr_tab));
	f = fopen(pr_file, "r");
	if (f) {
		while (fgets(line, sizeof(line), f)) {
			char ipstr[24], reason[PEERREP_REASON_MAX];
			unsigned port;
			int stage, events;
			if (line[0]=='#' || line[0]=='\n' || line[0]==0) continue;
			reason[0] = 0;
			if (sscanf(line, "%23[^:]:%u %d %d %31s", ipstr, &port, &stage, &events, reason) >= 4) {
				uint32_t ip = (uint32_t)inet_addr(ipstr);
				if ( (ip!=0xFFFFFFFFu) && (port>0) && (port<65536) &&
				     (stage>=PEERREP_STAGE_NONE) && (stage<=PEERREP_STAGE_BAN) && (events>=stage) ) {
					if (pr_count < PEERREP_MAX_SLOTS) {
						struct peerrep_rec *r = &pr_tab[pr_count++];
						r->ip = ip; r->port = (uint16_t)port;
						/* A persisted legacy stage is history only: never revive its action. */
						r->stage = PEERREP_STAGE_NONE; r->events = events;
						r->stage_events = 0;
						r->stage_until = 0; r->stage_until_valid = 0;
						snprintf(r->lastreason, sizeof(r->lastreason), "%s", reason);
						loaded++;
						continue;
					}
				}
			}
			bad++;
		}
		fclose(f);
	}
	pthread_mutex_unlock(&pr_lock);
	(void)bad; /* a bad line is skipped, not fatal; the caller may log loaded */
	return loaded;
}

void peerrep_save(void)
{
	FILE *f;
	char tmp[PEERREP_FILE_MAX+8];
	int i;

	if (!peerrep_on) return;
	pr_derive_file();
	snprintf(tmp, sizeof(tmp), "%s.tmp", pr_file);
	pthread_mutex_lock(&pr_lock);
	f = fopen(tmp, "w");
	if (!f) { pthread_mutex_unlock(&pr_lock); return; }
	fprintf(f, "# peer reputation history; legacy stage column is 0 and never restored (D111)\n");
	for (i=0; i<pr_count; i++) {
		char ipstr[24];
		fmt_ip(ipstr, pr_tab[i].ip);
		fprintf(f, "%s:%u %d %d %s\n", ipstr, pr_tab[i].port,
			PEERREP_STAGE_NONE, pr_tab[i].events,
			pr_tab[i].lastreason[0] ? pr_tab[i].lastreason : "unknown");
	}
	fclose(f);
	rename(tmp, pr_file);
	pthread_mutex_unlock(&pr_lock);
}

/* ------------------------------------------------------------------ */

int peerrep_note(uint32_t ip, uint16_t port, const char *reason, uint32_t now_ms)
{
	int i, escalated = 0;
	int confirmed;
	struct peerrep_rec *r;

	if (!peerrep_on) return 0;
	/* A gated push is not fresh proof of poisoning and must not renew TTL. */
	confirmed = reason && strcmp(reason, "push-while-isolated") != 0;
	pthread_mutex_lock(&pr_lock);
	for (i=0; i<pr_count; i++) {
		if (pr_tab[i].ip==ip && pr_tab[i].port==port) break;
	}
	if (i>=pr_count) {
		if (pr_count >= PEERREP_MAX_SLOTS) { pthread_mutex_unlock(&pr_lock); return 0; }
		r = &pr_tab[pr_count++];
		memset(r, 0, sizeof(*r));
		r->ip = ip; r->port = port;
		r->stage = PEERREP_STAGE_NONE;
	} else {
		r = &pr_tab[i];
	}

	pr_expire_stage_locked(r, now_ms);
	if (r->events < INT_MAX) r->events++;
	if (confirmed && r->stage_events < UINT32_MAX) r->stage_events++;
	if (reason)
		snprintf(r->lastreason, sizeof(r->lastreason), "%s", reason);

	/*
	 * A fresh confirmed event renews an active TTL, but cannot make it
	 * permanent: absent further bad evidence the action ends on deadline.
	 * The old BAN threshold maps to the same bounded isolation as ISOLATE.
	 */
	if (confirmed && r->stage != PEERREP_STAGE_NONE)
		r->stage_until = now_ms + pr_stage_ttl(r->stage);
	if (!confirmed) {
		pthread_mutex_unlock(&pr_lock);
		return 0;
	}

	if ( (r->stage < PEERREP_STAGE_BAN) &&
	     (r->stage_events >= (uint32_t)pr_th_ban) ) {
		pr_set_stage_locked(r, PEERREP_STAGE_BAN, now_ms);
		escalated = PEERREP_STAGE_BAN;
	}
	else if ( (r->stage < PEERREP_STAGE_ISOLATE) &&
	         (r->stage_events >= (uint32_t)pr_th_isolate) ) {
		pr_set_stage_locked(r, PEERREP_STAGE_ISOLATE, now_ms);
		escalated = PEERREP_STAGE_ISOLATE;
	}
	else if ( (r->stage < PEERREP_STAGE_DISTRUST) &&
	         (r->stage_events >= (uint32_t)pr_th_distrust) ) {
		pr_set_stage_locked(r, PEERREP_STAGE_DISTRUST, now_ms);
		escalated = PEERREP_STAGE_DISTRUST;
	}
	pthread_mutex_unlock(&pr_lock);
	return escalated;
}

int peerrep_ask_ok(uint32_t ip, uint16_t port, uint32_t now_ms)
{
	struct peerrep_rec *r;
	int ok = 1;
	if (!peerrep_on) return 1;
	pthread_mutex_lock(&pr_lock);
	r = pr_find(ip, port);
	if (r) {
		pr_expire_stage_locked(r, now_ms);
		if (r->stage >= PEERREP_STAGE_DISTRUST) ok = 0;
	}
	pthread_mutex_unlock(&pr_lock);
	return ok;
}

int peerrep_push_ok(uint32_t ip, uint16_t port, uint32_t now_ms)
{
	struct peerrep_rec *r;
	int ok = 1;
	if (!peerrep_on) return 1;
	pthread_mutex_lock(&pr_lock);
	r = pr_find(ip, port);
	if (r) {
		pr_expire_stage_locked(r, now_ms);
		if (r->stage >= PEERREP_STAGE_ISOLATE) ok = 0;
	}
	pthread_mutex_unlock(&pr_lock);
	return ok;
}

int peerrep_stage(uint32_t ip, uint16_t port, uint32_t now_ms)
{
	struct peerrep_rec *r;
	int st = 0;
	if (!peerrep_on) return 0;
	pthread_mutex_lock(&pr_lock);
	r = pr_find(ip, port);
	if (r) {
		pr_expire_stage_locked(r, now_ms);
		st = r->stage;
	}
	pthread_mutex_unlock(&pr_lock);
	return st;
}

/* ------------------------------------------------------------------ */

int peerrep_count(void)
{
	int n;
	if (!peerrep_on) return 0;
	pthread_mutex_lock(&pr_lock);
	n = pr_count;
	pthread_mutex_unlock(&pr_lock);
	return n;
}

int peerrep_get(int idx, uint32_t now_ms, uint32_t *ip, uint16_t *port,
                int *stage, int *events, char *reason, int rmax)
{
	int ok = 0;
	if (!peerrep_on) return 0;
	pthread_mutex_lock(&pr_lock);
	if ( (idx>=0) && (idx<pr_count) ) {
		pr_expire_stage_locked(&pr_tab[idx], now_ms);
		*ip = pr_tab[idx].ip; *port = pr_tab[idx].port;
		*stage = pr_tab[idx].stage; *events = pr_tab[idx].events;
		if (reason && rmax>0) {
			strncpy(reason, pr_tab[idx].lastreason, rmax-1);
			reason[rmax-1] = 0;
		}
		ok = 1;
	}
	pthread_mutex_unlock(&pr_lock);
	return ok;
}
