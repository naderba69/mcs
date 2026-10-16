/* cwpattern.h — CW Pattern Recognition (R22 candidate #11)
 *
 * PURPOSE: detect systematic patterns in fake CWs. Some liars use
 * predictable patterns (all zeros, sequential bytes, repeated values).
 * Build a pattern library of known fake patterns and reject CWs that
 * match.
 *
 * MECHANISM: maintain a library of known fake patterns (first 8 bytes
 * of CW). When a new CW arrives, check against the library.
 *
 * CONFIG: automatic — patterns are learned from proven fakes.
 *
 * ROLLBACK: remove cwpattern_check() calls; pattern matching is inert.
 */
#ifndef MCS_CWPATTERN_H
#define MCS_CWPATTERN_H

#include <stdint.h>
#include <string.h>

#define CWPATTERN_LIBRARY_SIZE 64
#define CWPATTERN_PREFIX_LEN 8

struct cwpattern_entry {
    uint8_t prefix[CWPATTERN_PREFIX_LEN];
    uint32_t seen_count;
    uint32_t first_seen;
    uint32_t last_seen;
    uint8_t used;
};

struct cwpattern_library {
    struct cwpattern_entry entries[CWPATTERN_LIBRARY_SIZE];
    uint32_t total_matches;
};

static inline void cwpattern_init(struct cwpattern_library *lib)
{
    if (!lib) return;
    memset(lib, 0, sizeof(*lib));
}

/* Check if a CW has an obvious fake pattern (no library needed) */
static inline int cwpattern_is_obvious_fake(const uint8_t cw[16])
{
    int i;
    int all_zero = 1, all_ff = 1, all_same = 1;
    uint8_t first = cw[0];

    if (!cw) return 0;

    for (i = 0; i < 16; i++) {
        if (cw[i] != 0x00) all_zero = 0;
        if (cw[i] != 0xFF) all_ff = 0;
        if (cw[i] != first) all_same = 0;
    }

    if (all_zero || all_ff || all_same) return 1;

    /* Check for sequential pattern: 00 01 02 03 ... */
    {
        int sequential = 1;
        for (i = 1; i < 16; i++) {
            if (cw[i] != (uint8_t)(cw[i-1] + 1)) {
                sequential = 0;
                break;
            }
        }
        if (sequential) return 1;
    }

    /* Check for repeated 4-byte pattern: AABBCCDD AABBCCDD ... */
    if (cw[0] == cw[4] && cw[1] == cw[5] && cw[2] == cw[6] && cw[3] == cw[7] &&
        cw[0] == cw[8] && cw[1] == cw[9] && cw[2] == cw[10] && cw[3] == cw[11] &&
        cw[0] == cw[12] && cw[1] == cw[13] && cw[2] == cw[14] && cw[3] == cw[15]) {
        return 1;
    }

    return 0;
}

/* Learn a new fake pattern (add to library) */
static inline void cwpattern_learn(struct cwpattern_library *lib,
                                    const uint8_t cw[16], uint32_t now)
{
    int i, free_slot = -1;
    struct cwpattern_entry *e;

    if (!lib || !cw) return;

    /* Check if pattern already in library */
    for (i = 0; i < CWPATTERN_LIBRARY_SIZE; i++) {
        e = &lib->entries[i];
        if (!e->used) {
            if (free_slot < 0) free_slot = i;
            continue;
        }
        if (memcmp(e->prefix, cw, CWPATTERN_PREFIX_LEN) == 0) {
            e->seen_count++;
            e->last_seen = now;
            return;
        }
    }

    /* Add new pattern */
    if (free_slot < 0) {
        /* Find least-seen pattern to replace */
        int min_seen = 0;
        for (i = 1; i < CWPATTERN_LIBRARY_SIZE; i++) {
            if (lib->entries[i].seen_count < lib->entries[min_seen].seen_count) {
                min_seen = i;
            }
        }
        free_slot = min_seen;
    }

    e = &lib->entries[free_slot];
    memcpy(e->prefix, cw, CWPATTERN_PREFIX_LEN);
    e->seen_count = 1;
    e->first_seen = now;
    e->last_seen = now;
    e->used = 1;
}

/* Check if a CW matches a known fake pattern */
static inline int cwpattern_check(const struct cwpattern_library *lib,
                                   const uint8_t cw[16])
{
    int i;
    if (!lib || !cw) return 0;

    /* Check obvious patterns first */
    if (cwpattern_is_obvious_fake(cw)) return 1;

    /* Check library */
    for (i = 0; i < CWPATTERN_LIBRARY_SIZE; i++) {
        const struct cwpattern_entry *e = &lib->entries[i];
        if (!e->used) continue;
        if (memcmp(e->prefix, cw, CWPATTERN_PREFIX_LEN) == 0) {
            ((struct cwpattern_library *)lib)->total_matches++;
            return 1;
        }
    }
    return 0;
}

#endif /* MCS_CWPATTERN_H */
