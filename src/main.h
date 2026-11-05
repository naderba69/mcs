
#include <pthread.h>

struct ledger_delivery_snapshot;
struct ecm_request;
/* ECM mutex wrapper also pins the config graph before taking lockecm. */
void ecm_lock(void);
void ecm_unlock(void);
/* For config reload, which already owns the exclusive config lifetime gate. */
void ecm_lock_config_exclusive(void);
void ecm_unlock_config_exclusive(void);
/* Lifetime-checked server-status update, with lockecm acquired if needed. */
int ecm_setsrvflag_if_lifetime(struct ecm_request *request, unsigned int srvid,
                               int flag, uint64_t expected_lifetime_id);
void cs_disconnect_cli(struct cs_client_data *cli);
void cs_senddcw_cli(struct cs_client_data *cli, const struct ledger_delivery_snapshot *delivery);
void cs_getclimsg();


/* Caller holds prg.locksrv, except config reload, which holds the write
 * lifetime gate, drains connect workers, and owns lockecm exclusively. */
void disconnect_srv(struct server_data *srv);
/* cstat is protected by this leaf mutex; writers avoid locksrv so the
 * lockecm-held no-thread ECM path never acquires locksrv. */
extern pthread_mutex_t srv_cstat_lock;
void srv_cstatadd(struct server_data *srv, int csid, int ok, uint32_t ecmoktime);
void server_set_statmsg(struct server_data *srv, char *message);
int server_connection_is_enabled(struct server_data *srv, int fd);
int server_publish_connection_ready(struct server_data *srv, int fd);
/* CacheEx mode-2 receiver threads retain an ID/generation token, not srv*. */
int server_receive_thread_start(struct server_data *srv);
/* Returns a borrowed cache-peer pointer; caller must hold prg.lockcache. */
struct cachepeer_data *getpeerbyid(int id);
/* HTTP CachePeer message/status operations re-resolve under lockcache. */
int cache_peer_send_sms_by_id(uint32_t peer_id, const char *message,
                              uint32_t *hash_out);
int cache_peer_sms_status_by_id(uint32_t peer_id, uint32_t hash);
/* Drain a deferred peer-reputation file save after releasing lockcache. */
void cache_peerrep_save_after_unlock(void);

// srv-cccam
#ifdef CCCAM_SRV
struct cccam_server_data *getcccamserverbyid(uint32_t id);
struct cc_client_data *getcccamclientbyid(uint32_t id);
struct cc_client_data *getcccamclientbyname(struct cccam_server_data *cccam, char *name);
void cc_disconnect_cli(struct cc_client_data *cli);
#endif

#ifdef MGCAMD_SRV
struct mgcamdserver_data *getmgcamdserverbyid(uint32_t id);
struct mg_client_data *getmgcamdclientbyid(uint32_t id);
struct mg_client_data *getmgcamdclientbyname(struct mgcamdserver_data *mgcamd, char *name);

void mg_disconnect_cli(struct mg_client_data *cli);
#endif

/*
void srv_cstatadd( struct server_data *srv, int csid, int ok); //, int ecmoktime)
void sidata_add(struct server_data *srv, uint8_t *nodeid, uint16_t caid, uint32_t prov, uint16_t sid,int val);
int sidata_update(struct server_data *srv, struct cardserver_data *cs, uint16_t caid, uint32_t prov, uint16_t sid,int val);
struct cs_card_data *srv_check_card( struct server_data *srv, uint16_t caid, uint32_t prov );
*/

struct cardserver_data *getcsbycaidprov( uint16_t caid, uint32_t prov);
struct cardserver_data *getcsbyid(uint32_t id);
struct cardserver_data *getcsbyport(int port);



void wakeup_sendecm();
void cache_pipe_out_shutdown(void);

char *src2string(int srctype, int srcid, char *ret);




////////////////////////////////////////////////////////////////////////////////
// ECM
////////////////////////////////////////////////////////////////////////////////

/* Returns NULL on allocation failure without modifying ECM rings/counters;
 * protocol handlers must send a failure response instead of dereferencing it. */
struct ecm_request *store_ecmdata(struct cardserver_data *cs,uint8_t *ecm,int ecmlen,
                                  unsigned short sid, unsigned short caid, unsigned int provid);
/* Presence-aware variant for protocols where provider may be absent; the flag
 * distinguishes explicit numeric zero from an unknown provider. */
struct ecm_request *store_ecmdata_known(struct cardserver_data *cs,uint8_t *ecm,int ecmlen,
                                        unsigned short sid, unsigned short caid,
                                        unsigned int provid, int provider_known);


