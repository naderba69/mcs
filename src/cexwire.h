/*
 * cexwire.h — TASK R17 (D72): the cache-exchange wire, pinned to the
 * OSCAM dialect. Pure module (header-only, gnu89, no allocation), the
 * single source of truth for the push layout, the push-filter table and
 * the filter match rule.
 *
 * WHY THIS EXISTS (the golden rule): every constant and offset below was
 * verified against the oscam sources on 2026-10-12 before adoption --
 *   gfto/oscam  module-camd35-cacheex.c   (camd35 0x3c/0x3d/0x3e/0x3f)
 *   gfto/oscam  module-cccam-cacheex.c    (cccam  MSG_CACHE_PUSH 0x81 /
 *                                           MSG_CACHE_FILTER 0x82)
 *   gfto/oscam  module-cacheex.c          (chk_csp_ctab -- the match)
 *   gfto/oscam  globals.h                 (E_FOUND 0, E_UNHANDLED 100)
 * Nothing here is remembered from folklore: the packers below reproduce
 * the oscam byte layout exactly (test_cexwire pins it), and MultiCS's own
 * historical packers (cacheex.c) are thin wrappers over this module, so
 * what we SEND is by construction what an oscam peer expects to receive.
 *
 * WIRE FRAMING (the one subtlety, measured on the tree):
 *   -- camd35: the SENDER builds the payload here (cmd at buf[0]); the
 *      transport wraps it: [ucrc 4][cmd..rc 4][datacrc 4] + payload[4..].
 *      A RECEIVER therefore sees cmd at buf[4] and every payload field
 *      shifted +4. The unpack below takes the FRAMED buffer, exactly as
 *      the receive sites hold it.
 *   -- cccam: the payload IS the message data (cmd is the cc message id
 *      on the envelope, not inside the buffer). Pack and unpack both use
 *      the payload buffer.
 *
 * BOUNDARIES (documented in COMPAT-ar.md; no invented policy):
 *   -- cwc_cycletime (payload[18]): parsed out and offered to callers,
 *      never invented. MultiCS does not implement cw-cycle stage 3; we
 *      always SEND 0 (= "absent"), which oscam treats as no info.
 *   -- rc (payload[3] camd35 / [14] cccam): we always SEND E_FOUND (0).
 *      On receive we do NOT restrict rc (more permissive than oscam's
 *      E_FOUND|E_UNHANDLED check) -- a documented boundary, not a bug.
 *   -- node list: oscam accumulates the full path (up to 10 nodes);
 *      MultiCS carries [own, immediate source] (stock). The node-loop
 *      option (per peer, default OFF) checks what we carry.
 */
#ifndef MCS_CEXWIRE_H
#define MCS_CEXWIRE_H

#include <stdint.h>
#include <string.h>

/* ---- command ids (camd35 payload cmds; verified: module-camd35-cacheex.c) */
#define CEXW_C35_FILTER      0x3C
#define CEXW_C35_IDREQ       0x3D
#define CEXW_C35_IDREPLY     0x3E
#define CEXW_C35_PUSH        0x3F
/* ---- cccam message ids (verified: module-cccam-data.h) */
#define CEXW_CC_PUSH         0x81
#define CEXW_CC_FILTER       0x82
/* ---- return codes (verified: globals.h) */
#define CEXW_RC_FOUND        0
#define CEXW_RC_UNHANDLED    100

/* tag rule, verified at module-camd35-cacheex.c:268/:328 -- the odd/even
 * byte survives only as 0x80/0x81; anything else collapses to 0. */
#define CEXW_TAG(x)          (((x)&0xFE)==0x80 ? (uint8_t)(x) : (uint8_t)0)

/* camd35 payload offsets (pre-framing). Framed offsets are +4. */
#define CEXW_C35_RC          3
#define CEXW_C35_SID         8
#define CEXW_C35_CAID       10
#define CEXW_C35_PROVID     12
#define CEXW_C35_CWC        18
#define CEXW_C35_TAG        19
#define CEXW_C35_ECMD5      20
#define CEXW_C35_HASH       36   /* 4 bytes, little-endian on the wire */
#define CEXW_C35_CW         40
#define CEXW_C35_NNODES     56
#define CEXW_C35_NODES      57
#define CEXW_C35_MINLEN     (CEXW_C35_NODES + 8)   /* 65: one node */
#define CEXW_C35_HDR        20

/* cccam payload offsets (verified: module-cccam-cacheex.c:245-310) */
#define CEXW_CC_SIZE12      12   /* 2-byte LE, base size = 36 */
#define CEXW_CC_RC          14
#define CEXW_CC_CWC         18
#define CEXW_CC_TAG         19
#define CEXW_CC_ECMD5       20
#define CEXW_CC_HASH        36
#define CEXW_CC_CW          40
#define CEXW_CC_NNODES      56
#define CEXW_CC_NODES       57
#define CEXW_CC_MINLEN      (CEXW_CC_NODES + 8)
#define CEXW_CC_BASESZ      36   /* ecmd5 16 + hash 4 + cw 16 */

#define CEXW_MAX_NODES      10   /* oscam's own cap ("max 10 nodes") */

/* push-filter table: the 0x3c/0x82 payload = u16 BE count, then four
 * arrays of 4-byte BE words (caid[], cmask[], prid[], srvid[]). camd35
 * carries 15 slots starting at payload offset 20; cccam carries 30
 * slots starting at offset 0 (verified: send_push_filter / filter_out). */
#define CEXW_C35_FILT_SLOTS 15
#define CEXW_CC_FILT_SLOTS  30
#define CEXW_FILT_SLOTS     30

struct cexw_filter {
	int      n;
	uint32_t caid[CEXW_FILT_SLOTS];
	uint32_t cmask[CEXW_FILT_SLOTS];
	uint32_t prid[CEXW_FILT_SLOTS];
	uint32_t srvid[CEXW_FILT_SLOTS];
};

struct cexw_push {
	uint16_t sid;
	uint16_t caid;
	uint32_t provid;
	uint8_t  cwc_cycletime;      /* 0 = absent; bit7 = next-cw-cycle flag */
	uint8_t  tag;
	uint8_t  ecmd5[16];
	uint32_t hash;
	uint8_t  cw[16];
	int      rc;
	int      nnodes;
	uint8_t  nodes[CEXW_MAX_NODES][8];
};

/* ---- endian helpers (the wire is mixed: fields BE, hash LE) ---- */
static void cexw_w16be(uint8_t *p, uint16_t v) { p[0]=(uint8_t)(v>>8); p[1]=(uint8_t)v; }
static void cexw_w32be(uint8_t *p, uint32_t v) {
	p[0]=(uint8_t)(v>>24); p[1]=(uint8_t)(v>>16); p[2]=(uint8_t)(v>>8); p[3]=(uint8_t)v;
}
static void cexw_w32le(uint8_t *p, uint32_t v) {
	p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24);
}
static inline uint16_t cexw_r16be(const uint8_t *p) {
	return (uint16_t)(((uint16_t)p[0]<<8) | p[1]);
}
static inline uint32_t cexw_r32be(const uint8_t *p) {
	return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|(uint32_t)p[3];
}
static inline uint32_t cexw_r32le(const uint8_t *p) {
	return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}

/*
 * Pack a camd35 push PAYLOAD (cmd at buf[0]; the transport frames it).
 * Layout verified byte-for-byte against module-camd35-cacheex.c:244-300.
 * The length field is 2-byte LE (buf[1]|buf[2]<<8) and covers everything
 * after the 20-byte header, exactly oscam's `size`.
 */
static inline int cexw_pack_camd35_push(uint8_t *buf, int max, const struct cexw_push *p)
{
	int nn = p->nnodes;
	int i, size;

	if (nn < 1) nn = 1;
	if (nn > CEXW_MAX_NODES) nn = CEXW_MAX_NODES;
	size = 16 + 4 + 16 + 1 + nn * 8;             /* ecmd5+hash+cw+count+nodes */
	if (max < CEXW_C35_HDR + size) return 0;
	memset(buf, 0, CEXW_C35_HDR + size);
	buf[0] = CEXW_C35_PUSH;
	buf[1] = (uint8_t)(size & 0xff);
	buf[2] = (uint8_t)(size >> 8);
	buf[3] = (uint8_t)p->rc;
	cexw_w16be(buf + CEXW_C35_SID, p->sid);
	cexw_w16be(buf + CEXW_C35_CAID, p->caid);
	cexw_w32be(buf + CEXW_C35_PROVID, p->provid);
	buf[CEXW_C35_CWC] = p->cwc_cycletime;
	buf[CEXW_C35_TAG] = CEXW_TAG(p->tag);
	memcpy(buf + CEXW_C35_ECMD5, p->ecmd5, 16);
	cexw_w32le(buf + CEXW_C35_HASH, p->hash);    /* i2b(htonl(hash)) == LE */
	memcpy(buf + CEXW_C35_CW, p->cw, 16);
	buf[CEXW_C35_NNODES] = (uint8_t)nn;
	for (i = 0; i < nn; i++) memcpy(buf + CEXW_C35_NODES + 8*i, p->nodes[i], 8);
	return CEXW_C35_HDR + size;
}

/* Pack a cccam MSG_CACHE_PUSH payload. Verified against
 * module-cccam-cacheex.c:240-310 (same fields, size word at [12]). */
static inline int cexw_pack_cccam_push(uint8_t *buf, int max, const struct cexw_push *p)
{
	int nn = p->nnodes;
	int i, size;

	if (nn < 1) nn = 1;
	if (nn > CEXW_MAX_NODES) nn = CEXW_MAX_NODES;
	size = CEXW_C35_HDR + CEXW_CC_BASESZ + 1 + nn * 8;
	if (max < size) return 0;
	memset(buf, 0, size);
	cexw_w16be(buf + 0, p->caid);
	cexw_w32be(buf + 2, p->provid);
	cexw_w16be(buf + 10, p->sid);
	buf[CEXW_CC_SIZE12] = (uint8_t)(CEXW_CC_BASESZ & 0xff);
	buf[CEXW_CC_SIZE12+1] = (uint8_t)(CEXW_CC_BASESZ >> 8);
	buf[CEXW_CC_RC] = (uint8_t)p->rc;
	buf[CEXW_CC_CWC] = p->cwc_cycletime;
	buf[CEXW_CC_TAG] = CEXW_TAG(p->tag);
	memcpy(buf + CEXW_CC_ECMD5, p->ecmd5, 16);
	cexw_w32le(buf + CEXW_CC_HASH, p->hash);
	memcpy(buf + CEXW_CC_CW, p->cw, 16);
	buf[CEXW_CC_NNODES] = (uint8_t)nn;
	for (i = 0; i < nn; i++) memcpy(buf + CEXW_CC_NODES + 8*i, p->nodes[i], 8);
	return size;
}

/*
 * Unpack a camd35 push from the FRAMED buffer (cmd at buf[4]; fields at
 * their payload offset +4). len = the frame's data length as the sites
 * compute it. Returns 1 on success, 0 if the frame is too short or the
 * node count is absurd. The cwc_cycletime and rc are reported, never
 * acted on (documented boundaries).
 */
static inline int cexw_unpack_camd35_push(const uint8_t *buf, int len, struct cexw_push *p)
{
	int i, nn;

	if (len < 4 + CEXW_C35_MINLEN) return 0;
	memset(p, 0, sizeof(*p));
	p->rc = buf[4 + CEXW_C35_RC];
	p->sid = cexw_r16be(buf + 4 + CEXW_C35_SID);
	p->caid = cexw_r16be(buf + 4 + CEXW_C35_CAID);
	p->provid = cexw_r32be(buf + 4 + CEXW_C35_PROVID);
	p->cwc_cycletime = buf[4 + CEXW_C35_CWC];
	p->tag = CEXW_TAG(buf[4 + CEXW_C35_TAG]);
	memcpy(p->ecmd5, buf + 4 + CEXW_C35_ECMD5, 16);
	p->hash = cexw_r32le(buf + 4 + CEXW_C35_HASH);
	memcpy(p->cw, buf + 4 + CEXW_C35_CW, 16);
	nn = buf[4 + CEXW_C35_NNODES];
	if (nn > CEXW_MAX_NODES) return 0;
	if (nn < 1) nn = 1;
	/* framed len must cover payload[0..56+8*nn) = 4 + 57 + 8*nn */
	if (len < 4 + CEXW_C35_NODES + 8*nn) nn = 1;
	p->nnodes = nn;
	for (i = 0; i < nn; i++) memcpy(p->nodes[i], buf + 4 + CEXW_C35_NODES + 8*i, 8);
	return 1;
}

/* Unpack a cccam MSG_CACHE_PUSH payload (same field map, no framing). */
static inline int cexw_unpack_cccam_push(const uint8_t *buf, int len, struct cexw_push *p)
{
	int i, nn;

	if (len < CEXW_CC_MINLEN) return 0;
	memset(p, 0, sizeof(*p));
	p->caid = cexw_r16be(buf + 0);
	p->provid = cexw_r32be(buf + 2);
	p->sid = cexw_r16be(buf + 10);
	p->rc = buf[CEXW_CC_RC];
	p->cwc_cycletime = buf[CEXW_CC_CWC];
	p->tag = CEXW_TAG(buf[CEXW_CC_TAG]);
	memcpy(p->ecmd5, buf + CEXW_CC_ECMD5, 16);
	p->hash = cexw_r32le(buf + CEXW_CC_HASH);
	memcpy(p->cw, buf + CEXW_CC_CW, 16);
	nn = buf[CEXW_CC_NNODES];
	if (nn > CEXW_MAX_NODES) return 0;
	if (nn < 1) nn = 1;
	if (len < CEXW_CC_NODES + 8*nn) nn = 1;
	p->nnodes = nn;
	for (i = 0; i < nn; i++) memcpy(p->nodes[i], buf + CEXW_CC_NODES + 8*i, 8);
	return 1;
}

/* The sender-side rc rule, verified at module-camd35-cacheex.c:223. */
static inline int cexw_rc_ok(int rc)
{
	return (rc == CEXW_RC_FOUND || rc == CEXW_RC_UNHANDLED) ? 1 : 0;
}

/* ---- push-filter tables (0x3c / 0x82) ---- */

/* camd35: FRAMED buffer; table starts at payload offset 20 (= frame 24),
 * 15 slots. Returns the entry count stored, -1 if the frame is short. */
static inline int cexw_unpack_camd35_filter(const uint8_t *buf, int len, struct cexw_filter *f)
{
	int i, n;
	const uint8_t *p = buf + 4 + 20;

	memset(f, 0, sizeof(*f));
	if (len < 4 + 20 + 2 + 4*4*CEXW_C35_FILT_SLOTS) return -1;
	n = cexw_r16be(p);
	if (n > CEXW_C35_FILT_SLOTS) n = CEXW_C35_FILT_SLOTS;
	p += 2;
	for (i = 0; i < CEXW_C35_FILT_SLOTS; i++) f->caid[i] = cexw_r32be(p + 4*i);
	p += 4*CEXW_C35_FILT_SLOTS;
	for (i = 0; i < CEXW_C35_FILT_SLOTS; i++) f->cmask[i] = cexw_r32be(p + 4*i);
	p += 4*CEXW_C35_FILT_SLOTS;
	for (i = 0; i < CEXW_C35_FILT_SLOTS; i++) f->prid[i] = cexw_r32be(p + 4*i);
	p += 4*CEXW_C35_FILT_SLOTS;
	for (i = 0; i < CEXW_C35_FILT_SLOTS; i++) f->srvid[i] = cexw_r32be(p + 4*i);
	f->n = n;
	return n;
}

/* cccam: payload; count at 0, 30 slots. Returns count, -1 if short. */
static inline int cexw_unpack_cccam_filter(const uint8_t *buf, int len, struct cexw_filter *f)
{
	int i, n;
	const uint8_t *p = buf;

	memset(f, 0, sizeof(*f));
	if (len < 2 + 4*4*CEXW_CC_FILT_SLOTS) return -1;
	n = cexw_r16be(p);
	if (n > CEXW_CC_FILT_SLOTS) n = CEXW_CC_FILT_SLOTS;
	p += 2;
	for (i = 0; i < CEXW_CC_FILT_SLOTS; i++) f->caid[i] = cexw_r32be(p + 4*i);
	p += 4*CEXW_CC_FILT_SLOTS;
	for (i = 0; i < CEXW_CC_FILT_SLOTS; i++) f->cmask[i] = cexw_r32be(p + 4*i);
	p += 4*CEXW_CC_FILT_SLOTS;
	for (i = 0; i < CEXW_CC_FILT_SLOTS; i++) f->prid[i] = cexw_r32be(p + 4*i);
	p += 4*CEXW_CC_FILT_SLOTS;
	for (i = 0; i < CEXW_CC_FILT_SLOTS; i++) f->srvid[i] = cexw_r32be(p + 4*i);
	f->n = n;
	return n;
}

/*
 * The filter match -- chk_csp_ctab() verbatim (module-cacheex.c:978):
 *   no table or caid==0          -> accept all
 *   entry matches when
 *     caid: d==caid || d==caid>>8 || (cmask>=0 && (caid&cmask)==d) || d==-1
 *     prid: -1 wildcard or exact
 *     srvid: -1 wildcard or exact
 *   entries with caid==0 never match (oscam gates on d->caid > 0).
 * Empty received table (n==0) behaves as "accept all" -- the same
 * meaning oscam gives a peer that never sent a filter.
 */
static inline int cexw_filter_match(const struct cexw_filter *f, uint16_t caid,
                             uint32_t prid, uint16_t sid)
{
	int i;

	if (!caid || !f->n) return 1;
	for (i = 0; i < f->n && i < CEXW_FILT_SLOTS; i++) {
		/* oscam: entries are gated by d->caid > 0 as int32 -- values
		 * with the top bit set (including 0xFFFFFFFF == -1) never
		 * match, and the cmask test needs cmask >= 0 as int32. */
		int32_t d  = (int32_t)f->caid[i];
		int32_t mk = (int32_t)f->cmask[i];
		int32_t pr = (int32_t)f->prid[i];
		int32_t sr = (int32_t)f->srvid[i];
		if (d <= 0) continue;
		if (!(d == (int32_t)caid || d == (int32_t)(caid >> 8) ||
		      (mk >= 0 && ((int32_t)caid & mk) == d)))
			continue;
		if (!(pr == -1 || pr == (int32_t)prid)) continue;
		if (!(sr == -1 || sr == (int32_t)sid)) continue;
		return 1;
	}
	return 0;
}

/* node-list membership: does `id` appear among the carried nodes? */
static inline int cexw_node_listed(const struct cexw_push *p, const uint8_t id[8])
{
	int i;
	for (i = 0; i < p->nnodes; i++)
		if (memcmp(p->nodes[i], id, 8) == 0) return 1;
	return 0;
}

#endif /* MCS_CEXWIRE_H */
