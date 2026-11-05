#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "../src/pipe.h"

/* Minimal process/debug dependencies needed by the production pipe.c unit. */
struct test_program_data { int restart; } prg;
int flag_debugfile;
#define DBG_ERROR 1
int getdbgflag(int type, int id1, int id2);
void debugf(int flag, const char *format, ...);
static int pipe_test_partial_write_once;
static const uint8_t pipe_test_fill[4096];
static ssize_t pipe_test_write(int fd, const void *buf, size_t count);

int getdbgflag(int type, int id1, int id2)
{
    (void)type; (void)id1; (void)id2;
    return 0;
}

void debugf(int flag, const char *format, ...)
{
    (void)flag; (void)format;
}

/* Intercept one write in the focused partial-frame test; all other calls
 * delegate to the real system call. The pipe is left one byte short first. */
#define write pipe_test_write
#include "../src/pipe.c"
#undef write

static ssize_t pipe_test_write(int fd, const void *buf, size_t count)
{
    if (pipe_test_partial_write_once && count > 0) {
        ssize_t first;
        pipe_test_partial_write_once = 0;
        first = write(fd, buf, 1);
        if (first != 1) return first;
        while (write(fd, pipe_test_fill, sizeof(pipe_test_fill)) > 0) { }
        return first;
    }
    return write(fd, buf, count);
}

static uint64_t monotonic_ms(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return UINT64_MAX;
    return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
}

static int set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

int main(void)
{
    int failures = 0;
    int fds[2];
    int rc;
    int len;
    uint8_t message[PIPE_SERVER_CONNECTED_LEN];
    uint8_t cscache_message[PIPE_SERVER_CSCACHE_REPLY_LEN];
    uint32_t server_id = 0x12345678u, decoded_id = 0;
    uint64_t generation = 0x1020304050607080ULL, decoded_generation = 0;
    uint64_t start, elapsed;
    uint8_t fill[4096];

    memset(fill, 0xA5, sizeof(fill));
    if (pipe(fds) != 0 || !set_nonblocking(fds[1])) {
        perror("pipe/nonblocking setup");
        return 1;
    }
    while (write(fds[1], fill, sizeof(fill)) > 0) { }
    if (errno != EAGAIN && errno != EWOULDBLOCK) {
        perror("fill nonblocking pipe");
        close(fds[0]); close(fds[1]);
        return 1;
    }

    start = monotonic_ms();
    rc = pipe_send_server_connected(fds[1], server_id, generation);
    elapsed = monotonic_ms() - start;
    if (rc < 0 && elapsed <= 1000u && !prg.restart)
        printf("PASS: a full pipe times out as a no-byte failure without poisoning the frame stream\n");
    else {
        printf("FAIL: full control pipe result=%d elapsed=%llu ms restart=%d\n",
               rc, (unsigned long long)elapsed, prg.restart);
        failures++;
    }
    close(fds[0]); close(fds[1]);

    /* Inject a genuine one-byte frame write, then fill the pipe from the
     * wrapper so the remainder times out. That committed prefix must be fatal. */
    if (pipe(fds) != 0 || !set_nonblocking(fds[1])) {
        perror("pipe/nonblocking setup");
        return 1;
    }
    prg.restart = 0;
    pipe_test_partial_write_once = 1;
    start = monotonic_ms();
    rc = pipe_send_server_connected(fds[1], server_id, generation);
    elapsed = monotonic_ms() - start;
    if (rc < 0 && elapsed <= 1000u && prg.restart)
        printf("PASS: a real short frame write remains fail-closed\n");
    else {
        printf("FAIL: partial control pipe result=%d elapsed=%llu ms restart=%d\n",
               rc, (unsigned long long)elapsed, prg.restart);
        failures++;
    }
    close(fds[0]); close(fds[1]);
    prg.restart = 0; /* Isolate the next independent successful-pipe case. */

    if (pipe(fds) != 0 || !set_nonblocking(fds[1])) {
        perror("pipe/nonblocking setup");
        return 1;
    }
    rc = pipe_send_server_connected(fds[1], server_id, generation);
    len = rc > 0 ? pipe_recv(fds[0], message, sizeof(message)) : 0;
    if (rc > 0 && len == (int)PIPE_SERVER_CONNECTED_LEN &&
        pipe_decode_server_connected(message, (size_t)len, &decoded_id,
                                     &decoded_generation) &&
        decoded_id == server_id && decoded_generation == generation)
        printf("PASS: successful connection notice preserves its ID and generation\n");
    else {
        printf("FAIL: successful control-pipe round trip failed (send=%d recv=%d)\n",
               rc, len);
        failures++;
    }
    close(fds[0]); close(fds[1]);

    /* The no-thread cached-server reply is value-only and carries an exact
     * request lifetime, so it can be drained under locksrv after ECM unlock. */
    {
        const uint64_t request_lifetime = 0x8877665544332211ULL;
        const uint16_t caid = 0x0500, sid = 0x1234;
        const uint32_t provid = 0x00ABCDEFu;
        const uint8_t ecm_tag = 0x81;
        uint8_t cw[16], decoded_cw[16], decoded_tag = 0;
        uint64_t decoded_lifetime = 0;
        uint32_t decoded_provid = 0;
        uint16_t decoded_caid = 0, decoded_sid = 0;
        int cscache_len, cscache_send, cscache_recv;
        int i;
        for (i = 0; i < 16; i++) cw[i] = (uint8_t)(i * 7u + 3u);
        if (pipe(fds) != 0 || !set_nonblocking(fds[1])) {
            perror("pipe/nonblocking setup");
            return 1;
        }
        cscache_send = pipe_send_server_cscache_reply(fds[1], server_id,
                request_lifetime, caid, provid, sid, ecm_tag, cw);
        cscache_recv = cscache_send > 0
                ? pipe_recv(fds[0], cscache_message, sizeof(cscache_message)) : 0;
        cscache_len = cscache_recv > 0
                ? pipe_decode_server_cscache_reply(cscache_message,
                    (size_t)cscache_recv, &decoded_id, &decoded_lifetime,
                    &decoded_caid, &decoded_provid, &decoded_sid,
                    &decoded_tag, decoded_cw) : 0;
        if (cscache_send > 0 && cscache_recv == (int)PIPE_SERVER_CSCACHE_REPLY_LEN &&
            cscache_len && decoded_id == server_id &&
            decoded_lifetime == request_lifetime && decoded_caid == caid &&
            decoded_provid == provid && decoded_sid == sid &&
            decoded_tag == ecm_tag && memcmp(cw, decoded_cw, sizeof(cw)) == 0)
            printf("PASS: cached Newcamd reply preserves server, lifetime, channel, tag, and CW\n");
        else {
            printf("FAIL: cached Newcamd reply round trip failed (send=%d recv=%d decode=%d)\n",
                   cscache_send, cscache_recv, cscache_len);
            failures++;
        }
        close(fds[0]); close(fds[1]);
    }

    /* The production no-thread path uses this bounded queue rather than
     * waiting for ECM-pipe capacity while lockecm is held. Check saturation
     * is reported and that queued values remain fully decodable. */
    {
        static cache_pipe_queue_t queue = CACHE_PIPE_QUEUE_INITIALIZER;
        uint8_t queued_message[CACHE_PIPE_QUEUE_PAYLOAD_MAX];
        uint8_t cw[16], queued_cw[16], queued_tag = 0;
        uint64_t queued_lifetime = 0;
        uint32_t queued_id = 0, queued_provid = 0;
        uint16_t queued_caid = 0, queued_sid = 0;
        size_t queued_len = 0;
        int i, full = 1, overflow, queued;
        for (i = 0; i < 16; i++) cw[i] = (uint8_t)(0xE0u + (unsigned)i);
        for (i = 0; i < (int)CACHE_PIPE_QUEUE_CAPACITY; i++) {
            int queued_bytes = pipe_queue_server_cscache_reply(&queue,
                    server_id, (uint64_t)i + 1u, 0x0500, 0x00ABCDEFu,
                    0x1234, 0x81, cw);
            if (queued_bytes != (int)PIPE_SERVER_CSCACHE_REPLY_LEN) {
                full = 0;
                break;
            }
        }
        overflow = pipe_queue_server_cscache_reply(&queue, server_id,
                    (uint64_t)CACHE_PIPE_QUEUE_CAPACITY + 1u, 0x0500,
                    0x00ABCDEFu, 0x1234, 0x81, cw);
        queued = cache_pipe_queue_pop(&queue, queued_message, &queued_len);
        if (full && overflow < 0 && queued &&
            queued_len == (size_t)PIPE_SERVER_CSCACHE_REPLY_LEN &&
            pipe_decode_server_cscache_reply(queued_message, queued_len,
                &queued_id, &queued_lifetime, &queued_caid, &queued_provid,
                &queued_sid, &queued_tag, queued_cw) &&
            queued_id == server_id && queued_lifetime == 1u &&
            queued_caid == 0x0500 && queued_provid == 0x00ABCDEFu &&
            queued_sid == 0x1234 && queued_tag == 0x81 &&
            memcmp(cw, queued_cw, sizeof(cw)) == 0)
            printf("PASS: deferred cached-server reply queue is value-safe and rejects overflow\n");
        else {
            printf("FAIL: deferred cached-server reply queue saturation/round-trip failed\n");
            failures++;
        }
        cache_pipe_queue_close(&queue);
        while (cache_pipe_queue_pop(&queue, queued_message, &queued_len)) { }
    }

    printf("%d pipe-server-send checks failed\n", failures);
    return failures ? 1 : 0;
}
