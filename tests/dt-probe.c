/*
 * dt-probe.c -- TASK 3.11: log into telnet and run one or more commands.
 *
 * argv: host port user pass cmd [cmd...]
 * Prints each reply (CR stripped) between a "--- <cmd> ---" marker and the
 * next prompt, so a makefile can grep the transcript. Exit 0 only when every
 * command got a prompt back.
 *
 *   DT-AUTH-FAIL  rejected at user or password
 *   DT-PROTO      the server did not speak the expected greeting or prompt
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static int read_until(int fd, char *buf, int cap, const char *marker, int to_ms)
{
	int got = 0;
	buf[0] = 0;
	while (got < cap - 1) {
		struct pollfd p;
		p.fd = fd; p.events = POLLIN; p.revents = 0;
		if (poll(&p, 1, to_ms) <= 0) break;
		{
			char c;
			int n = recv(fd, &c, 1, 0);
			if (n <= 0) break;
			buf[got++] = c;
			buf[got] = 0;
			if (strstr(buf, marker)) {
				/* The prompt is "[command]: " — the trailing space is
				 * still in the socket and would prefix the next reply. */
				struct pollfd q;
				q.fd = fd; q.events = POLLIN; q.revents = 0;
				if (poll(&q, 1, 200) > 0) {
					char sp;
					if (recv(fd, &sp, 1, MSG_PEEK) == 1 && sp == ' ')
						recv(fd, &sp, 1, 0);
				}
				return got;
			}
		}
	}
	return got;
}

static void send_line(int fd, const char *s)
{
	char b[512];
	int n = snprintf(b, sizeof(b), "%s\r\n", s);
	send(fd, b, n, 0);
}

static void print_body(const char *buf)
{
	const char *p = buf;
	while (*p) {
		if (*p != '\r') putchar(*p);
		p++;
	}
}

int main(int argc, char **argv)
{
	int fd, i, n;
	struct sockaddr_in a;
	char buf[8192];

	if (argc < 6) { printf("DT-PROTO\n"); return 2; }
	fd = socket(AF_INET, SOCK_STREAM, 0);
	memset(&a, 0, sizeof(a));
	a.sin_family = AF_INET;
	a.sin_port = htons((unsigned short)atoi(argv[2]));
	a.sin_addr.s_addr = inet_addr(argv[1]);
	if (fd < 0 || connect(fd, (struct sockaddr *)&a, sizeof(a)) < 0) {
		printf("DT-PROTO\n");
		return 2;
	}

	n = read_until(fd, buf, sizeof(buf), "Login", 5000);
	if (!strstr(buf, "Login")) { printf("DT-PROTO\n"); return 2; }
	send_line(fd, argv[3]);
	n = read_until(fd, buf, sizeof(buf), "Password", 5000);
	if (n <= 0) { printf("DT-PROTO\n"); return 2; }
	if (strstr(buf, "wrong username")) { printf("DT-AUTH-FAIL\n"); return 1; }
	if (!strstr(buf, "Password")) { printf("DT-PROTO\n"); return 2; }
	send_line(fd, argv[4]);
	n = read_until(fd, buf, sizeof(buf), "[command]:", 5000);
	if (!strstr(buf, "[command]:")) { printf("DT-PROTO\n"); return 2; }

	for (i = 5; i < argc; i++) {
		send_line(fd, argv[i]);
		n = read_until(fd, buf, sizeof(buf), "[command]:", 5000);
		if (!strstr(buf, "[command]:")) { printf("DT-PROTO\n"); return 2; }
		printf("--- %s ---\n", argv[i]);
		print_body(buf);
		if (buf[0] && buf[strlen(buf) - 1] != '\n') printf("\n");
	}
	send_line(fd, "exit");
	return 0;
}
