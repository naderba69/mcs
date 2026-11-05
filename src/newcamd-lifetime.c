#include "newcamd-lifetime.h"

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>

/* Writer-preferred, reentrant-reader gate: reload cannot be starved by the
 * network threads repeatedly starting their next poll cycle. */
static pthread_mutex_t newcamd_config_gate = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t newcamd_config_changed = PTHREAD_COND_INITIALIZER;
static unsigned int newcamd_active_readers;
static unsigned int newcamd_waiting_writers;
static int newcamd_active_writer;
static __thread unsigned int newcamd_config_read_depth;
static __thread unsigned int newcamd_config_write_depth;

void newcamd_config_read_lock(void)
{
    if (newcamd_config_read_depth) {
        newcamd_config_read_depth++;
        return;
    }
    if (newcamd_config_write_depth) abort();
    if (pthread_mutex_lock(&newcamd_config_gate) != 0) abort();
    while (newcamd_active_writer || newcamd_waiting_writers) {
        if (pthread_cond_wait(&newcamd_config_changed,
                              &newcamd_config_gate) != 0)
            abort();
    }
    newcamd_active_readers++;
    newcamd_config_read_depth = 1;
    if (pthread_mutex_unlock(&newcamd_config_gate) != 0) abort();
}

void newcamd_config_read_unlock(void)
{
    if (!newcamd_config_read_depth) abort();
    if (--newcamd_config_read_depth) return;
    if (pthread_mutex_lock(&newcamd_config_gate) != 0) abort();
    if (!newcamd_active_readers) abort();
    newcamd_active_readers--;
    if (!newcamd_active_readers && newcamd_waiting_writers)
        pthread_cond_signal(&newcamd_config_changed);
    else if (!newcamd_active_readers)
        pthread_cond_broadcast(&newcamd_config_changed);
    if (pthread_mutex_unlock(&newcamd_config_gate) != 0) abort();
}

/* Acquire the config lifetime pin and a related runtime mutex without waiting
 * on the mutex while the pin is held. This avoids a gate->mutex waiter blocking
 * a runtime thread that owns the mutex and is itself waiting for the read gate.
 * The caller must not already hold a config read pin or the mutex. */
void newcamd_config_read_lock_mutex(pthread_mutex_t *mutex)
{
    int rc;
    if (!mutex || newcamd_config_read_depth) abort();
    for (;;) {
        newcamd_config_read_lock();
        rc = pthread_mutex_trylock(mutex);
        if (rc == 0) return;
        newcamd_config_read_unlock();
        if (rc != EBUSY) abort();
        if (pthread_mutex_lock(mutex) != 0) abort();
        if (pthread_mutex_unlock(mutex) != 0) abort();
    }
}

void newcamd_config_read_unlock_mutex(pthread_mutex_t *mutex)
{
    if (!mutex || !newcamd_config_read_depth) abort();
    if (pthread_mutex_unlock(mutex) != 0) abort();
    newcamd_config_read_unlock();
}

void newcamd_config_write_lock(void)
{
    if (newcamd_config_read_depth || newcamd_config_write_depth) abort();
    if (pthread_mutex_lock(&newcamd_config_gate) != 0) abort();
    newcamd_waiting_writers++;
    while (newcamd_active_writer || newcamd_active_readers) {
        if (pthread_cond_wait(&newcamd_config_changed,
                              &newcamd_config_gate) != 0)
            abort();
    }
    newcamd_waiting_writers--;
    newcamd_active_writer = 1;
    newcamd_config_write_depth = 1;
    if (pthread_mutex_unlock(&newcamd_config_gate) != 0) abort();
}

void newcamd_config_write_unlock(void)
{
    if (newcamd_config_write_depth != 1) abort();
    newcamd_config_write_depth = 0;
    if (pthread_mutex_lock(&newcamd_config_gate) != 0) abort();
    if (!newcamd_active_writer) abort();
    newcamd_active_writer = 0;
    pthread_cond_broadcast(&newcamd_config_changed);
    if (pthread_mutex_unlock(&newcamd_config_gate) != 0) abort();
}

#define CLIENT_STATE_LOCK_COUNT 64
static pthread_mutex_t client_state_locks[CLIENT_STATE_LOCK_COUNT];
static pthread_once_t client_state_locks_once = PTHREAD_ONCE_INIT;
static int client_state_locks_error;

static void client_state_locks_init(void)
{
    unsigned int i;
    for (i = 0; i < CLIENT_STATE_LOCK_COUNT; i++) {
        int rc = pthread_mutex_init(&client_state_locks[i], NULL);
        if (rc != 0) {
            while (i > 0) pthread_mutex_destroy(&client_state_locks[--i]);
            client_state_locks_error = rc;
            return;
        }
    }
}

static pthread_mutex_t *client_state_lock_for(const void *client)
{
    uintptr_t key;
    if (!client || pthread_once(&client_state_locks_once,
                                client_state_locks_init) != 0 ||
        client_state_locks_error)
        return NULL;
    key = (uintptr_t)client;
    return &client_state_locks[(key >> 4) % CLIENT_STATE_LOCK_COUNT];
}

int client_state_lock(const void *client)
{
    pthread_mutex_t *lock = client_state_lock_for(client);
    if (!lock) return EINVAL;
    return pthread_mutex_lock(lock);
}

void client_state_unlock(const void *client)
{
    pthread_mutex_t *lock = client_state_lock_for(client);
    if (!lock || pthread_mutex_unlock(lock) != 0) abort();
}

int newcamd_client_state_lock(const void *client)
{
    return client_state_lock(client);
}

void newcamd_client_state_unlock(const void *client)
{
    client_state_unlock(client);
}

static pthread_mutex_t server_connect_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t server_connect_changed = PTHREAD_COND_INITIALIZER;
static unsigned int server_connect_active;
static uint64_t server_connect_generation;

uint64_t server_connect_begin(void)
{
    uint64_t generation;
    if (pthread_mutex_lock(&server_connect_lock) != 0) abort();
    if (server_connect_active == UINT_MAX) abort();
    server_connect_active++;
    generation = ++server_connect_generation;
    if (!generation) generation = ++server_connect_generation;
    if (pthread_mutex_unlock(&server_connect_lock) != 0) abort();
    return generation;
}

void server_connect_end(void)
{
    if (pthread_mutex_lock(&server_connect_lock) != 0) abort();
    if (!server_connect_active) abort();
    server_connect_active--;
    if (!server_connect_active &&
        pthread_cond_broadcast(&server_connect_changed) != 0) abort();
    if (pthread_mutex_unlock(&server_connect_lock) != 0) abort();
}

void server_connect_wait_idle(void)
{
    if (pthread_mutex_lock(&server_connect_lock) != 0) abort();
    while (server_connect_active) {
        if (pthread_cond_wait(&server_connect_changed, &server_connect_lock) != 0)
            abort();
    }
    if (pthread_mutex_unlock(&server_connect_lock) != 0) abort();
}
