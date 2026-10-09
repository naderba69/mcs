
void stringtohtml(char *src, char *dest)
{
	while ( *src )
	{
		if ( *src=='<' ) {
			*dest = '&'; dest++;
			*dest = 'l'; dest++;
			*dest = 't'; dest++;
			*dest = ';'; dest++;
		}
		else if ( *src=='>' ) {
			*dest = '&'; dest++;
			*dest = 'g'; dest++;
			*dest = 't'; dest++;
			*dest = ';'; dest++;
		}
		else if ( *src=='&' ) {
			*dest = '&'; dest++;
			*dest = 'a'; dest++;
			*dest = 'm'; dest++;
			*dest = 'p'; dest++;
			*dest = ';'; dest++;
		}
		else if ( *src=='"' ) {
			*dest = '&'; dest++;
			*dest = 'q'; dest++;
			*dest = 'u'; dest++;
			*dest = 'o'; dest++;
			*dest = 't'; dest++;
			*dest = ';'; dest++;
		}
		else {
			*dest = *src; dest++;
		}
		src++;
	}
	*dest = 0;
}

/*
void fpeer_update(struct cacheserver_data *cache)
{
	memset( cache->fpeer, 0, sizeof(cache->fpeer) ); // NULL
	struct cachepeer_data *peer = cache->peer;
	while (peer) {
		if ( peer->host->ip ) {
			int index = peer->host->ip & 0xFF;
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

// 0: removed
int peer_doublecheck(struct cacheserver_data *cache, struct cachepeer_data *xpeer)
{
	struct cachepeer_data *peer = cache->peer;
	struct cachepeer_data *previous = NULL;
	while (peer) {
		if (peer!=xpeer)
		if (peer->port==xpeer->port)
		if (peer->host->ip==xpeer->host->ip) {
			if (peer->runtime) {
				if (previous) previous->next = peer->next; else cache->peer = peer->next;
				//close(peer->outsock);
				free( peer );
				return 0;				
			}
			else if (xpeer->runtime) peer_doublecheck(cache, peer);
			return 1;
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
#ifdef MCS_DCWSTATS
#include "cwlog.h"		/* TASK R11 (D65): the per-CW verdict ring */
#endif

struct cachepeer_data *getpeerbyaddr(struct cacheserver_data *cache, uint32_t ip, uint16_t port)
{
	struct cachepeer_data *peer = cache->peer;
	while(peer) {
		if ( (peer->host->ip==ip)&&(peer->recvport==port) ) return peer;
		peer = peer->next;
	}
	return NULL;
}

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
// A full ECM identity is present in ecmd5 (set only from the local ECM pipe).
#define CACHE_FLAG_ECMD5_VALID 0x20


typedef enum { NO_CYCLE=0, CW0CYCLE=1, CW1CYCLE=2} cwcycle_t;

#define DCW_ERROR    0x01
#define DCW_CYCLE    0x02
#define DCW_CHECKED  0x04
#define DCW_SKIP     0x08
#define DCW_SENT     0x10

struct cw_cache_data {
	struct cw_cache_data *next;
	uint8_t cw[16]; // for storing all codes
	uint32_t cwsum; // Checksum
	uint8_t status;
	cwcycle_t cwcycle; // cw1='1' / cw0='0'
	uint32_t peerid; // fisrt peerid 
	uint16_t nbpeers; // number of peers reporting this dcw
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
};

#ifdef CACHEEX
/*
 * The CSP wire format carries only a 32-bit hash, not the full ECM MD5.
 * Establish the cache entry's MD5 provenance only when it arrives from the
 * local ECM pipe; a peer-created entry must never make arbitrary bytes look
 * like a valid reuse identity.
 */
static void cache_set_ecmd5(struct cache_data *pcache, const uint8_t ecmd5[16])
{
	memcpy(pcache->ecmd5, ecmd5, sizeof(pcache->ecmd5));
	pcache->flags |= CACHE_FLAG_ECMD5_VALID;
}

/*
 * Deterministic M6 test seam: CSP has no MD5 field, so fill the otherwise
 * absent bytes with key-derived garbage when explicitly requested. This lets
 * the live regression test prove that invalid bytes are rejected even when
 * they differ between services; the validity flag remains clear.
 */
static void cache_test_csp_md5_poison(struct cache_data *req)
{
	int i;
	if (!getenv("MCS_TEST_CSP_ECMD5_POISON")) return;
	for (i = 0; i < (int)sizeof(req->ecmd5); i++) {
		req->ecmd5[i] = (uint8_t)((req->sid >> ((i & 1) * 8))
					 ^ (req->hash >> ((i & 3) * 8))
					 ^ (0xA5u + (unsigned)i * 17u));
	}
}
#endif

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
	 * returned table. The node and the 1024-slot table (~88 KB per CAID) are
	 * checked here, once, at the only place they are born: on failure the
	 * table for this CAID simply does not exist yet, the caller drops the one
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
		if ( (new->recvtime+cfg.cache.alivetime) < ticks ) {
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
#ifdef CACHEEX
	/*
	 * Retain the bytes for cache-exchange forwarding, but do not call them a
	 * local ECM identity. CSP TYPE_REQUEST/TYPE_REPLY normally zero-initialize
	 * `req` because their wire format has no MD5 (the M6 regression hook can
	 * poison the absent bytes deliberately); validity is set only by
	 * cache_set_ecmd5() when a local ECM-pipe message supplies the digest.
	 */
	memcpy(pcache->ecmd5, newdata->ecmd5, sizeof(pcache->ecmd5));
	pcache->flags &= (uint8_t)~CACHE_FLAG_ECMD5_VALID;
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
		if ( (pcache->recvtime+cfg.cache.alivetime) < ticks ) return NULL;
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
 * TASK 3.6 -- the death of a cache entry's waiter, made visible to the entry.
 *
 * CACHE_FLAG_SENDPIPE arms an entry's push path: the local FIND/REQUEST
 * handlers set it while a local ECM is waiting on the cache (pcache->ecm),
 * and every push -- cache_setdcw()'s forced push, the FIND scans, the
 * cross-channel goodcw walks -- is gated on it, as is the peer-request
 * auto-answer in TYPE_REQUEST. Nothing in the tree ever cleared it, so one
 * FIND on a hash kept the entry armed for its whole alive time (45 s by
 * default): keys pushed after the waiter's death were force-marked DCW_SENT
 * and piped at a waiter that was no longer there -- buried for every future
 * ECM of that hash -- and peers asking for the hash got silence.
 *
 * This is the clearing half. The two death moments of the waiter call it:
 * ecm_setdcwdata() when the waiter is answered (SUCCESS, any source) and
 * ecm_faileddcw() when it gives up (FAILED). The guard is the back-pointer
 * itself: only the link THIS entry holds is cleared, so a newer waiter that
 * re-armed the entry keeps its arm. Callers must have committed the death
 * (dcwstatus) BEFORE calling: the FIND/REQUEST handlers refuse to arm a
 * waiter that is already answered or failed, and both this helper and those
 * handlers run under lockcache, so "clear first, arm later" cannot
 * interleave back into a stuck flag. Never cleared earlier: while the waiter
 * lives (WAITCACHE, or WAIT with the card servers racing) the arm IS the
 * delivery path -- clearing it there is the black screen this task fixes.
 */
void cache_clear_sendpipe( ECM_DATA *ecm )
{
	struct cache_data req;
	struct cache_data *pcache;
	memset( &req, 0, sizeof(req) );
	req.tag = ecm->ecm[0];
	req.sid = ecm->sid;
	req.caid = ecm->caid;
	req.hash = ecm->hash;
	pthread_mutex_lock( &prg.lockcache );
	pcache = cache_fetch( &req );
	if ( pcache && (pcache->ecm==ecm) ) {
		pcache->flags &= ~CACHE_FLAG_SENDPIPE;
	}
	pthread_mutex_unlock( &prg.lockcache );
}


/*
 * TASK 3.6 -- may this FIND/REQUEST still arm SENDPIPE for this ECM?
 *
 * A waiter that is already answered (SUCCESS) or has given up (FAILED) must
 * not arm the entry: the arm would outlive the waiter by the entry's alive
 * time and the flag would be stuck again. This closes the death-before-the-
 * pipe race: ecm_faileddcw() commits dcwstatus BEFORE cache_clear_sendpipe(),
 * so an arm decision made under lockcache after that commit sees the dead
 * waiter, and one made before it is undone by the clear. Called only with
 * the pipe's own fresh ECM (milliseconds old, un-recyclable) under
 * lockcache; the int read is the same lock-free read the ecm thread itself
 * does in check_ecm().
 */
static int cache_sendpipe_armable( ECM_DATA *ecm )
{
	if (!ecm) return 0;
	if (ecm->dcwstatus==STAT_DCW_SUCCESS) return 0;
	if (ecm->dcwstatus==STAT_DCW_FAILED) return 0;
	return 1;
}


/*
int checkcycle( uint8_t cw1cycle, uint8_t ecmtag, uint8_t cwcycle ) 
{
	if (cfg.cache.filter) {
		// Check Cycle
		if (cw1cycle==0x80) { // cw1 cycle on tag=0x80
			if ( (ecmtag==0x80)&&(cwcycle==0) ) return 0;
			if ( (ecmtag==0x81)&&(cwcycle==1) ) return 0;
		}
		else if (cw1cycle==0x81) { // cw1 cycle on tag=0x81
			if ( (ecmtag==0x81)&&(cwcycle==0) ) return 0;
			if ( (ecmtag==0x80)&&(cwcycle==1) ) return 0;
		}
	}
	return 1;
}
*/









///////////////////////////////////////////////////////////////////////////////
// CACHE ---> ECM
///////////////////////////////////////////////////////////////////////////////

int get_cache2ecm( uint8_t *buf, struct cache_data *pcache, uint8_t *cw, int *cwmark )
{
	pcache->tag = buf[1];
	pcache->sid = (buf[2]<<8) | buf[3];
	pcache->onid = (buf[4]<<8) | buf[5];
	pcache->caid = (buf[6]<<8) | buf[7];
	pcache->hash = (buf[8]<<24) | (buf[9]<<16) | (buf[10]<<8) | (buf[11]);
//	memcpy( &(pcache->tag), buf+1, 11);
	int index = 12;
	memcpy( &(pcache->ecm), buf+index, sizeof(void*) );
	//debugf(0, " get_cache2ecm %p\n", pcache->ecm);
	index += sizeof(void*);
	if (cw) {
		int peerid = (buf[index]<<24) | (buf[index+1]<<16) | (buf[index+2]<<8) | (buf[index+3]);
		index+=4;
		memcpy( cw, buf+index, 16);
		index+=16;
		/* TASK 2.5: the cycle declared by the key's sender, if any. */
		if (cwmark) *cwmark = buf[index];
		return peerid;
	}
	return 0;
}

int put_cache2ecm(uint8_t type, uint8_t *buf, struct cache_data *pcache, uint8_t *cw, int peerid, int cwmark )
{
	buf[0] = type;
	buf[1] = pcache->tag;
	buf[2] = pcache->sid>>8; buf[3] = pcache->sid&0xff;
	buf[4] = pcache->onid>>8; buf[5] = pcache->onid&0xff;
	buf[6] = pcache->caid>>8; buf[7] = pcache->caid&0xff;
	buf[8] = pcache->hash>>24; buf[9] = pcache->hash>>16; buf[10] = pcache->hash>>8; buf[11] = pcache->hash & 0xff;
//	memcpy( buf+1, &(pcache->tag), 11);
	int index = 12;
	memcpy( buf+index, &pcache->ecm, sizeof(void*) );
	//debugf(0, " put_cache2ecm %p\n", pcache->ecm);
	index += sizeof(void*);
	if (cw) {
		buf[index] = peerid>>24; buf[index+1] = peerid>>16; buf[index+2] = peerid>>8; buf[index+3] = peerid & 0xff;
		index+=4;
		memcpy( buf+index, cw, 16);
		index+=16;
		/* TASK 2.5: the cycle the key's owning cwdata declared, riding with
		 * the key so the ECM side can see a contradiction at delivery time. */
		buf[index] = (uint8_t)cwmark;
		index+=1;
	}
	return index;
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

void pipe_cache2ecm_find_failed(struct cache_data *pcache)
{
	uint8_t buf[48];
	int len = put_cache2ecm(PIPE_CACHE_FIND_FAILED, buf, pcache, NULL, 0, 0);
	pipe_send( prg.pipe.ecm[1], buf, len);
}

void pipe_cache2ecm_find_success(struct cache_data *pcache, uint8_t *cw, int peerid )
{
	uint8_t buf[48];
#ifdef MCS_DCWSTATS
	/* TASK R11 (D65): the single delivery funnel -- every accepted key
	 * reaches its waiter (or the store) through this pipe send, the
	 * window-release walk included. waiter = an ECM is parked on this
	 * entry; without one the key is a seed, and the page says so. */
	cwlog_deliver(peerid, pcache->caid, pcache->provid, pcache->sid, cw,
		      pcache->ecm ? 1 : 0);
#endif
	/*
	 * TASK 2.5: the marker travels with the key. It is read from the entry's
	 * own cwdata -- the same list the callers just walked -- so the call
	 * sites stay untouched; a key that is not in this entry's list (the
	 * cross-channel dcwcheck2/3 relays) has no declaration behind it and
	 * rides with NO_CYCLE, which is the truth about it.
	 */
	int cwmark = NO_CYCLE;
	struct cw_cache_data *cwd = pcache->cwdata;
	while (cwd) {
		if ( dcwcmp16(cwd->cw, cw) ) { cwmark = cwd->cwcycle; break; }
		cwd = cwd->next;
	}
	int len = put_cache2ecm(PIPE_CACHE_FIND_SUCCESS, buf, pcache, cw, peerid, cwmark);
	pipe_send( prg.pipe.ecm[1], buf, len);
}

///////////////////////////////////////////////////////////////////////////////
// ECM ---> CACHE
///////////////////////////////////////////////////////////////////////////////

int get_ecm2cache(uint8_t *buf , struct cache_data *pcache, uint8_t *cw)
{
	pcache->tag = buf[1];
	pcache->sid = (buf[2]<<8) | buf[3];
	pcache->onid = (buf[4]<<8) | buf[5];
	pcache->caid = (buf[6]<<8) | buf[7];
	pcache->hash = (buf[8]<<24) | (buf[9]<<16) | (buf[10]<<8) | (buf[11]);
	pcache->provid = (buf[12]<<16) | (buf[13]<<8) | (buf[14]);
	pcache->cwcycle = buf[15];
	int index = 16;
	//
	memcpy( &pcache->ecm, buf+index, sizeof(void*) );
	index += sizeof(void*);
#ifdef CACHEEX
	memcpy( pcache->ecmd5, buf+index, 16 );
	index+=16;
#endif
	if (cw) {
		memcpy( cw, buf+index, 16 );
		index+=16;
	}
	return index;
}

int put_ecm2cache(uint8_t type, uint8_t *buf , ECM_DATA *ecm, uint16_t onid, uint8_t *cw )
{
	buf[0] = type;
	buf[1] = ecm->ecm[0];
	buf[2] = ecm->sid>>8; buf[3] = ecm->sid;
	buf[4] = onid>>8; buf[5] = onid;
	buf[6] = ecm->caid>>8; buf[7] = ecm->caid;
	buf[8] = ecm->hash>>24; buf[9] = ecm->hash>>16; buf[10] = ecm->hash>>8; buf[11] = ecm->hash;
	buf[12] = ecm->provid>>16; buf[13] = ecm->provid>>8; buf[14] = ecm->provid;
	// cwcycle
	if (!ecm->cw1cycle) buf[15] = NO_CYCLE;
	else if (ecm->ecm[0]==ecm->cw1cycle) buf[15] = CW1CYCLE; else buf[15] = CW0CYCLE;
	//
	int index = 16;
	memcpy( buf+index, &ecm, sizeof(void*) );
	index += sizeof(void*);
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
		ecm->dcwstatus = STAT_DCW_WAIT;
		ecm->checktime = 1;
	}
	else {
		if (!cs->option.cachetimeout) {
			ecm->dcwstatus = STAT_DCW_WAIT;
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
	if ( ecm->lastdecode.ecm && (ecm->lastdecode.counter>1) ) cw = ecm->lastdecode.dcw;
	int len = put_ecm2cache(PIPE_CACHE_FIND, buf, ecm, cs->option.onid, cw);
	pipe_send( prg.pipe.cache[1], buf, len);
}

void pipe_cache_request( ECM_DATA *ecm, struct cardserver_data *cs)
{
	uint8_t buf[64];
	int len = put_ecm2cache(PIPE_CACHE_REQUEST, buf, ecm, cs->option.onid, NULL);
	pipe_send( prg.pipe.cache[1], buf, len);
}

void pipe_cache_reply( ECM_DATA *ecm, struct cardserver_data *cs)
{
	uint8_t buf[64];
	int len;
	if (ecm->dcwstatus==STAT_DCW_SUCCESS)
		len = put_ecm2cache(PIPE_CACHE_REPLY, buf, ecm, cs->option.onid, ecm->cw);
	else
		len = put_ecm2cache(PIPE_CACHE_REPLY, buf, ecm, cs->option.onid, NULL);
	pipe_send( prg.pipe.cache[1], buf, len);
}

void pipe_cache_resendreq(ECM_DATA *ecm, struct cardserver_data *cs)
{
	uint8_t buf[64];
	int len;
	len = put_ecm2cache(PIPE_CACHE_RESENDREQ, buf, ecm, cs->option.onid, NULL);
	pipe_send( prg.pipe.cache[1], buf, len);
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

	if (peer->host->ip && peer->port) {
		struct sockaddr_in si_other;
		int slen=sizeof(si_other);
		memset((char *) &si_other, 0, sizeof(si_other));
		si_other.sin_family = AF_INET;
		si_other.sin_port = htons( peer->port );
		si_other.sin_addr.s_addr = peer->host->ip;
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

int peer_card_binarysearch( struct cachepeer_data *peer, uint16_t caid, uint32_t provid)
{
	/*
	 * TASK 3.17. The comparison is peer_cards_find(). It never reads
	 * cards[nbcards]. On this packed struct that slot is nbcards itself,
	 * so a 1024-card list must not treat the count as a card. Signed
	 * comparison is unchanged.
	 */
	return peer_cards_find(peer->cards, peer->nbcards, caid, provid);
}

int peer_acceptcard( struct cachepeer_data *peer, uint16_t caid, uint32_t provid)
{
	int i;

	/* No callers. A full list has no zero sentinel; a miss is not accept. */
	if ( peer->cards[0] ) {
		int caprov = (caid<<16) | provid;
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
 * TASK R4 (D58) -- one confirmed bad event lands in the ledger. On a
 * threshold crossing the ladder speaks once, with the reason and the
 * consequence, the file is rewritten, and the BAN stage pulls the stock
 * FLAG_DISABLE lever (every later packet is dropped by the existing
 * IS_DISABLED paths). Never called for unknown peers.
 */
static void peerrep_event(struct cachepeer_data *peer, const char *reason)
{
	int st = peerrep_note(peer->host->ip, peer->port, reason);
	if (st>0) {
		static const char *const pr_cons[] = { "", "its keys are no longer requested",
			"its pushes are refused", "the peer is disabled and pinned in the file" };
		/* the file is the source of truth: it is pinned BEFORE the log line
		 * says so (the rig races the log against the file and caught the
		 * old order printing first, renaming later) */
		peerrep_save();
		debugf(getdbgflag(DBG_CACHE,0,peer->id),
			" [PEER REP] peer %s:%u ESCALATED to %s -- last event '%s' -- consequence: %s\n",
			peer->host->name, peer->port,
			st==PEERREP_STAGE_DISTRUST?"DISTRUST":(st==PEERREP_STAGE_ISOLATE?"ISOLATE":"BAN"),
			reason, pr_cons[st]);
		if (st==PEERREP_STAGE_BAN) peer->flags |= FLAG_DISABLE;
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
				if ( peerrep_ask_ok(peer->host->ip, peer->port) ) /* TASK R4 (D58): distrust+ means we stop asking */
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

void save_sms( struct sms_data *sms, struct cachepeer_data *peer)
{
	// print to sms file
	FILE *fhandle;
	fhandle=fopen(sms_file, "at");
	if (fhandle!=0) {
		// Get Time
		char timebuf [80];
		struct tm * timeinfo = localtime (&(time_t){sms->rawtime});
		strftime (timebuf,80,"%x %X",timeinfo);
		if (sms->status&1) fprintf(fhandle,"\n\n[ %s ] >> SMS to peer (%s:%d)\n", timebuf, peer->host->name, peer->port);
		else  fprintf(fhandle,"\n\n[ %s ] << SMS from peer (%s:%d)\n", timebuf, peer->host->name, peer->port);
		fputs(sms->msg, fhandle);
		fclose(fhandle);
	}
}

struct sms_data *cache_new_sms(char *msg)
{
	struct sms_data *sms = malloc( sizeof(struct sms_data) );
	int len = strlen(msg);
	uint32_t hash = hashCode((uint8_t*)msg, len);
	strcpy( sms->msg, msg );
	sms->hash = hash;
	sms->rawtime = time(NULL);
	sms->next = NULL;
	return (sms);
}

void cache_send_sms(struct cachepeer_data *peer, struct sms_data *sms)
{
	int len = strlen(sms->msg);
	sms->status = 1; // bit 0 (0:in,1:out) bit 1 (0:unread/unack, 1:read/ack)
	sms->next = peer->sms;
	peer->sms = sms;
	// Send Buffer
	uint8_t buf[1024];
	buf[0] = TYPE_SMS;
	buf[1] = sms->hash>>24;
	buf[2] = sms->hash>>16;
	buf[3] = sms->hash>>8;
	buf[4] = sms->hash;
	memcpy( buf+5, sms->msg, len );
	sendtopeer(peer, buf, len+5);
	// Debug
	debugf(getdbgflag(DBG_CACHE,0,0)," SMS to peer (%s:%d)\n", peer->host->name, peer->port);
	save_sms( sms, peer);
}


#endif

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
	uint32_t ticks = GetTickCount() - cfg.cache.alivetime;
	while (pcache) {
		if ( (pcache->recvtime+cfg.cache.filtertime)<thereq->recvtime) {
			if ( pcache->recvtime < ticks ) break;
			if ( (pcache->sid==thereq->sid)&&(pcache->hash!=thereq->hash) ) // need provider XXX
			if ( !pcache->provid || !thereq->provid || (pcache->provid==thereq->provid) ) // csp luck of provid
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
int cache_check_cw( uint32_t recvtime, uint8_t tag, uint16_t caid, uint32_t hash, uint16_t sid, uint8_t cw[16], int cwpart )
{
	int index = sid&MAX_CACHE_INDEX;
	struct cache_data **cachetab = getcachetabbycaid(caid);
	/* TASK 3.4: no table for this CAID (allocation failed) -- nothing cached. */
	if (!cachetab) return 0;
	struct cache_data *pcache = cachetab[index];
	uint32_t ticks = GetTickCount() - cfg.cache.alivetime;
	while (pcache) {
		if ( (pcache->recvtime+cfg.cache.filtertime)<recvtime) {
			if ( pcache->recvtime < ticks ) break;
			if ( (pcache->sid==sid)&&(pcache->hash!=hash)&&(pcache->tag!=tag) ) { // ??? maybe find same dcw for different providers
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
							if ( dcwcmp16(cwdata->cw,cw) ) return 0;
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
struct cache_data *cache_fetch_goodcw2( struct cache_data *thereq, uint8_t cw[16], int peerid )
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
	uint32_t ticks = GetTickCount() - cfg.cache.alivetime;
	while (pcache) {
		if ( pcache->recvtime < ticks ) break;
		if ( pcache->flags&CACHE_FLAG_SENDPIPE )
		if ( (pcache->sid==thereq->sid)&&(pcache->hash!=thereq->hash) ) // need provider XXX
		//if ( !pcache->provid || !thereq->provid || (pcache->provid!=thereq->provid) )
		if ( pcache->tag && (pcache->tag==thereq->tag) )
		if ( pcache->cwcycle!=NO_CYCLE )
		if ( !isnullDCW(pcache->prevcw) )
		if ( !iscwincache(pcache,cw) )
		{
			if (  ( (pcache->cwcycle==CW1CYCLE) && dcwcmp8(pcache->prevcw,cw) && !similarcw(pcache->prevcw+8,cw+8) ) ||
				( (pcache->cwcycle==CW0CYCLE) && !similarcw(pcache->prevcw,cw) && dcwcmp8(pcache->prevcw+8,cw+8) )  ) {

				pipe_cache2ecm_find_success(pcache, cw, peerid);

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
		pcache = pcache->next;
		if (pcache==cachetab[index]) break;
	}
	return result;
}

// cache check3 
// update dcw for cache data of same caid,tag,hash with different sid
//[17:36:34.278] <- ecm from cs378x client 'm1' ch 0100:003315:233d:41353b6b
//[17:36:34.278] <- ecm from cs378x client 'm1' ch 0100:003315:2351:41353b6b

struct cache_data *cache_fetch_goodcw3( struct cache_data *thereq, uint8_t cw[16], int peerid )
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
	uint32_t ticks = GetTickCount() - cfg.cache.alivetime;
	while (pcache) {
		if ( pcache->recvtime < ticks ) break;
		if ( pcache->flags&CACHE_FLAG_SENDPIPE )
		if (pcache->sid!=thereq->sid)
		if ( (pcache->hash==thereq->hash) ) // need provider XXX
		if ( pcache->tag && (pcache->tag==thereq->tag) )
		if ( pcache->cwcycle!=NO_CYCLE )
		if ( !isnullDCW(pcache->prevcw) )
		if ( !iscwincache(pcache,cw) )
		{
			if (  ( (pcache->cwcycle==CW1CYCLE) && dcwcmp8(pcache->prevcw,cw) && !similarcw(pcache->prevcw+8,cw+8) ) ||
				( (pcache->cwcycle==CW0CYCLE) && !similarcw(pcache->prevcw,cw) && dcwcmp8(pcache->prevcw+8,cw+8) )  ) {

				pipe_cache2ecm_find_success(pcache, cw, peerid);

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
		pcache = pcache->next;
		if (pcache==cachetab[index]) break;
	}
	return result;
}


// -1: bad cw
// 0: nothing to do
// 1: dcw set
// 2: dcw set & sent to pipe
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
	int piped = 0;   /* TASK R11 (D65): did THIS call pipe to a waiter? */
	{
		struct cache_data *pend = cache_fetch(req);
		struct cardserver_data *pcs = (pend && pend->ecm) ? pend->ecm->cs : NULL;
		int has_waiter = (pend && pend->ecm) ? 1 : 0;
		cex_exchange = cex_is_exchange(peerid);
		int gate_local = cfg.cache.gate_localonly;
		cex_gate_fake  = cfg.cache.gate_blockfake;
		cex_gate_check = cfg.cache.gate_cwcheck;
		if (pcs) {
			if (pcs->option.fexblockfake) cex_gate_fake = 1;
			if (pcs->option.excheck > cex_gate_check) cex_gate_check = pcs->option.excheck;
		}
		/* LOCAL_ONLY: an exchange push with no waiting local ECM never
		 * seeds the cache -- returned BEFORE cache_new(), so nothing is
		 * stored, nothing is served later, and the key cannot pre-poison
		 * a future request. */
		if ( cex_localonly_drop(gate_local, cex_exchange, has_waiter) ) {
#ifdef MCS_DCWSTATS
			DCWSTATS_REJECT(DCW_REJ_CEXLOCAL);
			cwlog_refuse(DCW_REJ_CEXLOCAL, peerid, req->caid, req->provid, req->sid, cw);
#endif
			debugf(getdbgflag(DBG_CACHE,0,0), " [CACHEEX GATE] LOCAL_ONLY: unsolicited push dropped ch %04x:%06x:%04x\n", req->caid, req->provid, req->sid);
			return -1;
		}
		if ( !acceptDCW_profile(cw, pcs) ) {
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
		cwp_note(DCW_SOURCE_CACHE, peerid, cw, req->caid, req->provid, req->sid,
			 GetTickCount());
		cm_note(DCW_SOURCE_CACHE, peerid, cw, req->caid, req->provid, req->sid,
		        GetTickCount());
#ifdef MCS_DCWSTATS
		/* TASK R11 (D65): the specific test (checksum/null/repeat/
		 * badlist) is counted inside dcw.c; the ring row carries the
		 * channel and the peer, which dcw.c cannot see here. */
		cwlog_refuse(CWLOG_R_PROFILE, peerid, req->caid, req->provid, req->sid, cw);
#endif
		return -1;
	}
		/* TASK R1 — BLOCK_FAKE_CW: the two deterministic r82a content
		 * tests, forced for exchange arrivals when the gate is armed.
		 * The rejected key gets the same evidence treatment as a
		 * profile-gate rejection (the TASK 2.4 doctrine: the key most
		 * worth examining is the one about to be discarded). */
		if ( cex_fake_reject(cex_gate_fake, cex_exchange,
		                     checksumDCW(cw) && !isbadDCW(cw)) ) {
#ifdef MCS_DCWSTATS
			DCWSTATS_REJECT(DCW_REJ_CEXFAKE);
			cwlog_refuse(DCW_REJ_CEXFAKE, peerid, req->caid, req->provid, req->sid, cw);
#endif
			{ struct cachepeer_data *pr_peer = getpeerbyid(peerid); /* TASK R4 (D58) */
			  if (pr_peer) peerrep_event(pr_peer, "cacheex-fake-cw"); }
			cwp_note(DCW_SOURCE_CACHE, peerid, cw, req->caid, req->provid, req->sid,
			         GetTickCount());
			cm_note(DCW_SOURCE_CACHE, peerid, cw, req->caid, req->provid, req->sid,
			        GetTickCount());
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
	if ( cwn_gate(cw, req->caid, req->provid, req->sid, GetTickCount()) ) {
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
	if (!pcache) return DCW_ERROR;

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
		if (!cwdata) return DCW_ERROR;
		memset( cwdata, 0, sizeof(struct cw_cache_data) );
		memcpy(cwdata->cw, cw, 16);
		cwdata->status = 0;
		cwdata->cwcycle = NO_CYCLE;
		cwdata->peerid = peerid;
		cwdata->next = pcache->cwdata;
		pcache->cwdata = cwdata;
	}
	cwdata->nbpeers++;

	if (cwdata->status&DCW_ERROR) return DCW_ERROR;

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
		int cw_weight = 1;
		{
			struct cachepeer_data *vc_peer = getpeerbyid(peerid);
			if ( vc_peer && (peerrep_stage(vc_peer->host->ip, vc_peer->port) >= 1) )
				cw_weight = 0; /* already distrusted: the vote counts nothing */
		}
		{
			uint32_t cwin = (uint32_t)cfg.cache.consensus_window;
			const uint8_t *cwl = NULL;
			int cv;
			if (cwin < 100) cwin = 100;
			if (cwin > 2000) cwin = 2000;
			cv = cwconsensus_verdict(req->caid, req->sid, req->hash, req->tag,
			                         cw, peerid, cw_weight, GetTickCount(),
			                         cwin, &cwl);
			if ( cv==CWC_HOLD ) {
#ifdef MCS_DCWSTATS
				cwlog_hold(peerid, req->caid, req->provid, req->sid, cw);
#endif
				debugf(getdbgflag(DBG_CACHE,0,0),
					" [CONSENSUS] holding cw for corroboration (%d distinct so far) ch %04x:%06x:%04x\n",
					1, req->caid, req->provid, req->sid);
				return DCW_ERROR | DCW_SKIP;
			}
			if ( cv==CWC_REFUSE ) {
#ifdef MCS_DCWSTATS
				DCWSTATS_REJECT(DCW_REJ_CONSENSUS);
				cwlog_refuse(DCW_REJ_CONSENSUS, peerid, req->caid, req->provid, req->sid, cw);
#endif
				{
					struct cachepeer_data *rl_peer = getpeerbyid(peerid);
					if (rl_peer) peerrep_event(rl_peer, "consensus-mismatch");
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

	// ACCEPTED
	if (cfg.cache.dcwcheck2) cache_fetch_goodcw2(pcache, cw, peerid);
	if (cfg.cache.dcwcheck3) cache_fetch_goodcw3(pcache, cw, peerid);

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
				if ( (pcache->flags&CACHE_FLAG_SENDPIPE) && !(cwdata->status&DCW_SENT) ) {
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
			if (!(cwdata->status&DCW_CYCLE)) return cwdata->status; //XXX
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
				struct cardserver_data *cyc_cs = pcache->ecm ? pcache->ecm->cs : NULL;
				int cyc_mode = cyc_cs ? cyc_cs->option.dcwfilter_cycle
				                      : (dcw_filter_cycle ? DCWFILTER_ON : DCWFILTER_INHERIT);
				if ( (cyc_mode==DCWFILTER_ON) ||
				     (cyc_mode==DCWFILTER_INHERIT && dcw_filter_cycle) ) {
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
			return cwdata->status;
		}
	}
	else {
		// half nulled cw: exit if non-nds
		if ( (req->caid>>8)!=9 ) {
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

	if ( (pcache->flags&CACHE_FLAG_SENDPIPE) && !(cwdata->status&DCW_SENT) ) {
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
	if ( (received<2)||(received>512) ) return;
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
	switch(buf[0]) {


		case TYPE_REQUEST:
			// Check Peer
			peer = getpeerbyaddr(cache, recv_ip,recv_port);
			if (!peer) { cg_note_unknown( buf[0], recv_ip, recv_port ); break; }	/* TASK 2.9: counted, once said */
			// Check Status
			if (IS_DISABLED(peer->flags)) break;
			memset(&req, 0, sizeof(req));
			// Get DATA
			req.tag = buf[1];
			req.sid = (buf[2]<<8) | buf[3];
			req.onid = (buf[4]<<8) | buf[5];
			req.caid = (buf[6]<<8) | buf[7];
			req.hash = (buf[8]<<24) | (buf[9]<<16) | (buf[10]<<8) |buf[11];
			req.provid = 0;

			pthread_mutex_lock( &prg.lockcache );

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
					if ( pcache->cwdata && !(pcache->flags&CACHE_FLAG_SENDPIPE) ) {
						struct cw_cache_data *cwdata = pcache->cwdata;
						while (cwdata) {
							if (!(cwdata->status&DCW_ERROR)) {
								cache_send_reply( pcache, peer, cwdata->cw);
								if (cfg.cache.forward) peer->sentrep++;
							}
							cwdata = cwdata->next;
						}
					}
				}
			}

			pthread_mutex_unlock( &prg.lockcache );
			break;


		case TYPE_REPLY:
			// Check Peer
			peer = getpeerbyaddr(cache, recv_ip,recv_port);
			if (!peer) { cg_note_unknown( buf[0], recv_ip, recv_port ); break; }	/* TASK 2.9: counted, once said */
			// Check Status
			if (IS_DISABLED(peer->flags)) break;
			/* TASK R4 (D58): an isolated peer's push is dropped unprocessed
			 * -- but the arrival itself still counts: after confirmed
			 * contradictions, choosing to keep pushing is the peer's own
			 * escalation (reason push-while-isolated). Off = never speaks. */
			if ( !peerrep_push_ok(peer->host->ip, peer->port) ) {
				peerrep_event(peer, "push-while-isolated");
				debugf(getdbgflag(DBG_CACHE,0,peer->id),
					" [PEER REP] push from %s:%u dropped unprocessed\n",
					peer->host->name, peer->port);
				break;
			}
			// Check Integrity
			if (buf[12]!=buf[1]) break;
			memset(&req, 0, sizeof(req));
			// SetUp Request
			req.tag = buf[1];
			req.sid = (buf[2]<<8) | buf[3];
			req.onid = (buf[4]<<8) | buf[5];
			req.caid = (buf[6]<<8) | buf[7];
			req.hash = (buf[8]<<24) | (buf[9]<<16) | (buf[10]<<8) |buf[11];
			req.provid = 0;
#ifdef CACHEEX
			cache_test_csp_md5_poison(&req);
#endif

			pthread_mutex_lock( &prg.lockcache );

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
						/*
						 * M6: a CSP TYPE_REPLY has no full ECM MD5. Only use an
						 * identity explicitly populated from a local ECM-pipe message;
						 * a fresh peer-created entry (including an unsolicited push) is
						 * deliberately MD5-invalid and must not vote on reuse.
						 * prg.lockcache is already held for this path.
						 */
						{
							struct cache_data *pc = cache_fetch(&req);
							if (pc && (pc->flags & CACHE_FLAG_ECMD5_VALID)) {
								uint32_t now = GetTickCount();
								if ( cwreuse_offer( &cwreuse_tab, cw, pc->ecmd5, 1,
									                    req.caid, req.sid, req.provid, now ) == CWREUSE_PROOF ) {
									cwreuse_proofs++;
									/* TASK 2.6: the convicted key itself goes into negative
									 * memory, so no other source and no fresh entry can ever
									 * serve it. */
									cwn_note( DCW_SOURCE_CACHE, peer->id|PEER_CSP, cw, req.caid, req.provid, req.sid, now );
									trust_record_lk( &trust_tab, TRUST_EV_PROOF, DCW_SOURCE_CACHE, peer->id|PEER_CSP,
										              req.caid, req.provid, req.sid, now );
									/* TASK 2.8: the same proof, summed one level coarser. */
									trustagg_note_lk( TRUST_EV_PROOF, DCW_SOURCE_CACHE, peer->id|PEER_CSP,
									                  req.caid, now );
									debugf(DBG_ERROR," !!! CW REUSE PROOF: same cw for a different ecm on a different service, ch %04x:%06x:%04x from cache peer%d (%s:%d)\n",
										req.caid, req.provid, req.sid, peer->id, peer->host->name, peer->port);
									if ( cache_purge_mark( &purge_tab, DCW_SOURCE_CACHE, peer->id|PEER_CSP,
										        req.caid, req.sid, req.provid, now ) ) {
										int npurged = cache_purge_sweep( &purge_tab, DCW_SOURCE_CACHE, peer->id|PEER_CSP,
											        req.caid, req.sid, req.provid, DCW_ERROR );
										debugf(DBG_ERROR," !!! CACHE PURGE: ch %04x:%06x:%04x marked %d cached cw(s) from cache peer%d\n",
											req.caid, req.provid, req.sid, npurged, peer->id);
									}
								}
							}
						}
#endif
				}
			}

			pthread_mutex_unlock( &prg.lockcache );
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
			req.tag = buf[5];
			req.sid = (buf[6]<<8) | buf[7];
			req.onid = (buf[8]<<8) | buf[9];
			req.caid = (buf[10]<<8) | buf[11];
			req.hash = (buf[12]<<24) | (buf[13]<<16) | (buf[14]<<8) | buf[15];
			req.provid = 0;
			//
			pthread_mutex_lock( &prg.lockcache );
			// Check Cache Request
			if ( cache_check(&req) ) {
				struct cache_data *pcache = cache_fetch( &req );
				if (pcache!=NULL) {
					buf[4] = TYPE_REPLY;
					buf[16] = buf[5];
					struct cw_cache_data *cwdata = pcache->cwdata;
					while (cwdata) {
						if (!(cwdata->status&DCW_ERROR)) {
							memcpy( buf+17, cwdata->cw, 16);
							sendtopeer( peer, buf+4, 29);
						}
						cwdata = cwdata->next;
					}
				}
			}
			//
			pthread_mutex_unlock( &prg.lockcache );
			break;

		case TYPE_PINGREQ:
			// Check Peer
			peer = cache->peer;
			int port = (buf[11]<<8)|buf[12];
			struct cachepeer_data *peerip = NULL;
			while (peer) {
				if (peer->host->ip==recv_ip) {
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
									if ( (index+buf[index+1]+2)>received ) break;
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
						peer->host->checkiptime = 0;
					}
				}
				else if (cfg.cache.autoadd) {
					// add new peer
					peer = malloc( sizeof(struct cachepeer_data) );
					memset( peer, 0, sizeof(struct cachepeer_data) );
					peer->flags = FLAG_CACHE_SENDREQ | FLAG_CACHE_SENDREP;
					// ADD HOST
					struct host_data *host = add_host( &cfg, ip2string(recv_ip) );
					host->ip = recv_ip;
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
							if ( (index+buf[index+1]+2)>received ) break;
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
					if ( (index+buf[index+1]+2)>received ) break;
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
				if ( (peer->host->ip==recv_ip)&&(peer->id==peerid) ) {
					peer->protocol = 0;
					peer->recvport = recv_port;
					peer->lastpingrecv = GetTickCount();
					if (peer->ping>0)
						peer->ping = (peer->ping+peer->lastpingrecv-peer->lastpingsent)/2;
					else {
						if (!peer_doublecheck(cache,peer)) break;
						debugf(getdbgflag(DBG_CACHE,0,peer->id), " cache: Peer (%s:%d) come Online\n", peer->host->name, peer->port );
						/* TASK R4 (D58): a ban pinned in the reputation file
						 * survives the restart -- re-applied the moment the
						 * peer shows up again, said once. */
						if ( peerrep_stage(peer->host->ip, peer->port) >= PEERREP_STAGE_BAN ) {
							if ( !(peer->flags & FLAG_DISABLE) ) {
								peer->flags |= FLAG_DISABLE;
								debugf(getdbgflag(DBG_CACHE,0,peer->id),
									" [PEER REP] ban restored from the reputation file for %s:%u\n",
									peer->host->name, peer->port);
							}
						}
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
						if ( (peer->host->ip==recv_ip)&&(peer->id==peerid) ) {
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
								/* TASK R4 (D58): the pinned-ban restore, other Online door. */
								if ( peerrep_stage(peer->host->ip, peer->port) >= PEERREP_STAGE_BAN ) {
									if ( !(peer->flags & FLAG_DISABLE) ) {
										peer->flags |= FLAG_DISABLE;
										debugf(getdbgflag(DBG_CACHE,0,peer->id),
											" [PEER REP] ban restored from the reputation file for %s:%u\n",
											peer->host->name, peer->port);
									}
								}
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
									uint32_t caprov = (peer->sharelimits[i].caid<<16)|(peer->sharelimits[i].provid);
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
										uint32_t caprov = (cs->card.caid<<16)|(cs->card.prov[i].id);
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
			if (!peer) { cg_note_unknown( buf[0], recv_ip, recv_port ); break; }	/* TASK 2.9: counted, once said */
			// reset cards
			int idx = 0;
			if (buf[1]&1) memset( peer->cards, 0, sizeof(peer->cards) ); 
			else for (idx=0; idx<1024; idx++) if (!peer->cards[idx]) break;
			//
			int totalcards = (received-2)/4;
			int j=0;
			while ( (j<totalcards)&&(idx<1024) ){
				peer->cards[idx] = (buf[2+j*4]<<24)|(buf[3+j*4]<<16)|(buf[4+j*4]<<8)|(buf[5+j*4]);
				j++; idx++;
			}
			peer->nbcards = idx;
			// Arrange Cards
			int i;
			for (i=0; i<(idx-1); i++)
				for (j=i+1; j<idx; j++)
					if ( peer->cards[i] > peer->cards[j] ) { uint32_t x=peer->cards[i]; peer->cards[i] = peer->cards[j]; peer->cards[j] = x; }
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
			if (received<6) break;
			uint32_t hash = (buf[1]<<24) | (buf[2]<<16) | (buf[3]<<8) | (buf[4]);
			buf[received] = 0;
			//
			peer_check_messages( peer );
			// Create data
			struct sms_data *sms = malloc( sizeof(struct sms_data) );
			stringtohtml( buf+5, sms->msg);
//			strcpy( sms->msg, (char*)buf+5);
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
			// print to sms file
			save_sms( sms, peer);
			break;


		case TYPE_SMS_ACK:
			peer = getpeerbyaddr(cache, recv_ip,recv_port);
			if (!peer) { cg_note_unknown( buf[0], recv_ip, recv_port ); break; }	/* TASK 2.9: counted, once said */
			if (received!=5) break;
			if (!peer->sms) break;
			hash = (buf[1]<<24) | (buf[2]<<16) | (buf[3]<<8) | (buf[4]);
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

	if (!cachepref_enabled) return CACHEPREF_ALLOW;

	/*
	 * pcache->caid/provid/sid are all set before the CW scan runs, in both the
	 * cache_new and the cache_fetch branch of PIPE_CACHE_FIND. GR4: the key is
	 * the full five-part identity, so a peer that is good on one bouquet stays
	 * good on it even if it is poisoning another.
	 */
	score = trust_score_lk(&trust_tab, DCW_SOURCE_CACHE, (int)cwdata->peerid,
	                       pcache->caid, pcache->provid, pcache->sid);
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


void cache_pipe_recvmsg()
{
	uint8_t buf[64];
	uint8_t cw[16];
	struct cache_data req;
	struct cache_data *pcache;

	int len =  pipe_recv( prg.pipe.cache[0], buf );
	if (len<=0) return;

	switch (buf[0]) {

		case PIPE_CACHE_FIND:
			get_ecm2cache(buf , &req, req.prevcw);
			//debugf(0, " Get PIPE_CACHE_FIND: %04x:%06x:%04x:%08x\n", req.caid, req.provid, req.sid, req.hash);

			pcache = cache_fetch( &req );
			if (pcache==NULL) {
				pcache = cache_new( &req );
				/* TASK 3.4: the ECM keeps its own cachetimeout and falls back
				 * to the card servers; the find is simply not served. */
				if (!pcache) break;
				pcache->ecm = req.ecm;
				pcache->cwcycle = req.cwcycle;

#ifdef CACHEEX
				if (len==16+sizeof(void*)+16+16) memcpy( pcache->prevcw, req.prevcw, 16);
#else
				if (len==16+sizeof(void*)+16) memcpy( pcache->prevcw, req.prevcw, 16);
#endif
				/* TASK 3.6: arm only a waiter that is still waiting; a waiter
				 * that died while its FIND sat in the pipe must not leave the
				 * entry armed for its whole alive time. */
				if ( cache_sendpipe_armable(req.ecm) ) pcache->flags |= CACHE_FLAG_SENDPIPE;
				// Send find failed
				pipe_cache2ecm_find_failed(pcache);
			}
			else {
				pcache->ecm = req.ecm;
				pcache->tag = req.tag; // set tag if not set (coming from cahceex)
				pcache->provid = req.provid; // set provid if not set (coming from csp)
				pcache->cwcycle = req.cwcycle;
				pcache->provid = req.provid;
#ifdef CACHEEX
				if (len==16+sizeof(void*)+16+16) memcpy( pcache->prevcw, req.prevcw, 16);
#else
				if (len==16+sizeof(void*)+16) memcpy( pcache->prevcw, req.prevcw, 16);
#endif
				/* TASK 3.6: same arm guard as the new-entry branch. */
				if ( cache_sendpipe_armable(req.ecm) ) pcache->flags |= CACHE_FLAG_SENDPIPE;


				// Check stored cw with status = 0, look for same cycle
				if ( !isnullDCW(pcache->prevcw) ) {

					// look for consecutif cw & same cycle
					struct cw_cache_data *cwdata = pcache->cwdata;
					while (cwdata) {
						if ( !(cwdata->status&DCW_ERROR) && !(cwdata->status&DCW_SENT) )
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
						if ( !(cwdata->status&DCW_ERROR) && !(cwdata->status&DCW_SENT) )
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
						if ( !(cwdata->status&DCW_ERROR) && !(cwdata->status&DCW_SENT) )
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
						if ( !(cwdata->status&DCW_ERROR) && !(cwdata->status&DCW_SENT) )
						if ( !(cwdata->status&DCW_CYCLE) && (cwdata->nbpeers>=5) ) {
							if ( cachepref_gate(pcache, cwdata)==CACHEPREF_ALLOW ) {
								cwdata->status |= DCW_SENT;
								pipe_cache2ecm_find_success(pcache, cwdata->cw, cwdata->peerid );
							}
						}
						cwdata = cwdata->next;
					}

				}

			}
#ifdef CACHEEX
			cache_set_ecmd5(pcache, req.ecmd5);
#endif
			break;


		case PIPE_CACHE_REQUEST:
			get_ecm2cache(buf , &req, NULL);
			//debugf(0, " Get PIPE_CACHE_REQUEST: %04x:%06x:%04x:%08x\n", req.caid, req.provid, req.sid, req.hash);
			pcache = cache_fetch( &req );
			if (pcache==NULL) pcache = cache_new( &req );
			else {
				pcache->tag = req.tag; // set tag if not set (coming from cahceex)
				pcache->provid = req.provid; // set provid if not set (coming from csp)
			}
			/* TASK 3.4: no entry could be made (allocation failed) -- the
			 * request to peers is dropped; the ECM's server flow is unchanged. */
			if (!pcache) break;
			pcache->ecm = req.ecm;
#ifdef CACHEEX
			cache_set_ecmd5(pcache, req.ecmd5);
#endif
			/* TASK 3.6: same arm guard -- the REQUEST rides the pipe too, and
			 * its waiter can be answered or failed before the cache thread gets
			 * here. */
			if ( cache_sendpipe_armable(req.ecm) ) pcache->flags |= CACHE_FLAG_SENDPIPE;
			// Send Request if not dcw sent
			if (!(pcache->flags&CACHE_FLAG_REQSENT)) {
				pcache->flags |= CACHE_FLAG_REQSENT;
				cfg.cache.req++;
				cache_send_request(pcache,NULL);
			}
			break;


		case PIPE_CACHE_REPLY:
			get_ecm2cache(buf , &req, cw);
			//debugf(0, " Get PIPE_CACHE_REPLY: %04x:%06x:%04x:%08x\n", req.caid, req.provid, req.sid, req.hash);

			pcache = cache_fetch( &req );
			if (pcache==NULL) pcache = cache_new( &req );
			else {
				pcache->tag = req.tag; // set tag if not set (coming from cahceex)
				pcache->provid = req.provid; // set provid if not set (coming from csp)
			}
			/* TASK 3.4: the key has nowhere to be remembered (allocation
			 * failed); it is dropped -- its sender's client was already served
			 * and nothing else is waiting on it. */
			if (!pcache) break;
#ifdef CACHEEX
			cache_set_ecmd5(pcache, req.ecmd5);
#endif
			//Check & update DCW
			struct cw_cache_data *cwdata = pcache->cwdata;
			while (cwdata) {
				if ( dcwcmp16(cwdata->cw, cw) ) break;
				cwdata = cwdata->next;
			}
			// ADD if not found
			if (!cwdata) {
				struct cw_cache_data *cwdata = malloc( sizeof(struct cw_cache_data) );
				/* TASK 3.4: the remembered copy is skipped; the reply below
				 * still goes out from the local key. */
				if (cwdata) {
					memset( cwdata, 0, sizeof(struct cw_cache_data) );
					memcpy(cwdata->cw, cw, 16);
					//cwdata->cwcycle = NO_CYCLE;
					//cwdata->peerid = peerid;
					cwdata->next = pcache->cwdata;
					pcache->cwdata = cwdata;
					if (cfg.cache.dcwcheck2) cache_fetch_goodcw2(pcache, cw, 0);
					if (cfg.cache.dcwcheck3) cache_fetch_goodcw3(pcache, cw, 0);
				}
			}
			//cwdata->status |= DCW_CYCLE;

			// Send Reply
			if ( !(pcache->flags&CACHE_FLAG_REPSENT) ) cfg.cache.rep++;
			pcache->flags |= CACHE_FLAG_REPSENT;
			cache_send_reply(pcache, NULL, cw);
			break;

		case PIPE_CACHE_RESENDREQ:
			get_ecm2cache(buf , &req, NULL);
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


void cache_check_peers(struct cacheserver_data *cache)
{
	struct cachepeer_data *peer = cache->peer;
	while (peer) {
		if (!IS_DISABLED(peer->flags))
		if ( (peer->host->ip)&&(peer->port) ) {
			uint32_t ticks = GetTickCount();
			if (peer->ping==0) { // inactive
				if ( (!peer->lastpingsent)||((peer->lastpingsent+9000)<ticks) ) { // send every 15s
					cache_send_ping(cache, peer);
					peer->lastpingsent = ticks;
					peer->lastpingrecv = 0;
					peer->ping = -1;
				}
			}
			else if (peer->ping==-1) { // inactive
				if ( (!peer->lastpingsent)||((peer->lastpingsent+19000)<ticks) ) { // send every 15s
					cache_send_ping(cache, peer);
					peer->lastpingsent = ticks;
					peer->lastpingrecv = 0;
					peer->ping = -2;
				}
			}
			else if (peer->ping==-2) { // inactive
				if ( (!peer->lastpingsent)||((peer->lastpingsent+29000)<ticks) ) { // send every 15s
					cache_send_ping(cache, peer);
					peer->lastpingsent = ticks;
					peer->lastpingrecv = 0;
					peer->ping = -3;
				}
			}
			else if (peer->ping<=-3) { // inactive
				if ( (!peer->lastpingsent)||((peer->lastpingsent+59000)<ticks) ) { // send every 15s
					cache_send_ping(cache, peer);
					peer->lastpingsent = ticks;
					peer->lastpingrecv = 0;
				}
			}
			else if (peer->ping>0) {
				if ( (!peer->lastpingrecv)&&((peer->lastpingsent+9000)<ticks) ) {
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
						peer->host->checkiptime = 0; // maybe ip changed
						//ipeer_update(cache);
					}
				}
				else if ( (peer->lastpingsent+60000)<ticks ) { // send every 75s
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
}


///////////////////////////////////////////////////////////////////////////////
// 
///////////////////////////////////////////////////////////////////////////////

#ifdef EPOLL_CACHE


void *cache_thread(void *param)
{
	prg.pid_cache = syscall(SYS_gettid);
	prg.tid_cache = pthread_self();
	prctl(PR_SET_NAME,"Cache RecvMSG Thread",0,0,0);

	sleep(3);

	int i;
	struct epoll_event evlist[MAX_EPOLL_EVENTS]; // epoll recv events

	uint32_t chkticks = 0;
	while (!prg.restart) {
		// Check Peers Ping
		if ( GetTickCount()>(chkticks+5000) ) {
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
							chw->status |= DCW_SENT;
							pipe_cache2ecm_find_success(chpc, chw->cw, cch.peerid);
							debugf(getdbgflag(DBG_CACHE,0,0),
								" [CONSENSUS] window expired, single-source cw released ch %04x:%06x:%04x\n",
								cch.caid, cch.provid, cch.sid);
						}
						break;
					}
					chw = chw->next;
				}
			}
		}

		int ready = epoll_wait( prg.epoll.cache, evlist, MAX_EPOLL_EVENTS, 1001);
		if (ready == -1) {
			if ( (errno==EINTR)||(errno==EAGAIN) ) {
				usleep(1000);
				continue;
			}
			else {
				usleep(99000);
				debugf(DBG_ERROR,"Err! epoll_wait (%d)", errno);
			}
		}
		else if (ready==0) continue; // timeout

		for (i=0; i < ready; i++) {
			#ifdef CACHE_QUEUE
			if ( evlist[i].events & (EPOLLIN|EPOLLPRI) ) cache_queue_recv(evlist[i].data.ptr);
#else
			if ( evlist[i].events & (EPOLLIN|EPOLLPRI) ) cache_recvmsg(evlist[i].data.ptr);
#endif
		}
	}
	return NULL;
}

#else

void *cache_thread(void *param)
{
	prg.pid_cache = syscall(SYS_gettid);
	prg.tid_cache = pthread_self();
	prctl(PR_SET_NAME,"Cache RecvMSG Thread",0,0,0);

	uint32_t chkticks = 0;
	while (!prg.restart) {
		// Check Peers Ping
		if ( GetTickCount()>(chkticks+3000) ) {
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
							chw->status |= DCW_SENT;
							pipe_cache2ecm_find_success(chpc, chw->cw, cch.peerid);
							debugf(getdbgflag(DBG_CACHE,0,0),
								" [CONSENSUS] window expired, single-source cw released ch %04x:%06x:%04x\n",
								cch.caid, cch.provid, cch.sid);
						}
						break;
					}
					chw = chw->next;
				}
			}
		}

		struct pollfd pfd[100];
		int pfdcount = 0;
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

		int retval = poll(pfd, pfdcount, 3001);

		if ( retval>0 ) {
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
		} else usleep( 99000 );
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
	prg.pid_cache_pipe = syscall(SYS_gettid);
	prg.tid_cache_pipe = pthread_self();
	prctl(PR_SET_NAME,"Cache Pipe Thread",0,0,0);

	while (!prg.restart) {
		struct pollfd pfd;
		pfd.fd = prg.pipe.cache[0];
		pfd.events = POLLIN | POLLPRI;
		int retval = poll(&pfd, 1, 3031);
		if ( retval>0 ) {
			pthread_mutex_lock( &prg.lockcache );
			cache_pipe_recvmsg();
			pthread_mutex_unlock( &prg.lockcache );
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
 * two lines the inline path ran first -- then the hand-off. No locks
 * here, ever: that is the whole point of the queue.
 */
static void cache_queue_recv(struct cacheserver_data *cache)
{
	unsigned char buf[2048];
	struct sockaddr_in si_other;
	socklen_t slen = sizeof(si_other);
	int received = recvfrom( cache->handle, buf, sizeof(buf), 0, (struct sockaddr*)&si_other, &slen);
	if ( (received<2)||(received>512) ) return;
	cq_push(cache, &si_other, buf, received);
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
		if ( GetTickCount()>(lastreported+60000) ) {
			/* time-gated (not pop-gated): one line per minute whenever the
			 * queue did ANY work since startup, so a silent log means an
			 * idle queue and never a stuck one */
			if ( cq_enqueued() || cq_dropped() )
				debugf(getdbgflag(DBG_CACHE,0,0)," [CACHE QUEUE] enqueued %lu dropped %lu\n",
					cq_enqueued(), cq_dropped());
			lastreported = GetTickCount();
		}
		struct cq_entry e;
		if (!cq_pop(&e)) { usleep(10000); continue; }
		cq_current = e;
		cq_active = 1;
		cache_recvmsg((struct cacheserver_data *)e.src);
		cq_active = 0;
	}
	return NULL;
}
#endif /* CACHE_QUEUE */

int start_thread_cache()
{
#ifdef THREAD_CACHE_PIPE
	create_thread(PACKED_MPTR(&prg, pthread_t, tid_cache), (threadfn)cache_pipe_thread,NULL);
#endif

#ifdef CACHE_QUEUE
	cq_init();
	create_thread(PACKED_MPTR(&prg, pthread_t, tid_cache), (threadfn)cache_queue_thread,NULL);
#endif

#ifdef THREAD_CACHE_PIPE
	create_thread(PACKED_MPTR(&prg, pthread_t, tid_cache), (threadfn)cache_pipe_thread,NULL);
#endif

	create_thread(PACKED_MPTR(&prg, pthread_t, tid_cache), (threadfn)cache_thread,NULL);
	return 0;
}
