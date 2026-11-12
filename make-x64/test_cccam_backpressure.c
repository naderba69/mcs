/* test_cccam_backpressure.c — R48 CCcam send backpressure regression.
 * Fills a socketpair send buffer to EAGAIN and verifies cc_msg_send
 * fails cleanly (no deadlock, no crash) and the peer stays usable.
 * This proves the send path tolerates backpressure; it does not prove
 * lock ordering or session races (those need live reconnect tests).
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <poll.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <errno.h>

#include "../src/msg-cccam.c"

#ifdef DEBUG_NETWORK
int flag_debugnet;
#endif

uint32_t getdbgflag(int i, int j, int k)
{
	(void)i;
	(void)j;
	(void)k;
	return 0;
}

void debugf(uint32_t flag, char *format, ...)
{
	(void)flag;
	(void)format;
}

void debughex(uint8_t *buffer, int len)
{
	(void)buffer;
	(void)len;
}

int recv_nonb(int sock, uint8_t *buf, int len, int timeout)
{
	(void)sock;
	(void)buf;
	(void)len;
	(void)timeout;
	return -1;
}

/* Real send_nonb (from src/sockets.c) so backpressure is exercised. */
int send_nonb(int sock, uint8_t *buf, int len, int to)
{
	int remain = len;
	uint8_t *ptr = buf;
	int got;
	(void)to;
	if (sock <= 0) return 0;
	got = send(sock, (void *)ptr, (size_t)remain, MSG_NOSIGNAL | MSG_DONTWAIT);
	if (got >= 0) {
		remain -= got;
		ptr += got;
	} else if (errno != EWOULDBLOCK && errno != EAGAIN && errno != EINTR) {
		return 0;
	}
	while (remain) {
		struct pollfd pfd;
		int ret;
		pfd.fd = sock;
		pfd.events = POLLOUT;
		ret = poll(&pfd, 1, 100);
		if (ret == 0) return 0;
		if (ret < 0) {
			if (errno == EINTR || errno == EAGAIN) continue;
			return 0;
		}
		if (pfd.revents & POLLOUT) {
			got = send(sock, (void *)ptr, (size_t)remain, MSG_NOSIGNAL | MSG_DONTWAIT);
			if (got >= 0) {
				remain -= got;
				ptr += got;
			} else if (errno != EWOULDBLOCK && errno != EAGAIN && errno != EINTR) {
				return 0;
			}
		} else if (pfd.revents & (POLLHUP | POLLERR | POLLNVAL)) return 0;
	}
	return 1;
}

int main(void)
{
	int fails = 0;
	int sv[2];
	int sz = 4096;
	int flags;
	struct cc_crypt_block blk;
#define CHECK(cond, msg) do { \
	if (cond) printf("  [ ok ] %s\n", msg); \
	else { printf("  [FAIL] %s\n", msg); fails++; } \
} while (0)

	printf("test_cccam_backpressure: socketpair EAGAIN tolerance\n");

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
		printf("  [FAIL] socketpair failed: %s\n", strerror(errno));
		return 1;
	}
	/* Shrink send buffer to force EAGAIN quickly */
	setsockopt(sv[0], SOL_SOCKET, SO_SNDBUF, &sz, sizeof(sz));
	setsockopt(sv[1], SOL_SOCKET, SO_SNDBUF, &sz, sizeof(sz));
	/* Non-blocking sender */
	flags = fcntl(sv[0], F_GETFL, 0);
	fcntl(sv[0], F_SETFL, flags | O_NONBLOCK);

	memset(&blk, 0, sizeof(blk));

	/* Fill until EAGAIN or failure; must terminate (no infinite loop) */
	{
		int sends = 0, saw_eagain = 0, saw_fail = 0;
		int i;
		for (i = 0; i < 100000; i++) {
			uint8_t buf[64];
			int r;
			memset(buf, 0xAB, sizeof(buf));
			r = cc_msg_send(sv[0], &blk, CC_MSG_ECM_REQUEST, 64, buf);
			if (r) {
				sends++;
			} else {
				saw_fail = 1;
				if (errno == EAGAIN || errno == EWOULDBLOCK) saw_eagain = 1;
				break;
			}
		}
		CHECK(sends > 0, "some sends succeeded before backpressure");
		CHECK(saw_fail, "send eventually failed under backpressure (no hang)");
		/* send_nonb polls 100ms then returns FALSE; errno may not be
		 * EAGAIN on timeout, so EAGAIN is informational only. */
		printf("  [info] EAGAIN seen: %d\n", saw_eagain);
	}

	/* Peer still usable: drain and send again */
	{
		uint8_t drain[65536];
		ssize_t n;
		int drained = 0;
		while ((n = recv(sv[1], drain, sizeof(drain), MSG_DONTWAIT)) > 0)
			drained += (int)n;
		CHECK(drained > 0, "peer received buffered data");
	}

	/* After drain, send succeeds again */
	{
		uint8_t buf[64];
		int r;
		memset(buf, 0xCD, sizeof(buf));
		r = cc_msg_send(sv[0], &blk, CC_MSG_ECM_REQUEST, 64, buf);
		CHECK(r != 0, "send succeeds after drain (peer usable)");
	}

	close(sv[0]);
	close(sv[1]);
	printf("test_cccam_backpressure: %d failed\n", fails);
	return fails ? 1 : 0;
}
