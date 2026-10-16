/* r22stats.h — R22 Layer Statistics (R23 monitoring)
 *
 * PURPOSE: track counters for every R22 layer action so the operator
 * can see what the layers are doing in production.
 *
 * MECHANISM: global counters, incremented by the hooks in r22layers.h.
 * Read-only from telnet/HTTP interfaces.
 *
 * CONFIG: always active when R22_LAYERS is defined (zero overhead when off).
 *
 * ROLLBACK: remove r22stats_inc() calls from r22layers.h.
 */
#ifndef MCS_R22STATS_H
#define MCS_R22STATS_H

#include <stdint.h>
#include <string.h>

#ifdef R22_LAYERS

struct r22stats {
    /* cache_setdcw hook counters */
    uint32_t hook_cache_total;       /* total CWs processed */
    uint32_t hook_cache_rejected;    /* CWs rejected (fake/blocked) */
    uint32_t hook_cache_accepted;    /* CWs accepted */

    /* Layer-specific counters */
    uint32_t pattern_obvious_fake;   /* cwpattern: obvious fakes detected */
    uint32_t pattern_learned;        /* cwpattern: patterns learned */
    uint32_t crosschannel_blocked;   /* cwcrosschannel: >3 channels blocked */
    uint32_t propblock_hit;          /* cwpropblock: blocked CWs caught */
    uint32_t propblock_added;        /* cwpropblock: CWs added to block list */
    uint32_t fingerprint_liar;       /* cwfingerprint: known liars detected */
    uint32_t quarantine_held;        /* cwquarantine: CWs held */
    uint32_t quarantine_released;    /* cwquarantine: CWs released */
    uint32_t redundancy_stored;      /* cwredundancy: CWs stored */
    uint32_t redundancy_rotated;     /* cwredundancy: rotations on failure */
    uint32_t priority_offered;       /* cwpriority: CWs offered to queue */
    uint32_t validity_recorded;      /* cwvalidity: changes recorded */
    uint32_t zerolatency_recorded;   /* zerolatency: changes recorded */
    uint32_t predict_recorded;       /* cwpredict: changes recorded */

    /* ecm_setsrvflagdcw hook counters */
    uint32_t hook_server_total;      /* total server CWs processed */
    uint32_t channel_success;        /* cwchannel: successes recorded */
    uint32_t feedback_delivery;      /* cwfeedback: deliveries recorded */
    uint32_t fallback_offered;       /* fallback: CWs offered */
    uint32_t clientbuf_added;        /* clientbuf: CWs buffered */

    /* Failure/verification counters (future use) */
    uint32_t cw_failed;              /* hook_cw_failed calls */
    uint32_t cw_verified;            /* hook_cw_verified calls */
    uint32_t blackscreen_detected;   /* blackscreen: detections */
    uint32_t blackscreen_resolved;   /* blackscreen: resolutions */
    uint32_t autoswitch_triggered;   /* autoswitch: switches */
};

static struct r22stats g_r22stats;

static inline void r22stats_init(void)
{
    memset(&g_r22stats, 0, sizeof(g_r22stats));
}

static inline void r22stats_inc(uint32_t *counter)
{
    if (counter) (*counter)++;
}

/* Read-only access for telnet/HTTP */
static inline const struct r22stats *r22stats_get(void)
{
    return &g_r22stats;
}

/* Format stats as text (for telnet) */
static inline int r22stats_format(char *buf, int bufsz)
{
    const struct r22stats *s = &g_r22stats;
    int n = 0;

    n += snprintf(buf + n, bufsz - n,
        "=== R22 Layer Statistics ===\n"
        "cache_setdcw hook:\n"
        "  total:     %u\n"
        "  rejected:  %u\n"
        "  accepted:  %u\n"
        "\n"
        "Layer actions:\n"
        "  pattern obvious fake:  %u\n"
        "  pattern learned:       %u\n"
        "  crosschannel blocked:  %u\n"
        "  propblock hit:         %u\n"
        "  propblock added:       %u\n"
        "  fingerprint liar:      %u\n"
        "  quarantine held:       %u\n"
        "  quarantine released:   %u\n"
        "  redundancy stored:     %u\n"
        "  redundancy rotated:    %u\n"
        "  priority offered:      %u\n"
        "  validity recorded:     %u\n"
        "  zerolatency recorded:  %u\n"
        "  predict recorded:      %u\n"
        "\n"
        "ecm_setsrvflagdcw hook:\n"
        "  total:     %u\n"
        "  channel success:       %u\n"
        "  feedback delivery:     %u\n"
        "  fallback offered:      %u\n"
        "  clientbuf added:       %u\n"
        "\n"
        "Failure/verification:\n"
        "  cw failed:             %u\n"
        "  cw verified:           %u\n"
        "  blackscreen detected:  %u\n"
        "  blackscreen resolved:  %u\n"
        "  autoswitch triggered:  %u\n",
        s->hook_cache_total,
        s->hook_cache_rejected,
        s->hook_cache_accepted,
        s->pattern_obvious_fake,
        s->pattern_learned,
        s->crosschannel_blocked,
        s->propblock_hit,
        s->propblock_added,
        s->fingerprint_liar,
        s->quarantine_held,
        s->quarantine_released,
        s->redundancy_stored,
        s->redundancy_rotated,
        s->priority_offered,
        s->validity_recorded,
        s->zerolatency_recorded,
        s->predict_recorded,
        s->hook_server_total,
        s->channel_success,
        s->feedback_delivery,
        s->fallback_offered,
        s->clientbuf_added,
        s->cw_failed,
        s->cw_verified,
        s->blackscreen_detected,
        s->blackscreen_resolved,
        s->autoswitch_triggered
    );

    return n;
}

/* Format stats as JSON (for HTTP) */
static inline int r22stats_json(char *buf, int bufsz)
{
    const struct r22stats *s = &g_r22stats;
    int n = 0;

    n += snprintf(buf + n, bufsz - n,
        "{"
        "\"hook_cache_total\":%u,"
        "\"hook_cache_rejected\":%u,"
        "\"hook_cache_accepted\":%u,"
        "\"pattern_obvious_fake\":%u,"
        "\"pattern_learned\":%u,"
        "\"crosschannel_blocked\":%u,"
        "\"propblock_hit\":%u,"
        "\"propblock_added\":%u,"
        "\"fingerprint_liar\":%u,"
        "\"quarantine_held\":%u,"
        "\"quarantine_released\":%u,"
        "\"redundancy_stored\":%u,"
        "\"redundancy_rotated\":%u,"
        "\"priority_offered\":%u,"
        "\"validity_recorded\":%u,"
        "\"zerolatency_recorded\":%u,"
        "\"predict_recorded\":%u,"
        "\"hook_server_total\":%u,"
        "\"channel_success\":%u,"
        "\"feedback_delivery\":%u,"
        "\"fallback_offered\":%u,"
        "\"clientbuf_added\":%u,"
        "\"cw_failed\":%u,"
        "\"cw_verified\":%u,"
        "\"blackscreen_detected\":%u,"
        "\"blackscreen_resolved\":%u,"
        "\"autoswitch_triggered\":%u"
        "}",
        s->hook_cache_total,
        s->hook_cache_rejected,
        s->hook_cache_accepted,
        s->pattern_obvious_fake,
        s->pattern_learned,
        s->crosschannel_blocked,
        s->propblock_hit,
        s->propblock_added,
        s->fingerprint_liar,
        s->quarantine_held,
        s->quarantine_released,
        s->redundancy_stored,
        s->redundancy_rotated,
        s->priority_offered,
        s->validity_recorded,
        s->zerolatency_recorded,
        s->predict_recorded,
        s->hook_server_total,
        s->channel_success,
        s->feedback_delivery,
        s->fallback_offered,
        s->clientbuf_added,
        s->cw_failed,
        s->cw_verified,
        s->blackscreen_detected,
        s->blackscreen_resolved,
        s->autoswitch_triggered
    );

    return n;
}

#define R22STATS_INC(field) r22stats_inc(&g_r22stats.field)

#else /* !R22_LAYERS */

/* No-op when R22_LAYERS is not defined */
static inline void r22stats_init(void) {}
#define R22STATS_INC(field) ((void)0)
static inline int r22stats_format(char *buf, int bufsz)
{ (void)buf; (void)bufsz; return 0; }
static inline int r22stats_json(char *buf, int bufsz)
{ (void)buf; (void)bufsz; return 0; }

#endif /* R22_LAYERS */

#endif /* MCS_R22STATS_H */
