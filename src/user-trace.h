/* Bounded, opt-in diagnostic session for a single CCcam client and channel.
 * No ECM or CW bytes are stored here. Not yet connected to the HTTP server. */
#ifndef MCS_USER_TRACE_H
#define MCS_USER_TRACE_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define USER_TRACE_CAPACITY 256

enum user_trace_event_type {
    USER_TRACE_RECEIVED = 1,
    USER_TRACE_DENIED,
    USER_TRACE_JOINED,
    USER_TRACE_NEW,
    USER_TRACE_SENT,
    USER_TRACE_REJECTED,
    USER_TRACE_FAILED,
    USER_TRACE_DELIVERED,
    USER_TRACE_SEND_FAILED
};

struct user_trace_target {
    uint32_t client_id;
    uint16_t caid;
    uint32_t provider;
    uint16_t sid;
};

struct user_trace_event {
    uint64_t session;
    uint64_t sequence;
    uint64_t attempt;
    time_t timestamp;
    enum user_trace_event_type type;
    uint32_t ecm_hash; /* Diagnostic fingerprint, not a unique request ID. */
    int source_type;
    int source_id;
    uint32_t elapsed_ms;
    char detail[128];
};

struct user_trace_snapshot {
    struct user_trace_target target;
    uint64_t session;
    uint64_t overwritten;
    int active;
    size_t count;
    struct user_trace_event events[USER_TRACE_CAPACITY];
};

/* A start replaces the previous session and invalidates its tokens. */
uint64_t user_trace_start(const struct user_trace_target *target);
void user_trace_stop(uint64_t session);
/* Returns zero if the request is outside the active target. */
uint64_t user_trace_begin(uint32_t client_id, uint16_t caid, uint32_t provider,
                          uint16_t sid, uint32_t hash, uint64_t *session);
/* Rejects stale sessions and attempts not created by the active session. */
int user_trace_add(uint64_t session, uint64_t attempt,
                   enum user_trace_event_type type, uint32_t hash,
                   int source_type, int source_id, uint32_t elapsed_ms,
                   const char *detail);
void user_trace_get_snapshot(struct user_trace_snapshot *out);

#endif
