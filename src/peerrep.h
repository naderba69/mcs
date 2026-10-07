/*
 * peerrep.h -- TASK R4 (D58): the persistent peer-reputation ladder.
 *
 * The surface: a cache peer that hands us poison is currently punished by
 * one-shot machinery only (dcwstats counters, per-source marks), none of
 * it persistent and none of it cumulative -- the same peer can poison us
 * every day from scratch. The remedy, opt-in like every protection:
 *
 *   PEER REPUTATION: ON              (default OFF = stock, zero accounting)
 *   PEER REPUTATION FILE: path       (default: multics.peers next to the cfg)
 *   PEER REPUTATION DISTRUST: 5      (confirmed bad events -> stop asking it)
 *   PEER REPUTATION ISOLATE: 20      (-> refuse its pushes)
 *   PEER REPUTATION BAN: 50          (-> peer disabled, pinned in the file)
 *
 * The ladder counts ONLY confirmed, individually-logged refusal events --
 * today the cycle-contradiction and the fake-CW content refusals, each one
 * already counted by dcwstats and logged with its reason. No timers, no
 * volume heuristics, no probabilities: a peer climbs only on evidence that
 * named it. Escalation is upward only; recovery is the operator editing
 * the file (documented in the field guide) -- the ladder never forgives on
 * its own, and an innocent peer needs many named, logged events to move at
 * all (zero mercy misfires).
 *
 * Past ISOLATE the peer's pushes are dropped unprocessed; a datagram that
 * arrives while isolated still counts, reason "push-while-isolated": after
 * six confirmed contradictions, choosing to keep pushing is the peer's own
 * escalation. Once banned, the stock FLAG_DISABLE path drops everything.
 *
 * The file is the source of truth across restarts: loaded at startup,
 * rewritten atomically (tmp+rename) on every escalation.
 */

#ifndef MCS_PEERREP_H
#define MCS_PEERREP_H

#include <stdint.h>

#define PEERREP_STAGE_NONE      0   /* monitor only */
#define PEERREP_STAGE_DISTRUST  1   /* stop asking it (no cache requests) */
#define PEERREP_STAGE_ISOLATE   2   /* also refuse its pushes */
#define PEERREP_STAGE_BAN       3   /* disabled + pinned in the file */

#define PEERREP_MAX_SLOTS   64
#define PEERREP_REASON_MAX  32
#define PEERREP_FILE_MAX    256

void peerrep_defaults(void);                        /* off + thresholds 5/20/50 */
void peerrep_configure(int on, const char *file, int distrust, int isolate, int ban);

/* The config parser's direct handles (the dcw_filter_* pattern): */
extern int  peerrep_on;                             /* PEER REPUTATION: ON/OFF */
void peerrep_file_set(const char *path);            /* PEER REPUTATION FILE: */
void peerrep_threshold_set(char which, int val);
void peerrep_thresholds(int *distrust, int *isolate, int *ban); /* TASK R4: startup log */    /* DISTRUST / ISOLATE / BAN */

/* Parse the reputation file into the table. Returns records loaded. */
int  peerrep_load(void);
const char *peerrep_file(void);  /* the resolved path, for the startup log */

/* Atomic rewrite of the whole table. No-op when off. */
void peerrep_save(void);

/* The n-th consecutive confirmed bad event from this peer. Returns the
 * stage escalated TO on a threshold crossing (1/2/3), else 0. */
int  peerrep_note(uint32_t ip, uint16_t port, const char *reason);

/* Ladder gates: both return 1 (allow) whenever the module is off. */
int  peerrep_ask_ok(uint32_t ip, uint16_t port);    /* stage < DISTRUST */
int  peerrep_push_ok(uint32_t ip, uint16_t port);   /* stage < ISOLATE  */

int  peerrep_stage(uint32_t ip, uint16_t port);     /* current stage, 0 when off */

/* telnet listing */
int  peerrep_count(void);
int  peerrep_get(int idx, uint32_t *ip, uint16_t *port, int *stage, int *events,
                 char *reason, int rmax);

#endif /* MCS_PEERREP_H */
