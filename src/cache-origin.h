/*
 * cache-origin.h — R48/D111 local provenance tags for cache candidates.
 *
 * These bits are private process/cache-pipe metadata. They are not serialized
 * into CSP, CacheEX, or any other peer protocol. Multiple contributors to the
 * same CW accumulate instead of overwriting one another. The three classes
 * distinguish an accepted local line-server answer, a CSP cache peer, and a
 * CacheEX push ingress (batch); re-pushes retain the accumulated local mask.
 */
#ifndef MCS_CACHE_ORIGIN_H
#define MCS_CACHE_ORIGIN_H

#include <stdint.h>

#define CACHE_ORIGIN_LINE_SERVER 0x01u
#define CACHE_ORIGIN_PEER        0x02u
#define CACHE_ORIGIN_BATCH       0x04u
#define CACHE_ORIGIN_MASK        (CACHE_ORIGIN_LINE_SERVER | \
                                  CACHE_ORIGIN_PEER | \
                                  CACHE_ORIGIN_BATCH)

static inline uint8_t cache_origin_merge(uint8_t existing, uint8_t incoming)
{
    return (uint8_t)((existing | incoming) & CACHE_ORIGIN_MASK);
}

static inline int cache_origin_mask_valid(uint8_t mask)
{
    return (mask & (uint8_t)~CACHE_ORIGIN_MASK) == 0;
}

#endif /* MCS_CACHE_ORIGIN_H */
