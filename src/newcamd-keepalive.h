#ifndef MCS_NEWCAMD_KEEPALIVE_H
#define MCS_NEWCAMD_KEEPALIVE_H

#include <stdint.h>
#include "tick_math.h"

#define NEWCAMD_KEEPALIVE_REPLY_TIMEOUT_MS 10000u

/* `keepalive_time` is the last keepalive/activity tick, never the boolean
 * status field. Unsigned elapsed arithmetic remains valid across tick wrap. */
static inline int newcamd_keepalive_reply_timed_out(uint32_t now,
                                                     uint32_t keepalive_time)
{
    return tick32_elapsed(now, keepalive_time) >
           NEWCAMD_KEEPALIVE_REPLY_TIMEOUT_MS;
}

#endif /* MCS_NEWCAMD_KEEPALIVE_H */
