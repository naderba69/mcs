#include "user-trace.h"
#include <pthread.h>
#include <string.h>

/* Separate mutex avoids embedding an unaligned pthread object when MultiCS
 * is compiled with -fpack-struct. All state below is protected by it. */
static pthread_mutex_t trace_lock = PTHREAD_MUTEX_INITIALIZER;
static struct user_trace_target target;
static struct user_trace_event events[USER_TRACE_CAPACITY];
static uint64_t generation, next_attempt, next_sequence, overwritten;
static size_t next_index, count;
static int active;

static uint64_t increment_nonzero(uint64_t *value)
{
    ++*value;
    if (!*value) ++*value;
    return *value;
}

static void append_locked(struct user_trace_event *event)
{
    event->session = generation;
    event->sequence = increment_nonzero(&next_sequence);
    events[next_index] = *event;
    next_index = (next_index + 1) % USER_TRACE_CAPACITY;
    if (count < USER_TRACE_CAPACITY) ++count;
    else ++overwritten;
}

uint64_t user_trace_start(const struct user_trace_target *new_target)
{
    uint64_t result;
    if (!new_target || !new_target->client_id) return 0;
    pthread_mutex_lock(&trace_lock);
    target = *new_target;
    result = increment_nonzero(&generation);
    next_attempt = next_sequence = overwritten = 0;
    next_index = count = 0;
    active = 1;
    pthread_mutex_unlock(&trace_lock);
    return result;
}

void user_trace_stop(uint64_t session)
{
    pthread_mutex_lock(&trace_lock);
    if (session && session == generation) active = 0;
    pthread_mutex_unlock(&trace_lock);
}

uint64_t user_trace_begin(uint32_t client_id, uint16_t caid, uint32_t provider,
                          uint16_t sid, uint32_t hash, uint64_t *session)
{
    struct user_trace_event event = {0};
    uint64_t attempt = 0;
    if (session) *session = 0;
    pthread_mutex_lock(&trace_lock);
    if (active && client_id == target.client_id && caid == target.caid &&
        provider == target.provider && sid == target.sid) {
        event.attempt = increment_nonzero(&next_attempt);
        event.timestamp = time(NULL);
        event.type = USER_TRACE_RECEIVED;
        event.ecm_hash = hash;
        append_locked(&event);
        attempt = event.attempt;
        if (session) *session = generation;
    }
    pthread_mutex_unlock(&trace_lock);
    return attempt;
}

int user_trace_add(uint64_t session, uint64_t attempt,
                   enum user_trace_event_type type, uint32_t hash,
                   int source_type, int source_id, uint32_t elapsed_ms,
                   const char *detail)
{
    struct user_trace_event event = {0};
    size_t i;
    int found = 0;
    if (!session || !attempt || type <= USER_TRACE_RECEIVED ||
        type > USER_TRACE_SEND_FAILED) return 0;
    pthread_mutex_lock(&trace_lock);
    if (active && session == generation) {
        /* The original RECEIVED event must still be retained. A recycled
         * slot must never make an old token point at an unrelated request. */
        for (i = 0; i < count; ++i) {
            size_t index = (next_index + USER_TRACE_CAPACITY - count + i)
                           % USER_TRACE_CAPACITY;
            if (events[index].type == USER_TRACE_RECEIVED &&
                events[index].attempt == attempt && events[index].ecm_hash == hash) {
                found = 1;
                break;
            }
        }
        if (found) {
            event.attempt = attempt;
            event.timestamp = time(NULL);
            event.type = type;
            event.ecm_hash = hash;
            event.source_type = source_type;
            event.source_id = source_id;
            event.elapsed_ms = elapsed_ms;
            if (detail) {
                strncpy(event.detail, detail, sizeof(event.detail) - 1);
                event.detail[sizeof(event.detail) - 1] = '\0';
            }
            append_locked(&event);
        }
    }
    pthread_mutex_unlock(&trace_lock);
    return found;
}

void user_trace_get_snapshot(struct user_trace_snapshot *out)
{
    size_t i;
    if (!out) return;
    pthread_mutex_lock(&trace_lock);
    out->target = target;
    out->session = generation;
    out->active = active;
    out->overwritten = overwritten;
    out->count = count;
    for (i = 0; i < count; ++i) {
        size_t index = (next_index + USER_TRACE_CAPACITY - count + i)
                       % USER_TRACE_CAPACITY;
        out->events[i] = events[index];
    }
    pthread_mutex_unlock(&trace_lock);
}
