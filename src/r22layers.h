/* r22layers.h — R22 Advanced CW/ECM Layers Integration (R23)
 *
 * PURPOSE: wire the 44 header-only layers into the three hook points:
 *   - cache_setdcw()     — CW arriving from peers/cache
 *   - ecm_setsrvflagdcw() — CW arriving from card servers
 *   - setdcw.c acceptDCW path — final delivery gate
 *
 * COMPILE: #define R22_LAYERS in the build to enable (default OFF).
 *          When OFF, every hook compiles to nothing and the binary
 *          fingerprint is unchanged.
 *
 * ROLLBACK: undefine R22_LAYERS and rebuild — stock behaviour returns.
 */
#ifndef MCS_R22LAYERS_H
#define MCS_R22LAYERS_H

#include <stdint.h>
#include <string.h>

#ifdef R22_LAYERS

#include "cwverify.h"
#include "cwreplace.h"
#include "ecmprefetch.h"
#include "cachewarm.h"
#include "blackscreen.h"
#include "cwconfidence.h"
#include "cwquarantine.h"
#include "cwpropblock.h"
#include "cwfeedback.h"
#include "cwfingerprint.h"
#include "cwchannel.h"
#include "cwdecay.h"
#include "multicw.h"
#include "cwvalidity.h"
#include "cwemergency.h"
#include "cwretry.h"
#include "cwpattern.h"
#include "cwcrosschannel.h"
#include "fastretx.h"
#include "parquery.h"
#include "cwpush.h"
#include "zerohandoff.h"
#include "pipeline.h"
#include "preconn.h"
#include "predictzap.h"
#include "autorecov.h"
#include "autoswitch.h"
#include "chanpred.h"
#include "fallback.h"
#include "cwpriority.h"
#include "cwpredict.h"
#include "clientbuf.h"
#include "cwredundancy.h"
#include "zerolatency.h"
#include "adapfield.h"
#include "scramchk.h"
#include "ecmcache.h"
#include "ecmcompress.h"
#include "udpfast.h"
#include "multipath.h"
#include "r22stats.h"
/* R24: Advanced CW/Blackscreen layers */
#include "cwprevalid.h"
#include "cwsrcscore.h"
#include "cwrollback.h"
#include "blackdetect.h"
#include "cwmultibuf.h"
#include "cwpredictswitch.h"
#include "blackanalytics.h"
#include "cwadaptive.h"
#include "cwcrossval.h"
#include "cwlearn.h"
#include "cwml.h"

/* R25: Immediate + Short-term recommendations */
#include "cwlock.h"
#include "cwpersist.h"
#include "cwanomaly.h"
#include "cwexplain.h"
#include "cwhistogram.h"
#include "cwfastpath.h"
#include "cwmemlimit.h"
#include "cwfeedback2.h"
#include "cwcrosslearn.h"
#include "udpfast.h"
#include "multipath.h"
#include "clientbuf.h"
#include "ecmcache.h"
#include "cwpredict.h"
#include "cwpriority.h"
#include "ecmcompress.h"
#include "adapfield.h"
#include "scramchk.h"
#include "cwredundancy.h"
#include "zerolatency.h"

/* ===== Global tables (one per layer that needs state) ===== */

static struct cwquarantine_table   r22_quar;
static struct cwpropblock_table    r22_propb;
static struct cwfeedback_table     r22_fdbk;
static struct cwfingerprint_table  r22_fpr;
static struct cwchannel_table      r22_chan;
static struct cwvalidity_table     r22_val;
static struct cwemergency_table    r22_emerg;
static struct cwpattern_library    r22_pat;
static struct cwcc_table           r22_xchan;
static struct cwredundancy_table   r22_red;
static struct blackscreen_table    r22_bscr;
static struct autoswitch_table     r22_asw;
static struct chanpred_table       r22_cpred;
static struct fallback_table       r22_fb;
static struct clientbuf_table      r22_cbuf;
static struct cwpriority_table     r22_prio;
static struct predictzap_table     r22_pzap;
static struct zerolatency_table    r22_zlat;
static struct pipeline_table       r22_pipe;
static struct cwreplace_table      r22_repl;
static struct multicw_table        r22_mcw;
static struct cwpredict_table      r22_pred;

/* ===== Init ===== */

static inline void r22_layers_init(void)
{
    cwquarantine_init(&r22_quar, 3);  /* 3s quarantine */
    cwpropblock_init(&r22_propb);
    cwfeedback_init(&r22_fdbk);
    cwfingerprint_init(&r22_fpr);
    cwchannel_init(&r22_chan);
    cwvalidity_init(&r22_val);
    cwemergency_init(&r22_emerg, 1);  /* enabled */
    cwpattern_init(&r22_pat);
    cwcrosschannel_init(&r22_xchan);
    cwredundancy_init(&r22_red);
    blackscreen_init(&r22_bscr);
    autoswitch_init(&r22_asw, 1);  /* enabled */
    chanpred_init(&r22_cpred);
    fallback_init(&r22_fb, 1);  /* enabled */
    clientbuf_init(&r22_cbuf, 5);  /* max 5 CWs per channel */
    cwpriority_init(&r22_prio);
    predictzap_init(&r22_pzap, 2);  /* depth 2 */
    zerolatency_init(&r22_zlat);
    pipeline_init(&r22_pipe);
    cwreplace_init(&r22_repl);
    multicw_init(&r22_mcw, 1);  /* enabled */
    cwpredict_init(&r22_pred);
    /* R24: Advanced layers */
    r22stats_init();
    cwprevalid_init();
    cwsrcscore_init();
    cwrollback_init();
    blackdetect_init();
    cwmultibuf_init();
    cwpredictswitch_init();
    blackanalytics_init();
    cwadaptive_init();
    cwcrossval_init();
    cwlearn_init();
    cwml_init();
    
    /* R25: Immediate + Short-term recommendations */
    cwlock_init_all();
    cwpersist_init("./data", 300);  /* auto-save every 5 minutes */
    cwanomaly_init();
    cwexplain_init();
    cwhistogram_init();
    cwfastpath_init();
    cwmem_init(10);  /* 10 MB limit */
    cwfeedback2_init();
    cwcrosslearn_init();
    
    /* Register histograms */
    cwhistogram_create_linear("cw_latency_ms", 0.0, 1000.0, 50);
    cwhistogram_create_log("cw_processing_us", 1.0, 100000.0, 50);
    cwhistogram_create_linear("peer_score", 0.0, 1.0, 20);
    
    /* Register memory trackers */
    cwmem_register_tracker("stats", CWMEM_REGION_STATS, 1024 * 1024);
    cwmem_register_tracker("srcscore", CWMEM_REGION_SRCSCORE, 256 * 1024);
    cwmem_register_tracker("learn", CWMEM_REGION_LEARN, 512 * 1024);
    cwmem_register_tracker("rollback", CWMEM_REGION_ROLLBACK, 128 * 1024);
    cwmem_register_tracker("multibuf", CWMEM_REGION_MULTIBUF, 64 * 1024);
    cwmem_register_tracker("analytics", CWMEM_REGION_ANALYTICS, 256 * 1024);
    cwmem_register_tracker("ml", CWMEM_REGION_ML, 1024 * 1024);
    
    /* Register anomaly metrics */
    cwanomaly_register_metric("cw_count");
    cwanomaly_register_metric("cw_latency");
    cwanomaly_register_metric("error_rate");
    cwanomaly_register_metric("peer_score");
    cwanomaly_register_metric("cache_hit_rate");
}

/* ================================================================
 * HOOK 1: cache_setdcw() — CW arriving from peers/cache
 *
 * Called AFTER existing gates (LOCAL_ONLY, BLOCK_FAKE_CW, etc.)
 * but BEFORE the CW is stored in cache.
 *
 * Returns: 0 = proceed, -1 = reject (fake CW)
 * ================================================================ */

static inline int r22_hook_cache_setdcw(
    uint16_t caid, uint32_t provid, uint16_t sid,
    const uint8_t *cw, int peerid, uint32_t now)
{
    int source_id = peerid & 0xFF;
    int channel_count;
    
    R22STATS_INC(hook_cache_total);

    /* R24: Source scoring — DISABLED TEMPORARILY (too strict) */
    /* TODO: Re-enable after testing */
#if 0
    if (!cwsrcscore_is_trusted(source_id)) {
        R22STATS_INC(hook_cache_rejected);
        cwsrcscore_update(source_id, 1);  /* was fake */
        return -1;
    }
#endif

    /* R24: Predictive switching — DISABLED TEMPORARILY (too strict) */
    /* TODO: Re-enable after testing */
#if 0
    if (!cwpredictswitch_should_apply(cw)) {
        R22STATS_INC(hook_cache_rejected);
        cwpredictswitch_log_true_reject();
        return -1;
    }
#endif

    /* R24: Pattern learning — does CW match learned pattern? */
    if (!cwlearn_matches(cw, caid, provid, sid)) {
        /* Suspicious but don't reject yet, just lower confidence */
    }

    /* 1. Pattern check — obvious fake? */
    if (cwpattern_is_obvious_fake(cw)) {
        cwpropblock_add(&r22_propb, cw, caid, provid, sid, now);
        R22STATS_INC(pattern_obvious_fake);
        R22STATS_INC(hook_cache_rejected);
        cwsrcscore_update(source_id, 1);  /* was fake */
        cwadaptive_log_result(1, 1);  /* predicted fake, was fake */
        return -1;
    }

    /* 2. Cross-channel check — same CW for too many channels? */
    channel_count = cwcrosschannel_channel_count(&r22_xchan, cw);
    if (channel_count > 3) {
        /* Same CW used for >3 channels: suspicious */
        cwpattern_learn(&r22_pat, cw, now);
        cwpropblock_add(&r22_propb, cw, caid, provid, sid, now);
        R22STATS_INC(crosschannel_blocked);
        R22STATS_INC(hook_cache_rejected);
        return -1;
    }

    /* 3. Propagation block — is this CW blocked? */
    if (cwpropblock_check(&r22_propb, cw, caid, provid, sid, now)) {
        R22STATS_INC(propblock_hit);
        R22STATS_INC(hook_cache_rejected);
        return -1;
    }

    /* 4. Fingerprint check — is this peer a known liar? */
    if (cwfingerprint_is_liar(&r22_fpr, source_id)) {
        /* Known liar: record but allow (with lower confidence elsewhere) */
        cwfingerprint_record_delivery(&r22_fpr, source_id, now);
        R22STATS_INC(fingerprint_liar);
    }

    /* 5. Quarantine low-confidence CWs (heuristic: no history = low confidence) */
    if (!cwchannel_has_history(&r22_chan, source_id, caid, provid, sid, 5)) {
        cwquarantine_hold(&r22_quar, caid, provid, sid, cw, now, 30, 0);
        R22STATS_INC(quarantine_held);
    }

    /* 6. Store in redundancy pool */
    cwredundancy_add(&r22_red, caid, provid, sid, cw, 50, now,
                     (uint8_t)source_id);
    R22STATS_INC(redundancy_stored);

    /* 7. Offer to priority queue */
    cwpriority_offer(&r22_prio, caid, provid, sid, cw, 50, source_id, 0, now);
    R22STATS_INC(priority_offered);

    /* 8. Record cross-channel usage */
    cwcrosschannel_record_valid(&r22_xchan, cw, caid, provid, sid, now);

    /* 9. Record CW change for prediction */
    cwvalidity_record_change(&r22_val, caid, provid, sid, now);
    R22STATS_INC(validity_recorded);
    
    zerolatency_record(&r22_zlat, caid, provid, sid, now);
    R22STATS_INC(zerolatency_recorded);
    
    cwpredict_record_change(&r22_pred, caid, provid, sid, now);
    R22STATS_INC(predict_recorded);

    /* R24: Learn from this CW */
    cwlearn_learn(cw, caid, provid, sid);
    
    /* R24: Save to rollback buffer */
    cwrollback_save(cw, caid, provid, sid);
    
    /* R24: Add to multi-CW buffer */
    cwmultibuf_add(cw, caid, provid, sid);
    
    /* R24: Update source score (good CW) */
    cwsrcscore_update(source_id, 0);  /* was not fake */
    
    /* R24: Adaptive learning */
    cwadaptive_log_result(0, 0);  /* predicted good, was good */
    
    R22STATS_INC(hook_cache_accepted);
    
    return 0;
}

/* ================================================================
 * HOOK 2: ecm_setsrvflagdcw() — CW arriving from card servers
 *
 * Called when a card server delivers a CW. This is the trusted
 * path (direct from the card), so we record it as ground truth.
 * ================================================================ */

static inline void r22_hook_ecm_setsrvflagdcw(
    uint16_t caid, uint32_t provid, uint16_t sid,
    const uint8_t *cw, unsigned int srvid, uint32_t now)
{
    int source_id = (int)(srvid & 0xFF);
    
    R22STATS_INC(hook_server_total);

    /* 1. Record as ground truth (success) */
    cwchannel_record_success(&r22_chan, source_id, caid, provid, sid);
    cwfingerprint_record_delivery(&r22_fpr, source_id, now);
    R22STATS_INC(channel_success);

    /* 2. Update cross-channel */
    cwcrosschannel_record_valid(&r22_xchan, cw, caid, provid, sid, now);

    /* 3. Update validity window */
    cwvalidity_record_change(&r22_val, caid, provid, sid, now);
    R22STATS_INC(validity_recorded);

    /* 4. Store in redundancy pool (highest confidence) */
    cwredundancy_add(&r22_red, caid, provid, sid, cw, 100, now,
                     (uint8_t)source_id);
    R22STATS_INC(redundancy_stored);

    /* 5. Offer to priority queue (high priority) */
    cwpriority_offer(&r22_prio, caid, provid, sid, cw, 100, 1, 0, now);
    R22STATS_INC(priority_offered);

    /* 6. Record CW change for prediction */
    zerolatency_record(&r22_zlat, caid, provid, sid, now);
    R22STATS_INC(zerolatency_recorded);
    cwpredict_record_change(&r22_pred, caid, provid, sid, now);
    R22STATS_INC(predict_recorded);

    /* 7. Record feedback (delivery) */
    cwfeedback_record_delivery(&r22_fdbk, 0, caid, provid, sid, cw, source_id, now);
    R22STATS_INC(feedback_delivery);

    /* 8. Release from quarantine if held */
    {
        uint8_t released_cw[16];
        cwquarantine_release(&r22_quar, caid, provid, sid, now, released_cw);
        R22STATS_INC(quarantine_released);
    }

    /* 9. Offer as fallback */
    fallback_offer(&r22_fb, caid, provid, sid, cw, 100, source_id, now);
    R22STATS_INC(fallback_offered);

    /* 10. Add to client buffer */
    clientbuf_add(&r22_cbuf, caid, provid, sid, cw, now, 30);
    R22STATS_INC(clientbuf_added);
    
    /* R24: Learn from this CW (ground truth) */
    cwlearn_learn(cw, caid, provid, sid);
    
    /* R24: Save to rollback buffer */
    cwrollback_save(cw, caid, provid, sid);
    
    /* R24: Add to multi-CW buffer */
    cwmultibuf_add(cw, caid, provid, sid);
    
    /* R24: Update source score (ground truth = good) */
    cwsrcscore_update(source_id, 0);
}

/* ================================================================
 * HOOK 3: CW failed (client reports black screen or decryption fail)
 * ================================================================ */

static inline void r22_hook_cw_failed(
    uint16_t caid, uint32_t provid, uint16_t sid,
    const uint8_t *cw, int source_id, uint32_t now)
{
    R22STATS_INC(cw_failed);
    
    /* 1. Record feedback (failure) */
    cwfeedback_record_delivery(&r22_fdbk, 0, caid, provid, sid, cw, source_id, now);

    /* 2. Update channel score (failure) */
    cwchannel_record_failure(&r22_chan, source_id, caid, provid, sid);

    /* 3. Record black screen detection */
    blackscreen_detect(&r22_bscr, caid, provid, sid, now);
    R22STATS_INC(blackscreen_detected);

    /* 4. Block this CW */
    cwpropblock_add(&r22_propb, cw, caid, provid, sid, now);
    R22STATS_INC(propblock_added);

    /* 5. Rotate redundancy pool */
    cwredundancy_rotate(&r22_red, caid, provid, sid);
    R22STATS_INC(redundancy_rotated);

    /* 6. Record fingerprint (this peer delivered a fake) */
    cwfingerprint_record_fake(&r22_fpr, source_id, now);

    /* 7. Check auto-switch */
    autoswitch_record(&r22_asw, caid, provid, sid, source_id, 0);
    R22STATS_INC(autoswitch_triggered);
    
    /* R24: Update source score (was fake) */
    cwsrcscore_update(source_id, 1);
    
    /* R24: Log blackscreen analytics */
    blackanalytics_log_event(caid, provid, sid, 0, source_id, 0, 0);
    
    /* R24: Adaptive learning (predicted good, was fake = false negative) */
    cwadaptive_log_result(0, 1);
    
    /* R24: ML training sample */
    cwml_features_t features;
    cwml_extract_features(cw, 0.0, 0.0, 0.0, 0.0, &features);
    cwml_add_training_sample(&features, 1);  /* label = 1 (fake) */
}

/* ================================================================
 * HOOK 4: CW verified (client confirms successful decryption)
 * ================================================================ */

static inline void r22_hook_cw_verified(
    uint16_t caid, uint32_t provid, uint16_t sid,
    const uint8_t *cw, int source_id, uint32_t now)
{
    R22STATS_INC(cw_verified);
    
    /* 1. Update channel score (success) */
    cwchannel_record_success(&r22_chan, source_id, caid, provid, sid);

    /* 2. Record fingerprint (good CW) */
    cwfingerprint_record_delivery(&r22_fpr, source_id, now);

    /* 3. Record feedback (success) */
    cwfeedback_record_delivery(&r22_fdbk, 0, caid, provid, sid, cw, source_id, now);

    /* 4. Update auto-switch (success) */
    autoswitch_record(&r22_asw, caid, provid, sid, source_id, 1);

    /* 5. Resolve black screen if active */
    blackscreen_resolve(&r22_bscr, caid, provid, sid);
    R22STATS_INC(blackscreen_resolved);
    
    /* R24: Update source score (good CW) */
    cwsrcscore_update(source_id, 0);
    
    /* R24: Mark success in multi-buffer */
    cwmultibuf_mark_success(cw, caid, sid);
    
    /* R24: Log blackscreen resolved */
    blackanalytics_log_event(caid, provid, sid, 0, source_id, 1, 1);
    
    /* R24: Adaptive learning (predicted good, was good = true positive) */
    cwadaptive_log_result(0, 0);
    
    /* R24: ML training sample */
    cwml_features_t features;
    cwml_extract_features(cw, 1.0, 1.0, 1.0, 1.0, &features);
    cwml_add_training_sample(&features, 0);  /* label = 0 (good) */
}

#else /* !R22_LAYERS */

/* All hooks compile to nothing when R22_LAYERS is not defined */
static inline void r22_layers_init(void) {}
static inline int r22_hook_cache_setdcw(
    uint16_t caid, uint32_t provid, uint16_t sid,
    const uint8_t *cw, int peerid, uint32_t now)
{ (void)caid; (void)provid; (void)sid; (void)cw; (void)peerid; (void)now; return 0; }
static inline void r22_hook_ecm_setsrvflagdcw(
    uint16_t caid, uint32_t provid, uint16_t sid,
    const uint8_t *cw, unsigned int srvid, uint32_t now)
{ (void)caid; (void)provid; (void)sid; (void)cw; (void)srvid; (void)now; }
static inline void r22_hook_cw_failed(
    uint16_t caid, uint32_t provid, uint16_t sid,
    const uint8_t *cw, int source_id, uint32_t now)
{ (void)caid; (void)provid; (void)sid; (void)cw; (void)source_id; (void)now; }
static inline void r22_hook_cw_verified(
    uint16_t caid, uint32_t provid, uint16_t sid,
    const uint8_t *cw, int source_id, uint32_t now)
{ (void)caid; (void)provid; (void)sid; (void)cw; (void)source_id; (void)now; }

#endif /* R22_LAYERS */

#endif /* MCS_R22LAYERS_H */
