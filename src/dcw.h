
#ifndef MCS_DCW_H
#define MCS_DCW_H

int checksumDCW(uint8_t *data);
int isnullDCW(uint8_t *data);
int isbadDCW(uint8_t *data);
int acceptDCW(uint8_t *data);

/*
 * TASK 3.15 — a profile may override the two global gates.
 * 0 inherits the global DCWFILTER. 1 forces the test on. 2 forces it
 * off. Null keys and the BAD-DCW list are not overridable.
 */
#define DCWFILTER_INHERIT 0
#define DCWFILTER_ON      1
#define DCWFILTER_OFF     2
int acceptDCW_for(uint8_t *data, int checksum_mode, int repeat_mode);

/*
 * TASK R26 (D77) — the CA-aware form. When ca_modern is armed (per
 * profile: `CA MODERN: ON`) and the CA system of `caid` does not
 * carry the DVB-CSA checksum bytes (casystem.h: AES/CSA3, BISS2,
 * constant-CW), the checksum test is skipped even in INHERIT mode.
 * An explicit DCWFILTER CHECKSUM: ON still forces the test, OFF
 * still skips it, and with ca_modern == 0 the decision is exactly
 * acceptDCW_for(). Null keys and the BAD-DCW list are never rescued.
 */
int acceptDCW_for_ca(uint8_t *data, uint16_t caid, int checksum_mode,
		     int repeat_mode, int ca_modern);

/*
 * TASK 3.14 — the documented BAD-DCW list.
 *
 * acceptDCW() does not see config.h. The server copies the committed
 * list into a fixed table (dcw_badlist_publish) and acceptDCW asks
 * dcw_on_badlist. An empty table is the default: the decision is the
 * one r82a made. More than DCW_BADLIST_MAX keys are not stored; the
 * server warns once.
 */
#define DCW_BADLIST_MAX 32
int dcw_on_badlist(uint8_t *data);
void dcw_badlist_publish(const uint8_t *keys, int n);

/* Drop what no profile is allowed to rescue: a null key, or one the
 * operator listed. Cache-ex pre-checks use this so a checksum exception
 * still reaches the profile gate. */
static inline int dcw_hard_reject(uint8_t *data)
{
	if (isnullDCW(data)) return 1;
	if (dcw_on_badlist(data)) return 1;
	return 0;
}

int similarcw( uint8_t *cw1, uint8_t *cw2 );
// for nds
int ishalfnulledcw( uint8_t dcw[16] );

/* Black Screen Prevention API */
void dcw_on_failure(uint16_t caid, uint16_t sid, uint32_t provid);
void dcw_on_success(uint16_t caid, uint16_t sid);
int dcw_needs_emergency(uint16_t caid, uint16_t sid);

#endif /* MCS_DCW_H */

