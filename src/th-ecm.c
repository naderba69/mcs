#include "tick_math.h"
#include "cache-pipe-codec.h"
#include "cache-origin.h"
#include "biss.h"  /* BISS key management */
#include "caid-optimizations.h"  /* CAID-specific optimizations */
#include "ecm-provider-config.h"  /* Provider-level ECM configuration */

/* ============================================================================
 * HELPER: Get effective timeout for a specific ECM (CAID:Provider aware)
 * Returns provider-specific timeout if configured, else default
 * NOT static - used by cli-cccam.c which is included before th-ecm.c
 * ============================================================================ */
uint32_t ecm_get_effective_timeout(ECM_DATA *ecm, struct cardserver_data *cs, uint32_t default_timeout)
{
    uint32_t provider_timeout = provider_get_fallback_timeout(ecm->caid, ecm->provid);
    if (provider_timeout > 0) {
        return provider_timeout;
    }
    return default_timeout;
}

/* ============================================================================
 * HELPER: Check if ECM length is whitelisted for this CAID:Provider
 * NOT static - used by cli-cccam.c which is included before th-ecm.c
 * ============================================================================ */
int ecm_is_whitelisted(ECM_DATA *ecm)
{
    if (ecm->ecm[0] == 0) return 1;  // No ECM data, accept
    return provider_ecm_whitelisted(ecm->caid, ecm->provid, ecm->ecm[1]);
}

/* ============================================================================
 * HELPER: Get effective cache time for this CAID:Provider
 * NOT static - used by cli-cccam.c which is included before th-ecm.c
 * ============================================================================ */
uint32_t ecm_get_effective_cache_time(ECM_DATA *ecm, uint32_t default_time)
{
    uint32_t provider_time = provider_get_cw_cache_time(ecm->caid, ecm->provid);
    if (provider_time > 0) {
        return provider_time;
    }
    return default_time;
}

/* R48 D-2 / D111 A1: explicit CACHE TIMEOUT wins over explicit SERVER TIMEOUT;
 * either operator value wins over the CAID preset. With CACHE EVIDENCE OFF,
 * preserve that resolved value unchanged. When armed, it seeds learning and is
 * also the operator ceiling, so learning never erases the explicit choice. */
static uint32_t ecm_cache_wait_timeout(ECM_DATA *ecm,
                                       struct cardserver_data *cs,
                                       uint32_t now_ms)
{
    uint32_t preset = caid_integration_get_cache_timeout(ecm->caid);
    uint32_t cache_timeout = cs->option.cachetimeout > 0 ?
                             (uint32_t)cs->option.cachetimeout : 0;
    uint32_t server_timeout = cs->option.server.timeout;
    uint32_t baseline = r48_cache_window_configured_ms(
        preset, cache_timeout,
        cs->option.cachetimeout_explicit,
        server_timeout,
        cs->option.server.timeout_explicit);
    uint32_t ceiling = baseline;
    if (!cs->option.cache_evidence) return baseline;
    return r48_cache_window_get_timeout(cs->id, ecm->caid, baseline,
                                        ceiling, now_ms, 1);
}

/* Protocol handlers historically scheduled their first worker pass from
 * CACHE TIMEOUT (including an immediate pass for zero). Preserve that behavior
 * when OFF; when ON, allow the learned value to shorten, never lengthen, a
 * positive initial wait. The worker then keeps the same effective deadline. */
uint32_t ecm_get_cache_initial_delay_ms(struct ecm_request *request,
                                        struct cardserver_data *cs,
                                        uint32_t now_ms)
{
    ECM_DATA *ecm = (ECM_DATA *)request;
    uint32_t configured_ms;
    uint32_t learned_ms;
    if (!ecm || !cs) return 0;
    configured_ms = cs->option.cachetimeout > 0 ?
                    (uint32_t)cs->option.cachetimeout : 0;
    if (!cs->option.cache_evidence || !configured_ms)
        return configured_ms;
    learned_ms = ecm_cache_wait_timeout(ecm, cs, now_ms);
    return r48_cache_window_initial_schedule_ms(configured_ms, learned_ms, 1);
}

void clients_check_sendcw(ECM_DATA *ecm, const struct ledger_delivery_snapshot *delivery)
{
	cs_check_sendcw(ecm, delivery);
#ifdef MGCAMD_SRV
	mg_check_sendcw(ecm, delivery);
#endif
#ifdef CCCAM_SRV
	cc_check_sendcw(ecm, delivery);
#endif
#ifdef FREECCCAM_SRV
	freecc_check_sendcw(ecm, delivery);
#endif
#ifdef CS378X_SRV
	cs378x_check_sendcw(ecm, delivery);
#endif
#ifdef CAMD35_SRV
	camd35_check_sendcw(ecm, delivery);
#endif
}

/* Called only with ecm_lock() held. Capture the completed request, then
 * release the ECM pool lock before taking the config read pin or any client
 * state/transport lock. The reload order is config-write -> lockecm, so taking
 * config-read while holding lockecm can deadlock with a waiting reload. The
 * config epoch is changed under both locks; if reload wins the handoff, do not
 * dispatch using the retired profile graph. Return with the caller's original
 * lockecm ownership intact. */
int clients_check_sendcw_from_locked(ECM_DATA *ecm,
                                     struct ledger_delivery_snapshot *delivery)
{
	uint64_t lifetime_id;
	uint64_t config_generation;
	if (!delivery || !ledger_delivery_snapshot_capture_locked(delivery, ecm))
		return -1; /* no config read lock was acquired */
	lifetime_id = delivery->lifetime_id;
	config_generation = cfg.cache.generation;

	/* Acquire locks in the reload's order; the lifetime/epoch checks close the
	 * gap while ECM_DATA remains reusable pool storage. */
	ecm_unlock();
	newcamd_config_read_lock();
	ecm_lock();
	if (cfg.cache.generation != config_generation ||
	    ecm_request_lifetime_id_load(ecm) != lifetime_id) {
		newcamd_config_read_unlock();
		return 0;
	}

	ecm_unlock();
	clients_check_sendcw(ecm, delivery);
	ecm_lock();
	newcamd_config_read_unlock();
	return ecm_request_lifetime_id_load(ecm) == lifetime_id ? 1 : 0;
}


#include "loadbalance.c"

/* R200 v2.5 Integration: SuperSmartFilter + DCW STRICT */
#include "supersmartfilter.h"
#include "dcwstrict.h"
#include "cw-postvalidate.h"
#include "ecm-pattern-analysis.h"
#include "implicit-bs-detect.h"
#include "cache-intelligence.h"
#include "smart-server-select.h"
#include "setdcw.c"

///////////////////////////////////////////////////////////////////////////////
// Check sending ecm to servers
///////////////////////////////////////////////////////////////////////////////

void wakeup_sendecm() // not needed in mono-thread
{
	pipe_wakeup( prg.pipe.ecm[1] );
}

int ecm_faileddcw( ECM_DATA *ecm )
{
	struct ledger_delivery_snapshot failed_delivery;
	int dispatch_result;
	if (ecm->trace_instance) user_trace_shared_event(ecm->trace_instance, USER_TRACE_FAILED, 0, 0, 0, "ECM failed; reason may be unavailable");
	ecm_request_set_dcwstatus(ecm, STAT_DCW_FAILED);
	ecm->checktime = 0;
	ecm->waitserver = 0;
	sid_newecm(ecm);
	/*
	 * TASK 3.6: clear the cache arm before dropping lockecm for transport
	 * delivery. Otherwise a recycled ECM slot could inherit this waiter's arm.
	 */
	cache_clear_sendpipe(ecm);
	dispatch_result = clients_check_sendcw_from_locked(ecm, &failed_delivery);
	if (dispatch_result <= 0) return 0;
#ifdef TESTCHANNEL
	int testchannel = ( (ecm->caid==cfg.testchn.caid) && (ecm->provid==cfg.testchn.provid) && (ecm->sid==cfg.testchn.sid) );
	if (testchannel) {
		fdebugf(" <Decode_Failed> ch %04x:%06x:%04x/%02x:%08x\n", ecm->caid, ecm->provid, ecm->sid, ecm->ecm[0], ecm->hash);
	}
#endif
	return 1;
}

/* Caller holds lockecm. A reputation tie is a reference request, not merely a
 * scheduling hint: preserve the selected candidate through one outstanding or
 * normal line attempt, but cap added latency at the D111 window and never cross
 * the configured decode deadline. */
static uint32_t cache_verify_timeout_ms(ECM_DATA *ecm,
                                        struct cardserver_data *cs)
{
	uint32_t timeout;
	if (!ecm || !cs || ecm->period <= 0) return 0;
	timeout = ecm_get_effective_timeout(ecm, cs, cs->option.dcw.timeout);
	if (timeout && (uint32_t)ecm->period > UINT32_MAX / timeout)
		return UINT32_MAX;
	return timeout * (uint32_t)ecm->period;
}

/* D111 allows at most +50 ms over the stock response path. Leave 10 ms for
 * worker wake-up/queueing jitter, and never extend beyond the decode deadline. */
#define CACHE_VERIFY_MAX_HOLD_MS 40u

static int cache_verify_hold_expired(ECM_DATA *ecm, uint32_t now_ms)
{
	uint32_t timeout_ms;
	if (!ecm || !ecm->cs) return 1;
	timeout_ms = cache_verify_timeout_ms(ecm, ecm->cs);
	if (!timeout_ms || tick32_elapsed(now_ms, ecm->recvtime) >= timeout_ms)
		return 1;
	return tick32_deadline_reached(now_ms, ecm->cache_verify_deadline);
}

static int cache_candidate_schedule_or_handoff(ECM_DATA *ecm,
                                                const uint8_t cw[16],
                                                uint32_t peerid, int cwmark,
                                                uint8_t origin_mask,
                                                int verify_line,
                                                uint32_t now_ms)
{
	struct cardserver_data *cs;
	uint32_t timeout_ms, elapsed_ms, hold_ms;
	int waiting;
	if (!ecm || !cw || !cache_origin_mask_valid(origin_mask) ||
	    !ecm_request_lifetime_id_load(ecm)) return 0;
	cs = ecm->cs;
	if (!cs || !cs->option.fallowcache) return 0;
	if (ecm->cache_verify_pending) return 1; /* Preserve the first selection. */
	waiting = (ecm->dcwstatus == STAT_DCW_WAITCACHE ||
	           ecm->dcwstatus == STAT_DCW_WAIT);
	if (verify_line && cs->option.cache_evidence && !ecm->cache_verify_requested &&
	    waiting) {
		timeout_ms = cache_verify_timeout_ms(ecm, cs);
		elapsed_ms = tick32_elapsed(now_ms, ecm->recvtime);
		ecm->cache_verify_requested = 1; /* one tie gets one reference attempt */
		if (timeout_ms && elapsed_ms < timeout_ms) {
			hold_ms = timeout_ms - elapsed_ms;
			if (hold_ms > CACHE_VERIFY_MAX_HOLD_MS)
				hold_ms = CACHE_VERIFY_MAX_HOLD_MS;
			ecm->cache_verify_pending = 1;
			ecm->cache_verify_deadline = now_ms + hold_ms;
			ecm->cache_verify_dispatched =
			    !!(ecm->server_totalwait || ecm->cache_verify_response_queued);
			ecm->cache_verify_origin_mask = origin_mask;
			ecm->cache_verify_peerid = peerid;
			ecm->cache_verify_cwmark = cwmark;
			memcpy(ecm->cache_verify_cw, cw,
			       sizeof(ecm->cache_verify_cw));
			ecm->checktime = 1;
			ecm->waitserver = 0;
			if (ecm->trace_instance)
				user_trace_shared_event(ecm->trace_instance, USER_TRACE_CACHE,
				    0, 0, elapsed_ms,
				    ecm->cache_verify_dispatched
				        ? "cache reputation tie: waiting for outstanding line reference"
				        : "cache reputation tie: scheduling one line request before fallback");
			return 1;
		}
		ecm->cache_verify_deadline = 0;
		if (ecm->trace_instance)
			user_trace_shared_event(ecm->trace_instance, USER_TRACE_CACHE,
			    0, 0, elapsed_ms,
			    "cache reputation tie: decode deadline reached; immediate cache fallback");
	}
	ecm_setdcw_marked(ecm, (uint8_t *)cw, DCW_SOURCE_CACHE, (int)peerid,
	                  cwmark, origin_mask);
	return 0;
}

void check_ecm(ECM_DATA *ecm, uint32_t ticks)
{
	int cache_verify_dispatch = 0;
	ecm->checktime = 0; // invalid
	ecm->waitserver = 0;
	if (ecm->cache_verify_pending) {
			struct cardserver_data *cs = ecm->cs;
			int deadline_reached = cache_verify_hold_expired(ecm, ticks);
			/* A line frame already queued precedes fallback in the same FIFO.
			 * Otherwise cap verification latency at the short D111 window and
			 * always stay inside the request's decode deadline. */
		if (!cs || (ecm->dcwstatus != STAT_DCW_WAITCACHE &&
		            ecm->dcwstatus != STAT_DCW_WAIT))
			return;
		if (ecm->cache_verify_dispatched) {
			if (!deadline_reached &&
			    (ecm->server_totalwait || ecm->cache_verify_response_queued)) {
					ecm->checktime = ecm->cache_verify_deadline;
				ecm->waitserver = 1;
				return;
			}
			/* check_sendecm performs the single hand-off after this wait state. */
			return;
		}
		cache_verify_dispatch = 1;
		ecm_request_set_dcwstatus(ecm, STAT_DCW_WAIT);
		goto send_ecm;
	}

	// Record channel access for prefetching, cache priority, and warming
	prefetch_record(ecm->caid, ecm->provid, ecm->sid);
	cache_priority_update(ecm->caid, ecm->provid, ecm->sid);
	predictive_warming_record(ecm->caid, ecm->provid, ecm->sid);

	// TASK R26 (D77) - BISS2 KEY CHECK (CAID 2610 only): the 128-bit
	// session word IS the full DCW. Never repeat a half, and never
	// fall through to the BISS1 table for this CAID.
	if (ecm->caid == BISS2_CAID && (!ecm->cs || !ecm->cs->option.disable_softcam)) {
		uint8_t biss2_key[BISS2_KEY_SIZE];
		if (biss2_get(ecm->caid, ecm->sid, ecm->provid, biss2_key)) {
			uint8_t dcw[16];
			memcpy(dcw, biss2_key, BISS2_KEY_SIZE);
			debugf(0, "BISS2: Using stored session word for %04X:%06X:%04X\n", ecm->caid, ecm->provid, ecm->sid);
			cw_prediction_record(ecm->caid, ecm->provid, ecm->sid, dcw);
			ecm_setdcw(ecm, dcw, 99, 0);  // Source 99 = BISS
			/* TASK R27 (D78): do NOT call clients_check_sendcw()
			 * here. ecm_setdcw() hands the key to the Set-DCW
			 * thread (THREAD_DCW), which commits STAT_DCW_SUCCESS
			 * and answers the waiting clients itself; answering
			 * here raced that commit and the client was sent a
			 * decode-failed for a key that arrived moments later
			 * (proven live by tests/md P3). */
			return;
		}
	}

	// BISS KEY CHECK - Try to use stored BISS key first
	if (!ecm->cs || !ecm->cs->option.disable_softcam) {
		uint8_t biss_key[8];
		/* Quick check if BISS key exists before attempting to get it */
		if (biss_has_key(ecm->caid, ecm->sid, ecm->provid) && biss_get(ecm->caid, ecm->sid, ecm->provid, biss_key)) {
			// Found BISS key! Use it directly
			uint8_t dcw[16];
			memcpy(dcw, biss_key, 8);      // First 8 bytes
			memcpy(dcw+8, biss_key, 8);    // Repeat for second 8 bytes

			debugf(0, "BISS: Using stored key for %04X:%06X:%04X\n", ecm->caid, ecm->provid, ecm->sid);

			// Record for CW prediction
			cw_prediction_record(ecm->caid, ecm->provid, ecm->sid, dcw);

			// Set DCW directly
			ecm_setdcw(ecm, dcw, 99, 0);  // Source 99 = BISS
			/* TASK R27 (D78): no clients_check_sendcw() here -- the
			 * Set-DCW thread answers after committing (see BISS2). */
			return;
		}
	}

	// CACHE(fallowcache = 1)
	if (ecm->dcwstatus==STAT_DCW_WAITCACHE) {
		struct cardserver_data *cs = ecm->cs;
		if (!cs) {
			ecm->statusmsg = "Invalid profile id";
			if (!ecm_faileddcw(ecm)) return;
			return;
		}
		if (cs->option.fallowcache) {
			if (!ecm->waitcache) { // Not done
			if (cs->option.fallowcache) {
				/* Predictive warming: check if warming is enabled */
				if (predictive_warming_should_warm()) {
					debugf(0, " [WARMING] Cache warming enabled for %04X:%06X:%04X\n",
					       ecm->caid, ecm->provid, ecm->sid);
				}
				pipe_cache_find(ecm, cs);
				if (ecm->trace_instance) user_trace_shared_event(ecm->trace_instance, USER_TRACE_CACHE,
				    0, 0, ticks-ecm->recvtime, "cache lookup invoked; pipe delivery and result not confirmed");
				ecm->waitcache = 1;
				uint32_t cache_timeout = ecm_cache_wait_timeout(ecm, cs, ticks);
				if (ecm->trace_instance) {
					char detail[128];
					snprintf(detail, sizeof(detail), "cache wait policy: ECM thread timeout=%u ms; profile=%u ms; static=%s; wait may end early",
					         cache_timeout, (unsigned)cs->option.cachetimeout,
					         cs->option.cachestatic ? "YES" : "NO");
					user_trace_shared_event(ecm->trace_instance, USER_TRACE_CACHE, 0, 0,
					                        ticks-ecm->recvtime, detail);
				}
				ecm->checktime = ecm->recvtime + cache_timeout; // wait for cache (CAID-specific)
			}
			else ecm_request_set_dcwstatus(ecm, STAT_DCW_WAIT);
		}
		else {
			uint32_t cache_timeout = ecm_cache_wait_timeout(ecm, cs, ticks);
			if (ecm->trace_instance) {
				char detail[128];
				snprintf(detail, sizeof(detail), "cache wait policy: ECM thread timeout=%u ms; profile=%u ms; static=%s; wait may end early",
				         cache_timeout, (unsigned)cs->option.cachetimeout,
				         cs->option.cachestatic ? "YES" : "NO");
				user_trace_shared_event(ecm->trace_instance, USER_TRACE_CACHE, 0, 0,
				                        ticks-ecm->recvtime, detail);
			}
			if (tick32_elapsed(ticks, ecm->recvtime) >= cache_timeout) ecm_request_set_dcwstatus(ecm, STAT_DCW_WAIT);
			else {
				//debugf(" Wait for cache\n");
				ecm->checktime = ecm->recvtime+cache_timeout;
			}
		}
		}
		else ecm_request_set_dcwstatus(ecm, STAT_DCW_WAIT);
	}

	// SEND ECM
send_ecm:
	if ( (ecm->dcwstatus==STAT_DCW_WAIT) ) {
		// Check Profile
		struct cardserver_data *cs = ecm->cs;
		if (!cs) {
			ecm->statusmsg = "Invalid profile id";
			if (!ecm_faileddcw(ecm)) return;
			return;
		}
		//check for decode failed
		// Check for Max used Servers
		if ( (cs->option.server.max>0) && (ecm->server_totalsent>=cs->option.server.max) ) {
			if (cache_verify_dispatch) return;
			if (!ecm->server_totalwait) {
				ecm->statusmsg = "Decode failed, max servers is reached and no more servers to wait";
				if (ecm->trace_instance) user_trace_shared_event(ecm->trace_instance, USER_TRACE_FAILED, 0, 0, ticks - ecm->recvtime, "maximum servers reached");
				if (!ecm_faileddcw(ecm)) return;
				// Send DCW to Cache if not sent
				if ( cs->option.fallowcache && cs->option.cachesendrep && (ecm->cachestatus!=ECM_CACHE_REP) ) {
					//pipe_send_cache_reply(ecm,cs); // Send failed Cache Reply
					ecm->cachestatus = ECM_CACHE_REP;
				}
			}
			else ecm->checktime = ecm->recvtime + ecm_get_effective_timeout(ecm, cs, cs->option.dcw.timeout)*ecm->period;
		}
		// Check for ECM TimeOut (CAID:Provider-aware)
		else if ( (ticks-ecm->recvtime) >= ecm_get_effective_timeout(ecm, cs, cs->option.dcw.timeout)*ecm->period ) {
			if (cache_verify_dispatch) return;
			ecm->statusmsg = "Decode failed, dcw timeout";
			if (ecm->trace_instance) user_trace_shared_event(ecm->trace_instance, USER_TRACE_FAILED, 0, 0, ticks - ecm->recvtime, "DCW timeout");
			if (!ecm_faileddcw(ecm)) return;
			// Send DCW to Cache if not sent
			if ( cs->option.fallowcache && cs->option.cachesendrep && (ecm->cachestatus!=ECM_CACHE_REP) ) {
				//pipe_send_cache_reply(ecm,cs); // Send failed Cache Reply
				ecm->cachestatus = ECM_CACHE_REP;
			}
		}
		// Check for ECM sending
		else if ( (ticks-ecm->recvtime) <= (ecm_get_effective_timeout(ecm, cs, cs->option.server.timeout)*ecm->period) ) { // CAID:Provider-aware timeout
			// Check for cache request
			if ( (ecm->cachestatus==ECM_CACHE_NONE) && cs->option.fallowcache && cs->option.cachesendreq ) {
				pipe_cache_request(ecm,cs);
				if (ecm->trace_instance) user_trace_shared_event(ecm->trace_instance, USER_TRACE_CACHE,
				    0, 0, ticks-ecm->recvtime, "cache peer request invoked; pipe delivery not confirmed");
				ecm->cachestatus = ECM_CACHE_REQ;
			}
			else if (ecm->trace_instance) {
				char detail[128];
				snprintf(detail, sizeof(detail), "cache peer request skipped: fallow=%u sendreq=%u status=%u",
				         (unsigned)cs->option.fallowcache,
				         (unsigned)cs->option.cachesendreq,
				         (unsigned)ecm->cachestatus);
				user_trace_shared_event(ecm->trace_instance, USER_TRACE_CACHE, 0, 0,
				                        ticks-ecm->recvtime, detail);
			}
			// check for decode failed with no remaining server to wait, send to new cardserver
			if ( !ecm->server_totalwait
				|| (cs->option.server.first>ecm->server_totalsent)
				|| ((ticks-ecm->lastsendtime)>=cs->option.server.interval)
			) {
				//debugf(" check_sendecm[%s] ch %04x:%06x:%04x\n", cs->name,ecm->caid,ecm->provid,ecm->sid);
				struct server_data *newsrv = NULL;
				if ( srvtab_arrange(cs, ecm, ecm->server_totalsent > 0 )==-1 ) { // No more servers to decode
					//debugf(0," sendecm: no servers found to decode\n");
					ecm->checktime = ecm->recvtime + ecm_get_effective_timeout(ecm, cs, cs->option.dcw.timeout)*ecm->period;
					return;
				}
			else if (psrvlist[0] && psrvlist[0]->srv) {
				// SEND ECM
				newsrv = psrvlist[0]->srv;
				newsrv->busycard = psrvlist[0]->card; // dont save pointers!!!
				newsrv->busycardid = psrvlist[0]->shareid;

				/* D111: adaptive cache/source evidence may reorder cache paths,
				 * but never auto-excludes the operator's line-server reference. */
				/* ECM DOUBLE CHECK — Verify CW from multiple sources (activated 2026-10-28)
				 * For certain CAIDs, require CW confirmation from 2+ servers */
				if (!cache_verify_dispatch && ecm_doublecheck_needed(ecm->caid)) {
					ecm_doublecheck_start(ecm->hash, ecm->caid, ecm->provid, ecm->sid);
					debugf(0, " [ECM-DBLCHK] Double check started for %04X:%06X:%04X hash=%08X\n",
						ecm->caid, ecm->provid, ecm->sid, ecm->hash);
				}
					//debugf(0," sendecm: selected server to decode (%s:%d)\n", newsrv->host->name, newsrv->port);
					if (newsrv->type==TYPE_NEWCAMD) {
						if (cs_sendecm_srv(cs, newsrv, ecm)>0) {
							ecm->lastsendtime = ticks;
							debugf(getdbgflagpro(DBG_SERVER,0,newsrv->id,cs->id)," -> ecm to Newcamd server%d (%s:%d) ch %04x:%06x:%04x:%08x\n",(1+ecm->server_totalsent),newsrv->host->name,newsrv->port,ecm->caid,ecm->provid,ecm->sid,ecm->hash);
							newsrv->lastecmtime = ticks;
							newsrv->ecmnb++;
							newsrv->busy=1;
							newsrv->ecm.request = ecm;
							newsrv->ecm.lifetime_id = ecm_request_lifetime_id_load(ecm);
							newsrv->ecm.trace_instance_at_send = ecm->trace_instance;
							newsrv->ecm.hash = ecm->hash;
							newsrv->retry=0;
							ecm_addsrv(ecm, newsrv->id);
							if (ecm->trace_instance) user_trace_shared_event(ecm->trace_instance, USER_TRACE_SENT, newsrv->type, newsrv->id, ticks - ecm->recvtime, "ECM sent to server");
							ecm_addsrvip(ecm, host_ip_snapshot(newsrv->host));
						}
					}
#ifdef CCCAM_CLI
					else if (newsrv->type==TYPE_CCCAM) {
						if (cc_sendecm_srv(newsrv, ecm)>0) {
							ecm->lastsendtime = ticks;
							debugf(getdbgflagpro(DBG_SERVER,0,newsrv->id,cs->id)," -> ecm to CCcam server%d (%s:%d) ch %04x:%06x:%04x:%08x\n",(1+ecm->server_totalsent),newsrv->host->name,newsrv->port,ecm->caid,ecm->provid,ecm->sid,ecm->hash);
							newsrv->lastecmtime = ticks;
							newsrv->ecmnb++;
							struct cs_card_data *card = cc_getcardbyid( newsrv, newsrv->busycardid );
							if (card) card->ecmnb++;
							newsrv->busy=1;
							newsrv->ecm.request = ecm;
							newsrv->ecm.lifetime_id = ecm_request_lifetime_id_load(ecm);
							newsrv->ecm.trace_instance_at_send = ecm->trace_instance;
							newsrv->ecm.hash = ecm->hash;
							newsrv->retry=0;
							ecm_addsrv(ecm, newsrv->id);
							if (ecm->trace_instance) user_trace_shared_event(ecm->trace_instance, USER_TRACE_SENT, newsrv->type, newsrv->id, ticks - ecm->recvtime, "ECM sent to server");
							ecm_addsrvip(ecm, host_ip_snapshot(newsrv->host));
						}
					}
#endif

#ifdef RADEGAST_CLI
					else if (newsrv->type==TYPE_RADEGAST) {
						if (rdgd_sendecm_srv(newsrv, ecm)>0) {
							ecm->lastsendtime = ticks;
							debugf(getdbgflagpro(DBG_SERVER,0,newsrv->id,cs->id)," -> ecm to Radegast server%d (%s:%d) ch %04x:%06x:%04x:%08x\n",(1+ecm->server_totalsent),newsrv->host->name,newsrv->port,ecm->caid,ecm->provid,ecm->sid,ecm->hash);
							newsrv->lastecmtime = ticks;
							newsrv->ecmnb++;
							newsrv->busy=1;
							newsrv->ecm.request = ecm;
							newsrv->ecm.lifetime_id = ecm_request_lifetime_id_load(ecm);
							newsrv->ecm.hash = ecm->hash;
							newsrv->retry=0;
							ecm_addsrv(ecm, newsrv->id);
							if (ecm->trace_instance) user_trace_shared_event(ecm->trace_instance, USER_TRACE_SENT, newsrv->type, newsrv->id, ticks - ecm->recvtime, "ECM sent to server");
							ecm_addsrvip(ecm, host_ip_snapshot(newsrv->host));
						}
					}
#endif
#ifdef CAMD35_CLI
					else if (newsrv->type==TYPE_CAMD35) {
						if (camd35_sendecm_srv(newsrv, ecm)>0) {
							ecm->lastsendtime = ticks;
							debugf(getdbgflagpro(DBG_SERVER,0,newsrv->id,cs->id)," -> ecm to camd35 server%d (%s:%d) ch %04x:%06x:%04x:%08x\n",(1+ecm->server_totalsent),newsrv->host->name,newsrv->port,ecm->caid,ecm->provid,ecm->sid,ecm->hash);
							newsrv->lastecmtime = ticks;
							newsrv->ecmnb++;
							newsrv->busy=1;
							newsrv->ecm.request = ecm;
							newsrv->ecm.lifetime_id = ecm_request_lifetime_id_load(ecm);
							newsrv->ecm.hash = ecm->hash;
							newsrv->retry=0;
							ecm_addsrv(ecm, newsrv->id);
							if (ecm->trace_instance) user_trace_shared_event(ecm->trace_instance, USER_TRACE_SENT, newsrv->type, newsrv->id, ticks - ecm->recvtime, "ECM sent to server");
							ecm_addsrvip(ecm, host_ip_snapshot(newsrv->host));
						}
					}
#endif
#ifdef CS378X_CLI
					else if (newsrv->type==TYPE_CS378X) {
						if (cs378x_sendecm_srv(newsrv, ecm)>0) {
							ecm->lastsendtime = ticks;
							debugf(getdbgflagpro(DBG_SERVER,0,newsrv->id,cs->id)," -> ecm to cs378x server%d (%s:%d) ch %04x:%06x:%04x:%08x\n",(1+ecm->server_totalsent),newsrv->host->name,newsrv->port,ecm->caid,ecm->provid,ecm->sid,ecm->hash);

							newsrv->lastecmtime = ticks;
							newsrv->ecmnb++;
							newsrv->busy=1;
							newsrv->ecm.request = ecm;
							newsrv->ecm.lifetime_id = ecm_request_lifetime_id_load(ecm);
							newsrv->ecm.hash = ecm->hash;
							newsrv->retry=0;
							ecm_addsrv(ecm, newsrv->id);
							if (ecm->trace_instance) user_trace_shared_event(ecm->trace_instance, USER_TRACE_SENT, newsrv->type, newsrv->id, ticks - ecm->recvtime, "ECM sent to server");
							ecm_addsrvip(ecm, host_ip_snapshot(newsrv->host));
						}
					}
#endif
					ecm->statusmsg = "Waiting for servers...";
					if ( cs->option.server.first > (ecm->server_totalsent+1) ) ecm->checktime = ticks + 10;
					else {
						ecm->checktime = ticks + cs->option.server.interval;
						if ( (ecm->checktime-ecm->recvtime) > (ecm_get_effective_timeout(ecm, cs, cs->option.server.timeout)*ecm->period) ) ecm->checktime = ecm->recvtime + ecm_get_effective_timeout(ecm, cs, cs->option.dcw.timeout)*ecm->period;
					}
				}
				else {
					ecm->statusmsg = "Wait for available servers";

					//debugf( getdbgflag(DBG_NEWCAMD,cs->id,0), " check_sendecm[%s] ch %04x:%06x:%04x, Wait for available server...\n", cs->name,ecm->caid,ecm->provid,ecm->sid);

					// PROBLEM HERE ??????
					ecm->waitserver = 1; // XXX
					ecm->checktime = ecm->recvtime + ecm_get_effective_timeout(ecm, cs, cs->option.dcw.timeout)*ecm->period; // CAID:Provider-aware timeout

#ifdef BUSY_SERVER
					if (!ecm->server_totalwait && !ecm->server_totalsent &&
					    !cache_verify_dispatch) {
						ecm->statusmsg = "Decode failed, no free server";
						if (!ecm_faileddcw(ecm)) return;
						cs->ecmbusysrv++;
					}
#endif
					return;
				}
			}
		else {
			ecm->checktime = ecm->lastsendtime + cs->option.server.interval;
			if ( (ecm->checktime-ecm->recvtime) > (ecm_get_effective_timeout(ecm, cs, cs->option.server.timeout)*ecm->period) ) ecm->checktime = ecm->recvtime + ecm_get_effective_timeout(ecm, cs, cs->option.dcw.timeout)*ecm->period;
		}
		}
		else if (!ecm->server_totalwait) {
			if (cache_verify_dispatch) return;
			if (ecm->trace_instance) user_trace_shared_event(ecm->trace_instance, USER_TRACE_FAILED,
			    0, 0, ticks - ecm->recvtime, "server dispatch window expired; no pending server");
			ecm->statusmsg = "Decode failed, no server open this channel";
			if (!ecm_faileddcw(ecm)) return;
			// Send DCW to Cache if not sent
			if ( cs->option.fallowcache && cs->option.cachesendrep && (ecm->cachestatus!=ECM_CACHE_REP) ) {
				//pipe_send_cache_reply(ecm,cs); //Send Failed Cache Reply
				ecm->cachestatus = ECM_CACHE_REP;
			}
		}
		else if ( cs->option.fallowcache && cs->option.cacheresendreq && (ecm->cachestatus==ECM_CACHE_REQ) ) {
			pipe_cache_resendreq(ecm, cs);
			if (ecm->trace_instance) user_trace_shared_event(ecm->trace_instance, USER_TRACE_CACHE,
			    0, 0, ticks-ecm->recvtime, "cache peer retry invoked; pipe delivery not confirmed");
			ecm->cachestatus = ECM_CACHE_REQ2;
			ecm->checktime = ecm->recvtime + ecm_get_effective_timeout(ecm, cs, cs->option.dcw.timeout)*ecm->period;
		}
		else ecm->checktime = ecm->recvtime + ecm_get_effective_timeout(ecm, cs, cs->option.dcw.timeout)*ecm->period;
	}
}

static void cache_verify_handoff_pending(ECM_DATA *ecm, uint32_t now_ms)
{
	struct cardserver_data *cs;
	int deadline_reached;
	if (!ecm || !ecm->cache_verify_pending || !ecm->cache_verify_dispatched) return;
	cs = ecm->cs;
	deadline_reached = cache_verify_hold_expired(ecm, now_ms);
	if (!deadline_reached &&
	    (ecm->server_totalwait || ecm->cache_verify_response_queued)) return;
	if (!cache_origin_mask_valid(ecm->cache_verify_origin_mask)) {
		ecm->cache_verify_pending = 0;
		ecm->cache_verify_dispatched = 0;
		ecm->cache_verify_deadline = 0;
		ecm->checktime = 1;
		ecm->waitserver = 0;
		return;
	}
	if (ecm->dcwstatus == STAT_DCW_FAILED) {
		ecm->cache_verify_pending = 0;
		ecm->cache_verify_dispatched = 0;
		ecm->cache_verify_deadline = 0;
		return;
	}
	/* The line response is either resolved, unavailable, or at the existing
	 * decode deadline. Its Set-DCW frame precedes this fallback in the same
	 * FIFO, so a reply already in flight still wins before the cache candidate. */
	if (ecm->trace_instance)
		user_trace_shared_event(ecm->trace_instance, USER_TRACE_CACHE,
		    0, 0, tick32_elapsed(now_ms, ecm->recvtime),
		    "cache reputation tie: cache candidate queued after line resolution or decode-deadline fallback");
	ecm->cache_verify_pending = 0;
	ecm->cache_verify_dispatched = 0;
	ecm->cache_verify_deadline = 0;
	if (ecm->dcwstatus == STAT_DCW_SUCCESS) {
		/* An unusual earlier success can still use this candidate as evidence;
		 * it must not hold the already-completed request open. */
		ecm->cache_verify_queued = 0;
	} else {
		ecm->cache_verify_queued = 1;
		ecm->checktime = 0;
		ecm->waitserver = 0;
	}
	if (!ecm_setdcw_marked(ecm, ecm->cache_verify_cw, DCW_SOURCE_CACHE,
	                       (int)ecm->cache_verify_peerid,
	                       ecm->cache_verify_cwmark,
	                       ecm->cache_verify_origin_mask)) {
		ecm->cache_verify_queued = 0;
		if (prg.restart) {
			/* A failed required pipe write requests process recovery. Preserve the
			 * arm until restart so an intervening worker pass cannot fail the ECM. */
			ecm->cache_verify_pending = 1;
			ecm->cache_verify_dispatched = 1;
			ecm->cache_verify_deadline = now_ms;
			ecm->checktime = now_ms + 10u;
			ecm->waitserver = 0;
		} else {
			/* A synchronous non-thread validation rejected the candidate. Resume
			 * normal line handling instead of retrying the same invalid CW forever. */
			ecm->cache_verify_pending = 0;
			ecm->cache_verify_dispatched = 0;
			ecm->cache_verify_deadline = 0;
			ecm->checktime = 1;
			ecm->waitserver = 0;
		}
	}
}

/* Shared state transition for the ECM worker after check_ecm has either
 * dispatched/reused a line request or found no usable line. */
static void cache_verify_after_ecm_check(ECM_DATA *ecm, uint32_t now_ms)
{
	if (!ecm || !ecm->cache_verify_pending) return;
	if (!ecm->cache_verify_dispatched)
		ecm->cache_verify_dispatched = 1;
	cache_verify_handoff_pending(ecm, now_ms);
}

static inline void check_sendecm(int newserver)
{
	uint32_t ticks = GetTickCount();
	struct ecm_request *req = ecmdata;
	while (req) {
		if (tick32_elapsed(ticks, req->recvtime) > TIME_ECMALIVE) break;

		if (req->checktime) { // (checktime==0) --> do nothing
			if ( (req->checktime == 1 || tick32_deadline_reached(ticks, req->checktime)) ||
			    (req->waitserver && newserver) ) {
				int cache_verify_pending = req->cache_verify_pending;
				check_ecm(req, ticks);
				if (cache_verify_pending && req->cache_verify_pending) {
					/* One normal worker pass, then wait for the line or enqueue
					 * the cache fallback using the shared state transition. */
					ticks = GetTickCount();
					cache_verify_after_ecm_check(req, ticks);
				}
				ticks = GetTickCount();
			}
		}

		req = req->next;
		if (req==ecmdata) break;
	}
}

/// recalculate ecmcheck wakeup time
uint32_t getecmwakeuptime()
{
	uint32_t ticks = GetTickCount();
	uint32_t waketime = ticks + 10000u;
	struct ecm_request *req = ecmdata;
	while (req) {
		if (tick32_elapsed(ticks, req->recvtime) > TIME_ECMALIVE) break;

		if (req->checktime) {
			if (req->checktime == 1 || tick32_deadline_reached(ticks, req->checktime))
				return ticks;
			if (tick32_deadline_before(req->checktime, waketime))
				waketime = req->checktime;
		}

		req = req->next;
		if (req==ecmdata) break;
	}
	return waketime;
}


///////////////////////////////////////////////////////////////////////////////
// CACHE PIPE RECV MESSAGES
///////////////////////////////////////////////////////////////////////////////
int newserver = 0;

/* Caller holds the config read pin; only normal decode servers are registered
 * with the ECM epoll loop. Cache-exchange servers have their own poll owner. */
static struct server_data *find_main_server_id(uint32_t id)
{
	struct server_data *srv = cfg.server;
	while (srv) {
		if (srv->id == id) return srv;
		srv = srv->next;
	}
	return NULL;
}

static struct server_data *find_main_server_generation(uint64_t generation)
{
	struct server_data *srv = cfg.server;
	if (!generation) return NULL;
	while (srv) {
		if (srv->connect_generation == generation) return srv;
		srv = srv->next;
	}
	return NULL;
}

void recv_ecm_pipe()
{
	uint8_t buf[1024];
	struct cache_data req;
	uint8_t cw[16];
	uint32_t peerid;

	ECM_DATA *ecm;
	struct pollfd pfd;
	int cache2ecm_base_len = 12 + sizeof(void*) + sizeof(uint64_t);

	do {
		int len = pipe_recv( prg.pipe.ecm[0], buf, sizeof(buf));
		if (len>0) {
			switch(buf[0]) {

				case PIPE_WAKEUP:
					if (len != 2) break;
					break;

				case PIPE_LOCK:
					if (len != 2) break;
					pthread_mutex_lock(&prg.lockmain);
					pthread_mutex_unlock(&prg.lockmain);
					break;

				case PIPE_CACHE_FIND_FAILED:
					if (len != cache2ecm_base_len) break;
					if (!get_cache2ecm(buf, (size_t)len, &req, NULL, NULL, NULL, NULL)) break;
					ecm_lock();
					ecm = req.ecm; //search_ecmdata_byhash( req.caid, req.sid, req.hash );
					if (cache_ecm_snapshot_validate(ecm, req.ecm_lifetime_id) &&
					    ecm_request_lifetime_matches(ecm, req.ecm, req.ecm_lifetime_id)) {
						if ( (ecm->caid==req.caid)&&(ecm->hash==req.hash)&&(ecm->sid==req.sid)&&(ecm->dcwstatus==STAT_DCW_WAITCACHE) ) {
							struct cardserver_data *cs = ecm->cs;
							if ( cs && (!cs->option.cachestatic) )
							ecm_request_set_dcwstatus(ecm, STAT_DCW_WAIT);
							ecm->checktime = ecm->recvtime;
						}
					}
					ecm_unlock();
					break;

				case PIPE_CACHE_FIND_SUCCESS:  // SET DCW
					{
						if (len != cache2ecm_base_len + (int)CACHE_PIPE_FIND_SUCCESS_EXTRA_LEN) break;
						int ccymark = NO_CYCLE;   /* TASK 2.5: what the sender declared */
						uint8_t origin_mask = 0, verify_line = 0;
						if (!get_cache2ecm_with_verify(buf, (size_t)len, &req, cw,
						        &ccymark, &peerid, &origin_mask, &verify_line)) break;

						ecm_lock();
						ecm = req.ecm; //search_ecmdata_byhash( req.caid, req.sid, req.hash);
						if (cache_ecm_snapshot_validate(ecm, req.ecm_lifetime_id) &&
						    ecm_request_lifetime_matches(ecm, req.ecm, req.ecm_lifetime_id)) {
							if ((ecm->caid==req.caid)&&(ecm->hash==req.hash)&&(ecm->sid==req.sid)) {
								struct cardserver_data *cs = ecm->cs;
								/*
								 * TASK 3.16: the push can already be in this pipe
								 * when the failure is committed. WAIT and WAITCACHE
								 * are still waiting and must be served. A FAILED
								 * ECM has already told its client decode-failed;
								 * reviving it delivers a key after that answer.
								 * SUCCESS is left to ecm_setdcwdata, which returns.
								 */
								if (ecm->dcwstatus==STAT_DCW_FAILED) {
									debugf(DBG_ERROR, " !!! cache: not reviving a failed ECM ch %04x:%06x:%04x\n", ecm->caid, ecm->provid, ecm->sid);
								}
								else if (cs && cs->option.fallowcache) {
										uint32_t now_ms = GetTickCount();
										cache_candidate_schedule_or_handoff(
										    ecm, cw, peerid, ccymark, origin_mask,
										    verify_line, now_ms);
						}
							}
						}
						ecm_unlock();
					}
					break;

				case PIPE_SRV_CONNECTED:
					{
						uint32_t server_id;
						uint64_t connect_generation;
						if (len == 2) { /* non-epoll wakeup */
							newserver = 1;
							break;
						}
						if (!pipe_decode_server_connected(buf, (size_t)len,
						                                  &server_id,
						                                  &connect_generation)) break;
						newcamd_config_read_lock();
						struct server_data *srv = find_main_server_id(server_id);
						if (srv && srv->connect_generation == connect_generation &&
						    srv->connection.status > 0 && srv->handle > 0) {
#ifdef EPOLL_ECM
							struct epoll_event ev;
							memset(&ev, 0, sizeof(ev));
							ev.events = EPOLLIN;
								ev.data.u64 = connect_generation;
								if (prg.epoll.ecm > 0 &&
							    epoll_ctl(prg.epoll.ecm, EPOLL_CTL_ADD,
							              srv->handle, &ev) == -1 && errno != EEXIST)
								debugf(DBG_ERROR, "Err! EPOLL_CTL_ADD %s (%d)\n",
								       srv->user, srv->handle);
#endif
						}
						newcamd_config_read_unlock();
						newserver = 1;
						break;
					}
				case PIPE_SRV_CSCACHE_REPLY:
					{
						uint32_t server_id, provid;
						uint64_t request_lifetime_id;
						uint16_t caid, sid;
						uint8_t ecm_tag, reply_cw[16];
						if (!pipe_decode_server_cscache_reply(buf, (size_t)len,
						        &server_id, &request_lifetime_id, &caid, &provid,
						        &sid, &ecm_tag, reply_cw)) break;
						newcamd_config_read_lock();
						pthread_mutex_lock(&prg.locksrv);
						struct server_data *srv = find_main_server_id(server_id);
						if (srv && srv->type == TYPE_NEWCAMD && srv->cscached &&
						    srv->busy && srv->connection.status > 0 && srv->handle > 0 &&
						    srv->ecm.lifetime_id == request_lifetime_id) {
							struct cs_custom_data srvcd;
							uint8_t reply_buf[19];
							memset(&srvcd, 0, sizeof(srvcd));
							memset(reply_buf, 0, sizeof(reply_buf));
							srvcd.msgid = srv->ecm.msgid;
							srvcd.caid = caid;
							srvcd.sid = sid;
							srvcd.provid = provid;
							reply_buf[0] = ecm_tag | 0x40; /* C0/C1 cached DCW */
							reply_buf[2] = 0x10;
							memcpy(reply_buf + 3, reply_cw, sizeof(reply_cw));
							if (!cs_message_send(srv->handle, &srvcd, reply_buf,
							                      sizeof(reply_buf), srv->sessionkey))
								disconnect_srv(srv);
						}
						pthread_mutex_unlock(&prg.locksrv);
						newcamd_config_read_unlock();
						break;
					}

				case PIPE_SRV_AVAILABLE:
					if (len != 2) break;
					newserver = 1;
					break;
			}
		}

		pfd.fd = prg.pipe.ecm[0];
		pfd.events = POLLIN | POLLPRI;
	} while ( poll(&pfd, 1, 0)>0 );
}


///////////////////////////////////////////////////////////////////////////////
// RECEIVE MESSAGES THREAD
///////////////////////////////////////////////////////////////////////////////


inline void srv_recvmsg( struct server_data *srv )
{
	if (srv->type==TYPE_NEWCAMD) cs_srv_recvmsg(srv);
#ifdef CCCAM_CLI
	else if (srv->type==TYPE_CCCAM) cc_srv_recvmsg(srv);
#endif
#ifdef RADEGAST_CLI
	else if (srv->type==TYPE_RADEGAST) rdgd_srv_recvmsg(srv);
#endif
#ifdef CAMD35_CLI
	else if (srv->type==TYPE_CAMD35) camd35_srv_recvmsg(srv);
#endif
#ifdef CS378X_CLI
	else if (srv->type==TYPE_CS378X) cs378x_srv_recvmsg(srv);
#endif
}


#ifdef EPOLL_ECM

void *recv_msg_thread(void *param)
{
	__atomic_store_n(&prg.pid_msg, (pid_t)syscall(SYS_gettid), __ATOMIC_RELEASE);
	prg.tid_msg = pthread_self();
	prctl(PR_SET_NAME,"ECM Thread",0,0,0);

	struct epoll_event evlist[MAX_EPOLL_EVENTS]; // epoll recv events
	prg.epoll.ecm = epoll_create(MAX_EPOLL_EVENTS);

	// Add the control pipe. Server sockets are resolved by stable ID after each
	// PIPE_SRV_CONNECTED message; no raw config pointer crosses the pipe.
	struct epoll_event ev;
	memset(&ev, 0, sizeof(ev));
	ev.events = EPOLLIN | EPOLLPRI | EPOLLRDHUP;
	ev.data.u64 = 0;
	if (prg.epoll.ecm < 0 ||
	    epoll_ctl(prg.epoll.ecm, EPOLL_CTL_ADD, prg.pipe.ecm[0], &ev) == -1)
		debugf(DBG_ERROR,"Err! epoll_ctl for ECM pipe (%d)\n", errno);

	while (!prg.restart) {
		uint64_t config_generation;
		newcamd_config_read_lock();
		config_generation = cfg.cache.generation;
		newcamd_config_read_unlock();
		newserver = 0;
		uint32_t mintime = ecm_check_time;
		uint32_t ticks = GetTickCount();
		uint32_t ms;
		{
			int32_t until = tick32_until(mintime, ticks);
			if (until > 3) ms = (uint32_t)until; else ms = 3;
		}

		int ready = epoll_wait(prg.epoll.ecm, evlist, MAX_EPOLL_EVENTS, ms);
		if (ready == -1) {
			if (errno == EINTR || errno == EAGAIN) {
				usleep(1000);
				continue;
			}
			usleep(99000);
			debugf(DBG_ERROR,"Err! epoll_wait (%d)", errno);
		}

		if (ready > 0) {
			/* A reload may finish between epoll_wait() and this read pin. If so,
			 * discard the whole batch; the next iteration rebuilds against the
			 * current graph, and an unread pipe stays level-triggered. */
			newcamd_config_read_lock();
			if (config_generation == cfg.cache.generation) {
				int i;
				for (i = 0; i < ready; i++) {
					uint64_t generation = evlist[i].data.u64;
					if (generation == 0) {
						if (evlist[i].events & (EPOLLIN | EPOLLPRI)) recv_ecm_pipe();
						else if (evlist[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
							debugf(DBG_ERROR,"Err! epoll_wait() pipe\n");
						continue;
					}
					struct server_data *srv = find_main_server_generation(generation);
					if (!srv) continue; // event from a retired connection attempt
					pthread_mutex_lock(&prg.locksrv);
					if (srv->connect_generation == generation &&
					    srv->connection.status > 0 && srv->handle > 0) {
						if (evlist[i].events & (EPOLLIN | EPOLLPRI)) srv_recvmsg(srv);
						else if (evlist[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
							disconnect_srv(srv);
					}
					pthread_mutex_unlock(&prg.locksrv);
				}
			}
			newcamd_config_read_unlock();
		}

		pthread_mutex_lock(&prg.locksrv);
		ecm_lock();
		check_sendecm(newserver);
		ecm_check_time = getecmwakeuptime();
		ecm_unlock();
		pthread_mutex_unlock(&prg.locksrv);
	}
	return NULL;
}

#else

void *recv_msg_thread(void *param)
{
	struct pollfd pfd[MAX_PFD];
	uint64_t pfd_generation[MAX_PFD];
	int pfdcount;

	__atomic_store_n(&prg.pid_msg, (pid_t)syscall(SYS_gettid), __ATOMIC_RELEASE);
	prg.tid_msg = pthread_self();
	prctl(PR_SET_NAME,"ECM Thread",0,0,0);

	while (!prg.restart) {
		uint64_t config_generation;
		uint32_t mintime = ecm_check_time;
		newserver = 0;
		uint32_t ticks = GetTickCount();
		uint32_t ms;
		{
			int32_t until = tick32_until(mintime, ticks);
			if (until > 3) ms = (uint32_t)until; else ms = 3;
		}

		pfdcount = 0;
		newcamd_config_read_lock();
		config_generation = cfg.cache.generation;
		pfd[pfdcount].fd = prg.pipe.ecm[0];
		pfd_generation[pfdcount] = 0;
		pfd[pfdcount++].events = POLLIN | POLLPRI;

		struct server_data *srv = cfg.server;
		while (srv) {
			srv->ipoll = -1;
			if (!IS_DISABLED(srv->flags) && srv->handle > 0 &&
			    pfdcount < SERVER_MAX_PFD && pfdcount < MAX_PFD) {
				srv->ipoll = pfdcount;
				pfd[pfdcount].fd = srv->handle;
				pfd_generation[pfdcount] = srv->connect_generation;
				pfd[pfdcount++].events = POLLIN | POLLPRI;
			}
			srv = srv->next;
		}
		newcamd_config_read_unlock();

		int retval = poll(pfd, pfdcount, ms);
		if (retval > 0) {
			/* Consume control frames regardless of whether the server-list epoch
			 * changed; their stable ID/generation is validated by the consumer. */
			if (pfd[0].revents & (POLLIN | POLLPRI)) recv_ecm_pipe();

			newcamd_config_read_lock();
			if (config_generation == cfg.cache.generation) {
				pthread_mutex_lock(&prg.locksrv);
				srv = cfg.server;
				while (srv) {
					if (!IS_DISABLED(srv->flags) && srv->handle > 0 &&
						    srv->ipoll >= 0 && srv->ipoll < pfdcount &&
						    srv->handle == pfd[srv->ipoll].fd &&
						    srv->connect_generation == pfd_generation[srv->ipoll]) {
						if (pfd[srv->ipoll].revents & (POLLHUP | POLLNVAL))
							disconnect_srv(srv);
						else if (pfd[srv->ipoll].revents & (POLLIN | POLLPRI))
							srv_recvmsg(srv);
					}
					srv = srv->next;
				}
				pthread_mutex_unlock(&prg.locksrv);
			}
			newcamd_config_read_unlock();
		} else if (retval < 0 && errno != EINTR) {
			debugf(0," thread receive messages: poll error %d(errno=%d)\n", retval, errno);
			usleep(91000);
		}

		pthread_mutex_lock(&prg.locksrv);
		ecm_lock();
		check_sendecm(newserver);
		ecm_check_time = getecmwakeuptime();
		ecm_unlock();
		pthread_mutex_unlock(&prg.locksrv);

		newcamd_config_read_lock();
		uint32_t delay = cfg.delay.thread;
		newcamd_config_read_unlock();
		usleep(delay * 2u);
	}
	return NULL;
}

#endif


// Check for keepalive with servers/clients
void *thread_keepalive(void *param)
{
	while (1) {
		sleep(5);
		uint32_t ticks = GetTickCount();


		// Pin the reloadable server graph during keepalive sends and disconnects.
			newcamd_config_read_lock();
			pthread_mutex_lock(&prg.locksrv);
			// Check Servers
			struct server_data *srv = cfg.server;
		while (srv) {
			if ( !IS_DISABLED(srv->flags)&&(srv->connection.status>0) ) {
				if (srv->type==TYPE_NEWCAMD) cs_check_keepalive(srv);

				else if (srv->type==TYPE_CCCAM) {
					if ( !srv->keepalive.status && tick32_elapsed(ticks, srv->keepalive.time) > 75000u ) {
						srv->keepalive.status = 1; // Sent and waiting for reply
						srv->keepalive.time = ticks;
						if ( !cc_msg_send( srv->handle, &srv->sendblock, CC_MSG_KEEPALIVE, 0, NULL) ) disconnect_srv( srv );
					}
				}

#ifdef CAMD35_CLI
				else if (srv->type==TYPE_CAMD35) {
					if ( !srv->keepalive.status && tick32_elapsed(ticks, srv->keepalive.time) > 30000u ) {
						camd35_send_keepalive(srv);
						srv->keepalive.status = 1; // Sent and waiting for reply
						srv->keepalive.time = ticks;
					}
					else if ( (srv->keepalive.status==1) && tick32_elapsed(ticks, srv->keepalive.time) > 10000u ) {
						camd35_send_keepalive(srv);
						srv->keepalive.status = 2;
						srv->keepalive.time = ticks;
					}
					else if ( (srv->keepalive.status>1) && tick32_elapsed(ticks, srv->keepalive.time) > 10000u ) {
						debugf(getdbgflag(DBG_SERVER,0,srv->id)," ??? no keepalive response from camd35 server (%s:%d)\n",srv->host->name,srv->port);
						disconnect_srv( srv );
					}
				}
#endif
			}
			srv = srv->next;
		}


		// Check Cacheex Servers
		srv = cfg.cacheexserver;
		while (srv) {
			if ( !IS_DISABLED(srv->flags)&&(srv->connection.status>0) ) {
				if (srv->type==TYPE_CCCAM) {
					if ( !srv->keepalive.status && tick32_elapsed(ticks, srv->keepalive.time) > 75000u ) {
						srv->keepalive.status = 1; // Sent and waiting for reply
						srv->keepalive.time = ticks;
						if ( !cc_msg_send( srv->handle, &srv->sendblock, CC_MSG_KEEPALIVE, 0, NULL) ) disconnect_srv( srv );
					}
				}

#ifdef CAMD35_CLI
				else if (srv->type==TYPE_CAMD35) {
					if ( !srv->keepalive.status && tick32_elapsed(ticks, srv->keepalive.time) > 30000u ) {
						camd35_send_keepalive(srv);
						srv->keepalive.status = 1; // Sent and waiting for reply
						srv->keepalive.time = ticks;
					}
					else if ( (srv->keepalive.status==1) && tick32_elapsed(ticks, srv->keepalive.time) > 10000u ) {
						camd35_send_keepalive(srv);
						srv->keepalive.status = 2;
						srv->keepalive.time = ticks;
					}
					else if ( (srv->keepalive.status>1) && tick32_elapsed(ticks, srv->keepalive.time) > 10000u ) {
						debugf(getdbgflag(DBG_SERVER,0,srv->id)," ??? no keepalive response from camd35 server (%s:%d)\n",srv->host->name,srv->port);
						disconnect_srv( srv );
					}
				}
#endif
			}
			srv = srv->next;
			}
			pthread_mutex_unlock(&prg.locksrv);
			newcamd_config_read_unlock();


#ifdef CAMD35_SRV
		sleep(1);
		newcamd_config_read_lock();
		// Check camd35 Clients
		ticks = GetTickCount();
		struct camd35_server_data *camd35 = cfg.camd35.server;
		while (camd35) {
			struct camd35_client_data *cli = camd35->client;
			while (cli) {
				if (cli->connection.status>0) {
					if (tick32_elapsed(ticks, cli->lastactivity) > 300000u) {
						camd35_disconnect_cli(cli);
					}
				}
				cli = cli->next;
			}
			camd35 = camd35->next;
		}
		newcamd_config_read_unlock();
#endif

	}
}


int start_thread_recv_msg()
{
	create_thread(PACKED_MPTR(&prg, pthread_t, tid_msg), (threadfn)recv_msg_thread,NULL);
	create_thread(PACKED_MPTR(&prg, pthread_t, tid_msg), (threadfn)thread_keepalive,NULL);
#ifdef THREAD_DCW
	create_thread(PACKED_MPTR(&prg, pthread_t, tid_msg), (threadfn)setdcw_thread,NULL);
#endif
	return 0;
}


