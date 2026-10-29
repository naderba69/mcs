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
    struct user_trace_target target = {7, 0x1884, 0, 0x1136, 0};
    uint64_t first, second, session, attempt;
    pthread_t threads[4];
    size_t i;
    char detail[256];
    assert(user_trace_start(NULL) == 0);
    {
        uint8_t cw[16] = {1,2,3,6,4,5,6,15};
        uint64_t a, b, id;
        target.all_channels = 1;
        first = user_trace_start(&target);
        assert(user_trace_matches(7, 0x09bd, 0, 0x014a));
        assert(!user_trace_matches(8, 0x09bd, 0, 0x014a));
        a = user_trace_begin(7, 0x09bd, 0, 0x014a, 10, &session);
        b = user_trace_begin(7, 0x1884, 3, 0x1136, 11, &session);
        assert(a && b && a != b);
        id = user_trace_next_instance();
        assert(user_trace_bind_shared(first, a, 10, id));
        assert(user_trace_shared_cw(id, 1, 9, 20, "CW response", cw) == 1);
        assert(user_trace_client_cw(7, id, 10, 2, 9, 25, "CW sent", cw));
        user_trace_get_snapshot(&snapshot);
        assert(snapshot.count == 4);
        assert(snapshot.events[2].caid == 0x09bd && snapshot.events[2].sid == 0x014a);
        assert(snapshot.events[3].caid == 0x09bd && snapshot.events[3].has_cw);
        assert(!memcmp(snapshot.events[2].cw, cw, 16));
        assert(!memcmp(snapshot.events[3].cw, cw, 16));
        assert(snapshot.events[1].caid == 0x1884 && snapshot.events[1].provider == 3);
        target.all_channels = 0;
    }
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
    /* Two attempts may join the same live shared ECM, while identical hashes
     * on another ECM instance must not receive its events. */
    {
        uint64_t a = user_trace_begin(7, 0x1884, 0, 0x1136, 99, &session);
        uint64_t b = user_trace_begin(7, 0x1884, 0, 0x1136, 99, &session);
        uint64_t c = user_trace_begin(7, 0x1884, 0, 0x1136, 99, &session);
        assert(user_trace_bind_shared(second, a, 99, 100));
        assert(user_trace_bind_shared(second, a, 99, 100)); /* idempotent */
        assert(!user_trace_bind_shared(second, a, 99, 101));
        assert(user_trace_bind_shared(second, b, 99, 100));
        assert(user_trace_bind_shared(second, c, 99, 101));
        assert(!user_trace_shared_event(100, USER_TRACE_DELIVERED, 0, 0, 1, "bad"));
        assert(user_trace_shared_event(100, USER_TRACE_SENT, 2, 8, 10, "sent") == 2);
        user_trace_get_snapshot(&snapshot);
        assert(snapshot.count == 5);
        assert(snapshot.events[3].shared && snapshot.events[3].attempt == a);
        assert(snapshot.events[4].shared && snapshot.events[4].attempt == b);
        assert(user_trace_shared_event(100, USER_TRACE_SOURCE_REPLY, 1, 8, 12, "source reply") == 2);
        user_trace_get_snapshot(&snapshot);
        assert(snapshot.events[snapshot.count - 1].type == USER_TRACE_SOURCE_REPLY);
        assert(snapshot.events[snapshot.count - 1].attempt == b);
        assert(!user_trace_shared_event(102, USER_TRACE_SOURCE_REPLY, 1, 8, 12, "wrong instance"));
        user_trace_unbind_shared(second, a);
        assert(user_trace_shared_event(100, USER_TRACE_REJECTED, 2, 8, 15, "rejected") == 1);
        assert(!user_trace_shared_event(102, USER_TRACE_REJECTED, 2, 8, 15, "wrong instance"));
        user_trace_unbind_shared(second, b);
        assert(!user_trace_shared_event(100, USER_TRACE_FAILED, 0, 0, 1, "old"));
    }
    second = user_trace_start(&target);
    {
        uint64_t a = user_trace_begin(7, 0x1884, 0, 0x1136, 5, &session);
        uint64_t id = user_trace_next_instance();
        assert(id && user_trace_next_instance() != id);
        assert(user_trace_bind_shared(second, a, 5, id));
        assert(!user_trace_client_result(8, id, 5, USER_TRACE_FAILED, 0, 0, 1, "wrong user"));
        assert(!user_trace_client_result(7, id + 999, 5, USER_TRACE_FAILED, 0, 0, 1, "wrong instance"));
        assert(!user_trace_client_result(7, id, 6, USER_TRACE_FAILED, 0, 0, 1, "wrong hash"));
        assert(user_trace_client_result(7, id, 5, USER_TRACE_FAILED, 0, 0, 1, "failed"));
        user_trace_get_snapshot(&snapshot);
        assert(snapshot.events[snapshot.count - 1].attempt == a);
        assert(snapshot.events[snapshot.count - 1].type == USER_TRACE_FAILED);
        assert(!user_trace_client_result(7, id, 5, USER_TRACE_FAILED, 0, 0, 1, "duplicate"));
    }
    second = user_trace_start(&target);
    {
        uint64_t a = user_trace_begin(7, 0x1884, 0, 0x1136, 5, &session);
        uint64_t b = user_trace_begin(7, 0x1884, 0, 0x1136, 5, &session);
        uint64_t id = user_trace_next_instance();
        assert(user_trace_bind_shared(second, a, 5, id));
        assert(user_trace_bind_shared(second, b, 5, id));
        assert(!user_trace_client_result(7, id, 5, USER_TRACE_DELIVERED, 2, 1, 5, "ambiguous"));
        user_trace_get_snapshot(&snapshot);
        assert(snapshot.dropped_links == 2 && snapshot.dropped_events == 1);
        assert(!user_trace_shared_event(id, USER_TRACE_SENT, 2, 1, 1, "stale"));
    }
    second = user_trace_start(&target);
    {
        uint64_t attempts[USER_TRACE_MAX_LINKS + 1];
        for (i = 0; i < USER_TRACE_MAX_LINKS + 1; ++i) {
            attempts[i] = user_trace_begin(7, 0x1884, 0, 0x1136, (uint32_t)i, &session);
            assert(attempts[i]);
            if (i < USER_TRACE_MAX_LINKS)
                assert(user_trace_bind_shared(second, attempts[i], (uint32_t)i, 500 + i));
            else
                assert(!user_trace_bind_shared(second, attempts[i], (uint32_t)i, 500 + i));
        }
        user_trace_get_snapshot(&snapshot);
        assert(snapshot.dropped_links == 1);
        user_trace_unbind_shared(second, attempts[0]);
        assert(user_trace_bind_shared(second, attempts[USER_TRACE_MAX_LINKS],
                                      USER_TRACE_MAX_LINKS, 564));
    }
    second = user_trace_start(&target);
    {
        uint64_t old = user_trace_begin(7, 0x1884, 0, 0x1136, 100, &session);
        assert(user_trace_bind_shared(second, old, 100, 900));
        for (i = 0; i < USER_TRACE_CAPACITY - 1; ++i)
            assert(user_trace_begin(7, 0x1884, 0, 0x1136, (uint32_t)i, &session));
        assert(!user_trace_shared_event(900, USER_TRACE_SENT, 2, 1, 10, "evicted"));
        user_trace_get_snapshot(&snapshot);
        assert(snapshot.dropped_links == 1 && snapshot.dropped_events == 1);
        assert(snapshot.events[0].attempt == old);
    }
    second = user_trace_start(&target);
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
