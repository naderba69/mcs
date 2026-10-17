#ifndef MCS_DCWTIMING_H
#define MCS_DCWTIMING_H

#include <stdint.h>

/*
 * dcwtiming.h -- two per-profile timing options, pure so the unit test
 * can drive them without the server.
 *
 *   DCW VALIDECMTIME: <ms>   (default 0 = off, stock behaviour)
 *       A control word that reaches the client more than <ms> after the
 *       ECM was first received is refused. The request then stays open for
 *       a source that answers in time, instead of a late key being shown.
 *
 *   ENABLE ECMLOOPING: YES   (default NO = off, stock behaviour)
 *       When every server asked for an ECM has answered without a key and
 *       no server is left to ask, the ECM is sent again to the same
 *       servers. At most ECMLOOP_MAX extra rounds per ECM, and only while
 *       the server window is still open (the caller checks that).
 */

#define ECMLOOP_MAX 2

/* 1 when a control word arriving at `now` is too late for `limit_ms`.
 * limit_ms <= 0 means the option is off and nothing is late. */
static int dcw_arrives_too_late(uint32_t now, uint32_t recvtime, int limit_ms)
{
	if (limit_ms <= 0) return 0;
	if (now < recvtime) return 0;          /* clock step: never refuse */
	return (int)(now - recvtime) > limit_ms;
}

/* 1 when the ECM may be sent again to the same servers.
 * totalwait is the number of servers still awaiting an answer. */
static int ecmloop_may_retry(int enabled, int loops, int sent, int totalwait)
{
	if (!enabled) return 0;
	if (sent <= 0) return 0;               /* nobody was asked (e.g. no server connected yet): not a no-key answer */
	if (totalwait > 0) return 0;           /* someone may still answer */
	return loops < ECMLOOP_MAX;
}

#endif /* MCS_DCWTIMING_H */
