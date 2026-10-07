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

#define CQ_SLOTS 256
#define CQ_BUF   512   /* cache_recvmsg() accepts 2..512-byte datagrams */

struct cq_entry {
	void            *src;   /* opaque: which socket it arrived on */
	int              len;
	unsigned char    buf[CQ_BUF];
	struct sockaddr_in from;
};

void          cq_init(void);
/* 1 = queued, 0 = refused: the ring was full (counted by cq_dropped(),
 * the backpressure the module exists for) or the arguments were invalid --
 * len outside 0..CQ_BUF or a NULL pointer (a caller error, NOT counted as a
 * drop; the R6 A/B run asserts zero drops on valid traffic). TASK R13 (D67)
 * corrected this comment: it claimed both cases were drops. */
int           cq_push(void *src, const struct sockaddr_in *from,
                      const unsigned char *buf, int len);
/* 1 = entry copied out, 0 = ring empty */
int           cq_pop(struct cq_entry *out);
unsigned long cq_enqueued(void);
unsigned long cq_dropped(void);
int           cq_depth(void);

#endif /* MCS_CACHEQUEUE_H */
