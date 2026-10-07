/*
 * cachequeue.c -- TASK R6 (D60): the bounded SPSC ring. See cachequeue.h.
 */
#include <string.h>
#include <pthread.h>

#include "cachequeue.h"

static struct cq_entry cq_ring[CQ_SLOTS];
static int             cq_head, cq_tail, cq_count;
static pthread_mutex_t cq_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned long   cq_n_enqueued, cq_n_dropped;

void cq_init(void)
{
	cq_head = cq_tail = cq_count = 0;
	cq_n_enqueued = cq_n_dropped = 0;
}

int cq_push(void *src, const struct sockaddr_in *from,
            const unsigned char *buf, int len)
{
	if (!from || !buf || len < 0 || len > CQ_BUF) return 0;
	pthread_mutex_lock(&cq_lock);
	if (cq_count >= CQ_SLOTS) {
		cq_n_dropped++;
		pthread_mutex_unlock(&cq_lock);
		return 0;
	}
	struct cq_entry *e = &cq_ring[cq_head];
	e->src = src;
	e->len = len;
	memcpy(e->buf, buf, len);
	e->from = *from;
	cq_head = (cq_head + 1) % CQ_SLOTS;
	cq_count++;
	cq_n_enqueued++;
	pthread_mutex_unlock(&cq_lock);
	return 1;
}

int cq_pop(struct cq_entry *out)
{
	if (!out) return 0;
	pthread_mutex_lock(&cq_lock);
	if (cq_count == 0) {
		pthread_mutex_unlock(&cq_lock);
		return 0;
	}
	*out = cq_ring[cq_tail];
	cq_tail = (cq_tail + 1) % CQ_SLOTS;
	cq_count--;
	pthread_mutex_unlock(&cq_lock);
	return 1;
}

unsigned long cq_enqueued(void) { return cq_n_enqueued; }
unsigned long cq_dropped(void)  { return cq_n_dropped; }
int cq_depth(void)              { return cq_count; }
