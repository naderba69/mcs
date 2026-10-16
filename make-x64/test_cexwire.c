/*
 * test_cexwire.c — TASK R17 (D72): pin the cache-exchange wire to the
 * oscam dialect.
 *
 * The independence rule that makes this test worth anything: the golden
 * packets below are built BYTE BY BYTE from the oscam source layout
 * (gfto/oscam module-camd35-cacheex.c / module-cccam-cacheex.c, fetched
 * 2026-10-12), never by calling our packer twice. If our packer drifts
 * from the wire, the memcmp tests fail; if our unpacker drifts, the
 * field tests fail.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/cexwire.h"

static int okc, failc;
static void check(const char *name, int cond)
{
	if (cond) { okc++; printf("  ok  %s\n", name); }
	else { failc++; printf("  FAIL %s\n", name); }
}

/* build an oscam camd35 push frame by hand (module-camd35-cacheex.c:244) */
static int golden_camd35(uint8_t *w, uint16_t sid, uint16_t caid, uint32_t prid,
                         uint8_t cwc, uint8_t tag, const uint8_t *ecmd5,
                         uint32_t hash, const uint8_t *cw,
                         int nnodes, const uint8_t nodes[][8])
{
	int i, size = 16 + 4 + 16 + 1 + nnodes * 8;  /* oscam's `size` */
	memset(w, 0, 256);
	/* framing: [ucrc 4][payload 0..3][datacrc 4][payload 4..] */
	w[4] = 0x3f;
	w[5] = size & 0xff;
	w[6] = size >> 8;
	w[7] = 0;                                   /* rc = E_FOUND */
	w[12] = sid >> 8; w[13] = sid & 0xff;
	w[14] = caid >> 8; w[15] = caid & 0xff;
	w[16] = prid >> 24; w[17] = prid >> 16; w[18] = prid >> 8; w[19] = prid;
	w[22] = cwc;
	w[23] = tag;
	memcpy(w + 24, ecmd5, 16);
	/* i2b_buf(4, htonl(hash)) == little-endian bytes */
	w[40] = hash & 0xff; w[41] = (hash >> 8) & 0xff;
	w[42] = (hash >> 16) & 0xff; w[43] = hash >> 24;
	memcpy(w + 44, cw, 16);
	w[60] = nnodes;
	for (i = 0; i < nnodes; i++) memcpy(w + 61 + 8*i, nodes[i], 8);
	return 4 + 20 + size;
}

int main(void)
{
	uint8_t ecmd5[16], cw[16], nodes[3][8], buf[512], w[256];
	struct cexw_push p, q;
	struct cexw_filter f;
	int len, i;

	printf("test_cexwire: TASK R17 the oscam cacheex wire (spec-pinned)\n");

	for (i = 0; i < 16; i++) { ecmd5[i] = (uint8_t)(0xA0 + i); cw[i] = (uint8_t)(0x10 + i); }
	for (i = 0; i < 8; i++) { nodes[0][i] = (uint8_t)(0x11 + i); nodes[1][i] = (uint8_t)(0x22 + i); nodes[2][i] = (uint8_t)(0x33 + i); }

	memset(&p, 0, sizeof(p));
	p.sid = 0x0064; p.caid = 0x1884; p.provid = 0x000001;
	p.cwc_cycletime = 0x88;                     /* 0x80|8: next-cw flag + 8s */
	p.tag = 0x81;
	memcpy(p.ecmd5, ecmd5, 16);
	p.hash = 0xA1669C0C;
	memcpy(p.cw, cw, 16);
	p.rc = CEXW_RC_FOUND;
	p.nnodes = 2;
	memcpy(p.nodes[0], nodes[0], 8);
	memcpy(p.nodes[1], nodes[1], 8);

	/* ---- camd35 pack == the golden frame ---- */
	len = cexw_pack_camd35_push(buf, sizeof(buf), &p);
	check("camd35 pack returns the payload length", len == 20 + (16+4+16+1+16));
	/* compare against golden payload (strip our framing gap the same way) */
	{
		uint8_t gold[256];
		int glen = golden_camd35(gold, 0x64, 0x1884, 0x000001, 0x88, 0x81,
		                         ecmd5, 0xA1669C0C, cw, 2, nodes);
		/* our packer emits payload form; the framed golden carries the
		 * same payload at +4 with ucrc/datacrc gaps. Compare field-wise. */
		check("camd35 cmd 0x3f", buf[0] == 0x3f);
		check("camd35 size 2-byte LE == oscam size", buf[1] == (glen-24) && buf[2] == 0);
		check("camd35 rc == E_FOUND (0)", buf[3] == 0);
		check("camd35 sid@8 BE", buf[8] == 0x00 && buf[9] == 0x64);
		check("camd35 caid@10 BE", buf[10] == 0x18 && buf[11] == 0x84);
		check("camd35 provid@12 BE", buf[12] == 0 && buf[13] == 0 && buf[14] == 0 && buf[15] == 1);
		check("camd35 cwc@18 preserved", buf[18] == 0x88);
		check("camd35 tag@19", buf[19] == 0x81);
		check("camd35 ecmd5@20", memcmp(buf + 20, ecmd5, 16) == 0);
		check("camd35 hash@36 little-endian", buf[36] == 0x0C && buf[37] == 0x9C && buf[38] == 0x66 && buf[39] == 0xA1);
		check("camd35 cw@40", memcmp(buf + 40, cw, 16) == 0);
		check("camd35 nodecount@56", buf[56] == 2);
		check("camd35 node0@57", memcmp(buf + 57, nodes[0], 8) == 0);
		check("camd35 node1@65", memcmp(buf + 65, nodes[1], 8) == 0);
		/* the same payload bytes as the golden frame, field for field */
		check("camd35 payload == golden[4..24) header+fields",
		      memcmp(buf, gold + 4, 20) == 0);
		check("camd35 payload == golden[24..) body",
		      memcmp(buf + 20, gold + 24, (glen - 24) - 20) == 0);
	}

	/* ---- unpack the golden frame (independently built) ---- */
	{
		uint8_t gold[256];
		int glen = golden_camd35(gold, 0x64, 0x1884, 0x000001, 0x88, 0x81,
		                         ecmd5, 0xA1669C0C, cw, 2, nodes);
		check("camd35 unpack golden succeeds", cexw_unpack_camd35_push(gold, glen, &q) == 1);
		check("unpack sid", q.sid == 0x0064);
		check("unpack caid", q.caid == 0x1884);
		check("unpack provid", q.provid == 0x000001);
		check("unpack cwc", q.cwc_cycletime == 0x88);
		check("unpack tag", q.tag == 0x81);
		check("unpack ecmd5", memcmp(q.ecmd5, ecmd5, 16) == 0);
		check("unpack hash LE", q.hash == 0xA1669C0C);
		check("unpack cw", memcmp(q.cw, cw, 16) == 0);
		check("unpack rc", q.rc == CEXW_RC_FOUND);
		check("unpack nnodes", q.nnodes == 2);
		check("unpack node0", memcmp(q.nodes[0], nodes[0], 8) == 0);
		check("unpack node1", memcmp(q.nodes[1], nodes[1], 8) == 0);
		check("unpack rejects a short frame", cexw_unpack_camd35_push(gold, 30, &q) == 0);
	}

	/* ---- the tag rule (module-camd35-cacheex.c:268) ---- */
	check("tag 0x80 kept", CEXW_TAG(0x80) == 0x80);
	check("tag 0x81 kept", CEXW_TAG(0x81) == 0x81);
	check("tag 0x24 collapses to 0", CEXW_TAG(0x24) == 0);
	check("tag 0x00 stays 0", CEXW_TAG(0x00) == 0);

	/* ---- the sender rc rule (module-camd35-cacheex.c:223) ---- */
	check("rc E_FOUND accepted", cexw_rc_ok(CEXW_RC_FOUND));
	check("rc E_UNHANDLED accepted", cexw_rc_ok(CEXW_RC_UNHANDLED));
	check("rc 50 rejected by oscam's rule", !cexw_rc_ok(50));

	/* ---- cccam push layout (module-cccam-cacheex.c:240-310) ---- */
	len = cexw_pack_cccam_push(buf, sizeof(buf), &p);
	check("cccam pack length", len == 20 + 36 + 1 + 16);
	check("cccam caid@0 BE", buf[0] == 0x18 && buf[1] == 0x84);
	check("cccam provid@2 BE", buf[5] == 1);
	check("cccam sid@10 BE", buf[10] == 0x00 && buf[11] == 0x64);
	check("cccam size@12 == 36 (base, 2-byte LE)", buf[12] == 36 && buf[13] == 0);
	check("cccam rc@14 == E_FOUND", buf[14] == 0);
	check("cccam cwc@18", buf[18] == 0x88);
	check("cccam tag@19", buf[19] == 0x81);
	check("cccam ecmd5@20", memcmp(buf + 20, ecmd5, 16) == 0);
	check("cccam hash@36 LE", buf[36] == 0x0C && buf[39] == 0xA1);
	check("cccam cw@40", memcmp(buf + 40, cw, 16) == 0);
	check("cccam nnodes@56", buf[56] == 2);
	check("cccam unpack roundtrip",
	      cexw_unpack_cccam_push(buf, len, &q) == 1 &&
	      q.caid == 0x1884 && q.provid == 1 && q.sid == 0x64 &&
	      q.cwc_cycletime == 0x88 && q.nnodes == 2 &&
	      memcmp(q.cw, cw, 16) == 0 && q.hash == 0xA1669C0C);
	check("cccam MSG ids match oscam", CEXW_CC_PUSH == 0x81 && CEXW_CC_FILTER == 0x82);
	check("camd35 cmd ids match oscam",
	      CEXW_C35_FILTER == 0x3C && CEXW_C35_IDREQ == 0x3D &&
	      CEXW_C35_IDREPLY == 0x3E && CEXW_C35_PUSH == 0x3F);

	/* ---- push-filter tables ---- */
	{
		uint8_t fb[512];
		int n;
		memset(fb, 0, sizeof(fb));
		/* camd35 filter: framed, count at payload 20 (frame 24), 15 slots */
		fb[4 + 20] = 0; fb[4 + 21] = 2;               /* cevnum = 2 */
		/* caids at payload 22: 0x1884, 0x0600 */
		fb[4 + 22 + 2] = 0x18; fb[4 + 22 + 3] = 0x84;
		fb[4 + 22 + 4 + 2] = 0x06;
		/* cmasks (15 slots at frame 86): all 0xFFFFFFFF (no mask) */
		{
			int off = 4 + 22 + 4*15;
			for (i = 0; i < 15; i++) { fb[off + 4*i] = 0xFF; fb[off + 4*i+1] = 0xFF; fb[off + 4*i+2] = 0xFF; fb[off + 4*i+3] = 0xFF; }
		}
		/* prids (frame 146): first = 1, second = -1 (wildcard) */
		{
			int off = 4 + 22 + 4*30;
			fb[off] = 0; fb[off+1] = 0; fb[off+2] = 0; fb[off+3] = 1;
			fb[off + 4] = 0xFF; fb[off + 5] = 0xFF; fb[off + 6] = 0xFF; fb[off + 7] = 0xFF;
		}
		/* srvids (frame 206): all -1 */
		{
			int off = 4 + 22 + 4*45;
			for (i = 0; i < 15; i++) { fb[off + 4*i] = 0xFF; fb[off + 4*i+1] = 0xFF; fb[off + 4*i+2] = 0xFF; fb[off + 4*i+3] = 0xFF; }
		}
		n = cexw_unpack_camd35_filter(fb, 4 + 20 + 2 + 4*4*15, &f);
		check("camd35 filter unpack count", n == 2 && f.n == 2);
		check("camd35 filter caids", f.caid[0] == 0x1884 && f.caid[1] == 0x0600);
		check("camd35 filter prids", f.prid[0] == 1 && f.prid[1] == 0xFFFFFFFFu);
		check("filter match exact", cexw_filter_match(&f, 0x1884, 1, 0x64));
		check("filter match second entry (wildcard prid)", cexw_filter_match(&f, 0x0600, 0x1234, 0x0001));
		check("filter reject unlisted caid", !cexw_filter_match(&f, 0x0100, 1, 0x64));
		check("filter reject wrong provid", !cexw_filter_match(&f, 0x1884, 2, 0x64));
		/* empty table = accept all (oscam: !tab->cevnum -> 1) */
		f.n = 0;
		check("empty filter accepts all", cexw_filter_match(&f, 0x0100, 9, 9));
	}

	/* ---- node list ---- */
	check("node listed (first)", cexw_node_listed(&p, nodes[0]));
	check("node listed (second)", cexw_node_listed(&p, nodes[1]));
	check("node not listed", !cexw_node_listed(&p, nodes[2]));

	printf("test_cexwire done: %d/%d ok\n", okc, okc + failc);
	return failc ? 1 : 0;
}
