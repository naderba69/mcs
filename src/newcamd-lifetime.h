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

/* Client records are packed, so state synchronization lives outside the struct.
 * Use this striped lock only for short snapshots/state changes; release it before
 * socket I/O or waiting on the separate per-client I/O guard. Protocol send paths
 * retain correctness with their session/request reservations. */
int client_state_lock(const void *client);
void client_state_unlock(const void *client);

/* Compatibility-named wrappers for Newcamd callers. */
int newcamd_client_state_lock(const void *client);
void newcamd_client_state_unlock(const void *client);
/* Separate socket-lifetime guard for protocol client records. Callers must
 * drop the client-state lock before waiting on it or doing socket I/O. */
int client_io_lock(const void *client);
void client_io_unlock(const void *client);
/* Compatibility-named wrappers for existing Newcamd callers. */
int newcamd_client_io_lock(const void *client);
void newcamd_client_io_unlock(const void *client);

/* Ready events are snapshots too: an old POLLHUP/EPOLL event must not be
 * applied to a reconnected client that reused the same descriptor number.
 * Poll snapshots compare the session's low generation word; EPOLL stores that
 * word beside the client id in its 64-bit data payload. These event snapshots
 * are consumed within one poll/epoll dispatch, while the full generation stays
 * in the client record for send-token validation. */
uint64_t newcamd_epoll_client_token(uint32_t client_id,
                                    uint64_t session_generation);
int newcamd_epoll_client_token_decode(uint64_t token, uint32_t *client_id,
                                      uint32_t *generation_low);
int newcamd_session_generation_low_matches(uint32_t expected_low,
                                           uint64_t current_generation);

/* Keep server_data/host_data alive while detached outbound connection workers
 * borrow the current config. begin() returns a unique per-attempt token used by
 * internal pipe/epoll events; reload waits for these workers before reclaiming
 * the graph. */
uint64_t server_connect_begin(void);
void server_connect_end(void);
void server_connect_wait_idle(void);

#endif /* MCS_NEWCAMD_LIFETIME_H */
