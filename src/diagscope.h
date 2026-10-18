/*
 * MultiCS r82a -- src/diagscope.h
 *
 * TASK R48 (D111) -- the detail scope of the diagnosis page.
 *
 * The page keeps two kinds of numbers. The counters (delivered, refused by
 * reason, held) are cheap and always on under DCW STATS. The DETAIL --
 * the latency of a card answer, and the requests that ended with no answer
 * at all -- is recorded only for the channels and profiles named here. An
 * empty scope costs one comparison per ECM and nothing else.
 *
 * The scope is changed at run time from the telnet (`diagscope ...`), never
 * from the config file alone, so the operator can start a measurement on a
 * live box without a restart. It is bounded: at most DIAGSCOPE_CHANNELS
 * channels and DIAGSCOPE_PROFILES profiles, so detail cannot grow without
 * limit.
 *
 * This header is pure data and rules. It never locks; the caller (cwlog.c)
 * holds the mutex that already guards the channel table.
 */
#ifndef MCS_DIAGSCOPE_H
#define MCS_DIAGSCOPE_H

#include <stdint.h>
#include <string.h>

#define DIAGSCOPE_CHANNELS	4
#define DIAGSCOPE_PROFILES	4
#define DIAGSCOPE_NAME		64

struct diag_scope {
	int      nch;
	uint32_t caid[DIAGSCOPE_CHANNELS];
	uint32_t provid[DIAGSCOPE_CHANNELS];
	uint16_t sid[DIAGSCOPE_CHANNELS];
	int      nprof;
	char     prof[DIAGSCOPE_PROFILES][DIAGSCOPE_NAME];
};

__attribute__((unused))
static int diag_scope_empty(const struct diag_scope *s)
{
	return s->nch == 0 && s->nprof == 0;
}

__attribute__((unused))
static int diag_scope_find_ch(const struct diag_scope *s,
			      uint32_t caid, uint32_t provid, uint16_t sid)
{
	int i;

	for (i = 0; i < s->nch; i++)
		if (s->caid[i] == caid && s->provid[i] == provid && s->sid[i] == sid)
			return i;
	return -1;
}

/* A profile name as the config stores it: "[ cardx ]" keeps the blank before
 * the bracket, so trailing blanks are not part of the name. Both the stored name
 * and the one looked up are trimmed, or the card's own name would never match. */
__attribute__((unused))
static size_t diag_name_len(const char *n)
{
	size_t l = strlen(n);

	while (l > 0 && (n[l - 1] == ' ' || n[l - 1] == '\t')) l--;
	return l;
}

__attribute__((unused))
static int diag_name_eq(const char *a, const char *b)
{
	size_t la = diag_name_len(a);

	return la > 0 && la == diag_name_len(b) && !strncmp(a, b, la);
}

__attribute__((unused))
static int diag_scope_find_prof(const struct diag_scope *s, const char *name)
{
	int i;

	if (!name || diag_name_len(name) == 0) return -1;
	for (i = 0; i < s->nprof; i++)
		if (diag_name_eq(s->prof[i], name)) return i;
	return -1;
}

/* 0 added, 1 already in scope, -1 no room. */
__attribute__((unused))
static int diag_scope_add_ch(struct diag_scope *s,
			     uint32_t caid, uint32_t provid, uint16_t sid)
{
	if (diag_scope_find_ch(s, caid, provid, sid) >= 0) return 1;
	if (s->nch >= DIAGSCOPE_CHANNELS) return -1;
	s->caid[s->nch] = caid;
	s->provid[s->nch] = provid;
	s->sid[s->nch] = sid;
	s->nch++;
	return 0;
}

/* 0 removed, -1 not in scope. Keeps the rest in order. */
__attribute__((unused))
static int diag_scope_del_ch(struct diag_scope *s,
			     uint32_t caid, uint32_t provid, uint16_t sid)
{
	int i = diag_scope_find_ch(s, caid, provid, sid);

	if (i < 0) return -1;
	for (; i + 1 < s->nch; i++) {
		s->caid[i] = s->caid[i + 1];
		s->provid[i] = s->provid[i + 1];
		s->sid[i] = s->sid[i + 1];
	}
	s->nch--;
	return 0;
}

/* 0 added, 1 already in scope, -1 no room, -2 empty or too long. */
__attribute__((unused))
static int diag_scope_add_prof(struct diag_scope *s, const char *name)
{
	size_t l;

	if (!name) return -2;
	l = diag_name_len(name);
	if (l == 0 || l >= DIAGSCOPE_NAME) return -2;
	if (diag_scope_find_prof(s, name) >= 0) return 1;
	if (s->nprof >= DIAGSCOPE_PROFILES) return -1;
	memcpy(s->prof[s->nprof], name, l);
	s->prof[s->nprof][l] = 0;
	s->nprof++;
	return 0;
}

/* 0 removed, -1 not in scope. */
__attribute__((unused))
static int diag_scope_del_prof(struct diag_scope *s, const char *name)
{
	int i = diag_scope_find_prof(s, name);

	if (i < 0) return -1;
	for (; i + 1 < s->nprof; i++)
		memcpy(s->prof[i], s->prof[i + 1], DIAGSCOPE_NAME);
	s->nprof--;
	return 0;
}

__attribute__((unused))
static void diag_scope_clear(struct diag_scope *s)
{
	memset(s, 0, sizeof(*s));
}

/*
 * Does a request belong to the detail scope? A channel named in the scope
 * matches; so does a request whose profile is named in the scope. The
 * profile is the card server's name (cardserver_data.name), or NULL/"" when
 * there is none.
 */
__attribute__((unused))
static int diag_scope_match(const struct diag_scope *s,
			    uint32_t caid, uint32_t provid, uint16_t sid,
			    const char *profile)
{
	if (diag_scope_empty(s)) return 0;
	if (diag_scope_find_ch(s, caid, provid, sid) >= 0) return 1;
	return diag_scope_find_prof(s, profile) >= 0;
}

#endif /* MCS_DIAGSCOPE_H */
