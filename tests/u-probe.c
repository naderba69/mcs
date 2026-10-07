/*
 * u-probe.c -- TASK 3.8 rig: telnet login with a truncated configured pair.
 *
 * The telnet server greets with "Login: ", reads one CRLF-terminated line,
 * strcmp()s it against cfg.telnet.user, then asks "Password: " the same way.
 * Pre-fix (3.7 binary): a config line "TELNET USER: <70 chars>" stored the
 * 70 bytes -- the stored name is LONGER than its 63-char truncated form, so
 * logging in with the truncated name fails ("wrong username, bye."). Post-fix
 * the stored name IS the 63-char truncated form, one house-format warning is
 * logged, and the truncated pair logs in.
 *
 * argv: host port user pass   -- prints exactly one verdict line:
 *   TELNET-AUTH-OK   (reached the command prompt)
 *   TELNET-AUTH-FAIL (rejected at user or password)
 *   TELNET-PROTO     (server did not speak the expected greeting)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static int rd_line(int fd, char *buf, int cap, int to_ms)
{
	int got = 0;
	while (got < cap - 1) {
		struct pollfd p; p.fd = fd; p.events = POLLIN; p.revents = 0;
		int r = poll(&p, 1, to_ms);
		if (r <= 0) return got;
		char c;
		int n = recv(fd, &c, 1, 0);
		if (n <= 0) return got;
		buf[got++] = c;
		if (c == '\n') break;
	}
	buf[got] = 0;
	return got;
}

static void send_line(int fd, const char *s)
{
	char b[512];
	int n = snprintf(b, sizeof(b), "%s\r\n", s);
	send(fd, b, n, 0);
}

int main(int argc, char **argv)
{
	if (argc < 5) { printf("TELNET-PROTO\n"); return 2; }
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in a; memset(&a, 0, sizeof(a));
	a.sin_family = AF_INET;
	a.sin_port = htons((unsigned short)atoi(argv[2]));
	a.sin_addr.s_addr = inet_addr(argv[1]);
	if (connect(fd, (struct sockaddr *)&a, sizeof(a)) < 0) { printf("TELNET-PROTO\n"); return 2; }

	char buf[4096]; int n;
	/* The greeting is two lines: "Welcome..." then "\r\nLogin: ". Read until
	 * the Login prompt or timeout -- one read is not a line-boundary bet. */
	{ int tries = 0; buf[0] = 0;
	  while (tries++ < 5) {
		n = rd_line(fd, buf, sizeof(buf), 3000);
		if (n <= 0) break;
		if (strstr(buf, "Login")) break;
	  }
	}
	if (!strstr(buf, "Login")) { printf("TELNET-PROTO\n"); return 2; }
	send_line(fd, argv[3]);
	n = rd_line(fd, buf, sizeof(buf), 5000);
	if (n <= 0) { printf("TELNET-PROTO\n"); return 2; }
	if (strstr(buf, "wrong username")) { printf("TELNET-AUTH-FAIL\n"); return 1; }
	if (!strstr(buf, "Password")) { printf("TELNET-PROTO\n"); return 2; }
	send_line(fd, argv[4]);
	/* The success banner is multi-line; read until a verdict or timeout. */
	{ int tries = 0; int ok = 0, bad = 0;
	  while (tries++ < 8) {
		n = rd_line(fd, buf, sizeof(buf), 3000);
		if (n <= 0) break;
		if (strstr(buf, "wrong password")) { bad = 1; break; }
		if (strstr(buf, "command")) { ok = 1; break; }
	  }
	  if (ok) { printf("TELNET-AUTH-OK\n"); return 0; }
	  if (bad) { printf("TELNET-AUTH-FAIL\n"); return 1; }
	}
	printf("TELNET-PROTO\n");
	return 2;
}
