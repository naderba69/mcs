/*
 * cachequeue.h -- TASK R6 (D60): the bounded ring between the cache
 * socket drain and the cache parser, compiled only under CACHE_QUEUE.
 *
 * PURE module: fixed storage, one mutex (one producer -- the receive
 * thread -- one consumer -- the worker), no allocation, no tree globals.
 * Unit-tested by make-x64/test_cachequeue.c; clustredcache.c is the only
 * in-tree user. Capacity IS the backpressure: a full ring drops the
 * NEWEST datagram and counts it (cache is lossy by design).
 */
#ifndef MCS_CACHEQUEUE_H
#define MCS_CACHEQUEUE_H

#include <netinet/in.h>
#include <stdint.h>

#define CQ_SLOTS 256
#define CQ_BUF   512   /* cache_recvmsg() accepts 2..512-byte datagrams */

struct cq_entry {
	int              src_id;          /* stable profile ID, not a borrowed pointer */
	int              src_handle;      /* socket descriptor captured at receive */
	uint64_t         config_generation; /* rejects stale entries after reload */
	int              len;
	unsigned char    buf[CQ_BUF];
	struct sockaddr_in from;
};

void          cq_init(void);
/* 1 = queued, 0 = dropped (ring full, or len outside 0..CQ_BUF) */
int           cq_push(int src_id, int src_handle, uint64_t config_generation,
                      const struct sockaddr_in *from,
                      const unsigned char *buf, int len);
/* Match the complete profile identity carried by a deferred datagram. */
static inline int cq_source_matches(const struct cq_entry *entry, int src_id,
                                    int src_handle, uint64_t config_generation)
{
	return entry && entry->src_id == src_id &&
	       entry->src_handle == src_handle &&
	       entry->config_generation == config_generation;
}
/* 1 = entry copied out, 0 = ring empty */
int           cq_pop(struct cq_entry *out);
unsigned long cq_enqueued(void);
unsigned long cq_dropped(void);
int           cq_depth(void);

#endif /* MCS_CACHEQUEUE_H */
