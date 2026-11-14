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
};

void mcs_field_status_fill(struct mcs_field_status *st);

#endif /* FIELD_STATUS_H */
