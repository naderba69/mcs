#ifndef MCS_NEWCAMD_REQUEST_H
#define MCS_NEWCAMD_REQUEST_H

#include <stdint.h>

/* A Newcamd reply may only consume the same live request generation that was
 * captured when the CW was committed. The pointer alone is reusable storage;
 * lifetime_id distinguishes a later request placed in that same slot. */
static inline int newcamd_delivery_identity_matches(
    const void *current_request, uint64_t current_lifetime_id,
    const void *delivery_request, uint64_t delivery_lifetime_id)
{
    return current_request && delivery_request && delivery_lifetime_id &&
           current_request == delivery_request &&
           current_lifetime_id == delivery_lifetime_id;
}

#endif /* MCS_NEWCAMD_REQUEST_H */
