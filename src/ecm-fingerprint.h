/*
 * ecm-fingerprint.h -- R48 ECM fingerprint (observe-only).
 *
 * Learns the typical ECM length per (CAID,SID) and reports anomalies.
 * cs_accept_ecm() already validates ECM shape statically (tag, length
 * header, HEADERWLST, per-profile ECM LENGTH); what it cannot do is
 * notice that a channel suddenly sends a length it never sent before
 * (corrupt ECM, wrong stream, provider change). This table does that:
 * after ECMFP_ESTABLISH_SAMPLES sightings, a length outside the
 * established [min,max] is an anomaly.
 *
 * Observe-only by design: ecmfp_note() never refuses; the caller traces
 * the anomaly in /user-trace. A novel length is evidence, not a verdict.
 * Bounded (ECMFP_SLOTS channels); a full table stops learning new
 * channels (fail-open). Header-only like cwcycle.h/dcwstruct.h; the
 * single production includer is main.c (cs_accept_ecm), so the static
 * table has one instance. cs_accept_ecm runs before lockecm on several
 * server threads, hence the leaf mutex (nothing else taken while held).
 */
#ifndef ECM_FINGERPRINT_H
#define ECM_FINGERPRINT_H

#include <pthread.h>
#include <stdint.h>
#include <string.h>

#define ECMFP_SLOTS 128
#define ECMFP_ESTABLISH_SAMPLES 5

struct ecmfp_entry {
	uint16_t caid;
	uint16_t sid;
	uint16_t minlen;
	uint16_t maxlen;
	uint16_t count;
	uint8_t used;
};

static struct ecmfp_entry ecmfp_tab[ECMFP_SLOTS];
static pthread_mutex_t ecmfp_lock = PTHREAD_MUTEX_INITIALIZER;

static int ecmfp_note(uint16_t caid, uint16_t sid, uint16_t ecmlen)
{
	int i, anomaly = 0;
	if (!ecmlen) return 0;
	pthread_mutex_lock(&ecmfp_lock);
	for (i = 0; i < ECMFP_SLOTS; i++) {
		if (!ecmfp_tab[i].used) {
			ecmfp_tab[i].used = 1;
			ecmfp_tab[i].caid = caid;
			ecmfp_tab[i].sid = sid;
			ecmfp_tab[i].minlen = ecmlen;
			ecmfp_tab[i].maxlen = ecmlen;
			ecmfp_tab[i].count = 1;
			break;
		}
		if (ecmfp_tab[i].caid == caid && ecmfp_tab[i].sid == sid) {
			if (ecmfp_tab[i].count >= ECMFP_ESTABLISH_SAMPLES &&
			    (ecmlen < ecmfp_tab[i].minlen || ecmlen > ecmfp_tab[i].maxlen))
				anomaly = 1;
			if (ecmlen < ecmfp_tab[i].minlen) ecmfp_tab[i].minlen = ecmlen;
			if (ecmlen > ecmfp_tab[i].maxlen) ecmfp_tab[i].maxlen = ecmlen;
			/* Saturate at 65535: a uint16_t wrap would drop count
			 * below the establish threshold and pause detection. */
			if (ecmfp_tab[i].count < 65535) ecmfp_tab[i].count++;
			break;
		}
	}
	pthread_mutex_unlock(&ecmfp_lock);
	return anomaly;
}

static void ecmfp_reset(void) /* test-only */
{
	pthread_mutex_lock(&ecmfp_lock);
	memset(ecmfp_tab, 0, sizeof(ecmfp_tab));
	pthread_mutex_unlock(&ecmfp_lock);
}

#endif
