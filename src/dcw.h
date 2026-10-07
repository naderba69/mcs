
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

#endif /* MCS_DCW_H */

