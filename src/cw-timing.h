/*
 * cw-timing.h -- R48 CW timing anomaly detector (observe-only).
 *
 * A genuine card needs time to decrypt: ECM dispatch to CW return is
 * network RTT + card processing (~50-500ms typical). A fake server
 * with no card returns in network RTT only (~5-50ms). A CW that
 * arrives implausibly fast from a SERVER source (not cache, which is
 * legitimately fast) is a timing anomaly.
 *
 * This is observe-only: cwtime_anomaly_note() records the delta and returns
 * 1 if it is below the anomaly threshold. The caller traces it. A
 * fast CW is evidence, not a verdict -- a fast network or a
 * pre-computing server can also be fast. The value is in the
 * aggregate: a peer that is consistently too fast is a fake server.
 *
 * Header-only like cw-fingerprint.h; the single production includer
 * is setdcw.c (live THREAD_DCW path). Leaf mutex, nothing else taken
 * while held.
 */
#ifndef CW_TIMING_H
#define CW_TIMING_H

#include <pthread.h>
#include <stdint.h>
#include <string.h>

#define CWTIME_SLOTS 256
/* Below this many ms from dispatch to CW return, a SERVER-sourced
 * CW is a timing anomaly. Cache sources are exempt (legitimately
 * fast). Tuned conservative: genuine cards rarely respond under
 * 30ms even on LAN. */
#define CWTIME_ANOMALY_MS 30

struct cwtime_entry {
	uint32_t srcid;
	uint32_t fast_count;
	uint32_t total_count;
	uint8_t used;
};

static struct cwtime_entry cwtime_tab[CWTIME_SLOTS];
static pthread_mutex_t cwtime_lock = PTHREAD_MUTEX_INITIALIZER;

/*
 * Record one server-sourced CW timing. delta_ms is the elapsed time
 * from ECM dispatch to CW processing. Returns 1 if delta_ms is below
 * the anomaly threshold. Never refuses.
 */
static int cwtime_anomaly_note(uint32_t srcid, uint32_t delta_ms)
{
	int i, anomaly = 0;
	pthread_mutex_lock(&cwtime_lock);
	for (i = 0; i < CWTIME_SLOTS; i++) {
		if (!cwtime_tab[i].used) {
			cwtime_tab[i].used = 1;
			cwtime_tab[i].srcid = srcid;
			cwtime_tab[i].fast_count = 0;
			cwtime_tab[i].total_count = 0;
			break;
		}
		if (cwtime_tab[i].srcid == srcid)
			break;
	}
	if (i < CWTIME_SLOTS) {
		cwtime_tab[i].total_count++;
		if (delta_ms < CWTIME_ANOMALY_MS) {
			cwtime_tab[i].fast_count++;
			anomaly = 1;
		}
	}
	pthread_mutex_unlock(&cwtime_lock);
	return anomaly;
}

static void cwtime_reset(void)
{
	pthread_mutex_lock(&cwtime_lock);
	memset(cwtime_tab, 0, sizeof(cwtime_tab));
	pthread_mutex_unlock(&cwtime_lock);
}

#endif /* CW_TIMING_H */
