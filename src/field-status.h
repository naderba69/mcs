/* field-status.h — one snapshot of the field-trial state for the
 * operator pages. The DoubleCheck state (ecm-doublecheck.h) and the
 * R22 counters (r22stats.h) are statics of the main translation unit;
 * httpserver.o is a separate TU and must never include those headers
 * (it would silently get its own zeroed copies). main.c fills this
 * POD through mcs_field_status_fill(); httpserver.c only reads it. */
#ifndef FIELD_STATUS_H
#define FIELD_STATUS_H

#include <stdint.h>

#define MCS_DC_EVENT_MAX 32

struct mcs_dc_event {
    uint32_t timestamp;         /* wall-clock seconds, display only */
    uint16_t caid;
    uint32_t provid;
    uint16_t sid;
    int kind;                   /* 1 = two sources agree, 2 = disagree */
    int sources;
};

struct mcs_cwpair_pair { uint16_t caid_a; uint32_t provid_a; uint16_t sid_a;
                         uint16_t caid_b; uint32_t provid_b; uint16_t sid_b;
                         unsigned matches; uint32_t last_tick; }; /* band 82: freshness for confirmation */
void nopushafter_snapshot(unsigned long *checked, unsigned long *blocked, int *entries);
void service_doubt_snapshot(unsigned long *checked, unsigned long *doubt, unsigned long *majority);
int cwpair_snapshot(unsigned long *observed, unsigned long *pair_matches,
                    unsigned long *served, unsigned long *stale,
                    unsigned long *strict_rejected, unsigned long *no_twin,
                    int *discover, struct mcs_cwpair_pair *pairs, int maxpairs);

/* CW_DUP recent flag (band 104), newest-first copy for /user-trace */
#define MCS_CWDUP_EVENT_MAX 12
/* Per-CAID consensus tally row (band 112) */
#define MCS_CONS_CAID_MAX 16
struct mcs_cons_caid {
    uint16_t caid;
    unsigned long votes;
    unsigned long holds;
    unsigned long releases;
    unsigned long refusals;
};

/* Per-CAID post-consensus gate tally row (band 110) */
#define MCS_GATE_CAID_MAX 16
struct mcs_gate_caid {
    uint16_t caid;
    unsigned long seen;
    unsigned long no_cycle;
    unsigned long cycle_late;
    unsigned long cycle_wrong;
    unsigned long shadow_rot;       /* one-half rotation (est. would pass) */
    unsigned long shadow_neither;   /* neither half stable */
    unsigned long shadow_bothsame;  /* identical to prevcw */
    unsigned long shadow_noprev;    /* no previous CW on the channel */
    unsigned long cyc1_undeclared;  /* channel declares no cycle parity */
    unsigned long cyc1_learnable;   /* ...with real rotation evidence unused */
};

struct mcs_cwdup_event {
    uint32_t timestamp;
    uint16_t caid;
    uint32_t provid;
    uint16_t sid;
    int rep;
    int rep_needed;
    int mode;                   /* 1 = log-only, 2 = reject */
    int rejected;
    uint32_t peer_id;
    uint32_t competitor_age_ms;
};

struct mcs_field_status {
    int r22_compiled;           /* built with -DR22_LAYERS */
    int r22_enabled;            /* R22 LAYERS config switch */
    uint32_t r22_cache_total;
    uint32_t r22_cache_accepted;
    uint32_t r22_cache_rejected;
    uint32_t r22_pattern_fake;
    uint32_t r22_cross_blocked;
    uint32_t r22_propblock_hit;
    uint32_t r22_quarantine_held;
    uint32_t r22_server_total;
    int dc_caids;               /* CAIDs under DoubleCheck observation */
    uint64_t dc_total;
    uint64_t dc_ok;
    uint64_t dc_fail;
    uint64_t dc_timeouts;
    int dc_event_count;
    struct mcs_dc_event dc_events[MCS_DC_EVENT_MAX]; /* newest first */
    /* CWPAIR (band 38) */
    int cwpair_discover;
    unsigned long cwpair_observed;
    unsigned long cwpair_matches;
    unsigned long cwpair_served;
    unsigned long cwpair_stale;
    unsigned long cwpair_strict_rejected;
    unsigned long cwpair_no_twin;
    int cwpair_pair_count;
    struct mcs_cwpair_pair cwpair_pairs[32];
    /* CACHEEX NOPUSH AFTER (band 41) */
    int nopush_entries;
    unsigned long nopush_checked;
    unsigned long nopush_blocked;
    /* SERVICE DOUBT (band 42) */
    int svc_armed;
    int svc_profiles_on;
    int svc_profiles_off;
    /* Armed-gates summary (band 46): so the trace page alone shows
     * which rejecting gates are live, globally and per profile. */
    int gate_ci_global;
    int gate_ci_on;
    int gate_ci_off;
    int gate_dd_global;
    int gate_dd_on;
    int gate_dd_off;
    int gate_ev_profiles;
    int gate_checksum_exempt_profiles;
    /* Auto-recovery bookkeeping (band 47, monitor only) */
    int ar_enabled;
    unsigned long ar_recoveries;
    unsigned long ar_successes;
    int ar_active;
    unsigned long svc_checked;
    unsigned long svc_doubt;
    unsigned long svc_majority;
    /* CW_DUP (band 104): global mode + counters + recent flags */
    int cwdup_global_mode;      /* CACHE SF CWDUP: 0 off, 1 log-only, 2 reject */
    uint64_t cwdup_flagged;
    uint64_t cwdup_rejected;
    uint64_t cwdup_event_total;
    int cwdup_event_count;
    struct mcs_cwdup_event cwdup_events[MCS_CWDUP_EVENT_MAX];
    /* CW_REUSE (band 105): same shape, second detector */
    int cwreuse_global_mode;    /* CACHE SF CWREUSE: 0 off, 1 log-only, 2 reject */
    uint64_t cwreuse_flagged;
    uint64_t cwreuse_rejected;
    uint64_t cwreuse_event_total;
    int cwreuse_event_count;
    struct mcs_cwdup_event cwreuse_events[MCS_CWDUP_EVENT_MAX];
    /* MARCHCHECK (band 106): same shape, third detector; in events,
     * rep carries the parked slot (0 = first half, 1 = second half) */
    int marchcheck_global_mode; /* CACHE SF MARCHCHECK: 0 off, 1 log-only, 2 reject */
    uint64_t marchcheck_flagged;
    uint64_t marchcheck_rejected;
    uint64_t marchcheck_event_total;
    int marchcheck_event_count;
    struct mcs_cwdup_event marchcheck_events[MCS_CWDUP_EVENT_MAX];
    /* LATECW (band 107): same shape, fourth detector; in events,
     * rep = ECM age at arrival (ms), rep_needed = threshold (ms) */
    int latecw_global_mode;     /* CACHE SF LATECW: 0 off, 1 log-only, 2 reject */
    int latecw_global_ms;       /* CACHE SF LATECWMS */
    uint64_t latecw_flagged;
    uint64_t latecw_rejected;
    uint64_t latecw_event_total;
    int latecw_event_count;
    struct mcs_cwdup_event latecw_events[MCS_CWDUP_EVENT_MAX];
    /* Cache consensus + post-consensus gate tally (band 109) */
    unsigned long cons_votes;
    unsigned long cons_holds;
    unsigned long cons_released;
    unsigned long cons_refused;
    int cons_held_now;
    int cons_overdue_now;
    unsigned long gate_seen;
    unsigned long gate_passed;
    unsigned long gate_expired;
    unsigned long gate_no_cycle;
    unsigned long gate_cycle_late;
    unsigned long gate_cycle_wrong;
    unsigned long gate_halfnull;
    unsigned long shadow_rot_cw1, shadow_rot_cw0, shadow_neither;
    unsigned long shadow_bothsame, shadow_noprev;
    unsigned long cyc1_declared, cyc1_undeclared, cyc1_learnable;
    int cons_caid_count;
    int cons_caid_dropped;
    unsigned long cons_caid_overflow;
    struct mcs_cons_caid cons_caids[MCS_CONS_CAID_MAX];
    int gate_caid_count;
    unsigned long gate_caid_overflow;
    struct mcs_gate_caid gate_caids[MCS_GATE_CAID_MAX];
};

void mcs_field_status_fill(struct mcs_field_status *st);

#endif /* FIELD_STATUS_H */
