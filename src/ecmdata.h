#define MAX_ECM_DATA 9000
#define TIME_ECMALIVE 15000

/* CWs often reside in packed objects or byte buffers. Compare bytes rather
 * than dereferencing potentially unaligned and aliasing integer pointers. */
#include <string.h>
static inline int dcwcmp8(const void *a, const void *b)
{
    return memcmp(a, b, 8) == 0;
}
static inline int dcwcmp16(const void *a, const void *b)
{
    return memcmp(a, b, 16) == 0;
}

#define ECM_SRV_REQUEST     0
#define ECM_SRV_REPLY_GOOD  1
#define ECM_SRV_REPLY_FAIL  2
#define ECM_SRV_EXCLUDE     3

// cachestatus
#define ECM_CACHE_NONE      0
#define ECM_CACHE_REQ       1
#define ECM_CACHE_REQ2      2
#define ECM_CACHE_REP       4
#define ECM_CACHE_CWC       8

typedef enum
{
	STAT_DCW_FAILED,	// decode failed
	STAT_DCW_WAIT,		// Wait servers answer
	STAT_DCW_WAITCACHE,	// Wait cached servers answer
	STAT_DCW_SUCCESS	// dcw returned 
} dcwstatus_type;


#define DCW_SOURCE_NONE      0
#define DCW_SOURCE_CACHE     1
#define DCW_SOURCE_SERVER    2
#ifdef SRV_CSCACHE
#define DCW_SOURCE_CSCLIENT  3
#define DCW_SOURCE_MGCLIENT  4
#endif
#define DCW_SOURCE_CCCLIENT  6

struct ecm_request {
	struct ecm_request *next;
	struct ecm_request *prev;

	struct ecm_request *csnext;
	struct ecm_request *csprev;

	struct cardserver_data *cs;
	// Ecm Data
	uint32_t recvtime;     // First request time in ms received from client
	uint32_t lastrecvtime; // Last request time received from client
	uint32_t lastsendtime; // Last request Time sent to server
	uint16_t sid;					// Service id
	uint16_t caid;				// CA id
	uint32_t provid;				// Provider value (zero may be absent or explicit)
	uint8_t provider_known;			// Whether that numeric value was present/derivable
	uint16_t chid;				// for irdeto
	int ecmlen;
	uint8_t ecm[MAX_ECM_SIZE];
	uint32_t hash;
	uint64_t lifetime_id __attribute__((aligned(8))); /* Nonzero per allocation/reuse; aligned for atomic cache-side reads. */
	uint64_t trace_instance; /* Assigned lazily while lockecm is held. */
#ifdef CACHEEX
	unsigned char ecmd5[16]; //MD5_DIGEST_LENGTH];
#endif
	// DCW/Status
	dcwstatus_type dcwstatus;
	uint8_t cw[16];
	// DCW SOURCE
	int dcwsrctype;
	int dcwsrcid;
	//int peerid; // Cache PeerID sending dcw(0=nothing)

	int cw1cycle; // 0x80 / 0x81

	unsigned int checktime; // time when recheck the ecm.
	unsigned int waitserver; // wait for available server.

	unsigned char cachestatus;// 0:nothing sent;; 1:request sent;; 2:reply sent
	unsigned char cacheexstatus;// 0:nothing sent;; 1:request sent;; 2:reply sent

	char *statusmsg; // DCW status message

#ifdef CHECK_NEXTDCW
	// Last Successive ECM/DCW
	struct {	
		struct ecm_request *ecm; // NULL: nothing, else found last decode and checked
		uint8_t dcw[16];
		int error;
        int ecmid;
        int counter; // successif dcw counter * -1:error, 0: not found, 1: found and checked 1 time, 2: found and checked 2 times ...
		int cwcycle; // 0: CW0 next to cycle; 1: CW1 next to cycle
        uint32_t dcwchangetime;
	} lastdecode; // maybe we have 3 different channels with same caid:provid:sid ???
#endif

	// Debug ECM tracking
	int debug_request_id;  // Request ID from debug_ecm_record_request()

	// SERVERS that received ecm request
	struct {
		uint32_t srvid; // Server ID 0=nothing
		int flag; // 0=request , 1=reply, 2: excluded(server disconnected, card removed...)
		uint32_t sendtime; // ECM request sent time
		uint32_t statustime; // Last Status Time
		uint8_t dcw[16];
	} server[20]; 
	int server_totalsent;
	int server_totalwait;

	int waitcache; // 1: Wait for Cache; 0: dont wait

	int period; // ==1, number of retries to decode ecm. 

	uint32_t iplist[20]; 	// Clients ip list: to Remove Circular request: check for client ip & srv ip
	uint32_t srviplist[20]; 	// Clients ip list: to Remove Circular request: check for client ip & srv ip

};

typedef struct ecm_request ECM_DATA;

/* A pipe may outlive the request object it points at; pool slots are reused.
 * The pointer identifies storage, while lifetime_id identifies the exact ECM
 * request held in that storage. Zero is reserved for an invalid snapshot. */
static inline uint64_t ecm_request_lifetime_id_load(const ECM_DATA *ecm)
{
	return ecm ? __atomic_load_n(&ecm->lifetime_id, __ATOMIC_ACQUIRE) : 0;
}

/* A small cache-side snapshot registry, protected by lockcache, mirrors only
 * the fields cache workers need. It avoids both a reverse lock order and any
 * unlocked ordinary reads of this ECM object. */
/* Called with lockecm held; takes lockcache in the sanctioned lock order. */
void cache_ecm_snapshot_mark_inactive(ECM_DATA *ecm);
/* Validate serialized cache-pipe pointers by trusted address/generation before
 * any ECM-side dereference; caller may hold lockecm (lockecm -> lockcache). */
int cache_ecm_snapshot_validate(ECM_DATA *ecm, uint64_t lifetime_id);
/* Retire the old generation before an ECM pool slot is cleared/reused. */
void cache_ecm_snapshot_retire(ECM_DATA *ecm);

/* Caller must hold lockecm. Final statuses retire the cache snapshot before
 * publishing the state; WAIT/WAITCACHE keep the lifetime active. */
static inline void ecm_request_set_dcwstatus(ECM_DATA *ecm, dcwstatus_type status)
{
	if (!ecm) return;
	if (status==STAT_DCW_SUCCESS || status==STAT_DCW_FAILED)
		cache_ecm_snapshot_mark_inactive(ecm);
	ecm->dcwstatus = status;
}

static inline int ecm_request_lifetime_matches(const ECM_DATA *actual,
                                               const ECM_DATA *expected,
                                               uint64_t expected_lifetime_id)
{
	return actual && actual == expected && expected_lifetime_id != 0 &&
	       ecm_request_lifetime_id_load(actual) == expected_lifetime_id;
}

extern struct ecm_request *ecmdata;
extern int totalecm;

void init_ecmdata();
uint32_t ecm_crc( uint8_t *ecm, int ecmlen);
unsigned int hashCode( unsigned char *buf, int count);
uint8_t checkECMD5(uint8_t *ecmd5);

void ecm_addip( ECM_DATA *ecm, unsigned int ip);
int ecm_checkip(ECM_DATA *ecm, unsigned int ip);

uint32_t ecm_getprovid( uint8_t *ecm, int ecmlen, uint16_t caid ); /* R25: bounded */
int ecm_getprovid_known( uint8_t *ecm, int ecmlen, uint16_t caid, uint32_t *provid );
uint16_t ecm_getchid( uint8_t *ecm, int ecmlen, uint16_t caid ); /* R25: bounded */

//struct ecm_request *store_ecmdata(struct cardserver_data *cs,uint8_t *ecm,int ecmlen, unsigned short sid, unsigned short caid, unsigned int provid);
struct ecm_request *search_ecmdata_dcw( uint8_t *ecm, int ecmlen, unsigned short sid,
                                        unsigned short caid, uint32_t provid,
                                        int provider_known);
struct ecm_request *search_ecmdata_any(struct cardserver_data *cs, uint8_t *ecm,
                                        int ecmlen, unsigned short sid,
                                        unsigned short caid, uint32_t provid,
                                        int provider_known);
struct ecm_request *search_ecmdata_byhash( uint16_t caid, uint16_t sid,uint32_t hash );
struct ecm_request *search_ecmdata_byecmd5( uint16_t caid, uint32_t provid, uint8_t ecmd5[16] );

int ecm_addsrv(ECM_DATA *ecm, unsigned srvid);
void ecm_addsrvip(ECM_DATA *ecm, unsigned int ip);
int ecm_checksrvip(ECM_DATA *ecm, unsigned int ip);
int ecm_nbsentsrv(ECM_DATA *ecm);
int ecm_nbwaitsrv(ECM_DATA *ecm);
int ecm_setsrvflag(ECM_DATA *ecm, unsigned int srvid, int flag);
int ecm_setsrvflagdcw(ECM_DATA *ecm, unsigned int srvid, int flag, uint8_t dcw[16]);
int ecm_getsrvflag(ECM_DATA *ecm, unsigned int srvid);


int ishalfnulledcw( uint8_t dcw[16] );

int ecmdata_check_cw( uint8_t tag, uint32_t hash, unsigned short caid,
                       unsigned int provid, int provider_known,
                       unsigned short sid, uint8_t cw[16], int cwpart );

#ifdef CHECK_NEXTDCW

void checkfreeze_storeECM(ECM_DATA *ecm);
void checkfreeze_checkECM( ECM_DATA *ecm, ECM_DATA *oldecm );
int checkfreeze_setdcw( ECM_DATA *ecm, uint8_t dcw[16] );

#endif

