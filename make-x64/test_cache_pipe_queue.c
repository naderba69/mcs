#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include "../src/cache-pipe-queue.h"

static int failures;
static pthread_mutex_t wait_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wait_changed = PTHREAD_COND_INITIALIZER;
static int consumer_started;
static int consumer_result = -1;
static cache_pipe_queue_t *consumer_queue;

static void *waiting_consumer(void *unused)
{
    uint8_t out[CACHE_PIPE_QUEUE_PAYLOAD_MAX];
    size_t out_len;
    (void)unused;
    pthread_mutex_lock(&wait_lock);
    consumer_started = 1;
    pthread_cond_broadcast(&wait_changed);
    pthread_mutex_unlock(&wait_lock);
    consumer_result = cache_pipe_queue_pop(consumer_queue, out, &out_len);
    return NULL;
}
#define CHECK(condition, description) do { \
    if (condition) printf("PASS: %s\n", description); \
    else { printf("FAIL: %s\n", description); failures++; } \
} while (0)

int main(void)
{
    static cache_pipe_queue_t queue = CACHE_PIPE_QUEUE_INITIALIZER;
    uint8_t frame[4], out[CACHE_PIPE_QUEUE_PAYLOAD_MAX];
    size_t out_len = 0;
    unsigned int i;

    frame[0] = 0x11; frame[1] = 0x22; frame[2] = 0x33;
    CHECK(cache_pipe_queue_push(&queue, frame, 3),
          "a serialized cache frame is accepted into the outbound queue");
    memset(out, 0, sizeof(out));
    CHECK(cache_pipe_queue_pop(&queue, out, &out_len) && out_len == 3 &&
          memcmp(frame, out, 3) == 0,
          "the queue preserves frame bytes and payload length");

    {
        int filled = 1, fifo = 1;
        for (i = 0; i < CACHE_PIPE_QUEUE_CAPACITY; i++) {
            frame[0] = (uint8_t)i;
            if (!cache_pipe_queue_push(&queue, frame, 1)) filled = 0;
        }
        CHECK(filled, "queue accepts items up to its bounded capacity");
        CHECK(!cache_pipe_queue_push(&queue, frame, 1),
              "a full outbound queue fails immediately instead of waiting on pipe I/O");
        for (i = 0; i < CACHE_PIPE_QUEUE_CAPACITY; i++) {
            if (!cache_pipe_queue_pop(&queue, out, &out_len) || out_len != 1 ||
                out[0] != (uint8_t)i) fifo = 0;
        }
        CHECK(fifo, "consecutive serialized frames are dequeued in FIFO order");
    }
    CHECK(!cache_pipe_queue_push(&queue, frame,
                                 CACHE_PIPE_QUEUE_PAYLOAD_MAX + 1u),
          "oversized cache pipe payloads are rejected before enqueue");

    {
        static cache_pipe_queue_t closing_queue = CACHE_PIPE_QUEUE_INITIALIZER;
        pthread_t consumer;
        int joined;
        consumer_started = 0;
        consumer_result = -1;
        consumer_queue = &closing_queue;
        joined = pthread_create(&consumer, NULL, waiting_consumer, NULL) == 0;
        if (joined) {
            pthread_mutex_lock(&wait_lock);
            while (!consumer_started) pthread_cond_wait(&wait_changed, &wait_lock);
            pthread_mutex_unlock(&wait_lock);
            usleep(20000);
            cache_pipe_queue_close(&closing_queue);
            joined = pthread_join(consumer, NULL) == 0;
        }
        CHECK(joined && consumer_result == 0,
              "closing an empty queue wakes and releases its blocked consumer");
        CHECK(!cache_pipe_queue_push(&closing_queue, frame, 1),
              "a closed outbound queue refuses new frames");
    }

    {
        static cache_pipe_queue_t pending_queue = CACHE_PIPE_QUEUE_INITIALIZER;
        frame[0] = 0x5a;
        CHECK(cache_pipe_queue_push(&pending_queue, frame, 1),
              "a frame queued before shutdown remains available to drain");
        cache_pipe_queue_close(&pending_queue);
        CHECK(cache_pipe_queue_pop(&pending_queue, out, &out_len) &&
              out_len == 1 && out[0] == 0x5a,
              "closed queues preserve already accepted frames until drained");
        CHECK(!cache_pipe_queue_pop(&pending_queue, out, &out_len),
              "a closed and drained queue returns without blocking");
    }
    printf("%d cache-pipe queue checks failed\n", failures);
    return failures ? 1 : 0;
}
