/*
 * peerrep.h -- legacy IP:port peer evidence, constrained by D111.
 *
 * This optional legacy counter remains OFF by default and is NOT the R48
 * source x provider reputation model. It is keyed only by peer IP:port and
 * cannot supply R48 provider-scoped evidence.
 *
 * The existing config keys and event counters are retained for compatibility,
 * but their actions are bounded legacy gates: DISTRUST expires after 15 min;
 * ISOLATE and the old BAN threshold both expire after 60 min. No stage sets
 * FLAG_DISABLE. Runtime TTL actions are deliberately not restored from disk;
 * only event history is loaded, so a restart cannot revive a permanent ban.
 * When explicitly enabled, the temporary stages still affect the old request /
 * push gates and legacy consensus weighting; the R48 provider-scoped evidence
 * table neither reads nor persists these stages.
 *
 * D111's full quarantine contract (provider/channel scope, clean-sample exit,
 * weighted 30% cap and storm guard) must be implemented separately and remains
 * outstanding. This legacy layer is a safety correction, not a substitute.
 */

#ifndef MCS_PEERREP_H
#define MCS_PEERREP_H

#include <stdint.h>

#define PEERREP_STAGE_NONE      0   /* monitor only */
#define PEERREP_STAGE_DISTRUST  1   /* temporary request-order pause */
#define PEERREP_STAGE_ISOLATE   2   /* temporary cache-peer isolation */
#define PEERREP_STAGE_BAN       3   /* legacy threshold; 60m isolate, never disable */

#define PEERREP_DISTRUST_TTL_MS  900000u  /* D111: 15 minutes */
#define PEERREP_ISOLATE_TTL_MS  3600000u  /* D111: 60 minutes */
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

/* Record one diagnostic event. A counted legacy event can raise a temporary
 * stage; "push-while-isolated" is diagnostic only and cannot renew or raise
 * a stage. Returns the stage entered on a threshold crossing, else 0. */
int  peerrep_note(uint32_t ip, uint16_t port, const char *reason,
                  uint32_t now_ms);

/* Ladder gates: both return 1 (allow) whenever the module is off. */
int  peerrep_ask_ok(uint32_t ip, uint16_t port, uint32_t now_ms);
int  peerrep_push_ok(uint32_t ip, uint16_t port, uint32_t now_ms);

int  peerrep_stage(uint32_t ip, uint16_t port, uint32_t now_ms);

/* telnet/web listing; expired TTL stages are normalized during the read. */
int  peerrep_count(void);
int  peerrep_get(int idx, uint32_t now_ms, uint32_t *ip, uint16_t *port,
                 int *stage, int *events, char *reason, int rmax);

#endif /* MCS_PEERREP_H */
