/*
 * cw-fingerprint.h -- R48 CW fingerprint (observe-only).
 *
 * Learns the per-byte-position value range of genuine control words
 * per (CAID,PROVID,SID): the min and max byte seen at each of the 16
 * positions. A forged CW from a biased generator -- limited byte
 * range, a generator that does not know the channel's key schedule --
 * keeps a position's range narrow even after many sightings, while a
 * genuine uniform generator spreads each position across [0,255].
 *
 * Honest limitation: a uniform-random fake, or a genuine CW with one
 * byte XOR-flipped (the r107 XOR 0xF0 fake), is statistically
 * indistinguishable from a genuine CW by any range test -- XOR is a
 * bijection, so the range is preserved. Those fakes are caught by
 * consensus (cwconsensus: compare with independent sources) and by
 * ECM<->CW binding (lifetime_id), not by this table. What this table
 * catches is the class of fakes whose generator is biased: cheap fake
 * servers that emit bytes from a small set, or replayed CWs from
 * another channel whose byte profile differs.
 *
 * Observe-only by design: cwfp_note() never refuses; the caller traces
 * the anomaly in /user-trace and bumps a counter. A range deviation is
 * evidence, not a verdict. The test runs after the channel is
 * established (CWFP_ESTABLISH_SAMPLES sightings): a channel whose any
 * position has never exceeded CWFP_RANGE_CEILING is flagged as a
 * biased generator, and its CWs are reported anomalous.
 *
 * Bounded (CWFP_SLOTS channels); a full table stops learning new
 * channels (fail-open). Header-only like ecm-fingerprint.h; the
 * single production includer is setdcw.c (live THREAD_DCW path), so
 * the static table has one instance. The ECM worker runs on several
 * threads, hence the leaf mutex (nothing else taken while held).
 */
#ifndef CW_FINGERPRINT_H
#define CW_FINGERPRINT_H

#include <pthread.h>
#include <stdint.h>
#include <string.h>

#define CWFP_SLOTS 128
#define CWFP_ESTABLISH_SAMPLES 100
/* A genuine uniform generator exceeds this byte value at every
 * position within ~100 sightings with overwhelming probability
 * (P(max < 128 after 100 uniform draws) = (1/2)^100). A position
 * that never exceeds it is a biased generator. */
#define CWFP_RANGE_CEILING 128

struct cwfp_entry {
	uint16_t caid;
	uint32_t provid;
	uint16_t sid;
	uint32_t count;
	uint8_t used;
	/* Per-position observed maximum byte value. */
	uint8_t posmax[16];
};

static struct cwfp_entry cwfp_tab[CWFP_SLOTS];
static pthread_mutex_t cwfp_lock = PTHREAD_MUTEX_INITIALIZER;

/*
 * Record one CW sighting for (caid,provid,sid). Returns 1 if the
 * channel's generator is biased (any position never exceeded the
 * range ceiling after establishment), 0 if the channel looks uniform
 * or is not yet established. Never refuses.
 */
static int cwfp_note(uint16_t caid, uint32_t provid, uint16_t sid,
		     const uint8_t cw[16])
{
	int i, anomaly = 0, pos;
	pthread_mutex_lock(&cwfp_lock);
	for (i = 0; i < CWFP_SLOTS; i++) {
		if (!cwfp_tab[i].used) {
			cwfp_tab[i].used = 1;
			cwfp_tab[i].caid = caid;
			cwfp_tab[i].provid = provid;
			cwfp_tab[i].sid = sid;
			cwfp_tab[i].count = 0;
			memset(cwfp_tab[i].posmax, 0, sizeof(cwfp_tab[i].posmax));
			break;
		}
		if (cwfp_tab[i].caid == caid && cwfp_tab[i].provid == provid &&
		    cwfp_tab[i].sid == sid)
			break;
	}
	if (i >= CWFP_SLOTS) {
		/* Table full: fail-open, do not learn. */
		pthread_mutex_unlock(&cwfp_lock);
		return 0;
	}
	{
		struct cwfp_entry *e = &cwfp_tab[i];
		for (pos = 0; pos < 16; pos++)
			if (cw[pos] > e->posmax[pos])
				e->posmax[pos] = cw[pos];
		e->count++;
		/* Biased-generator verdict: after establishment, any
		 * position that never exceeded the ceiling means the
		 * generator does not produce high bytes there. */
		if (e->count >= CWFP_ESTABLISH_SAMPLES) {
			for (pos = 0; pos < 16; pos++) {
				if (e->posmax[pos] < CWFP_RANGE_CEILING) {
					anomaly = 1;
					break;
				}
			}
		}
	}
	pthread_mutex_unlock(&cwfp_lock);
	return anomaly;
}

static void cwfp_reset(void)
{
	pthread_mutex_lock(&cwfp_lock);
	memset(cwfp_tab, 0, sizeof(cwfp_tab));
	pthread_mutex_unlock(&cwfp_lock);
}

#endif /* CW_FINGERPRINT_H */
