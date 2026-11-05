#ifndef MCS_PIPE_H
#define MCS_PIPE_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "cache-pipe-queue.h"

///////////////////////////////////////////////////////////////////////////////
//
///////////////////////////////////////////////////////////////////////////////

extern int frcc_pipe[2]; // newcamd server recv pipe

extern int dcwpipe[2];

// PIPE COMMANDS
#define PIPE_LOCK                1
#define PIPE_WAKEUP              2
#define PIPE_SENDCW              3

#define PIPE_SRV_CONNECTED       4
#define PIPE_SRV_AVAILABLE       5

#define PIPE_CLI_CONNECTED       6
#define PIPE_SRV_CSCACHE_REPLY   7

/* Connected-server wakeups carry stable identity, never a server_data pointer.
 * The consumer checks the per-attempt generation under a config lifetime pin. */
#define PIPE_SERVER_CONNECTED_LEN (1u + sizeof(uint32_t) + sizeof(uint64_t))

static inline int pipe_encode_server_connected(uint8_t *buf, size_t capacity,
                                               uint32_t server_id,
                                               uint64_t connection_generation)
{
	if (!buf || capacity < PIPE_SERVER_CONNECTED_LEN || !server_id ||
	    !connection_generation) return 0;
	buf[0] = PIPE_SRV_CONNECTED;
	memcpy(buf + 1, &server_id, sizeof(server_id));
	memcpy(buf + 1 + sizeof(server_id), &connection_generation,
	       sizeof(connection_generation));
	return (int)PIPE_SERVER_CONNECTED_LEN;
}

static inline int pipe_decode_server_connected(const uint8_t *buf, size_t length,
                                               uint32_t *server_id,
                                               uint64_t *connection_generation)
{
	if (!buf || !server_id || !connection_generation ||
	    length != PIPE_SERVER_CONNECTED_LEN || buf[0] != PIPE_SRV_CONNECTED)
		return 0;
	memcpy(server_id, buf + 1, sizeof(*server_id));
	memcpy(connection_generation, buf + 1 + sizeof(*server_id),
	       sizeof(*connection_generation));
	return *server_id != 0 && *connection_generation != 0;
}

/* A no-thread ECM commit defers a cached Newcamd reply to the ECM event loop,
 * which sends it under locksrv after revalidating the server's request lifetime. */
#define PIPE_SERVER_CSCACHE_REPLY_LEN \
	(1u + sizeof(uint32_t) + sizeof(uint64_t) + sizeof(uint16_t) + \
	 sizeof(uint32_t) + sizeof(uint16_t) + sizeof(uint8_t) + 16u)

static inline int pipe_encode_server_cscache_reply(
	uint8_t *buf, size_t capacity, uint32_t server_id,
	uint64_t request_lifetime_id, uint16_t caid, uint32_t provid,
	uint16_t sid, uint8_t ecm_tag, const uint8_t cw[16])
{
	size_t offset = 1;
	if (!buf || !cw || capacity < PIPE_SERVER_CSCACHE_REPLY_LEN ||
	    !server_id || !request_lifetime_id) return 0;
	buf[0] = PIPE_SRV_CSCACHE_REPLY;
	memcpy(buf + offset, &server_id, sizeof(server_id));
	offset += sizeof(server_id);
	memcpy(buf + offset, &request_lifetime_id, sizeof(request_lifetime_id));
	offset += sizeof(request_lifetime_id);
	memcpy(buf + offset, &caid, sizeof(caid));
	offset += sizeof(caid);
	memcpy(buf + offset, &provid, sizeof(provid));
	offset += sizeof(provid);
	memcpy(buf + offset, &sid, sizeof(sid));
	offset += sizeof(sid);
	buf[offset++] = ecm_tag;
	memcpy(buf + offset, cw, 16);
	return (int)PIPE_SERVER_CSCACHE_REPLY_LEN;
}

static inline int pipe_decode_server_cscache_reply(
	const uint8_t *buf, size_t length, uint32_t *server_id,
	uint64_t *request_lifetime_id, uint16_t *caid, uint32_t *provid,
	uint16_t *sid, uint8_t *ecm_tag, uint8_t cw[16])
{
	size_t offset = 1;
	if (!buf || !server_id || !request_lifetime_id || !caid || !provid ||
	    !sid || !ecm_tag || !cw || length != PIPE_SERVER_CSCACHE_REPLY_LEN ||
	    buf[0] != PIPE_SRV_CSCACHE_REPLY) return 0;
	memcpy(server_id, buf + offset, sizeof(*server_id));
	offset += sizeof(*server_id);
	memcpy(request_lifetime_id, buf + offset, sizeof(*request_lifetime_id));
	offset += sizeof(*request_lifetime_id);
	memcpy(caid, buf + offset, sizeof(*caid));
	offset += sizeof(*caid);
	memcpy(provid, buf + offset, sizeof(*provid));
	offset += sizeof(*provid);
	memcpy(sid, buf + offset, sizeof(*sid));
	offset += sizeof(*sid);
	*ecm_tag = buf[offset++];
	memcpy(cw, buf + offset, 16);
	return *server_id != 0 && *request_lifetime_id != 0;
}

/* Queue a value-only cached-server reply without waiting for pipe capacity.
 * The consumer thread performs pipe_send after the ECM lock is released. */
static inline int pipe_queue_server_cscache_reply(
	cache_pipe_queue_t *queue, uint32_t server_id,
	uint64_t request_lifetime_id, uint16_t caid, uint32_t provid,
	uint16_t sid, uint8_t ecm_tag, const uint8_t cw[16])
{
	uint8_t buf[PIPE_SERVER_CSCACHE_REPLY_LEN];
	int len = pipe_encode_server_cscache_reply(buf, sizeof(buf), server_id,
	        request_lifetime_id, caid, provid, sid, ecm_tag, cw);
	if (len <= 0 || !cache_pipe_queue_push(queue, buf, (size_t)len)) return -1;
	return len;
}

#define PIPE_CACHE_FIND          11
#define PIPE_CACHE_FIND_FAILED   12
#define PIPE_CACHE_FIND_WAIT     13
#define PIPE_CACHE_FIND_SUCCESS  14

#define PIPE_CACHE_REQUEST       15
#define PIPE_CACHE_REPLY         16
#define PIPE_CACHE_RESENDREQ     17

#define PIPE_CACHEEX_PUSH_LOCAL  21
#define PIPE_CACHEEX_PUSH_REMOTE 22
//#define PIPE_CACHEEX_PUSH_OUT  23

#define PIPE_CACHEEX_PUSH_IN     24

#define PIPE_CARD_DEL            31
#define PIPE_CARD_ADD            32

/* Internal messages between threads; these are deliberately value snapshots,
 * never pointers into the reloadable profile/client graph. */
#define PIPE_CARD_MAX_PROVIDERS 32
#define PIPE_CARD_SHARE_CCCAM   0x01u
#define PIPE_CARD_SHARE_MGCAMD  0x02u
#define PIPE_CLIENT_ID_LEN (1u + sizeof(uint32_t))

struct pipe_card_snapshot {
	uint32_t profile_id;
	uint16_t caid;
	uint16_t provider_count;
	uint8_t share_flags;
	uint8_t reserved[3];
	uint32_t provider_ids[PIPE_CARD_MAX_PROVIDERS];
};

static inline int pipe_encode_u32_message(uint8_t *buf, size_t capacity,
                                          uint8_t command, uint32_t value)
{
	if (!buf || capacity < PIPE_CLIENT_ID_LEN) return 0;
	buf[0] = command;
	memcpy(buf + 1, &value, sizeof(value));
	return (int)PIPE_CLIENT_ID_LEN;
}

static inline int pipe_decode_u32_message(const uint8_t *buf, size_t length,
                                          uint8_t command, uint32_t *value)
{
	if (!buf || !value || length != PIPE_CLIENT_ID_LEN || buf[0] != command)
		return 0;
	memcpy(value, buf + 1, sizeof(*value));
	return 1;
}

static inline int pipe_encode_card_message(uint8_t *buf, size_t capacity,
                                           uint8_t command,
                                           const struct pipe_card_snapshot *snapshot)
{
	if (!buf || !snapshot || capacity < 1u + sizeof(*snapshot) ||
	    (command != PIPE_CARD_DEL && command != PIPE_CARD_ADD) ||
	    snapshot->provider_count > PIPE_CARD_MAX_PROVIDERS ||
	    (snapshot->share_flags & ~(PIPE_CARD_SHARE_CCCAM|PIPE_CARD_SHARE_MGCAMD)))
		return 0;
	buf[0] = command;
	memcpy(buf + 1, snapshot, sizeof(*snapshot));
	return (int)(1u + sizeof(*snapshot));
}

static inline int pipe_decode_card_message(const uint8_t *buf, size_t length,
                                           uint8_t command,
                                           struct pipe_card_snapshot *snapshot)
{
	if (!buf || !snapshot || length != 1u + sizeof(*snapshot) ||
	    buf[0] != command ||
	    (command != PIPE_CARD_DEL && command != PIPE_CARD_ADD))
		return 0;
	memcpy(snapshot, buf + 1, sizeof(*snapshot));
	if (snapshot->provider_count > PIPE_CARD_MAX_PROVIDERS ||
	    (snapshot->share_flags & ~(PIPE_CARD_SHARE_CCCAM|PIPE_CARD_SHARE_MGCAMD)))
		return 0;
	return 1;
}


// to check for EINTR
int pipe_read( int fd, uint8_t *buf, int len );
int pipe_write( int fd, uint8_t *buf, int len );
int pipe_purge( int fd );
/* Receives one complete internal frame. Oversized payloads are consumed and
 * rejected; capacity is the actual writable size of `buf`. */
int pipe_recv( int fd, uint8_t *buf, size_t capacity );
int pipe_send( int fd, uint8_t *buf, int len );
void pipe_cmd( int pfd, int cmd );
void pipe_lock( int pfd );
void pipe_wakeup( int pfd );
void pipe_send_u32( int pfd, int cmd, uint32_t value );
int pipe_send_server_connected(int pfd, uint32_t server_id,
                               uint64_t connection_generation);
int pipe_send_server_cscache_reply(int pfd, uint32_t server_id,
                                   uint64_t request_lifetime_id,
                                   uint16_t caid, uint32_t provid,
                                   uint16_t sid, uint8_t ecm_tag,
                                   const uint8_t cw[16]);

#endif /* MCS_PIPE_H */

