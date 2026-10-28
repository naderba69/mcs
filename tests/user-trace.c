/* cc -std=c99 -Wall -Wextra -Werror -pthread -Isrc tests/user-trace.c src/user-trace.c -o /tmp/user-trace && /tmp/user-trace */
#include "user-trace.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

static struct user_trace_snapshot snapshot;
static void *worker(void *arg)
{
    unsigned i;
    (void)arg;
    for (i = 0; i < 1000; i++) {
        uint64_t session;
        uint64_t attempt = user_trace_begin(7, 0x1884, 0, 0x1136, i, &session);
        assert(attempt && session);
        /* Another writer may legitimately evict the original event. */
        user_trace_add(session, attempt, USER_TRACE_FAILED, i, 0, 0, 100, "timeout");
    }
    return NULL;
}

int main(void)
{
    struct user_trace_target target = {7, 0x1884, 0, 0x1136};
    uint64_t first, second, session, attempt;
    pthread_t threads[4];
    size_t i;
    char detail[256];
    assert(user_trace_start(NULL) == 0);
    first = user_trace_start(&target);
    assert(first);
    assert(!user_trace_begin(8, 0x1884, 0, 0x1136, 1, &session) && !session);
    assert(!user_trace_begin(7, 0x1884, 1, 0x1136, 1, &session) && !session);
    attempt = user_trace_begin(7, 0x1884, 0, 0x1136, 123, &session);
    assert(attempt && session == first);
    assert(!user_trace_add(first, attempt, USER_TRACE_SENT, 456, 1, 2, 10, "bad"));
    memset(detail, 'x', sizeof(detail)); detail[sizeof(detail)-1] = 0;
    assert(user_trace_add(first, attempt, USER_TRACE_SENT, 123, 1, 2, 10, detail));
    user_trace_get_snapshot(&snapshot);
    assert(snapshot.count == 2 && snapshot.active && snapshot.overwritten == 0);
    assert(snapshot.events[0].attempt == attempt && snapshot.events[1].attempt == attempt);
    assert(strlen(snapshot.events[1].detail) == sizeof(snapshot.events[1].detail)-1);
    second = user_trace_start(&target);
    assert(second && second != first);
    assert(!user_trace_add(first, attempt, USER_TRACE_FAILED, 123, 0, 0, 1, "stale"));
    user_trace_stop(first);
    user_trace_get_snapshot(&snapshot);
    assert(snapshot.active && snapshot.session == second && !snapshot.count);
    for (i = 0; i < 4; i++) assert(!pthread_create(&threads[i], NULL, worker, NULL));
    for (i = 0; i < 4; i++) assert(!pthread_join(threads[i], NULL));
    user_trace_get_snapshot(&snapshot);
    assert(snapshot.count == USER_TRACE_CAPACITY && snapshot.overwritten > 0);
    for (i = 1; i < snapshot.count; i++)
        assert(snapshot.events[i].sequence == snapshot.events[i-1].sequence + 1);
    user_trace_stop(second);
    assert(!user_trace_begin(7, 0x1884, 0, 0x1136, 0, &session));
    user_trace_get_snapshot(&snapshot);
    assert(!snapshot.active && snapshot.count == USER_TRACE_CAPACITY);
    puts("user trace core passed");
    return 0;
}
