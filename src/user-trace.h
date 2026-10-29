/* Bounded, opt-in diagnostic session for a single CCcam client and channel.
 * CW bytes may be stored temporarily in RAM for an explicitly started session. */
#ifndef MCS_USER_TRACE_H
#define MCS_USER_TRACE_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define USER_TRACE_CAPACITY 256
#define USER_TRACE_MAX_LINKS 64

enum user_trace_event_type {
    USER_TRACE_RECEIVED = 1,
    USER_TRACE_DENIED,
    USER_TRACE_JOINED,
    USER_TRACE_NEW,
    USER_TRACE_SENT,
    USER_TRACE_REJECTED,
    USER_TRACE_FAILED,
    USER_TRACE_DELIVERED,
    USER_TRACE_SEND_FAILED,
    USER_TRACE_SOURCE_REPLY,
    USER_TRACE_VALIDATED,
    USER_TRACE_CACHE,
    USER_TRACE_DISPATCH_SKIPPED
};

struct user_trace_target {
    uint32_t client_id;
    uint16_t caid;
    uint32_t provider;
    uint16_t sid;
    int all_channels;
};

struct user_trace_event {
    uint64_t session;
    uint64_t sequence;
    uint64_t attempt;
    time_t timestamp;
    enum user_trace_event_type type;
    uint32_t ecm_hash; /* Diagnostic fingerprint, not a unique request ID. */
    uint16_t caid, sid;
    uint32_t provider;
    uint8_t has_cw;
    uint8_t cw[16];
    int shared; /* Event is about a shared ECM, not exclusively this client. */
    int source_type;
    int source_id;
    uint32_t elapsed_ms;
    char detail[128];
};

struct user_trace_snapshot {
    struct user_trace_target target;
    uint64_t session;
    uint64_t overwritten;
    uint64_t dropped_links; /* Full link table or start event evicted. */
    uint64_t dropped_events; /* Event could not retain its start record. */
    int active;
    size_t count;
    struct user_trace_event events[USER_TRACE_CAPACITY];
};

/* A start replaces the previous session and invalidates its tokens. */
/* Protect the CCcam client list while the HTTP selector snapshots it and
 * while configuration is reloaded. Never hold this lock during socket I/O. */
void user_trace_config_lock(void);
void user_trace_config_unlock(void);
void user_trace_stop_active(void);
uint64_t user_trace_start(const struct user_trace_target *target);
void user_trace_stop(uint64_t session);
int user_trace_matches(uint32_t client_id, uint16_t caid, uint32_t provider, uint16_t sid);
uint64_t user_trace_next_instance(void);
/* A queued response must not attach to a recycled ECM with the same hash. */
static inline int user_trace_same_instance(uint64_t sent, uint64_t current)
{
    return sent != 0 && sent == current;
}
/* Returns zero if the request is outside the active target. */
uint64_t user_trace_begin(uint32_t client_id, uint16_t caid, uint32_t provider,
                          uint16_t sid, uint32_t hash, uint64_t *session);
/* Rejects stale sessions and attempts not created by the active session. */
int user_trace_add(uint64_t session, uint64_t attempt,
                   enum user_trace_event_type type, uint32_t hash,
                   int source_type, int source_id, uint32_t elapsed_ms,
                   const char *detail);
/* instance must be a nonzero, process-unique ECM lifetime ID, not an ECM
 * pointer, hash, or timestamp. Callers must close links at the end of a
 * request. A bind never backfills events that happened before the bind. */
int user_trace_bind_shared(uint64_t session, uint64_t attempt, uint32_t hash,
                           uint64_t instance);
/* Shared ECM events never represent delivery to an individual client. Returns
 * the number of linked attempts recorded. */
size_t user_trace_shared_event(uint64_t instance, enum user_trace_event_type type,
                               int source_type, int source_id,
                               uint32_t elapsed_ms, const char *detail);
size_t user_trace_shared_cw(uint64_t instance, int source_type, int source_id,
                            uint32_t elapsed_ms, const char *detail, const uint8_t cw[16]);
void user_trace_unbind_shared(uint64_t session, uint64_t attempt);
/* Completion refers to the current attempt for this single selected client.
 * Returns 0 if there is no unambiguous live binding. */
int user_trace_client_result(uint32_t client_id, uint64_t instance, uint32_t hash,
                             enum user_trace_event_type type, int source_type,
                             int source_id, uint32_t elapsed_ms, const char *detail);
int user_trace_client_cw(uint32_t client_id, uint64_t instance, uint32_t hash,
                         int source_type, int source_id, uint32_t elapsed_ms,
                         const char *detail, const uint8_t cw[16]);
/* Channel-level cache observations: never evidence that this client's ECM
 * caused the event. Hash is a fingerprint, not a unique request ID. */
void user_trace_cache_observation(uint16_t caid, uint32_t provider, uint16_t sid,
                                  uint32_t hash, int peerid, const char *detail,
                                  const uint8_t cw[16]);
void user_trace_get_snapshot(struct user_trace_snapshot *out);

#endif
