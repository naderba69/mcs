#ifndef MCS_NEWCAMD_LIFETIME_H
#define MCS_NEWCAMD_LIFETIME_H

#include <stdint.h>
#include <pthread.h>

/*
 * The config read side protects borrowed cardserver/client pointers across
 * reload. Read locks are nestable within one thread; write locks are used by
 * reread_config while it swaps and reclaims the Newcamd config graph.
 */
void newcamd_config_read_lock(void);
void newcamd_config_read_unlock(void);
/* Pair the lifetime pin with a runtime mutex without blocking on that mutex
 * while holding the pin. Call only when this thread has no read pin yet. */
void newcamd_config_read_lock_mutex(pthread_mutex_t *mutex);
void newcamd_config_read_unlock_mutex(pthread_mutex_t *mutex);
void newcamd_config_write_lock(void);
void newcamd_config_write_unlock(void);

/* Client records are packed, so their synchronization lives outside the struct.
 * The striped lock is shared by protocol client records and serializes request
 * replacement against a response send. */
int client_state_lock(const void *client);
void client_state_unlock(const void *client);

/* Compatibility-named wrappers for Newcamd callers. */
int newcamd_client_state_lock(const void *client);
void newcamd_client_state_unlock(const void *client);

/* Keep server_data/host_data alive while detached outbound connection workers
 * borrow the current config. begin() returns a unique per-attempt token used by
 * internal pipe/epoll events; reload waits for these workers before reclaiming
 * the graph. */
uint64_t server_connect_begin(void);
void server_connect_end(void);
void server_connect_wait_idle(void);

#endif /* MCS_NEWCAMD_LIFETIME_H */
