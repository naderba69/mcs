/*
 * cardfill.c — TASK 3.17 live peer.
 *
 * Advertises 1024 cards (CAID 0x1884, provid 0..1023) in datagrams the
 * server will actually accept (it drops anything over 512 bytes), then
 * answers a TYPE_REQUEST for 0x1884 and only logs any other CAID.
 *
 * argv: server-cache-port  my-port  32-hex-cw
 *
 * One line per event, so the Makefile can assert on them.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <poll.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#define TYPE_REQUEST   1
#define TYPE_REPLY     2
#define TYPE_PINGREQ   3
#define TYPE_CARD_LIST 0x13
#define TYPE_HELLO_ACK 0x10

static int hex2nib(int c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

static int parse_cw(const char *s, uint8_t out[16])
{
	int i;
	if (strlen(s) != 32) return -1;
	for (i = 0; i < 16; i++) {
		int hi = hex2nib(s[i * 2]), lo = hex2nib(s[i * 2 + 1]);
		if (hi < 0 || lo < 0) return -1;
		out[i] = (uint8_t)((hi << 4) | lo);
	}
	return 0;
}

static void advertise(int fd, struct sockaddr_in *srv)
{
	unsigned char pkt[512];
	int sent = 0;
	int first = 1;

	while (sent < 1024) {
		int n = 1024 - sent;
		int i;
		if (n > 127) n = 127;
		memset(pkt, 0, sizeof(pkt));
		pkt[0] = TYPE_CARD_LIST;
		pkt[1] = first ? 1 : 0;
		first = 0;
		for (i = 0; i < n; i++) {
			uint32_t id = 0x18840000u + (uint32_t)(sent + i);
			int o = 2 + i * 4;
			pkt[o]     = (unsigned char)(id >> 24);
			pkt[o + 1] = (unsigned char)(id >> 16);
			pkt[o + 2] = (unsigned char)(id >> 8);
			pkt[o + 3] = (unsigned char)id;
		}
		sendto(fd, pkt, (size_t)(2 + n * 4), 0,
		       (struct sockaddr *)srv, sizeof(*srv));
		sent += n;
	}
	printf("PEER: advertised 1024 cards\n");
	fflush(stdout);
}

int main(int argc, char **argv)
{
	int srvport = 16852, myport = 16853;
	uint8_t cw[16] = { 0x11,0x22,0x33,0x66, 0x44,0x55,0x66,0xFF,
	                   0x77,0x88,0x99,0x98, 0xAA,0xBB,0xCC,0x31 };
	unsigned char buf[512];
	struct sockaddr_in srv, me, from;
	socklen_t flen;
	int fd, cards = 0;

	if (argc > 1) srvport = atoi(argv[1]);
	if (argc > 2) myport = atoi(argv[2]);
	if (argc > 3 && argv[3][0]) {
		if (parse_cw(argv[3], cw)) {
			printf("PEER: bad cw hex\n");
			return 2;
		}
	}

	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0) { perror("socket"); return 2; }
	memset(&me, 0, sizeof(me));
	me.sin_family = AF_INET;
	me.sin_addr.s_addr = htonl(INADDR_ANY);
	me.sin_port = htons((uint16_t)myport);
	if (bind(fd, (struct sockaddr *)&me, sizeof(me)) < 0) {
		perror("bind");
		return 2;
	}
	memset(&srv, 0, sizeof(srv));
	srv.sin_family = AF_INET;
	srv.sin_port = htons((uint16_t)srvport);
	inet_aton("127.0.0.1", &srv.sin_addr);

	for (;;) {
		struct pollfd p;
		int n;
		p.fd = fd;
		p.events = POLLIN;
		if (poll(&p, 1, 1000) <= 0) continue;
		flen = sizeof(from);
		n = (int)recvfrom(fd, buf, sizeof(buf), 0, (struct sockaddr *)&from, &flen);
		if (n < 1) continue;

		if (buf[0] == TYPE_PINGREQ && n >= 6) {
			unsigned char ack[16];
			memset(ack, 0, sizeof(ack));
			ack[0] = TYPE_HELLO_ACK;
			ack[4] = buf[4];
			ack[5] = buf[5];
			sendto(fd, ack, 16, 0, (struct sockaddr *)&srv, sizeof(srv));
			printf("PEER: answered ping\n");
			fflush(stdout);
			if (!cards) {
				advertise(fd, &srv);
				cards = 1;
			}
			continue;
		}

		if (buf[0] == TYPE_REQUEST && n >= 12) {
			unsigned int caid = ((unsigned int)buf[6] << 8) | buf[7];
			printf("PEER: request caid %04X\n", caid);
			fflush(stdout);
			if (caid == 0x1884) {
				unsigned char rep[30];
				memset(rep, 0, sizeof(rep));
				rep[0] = TYPE_REPLY;
				memcpy(rep + 1, buf + 1, 11);
				rep[12] = buf[1]; /* integrity: the server requires buf[12]==buf[1] */
				memcpy(rep + 13, cw, 16);
				sendto(fd, rep, 29, 0, (struct sockaddr *)&srv, sizeof(srv));
				printf("PEER: answered\n");
				fflush(stdout);
			}
		}
	}
}
