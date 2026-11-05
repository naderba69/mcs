#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include "../src/common.h"
#include "../src/ecmdata.h"
#include "../src/newcamd-lifetime.h"
#include "../src/newcamd-request.h"
#include "../src/newcamd-keepalive.h"

static pthread_mutex_t test_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t test_changed = PTHREAD_COND_INITIALIZER;
static int reader_entered;
static int release_reader;
static int writer_entered;
static pthread_mutex_t runtime_state_lock = PTHREAD_MUTEX_INITIALIZER;
static int runtime_state_locked;
static int release_runtime_state_lock;
static int runtime_reader_started;
static int runtime_reader_acquired;
static unsigned int protected_count;
static int shared_client;
static int delivery_active;
static int allow_delivery_finish;
static int close_attempting;
static int delivery_busy;
static int delivery_status;
static int client_closed;
static int event_order;
static int delivery_order;
static int close_order;
static int failures;
static int handoff_ready;
static int handoff_release;
static int handoff_writer_entered;
static int handoff_dispatch_allowed;
static uint64_t simulated_config_generation = 1;
static uint64_t handoff_captured_generation;
static int connect_wait_started;
static int connect_wait_completed;
static pthread_mutex_t simulated_ecm_lock = PTHREAD_MUTEX_INITIALIZER;

#define CHECK(condition, description) do { \
    if (condition) printf("PASS: %s\n", description); \
    else { printf("FAIL: %s\n", description); failures++; } \
} while (0)

static void *reader_thread(void *unused)
{
    (void)unused;
    newcamd_config_read_lock();
    pthread_mutex_lock(&test_lock);
    reader_entered = 1;
    pthread_cond_broadcast(&test_changed);
    while (!release_reader) pthread_cond_wait(&test_changed, &test_lock);
    pthread_mutex_unlock(&test_lock);
    newcamd_config_read_unlock();
    return NULL;
}

static void *writer_thread(void *unused)
{
    (void)unused;
    newcamd_config_write_lock();
    pthread_mutex_lock(&test_lock);
    writer_entered = 1;
    pthread_cond_broadcast(&test_changed);
    pthread_mutex_unlock(&test_lock);
    newcamd_config_write_unlock();
    return NULL;
}

static void *runtime_state_holder_thread(void *unused)
{
    (void)unused;
    pthread_mutex_lock(&runtime_state_lock);
    pthread_mutex_lock(&test_lock);
    runtime_state_locked = 1;
    pthread_cond_broadcast(&test_changed);
    while (!release_runtime_state_lock)
        pthread_cond_wait(&test_changed, &test_lock);
    pthread_mutex_unlock(&test_lock);
    pthread_mutex_unlock(&runtime_state_lock);
    return NULL;
}

static void *runtime_state_reader_thread(void *unused)
{
    (void)unused;
    pthread_mutex_lock(&test_lock);
    runtime_reader_started = 1;
    pthread_cond_broadcast(&test_changed);
    pthread_mutex_unlock(&test_lock);
    newcamd_config_read_lock_mutex(&runtime_state_lock);
    pthread_mutex_lock(&test_lock);
    runtime_reader_acquired = 1;
    pthread_cond_broadcast(&test_changed);
    pthread_mutex_unlock(&test_lock);
    newcamd_config_read_unlock_mutex(&runtime_state_lock);
    return NULL;
}

static void *connect_wait_thread(void *unused)
{
    (void)unused;
    pthread_mutex_lock(&test_lock);
    connect_wait_started = 1;
    pthread_cond_broadcast(&test_changed);
    pthread_mutex_unlock(&test_lock);
    server_connect_wait_idle();
    pthread_mutex_lock(&test_lock);
    connect_wait_completed = 1;
    pthread_cond_broadcast(&test_changed);
    pthread_mutex_unlock(&test_lock);
    return NULL;
}

static void *handoff_reader_thread(void *unused)
{
    (void)unused;
    pthread_mutex_lock(&simulated_ecm_lock);
    handoff_captured_generation = simulated_config_generation;
    pthread_mutex_lock(&test_lock);
    handoff_ready = 1;
    pthread_cond_broadcast(&test_changed);
    while (!handoff_release) pthread_cond_wait(&test_changed, &test_lock);
    pthread_mutex_unlock(&test_lock);

    /* Match production lock order: release lockecm before taking config-read. */
    pthread_mutex_unlock(&simulated_ecm_lock);
    newcamd_config_read_lock();
    pthread_mutex_lock(&simulated_ecm_lock);
    handoff_dispatch_allowed =
        (simulated_config_generation == handoff_captured_generation);
    pthread_mutex_unlock(&simulated_ecm_lock);
    newcamd_config_read_unlock();
    return NULL;
}

static void *handoff_writer_thread(void *unused)
{
    (void)unused;
    pthread_mutex_lock(&test_lock);
    pthread_cond_broadcast(&test_changed);
    pthread_mutex_unlock(&test_lock);

    /* Reload order is config-write -> lockecm. */
    newcamd_config_write_lock();
    pthread_mutex_lock(&test_lock);
    handoff_writer_entered = 1; /* owns config-write; next waits for lockecm */
    pthread_cond_broadcast(&test_changed);
    pthread_mutex_unlock(&test_lock);
    pthread_mutex_lock(&simulated_ecm_lock);
    simulated_config_generation++;
    pthread_mutex_unlock(&simulated_ecm_lock);
    newcamd_config_write_unlock();
    return NULL;
}

static void *client_counter_thread(void *unused)
{
    unsigned int i;
    (void)unused;
    for (i = 0; i < 50000; i++) {
        if (client_state_lock(&shared_client) != 0) return (void *)1;
        protected_count++;
        client_state_unlock(&shared_client);
    }
    return NULL;
}

static void *newcamd_client_counter_thread(void *unused)
{
    unsigned int i;
    (void)unused;
    for (i = 0; i < 50000; i++) {
        if (newcamd_client_state_lock(&shared_client) != 0) return (void *)1;
        protected_count++;
        newcamd_client_state_unlock(&shared_client);
    }
    return NULL;
}

static void *delivery_thread(void *unused)
{
    (void)unused;
    if (newcamd_client_state_lock(&shared_client) != 0) return (void *)1;
    pthread_mutex_lock(&test_lock);
    delivery_active = 1;
    pthread_cond_broadcast(&test_changed);
    while (!allow_delivery_finish) pthread_cond_wait(&test_changed, &test_lock);
    pthread_mutex_unlock(&test_lock);
    delivery_status = 1; /* STAT_DCW_SENT */
    delivery_busy = 0;
    delivery_order = ++event_order;
    newcamd_client_state_unlock(&shared_client);
    return NULL;
}

static void *close_thread(void *unused)
{
    (void)unused;
    pthread_mutex_lock(&test_lock);
    close_attempting = 1;
    pthread_cond_broadcast(&test_changed);
    pthread_mutex_unlock(&test_lock);
    if (newcamd_client_state_lock(&shared_client) != 0) return (void *)1;
    client_closed = 1;
    close_order = ++event_order;
    newcamd_client_state_unlock(&shared_client);
    return NULL;
}

int main(void)
{
    pthread_t reader, writer, handoff_reader, handoff_writer, client_a, client_b;
    pthread_t connect_waiter, runtime_holder, runtime_reader;
    void *client_result_a = NULL, *client_result_b = NULL;
    int writer_blocked, close_still_waiting, connect_wait_blocked;
    int runtime_gate_progress;
    uint64_t connect_token_a, connect_token_b;

    CHECK(!newcamd_keepalive_reply_timed_out(10999u, 1000u),
          "Newcamd keepalive is not timed out before the ten-second boundary");
    CHECK(!newcamd_keepalive_reply_timed_out(11000u, 1000u) &&
          newcamd_keepalive_reply_timed_out(11001u, 1000u),
          "Newcamd keepalive timeout uses the sent/activity timestamp, not status");
    CHECK(!newcamd_keepalive_reply_timed_out(4999u, UINT32_MAX-5000u) &&
          newcamd_keepalive_reply_timed_out(5000u, UINT32_MAX-5000u),
          "Newcamd keepalive timeout calculation remains correct across tick wrap");

    newcamd_config_read_lock();
    newcamd_config_read_lock();
    newcamd_config_read_unlock();
    newcamd_config_read_unlock();
    CHECK(1, "nested read-side config pins can be released independently");

    runtime_state_locked = release_runtime_state_lock = 0;
    runtime_reader_started = runtime_reader_acquired = 0;
    writer_entered = 0;
    pthread_create(&runtime_holder, NULL, runtime_state_holder_thread, NULL);
    pthread_mutex_lock(&test_lock);
    while (!runtime_state_locked) pthread_cond_wait(&test_changed, &test_lock);
    pthread_mutex_unlock(&test_lock);
    pthread_create(&runtime_reader, NULL, runtime_state_reader_thread, NULL);
    pthread_mutex_lock(&test_lock);
    while (!runtime_reader_started) pthread_cond_wait(&test_changed, &test_lock);
    pthread_mutex_unlock(&test_lock);
    usleep(20000);
    pthread_create(&writer, NULL, writer_thread, NULL);
    usleep(20000);
    pthread_mutex_lock(&test_lock);
    runtime_gate_progress = writer_entered && !runtime_reader_acquired;
    release_runtime_state_lock = 1;
    pthread_cond_broadcast(&test_changed);
    pthread_mutex_unlock(&test_lock);
    pthread_join(runtime_holder, NULL);
    pthread_join(runtime_reader, NULL);
    pthread_join(writer, NULL);
    CHECK(runtime_gate_progress && runtime_reader_acquired,
          "config readers wait for a busy runtime mutex without pinning the reload gate");
    writer_entered = 0;

    connect_wait_started = connect_wait_completed = 0;
    connect_token_a = server_connect_begin();
    connect_token_b = server_connect_begin();
    pthread_create(&connect_waiter, NULL, connect_wait_thread, NULL);
    pthread_mutex_lock(&test_lock);
    while (!connect_wait_started) pthread_cond_wait(&test_changed, &test_lock);
    pthread_mutex_unlock(&test_lock);
    usleep(20000);
    server_connect_end();
    pthread_mutex_lock(&test_lock);
    connect_wait_blocked = !connect_wait_completed;
    pthread_mutex_unlock(&test_lock);
    CHECK(connect_token_a && connect_token_b && connect_token_a != connect_token_b &&
          connect_wait_blocked,
          "connection attempts get distinct tokens and reload waits for every worker");
    server_connect_end();
    pthread_join(connect_waiter, NULL);
    CHECK(connect_wait_completed,
          "reload's worker drain completes when the final connection worker exits");

    pthread_create(&reader, NULL, reader_thread, NULL);
    pthread_mutex_lock(&test_lock);
    while (!reader_entered) pthread_cond_wait(&test_changed, &test_lock);
    pthread_mutex_unlock(&test_lock);
    pthread_create(&writer, NULL, writer_thread, NULL);
    usleep(20000);
    pthread_mutex_lock(&test_lock);
    writer_blocked = !writer_entered;
    CHECK(writer_blocked,
          "reload writer waits while a reader still borrows config pointers");
    release_reader = 1;
    pthread_cond_broadcast(&test_changed);
    pthread_mutex_unlock(&test_lock);
    pthread_join(reader, NULL);
    pthread_join(writer, NULL);
    CHECK(writer_entered, "reload writer proceeds after the reader exits");

    handoff_ready = handoff_release = 0;
    handoff_writer_entered = 0;
    handoff_dispatch_allowed = 1;
    pthread_create(&handoff_reader, NULL, handoff_reader_thread, NULL);
    pthread_mutex_lock(&test_lock);
    while (!handoff_ready) pthread_cond_wait(&test_changed, &test_lock);
    pthread_mutex_unlock(&test_lock);
    pthread_create(&handoff_writer, NULL, handoff_writer_thread, NULL);
    pthread_mutex_lock(&test_lock);
    while (!handoff_writer_entered) pthread_cond_wait(&test_changed, &test_lock);
    handoff_release = 1;
    pthread_cond_broadcast(&test_changed);
    pthread_mutex_unlock(&test_lock);
    pthread_join(handoff_reader, NULL);
    pthread_join(handoff_writer, NULL);
    CHECK(handoff_writer_entered && !handoff_dispatch_allowed &&
          simulated_config_generation == handoff_captured_generation + 1,
          "reload can finish in the lock handoff and stale-profile dispatch is skipped");

    pthread_create(&client_a, NULL, client_counter_thread, NULL);
    pthread_create(&client_b, NULL, newcamd_client_counter_thread, NULL);
    pthread_join(client_a, &client_result_a);
    pthread_join(client_b, &client_result_b);
    CHECK(!client_result_a && !client_result_b && protected_count == 100000,
          "generic CCcam and Newcamd state-lock callers serialize the same client record");

    {
        int request_a, request_b;
        CHECK(newcamd_delivery_identity_matches(&request_a, 41, &request_a, 41),
              "reply identity accepts the exact in-flight request generation");
        CHECK(!newcamd_delivery_identity_matches(&request_a, 42, &request_a, 41),
              "reply for a previous request is rejected after the same storage slot is reused");
        CHECK(!newcamd_delivery_identity_matches(&request_b, 41, &request_a, 41),
              "reply identity rejects a different request pointer even with a matching generation");
    }

    {
        ECM_DATA request_slot;
        memset(&request_slot, 0, sizeof(request_slot));
        __atomic_store_n(&request_slot.lifetime_id, 41, __ATOMIC_RELEASE);
        CHECK(ecm_request_lifetime_matches(&request_slot, &request_slot, 41),
              "server reply context accepts the exact ECM pool lifetime captured at send");
        __atomic_store_n(&request_slot.lifetime_id, 42, __ATOMIC_RELEASE);
        CHECK(!ecm_request_lifetime_matches(&request_slot, &request_slot, 41),
              "server reply is rejected when the same ECM storage has been reused");
        CHECK(!ecm_request_lifetime_matches(&request_slot, &request_slot, 0),
              "server reply rejects an unset send-time lifetime token");
    }

    {
        pthread_t sender, closer;
        void *sender_result = NULL, *closer_result = NULL;
        delivery_active = allow_delivery_finish = close_attempting = 0;
        delivery_busy = 1;
        delivery_status = client_closed = event_order = delivery_order = close_order = 0;
        pthread_create(&sender, NULL, delivery_thread, NULL);
        pthread_mutex_lock(&test_lock);
        while (!delivery_active) pthread_cond_wait(&test_changed, &test_lock);
        pthread_mutex_unlock(&test_lock);
        pthread_create(&closer, NULL, close_thread, NULL);
        pthread_mutex_lock(&test_lock);
        while (!close_attempting) pthread_cond_wait(&test_changed, &test_lock);
        pthread_mutex_unlock(&test_lock);
        usleep(20000);
        pthread_mutex_lock(&test_lock);
        close_still_waiting = (close_order == 0);
        pthread_mutex_unlock(&test_lock);
        CHECK(close_still_waiting,
              "concurrent close waits until the in-progress reply finishes its state transition");
        pthread_mutex_lock(&test_lock);
        allow_delivery_finish = 1;
        pthread_cond_broadcast(&test_changed);
        pthread_mutex_unlock(&test_lock);
        pthread_join(sender, &sender_result);
        pthread_join(closer, &closer_result);
        CHECK(!sender_result && !closer_result && delivery_order == 1 &&
              close_order == 2 && delivery_status == 1 && !delivery_busy &&
              client_closed,
              "reply publishes SENT before busy clears and close proceeds afterward");
    }

    printf("%d Newcamd lifetime checks failed\n", failures);
    return failures ? 1 : 0;
}
