#include "user-trace.h"
#include <pthread.h>
#include <string.h>

/* Separate mutex avoids embedding an unaligned pthread object when MultiCS
 * is compiled with -fpack-struct. All state below is protected by it. */
static pthread_mutex_t trace_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t trace_config_lock = PTHREAD_MUTEX_INITIALIZER;
static struct user_trace_target target;
static struct user_trace_event events[USER_TRACE_CAPACITY];
struct shared_link { uint64_t attempt, instance; uint32_t hash; };
static struct shared_link links[USER_TRACE_MAX_LINKS];
static uint64_t generation, next_attempt, next_sequence, overwritten, dropped_links, dropped_events;
static uint64_t next_instance;
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
    size_t i;
    /* Drop the association as soon as its start record is no longer visible.
     * Later events must not appear to belong to an unknown request. */
    if (count == USER_TRACE_CAPACITY && events[next_index].type == USER_TRACE_RECEIVED) {
        for (i = 0; i < USER_TRACE_MAX_LINKS; ++i) {
            if (links[i].attempt && links[i].attempt == events[next_index].attempt) {
                memset(&links[i], 0, sizeof(links[i]));
                ++dropped_links;
            }
        }
    }
    if (event->type != USER_TRACE_RECEIVED) {
        for (i = 0; i < count; ++i) {
            size_t idx = (next_index + USER_TRACE_CAPACITY - count + i) % USER_TRACE_CAPACITY;
            if (events[idx].type == USER_TRACE_RECEIVED && events[idx].attempt == event->attempt) {
                event->caid = events[idx].caid; event->provider = events[idx].provider;
                event->sid = events[idx].sid; break;
            }
        }
    }
    event->session = generation;
    event->sequence = increment_nonzero(&next_sequence);
    events[next_index] = *event;
    next_index = (next_index + 1) % USER_TRACE_CAPACITY;
    if (count < USER_TRACE_CAPACITY) ++count;
    else ++overwritten;
}

void user_trace_config_lock(void) { pthread_mutex_lock(&trace_config_lock); }
void user_trace_config_unlock(void) { pthread_mutex_unlock(&trace_config_lock); }
void user_trace_stop_active(void)
{
    pthread_mutex_lock(&trace_lock);
    active = 0;
    pthread_mutex_unlock(&trace_lock);
}

uint64_t user_trace_start(const struct user_trace_target *new_target)
{
    uint64_t result;
    if (!new_target || !new_target->client_id) return 0;
    pthread_mutex_lock(&trace_lock);
    target = *new_target;
    result = increment_nonzero(&generation);
    next_attempt = next_sequence = overwritten = dropped_links = dropped_events = 0;
    memset(links, 0, sizeof(links));
    memset(events, 0, sizeof(events));
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

int user_trace_matches(uint32_t client_id, uint16_t caid, uint32_t provider, uint16_t sid)
{
    int matched;
    pthread_mutex_lock(&trace_lock);
    matched = active && client_id == target.client_id &&
              (target.all_channels || (caid == target.caid && provider == target.provider && sid == target.sid));
    pthread_mutex_unlock(&trace_lock);
    return matched;
}

uint64_t user_trace_next_instance(void)
{
    uint64_t result;
    pthread_mutex_lock(&trace_lock);
    /* Never reuse a lifetime ID after wrap; fail closed. */
    result = next_instance == UINT64_MAX ? 0 : ++next_instance;
    pthread_mutex_unlock(&trace_lock);
    return result;
}

uint64_t user_trace_begin(uint32_t client_id, uint16_t caid, uint32_t provider,
                          uint16_t sid, uint32_t hash, uint64_t *session)
{
    struct user_trace_event event = {0};
    uint64_t attempt = 0;
    if (session) *session = 0;
    pthread_mutex_lock(&trace_lock);
    if (active && client_id == target.client_id &&
        (target.all_channels || (caid == target.caid && provider == target.provider && sid == target.sid))) {
        event.attempt = increment_nonzero(&next_attempt);
        event.timestamp = time(NULL);
        event.type = USER_TRACE_RECEIVED;
        event.ecm_hash = hash;
        event.caid = caid; event.provider = provider; event.sid = sid;
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
        if (found && count == USER_TRACE_CAPACITY &&
            events[next_index].type == USER_TRACE_RECEIVED &&
            events[next_index].attempt == attempt) {
            ++dropped_events;
            found = 0;
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

int user_trace_bind_shared(uint64_t session, uint64_t attempt, uint32_t hash,
                           uint64_t instance)
{
    size_t i, free_slot = USER_TRACE_MAX_LINKS;
    int found = 0, result = 0;
    if (!session || !attempt || !instance) return 0;
    pthread_mutex_lock(&trace_lock);
    if (!active || session != generation) goto done;
    for (i = 0; i < count; ++i) {
        size_t index = (next_index + USER_TRACE_CAPACITY - count + i) % USER_TRACE_CAPACITY;
        if (events[index].type == USER_TRACE_RECEIVED &&
            events[index].attempt == attempt && events[index].ecm_hash == hash) {
            found = 1;
            break;
        }
    }
    if (!found) goto done;
    for (i = 0; i < USER_TRACE_MAX_LINKS; ++i) {
        if (links[i].attempt == attempt) {
            result = links[i].instance == instance && links[i].hash == hash;
            goto done;
        }
        if (!links[i].attempt && free_slot == USER_TRACE_MAX_LINKS) free_slot = i;
    }
    if (free_slot == USER_TRACE_MAX_LINKS) {
        ++dropped_links;
        goto done;
    }
    links[free_slot].attempt = attempt;
    links[free_slot].instance = instance;
    links[free_slot].hash = hash;
    result = 1;
done:
    pthread_mutex_unlock(&trace_lock);
    return result;
}

static size_t shared_event(uint64_t instance, enum user_trace_event_type type,
                           int source_type, int source_id, uint32_t elapsed_ms,
                           const char *detail, const uint8_t *cw)
{
    size_t i, recorded = 0;
    if (!instance || (type != USER_TRACE_SENT && type != USER_TRACE_REJECTED &&
                      type != USER_TRACE_FAILED && type != USER_TRACE_SOURCE_REPLY &&
                      type != USER_TRACE_VALIDATED && type != USER_TRACE_CACHE)) return 0;
    pthread_mutex_lock(&trace_lock);
    if (active) {
        for (i = 0; i < USER_TRACE_MAX_LINKS; ++i) {
            struct user_trace_event event = {0};
            if (!links[i].attempt || links[i].instance != instance) continue;
            if (count == USER_TRACE_CAPACITY &&
                events[next_index].type == USER_TRACE_RECEIVED &&
                events[next_index].attempt == links[i].attempt) {
                memset(&links[i], 0, sizeof(links[i]));
                ++dropped_links;
                ++dropped_events;
                continue;
            }
            event.attempt = links[i].attempt;
            event.ecm_hash = links[i].hash;
            event.shared = 1;
            event.timestamp = time(NULL);
            event.type = type;
            event.source_type = source_type;
            event.source_id = source_id;
            event.elapsed_ms = elapsed_ms;
            if (cw) { event.has_cw = 1; memcpy(event.cw, cw, sizeof(event.cw)); }
            if (detail) {
                strncpy(event.detail, detail, sizeof(event.detail) - 1);
                event.detail[sizeof(event.detail) - 1] = '\0';
            }
            append_locked(&event);
            ++recorded;
        }
    }
    pthread_mutex_unlock(&trace_lock);
    return recorded;
}

size_t user_trace_shared_event(uint64_t instance, enum user_trace_event_type type,
                               int source_type, int source_id, uint32_t elapsed_ms, const char *detail)
{
    return shared_event(instance, type, source_type, source_id, elapsed_ms, detail, NULL);
}
size_t user_trace_shared_cw(uint64_t instance, int source_type, int source_id,
                            uint32_t elapsed_ms, const char *detail, const uint8_t cw[16])
{
    if (!cw) return 0;
    return shared_event(instance, USER_TRACE_SOURCE_REPLY, source_type, source_id, elapsed_ms, detail, cw);
}

void user_trace_unbind_shared(uint64_t session, uint64_t attempt)
{
    size_t i;
    if (!session || !attempt) return;
    pthread_mutex_lock(&trace_lock);
    if (session == generation) {
        for (i = 0; i < USER_TRACE_MAX_LINKS; ++i) {
            if (links[i].attempt == attempt) {
                memset(&links[i], 0, sizeof(links[i]));
                break;
            }
        }
    }
    pthread_mutex_unlock(&trace_lock);
}

static int client_result(uint32_t client_id, uint64_t instance, uint32_t hash,
                         enum user_trace_event_type type, int source_type,
                         int source_id, uint32_t elapsed_ms, const char *detail, const uint8_t *cw)
{
    size_t i, found = USER_TRACE_MAX_LINKS, matches = 0;
    struct user_trace_event event = {0};
    int result = 0;
    if (!instance || (type != USER_TRACE_FAILED && type != USER_TRACE_DELIVERED &&
                      type != USER_TRACE_SEND_FAILED)) return 0;
    pthread_mutex_lock(&trace_lock);
    if (!active || client_id != target.client_id) goto done;
    for (i = 0; i < USER_TRACE_MAX_LINKS; ++i) {
        if (links[i].attempt && links[i].instance == instance && links[i].hash == hash) { found = i; ++matches; }
    }
    if (matches > 1) {
        /* A duplicate live association is ambiguous: never guess which
         * client attempt received this completion. Make the loss visible. */
        ++dropped_events;
        for (i = 0; i < USER_TRACE_MAX_LINKS; ++i) {
            if (links[i].attempt && links[i].instance == instance && links[i].hash == hash) {
                memset(&links[i], 0, sizeof(links[i]));
                ++dropped_links;
            }
        }
        goto done;
    }
    if (!matches) goto done;
    if (count == USER_TRACE_CAPACITY && events[next_index].type == USER_TRACE_RECEIVED &&
        events[next_index].attempt == links[found].attempt) {
        ++dropped_links;
        ++dropped_events;
        memset(&links[found], 0, sizeof(links[found]));
        goto done;
    }
    event.attempt = links[found].attempt;
    event.ecm_hash = links[found].hash;
    event.timestamp = time(NULL);
    event.type = type;
    event.source_type = source_type;
    event.source_id = source_id;
    event.elapsed_ms = elapsed_ms;
    if (cw) { event.has_cw = 1; memcpy(event.cw, cw, sizeof(event.cw)); }
    if (detail) {
        strncpy(event.detail, detail, sizeof(event.detail) - 1);
        event.detail[sizeof(event.detail) - 1] = '\0';
    }
    memset(&links[found], 0, sizeof(links[found]));
    append_locked(&event);
    result = 1;
done:
    pthread_mutex_unlock(&trace_lock);
    return result;
}

int user_trace_client_result(uint32_t client_id, uint64_t instance, uint32_t hash,
                             enum user_trace_event_type type, int source_type,
                             int source_id, uint32_t elapsed_ms, const char *detail)
{
    return client_result(client_id, instance, hash, type, source_type, source_id, elapsed_ms, detail, NULL);
}
int user_trace_client_cw(uint32_t client_id, uint64_t instance, uint32_t hash,
                         int source_type, int source_id, uint32_t elapsed_ms,
                         const char *detail, const uint8_t cw[16])
{
    if (!cw) return 0;
    return client_result(client_id, instance, hash, USER_TRACE_DELIVERED,
                         source_type, source_id, elapsed_ms, detail, cw);
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
    out->dropped_links = dropped_links;
    out->dropped_events = dropped_events;
    out->count = count;
    for (i = 0; i < count; ++i) {
        size_t index = (next_index + USER_TRACE_CAPACITY - count + i)
                       % USER_TRACE_CAPACITY;
        out->events[i] = events[index];
    }
    pthread_mutex_unlock(&trace_lock);
}
