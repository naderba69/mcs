
#include "common.h"
#include "cache-origin.h"
#include "peer_origin.h"
#include "cache-pipe-codec.h"
#include "user-trace.h"
#include "cafamily.h"	/* TASK R36 (D92) */
#include "ecm-provider-config.h"  /* Provider-level ECM configuration */
#include "cache-pipe-queue.h"
#include "newcamd-lifetime.h"
#include <stdlib.h>

/* Cache requests can originate while lockecm is held. Serialize their frames
 * here, then let one worker perform the potentially waiting pipe write. */
static cache_pipe_queue_t cache_pipe_out_queue = CACHE_PIPE_QUEUE_INITIALIZER;
static volatile uint32_t cache_pipe_out_dropped;
static int cache_pipe_out_enqueue(const uint8_t *buf, int len)
{
	if (len <= 0 || !cache_pipe_queue_push(&cache_pipe_out_queue, buf, (size_t)len)) {
		__sync_fetch_and_add(&cache_pipe_out_dropped, 1u);
		return 0;
	}
	return 1;
}

#if !defined(THREAD_DCW) && defined(CLI_CSCACHE)
/* No-thread DCW commits hold lockecm. Queue the small value snapshot here so
 * a full ECM pipe cannot stall the commit; the detached worker performs the
 * bounded pipe_write only after the producer has returned to the ECM loop. */
static cache_pipe_queue_t server_cscache_reply_out_queue = CACHE_PIPE_QUEUE_INITIALIZER;

static int cache_queue_server_cscache_reply(uint32_t server_id,
                                            uint64_t request_lifetime_id,
                                            uint16_t caid, uint32_t provid,
                                            uint16_t sid, uint8_t ecm_tag,
                                            const uint8_t cw[16])
{
	return pipe_queue_server_cscache_reply(&server_cscache_reply_out_queue,
	        server_id, request_lifetime_id, caid, provid, sid, ecm_tag, cw);
}

static void *server_cscache_reply_out_thread(void *param)
{
	uint8_t buf[CACHE_PIPE_QUEUE_PAYLOAD_MAX];
	size_t len;
	(void)param;
	prctl(PR_SET_NAME, "ECM Pipe Send", 0, 0, 0);
	while (cache_pipe_queue_pop(&server_cscache_reply_out_queue, buf, &len)) {
		if (pipe_send(prg.pipe.ecm[1], buf, (int)len) < 0 && !prg.restart)
			debugf(DBG_ERROR, " !!! ECM pipe writer could not deliver a queued cached-server reply\n");
	}
	return NULL;
}
#endif

static void *cache_pipe_out_thread(void *param)
{
	uint8_t buf[CACHE_PIPE_QUEUE_PAYLOAD_MAX];
	size_t len;
	uint32_t reported_drops = 0;
	(void)param;
	prctl(PR_SET_NAME, "Cache Pipe Send", 0, 0, 0);
	while (!prg.restart) {
		uint32_t dropped;
		if (!cache_pipe_queue_pop(&cache_pipe_out_queue, buf, &len)) break;
		if (pipe_send(prg.pipe.cache[1], buf, (int)len) < 0 && !prg.restart)
			debugf(DBG_ERROR, " !!! cache pipe writer failed; queued frame could not be delivered\n");
		dropped = __sync_fetch_and_add(&cache_pipe_out_dropped, 0u);
		if (dropped != reported_drops) {
			debugf(DBG_ERROR, " !!! cache pipe outbound queue dropped %u frame(s) under overload\n", dropped);
			reported_drops = dropped;
		}
	}
	return NULL;
}

/* Wake the detached sender when mainprocess exits so shutdown never leaves it
 * asleep forever on an empty queue. Pending frames are irrelevant after restart. */
void cache_pipe_out_shutdown(void)
{
	cache_pipe_queue_close(&cache_pipe_out_queue);
#if !defined(THREAD_DCW) && defined(CLI_CSCACHE)
	cache_pipe_queue_close(&server_cscache_reply_out_queue);
#endif
}

/* Return 0 rather than storing or acknowledging an incomplete HTML escape. */
static int stringtohtml(const char *src, char *dest, size_t capacity)
{
	size_t used = 0;
	if (!capacity) return 0;
	while (*src) {
		const char *replacement = NULL;
		size_t len = 1;
		switch (*src) {
		case '<': replacement = "&lt;"; break;
		case '>': replacement = "&gt;"; break;
		case '&': replacement = "&amp;"; break;
		case '"': replacement = "&quot;"; break;
		default: break;
		}
		if (replacement) len = strlen(replacement);
		if (len >= capacity - used) { dest[0] = 0; return 0; }
		if (replacement) memcpy(dest + used, replacement, len);
		else dest[used] = *src;
		used += len;
		src++;
	}
	dest[used] = 0;
	return 1;
}

/*
void fpeer_update(struct cacheserver_data *cache)
{
	memset( cache->fpeer, 0, sizeof(cache->fpeer) ); // NULL
	struct cachepeer_data *peer = cache->peer;
	while (peer) {
		if ( host_ip_snapshot(peer->host) ) {
			int index = host_ip_snapshot(peer->host) & 0xFF;
			peer->fnext = cache->fpeer[index];
			cache->fpeer[index] = peer;
		}
		peer = peer->next;
	}
}


//// Connected Peers
void ipeer_update(struct cacheserver_data *cache)
{
	cache->peerReq = NULL;
	cache->peerRep = NULL;
	struct cachepeer_data *peer = cache->peer;
	while (peer) {
		if (peer->ping>0) {
			if (peer->flags&FLAG_CACHE_SENDREQ) {
				peer->nextreq = cache->peerReq;
				cache->peerReq = peer;
			}
			if (peer->flags&FLAG_CACHE_SENDREP) {
				peer->nextrep = cache->peerRep;
				cache->peerRep = peer;
			}
		}
		peer = peer->next;
	}
}
*/

// 0: removed. Caller holds prg.lockcache; this may unlink/free a runtime peer.
int peer_doublecheck(struct cacheserver_data *cache, struct cachepeer_data *xpeer)
{
	struct cachepeer_data *peer = cache->peer;
	struct cachepeer_data *previous = NULL;
	while (peer) {
		if (peer != xpeer && peer->port == xpeer->port) {
			uint32_t peer_ip = host_ip_snapshot(peer->host);
			uint32_t xpeer_ip = host_ip_snapshot(xpeer->host);
			if (peer_ip == xpeer_ip) {
				if (peer->runtime) {
					if (previous) previous->next = peer->next;
					else cache->peer = peer->next;
					//close(peer->outsock);
					free(peer);
					return 0;
				}
				else if (xpeer->runtime) peer_doublecheck(cache, peer);
				return 1;
			}
		}
		previous = peer;
		peer = peer->next;
	}
	return 1;
}



///////////////////////////////////////////////////////////////////////////////
//
///////////////////////////////////////////////////////////////////////////////

/*
 * TASK 1.1 — the origin flags and the id mask now live in peer_origin.h, so
 * that they have one definition and can be unit-tested. This file is
 * #included by main.c, which includes the header first.
 */
#include "peer_origin.h"
#include "cacheex_gates.h"	/* TASK R1: the exchange-gate predicates */
#include "dcwstats.h"		/* TASK R1: CEXLOCAL/CEXFAKE/CEXCONF counters */
#include "peerrep.h"		/* TASK R4 (D58): the persistent peer-reputation ladder */
#include "cwconsensus.h"/* TASK R8 (D62): the per-request cw arbitrer */
#include "cache-intelligence.h" /* CACHE-AI: 7-level smart cache optimization */
#ifdef MCS_DCWSTATS
#include "cwlog.h"		/* TASK R11 (D65): the per-CW verdict ring */
#endif

/* Borrowed peer pointers are valid only while the caller holds lockcache. */
struct cachepeer_data *getpeerbyaddr(struct cacheserver_data *cache, uint32_t ip, uint16_t port)
{
	struct cachepeer_data *peer = cache->peer;
	while(peer) {
		if ( (host_ip_snapshot(peer->host)==ip)&&(peer->recvport==port) ) return peer;
		peer = peer->next;
	}
	return NULL;
}

/* Borrowed peer pointers are valid only while the caller holds lockcache. */
struct cachepeer_data *getpeerbyid(int id)
{
	/*
	 * TASK 1.1 — strip the origin flags. `id` may arrive as a bare peer id or
	 * as `peerid | PEER_CSP` and friends; masking here makes both work and
	 * leaves callers that already masked unaffected.
	 */
	id = peer_origin_id(id);
	struct cacheserver_data *cache = cfg.cache.server;
	while (cache) {
		struct cachepeer_data *peer = cache->peer;
		while(peer) {
			if (peer->id==id) return peer;
			peer = peer->next;
		}
		cache = cache->next;
	}
	return NULL;
}

///////////////////////////////////////////////////////////////////////////////
// Multics ID encryption
///////////////////////////////////////////////////////////////////////////////

static unsigned char T1[]={
  0x2a,0xe1,0x0b,0x13,0x3e,0x6e,0x32,0x48,
  0xd3,0x31,0x08,0x8c,0x8f,0x95,0xbd,0xd0,
  0xe4,0x6d,0x50,0x81,0x20,0x30,0xbb,0x75,
  0xf5,0xd4,0x7c,0x87,0x2c,0x4e,0xe8,0xf4,
  0xbe,0x24,0x9e,0x4d,0x80,0x37,0xd2,0x5f,
  0xdb,0x04,0x7a,0x3f,0x14,0x72,0x67,0x2d,
  0xcd,0x15,0xa6,0x4c,0x2e,0x3b,0x0c,0x41,
  0x62,0xfa,0xee,0x83,0x1e,0xa2,0x01,0x0e,
  0x7f,0x59,0xc9,0xb9,0xc4,0x9d,0x9b,0x1b,
  0x9c,0xca,0xaf,0x3c,0x73,0x1a,0x65,0xb1,
  0x76,0x84,0x39,0x98,0xe9,0x53,0x94,0xba,
  0x1d,0x29,0xcf,0xb4,0x0d,0x05,0x7d,0xd1,
  0xd7,0x0a,0xa0,0x5c,0x91,0x71,0x92,0x88,
  0xab,0x93,0x11,0x8a,0xd6,0x5a,0x77,0xb5,
  0xc3,0x19,0xc1,0xc7,0x8e,0xf9,0xec,0x35,
  0x4b,0xcc,0xd9,0x4a,0x18,0x23,0x9f,0x52,
  0xdd,0xe3,0xad,0x7b,0x47,0x97,0x60,0x10,
  0x43,0xef,0x07,0xa5,0x49,0xc6,0xb3,0x55,
  0x28,0x51,0x5d,0x64,0x66,0xfc,0x44,0x42,
  0xbc,0x26,0x09,0x74,0x6f,0xf7,0x6b,0x4f,
  0x2f,0xf0,0xea,0xb8,0xae,0xf3,0x63,0x6a,
  0x56,0xb2,0x02,0xd8,0x34,0xa4,0x00,0xe6,
  0x58,0xeb,0xa3,0x82,0x85,0x45,0xe0,0x89,
  0x7e,0xfd,0xf2,0x3a,0x36,0x57,0xff,0x06,
  0x69,0x54,0x79,0x9a,0xb6,0x6c,0xdc,0x8b,
  0xa7,0x1f,0x90,0x03,0x17,0x1c,0xed,0xd5,
  0xaa,0x5e,0xfe,0xda,0x78,0xb0,0xbf,0x12,
  0xa8,0x22,0x21,0x3d,0xc2,0xc0,0xb7,0xa9,
  0xe7,0x33,0xfb,0xf1,0x70,0xe5,0x17,0x96,
  0xf8,0x8d,0x46,0xa1,0x86,0xe2,0x40,0x38,
  0xf6,0x68,0x25,0x16,0xac,0x61,0x27,0xcb,
  0x5b,0xc8,0x2b,0x0f,0x99,0xde,0xce,0xc5
};

static unsigned char T2[]={
  0xbf,0x11,0x6d,0xfa,0x26,0x7f,0xf3,0xc8,
  0x9e,0xdd,0x3f,0x16,0x97,0xbd,0x08,0x80,
  0x51,0x42,0x93,0x49,0x5b,0x64,0x9b,0x25,
  0xf5,0x0f,0x24,0x34,0x44,0xb8,0xee,0x2e,
  0xda,0x8f,0x31,0xcc,0xc0,0x5e,0x8a,0x61,
  0xa1,0x63,0xc7,0xb2,0x58,0x09,0x4d,0x46,
  0x81,0x82,0x68,0x4b,0xf6,0xbc,0x9d,0x03,
  0xac,0x91,0xe8,0x3d,0x94,0x37,0xa0,0xbb,
  0xce,0xeb,0x98,0xd8,0x38,0x56,0xe9,0x6b,
  0x28,0xfd,0x84,0xc6,0xcd,0x5f,0x6e,0xb6,
  0x32,0xf7,0x0e,0xf1,0xf8,0x54,0xc1,0x53,
  0xf0,0xa7,0x95,0x7b,0x19,0x21,0x23,0x7d,
  0xe1,0xa9,0x75,0x3e,0xd6,0xed,0x8e,0x6f,
  0xdb,0xb7,0x07,0x41,0x05,0x77,0xb4,0x2d,
  0x45,0xdf,0x29,0x22,0x43,0x89,0x83,0xfc,
  0xd5,0xa4,0x88,0xd1,0xf4,0x55,0x4f,0x78,
  0x62,0x1e,0x1d,0xb9,0xe0,0x2f,0x01,0x13,
  0x15,0xe6,0x17,0x6a,0x8d,0x0c,0x96,0x7e,
  0x86,0x27,0xa6,0x0d,0xb5,0x73,0x71,0xaa,
  0x36,0xd0,0x06,0x66,0xdc,0xb1,0x2a,0x5a,
  0x72,0xbe,0x3a,0xc5,0x40,0x65,0x1b,0x02,
  0x10,0x9f,0x3b,0xf9,0x2b,0x18,0x5c,0xd7,
  0x12,0x47,0xef,0x1a,0x87,0xd2,0xc2,0x8b,
  0x99,0x9c,0xd3,0x57,0xe4,0x76,0x67,0xca,
  0x3c,0xfb,0x90,0x20,0x14,0x48,0xc9,0x60,
  0xb0,0x70,0x4e,0xa2,0xad,0x35,0xea,0xc4,
  0x74,0xcb,0x39,0xde,0xe7,0xd4,0xa3,0xa5,
  0x04,0x92,0x8c,0xd9,0x7c,0x1c,0x7a,0xa8,
  0x52,0x79,0xf2,0x33,0xba,0x1f,0x30,0x9a,
  0x00,0x50,0x4c,0xff,0xe5,0xcf,0x59,0xc3,
  0xe3,0x0a,0x85,0xb3,0xae,0xec,0x0b,0xfe,
  0xe2,0xab,0x4a,0xaf,0x69,0x6c,0x2c,0x5d
};

#define SN(b) (((b&0xf0)>>4)+((b&0xf)<<4))

static void fase(unsigned char *k,unsigned char *D)
{
	unsigned char l,dt; // paso 1 

	for(l=0;l<4;++l) D[l]^=k[l];  // paso 2 

	for(l=0;l<4;++l) D[l]=T1[D[l]];

	for(l=6;l>3;--l) { 
		D[(l+2)&3]^=D[(l+1)&3]; 
		dt=(SN(D[(l+1)&3])+D[l&3])&0xff; 
		D[l&3]=T2[dt];
	} 
	for(l=3;l>0;--l) {
		D[(l+2)&3]^=D[(l+1)&3]; 
		D[l&3]=T1[(SN(D[(l+1)&3])+D[l&3])&0xff]; 
	} 
	D[2]^=D[1]; 
	D[1]^=D[0]; 
}


// Packet Encryption

void encryptcache(uint8_t *buf, int len)
{
	int i;
	for (i=1; i<len; i++) buf[i] = (buf[i]+i) & 0xff;
}

void decryptcache(uint8_t *buf, int len)
{
	int i;
	for (i=1; i<len; i++) buf[i] = (0xff00+buf[i]-i) & 0xff;
}

///////////////////////////////////////////////////////////////////////////////
// 
///////////////////////////////////////////////////////////////////////////////

#define CACHE_SENT_NONE     0
#define CACHE_SENT_REQUEST  1
#define CACHE_SENT_REPLY    2

#define BIT_CACHE_NEWPROTO  0x01
#define BIT_CACHE_HACK      0x10
#define BIT_CACHE_REQPING   0x80

//	uint8_t status; // 0:Wait; 1: dcw received
#define CACHE_FLAG_DCW        0x01
//	int sendpipe; // flag send dcw to ecmpipe
#define CACHE_FLAG_SENDPIPE   0x02
// Request Sent
#define CACHE_FLAG_REQSENT    0x04
// Reply Sent
#define CACHE_FLAG_REPSENT    0x08
// Forward cache to peers
#define CACHE_FLAG_FWD        0x10
// Cacheex Reply Sent
#define CACHEEX_FLAG_REPSENT  0x20


typedef enum { NO_CYCLE=0, CW0CYCLE=1, CW1CYCLE=2} cwcycle_t;

#define DCW_ERROR    0x01
#define DCW_CYCLE    0x02
#define DCW_CHECKED  0x04
#define DCW_SKIP     0x08
#define DCW_SENT     0x10
/* Explicitly passed source gates for serving peers; not evidence of picture. */
#define DCW_PEER_READY 0x20
#define DCW_LOCAL_SOURCE 0x40

struct cw_cache_data {
	struct cw_cache_data *next;
	uint8_t cw[16]; // for storing all codes
	uint32_t cwsum; // Checksum
	uint8_t status;
	cwcycle_t cwcycle; // cw1='1' / cw0='0'
	uint32_t peerid; // fisrt peerid 
	uint16_t nbpeers; // number of peers reporting this dcw
	/* Internal provenance only; never serialized to a peer protocol. */
	uint8_t origin_mask;
};


struct __attribute__ ((__packed__)) cache_data {
	struct cache_data *next; // main list
	struct cache_data *prev; // previous

	uint8_t flags;
	uint32_t recvtime;
	// CSP
	uint8_t tag;
	uint16_t sid;
	uint16_t onid;
	uint16_t caid;
	uint32_t hash; // Non-NULL
	//
	uint32_t provid;
	cwcycle_t cwcycle; // ecmtag when cw1 cycle
	uint8_t prevcw[16];
#ifdef CACHEEX
	uint8_t ecmd5[16];
#endif

	struct cw_cache_data *cwdata;

	ECM_DATA *ecm;
	/* Exact request generation paired with ecm. The ECM pool reuses storage,
	 * so a pointer/key tuple alone cannot safely identify a queued waiter. */
	uint64_t ecm_lifetime_id;
	/* A local request's numeric provider, distinct from the cache entry's
	 * provider (which incoming messages may overwrite or leave unknown).
	 * This association lasts until the entry is recycled, including after
	 * its waiter finishes. It does not authenticate the provider or the CW. */
	uint8_t local_provider_set;
	uint32_t local_provider;
	/* `provid == 0` can be either a real provider value or absent on CSP. */
	uint8_t provider_known;
};

/* Classify only proven ingress identities. A local card-server answer is
 * the line anchor; CSP is the request/reply peer class; CacheEX clients and
 * servers deliver pushed/batch candidates. Do not infer provenance for bare
 * source ids or malformed combinations of origin bits. */
static uint8_t cache_origin_for_source(int srctype, int srcid)
{
	int origin;
	if (srctype == DCW_SOURCE_SERVER) return CACHE_ORIGIN_LINE_SERVER;
	if (srctype != DCW_SOURCE_CACHE) return 0;
	origin = peer_origin_flag(srcid);
	switch (origin) {
	case PEER_CSP:
		return CACHE_ORIGIN_PEER;
	case PEER_CCCAM_CLIENT:
	case PEER_CAMD35_CLIENT:
	case PEER_CS378X_CLIENT:
	case PEER_CACHEEX_SERVER:
		return CACHE_ORIGIN_BATCH;
	default:
		return 0;
	}
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

int cache_check( struct cache_data *req )
{
	if ( ((req->tag&0xFE)!=0x80)||!req->caid||!req->hash||!req->sid ) return 0;
	if (cfg.cache.caids[0]) {
		int i;
		for(i=0; i<32; i++) {
			if (!cfg.cache.caids[i]) break;
			if (cfg.cache.caids[i]==req->caid) return 1;
		}
		return 0;
	}
	//if (!cfg.cache.faccept0onid && !req->onid ) return 0;
	return 1;
}

int cache_check_request( unsigned char tag, unsigned short sid, unsigned short onid, unsigned short caid, unsigned int hash )
{
	if ( ((tag&0xFE)!=0x80)||!caid||!hash ) return 0;
	//if (!cfg.cache.faccept0onid && !onid ) return 0;
	return 1;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

#define MAX_CACHE_INDEX  0x3FF


////typedef struct cache_data t_cachetab[MAX_CACHE_INDEX+1];


struct cache_list {
	struct cache_list *next;
	//struct cache_data *cachetab[MAX_CACHE_INDEX+1];
	unsigned short caid;
	struct cache_data **cachetab;
};


struct cache_list *cachelist;

struct cache_data **getcachetabbycaid(unsigned short caid)
{
	// look for availabe tab
	struct cache_list *x = cachelist;
	while (x) {
		if (x->caid==caid) {
			return x->cachetab;
		}
		x = x->next;
	}
	// if not found add new one
	struct cache_list *newdata = malloc( sizeof(struct cache_list) );
	/*
	 * TASK 3.4 -- allocation can fail, and every caller dereferences the
	 * returned table. The node and the 1024-slot table are checked here, once,
	 * at the only place they are born (~94 KiB per table in this x64 CACHEEX
	 * build). On failure, the table for this CAID simply does not exist yet;
	 * the caller drops the one
	 * request it was holding (GR9: nothing blocks, nothing queues), and the
	 * next request retries the allocation. The node is linked into cachelist
	 * only after its table exists, so a half-built node can never be found
	 * and dereferenced by a later call.
	 */
	if (!newdata) {
		debugf(DBG_ERROR, " !!! CACHE: out of memory for cache list (caid %04X)\n", caid);
		return NULL;
	}
	newdata->next = cachelist;
	newdata->caid = caid;
	newdata->cachetab = malloc( sizeof(struct cache_data) * (MAX_CACHE_INDEX+1) );
	if (!newdata->cachetab) {
		debugf(DBG_ERROR, " !!! CACHE: out of memory for cache table (caid %04X)\n", caid);
		free(newdata);
		return NULL;
	}
	cachelist = newdata;
	memset( newdata->cachetab, 0, sizeof(struct cache_data) * (MAX_CACHE_INDEX+1) );
	//printf(" New cache list for caid = %02X\n", caid);
	return	newdata->cachetab;
}

/*
 * TASK 1.5 — pull in cache_purge_sweep() here rather than from main.c.
 *
 * The sweep needs struct cache_data, struct cw_cache_data, MAX_CACHE_INDEX and
 * getcachetabbycaid(), all of which are defined above this point but *after*
 * every #include in main.c. Re-including the header with the guard set is what
 * puts the function inside this scope without moving any definition, and it
 * keeps DCW_ERROR (line 286) a single source of truth -- the sweep takes the
 * mark bit as a parameter instead of redefining 0x01 somewhere else.
 */
#define MCS_CACHE_PURGE_WALK
#include "cache_purge.h"


/* A locally attached provider is evidence of a numeric conflict, not proof
 * of a CSP CW's origin. Unknown associations retain the historical behavior. */
static int cache_local_providers_conflict(const struct cache_data *a,
                                          const struct cache_data *b)
{
    return a->local_provider_set && b->local_provider_set &&
           a->local_provider != b->local_provider;
}

/* An explicit provider value is distinct from a provider-less CSP wire key
 * whose numeric field is zero. Never treat a differing known local/cache value
 * as equivalent; `provider_known` distinguishes a real zero from no metadata. */
static int cache_provider_identity_conflicts(const struct cache_data *entry,
                                             uint32_t provider,
                                             int provider_known)
{
    if (!entry || !provider_known) return 0;
    if (entry->local_provider_set && entry->local_provider != provider) return 1;
    return (entry->provider_known || entry->provid != 0) && entry->provid != provider;
}

/* Compare every provider value that is actually known on both cache records.
 * Unknown CSP metadata remains ambiguous and preserves the old wildcard rule;
 * an explicit zero, however, is a real numeric value and must match. */
static int cache_provider_records_conflict(const struct cache_data *a,
                                           const struct cache_data *b)
{
    if (!a || !b) return 0;
    if (cache_local_providers_conflict(a, b)) return 1;
    if (cache_provider_identity_conflicts(a, b->local_provider, b->local_provider_set)) return 1;
    if (cache_provider_identity_conflicts(a, b->provid,
                                          b->provider_known || b->provid != 0)) return 1;
    if (cache_provider_identity_conflicts(b, a->local_provider, a->local_provider_set)) return 1;
    if (cache_provider_identity_conflicts(b, a->provid,
                                          a->provider_known || a->provid != 0)) return 1;
    return 0;
}

/* Consensus uses the cache record's effective provider scope. A known local
 * ECM value wins for provider-less CSP; otherwise use explicit cache metadata.
 * A providerless request and an explicit numeric zero remain distinct. */
static int cache_consensus_provider_scope(const struct cache_data *entry,
                                          uint32_t *provider)
{
    if (provider) *provider = 0;
    if (!entry) return 0;
    if (entry->local_provider_set) {
        if (provider) *provider = entry->local_provider;
        return 1;
    }
    if (entry->provider_known || entry->provid != 0) {
        if (provider) *provider = entry->provid;
        return 1;
    }
    return 0;
}

static int cache_request_provider_known(const struct cache_data *request)
{
    return request && (request->provider_known || request->provid != 0);
}

/* Attach local provider identity without letting an unknown zero erase known
 * cache metadata or a previous local association. */
static void cache_attach_local_provider(struct cache_data *entry,
                                        const struct cache_data *request)
{
    if (!entry || !cache_request_provider_known(request)) return;
    entry->local_provider_set = 1;
    entry->local_provider = request->provid;
    entry->provider_known = 1;
    entry->provid = request->provid;
}

/* Cache timestamps are 32-bit milliseconds. Compare elapsed time rather than
 * absolute deadlines, which wrap at startup and every ~49.7 days. The cache
 * lifetime and filter window are far below half the counter range. */
static int cache_time_expired(uint32_t now, uint32_t then, uint32_t lifetime)
{
    return (uint32_t)(now - then) > lifetime;
}

static int cache_time_older_than(uint32_t request, uint32_t entry, uint32_t window)
{
    uint32_t elapsed = request - entry;
    return elapsed <= INT32_MAX && elapsed > window;
}

struct cache_data *cache_new( struct cache_data *newdata )
{
	int index = newdata->sid&MAX_CACHE_INDEX;
	struct cache_data **cachetab = getcachetabbycaid(newdata->caid);
	/* TASK 3.4: no table for this CAID (allocation failed) -- the caller sees
	 * NULL and drops this one event; nothing here may dereference cachetab. */
	if (!cachetab) return NULL;
	struct cache_data *pcache = cachetab[index];
	struct cache_data *new;
	uint32_t ticks = GetTickCount();
	// add new or use dead data
	if (!pcache) {
		// nothing so add new
		//debugf(0," first data for index = %02X\n", index);
		new = malloc( sizeof(struct cache_data) );
		/* TASK 3.4: the bucket stays empty; the caller drops this event. */
		if (!new) return NULL;
		memset( new, 0,  sizeof(struct cache_data) );
		new->next = NULL;
		new->prev = NULL;
		cachetab[index] = new; // it becomes the current one
	}
	else if (!pcache->next) {
		//debugf(0," second data for index = %02X\n", index);
		new = malloc( sizeof(struct cache_data) );
		/* TASK 3.4: the bucket keeps its one live entry; the caller drops this event. */
		if (!new) return NULL;
		memset( new, 0,  sizeof(struct cache_data) );
		// add new
		new->prev = cachetab[index];
		new->next = cachetab[index];
		// update previous
		cachetab[index]->next = new;
		// update next
		cachetab[index]->prev = new;
		//
		cachetab[index] = new; // it becomes the current one
	}
	else {
		new = cachetab[index]->prev;
		if ( cache_time_expired(ticks, new->recvtime, cfg.cache.alivetime) ) {
			// cache is dead, add data to this one without allocating new data
			// free old dcw
			struct cw_cache_data *cwdata = new->cwdata;
			while (cwdata) {
				struct cw_cache_data *tmp = cwdata;
				cwdata = cwdata->next;
				free( tmp );
			}
			//debugf(0," reuse data for index = %02X\n", index);
			pcache = new->prev; // store previous
			memset( new, 0,  sizeof(struct cache_data) );
			new->prev = pcache;
			new->next = cachetab[index];
			cachetab[index] = new; // it becomes the current one
		}
	else { // allocate new data
		//debugf(getdbgflag(DBG_CACHE,0,0)," new data for index = %02X\n", index);
		new = malloc( sizeof(struct cache_data) );
		/* TASK 3.4: the bucket keeps its live entries; the caller drops this event. */
		if (!new) return NULL;
		memset( new, 0, sizeof(struct cache_data) );
			// add new in list
			new->prev = cachetab[index]->prev;
			new->next = cachetab[index];
			// update previous
			cachetab[index]->prev->next = new;
			// update next
			cachetab[index]->prev = new;
			//
			cachetab[index] = new; // it becomes the current one			
		}
	}
	pcache = cachetab[index];
	//pcache->status = CACHE_STAT_WAIT; // 0:Wait; 1: dcw received
	pcache->recvtime = ticks;
	pcache->tag = newdata->tag;
	pcache->sid = newdata->sid;
	pcache->onid = newdata->onid;
	pcache->caid = newdata->caid;
	pcache->hash = newdata->hash;
	pcache->provid = newdata->provid;
	pcache->provider_known = newdata->provider_known || newdata->provid != 0;
#ifdef CACHEEX
	memcpy( pcache->ecmd5, newdata->ecmd5, 16);
#endif
	return pcache;
}

struct cache_data *cache_fetch( struct cache_data *thereq )
{
	int index = thereq->sid&MAX_CACHE_INDEX;
	struct cache_data **cachetab = getcachetabbycaid(thereq->caid);
	/* TASK 3.4: no table for this CAID (allocation failed) -- nothing cached. */
	if (!cachetab) return NULL;
	struct cache_data *pcache = cachetab[index];
	uint32_t ticks = GetTickCount();
	while (pcache) {
		if ( cache_time_expired(ticks, pcache->recvtime, cfg.cache.alivetime) ) return NULL;
		if ( (pcache->hash==thereq->hash)&&(pcache->sid==thereq->sid) )
			if ( (pcache->tag==thereq->tag) || !pcache->tag || !thereq->tag ) return pcache;
		pcache = pcache->next;
		if (pcache==cachetab[index]) break;
	}
	return NULL;
}


char *cwcycle2str( cwcycle_t cwcycle )
{
	if (cwcycle==CW0CYCLE) return "CW0CYCLE";
	if (cwcycle==CW1CYCLE) return "CW1CYCLE";
	else return "NO_CYCLE";
}


/*
 * Cache workers need only two ECM facts: whether a waiter is still live and
 * which profile was associated with it. Keep those in a bounded side registry
 * under lockcache instead of extending/re-reading ECM_DATA while holding the
 * cache lock. ECM-side publication follows the established lockecm -> lockcache
 * order; cache-side readers never acquire lockecm.
 */
#define CACHE_ECM_SNAPSHOT_SLOTS 16384u
#define CACHE_ECM_SNAPSHOT_EMPTY 0u
#define CACHE_ECM_SNAPSHOT_USED  1u
#define CACHE_ECM_SNAPSHOT_TOMB  2u

struct cache_ecm_snapshot {
	ECM_DATA *ecm;
	uint64_t lifetime_id;
	uint32_t recvtime;
	struct cardserver_data *profile;
#ifdef CACHEEX
	struct ledger_identity ledger_identity;
	uint8_t ledger_identity_valid;
#endif
	uint8_t active;
	uint8_t state;
};

static struct cache_ecm_snapshot cache_ecm_snapshots[CACHE_ECM_SNAPSHOT_SLOTS];

static unsigned cache_ecm_snapshot_hash(const ECM_DATA *ecm)
{
	uintptr_t value = (uintptr_t)ecm;
	value >>= 3;
	value ^= value >> 11;
	value *= (uintptr_t)0x9e3779b1u;
	return (unsigned)(value & (CACHE_ECM_SNAPSHOT_SLOTS - 1u));
}

/* lockcache is held. Inactive records may be reclaimed while preserving the
 * open-addressing probe chain with a tombstone. One record per ECM storage
 * pointer is enough because a reused slot publishes its new lifetime in place. */
static int cache_ecm_snapshot_slot_locked(ECM_DATA *ecm, int insert,
                                         unsigned *slot_out, int *reclaim_out)
{
	unsigned start, probe;
	int first_tomb = -1, first_inactive = -1;
	if (!ecm || !slot_out) return 0;
	start = cache_ecm_snapshot_hash(ecm);
	for (probe = 0; probe < CACHE_ECM_SNAPSHOT_SLOTS; probe++) {
		unsigned pos = (start + probe) & (CACHE_ECM_SNAPSHOT_SLOTS - 1u);
		struct cache_ecm_snapshot *entry = &cache_ecm_snapshots[pos];
		if (entry->state == CACHE_ECM_SNAPSHOT_USED) {
			if (entry->ecm == ecm) {
				*slot_out = pos;
				if (reclaim_out) *reclaim_out = 0;
				return 1;
			}
			if (!entry->active && first_inactive < 0) first_inactive = (int)pos;
		} else if (entry->state == CACHE_ECM_SNAPSHOT_TOMB) {
			if (first_tomb < 0) first_tomb = (int)pos;
		} else {
			if (!insert) return 0;
			/* Preserve queued inactive snapshots whenever an empty slot still
			 * exists. They may be the only proof for a serialized cache reply
			 * not yet drained from the pipe. Reclaim one only after a full scan. */
			if (first_tomb >= 0) {
				*slot_out = (unsigned)first_tomb;
				if (reclaim_out) *reclaim_out = 1;
			} else {
				*slot_out = pos;
				if (reclaim_out) *reclaim_out = 0;
			}
			return 0;
		}
	}
	if (!insert) return 0;
	if (first_tomb >= 0) {
		*slot_out = (unsigned)first_tomb;
		if (reclaim_out) *reclaim_out = 1;
		return 0;
	}
	if (first_inactive >= 0) {
		*slot_out = (unsigned)first_inactive;
		if (reclaim_out) *reclaim_out = 1;
		return 0;
	}
	return 0;
}

static int cache_ecm_snapshot_record(ECM_DATA *ecm, struct cardserver_data *cs,
                                    int active)
{
	uint64_t lifetime_id;
	unsigned slot;
	int found;
#ifdef CACHEEX
	struct ledger_identity ledger_identity;
	int ledger_identity_valid;
#endif
	if (!ecm || !cs) return 0;
#ifdef CACHEEX
	ledger_identity_valid = ledger_identity_from_ecm(&ledger_identity, ecm);
	if (ledger_identity_valid)
		ledger_identity.cache_evidence_enabled =
		    (uint8_t)!!cs->option.cache_evidence;
#endif
	/* Caller holds lockecm (including the reload-exclusive variant), so this
	 * direct ECM read is stable. Registry protection is lockcache only. */
	lifetime_id = ecm_request_lifetime_id_load(ecm);
	if (!lifetime_id) return 0;
	pthread_mutex_lock(&prg.lockcache);
	slot = CACHE_ECM_SNAPSHOT_SLOTS;
	found = cache_ecm_snapshot_slot_locked(ecm, 1, &slot, NULL);
	/* A saturated table with no inactive/tombstone slot is fail-closed: the
	 * caller skips cache IPC and normal server fallback still proceeds. */
	if (!found && slot >= CACHE_ECM_SNAPSHOT_SLOTS) {
		pthread_mutex_unlock(&prg.lockcache);
		return 0;
	}
	if (!found || cache_ecm_snapshots[slot].lifetime_id != lifetime_id) {
		memset(&cache_ecm_snapshots[slot], 0, sizeof(cache_ecm_snapshots[slot]));
	}
	cache_ecm_snapshots[slot].ecm = ecm;
	cache_ecm_snapshots[slot].lifetime_id = lifetime_id;
	cache_ecm_snapshots[slot].recvtime = ecm->recvtime;
	cache_ecm_snapshots[slot].profile = cs;
#ifdef CACHEEX
	cache_ecm_snapshots[slot].ledger_identity_valid = !!ledger_identity_valid;
	if (ledger_identity_valid)
		cache_ecm_snapshots[slot].ledger_identity = ledger_identity;
	else
		memset(&cache_ecm_snapshots[slot].ledger_identity, 0,
		       sizeof(cache_ecm_snapshots[slot].ledger_identity));
#endif
	cache_ecm_snapshots[slot].active = !!active;
	cache_ecm_snapshots[slot].state = CACHE_ECM_SNAPSHOT_USED;
	pthread_mutex_unlock(&prg.lockcache);
	return 1;
}

static int cache_ecm_snapshot_publish(ECM_DATA *ecm, struct cardserver_data *cs)
{
	return cache_ecm_snapshot_record(ecm, cs, 1);
}

/* Called from the ECM-side status transition before the plain dcwstatus write.
 * It does not inspect that status or any other mutable ECM field. */
void cache_ecm_snapshot_mark_inactive(ECM_DATA *ecm)
{
	uint64_t lifetime_id;
	unsigned slot;
	if (!ecm) return;
	lifetime_id = ecm_request_lifetime_id_load(ecm);
	if (!lifetime_id) return;
	pthread_mutex_lock(&prg.lockcache);
	if (cache_ecm_snapshot_slot_locked(ecm, 0, &slot, NULL) &&
	    cache_ecm_snapshots[slot].lifetime_id == lifetime_id)
		cache_ecm_snapshots[slot].active = 0;
	pthread_mutex_unlock(&prg.lockcache);
}

/* A queued pointer is only safe to dereference while it names the exact pool
 * generation registered by an ECM-side producer. Retire that identity under
 * lockcache before the ECM slot is cleared and assigned a new lifetime. */
void cache_ecm_snapshot_retire(ECM_DATA *ecm)
{
	uint64_t lifetime_id;
	unsigned slot;
	if (!ecm) return;
	lifetime_id = ecm_request_lifetime_id_load(ecm);
	if (!lifetime_id) return;
	pthread_mutex_lock(&prg.lockcache);
	if (cache_ecm_snapshot_slot_locked(ecm, 0, &slot, NULL) &&
	    cache_ecm_snapshots[slot].lifetime_id == lifetime_id) {
		cache_ecm_snapshots[slot].active = 0;
		cache_ecm_snapshots[slot].lifetime_id = 0;
		cache_ecm_snapshots[slot].profile = NULL;
#ifdef CACHEEX
		cache_ecm_snapshots[slot].ledger_identity_valid = 0;
		memset(&cache_ecm_snapshots[slot].ledger_identity, 0,
		       sizeof(cache_ecm_snapshots[slot].ledger_identity));
#endif
	}
	pthread_mutex_unlock(&prg.lockcache);
}

/* lockcache is held by every caller of the functions below. */
static int cache_ecm_snapshot_read_locked(ECM_DATA *ecm, uint64_t lifetime_id,
                                          int *active_out,
                                          struct cardserver_data **profile_out,
                                          uint32_t *recvtime_out)
{
	unsigned slot;
	if (active_out) *active_out = 0;
	if (profile_out) *profile_out = NULL;
	if (recvtime_out) *recvtime_out = 0;
	if (!ecm || !lifetime_id ||
	    !cache_ecm_snapshot_slot_locked(ecm, 0, &slot, NULL)) return 0;
	if (cache_ecm_snapshots[slot].lifetime_id != lifetime_id) return 0;
	if (active_out) *active_out = cache_ecm_snapshots[slot].active;
	if (profile_out) *profile_out = cache_ecm_snapshots[slot].profile;
	if (recvtime_out) *recvtime_out = cache_ecm_snapshots[slot].recvtime;
	return 1;
}

#ifdef CACHEEX
/* Copy exact request identity from the side registry. Cache-side code must not
 * dereference a cache_data ECM pointer; this additionally requires a live
 * waiter so a stale cache entry cannot attribute a source offer to a later
 * request that merely reused the same cache key. */
static int cache_ecm_snapshot_ledger_identity_locked(
	ECM_DATA *ecm, uint64_t lifetime_id, struct ledger_identity *identity_out)
{
	unsigned slot;
	if (!identity_out ||
	    !cache_ecm_snapshot_slot_locked(ecm, 0, &slot, NULL) ||
	    cache_ecm_snapshots[slot].lifetime_id != lifetime_id ||
	    !cache_ecm_snapshots[slot].active ||
	    !cache_ecm_snapshots[slot].ledger_identity_valid) return 0;
	*identity_out = cache_ecm_snapshots[slot].ledger_identity;
	return ledger_identity_valid(identity_out);
}
#endif

/* The serialized value is treated only as an address token until this table
 * confirms that an ECM-side producer registered the same generation. */
int cache_ecm_snapshot_validate(ECM_DATA *ecm, uint64_t lifetime_id)
{
	int valid;
	pthread_mutex_lock(&prg.lockcache);
	valid = cache_ecm_snapshot_read_locked(ecm, lifetime_id, NULL, NULL, NULL);
	pthread_mutex_unlock(&prg.lockcache);
	return valid;
}

/* A waiter that completed before its FIND/REQUEST is drained is already
 * inactive in the side registry. No cache worker reads plain dcwstatus/cs. */
static int cache_sendpipe_armable(ECM_DATA *ecm, uint64_t lifetime_id)
{
	int active = 0;
	/* This is a cache-thread lookup under lockcache. Do not dereference a raw
	 * pointer decoded from the pipe; the side table proves address + lifetime. */
	return cache_ecm_snapshot_read_locked(ecm, lifetime_id, &active, NULL, NULL) && active;
}

static int cache_entry_ecm_current(const struct cache_data *entry)
{
	/* lockcache is held by cache-side callers; pointer comparison in the side
	 * registry is safe even if the serialized value itself is malformed. */
	return entry && cache_ecm_snapshot_read_locked(entry->ecm,
	                                               entry->ecm_lifetime_id,
	                                               NULL, NULL, NULL);
}

static int cache_entry_sendpipe_active(const struct cache_data *entry)
{
	return entry && (entry->flags & CACHE_FLAG_SENDPIPE) &&
	       cache_sendpipe_armable(entry->ecm, entry->ecm_lifetime_id);
}

static struct cardserver_data *cache_entry_cardserver_current(
	const struct cache_data *entry)
{
	struct cardserver_data *cs = NULL;
	if (!cache_entry_ecm_current(entry)) return NULL;
	if (!cache_ecm_snapshot_read_locked(entry->ecm, entry->ecm_lifetime_id,
	                                    NULL, &cs, NULL)) return NULL;
	if (!cache_entry_ecm_current(entry)) return NULL;
	return cs;
}

static int cache_entry_request_recvtime(const struct cache_data *entry,
                                        uint32_t *recvtime_out)
{
	return entry && recvtime_out &&
	       cache_ecm_snapshot_read_locked(entry->ecm, entry->ecm_lifetime_id,
	                                      NULL, NULL, recvtime_out);
}

/* The side registry is retired on slot reuse and may reclaim inactive
 * records; its fixed capacity is sized above MAX_ECM_DATA with load headroom. */

/*
 * TASK 3.6 -- clear the cache entry's waiter arm only after the matching ECM
 * has committed SUCCESS or FAILED. A generation match is required in addition
 * to the storage pointer so a late cleanup cannot clear a newer waiter's arm.
 * The caller follows lockecm -> lockcache; cache workers never acquire
 * lockecm, so this order is acyclic. While the waiter is still active, the arm
 * is the push path and must not be cleared.
 */
void cache_clear_sendpipe( ECM_DATA *ecm )
{
	struct cache_data req;
	struct cache_data *pcache;
	if (!ecm) return;
	memset( &req, 0, sizeof(req) );
	req.tag = ecm->ecm[0];
	req.sid = ecm->sid;
	req.caid = ecm->caid;
	req.hash = ecm->hash;
	req.ecm_lifetime_id = ecm_request_lifetime_id_load(ecm);
	if (!req.ecm_lifetime_id) return;
	pthread_mutex_lock( &prg.lockcache );
	pcache = cache_fetch( &req );
	if ( pcache && pcache->ecm==ecm &&
	     pcache->ecm_lifetime_id==req.ecm_lifetime_id ) {
		pcache->flags &= ~CACHE_FLAG_SENDPIPE;
		pcache->ecm = NULL;
		pcache->ecm_lifetime_id = 0;
	}
	pthread_mutex_unlock( &prg.lockcache );
}

static int cache_entry_ecm_excheck_gt1(const struct cache_data *entry)
{
#ifdef CACHEEX
	struct cardserver_data *cs = cache_entry_cardserver_current(entry);
	return cs && cs->option.excheck > 1;
#else
	(void)entry;
	return 0;
#endif
}


/*
 * TASK BS1 (2026-10-26) -- CW Cycle Validation for Black Screen Prevention
 *
 * REACTIVATED: checkcycle() was commented out in stock r82a, leaving the
 * cache path wide open to cycle-mismatched keys. A CW that is correct
 * in content but belongs to the WRONG half of the even/odd cycle
 * produces a black screen despite passing acceptDCW().
 *
 * The cycle marker (cwmark) now travels with every key through the
 * pipe system (TASK 2.5), and the validation happens in
 * ecm_setdcwdata() at delivery time. This function provides the
 * verdict; the caller decides whether to log-and-deliver or to
 * refuse outright (controlled by dcw_filter_cycle).
 *
 * Returns: 1 = cycle OK (or unverifiable), 0 = cycle MISMATCH.
 */
int checkcycle( uint8_t cw1cycle, uint8_t ecmtag, uint8_t cwcycle )
{
	/* If no cycle info available, cannot judge — allow */
	if (!cw1cycle) return 1;
	if (cwcycle == NO_CYCLE) return 1;

	if (cw1cycle==0x80) { // cw1 cycle on tag=0x80
		if ( (ecmtag==0x80)&&(cwcycle==CW0CYCLE) ) return 0;
		if ( (ecmtag==0x81)&&(cwcycle==CW1CYCLE) ) return 0;
	}
	else if (cw1cycle==0x81) { // cw1 cycle on tag=0x81
		if ( (ecmtag==0x81)&&(cwcycle==CW0CYCLE) ) return 0;
		if ( (ecmtag==0x80)&&(cwcycle==CW1CYCLE) ) return 0;
	}
	return 1;
}

/*
 * TASK BS1 -- cycle mismatch counter for diagnostics.
 */
static volatile unsigned long g_cycle_mismatches = 0;
unsigned long checkcycle_mismatch_count(void) { return g_cycle_mismatches; }
void checkcycle_mismatch_inc(void) { __sync_fetch_and_add(&g_cycle_mismatches, 1); }









///////////////////////////////////////////////////////////////////////////////
// CACHE ---> ECM
///////////////////////////////////////////////////////////////////////////////

int get_cache2ecm_with_verify(uint8_t *buf, size_t len,
                              struct cache_data *pcache, uint8_t *cw,
                              int *cwmark, uint32_t *peerid_out,
                              uint8_t *origin_mask_out,
                              uint8_t *verify_line_out)
{
	int base_len = 12 + (int)sizeof(void*) + (int)sizeof(uint64_t);
	int has_cw;
	int index = 12;
	uint64_t ecm_lifetime_id;
	if (cwmark) *cwmark = NO_CYCLE;
	if (peerid_out) *peerid_out = 0;
	if (origin_mask_out) *origin_mask_out = 0;
	if (verify_line_out) *verify_line_out = 0;
	if (!buf || !pcache || len < (size_t)base_len) return 0;
	if (buf[0] == PIPE_CACHE_FIND_FAILED) has_cw = 0;
	else if (buf[0] == PIPE_CACHE_FIND_SUCCESS) has_cw = 1;
	else return 0;
	if (len != (size_t)(base_len + (has_cw ? CACHE_PIPE_FIND_SUCCESS_EXTRA_LEN : 0))) return 0;
	if ((has_cw && !cw) || (!has_cw && cw)) return 0;
	memset(pcache, 0, sizeof(*pcache));
	pcache->tag = buf[1];
	pcache->sid = (buf[2]<<8) | buf[3];
	pcache->onid = (buf[4]<<8) | buf[5];
	pcache->caid = (buf[6]<<8) | buf[7];
	pcache->hash = ((uint32_t)buf[8]<<24) | (buf[9]<<16) | (buf[10]<<8) | buf[11];
	memcpy(&pcache->ecm, buf+index, sizeof(void*));
	index += sizeof(void*);
	memcpy(&ecm_lifetime_id, buf+index, sizeof(ecm_lifetime_id));
	if (!ecm_lifetime_id) return 0;
	pcache->ecm_lifetime_id = ecm_lifetime_id;
	index += sizeof(ecm_lifetime_id);
	if (has_cw) {
		uint32_t peerid = ((uint32_t)buf[index]<<24) | ((uint32_t)buf[index+1]<<16) |
		                  ((uint32_t)buf[index+2]<<8) | buf[index+3];
		index += CACHE_PIPE_FIND_SUCCESS_PEERID_LEN;
		memcpy(cw, buf+index, CACHE_PIPE_FIND_SUCCESS_CW_LEN);
		index += CACHE_PIPE_FIND_SUCCESS_CW_LEN;
		/* TASK 2.5: accept only the three cycle markers. An unknown marker
		 * would otherwise bypass checkcycle's explicit CW0/CW1 comparisons. */
		if (buf[index] != NO_CYCLE && buf[index] != CW0CYCLE &&
		    buf[index] != CW1CYCLE) return 0;
		if (cwmark) *cwmark = buf[index];
		index += CACHE_PIPE_FIND_SUCCESS_CYCLE_LEN;
		if (!cache_origin_mask_valid(buf[index])) return 0;
		if (origin_mask_out) *origin_mask_out = buf[index];
		index += CACHE_PIPE_FIND_SUCCESS_ORIGIN_MASK_LEN;
		if (buf[index] > 1u) return 0;
		if (verify_line_out) *verify_line_out = buf[index];
		if (peerid_out) *peerid_out = peerid;
	}
	return 1;
}

int get_cache2ecm(uint8_t *buf, size_t len, struct cache_data *pcache,
                  uint8_t *cw, int *cwmark, uint32_t *peerid_out,
                  uint8_t *origin_mask_out)
{
	return get_cache2ecm_with_verify(buf, len, pcache, cw, cwmark,
	                                peerid_out, origin_mask_out, NULL);
}

int put_cache2ecm_with_verify(uint8_t type, uint8_t *buf, size_t capacity,
                              struct cache_data *pcache, uint8_t *cw,
                              uint32_t peerid, int cwmark,
                              uint8_t origin_mask, int verify_line)
{
	int required_len = 12 + (int)sizeof(void*) + (int)sizeof(uint64_t);
	uint64_t ecm_lifetime_id;
	if (!buf || !pcache || (verify_line != 0 && verify_line != 1)) return 0;
	if (type == PIPE_CACHE_FIND_SUCCESS) {
		if (!cw || !cache_origin_mask_valid(origin_mask) ||
		    (cwmark != NO_CYCLE && cwmark != CW0CYCLE && cwmark != CW1CYCLE))
			return 0;
		required_len += CACHE_PIPE_FIND_SUCCESS_EXTRA_LEN;
	} else if (type != PIPE_CACHE_FIND_FAILED || cw || origin_mask || verify_line) return 0;
	if (!pcache->ecm || !pcache->ecm_lifetime_id || capacity < (size_t)required_len)
		return 0;
	ecm_lifetime_id = pcache->ecm_lifetime_id;
	buf[0] = type;
	buf[1] = pcache->tag;
	buf[2] = pcache->sid>>8; buf[3] = pcache->sid&0xff;
	buf[4] = pcache->onid>>8; buf[5] = pcache->onid&0xff;
	buf[6] = pcache->caid>>8; buf[7] = pcache->caid&0xff;
	buf[8] = pcache->hash>>24; buf[9] = pcache->hash>>16; buf[10] = pcache->hash>>8; buf[11] = pcache->hash & 0xff;
//	memcpy( buf+1, &(pcache->tag), 11);
	int index = 12;
	memcpy( buf+index, &pcache->ecm, sizeof(void*) );
	//debugf(0, " put_cache2ecm %p\\n", pcache->ecm);
	index += sizeof(void*);
	memcpy(buf+index, &ecm_lifetime_id, sizeof(ecm_lifetime_id));
	index += sizeof(ecm_lifetime_id);
	if (cw) {
		buf[index] = peerid>>24; buf[index+1] = peerid>>16; buf[index+2] = peerid>>8; buf[index+3] = peerid & 0xff;
		index+=CACHE_PIPE_FIND_SUCCESS_PEERID_LEN;
		memcpy( buf+index, cw, CACHE_PIPE_FIND_SUCCESS_CW_LEN);
		index+=CACHE_PIPE_FIND_SUCCESS_CW_LEN;
		/* TASK 2.5: the cycle the key's owning cwdata declared, riding with
		 * the key so the ECM side can see a contradiction at delivery time. */
		buf[index] = (uint8_t)cwmark;
		index+=CACHE_PIPE_FIND_SUCCESS_CYCLE_LEN;
		buf[index] = origin_mask;
		index+=CACHE_PIPE_FIND_SUCCESS_ORIGIN_MASK_LEN;
		buf[index] = (uint8_t)verify_line;
		index+=CACHE_PIPE_FIND_SUCCESS_VERIFY_LEN;
	}
	return index;
}

int put_cache2ecm(uint8_t type, uint8_t *buf, size_t capacity,
                  struct cache_data *pcache, uint8_t *cw, uint32_t peerid,
                  int cwmark, uint8_t origin_mask)
{
	return put_cache2ecm_with_verify(type, buf, capacity, pcache, cw,
	                                 peerid, cwmark, origin_mask, 0);
}

#ifdef MCS_DCWSTATS
/*
 * TASK R11 (D65): one ring row per verdict, fed only while the counters'
 * own switch (dcwstats_on) is on -- the ring lives and dies with the
 * counters, no new config key. Kept as one-liners so each verdict site
 * below reads as evidence, not bookkeeping. The stock flavour never
 * compiles any of this.
 */
static void cwlog_refuse(int reason, int peerid, uint32_t caid, uint32_t provid,
			 uint16_t sid, const uint8_t *cw)
{
	if (dcwstats_on)
		cwlog_note(CWLOG_REFUSED, reason, peerid, caid, provid, sid, cw, GetTickCount());
}
static void cwlog_hold(int peerid, uint32_t caid, uint32_t provid,
		       uint16_t sid, const uint8_t *cw)
{
	if (dcwstats_on)
		cwlog_note(CWLOG_HELD, CWLOG_R_HOLD, peerid, caid, provid, sid, cw, GetTickCount());
}
static void cwlog_deliver(int peerid, uint32_t caid, uint32_t provid,
			  uint16_t sid, const uint8_t *cw, int waiter)
{
	if (dcwstats_on)
		cwlog_note(waiter ? CWLOG_DELIVERED : CWLOG_STORED, 0, peerid,
			   caid, provid, sid, cw, GetTickCount());
}
#endif /* MCS_DCWSTATS */

/* This cache->ECM IPC is a required hand-off: losing it can strand a live
 * waiter or leave a candidate marked sent. A zero-byte pipe timeout is
 * stream-safe in pipe_send(), but this caller cannot reconstruct the message. */
static int cache_ecm_pipe_send_required(int fd, uint8_t *buf, int len)
{
	int sent = pipe_send(fd, buf, len);
	if (sent < 0) prg.restart = 1;
	return sent;
}

void pipe_cache2ecm_find_failed(struct cache_data *pcache)
{
	uint8_t buf[64];
	int len;
	if (!cache_entry_ecm_current(pcache)) return;
	user_trace_cache_observation(pcache->caid, pcache->provid, pcache->sid, pcache->hash, 0, "cache lookup: no existing entry; peer response may still arrive", NULL);
	len = put_cache2ecm(PIPE_CACHE_FIND_FAILED, buf, sizeof(buf), pcache, NULL, 0, 0, 0);
	if (len > 0) cache_ecm_pipe_send_required(prg.pipe.ecm[1], buf, len);
}

/* Resolve metadata only for an exact candidate in this entry. Cross-entry
 * relays use the explicit source mask and the destination's checked cycle. */
static uint8_t cache_entry_cw_origin(const struct cache_data *pcache,
                                     const uint8_t *cw, int *cwmark)
{
	struct cw_cache_data *cwd;
	if (cwmark) *cwmark = NO_CYCLE;
	if (!pcache || !cw) return 0;
	for (cwd = pcache->cwdata; cwd; cwd = cwd->next) {
		if (dcwcmp16(cwd->cw, (uint8_t *)cw)) {
			if (cwmark) *cwmark = (int)cwd->cwcycle;
			return cache_origin_merge(0, cwd->origin_mask);
		}
	}
	return 0;
}

static void pipe_cache2ecm_find_success_with_origin(struct cache_data *pcache,
                                                     uint8_t *cw,
                                                     uint32_t peerid,
                                                     int cwmark,
                                                     uint8_t origin_mask,
                                                     int verify_line)
{
	uint8_t buf[64];
	int sent = 0;
#ifdef MCS_DCWSTATS
	int active;
#endif
	if (!cache_origin_mask_valid(origin_mask) ||
	    !cache_entry_ecm_current(pcache)) return;
	{
		int len = put_cache2ecm_with_verify(PIPE_CACHE_FIND_SUCCESS, buf,
		                        sizeof(buf), pcache, cw, peerid, cwmark,
		                        origin_mask, verify_line);
		if (len > 0)
			sent = cache_ecm_pipe_send_required(prg.pipe.ecm[1], buf, len);
	}
	/* The note describes a committed pipe hand-off, not the earlier decision
	 * to forward. Client delivery remains unconfirmed until the ECM worker acts. */
	if (sent > 0) {
		user_trace_cache_observation(pcache->caid, pcache->provid, pcache->sid,
		    pcache->hash, peerid,
		    verify_line ? "cache reputation tie selected; verification hand-off requested" :
		                  "cache forwarded CW to ECM pipe; delivery not confirmed", cw);
#ifdef MCS_DCWSTATS
		active = cache_entry_sendpipe_active(pcache);
		/* TASK R11 (D65): this is the single accepted-key hand-off funnel. */
		cwlog_deliver(peerid, pcache->caid, pcache->provid, pcache->sid, cw,
			      active ? 1 : 0);
#endif
	}
	/* Measure only a successful hand-off to a live local waiter. The request
	 * start time comes from the lifetime registry, not a recycled ECM pointer
	 * or an older cache-entry timestamp. */
	if (sent > 0 && cache_entry_sendpipe_active(pcache)) {
		struct cardserver_data *cs = cache_entry_cardserver_current(pcache);
		uint32_t request_recvtime = 0;
		if (cs && cs->option.cache_evidence &&
		    cache_entry_request_recvtime(pcache, &request_recvtime)) {
			uint32_t now_ms = GetTickCount();
			r48_cache_window_note_cache(cs->id, pcache->caid,
			    tick32_elapsed(now_ms, request_recvtime), now_ms);
		}
	}
}

void pipe_cache2ecm_find_success(struct cache_data *pcache, uint8_t *cw,
                                 uint32_t peerid)
{
	int cwmark = NO_CYCLE;
	uint8_t origin_mask = cache_entry_cw_origin(pcache, cw, &cwmark);
	pipe_cache2ecm_find_success_with_origin(pcache, cw, peerid, cwmark,
	                                        origin_mask, 0);
}


///////////////////////////////////////////////////////////////////////////////
// ECM ---> CACHE
///////////////////////////////////////////////////////////////////////////////

int get_ecm2cache(uint8_t *buf, int len, struct cache_data *pcache, uint8_t *cw)
{
	int base_len = 16 + (int)sizeof(void*) + (int)sizeof(uint64_t);
	/* Pipe messages serialize only the wire/cache key fields. Clear local-only
	 * provider association and pointers before decoding into a stack record. */
#ifdef CACHEEX
	base_len += 16; /* ecmd5 */
#endif
	if (!buf || !pcache || len < base_len) return -1;
	/* The low seven bits carry the cache-side cycle enum; fail closed on any
	 * value outside its three defined states rather than treating it as an
	 * unverifiable marker later. */
	{
		uint8_t cycle = (uint8_t)(buf[15] & 0x7Fu);
		if (cycle != NO_CYCLE && cycle != CW0CYCLE && cycle != CW1CYCLE)
			return -1;
	}
	switch (buf[0]) {
	case PIPE_CACHE_FIND:
		if (len != base_len && len != base_len + 16) return -1;
		break;
	case PIPE_CACHE_REQUEST:
	case PIPE_CACHE_RESENDREQ:
		if (len != base_len) return -1;
		break;
	case PIPE_CACHE_REPLY:
		if (len != base_len + (int)CACHE_PIPE_REPLY_CW_LEN +
		    (int)CACHE_PIPE_REPLY_TRAILER_LEN || !cw) return -1;
		break;
	default:
		return -1;
	}
	memset(pcache, 0, sizeof(*pcache));
	pcache->tag = buf[1];
	pcache->sid = (buf[2]<<8) | buf[3];
	pcache->onid = (buf[4]<<8) | buf[5];
	pcache->caid = (buf[6]<<8) | buf[7];
	pcache->hash = ((uint32_t)buf[8]<<24) | (buf[9]<<16) | (buf[10]<<8) | (buf[11]);
	pcache->provid = (buf[12]<<16) | (buf[13]<<8) | (buf[14]);
	pcache->provider_known = !!(buf[15] & 0x80) || pcache->provid != 0;
	pcache->cwcycle = (cwcycle_t)(buf[15] & 0x7F);
	int index = 16;
	uint64_t ecm_lifetime_id;
	//
	memcpy( &pcache->ecm, buf+index, sizeof(void*) );
	index += sizeof(void*);
	memcpy(&ecm_lifetime_id, buf+index, sizeof(ecm_lifetime_id));
	if (!ecm_lifetime_id) return -1;
	pcache->ecm_lifetime_id = ecm_lifetime_id;
	index += sizeof(ecm_lifetime_id);
#ifdef CACHEEX
	memcpy( pcache->ecmd5, buf+index, 16 );
	index+=16;
#endif
	if (cw && cache_pipe_copy_optional_cw(buf, (size_t)len,
	                                      (size_t)index, cw))
		index += 16;
	return index;
}

/* The pipe_* serializers call this with lockecm held so every copied field and
 * the lifetime token belong to one ECM generation. It deliberately takes no
 * locks; taking locksv here would invert the ECM worker's lock order. */
static int put_ecm2cache(uint8_t type, uint8_t *buf, size_t capacity,
                         ECM_DATA *ecm, uint16_t onid, uint8_t *cw)
{
	uint64_t ecm_lifetime_id;
	int required_len = 16 + (int)sizeof(void*) + (int)sizeof(uint64_t);
	if (!ecm || !buf) return 0;
#ifdef CACHEEX
	required_len += 16;
#endif
	if (type != PIPE_CACHE_FIND && type != PIPE_CACHE_REQUEST &&
	    type != PIPE_CACHE_REPLY && type != PIPE_CACHE_RESENDREQ)
		return 0;
	if (cw) required_len += 16;
	if (capacity < (size_t)required_len) return 0;
	ecm_lifetime_id = ecm_request_lifetime_id_load(ecm);
	if (!ecm_lifetime_id) return 0;
	buf[0] = type;
	buf[1] = ecm->ecm[0];
	buf[2] = ecm->sid>>8; buf[3] = ecm->sid;
	buf[4] = onid>>8; buf[5] = onid;
	buf[6] = ecm->caid>>8; buf[7] = ecm->caid;
	buf[8] = ecm->hash>>24; buf[9] = ecm->hash>>16; buf[10] = ecm->hash>>8; buf[11] = ecm->hash;
	buf[12] = ecm->provid>>16; buf[13] = ecm->provid>>8; buf[14] = ecm->provid;
	// Reserve the unused high bit for provider presence; cycle values are 0..2.
	if (!ecm->cw1cycle) buf[15] = NO_CYCLE;
	else if (ecm->ecm[0]==ecm->cw1cycle) buf[15] = CW1CYCLE; else buf[15] = CW0CYCLE;
	if (ecm->provider_known || ecm->provid != 0) buf[15] |= 0x80;
	//
	int index = 16;
	memcpy( buf+index, &ecm, sizeof(void*) );
	index += sizeof(void*);
	memcpy(buf+index, &ecm_lifetime_id, sizeof(ecm_lifetime_id));
	index += sizeof(ecm_lifetime_id);
#ifdef CACHEEX
	memcpy( buf+index, ecm->ecmd5, 16 );
	index+=16;
#endif
	if (cw) { // previous cw if find/request, cw for reply
		memcpy( buf+index, cw, 16 );
		index+=16;
	}
	return index;
}

/*
#ifdef THREAD_DCW
void pipe_cache_find( ECM_DATA *ecm, struct cardserver_data *cs)
{
	struct cache_data req;
	req.tag = ecm->ecm[0];
	req.sid = ecm->sid;
	req.onid = cs->option.onid;
	req.caid = ecm->caid;
	req.hash = ecm->hash;
	req.provid = ecm->provid;
	req.cw1cycle = ecm->cw1cycle;


	pthread_mutex_lock( &prg.lockcache );

	struct cache_data *pcache = cache_fetch( &req );
	if (pcache==NULL) {
		pcache = cache_new( &req );
		pcache->ecm = ecm;
		pcache->cw1cycle = req.cw1cycle;
#ifdef CACHEEX
		memcpy( pcache->ecmd5, ecm->ecmd5, 16 );
#endif
		pcache->flags |= CACHE_FLAG_SENDPIPE;
		// XXX set find failed
		ecm_request_set_dcwstatus(ecm, STAT_DCW_WAIT);
		ecm->checktime = 1;
	}
	else {
		if (!cs->option.cachetimeout) {
			ecm_request_set_dcwstatus(ecm, STAT_DCW_WAIT);
			ecm->checktime = 1;
		}
		pcache->ecm = ecm;
		pcache->tag = req.tag; // set tag if not set (coming from cahceex)
		pcache->provid = req.provid; // set provid if not set (coming from csp)
		pcache->cw1cycle = req.cw1cycle;
		pcache->provid = req.provid;
#ifdef CACHEEX
		memcpy( pcache->ecmd5, ecm->ecmd5, 16 );
#endif
		pcache->flags |= CACHE_FLAG_SENDPIPE;
		// Check stored cw
		if (pcache->icwlist) {
			int i;
			for(i=0; i<pcache->icwlist; i++) {
				if ( pcache->cwlist[i].status ) {
					if ( checkcycle( pcache->cw1cycle, pcache->tag, pcache->cwlist[i].cwcycle ) ) {
						ecm_setdcw( ecm, pcache->cwlist[i].cw, DCW_SOURCE_CACHE, pcache->cwlist[i].peerid );
					}
				}
			}
		}
	}

	pthread_mutex_unlock( &prg.lockcache );
}
*/

void pipe_cache_find( ECM_DATA *ecm, struct cardserver_data *cs)
{
	uint8_t buf[64];
	uint8_t *cw = NULL;
	int len;
	if (!ecm || !cs || !cache_ecm_snapshot_publish(ecm, cs)) return;
	if ( ecm->lastdecode.ecm && (ecm->lastdecode.counter>1) ) cw = ecm->lastdecode.dcw;
	len = put_ecm2cache(PIPE_CACHE_FIND, buf, sizeof(buf), ecm, cs->option.onid, cw);
	if (len > 0 && ecm_request_lifetime_id_load(ecm) != 0)
		cache_pipe_out_enqueue(buf, len);
}

void pipe_cache_request( ECM_DATA *ecm, struct cardserver_data *cs)
{
	uint8_t buf[64];
	int len;
	if (!ecm || !cs || !cache_ecm_snapshot_publish(ecm, cs)) return;
	len = put_ecm2cache(PIPE_CACHE_REQUEST, buf, sizeof(buf), ecm, cs->option.onid, NULL);
	if (len > 0 && ecm_request_lifetime_id_load(ecm) != 0)
		cache_pipe_out_enqueue(buf, len);
}

/* Serialize while the caller still owns the ECM's stable identity (the
 * successful setdcw commit is under lockecm). Sending is deferred until after
 * lockecm is released so a full pipe cannot stall the ECM lock. */
int pipe_cache_reply(ECM_DATA *ecm, struct cardserver_data *cs, uint8_t *buf,
		     size_t capacity, uint8_t source_origin_mask)
{
	int len;
	uint64_t lifetime_id;
	/* Caller owns lockecm at the SUCCESS commit. No dcwstatus read is needed;
	 * serialize only a live, nonzero generation and set REP after the complete
	 * snapshot has been written. */
	if (!ecm || !cs || !buf || !cache_origin_mask_valid(source_origin_mask) ||
	    !cs->option.fallowcache || !cs->option.cachesendrep ||
	    (ecm->cachestatus & ECM_CACHE_REP)) return 0;
	/* The two private trailer bytes preserve the cache-origin behavior bit and
	 * the candidate's source-class mask across the asynchronous pipe handoff. */
	{
		size_t required_len = 16u + sizeof(void*) + sizeof(uint64_t) + 16u +
		                     CACHE_PIPE_REPLY_CW_LEN + CACHE_PIPE_REPLY_TRAILER_LEN;
#ifndef CACHEEX
		required_len -= 16u;
#endif
		if (capacity < required_len) return 0;
	}
	/* The success transition has already marked the waiter inactive. Keep an
	 * inactive identity record even if no FIND/REQUEST was previously queued. */
	if (!cache_ecm_snapshot_record(ecm, cs, 0)) return 0;
	lifetime_id = ecm_request_lifetime_id_load(ecm);
	if (!lifetime_id) return 0;
	len = put_ecm2cache(PIPE_CACHE_REPLY, buf, capacity, ecm, cs->option.onid, ecm->cw);
	if (len <= 0 || !ecm_request_lifetime_matches(ecm, ecm, lifetime_id)) return 0;
	/* Snapshot the origin with the CW: the ECM object can be recycled before
	 * this asynchronous pipe message is processed. Keep the legacy cache bit
	 * separate from source class; unproven sources remain unclassified. */
	buf[len + CACHE_PIPE_REPLY_FROM_CACHE_OFFSET] =
		(ecm->dcwsrctype == DCW_SOURCE_CACHE) ? 1 : 0;
	buf[len + CACHE_PIPE_REPLY_ORIGIN_MASK_OFFSET] =
			cache_origin_merge(source_origin_mask,
			                   cache_origin_for_source(ecm->dcwsrctype, ecm->dcwsrcid));
	len += (int)CACHE_PIPE_REPLY_TRAILER_LEN;
	if (!ecm_request_lifetime_matches(ecm, ecm, lifetime_id)) return 0;
	ecm->cachestatus |= ECM_CACHE_REP;
	return len;
}

void pipe_cache_reply_send(uint8_t *buf, int len)
{
	if (buf && len > 0) cache_pipe_out_enqueue(buf, len);
}

void pipe_cache_resendreq(ECM_DATA *ecm, struct cardserver_data *cs)
{
	uint8_t buf[64];
	int len;
	if (!ecm || !cs || !cache_ecm_snapshot_publish(ecm, cs)) return;
	len = put_ecm2cache(PIPE_CACHE_RESENDREQ, buf, sizeof(buf), ecm, cs->option.onid, NULL);
	if (len <= 0) return;
	cache_pipe_out_enqueue(buf, len);
	debugf( getdbgflag(DBG_NEWCAMD,cs->id,0), " [%s] CACHE RESENDREQ ch %04x:%06x:%04x\n", cs->name,ecm->caid,ecm->provid,ecm->sid);
}

///////////////////////////////////////////////////////////////////////////////
// 
///////////////////////////////////////////////////////////////////////////////


void sendtoip( int handle, uint32_t ip, int port, unsigned char *buf, int len)
{
	if (ip && port) {
		struct sockaddr_in si_other;
		int slen=sizeof(si_other);
		memset((char *) &si_other, 0, sizeof(si_other));
		si_other.sin_family = AF_INET;
		si_other.sin_port = htons( port );
		si_other.sin_addr.s_addr = ip;
		sendto( handle, buf, len, 0, (struct sockaddr *)&si_other, slen );
	}
}

void sendtopeer( struct cachepeer_data *peer, unsigned char *buf, int len)
{
	uint32_t peer_ip = host_ip_snapshot(peer->host);
	/* Disabled means disabled in both directions, even if a stale ping is
	 * positive or another sender bypasses cache_check_peers(). */
	if (IS_DISABLED(peer->flags)) return;
	if (peer_ip && peer->port) {
		struct sockaddr_in si_other;
		int slen=sizeof(si_other);
		memset((char *) &si_other, 0, sizeof(si_other));
		si_other.sin_family = AF_INET;
		si_other.sin_port = htons( peer->port );
		si_other.sin_addr.s_addr = peer_ip;
#ifdef DEBUG_NETWORK2
		if (flag_debugnet) {
			debugf(getdbgflag(DBG_CACHE,0,0)," cache: send data (%d) to peer (%s:%d)\n", len, peer->host->name,peer->port);
			debughex(buf,len);
		}
#endif

		sendto(peer->outsock, buf, len, 0, (struct sockaddr *)&si_other, slen);
/*
		while (1) {
			struct pollfd pfd;
			pfd.fd = peer->outsock;
			pfd.events = POLLOUT;
			int retval = poll(&pfd, 1, 10);
			if (retval>0) {
				if ( pfd.revents & (POLLOUT) ) {
					sendto(peer->outsock, buf, len, 0, (struct sockaddr *)&si_other, slen);
				} else debugf(getdbgflag(DBG_CACHE,0,0)," cache: error sending data to peer (%s:%d) \n", peer->host->name,peer->port);
				break;
			}
			else if (retval<0) {
				if ( (errno!=EINTR)&&(errno!=EAGAIN) ) {
					debugf(getdbgflag(DBG_CACHE,0,0)," cache: error sending data to peer (%s:%d) \n", peer->host->name,peer->port);
					break;
				}
			}
			else debugf(getdbgflag(DBG_CACHE,0,0)," cache: error sending data to peer (%s:%d) \n", peer->host->name,peer->port);
		}
*/
	}
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

#define TYPE_REQUEST   1
#define TYPE_REPLY     2
#define TYPE_PINGREQ   3
#define TYPE_PINGRPL   4
#define TYPE_RESENDREQ 5

#ifdef NEWCACHE

#define TYPE_HELLO          0x03
#define TYPE_HELLO_ACK      0x10
#define TYPE_KEEPALIVE      0x11
#define TYPE_KEEPALIVE_ACK  0x12
#define TYPE_CARD_LIST      0x13
#define TYPE_SMS            0x14
#define TYPE_SMS_ACK        0x15

#define TYPE_VERSION        0x90
#define TYPE_VERSION_ACK    0x91

#define TYPE_EXTENDED       0x92
#define TYPE_EXTENDED_ACK   0x93

#define TYPE_UNKNOWN        0xFF

#endif

#include "peer_cards.h"	/* TASK 3.17: bounded card-list search */

static int cache_peer_card_compare(const void *left, const void *right)
{
	uint32_t a = *(const uint32_t *)left;
	uint32_t b = *(const uint32_t *)right;
	return (a > b) - (a < b);
}

int peer_card_binarysearch( struct cachepeer_data *peer, uint16_t caid, uint32_t provid)
{
	/*
	 * TASK 3.17. The comparison is peer_cards_find(). It never reads
	 * cards[nbcards]. On this packed struct that slot is nbcards itself,
	 * so a 1024-card list must not treat the count as a card. Search order
	 * matches the unsigned ordering used when the card list is received.
	 */
	return peer_cards_find(peer->cards, peer->nbcards, caid, provid);
}

int peer_acceptcard( struct cachepeer_data *peer, uint16_t caid, uint32_t provid)
{
	int i;

	/* No callers. A full list has no zero sentinel; a miss is not accept. */
	if ( peer->cards[0] ) {
		uint32_t caprov = ((uint32_t)caid << 16) | provid;
		if (!peer_cards_listed(peer->cards, peer->nbcards, caprov)) return 0;
	}
	if ( peer->sharelimits[0].caid!=0xffff ) {
		for (i=0; i<100; i++) {
			if (peer->sharelimits[i].caid==0xffff) return 0;
			if (peer->sharelimits[i].caid==caid) {
				if (peer->sharelimits[i].provid==provid) break;
				else if (peer->sharelimits[i].provid==0xFFFFFF) break;
			}
		}
	}
	return 1;
}		


/*
 * Legacy peerrep records IP:port-scoped diagnostics. A threshold crossing
 * activates only a temporary gate (15m request pause or 60m cache isolation);
 * it never sets FLAG_DISABLE. The persisted file is historical diagnostics,
 * not a source of active quarantine on restart. Never called for unknown peers.
 */
/* peerrep_event runs under lockcache. Copy transition details here; after
 * lockcache is released, persist the diagnostic history and then log. */
struct cache_peerrep_deferred_event {
	int stage;
	uint16_t port;
	uint32_t peer_id;
	char host[256];
	char reason[PEERREP_REASON_MAX];
};
static __thread struct cache_peerrep_deferred_event cache_peerrep_deferred;

void cache_peerrep_save_after_unlock(void)
{
	static const char *const pr_cons[] = { "", "requests paused for up to 15m",
		"cache pushes refused for up to 60m", "legacy BAN threshold: temporary isolation up to 60m; peer remains enabled" };
	struct cache_peerrep_deferred_event event = cache_peerrep_deferred;
	if (!event.stage) return;
	memset(&cache_peerrep_deferred, 0, sizeof(cache_peerrep_deferred));
	peerrep_save();
	debugf(getdbgflag(DBG_CACHE,0,event.peer_id),
		" [PEER REP] peer %s:%u ESCALATED to %s -- last event '%s' -- consequence: %s\\n",
		event.host, event.port,
		event.stage==PEERREP_STAGE_DISTRUST?"DISTRUST":
		(event.stage==PEERREP_STAGE_ISOLATE?"ISOLATE":"TEMPORARY ISOLATE"),
		event.reason, pr_cons[event.stage]);
}

static void peerrep_event(struct cachepeer_data *peer, const char *reason)
{
	int st;
	if (!peerrep_on) return;
	st = peerrep_note(host_ip_snapshot(peer->host), peer->port, reason,
	                  GetTickCount());
	if (st>0) {
		cache_peerrep_deferred.stage = st;
		cache_peerrep_deferred.port = peer->port;
		cache_peerrep_deferred.peer_id = peer->id;
		snprintf(cache_peerrep_deferred.host, sizeof(cache_peerrep_deferred.host),
		         "%s", peer->host ? peer->host->name : "");
		snprintf(cache_peerrep_deferred.reason, sizeof(cache_peerrep_deferred.reason),
		         "%s", reason ? reason : "unknown");
	}
}
void cache_send_request(struct cache_data *pcache,struct cachepeer_data *peer)
{
	uint8_t buf[64];
	//01 80 00CD 0001 0500 8D1DB359
	buf[0] = TYPE_REQUEST;
	buf[1] = pcache->tag;
	buf[2] = pcache->sid>>8;
	buf[3] = pcache->sid;
	buf[4] = pcache->onid>>8;
	buf[5] = pcache->onid&0xff;
	buf[6] = pcache->caid>>8;
	buf[7] = pcache->caid;
	buf[8] = pcache->hash>>24;
	buf[9] = pcache->hash>>16;
	buf[10] = pcache->hash>>8;
	buf[11] = pcache->hash;
	if (peer) {
		sendtopeer(peer, buf, 12);
		peer->sentreq++;
	}
	else {
		struct cacheserver_data	*cache = cfg.cache.server;
		while (cache) {
			peer = cache->peer;
			while (peer) {
				if (peer->ping>0)
				if (peer->flags&FLAG_CACHE_SENDREQ)
				if ( !peerrep_on || peerrep_ask_ok(host_ip_snapshot(peer->host), peer->port, GetTickCount()) ) /* legacy TTL gate, OFF is zero-work */
				if ( !peer->fblock0onid || pcache->onid )
				if ( peer_card_binarysearch(peer, pcache->caid, pcache->provid) ) {
					/*
					 * TASK 2.8 -- the one consequence the coarse trust tier
					 * has: a peer below the actionable line on THIS caid is
					 * not asked for it, for one bounded window, then one
					 * request tests it again. Only when the operator has
					 * turned PEER-TRUST-REQUESTS on; pushes and replies are
					 * never gated, and nothing is disconnected.
					 * trustagg_gate() takes trust_lock inside -- the
					 * lockcache -> trust_lock order cachepref_gate()
					 * already established.
					 */
					if ( !peertrust_requests ||
					     trustagg_gate(DCW_SOURCE_CACHE, peer->id|PEER_CSP,
					                   pcache->caid) ) {
						sendtopeer(peer, buf, 12);
						peer->sentreq++;
					}
				}
				peer = peer->next;
			}
			cache = cache->next;
		}
	}
}


void cache_send_reply(struct cache_data *pcache,struct cachepeer_data *peer, uint8_t cw[16])
{
	uint8_t buf[64];
	//Common Data
	buf[0] = TYPE_REPLY;
	buf[1] = pcache->tag;
	buf[2] = pcache->sid>>8;
	buf[3] = pcache->sid;
	buf[4] = pcache->onid>>8;
	buf[5] = pcache->onid&0xff;
	buf[6] = pcache->caid>>8;
	buf[7] = pcache->caid;
	buf[8] = pcache->hash>>24;
	buf[9] = pcache->hash>>16;
	buf[10] = pcache->hash>>8;
	buf[11] = pcache->hash;
	buf[12] = pcache->tag;
	memcpy( buf+13, cw, 16);

	if (peer) {
		sendtopeer(peer, buf, 29);
		//peer->sentrep++;
	}
	else {
		struct cacheserver_data	*cache = cfg.cache.server;
		while (cache) {
			peer = cache->peer;
			while ( peer ) {
				if (peer->ping>0)
				//if (peer->flags&FLAG_CACHE_SENDREP)
				if ( !peer->fblock0onid || pcache->onid )
				if ( peer_card_binarysearch(peer, pcache->caid, pcache->provid) ) {
					sendtopeer(peer, buf, 29);
					peer->sentrep++;
				}
				peer = peer->next;
			}
			cache = cache->next;
		}
	}
}


void cache_send_fwdreply(struct cache_data *pcache, uint8_t cw[16], cwcycle_t cwcycle)
{
	uint8_t buf[64];
	//Common Data
	buf[0] = TYPE_REPLY;
	buf[1] = pcache->tag;
	buf[2] = pcache->sid>>8;
	buf[3] = pcache->sid;
	buf[4] = pcache->onid>>8;
	buf[5] = pcache->onid&0xff;
	buf[6] = pcache->caid>>8;
	buf[7] = pcache->caid;
	buf[8] = pcache->hash>>24;
	buf[9] = pcache->hash>>16;
	buf[10] = pcache->hash>>8;
	buf[11] = pcache->hash;
	buf[12] = pcache->tag;
	memcpy( buf+13, cw, 16);
	buf[29] = cwcycle;

	struct cacheserver_data	*cache = cfg.cache.server;
	while (cache) {
		struct cachepeer_data *peer = cache->peer;
		while ( peer ) {
			//if (peer->flags&FLAG_CACHE_SENDREP)
			if ( peer->ping>0 )
			if ( peer->fwd )
			if ( peer_card_binarysearch(peer, pcache->caid, pcache->provid) ) {
				sendtopeer(peer, buf, 30);
				peer->sentrep++;
			}
			peer = peer->next;
		}
		cache = cache->next;
	}
}

void cache_send_resendreq(struct cache_data *pcache)
{
	struct cacheserver_data *cache = cfg.cache.server;
	while (cache) {
		// <1:TYPE_RESENDREQ> <2:port> <1:ecmtag> <2:sid> <2:onid> <2:caid> <4:hash>
		uint8_t buf[64];
		buf[0] = TYPE_RESENDREQ;
		//Port
		buf[1] = 0;
		buf[2] = 0;
		buf[3] = cache->port>>8;
		buf[4] = cache->port&0xff;
		buf[5] = pcache->tag;
		buf[6] = pcache->sid>>8;
		buf[7] = pcache->sid;
		buf[8] = pcache->onid>>8;
		buf[9] = pcache->onid&0xff;
		buf[10] = pcache->caid>>8;
		buf[11] = pcache->caid;
		buf[12] = pcache->hash>>24;
		buf[13] = pcache->hash>>16;
		buf[14] = pcache->hash>>8;
		buf[15] = pcache->hash;
		struct cachepeer_data *peer = cache->peer;
		while ( peer ) {
			if ( peer->ping>0 )
			if ( peer_card_binarysearch(peer, pcache->caid, pcache->provid) )
				sendtopeer(peer, buf, 16);
			peer = peer->next;
		}
		cache = cache->next;
	}
}

void cache_send_ping(struct cacheserver_data *cache, struct cachepeer_data *peer)
{
	unsigned char buf[32];
	buf[0] = TYPE_PINGREQ;
	// New Cache IDENT
	buf[1] = 'M';
	buf[2] = 'C';
	/// buf[3] = 1 | BIT_CACHE_HACK;
	buf[3] = 1;
	// PEER ID
	buf[4] = peer->id>>8; 
	buf[5] = peer->id&0xff;
	// MULTICS CRC
	buf[6] = peer->crc[0] = 0xff & rand();
	buf[7] = peer->crc[1] = 0xff & rand();
	buf[8] = peer->crc[2] = 0xff & rand();
	//Port
	buf[9] = peer->crc[3] = 0;
	buf[10] = 0;
	buf[11] = cache->port>>8;
	buf[12] = cache->port&0xff;

	//Program
	buf[13] = 0x01; //ID
	buf[14] = 7; //LEN
	buf[15] = 'M'; buf[16] = 'u'; buf[17] = 'l'; buf[18] = 't'; buf[19] = 'i'; buf[20] = 'C'; buf[21] = 'S';
	//Version
	buf[22] = 0x02; //ID
	buf[23] = 3; //LEN
	buf[24] = 'r'; buf[25] = '0'+(REVISION/10); buf[26] = '0'+(REVISION%10);
	//
	sendtopeer( peer, buf, 27);
}


#ifdef NEWCACHE

void cache_send_keepalive(struct cacheserver_data *cache, struct cachepeer_data *peer)
{
	if (peer->protocol&1) {
		unsigned char buf[32];
		buf[0] = TYPE_KEEPALIVE;
		buf[1] = peer->id>>8; 
		buf[2] = peer->id&0xff;
		sendtopeer( peer, buf, 3);
	}
	else cache_send_ping(cache, peer);
}

static void save_sms_record(const struct sms_data *sms,
                            const char *peer_name, uint16_t peer_port)
{
	FILE *fhandle = fopen(sms_file, "at");
	if (fhandle) {
		char timebuf[80];
		time_t rawtime;
		struct tm time_storage;
		struct tm *timeinfo;
		memcpy(&rawtime, PACKED_MPTR(sms, time_t, rawtime), sizeof(rawtime));
		timeinfo = localtime_r(&rawtime, &time_storage);
		if (timeinfo) strftime(timebuf, sizeof(timebuf), "%x %X", timeinfo);
		else snprintf(timebuf, sizeof(timebuf), "unknown time");
		if (sms->status & 1)
			fprintf(fhandle, "\n\n[ %s ] >> SMS to peer (%s:%u)\n", timebuf, peer_name, peer_port);
		else
			fprintf(fhandle, "\n\n[ %s ] << SMS from peer (%s:%u)\n", timebuf, peer_name, peer_port);
		fputs(sms->msg, fhandle);
		fclose(fhandle);
	}
}

int cache_peer_send_sms_by_id(uint32_t peer_id, const char *message,
                              uint32_t *hash_out)
{
	struct cachepeer_data *peer;
	struct sms_data *sms;
	struct sms_data saved_sms;
	char peer_name[256];
	uint32_t peer_ip, hash;
	uint16_t peer_port;
	int peer_socket, peer_flags, len;
	uint8_t buf[sizeof(sms->msg) + 5];
	if (!message) return 0;
	len = (int)strnlen(message, sizeof(sms->msg));
	if (len <= 0 || len >= (int)sizeof(sms->msg)) return 0;
	sms = (struct sms_data *)calloc(1, sizeof(*sms));
	if (!sms) return 0;
	memcpy(sms->msg, message, (size_t)len);
	sms->msg[len] = 0;
	hash = hashCode((uint8_t *)sms->msg, (unsigned int)len);
	sms->hash = hash;
	sms->rawtime = time(NULL);
	sms->status = 1;

	pthread_mutex_lock(&prg.lockcache);
	peer = getpeerbyid((int)peer_id);
	if (!peer) {
		pthread_mutex_unlock(&prg.lockcache);
		free(sms);
		return 0;
	}
	sms->next = peer->sms;
	peer->sms = sms;
	memset(&saved_sms, 0, sizeof(saved_sms));
	saved_sms.status = sms->status;
	saved_sms.rawtime = sms->rawtime;
	snprintf(saved_sms.msg, sizeof(saved_sms.msg), "%s", sms->msg);
	if (peer->host)
		snprintf(peer_name, sizeof(peer_name), "%s", peer->host->name);
	else
		peer_name[0] = 0;
	peer_ip = peer->host ? host_ip_snapshot(peer->host) : 0;
	peer_port = peer->port;
	peer_socket = peer->outsock;
	peer_flags = (int)peer->flags;
	pthread_mutex_unlock(&prg.lockcache);

	buf[0] = TYPE_SMS;
	buf[1] = (uint8_t)(hash >> 24);
	buf[2] = (uint8_t)(hash >> 16);
	buf[3] = (uint8_t)(hash >> 8);
	buf[4] = (uint8_t)hash;
	memcpy(buf + 5, saved_sms.msg, (size_t)len);
	if (!IS_DISABLED((uint32_t)peer_flags) && peer_ip && peer_port)
		sendtoip(peer_socket, peer_ip, peer_port, buf, len + 5);
	debugf(getdbgflag(DBG_CACHE,0,0), " SMS to peer (%s:%u)\n", peer_name, peer_port);
	save_sms_record(&saved_sms, peer_name, peer_port);
	if (hash_out) *hash_out = hash;
	return 1;
}

int cache_peer_sms_status_by_id(uint32_t peer_id, uint32_t hash)
{
	struct cachepeer_data *peer;
	struct sms_data *sms;
	int status = -1;
	pthread_mutex_lock(&prg.lockcache);
	peer = getpeerbyid((int)peer_id);
	if (peer) {
		for (sms = peer->sms; sms; sms = sms->next) {
			if (sms->hash == hash && (sms->status & 1)) {
				status = sms->status;
				break;
			}
		}
	}
	pthread_mutex_unlock(&prg.lockcache);
	return status;
}

void peer_check_messages( struct cachepeer_data *peer )
{
	// Remove Old Messages if there is too much
	struct sms_data *sms = peer->sms;
	int nb = 0;
	while (sms) {
		nb++;
		if (nb>=30) break;
		sms = sms->next;
	}
	// Remove Messages
	if (sms) {
		struct sms_data *next = sms->next;
		sms->next = NULL;
		while (next) {
			sms = next;
			next = sms->next;
			free(sms);
		}
	}
}
#endif /* NEWCACHE */






///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
// DCW CHECK & SET 
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////


/*
	look for older cw anf check for cwcycle
	return
	-1 : error
	0: nothing found
	1: found cycle
*/

int cache_fetch_goodcw( struct cache_data *thereq, uint8_t cw[16], cwcycle_t *cwcycle )
{
#ifdef TESTCHANNEL
	int testchannel = ( (thereq->caid==cfg.testchn.caid) && (!thereq->provid || thereq->provid==cfg.testchn.provid) && (thereq->sid==cfg.testchn.sid) );
	if (testchannel) {
		char dump[64];
		array2hex( cw, dump, 16);
		char dump2[64];
		array2hex( thereq->prevcw, dump2, 16);
		fdebugf(" (cache_fetch_goodcw.%x) ch %04x:%06x:%04x/%02x:%08x -> %s (%s) P:%s\n", thereq->flags&CACHE_FLAG_SENDPIPE, thereq->caid, thereq->provid, thereq->sid,
			thereq->tag, thereq->hash, dump, cwcycle2str(thereq->cwcycle), dump2 );
	}
#endif

	int index = thereq->sid&MAX_CACHE_INDEX;
	struct cache_data **cachetab = getcachetabbycaid(thereq->caid);
	/* TASK 3.4: no table for this CAID (allocation failed) -- no cycle verdict. */
	if (!cachetab) return 0;
	struct cache_data *pcache = cachetab[index];
	struct cache_data *result = NULL;
	uint32_t ticks = GetTickCount();
	while (pcache) {
		if ( cache_time_older_than(thereq->recvtime, pcache->recvtime, cfg.cache.filtertime)) {
			if ( cache_time_expired(ticks, pcache->recvtime, cfg.cache.alivetime) ) break;
			if ( (pcache->sid==thereq->sid)&&(pcache->hash!=thereq->hash) )
			if ( !cache_provider_records_conflict(pcache, thereq) )
			if ( pcache->tag && (pcache->tag!=thereq->tag) ) { // old oscam cacheex luck of ecmtag
				struct cw_cache_data *cwdata = pcache->cwdata;
				while (cwdata) {
					if ( dcwcmp16(cwdata->cw,cw) ) {
#ifdef TESTCHANNEL
						if (testchannel) {
							char dump[64];
							array2hex( cwdata->cw, dump, 16);
							fdebugf(" >> Same dcw, cache %04x:%06x:%04x/%02x:%08x::%s\n", pcache->caid, pcache->provid, pcache->sid, pcache->tag, pcache->hash, dump);
						}
#endif
						return DCW_ERROR; // not gooooooooooooood maybe another provider of same channel
					}
					else if ( !(cwdata->status&DCW_ERROR) ) {
						if ( dcwcmp8(cwdata->cw,cw) && !similarcw(cwdata->cw+8,cw+8) ) { // ????????????????????????????
							if (cwdata->cwcycle!=CW1CYCLE) {
								*cwcycle = CW1CYCLE; // CW1 changed
#ifdef TESTCHANNEL
								if (testchannel) {
									char dump[64];
									array2hex( cwdata->cw, dump, 16);
									fdebugf(" >> CW1 cycle(%d) cache %04x:%06x:%04x/%02x:%08x::%s\n", cwdata->status, pcache->caid, pcache->provid, pcache->sid, pcache->tag, pcache->hash, dump);
								}
#endif
								if (cwdata->status&DCW_CYCLE) return DCW_CYCLE;
								result = pcache;
							}
						}
						else if ( !similarcw(cwdata->cw,cw) && dcwcmp8(cwdata->cw+8,cw+8) ) {
							if (cwdata->cwcycle!=CW0CYCLE) {
								*cwcycle = CW0CYCLE; // CW0 changed
#ifdef TESTCHANNEL
								if (testchannel) {
									char dump[64];
									array2hex( cwdata->cw, dump, 16);
									fdebugf(" >> CW0 cycle(%d) cache %04x:%06x:%04x/%02x:%08x::%s\n", cwdata->status, pcache->caid, pcache->provid, pcache->sid, pcache->tag, pcache->hash, dump);
								}
#endif
								if (cwdata->status&DCW_CYCLE) return DCW_CYCLE; //2
								result = pcache;
							}
						}
					}
					cwdata = cwdata->next;
				}
			}
		}
		pcache = pcache->next;
		if (pcache==cachetab[index]) break;
	}
	if (result) return DCW_CYCLE; //1
	return 0; //DCW_NOCYCLE
}


// search for same cw in cache
int cache_check_cw( uint32_t recvtime, uint8_t tag, uint16_t caid, uint32_t hash,
                    uint16_t sid, uint32_t provid, int provider_known,
                    uint8_t cw[16], int cwpart )
{
	int index = sid&MAX_CACHE_INDEX;
	struct cache_data **cachetab = getcachetabbycaid(caid);
	/* TASK 3.4: no table for this CAID (allocation failed) -- nothing cached. */
	if (!cachetab) return 0;
	struct cache_data *pcache = cachetab[index];
	uint32_t ticks = GetTickCount();
	while (pcache) {
		if ( cache_time_older_than(recvtime, pcache->recvtime, cfg.cache.filtertime)) {
			if ( cache_time_expired(ticks, pcache->recvtime, cfg.cache.alivetime) ) break;
			/* A conflicting known provider cannot prove that this ECM repeated
			 * the same channel key. Unknown provider zero remains ambiguous, so
			 * the conservative half-key checks stay enabled. A complete 16-byte
			 * repeat is allowed below: one valid key can span multiple ECMs. */
			if ( (pcache->sid==sid)&&(pcache->hash!=hash)&&(pcache->tag!=tag) &&
			     !cache_provider_identity_conflicts(pcache, provid, provider_known) ) {
				struct cw_cache_data *cwdata = pcache->cwdata;
				while (cwdata) {
					switch (cwpart) {
						case 0:
							if ( dcwcmp8(cwdata->cw,cw) ) return 0;
							if ( dcwcmp8(cwdata->cw+8,cw) ) return 0;
							break;
						case 1:
							if ( dcwcmp8(cwdata->cw,cw+8) ) return 0;
							if ( dcwcmp8(cwdata->cw+8,cw+8) ) return 0;
							break;
						case 2:
							/* A complete CW can remain valid across several ECMs in
							 * one crypto period. Its exact repetition is not evidence
							 * of a stale answer; checkfreeze_setdcw() follows the same
							 * rule. Keep the half-key checks above for cwpart 0/1. */
							break;
					}
					cwdata = cwdata->next;
				}
			}
		}
		pcache = pcache->next;
		if (pcache==cachetab[index]) break;
	}
	return 1;
}


struct cw_cache_data *iscwincache(struct cache_data *pcache, uint8_t cw[16])
{
	struct cw_cache_data *cwdata = pcache->cwdata;
	while (cwdata) {
		if ( dcwcmp16(cwdata->cw, cw) ) return cwdata;
		cwdata = cwdata->next;
	}
	return NULL;
}

// update dcw for cache data of same channel with different hash and provider
struct cache_data *cache_fetch_goodcw2( struct cache_data *thereq, uint8_t cw[16], int peerid, uint8_t source_origin_mask )
{
#ifdef TESTCHANNEL
	int testchannel = ( (thereq->caid==cfg.testchn.caid) && (!thereq->provid || thereq->provid==cfg.testchn.provid) && (thereq->sid==cfg.testchn.sid) );
	if (testchannel) {
		char dump[64];
		array2hex( cw, dump, 16);
		fdebugf(" (cache_fetch_goodcw2) ch %04x:%06x:%04x/%02x:%08x -> %s\n", thereq->caid, thereq->provid, thereq->sid, thereq->tag, thereq->hash, dump);
	}
#endif
	int index = thereq->sid&MAX_CACHE_INDEX;
	struct cache_data **cachetab = getcachetabbycaid(thereq->caid);
	/* TASK 3.4: no table for this CAID (allocation failed) -- no dcwcheck probe. */
	if (!cachetab) return NULL;
	struct cache_data *pcache = cachetab[index];
	struct cache_data *result = NULL;
	uint32_t ticks = GetTickCount();
	while (pcache) {
		if ( cache_time_expired(ticks, pcache->recvtime, cfg.cache.alivetime) ) break;
		if ( cache_entry_sendpipe_active(pcache) )
		if ( (pcache->sid==thereq->sid)&&(pcache->hash!=thereq->hash) )
		if ( pcache->tag && (pcache->tag==thereq->tag) )
		if ( pcache->cwcycle!=NO_CYCLE )
		if ( !isnullDCW(pcache->prevcw) )
		if ( !iscwincache(pcache,cw) )
		{
			if (  ( (pcache->cwcycle==CW1CYCLE) && dcwcmp8(pcache->prevcw,cw) && !similarcw(pcache->prevcw+8,cw+8) ) ||
				( (pcache->cwcycle==CW0CYCLE) && !similarcw(pcache->prevcw,cw) && dcwcmp8(pcache->prevcw+8,cw+8) )  ) {

				if (cache_local_providers_conflict(pcache, thereq)) {
					user_trace_cache_observation(pcache->caid, pcache->provid, pcache->sid,
					    pcache->hash, peerid, "cache provider collision: different local provider; conflicting operation skipped", NULL);
				} else if (cache_provider_records_conflict(pcache, thereq)) {
					user_trace_cache_observation(pcache->caid, pcache->provid, pcache->sid,
					    pcache->hash, peerid, "cache provider collision: known numeric providers differ; cross-cache relay skipped", NULL);
				} else if (cfg.cache.consensus || cfg.cache.threshold > 1 ||
				           cfg.cache.gate_cwcheck > 1 ||
				           cache_entry_ecm_excheck_gt1(pcache)) {
					/* This CW is not in the target entry (iscwincache above).
					 * Source-entry votes/arrivals cannot establish the target's
					 * confirmation policy. Let its ordinary lookup continue. */
					user_trace_cache_observation(pcache->caid, pcache->provid, pcache->sid,
					    pcache->hash, peerid, "cache relay deferred: target confirmation not established", NULL);
				} else {
				pipe_cache2ecm_find_success_with_origin(pcache, cw, peerid,
					                                      (int)pcache->cwcycle,
					                                      source_origin_mask, 0);

#ifdef TESTCHANNEL
				if (testchannel) {
					char dump1[64];
					char dump2[64];
					array2hex( cw, dump1, 16);
					array2hex( pcache->prevcw, dump2, 16);
					fdebugf(" ==%s== cache %04x:%06x:%04x/%02x:%08x %s => %s\n", cwcycle2str(pcache->cwcycle), pcache->caid, pcache->provid, pcache->sid, pcache->tag, pcache->hash, dump2,dump1);
					fdebugf(" ==%s== Update %04x:%06x:%04x/%02x:%08x from %04x:%06x:%04x/%02x:%08x\n", cwcycle2str(pcache->cwcycle), 
						pcache->caid, pcache->provid, pcache->sid, pcache->tag, pcache->hash,
						thereq->caid, thereq->provid, thereq->sid, thereq->tag, thereq->hash );
				}
#endif
				}
			}
		}
		pcache = pcache->next;
		if (pcache==cachetab[index]) break;
	}
	return result;
}

// cache check3 
// update dcw for cache data of same caid,tag,hash with different sid
//[17:36:34.278] <- ecm from cs378x client 'm1' ch 0100:003315:233d:41353b6b
//[17:36:34.278] <- ecm from cs378x client 'm1' ch 0100:003315:2351:41353b6b

struct cache_data *cache_fetch_goodcw3( struct cache_data *thereq, uint8_t cw[16], int peerid, uint8_t source_origin_mask )
{
#ifdef TESTCHANNEL
	int testchannel = ( (thereq->caid==cfg.testchn.caid) && (!thereq->provid || thereq->provid==cfg.testchn.provid) && (thereq->sid==cfg.testchn.sid) );
	if (testchannel) {
		char dump[64];
		array2hex( cw, dump, 16);
		fdebugf(" (cache_fetch_goodcw3) ch %04x:%06x:%04x/%02x:%08x -> %s\n", thereq->caid, thereq->provid, thereq->sid, thereq->tag, thereq->hash, dump);
	}
#endif
	int index = thereq->sid&MAX_CACHE_INDEX;
	struct cache_data **cachetab = getcachetabbycaid(thereq->caid);
	/* TASK 3.4: no table for this CAID (allocation failed) -- no dcwcheck probe. */
	if (!cachetab) return NULL;
	struct cache_data *pcache = cachetab[index];
	struct cache_data *result = NULL;
	uint32_t ticks = GetTickCount();
	while (pcache) {
		if ( cache_time_expired(ticks, pcache->recvtime, cfg.cache.alivetime) ) break;
		if ( cache_entry_sendpipe_active(pcache) )
		if (pcache->sid!=thereq->sid)
		if ( (pcache->hash==thereq->hash) )
		if ( pcache->tag && (pcache->tag==thereq->tag) )
		if ( pcache->cwcycle!=NO_CYCLE )
		if ( !isnullDCW(pcache->prevcw) )
		if ( !iscwincache(pcache,cw) )
		{
			if (  ( (pcache->cwcycle==CW1CYCLE) && dcwcmp8(pcache->prevcw,cw) && !similarcw(pcache->prevcw+8,cw+8) ) ||
				( (pcache->cwcycle==CW0CYCLE) && !similarcw(pcache->prevcw,cw) && dcwcmp8(pcache->prevcw+8,cw+8) )  ) {

				if (cache_local_providers_conflict(pcache, thereq)) {
					user_trace_cache_observation(pcache->caid, pcache->provid, pcache->sid,
					    pcache->hash, peerid, "cache provider collision: different local provider; conflicting operation skipped", NULL);
				} else if (cache_provider_records_conflict(pcache, thereq)) {
					user_trace_cache_observation(pcache->caid, pcache->provid, pcache->sid,
					    pcache->hash, peerid, "cache provider collision: known numeric providers differ; cross-cache relay skipped", NULL);
				} else if (cfg.cache.consensus || cfg.cache.threshold > 1 ||
				           cfg.cache.gate_cwcheck > 1 ||
				           cache_entry_ecm_excheck_gt1(pcache)) {
					/* This CW is not in the target entry (iscwincache above).
					 * Source-entry votes/arrivals cannot establish the target's
					 * confirmation policy. Let its ordinary lookup continue. */
					user_trace_cache_observation(pcache->caid, pcache->provid, pcache->sid,
					    pcache->hash, peerid, "cache relay deferred: target confirmation not established", NULL);
				} else {
				pipe_cache2ecm_find_success_with_origin(pcache, cw, peerid,
					                                      (int)pcache->cwcycle,
					                                      source_origin_mask, 0);

#ifdef TESTCHANNEL
				if (testchannel) {
					char dump1[64];
					char dump2[64];
					array2hex( cw, dump1, 16);
					array2hex( pcache->prevcw, dump2, 16);
					fdebugf(" ==%s== cache %04x:%06x:%04x/%02x:%08x %s => %s\n", cwcycle2str(pcache->cwcycle), pcache->caid, pcache->provid, pcache->sid, pcache->tag, pcache->hash, dump2,dump1);
					fdebugf(" ==%s== Update %04x:%06x:%04x/%02x:%08x from %04x:%06x:%04x/%02x:%08x\n", cwcycle2str(pcache->cwcycle), 
						pcache->caid, pcache->provid, pcache->sid, pcache->tag, pcache->hash,
						thereq->caid, thereq->provid, thereq->sid, thereq->tag, thereq->hash );
				}
#endif
				}
			}
		}
		pcache = pcache->next;
		if (pcache==cachetab[index]) break;
	}
	return result;
}


// -1: bad cw
// 0: nothing to do
// 1: dcw set
// 2: dcw set & sent to pipe
/* Run the same post-consensus cycle/family gates for both immediately
 * corroborated and window-expired CWs. Caller holds prg.lockcache. */
static int cache_after_consensus(struct cache_data *req, struct cache_data *pcache,
                                 struct cw_cache_data *cwdata, uint8_t cw[16],
                                 cwcycle_t cwcycle, int peerid)
{
    int piped = 0;
    cwcycle_t incoming_cycle = cwcycle;
    uint8_t previous_status = cwdata->status;
    cwcycle_t previous_cycle = cwdata->cwcycle;
    uint32_t previous_peerid = cwdata->peerid;
	// Half Nulled CW
	char nullcw[8] = "\0\0\0\0\0\0\0\0";
	if ( !dcwcmp8(cw,nullcw) && !dcwcmp8(cw+8,nullcw) ) {

		if ( !isnullDCW(pcache->prevcw) ) {
			if (  ( (pcache->cwcycle==CW1CYCLE) && dcwcmp8(pcache->prevcw,cw) && !similarcw(pcache->prevcw+8,cw+8) ) ||
				( (pcache->cwcycle==CW0CYCLE) && !similarcw(pcache->prevcw,cw) && dcwcmp8(pcache->prevcw+8,cw+8) )  ) {
				// update new dcw
				cwdata->status |= DCW_CYCLE;
				cwdata->cwcycle = pcache->cwcycle;
				cwdata->peerid = peerid;
				cwdata->status |= DCW_PEER_READY;
				/* The source entry passed its cycle gate; only now may it
				 * be considered for the optional cross-entry relays. */
				if (cfg.cache.dcwcheck2) cache_fetch_goodcw2(pcache, cw, peerid, cwdata->origin_mask);
				if (cfg.cache.dcwcheck3) cache_fetch_goodcw3(pcache, cw, peerid, cwdata->origin_mask);
				if ( cache_entry_sendpipe_active(pcache) && !(cwdata->status&DCW_SENT) ) {
					pipe_cache2ecm_find_success(pcache, cw, peerid);
					piped = 1;   /* TASK R11 (D65) */
					cwdata->status |= DCW_SENT;
				}
				return cwdata->status;
			}
		}
		// cache without cw1 cycle
		if (cfg.cache.filter && (cwcycle==NO_CYCLE) ) {
			cwdata->status |= cache_fetch_goodcw(pcache, cw, &cwcycle);
			cwdata->cwcycle = cwcycle;
			cwdata->peerid = peerid;
			if (!(cwdata->status&DCW_CYCLE)) {
				/* R48 cycle strategy (opt-in): temporal estimation.
				 * When no marker is carried and the filter cannot
				 * establish the cycle, estimate it from the previous
				 * CW's half pattern: exactly one half shared with
				 * prevcw means the other half rotated. This is an
				 * estimate, not a declaration; the cycle gate below
				 * still judges it against the entry expectation.
				 * OFF (default) = stock refusal.
				 * NOTE (review): the half->cycle mapping below
				 * (first-half-stable => CW1, second-half-stable =>
				 * CW0) is an assumption, not provider-verified. A
				 * backwards mapping would misjudge the gate, so
				 * this stays opt-in OFF until a field capture
				 * confirms it per provider. */
				if (cfg.cache.cycle_estimate) {
					if (!isnullDCW(pcache->prevcw) && pcache->cwcycle != NO_CYCLE) {
						int first_same = dcwcmp8(cw, pcache->prevcw);
						int second_same = dcwcmp8(cw+8, pcache->prevcw+8);
						if (first_same && !second_same) {
							cwcycle = CW1CYCLE;
							cwdata->status |= DCW_CYCLE;
							cwdata->cwcycle = cwcycle;
							user_trace_cache_observation(req->caid, req->provid, req->sid, req->hash, peerid,
							    "cache cycle estimated temporally: CW1 (no marker)", cw);
						} else if (!first_same && second_same) {
							cwcycle = CW0CYCLE;
							cwdata->status |= DCW_CYCLE;
							cwdata->cwcycle = cwcycle;
							user_trace_cache_observation(req->caid, req->provid, req->sid, req->hash, peerid,
							    "cache cycle estimated temporally: CW0 (no marker)", cw);
						}
					} else if (isnullDCW(pcache->prevcw) && pcache->cwcycle == NO_CYCLE) {
						/* First CW for this channel: no prevcw and no expected
						 * cycle. Estimate from ecmtag assuming cw1cycle==0x80
						 * (the common default; cw1cycle is not stored in
						 * cache_data). If the provider uses cw1cycle==0x81,
						 * the estimate will be backwards and the cycle gate
						 * below will refuse the key (safe). This is still
						 * better than refusing all first CWs when consensus
						 * has already proceeded. */
						if (req->tag == 0x80) {
							cwcycle = CW1CYCLE;
							cwdata->status |= DCW_CYCLE;
							cwdata->cwcycle = cwcycle;
							user_trace_cache_observation(req->caid, req->provid, req->sid, req->hash, peerid,
							    "cache cycle estimated from ecmtag: CW1 (first CW)", cw);
						} else if (req->tag == 0x81) {
							cwcycle = CW0CYCLE;
							cwdata->status |= DCW_CYCLE;
							cwdata->cwcycle = cwcycle;
							user_trace_cache_observation(req->caid, req->provid, req->sid, req->hash, peerid,
							    "cache cycle estimated from ecmtag: CW0 (first CW)", cw);
						}
					}
				}
				if (!(cwdata->status&DCW_CYCLE)) {
					char detail[128];
					user_trace_cache_observation(req->caid, req->provid, req->sid, req->hash, peerid, "cache not forwarded: filter did not establish cycle", cw);
					snprintf(detail, sizeof(detail), "cache cycle context: incoming=%s; expected=%s; previous CW=%s",
					         cwcy_str((int)incoming_cycle), cwcy_str((int)pcache->cwcycle),
					         isnullDCW(pcache->prevcw) ? "absent" : "present");
					user_trace_cache_observation(req->caid, req->provid, req->sid, req->hash, peerid, detail, cw);
					return cwdata->status | DCW_ERROR | DCW_SKIP;
				}
			}
		}
		else {
			// update new dcw
			cwdata->status |= DCW_CYCLE;
			cwdata->cwcycle = cwcycle;
			cwdata->peerid = peerid;
		}
		// Check Cycle
		if ( (pcache->cwcycle!=NO_CYCLE)&&(pcache->cwcycle!=cwcycle) ) {
			/*
			 * TASK R2 (D56) -- the D9 remedy, at the ONLY site where this
			 * tree actually refuses a contradictory key. The refusal itself
			 * is stock r82a and stays untouched: the entry's expectation
			 * (pcache->cwcycle, written by the ECM FIND through the live
			 * rule -- put_ecm2cache() computes exactly cwcy_expect()) met a
			 * DECLARED wire mark for the other half. What stock never had
			 * is any way for the operator to SEE it: no counter, no line.
			 * D9 demanded measurement before policy, so where the profile
			 * arms `DCWFILTER CYCLE` (tristate over the global default
			 * OFF), the refusal is counted under MCS_DCWSTATS and logged
			 * with its reason. The counted verdict is the same CWCY_CONTRA
			 * the TASK 2.5 observation records one storey up: both halves
			 * declared and different. An UNMARKED key refused here (the
			 * strict FILTER-OFF shape) stays uncounted -- silence judged
			 * as silence, not as contradiction (GR3). Disarmed = the
			 * refusal happens exactly as stock, invisibly, byte for byte.
			 */
			if ( cwcy_judge(pcache->cwcycle, cwcy_observed((int)cwcycle)) == CWCY_CONTRA ) {
				struct cardserver_data *cyc_cs = cache_entry_cardserver_current(pcache);
				if (dcw_profile_cycle_enabled(cyc_cs)) {
#ifdef MCS_DCWSTATS
					DCWSTATS_REJECT(DCW_REJ_CYCLE);
					cwlog_refuse(DCW_REJ_CYCLE, peerid, req->caid, req->provid, req->sid, cw);
#endif
					debugf(getdbgflag(DBG_CACHE,0,0),
						" [CYCLE GATE] wrong-half key refused at ingest (expected %s, saw %s) ch %04x:%06x:%04x\n",
						cwcy_str(pcache->cwcycle), cwcy_str(cwcy_observed((int)cwcycle)),
						req->caid, req->provid, req->sid);
				{ struct cachepeer_data *pr_peer = getpeerbyid(peerid); /* TASK R4 (D58) */
				  if (pr_peer) peerrep_event(pr_peer, "cycle-contradiction"); }
				}
			}
			/*
			 * TASK R12 (D66) -- the node poison is gone; the refusal stays.
			 * Stock r82a marked the KEY'S node DCW_ERROR here, but the
			 * contradiction is between the DECLARED mark and the entry's
			 * expectation -- a verdict about this reply's declaration, not
			 * about the bytes. Two peers can carry the very same key with
			 * different marks (the R4 flood made B do it on purpose); the
			 * poisoned node then swallowed the honest reply at the status
			 * early-out above -- silently, before any gate, counter or
			 * ring row -- and the client starved (reproduced live: honest
			 * mark matching the expectation, zero-DCW timeout). This reply
			 * is still refused (the return below); a repeat with the same
			 * wrong mark is refused here again, per reply; an honest mark
			 * on the same bytes now flows through and delivers. Content-
			 * based refusals (half-nulled non-NDS) and the consensus
			 * convictions keep their node marks: those judge the bytes.
			 */
			{
				char detail[192];
				/* R48 late-vs-wrong (observe only): late = bytes share
				 * exactly one half with prevcw (stale period, correct
				 * key material); wrong = neither half matches (bytes
				 * judge the key, not just the declaration). */
				const char *cls = "wrong";
				if (!isnullDCW(pcache->prevcw)) {
					int f = dcwcmp8(cw, pcache->prevcw);
					int s = dcwcmp8(cw+8, pcache->prevcw+8);
					if (f != s) cls = "late";
				}
				snprintf(detail, sizeof(detail), "cache cycle mismatch: expected=%s; assessed=%s; incoming=%s; class=%s",
				         cwcy_str((int)pcache->cwcycle), cwcy_str((int)cwcycle), cwcy_str((int)incoming_cycle), cls);
				user_trace_cache_observation(req->caid, req->provid, req->sid, req->hash, peerid, detail, cw);
			}
			/* A rejected declaration must not make a new stored candidate
			 * look cycle-approved to a later FIND or peer request. Preserve
			 * any prior accepted declaration for these same CW bytes. */
			cwdata->status = (cwdata->status & ~DCW_CYCLE) | (previous_status & DCW_CYCLE);
			cwdata->cwcycle = previous_cycle;
			cwdata->peerid = previous_peerid;
			return cwdata->status | DCW_ERROR | DCW_SKIP;
		}
	}
	else {
		// half nulled cw: exit if non-nds
		if ( !ca_halfnull_ok(req->caid) ) { /* TASK R36 (D92): CAID FAMILY HALFNULLED (stock: 09 only) */
		user_trace_cache_observation(req->caid, req->provid, req->sid, req->hash, peerid, "cache rejected: half-null not allowed for CAID family", cw);
			cwdata->status |= DCW_ERROR;
			return cwdata->status;
		}
		//
		if ( dcwcmp8(cw,nullcw) ) cwcycle = CW1CYCLE;
		else if ( dcwcmp8(cw+8,nullcw) ) cwcycle = CW0CYCLE;
		// update new dcw
		cwdata->status |= DCW_CYCLE;
		cwdata->cwcycle = cwcycle;
		cwdata->peerid = peerid; // CSP CACHE
	}

	cwdata->status |= DCW_PEER_READY;
	/* Do not relay a source candidate that its own cycle/family gate
	 * rejected. This is after the final gate on the normal path. */
	if (cfg.cache.dcwcheck2) cache_fetch_goodcw2(pcache, cw, peerid, cwdata->origin_mask);
	if (cfg.cache.dcwcheck3) cache_fetch_goodcw3(pcache, cw, peerid, cwdata->origin_mask);

	if ( cache_entry_sendpipe_active(pcache) && !(cwdata->status&DCW_SENT) ) {
		cwdata->status |= DCW_SENT;
		/*
		 * TASK 3.16 rig. Unset, this is one compare. A positive
		 * MCS_316_HOLD_MS (capped at 500) sleeps here, still holding
		 * the caller's lockcache, so the ECM thread can commit FAILED
		 * and block in cache_clear_sendpipe before the push is piped.
		 * The handler then sees a dead waiter. Production does not
		 * set the variable.
		 */
		{
			static int hold_ms = -1;
			if (hold_ms < 0) {
				const char *e = getenv("MCS_316_HOLD_MS");
				hold_ms = e ? atoi(e) : 0;
				if (hold_ms < 0) hold_ms = 0;
				if (hold_ms > 500) hold_ms = 500;
			}
			if (hold_ms) usleep((useconds_t)hold_ms * 1000);
		}
		pipe_cache2ecm_find_success(pcache, cw, peerid);
		piped = 1;   /* TASK R11 (D65) */
#ifdef TESTCHANNEL
		int testchannel = ( (pcache->caid==cfg.testchn.caid) && (!pcache->provid || pcache->provid==cfg.testchn.provid) && (pcache->sid==cfg.testchn.sid) );
		if (testchannel) {
			char dump[64];
			array2hex( cw, dump, 16);
			fdebugf(" (Cache->Setdcw) ch %04x:%06x:%04x/%02x:%08x -> %s (%s)\n", pcache->caid, pcache->provid, pcache->sid, pcache->tag, pcache->hash,
				dump, cwcycle2str(pcache->cwcycle) );
		}
#endif
	}
#ifdef MCS_DCWSTATS
	/* TASK R11 (D65): the no-waiter seeds (no SENDPIPE, nothing piped)
	 * end here silently; the ring records them as STORED so the page
	 * shows the whole intake, not only what reached an ECM. */
	if ( !piped && !(cwdata->status & DCW_SENT) )
		cwlog_note(CWLOG_STORED, 0, peerid, req->caid, req->provid, req->sid, cw, GetTickCount());
#endif
	return cwdata->status;
}

#ifdef CACHEEX
/* Record a cache offer before cache-level validation, but only when this exact
 * cache entry is attached to a live local ECM snapshot. CSP does not carry an
 * ECMD5, so its identity must come from that saved request context; CacheEX
 * additionally has an on-wire ECMD5, which must match before it can be joined
 * to the local request. No raw ECM pointer is dereferenced under lockcache. */
static void cache_ledger_note_candidate_locked(const struct cache_data *req,
                                               const struct cache_data *entry,
                                               const uint8_t cw[16], int peerid)
{
	struct ledger_identity identity;
	int exchange;
	if (!req || !entry || !cw || !cache_entry_sendpipe_active(entry) ||
	    !cache_ecm_snapshot_ledger_identity_locked(entry->ecm,
	                                               entry->ecm_lifetime_id,
	                                               &identity)) return;
	if (identity.caid != req->caid || identity.sid != req->sid ||
	    identity.hash != req->hash) return;

	exchange = cex_is_exchange(peerid);
	if (exchange) {
		/* CacheEX carries the byte identity and an explicit provider value.
		 * Reject an absent/mismatched digest rather than borrowing the
		 * active request's identity on the strength of the 32-bit cache key. */
		if (!ledger_key_valid(req->ecmd5) ||
		    memcmp(identity.ecmd5, req->ecmd5, sizeof(identity.ecmd5)) != 0 ||
		    (req->tag && req->tag != identity.tag) ||
		    !cache_request_provider_known(req)) return;
		/* The candidate is keyed by the provider the peer actually supplied.
		 * A conflicting value is kept separate from the active request and
		 * cannot become its acceptance or failure evidence. */
		identity.provid = req->provid;
		identity.provider_known = 1;
	} else if (req->tag != identity.tag) {
		/* A provider-less CSP reply must still match the saved ECM tag. */
		return;
	}
	ledger_note_candidate_identity(&identity, cw, DCW_SOURCE_CACHE, peerid,
	                               GetTickCount());
}
#endif

int cache_setdcw( struct cache_data *req, uint8_t cw[16], cwcycle_t cwcycle, int peerid )
{
	/*
	 * TASK 3.15 — the waiting ECM's profile, if it has one. No waiter
	 * means the global gate, which is what an unsolicited push saw
	 * before. A profile that inherits also sees the global gate.
	 *
	 * TASK R1 — the exchange gates. Everything arriving from the TCP
	 * cache-exchange family (cccam/camd35/cs378x cacheex peers, both
	 * directions; never CSP and never a card server) can now be gated
	 * per profile: LOCAL_ONLY drops unsolicited pushes before the cache
	 * entry is even created, BLOCK_FAKE_CW forces the deterministic r82a
	 * content tests, CWCHECK raises a confirmation floor. CSP is
	 * deliberately out of scope (it has FILTER/THRESHOLD/cacheguard);
	 * unarmed profiles behave exactly as before, instruction for
	 * instruction.
	 */
	int cex_exchange;
	int cex_gate_fake, cex_gate_check;
	int has_waiter;
	uint32_t validation_provider;
	uint32_t gate_provider;
	int gate_provider_known;
	struct cache_data *pend;
	struct cardserver_data *pcs;
	user_trace_cache_observation(req->caid, req->provid, req->sid, req->hash, peerid, "cache CW entered validation; decision pending", cw);
	{
		pend = cache_fetch(req);
		/* The ECM pointer remains in an entry after completion for identity
		 * checks, so it alone is not evidence of a live waiter. SENDPIPE is
		 * cleared when that request succeeds or fails; the status guard also
		 * closes the short commit-before-clear window. */
		has_waiter = cache_entry_sendpipe_active(pend);
		pcs = has_waiter ? cache_entry_cardserver_current(pend) : NULL;
		if (has_waiter && !cache_entry_sendpipe_active(pend)) {
			has_waiter = 0;
			pcs = NULL;
		}
		cex_exchange = cex_is_exchange(peerid);
#ifdef CACHEEX
		/* A rejected cache offer used to disappear here: the later ledger hook
		 * lives in ECM Set-DCW, which no early cache gate reaches. Record only
		 * against the exact active ECM snapshot (and require the wire ECMD5 for
		 * CacheEX) before provider/profile/content gates can return. */
		cache_ledger_note_candidate_locked(req, pend, cw, peerid);
#endif
		int gate_local = cfg.cache.gate_localonly;
		cex_gate_fake  = cfg.cache.gate_blockfake;
		cex_gate_check = cfg.cache.gate_cwcheck;

		/* CacheEX carries a provider; CSP does not. A source with a known,
		 * conflicting provider must not be folded into the entry or piped to
		 * another provider's pending ECM. */
		if (cache_provider_identity_conflicts(pend, req->provid, cex_exchange)) {
			user_trace_cache_observation(req->caid, req->provid, req->sid, req->hash, peerid,
			    "cache provider collision: explicit incoming provider conflicts with known cache/local provider; CW refused", cw);
			return DCW_ERROR;
		}

		/* Evidence gates use the best identity for the cache entry, not the
		 * provider-less CSP wire value. An explicit CacheEX/provider value wins;
		 * otherwise use the cache's known local/metadata scope. With no known
		 * context, retain the existing unknown-zero scope rather than widening
		 * the gate to every provider. Keep req->provid untouched for tracing. */
		gate_provider = req->provid;
		gate_provider_known = cex_exchange || cache_request_provider_known(req);
		if (!gate_provider_known)
			gate_provider_known = cache_consensus_provider_scope(pend, &gate_provider);
		if (!gate_provider_known) gate_provider = req->provid;

		/* A provider-less CSP reply may be checked against the exact active
		 * local ECM it answered. Keep req->provid untouched for source tracing:
		 * zero still correctly records that CSP supplied no provider. */
		validation_provider = req->provid;
		if (!validation_provider && has_waiter) {
			if (pend->local_provider_set) validation_provider = pend->local_provider;
			else if (pend->provid) validation_provider = pend->provid;
			if (validation_provider)
				user_trace_cache_observation(req->caid, 0, req->sid, req->hash, peerid,
				    "CSP supplied no provider; profile validation used the active local ECM provider only", cw);
		}
#ifdef CACHEEX
		if (pcs) {
			if (pcs->option.fexblockfake) cex_gate_fake = 1;
			if (pcs->option.excheck > cex_gate_check) cex_gate_check = pcs->option.excheck;
		}
#endif
		/* LOCAL_ONLY: an exchange push with no waiting local ECM never
		 * seeds the cache -- returned BEFORE cache_new(), so nothing is
		 * stored, nothing is served later, and the key cannot pre-poison
		 * a future request. */
		if ( cex_localonly_drop(gate_local, cex_exchange, has_waiter) ) {
			user_trace_cache_observation(req->caid, req->provid, req->sid, req->hash, peerid, "cache rejected: CacheEX local-only gate", cw);
#ifdef MCS_DCWSTATS
			DCWSTATS_REJECT(DCW_REJ_CEXLOCAL);
			cwlog_refuse(DCW_REJ_CEXLOCAL, peerid, req->caid, req->provid, req->sid, cw);
#endif
			debugf(getdbgflag(DBG_CACHE,0,0), " [CACHEEX GATE] LOCAL_ONLY: unsolicited push dropped ch %04x:%06x:%04x\n", req->caid, req->provid, req->sid);
			return -1;
		}
		int validation_reason = DCW_VALID, checksum_exemption_used = 0;
		if ( !acceptDCW_profile_ecm_trace_reason(cw, pcs, req->caid, validation_provider, req->sid,
		                                               &validation_reason, &checksum_exemption_used) ) {
		const char *why = "cache rejected: profile CW validation; specific check unknown";
		switch (validation_reason) {
		case DCW_INVALID_CHECKSUM: why = "cache rejected: profile CW checksum validation failed"; break;
		case DCW_INVALID_NULL: why = "cache rejected: profile CW null-word validation failed"; break;
		case DCW_INVALID_REPEAT: why = "cache rejected: profile CW repeated-byte validation failed"; break;
		case DCW_INVALID_BADLIST: why = "cache rejected: profile CW operator BAD-DCW list"; break;
		case DCW_INVALID_PATTERN: why = "cache rejected: profile CW enabled pattern validation"; break;
		default: break;
		}
		user_trace_cache_observation(req->caid, req->provid, req->sid, req->hash, peerid, why, cw);
		user_trace_cache_observation(req->caid, req->provid, req->sid, req->hash, peerid,
		    pcs ? "cache CW validation policy: attached ECM profile" : "cache CW validation policy: global; no attached ECM profile", cw);
		/*
		 * TASK 2.4 -- the blind spot this closes, found by running the live
		 * target and watching nothing arrive.
		 *
		 * Every evidence hook in this project (TASK 1.8's structural scan,
		 * 2.2's entropy and collision scan, 2.4's plausibility scan) is placed
		 * in setdcw.c ABOVE the acceptDCW() gate, precisely so that the key
		 * most worth examining -- the one about to be discarded -- is examined
		 * anyway. For a key that arrives from a card server that holds, because
		 * srv-newcamd.c calls ecm_setdcw() and setdcw.c does the gating.
		 *
		 * A key that arrives THROUGH THE CACHE does not: this function gates
		 * first and returns -1, so the key never reaches setdcw.c and no layer
		 * ever sees it. Measured live: four forged keys from a cache peer, four
		 * replies, zero deliveries and a stats line reading
		 * `0 full, 0 one-group` -- the layer was blind, not quiet.
		 *
		 * The call is on the REJECTING path only, so a key that passes is
		 * counted exactly once, where it is delivered. The source attribution is
		 * the cache's own: (DCW_SOURCE_CACHE, peerid), the same pair
		 * ecm_setdcw() uses a few lines below, so GR1 holds as before.
		 */
		cwp_note(DCW_SOURCE_CACHE, peerid, cw, req->caid, gate_provider,
		         gate_provider_known, req->sid, GetTickCount());
		cm_note(DCW_SOURCE_CACHE, peerid, cw, req->caid, gate_provider,
		        gate_provider_known, req->sid, GetTickCount());
#ifdef MCS_DCWSTATS
		/* TASK R11 (D65): the specific test (checksum/null/repeat/
		 * badlist) is counted inside dcw.c; the ring row carries the
		 * channel and the peer, which dcw.c cannot see here. */
		cwlog_refuse(CWLOG_R_PROFILE, peerid, req->caid, req->provid, req->sid, cw);
#endif
		return -1;
	}
		if (checksum_exemption_used)
			user_trace_cache_observation(req->caid, req->provid, req->sid, req->hash, peerid,
			    "cache CW passed profile checks under scoped checksum exemption; validity and picture unknown", cw);
		/* TASK R1 — BLOCK_FAKE_CW: the two deterministic r82a content
		 * tests, forced for exchange arrivals when the gate is armed.
		 * The rejected key gets the same evidence treatment as a
		 * profile-gate rejection (the TASK 2.4 doctrine: the key most
		 * worth examining is the one about to be discarded). */
		if ( cex_fake_reject(cex_gate_fake, cex_exchange,
		                     checksumDCW(cw) && !isbadDCW(cw)) ) {
            user_trace_cache_observation(req->caid, req->provid, req->sid, req->hash, peerid,
                "cache rejected: CacheEX BLOCK_FAKE_CW gate", cw);
#ifdef MCS_DCWSTATS
			DCWSTATS_REJECT(DCW_REJ_CEXFAKE);
			cwlog_refuse(DCW_REJ_CEXFAKE, peerid, req->caid, req->provid, req->sid, cw);
#endif
			{ struct cachepeer_data *pr_peer = getpeerbyid(peerid); /* TASK R4 (D58) */
			  if (pr_peer) peerrep_event(pr_peer, "cacheex-fake-cw"); }
			cwp_note(DCW_SOURCE_CACHE, peerid, cw, req->caid, gate_provider,
			         gate_provider_known, req->sid, GetTickCount());
			cm_note(DCW_SOURCE_CACHE, peerid, cw, req->caid, gate_provider,
			        gate_provider_known, req->sid, GetTickCount());
			debugf(DBG_ERROR, " !!! [CACHEEX GATE] BLOCK_FAKE_CW: fake cw from exchange peer (%x|%d) ch %04x:%06x:%04x\n",
				peerid & PEER_ORIGIN_FLAGS, peerid & PEER_ID_MASK,
				req->caid, req->provid, req->sid);
			return -1;
		}
	}

	/*
	 * TASK 2.6 -- negative memory gate. A key that a definitive proof (the
	 * ledger's dispute-plus-client-failure, or a reuse proof) has convicted
	 * never enters the cache again: no storage, no peer-agreement count, no
	 * delivery -- to this client or any other. Placed AFTER the plausibility
	 * note on purpose, so 2.4's counting of refused keys is unchanged, and
	 * BEFORE the entry lookup, so a refused key costs nothing at all. The
	 * gate only answers "are these bytes remembered poison for this
	 * channel"; every score and every disable remains the proof's job.
	 */
	if ( cwn_gate(cw, req->caid, gate_provider, gate_provider_known,
	              req->sid, GetTickCount()) ) {
		user_trace_cache_observation(req->caid, req->provid, req->sid, req->hash, peerid, "cache rejected: negative memory gate", cw);
#ifdef MCS_DCWSTATS
		cwlog_refuse(CWLOG_R_CONVICTED, peerid, req->caid, req->provid, req->sid, cw);
#endif
		return DCW_ERROR;
	}

	/*
	 * TASK R30 (D81) -- attestation gate. A key that independent clients
	 * filed by re-asking too soon after the same delivered bytes gets
	 * exactly the negative-memory treatment -- refused before storage,
	 * agreement counting or delivery -- except that the filing EXPIRES
	 * (CWATTEST_TTL_MS): attestation is strong evidence, not a GR3
	 * proof. No-op unless CACHE ATTEST is armed.
	 */
	if ( attest_gate(cw, req->caid, gate_provider, gate_provider_known,
	                 req->sid, GetTickCount()) ) {
		user_trace_cache_observation(req->caid, req->provid, req->sid, req->hash, peerid, "cache rejected: attestation gate", cw);
#ifdef MCS_DCWSTATS
		cwlog_refuse(CWLOG_R_CONVICTED, peerid, req->caid, req->provid, req->sid, cw);
#endif
		return DCW_ERROR;
	}

	// Search for Cache data
	struct cw_cache_data *cwdata = NULL;
	struct cache_data *pcache = cache_fetch( req );
	if (pcache==NULL) pcache = cache_new( req );
	else {
		if (!pcache->tag) pcache->tag = req->tag; // set tag if not set (coming from cahceex)
		if (!pcache->provid && req->provid) pcache->provid = req->provid; // set provid if not set (coming from csp)
		// check for dcw
		cwdata = pcache->cwdata;
		while (cwdata) {
			if ( dcwcmp16(cwdata->cw, cw) ) break;
			cwdata = cwdata->next;
		}
	}
	/* TASK 3.4: neither a fetch nor a new entry was possible (allocation
	 * failed). The report is dropped this round -- the key is not stored and
	 * not counted -- which is the same outcome a cache entry past its alive
	 * time already produces; the peers' own retry brings it back. */
	if (!pcache) {
        user_trace_cache_observation(req->caid, req->provid, req->sid, req->hash, peerid,
            "cache rejected: cache entry unavailable", cw);
        return DCW_ERROR;
    }
	if (cex_exchange) {
		/* CacheEX has a provider field even when its numeric value is zero. */
		if (!pcache->provider_known && !pcache->provid) pcache->provid = req->provid;
		pcache->provider_known = 1;
	}

	/*
	 * TASK 2.5 -- what the peer OFFERED, judged against the half the pending
	 * ECM declared (pcache->cwcycle, written by the ECM thread's FIND from the
	 * channel's own rule). The observed marker is the peer's own declaration
	 * on the wire -- buf[29] for forwarding CSP peers, always NO_CYCLE for
	 * cache-ex -- sanitised to the three wire values; a fresh entry (no
	 * pending ECM) has cwcycle NO_CYCLE, so the observation lands UNJUDGED
	 * instead of inventing an expectation. The note happens BEFORE any gate
	 * below: a contradiction is evidence about the peer whether or not the
	 * existing Check Cycle gate eats the key. It cannot change anything here
	 * -- it counts and, at most, writes one line per window (GR3: evidence,
	 * never an accusation).
	 */
	ccy_note(DCW_SOURCE_CACHE, peerid, req->caid, req->provid, req->sid,
	         GetTickCount(), CWCY_AT_RECV,
	         pcache->cwcycle, cwcy_observed((int)cwcycle));

	// add new if not found
	if (!cwdata) {
		cwdata = malloc( sizeof(struct cw_cache_data) );
		/* TASK 3.4: the agreement record for this key could not be extended;
		 * dropping the report keeps the counters honest (no phantom peer was
		 * counted, no key was delivered on a bookkeeping failure). */
		if (!cwdata) {
            user_trace_cache_observation(req->caid, req->provid, req->sid, req->hash, peerid,
                "cache rejected: CW record allocation failed", cw);
            return DCW_ERROR;
        }
		memset( cwdata, 0, sizeof(struct cw_cache_data) );
		memcpy(cwdata->cw, cw, 16);
		cwdata->status = 0;
		cwdata->cwcycle = NO_CYCLE;
		cwdata->peerid = peerid;
		cwdata->next = pcache->cwdata;
		pcache->cwdata = cwdata;
	}
	/* cache_setdcw() is a peer-ingress path; add the class only when the
	 * peer-origin encoding proves that fact. This is provenance, not trust. */
	cwdata->origin_mask = cache_origin_merge(cwdata->origin_mask,
	        cache_origin_for_source(DCW_SOURCE_CACHE, peerid));
	cwdata->nbpeers++;

	if (cwdata->status&DCW_ERROR) {
		/* R48 cycle strategy (opt-in): the error mark is scoped by cycle.
		 * If this arrival declares a different cycle than the one the
		 * error was recorded under, the error expires with the cycle
		 * change and the key is re-evaluated. Content-based errors
		 * (half-null, checksum) keep their mark: they judge the bytes,
		 * not the declaration. OFF (default) = stock swallow. */
		if (cfg.cache.cycle_error_reset && cwcycle != NO_CYCLE &&
		    cwdata->cwcycle != NO_CYCLE && cwdata->cwcycle != cwcycle) {
			cwdata->status &= ~DCW_ERROR;
			user_trace_cache_observation(req->caid, req->provid, req->sid, req->hash, peerid,
			    "cache error mark expired on cycle change; re-evaluating CW", cw);
		} else {
			user_trace_cache_observation(req->caid, req->provid, req->sid, req->hash, peerid,
			    "cache rejected: existing CW record marked error; original cause unknown", cw);
			return DCW_ERROR;
		}
	}

	// TEST for min Peers
	/*
	 * TASK 1.11a — was `nbpeers != cfg.cache.threshold`.
	 *
	 * `!=` is not a minimum. With the default threshold of 1 it behaves like
	 * one, so the bug is invisible in a default install, but `CACHE THRESHOLD: 2`
	 * accepted a CW on exactly the second peer and rejected it for the third and
	 * every peer after that. The comparison is now a real floor; see
	 * cache_threshold.h for the full argument and test_cache_threshold.c for the
	 * cases.
	 */
	if ( !cache_threshold_reached(cwdata->nbpeers, cfg.cache.threshold) ) {
		user_trace_cache_observation(req->caid, req->provid, req->sid, req->hash, peerid, "cache held: threshold not reached", cw);
#ifdef MCS_DCWSTATS
		/* TASK R11 (D65): the stock skip, now visible per key. Never
		 * fires at the default threshold of 1. */
		cwlog_refuse(CWLOG_R_THRESHOLD, peerid, req->caid, req->provid, req->sid, cw);
#endif
		return DCW_ERROR | DCW_SKIP;
	}

	/* TASK R1 — CWCHECK: an exchange arrival waits for confirmations.
	 * The same CW (the existing nbpeers arrival counter, any cache
	 * source) must cross the floor before it is served; until then the
	 * normal DCW TIMEOUT flow to real servers is untouched. Placed
	 * exactly at the stock threshold site, with the stock SKIP result:
	 * a held key is not an error event, it is an unconfirmed one. */
	if ( cex_check_below_floor(cex_gate_check, cfg.cache.threshold,
	                           cex_exchange, cwdata->nbpeers) ) {
		user_trace_cache_observation(req->caid, req->provid, req->sid, req->hash, peerid, "cache held: CacheEX confirmation threshold", cw);
#ifdef MCS_DCWSTATS
		DCWSTATS_REJECT(DCW_REJ_CEXCONF);
		cwlog_refuse(DCW_REJ_CEXCONF, peerid, req->caid, req->provid, req->sid, cw);
#endif
		debugf(getdbgflag(DBG_CACHE,0,0), " [CACHEEX GATE] CWCHECK: holding cw, %d/%d confirmations ch %04x:%06x:%04x\n",
			cwdata->nbpeers,
			cex_gate_check > (int)cfg.cache.threshold ? cex_gate_check : (int)cfg.cache.threshold,
			req->caid, req->provid, req->sid);
		return DCW_ERROR | DCW_SKIP;
	}

	/*
	 * TASK R8 (D62) -- the consensus verdict. Armed by CACHE CONSENSUS: ON,
	 * this is the point where a structurally clean key still has to answer
	 * for WHO vouches for it. HOLD = stock CWCHECK's skip shape (the key is
	 * stored, nothing delivers, the window walk or a second distinct voter
	 * resumes it); REFUSE = lost a weighted decision, node dead + counter +
	 * ladder event. OFF (default) = zero cost, zero lines, stock byte for
	 * byte. CSP/unknown senders vote with full weight: they are the other
	 * layers' problem (FILTER/THRESHOLD/cacheguard), as documented in D62.
	 */
	if (cfg.cache.consensus) {
		/* TASK R29 (D80): apply the operator's corroboration
		 * threshold (0/unset = engine default 2). Cheap enough to
		 * restate per arrival; a config reload takes effect on the
		 * very next push. */
		cwconsensus_set_min_sources(cfg.cache.consensus_sources >= 2
				    ? cfg.cache.consensus_sources : 2);
		cwconsensus_set_independence(cfg.cache.consensus_independence);
		cwconsensus_set_quarantine(cfg.cache.quarantine);
		cwconsensus_set_storm_guard(cfg.cache.storm_guard);
		cwconsensus_set_rotation_freeze(cfg.cache.rotation_freeze);
		r48_source_rep_set_cap30(cfg.cache.cap30);
		int cw_weight = 1;
		uint32_t cw_group = 0;
		{
			struct cachepeer_data *vc_peer = getpeerbyid(peerid);
			if ( peerrep_on && vc_peer &&
			     (peerrep_stage(host_ip_snapshot(vc_peer->host), vc_peer->port,
		                    GetTickCount()) >= PEERREP_STAGE_DISTRUST) )
				cw_weight = 0; /* bounded legacy deweight; does not alter R48 source/provider facts */
			/* R48 independence: same host IP = one source. */
			if (cfg.cache.consensus_independence && vc_peer)
				cw_group = host_ip_snapshot(vc_peer->host);
		}
		{
			uint32_t cwin = (uint32_t)cfg.cache.consensus_window;
			const uint8_t *cwl = NULL;
			uint32_t consensus_provider = 0;
			int consensus_provider_known = cache_consensus_provider_scope(pcache, &consensus_provider);
			int cv;
			if (cwin < 100) cwin = 100;
			if (cwin > 2000) cwin = 2000;
			uint32_t vote_tick = GetTickCount();
			cv = cwconsensus_verdict_group(req->caid, req->sid, consensus_provider,
			                         consensus_provider_known, req->hash, req->tag,
			                         cw, peerid, cw_group, cw_weight, vote_tick, cwin, &cwl);
			{
				int distinct = 0, weight = 0;
				char detail[128];
				int known = cwconsensus_vote_counts(req->caid, req->sid, consensus_provider,
				                                    consensus_provider_known, req->hash,
				                                    req->tag, cw, vote_tick, cwin,
				                                    &distinct, &weight);
				if (known) {
					snprintf(detail, sizeof(detail), "cache consensus vote: verdict=%s; peer IDs=%d; weight=%d; threshold=%d",
					         cv==CWC_HOLD ? "held" : (cv==CWC_REFUSE ? "refused" : "proceed"),
					         distinct, weight, cfg.cache.consensus_sources >= 2 ? cfg.cache.consensus_sources : 2);
				} else {
					snprintf(detail, sizeof(detail), "cache consensus vote: no candidate slot; verdict=%s; agreement unconfirmed",
					         cv==CWC_DELIVER ? "proceed" : "other");
				}
				user_trace_cache_observation(req->caid, req->provid, req->sid, req->hash, peerid, detail, cw);
				if (!known) {
					struct cwconsensus_capacity cap;
					cwconsensus_capacity(vote_tick, cwin, &cap);
					snprintf(detail, sizeof(detail), "cache consensus capacity: used=%d/%d; held=%d; overdue=%d; reclaimable=%d; oldest hold=%u ms; window=%u ms",
					         cap.used, cap.capacity, cap.held, cap.overdue, cap.reclaimable,
					         cap.oldest_hold_ms, cwin);
					user_trace_cache_observation(req->caid, req->provid, req->sid, req->hash, peerid, detail, cw);
				}
			}
			if ( cv==CWC_HOLD ) {
				cwconsensus_hold_cycle(req->caid, req->sid, consensus_provider,
				                        consensus_provider_known, req->hash,
				                        req->tag, cw, peerid, cwcycle);
				user_trace_cache_observation(req->caid, req->provid, req->sid, req->hash, peerid, "cache held: consensus awaiting corroboration", cw);
#ifdef MCS_DCWSTATS
				cwlog_hold(peerid, req->caid, req->provid, req->sid, cw);
#endif
				debugf(getdbgflag(DBG_CACHE,0,0),
					" [CONSENSUS] holding cw for corroboration ch %04x:%06x:%04x\n",
					req->caid, req->provid, req->sid);
				return DCW_ERROR | DCW_SKIP;
			}
			if ( cv==CWC_REFUSE ) {
				user_trace_cache_observation(req->caid, req->provid, req->sid, req->hash, peerid,
				    cwl ? "cache rejected: consensus weighted decision" : "cache rejected: consensus decision; weighted loss not established for this arrival", cw);
#ifdef MCS_DCWSTATS
				DCWSTATS_REJECT(DCW_REJ_CONSENSUS);
				cwlog_refuse(DCW_REJ_CONSENSUS, peerid, req->caid, req->provid, req->sid, cw);
#endif
				{
					struct cachepeer_data *rl_peer = getpeerbyid(peerid);
					/* Only a weighted loss returns conflicting CW evidence. A timing
					 * tie or sticky refusal does not condemn this sender. */
					if (rl_peer && cwl) peerrep_event(rl_peer, "consensus-mismatch");
				}
				cwdata->status |= DCW_ERROR;
				debugf(getdbgflag(DBG_CACHE,0,0),
					" [CONSENSUS] cw refused: lost the weighted decision for ch %04x:%06x:%04x (peer %d)\n",
					req->caid, req->provid, req->sid, peerid & PEER_ID_MASK);
				return cwdata->status;
			}
			if ( cwl ) {
				/* won against a HELD loser: kill its node so the
				 * window walk can never release it later */
				struct cw_cache_data *clw = pcache->cwdata;
				while (clw) {
					if ( dcwcmp16(clw->cw, cwl) ) { clw->status |= DCW_ERROR; break; }
					clw = clw->next;
				}
				debugf(getdbgflag(DBG_CACHE,0,0),
					" [CONSENSUS] cw delivered on the weighted decision for ch %04x:%06x:%04x\n",
					req->caid, req->provid, req->sid);
			}
		}
	}

	return cache_after_consensus(req, pcache, cwdata, cw, cwcycle, peerid);
}



///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
// RECEIVE MESSAGES
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

/*
 * TASK R6 (D60) -- the bounded queue between the socket and the parser.
 * Compile flag CACHE_QUEUE, OFF by default: the default binary never sees
 * any of this (the release-queue flavours turn it on). With the flag ON,
 * the receive thread only drains the sockets -- recvfrom plus the wire
 * range check, zero locks -- and hands raw datagrams to one worker
 * through the fixed cachequeue ring; the whole parse/forward/lockcache
 * switch runs on the worker, so receive never waits behind a held
 * lockcache and the kernel socket buffer drains at recvfrom speed.
 * Capacity IS the backpressure: a full ring drops the NEWEST datagram,
 * counted, throttled-said -- cache is lossy by design, and a bounded,
 * counted drop beats an unbounded queue or a silently overflowing
 * socket buffer. The parser switch itself is the UNCHANGED
 * cache_recvmsg(): the worker renders entries from the ring through the
 * exact same code the inline path has always run (cq_active/cq_current
 * select the packet source below).
 */
#ifdef CACHE_QUEUE
#include "cachequeue.h"

static struct cq_entry cq_current;   /* the entry the worker is rendering */
static int cq_active;                /* nonzero only on the worker thread */
static void cache_queue_recv(struct cacheserver_data *cache);
void *cache_queue_thread(void *param);

#endif /* CACHE_QUEUE */

/* The first 12 bytes of a CSP reply contain only an untrusted claimed
 * channel and fingerprint. Never attach these drops to a client or CW. */
static void cache_trace_csp_drop(const unsigned char *buf, int len, int peerid,
                                 const char *reason)
{
    char detail[128];
    uint32_t hash;
    if (len < 12) {
        user_trace_note_short_csp_reply();
        return;
    }
    hash = ((uint32_t)buf[8] << 24) | ((uint32_t)buf[9] << 16) |
           ((uint32_t)buf[10] << 8) | (uint32_t)buf[11];
    snprintf(detail, sizeof(detail), "CSP reply dropped: %s; length=%d; header unverified", reason, len);
    user_trace_cache_observation((uint16_t)((buf[6]<<8)|buf[7]), 0,
        (uint16_t)((buf[2]<<8)|buf[3]), hash, peerid, detail, NULL);
}

/* Apply the same stored-candidate policy to ordinary CSP requests and
 * resend requests. The caller holds lockcache. This says only that a stored
 * candidate passed the applicable cache gates, not that it produces picture. */
static int cache_peer_candidate_eligible(struct cache_data *entry,
                                         struct cw_cache_data *candidate)
{
    uint32_t provider = 0;
    int provider_known = cache_consensus_provider_scope(entry, &provider);
    if (candidate->status & DCW_ERROR) return 0;
    if (!(candidate->status & DCW_PEER_READY)) return 0;
    if (candidate->nbpeers < cfg.cache.threshold &&
        !((candidate->status & DCW_LOCAL_SOURCE) && cfg.cache.threshold <= 1)) return 0;
    if (cfg.cache.consensus && cwconsensus_is_held(entry->caid, entry->sid,
            provider, provider_known, entry->hash, entry->tag, candidate->cw)) return 0;
    return 1;
}

#ifdef CACHEEX
/*
 * A CSP TYPE_REPLY carries no provider. Reuse evidence must therefore inherit
 * the matched cache record's effective provider scope for every provider-keyed
 * action. In particular, using req->provid here would file trust under zero
 * even when cachepref_gate() later looks up the source under the local provider.
 * The caller reaches this only after the reply passed cache_setdcw(), and holds
 * lockcache just like the rest of the CSP receive path.
 */
static void cache_csp_reuse_check(struct cachepeer_data *peer,
                                  struct cache_data *req,
                                  const uint8_t cw[16])
{
    struct cache_data *entry;
    uint32_t now, proof_provider = 0;
    int proof_provider_known;

    if (!peer || !req || !cw) return;
    entry = cache_fetch(req);
    if (!entry) return;

    now = GetTickCount();
    proof_provider_known = cache_consensus_provider_scope(entry, &proof_provider);
    if (cwreuse_offer_scoped(&cwreuse_tab, cw, entry->ecmd5, 1,
                             req->caid, req->sid, proof_provider,
                             proof_provider_known, now) != CWREUSE_PROOF)
        return;

    cwreuse_proofs++;
    /* CSP has no provider field: retain a known local/cache scope, otherwise
     * keep the conviction in the distinct unknown-provider scope. */
    cwn_note(DCW_SOURCE_CACHE, peer->id|PEER_CSP, cw, req->caid,
             proof_provider, proof_provider_known, req->sid, now);
    trust_record_lk_scoped(&trust_tab, TRUST_EV_PROOF, DCW_SOURCE_CACHE,
                           peer->id|PEER_CSP, req->caid, proof_provider,
                           proof_provider_known, req->sid, now);
    trustagg_note_lk(TRUST_EV_PROOF, DCW_SOURCE_CACHE, peer->id|PEER_CSP,
                     req->caid, now);
    debugf(DBG_ERROR,
           " !!! CW REUSE PROOF: same cw for a different ecm on a different service, ch %04x:%06x:%04x from cache peer%d (%s:%d)\n",
           req->caid, req->provid, req->sid, peer->id, peer->host->name, peer->port);

    if (cache_purge_mark_scoped(&purge_tab, DCW_SOURCE_CACHE, peer->id|PEER_CSP,
                                req->caid, req->sid, proof_provider,
                                proof_provider_known, now)) {
        int npurged = cache_purge_sweep_scoped(&purge_tab, DCW_SOURCE_CACHE,
                                               peer->id|PEER_CSP, req->caid,
                                               req->sid, proof_provider,
                                               proof_provider_known, DCW_ERROR);
        debugf(DBG_ERROR,
               " !!! CACHE PURGE: ch %04x:%06x:%04x marked %d cached cw(s) from cache peer%d\n",
               req->caid, proof_provider, req->sid, npurged, peer->id);
    }
}
#endif

/* Takes lockcache for the complete peer/cache-table state transition. All
 * callers hold the config lifetime read gate, but not lockcache. */
void cache_recvmsg(struct cacheserver_data *cache)
{
	unsigned int recv_ip;
	unsigned short recv_port;
	unsigned char buf[2048];
	char str[1024];
	struct sockaddr_in si_other;
	socklen_t slen=sizeof(si_other);
	struct cachepeer_data *peer;

#ifdef CACHE_QUEUE
	int received;
	if (cq_active) {
		memcpy(buf, cq_current.buf, cq_current.len);
		received = cq_current.len;
		si_other = cq_current.from;
		slen = sizeof(si_other);
		memcpy(&recv_ip, &si_other.sin_addr, 4);
		recv_port = ntohs(si_other.sin_port);
	} else
	received = recvfrom( cache->handle, buf, sizeof(buf), 0, (struct sockaddr*)&si_other, &slen);
#else
	int received = recvfrom( cache->handle, buf, sizeof(buf), 0, (struct sockaddr*)&si_other, &slen);
#endif
	if ( (received<2)||(received>512) ) {
        if (received > 0 && buf[0] == TYPE_REPLY)
            cache_trace_csp_drop(buf, received, 0, received > 512 ? "oversized datagram" : "incomplete header");
        return;
    }
	memcpy( &recv_ip, &si_other.sin_addr, 4);
	recv_port = ntohs(si_other.sin_port);

	/*
	 * TASK 2.9 -- the protocol guard's first gate. Five of the types below
	 * read fixed offsets; a datagram shorter than its type's minimum made
	 * the handler decide on uninitialised stack memory (a REQUEST keyed by
	 * a garbage hash, a PINGREQ anchoring a peer at a garbage port).
	 * Uninterpretable is not decidable: drop before any parse. The minimums
	 * are what this server's own senders already produce, so a well-formed
	 * peer never sees this gate move.
	 */
	{
		int cgmin = cg_minlen( buf[0] );
		if ( received < cgmin ) {
			cg_note_short( buf[0], received, recv_ip, recv_port );
            if (buf[0] == TYPE_REPLY)
                cache_trace_csp_drop(buf, received, 0, "incomplete integrity header");
			return;
		}
	}

#ifdef DEBUG_NETWORK2
	if (flag_debugnet) {
		debugf(getdbgflag(DBG_CACHE,0,0)," cache: recv data (%d) from address (%s:%d)\n", received, ip2string(recv_ip), recv_port );
		debughex(buf,received);
	}
#endif

	// Store Data
	struct cache_data req;
	struct sms_data sms_log_copy;
	char sms_log_peer[256];
	uint16_t sms_log_port = 0;
	int sms_log_pending = 0;
	memset(&req, 0, sizeof(req));
	memset(&cache_peerrep_deferred, 0, sizeof(cache_peerrep_deferred));
	/* lockcache owns cache-peer state/list and cache-table state. */
	pthread_mutex_lock(&prg.lockcache);
	switch(buf[0]) {


		case TYPE_REQUEST:
			// Check Peer
			peer = getpeerbyaddr(cache, recv_ip,recv_port);
			if (!peer) { cg_note_unknown( buf[0], recv_ip, recv_port ); break; }	/* TASK 2.9: counted, once said */
			// Check Status
			if (IS_DISABLED(peer->flags)) break;
			// Get DATA
			req.tag = buf[1];
			req.sid = (buf[2]<<8) | buf[3];
			req.onid = (buf[4]<<8) | buf[5];
			req.caid = (buf[6]<<8) | buf[7];
			req.hash = ((uint32_t)buf[8]<<24) | (buf[9]<<16) | (buf[10]<<8) |buf[11];
			req.provid = 0;


			// Check Cache Request
			if ( cache_check(&req) ) {
				peer->reqnb++;
				// ADD CACHE
				struct cache_data *pcache = cache_fetch( &req );
				if (pcache==NULL) pcache = cache_new( &req );
				else {
					if (!pcache->tag) pcache->tag = req.tag; // set tag if not set (coming from cahceex)
					if (!pcache->provid && req.provid) pcache->provid = req.provid; // set provid if not set (coming from csp)
					if ( cfg.cache.forward || ((peer->ismultics)&&(peer->protocol&BIT_CACHE_HACK)) )
					if ( pcache->cwdata && !cache_entry_sendpipe_active(pcache) ) {
						struct cw_cache_data *cwdata = pcache->cwdata;
						int peer_defer_noted = 0;
						while (cwdata) {
							if (cache_peer_candidate_eligible(pcache, cwdata)) {
								cache_send_reply( pcache, peer, cwdata->cw);
								if (cfg.cache.forward) peer->sentrep++;
							} else if (!peer_defer_noted) {
								user_trace_cache_observation(pcache->caid, pcache->provid,
								    pcache->sid, pcache->hash, peer->id|PEER_CSP,
								    "cache peer reply deferred: candidate not approved, held or below threshold", NULL);
								peer_defer_noted = 1;
							}
							cwdata = cwdata->next;
						}
					}
				}
			}

			break;


		case TYPE_REPLY:
			// Check Peer
			peer = getpeerbyaddr(cache, recv_ip,recv_port);
			if (!peer) {
                cg_note_unknown(buf[0], recv_ip, recv_port);
                cache_trace_csp_drop(buf, received, 0, "unknown sender");
                break;
            }
			// Check Status
			if (IS_DISABLED(peer->flags)) {
                cache_trace_csp_drop(buf, received, peer->id|PEER_CSP, "configured peer disabled");
                break;
            }
			/* Legacy peerrep isolation is temporary and IP:port scoped.
			 * A push rejected by this gate is not fresh proof of poisoning,
			 * so it is recorded diagnostically but cannot renew the TTL. */
			if ( peerrep_on &&
			     !peerrep_push_ok(host_ip_snapshot(peer->host), peer->port,
			                      GetTickCount()) ) {
                cache_trace_csp_drop(buf, received, peer->id|PEER_CSP, "peer reputation isolation");
				peerrep_event(peer, "push-while-isolated");
				debugf(getdbgflag(DBG_CACHE,0,peer->id),
					" [PEER REP] push from %s:%u dropped unprocessed\n",
					peer->host->name, peer->port);
				break;
			}
			// Check Integrity
			if (buf[12]!=buf[1]) {
                cache_trace_csp_drop(buf, received, peer->id|PEER_CSP, "integrity tag mismatch");
                break;
            }
			// SetUp Request
			req.tag = buf[1];
			req.sid = (buf[2]<<8) | buf[3];
			req.onid = (buf[4]<<8) | buf[5];
			req.caid = (buf[6]<<8) | buf[7];
			req.hash = ((uint32_t)buf[8]<<24) | (buf[9]<<16) | (buf[10]<<8) |buf[11];
			req.provid = 0;


			// Check Cache Request
			if ( cache_check(&req) ) {
				// check for length
				if ( received>=29 ) {
					uint8_t cw[16];
					peer->repok++;
					memcpy(cw, buf+13, 16);
					// Search for Cache data
					cwcycle_t cwcycle = NO_CYCLE;
					if ( peer->fwd && (received==30) ) cwcycle = buf[29];
					int status = cache_setdcw(&req,cw,cwcycle,peer->id|PEER_CSP);
					if ( !(status&DCW_ERROR) ) { // && (status&DCW_CYCLE) ) {
						/*
						 * GR10: a CW received from a cache peer is never re-pushed
						 * to other cache peers unless the operator explicitly sets
						 * CACHE FORWARD. This path used to ignore that switch, so
						 * `CACHE FORWARD: OFF` still relayed: one poisoned peer could
						 * re-seed the whole mesh and defeat any local cache purge.
						 */
						if ( cache_should_relay(cfg.cache.forward, peer->fwd) ) {
							cache_send_fwdreply( &req, cw, cwcycle);
						}
						else {
							/* GR8: record the suppression so a "why did my peers
							 * stop getting CWs" question has an answer. Rare path
							 * (once per peer CW), so a format call is affordable. */
							debugf( getdbgflag(DBG_CACHE,0,0), " cache(GR10): not re-pushing cw from peer%d (%s:%d) ch %04x:%06x:%04x/%08x\n",
								peer->id, peer->host->name, peer->port, req.caid, req.provid, req.sid, req.hash );
						}
					}
#ifdef CACHEEX
							/* CSP has no provider on the wire. The helper uses the
						 * provider context of the matched cache entry consistently
						 * for reuse, trust, negative memory, and purge. */
							cache_csp_reuse_check(peer, &req, cw);
#endif
				} else {
                    cache_trace_csp_drop(buf, received, peer->id|PEER_CSP, "incomplete CW payload");
                }
			} else {
                const char *why = ((req.tag & 0xfe) != 0x80) ? "invalid ECM tag" :
                    (!req.caid || !req.hash || !req.sid) ? "missing channel or hash" : "CAID not allowed by cache";
                cache_trace_csp_drop(buf, received, peer->id|PEER_CSP, why);
            }

			break;

		case TYPE_RESENDREQ:
			// Check Peer
			peer = getpeerbyaddr(cache, recv_ip,recv_port);
			if (!peer) { cg_note_unknown( buf[0], recv_ip, recv_port ); break; }	/* TASK 2.9: counted, once said */
			// Check Status
			if (IS_DISABLED(peer->flags)) break;
			// Check Packer length
			if (received<16) break;
			// Good Packet
			struct cache_data req;
			memset(&req, 0, sizeof(req));
			req.tag = buf[5];
			req.sid = (buf[6]<<8) | buf[7];
			req.onid = (buf[8]<<8) | buf[9];
			req.caid = (buf[10]<<8) | buf[11];
			req.hash = ((uint32_t)buf[12]<<24) | (buf[13]<<16) | (buf[14]<<8) | buf[15];
			req.provid = 0;
			//
			// Check Cache Request
			if ( cache_check(&req) ) {
				struct cache_data *pcache = cache_fetch( &req );
				if (pcache!=NULL && !cache_entry_sendpipe_active(pcache)) {
					buf[4] = TYPE_REPLY;
					buf[16] = buf[5];
					struct cw_cache_data *cwdata = pcache->cwdata;
					int peer_defer_noted = 0;
					while (cwdata) {
						if (cache_peer_candidate_eligible(pcache, cwdata)) {
							memcpy( buf+17, cwdata->cw, 16);
							sendtopeer( peer, buf+4, 29);
						} else if (!peer_defer_noted) {
							user_trace_cache_observation(pcache->caid, pcache->provid,
							    pcache->sid, pcache->hash, peer->id|PEER_CSP,
							    "cache peer reply deferred: candidate not approved, held or below threshold", NULL);
							peer_defer_noted = 1;
						}
						cwdata = cwdata->next;
					}
				}
			}
			//
			break;

		case TYPE_PINGREQ:
			// Check Peer
			peer = cache->peer;
			int port = (buf[11]<<8)|buf[12];
			struct cachepeer_data *peerip = NULL;
			while (peer) {
				if (host_ip_snapshot(peer->host)==recv_ip) {
					peerip = peer;
					if (peer->port==port) break; // Found
				}
				peer = peer->next;
			}
			if (!peer) {
				if (peerip) {
					// Check for peer reuse
					peer = cache->peer;
					while (peer) {
						if ( (peer->port==port) && !strcmp(peer->host->name,peerip->host->name) ) break;
						peer = peer->next;
					}
					if (!peer) {
						if (peerip->port==0) peerip->port = port;
						else {
							// add new peer
							peer = malloc( sizeof(struct cachepeer_data) );
							memset( peer, 0, sizeof(struct cachepeer_data) );
							peer->host = peerip->host;
							peer->port = port;
							peer->outsock = cache->handle; //CreateClientSockUdp(0,0);
							peer->runtime = 1;
							peer->fblock0onid = peerip->fblock0onid;
							peer->id = cfg.cache.peerid;
							cfg.cache.peerid++;
							peer->flags = FLAG_CACHE_SENDREQ | FLAG_CACHE_SENDREP;
							peer->next = peerip->next;
							peerip->next = peer;

							if (!peerip->autoadd || !cfg.cache.autoadd) {
								peer->flags |= FLAG_DISABLE;
								// Check for extended reply ( Program Name/Version )
								int index = 13;
								while (received>index) {
									if (!cg_tlv_is_complete(buf, received, index)) break;
									switch(buf[index]) {
										case 0x01:
											if (buf[index+1]<32) { memcpy(peer->program, buf+index+2, buf[index+1]); peer->program[buf[index+1]] = 0; }
											break;
										case 0x02:
											if (buf[index+1]<32) { memcpy(peer->version, buf+index+2, buf[index+1]); peer->version[buf[index+1]] = 0; }
											break;
									}
									index += 2+buf[index+1];
								}
							}
							else debugf(getdbgflag(DBG_CACHE,0,0), " cache: new peer (%s:%d)\n", peer->host->name, peer->port );
						}
					}
					else {
						host_checkiptime_set(peer->host, 0);
					}
				}
				else if (cfg.cache.autoadd) {
					// add new peer
					peer = malloc( sizeof(struct cachepeer_data) );
					memset( peer, 0, sizeof(struct cachepeer_data) );
					peer->flags = FLAG_CACHE_SENDREQ | FLAG_CACHE_SENDREP;
					// ADD HOST
					struct host_data *host = add_host( &cfg, ip2string(recv_ip) );
					host_ip_set(host, recv_ip);
					peer->host = host;
					peer->port = port;
					peer->id = cfg.cache.peerid;
					peer->srvid = cache->id;
					peer->outsock = cache->handle; //CreateClientSockUdp(0,0);
					peer->runtime = 1;
					cfg.cache.peerid++;
					cfg_addcachepeer(cache, peer);
					if (!cfg.cache.autoenable) {
						peer->flags |= FLAG_DISABLE;
						// Check for extended reply ( Program Name/Version )
						int index = 13;
						while (received>index) {
							if (!cg_tlv_is_complete(buf, received, index)) break;
							switch(buf[index]) {
								case 0x01:
									if (buf[index+1]<32) { memcpy(peer->program, buf+index+2, buf[index+1]); peer->program[buf[index+1]] = 0; }
									break;
								case 0x02:
									if (buf[index+1]<32) { memcpy(peer->version, buf+index+2, buf[index+1]); peer->version[buf[index+1]] = 0; }
									break;
							}
							index += 2+buf[index+1];
						}
					}
					else debugf(getdbgflag(DBG_CACHE,0,0), " cache: new peer (%s:%d)\n", peer->host->name, peer->port );
				}
				else debugf( getdbgflag(DBG_CACHE,0,0), " cache: Alert! unknown peer (%s:%d)\n", ip2string(recv_ip), port );
			}
			//
			if (peer) {
				// Check Status
				if (IS_DISABLED(peer->flags)) break;
				// Set Defaults
				memset( peer->cards, 0, sizeof(peer->cards) );
				peer->protocol = 0; // Normal CSP Protocol
				peer->program[0] = 0;
				peer->version[0] = 0;
				// Check for extended reply ( Program Name/Version )
				int index = 13;
				while (received>index) {
					if (!cg_tlv_is_complete(buf, received, index)) break;
					switch(buf[index]) {
						case 0x01:
							if (buf[index+1]<32) { memcpy(peer->program, buf+index+2, buf[index+1]); peer->program[buf[index+1]] = 0; }
							break;
						case 0x02:
							if (buf[index+1]<32) { memcpy(peer->version, buf+index+2, buf[index+1]); peer->version[buf[index+1]] = 0; }
							break;
					}
					index += 2+buf[index+1];
				}
				// Set Default Reply
				buf[0] = TYPE_PINGRPL;
				// Check for New Protocol
				if ( buf[1]=='M' && buf[2]=='C' ) {
					peer->protocol = buf[3];
					if (peer->protocol&1) {
						buf[0] = TYPE_HELLO_ACK;
						// Decode CRC
						buf[13] = cache->port>>8;
						buf[14] = cache->port;
						fase( buf+11, buf+6);
					}
				} else if ( !peer->csp || (received>13) ) { peer->flags |= FLAG_DISABLE; break; }
				sendtopeer( peer, buf, 9);
				// Check for activity
				if (peer->recvport!=recv_port) {
					peer->ping = 0;
					peer->recvport = recv_port;
					peer->ismultics = 0;
				}
			}
			break;


		case TYPE_PINGRPL:
			// Get Peer
			peer = cache->peer;
			int peerid = (buf[4]<<8) | buf[5];
			while (peer) {
				if ( (host_ip_snapshot(peer->host)==recv_ip)&&(peer->id==peerid) ) {
					if (IS_DISABLED(peer->flags)) break;
					peer->protocol = 0;
					peer->recvport = recv_port;
					peer->lastpingrecv = GetTickCount();
					if (peer->ping>0)
						peer->ping = (peer->ping+peer->lastpingrecv-peer->lastpingsent)/2;
					else {
						if (!peer_doublecheck(cache,peer)) break;
						debugf(getdbgflag(DBG_CACHE,0,peer->id), " cache: Peer (%s:%d) come Online\n", peer->host->name, peer->port );
						if (IS_DISABLED(peer->flags)) { peer->ping = 0; break; }
						peer->ping = peer->lastpingrecv-peer->lastpingsent;
						//ipeer_update(cache);
					}
					peer->ping++;
					break;
				}
				peer = peer->next;
			}
			break;


#ifdef NEWCACHE
				case TYPE_HELLO_ACK:
					// Get Peer
					peerid = (buf[4]<<8) | buf[5];
					peer = cache->peer;
					while (peer) {
						if ( (host_ip_snapshot(peer->host)==recv_ip)&&(peer->id==peerid) ) {
							if (IS_DISABLED(peer->flags)) break;
							// Check for private new cache
							uint8_t k[4]; k[0] = cache->port>>8; k[1] = cache->port; k[2]=peer->port>>8; k[3]=peer->port; 
							fase( k, peer->crc);
							if ( (peer->crc[0]==buf[6])&&(peer->crc[1]==buf[7])&&(peer->crc[2]==buf[8]) ) peer->ismultics =1; else peer->ismultics = 0;
							//
							peer->recvport = recv_port;
							peer->lastpingrecv = GetTickCount();
							if (peer->ping>0)
								peer->ping = (peer->ping+peer->lastpingrecv-peer->lastpingsent)/2;
							else {
								if (!peer_doublecheck(cache,peer)) break;
								debugf(getdbgflag(DBG_CACHE,0,peer->id), " cache: Peer (%s:%d) come Online*\n", peer->host->name, peer->port );
								if (IS_DISABLED(peer->flags)) { peer->ping = 0; break; }
								peer->ping = peer->lastpingrecv-peer->lastpingsent;
								//ipeer_update(cache);
							}
							peer->ping++;
							//debugf(getdbgflag(DBG_CACHE,0,peer->id), " cache: sending card data to peer (%s:%d)\n", peer->host->name, peer->port );
							// Send CARDS DATA
							buf[0] = TYPE_CARD_LIST;
							buf[1] = 1; // Reset Cards
							int pos = 2;

							//sendtopeer( peer, buf, pos);

							if (peer->sharelimits[0].caid!=0xffff) {
								int i;
								for (i=0; i<100; i++) {
									if (peer->sharelimits[i].caid==0xffff) break;
									uint32_t caprov = ((uint32_t)peer->sharelimits[i].caid << 16)|(peer->sharelimits[i].provid);
 									buf[pos] = caprov>>24;
 									buf[pos+1] = caprov>>16;
 									buf[pos+2] = caprov>>8;
 									buf[pos+3] = caprov;
									pos +=4;
									if (pos>400) {
										sendtopeer( peer, buf, pos);
										buf[0] = TYPE_CARD_LIST;
										buf[1] = 0; // no Reset
										pos = 2;
									}
								}
								if (pos>2) {
									sendtopeer( peer, buf, pos);
								}
							}
							else
							{
								struct cardserver_data *cs = cfg.cardserver;
								while (cs) {
									int i;
									if (cs->option.fallowcache)
									for (i=0; i<cs->card.nbprov; i++) {
										uint32_t caprov = ((uint32_t)cs->card.caid << 16)|(cs->card.prov[i].id);
 										buf[pos] = caprov>>24;
 										buf[pos+1] = caprov>>16;
 										buf[pos+2] = caprov>>8;
 										buf[pos+3] = caprov;
										pos +=4;
										if (pos>400) {
											sendtopeer( peer, buf, pos);
											buf[0] = TYPE_CARD_LIST;
											buf[1] = 0; // no Reset
											pos = 2;
										}
									}
									cs = cs->next;
								}
								if (pos>2) {
									sendtopeer( peer, buf, pos);
								}
							}

							break;
						}
						peer = peer->next;
					}
					break;


		case TYPE_CARD_LIST:
			// Check Peer
			peer = getpeerbyaddr(cache, recv_ip,recv_port);
			if (!peer) { cg_note_unknown( buf[0], recv_ip, recv_port ); break; } /* TASK 2.9 */
			if (IS_DISABLED(peer->flags)) break;
			// reset cards
			int idx = 0;
			if (buf[1]&1) memset( peer->cards, 0, sizeof(peer->cards) ); 
			else for (idx=0; idx<1024; idx++) if (!peer->cards[idx]) break;
			//
			int totalcards = (received-2)/4;
			int j=0;
			while ( (j<totalcards)&&(idx<1024) ){
				peer->cards[idx] = ((uint32_t)buf[2+j*4]<<24)|((uint32_t)buf[3+j*4]<<16)|((uint32_t)buf[4+j*4]<<8)|buf[5+j*4];
				j++; idx++;
			}
			peer->nbcards = idx;
			// Keep the sorted order expected by peer_card_binarysearch().
			qsort(peer->cards, (size_t)idx, sizeof(peer->cards[0]),
			      cache_peer_card_compare);
			break;


		case TYPE_KEEPALIVE:
			// Check Peer
			peer = getpeerbyaddr(cache, recv_ip,recv_port);
			if (!peer) { cg_note_unknown( buf[0], recv_ip, recv_port ); break; }	/* TASK 2.9: counted, once said */
			// Check Status
			if (IS_DISABLED(peer->flags)) break;
			if (!peer->nbcards) break;
			// Send Reply
			buf[0] = TYPE_KEEPALIVE_ACK;
			sendtopeer( peer, buf, received);
			break;


		case TYPE_KEEPALIVE_ACK:
			// Check Peer
			peer = getpeerbyaddr(cache, recv_ip,recv_port);
			if (!peer) { cg_note_unknown( buf[0], recv_ip, recv_port ); break; }	/* TASK 2.9: counted, once said */
			// Check Status
			if (IS_DISABLED(peer->flags)) break;
			//
			peer->lastpingrecv = GetTickCount();

			if (peer->ping>0)
				peer->ping = (peer->ping+peer->lastpingrecv-peer->lastpingsent)/2;
			else
				peer->ping = peer->lastpingrecv-peer->lastpingsent;
			peer->ping++;
			break;


		case TYPE_SMS:
			peer = getpeerbyaddr(cache, recv_ip,recv_port);
			if (!peer) { cg_note_unknown( buf[0], recv_ip, recv_port ); break; }	/* TASK 2.9: counted, once said */
			if (IS_DISABLED(peer->flags)) break;
			if (received<6) break;
			uint32_t hash = ((uint32_t)buf[1]<<24) | (buf[2]<<16) | (buf[3]<<8) | (buf[4]);
			buf[received] = 0;
			// Reject an oversized escaped message without storing it or sending ACK.
			struct sms_data *sms = malloc( sizeof(struct sms_data) );
			if (!sms) break;
			if (!stringtohtml((const char *)buf+5, sms->msg, sizeof(sms->msg))) {
				free(sms);
				break;
			}
			peer_check_messages( peer );
			sms->hash = hash;
			sms->status = 0; // bit 0 (0:in,1:out) bit 1 (0:unread/unAck, 1:read/ack)
			sms->rawtime = time(NULL);
			sms->next = peer->sms;
			peer->sms = sms;

			// SEND ACK
			buf[0] = TYPE_SMS_ACK;
			sendtopeer( peer, buf, 5 );
			// debug
			debugf(getdbgflag(DBG_CACHE,0,0)," cache: SMS from peer (%s:%d)\n", peer->host->name, peer->port);
			// Save the file record after releasing lockcache.
			memset(&sms_log_copy, 0, sizeof(sms_log_copy));
			sms_log_copy.status = sms->status;
			sms_log_copy.rawtime = sms->rawtime;
			snprintf(sms_log_copy.msg, sizeof(sms_log_copy.msg), "%s", sms->msg);
			snprintf(sms_log_peer, sizeof(sms_log_peer), "%s",
			         peer->host ? peer->host->name : "");
			sms_log_port = peer->port;
			sms_log_pending = 1;
			break;


		case TYPE_SMS_ACK:
			peer = getpeerbyaddr(cache, recv_ip,recv_port);
			if (!peer) { cg_note_unknown( buf[0], recv_ip, recv_port ); break; }	/* TASK 2.9: counted, once said */
			if (IS_DISABLED(peer->flags)) break;
			if (received!=5) break;
			if (!peer->sms) break;
			hash = ((uint32_t)buf[1]<<24) | (buf[2]<<16) | (buf[3]<<8) | (buf[4]);
			// Search for data
			sms = peer->sms;
			while (sms) {
				if ( (sms->hash==hash)&&(sms->status==1) ) {
					debugf(getdbgflag(DBG_CACHE,0,0)," cache: SMS ACK from peer (%s:%d)\n", peer->host->name, peer->port);
					sms->status = 3;
				}
				sms = sms->next;
			}
			break;
#endif


		case TYPE_VERSION:
			peer = getpeerbyaddr(cache, recv_ip,recv_port);
			if (peer) {
				buf[0] = TYPE_VERSION_ACK;
				buf[1] = 'r';
				buf[2] = '0'+(REVISION/10);
				buf[3] = '0'+(REVISION%10);
				sendtopeer( peer, buf, 4 );
			}
			break;

		case TYPE_EXTENDED:
			if (buf[1]=='V') { // Version
				buf[0] = TYPE_EXTENDED_ACK;
				buf[2] = 'r';
				buf[3] = '0'+(REVISION/10);
				buf[4] = '0'+(REVISION%10);
				sendtoip( cache->handle, recv_ip, recv_port, buf, 5);
			}
			break;

		case TYPE_UNKNOWN:
			break;

		default:
			if (received>100) array2hex( buf, str, 100); else array2hex( buf, str, received);
			debugf(getdbgflag(DBG_CACHE,0,0)," cache: Unknown message from %s (%d) : %s\n", ip2string(recv_ip), received, str );
#ifdef NEWCACHE
			peer = getpeerbyaddr(cache, recv_ip,recv_port);
			if ( peer && (peer->protocol&1) ) {
				buf[1] = buf[0];
				buf[0] = TYPE_UNKNOWN;
				sendtopeer( peer, buf, 2 );
			}
#endif
			break;

	}
	pthread_mutex_unlock(&prg.lockcache);
	cache_peerrep_save_after_unlock();
	if (sms_log_pending)
		save_sms_record(&sms_log_copy, sms_log_peer, sms_log_port);
}


/*
 * TASK 1.7 -- TRUSTED-CACHE-FIRST, applied where a *stored* cache entry is
 * offered to a waiting ECM.
 *
 * Returns CACHEPREF_ALLOW or CACHEPREF_DEFER; the policy itself lives in
 * cachepref.h so it can be unit-tested without the cache. On DEFER the caller
 * simply does nothing: no DCW_SENT mark, no pipe message. The ECM then waits
 * out `cachetimeout` and goes to the card servers, which is exactly what
 * upstream does when the cache holds no usable CW -- so this adds no new state
 * and no new path through the ECM thread.
 *
 * Scope, deliberately narrow: this gates PIPE_CACHE_FIND only, i.e. what we
 * hand out of the cache when asked. It does NOT gate cache_setdcw(), where a CW
 * that has just arrived from a peer is delivered to a waiting ECM. Dropping a
 * live push from an untrusted peer would silence that peer entirely on soft
 * evidence alone, which is the hard action GR3 forbids; acceptance is governed
 * by the min-peer floor (TASK 1.11a) and, on a definitive proof, by the purge
 * mark (TASK 1.5). A stored entry is different: it can be served again and
 * again for as long as it lives, which is how one poisoned push becomes a
 * permanent black screen on a channel.
 *
 * Called with prg.lockcache held (cache_pipe_recvmsg() is invoked under it at
 * :2525 and :2567). trust_lock is a leaf lock, so lockcache -> trust_lock adds
 * no cycle.
 */
static int cachepref_gate(struct cache_data *pcache, struct cw_cache_data *cwdata)
{
	int score, trusted, crosschecked, allow;
	uint32_t trust_provider;
	int trust_provider_known;

	if (!cachepref_enabled) return CACHEPREF_ALLOW;

	/*
	 * pcache->caid/provid/sid are all set before the CW scan runs, in both the
	 * cache_new and the cache_fetch branch of PIPE_CACHE_FIND. GR4: the key is
	 * the source, CAID, provider scope/value, and SID, so a peer good on one
	 * bouquet stays good on it even if it is poisoning another.
	 */
	trust_provider = pcache->provid;
	trust_provider_known = cache_consensus_provider_scope(pcache, &trust_provider);
	if (!trust_provider_known) trust_provider = pcache->provid;
	score = trust_score_lk_scoped(&trust_tab, DCW_SOURCE_CACHE, (int)cwdata->peerid,
	                              pcache->caid, trust_provider,
	                              trust_provider_known, pcache->sid);
	trusted = cachepref_trusted(score, TRUST_ACTIONABLE);

	/*
	 * A non-null prevcw means the scan that found this CW ran the successor test
	 * against the client's previous key. A null one means it is the "NO PREVIOUS
	 * CW" scan -- a channel change, where nothing contradicts the entry.
	 */
	crosschecked = !isnullDCW(pcache->prevcw);

	allow = cachepref_allow(cachepref_enabled, trusted, crosschecked);
	if (allow==CACHEPREF_DEFER) {
		cachepref_defer++;
		/*
		 * GR8: the evidence, every time. Under DBG_CACHE rather than DBG_ERROR
		 * because this can fire on every ECM for a distrusted source and an
		 * unconditional line would bury the log; the counter is always
		 * incremented so the policy stays measurable either way.
		 */
		debugf(getdbgflag(DBG_CACHE,0,0)," !?> CACHE DEFER: ch %04x:%06x:%04x cw from %s (trust %d < %d, no previous cw to check against) -- left to the card servers\n",
			pcache->caid, pcache->provid, pcache->sid,
			(cwdata->peerid&PEER_CSP)?"cache peer (csp)":"cache peer",
			score, TRUST_ACTIONABLE);
	}
	return allow;
}


/* A stored record can have many arrivals while the consensus window still
 * owns it. FIND must not mistake arrival count for a completed verdict. */
static int cache_find_consensus_held(struct cache_data *entry,
                                     struct cw_cache_data *candidate, int *noted)
{
    uint32_t provider = 0;
    int provider_known = cache_consensus_provider_scope(entry, &provider);
    int held = cfg.cache.consensus &&
        cwconsensus_is_held(entry->caid, entry->sid, provider, provider_known,
                            entry->hash, entry->tag, candidate->cw);
    if (held && !*noted) {
        user_trace_cache_observation(entry->caid, entry->provid, entry->sid,
                                     entry->hash, candidate->peerid,
                                     "cache find deferred: candidate held by consensus", candidate->cw);
        *noted = 1;
    }
    return held;
}

/* Apply exactly the existing FIND policy before ranking candidates. This does
 * not wait for more arrivals: CACHE EVIDENCE only orders candidates already
 * present when the lookup runs, preserving the hit-on-arrival latency contract. */
static int cache_find_candidate_eligible_r48(struct cache_data *entry,
                                              struct cw_cache_data *candidate,
                                              int has_prevcw,
                                              int *find_hold_noted,
                                              int *find_policy_noted)
{
    int cycle_match = 0;
    if ((candidate->status & (DCW_ERROR | DCW_SENT)) ||
        cache_find_consensus_held(entry, candidate, find_hold_noted))
        return 0;

    if (has_prevcw) {
        if (candidate->nbpeers < cfg.cache.threshold) return 0;
        if (candidate->status & DCW_CYCLE) {
            if (entry->cwcycle != candidate->cwcycle) return 0;
        }
        if (entry->cwcycle == CW1CYCLE)
            cycle_match = dcwcmp8(entry->prevcw, candidate->cw) &&
                          !similarcw(entry->prevcw + 8, candidate->cw + 8);
        else if (entry->cwcycle == CW0CYCLE)
            cycle_match = !similarcw(entry->prevcw, candidate->cw) &&
                          dcwcmp8(entry->prevcw + 8, candidate->cw + 8);
        if (!cycle_match) return 0;
    } else if (candidate->status & DCW_CYCLE) {
        if (candidate->nbpeers < cfg.cache.threshold) return 0;
        if (entry->cwcycle != NO_CYCLE && entry->cwcycle != candidate->cwcycle)
            return 0;
    } else {
        if (!*find_policy_noted &&
            (cfg.cache.filter || candidate->nbpeers < cfg.cache.threshold)) {
            user_trace_cache_observation(entry->caid, entry->provid, entry->sid,
                entry->hash, candidate->peerid,
                cfg.cache.filter ? "cache find deferred: cycle not established" :
                                   "cache find deferred: threshold not reached",
                candidate->cw);
            *find_policy_noted = 1;
        }
        if (cfg.cache.filter || candidate->nbpeers < 5 ||
            candidate->nbpeers < cfg.cache.threshold)
            return 0;
    }

    return cachepref_gate(entry, candidate) == CACHEPREF_ALLOW;
}

/* Source reputation is provider-scoped and protected by trust_lock. Missing
 * evidence ranks neutral, not bad. Caller already holds lockcache, following
 * the established lockcache -> trust_lock order. */
static uint16_t cache_find_candidate_r48_rank(struct cache_data *entry,
                                               struct cw_cache_data *candidate,
                                               uint32_t now_ms)
{
    uint32_t provider = 0;
    int provider_known = cache_consensus_provider_scope(entry, &provider);
    uint16_t rank;
    if (!provider_known) provider = entry->provid;
    pthread_mutex_lock(&trust_lock);
    rank = r48_source_rep_rank_q8(&r48_source_rep_tab, DCW_SOURCE_CACHE,
                                  (int)candidate->peerid, entry->caid,
                                  provider, provider_known, now_ms);
    pthread_mutex_unlock(&trust_lock);
    return rank;
}

/* Tie-break with carried line-reference provenance, then stable source ID and
 * CW bytes. Arrival/list order is never the tie-break under CACHE EVIDENCE. */
static int cache_find_candidate_r48_precedes(const struct cw_cache_data *candidate,
                                              uint16_t rank,
                                              const struct cw_cache_data *best,
                                              uint16_t best_rank)
{
    int candidate_line, best_line;
    if (!best) return 1;
    if (rank != best_rank) return rank > best_rank;
    candidate_line = !!(candidate->origin_mask & CACHE_ORIGIN_LINE_SERVER);
    best_line = !!(best->origin_mask & CACHE_ORIGIN_LINE_SERVER);
    if (candidate_line != best_line) return candidate_line > best_line;
    if ((uint32_t)candidate->peerid != (uint32_t)best->peerid)
        return (uint32_t)candidate->peerid < (uint32_t)best->peerid;
    return memcmp(candidate->cw, best->cw, sizeof(candidate->cw)) < 0;
}

static struct cw_cache_data *cache_find_select_r48(struct cache_data *entry,
                                                    int *verify_line,
                                                    int *find_hold_noted,
                                                    int *find_policy_noted)
{
    struct cw_cache_data *candidate, *best = NULL;
    uint32_t now_ms = GetTickCount();
    int has_prevcw = !isnullDCW(entry->prevcw);
    int tied_at_best = 0;
    uint16_t best_rank = 0;
    if (verify_line) *verify_line = 0;
    for (candidate = entry->cwdata; candidate; candidate = candidate->next) {
        uint16_t rank;
        if (!cache_find_candidate_eligible_r48(entry, candidate, has_prevcw,
                                               find_hold_noted,
                                               find_policy_noted))
            continue;
        rank = cache_find_candidate_r48_rank(entry, candidate, now_ms);
        if (!best || rank > best_rank) {
            best = candidate;
            best_rank = rank;
            tied_at_best = 0;
        } else if (rank == best_rank) {
            tied_at_best = 1;
            if (cache_find_candidate_r48_precedes(candidate, rank, best,
                                                  best_rank))
                best = candidate;
        }
    }
    if (verify_line) *verify_line = tied_at_best;
    return best;
}


/* A cache key lacks provider identity. Do not reassign an entry associated
 * with one known local provider value to another known value, even after its
 * waiter finishes. An unknown local zero cannot establish a collision.
 * Called only for ECM-to-cache pipe events with lockcache held. */
static int cache_local_provider_collision(struct cache_data *entry,
                                          struct cache_data *req)
{
    if (!entry || !cache_request_provider_known(req)) return 0;
    if (entry->local_provider_set && entry->local_provider != req->provid) {
        user_trace_cache_observation(req->caid, req->provid, req->sid, req->hash, 0,
            "cache provider collision: different local provider; conflicting operation skipped", NULL);
        return 1;
    }
    if ((entry->provider_known || entry->provid) && entry->provid != req->provid) {
        user_trace_cache_observation(req->caid, req->provid, req->sid, req->hash, 0,
            "cache provider collision: explicit cached provider differs from local request; conflicting operation skipped", NULL);
        return 1;
    }
    return 0;
}

static int cache_ecm_pipe_base_len(void)
{
	int length = 16 + sizeof(void*) + sizeof(uint64_t);
#ifdef CACHEEX
	length += 16; /* ecmd5 */
#endif
	return length;
}

void cache_pipe_recvmsg()
{
	/* pipe_recv() has a 1020-byte payload ceiling; size for that contract and
	 * validate each cache message before decoding its fixed offsets. */
	uint8_t buf[1024];
	uint8_t cw[16];
	struct cache_data req;
	struct cache_data *pcache;
	struct cardserver_data *find_profile;
	struct cw_cache_data *selected_candidate;
	int find_hold_noted = 0, find_policy_noted = 0;
	int verify_line = 0;

	int len =  pipe_recv( prg.pipe.cache[0], buf, sizeof(buf) );
	if (len<=0) return;
	int base_len = cache_ecm_pipe_base_len();

	switch (buf[0]) {

		case PIPE_CACHE_FIND:
			if (len != base_len && len != base_len + 16) break;
			if (get_ecm2cache(buf, len, &req, req.prevcw) < 0) break;
			if (!cache_ecm_snapshot_read_locked(req.ecm, req.ecm_lifetime_id, NULL, NULL, NULL)) break;
			//debugf(0, " Get PIPE_CACHE_FIND: %04x:%06x:%04x:%08x\n", req.caid, req.provid, req.sid, req.hash);

			pcache = cache_fetch( &req );
			if (pcache==NULL) {
				pcache = cache_new( &req );
				/* TASK 3.4: the ECM keeps its own cachetimeout and falls back
				 * to the card servers; the find is simply not served. */
				if (!pcache) break;
				pcache->ecm = req.ecm;
				pcache->ecm_lifetime_id = req.ecm_lifetime_id;
				cache_attach_local_provider(pcache, &req);
				pcache->cwcycle = req.cwcycle;

#ifdef CACHEEX
				if (len==base_len+16) memcpy( pcache->prevcw, req.prevcw, 16);
				memcpy( pcache->ecmd5, req.ecmd5, 16 );
#else
				if (len==base_len+16) memcpy( pcache->prevcw, req.prevcw, 16);
#endif
				/* TASK 3.6: arm only a waiter that is still waiting; a waiter
				 * that died while its FIND sat in the pipe must not leave the
				 * entry armed for its whole alive time. */
				if ( cache_sendpipe_armable(req.ecm, req.ecm_lifetime_id) ) pcache->flags |= CACHE_FLAG_SENDPIPE;
				// Send find failed
				pipe_cache2ecm_find_failed(pcache);
			}
			else {
				if (cache_local_provider_collision(pcache, &req)) {
					/* Wake this request's normal cache-miss flow without
					 * consuming or modifying the other local provider's entry. */
					uint8_t miss[48];
								int misslen = put_cache2ecm(PIPE_CACHE_FIND_FAILED, miss, sizeof(miss), &req, NULL, 0, 0, 0);
								if (misslen > 0) cache_ecm_pipe_send_required(prg.pipe.ecm[1], miss, misslen);
					break;
				}
				pcache->ecm = req.ecm;
				pcache->ecm_lifetime_id = req.ecm_lifetime_id;
				cache_attach_local_provider(pcache, &req);
				pcache->tag = req.tag; // set tag if not set (coming from cache)
				pcache->cwcycle = req.cwcycle;
#ifdef CACHEEX
				if (len==base_len+16) memcpy( pcache->prevcw, req.prevcw, 16);
				memcpy( pcache->ecmd5, req.ecmd5, 16 );
#else
				if (len==base_len+16) memcpy( pcache->prevcw, req.prevcw, 16);
#endif
				/* TASK 3.6: same arm guard as the new-entry branch. */
				if ( cache_sendpipe_armable(req.ecm, req.ecm_lifetime_id) ) pcache->flags |= CACHE_FLAG_SENDPIPE;


				/* R48 D-1: when this profile explicitly arms CACHE EVIDENCE,
				 * rank the candidates already stored for this hit. With the option
				 * OFF, retain the historical send path below byte-for-byte. */
				find_profile = cache_entry_cardserver_current(pcache);
				if (find_profile && find_profile->option.cache_evidence) {
					selected_candidate = cache_find_select_r48(pcache, &verify_line,
					    &find_hold_noted, &find_policy_noted);
					if (selected_candidate) {
						selected_candidate->status |= DCW_SENT;
						pipe_cache2ecm_find_success_with_origin(
						    pcache, selected_candidate->cw,
						    selected_candidate->peerid,
						    (int)selected_candidate->cwcycle,
					    cache_entry_cw_origin(pcache, selected_candidate->cw, NULL),
					    verify_line);
					}
				} else if ( !isnullDCW(pcache->prevcw) ) {

					// look for consecutif cw & same cycle
					struct cw_cache_data *cwdata = pcache->cwdata;
					while (cwdata) {
						if ( !(cwdata->status&DCW_ERROR) && !(cwdata->status&DCW_SENT) &&
				     !cache_find_consensus_held(pcache, cwdata, &find_hold_noted) )
						if ( (cwdata->status&DCW_CYCLE)&&(cwdata->nbpeers>=cfg.cache.threshold) ) {
							if ( pcache->cwcycle==cwdata->cwcycle ) {
								if (  ( (pcache->cwcycle==CW1CYCLE) && dcwcmp8(pcache->prevcw,cwdata->cw) && !similarcw(pcache->prevcw+8,cwdata->cw+8) ) ||
									( (pcache->cwcycle==CW0CYCLE) && !similarcw(pcache->prevcw,cwdata->cw) && dcwcmp8(pcache->prevcw+8,cwdata->cw+8) )  ) {
										if ( cachepref_gate(pcache, cwdata)==CACHEPREF_ALLOW ) {
											cwdata->status |= DCW_SENT;
											pipe_cache2ecm_find_success(pcache, cwdata->cw, cwdata->peerid );
										}
								}
							}
						}
						cwdata = cwdata->next;
					}

					// look for consecutif cw only
					cwdata = pcache->cwdata;
					while (cwdata) {
						if ( !(cwdata->status&DCW_ERROR) && !(cwdata->status&DCW_SENT) &&
				     !cache_find_consensus_held(pcache, cwdata, &find_hold_noted) )
						if ( !(cwdata->status&DCW_CYCLE) && (cwdata->nbpeers>=cfg.cache.threshold) ) {
							if (  ( (pcache->cwcycle==CW1CYCLE) && dcwcmp8(pcache->prevcw,cwdata->cw) && !similarcw(pcache->prevcw+8,cwdata->cw+8) ) ||
								( (pcache->cwcycle==CW0CYCLE) && !similarcw(pcache->prevcw,cwdata->cw) && dcwcmp8(pcache->prevcw+8,cwdata->cw+8) )  ) {
									if ( cachepref_gate(pcache, cwdata)==CACHEPREF_ALLOW ) {
										cwdata->status |= DCW_SENT;
										pipe_cache2ecm_find_success(pcache, cwdata->cw, cwdata->peerid );
									}
							}
						}
						cwdata = cwdata->next;
					}

				}

				else { // NO PREVIOUS CW

					// cached cw with cycle
					struct cw_cache_data *cwdata = pcache->cwdata;
					while (cwdata) {
						if ( !(cwdata->status&DCW_ERROR) && !(cwdata->status&DCW_SENT) &&
				     !cache_find_consensus_held(pcache, cwdata, &find_hold_noted) )
						if ( (cwdata->status&DCW_CYCLE)&&(cwdata->nbpeers>=cfg.cache.threshold) ) {
							if ( (pcache->cwcycle==NO_CYCLE)||(pcache->cwcycle==cwdata->cwcycle) ) {
								if ( cachepref_gate(pcache, cwdata)==CACHEPREF_ALLOW ) {
									cwdata->status |= DCW_SENT;
									pipe_cache2ecm_find_success(pcache, cwdata->cw, cwdata->peerid );
								}
							}
						}
						cwdata = cwdata->next;
					}

					// no cycle for cached cw
					cwdata = pcache->cwdata;
					while (cwdata) {
						if ( !(cwdata->status&DCW_ERROR) && !(cwdata->status&DCW_SENT) &&
				     !cache_find_consensus_held(pcache, cwdata, &find_hold_noted) ) {
						if (!(cwdata->status & DCW_CYCLE) && !find_policy_noted &&
						    (cfg.cache.filter || cwdata->nbpeers < cfg.cache.threshold)) {
							user_trace_cache_observation(pcache->caid, pcache->provid, pcache->sid,
							    pcache->hash, cwdata->peerid,
							    cfg.cache.filter ? "cache find deferred: cycle not established" :
							                       "cache find deferred: threshold not reached", cwdata->cw);
							find_policy_noted = 1;
						}
						/* Five arrivals alone do not satisfy an enabled cycle filter
						 * or an operator threshold greater than five. */
						if ( !(cwdata->status&DCW_CYCLE) && !cfg.cache.filter &&
						     cwdata->nbpeers>=5 && cwdata->nbpeers>=cfg.cache.threshold ) {
							if ( cachepref_gate(pcache, cwdata)==CACHEPREF_ALLOW ) {
								cwdata->status |= DCW_SENT;
								pipe_cache2ecm_find_success(pcache, cwdata->cw, cwdata->peerid );
							}
						}
						}
						cwdata = cwdata->next;
					}

				}

			}
			break;


		case PIPE_CACHE_REQUEST:
			if (len != base_len) break;
			if (get_ecm2cache(buf, len, &req, NULL) < 0) break;
			if (!cache_ecm_snapshot_read_locked(req.ecm, req.ecm_lifetime_id, NULL, NULL, NULL)) break;
			//debugf(0, " Get PIPE_CACHE_REQUEST: %04x:%06x:%04x:%08x\n", req.caid, req.provid, req.sid, req.hash);
			pcache = cache_fetch( &req );
			if (pcache==NULL) pcache = cache_new( &req );
			else {
				if (cache_local_provider_collision(pcache, &req)) break;
				pcache->tag = req.tag; // set tag if not set (coming from cache)
			}
			/* TASK 3.4: no entry could be made (allocation failed) -- the
			 * request to peers is dropped; the ECM's server flow is unchanged. */
			if (!pcache) break;
			pcache->ecm = req.ecm;
				pcache->ecm_lifetime_id = req.ecm_lifetime_id;
			cache_attach_local_provider(pcache, &req);
			/* TASK 3.6: same arm guard -- the REQUEST rides the pipe too, and
			 * its waiter can be answered or failed before the cache thread gets
			 * here. */
			if ( cache_sendpipe_armable(req.ecm, req.ecm_lifetime_id) ) pcache->flags |= CACHE_FLAG_SENDPIPE;
			// Send Request if not dcw sent
			if (!(pcache->flags&CACHE_FLAG_REQSENT)) {
				pcache->flags |= CACHE_FLAG_REQSENT;
				cfg.cache.req++;
				cache_send_request(pcache,NULL);
			}
			break;


		case PIPE_CACHE_REPLY: {
			int expected = base_len + (int)CACHE_PIPE_REPLY_CW_LEN;
			/* Reject incomplete or unknown trailer bits rather than attributing
			 * a cache-sourced key to a local source. */
			if (len != expected + (int)CACHE_PIPE_REPLY_TRAILER_LEN ||
			    buf[expected + CACHE_PIPE_REPLY_FROM_CACHE_OFFSET] > 1 ||
			    !cache_origin_mask_valid(
			        buf[expected + CACHE_PIPE_REPLY_ORIGIN_MASK_OFFSET])) break;
			int from_cache = buf[expected + CACHE_PIPE_REPLY_FROM_CACHE_OFFSET];
			uint8_t origin_mask = buf[expected + CACHE_PIPE_REPLY_ORIGIN_MASK_OFFSET];
			if (get_ecm2cache(buf, len, &req, cw) < 0) break;
			if (!cache_ecm_snapshot_read_locked(req.ecm, req.ecm_lifetime_id, NULL, NULL, NULL)) break;
			//debugf(0, " Get PIPE_CACHE_REPLY: %04x:%06x:%04x:%08x\n", req.caid, req.provid, req.sid, req.hash);

			pcache = cache_fetch( &req );
			if (pcache==NULL) pcache = cache_new( &req );
			else {
				if (cache_local_provider_collision(pcache, &req)) break;
				pcache->tag = req.tag; // set tag if not set (coming from cache)
			}
			/* TASK 3.4: the key has nowhere to be remembered (allocation
			 * failed); it is dropped -- its sender's client was already served
			 * and nothing else is waiting on it. */
			if (!pcache) break;
			cache_attach_local_provider(pcache, &req);
			uint32_t consensus_provider = 0;
			int consensus_provider_known = cache_consensus_provider_scope(pcache, &consensus_provider);
			//Check & update DCW
			struct cw_cache_data *cwdata = pcache->cwdata;
			while (cwdata) {
				if ( dcwcmp16(cwdata->cw, cw) ) break;
				cwdata = cwdata->next;
			}
			// ADD if not found
			if (!cwdata) {
				cwdata = malloc( sizeof(struct cw_cache_data) );
				/* TASK 3.4: the remembered copy is skipped; the reply below
				 * still goes out from the local key. */
				if (cwdata) {
					memset( cwdata, 0, sizeof(struct cw_cache_data) );
					memcpy(cwdata->cw, cw, 16);
					//cwdata->cwcycle = NO_CYCLE;
					//cwdata->peerid = peerid;
					cwdata->next = pcache->cwdata;
					pcache->cwdata = cwdata;
					if (!from_cache && cfg.cache.dcwcheck2) cache_fetch_goodcw2(pcache, cw, 0, origin_mask);
					if (!from_cache && cfg.cache.dcwcheck3) cache_fetch_goodcw3(pcache, cw, 0, origin_mask);
				}
				}
				/* Preserve all locally observed ingress classes for this exact CW.
				 * A cache hit never clears or replaces prior line/peer provenance. */
				if (cwdata)
					cwdata->origin_mask = cache_origin_merge(cwdata->origin_mask, origin_mask);
				/* A local source has already passed the ECM's own acceptance path.
				 * Keep that provenance distinct from peer arrival counts. */
				if (cwdata && !from_cache) cwdata->status |= DCW_PEER_READY | DCW_LOCAL_SOURCE;

			/* A cache-derived CW is not a fresh local-source response. It may
			 * only bridge to CSP when forwarding is enabled and the existing
			 * cache candidate passed its own gates; do not invent confirmation
			 * if the original entry was already recycled. */
			if (!from_cache || (cfg.cache.forward && cwdata &&
			    !(cwdata->status & DCW_ERROR) && (cwdata->status & DCW_PEER_READY) &&
			    cwdata->nbpeers >= cfg.cache.threshold &&
			    (!cfg.cache.consensus || !cwconsensus_is_held(pcache->caid, pcache->sid,
			        consensus_provider, consensus_provider_known, pcache->hash,
			        pcache->tag, cwdata->cw)))) {
				if ( !(pcache->flags&CACHE_FLAG_REPSENT) ) cfg.cache.rep++;
				pcache->flags |= CACHE_FLAG_REPSENT;
				cache_send_reply(pcache, NULL, cw);
			} else if (from_cache) {
				user_trace_cache_observation(pcache->caid, pcache->provid, pcache->sid,
				    pcache->hash, 0, "cache-origin reply not rebroadcast: disabled or candidate unavailable", NULL);
			}
			break;
		}

		case PIPE_CACHE_RESENDREQ:
			if (len != base_len) break;
			if (get_ecm2cache(buf, len, &req, NULL) < 0) break;
			if (!cache_ecm_snapshot_read_locked(req.ecm, req.ecm_lifetime_id, NULL, NULL, NULL)) break;
/*
			pcache = cache_fetch( &req );
			if (pcache==NULL) pcache = cache_new( &req );
			else {
				struct cw_cache_data *cwdata = pcache->cwdata;
				while (cwdata) {
					pipe_cache2ecm_find_success(pcache, cwdata->cw, cwdata->peerid );
					cwdata = cwdata->next;
				}
			}
*/
			cache_send_resendreq(&req);
			break;

	}
}


/* Caller holds the config lifetime read gate; this function owns lockcache. */
void cache_check_peers(struct cacheserver_data *cache)
{
	struct cachepeer_data *peer;
	pthread_mutex_lock(&prg.lockcache);
	peer = cache->peer;
	while (peer) {
		if (!IS_DISABLED(peer->flags))
		if ( (host_ip_snapshot(peer->host))&&(peer->port) ) {
			uint32_t ticks = GetTickCount();
			if (peer->ping==0) { // inactive
				if ( (!peer->lastpingsent)||((uint32_t)(ticks-peer->lastpingsent)>9000u) ) { // send every 15s
					cache_send_ping(cache, peer);
					peer->lastpingsent = ticks;
					peer->lastpingrecv = 0;
					peer->ping = -1;
				}
			}
			else if (peer->ping==-1) { // inactive
				if ( (!peer->lastpingsent)||((uint32_t)(ticks-peer->lastpingsent)>19000u) ) { // send every 15s
					cache_send_ping(cache, peer);
					peer->lastpingsent = ticks;
					peer->lastpingrecv = 0;
					peer->ping = -2;
				}
			}
			else if (peer->ping==-2) { // inactive
				if ( (!peer->lastpingsent)||((uint32_t)(ticks-peer->lastpingsent)>29000u) ) { // send every 15s
					cache_send_ping(cache, peer);
					peer->lastpingsent = ticks;
					peer->lastpingrecv = 0;
					peer->ping = -3;
				}
			}
			else if (peer->ping<=-3) { // inactive
				if ( (!peer->lastpingsent)||((uint32_t)(ticks-peer->lastpingsent)>59000u) ) { // send every 15s
					cache_send_ping(cache, peer);
					peer->lastpingsent = ticks;
					peer->lastpingrecv = 0;
				}
			}
			else if (peer->ping>0) {
				if ( (!peer->lastpingrecv)&&((uint32_t)(ticks-peer->lastpingsent)>9000u) ) {
					if (peer->lastpingnb==1) {
#ifdef NEWCACHE
						cache_send_keepalive(cache, peer);
#else
						cache_send_ping(cache, peer);
#endif
						peer->lastpingsent = ticks;
						peer->lastpingnb = 2;
					}
					else if (peer->lastpingnb==2) {
#ifdef NEWCACHE
						cache_send_keepalive(cache, peer);
#else
						cache_send_ping(cache, peer);
#endif
						peer->lastpingsent = ticks;
						peer->lastpingnb = 3;
					}
					else if (peer->lastpingnb==3) {
						cache_send_ping(cache, peer);
						peer->lastpingsent = ticks;
						peer->lastpingrecv = 0;
						peer->ping = 0;
						host_checkiptime_set(peer->host, 0); // maybe ip changed
						//ipeer_update(cache);
					}
				}
				else if ( (uint32_t)(ticks-peer->lastpingsent)>60000u ) { // send every 75s
#ifdef NEWCACHE
					cache_send_keepalive(cache, peer);
#else
					cache_send_ping(cache, peer);
#endif
					peer->lastpingsent = ticks;
					peer->lastpingrecv = 0;
					peer->lastpingnb = 1;
				}
			}
		}
		peer = peer->next;
	}
	pthread_mutex_unlock(&prg.lockcache);
}


///////////////////////////////////////////////////////////////////////////////
// 
///////////////////////////////////////////////////////////////////////////////

#ifdef EPOLL_CACHE


void *cache_thread(void *param)
{
	__atomic_store_n(&prg.pid_cache, (pid_t)syscall(SYS_gettid), __ATOMIC_RELEASE);
	prg.tid_cache = pthread_self();
	prctl(PR_SET_NAME,"Cache RecvMSG Thread",0,0,0);

	sleep(3);

	int i;
	struct epoll_event evlist[MAX_EPOLL_EVENTS]; // epoll recv events

	uint32_t chkticks = 0;
	while (!prg.restart) {
		newcamd_config_read_lock();
		uint64_t config_generation = cfg.cache.generation;
		// Check Peers Ping
		if ( (uint32_t)((uint32_t)GetTickCount()-chkticks)>5000u ) {
			struct cacheserver_data *cache = cfg.cache.server;
			while (cache) {
				cache_check_peers(cache);
				cache = cache->next;
			}
			chkticks = GetTickCount();
		}
		/* TASK R8 (D62): every wake releases consensus holds whose window
		 * expired -- a single-source channel loses only the window, never
		 * the key (each loop pass is one housekeeping tick; the release
		 * cadence is therefore window + one tick, worst case). */
		pthread_mutex_lock(&prg.lockcache);
		while ( cfg.cache.consensus ) {
			struct cwconsensus_hold cch;
			struct cache_data creq;
			struct cache_data *chpc;
			if ( !cwconsensus_expired(&cch, GetTickCount(),
			                          (uint32_t)(cfg.cache.consensus_window>=100?cfg.cache.consensus_window:400)) )
				break;
			memset(&creq, 0, sizeof(creq));
			creq.caid = cch.caid; creq.sid = cch.sid; creq.hash = cch.hash; creq.tag = cch.tag;
			chpc = cache_fetch(&creq);
			if (chpc) {
				struct cw_cache_data *chw = chpc->cwdata;
				while (chw) {
					if ( dcwcmp16(chw->cw, cch.cw) ) {
						if ( !(chw->status & (DCW_SENT|DCW_ERROR)) ) {
							/* Expiry only clears consensus; the normal cycle/family
							 * gates still decide whether the CW may be forwarded. */
							user_trace_cache_observation(chpc->caid, chpc->provid, chpc->sid, chpc->hash, cch.peerid,
							    "cache consensus window expired; cycle checks pending", chw->cw);
							cache_after_consensus(chpc, chpc, chw, chw->cw,
							                      (cwcycle_t)cch.cycle, cch.peerid);
							debugf(getdbgflag(DBG_CACHE,0,0),
								" [CONSENSUS] window expired, CW rechecked ch %04x:%06x:%04x\n",
								cch.caid, chpc->provid, cch.sid);
						}
						break;
					}
					chw = chw->next;
				}
			}
		}
		pthread_mutex_unlock(&prg.lockcache);

		int consensus_enabled = !!cfg.cache.consensus;
		int poll_timeout = consensus_enabled ? 100 : 1001;
		newcamd_config_read_unlock();
		int ready = epoll_wait( prg.epoll.cache, evlist, MAX_EPOLL_EVENTS, poll_timeout);
		if (ready < 0) {
			if ( (errno==EINTR)||(errno==EAGAIN) ) usleep(1000);
			else {
				usleep(99000);
				debugf(DBG_ERROR,"Err! epoll_wait (%d)", errno);
			}
			continue;
		}
		if (ready == 0) continue; // timeout

		newcamd_config_read_lock();
		if (config_generation != cfg.cache.generation) {
			newcamd_config_read_unlock();
			continue;
		}

		for (i=0; i < ready; i++) {
				#ifdef CACHE_QUEUE
				if ( evlist[i].events & (EPOLLIN|EPOLLPRI) ) cache_queue_recv(evlist[i].data.ptr);
#else
				if ( evlist[i].events & (EPOLLIN|EPOLLPRI) ) cache_recvmsg(evlist[i].data.ptr);
#endif
			}
			newcamd_config_read_unlock();
		}
		return NULL;
}

#else

void *cache_thread(void *param)
{
	__atomic_store_n(&prg.pid_cache, (pid_t)syscall(SYS_gettid), __ATOMIC_RELEASE);
	prg.tid_cache = pthread_self();
	prctl(PR_SET_NAME,"Cache RecvMSG Thread",0,0,0);

	uint32_t chkticks = 0;
	while (!prg.restart) {
		newcamd_config_read_lock();
		uint64_t config_generation = cfg.cache.generation;
		// Check Peers Ping
		if ( (uint32_t)((uint32_t)GetTickCount()-chkticks)>3000u ) {
			/*
			 * TASK 1.9 -- SERVICE-BLACKLIST-TIME expiry and the STATS-WINDOW
			 * summary, both on this already-existing three-second wakeup so that
			 * neither costs a thread, a timer or a poll. phase1_stats_tick()
			 * does nothing at all when both options are at their defaults.
			 * TASK 2.10 rides the same wakeup: fade idle trust, then a
			 * throttled persistence snapshot -- both off the ECM path.
			 */
			phase1_stats_tick();
			tl_tick( GetTickCount() );

			struct cacheserver_data *cache = cfg.cache.server;
			while (cache) {
				cache_check_peers(cache);
				cache = cache->next;
			}
			chkticks = GetTickCount();
		}

		/* TASK R8 (D62): the poll flavour's identical release walk. */
		pthread_mutex_lock(&prg.lockcache);
		while ( cfg.cache.consensus ) {
			struct cwconsensus_hold cch;
			struct cache_data creq;
			struct cache_data *chpc;
			if ( !cwconsensus_expired(&cch, GetTickCount(),
			                          (uint32_t)(cfg.cache.consensus_window>=100?cfg.cache.consensus_window:400)) )
				break;
			memset(&creq, 0, sizeof(creq));
			creq.caid = cch.caid; creq.sid = cch.sid; creq.hash = cch.hash; creq.tag = cch.tag;
			chpc = cache_fetch(&creq);
			if (chpc) {
				struct cw_cache_data *chw = chpc->cwdata;
				while (chw) {
					if ( dcwcmp16(chw->cw, cch.cw) ) {
						if ( !(chw->status & (DCW_SENT|DCW_ERROR)) ) {
							/* Expiry only clears consensus; the normal cycle/family
							 * gates still decide whether the CW may be forwarded. */
							user_trace_cache_observation(chpc->caid, chpc->provid, chpc->sid, chpc->hash, cch.peerid,
							    "cache consensus window expired; cycle checks pending", chw->cw);
							cache_after_consensus(chpc, chpc, chw, chw->cw,
							                      (cwcycle_t)cch.cycle, cch.peerid);
							debugf(getdbgflag(DBG_CACHE,0,0),
								" [CONSENSUS] window expired, CW rechecked ch %04x:%06x:%04x\n",
								cch.caid, chpc->provid, cch.sid);
						}
						break;
					}
					chw = chw->next;
				}
			}
		}
		pthread_mutex_unlock(&prg.lockcache);

		struct pollfd pfd[100];
		int pfdcount = 0;
		int consensus_enabled = !!cfg.cache.consensus;
		int poll_timeout = consensus_enabled ? 100 : 3001;
#ifndef THREAD_CACHE_PIPE
		pfd[pfdcount].fd = prg.pipe.cache[0];
		pfd[pfdcount].events = POLLIN | POLLPRI;
		pfdcount++;
#endif
		struct cacheserver_data *cache = cfg.cache.server;
		while (cache) {
			if (cache->handle>0) {
				cache->ipoll = pfdcount;
				pfd[pfdcount].fd = cache->handle;
				pfd[pfdcount++].events = POLLIN | POLLPRI;
			} else cache->ipoll = -1;
			cache = cache->next;
		}

		newcamd_config_read_unlock();
		int retval = poll(pfd, pfdcount, poll_timeout);

		if ( retval>0 ) {
			newcamd_config_read_lock();
			if (config_generation != cfg.cache.generation) {
				newcamd_config_read_unlock();
				continue;
			}
#ifndef THREAD_CACHE_PIPE
			if ( pfd[0].revents & (POLLIN|POLLPRI) ) {
				/* R6 (D60): the request fan-out stays HERE, exactly where
				 * stock runs it -- inline under lockcache on the receive
				 * thread. Only the peer datagrams move to the worker; the
				 * pipe is the ECM thread's interface and moving it measured
				 * as pure regression (finds stalled behind the worker's
				 * wakeup latency). */
				pthread_mutex_lock( &prg.lockcache );
				cache_pipe_recvmsg();
				pthread_mutex_unlock( &prg.lockcache );
			}
#endif
			struct cacheserver_data *cache = cfg.cache.server;
			while (cache) {
				if ( (cache->handle>0)&&(cache->ipoll>=0)&&(cache->handle==pfd[cache->ipoll].fd) )
				if ( pfd[cache->ipoll].revents & (POLLIN|POLLPRI) ) {
					//pthread_mutex_lock( &prg.lockcache );
#ifdef CACHE_QUEUE
					cache_queue_recv(cache);
#else
					cache_recvmsg(cache);
#endif
					//pthread_mutex_unlock( &prg.lockcache );
				}
				cache = cache->next;
			}
				newcamd_config_read_unlock();
			} else if (retval < 0 || !consensus_enabled) usleep( 99000 );
		}

		//close(cfg.cache.handle);
	return NULL;
}

#endif


///////////////////////////////////////////////////////////////////////////////
// 
///////////////////////////////////////////////////////////////////////////////
#ifdef THREAD_CACHE_PIPE

void *cache_pipe_thread(void *param)
{
	__atomic_store_n(&prg.pid_cache_pipe, (pid_t)syscall(SYS_gettid), __ATOMIC_RELEASE);
	prg.tid_cache_pipe = pthread_self();
	prctl(PR_SET_NAME,"Cache Pipe Thread",0,0,0);

	while (!prg.restart) {
		struct pollfd pfd;
		pfd.fd = prg.pipe.cache[0];
		pfd.events = POLLIN | POLLPRI;
		int retval = poll(&pfd, 1, 3031);
			if ( retval>0 ) {
				newcamd_config_read_lock();
				pthread_mutex_lock( &prg.lockcache );
				cache_pipe_recvmsg();
				pthread_mutex_unlock( &prg.lockcache );
				newcamd_config_read_unlock();
			}
			else usleep( 99000 );
	}
	return NULL;
}

#endif
///////////////////////////////////////////////////////////////////////////////
// 
///////////////////////////////////////////////////////////////////////////////

#ifdef CACHE_QUEUE
/*
 * The receive-side half: recvfrom plus the wire range check -- the same
 * range guard the inline path ran first -- then the hand-off. Only
 * malformed CSP replies may take the diagnostic lock before the queue.
 */
static void cache_queue_recv(struct cacheserver_data *cache)
{
	unsigned char buf[2048];
	struct sockaddr_in si_other;
	socklen_t slen = sizeof(si_other);
	int received = recvfrom( cache->handle, buf, sizeof(buf), 0, (struct sockaddr*)&si_other, &slen);
	if ( (received<2)||(received>512) ) {
        if (received > 0 && buf[0] == TYPE_REPLY)
            cache_trace_csp_drop(buf, received, 0, received > 512 ? "oversized datagram" : "incomplete header");
        return;
    }
	cq_push(cache->id, cache->handle, cfg.cache.generation,
	        &si_other, buf, received);
}

/*
 * The worker: pops raw datagrams and renders them through the UNCHANGED
 * cache_recvmsg() switch (parse, forward, lockcache -- all of it), so
 * both the inline path and the queued path are one code path with two
 * packet sources. A one-line throttled tally says when the ring did
 * work; silent otherwise.
 */
void *cache_queue_thread(void *param)
{
	prctl(PR_SET_NAME,"Cache Queue Thread",0,0,0);
	uint32_t lastreported = 0;
	/* The worker polls the ring at a 10 ms idle cadence: against the 2 s
	 * CACHE TIMEOUT (and even the 400 ms DCW TIMEOUT) a queued datagram's
	 * worst-case 10 ms wait is noise, and the process idles like stock
	 * instead of burning a core on wake-ups. 100 wake-ups/s cost less
	 * than 1% here; every shorter cadence tried in the rig measured
	 * worse (the 200/1000 adaptive pair peaked near 7 s of CPU for the
	 * same workload the flat 10 ms does in a fraction of). */
	while (!prg.restart) {
		if ( (uint32_t)((uint32_t)GetTickCount()-lastreported)>60000u ) {
			/* time-gated (not pop-gated): one line per minute whenever the
			 * queue did ANY work since startup, so a silent log means an
			 * idle queue and never a stuck one */
			if ( cq_enqueued() || cq_dropped() )
				debugf(getdbgflag(DBG_CACHE,0,0)," [CACHE QUEUE] enqueued %lu dropped %lu\n",
					cq_enqueued(), cq_dropped());
			lastreported = GetTickCount();

			/* CACHE-AI: Periodic cleanup — unban expired peers, decay inactive trust */
			ci_tick(ci_now_ms());
		}
			struct cq_entry e;
			struct cacheserver_data *cache;
			if (!cq_pop(&e)) { usleep(10000); continue; }
			/* Resolve only while the config graph is pinned. A queued raw
			 * pointer could outlive a reload and be recycled for another peer. */
			newcamd_config_read_lock();
			if (e.config_generation != cfg.cache.generation) {
				newcamd_config_read_unlock();
				continue;
			}
			cache = cfg.cache.server;
			while (cache && !cq_source_matches(&e, cache->id, cache->handle,
			                                    cfg.cache.generation))
				cache = cache->next;
			if (!cache) {
				newcamd_config_read_unlock();
				continue;
			}
			cq_current = e;
			cq_active = 1;
			cache_recvmsg(cache);
			cq_active = 0;
			newcamd_config_read_unlock();
	}
	return NULL;
}
#endif /* CACHE_QUEUE */

int start_thread_cache()
{
	create_thread(PACKED_MPTR(&prg, pthread_t, tid_cache), cache_pipe_out_thread, NULL);
#if !defined(THREAD_DCW) && defined(CLI_CSCACHE)
	if (!create_thread(NULL, server_cscache_reply_out_thread, NULL)) {
		debugf(DBG_ERROR, " !!! unable to start cached-server ECM pipe writer\n");
		prg.restart = 1;
		return -1;
	}
#endif
#ifdef THREAD_CACHE_PIPE
	create_thread(PACKED_MPTR(&prg, pthread_t, tid_cache), (threadfn)cache_pipe_thread,NULL);
#endif

#ifdef CACHE_QUEUE
	cq_init();
	create_thread(PACKED_MPTR(&prg, pthread_t, tid_cache), (threadfn)cache_queue_thread,NULL);
#endif

	create_thread(PACKED_MPTR(&prg, pthread_t, tid_cache), (threadfn)cache_thread,NULL);
	return 0;
}
