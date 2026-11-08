#ifndef MCS_CACHE_PIPE_QUEUE_H
#define MCS_CACHE_PIPE_QUEUE_H

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define CACHE_PIPE_QUEUE_CAPACITY 1024u
#define CACHE_PIPE_QUEUE_PAYLOAD_MAX 96u

struct cache_pipe_queue_entry {
    size_t len;
    uint8_t data[CACHE_PIPE_QUEUE_PAYLOAD_MAX];
};

typedef struct cache_pipe_queue {
    pthread_mutex_t mutex;
    pthread_cond_t ready;
    unsigned int head;
    unsigned int tail;
    unsigned int count;
    int closed;
    struct cache_pipe_queue_entry entries[CACHE_PIPE_QUEUE_CAPACITY];
} cache_pipe_queue_t;

#define CACHE_PIPE_QUEUE_INITIALIZER \
    { PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 0u, 0u, 0u, 0, {{0u, {0}}} }

/* Fast bounded enqueue: callers may hold lockecm, so this never waits for pipe
 * capacity or a socket write. Returns 1 on enqueue and 0 when invalid/full. */
static inline int cache_pipe_queue_push(cache_pipe_queue_t *queue,
                                        const uint8_t *data, size_t len)
{
    unsigned int slot;
    if (!queue || !data || !len || len > CACHE_PIPE_QUEUE_PAYLOAD_MAX)
        return 0;
    if (pthread_mutex_lock(&queue->mutex) != 0) return 0;
    if (queue->closed || queue->count == CACHE_PIPE_QUEUE_CAPACITY) {
        pthread_mutex_unlock(&queue->mutex);
        return 0;
    }
    slot = queue->tail;
    queue->entries[slot].len = len;
    memcpy(queue->entries[slot].data, data, len);
    queue->tail = (queue->tail + 1u) % CACHE_PIPE_QUEUE_CAPACITY;
    queue->count++;
    pthread_cond_signal(&queue->ready);
    pthread_mutex_unlock(&queue->mutex);
    return 1;
}

/* Stop future producers and wake an empty-queue consumer. A consumer still
 * drains items that were already accepted before reporting the closed/empty
 * queue as empty. */
static inline void cache_pipe_queue_close(cache_pipe_queue_t *queue)
{
    if (!queue || pthread_mutex_lock(&queue->mutex) != 0) return;
    queue->closed = 1;
    pthread_cond_broadcast(&queue->ready);
    pthread_mutex_unlock(&queue->mutex);
}

/* The sole consumer blocks only on this condition variable, never while
 * holding lockecm/lockcache. Returns 1 when an item was copied out, or 0
 * when closed and drained (or a synchronization error occurred). */
static inline int cache_pipe_queue_pop(cache_pipe_queue_t *queue,
                                       uint8_t data[CACHE_PIPE_QUEUE_PAYLOAD_MAX],
                                       size_t *len)
{
    unsigned int slot;
    if (!queue || !data || !len) return 0;
    if (pthread_mutex_lock(&queue->mutex) != 0) return 0;
    while (!queue->count && !queue->closed) {
        if (pthread_cond_wait(&queue->ready, &queue->mutex) != 0) {
            pthread_mutex_unlock(&queue->mutex);
            return 0;
        }
    }
    if (!queue->count) {
        pthread_mutex_unlock(&queue->mutex);
        return 0;
    }
    slot = queue->head;
    *len = queue->entries[slot].len;
    memcpy(data, queue->entries[slot].data, *len);
    queue->head = (queue->head + 1u) % CACHE_PIPE_QUEUE_CAPACITY;
    queue->count--;
    pthread_mutex_unlock(&queue->mutex);
    return 1;
}

#endif /* MCS_CACHE_PIPE_QUEUE_H */
