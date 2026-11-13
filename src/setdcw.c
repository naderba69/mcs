///////////////////////////////////////////////////////////////////////////////
// SETDCW
///////////////////////////////////////////////////////////////////////////////

#include "common.h"
#include "cache-origin.h"
#include "cafamily.h"	/* TASK R36 (D92) */
#include "ecm-provider-config.h"  /* Provider-level ECM configuration */
#include "cache-window.h"  /* R48 opt-in cache wait-window learning */
#include "cw-fingerprint.h"  /* R48 CW fingerprint (observe-only) */
#include "cw-timing.h"  /* R48 CW timing anomaly (observe-only) */
#include "tick_math.h"
#include <stdlib.h>

/* Capture only accepted line-server responses. The request's saved sendtime is
 * the precise dispatch mark; cache/client sources are never card samples. */
static void r48_cache_window_note_card_response(const ECM_DATA *ecm,
                                                const struct cardserver_data *cs,
                                                int srctype, int srcid)
{
	uint32_t now_ms;
	int i;
	if (!ecm || !cs || !cs->option.cache_evidence ||
	    srctype != DCW_SOURCE_SERVER) return;
	now_ms = GetTickCount();
	for (i = 0; i < 20; i++) {
		if (ecm->server[i].srvid == (uint32_t)srcid &&
		    (ecm->server[i].flag == ECM_SRV_REQUEST ||
		     ecm->server[i].flag == ECM_SRV_REPLY_GOOD)) {
			r48_cache_window_note_card(cs->id, ecm->caid,
			    tick32_elapsed(now_ms, ecm->server[i].sendtime), now_ms);
			return;
		}
	}
}

static inline void peer_hitprofile( struct cachepeer_data *peer, int csid )
{
	int i;
	for(i=0; i<MAX_CSPORTS; i++) {
		if (!peer->csporthit[i].csid) {
			peer->csporthit[i].csid = csid;
			peer->csporthit[i].hits = 1;
			break;
		}
		else if (peer->csporthit[i].csid==csid) {
			peer->csporthit[i].hits++;
			break;
		}
	}
}

/* Peer-table fields share lockcache with the cache worker and HTTP snapshots.
 * Profile aggregate counters remain under lockcacheex; the two locks are
 * acquired sequentially, never nested. */
static void cache_peer_hit_update(struct cardserver_data *cs, uint32_t peer_id,
                                  uint16_t caid, uint32_t provid,
                                  uint16_t sid, uint32_t ecmtime,
                                  int instant, int time_bucket)
{
	struct cachepeer_data *peer;
	int found = 0;
	pthread_mutex_lock(&prg.lockcache);
	peer = getpeerbyid((int)peer_id);
	if (peer) {
		peer->lastcaid = caid;
		peer->lastprov = provid;
		peer->lastsid = sid;
		peer->lastdecodetime = ecmtime;
		peer_hitprofile(peer, cs->id);
		peer->hitnb++;
		cfg.cache.hits++;
		if (instant) {
			peer->ihitnb++;
			cfg.cache.ihits++;
		}
		found = 1;
	}
	pthread_mutex_unlock(&prg.lockcache);

	pthread_mutex_lock(&prg.lockcacheex);
	if (found) {
		cs->hits.csp++;
		if (instant) cs->hits.instant.csp++;
	}
	if (time_bucket < 99) cs->ttimecache[time_bucket]++;
	else cs->ttimecache[99]++;
	pthread_mutex_unlock(&prg.lockcacheex);
}

#ifdef CACHEEX
static inline void cacheex_cccam_hitprofile( struct cc_client_data *cli, int csid )
{
	int i;
	for(i=0; i<MAX_CSPORTS; i++) {
		if (!cli->csporthit[i].csid) {
			cli->csporthit[i].csid = csid;
			cli->csporthit[i].hits = 1;
			break;
		}
		else if (cli->csporthit[i].csid==csid) {
			cli->csporthit[i].hits++;
			break;
		}
	}
}

#ifdef CAMD35_SRV
static inline void cacheex_camd35_hitprofile( struct camd35_client_data *cli, int csid )
{
	int i;
	for(i=0; i<MAX_CSPORTS; i++) {
		if (!cli->csporthit[i].csid) {
			cli->csporthit[i].csid = csid;
			cli->csporthit[i].hits = 1;
			break;
		}
		else if (cli->csporthit[i].csid==csid) {
			cli->csporthit[i].hits++;
			break;
		}
	}
}
#endif

#ifdef CS378X_SRV
static inline void cacheex_cs378x_hitprofile( struct camd35_client_data *cli, int csid )
{
	int i;
	for(i=0; i<MAX_CSPORTS; i++) {
		if (!cli->csporthit[i].csid) {
			cli->csporthit[i].csid = csid;
			cli->csporthit[i].hits = 1;
			break;
		}
		else if (cli->csporthit[i].csid==csid) {
			cli->csporthit[i].hits++;
			break;
		}
	}
}
#endif

static inline void cacheex_server_hitprofile( struct server_data *srv, int csid )
{
	int i;
	for(i=0; i<MAX_CSPORTS; i++) {
		if (!srv->csporthit[i].csid) {
			srv->csporthit[i].csid = csid;
			srv->csporthit[i].hits = 1;
			break;
		}
		else if (srv->csporthit[i].csid==csid) {
			srv->csporthit[i].hits++;
			break;
		}
	}
}

#endif


static inline int dcwcheck_nds( ECM_DATA *ecm, uint8_t dcw[16], int swap )
{
	char nullcw[8] = "\0\0\0\0\0\0\0\0";
	//Must be halfnulled dcw
	//if ( memcmp(dcw,nullcw,3) && memcmp(dcw+8,nullcw,3) ) return 0;
	// get cwcycle
	int cwcycle = 0;
	if ( dcwcmp8(dcw,nullcw) ) cwcycle = 1;
	else if ( dcwcmp8(dcw+8,nullcw) ) cwcycle = 0;
	//
	//if (ecm->cw1cycle==0) return 1;
	if (0x81==ecm->ecm[0]) {
		if (cwcycle==1) return 1;
#ifdef DCWSWAP
		else if (swap) {
			char tmp[8];
			memcpy( tmp, dcw, 8);
			memcpy( dcw, dcw+8, 8);
			memcpy( dcw+8, tmp, 8);
			return 1;
		}
#endif
		else return 0;
	}
	else {
		if (cwcycle==0) return 1;
#ifdef DCWSWAP
		else if (swap) {
			char tmp[8];
			memcpy( tmp, dcw, 8);
			memcpy( dcw, dcw+8, 8);
			memcpy( dcw+8, tmp, 8);
			return 1;
		}
#endif
		else return 0;
	}
	
}


#ifndef THREAD_DCW

static void ecm_setdcw_origin(ECM_DATA *ecm, uint8_t dcw[16], int srctype,
                              int srcid, uint8_t source_origin_mask)
{
	uint8_t cache_reply_buf[80];
	int cache_reply_len = 0;
	struct ledger_delivery_snapshot delivery;
	int dispatch_result;
#ifdef CACHEEX
	uint8_t offered_cw[16];
	if (dcw) memcpy(offered_cw, dcw, sizeof(offered_cw));
	if (ecm && dcw) ledger_note_candidate(ecm, offered_cw, srctype, srcid, GetTickCount());
#endif
	char nullcw[16] = "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0";
	if (!ecm || !dcw || dcwcmp16(dcw,nullcw)) return;

	struct cardserver_data *cs = ecm->cs;
	if (!cs) return;

	if ( dcwcmp8(dcw,nullcw) && dcwcmp8(dcw+8,nullcw) ) return;

	/* ADVANCED LOG: CW received */
	debugf(0, "━━━ [CW-RECV] %04X:%06X:%04X from %s (id=%d) ━━━\n",
		ecm->caid, ecm->provid, ecm->sid,
		(srctype==DCW_SOURCE_CACHE)?"CACHE":"SERVER", srcid);
	debugf(0, "    CW: %02X %02X %02X %02X %02X %02X %02X %02X | %02X %02X %02X %02X %02X %02X %02X %02X\n",
		dcw[0], dcw[1], dcw[2], dcw[3], dcw[4], dcw[5], dcw[6], dcw[7],
		dcw[8], dcw[9], dcw[10], dcw[11], dcw[12], dcw[13], dcw[14], dcw[15]);

	/* Validate the candidate CW, not ECM request bytes. The old direct path
	 * passed ECM data to CW-only helpers and no longer matched their API. Use
	 * the same configured SSF pipeline as THREAD_DCW builds. */
	{
		int ssf_reason = 0;
		uint32_t now_ms = GetTickCount();
		int ssf_rejected = supersmartfilter_check(dcw,
#ifdef CACHEEX
			ecm->ecmd5,
#else
			NULL,
#endif
			ecm->caid, ecm->provid, ecm->sid, (uint32_t)srcid, now_ms, &ssf_reason);
		if (ssf_rejected) {
			char detail[96];
			snprintf(detail, sizeof(detail),
			         "CW rejected by ECM worker: SuperSmartFilter reason code %d", ssf_reason);
			debugf(DBG_ERROR, "    ❌ [SSF-REJECT] %04X:%06X:%04X from src %d -- reason %d\n",
			       ecm->caid, ecm->provid, ecm->sid, srcid, ssf_reason);
			if (ecm->trace_instance)
				user_trace_shared_event(ecm->trace_instance, USER_TRACE_REJECTED,
				                        srctype, srcid, tick32_elapsed(now_ms, ecm->recvtime), detail);
			return;
		}
	}


	/* DCW validation: check for fake patterns and badlist */
//	if (dcw_pattern_fake(dcw)) {
//		debugf(0, " [DCW] Rejected fake pattern CW for %04X:%06X:%04X\n", ecm->caid, ecm->provid, ecm->sid);
//		return;
//	}
//	if (dcw_on_badlist(dcw)) {
//		debugf(0, " [DCW] Rejected badlisted CW for %04X:%06X:%04X\n", ecm->caid, ecm->provid, ecm->sid);
//		return;
//	}

	/*
	 * TASK 1.8 — structural soft signal, evaluated before acceptDCW() so that
	 * a key which is about to be rejected is attributed as well. A rejected key
	 * otherwise leaves no trace anywhere: acceptDCW() returns a bare 0 and the
	 * client only ever sees a timeout.
	 *
	 * This does not change delivery. The scan's only output is evidence, and
	 * acceptDCW() below still makes the same decision it always made.
	 *
	 * The two filter gates are passed in rather than read inside the scan so
	 * that a filter the operator has disabled contributes nothing: if they
	 * turned DCWFILTER CHECKSUM off, their bouquet legitimately fails it and
	 * counting that would penalise a good source on every key.
	 */
	{
		int checksum_filter, repeat_filter;
		dcw_filter_globals_snapshot(&checksum_filter, &repeat_filter, NULL, NULL);
		dcwstruct_note(&trust_tab,
			dcwstruct_scan(dcw, checksum_filter, repeat_filter),
			dcw, srctype, srcid, ecm->caid, ecm->provid, ecm->provider_known, ecm->sid, GetTickCount());
	}

	/* TASK 2.2 -- entropy and collision forensics (unreachable half; see the active copy). */
	cwentropy_note(&trust_tab,
#ifdef CACHEEX
		ecm->ecmd5,
#else
		0,
#endif
		dcw, srctype, srcid, ecm->caid, ecm->provid, ecm->provider_known, ecm->sid, GetTickCount());

	/*
	 * TASK 2.4 -- plausibility: how much of the key's shape survives.
	 *
	 * The third of the three evidence-only hooks, in the same place and for the
	 * same reason as the two above: this runs before acceptDCW() decides
	 * anything, so a key that is about to be discarded is measured too. It
	 * reads no filter gate on purpose -- "exactly one of four group sums is
	 * wrong" means the same thing whether or not the operator left DCWFILTER
	 * CHECKSUM on, which is what makes this a statement about the key rather
	 * than about the configuration.
	 *
	 * Nothing here can reject, delay or alter a delivery: the return value goes
	 * to a counter and, at most, to one soft trust event.
	 */
	cwp_note(srctype, srcid, dcw, ecm->caid, ecm->provid, ecm->provider_known, ecm->sid, GetTickCount());
	/*
	 * TASK 2.7 -- the mirror scan, reading no filter gate on purpose: "half 2
	 * is the complement of half 1" means the same thing whether or not the
	 * operator left the checksum filter on. The repaired variant passes the
	 * checksum layer and is DELIVERED -- this is the only place its story
	 * gets told.
	 */
	cm_note(srctype, srcid, dcw, ecm->caid, ecm->provid, ecm->provider_known, ecm->sid, GetTickCount());



	/*
	 * DCW MINTIME is an operator-configured server-only timing gate.
	 * It measures elapsed time from dispatch to this processing point,
	 * not necessarily network response latency. A fast answer alone does
	 * not prove an invalid CW. Cache and local client sources are exempt.
	 */
	if (srctype == DCW_SOURCE_SERVER && cs->option.dcw.mintime > 0) {
		int k;
		uint32_t now = GetTickCount();
		for (k=0; k<20; k++) {
			if (ecm->server[k].srvid && ecm->server[k].srvid==(uint32_t)srcid) {
				uint32_t elapsed = tick32_elapsed(now, ecm->server[k].sendtime);
				if (tick32_elapsed_less_than(now, ecm->server[k].sendtime,
				                             (uint32_t)cs->option.dcw.mintime)) {
					debugf(DBG_ERROR, " !!! [MINTIME] %s: server %d processing after %ums < %dms, CW skipped\n", cs->name, srcid, (unsigned)elapsed, cs->option.dcw.mintime);
					return;
				}
				break;
			}
		}
	}

	if (!acceptDCW_profile_ecm(dcw, cs, ecm->caid, ecm->provid, ecm->sid)) return;


	/*
	 * TASK BS2 (2026-10-26) -- Fake CW Detection for Known Vulnerable CAIDs
	 *
	 * Some providers (notably CAID 1810 Movistar+, 1883 Canal+, 1884 NLS)
	 * launch anti-sharing attacks by sending fake CWs through EMM or
	 * cache-exchange. These fake CWs often have detectable structural
	 * patterns (complement mirror, repeated bytes, constant step).
	 *
	 * dcw_pattern_fake() detects these shapes with ~1e-12 false positive
	 * rate on genuine keys (verified by TASK R28/D79). Armed here for
	 * the CAIDs most affected by provider fake-CW attacks.
	 *
	 * Source: LinuxSat-Support Thread #151206 (CAID 1810),
	 *         EXTERNAL-RESEARCH-REPORT.md (all three CAIDs).
	 * 1814 (MEO PT): added 2026-11-13 on operator field report of heavy
	 * fake black-screen on this CAID (same attack shape as 1810/1883).
	 */
	if (srctype != DCW_SOURCE_CACHE || (srcid & PEER_ORIGIN_FLAGS)) {
		/* Check non-cache sources and cache-exchange pushes */
		int check_fake = 0;
		switch (ecm->caid) {
			case 0x1810:  /* Movistar+ - known provider attacks */
			case 0x1883:  /* Canal+ France */
			case 0x1884:  /* NLS */
			case 0x1811:  /* CanalSat NG */
			case 0x1814:  /* MEO PT - operator field report 2026-11-13 */
				check_fake = 1;
				break;
		}
		if (check_fake && dcw_pattern_fake(dcw)) {
			debugf(0, " !!! [FAKE-CW] %s: Fake CW detected for %04X:%06X:%04X (shape analysis) -- REFUSED\n",
				cs->name, ecm->caid, ecm->provid, ecm->sid);
#ifdef MCS_DCWSTATS
			DCWSTATS_REJECT(DCW_REJ_PATTERN);
#endif
			return;
		}
	}

	/*
	 * TASK CWPOSTVAL (2026-10-27) -- Post-Acceptance CW Validation Pipeline
	 *
	 * The "key question": can we detect a CW that passes all basic checks
	 * (checksum, null, bad-list) but is still WRONG?
	 *
	 * Answer: YES — using 6 complementary techniques:
	 * 1. Fake CW fingerprint database (auto-learns from detected fakes)
	 * 2. CW Shannon entropy analysis (real CWs ≈3.5, fakes <1.5)
	 * 3. Pattern detection (repeating bytes, duplicate halves)
	 * 4. CW plausibility scoring (0-12 multi-factor)
	 * 5. Peer trust scoring (reputation-based filtering)
	 * 6. Pre-descramble structural validation
	 *
	 * Post-descramble TS validation (cwpostval_confirm_cw) would be called
	 * after the descrambler processes packets — that hook is in the client
	 * delivery path (srv-cccam.c etc.) where we can observe actual TS data.
	 */
	{
		/* ECM Pattern Analysis — detect coordinated attacks FIRST */
		uint32_t now_ms = GetTickCount();
		ecmpat_attack_type_t attack_type = ECMPAT_ATTACK_NONE;
		int pat_result = ecmpat_analyze(
			(uint32_t)srcid, ecm->caid, ecm->provid, ecm->sid,
			dcw, 1 /* is_success=true at this point, will update */,
			0 /* not EMM */, ecm->ecm[0], now_ms, &attack_type);

		if (pat_result == 2) {
			/* Source is quarantined — block ALL CWs */
			debugf(0, "    ❌ [ECM-PATTERN-REJECT] Source %d QUARANTINED -- all CWs blocked\n", srcid);
			cwpostval_trust_bad((uint32_t)srcid);
			ci_record_failure((uint32_t)srcid, ecm->caid, ecm->provid, ecm->sid, CI_CW_KNOWN_FAKE);
			return;
		}
		if (pat_result == 1) {
			debugf(0, "    ❌ [ECM-PATTERN-REJECT] %04X:%06X:%04X from src %d -- ATTACK: %s\n",
				ecm->caid, ecm->provid, ecm->sid, srcid, ecmpat_attack_name(attack_type));
			cwpostval_trust_bad((uint32_t)srcid);
			return;
		}
		debugf(0, "    ✅ [ECM-PATTERN] no attack detected\n");

		/* CACHE-AI: Quick CW Validation (Level 3)
		 * Fast check for null, repeating bytes, low entropy, known fakes */
		{
			ci_cw_status_t cw_status = ci_validate_cw(dcw, ecm->caid, ecm->provid, ecm->sid);
			if (cw_status != CI_CW_VALID) {
				debugf(0, "    ❌ [CACHE-AI-REJECT] %04X:%06X:%04X from src %d -- %s\n",
					ecm->caid, ecm->provid, ecm->sid, srcid, ci_cw_status_name(cw_status));
				ci_record_failure((uint32_t)srcid, ecm->caid, ecm->provid, ecm->sid, cw_status);
				/* Add to known-fake database if it's a pattern */
				if (cw_status == CI_CW_KNOWN_FAKE || cw_status == CI_CW_REPEAT ||
					cw_status == CI_CW_LOW_ENTROPY) {
					ci_add_fake_cw(dcw, ecm->caid, ecm->provid);
				}
				return;
			}
			debugf(0, "    ✅ [CACHE-AI] CW validation OK\n");
		}

		/* CACHE-AI: Check predictive cache first (Level 7) */
		if (ecm->ecm[0]) {
			uint8_t predicted_cw[16];
			uint32_t predict_peer = 0;
			if (ci_predict_fetch(ecm->caid, ecm->provid, ecm->sid,
				ecm->ecm[0], predicted_cw, &predict_peer)) {
				/* Found pre-fetched CW! Use it immediately */
				debugf(0, " [CACHE-AI] PREDICTIVE HIT! %04X:%06X:%04X from pre-fetched CW (peer %d)\n",
					ecm->caid, ecm->provid, ecm->sid, predict_peer);
				memcpy(dcw, predicted_cw, 16);
				/* Continue with this CW through validation pipeline */
			}
		}

		/* CACHE SPEED SUITE: CacheEx Drop Diffs check
		 * If this CW differs from a previously validated good CW for the same
		 * channel, reject it immediately (the earlier CW was confirmed working). */
		{
			uint8_t ecm_hash[16] = {0};
			/* Simple hash from ecm data */
			if (ecm->ecm[0]) {
				ecm_hash[0] = ecm->ecm[0];
				ecm_hash[1] = (ecm->caid >> 8) & 0xFF;
				ecm_hash[2] = ecm->caid & 0xFF;
				ecm_hash[3] = (ecm->sid >> 8) & 0xFF;
				ecm_hash[4] = ecm->sid & 0xFF;
			}
		if (!cacheex_dropdiffs_check(ecm->caid, ecm->provid, ecm->sid, dcw, ecm_hash)) {
			debugf(0, "    ❌ [CACHE-DIFF-REJECT] %04X:%06X:%04X from src %d -- CW differs from known-good\n",
				ecm->caid, ecm->provid, ecm->sid, srcid);
			cwpostval_trust_bad((uint32_t)srcid);
			return;
		}
		debugf(0, "    ✅ [CACHE-DIFF] matches known-good\n");
		}

		/* CW Post-Validation pipeline */
		cwpostval_result_t pvr = cwpostval_validate(
			dcw, ecm->caid, ecm->provid, (uint32_t)srcid,
			checksumDCW(dcw),   /* passes_checksum */
			!isnullDCW(dcw),    /* is_not_null */
			!isbadDCW(dcw),     /* is_not_bad */
			1                   /* in_cycle — assume yes at this stage */
		);

		if (!pvr.accepted) {
			debugf(0, "    ❌ [CW-POSTVAL-REJECT] %04X:%06X:%04X from src %d -- %s\n",
				ecm->caid, ecm->provid, ecm->sid, srcid, pvr.reason);
			cwpostval_trust_bad((uint32_t)srcid);
#ifdef MCS_DCWSTATS
			DCWSTATS_REJECT(DCW_REJ_PATTERN);
#endif
			return;
		}
		debugf(0, "    ✅ [CW-POSTVAL] OK (score=%d/12, entropy=%.1f)\n", 
			pvr.plausibility_score, pvr.entropy, pvr.peer_trust);

		if (pvr.plausibility_score < 10) {
			debugf(DBG_ERROR, " [CW-POSTVAL] %04X:%06X:%04X -- LOW SCORE %d/12 (entropy=%.1f, trust=%d): %s\n",
				ecm->caid, ecm->provid, ecm->sid,
				pvr.plausibility_score, pvr.entropy, pvr.peer_trust,
				pvr.reason);
		}

		/* CACHE SPEED: Store accepted CW as known-good for future drop-diffs check */
		{
			uint8_t ecm_hash[16] = {0};
			ecm_hash[0] = ecm->ecm[0];
			ecm_hash[1] = (ecm->caid >> 8) & 0xFF;
			ecm_hash[2] = ecm->caid & 0xFF;
			ecm_hash[3] = (ecm->sid >> 8) & 0xFF;
			ecm_hash[4] = ecm->sid & 0xFF;
			cacheex_dropdiffs_store_good(ecm->caid, ecm->provid, ecm->sid, dcw, ecm_hash, pvr.peer_trust);
		}

		/* CACHE-AI: Detect CW cycle pattern (Level 6) */
		ci_detect_cycle(ecm->caid, ecm->provid, ecm->sid, ecm->ecm[0], ci_now_ms());
	}

	int cwpart = 2;
	if (ecm->cw1cycle) {
		if (ecm->ecm[0]==ecm->cw1cycle) cwpart = 1; else cwpart = 0;
	}
	else {
		if ( dcwcmp8(dcw,nullcw) ) cwpart = 1;
		else if ( dcwcmp8(dcw+8,nullcw) ) cwpart = 0;
	}

	if (ecm->dcwstatus==STAT_DCW_SUCCESS) {
		return;
	}
	/* TASK 3.16: see the locked copy below. A cache push must not
	 * revive an ECM whose failure was already committed. */
	if (srctype==DCW_SOURCE_CACHE && ecm->dcwstatus==STAT_DCW_FAILED) {
		return;
	}
/*
	if ( !ecmdata_check_cw( ecm->ecm[0], ecm->hash, ecm->caid,
		                        ecm->provid, ecm->provider_known,
		                        ecm->sid, dcw, cwpart) ) {
		return;
	}
*/

	if (srctype!=DCW_SOURCE_CACHE) {
		pthread_mutex_lock( &prg.lockcache );
		int f = cache_check_cw( ecm->recvtime, ecm->ecm[0], ecm->caid, ecm->hash,
		                             ecm->sid, ecm->provid,
		                             ecm->provider_known,
		                             dcw, cwpart);
		pthread_mutex_unlock( &prg.lockcache );
		if (!f) {
			return;
		}
	}

	// filter non-nds halfnulled cw
	if ( dcwcmp8(dcw,nullcw) || dcwcmp8(dcw+8,nullcw) ) {
		if (!ca_halfnull_ok(ecm->caid)) { /* TASK R36 (D92): CAID FAMILY HALFNULLED (stock: 09 only) */
			/* EXTERNAL RESEARCH: Check provider-specific disablecrccws_only_for */
			if (!provider_should_disable_crc(ecm->caid, ecm->provid)) {
				return;
			}
			debugf(0, " [PROVIDER CONFIG] %04X:%06X - CRC bypass enabled (halfnulled CW accepted)\n", ecm->caid, ecm->provid);
		}
		int swap = 0;
#ifdef DCWSWAP
		if (cs)	if (cs->option.dcw.swap) swap = 1;
#endif
		if ( !dcwcheck_nds( ecm, dcw, swap) ) {
			return;
		}
	}

#ifdef CHECK_NEXTDCW
	/* CAID-specific validation: use relaxed check for problematic CAIDs */
	if (caid_should_use_relaxed_validation(ecm->caid)) {
		debugf(0, " [CAID OPT] %04X: using relaxed validation (skipping strict DCW check)\n", ecm->caid);
	}
	else if (cs->option.dcw.check && !cs->option.dcw.halfnulled) {
		int check = checkfreeze_setdcw(ecm,dcw);
		if (check==0) {
			ecm->lastdecode.error++;
			return;
		}
		else if (check==1) {
			ecm->lastdecode.counter = 0;
			ecm->lastdecode.cwcycle = 0;
		}
		else if (check&2) {
			ecm->lastdecode.counter++;
			if (check&4) ecm->lastdecode.cwcycle = '1';
			else ecm->lastdecode.cwcycle = '0';
		}
	}
#endif

#ifdef TESTCHANNEL
	int testchannel = ( (ecm->caid==cfg.testchn.caid) && (ecm->provid==cfg.testchn.provid) && (ecm->sid==cfg.testchn.sid) );
	if (testchannel) {
		char dump[64];
		array2hex( dcw, dump, 16);
		char temp[512];
		snprintf(temp, sizeof(temp), "source=%d/%d", srctype, srcid);
		fdebugf(" =(setdcw)= from %s ch %04x:%06x:%04x/%02x:%08x => %s\n", temp, ecm->caid, ecm->provid, ecm->sid, ecm->ecm[0], ecm->hash, dump);
	}
#endif

	r48_cache_window_note_card_response(ecm, cs, srctype, srcid);
#ifdef CACHEEX
	ledger_note_accepted(ecm, offered_cw, dcw, srctype, srcid, GetTickCount());
#endif
	ecm->statusmsg = "Decode Success";
	int instant = (ecm->dcwstatus==STAT_DCW_WAITCACHE);
	ecm->dcwsrctype = srctype;
	ecm->dcwsrcid = srcid;
	ecm_request_set_dcwstatus(ecm, STAT_DCW_SUCCESS);
	ecm->checktime = 0;
	ecm->waitserver = 0;
	sid_newecm(ecm);
	memcpy( ecm->cw, dcw, 16 );
	cache_reply_len = pipe_cache_reply(ecm, cs, cache_reply_buf,
	                                   sizeof(cache_reply_buf), source_origin_mask);
	/* Keep the non-THREAD_DCW path's cache waiter lifetime bounded too. */
	cache_clear_sendpipe(ecm);

	/* ADVANCED LOG: CW accepted! */
	debugf(0, "    ✅ [CW-ACCEPTED] %04X:%06X:%04X from %s (id=%d) -- ALL CHECKS PASSED\n",
		ecm->caid, ecm->provid, ecm->sid,
		(srctype==DCW_SOURCE_CACHE)?"CACHE":"SERVER", srcid);
	debugf(0, "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");

	/* CW-POSTVAL: Record successful CW for peer trust scoring */
	if (srctype == DCW_SOURCE_SERVER) cwpostval_trust_good((uint32_t)srcid);
	uint32_t ecmtime = tick32_elapsed(GetTickCount(), ecm->recvtime);

	/* CACHE-AI: record operational success/latency counters. */
	/* A locally accepted candidate is not an independent reference agreement.
	 * The ledger is the only producer of positive D-1 reputation; this legacy
	 * intelligence hook may still collect operational success/latency stats. */
	ci_record_success((uint32_t)srcid, ecm->caid, ecm->provid, ecm->sid,
	                  ecmtime, 0);

	/* CACHE-AI: Store CW for predictive cache (Level 7) */
	{
		uint8_t next_tag = ci_predict_next_tag(ecm->caid, ecm->ecm[0]);
		ci_predict_store(ecm->caid, ecm->provid, ecm->sid,
			next_tag, dcw, (uint32_t)srcid, 10000); /* Valid for 10s */
	}

	// Check timeout
	if ( ecmtime > (uint32_t)cs->option.dcw.timeout * (uint32_t)ecm->period ) return;
	// Release lockecm before taking client/config locks or writing sockets.
	// Revalidate the exact pool generation after returning with lockecm held.
	dispatch_result = clients_check_sendcw_from_locked(ecm, &delivery);
	if (dispatch_result <= 0) return;

	/* IBS: Record CW delivery for black screen detection */
	ibs_on_cw_sent(0, ecm->caid, ecm->provid, ecm->sid, (uint32_t)srcid, dcw, GetTickCount());

	// Update Stat
	cs->ecmok++;
	cs->ecmoktime += ecmtime;
	int time = (ecmtime+50)/100;
	if (time<99) cs->ttime[time]++; else cs->ttime[99]++;

	if (srctype==DCW_SOURCE_CACHE) {
		if (srcid&PEER_CSP) {
			cache_peer_hit_update(cs, (uint32_t)(srcid&0xffff),
			                     ecm->caid, ecm->provid, ecm->sid, ecmtime,
			                     instant, time);
		}
#ifdef CACHEEX
		else {
			pthread_mutex_lock(&prg.lockcacheex);
		if (srcid&PEER_CCCAM_CLIENT) { // Cacheex
			struct cc_client_data *cli = getcecccamclientbyid(srcid&0xffff);
			if (cli) {
				// setup client last used cache
				cli->cacheex.lastcaid = ecm->caid;
				cli->cacheex.lastprov = ecm->provid;
				cli->cacheex.lastsid = ecm->sid;
				cli->cacheex.lastdecodetime = ecmtime;
				// add to profiles hits
				cacheex_cccam_hitprofile( cli, cs->id );
				cli->cacheex.hits++;
				cs->hits.cacheex++;
				cfg.cacheex.hits++;
				if (instant) {
					cfg.cacheex.ihits++;
					cs->hits.instant.cacheex++;
					cli->cacheex.ihits++;
				}
			}
			if (time<99) cs->ttimecacheex[time]++; else cs->ttimecacheex[99]++;
		}

#ifdef CAMD35_SRV
		//PEERID_CAMD35
		else if (srcid&PEER_CAMD35_CLIENT) {
			struct camd35_client_data *cli = getcamd35clientbyid(srcid&0xffff);
			if (cli) {
				// setup client last used cache
				cli->cacheex.lastcaid = ecm->caid;
				cli->cacheex.lastprov = ecm->provid;
				cli->cacheex.lastsid = ecm->sid;
				cli->cacheex.lastdecodetime = ecmtime;
				// add to profiles hits
				cacheex_camd35_hitprofile( cli, cs->id );
				cli->cacheex.hits++;
				cs->hits.cacheex++;
				cfg.cacheex.hits++;
				if (instant) {
					cfg.cacheex.ihits++;
					cs->hits.instant.cacheex++;
					cli->cacheex.ihits++;
				}
			}
			if (time<99) cs->ttimecacheex[time]++; else cs->ttimecacheex[99]++;
		}
#endif

#ifdef CS378X_SRV
		//PEERID_CS378X
		else if (srcid&PEER_CS378X_CLIENT) {
			struct camd35_client_data *cli = getcs378xclientbyid(srcid&0xffff);
			if (cli) {
				// setup client last used cache
				cli->cacheex.lastcaid = ecm->caid;
				cli->cacheex.lastprov = ecm->provid;
				cli->cacheex.lastsid = ecm->sid;
				cli->cacheex.lastdecodetime = ecmtime;
				// add to profiles hits
				cacheex_cs378x_hitprofile( cli, cs->id );
				cli->cacheex.hits++;
				cs->hits.cacheex++;
				cfg.cacheex.hits++;
				if (instant) {
					cfg.cacheex.ihits++;
					cs->hits.instant.cacheex++;
					cli->cacheex.ihits++;
				}
			}
			if (time<99) cs->ttimecacheex[time]++; else cs->ttimecacheex[99]++;
		}
#endif

		else if (srcid&PEER_CACHEEX_SERVER) {
			struct server_data *srv = getcesrvbyid( srcid&0xffff );
			if (srv) {
				// setup client last used cache
				srv->cacheex.lastcaid = ecm->caid;
				srv->cacheex.lastprov = ecm->provid;
				srv->cacheex.lastsid = ecm->sid;
				srv->cacheex.lastdecodetime = ecmtime;
				// add to profiles hits
				cacheex_server_hitprofile( srv, cs->id );
				srv->cacheex.hits++;
				cs->hits.cacheex++;
				cfg.cacheex.hits++;
				if (instant) {
					cfg.cacheex.ihits++;
					cs->hits.instant.cacheex++;
					srv->cacheex.ihits++;
				}
			}
			if (time<99) cs->ttimecacheex[time]++; else cs->ttimecacheex[99]++;
		}
			pthread_mutex_unlock(&prg.lockcacheex);
		}
#endif
	}

	else if (srctype==DCW_SOURCE_SERVER) {
		struct server_data *srv = getsrvbyid( srcid&0xffff );
		if (srv) srv->hits++;
		if (time<99) cs->ttimecards[time]++; else cs->ttimecards[99]++;
	}
#ifdef SRV_CSCACHE
	else if (srctype==DCW_SOURCE_CSCLIENT) {
		/* cachedcw is updated by cs_check_sendcw() under the Newcamd
		 * config/client locks, after lockecm has been released. */
		if (time<99) cs->ttimeclients[time]++; else cs->ttimeclients[99]++;
	}
	else if (srctype==DCW_SOURCE_MGCLIENT) {
		struct mg_client_data *cli = getmgcamdclientbyid( srcid&0xffff );
		if (cli) cli->cachedcw++;
		if (time<99) cs->ttimeclients[time]++; else cs->ttimeclients[99]++;
	}
#endif

#ifdef CACHEEX
	// Serialize from the committed snapshot, with no ECM lock held across
	// pipe_send()'s bounded back-pressure wait.
	if (cs->option.fallowcacheex && ecmtime < cs->option.cacheexvalidtime) {
		/* Follow reload's config-read -> lockecm order: never wait for the
		 * read side while holding lockecm. Revalidate after the hand-off because
		 * the ECM slot and profile graph may both have changed in the gap. */
		uint64_t config_generation = cfg.cache.generation;
		ecm_unlock();
		newcamd_config_read_lock();
		ecm_lock();
		if (cfg.cache.generation != config_generation ||
		    ecm_request_lifetime_id_load(ecm) != delivery.lifetime_id) {
			newcamd_config_read_unlock();
			return;
		}
		ecm_unlock();
		pipe_send_cacheex_push_out_snapshot(&delivery);
		ecm_lock();
		newcamd_config_read_unlock();
		if (ecm_request_lifetime_id_load(ecm) != delivery.lifetime_id) return;
	}
#endif

	// Send the cache reply serialized at the successful commit.
	pipe_cache_reply_send(cache_reply_buf, cache_reply_len);

#ifdef CLI_CSCACHE
	// Send to Newcamd Cached Servers
	int i;
	uint64_t request_lifetime_id = ecm_request_lifetime_id_load(ecm);
	for (i=0; i<20; i++) {
		uint32_t server_id = ecm->server[i].srvid;
		if (!server_id) break;
		if (ecm->server[i].flag != ECM_SRV_REQUEST || !request_lifetime_id) continue;
		/* Do not inspect server runtime state here: no-thread callers can hold
		 * lockecm without locksrv. The ECM event loop filters for a live,
		 * cscached Newcamd request with this exact lifetime before sending. */
		if (cache_queue_server_cscache_reply(server_id,
		        request_lifetime_id, ecm->caid, ecm->provid, ecm->sid,
		        ecm->ecm[0], ecm->cw) <= 0)
			debugf(DBG_ERROR, " !!! could not queue cached Newcamd reply for server %u, request lifetime %llu\n",
			       (unsigned)server_id, (unsigned long long)request_lifetime_id);
	}
#endif
}

void ecm_setdcw(ECM_DATA *ecm, uint8_t dcw[16], int srctype, int srcid)
{
	ecm_setdcw_origin(ecm, dcw, srctype, srcid, 0);
}

/* The no-thread ECM pipe consumer invokes this while holding lockecm. Preserve
 * the cache peer's cycle marker and accumulated source classes through
 * validation without acquiring locksrv from the lockecm path. */
int ecm_setdcw_marked(ECM_DATA *ecm, uint8_t dcw[16], int srctype,
                      int srcid, int cwmark, uint8_t source_origin_mask)
{
	if (!ecm || !dcw || !cache_origin_mask_valid(source_origin_mask)) return 0;
	if (ecm->cs && srctype == DCW_SOURCE_CACHE && cwmark != 0 && ecm->cw1cycle) {
		int global_cycle = 0;
		dcw_filter_globals_snapshot(NULL, NULL, &global_cycle, NULL);
		if (provider_cwcycle_enabled(ecm->caid, ecm->provid) || global_cycle) {
			extern int checkcycle(uint8_t cw1cycle, uint8_t ecmtag, uint8_t cwcycle);
			extern void checkcycle_mismatch_inc(void);
			if (!checkcycle(ecm->cw1cycle, ecm->ecm[0], (uint8_t)cwmark)) {
				if (ecm->trace_instance)
					user_trace_shared_event(ecm->trace_instance, USER_TRACE_REJECTED,
					                        srctype, srcid,
					                        tick32_elapsed(GetTickCount(), ecm->recvtime),
					                        "CW rejected by ECM worker: cache cycle marker mismatch");
				checkcycle_mismatch_inc();
				debugf(DBG_ERROR,
				       " !!! [CYCLE] %s: CW cycle mismatch %04X:%06X:%04X/%02x (mark=%d) -- REFUSED\n",
				       ecm->cs ? ecm->cs->name : "profile unavailable",
				       ecm->caid, ecm->provid, ecm->sid, ecm->ecm[0], cwmark);
#ifdef MCS_DCWSTATS
				DCWSTATS_REJECT(DCW_REJ_CYCLE);
#endif
				return 0;
			}
		}
	}
	ecm_setdcw_origin(ecm, dcw, srctype, srcid, source_origin_mask);
	return 1;
}

#endif



#ifdef THREAD_DCW

/* Prechecks run on a snapshot outside lockecm. Report only if the queued
 * storage still has the same lifetime and trace token; never infer from hash. */
static void trace_queued_rejection(ECM_DATA *queued, uint64_t lifetime,
                                   uint64_t trace, int srctype, int srcid,
                                   const char *detail)
{
    if (!queued || !lifetime || !trace) return;
    ecm_lock();
    if (ecm_request_lifetime_id_load(queued) == lifetime && user_trace_same_instance(trace, queued->trace_instance))
        user_trace_shared_event(trace, USER_TRACE_REJECTED, srctype, srcid,
                                GetTickCount() - queued->recvtime, detail);
    ecm_unlock();
}

int ecm_setdcw_marked(ECM_DATA *ecm, uint8_t dcw[16], int srctype,
                      int srcid, int cwmark, uint8_t source_origin_mask);

/* Caller holds lockecm after a line CW passed every decode gate. Copy and
 * retire the held candidate atomically; it is queued after the line commit and
 * will re-enter Set-DCW as evidence only, never as a second client delivery. */
static int cache_verify_take_candidate_locked(ECM_DATA *ecm, uint8_t cw_out[16],
                                              uint32_t *peerid_out,
                                              int *cwmark_out,
                                              uint8_t *origin_mask_out)
{
	if (!ecm || !cw_out || !peerid_out || !cwmark_out || !origin_mask_out ||
	    !ecm->cache_verify_pending ||
	    !cache_origin_mask_valid(ecm->cache_verify_origin_mask)) return 0;
	memcpy(cw_out, ecm->cache_verify_cw, 16);
	*peerid_out = ecm->cache_verify_peerid;
	*cwmark_out = ecm->cache_verify_cwmark;
	*origin_mask_out = ecm->cache_verify_origin_mask;
	ecm->cache_verify_pending = 0;
	ecm->cache_verify_dispatched = 0;
	ecm->cache_verify_deadline = 0;
	return 1;
}

static void cache_verify_setdcw_result_done(ECM_DATA *queued,
                                             uint64_t lifetime,
                                             int srctype)
{
	int wake = 0;
	if (!queued || !lifetime) return;
	ecm_lock();
	if (ecm_request_lifetime_id_load(queued) == lifetime) {
		if (srctype == DCW_SOURCE_SERVER)
			queued->cache_verify_response_queued = 0;
		if (srctype == DCW_SOURCE_CACHE && queued->cache_verify_queued) {
			queued->cache_verify_queued = 0;
			if (queued->dcwstatus != STAT_DCW_SUCCESS &&
			    queued->dcwstatus != STAT_DCW_FAILED) {
				queued->checktime = 1;
				queued->waitserver = 0;
				wake = 1;
			}
		} else if (srctype == DCW_SOURCE_SERVER &&
		           queued->cache_verify_pending &&
		           queued->dcwstatus != STAT_DCW_SUCCESS &&
		           queued->dcwstatus != STAT_DCW_FAILED) {
			/* A rejected line result is now fully resolved; let the ECM worker
			 * deliver the cache fallback or resume its normal timeout path. */
			queued->checktime = 1;
			queued->waitserver = 0;
			wake = 1;
		}
	}
	ecm_unlock();
	if (wake) wakeup_sendecm();
}

void ecm_setdcwdata( ECM_DATA *ecm, uint8_t dcw[16], int srctype, int srcid,
                     int cwmark, uint8_t source_origin_mask,
                     uint64_t trace_at_queue, uint64_t lifetime_at_queue )
{
	uint8_t cache_reply_buf[80];
	int cache_reply_len = 0;
	uint32_t committed_recvtime = 0;
	uint32_t committed_provid = 0;
	uint32_t committed_hash = 0;
	uint32_t committed_server_srvid[20];
	uint16_t committed_caid = 0;
	uint16_t committed_sid = 0;
	uint8_t committed_tag = 0;
	uint8_t committed_cw[16];
	uint8_t committed_server_flag[20];
	int committed_period = 0;
	int committed_cw1cycle = 0;
	uint64_t committed_trace_instance = 0;
	ECM_DATA *committed_request = NULL;
	uint64_t committed_lifetime_id = 0;
	int committed_i;
	struct ledger_delivery_snapshot delivery;
	int delivery_valid = 0;
	int late_reference = 0;
	int queue_cache_reference = 0;
	uint8_t cache_reference_cw[16];
	uint8_t cache_reference_origin_mask = 0;
	uint32_t cache_reference_peerid = 0;
	int cache_reference_cwmark = NO_CYCLE;
#ifdef CACHEEX
	uint8_t offered_cw[16];
	if (dcw) memcpy(offered_cw, dcw, sizeof(offered_cw));
#endif
	/* The pipe carries a pointer to reusable storage. Copy the old request
	 * under lock for the prechecks, then revalidate before any commit. */
	ECM_DATA *queued_ecm = ecm;
	ECM_DATA snapshot;
	if (!cache_origin_mask_valid(source_origin_mask)) return;
	newcamd_config_read_lock();
	ecm_lock();
	if (!queued_ecm || !lifetime_at_queue ||
	    (srctype == DCW_SOURCE_CACHE &&
	     !cache_ecm_snapshot_validate(queued_ecm, lifetime_at_queue)) ||
	    ecm_request_lifetime_id_load(queued_ecm) != lifetime_at_queue) {
		ecm_unlock();
		newcamd_config_read_unlock(); return;
	}
	memcpy(&snapshot, queued_ecm, sizeof(snapshot));
	ecm_unlock();
	ecm = &snapshot;
#ifdef CACHEEX
	if (dcw) ledger_note_candidate(ecm, offered_cw, srctype, srcid, GetTickCount());
#endif
	char nullcw[16] = "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0";
	if ( dcwcmp16(dcw,nullcw) ) {
        trace_queued_rejection(queued_ecm, lifetime_at_queue, trace_at_queue, srctype, srcid, "CW rejected by ECM worker: all bytes zero");
        newcamd_config_read_unlock(); return;
    }

	struct cardserver_data *cs = ecm->cs;
	if (!cs) {
        trace_queued_rejection(queued_ecm, lifetime_at_queue, trace_at_queue, srctype, srcid, "CW rejected by ECM worker: profile unavailable");
        newcamd_config_read_unlock(); return;
    }

	if ( dcwcmp8(dcw,nullcw) && dcwcmp8(dcw+8,nullcw) ) {
        trace_queued_rejection(queued_ecm, lifetime_at_queue, trace_at_queue, srctype, srcid, "CW rejected by ECM worker: both halves zero");
        newcamd_config_read_unlock(); return;
    }

	/*
	 * TASK 1.8 — structural soft signal, evaluated before acceptDCW() so that
	 * a key which is about to be rejected is attributed as well. A rejected key
	 * otherwise leaves no trace anywhere: acceptDCW() returns a bare 0 and the
	 * client only ever sees a timeout.
	 *
	 * This does not change delivery. The scan's only output is evidence, and
	 * acceptDCW() below still makes the same decision it always made.
	 *
	 * The two filter gates are passed in rather than read inside the scan so
	 * that a filter the operator has disabled contributes nothing: if they
	 * turned DCWFILTER CHECKSUM off, their bouquet legitimately fails it and
	 * counting that would penalise a good source on every key.
	 */
	{
		int checksum_filter, repeat_filter;
		dcw_filter_globals_snapshot(&checksum_filter, &repeat_filter, NULL, NULL);
		dcwstruct_note(&trust_tab,
			dcwstruct_scan(dcw, checksum_filter, repeat_filter),
			dcw, srctype, srcid, ecm->caid, ecm->provid, ecm->provider_known, ecm->sid, GetTickCount());
	}

	/*
	 * TASK 2.2 -- entropy and collision forensics.
	 *
	 * Same place as dcwstruct_note() and for the same reason: above acceptDCW(),
	 * so a key that is about to be thrown away is examined too. That is not
	 * bookkeeping -- a forged key is exactly the kind that gets discarded, and a
	 * forgery nobody looks at is a forgery nobody can prove.
	 *
	 * The identity passed is the 128-bit ECM MD5 (its 64-bit prefix is what the
	 * ring compares against); with no CACHEEX there is no ecmd5 and the collision
	 * half goes quiet, while the entropy half keeps working, because it needs no
	 * identity at all.
	 */
	cwentropy_note(&trust_tab,
#ifdef CACHEEX
		ecm->ecmd5,
#else
		0,
#endif
		dcw, srctype, srcid, ecm->caid, ecm->provid, ecm->provider_known, ecm->sid, GetTickCount());

	/*
	 * TASK 2.4 -- plausibility: how much of the key's shape survives.
	 *
	 * The third of the three evidence-only hooks, in the same place and for the
	 * same reason as the two above: this runs before acceptDCW() decides
	 * anything, so a key that is about to be discarded is measured too. It
	 * reads no filter gate on purpose -- "exactly one of four group sums is
	 * wrong" means the same thing whether or not the operator left DCWFILTER
	 * CHECKSUM on, which is what makes this a statement about the key rather
	 * than about the configuration.
	 *
	 * Nothing here can reject, delay or alter a delivery: the return value goes
	 * to a counter and, at most, to one soft trust event.
	 */
	cwp_note(srctype, srcid, dcw, ecm->caid, ecm->provid, ecm->provider_known, ecm->sid, GetTickCount());

	/* TASK 2.7 -- the mirror scan, reading no filter gate on purpose. The
	 * repaired mirror variant passes the checksum layer and is DELIVERED;
	 * this is the only place its story gets told. */
	cm_note(srctype, srcid, dcw, ecm->caid, ecm->provid, ecm->provider_known, ecm->sid, GetTickCount());



	/*
	 * DCW MINTIME is an operator-configured server-only timing gate.
	 * It measures elapsed time from dispatch to this processing point,
	 * not necessarily network response latency. A fast answer alone does
	 * not prove an invalid CW. Cache and local client sources are exempt.
	 */
	if (srctype == DCW_SOURCE_SERVER && cs->option.dcw.mintime > 0) {
		int k;
		uint32_t now = GetTickCount();
		for (k=0; k<20; k++) {
			if (ecm->server[k].srvid && ecm->server[k].srvid==(uint32_t)srcid) {
				uint32_t elapsed = tick32_elapsed(now, ecm->server[k].sendtime);
				if (tick32_elapsed_less_than(now, ecm->server[k].sendtime,
				                             (uint32_t)cs->option.dcw.mintime)) {
					debugf(DBG_ERROR, " !!! [MINTIME] %s: server %d processing after %ums < %dms, CW skipped\n", cs->name, srcid, (unsigned)elapsed, cs->option.dcw.mintime);
					ecm_lock();
					if (ecm_request_lifetime_id_load(queued_ecm) == lifetime_at_queue &&
					    user_trace_same_instance(trace_at_queue, queued_ecm->trace_instance)) {
						char detail[128];
						snprintf(detail, sizeof(detail), "CW rejected by DCW MINTIME; %u ms since dispatch at processing; threshold %d ms", (unsigned)elapsed, cs->option.dcw.mintime);
						user_trace_shared_event(trace_at_queue, USER_TRACE_REJECTED, 0, 0,
						    now - queued_ecm->recvtime, detail);
					}
					ecm_unlock();
					newcamd_config_read_unlock(); return;
				}
				break;
			}
		}
	}

    /*
     * R48 CW timing anomaly (observe-only).
     *
     * A SERVER-sourced CW that returns implausibly fast (< 30ms from
     * dispatch) likely came from a server with no card (fake). Cache
     * sources are exempt -- they are legitimately fast. This never
     * refuses; it traces the anomaly for /user-trace and aggregates
     * per-peer fast counts for the operator's trust decisions.
     */
    if (srctype == DCW_SOURCE_SERVER) {
        int k;
        uint32_t now = GetTickCount();
        for (k = 0; k < 20; k++) {
            if (ecm->server[k].srvid && ecm->server[k].srvid == (uint32_t)srcid) {
                uint32_t elapsed = tick32_elapsed(now, ecm->server[k].sendtime);
                if (cwtime_anomaly_note((uint32_t)srcid, elapsed)) {
                    debugf(0, "    ⚠️ [CW-TIMING] %04X:%06X:%04X server %d -- %ums < %dms (possible fake, no card)\n",
                           ecm->caid, ecm->provid, ecm->sid, srcid, (unsigned)elapsed, CWTIME_ANOMALY_MS);
                }
                break;
            }
        }
    }

	{
        int reason = DCW_VALID;
        if (!acceptDCW_profile_ecm_reason(dcw, cs, ecm->caid, ecm->provid, ecm->sid, &reason)) {
            const char *why = "CW rejected by ECM worker profile validation: reason unknown";
            switch (reason) {
            case DCW_INVALID_CHECKSUM: why = "CW rejected by ECM worker profile validation: checksum"; break;
            case DCW_INVALID_NULL: why = "CW rejected by ECM worker profile validation: null-word"; break;
            case DCW_INVALID_REPEAT: why = "CW rejected by ECM worker profile validation: repeated-byte"; break;
            case DCW_INVALID_BADLIST: why = "CW rejected by ECM worker profile validation: BAD-DCW list"; break;
            case DCW_INVALID_PATTERN: why = "CW rejected by ECM worker profile validation: enabled pattern"; break;
            }
            trace_queued_rejection(queued_ecm, lifetime_at_queue, trace_at_queue, srctype, srcid, why);
            newcamd_config_read_unlock(); return;
        }
    }

    /*
     * Fake-CW gate for the live (THREAD_DCW) path.
     *
     * The original check_fake() lives inside the #ifndef THREAD_DCW block
     * above, which the production build (target=x64 defines THREAD_DCW)
     * never compiles. This gate mirrors its semantics on the live path:
     * Nagravision CAIDs (0x1800-0x1884) whose CW fails the structural
     * checksum are rejected as fake. The checksum itself is the same
     * checksumDCW() used by acceptDCW(), so a structurally valid key is
     * never rejected here; only keys that would fail checksum anyway are
     * dropped earlier, with a trace record instead of a silent timeout.
     *
     * 0x1814 (MEO PT) added 2026-11-13 per operator field report: the
     * profile's fake-CW reports were previously invisible on this path
     * because the gate was dead code.
     */
    if (ecm->caid >= 0x1800 && ecm->caid <= 0x1884) {
        if (!checksumDCW(dcw)) {
            trace_queued_rejection(queued_ecm, lifetime_at_queue, trace_at_queue, srctype, srcid,
                                   "CW rejected by ECM worker: fake CW (Nagravision checksum)");
            debugf(0, "    ❌ [FAKE-CW] %04X:%06X:%04X -- Nagravision checksum failed\n",
                   ecm->caid, ecm->provid, ecm->sid);
            newcamd_config_read_unlock(); return;
        }
    }
    /*
     * R48 trusted-source fast-path.
     *
     * When the operator designates a peer/server as trusted
     * (CACHE PEER ... { trusted=YES }), its CWs are delivered
     * immediately after structural validation, bypassing consensus.
     * Solution for majority-fake scenarios (100+ cache peers, 40+
     * Newcamd lines, most sending fakes): operator knows which
     * source is real, system trusts it. Structural checks still run.
     */
    {
        int is_trusted = 0;
        if (srctype == DCW_SOURCE_CACHE) {
            struct cachepeer_data *peer = getpeerbyid(srcid);
            if (peer && peer->trusted) is_trusted = 1;
        } else if (srctype == DCW_SOURCE_SERVER) {
            struct server_data *srv = getsrvbyid(srcid & 0xffff);
            if (srv && srv->trusted) is_trusted = 1;
        }
        if (is_trusted) {
            debugf(0, "    ✅ [TRUSTED] %04X:%06X:%04X from trusted source %d -- fast-path\n",
                   ecm->caid, ecm->provid, ecm->sid, srcid);
            ecm_lock();
            ecm = queued_ecm;
            if (ecm_request_lifetime_id_load(ecm) != lifetime_at_queue) {
                ecm_unlock();
                newcamd_config_read_unlock(); return;
            }
            goto trusted_deliver;
        }
    }


    /*
     * R48 CW fingerprint (observe-only, opt-in per profile).
     *
     * Learns the per-byte-position distribution of genuine CWs for
     * this channel and traces statistical anomalies -- the XOR 0xF0
     * last-byte fake and other biased generators that pass every
     * structural gate. Never refuses; the anomaly is evidence for
     * /user-trace and for the operator's source-trust decisions.
     */
    if (cs->option.cw_fingerprint) {
        if (cwfp_note(ecm->caid, ecm->provid, ecm->sid, dcw)) {
            debugf(0, "    ⚠️ [CW-FINGERPRINT] %04X:%06X:%04X -- statistical anomaly vs established baseline\n",
                   ecm->caid, ecm->provid, ecm->sid);
            trace_queued_rejection(queued_ecm, lifetime_at_queue, trace_at_queue, srctype, srcid,
                                   "CW fingerprint anomaly (observe-only, delivered)");
        }
    }

	/*
	 * TASK BS1 (2026-10-26) -- CW Cycle Validation for Black Screen Prevention
	 *
	 * When a CW arrives from the cache with a cycle marker (cwmark),
	 * validate that the cycle matches the ECM's expectation.
	 * A mismatch means the key is correct in content but wrong in
	 * timing: applying it produces a black screen despite passing
	 * every content filter. Closes docs/BLACKSCREEN.md Cause 5.
	 *
	 * ENHANCED: Now uses provider_cwcycle_enabled() to check only CAIDs
	 * that need cycle validation (from ecm-provider-config.h).
	 */
	if (srctype == DCW_SOURCE_CACHE && cwmark != 0 && ecm->cw1cycle) {
		int global_cycle = 0;
		dcw_filter_globals_snapshot(NULL, NULL, &global_cycle, NULL);
		/* Check if this CAID needs cycle validation */
		if (provider_cwcycle_enabled(ecm->caid, ecm->provid) || global_cycle) {
			extern int checkcycle(uint8_t, uint8_t, uint8_t);
			extern void checkcycle_mismatch_inc(void);
			if (!checkcycle(ecm->cw1cycle, ecm->ecm[0], cwmark)) {
                trace_queued_rejection(queued_ecm, lifetime_at_queue, trace_at_queue, srctype, srcid,
                                       "CW rejected by ECM worker: cache cycle marker mismatch");
				checkcycle_mismatch_inc();
				debugf(0, " !!! [CYCLE] %s: CW cycle mismatch %04X:%06X:%04X/%02x (mark=%d) -- REFUSED\n",
					cs->name, ecm->caid, ecm->provid, ecm->sid, ecm->ecm[0], cwmark);
#ifdef MCS_DCWSTATS
				DCWSTATS_REJECT(DCW_REJ_CYCLE);
#endif
				newcamd_config_read_unlock(); return;
			}
		}
	}

	/*
	 * R200 v2.5 Integration: DCW STRICT + SuperSmartFilter
	 *
	 * Advanced CW validation with 12 total checks:
	 * - DCW STRICT (3 stages): entropy < 5, all-same-byte, stale/recycled CW
	 * - SuperSmartFilter (9 stages): empty, fake, entropy, pattern, format,
	 *   blacklist, duplicate, cross-channel, multi-check
	 *
	 * Plus: Smart peer banning based on bad CW ratio, trust score penalty system.
	 */
	{
		uint32_t now_ms = GetTickCount();
		const char *dcwstrict_reason = NULL;
		/* R48: DCW STRICT is opt-in via DCWFILTER STRICT: ON.
		 * The snapshot is taken here (not in dcwstrict_init) so a
		 * config reload arms/disarms it without a restart. */
		dcwstrict_cfg.enabled = dcw_filter_strict_snapshot();
		int dcwstrict_rejected = dcwstrict_check(dcw, ecm->caid, ecm->provid, ecm->sid, now_ms, &dcwstrict_reason);
		
		if (dcwstrict_rejected) {
            char detail[128];
            snprintf(detail, sizeof(detail), "CW rejected by ECM worker: DCW STRICT; reason=%s",
                     dcwstrict_reason ? dcwstrict_reason : "unknown");
            trace_queued_rejection(queued_ecm, lifetime_at_queue, trace_at_queue, srctype, srcid, detail);
			debugf(0, "    ❌ [DCW-STRICT-REJECT] %04X:%06X:%04X -- %s\n",
				ecm->caid, ecm->provid, ecm->sid, dcwstrict_reason ? dcwstrict_reason : "unknown");
			if (srctype == DCW_SOURCE_CACHE && srcid > 0) {
				dcwstrict_trust_penalty(srcid);
			}
			newcamd_config_read_unlock(); return;
		}
		debugf(0, "    ✅ [DCW-STRICT] OK\n");
		
		/* R48: per-profile filter level override */
		ssf_profile_level_override = cs->option.sf_filter_level;
		int ssf_reason = 0;
		int ssf_rejected = supersmartfilter_check(dcw, 
#ifdef CACHEEX
			ecm->ecmd5,
#else
			NULL,
#endif
			ecm->caid, ecm->provid, ecm->sid, srcid, now_ms, &ssf_reason);
		
		if (ssf_rejected) {
            char detail[96];
            snprintf(detail, sizeof(detail), "CW rejected by ECM worker: SuperSmartFilter reason code %d", ssf_reason);
            trace_queued_rejection(queued_ecm, lifetime_at_queue, trace_at_queue, srctype, srcid, detail);
			debugf(0, "    ❌ [SSF-v2-REJECT] %04X:%06X:%04X -- reason code %d\n",
				ecm->caid, ecm->provid, ecm->sid, ssf_reason);
			if (srctype == DCW_SOURCE_CACHE && srcid > 0) {
				dcwstrict_trust_penalty(srcid);
			}
			newcamd_config_read_unlock(); return;
		}
		debugf(0, "    ✅ [SSF-v2] OK\n");
		
			/* D111 D-1: do not reward a source for passing local filters. A
			 * cache source receives positive trust only in the ledger's accepted
			 * cache/line-reference agreement batch. */
	}



	int cwpart = 2;
	if (ecm->cw1cycle) {
		if (ecm->ecm[0]==ecm->cw1cycle) cwpart = 1; else cwpart = 0;
	}
	else {
		if ( dcwcmp8(dcw,nullcw) ) cwpart = 1;
		else if ( dcwcmp8(dcw+8,nullcw) ) cwpart = 0;
	}

	ecm_lock();
	ecm = queued_ecm;
	if (ecm_request_lifetime_id_load(ecm) != lifetime_at_queue) {
		ecm_unlock();
		newcamd_config_read_unlock(); return;
	}

	if (ecm->dcwstatus==STAT_DCW_SUCCESS) {
		/* D111 D-1: allow the other source class to finish validation after the
		 * first CW was delivered. An accepted line-server reply may corroborate
		 * a cache CW without being sent to clients a second time; an already
		 * queued cache candidate may also be checked against a line result. */
		late_reference =
		    (srctype == DCW_SOURCE_SERVER && ecm->dcwsrctype == DCW_SOURCE_CACHE) ||
		    (srctype == DCW_SOURCE_CACHE && ecm->dcwsrctype == DCW_SOURCE_SERVER);
		if (!late_reference) {
        if (user_trace_same_instance(trace_at_queue, ecm->trace_instance))
            user_trace_shared_event(trace_at_queue, USER_TRACE_REJECTED, srctype, srcid,
                GetTickCount()-ecm->recvtime, "CW not applied: ECM already succeeded");
			ecm_unlock();
			newcamd_config_read_unlock(); return;
		}
	}
	/*
	 * TASK 3.16: the pipe handler may have queued this push while the
	 * ECM was still waiting, and the failure committed before this
	 * thread took the lock. Do not flip FAILED to SUCCESS, and do not
	 * send the key. A card-server reply is not this path.
	 */
	if (srctype==DCW_SOURCE_CACHE && ecm->dcwstatus==STAT_DCW_FAILED) {
        if (user_trace_same_instance(trace_at_queue, ecm->trace_instance))
            user_trace_shared_event(trace_at_queue, USER_TRACE_REJECTED, srctype, srcid,
                GetTickCount()-ecm->recvtime, "cache CW not applied: ECM already failed");
		ecm_unlock();
		newcamd_config_read_unlock(); return;
	}
/*
	if ( !ecmdata_check_cw( ecm->ecm[0], ecm->hash, ecm->caid,
		                        ecm->provid, ecm->provider_known,
		                        ecm->sid, dcw, cwpart) ) {
		ecm_unlock();
		newcamd_config_read_unlock(); return;
	}
*/

	if (srctype!=DCW_SOURCE_CACHE && !late_reference) {
		pthread_mutex_lock( &prg.lockcache );
		int f = cache_check_cw( ecm->recvtime, ecm->ecm[0], ecm->caid, ecm->hash,
		                             ecm->sid, ecm->provid,
		                             ecm->provider_known,
		                             dcw, cwpart);
		pthread_mutex_unlock( &prg.lockcache );
		if (!f) {
			if (user_trace_same_instance(trace_at_queue, ecm->trace_instance)) user_trace_shared_event(trace_at_queue, USER_TRACE_REJECTED,
			    srctype, srcid, GetTickCount()-ecm->recvtime, "CW rejected by cache repeat check: matching cached CW or cache table unavailable");
			ecm_unlock();
			newcamd_config_read_unlock(); return;
		}
	}

	// filter non-nds halfnulled cw
	if ( dcwcmp8(dcw,nullcw) || dcwcmp8(dcw+8,nullcw) ) {
		if (!ca_halfnull_ok(ecm->caid)) { /* TASK R36 (D92): CAID FAMILY HALFNULLED (stock: 09 only) */
			if (user_trace_same_instance(trace_at_queue, ecm->trace_instance))
				user_trace_shared_event(trace_at_queue, USER_TRACE_REJECTED, 0, 0,
				    GetTickCount()-ecm->recvtime, "CW rejected: zero half not allowed for CAID family");
			ecm_unlock();
			newcamd_config_read_unlock(); return;
		}
		int swap = 0;
#ifdef DCWSWAP
		if (cs)	if (cs->option.dcw.swap) swap = 1;
#endif
		if ( !dcwcheck_nds( ecm, dcw, swap) ) {
			if (user_trace_same_instance(trace_at_queue, ecm->trace_instance)) user_trace_shared_event(trace_at_queue, USER_TRACE_REJECTED,
			    0, 0, GetTickCount()-ecm->recvtime, "CW rejected: half does not match ECM cycle and swap disabled");
			ecm_unlock();
			newcamd_config_read_unlock(); return;
		}
	}

#ifdef CHECK_NEXTDCW
	if (cs->option.dcw.check && !cs->option.dcw.halfnulled) {
		int check = checkfreeze_setdcw(ecm,dcw);
		if (check==0) {
            if (user_trace_same_instance(trace_at_queue, ecm->trace_instance)) {
                const char *why = (dcwcmp8(dcw,nullcw) || dcwcmp8(dcw+8,nullcw)) ?
                    "CW rejected by DCW CHECK: zero half with previous decode" :
                    "CW rejected by DCW CHECK: inconsistent with recorded cycle";
                user_trace_shared_event(trace_at_queue, USER_TRACE_REJECTED, srctype, srcid,
                                        GetTickCount()-ecm->recvtime, why);
            }
			if (!late_reference) ecm->lastdecode.error++;
			ecm_unlock();
			newcamd_config_read_unlock(); return;
		}
		else if (!late_reference && check==1) {
			ecm->lastdecode.counter = 0;
			ecm->lastdecode.cwcycle = 0;
		}
		else if (!late_reference && (check&2)) {
			ecm->lastdecode.counter++;
			if (check&4) ecm->lastdecode.cwcycle = '1';
			else ecm->lastdecode.cwcycle = '0';
		}
	}
#endif

	if (late_reference && user_trace_same_instance(trace_at_queue, ecm->trace_instance))
		user_trace_shared_event(trace_at_queue, USER_TRACE_VALIDATED, srctype, srcid,
		    GetTickCount()-ecm->recvtime,
		    "late candidate passed validation; CW not reapplied to clients");
	r48_cache_window_note_card_response(ecm, cs, srctype, srcid);
#ifdef CACHEEX
	ledger_note_accepted(ecm, offered_cw, dcw, srctype, srcid, GetTickCount());
#endif
	if (late_reference) {
		/* This candidate passed the remaining decode gates and was recorded only
		 * as evidence. Preserve the first committed CW/source and send nothing. */
		ecm_unlock();
		newcamd_config_read_unlock();
		return;
	}
	if (srctype == DCW_SOURCE_SERVER)
		queue_cache_reference = cache_verify_take_candidate_locked(
		    ecm, cache_reference_cw, &cache_reference_peerid,
		    &cache_reference_cwmark, &cache_reference_origin_mask);
trusted_deliver:
	ecm->statusmsg = "Decode Success";
	int instant = (ecm->dcwstatus==STAT_DCW_WAITCACHE);
	ecm->dcwsrctype = srctype;
	ecm->dcwsrcid = srcid;
	ecm_request_set_dcwstatus(ecm, STAT_DCW_SUCCESS);
	ecm->checktime = 0;
	ecm->waitserver = 0;
	sid_newecm(ecm);
	memcpy( ecm->cw, dcw, 16 );
	cache_reply_len = pipe_cache_reply(ecm, cs, cache_reply_buf,
	                                   sizeof(cache_reply_buf), source_origin_mask);
	delivery_valid = ledger_delivery_snapshot_capture_locked(&delivery, ecm);
	if (delivery_valid) {
		committed_request = delivery.request;
		committed_lifetime_id = delivery.lifetime_id;
	}

	/* CW-POSTVAL: Record successful CW for peer trust scoring */
	if (srctype == DCW_SOURCE_SERVER) cwpostval_trust_good((uint32_t)srcid);

	/*
	 * TASK 3.6: the waiter is answered -- release the cache entry's push arm
	 * now, while the success is already committed (dcwstatus above) and the
	 * back-pointer still unambiguous. The entry's arm must not outlive its
	 * waiter: an armed entry with a dead waiter force-marks every later peer
	 * key DCW_SENT -- burying it for every future ECM of that hash -- and
	 * stays silent towards peer requests. lockecm -> lockcache is the tree's
	 * sanctioned order; the srctype!=CACHE cache_check_cw walk above took it
	 * first. The helper's guard keeps a newer waiter's arm intact.
	 */
	cache_clear_sendpipe( ecm );
	if (queue_cache_reference) {
		/* The validated line CW is already committed and has priority. Queue the
		 * saved cache CW behind it for evidence-only validation. */
		if (ecm->trace_instance)
			user_trace_shared_event(ecm->trace_instance, USER_TRACE_CACHE,
			    DCW_SOURCE_SERVER, srcid, GetTickCount() - ecm->recvtime,
			    "line reference accepted; queued tied cache candidate for evidence-only validation");
		ecm_setdcw_marked(ecm, cache_reference_cw, DCW_SOURCE_CACHE,
		                  (int)cache_reference_peerid, cache_reference_cwmark,
		                  cache_reference_origin_mask);
	}

	/* Freeze all fields used after unlocking; ECM_DATA storage may be reused
	 * as soon as the lock is released. Keep the trace tied to this lifetime. */
#ifdef CACHEEX
	if (delivery_valid) {
		committed_caid = delivery.caid;
		committed_provid = delivery.provid;
		committed_sid = delivery.sid;
		committed_hash = delivery.hash;
		committed_tag = delivery.tag;
		committed_recvtime = delivery.recvtime;
		committed_period = delivery.period;
		committed_cw1cycle = delivery.cw1cycle;
		memcpy(committed_cw, delivery.cw, sizeof(committed_cw));
	} else
#endif
	{
		committed_caid = ecm->caid;
		committed_provid = ecm->provid;
		committed_sid = ecm->sid;
		committed_hash = ecm->hash;
		committed_tag = ecm->ecm[0];
		committed_recvtime = ecm->recvtime;
		committed_period = ecm->period;
		committed_cw1cycle = ecm->cw1cycle;
		memcpy(committed_cw, ecm->cw, sizeof(committed_cw));
	}
	for (committed_i = 0; committed_i < 20; committed_i++) {
		committed_server_srvid[committed_i] = ecm->server[committed_i].srvid;
		committed_server_flag[committed_i] = (uint8_t)ecm->server[committed_i].flag;
	}
	committed_trace_instance = user_trace_same_instance(trace_at_queue, ecm->trace_instance) ? trace_at_queue : 0;
	ecm_unlock();

#ifdef TESTCHANNEL
	int testchannel = ((committed_caid == cfg.testchn.caid) &&
	                   (committed_provid == cfg.testchn.provid) &&
	                   (committed_sid == cfg.testchn.sid));
	if (testchannel) {
		char dump[64];
		array2hex(committed_cw, dump, sizeof(committed_cw));
		char temp[512];
		src2string(srctype, srcid, temp);
		fdebugf(" =(setdcw)= from %s ch %04x:%06x:%04x/%02x:%08x => %s\n",
		        temp, committed_caid, committed_provid, committed_sid,
		        committed_tag, committed_hash, dump);
	}
#endif

	// Check timeout
	uint32_t ecmtime = tick32_elapsed(GetTickCount(), committed_recvtime);
	if ( ecmtime > (uint32_t)cs->option.dcw.timeout * (uint32_t)committed_period ) {
		if (committed_trace_instance) {
			ecm_lock();
			if (ecm_request_lifetime_id_load(queued_ecm) == lifetime_at_queue &&
			    user_trace_same_instance(committed_trace_instance, queued_ecm->trace_instance))
				user_trace_shared_event(committed_trace_instance, USER_TRACE_DISPATCH_SKIPPED,
				    0, 0, ecmtime, "CW committed; client dispatch skipped after DCW timeout");
			ecm_unlock();
		}
		newcamd_config_read_unlock(); return;
	}
	// Send DCW to clients using the identity/CW frozen under lockecm.
	if (delivery_valid) clients_check_sendcw(delivery.request, &delivery);
	else debugf(DBG_ERROR, " !!! delivery snapshot unavailable for committed ECM %p; client send skipped\n", (void *)ecm);

	/* IBS: Record CW delivery for black screen detection */
	ibs_on_cw_sent(0, committed_caid, committed_provid, committed_sid,
	               (uint32_t)srcid, committed_cw, GetTickCount());

	/*
	 * TASK 2.5 -- the cache hand-off, observed at the moment it happens (see
	 * cwcycle.h). Only cache-sourced keys are watched, and srcid keeps every
	 * cache flavour in its own slot: CSP peers (id|PEER_CSP), cache-ex CCCam,
	 * Camd35 and Cs378x clients each carry a different flag (GR1/GR4). The
	 * expectation is the half THIS ECM declared through its own tag byte; the
	 * observed marker is what the key's sender declared on the wire, carried
	 * here through the two pipes -- 0 when nobody declared anything, which
	 * makes the delivery unverifiable, not contradictory. The call cannot
	 * reject, delay, alter or score anything: the delivery above is already
	 * done.
	 */
	if (srctype==DCW_SOURCE_CACHE)
		ccy_note(DCW_SOURCE_CACHE, srcid, committed_caid, committed_provid, committed_sid,
		         GetTickCount(), CWCY_AT_SEND,
		         cwcy_expect(committed_tag, committed_cw1cycle),
		         cwcy_observed(cwmark));

	// Update Stat
	cs->ecmok++;
	cs->ecmoktime += ecmtime;
	int time = (ecmtime+50)/100;
	if (time<99) cs->ttime[time]++; else cs->ttime[99]++;

	if (srctype==DCW_SOURCE_CACHE) {
		if (srcid&PEER_CSP) {
			cache_peer_hit_update(cs, (uint32_t)(srcid&0xffff),
			                     committed_caid, committed_provid, committed_sid, ecmtime,
			                     instant, time);
		}
#ifdef CACHEEX
		else {
			pthread_mutex_lock(&prg.lockcacheex);
		if (srcid&PEER_CCCAM_CLIENT) { // Cacheex
			struct cc_client_data *cli = getcecccamclientbyid(srcid&0xffff);
			if (cli) {
				// setup client last used cache
				cli->cacheex.lastcaid = committed_caid;
				cli->cacheex.lastprov = committed_provid;
				cli->cacheex.lastsid = committed_sid;
				cli->cacheex.lastdecodetime = ecmtime;
				// add to profiles hits
				cacheex_cccam_hitprofile( cli, cs->id );
				cli->cacheex.hits++;
				cs->hits.cacheex++;
				cfg.cacheex.hits++;
				if (instant) {
					cfg.cacheex.ihits++;
					cs->hits.instant.cacheex++;
					cli->cacheex.ihits++;
				}
			}
			if (time<99) cs->ttimecacheex[time]++; else cs->ttimecacheex[99]++;
		}

#ifdef CAMD35_SRV
		//PEERID_CAMD35
		else if (srcid&PEER_CAMD35_CLIENT) {
			struct camd35_client_data *cli = getcamd35clientbyid(srcid&0xffff);
			if (cli) {
				// setup client last used cache
				cli->cacheex.lastcaid = committed_caid;
				cli->cacheex.lastprov = committed_provid;
				cli->cacheex.lastsid = committed_sid;
				cli->cacheex.lastdecodetime = ecmtime;
				// add to profiles hits
				cacheex_camd35_hitprofile( cli, cs->id );
				cli->cacheex.hits++;
				cs->hits.cacheex++;
				cfg.cacheex.hits++;
				if (instant) {
					cfg.cacheex.ihits++;
					cs->hits.instant.cacheex++;
					cli->cacheex.ihits++;
				}
			}
			if (time<99) cs->ttimecacheex[time]++; else cs->ttimecacheex[99]++;
		}
#endif

#ifdef CS378X_SRV
		//PEERID_CS378X
		else if (srcid&PEER_CS378X_CLIENT) {
			struct camd35_client_data *cli = getcs378xclientbyid(srcid&0xffff);
			if (cli) {
				// setup client last used cache
				cli->cacheex.lastcaid = committed_caid;
				cli->cacheex.lastprov = committed_provid;
				cli->cacheex.lastsid = committed_sid;
				cli->cacheex.lastdecodetime = ecmtime;
				// add to profiles hits
				cacheex_cs378x_hitprofile( cli, cs->id );
				cli->cacheex.hits++;
				cs->hits.cacheex++;
				cfg.cacheex.hits++;
				if (instant) {
					cfg.cacheex.ihits++;
					cs->hits.instant.cacheex++;
					cli->cacheex.ihits++;
				}
			}
			if (time<99) cs->ttimecacheex[time]++; else cs->ttimecacheex[99]++;
		}
#endif

		else if (srcid&PEER_CACHEEX_SERVER) {
			struct server_data *srv = getcesrvbyid( srcid&0xffff );
			if (srv) {
				// setup client last used cache
				srv->cacheex.lastcaid = committed_caid;
				srv->cacheex.lastprov = committed_provid;
				srv->cacheex.lastsid = committed_sid;
				srv->cacheex.lastdecodetime = ecmtime;
				// add to profiles hits
				cacheex_server_hitprofile( srv, cs->id );
				srv->cacheex.hits++;
				cs->hits.cacheex++;
				cfg.cacheex.hits++;
				if (instant) {
					cfg.cacheex.ihits++;
					cs->hits.instant.cacheex++;
					srv->cacheex.ihits++;
				}
			}
			if (time<99) cs->ttimecacheex[time]++; else cs->ttimecacheex[99]++;
		}
			pthread_mutex_unlock(&prg.lockcacheex);
		}
#endif
	}

	else if (srctype==DCW_SOURCE_SERVER) {
		struct server_data *srv = getsrvbyid( srcid&0xffff );
		if (srv) {
			/* Match the locksrv-protected HTTP server-stat readers. */
			pthread_mutex_lock(&prg.locksrv);
			srv->hits++;
			pthread_mutex_unlock(&prg.locksrv);
		}
		if (time<99) cs->ttimecards[time]++; else cs->ttimecards[99]++;
	}
#ifdef SRV_CSCACHE
	else if (srctype==DCW_SOURCE_CSCLIENT) {
		/* cachedcw is updated by cs_check_sendcw() under the Newcamd
		 * config/client locks, after lockecm has been released. */
		if (time<99) cs->ttimeclients[time]++; else cs->ttimeclients[99]++;
	}
	else if (srctype==DCW_SOURCE_MGCLIENT) {
		struct mg_client_data *cli = getmgcamdclientbyid( srcid&0xffff );
		if (cli) cli->cachedcw++;
		if (time<99) cs->ttimeclients[time]++; else cs->ttimeclients[99]++;
	}
#endif

#ifdef CACHEEX
	// Send DCW to CACHE-EX servers from the frozen successful snapshot.
	if (delivery_valid && cs->option.fallowcacheex &&
	    ecmtime<cs->option.cacheexvalidtime) {
		pipe_send_cacheex_push_out_snapshot(&delivery);
	}
#endif

	// Send the cache reply serialized at the successful commit.
	pipe_cache_reply_send(cache_reply_buf, cache_reply_len);

#ifdef CLI_CSCACHE
	// Send only to servers still waiting for this exact ECM generation. lockecm
	// is no longer held; locksrv protects the pending request and session state.
	int i;
	for (i = 0; i < 20; i++) {
		if (!committed_server_srvid[i]) break;
		if (committed_server_flag[i] != ECM_SRV_REQUEST || !delivery_valid ||
		    !committed_request || !committed_lifetime_id) continue;
		struct server_data *srv = getsrvbyid(committed_server_srvid[i]);
		if (!srv) continue;
		pthread_mutex_lock(&prg.locksrv);
		if (!srv->busy || srv->type != TYPE_NEWCAMD || !srv->cscached ||
		    srv->ecm.lifetime_id != committed_lifetime_id ||
		    !ecm_request_lifetime_matches(srv->ecm.request, committed_request,
		                                 committed_lifetime_id)) {
			pthread_mutex_unlock(&prg.locksrv);
			continue;
		}
		struct cs_custom_data srvcd;
		unsigned char buf[32];
		srvcd.msgid = srv->ecm.msgid;
		srvcd.caid = committed_caid;
		srvcd.sid = committed_sid;
		srvcd.provid = committed_provid;
		buf[0] = committed_tag | 0x40; // 0xC0 | 0xC1
		buf[2] = 0x10;
		memcpy(&buf[3], committed_cw, sizeof(committed_cw));
		if (!cs_message_send(srv->handle, &srvcd, buf, 19, srv->sessionkey))
			disconnect_srv(srv);
		pthread_mutex_unlock(&prg.locksrv);
	}
#endif
	newcamd_config_read_unlock();
}





/*
 * TASK 2.5 / R48 -- the private DCW pipe carries the sender's declared cycle
 * marker and accumulated cache-source mask. Non-cache producers pass a zero
 * mask; the ECM/cache reply path adds only a locally established source class.
 */
static inline int get_setdcwdata(uint8_t *buf, void *ecm, uint8_t *dcw, int *srctype,
                          int *srcid, int *cwmark, uint8_t *source_origin_mask,
                          uint64_t *trace_at_queue, uint64_t *lifetime_at_queue)
{
	int index = 1;
	if (cwmark) *cwmark = NO_CYCLE;
	if (source_origin_mask) *source_origin_mask = 0;
	memcpy(srctype, buf+index, sizeof(int) );
	index += sizeof(int);
	memcpy(srcid, buf+index, sizeof(int) );
	index += sizeof(int);
	memcpy( ecm, buf+index, sizeof(void*) );
	index += sizeof(void*);
	memcpy( dcw, buf+index, 16 );
	index+=16;
	if (buf[index] != NO_CYCLE && buf[index] != CW0CYCLE &&
	    buf[index] != CW1CYCLE) return 0;
	if (cwmark) *cwmark = buf[index];
	index+=1;
	if (!cache_origin_mask_valid(buf[index])) return 0;
	if (source_origin_mask) *source_origin_mask = buf[index];
	index+=1;
	memcpy(trace_at_queue, buf+index, sizeof(*trace_at_queue));
	index += sizeof(*trace_at_queue);
	memcpy(lifetime_at_queue, buf+index, sizeof(*lifetime_at_queue));
	index += sizeof(*lifetime_at_queue);
	return index;
}

int put_setdcwdata(uint8_t *buf, ECM_DATA *ecm, uint8_t *dcw, int srctype,
                   int srcid, int cwmark, uint8_t source_origin_mask )
{
	if (!buf || !ecm || !dcw || !cache_origin_mask_valid(source_origin_mask) ||
	    (cwmark != NO_CYCLE && cwmark != CW0CYCLE && cwmark != CW1CYCLE))
		return 0;
	buf[0] = 55;
	int index = 1;
	memcpy(buf+index, &srctype, sizeof(int) );
	index += sizeof(int);
	memcpy(buf+index, &srcid, sizeof(int) );
	index += sizeof(int);
	memcpy( buf+index, &ecm, sizeof(void*) );
	index += sizeof(void*);
	memcpy( buf+index, dcw, 16 );
	index+=16;
	buf[index] = (uint8_t)cwmark;
	index+=1;
	buf[index] = source_origin_mask;
	index+=1;
	/* All live callers queue while holding lockecm. Trace identity controls
	 * attribution; the independent lifetime ID guards the queued CW. */
	memcpy(buf+index, &ecm->trace_instance, sizeof(ecm->trace_instance));
	index += sizeof(ecm->trace_instance);
	uint64_t lifetime_id = ecm_request_lifetime_id_load(ecm);
	memcpy(buf+index, &lifetime_id, sizeof(lifetime_id));
	index += sizeof(lifetime_id);
	return index;
}


/*
 * TASK 2.5 -- the marked form of the marshal. Every existing caller keeps
 * ecm_setdcw(): card servers and cache-ex clients never declare a cycle, so
 * their marker is 0. Only the cache pipe consumer (th-ecm.c,
 * PIPE_CACHE_FIND_SUCCESS) knows what the sending peer declared, and calls
 * ecm_setdcw_marked() so the declaration survives the trip to the delivery
 * hook inside ecm_setdcwdata().
 */
int ecm_setdcw_marked( ECM_DATA *ecm, uint8_t dcw[16], int srctype, int srcid,
                       int cwmark, uint8_t source_origin_mask )
{
	uint8_t buf[64];
	int len;
	int line_reference = 0;
	int sent;
	if (!ecm || !dcw || !cache_origin_mask_valid(source_origin_mask)) return 0;
	/* Live callers hold lockecm. Publish this only after the frame is queued so
	 * the ECM worker cannot mistake a failed queue attempt for a reference. */
	line_reference = (srctype == DCW_SOURCE_SERVER && ecm->cs &&
	                  ecm->cs->option.cache_evidence &&
	                  (ecm->dcwstatus == STAT_DCW_WAITCACHE ||
	                   ecm->dcwstatus == STAT_DCW_WAIT));
	len = put_setdcwdata(buf, ecm, dcw, srctype, srcid, cwmark,
	                     source_origin_mask);
	if (len <= 0) return 0;
	sent = pipe_send(dcwpipe[1], buf, len);
	if (sent <= 0) {
		/* Set-DCW is a required internal hand-off. A missing frame strands the
		 * waiter, so fail closed and let the process recovery path rebuild it. */
		prg.restart = 1;
		return 0;
	}
	if (line_reference) ecm->cache_verify_response_queued = 1;
	return 1;
}

void ecm_setdcw( ECM_DATA *ecm, uint8_t dcw[16], int srctype, int srcid )
{
	(void)ecm_setdcw_marked(ecm, dcw, srctype, srcid, 0, 0);
}

/* One production Set-DCW consumer step, shared with the isolated ordering
 * regression so it exercises the same lifetime checks and commit path. */
static int setdcw_process_frame(uint8_t *buf, int len)
{
	ECM_DATA *ecm = NULL;
	uint8_t dcw[16];
	uint8_t source_origin_mask = 0;
	int srctype, srcid, cwmark = 0, parsed_len;
	uint64_t trace_at_queue, lifetime_at_queue;
	int expected = 1 + 2*(int)sizeof(int) + (int)sizeof(void*) + 16 + 1 + 1 +
	               2*(int)sizeof(uint64_t);
	if (!buf || len != expected) return 0;
	parsed_len = get_setdcwdata(buf, &ecm, dcw, &srctype, &srcid, &cwmark,
	                            &source_origin_mask, &trace_at_queue,
	                            &lifetime_at_queue);
	if (parsed_len != len) return 0;
	ecm_setdcwdata(ecm, dcw, srctype, srcid, cwmark, source_origin_mask,
	               trace_at_queue, lifetime_at_queue);
	cache_verify_setdcw_result_done(ecm, lifetime_at_queue, srctype);
	return 1;
}

void *setdcw_thread(void *param)
{

	__atomic_store_n(&prg.pid_setdcw, (pid_t)syscall(SYS_gettid), __ATOMIC_RELEASE);
	prg.tid_setdcw = pthread_self();
	prctl(PR_SET_NAME,"Set DCW",0,0,0);

	struct pollfd pfd;
	while (1) {
		pfd.fd = dcwpipe[0];
		pfd.events = POLLIN | POLLPRI;
		int retval = poll(&pfd, 1, 3000);
		if ( retval>0 ) {
			uint8_t buf[64];
			int len = pipe_recv( dcwpipe[0], buf, sizeof(buf));
			if (len > 0) setdcw_process_frame(buf, len);
		}
	}
}

#endif

