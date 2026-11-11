#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

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

int send_nonb(int sock, uint8_t *buf, int len, int timeout)
{
	(void)sock;
	(void)buf;
	(void)len;
	(void)timeout;
	return 0;
}

static int check(int condition, const char *name)
{
	printf("  [%s] %s\n", condition ? "ok" : "FAIL", name);
	return condition;
}

static int write_piece(int fd, const uint8_t *data, int len)
{
	int written = 0;
	while (written < len) {
		ssize_t n = write(fd, data + written, (size_t)(len - written));
		if (n <= 0) return 0;
		written += (int)n;
	}
	return 1;
}

int main(void)
{
	int sockets[2];
	int failures = 0;
	int result;
	uint8_t key[] = { 0x31, 0x72, 0x19, 0xa4, 0x55, 0x8c, 0x02, 0xd1 };
	uint8_t plain[] = { 0, CC_MSG_KEEPALIVE, 0, 3, 0xa1, 0xb2, 0xc3 };
	uint8_t encrypted[sizeof(plain)];
	uint8_t output[CC_MAXMSGSIZE];
	struct cc_crypt_block sendblock, recvblock;
	struct message_data msg;

	printf("test_cccam_peekmsg: incremental reads preserve CCcam frame and cipher state\n");
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) != 0) {
		perror("socketpair");
		return 1;
	}
	cc_crypt_init(&sendblock, key, sizeof(key));
	cc_crypt_init(&recvblock, key, sizeof(key));
	memcpy(encrypted, plain, sizeof(plain));
	cc_encrypt(&sendblock, encrypted, sizeof(encrypted));
	memset(&msg, 0, sizeof(msg));
	memset(output, 0, sizeof(output));

	if (!write_piece(sockets[0], encrypted, 2)) {
		perror("write frame header");
		return 1;
	}
	result = cc_peekmsg(sockets[1], &recvblock, &msg, output);
	failures += !check(result == -1 && msg.len == 2 && recvblock.counter == 0,
	                   "a split header is retained without advancing cipher state");

	if (!write_piece(sockets[0], encrypted + 2, 2)) {
		perror("write frame header remainder");
		return 1;
	}
	result = cc_peekmsg(sockets[1], &recvblock, &msg, output);
	failures += !check(result == -1 && msg.len == 4 && recvblock.counter == 4,
	                   "complete header is decrypted once while body remains pending");

	if (!write_piece(sockets[0], encrypted + 4, 1)) {
		perror("write partial frame body");
		return 1;
	}
	result = cc_peekmsg(sockets[1], &recvblock, &msg, output);
	failures += !check(result == -1 && msg.len == 5 && recvblock.counter == 4,
	                   "a partial body is retained without premature decryption");

	if (!write_piece(sockets[0], encrypted + 5, 2)) {
		perror("write frame body remainder");
		return 1;
	}
	result = cc_peekmsg(sockets[1], &recvblock, &msg, output);
	failures += !check(result == (int)sizeof(plain),
	                   "the complete fragmented frame is returned");
	failures += !check(msg.len == 0 && recvblock.counter == sizeof(plain),
	                   "successful frame resets buffered length and advances cipher once");
	failures += !check(memcmp(output, plain, sizeof(plain)) == 0,
	                   "decrypted header and body match the original frame");

	close(sockets[0]);
	close(sockets[1]);
	printf("== %d/6 passed, %d failed ==\n", 6 - failures, failures);
	return failures ? 1 : 0;
}
