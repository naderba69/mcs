/*
 * Test-only LD_PRELOAD shim for newcamd-ack-race.sh.
 *
 * It observes the real accepted Newcamd socket, shrinks SO_SNDBUF, fills the
 * kernel send queue with real nonblocking sends until EAGAIN, then pauses the
 * server's real LOGIN_ACK send at a file barrier. After release, it records the
 * real ACK send result and the real POLLOUT poll result from send_nonb().
 * This source is never linked into a production binary.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define FD_LIMIT 65536
#define FILL_LIMIT (8u * 1024u * 1024u)

static ssize_t (*next_send)(int, const void *, size_t, int);
static int (*next_poll)(struct pollfd *, nfds_t, int);
static pthread_once_t resolve_once = PTHREAD_ONCE_INIT;
static int init_sent[FD_LIMIT];
static int injection_used;
static int ack_poll_fd = -1;
static int ack_poll_pending;
static int ack_poll_sequence;
static int ack_guard_fd = -1;
static const void *ack_guard_buf;
static size_t ack_guard_len;
static int ack_guard_active;
static int ack_retry_count;

static void resolve_next_symbols(void)
{
    next_send = (ssize_t (*)(int, const void *, size_t, int))dlsym(RTLD_NEXT, "send");
    next_poll = (int (*)(struct pollfd *, nfds_t, int))dlsym(RTLD_NEXT, "poll");
}

static void ensure_symbols(void)
{
    if (pthread_once(&resolve_once, resolve_next_symbols) != 0 ||
        !next_send || !next_poll)
        _exit(127);
}

static int configured_port(void)
{
    const char *value = getenv("MCS_TEST_NEWCAMD_PORT");
    return value ? atoi(value) : 0;
}

static int is_newcamd_socket(int fd)
{
    struct sockaddr_in local;
    socklen_t length = sizeof(local);
    int port = configured_port();
    if (port <= 0 || getsockname(fd, (struct sockaddr *)&local, &length) != 0)
        return 0;
    return local.sin_family == AF_INET && ntohs(local.sin_port) == (uint16_t)port;
}

static void write_marker(const char *env_name, const char *text)
{
    const char *path = getenv(env_name);
    char temporary[512];
    int fd;
    int length;
    size_t remaining;
    if (!path || !*path) return;
    length = snprintf(temporary, sizeof(temporary), "%s.tmp.%ld.%lu",
                      path, (long)getpid(), (unsigned long)pthread_self());
    if (length < 0 || (size_t)length >= sizeof(temporary)) return;
    fd = open(temporary, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return;
    remaining = strlen(text);
    while (remaining) {
        ssize_t written = write(fd, text, remaining);
        if (written > 0) {
            text += written;
            remaining -= (size_t)written;
        } else if (written < 0 && errno == EINTR) continue;
        else break;
    }
    if (close(fd) == 0) {
        if (rename(temporary, path) != 0) unlink(temporary);
    } else unlink(temporary);
}

static void wait_for_release(void)
{
    const char *path = getenv("MCS_ACK_RELEASE_FILE");
    struct timespec delay;
    unsigned int i;
    if (!path || !*path) return;
    delay.tv_sec = 0;
    delay.tv_nsec = 10000000L;
    for (i = 0; i < 1200; i++) {
        if (access(path, F_OK) == 0) return;
        nanosleep(&delay, NULL);
    }
}

static size_t fill_send_queue(int fd, size_t chunk_size, int *saturated)
{
    char junk[4096];
    size_t filled = 0;
    *saturated = 0;
    if (!chunk_size || chunk_size > sizeof(junk)) chunk_size = sizeof(junk);
    memset(junk, 0xa5, sizeof(junk));
    while (filled < FILL_LIMIT) {
        ssize_t n = next_send(fd, junk, chunk_size,
                              MSG_NOSIGNAL | MSG_DONTWAIT);
        if (n > 0) {
            filled += (size_t)n;
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) *saturated = 1;
        break;
    }
    return filled;
}

static ssize_t inject_ack_backpressure(int fd, const void *buf, size_t len,
                                       int flags)
{
    int send_buffer = 256;
    int actual_send_buffer = 0;
    int sndbuf_configured, sndbuf_read;
    socklen_t option_length = sizeof(actual_send_buffer);
    size_t filled;
    int saturated;
    char marker[192];
    ssize_t result;
    int saved_errno;

    sndbuf_configured = setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &send_buffer,
                                   sizeof(send_buffer)) == 0;
    filled = fill_send_queue(fd, len, &saturated);
    sndbuf_read = getsockopt(fd, SOL_SOCKET, SO_SNDBUF, &actual_send_buffer,
                             &option_length) == 0;
    snprintf(marker, sizeof(marker),
             "filled=%lu saturated=%d sndbuf=%d configured=%d read=%d chunk=%lu\n",
             (unsigned long)filled, saturated, actual_send_buffer,
             sndbuf_configured, sndbuf_read, (unsigned long)len);
    write_marker("MCS_ACK_BLOCK_MARKER", marker);
    wait_for_release();

    /* The peer's TCP stack may ACK the initial junk while this test is held at
     * the file barrier, even though the harness application is not reading.
     * Refill immediately before the real ACK, using an equal-sized write so an
     * EAGAIN on junk means this particular LOGIN_ACK frame cannot sneak in. */
    filled = fill_send_queue(fd, len, &saturated);
    snprintf(marker, sizeof(marker),
             "refilled=%lu saturated=%d chunk=%lu\n",
             (unsigned long)filled, saturated, (unsigned long)len);
    write_marker("MCS_ACK_REFILL_MARKER", marker);

    result = next_send(fd, buf, len, flags);
    saved_errno = errno;
    snprintf(marker, sizeof(marker), "result=%ld errno=%d len=%lu\n",
             (long)result, saved_errno, (unsigned long)len);
    write_marker("MCS_ACK_SEND_RESULT", marker);
    if (result < 0 && (saved_errno == EAGAIN || saved_errno == EWOULDBLOCK)) {
        __atomic_store_n(&ack_guard_fd, fd, __ATOMIC_RELAXED);
        __atomic_store_n(&ack_guard_buf, buf, __ATOMIC_RELAXED);
        __atomic_store_n(&ack_guard_len, len, __ATOMIC_RELAXED);
        __atomic_store_n(&ack_retry_count, 0, __ATOMIC_RELAXED);
        __atomic_store_n(&ack_poll_fd, fd, __ATOMIC_RELEASE);
        __atomic_store_n(&ack_poll_pending, 1, __ATOMIC_RELEASE);
        __atomic_store_n(&ack_guard_active, 1, __ATOMIC_RELEASE);
    }
    errno = saved_errno;
    return result;
}

static ssize_t retry_guarded_ack(int fd, const void *buf, size_t len, int flags)
{
    char marker[192];
    int saturated;
    int attempt;
    size_t filled;
    ssize_t result;
    int saved_errno;

    /* POLLOUT may be reported while fewer than one complete ACK frame's worth
     * of space is available. Refill with a same-sized real write immediately
     * before each ACK retry, preventing that writable edge from draining the
     * queue behind the test harness's intentionally unread TCP payload. */
    filled = fill_send_queue(fd, len, &saturated);
    result = next_send(fd, buf, len, flags);
    saved_errno = errno;
    attempt = __atomic_add_fetch(&ack_retry_count, 1, __ATOMIC_ACQ_REL);
    snprintf(marker, sizeof(marker),
             "attempt=%d refilled=%lu saturated=%d result=%ld errno=%d len=%lu\n",
             attempt, (unsigned long)filled, saturated, (long)result,
             saved_errno, (unsigned long)len);
    write_marker("MCS_ACK_RETRY_RESULT", marker);
    if (result < 0 && (saved_errno == EAGAIN || saved_errno == EWOULDBLOCK))
        __atomic_store_n(&ack_poll_pending, 1, __ATOMIC_RELEASE);
    else
        __atomic_store_n(&ack_guard_active, 0, __ATOMIC_RELEASE);
    errno = saved_errno;
    return result;
}

ssize_t send(int fd, const void *buf, size_t len, int flags)
{
    int index = fd >= 0 && fd < FD_LIMIT ? fd : -1;
    ensure_symbols();
    if (!is_newcamd_socket(fd)) return next_send(fd, buf, len, flags);

    /* After one genuine ACK EAGAIN, guard only retries of that exact frame.
     * Other Newcamd traffic and every later client remain unmodified. */
    if (__atomic_load_n(&ack_guard_active, __ATOMIC_ACQUIRE) &&
        fd == __atomic_load_n(&ack_guard_fd, __ATOMIC_RELAXED) &&
        buf == __atomic_load_n(&ack_guard_buf, __ATOMIC_RELAXED) &&
        len == __atomic_load_n(&ack_guard_len, __ATOMIC_RELAXED))
        return retry_guarded_ack(fd, buf, len, flags);

    /* The server's unencrypted 14-byte keymod is the first send on each
     * accepted socket. The following send is the encrypted login ACK. */
    if (len == 14) {
        if (index >= 0) __atomic_store_n(&init_sent[index], 1, __ATOMIC_RELEASE);
        return next_send(fd, buf, len, flags);
    }
    if (index >= 0 &&
        __atomic_load_n(&init_sent[index], __ATOMIC_ACQUIRE) &&
        !__atomic_exchange_n(&injection_used, 1, __ATOMIC_ACQ_REL))
        return inject_ack_backpressure(fd, buf, len, flags);
    return next_send(fd, buf, len, flags);
}

int poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
    int ack_wait;
    int capture;
    int result;
    int saved_errno;
    int sequence = 0;
    uint64_t entered_us = 0;
    struct timespec entered_at;
    char marker[192];
    ensure_symbols();
    ack_wait = nfds == 1 && fds &&
               fds[0].fd == __atomic_load_n(&ack_poll_fd, __ATOMIC_ACQUIRE) &&
               (fds[0].events & POLLOUT);
    capture = ack_wait &&
              __atomic_exchange_n(&ack_poll_pending, 0, __ATOMIC_ACQ_REL);
    if (capture) {
        int incoming_errno = errno;
        sequence = __atomic_add_fetch(&ack_poll_sequence, 1, __ATOMIC_ACQ_REL);
        if (clock_gettime(CLOCK_MONOTONIC, &entered_at) == 0)
            entered_us = (uint64_t)entered_at.tv_sec * 1000000u +
                         (uint64_t)entered_at.tv_nsec / 1000u;
        snprintf(marker, sizeof(marker),
                 "seq=%d timeout=%d fd=%d entered_us=%llu\n",
                 sequence, timeout, fds[0].fd,
                 (unsigned long long)entered_us);
        write_marker("MCS_ACK_POLL_ENTER", marker);
        errno = incoming_errno;
    }
    result = next_poll(fds, nfds, timeout);
    saved_errno = errno;
    if (ack_wait &&
        (result == 0 ||
         (result > 0 &&
          (fds[0].revents & (POLLHUP | POLLERR | POLLNVAL))) ||
         (result < 0 && saved_errno != EINTR)))
        __atomic_store_n(&ack_guard_active, 0, __ATOMIC_RELEASE);
    if (capture) {
        snprintf(marker, sizeof(marker),
                 "seq=%d timeout=%d result=%d revents=0x%x\n",
                 sequence, timeout, result, (unsigned int)fds[0].revents);
        write_marker("MCS_ACK_POLL_RESULT", marker);
    }
    errno = saved_errno;
    return result;
}
